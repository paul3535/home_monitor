"""Plain data types shared by the sensors, alert engine and storage."""
from __future__ import annotations

from dataclasses import dataclass
from enum import Enum


class AlertState(str, Enum):
    OK = "ok"
    PENDING = "pending"  # condition is true, but not yet for watch_s
    WATCH = "watch"
    WARN = "warn"
    ERROR = "error"


@dataclass(frozen=True)
class Reading:
    """A numeric sample (temperature). ts is wall-clock epoch seconds."""
    source: str
    value: float
    ts: float


@dataclass(frozen=True)
class StateChange:
    """A discrete state change (a reed switch going open/closed)."""
    source: str
    state: str
    ts: float
