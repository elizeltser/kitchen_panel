"""
Shared in-memory state — updated by scheduler, read by HTTP handlers.
All fields are module-level globals; safe for single-process async use.
"""
from datetime import datetime
from typing import Optional

# Display — page 0 (main dashboard)
png_bytes: bytes = b""
etag: str = ""
last_render_time: Optional[str] = None

# Display — page 1 (calendar month view)
calendar_png_bytes: bytes = b""
calendar_etag: str = ""

# Cached source data (populated by scheduler jobs)
weather_data: Optional[dict] = None
calendar_data: list = []
calendar_month_data: list = []  # all events for the current month
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


async def do_render_calendar():
    """Compose the calendar month-view PNG and store in module globals."""
    global calendar_png_bytes, calendar_etag
    from render_calendar import compose_calendar
    calendar_png_bytes, calendar_etag = compose_calendar(calendar_month_data)
