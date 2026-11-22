import asyncio
import tempfile
import time
import unittest
from pathlib import Path

from home_monitor.alerts import AlertEngine, BoundsRule, Escalation, StateRule
from home_monitor.app import Monitor
from home_monitor.commands import CommandError, CommandHandler
from home_monitor.config import ConfigError, load_config
from home_monitor.models import AlertState as S
from home_monitor.notify import event_notification, format_event
from home_monitor.report import build_report
from home_monitor.storage import Store

try:
    from tests.helpers import RecordingDispatcher
except ImportError:  # run from inside tests/
    from helpers import RecordingDispatcher

ROOT = Path(__file__).resolve().parent.parent

CONFIG = """
[system]
name = "t"
tick_seconds = 1
[contacts]
paul = { ntfy = "paul-topic" }
ryan = { ntfy = "ryan-topic" }
[notify.levels]
watch = ["paul"]
warn = ["paul"]
error = ["paul"]
[[ds18b20]]
name = "chest"
display_name = "Chest freezer"
id = "28-x"
interval_s = 60
[[switches]]
name = "gup"
pin = 17
[[switches]]
name = "gdown"
pin = 27
[[devices]]
name = "garage"
display_name = "Garage door"
type = "garage_door"
up = "gup"
down = "gdown"
[[alerts]]
name = "laundry"
display_name = "Laundry is waiting"
type = "bounds"
source = "chest"
op = "greater_than"
value = -8.0
watch_s = 0
warn_s = 30
error_s = 6000
notify_on = ["watch", "warn", "error"]
claim = ["paul", "ryan"]
[[alerts]]
name = "plain"
type = "bounds"
source = "chest"
op = "greater_than"
value = 99.0
"""


def load(text, **kw):
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "c.toml"
        p.write_text(text)
        return load_config(p, **kw)


class StuckDoor(unittest.TestCase):
    """The example config's garage_door_stuck alert: moving for more than 60 s, in either direction."""

    def rule(self):
        cfg = load_config(ROOT / "config.example.toml")
        from home_monitor.config import build_rules
        return next(r for r in build_rules(cfg, 0.0) if r.name == "garage_door_stuck")

    def test_the_example_is_set_to_60_seconds_and_covers_both_directions(self):
        r = self.rule()
        self.assertEqual((r.escalation.warn_s, r.escalation.watch_s), (60.0, 0.0))
        self.assertEqual(r.targets, ("opening", "closing"))
        self.assertEqual(r.label, "Garage door stuck partway")

    def test_a_normal_trip_is_silent(self):
        r = self.rule()
        e = r.on_state("opening", 0)
        self.assertEqual([(x.new, x.notify) for x in e], [(S.WATCH, False)])      # starts at watch, silently
        self.assertEqual(r.tick(14), [])
        e = r.on_state("up", 15)                                                   # reached the top after 15 s
        self.assertEqual([(x.is_recovery, x.notify) for x in e], [(True, False)])  # and nobody is bothered

    def test_stuck_for_60_seconds_warns_then_errors_whichever_way_it_was_going(self):
        for direction in ("opening", "closing"):
            r = self.rule()
            r.on_state(direction, 0)
            self.assertEqual(r.tick(59), [])
            warn = r.tick(60)[0]
            self.assertEqual((warn.new, warn.notify), (S.WARN, True))
            self.assertEqual((r.tick(120)[0].new, r.state), (S.ERROR, S.ERROR))
            back = r.on_state("up" if direction == "opening" else "down", 130)[0]
            self.assertEqual((back.is_recovery, back.notify), (True, True))      # "back to normal" after a real problem

    def test_detail_names_the_actual_state(self):
        r = self.rule()
        r.on_state("closing", 0)
        self.assertEqual(r.detail, "Garage door is closing")


class MultiState(unittest.TestCase):
    def test_a_list_of_states_and_the_old_single_state_both_work(self):
        many = StateRule("a", "s", ["x", "y"], escalation=Escalation(0, 1, 2, 0))
        one = StateRule("b", "s", "x", escalation=Escalation(0, 1, 2, 0))
        self.assertEqual([bool(many.on_state(v, 0)) for v in ("x", "y", "z")], [True, False, True])   # x on, y stays on, z recovers
        self.assertEqual([bool(one.on_state(v, 0)) for v in ("x", "y")], [True, True])               # x on, y off

    def test_bad_state_values_are_refused(self):
        for bad in ([], [""], [1], ""):
            with self.assertRaises(ValueError, msg=bad):
                StateRule("a", "s", bad)


class DisplayNamesInConfig(unittest.TestCase):
    def test_every_kind_of_thing_can_have_one_and_the_fallback_is_the_id(self):
        cfg = load(CONFIG)
        self.assertEqual((cfg.label("chest"), cfg.label("garage"), cfg.label("gup")), ("Chest freezer", "Garage door", "gup"))
        self.assertEqual((cfg.alert_label("laundry"), cfg.alert_label("plain")), ("Laundry is waiting", "plain"))
        sw = load(CONFIG.replace('name = "gup"\n', 'name = "gup"\ndisplay_name = "Up sensor"\n'))
        self.assertEqual(sw.label("gup"), "Up sensor")

    def test_bad_values_are_refused_without_guessing(self):
        for bad in ('display_name = 5', 'display_name = ""', 'display_name = "   "', f'display_name = "{"x" * 81}"'):
            with self.assertRaises(ConfigError, msg=bad) as cm:
                load(CONFIG.replace('display_name = "Chest freezer"', bad))
            self.assertIn("display_name", str(cm.exception))
            self.assertIn("chest", str(cm.exception))

    def test_the_example_config_uses_them(self):
        cfg = load_config(ROOT / "config.example.toml")
        self.assertEqual(cfg.alert_label("washing_machine_lid_down_too_long"), "Laundry is waiting")
        self.assertEqual(cfg.label("roselawn_garage_door"), "Garage door")
        self.assertEqual(cfg.label("chest_freezer"), "Chest freezer")


