import asyncio
import tempfile
import unittest
from pathlib import Path

from home_monitor.alerts import BoundsRule, Escalation, StateRule
from home_monitor.app import Monitor
from home_monitor.config import ConfigError, build_rules, load_config
from home_monitor.notify import format_event
from home_monitor.textutil import fmt_duration

try:
    from tests.helpers import RecordingDispatcher
except ImportError:  # run from inside tests/
    from helpers import RecordingDispatcher

ROOT = Path(__file__).resolve().parent.parent


def body(e):
    return format_event("t", e)[1]


class Elapsed(unittest.TestCase):
    def rule(self, **kw):
        return BoundsRule("hot", "chest", "greater_than", -8.0, escalation=Escalation(0, 30, 60, 30), **kw)

    def test_each_event_knows_how_long_the_problem_has_gone_on(self):
        r = self.rule()
        events = r.on_value(-5.0, 100) + r.tick(130) + r.tick(160) + r.tick(190)
        self.assertEqual([e.elapsed_s for e in events], [0, 30, 60, 90])            # watch, warn, error, reminder
        self.assertEqual(r.on_value(-15.0, 400)[0].elapsed_s, 300)                  # recovery: how long it lasted

    def test_the_message_says_how_long_instead_of_the_clock_time(self):
        r = self.rule()
        events = r.on_value(-5.0, 100) + r.tick(130) + r.tick(160) + r.tick(190)
        texts = [body(e) for e in events]
        self.assertEqual(texts[0], "chest = -5.00 (greater_than -8.0)\nStarted just now.\n")
        self.assertEqual(texts[1], "chest = -5.00 (greater_than -8.0)\nStarted 30s ago.\n")
        self.assertIn("Started 1m ago.", texts[2])
        self.assertIn("Started 1m ago.", texts[3])
        recovery = body(r.on_value(-15.0, 400)[0])
        self.assertEqual(recovery, "chest = -15.00 (greater_than -8.0)\nIt lasted 5m.\n")
        self.assertFalse(any("Time:" in t or "20" in t.split("\n")[1] for t in texts + [recovery]))   # no clock times

    def test_a_door_that_has_been_up_for_a_while(self):
        r = StateRule("up", "garage", "up", source_label="Garage door", escalation=Escalation(0, 60, 120, 300))
        r.on_state("up", 0)
        warn = r.tick(300)[0]                                                       # five minutes later
        self.assertEqual(body(warn), "Garage door is up\nStarted 5m ago.\n")
        self.assertEqual(body(r.on_state("down", 4000)[0]), "Garage door is down\nIt lasted 1h 6m.\n")

    def test_a_claimed_alert_still_says_who_took_it(self):
        r = self.rule()
        r.on_value(-5.0, 0)
        r.claimed_by = "ryan"
        self.assertEqual(body(r.tick(30)[0]), "chest = -5.00 (greater_than -8.0)\nStarted 30s ago.\nTaken by Ryan.\n")

    def test_the_formatting_helper_is_unchanged_and_still_importable_from_commands(self):
        from home_monitor.commands import fmt_duration as from_commands
        self.assertIs(from_commands, fmt_duration)
        self.assertEqual([fmt_duration(x) for x in (0, 45, 60, 5400, 7200, 90000)], ["0s", "45s", "1m", "1h 30m", "2h", "1d 1h"])


