"""Wires sensors -> alert engine -> storage/notifications on one asyncio loop."""
from __future__ import annotations

import asyncio
import functools
import logging
import logging.handlers
import signal
import socket
import time
from datetime import datetime, timedelta

from .alerts import AlertEngine, AlertEvent
from .config import Config, build_devices, build_rules
from .models import AlertState, Reading, StateChange
from . import __version__
from .commands import CommandHandler, fmt_duration
from .notify import Dispatcher, NtfyChannel, NtfyCommandListener, build_dispatcher, event_notification, message_notification
from .report import build_report
from .sensors.ds18b20 import poll_ds18b20, read_ds18b20
from .sensors.gpio import watch_switch
from .sensors.sim import simulate_switch, simulate_temp
from .storage import Store

log = logging.getLogger("home_monitor")


def setup_logging(cfg: Config, verbose: bool = False) -> None:
    cfg.log_dir.mkdir(parents=True, exist_ok=True)
    fmt = logging.Formatter("%(asctime)s %(levelname)-7s %(name)s: %(message)s")
    root = logging.getLogger()
    root.setLevel(logging.DEBUG if verbose else cfg.log_level)
    console = logging.StreamHandler()
    console.setFormatter(fmt)
    rotating = logging.handlers.RotatingFileHandler(
        cfg.log_dir / "home_monitor.log", maxBytes=1_000_000, backupCount=5)
    rotating.setFormatter(fmt)
    root.handlers[:] = [console, rotating]


class ScaledClock:
    """time.monotonic() running ``scale`` times faster, so tests can see 10 minutes of alert
    escalation in 10 seconds. Only the fake-Pi mode uses it."""

    def __init__(self, scale: float) -> None:
        self.scale = scale
        self._t0 = time.monotonic()

    def __call__(self) -> float:
        return (time.monotonic() - self._t0) * self.scale


def _pi_uptime() -> str | None:
    """How long the machine has been up, to tell a program restart from a power cut."""
    try:
        with open("/proc/uptime") as f:
            return fmt_duration(float(f.read().split()[0]))
    except (OSError, ValueError, IndexError):
        return None


