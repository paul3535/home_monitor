from __future__ import annotations

import asyncio
import logging

from ..alerts import AlertEvent
from .base import Channel, Notification, RateLimited
from .log import LogChannel
from .registry import build_channels

log = logging.getLogger(__name__)


class Dispatcher:
    """Sends each notification to every channel that accepts it, all at once, so a slow or broken
    channel (SMTP down) never delays the others (an ntfy push)."""
    RETRY_DELAYS = (0, 10, 60)  # a failed send must not lose an alert

    def __init__(self, channels: list) -> None:
        self.channels = list(channels)

    async def send(self, n: Notification) -> None:
        await asyncio.gather(*(self._deliver(c, n) for c in self.channels if c.accepts(n)))

    async def _deliver(self, channel, n: Notification) -> None:
        for attempt, delay in enumerate(self.RETRY_DELAYS, 1):
            if delay:
                await asyncio.sleep(delay)
            try:
                log.debug("%s: %s", channel.label, await channel.send(n))
                return
            except RateLimited as exc:
                log.warning("%s", exc)
                return
            except Exception as exc:  # noqa: BLE001 - network and SMTP errors are many
                log.error("%s failed (attempt %d/%d): %s", channel.label, attempt, len(self.RETRY_DELAYS), exc)
        log.error("%s: giving up on: %s", channel.label, n.subject)

    async def send_once(self, n: Notification) -> list[str]:
        """Send without retries and report what happened per channel (for the panel's test buttons)."""
        return list(await asyncio.gather(*(self._once(c, n) for c in self.channels)))

    @staticmethod
    async def _once(channel, n: Notification) -> str:
        if not channel.accepts(n):
            return f"{channel.label}: not used for {n.kind} messages"
        try:
            return f"{channel.label}: {await channel.send(n)}"
        except RateLimited as exc:
            return f"{channel.label}: {exc}"
        except Exception as exc:  # noqa: BLE001
            return f"{channel.label}: FAILED: {exc}"


def build_dispatcher(cfg) -> Dispatcher:
    channels: list = [LogChannel(), *build_channels(cfg.channels)]
    if len(channels) == 1:
        log.warning("no notification channels enabled: alerts only go to the log")
    return Dispatcher(channels)


def format_event(system: str, e: AlertEvent, local_time: str) -> tuple[str, str]:
    if e.is_recovery:
        subject = f"[{system}] {e.rule}: back to normal (was {e.old.value.upper()})"
    elif e.repeat:
        subject = f"[{system}] {e.rule}: still {e.new.value.upper()} (reminder {e.repeat})"
    else:
        subject = f"[{system}] {e.rule}: {e.old.value.upper()} -> {e.new.value.upper()}"
    return subject, f"{e.detail}\nTime: {local_time}\n"


def event_notification(system: str, e: AlertEvent, local_time: str, recipients) -> Notification:
    subject, body = format_event(system, e, local_time)
    return Notification("event", subject, body, tuple(recipients), level=e.level.value,
                        recovery=e.is_recovery, rule=e.rule, source=e.source, repeat=e.repeat)


def message_notification(kind: str, subject: str, body: str, recipients) -> Notification:
    return Notification(kind, subject, body, tuple(recipients))
