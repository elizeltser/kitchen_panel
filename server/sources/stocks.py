"""
Polygon.io stock fetcher + portfolio P&L calculator.
API key loaded from server/secrets/.env → STOCK_API_KEY
"""
import asyncio
import json
import logging
import os
from pathlib import Path
from typing import Optional

import httpx
from dotenv import load_dotenv

load_dotenv(Path(__file__).parent.parent / "secrets" / ".env")

log = logging.getLogger(__name__)
API_KEY = os.getenv("STOCK_API_KEY", "")
DATA = Path(__file__).parent.parent / "data"

_SNAPSHOT = "https://api.polygon.io/v2/snapshot/locale/us/markets/stocks/tickers/{ticker}"
_PREV_CLOSE = "https://api.polygon.io/v2/aggs/ticker/{ticker}/prev"


async def _fetch_ticker(client: httpx.AsyncClient, ticker: str) -> Optional[dict]:
    # Try live snapshot first
    try:
        r = await client.get(
            _SNAPSHOT.format(ticker=ticker),
            params={"apiKey": API_KEY},
        )
        if r.status_code == 200:
            snap = r.json().get("ticker", {})
            day = snap.get("day", {})
            prev = snap.get("prevDay", {})
            price = day.get("c") or prev.get("c")
            open_p = day.get("o") or prev.get("o") or price
            if price and open_p:
                change_pct = ((price - open_p) / open_p) * 100
                return {"symbol": ticker, "price": price, "change_pct": change_pct}
    except Exception as e:
        log.warning("Snapshot failed for %s: %s", ticker, e)

    # Fall back to previous close
    try:
        r = await client.get(
            _PREV_CLOSE.format(ticker=ticker),
            params={"apiKey": API_KEY, "adjusted": "true"},
        )
        r.raise_for_status()
        results = r.json().get("results", [])
        if results:
            res = results[0]
            price = res["c"]
            open_p = res["o"]
            change_pct = ((price - open_p) / open_p) * 100
            return {
                "symbol": ticker,
                "price": price,
                "change_pct": change_pct,
                "label": "[prev. close]",
            }
    except Exception as e:
        log.warning("Prev close failed for %s: %s", ticker, e)

    return None


def _load_portfolio() -> list:
    p = DATA / "portfolio.json"
    if not p.exists():
        return []
    try:
        return json.loads(p.read_text())["holdings"]
    except Exception:
        return []


def _portfolio_summary(holdings: list, prices: dict) -> str:
    if not holdings:
        return ""
    total_value = 0.0
    total_cost = 0.0
    for h in holdings:
        price = prices.get(h["ticker"])
        if price is None:
            continue
        total_value += h["shares"] * price
        total_cost += h["shares"] * h["avg_cost"]
    if total_value == 0:
        return ""
    pnl = total_value - total_cost
    pnl_pct = (pnl / total_cost) * 100 if total_cost else 0
    sign = "+" if pnl >= 0 else ""
    return (
        f"Portfolio: ${total_value:,.0f} total"
        f"  \u00b7  Today: {sign}${pnl:,.0f} ({sign}{pnl_pct:.1f}%)"
    )


async def get_prices() -> dict:
    tickers_file = DATA / "tickers.json"
    tickers = json.loads(tickers_file.read_text()) if tickers_file.exists() else []

    async with httpx.AsyncClient(timeout=10) as client:
        results = await asyncio.gather(*[_fetch_ticker(client, t) for t in tickers])

    ticker_data = [r for r in results if r]
    prices = {t["symbol"]: t["price"] for t in ticker_data}
    holdings = _load_portfolio()
    summary = _portfolio_summary(holdings, prices)

    return {"tickers": ticker_data, "portfolio_summary": summary}