class DisplayNamesInMessages(unittest.TestCase):
    def rule(self, **kw):
        return BoundsRule("laundry", "chest", "greater_than", -8.0, escalation=Escalation(0, 30, 60, 30), **kw)

    def event(self, rule, value=-5.0):
        return rule.on_value(value, 0)[0]

    def test_the_subject_and_detail_use_display_names_but_the_event_keeps_the_id(self):
        r = self.rule(display_name="Laundry is waiting", source_label="Chest freezer")
        e = self.event(r)
        subject, body = format_event("t", e)
        self.assertEqual(subject, "[t] Laundry is waiting: OK -> WATCH")
        self.assertIn("Chest freezer = -5.00 (greater_than -8.0)", body)
        n = event_notification("t", e, [])
        self.assertEqual((n.rule, n.source), ("laundry", "chest"))                   # buttons and commands use the id

    def test_recovery_and_reminder_subjects_too(self):
        r = self.rule(display_name="Laundry is waiting")
        self.event(r)
        r.tick(60)
        again = r.tick(90)[0]
        self.assertEqual(format_event("t", again)[0], "[t] Laundry is waiting: still ERROR (reminder 1)")
        back = r.on_value(-15.0, 100)[0]
        self.assertEqual(format_event("t", back)[0], "[t] Laundry is waiting: back to normal (was ERROR)")

    def test_without_a_display_name_nothing_changes(self):
        subject, body = format_event("t", self.event(self.rule()))
        self.assertEqual(subject, "[t] laundry: OK -> WATCH")
        self.assertIn("chest = -5.00", body)

    def test_the_snapshot_carries_the_label(self):
        r = self.rule(display_name="Laundry is waiting")
        self.assertEqual(AlertEngine([r]).snapshot(0)[0]["label"], "Laundry is waiting")

    def test_the_report_uses_display_names(self):
        store = Store(":memory:")
        now = time.time()
        store.add_reading(now - 60, "chest", -15.0)
        r = self.rule()
        store.add_event(now - 30, self.event(r))
        text = build_report(store, "t", ["chest"], now, hours=1, labels={"chest": "Chest freezer"},
                            rule_labels={"laundry": "Laundry is waiting"})
        self.assertIn("Chest freezer: now -15.00 C", text)
        self.assertIn("Laundry is waiting: ok -> watch", text)
        plain = build_report(store, "t", ["chest"], now, hours=1)
        self.assertIn("chest: now -15.00 C", plain)
        self.assertIn("laundry: ok -> watch", plain)


class DisplayNamesInCommands(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        p = Path(self._tmp.name) / "c.toml"
        p.write_text(CONFIG)
        cfg = load_config(p)
        cfg.db_path = Path(self._tmp.name) / "t.db"
        self.rec = RecordingDispatcher()
        self.mon = Monitor(cfg, dispatcher=self.rec)
        self.mon.loop = asyncio.get_running_loop()
        self.addCleanup(self.mon.store.close)
        self.h = CommandHandler(self.mon)
        self.mon.engine.on_value("chest", -5.0, self.mon.clock())            # the laundry alert is active

    def test_status_reads_naturally(self):
        self.mon.latest["chest"] = (-15.2, time.time())
        self.mon.devices[0].published = "down"
        body = self.h.run("status", ()).body
        self.assertIn("Chest freezer: -15.2 C", body)
        self.assertIn("Garage door: down", body)
        self.assertIn("Laundry is waiting: WATCH (Chest freezer = -5.00", body)

    def test_replies_use_the_display_name_and_errors_keep_the_id_you_typed(self):
        self.assertEqual(self.h.run("claim", ("laundry", "ryan", "ryan")).headline, "Laundry is waiting: you have it")
        self.assertEqual(self.h.run("release", ("laundry", "ryan")).headline, "Laundry is waiting: back to everyone")
        self.assertEqual(self.h.run("choose", ("laundry", "paul")).headline, "Laundry is waiting: give it to whom?")
        self.assertEqual(self.h.run("mute", ("laundry", "2h")).headline, "muted Laundry is waiting for 2h")
        self.assertEqual(self.h.run("unmute", ("laundry",)).headline, "unmuted Laundry is waiting")
        self.assertEqual(self.h.run("mute", ("all", "1h")).headline, "muted every alert for 1h")
        with self.assertRaises(CommandError) as cm:
            self.h.run("mute", ("nosuch", "1h"))
        self.assertIn("'nosuch'", str(cm.exception))

    async def test_claim_announcements_use_the_display_name(self):
        rule = self.mon.engine.rule("laundry")
        rule.claimed_by = "ryan"
        self.mon.announce_claim(rule, "ryan", "paul", None)
        await asyncio.sleep(0.1)
        self.assertEqual(self.rec.notifications[-1].subject, "[t] Laundry is waiting: Paul gave it to Ryan")
        self.assertEqual(self.rec.notifications[-1].rule, "laundry")             # still the id underneath

    async def test_stored_events_and_notifications_keep_ids_while_the_title_is_readable(self):
        events = self.mon.engine.tick(self.mon.clock() + 31)                   # the warn level
        self.mon.handle_events(events)
        await asyncio.sleep(0.1)
        self.assertEqual(self.mon.store.recent_events(0)[0][1], "laundry")      # the database keeps the id
        n = self.rec.notifications[-1]
        self.assertEqual((n.rule, n.subject), ("laundry", "[t] Laundry is waiting: WATCH -> WARN"))


if __name__ == "__main__":
    unittest.main()
