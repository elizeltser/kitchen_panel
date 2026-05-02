"""
Admin panel — FastAPI routes for quote, ticker, and portfolio management.
"""
import json
from pathlib import Path

from fastapi import APIRouter, HTTPException, Request, UploadFile
from fastapi.responses import HTMLResponse
from fastapi.templating import Jinja2Templates
from pydantic import BaseModel

import state

DATA = Path(__file__).parent.parent / "data"
TEMPLATES = Path(__file__).parent / "templates"
templates = Jinja2Templates(directory=str(TEMPLATES))
router = APIRouter()


def _read_json(name: str):
    p = DATA / name
    if not p.exists():
        return [] if name != "portfolio.json" else {"holdings": [], "last_updated": ""}
    return json.loads(p.read_text(encoding="utf-8"))


def _write_json(name: str, data):
    (DATA / name).write_text(
        json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8"
    )


# ── Admin UI pages ────────────────────────────────────────────


@router.get("/admin", response_class=HTMLResponse)
async def admin_home(request: Request):
    return templates.TemplateResponse(
        request, "base.html", {"status": state.source_statuses}
    )


@router.get("/admin/quotes", response_class=HTMLResponse)
async def admin_quotes(request: Request):
    quotes = _read_json("quotes.json")
    return templates.TemplateResponse(
        request, "quotes.html",
        {
            "quotes": quotes,
            "today_quote": state.quote_data,
            "total": len(quotes),
        },
    )


@router.get("/admin/stocks", response_class=HTMLResponse)
async def admin_stocks(request: Request):
    tickers = _read_json("tickers.json")
    portfolio = _read_json("portfolio.json")
    prices = {
        t["symbol"]: t["price"]
        for t in (state.stock_data or {}).get("tickers", [])
    }
    return templates.TemplateResponse(
        request, "stocks.html",
        {
            "tickers": tickers,
            "portfolio": portfolio,
            "prices": prices,
        },
    )


# ── Refetch API ───────────────────────────────────────────────

_FETCHERS = {
    "weather":  "scheduler._fetch_weather",
    "calendar": "scheduler._fetch_calendar",
    "stocks":   "scheduler._fetch_stocks",
    "quote":    "scheduler._select_quote",
}


@router.post("/api/refetch/{source}")
async def api_refetch(source: str):
    if source not in _FETCHERS:
        raise HTTPException(404, f"Unknown source: {source}")
    import importlib
    mod_name, fn_name = _FETCHERS[source].rsplit(".", 1)
    mod = importlib.import_module(mod_name)
    await getattr(mod, fn_name)()
    await state.do_render()
    return {"ok": True, "source": source, "status": state.source_statuses[source]}


# ── Quote API ─────────────────────────────────────────────────


@router.get("/api/quotes")
async def api_get_quotes():
    return _read_json("quotes.json")


class QuoteIn(BaseModel):
    quote: str
    attribution: str


@router.post("/api/quotes")
async def api_add_quote(q: QuoteIn):
    quotes = _read_json("quotes.json")
    quotes.append(q.model_dump())
    _write_json("quotes.json", quotes)
    return {"ok": True, "total": len(quotes)}


@router.put("/api/quotes/{idx}")
async def api_update_quote(idx: int, q: QuoteIn):
    quotes = _read_json("quotes.json")
    if idx < 0 or idx >= len(quotes):
        raise HTTPException(404)
    quotes[idx] = q.model_dump()
    _write_json("quotes.json", quotes)
    return {"ok": True}


@router.delete("/api/quotes/{idx}")
async def api_delete_quote(idx: int):
    quotes = _read_json("quotes.json")
    if idx < 0 or idx >= len(quotes):
        raise HTTPException(404)
    quotes.pop(idx)
    _write_json("quotes.json", quotes)
    return {"ok": True, "total": len(quotes)}


@router.post("/api/quotes/upload")
async def api_upload_quotes(file: UploadFile):
    content = await file.read()
    try:
        quotes = json.loads(content)
        assert isinstance(quotes, list)
    except Exception:
        raise HTTPException(400, "File must be a valid JSON array")
    _write_json("quotes.json", quotes)
    return {"ok": True, "total": len(quotes)}


# ── Ticker API ────────────────────────────────────────────────


@router.get("/api/tickers")
async def api_get_tickers():
    return _read_json("tickers.json")


class TickerIn(BaseModel):
    ticker: str


@router.post("/api/tickers")
async def api_add_ticker(t: TickerIn):
    tickers = _read_json("tickers.json")
    sym = t.ticker.upper()
    if sym not in tickers:
        tickers.append(sym)
        _write_json("tickers.json", tickers)
    return {"ok": True, "tickers": tickers}


@router.delete("/api/tickers/{ticker}")
async def api_delete_ticker(ticker: str):
    tickers = _read_json("tickers.json")
    tickers = [t for t in tickers if t != ticker.upper()]
    _write_json("tickers.json", tickers)
    return {"ok": True, "tickers": tickers}


# ── Portfolio API ─────────────────────────────────────────────


@router.get("/api/portfolio")
async def api_get_portfolio():
    return _read_json("portfolio.json")


class HoldingIn(BaseModel):
    ticker: str
    shares: float
    avg_cost: float
    notes: str = ""


@router.post("/api/portfolio")
async def api_add_holding(h: HoldingIn):
    data = _read_json("portfolio.json")
    data["holdings"].append(h.model_dump())
    _write_json("portfolio.json", data)
    return {"ok": True}


@router.put("/api/portfolio/{ticker}")
async def api_update_holding(ticker: str, h: HoldingIn):
    data = _read_json("portfolio.json")
    for i, item in enumerate(data["holdings"]):
        if item["ticker"] == ticker.upper():
            data["holdings"][i] = h.model_dump()
            _write_json("portfolio.json", data)
            return {"ok": True}
    raise HTTPException(404)


@router.delete("/api/portfolio/{ticker}")
async def api_delete_holding(ticker: str):
    data = _read_json("portfolio.json")
    data["holdings"] = [
        h for h in data["holdings"] if h["ticker"] != ticker.upper()
    ]
    _write_json("portfolio.json", data)
    return {"ok": True}
