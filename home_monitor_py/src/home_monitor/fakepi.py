"""A fake Raspberry Pi for testing, with a browser control panel.

What is real and what is fake:
  * REAL: watch_switch() (callbacks, thread hand-off, initial state, de-duplication),
    read_ds18b20() / parse_w1_slave() reading real files, the alert engine, SQLite, logging.
  * FAKE: the gpiozero Button (FakeButton below) and the kernel's 1-wire files, which live in
    a temp directory in the exact w1_slave format, so you can also edit them by hand:
        echo -e "aa : crc=aa YES\\naa t=-5000" > <fake_w1_root>/<sensor id>/w1_slave

Control panel: http://127.0.0.1:8099 (no authentication, so it binds to localhost by default).
"""
from __future__ import annotations

import asyncio
import json
import logging
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from .config import Config

log = logging.getLogger(__name__)

W1_FORMAT = "50 05 4b 46 7f ff 0c 10 1c : crc=1c {ok}\n50 05 4b 46 7f ff 0c 10 1c t={milli}\n"
MODES = ("ok", "crc", "unplugged")
# Door movements as (up_switch_closed, down_switch_closed) steps. Every move starts with
# "neither" (the door leaves its end stop), like the real door.
DOOR_MOVES = {
    "open": [(False, False), (True, False)],
    "close": [(False, False), (False, True)],
    "fail_close": [(False, False), (True, False)],  # starts closing, then reverses back up
}


class FakeButton:
    """Looks like gpiozero.Button to watch_switch(); the control panel drives it."""

    def __init__(self, pin: int, pull_up: bool = True, bounce_time: float | None = None) -> None:
        self.pin, self.pull_up, self.bounce_time = pin, pull_up, bounce_time
        self.is_pressed = True  # "pressed" == closed == pin pulled low; start closed
        self.when_pressed = None
        self.when_released = None
        self.closed = False
        self._last_change = float("-inf")
        self._lock = threading.Lock()

    def drive(self, pressed: bool) -> bool:
        """Change the pin level. Returns False if nothing happened (no change, or debounced)."""
        with self._lock:
            now = time.monotonic()
            if pressed == self.is_pressed:
                return False
            if self.bounce_time and now - self._last_change < self.bounce_time:
                return False  # like gpiozero: edges inside bounce_time are ignored
            self.is_pressed, self._last_change = pressed, now
        callback = self.when_pressed if pressed else self.when_released
        if callback:
            callback()  # called from this (HTTP) thread, as gpiozero calls from its own thread
        return True

    def close(self) -> None:
        self.closed = True


