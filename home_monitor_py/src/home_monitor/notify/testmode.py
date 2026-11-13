from __future__ import annotations

import collections
import re
import time
from dataclasses import replace

from .base import Contact, Notification, RateLimited

TOPIC = re.compile(r"^[A-Za-z0-9_-]{1,64}$")


def valid_test_target(target: str) -> bool:
    """A --fake-send-to target is an email address or an ntfy topic name."""
    return "@" in target or bool(TOPIC.match(target))


class FakeModeChannel:
    """Wraps a real channel for --fake-pi so test alerts can be sent safely.

    * every message is marked "[FAKE-PI TEST]"
    * ``redirect`` (email addresses and/or ntfy topics) replaces the recipients; the body names who would
      really have been told. A channel with no matching target (ntfy, when you gave only emails) sends nothing.
    * at most ``max_per_minute`` messages go out, so --time-scale cannot flood an inbox or a phone
    """

    def __init__(self, inner, redirect: list[str] | None = None, max_per_minute: int = 10) -> None:
        self.inner, self.redirect, self.max_per_minute = inner, redirect, max_per_minute
        self.kind, self.name = inner.kind, inner.name
        if getattr(inner, "command_topic", None):
            # A fake alert's buttons must not reach the real monitor, which would act on a real alert of
            # the same name. The fake Pi listens on its own topic instead.
            inner.command_topic = f"{inner.command_topic}-fake"
        self._sent: collections.deque[float] = collections.deque()
        if redirect is None:
            self.label = f"{inner.label} to the REAL recipients"
        else:
            mine = [t for t in redirect if ("@" in t) == (inner.kind == "email")] if inner.kind in ("email", "ntfy") else []
            self.label = f"{inner.label} -> {', '.join(mine) or '(no matching test target)'}"

    def accepts(self, n: Notification) -> bool:
        return self.inner.accepts(n)

    async def send(self, n: Notification) -> str:
        if self.redirect is not None:
            real = ", ".join(c.name for c in n.recipients) or "(nobody)"
            targets = tuple(Contact("test", {"email": t} if "@" in t else {"ntfy": t}) for t in self.redirect)
            n = replace(n, recipients=targets, body=n.body + f"\n--\nFake-Pi test. The real recipients would have been: {real}\n")
        n = replace(n, subject=f"[FAKE-PI TEST] {n.subject}")
        if not self.inner.destinations(n):
            return await self.inner.send(n)   # nothing to deliver to: no rate-limit cost
        now = time.monotonic()
        while self._sent and now - self._sent[0] > 60:
            self._sent.popleft()
        if len(self._sent) >= self.max_per_minute:
            raise RateLimited(f"fake-pi limit of {self.max_per_minute} messages a minute reached: not sent: {n.subject}")
        self._sent.append(now)
        return await self.inner.send(n)
