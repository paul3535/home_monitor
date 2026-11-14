import asyncio
import json
import tempfile
import unittest
import urllib.request
from pathlib import Path

from home_monitor.app import Monitor
from home_monitor.config import ConfigError, load_config
from home_monitor.fakepi import FakePi, start_control_server
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
watch = ["me"]
warn = ["me"]
error = ["me"]
[[switches]]
name = "gdown"
pin = 27
[[switches]]
name = "gup"
pin = 17
[[switches]]
name = "lid"
pin = 22
[[devices]]
name = "garage"
type = "garage_door"
up = "gup"
down = "gdown"
settle_s = 0.2
[[devices]]
name = "washer"
type = "washing_machine"
lid_up = "lid"
settle_s = 0.2
[[alerts]]
name = "up_too_long"
type = "state"
source = "garage"
state = "up"
warn_s = 60
error_s = 120
error_retry_s = 300
notify_on = ["watch", "warn", "error"]
[[alerts]]
name = "garage_unknown"
type = "state"
source = "garage"
state = "unknown"
warn_s = 1
error_s = 1
notify_on = ["watch", "warn", "error"]
[[alerts]]
name = "failed"
type = "door_failed_to_close"
source = "garage"
warn_s = 1
error_s = 1
error_retry_s = 30
notify_on = ["watch", "warn", "error"]
[[alerts]]
name = "washer_down"
type = "state"
source = "washer"
state = "down"
warn_s = 3600
error_s = 5400
notify_on = ["watch", "warn", "error"]
"""


class Config(unittest.TestCase):
    def load(self, text):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "c.toml"
            p.write_text(text)
            return load_config(p)

    def test_device_must_name_real_switches(self):
        with self.assertRaises(ConfigError):
            self.load('[[switches]]\nname="a"\npin=1\n[[devices]]\nname="g"\ntype="garage_door"\nup="a"\ndown="nope"\n')

    def test_alert_source_must_match_its_type(self):
        bad = CONFIG.replace('type = "door_failed_to_close"\nsource = "garage"', 'type = "bounds"\nsource = "garage"\nop = "greater_than"\nvalue = 1')
        with self.assertRaises(ConfigError):
            self.load(bad)


class GarageAndWasher(unittest.IsolatedAsyncioTestCase):
    async def test_garage_door_and_washing_machine_through_the_panel(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "c.toml"
            p.write_text(CONFIG)
            cfg = load_config(p)
            cfg.db_path = Path(tmp) / "t.db"
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

            async def snapshot():
                s = await acall("/api/state")
                return ({n: d["state"] for n, d in s["devices"].items()}, {a["rule"]: a["state"] for a in s["alerts"]})

            stop = asyncio.Event()
            run = asyncio.create_task(mon.run(stop))
            try:
                await asyncio.sleep(0.8)
                devices, _ = await snapshot()
                self.assertEqual(devices, {"garage": "down", "washer": "down"})   # realistic start, no spurious "unknown"

                # Both garage switches closed for 80 ms, then back: settles away, nothing published
                fake.set_switch("gup", True)
                await asyncio.sleep(0.08)
                fake.set_switch("gup", False)
                await asyncio.sleep(0.6)
                devices, alerts = await snapshot()
                self.assertEqual(devices["garage"], "down")
                self.assertEqual(alerts["garage_unknown"], "ok")

                await acall("/api/garage", {"name": "garage", "action": "open", "step_s": 0.4})
                await asyncio.sleep(1.0)
                self.assertEqual((await snapshot())[0]["garage"], "up")
                await asyncio.sleep(1.3)                                          # 60 virtual seconds up
                self.assertEqual((await snapshot())[1]["up_too_long"], "warn")

                await acall("/api/garage", {"name": "garage", "action": "fail_close", "step_s": 0.4})
                await asyncio.sleep(1.3)
                devices, alerts = await snapshot()
                self.assertEqual((devices["garage"], alerts["failed"]), ("up", "error"))

                await acall("/api/garage", {"name": "garage", "action": "close", "step_s": 0.4})
                await asyncio.sleep(1.2)
                devices, alerts = await snapshot()
                self.assertEqual((devices["garage"], alerts["failed"], alerts["up_too_long"]), ("down", "ok", "ok"))

                await acall("/api/switch", {"name": "lid", "state": "closed"})     # lid up
                await asyncio.sleep(0.6)
                devices, alerts = await snapshot()
                self.assertEqual((devices["washer"], alerts["washer_down"]), ("up", "ok"))
                states = [r[0] for r in mon.store._all("SELECT state FROM states WHERE source='garage' ORDER BY ts")]
                self.assertEqual(states, ["down", "opening", "up", "closing", "up", "closing", "down"])   # direction from where it last rested
            finally:
                stop.set()
                await run
                server.shutdown()
                server.server_close()

            subjects = " | ".join(sink.sent)
            for expected in ("up_too_long: WATCH -> WARN", "failed: OK -> WATCH", "failed: WATCH -> ERROR",
                             "failed: back to normal", "washer_down: OK -> WATCH"):
                self.assertIn(expected, subjects)
            self.assertNotIn("garage_unknown", subjects)


if __name__ == "__main__":
    unittest.main()
