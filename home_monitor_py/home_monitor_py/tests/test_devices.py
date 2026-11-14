import unittest

from home_monitor.alerts import DoorClosingFailureRule, Escalation
from home_monitor.devices import GarageDoor, WashingMachine
from home_monitor.models import AlertState as S


def garage(**kw):
    return GarageDoor("door", "up", "down", **kw)


def published(dev, *changes):
    """Feed (switch, state) changes and return the derived states that would be published."""
    out = []
    for source, state in changes:
        cand = dev.update(source, state)
        if cand is not None and dev.settled(cand):
            out.append(cand)
    return out


class GarageDoorStates(unittest.TestCase):
    def test_truth_table(self):
        cases = {(True, False): "up", (False, True): "down", (False, False): "neither", (True, True): "unknown"}
        for (up, down), expected in cases.items():
            d = garage()
            d.update("up", "closed" if up else "open")
            d.update("down", "closed" if down else "open")
            self.assertEqual(d.compute(), expected, (up, down))

    def test_waits_until_both_switches_have_reported(self):
        d = garage()
        self.assertIsNone(d.update("up", "open"))          # down unknown: say nothing
        self.assertEqual(d.update("down", "closed"), "down")

    def test_normal_cycle_publishes_each_state_once(self):
        d = garage()
        self.assertEqual(published(d, ("up", "open"), ("down", "closed"), ("down", "open"), ("up", "closed"),
                                   ("up", "open"), ("down", "closed")),
                         ["down", "opening", "up", "closing", "down"])

    def test_direction_comes_from_where_the_door_last_rested(self):
        d = garage()
        published(d, ("up", "open"), ("down", "closed"))          # at rest: down
        self.assertEqual(published(d, ("down", "open")), ["opening"])      # left down, so it is going up
        self.assertEqual(published(d, ("up", "closed")), ["up"])
        self.assertEqual(published(d, ("up", "open")), ["closing"])        # left up, so it is going down
        self.assertEqual(published(d, ("down", "closed")), ["down"])

    def test_without_history_the_direction_is_unknown(self):
        d = garage()                                                        # the monitor starts with the door mid-travel
        self.assertEqual(published(d, ("up", "open"), ("down", "open")), ["neither"])
        self.assertEqual(published(d, ("up", "closed")), ["up"])           # once it reaches an end it knows
        self.assertEqual(published(d, ("up", "open")), ["closing"])

    def test_a_reversal_mid_travel_returns_to_the_same_end(self):
        d = garage()
        published(d, ("up", "closed"), ("down", "open"))                   # up
        self.assertEqual(published(d, ("up", "open"), ("up", "closed")), ["closing", "up"])   # started closing, backed out

    def test_a_blip_in_between_does_not_lose_the_direction(self):
        d = garage()
        published(d, ("up", "open"), ("down", "closed"))                   # down
        published(d, ("down", "open"))                                      # opening
        cand = d.update("down", "closed")                                  # contact flickers: candidate is "down" again
        self.assertEqual(cand, "down")
        self.assertIsNone(d.update("down", "open"))                        # flicker over before it settled: nothing changes
        self.assertFalse(d.settled("down"))                                # the timer for "down" finds it no longer holds
        self.assertEqual(d.published, "opening")
        self.assertEqual(d.last_end, "down")
        d.update("up", "closed")
        d.update("down", "closed")                                         # both closed: "unknown", passing
        d.update("down", "open")
        self.assertEqual(d.compute(), "up")                                # up only: a real end position

    def test_unknown_in_the_middle_keeps_the_last_end(self):
        d = garage()
        published(d, ("up", "closed"), ("down", "open"))                   # up
        published(d, ("up", "open"))                                       # closing
        published(d, ("up", "closed"), ("down", "closed"))                 # reaches up, then both closed (a fault): unknown
        self.assertEqual(d.published, "unknown")
        d.update("up", "open")                                             # one contact lets go: "down" for an instant...
        cand = d.update("down", "open")                                    # ...then the other: both open, nothing settled between
        self.assertEqual(cand, "closing")                                  # unknown did not wipe the last end ("up")
        self.assertTrue(d.settled("closing"))

    def test_brief_overlap_of_both_switches_is_not_published(self):
        d = garage()
        published(d, ("up", "open"), ("down", "closed"))
        cand = d.update("up", "closed")                    # both closed for a moment
        self.assertEqual(cand, "unknown")
        self.assertIsNone(d.update("up", "open"))          # back to down before settle_s passed
        self.assertFalse(d.settled("unknown"))             # the timer finds the candidate has changed
        self.assertEqual(d.published, "down")


class WashingMachineStates(unittest.TestCase):
    def test_lid(self):
        d = WashingMachine("wm", "lid")
        self.assertIsNone(d.compute())
        self.assertEqual(published(d, ("lid", "closed"), ("lid", "open"), ("lid", "closed")), ["up", "down", "up"])


class DoorClosingFailure(unittest.TestCase):
    def rule(self):
        return DoorClosingFailureRule("fail", "door", escalation=Escalation(0, 1, 1, 30))

    def feed(self, rule, *states, start=0.0):
        events, t = [], start
        for st in states:
            events += rule.on_state(st, t)
            t += 10
        return events

    def test_normal_close_and_open_do_not_trigger(self):
        r = self.rule()
        self.assertEqual(self.feed(r, "down", "opening", "up", "closing", "down"), [])
        self.assertEqual(r.state, S.OK)

    def test_reversal_triggers(self):
        r = self.rule()
        events = self.feed(r, "up", "closing", "up")
        self.assertEqual([e.new for e in events], [S.WATCH])
        self.assertEqual(r.tick(21)[0].new, S.ERROR)

    def test_reaching_down_clears_it(self):
        r = self.rule()
        self.feed(r, "up", "closing", "up")
        e = self.feed(r, "closing", start=100)
        self.assertTrue(e[0].is_recovery)
        self.assertEqual(self.feed(r, "down", "opening", "up", start=200), [])   # normal opening afterwards

    def test_repeated_reversals(self):
        r = self.rule()
        self.feed(r, "up", "closing", "up", "closing", "up")
        self.assertNotEqual(r.state, S.OK)

    def test_opening_never_counts_as_a_failed_close(self):
        r = self.rule()
        self.assertEqual(self.feed(r, "down", "opening", "down", "opening", "up"), [])   # a failed OPEN is not this alert

    def test_a_door_that_left_up_with_unknown_direction_still_counts(self):
        r = self.rule()
        events = self.feed(r, "up", "neither", "up")
        self.assertEqual([e.new for e in events], [S.WATCH])

    def test_reminders_every_retry_interval(self):
        r = self.rule()
        self.feed(r, "up", "closing", "up", start=0)      # triggered at t=20
        r.tick(21)                                        # -> ERROR
        self.assertEqual(r.tick(50), [])                  # entered ERROR at 21; retry is 30 s
        self.assertEqual(r.tick(51)[0].repeat, 1)


if __name__ == "__main__":
    unittest.main()