class FakePi:
    def __init__(self, cfg: Config) -> None:
        self.w1_root = Path(tempfile.mkdtemp(prefix="fakepi_w1_"))
        self.sensors = {d["name"]: {"id": d["id"], "value": -15.0, "mode": "ok"} for d in cfg.ds18b20}
        self.pins = {s["name"]: int(s["pin"]) for s in cfg.switches}
        self.buttons: dict[int, FakeButton] = {}
        self._lock = threading.Lock()
        self._move_lock = threading.Lock()
        # Start in a realistic position: garage door down (up switch open, down switch closed),
        # washing machine lid down (lid_up switch open). Other switches start closed.
        by_name = {s["name"]: int(s["pin"]) for s in cfg.switches}
        self.garage = {d["name"]: (d["up"], d["down"]) for d in cfg.devices if d["type"] == "garage_door"}
        self._start_open = {by_name[up] for up, _ in self.garage.values()}
        self._start_open |= {by_name[d["lid_up"]] for d in cfg.devices if d["type"] == "washing_machine"}
        for name in self.sensors:
            self._write(name)

    # --- hardware seams used by the Monitor ---------------------------------
    def button_factory(self, pin: int, pull_up: bool = True, bounce_time: float | None = None) -> FakeButton:
        button = FakeButton(pin, pull_up, bounce_time)
        button.is_pressed = pin not in self._start_open
        self.buttons[pin] = button
        return button

    def _write(self, name: str) -> None:
        s = self.sensors[name]
        folder = self.w1_root / s["id"]
        target = folder / "w1_slave"
        if s["mode"] == "unplugged":
            target.unlink(missing_ok=True)
            return
        folder.mkdir(parents=True, exist_ok=True)
        tmp = folder / "w1_slave.tmp"
        tmp.write_text(W1_FORMAT.format(ok="NO" if s["mode"] == "crc" else "YES",
                                        milli=round(s["value"] * 1000)))
        tmp.replace(target)  # atomic, so a read never sees half a file

    # --- controls ------------------------------------------------------------
    def set_temp(self, name: str, value: float | None = None, mode: str | None = None) -> None:
        with self._lock:
            if name not in self.sensors:
                raise KeyError(f"unknown sensor {name!r}")
            if mode is not None:
                if mode not in MODES:
                    raise ValueError(f"mode must be one of {MODES}")
                self.sensors[name]["mode"] = mode
            if value is not None:
                value = float(value)
                if not -60.0 <= value <= 130.0:
                    raise ValueError("value out of range")
                self.sensors[name]["value"] = value
            self._write(name)

    def _button(self, name: str) -> FakeButton:
        if name not in self.pins:
            raise KeyError(f"unknown switch {name!r}")
        button = self.buttons.get(self.pins[name])
        if button is None:
            raise RuntimeError(f"{name} is not started yet")
        return button

    def set_switch(self, name: str, closed: bool) -> None:
        self._button(name).drive(closed)

    def bounce(self, name: str) -> None:
        """Toggle the switch like a noisy contact: 5 edges 2 ms apart, ending in the new state."""
        button = self._button(name)
        target = not button.is_pressed
        for level in (target, not target, target, not target, target):
            button.drive(level)
            time.sleep(0.002)

    def move_door(self, name: str, action: str, step_s: float = 1.5) -> None:
        """Run a garage door movement in the background. ``step_s`` is real time between steps and
        should exceed the device's settle_s, or the in-between 'neither' state is never published."""
        if name not in self.garage:
            raise KeyError(f"unknown garage door {name!r}")
        if action not in DOOR_MOVES:
            raise ValueError(f"action must be one of {sorted(DOOR_MOVES)}")
        up, down = self._button(self.garage[name][0]), self._button(self.garage[name][1])
        if not self._move_lock.acquire(blocking=False):
            raise RuntimeError("the door is still moving")

        def run() -> None:
            try:
                steps = DOOR_MOVES[action]
                for i, (up_closed, down_closed) in enumerate(steps):
                    up.drive(up_closed)
                    down.drive(down_closed)
                    if i < len(steps) - 1:
                        time.sleep(step_s)
            finally:
                self._move_lock.release()

        threading.Thread(target=run, name="fakepi-door", daemon=True).start()

    def switch_state(self, name: str) -> str:
        button = self.buttons.get(self.pins[name])
        return "open" if button is not None and not button.is_pressed else "closed"


# --- control panel ---------------------------------------------------------
def _state(fake: FakePi, monitor) -> dict:
    now = time.time()
    return {
        "time_scale": monitor.time_scale,
        "notify_label": monitor.notify_label,
        "w1_root": str(fake.w1_root),
        "sensors": {n: dict(s) for n, s in fake.sensors.items()},
        "switches": {n: {"pin": p, "state": fake.switch_state(n)} for n, p in fake.pins.items()},
        "devices": {d.name: {"kind": d.kind, "state": d.published, "inputs": d.inputs} for d in monitor.devices},
        "alerts": monitor.engine.snapshot(monitor.clock()),
        "events": [{"time": time.strftime("%H:%M:%S", time.localtime(ts)), "rule": rule, "old": old,
                    "new": new, "repeat": rep, "notified": bool(notified), "muted": bool(muted),
                    "detail": detail}
                   for ts, rule, old, new, rep, notified, muted, detail
                   in monitor.store.recent_events(now - 86400, 15)],
    }


