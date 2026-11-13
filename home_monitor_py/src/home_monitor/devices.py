"""Devices whose state is derived from one or more switches.

Switch states are "closed" / "open". A reed switch is "closed" while the magnet is near
(pin pulled LOW), which the C++ code called ON.

Derived states (lower-case versions of the C++ ones):
  garage_door:     up | down | neither | unknown
                   up = only the up switch is closed, down = only the down switch,
                   neither = door in transit, unknown = both closed (should never happen)
  washing_machine: up | down   (lid_up switch closed = lid up)

Settling: with interrupts every edge is seen, including the few milliseconds when two
switches are briefly out of step. A derived state is therefore only published once it has
held for ``settle_s`` (default 1 s); the polling C++ code never saw such blips.
"""
from __future__ import annotations


class DerivedDevice:
    kind = "device"

    def __init__(self, name: str, inputs: list[str], settle_s: float = 1.0) -> None:
        self.name = name
        self.inputs = inputs
        self.settle_s = settle_s
        self.published: str | None = None   # last state sent to the alert engine
        self.candidate: str | None = None   # what the switches say right now
        self._closed: dict[str, bool] = {}

    def compute(self) -> str | None:
        """The derived state, or None until every input has reported."""
        raise NotImplementedError

    def update(self, source: str, state: str) -> str | None:
        """Feed a switch change. Returns a candidate state to publish after settling, or None."""
        self._closed[source] = state == "closed"
        self.candidate = self.compute()
        if self.candidate is None or self.candidate == self.published:
            return None
        return self.candidate

    def settled(self, candidate: str) -> bool:
        """Called ``settle_s`` after update(); True if the candidate held and should be published."""
        if self.candidate != candidate or candidate == self.published:
            return False
        self.published = candidate
        return True


class GarageDoor(DerivedDevice):
    kind = "garage_door"

    def __init__(self, name: str, up: str, down: str, settle_s: float = 1.0) -> None:
        super().__init__(name, [up, down], settle_s)
        self.up, self.down = up, down

    def compute(self) -> str | None:
        if self.up not in self._closed or self.down not in self._closed:
            return None
        up, down = self._closed[self.up], self._closed[self.down]
        if up and not down:
            return "up"
        if down and not up:
            return "down"
        if not up and not down:
            return "neither"
        return "unknown"


class WashingMachine(DerivedDevice):
    kind = "washing_machine"

    def __init__(self, name: str, lid_up: str, settle_s: float = 1.0) -> None:
        super().__init__(name, [lid_up], settle_s)
        self.lid_up = lid_up

    def compute(self) -> str | None:
        if self.lid_up not in self._closed:
            return None
        return "up" if self._closed[self.lid_up] else "down"
