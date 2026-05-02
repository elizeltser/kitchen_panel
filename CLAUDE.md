# CLAUDE.md — reTerminal ePaper Dashboard

## What this repo is
Personal smart dashboard on a Seeed Studio reTerminal E1001 (7.5" monochrome ePaper, 800×480).

Two subsystems:
- **server/** — FastAPI app on Arch Linux. Renders PNG, serves it, hosts admin panel.
- **firmware/** — Zephyr RTOS app for the E1001's ESP32-S3. Polls server, decodes PNG, drives display.

Full spec: FSD.md

## Key constraints
- Image: exactly 800×480 px, PIL mode "1" (1-bit black/white)
- Server runs on Arch Linux — no Windows paths, no NSSM
- Fonts: always ImageFont.truetype() from server/fonts/ — never PIL default fonts
- HTTP client: always httpx.AsyncClient — never requests library
- Secrets: load from server/secrets/.env — never hardcode
- Data files in server/data/ are user-managed — never overwrite on startup
- No Claude/Anthropic API in this version

## Quick start (server)
  source .venv/bin/activate
  cd server && uvicorn main:app --host 0.0.0.0 --port 8080 --reload

## Quick start (firmware)
  cd firmware
  west build -b reterminal_e1001 .
  west flash --runner esptool