def start_control_server(fake: FakePi, monitor, host: str = "127.0.0.1", port: int = 8099) -> ThreadingHTTPServer:
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):  # keep the console for the monitor's own log
            log.debug("control panel: " + fmt, *args)

        def _send(self, code: int, body: bytes, ctype: str) -> None:
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def _json(self, code: int, obj) -> None:
            self._send(code, json.dumps(obj).encode(), "application/json")

        def do_GET(self) -> None:  # noqa: N802
            if self.path == "/":
                self._send(200, PAGE.encode(), "text/html; charset=utf-8")
            elif self.path == "/api/state":
                self._json(200, _state(fake, monitor))
            else:
                self._json(404, {"error": "not found"})

        def do_POST(self) -> None:  # noqa: N802
            try:
                length = int(self.headers.get("Content-Length", 0))
                body = json.loads(self.rfile.read(length) or b"{}")
                if self.path == "/api/temp":
                    fake.set_temp(body["name"], body.get("value"), body.get("mode"))
                elif self.path == "/api/switch":
                    fake.set_switch(body["name"], body["state"] == "closed")
                elif self.path == "/api/bounce":
                    fake.bounce(body["name"])
                elif self.path in ("/api/test-email", "/api/test-report"):
                    if monitor.loop is None:
                        raise RuntimeError("the monitor is not running yet")
                    coro = monitor.send_test_message() if self.path == "/api/test-email" else monitor.send_test_report()
                    message = asyncio.run_coroutine_threadsafe(coro, monitor.loop).result(timeout=45)
                    return self._json(200, {"ok": True, "message": message})
                elif self.path == "/api/command":
                    if monitor.loop is None:
                        raise RuntimeError("the monitor is not running yet")
                    from .commands import CommandError

                    async def run():
                        try:
                            return await monitor.run_command(body["name"], body.get("args", []))
                        except CommandError as exc:
                            return f"not run: {exc}"
                    return self._json(200, {"ok": True, "message": asyncio.run_coroutine_threadsafe(run(), monitor.loop).result(timeout=10)})
                elif self.path == "/api/garage":
                    fake.move_door(body["name"], body["action"], min(max(float(body.get("step_s", 1.5)), 0.05), 10))
                else:
                    return self._json(404, {"error": "not found"})
                self._json(200, {"ok": True})
            except (KeyError, ValueError, RuntimeError, TimeoutError, json.JSONDecodeError) as exc:
                self._json(400, {"error": str(exc)})

    server = ThreadingHTTPServer((host, port), Handler)
    threading.Thread(target=server.serve_forever, name="fakepi-panel", daemon=True).start()
    return server