class CustomMessage(unittest.TestCase):
    def rule(self, message, **kw):
        return StateRule("laundry", "wm", "down", message=message, source_label="Washing machine",
                         display_name="Laundry is waiting", escalation=Escalation(0, 60, 120, 60), **kw)

    def test_your_own_wording_replaces_the_default_lines(self):
        r = self.rule("The laundry has been waiting {duration} ({source})")
        events = r.on_state("down", 0) + r.tick(3720)
        self.assertEqual(body(events[0]), "The laundry has been waiting a moment (Washing machine)\n")
        self.assertEqual(body(events[1]), "The laundry has been waiting 1h 2m (Washing machine)\n")
        self.assertEqual(events[1].detail, "Washing machine is down")               # the plain detail is still there for logs

    def test_under_five_seconds_reads_as_a_moment_and_longer_is_a_number(self):
        r = self.rule("Up for {duration}")
        events = r.on_state("down", 0) + r.tick(4) + r.tick(5) + r.tick(59)
        self.assertEqual([body(e).strip() for e in r.tick(60)], ["Up for 1m"])
        self.assertEqual(body(events[0]).strip(), "Up for a moment")

    def test_the_subject_still_names_the_alert_and_a_claim_is_still_shown(self):
        r = self.rule("Waiting {duration}")
        r.on_state("down", 0)
        r.claimed_by = "ryan"
        subject, text = format_event("t", r.tick(120)[0])
        self.assertEqual(subject, "[t] Laundry is waiting: WATCH -> ERROR")
        self.assertEqual(text, "Waiting 2m\nTaken by Ryan.\n")

    def test_a_recovery_uses_the_standard_wording(self):
        r = self.rule("Waiting {duration}")
        r.on_state("down", 0)
        r.tick(60)
        self.assertEqual(body(r.on_state("up", 600)[0]), "Washing machine is up\nIt lasted 10m.\n")

    def test_bad_placeholders_are_refused(self):
        for bad in ("Waiting {minutes}", "Waiting {0}", "Waiting {duration", "{}"):
            with self.assertRaises(ValueError, msg=bad):
                self.rule(bad)
        self.assertEqual(body(self.rule("{{not a placeholder}} {duration}").on_state("down", 0)[0]),
                         "{not a placeholder} a moment\n")                               # braces can be escaped

    def test_the_config_names_the_alert_when_the_message_is_wrong(self):
        base = ('[[ds18b20]]\nname="c"\nid="28-x"\n[[alerts]]\nname="a"\ntype="bounds"\nsource="c"\nop="greater_than"\nvalue=1\n')
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "c.toml"
            p.write_text(base + 'message = "{wrong}"\n')
            with self.assertRaises(ConfigError) as cm:
                load_config(p)
            self.assertIn("'a'", str(cm.exception))
            self.assertIn("{duration}", str(cm.exception))


class ExampleConfig(unittest.TestCase):
    def rules(self):
        return {r.name: r for r in build_rules(load_config(ROOT / "config.example.toml"), 0.0)}

    def test_the_garage_and_laundry_alerts_say_it_in_plain_words(self):
        rules = self.rules()
        up, laundry = rules["garage_door_up_too_long"], rules["washing_machine_lid_down_too_long"]
        up.on_state("up", 0)
        self.assertEqual(up.tick(300)[0].message, "The garage door has been up for 5m")
        laundry.on_state("down", 0)
        self.assertEqual(laundry.tick(3600)[0].message, "The laundry has been waiting 1h")


class ThroughTheMonitor(unittest.IsolatedAsyncioTestCase):
    async def test_the_notification_that_goes_out_carries_the_duration(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "c.toml"
            p.write_text('[system]\nname="t"\n[contacts]\npaul = { ntfy = "paul-topic" }\n[notify.levels]\nwarn = ["paul"]\n'
                         '[[ds18b20]]\nname="chest"\ndisplay_name="Chest freezer"\nid="28-x"\n'
                         '[[alerts]]\nname="hot"\ntype="bounds"\nsource="chest"\nop="greater_than"\nvalue=-8.0\n'
                         'watch_s=0\nwarn_s=30\nerror_s=600\n')
            cfg = load_config(p)
            cfg.db_path = Path(tmp) / "t.db"
            rec = RecordingDispatcher()
            mon = Monitor(cfg, dispatcher=rec)
            mon.loop = asyncio.get_running_loop()
            now = mon.clock()
            mon.engine.on_value("chest", -5.0, now)
            mon.handle_events(mon.engine.tick(now + 31))
            await asyncio.sleep(0.1)
            mon.store.close()
        self.assertEqual(rec.notifications[-1].body, "Chest freezer = -5.00 (greater_than -8.0)\nStarted 31s ago.\n")


if __name__ == "__main__":
    unittest.main()
