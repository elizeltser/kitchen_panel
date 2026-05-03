"""
Shared in-memory state — updated by scheduler, read by HTTP handlers.
All fields are module-level globals; safe for single-process async use.
"""
from datetime import datetime
from typing import Optional

# Display
png_bytes: bytes = b""
etag: str = ""
last_render_time: Optional[str] = None

# Cached source data (populated by scheduler jobs)
weather_data: Optional[dict] = None
calendar_data: list = []
stock_data: Optional[dict] = None
quote_data: Optional[dict] = None
moon_phase: Optional[float] = None

# Health
source_statuses: dict = {
    "weather": "pending",
    "calendar": "pending",
    "stocks": "pending",
    "quote": "pending",
    "moon": "pending",
}


async def do_render():
    """Compose a new PNG from current cached data and store in module globals."""
    global png_bytes, etag, last_render_time
    from renderer import compose
    png_bytes, etag = compose(
        weather=weather_data,
        birthdays=calendar_data,
        stocks=stock_data,
        quote=quote_data,
        moon_phase=moon_phase,
    )
    last_render_time = datetime.now().isoformat()
