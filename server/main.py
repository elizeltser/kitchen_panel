from contextlib import asynccontextmanager

from fastapi import FastAPI, Request, Response

import state
from scheduler import start_scheduler, stop_scheduler
from admin.routes import router as admin_router


@asynccontextmanager
async def lifespan(app: FastAPI):
    await state.do_render()
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
