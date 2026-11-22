"""Escalating alert rules.

This keeps the model from the C++ version: once a condition becomes true, the rule
escalates by how long it has been true (PENDING -> WATCH -> WARN -> ERROR), repeats
ERROR notifications every ``error_retry_s``, and sends a recovery notice when the
condition clears.

Differences from the C++ version:
  * Rules are fed by events (a GPIO edge, a new reading) AND by a periodic tick(), so
    escalation timing no longer depends on a polling loop.
  * All timing uses a monotonic clock supplied by the caller (``now``), so a clock
    jump at boot (NTP sync on a Pi without an RTC) cannot trigger false escalations.
  * A StaleRule alerts when a sensor stops reporting, so a dead sensor is never silent.
"""
from __future__ import annotations

import operator
from dataclasses import dataclass

from .models import AlertState
from .textutil import fmt_duration

_OPS = {
    "greater_than": operator.gt,
    "greater_equal": operator.ge,
    "less_than": operator.lt,
    "less_equal": operator.le,
    "equal": operator.eq,
    "not_equal": operator.ne,
}


@dataclass(frozen=True)
class Escalation:
    watch_s: float = 0.0
    warn_s: float = 300.0
    error_s: float = 600.0
    error_retry_s: float = 300.0  # 0 disables repeat notifications
    notify_on: frozenset = frozenset({AlertState.WARN, AlertState.ERROR})

    def __post_init__(self) -> None:
        if not (0 <= self.watch_s <= self.warn_s <= self.error_s):
            raise ValueError("need 0 <= watch_s <= warn_s <= error_s")

    def state_for(self, elapsed: float) -> AlertState:
        if elapsed >= self.error_s:
            return AlertState.ERROR
        if elapsed >= self.warn_s:
            return AlertState.WARN
        if elapsed >= self.watch_s:
            return AlertState.WATCH
        return AlertState.PENDING


@dataclass(frozen=True)
class AlertEvent:
    rule: str
    source: str
    old: AlertState
    new: AlertState
    detail: str
    repeat: int = 0       # >0: "still in error" reminder number
    notify: bool = False  # should someone be told?
    muted: bool = False   # would have notified, but the rule is muted
    claimed_by: str | None = None   # who has taken this alert; messages about it go only to them
    label: str = ""       # the alert's display_name (or its name): what people read
    elapsed_s: float = 0.0   # how long the problem has been going on (for a recovery: how long it lasted)
    message: str = ""        # the alert's own wording, already filled in (empty = the default text)

    @property
    def is_recovery(self) -> bool:
        return self.new is AlertState.OK

    @property
    def level(self) -> AlertState:
        """The level whose recipients should hear about this (old level on recovery)."""
        return self.old if self.is_recovery else self.new


