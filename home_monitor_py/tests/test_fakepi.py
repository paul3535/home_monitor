import asyncio
import json
import tempfile
import unittest
import urllib.error
import urllib.request
from pathlib import Path

from home_monitor.app import Monitor
from home_monitor.config import load_config
from home_monitor.fakepi import FakeButton, FakePi, start_control_server
from home_monitor.sensors.ds18b20 import read_ds18b20
try:
    from tests.helpers import RecordingDispatcher
except ImportError:  # run from inside tests/
    from helpers import RecordingDispatcher

CONFIG = """
[system]
name = "t"
tick_seconds = 1
[contacts]
me = "me@x"
[notify.levels]
warn = ["me"]
error = ["me"]
[[ds18b20]]
name = "chest"
id = "28-x"
interval_s = 60
[[switches]]
name = "door"
pin = 17
[[alerts]]
name = "hot"
type = "bounds"
source = "chest"
op = "greater_than"
value = -8.0
warn_s = 30
error_s = 60
error_retry_s = 30
[[alerts]]
name = "door_open"
type = "state"
source = "door"
state = "open"
warn_s = 30
error_s = 600
"""


def make_cfg(tmp):
    p = Path(tmp) / "c.toml"
    p.write_text(CONFIG)
    cfg = load_config(p)
    cfg.db_path = Path(tmp) / "t.db"
    return cfg


class FakeHardware(unittest.TestCase):
    def test_sensor_modes_go_through_the_real_reader(self):
        with tempfile.TemporaryDirectory() as tmp:
            fake = FakePi(make_cfg(tmp))
            read = lambda: read_ds18b20("28-x", root=fake.w1_root, retries=1, retry_delay=0)
            self.assertEqual(read(), -15.0)
            fake.set_temp("chest", -3.0)           # a real freezer temperature, not an error code
            self.assertEqual(read(), -3.0)
            fake.set_temp("chest", mode="crc")
            self.assertIsNone(read())
            fake.set_temp("chest", mode="unplugged")
            self.assertIsNone(read())
            fake.set_temp("chest", mode="ok")
            self.assertEqual(read(), -3.0)
            fake.set_temp("chest", 85.0)           # power-on glitch value is rejected
            self.assertIsNone(read())
            with self.assertRaises(KeyError):
                fake.set_temp("nope", 1.0)
            with self.assertRaises(ValueError):
                fake.set_temp("chest", 9999)

    def test_button_debounce_and_dedupe(self):
        b = FakeButton(17, True, bounce_time=0.05)
        seen = []
        b.when_pressed = lambda: seen.append("closed")
        b.when_released = lambda: seen.append("open")
        self.assertFalse(b.drive(True))            # already closed: nothing to report
        self.assertTrue(b.drive(False))
        self.assertFalse(b.drive(True))            # inside bounce_time: ignored
        self.assertEqual(seen, ["open"])


class FakePiEndToEnd(unittest.IsolatedAsyncioTestCase):
    async def test_control_panel_drives_alerts_with_time_scale(self):
        with tempfile.TemporaryDirectory() as tmp:
            cfg = make_cfg(tmp)
            fake, sink = FakePi(cfg), RecordingDispatcher(cfg)
            mon = Monitor(cfg, dispatcher=sink, fake_pi=fake, time_scale=60)
            server = start_control_server(fake, mon, "127.0.0.1", 0)
            base = f"http://127.0.0.1:{server.server_address[1]}"

            def call(path, body=None):
                req = urllib.request.Request(base + path, data=None if body is None else json.dumps(body).encode(),
                                             headers={"Content-Type": "application/json"})
                with urllib.request.urlopen(req, timeout=5) as r:
                    return json.loads(r.read())

            async def acall(path, body=None):
                return await asyncio.to_thread(call, path, body)

            async def alert_state(name):
                state = await acall("/api/state")
                return next(a["state"] for a in state["alerts"] if a["rule"] == name)

            stop = asyncio.Event()
            run = asyncio.create_task(mon.run(stop))
            try:
                await asyncio.sleep(0.4)
                self.assertEqual((await acall("/api/state"))["switches"]["door"]["state"], "closed")
                self.assertEqual(await alert_state("hot"), "ok")

                # 60 virtual seconds of "too warm" take about one real second
                await acall("/api/temp", {"name": "chest", "value": -5})
                await asyncio.sleep(3.0)
                self.assertEqual(await alert_state("hot"), "error")
                await acall("/api/temp", {"name": "chest", "value": -15})
                await asyncio.sleep(1.5)
                self.assertEqual(await alert_state("hot"), "ok")

                # a noisy contact is one event, and the door alert follows
                await acall("/api/bounce", {"name": "door"})
                await asyncio.sleep(0.9)
                self.assertEqual(await alert_state("door_open"), "warn")
                await acall("/api/switch", {"name": "door", "state": "closed"})
                await asyncio.sleep(0.3)
                self.assertEqual(await alert_state("door_open"), "ok")
                states = [r[0] for r in mon.store._all("SELECT state FROM states WHERE source='door' ORDER BY ts")]
                self.assertEqual(states, ["closed", "open", "closed"])

                with self.assertRaises(urllib.error.HTTPError) as cm:
                    await acall("/api/temp", {"name": "nope", "value": 1})
                self.assertEqual(cm.exception.code, 400)
            finally:
                stop.set()
                await run
                server.shutdown()
                server.server_close()

            subjects = " | ".join(sink.sent)
            for expected in ("hot: WATCH -> WARN", "hot: WARN -> ERROR",
                             "hot: back to normal", "door_open: WATCH -> WARN"):
                self.assertIn(expected, subjects)


if __name__ == "__main__":
    unittest.main()
