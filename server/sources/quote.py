"""
Date-seeded quote of the day from data/quotes.json.
Same quote all day; changes each morning automatically.
"""
import json
import logging
import random
from datetime import date
from pathlib import Path

log = logging.getLogger(__name__)

DATA = Path(__file__).parent.parent / "data" / "quotes.json"

FALLBACK = {
    "quote": "Not all those who wander are lost.",
    "attribution": "J.R.R. Tolkien",
}


def get_today() -> dict:
    try:
        quotes = json.loads(DATA.read_text(encoding="utf-8"))
        if not quotes:
            return FALLBACK
        seed = int(date.today().strftime("%Y%m%d"))
        rng = random.Random(seed)
        return rng.choice(quotes)
    except Exception as e:
        log.warning("Quote load failed: %s", e)
        return FALLBACK
