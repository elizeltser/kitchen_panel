# CLAUDE.md — reTerminal ePaper Dashboard

## What this repo is
Personal smart dashboard on a Seeed Studio reTerminal E1001 (7.5" monochrome ePaper, 800×480).

Two subsystems:
- **server/** — FastAPI app (Windows primary, cross-platform). Renders PNG, serves it, hosts admin panel.
- **firmware/** — Zephyr RTOS app for the E1001's ESP32-S3. Polls server, decodes PNG, drives display.

Full spec: FSD.md

## Key constraints
- Image: exactly 800×480 px, PIL mode "L" (8-bit grayscale, 2× supersampled)
- Server runs on Windows (production) or Arch Linux (testing) — use pathlib for all paths, never hardcoded separators
- Fonts: always ImageFont.truetype() from server/fonts/ — never PIL default fonts
- HTTP client: always httpx.AsyncClient — never requests library
- Secrets: load from server/secrets/.env — never hardcode
- Data files in server/data/ are user-managed — never overwrite on startup
- No Claude/Anthropic API in this version

## Layout zones (pixels)
- Quote:     x=0,   y=0,   w=800, h=90
- Weather:   x=0,   y=90,  w=200, h=390  (moon disc at y≈420)
- Clock:     x=200, y=90,  w=600, h=260  (expands to h=390 if no reminders)
- Reminders: x=200, y=350, w=600, h=130  (hidden when empty)
- Stocks: fetched but NOT rendered on screen

## Quick start (server — Windows)
  .venv\Scripts\Activate.ps1
  cd server && uvicorn main:app --host 0.0.0.0 --port 8080 --reload

## Quick start (server — Linux/WSL)
  source .venv/bin/activate
  cd server && uvicorn main:app --host 0.0.0.0 --port 8080 --reload

## Quick start (firmware — Linux/WSL only)
  cd ~/zephyrproject
  west build -b reterminal_e1001/esp32s3/procpu ~/Documents/reTerminal/firmware
  west flash --runner esptool
