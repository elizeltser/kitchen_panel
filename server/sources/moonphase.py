import ephem


def get_moonphase() -> float:
    """Return moon phase as 0.0 (new) → 0.5 (full) → 1.0 (new again)."""
    ephem_now = ephem.now()
    prev_new = ephem.previous_new_moon(ephem_now)
    next_new = ephem.next_new_moon(ephem_now)
    cycle_length = float(next_new - prev_new)
    days_into_cycle = float(ephem_now - prev_new)
    return days_into_cycle / cycle_length
