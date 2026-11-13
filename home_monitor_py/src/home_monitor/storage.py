"""SQLite storage for readings, switch states and alert events (replaces CSV/PHP logs)."""
from __future__ import annotations

import sqlite3
import threading
from pathlib import Path

from .alerts import AlertEvent

SCHEMA = """
CREATE TABLE IF NOT EXISTS readings (ts REAL NOT NULL, source TEXT NOT NULL, value REAL NOT NULL);
CREATE INDEX IF NOT EXISTS idx_readings ON readings (source, ts);
CREATE TABLE IF NOT EXISTS states (ts REAL NOT NULL, source TEXT NOT NULL, state TEXT NOT NULL);
CREATE INDEX IF NOT EXISTS idx_states ON states (source, ts);
CREATE TABLE IF NOT EXISTS events (ts REAL NOT NULL, rule TEXT NOT NULL, source TEXT NOT NULL,
    old TEXT NOT NULL, new TEXT NOT NULL, repeat INTEGER NOT NULL, notified INTEGER NOT NULL,
    muted INTEGER NOT NULL, detail TEXT NOT NULL);
"""


class Store:
    def __init__(self, path: str | Path) -> None:
        if str(path) != ":memory:":
            Path(path).parent.mkdir(parents=True, exist_ok=True)
        self._db = sqlite3.connect(str(path), check_same_thread=False)
        self._lock = threading.Lock()
        with self._lock:
            self._db.execute("PRAGMA journal_mode=WAL")
            self._db.executescript(SCHEMA)

    def _run(self, sql: str, args: tuple = ()) -> None:
        with self._lock:
            self._db.execute(sql, args)
            self._db.commit()

    def _all(self, sql: str, args: tuple = ()) -> list[tuple]:
        with self._lock:
            return self._db.execute(sql, args).fetchall()

    def add_reading(self, ts: float, source: str, value: float) -> None:
        self._run("INSERT INTO readings VALUES (?,?,?)", (ts, source, value))

    def add_state(self, ts: float, source: str, state: str) -> None:
        self._run("INSERT INTO states VALUES (?,?,?)", (ts, source, state))

    def add_event(self, ts: float, e: AlertEvent) -> None:
        self._run("INSERT INTO events VALUES (?,?,?,?,?,?,?,?,?)",
                  (ts, e.rule, e.source, e.old.value, e.new.value, e.repeat,
                   int(e.notify), int(e.muted), e.detail))

    def stats(self, source: str, since: float) -> dict | None:
        row = self._all("SELECT MIN(value), MAX(value), AVG(value), COUNT(*) FROM readings "
                        "WHERE source=? AND ts>=?", (source, since))[0]
        if not row[3]:
            return None
        last = self._all("SELECT value FROM readings WHERE source=? ORDER BY ts DESC LIMIT 1", (source,))
        return {"min": row[0], "max": row[1], "avg": row[2], "n": row[3], "last": last[0][0]}

    def hourly(self, source: str, since: float) -> list[tuple]:
        """(hour_start_epoch, min, max, avg) per hour, newest first."""
        return self._all("SELECT CAST(ts/3600 AS INTEGER)*3600, MIN(value), MAX(value), AVG(value) "
                         "FROM readings WHERE source=? AND ts>=? GROUP BY 1 ORDER BY 1 DESC", (source, since))

    def recent_events(self, since: float, limit: int = 50) -> list[tuple]:
        return self._all("SELECT ts, rule, old, new, repeat, notified, muted, detail FROM events "
                         "WHERE ts>=? ORDER BY ts DESC LIMIT ?", (since, limit))

    def prune(self, before: float) -> None:
        for table in ("readings", "states", "events"):
            self._run(f"DELETE FROM {table} WHERE ts<?", (before,))

    def close(self) -> None:
        with self._lock:
            self._db.close()
