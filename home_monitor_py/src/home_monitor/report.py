"""Plain-text status report built from the database (replaces the PHP/HTML status page)."""
from __future__ import annotations

from datetime import datetime

from .storage import Store


def _fmt(ts: float) -> str:
    return datetime.fromtimestamp(ts).strftime("%Y-%m-%d %H:%M")


def build_report(store: Store, system: str, sensors: list[str], now: float, hours: int = 24) -> str:
    since = now - hours * 3600
    out = [f"{system} status at {_fmt(now)}", "", f"Temperatures, last {hours} h:"]
    for name in sensors:
        s = store.stats(name, since)
        if s is None:
            out.append(f"  {name}: NO READINGS")
            continue
        out.append(f"  {name}: now {s['last']:.2f} C, min {s['min']:.2f}, max {s['max']:.2f}, "
                   f"avg {s['avg']:.2f} ({s['n']} samples)")
    out += ["", "Hourly min / max:"]
    for name in sensors:
        out.append(f"  {name}")
        for hour, lo, hi, _avg in store.hourly(name, since)[:hours]:
            out.append(f"    {_fmt(hour)}  {lo:7.2f}  {hi:7.2f}")
    events = [e for e in store.recent_events(since) if e[3] != "pending"]
    out += ["", f"Alert events, last {hours} h: {len(events) or 'none'}"]
    for ts, rule, old, new, repeat, notified, muted, detail in events:
        tag = " (muted)" if muted else ""
        again = f" reminder {repeat}" if repeat else ""
        out.append(f"  {_fmt(ts)}  {rule}: {old} -> {new}{again}{tag}  {detail}")
    return "\n".join(out) + "\n"
