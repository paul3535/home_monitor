import asyncio
import tempfile
import time
import unittest
from pathlib import Path

from home_monitor.app import Monitor
from home_monitor.config import ConfigError, build_devices, load_config
from home_monitor.models import Reading, StateChange
from home_monitor.report import build_report
try:
    from tests.helpers import RecordingDispatcher
except ImportError:  # run from inside tests/
    from helpers import RecordingDispatcher

ROOT = Path(__file__).resolve().parent.parent


class Config(unittest.TestCase):
    def test_example_config_loads_and_builds(self):
        from home_monitor.config import build_rules
        cfg = load_config(ROOT / "config.example.toml")
        self.assertEqual(len(cfg.ds18b20), 2)
        self.assertEqual(len(build_rules(cfg, 0)), 9)
        self.assertEqual([d.kind for d in build_devices(cfg)], ["garage_door", "washing_machine"])
        self.assertEqual([c.addresses["email"] for c in cfg.resolve(cfg.level_contacts["error"])],
                         ["you@example.com", "mum@example.com", "dad@example.com"])
        self.assertEqual(cfg.contacts["paul"], {"email": "you@example.com", "ntfy": "paul-example-topic"})

    def test_unknown_source_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "c.toml"
            p.write_text('[[alerts]]\nname="a"\ntype="state"\nsource="nope"\nstate="open"\n')
            with self.assertRaises(ConfigError):
                load_config(p)

    def test_unknown_contact_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "c.toml"
            p.write_text('[notify.levels]\nerror=["ghost"]\n')
            with self.assertRaises(ConfigError):
                load_config(p)


class EndToEnd(unittest.IsolatedAsyncioTestCase):
    async def test_hot_freezer_sends_warn_then_recovery_and_is_stored(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "c.toml"
            p.write_text("""
[system]
name = "t"
tick_seconds = 0.05
[contacts]
me = "me@x"
[notify.levels]
warn = ["me"]
error = ["me"]
[[ds18b20]]
name = "chest"
id = "28-x"
[[alerts]]
name = "hot"
type = "bounds"
source = "chest"
op = "greater_than"
value = -8.0
warn_s = 0.2
error_s = 5
""")
            cfg = load_config(p)
            fake = RecordingDispatcher(cfg)
            mon = Monitor(cfg, dispatcher=fake)
            stop = asyncio.Event()
            run = asyncio.create_task(mon.run(stop))
            mon.queue.put_nowait(Reading("chest", -5.0, time.time()))
            await asyncio.sleep(0.5)
            mon.queue.put_nowait(Reading("chest", -15.0, time.time()))
            await asyncio.sleep(0.2)
            report = build_report(mon.store, "t", ["chest"], time.time(), hours=1)
            stop.set()
            await run
            subjects = fake.sent
            self.assertTrue(any("OK" not in s and "WARN" in s for s in subjects), subjects)
            self.assertTrue(any("back to normal" in s for s in subjects), subjects)
            self.assertEqual([c.name for c in fake.notifications[0].recipients], ["me"])
            self.assertEqual(fake.notifications[0].level, "warn")
            self.assertIn("chest: now -15.00", report)
            self.assertIn("hot: watch -> warn", report)


if __name__ == "__main__":
    unittest.main()
