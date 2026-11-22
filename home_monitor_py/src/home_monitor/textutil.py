"""Small text helpers shared by the alert engine, the commands and the notifications."""
from __future__ import annotations


def fmt_duration(seconds: float) -> str:
    """45 -> '45s', 5400 -> '1h 30m', 90000 -> '1d 1h'. Seconds are dropped once it is a minute or more."""
    seconds = int(round(seconds))
    if seconds < 60:
        return f"{seconds}s"
    days, rest = divmod(seconds, 86400)
    hours, rest = divmod(rest, 3600)
    minutes = rest // 60
    return " ".join(f"{n}{u}" for n, u in ((days, "d"), (hours, "h"), (minutes, "m")) if n) or "0m"
