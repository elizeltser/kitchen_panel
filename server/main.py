from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI, HTTPException, Request, Response
from fastapi.responses import FileResponse

import state
from scheduler import start_scheduler, stop_scheduler
from admin.routes import router as admin_router

SCREENSAVERS_DIR = Path(__file__).parent / "data" / "screensavers"


@asynccontextmanager
async def lifespan(app: FastAPI):
    from sources.moonphase import get_moonphase
    from sources.calendar_src import get_month_events
    try:
        state.moon_phase = get_moonphase()
        state.source_statuses["moon"] = "ok"
    except Exception:
        pass
    try:
        state.calendar_month_data = await get_month_events()
    except Exception:
        pass
    await state.do_render()
    await state.do_render_calendar()
    start_scheduler()
    yield
    stop_scheduler()


app = FastAPI(lifespan=lifespan)
app.include_router(admin_router)


@app.get("/display.png")
async def display_png(request: Request):
    if_none_match = request.headers.get("if-none-match", "")
    if if_none_match == state.etag and state.etag:
        return Response(status_code=304)
    return Response(
        content=state.png_bytes,
        media_type="image/png",
        headers={"ETag": state.etag, "Cache-Control": "no-cache"},
    )


@app.get("/status")
async def get_status():
    return {
        "etag": state.etag,
        "last_render": state.last_render_time,
        "sources": state.source_statuses,
    }


@app.post("/refresh")
async def force_refresh():
    await state.do_render()
    return {"status": "ok", "etag": state.etag}


@app.get("/display/{n}")
async def display_n(n: int, request: Request):
    if n == 0:
        png, etag = state.png_bytes, state.etag
    elif n == 1:
        png, etag = state.calendar_png_bytes, state.calendar_etag
        if not png:
            raise HTTPException(status_code=503, detail="Calendar not yet rendered")
    else:
        raise HTTPException(status_code=404, detail="Buffer not found")
    if_none_match = request.headers.get("if-none-match", "")
    if if_none_match == etag and etag:
        return Response(status_code=304)
    return Response(
        content=png,
        media_type="image/png",
        headers={"ETag": etag, "Cache-Control": "no-cache"},
    )


@app.get("/screensaver/count")
async def screensaver_count():
    SCREENSAVERS_DIR.mkdir(parents=True, exist_ok=True)
    count = len(sorted(SCREENSAVERS_DIR.glob("*.png")))
    return {"count": count}


@app.get("/screensaver/{n}")
async def get_screensaver(n: int):
    path = SCREENSAVERS_DIR / f"{n}.png"
    if not path.exists():
        raise HTTPException(status_code=404, detail="Screensaver not found")
    return FileResponse(str(path), media_type="image/png")