class AlertRule:
    def __init__(self, name: str, source: str, escalation: Escalation | None = None,
                 recipients: dict[str, list[str]] | None = None, claim: tuple = (), always_notify: tuple = (),
                 recovery_to: tuple = (), mute_scope: str = "alert", display_name: str | None = None,
                 source_label: str | None = None, message: str | None = None) -> None:
        if mute_scope not in ("alert", "person"):
            raise ValueError("mute_scope must be \"alert\" or \"person\"")
        self.name = name
        self.source = source
        self.display_name = display_name      # for people; the name stays the id used by commands and buttons
        self.source_label = source_label      # the source's display_name, used in the detail text
        self.message = message                # optional wording with {duration} and {source}, e.g. "The laundry has been waiting {duration}"
        if message:
            try:
                message.format(duration="1m", source="x")
            except (KeyError, IndexError, ValueError) as exc:
                raise ValueError(f"message may only use {{duration}} and {{source}} (and {{{{ }}}} for braces): {exc!r}") from exc
        self.escalation = escalation or Escalation()
        self.recipients = recipients  # optional per-level override of contact names
        self.claim = tuple(claim)     # people who may take this alert ("I'll do it"); empty = not claimable
        self.always_notify = tuple(always_notify)   # told about every message on this alert, claimed or not
        self.recovery_to = tuple(recovery_to)       # also told when it is over (e.g. the whole household)
        self.mute_scope = mute_scope   # "alert": a mute silences everyone; "person": only the person who muted
        self.muted_for: dict[str, float] = {}      # person -> muted until (monotonic), for mute_scope "person"
        self.claimed_by: str | None = None
        self.state = AlertState.OK
        self.muted_until = 0.0
        self.detail = ""
        self._triggered = False
        self._since: float | None = None
        self._repeats = 0
        self._next_reminder: float | None = None

    @property
    def label(self) -> str:
        return self.display_name or self.name

    @property
    def source_text(self) -> str:
        return self.source_label or self.source

    # Inputs: subclasses override the ones they care about.
    def on_value(self, value: float, now: float) -> list[AlertEvent]:
        return []

    def on_state(self, state: str, now: float) -> list[AlertEvent]:
        return []

    def tick(self, now: float) -> list[AlertEvent]:
        return self._advance(now)

    def mute(self, now: float, seconds: float, who: str | None = None) -> None:
        if who and self.mute_scope == "person":
            self.muted_for[who] = now + seconds
        else:
            self.muted_until = now + seconds

    def unmute(self, who: str | None = None) -> None:
        if who and self.mute_scope == "person":
            self.muted_for.pop(who, None)
        else:                       # an alert-wide unmute clears every mute, including personal ones
            self.muted_until = 0.0
            self.muted_for.clear()

    def is_muted_for(self, who: str, now: float) -> bool:
        """A personal mute (mute_scope "person"). Alert-wide mutes are applied when the event is made."""
        return now < self.muted_for.get(who, 0.0)

    # Core state machine
    def _update(self, triggered: bool, detail: str, now: float) -> list[AlertEvent]:
        self._triggered = triggered
        self.detail = detail
        return self._advance(now)

    def _advance(self, now: float) -> list[AlertEvent]:
        esc = self.escalation
        if not self._triggered:
            if self._since is None:
                return []
            old, lasted = self.state, now - self._since
            self._since = None
            self._repeats = 0
            self._next_reminder = None
            self.state = AlertState.OK
            event = self._event(old, AlertState.OK, now, elapsed=lasted)   # still carries who had claimed it
            self.claimed_by = None                          # a new problem starts unclaimed
            return [event]

        if self._since is None:
            self._since = now
        elapsed = now - self._since
        target = esc.state_for(elapsed)
        if target is not self.state:
            old, self.state = self.state, target
            if target is AlertState.ERROR:
                self._repeats = 0
                self._next_reminder = now + esc.error_retry_s if esc.error_retry_s > 0 else None
            return [self._event(old, target, now, elapsed=elapsed)]
        if (target is AlertState.ERROR and self._next_reminder is not None
                and now >= self._next_reminder):
            self._repeats += 1
            self._next_reminder = now + esc.error_retry_s
            return [self._event(target, target, now, repeat=self._repeats, elapsed=elapsed)]
        return []

    def _event(self, old: AlertState, new: AlertState, now: float, repeat: int = 0, elapsed: float = 0.0) -> AlertEvent:
        recovery = new is AlertState.OK
        level = old if recovery else new
        notify = level in self.escalation.notify_on
        muted = False
        if notify and not recovery and now < self.muted_until:
            notify, muted = False, True  # recoveries are never muted, as before
        text = ""
        if self.message and not recovery:
            how_long = "a moment" if elapsed < 5 else fmt_duration(elapsed)   # reads well after "for" or "waiting"
            text = self.message.format(duration=how_long, source=self.source_text)
        return AlertEvent(self.name, self.source, old, new, self.detail, repeat, notify, muted, self.claimed_by, self.label,
                          elapsed, text)


class BoundsRule(AlertRule):
    """Triggered while a numeric reading is past a limit."""

    def __init__(self, name: str, source: str, op: str, value: float,
                 value2: float | None = None, **kw) -> None:
        super().__init__(name, source, **kw)
        if op == "outside":
            if value2 is None or value >= value2:
                raise ValueError("'outside' needs value < value2")
        elif op not in _OPS:
            raise ValueError(f"unknown op {op!r}; use one of {sorted(_OPS) + ['outside']}")
        self.op, self.value, self.value2 = op, value, value2

    def _test(self, v: float) -> bool:
        if self.op == "outside":
            return v < self.value or v > self.value2  # type: ignore[operator]
        return _OPS[self.op](v, self.value)

    def on_value(self, value: float, now: float) -> list[AlertEvent]:
        limit = f"{self.value}" if self.op != "outside" else f"{self.value}..{self.value2}"
        return self._update(self._test(value), f"{self.source_text} = {value:.2f} ({self.op} {limit})", now)


