"""
APScheduler — periodic data fetches and render jobs.
"""
import asyncio
import logging

from apscheduler.schedulers.asyncio import AsyncIOScheduler
from apscheduler.triggers.cron import CronTrigger

import state

log = logging.getLogger(__name__)
_sched: AsyncIOScheduler | None = None


async def _fetch_weather():
    from sources.weather import get_today
    try:
        state.weather_data = await get_today()
        state.source_statuses["weather"] = "ok"
    except Exception as e:
        log.error("Weather fetch failed: %s", e)
        state.source_statuses["weather"] = f"error: {e}"


async def _fetch_calendar():
    from sources.calendar_src import get_birthdays_today
    try:
        state.calendar_data = await get_birthdays_today()
        state.source_statuses["calendar"] = "ok"
    except Exception as e:
        log.error("Calendar fetch failed: %s", e)
        state.source_statuses["calendar"] = f"error: {e}"
        state.calendar_data = []


async def _fetch_stocks():
    from sources.stocks import get_prices
    try:
        state.stock_data = await get_prices()
        state.source_statuses["stocks"] = "ok"
    except Exception as e:
        log.error("Stock fetch failed: %s", e)
        state.source_statuses["stocks"] = f"error: {e}"


async def _select_quote():
    from sources.quote import get_today
    try:
        state.quote_data = get_today()
        state.source_statuses["quote"] = "ok"
    except Exception as e:
        log.error("Quote selection failed: %s", e)
        state.source_statuses["quote"] = f"error: {e}"


async def _morning_fetch():
    """Run all source fetches concurrently, then re-render."""
    await asyncio.gather(
        _fetch_weather(),
        _fetch_calendar(),
        _fetch_stocks(),
        _select_quote(),
    )
    await state.do_render()


def start_scheduler():
    global _sched
    _sched = AsyncIOScheduler()
    # Morning: fetch all sources at 05:45
    _sched.add_job(_morning_fetch, CronTrigger(hour=5, minute=45), id="morning")
    # Evening re-fetches
    _sched.add_job(_fetch_calendar, CronTrigger(hour=18, minute=0), id="eve_cal")
    _sched.add_job(_fetch_stocks, CronTrigger(hour=17, minute=30), id="eve_stocks")
    # Clock tick — re-render every 60s so the time updates
    _sched.add_job(state.do_render, "interval", seconds=60, id="tick")
    _sched.start()
    log.info("Scheduler started")


def stop_scheduler():
    if _sched:
        _sched.shutdown(wait=False)