class Monitor:
    startup_wait_s = 20.0   # how long the startup message waits for the first readings

    def __init__(self, cfg: Config, simulate: bool = False, dispatcher: Dispatcher | None = None,
                 fake_pi=None, time_scale: float = 1.0) -> None:
        self.cfg = cfg
        self.simulate = simulate
        self.fake_pi = fake_pi
        self.time_scale = time_scale
        self.clock = time.monotonic if time_scale == 1.0 else ScaledClock(time_scale)
        self.store = Store(cfg.db_path)
        self.engine = AlertEngine(build_rules(cfg, self.clock()))
        self.devices = build_devices(cfg)
        self.latest: dict[str, tuple[float, float]] = {}   # sensor -> (value, wall time), for the status command
        self.dispatcher = dispatcher or build_dispatcher(cfg)
        self.queue: asyncio.Queue = asyncio.Queue()
        self.loop: asyncio.AbstractEventLoop | None = None
        self.notify_label = "real"  # shown in the fake-Pi panel
        self._background: set[asyncio.Task] = set()

    # --- event handling -------------------------------------------------
    def handle_events(self, events: list[AlertEvent]) -> None:
        for e in events:
            self.store.add_event(time.time(), e)
            level = {AlertState.ERROR: logging.ERROR, AlertState.WARN: logging.WARNING}.get(e.new, logging.INFO)
            log.log(level, "alert %s: %s -> %s%s (%s)", e.rule, e.old.value, e.new.value,
                    " [muted]" if e.muted else "", e.detail)
            if e.notify:
                recipients = self.cfg.recipients_for(self.engine.rule(e.rule).recipients, e.level.value)
                local_time = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                self._spawn(self.dispatcher.send(event_notification(self.cfg.system_name, e, local_time, recipients)))

    def _spawn(self, coro) -> None:
        task = asyncio.create_task(coro)  # so a slow SMTP server never blocks monitoring
        self._background.add(task)
        task.add_done_callback(self._background.discard)

    async def consume(self) -> None:
        while True:
            item = await self.queue.get()
            now = self.clock()
            if isinstance(item, Reading):
                self.latest[item.source] = (item.value, item.ts)
                self.store.add_reading(item.ts, item.source, item.value)
                self.handle_events(self.engine.on_value(item.source, item.value, now))
            elif isinstance(item, StateChange):
                self._apply_state(item.source, item.state, item.ts)
                self._feed_devices(item.source, item.state)

    def _apply_state(self, source: str, state: str, ts: float) -> None:
        self.store.add_state(ts, source, state)
        self.handle_events(self.engine.on_state(source, state, self.clock()))

    def _feed_devices(self, source: str, state: str) -> None:
        """Update garage door / washing machine states; publish each once it has settled."""
        loop = asyncio.get_running_loop()
        for dev in self.devices:
            if source in dev.inputs:
                candidate = dev.update(source, state)
                if candidate is not None:
                    loop.call_later(dev.settle_s, self._settle, dev, candidate)  # real time, not scaled

    def _settle(self, dev, candidate: str) -> None:
        if dev.settled(candidate):
            log.info("%s is now %s", dev.name, candidate)
            self._apply_state(dev.name, candidate, time.time())

    async def ticker(self) -> None:
        while True:
            await asyncio.sleep(max(self.cfg.tick_seconds / self.time_scale, 0.02))
            self.handle_events(self.engine.tick(self.clock()))

    async def daily(self) -> None:
        hour = self.cfg.daily_report_hour
        while True:
            now = datetime.now()
            nxt = now.replace(hour=hour or 0, minute=0, second=0, microsecond=0)
            if nxt <= now:
                nxt += timedelta(days=1)
            await asyncio.sleep((nxt - now).total_seconds())
            self.store.prune(time.time() - self.cfg.retention_days * 86400)
            if hour is not None:
                self._spawn(self.dispatcher.send(message_notification(
                    "report", f"[{self.cfg.system_name}] daily report", self.report_text(),
                    self.cfg.resolve(self.cfg.report_to))))

    def command_listeners(self) -> list[NtfyCommandListener]:
        """One listener per ntfy channel that has a command topic (fake-mode wrappers are looked through)."""
        out = []
        for channel in self.dispatcher.channels:
            base = getattr(channel, "inner", channel)
            if isinstance(base, NtfyChannel) and base.command_topic:
                out.append(NtfyCommandListener(base, CommandHandler(self, base.max_mute_s), self.cfg.system_name))
        return out

    async def run_command(self, name: str, args=()) -> str:
        """Run a command directly (the fake-Pi panel uses this); the reply text is returned."""
        result = CommandHandler(self).run(name, args)
        return f"{result.headline}\n{result.body}".strip()

    async def _send_startup(self) -> None:
        """Tell report_to that the monitor started, with the current status. Waits (up to startup_wait_s)
        until every sensor has reported and every device has a state, so the status is not empty."""
        deadline = time.monotonic() + self.startup_wait_s
        while time.monotonic() < deadline:
            if all(d["name"] in self.latest for d in self.cfg.ds18b20) and all(dev.published for dev in self.devices):
                break
            await asyncio.sleep(0.25)
        uptime = _pi_uptime()
        header = (f"{self.cfg.system_name} started {datetime.now():%Y-%m-%d %H:%M:%S} on {socket.gethostname()} "
                  f"(home_monitor {__version__}" + (f", machine up {uptime}" if uptime else "") + ")")
        status = CommandHandler(self).run("status", ()).body
        await self.dispatcher.send(message_notification(
            "startup", f"[{self.cfg.system_name}] started", f"{header}\n\n{status}\n", self.cfg.resolve(self.cfg.report_to)))

    def report_text(self) -> str:
        names = [d["name"] for d in self.cfg.ds18b20]
        return build_report(self.store, self.cfg.system_name, names, time.time())

    async def send_test_message(self) -> str:
        """For the fake-Pi panel: one message, no retries, with the result per channel."""
        body = f"This is a test message from home_monitor, sent {datetime.now():%Y-%m-%d %H:%M:%S}.\n"
        results = await self.dispatcher.send_once(message_notification(
            "test", f"[{self.cfg.system_name}] test message", body, self.cfg.resolve(self.cfg.report_to)))
        return "\n".join(results)

    async def send_test_report(self) -> str:
        results = await self.dispatcher.send_once(message_notification(
            "report", f"[{self.cfg.system_name}] daily report (test)", self.report_text(),
            self.cfg.resolve(self.cfg.report_to)))
        return "\n".join(results)

    # --- run ------------------------------------------------------------
    def _sensor_tasks(self) -> list[asyncio.Task]:
        tasks = []
        for d in self.cfg.ds18b20:
            interval = float(d.get("interval_s", 60)) / self.time_scale
            if self.simulate:
                coro = simulate_temp(d["name"], self.queue, min(interval, 2.0))
            elif self.fake_pi is not None:  # the real polling code, reading the fake w1 files
                reader = functools.partial(read_ds18b20, root=self.fake_pi.w1_root,
                                           retries=2, retry_delay=0.05)
                coro = poll_ds18b20(d["name"], d["id"], interval, self.queue, reader)
            else:
                coro = poll_ds18b20(d["name"], d["id"], interval, self.queue)
            tasks.append(asyncio.create_task(coro, name=f"temp:{d['name']}"))
        for s in self.cfg.switches:
            if self.simulate:
                coro = simulate_switch(s["name"], self.queue)
            else:  # the real watch_switch; button_factory=None means gpiozero
                coro = watch_switch(s["name"], int(s["pin"]), self.queue,
                                    active_low=bool(s.get("active_low", True)),
                                    bounce_s=float(s.get("bounce_s", 0.05)),
                                    button_factory=self.fake_pi.button_factory if self.fake_pi else None)
            tasks.append(asyncio.create_task(coro, name=f"switch:{s['name']}"))
        return tasks

    async def run(self, stop: asyncio.Event | None = None) -> None:
        stop = stop or asyncio.Event()
        loop = self.loop = asyncio.get_running_loop()
        for sig in (signal.SIGINT, signal.SIGTERM):
            try:
                loop.add_signal_handler(sig, stop.set)
            except NotImplementedError:  # e.g. Windows
                pass
        log.info("starting %s (%d sensors, %d switches, %d alert rules)%s", self.cfg.system_name,
                 len(self.cfg.ds18b20), len(self.cfg.switches), len(self.engine.rules),
                 " [SIMULATION]" if self.simulate else " [FAKE PI]" if self.fake_pi else "")
        tasks = [asyncio.create_task(self.consume(), name="consume"),
                 asyncio.create_task(self.ticker(), name="ticker"),
                 asyncio.create_task(self.daily(), name="daily"), *self._sensor_tasks(),
                 *(asyncio.create_task(l.run(), name="ntfy-commands") for l in self.command_listeners())]
        if self.cfg.send_startup:
            self._spawn(self._send_startup())
        stopper = asyncio.create_task(stop.wait(), name="stop")
        try:
            done, _ = await asyncio.wait([*tasks, stopper], return_when=asyncio.FIRST_COMPLETED)
            for t in done:
                if t is not stopper and t.exception():
                    raise t.exception()  # a sensor task died: fail loudly, systemd restarts us
        finally:
            for t in [*tasks, stopper]:
                t.cancel()
            await asyncio.gather(*tasks, stopper, return_exceptions=True)
            if self._background:
                await asyncio.wait(self._background, timeout=5)
            self.store.close()
            log.info("stopped")