class StateRule(AlertRule):
    """Triggered while the source is in a given state, or in any of several (e.g. ["opening", "closing"],
    which catches a garage door that is stuck partway whichever way it was going)."""

    def __init__(self, name: str, source: str, state: str | list, **kw) -> None:
        super().__init__(name, source, **kw)
        self.targets = (state,) if isinstance(state, str) else tuple(state)
        if not self.targets or not all(isinstance(t, str) and t for t in self.targets):
            raise ValueError("state must be a name, or a list of names")

    def on_state(self, state: str, now: float) -> list[AlertEvent]:
        return self._update(state in self.targets, f"{self.source_text} is {state}", now)


class DoorClosingFailureRule(AlertRule):
    """The garage door started closing (up -> closing) but went back to up without reaching down.

    Triggered while the door sits at "up" after such an attempt; cleared when it reaches
    "down" or starts moving again. (The C++ version compared the door state with ON/OFF, which
    a garage door never reports, so as far as I can tell it could not fire.)
    """

    def __init__(self, name: str, source: str, **kw) -> None:
        super().__init__(name, source, **kw)
        self._prev: str | None = None
        self._closing = False

    def on_state(self, state: str, now: float) -> list[AlertEvent]:
        prev, self._prev = self._prev, state
        if state == "down":
            self._closing = False
        elif state == "closing" or (prev == "up" and state == "neither"):   # "neither": direction unknown
            self._closing = True
        failed = state == "up" and self._closing
        return self._update(failed, f"{self.source_text} started closing but went back up without reaching down", now)


class StaleRule(AlertRule):
    """Triggered when a sensor has not produced a reading for max_age_s."""

    def __init__(self, name: str, source: str, max_age_s: float, now: float, **kw) -> None:
        super().__init__(name, source, **kw)
        self.max_age_s = max_age_s
        self._last_seen = now

    def on_value(self, value: float, now: float) -> list[AlertEvent]:
        self._last_seen = now
        return self._update(False, f"{self.source_text} reporting", now)

    def tick(self, now: float) -> list[AlertEvent]:
        age = now - self._last_seen
        return self._update(age > self.max_age_s, f"no reading from {self.source_text} for {age:.0f}s", now)


class AlertEngine:
    def __init__(self, rules: list[AlertRule]) -> None:
        names = [r.name for r in rules]
        if len(names) != len(set(names)):
            raise ValueError("alert names must be unique")
        self.rules = rules

    def rule(self, name: str) -> AlertRule:
        return next(r for r in self.rules if r.name == name)

    def on_value(self, source: str, value: float, now: float) -> list[AlertEvent]:
        return [e for r in self.rules if r.source == source for e in r.on_value(value, now)]

    def on_state(self, source: str, state: str, now: float) -> list[AlertEvent]:
        return [e for r in self.rules if r.source == source for e in r.on_state(state, now)]

    def tick(self, now: float) -> list[AlertEvent]:
        return [e for r in self.rules for e in r.tick(now)]

    def mute(self, now: float, seconds: float, rule: str | None = None, who: str | None = None) -> int:
        targets = [r for r in self.rules if rule is None or r.name == rule]
        for r in targets:
            r.mute(now, seconds, who)
        return len(targets)

    def unmute(self, rule: str | None = None, who: str | None = None) -> int:
        targets = [r for r in self.rules if rule is None or r.name == rule]
        for r in targets:
            r.unmute(who)
        return len(targets)

    def snapshot(self, now: float) -> list[dict]:
        return [{"rule": r.name, "label": r.label, "source": r.source, "state": r.state.value, "detail": r.detail,
                 "muted": now < r.muted_until, "muted_for_s": max(0.0, r.muted_until - now),
                 "claimed_by": r.claimed_by,
                 "muted_people": {w: round(t - now) for w, t in r.muted_for.items() if t > now}} for r in self.rules]
