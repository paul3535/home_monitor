import unittest

from home_monitor.alerts import (AlertEngine, BoundsRule, Escalation, StaleRule, StateRule)
from home_monitor.models import AlertState as S


def freezer_rule(**kw):
    # Thresholds from the C++ test email: > -8 C, watch 0 s, warn 300 s, error 600 s, retry 300 s
    return BoundsRule("chest_warm", "chest", "greater_than", -8.0,
                      escalation=Escalation(0, 300, 600, 300), **kw)


class BoundsEscalation(unittest.TestCase):
    def test_normal_reading_does_nothing(self):
        r = freezer_rule()
        self.assertEqual(r.on_value(-15.0, 0), [])
        self.assertEqual(r.state, S.OK)

    def test_escalates_on_the_old_timings(self):
        r = freezer_rule()
        e = r.on_value(-5.0, 0)[0]
        self.assertEqual((e.old, e.new, e.notify), (S.OK, S.WATCH, False))  # watch: no message
        self.assertEqual(r.tick(299), [])
        e = r.tick(300)[0]
        self.assertEqual((e.new, e.notify), (S.WARN, True))
        e = r.tick(600)[0]
        self.assertEqual((e.new, e.notify, e.repeat), (S.ERROR, True, 0))

    def test_error_repeats_every_retry_interval(self):
        r = freezer_rule()
        r.on_value(-5.0, 0)
        r.tick(600)
        self.assertEqual(r.tick(899), [])
        e = r.tick(900)[0]
        self.assertEqual((e.new, e.repeat, e.notify), (S.ERROR, 1, True))
        self.assertEqual(r.tick(1199), [])
        self.assertEqual(r.tick(1200)[0].repeat, 2)

    def test_timers_advance_without_new_readings(self):
        # A reading arriving once a minute must not delay escalation.
        r = freezer_rule()
        r.on_value(-5.0, 0)
        self.assertEqual(r.tick(300)[0].new, S.WARN)

    def test_recovery_notifies_if_a_message_had_gone_out(self):
        r = freezer_rule()
        r.on_value(-5.0, 0)
        r.tick(300)
        e = r.on_value(-15.0, 310)[0]
        self.assertTrue(e.is_recovery)
        self.assertEqual((e.old, e.notify, e.level), (S.WARN, True, S.WARN))
        self.assertEqual(r.state, S.OK)

    def test_recovery_from_watch_is_silent(self):
        r = freezer_rule()
        r.on_value(-5.0, 0)
        e = r.on_value(-15.0, 10)[0]
        self.assertEqual(e.notify, False)

    def test_a_blip_restarts_the_clock(self):
        r = freezer_rule()
        r.on_value(-5.0, 0)
        r.on_value(-15.0, 100)
        r.on_value(-5.0, 200)
        self.assertEqual(r.tick(499), [])          # 299 s since the second trigger
        self.assertEqual(r.tick(500)[0].new, S.WARN)

    def test_mute_suppresses_messages_but_not_recovery(self):
        r = freezer_rule()
        r.mute(0, 1000)
        r.on_value(-5.0, 0)
        e = r.tick(300)[0]
        self.assertEqual((e.new, e.notify, e.muted), (S.WARN, False, True))
        e = r.on_value(-15.0, 400)[0]
        self.assertTrue(e.is_recovery and e.notify)

    def test_outside_range(self):
        r = BoundsRule("x", "s", "outside", -20.0, -10.0, escalation=Escalation(0, 1, 2, 0))
        self.assertEqual(r.on_value(-15.0, 0), [])
        self.assertEqual(r.on_value(-9.0, 1)[0].new, S.WATCH)

    def test_bad_config_rejected(self):
        with self.assertRaises(ValueError):
            BoundsRule("x", "s", "bigger", 1.0)
        with self.assertRaises(ValueError):
            Escalation(0, 600, 300, 0)


class StateAndStale(unittest.TestCase):
    def test_door_open_too_long(self):
        r = StateRule("door", "reed", "open", escalation=Escalation(0, 300, 900, 0))
        self.assertEqual(r.on_state("closed", 0), [])
        r.on_state("open", 10)
        self.assertEqual(r.tick(309), [])
        self.assertEqual(r.tick(310)[0].new, S.WARN)
        e = r.on_state("closed", 320)[0]
        self.assertTrue(e.is_recovery)

    def test_silent_sensor_alerts_and_recovers(self):
        r = StaleRule("silent", "chest", max_age_s=180, now=0, escalation=Escalation(0, 0, 600, 0))
        self.assertEqual(r.tick(100), [])
        e = r.tick(181)[0]                      # stale and warn_s=0 -> WARN immediately
        self.assertEqual((e.new, e.notify), (S.WARN, True))
        e = r.on_value(-15.0, 200)[0]
        self.assertTrue(e.is_recovery)

    def test_regular_readings_keep_stale_rule_quiet(self):
        r = StaleRule("silent", "chest", max_age_s=180, now=0)
        for t in range(0, 3600, 60):
            r.on_value(-15.0, t)
            self.assertEqual(r.tick(t + 1), [])


class Engine(unittest.TestCase):
    def test_routes_by_source(self):
        a = BoundsRule("a", "chest", "greater_than", -8.0, escalation=Escalation(0, 1, 2, 0))
        b = BoundsRule("b", "upright", "greater_than", -8.0, escalation=Escalation(0, 1, 2, 0))
        eng = AlertEngine([a, b])
        events = eng.on_value("chest", -5.0, 0)
        self.assertEqual([e.rule for e in events], ["a"])
        self.assertEqual(b.state, S.OK)

    def test_duplicate_names_rejected(self):
        a = BoundsRule("a", "x", "greater_than", 0.0)
        with self.assertRaises(ValueError):
            AlertEngine([a, BoundsRule("a", "y", "greater_than", 0.0)])

    def test_mute_all(self):
        eng = AlertEngine([BoundsRule("a", "x", "greater_than", 0.0), BoundsRule("b", "y", "greater_than", 0.0)])
        self.assertEqual(eng.mute(0, 60), 2)


if __name__ == "__main__":
    unittest.main()
