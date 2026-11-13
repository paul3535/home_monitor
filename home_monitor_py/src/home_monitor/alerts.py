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

    @property
    def is_recovery(self) -> bool:
        return self.new is AlertState.OK

    @property
    def level(self) -> AlertState:
        """The level whose recipients should hear about this (old level on recovery)."""
        return self.old if self.is_recovery else self.new


class AlertRule:
    def __init__(self, name: str, source: str, escalation: Escalation | None = None,
                 recipients: dict[str, list[str]] | None = None) -> None:
        self.name = name
        self.source = source
        self.escalation = escalation or Escalation()
        self.recipients = recipients  # optional per-level override of contact names
        self.state = AlertState.OK
        self.muted_until = 0.0
        self.detail = ""
        self._triggered = False
        self._since: float | None = None
        self._repeats = 0
        self._next_reminder: float | None = None

    # Inputs: subclasses override the ones they care about.
    def on_value(self, value: float, now: float) -> list[AlertEvent]:
        return []

    def on_state(self, state: str, now: float) -> list[AlertEvent]:
        return []

    def tick(self, now: float) -> list[AlertEvent]:
        return self._advance(now)

    def mute(self, now: float, seconds: float) -> None:
        self.muted_until = now + seconds

    def unmute(self) -> None:
        self.muted_until = 0.0

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
            old = self.state
            self._since = None
            self._repeats = 0
            self._next_reminder = None
            self.state = AlertState.OK
            return [self._event(old, AlertState.OK, now)]

        if self._since is None:
            self._since = now
        target = esc.state_for(now - self._since)
        if target is not self.state:
            old, self.state = self.state, target
            if target is AlertState.ERROR:
                self._repeats = 0
                self._next_reminder = now + esc.error_retry_s if esc.error_retry_s > 0 else None
            return [self._event(old, target, now)]
        if (target is AlertState.ERROR and self._next_reminder is not None
                and now >= self._next_reminder):
            self._repeats += 1
            self._next_reminder = now + esc.error_retry_s
            return [self._event(target, target, now, repeat=self._repeats)]
        return []

    def _event(self, old: AlertState, new: AlertState, now: float, repeat: int = 0) -> AlertEvent:
        recovery = new is AlertState.OK
        level = old if recovery else new
        notify = level in self.escalation.notify_on
        muted = False
        if notify and not recovery and now < self.muted_until:
            notify, muted = False, True  # recoveries are never muted, as before
        return AlertEvent(self.name, self.source, old, new, self.detail, repeat, notify, muted)


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
        return self._update(self._test(value), f"{self.source} = {value:.2f} ({self.op} {limit})", now)


class StateRule(AlertRule):
    """Triggered while a switch is in a given state (e.g. 'open')."""

    def __init__(self, name: str, source: str, state: str, **kw) -> None:
        super().__init__(name, source, **kw)
        self.target = state

    def on_state(self, state: str, now: float) -> list[AlertEvent]:
        return self._update(state == self.target, f"{self.source} is {state}", now)


class DoorClosingFailureRule(AlertRule):
    """The garage door started closing (up -> neither) but went back to up without reaching down.

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
        elif prev == "up" and state == "neither":
            self._closing = True
        failed = state == "up" and self._closing
        return self._update(failed, f"{self.source} started closing but went back up without reaching down", now)


class StaleRule(AlertRule):
    """Triggered when a sensor has not produced a reading for max_age_s."""

    def __init__(self, name: str, source: str, max_age_s: float, now: float, **kw) -> None:
        super().__init__(name, source, **kw)
        self.max_age_s = max_age_s
        self._last_seen = now

    def on_value(self, value: float, now: float) -> list[AlertEvent]:
        self._last_seen = now
        return self._update(False, f"{self.source} reporting", now)

    def tick(self, now: float) -> list[AlertEvent]:
        age = now - self._last_seen
        return self._update(age > self.max_age_s, f"no reading from {self.source} for {age:.0f}s", now)


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

    def mute(self, now: float, seconds: float, rule: str | None = None) -> int:
        targets = [r for r in self.rules if rule is None or r.name == rule]
        for r in targets:
            r.mute(now, seconds)
        return len(targets)

    def unmute(self, rule: str | None = None) -> int:
        targets = [r for r in self.rules if rule is None or r.name == rule]
        for r in targets:
            r.unmute()
        return len(targets)

    def snapshot(self, now: float) -> list[dict]:
        return [{"rule": r.name, "source": r.source, "state": r.state.value, "detail": r.detail,
                 "muted": now < r.muted_until, "muted_for_s": max(0.0, r.muted_until - now)} for r in self.rules]