PAGE = """<!doctype html>
<meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Fake Pi</title>
<style>
:root{color-scheme:light dark;font-family:system-ui,sans-serif}
body{max-width:62rem;margin:1rem auto;padding:0 1rem}
h1{font-size:1.3rem} h2{font-size:1.05rem;margin:1.5rem 0 .3rem}
.row{display:flex;gap:.5rem;align-items:center;flex-wrap:wrap;padding:.4rem 0;border-bottom:1px solid #8884}
.name{min-width:11rem;font-weight:600}
button,select,input{font:inherit;padding:.3rem .6rem}
input[type=number]{width:6rem}
.pill{padding:.1rem .7rem;border-radius:1rem;font-size:.85rem;border:1px solid #8886;min-width:5rem;text-align:center}
.ok,.closed{background:#2a72}.pending,.watch{background:#cc02}.warn,.open{background:#f902}.error{background:#e222}
table{border-collapse:collapse;width:100%}td,th{text-align:left;padding:.25rem .5rem;border-bottom:1px solid #8883}
small{opacity:.7}
</style>
<h1>Fake Pi <small id="meta"></small></h1>
<h2>Temperature sensors (DS18B20)</h2><div id="sensors"></div>
<h2>Switches (GPIO)</h2><div id="switches"></div>
<h2>Devices</h2><div id="devices"></div>
<h2>Notifications</h2>
<div class="row"><span id="notify-mode"></span>
<button id="btn-test-email">Send test message</button><button id="btn-test-report">Send daily report now</button>
<button id="btn-status">Status</button><button id="btn-unmute">Unmute all</button></div>
<h2>Alert rules</h2><table id="alerts"></table>
<h2>Recent alert events</h2><table id="events"></table>
<script>
const refs = {sensors:{}, switches:{}, devices:{}};
let built = false;
function el(tag, props = {}, ...kids) {
  const e = Object.assign(document.createElement(tag), props);
  kids.forEach(k => e.append(k));
  return e;
}
async function api(path, body) {
  const r = await fetch(path, {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(body)});
  const j = await r.json();
  if (!r.ok) alert(j.error || r.status);
  else if (j.message) alert(j.message);
  refresh();
}
function build(s) {
  for (const name of Object.keys(s.sensors)) {
    const pill = el('span', {className:'pill'});
    const input = el('input', {type:'number', step:'0.1', value: s.sensors[name].value});
    const mode = el('select', {}, ...[['ok','Normal'],['crc','CRC error'],['unplugged','Unplugged']]
        .map(([v,t]) => el('option', {value:v, textContent:t})));
    mode.onchange = () => api('/api/temp', {name, mode: mode.value});
    const set = v => api('/api/temp', {name, value: v});
    refs.sensors[name] = {pill, mode};
    document.getElementById('sensors').append(el('div', {className:'row'},
      el('span', {className:'name', textContent:name}), pill, input,
      el('button', {textContent:'Set', onclick: () => set(parseFloat(input.value))}),
      el('button', {textContent:'-15 (normal)', onclick: () => set(-15)}),
      el('button', {textContent:'-5 (warm)', onclick: () => set(-5)}),
      el('button', {textContent:'85 (power-on glitch)', onclick: () => set(85)}), mode));
  }
  for (const name of Object.keys(s.switches)) {
    const pill = el('span', {className:'pill'});
    refs.switches[name] = {pill};
    document.getElementById('switches').append(el('div', {className:'row'},
      el('span', {className:'name', textContent:name + ' (GPIO ' + s.switches[name].pin + ')'}), pill,
      el('button', {textContent:'Close', onclick: () => api('/api/switch', {name, state:'closed'})}),
      el('button', {textContent:'Open', onclick: () => api('/api/switch', {name, state:'open'})}),
      el('button', {textContent:'Bounce (5 edges in 10 ms)', onclick: () => api('/api/bounce', {name})})));
  }
}
function buildDevices(s) {
  for (const [name, d] of Object.entries(s.devices)) {
    const pill = el('span', {className:'pill'});
    refs.devices[name] = {pill};
    const row = el('div', {className:'row'}, el('span', {className:'name', textContent:name}), pill);
    if (d.kind === 'garage_door') {
      [['open','Open door (down > up)'], ['close','Close door (up > down)'], ['fail_close','Start closing, then reverse']]
        .forEach(([action, label]) => row.append(el('button', {textContent:label, onclick: () => api('/api/garage', {name, action})})));
    } else if (d.kind === 'washing_machine') {
      row.append(el('button', {textContent:'Lid up', onclick: () => api('/api/switch', {name:d.inputs[0], state:'closed'})}),
                 el('button', {textContent:'Lid down', onclick: () => api('/api/switch', {name:d.inputs[0], state:'open'})}));
    }
    document.getElementById('devices').append(row);
  }
}
function rows(table, head, data, cells) {
  table.replaceChildren(el('tr', {}, ...head.map(h => el('th', {textContent:h}))),
    ...data.map(d => el('tr', {}, ...cells(d))));
}
function update(s) {
  document.getElementById('meta').textContent = 'time x' + s.time_scale + ' | w1: ' + s.w1_root;
  document.getElementById('notify-mode').textContent = 'Alerts go to: ' + s.notify_label;
  for (const [n, v] of Object.entries(s.sensors)) {
    const r = refs.sensors[n];
    r.pill.textContent = v.mode === 'ok' ? v.value.toFixed(2) + ' C' : v.mode;
    r.pill.className = 'pill ' + (v.mode === 'ok' ? 'ok' : 'error');
  }
  for (const [n, v] of Object.entries(s.switches)) {
    refs.switches[n].pill.textContent = v.state;
    refs.switches[n].pill.className = 'pill ' + v.state;
  }
  const devClass = {down:'ok', up:'warn', neither:'watch', unknown:'error'};
  for (const [n, v] of Object.entries(s.devices)) {
    refs.devices[n].pill.textContent = v.state || 'waiting';
    refs.devices[n].pill.className = 'pill ' + (devClass[v.state] || '');
  }
  rows(document.getElementById('alerts'), ['Rule','Source','State','Detail'], s.alerts, a => [
    el('td', {textContent:a.rule + (a.muted ? ' (muted)' : '')}), el('td', {textContent:a.source}),
    el('td', {}, el('span', {className:'pill ' + a.state, textContent:a.state})), el('td', {textContent:a.detail})]);
  rows(document.getElementById('events'), ['Time','Rule','Change','Sent?','Detail'], s.events, e => [
    el('td', {textContent:e.time}), el('td', {textContent:e.rule}),
    el('td', {textContent:e.old + ' -> ' + e.new + (e.repeat ? ' #' + e.repeat : '')}),
    el('td', {textContent:e.notified ? 'yes' : (e.muted ? 'muted' : 'no')}), el('td', {textContent:e.detail})]);
}
async function refresh() {
  try {
    const s = await (await fetch('/api/state')).json();
    if (!built) { build(s); buildDevices(s); built = true; }
    update(s);
  } catch (e) { document.getElementById('meta').textContent = 'disconnected'; }
}
document.getElementById('btn-test-email').onclick = () => api('/api/test-email', {});
document.getElementById('btn-test-report').onclick = () => api('/api/test-report', {});
document.getElementById('btn-status').onclick = () => api('/api/command', {name:'status'});
document.getElementById('btn-unmute').onclick = () => api('/api/command', {name:'unmute', args:['all']});
setInterval(refresh, 1000); refresh();
</script>
"""
