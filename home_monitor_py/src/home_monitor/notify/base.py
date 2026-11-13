"""The generic notification scheme.

    Alert engine --> Notification (what happened, who should hear) --> every enabled Channel

* A **Contact** is a person with an address per channel kind: {"email": "a@b.c", "ntfy": "topic"}.
* A **Channel** knows how to deliver to one kind of destination. It picks its own address out of
  each recipient, so adding a channel (Telegram, SMS, a webhook...) means one new class plus an
  optional key on contacts. Nothing else changes.
* Who is told (levels -> contacts) is decided by the config; how they are told is the channels' job.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import ClassVar

LEVELS = ("watch", "warn", "error")
KINDS = ("event", "report", "test", "startup")


class RateLimited(Exception):
    """Raised instead of sending when a test-mode channel hits its cap (never retried)."""


@dataclass(frozen=True)
class Contact:
    name: str
    addresses: dict  # channel kind -> destination, e.g. {"email": "a@b.c", "ntfy": "my-topic"}


@dataclass(frozen=True)
class Notification:
    kind: str                           # event | report | test | startup
    subject: str
    body: str
    recipients: tuple = ()              # of Contact
    level: str | None = None            # events: watch | warn | error (the old level for a recovery)
    recovery: bool = False
    rule: str = ""
    source: str = ""
    repeat: int = 0


class Channel:
    """Base class. Subclasses set ``kind``, ``option_names`` and implement ``send``."""
    kind: ClassVar[str] = "base"
    option_names: ClassVar[frozenset] = frozenset()
    default_kinds: ClassVar[tuple] = KINDS

    def __init__(self, name: str, options: dict) -> None:
        unknown = set(options) - self.option_names - {"levels", "kinds"}
        if unknown:
            allowed = sorted(self.option_names | {"levels", "kinds"})
            raise ValueError(f"unknown option(s) {sorted(unknown)}; allowed: {allowed}")
        self.name = name
        self.levels = frozenset(options.get("levels", LEVELS))
        self.kinds = frozenset(options.get("kinds", self.default_kinds))
        if not self.levels <= set(LEVELS):
            raise ValueError(f"levels must be among {list(LEVELS)}")
        if not self.kinds <= set(KINDS):
            raise ValueError(f"kinds must be among {list(KINDS)}")

    @property
    def label(self) -> str:
        return self.kind if self.name == self.kind else f"{self.kind} '{self.name}'"

    @property
    def summary(self) -> str:
        return self.label

    def accepts(self, n: Notification) -> bool:
        """Does this channel carry this message? (``levels`` only applies to alert events.)"""
        return n.kind in self.kinds and (n.kind != "event" or n.level in self.levels)

    def destinations(self, n: Notification) -> list[str]:
        """This channel's address for each recipient, without duplicates, in order."""
        out: list[str] = []
        for contact in n.recipients:
            dest = contact.addresses.get(self.kind)
            if dest and dest not in out:
                out.append(dest)
        return out

    async def send(self, n: Notification) -> str:
        """Deliver and return a short status ("sent to 2 addresses"). Raise on failure."""
        raise NotImplementedError
