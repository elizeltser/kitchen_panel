# CLAUDE.md — ePaper Dashboard Server

## What this does
FastAPI server on Arch Linux.
Renders 800×480 grayscale PNG for Seeed reTerminal E1001 ePaper display.
Hosts admin panel at /admin for managing quotes, stocks, portfolio.

## Run
  source ~/Documents/reTerminal/.venv/bin/activate
  cd server
  uvicorn main:app --host 0.0.0.0 --port 8080 --reload

## Useful commands
  curl -X POST http://localhost:8080/refresh
  curl http://localhost:8080/status
  curl -o /tmp/test.png http://localhost:8080/display.png
  curl http://localhost:8080/screensaver/count

## Critical constraints
- Image: exactly 800×480 px, PIL mode "L" (8-bit grayscale), rendered at 2× then LANCZOS-downsampled
- Fonts: always ImageFont.truetype() from server/fonts/ — never PIL default fonts
- HTTP: always httpx.AsyncClient — never requests library
- Secrets: load from server/secrets/.env — never hardcode
- Data files in server/data/ are user-managed — never overwrite on startup
- No Claude/Anthropic API in this version

## Layout zones (pixels)
- Quote:     x=0,   y=0,   w=800, h=90
- Weather:   x=0,   y=90,  w=200, h=390  (moon disc drawn at y≈420)
- Clock:     x=200, y=90,  w=600, h=260  (expands to h=390 if no reminders)
- Reminders: x=200, y=350, w=600, h=130  (hidden when empty)
- Stocks: NOT rendered (data fetched, zone at y=480 / off-screen)

## Clock zone
- Time: Montserrat Regular 148pt, centered; rendered as now+1min
- Date: Montserrat Regular 32pt, centered directly below time
- Format: H:MM  and  "Wednesday, April 6"

## Module map
  main.py        — FastAPI app, all HTTP routes (display, screensaver, admin)
  state.py       — shared in-memory state (png bytes, etag, all source data)
  renderer.py    — Pillow canvas composition (2× supersampling)
  scheduler.py   — APScheduler: morning fetch at 05:45, eve cal 18:00, eve stocks 17:30, tick every 60s
  sources/       — calendar_src.py, weather.py, stocks.py, quote.py, moonphase.py
  admin/         — admin panel routes and Jinja2 templates

## Data files (never overwrite on startup)
  server/data/quotes.json          — quote library
  server/data/tickers.json         — stock watchlist
  server/data/portfolio.json       — holdings with shares + avg cost
  server/data/screensavers/*.png   — numbered screensaver images (0.png, 1.png, …)

## Data sources
  Calendar: Birthdays + events, token.json auto-refreshes
  Stocks:   Polygon.io at 05:45 and 17:30; P&L from portfolio.json (not rendered)
  Weather:  Open-Meteo, lat=32.08 lon=34.78, Celsius, no wind speed
  Quote:    date-seeded random from quotes.json, no Claude API
  Moon:     ephem library, phase 0.0 (new) → 0.5 (full) → 1.0; disc in weather column
