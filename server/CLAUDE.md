# CLAUDE.md — ePaper Dashboard Server

## What this does
FastAPI server on Arch Linux.
Renders 800×480 1-bit PNG for Seeed reTerminal E1001 ePaper display.
Hosts admin panel at /admin for managing quotes, stocks, portfolio.

## Run
  source ~/Documents/reTerminal/.venv/bin/activate
  cd server
  uvicorn main:app --host 0.0.0.0 --port 8080 --reload

## Useful commands
  curl -X POST http://localhost:8080/refresh
  curl http://localhost:8080/status
  curl -o /tmp/test.png http://localhost:8080/display.png
  # then open /tmp/test.png to inspect the rendered image

## Critical constraints
- Image: exactly 800×480 px, PIL mode "1" (1-bit black/white)
- Fonts: always ImageFont.truetype() from server/fonts/ — never PIL default fonts
- HTTP: always httpx.AsyncClient — never requests library
- Secrets: load from server/secrets/.env — never hardcode
- Data files in server/data/ are user-managed — never overwrite on startup
- No Claude/Anthropic API in this version

## Layout zones (pixels)
- Quote:     x=0,   y=0,   w=800, h=70
- Weather:   x=0,   y=70,  w=220, h=310
- Clock:     x=220, y=70,  w=580, h=220  (expands to h=310 if no birthdays)
- Birthdays: x=220, y=290, w=580, h=90   (hidden if none today)
- Stocks:    x=0,   y=380, w=800, h=100

## Clock zone
- Time: Montserrat Bold 120pt, centered in upper portion of clock zone
- Date: Montserrat Regular 24pt, centered directly below time
- Format: H:MM  and  "Wednesday, April 6"

## Module map
  main.py        — FastAPI app, all HTTP routes
  state.py       — shared in-memory state (png bytes, etag, cached source data)
  renderer.py    — Pillow canvas composition
  scheduler.py   — APScheduler jobs (fetches + periodic render)
  sources/       — one module per data source
  admin/         — admin panel routes and Jinja2 templates

## Data files (never overwrite on startup)
  server/data/quotes.json     — quote library (upload via /api/quotes/upload)
  server/data/tickers.json    — stock watchlist
  server/data/portfolio.json  — holdings with shares + avg cost

## Data sources
  Calendar: Birthdays only, token.json auto-refreshes
  Stocks:   Polygon.io at 05:45 and 17:30; P&L from portfolio.json
  Weather:  Open-Meteo, lat=32.08 lon=34.78, Celsius, no wind speed
  Quote:    date-seeded random from quotes.json, no Claude API
