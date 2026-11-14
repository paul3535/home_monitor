"""Channel types you can use under [notify.channels.<name>]. To add one: write the class and list it here."""
from __future__ import annotations

from .base import Channel
from .email import EmailChannel
from .ntfy import NtfyChannel

CHANNEL_TYPES: dict[str, type[Channel]] = {"email": EmailChannel, "ntfy": NtfyChannel}


def build_channels(channels: dict[str, dict]) -> list[Channel]:
    """Create the enabled channels from the [notify.channels.*] tables. ``type`` defaults to the table
    name, so [notify.channels.email] is an email channel. Raises ValueError with the channel's name."""
    built: list[Channel] = []
    for name, options in channels.items():
        if not options.get("enabled", True):
            continue
        kind = options.get("type", name)
        cls = CHANNEL_TYPES.get(kind)
        if cls is None:
            raise ValueError(f"channel {name!r}: unknown type {kind!r}; use one of {sorted(CHANNEL_TYPES)}")
        try:
            built.append(cls(name, {k: v for k, v in options.items() if k not in ("type", "enabled")}))
        except (ValueError, TypeError) as exc:
            raise ValueError(f"channel {name!r}: {exc}") from exc
    return built
