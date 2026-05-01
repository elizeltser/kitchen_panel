# Functional Specification: ePaper Smart Dashboard System
**Document version:** 0.1 — Draft  
**Last updated:** 1.5.2026
**Author:** Eli Zeltser
**Status:** In Progress

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [System Architecture](#2-system-architecture)
3. [Component Specifications](#3-component-specifications)
   - 3.1 [Display Device — Seeed Studio reTerminal E1001](#31-display-device--seeed-studio-reterminal-e1001)
   - 3.2 [Server — Laptop Backend](#32-server--laptop-backend)
   - 3.3 [Data Sources](#33-data-sources)
   - 3.4 [Communication Layer](#34-communication-layer)
4. [Display Layout & Visual Design](#4-display-layout--visual-design)
5. [Update Schedule & Screen Management](#5-update-schedule--screen-management)
6. [Data Integration Specifications](#6-data-integration-specifications)
   - 6.1 [Google Calendar](#61-google-calendar)
   - 6.2 [Stock Data & AI Suggestions](#62-stock-data--ai-suggestions)
   - 6.3 [Weather](#63-weather)
   - 6.4 [Clock & Date](#64-clock--date)
7. [Firmware Specification (E1001)](#7-firmware-specification-e1001)
8. [Server Software Specification](#8-server-software-specification)
9. [Configuration & Secrets Management](#9-configuration--secrets-management)
10. [Error Handling & Fallback Behavior](#10-error-handling--fallback-behavior)
11. [Development Toolchain](#11-development-toolchain)
12. [Open Questions & Decisions Pending](#12-open-questions--decisions-pending)

---

## 1. Project Overview

### 1.1 Purpose

This document describes the functional requirements of a personal smart dashboard built on a **Seeed Studio reTerminal E1001** 7.5-inch monochrome ePaper display. The system provides a at-a-glance view of time, weather, calendar events, and stock portfolio status during defined morning and evening periods. An AI layer provides lightweight buy/hold/sell suggestions for tracked stocks. Calendar events are read from users google calender.

### 1.2 Goals

- Display accurate, readable information during two daily active windows without requiring interaction.
- Maximize screen lifespan by minimizing unnecessary full refreshes and enforcing a safe refresh schedule.
- Keep the display firmware simple and stateless — all intelligence and rendering lives on the laptop server.
- Enable future extension of data sources and layout without reflashing the device.

### 1.3 Non-Goals

- This is not a real-time trading terminal. Stock suggestions are informational only.
- The display is not interactive (no touch, no button-driven navigation in v1).
- The system does not need to function when the laptop server is off or unreachable.

### 1.4 Intended Users

This project will be used specifically for our home, and more specifically as a display for us to view and enjoy in our joint mornings and evenings.

---

## 2. System Architecture

### 2.1 High-Level Overview

The system follows a **server-rendered pull model**:

```
┌────────────────────────────────────┐
│         Old Laptop (Server)        │
│                                    │
│  ┌──────────┐   ┌───────────────┐  │
│  │ Data     │   │ Image         │  │
│  │ Fetcher  │──►│ Renderer      │  │
│  │          │   │ (Pillow/PNG)  │  │
│  └──────────┘   └──────┬────────┘  │
│   Google Cal            │          │
│   Stocks API     FastAPI HTTP      │
│   Weather API    server :8080      │
│   Claude API            │          │
└────────────────────────-│──────────┘
                          │  Wi-Fi (local network)
                          │  HTTP GET /display.png
                          ▼
              ┌───────────────────────┐
              │   reTerminal E1001    │
              │   ESP32-S3 firmware   │
              │                       │
              │  Poll → ETag check    │
              │  Decode PNG           │
              │  Refresh ePaper       │
              │  Deep sleep           │
              └───────────────────────┘
```

**Key design principle:** The E1001 firmware is intentionally dumb. It only knows how to fetch a PNG from a fixed URL, check whether it changed, render it to the display, and sleep. All business logic, data fetching, layout decisions, and AI calls happen on the laptop.

### 2.2 Data Flow

```
Every render cycle (laptop side):
  1. Fetch fresh data from all sources (calendar, weather, stocks)
  2. Call Claude API for stock suggestion if data changed
  3. Composite all data into an 800×480 1-bit PNG
  4. Cache PNG with a new ETag if content changed
  5. Serve via HTTP

Every poll cycle (E1001 side):
  1. Wake from deep sleep (or timer interrupt)
  2. Connect to Wi-Fi
  3. GET /display.png with If-None-Match: <last_etag>
  4. If 304 Not Modified → skip display update, go back to sleep
  5. If 200 OK → decode PNG, choose refresh mode, update display
  6. Store new ETag in NVS flash
  7. Compute sleep duration until next poll
  8. Enter deep sleep
```

### 2.3 Network Topology

> **[ EDIT ]** Fill in your local network details.

| Item | Value |
|---|---|
| Network type | <!-- e.g. Home Wi-Fi, 2.4GHz only (ESP32 requirement) --> |
| Laptop hostname / IP | <!-- e.g. static IP 192.168.1.50, or mDNS hostname --> |
| Server port | `8080` (configurable) |
| E1001 IP assignment | <!-- DHCP with reservation, or static --> |

---

## 3. Component Specifications

### 3.1 Display Device — Seeed Studio reTerminal E1001

| Property | Value |
|---|---|
| Display | 7.5-inch monochrome ePaper, GDEY075T7 |
| Resolution | 800 × 480 pixels |
| Controller | UC8179 |
| MCU | ESP32-S3R8 (dual-core LX7, 8MB PSRAM, 32MB flash) |
| Connectivity | 2.4GHz Wi-Fi, Bluetooth 5.0 BLE |
| Battery | 2000 mAh (not primary concern for this project) |
| Onboard sensors | SHT40 temperature & humidity, RTC |
| Pinout (ePaper SPI) | CLK=GPIO7, MOSI=GPIO9, CS=GPIO10, DC=GPIO8, RST=GPIO47, BUSY=GPIO48 |
| Buttons | Left=GPIO4, Right=GPIO5, Green/Reset=GPIO3 |
| LED | GPIO6 (active LOW) |
| Buzzer | GPIO45 |

**Refresh modes available:**

| Mode | Duration | Flicker | Use case |
|---|---|---|---|
| Full refresh | ~3s | Multiple flashes | Window start, daily maintenance |
| Fast refresh | ~1.5s | Single flash | Content change (calendar, stocks) |
| Partial refresh | ~0.3s | None | Clock tick (time-only region) |

### 3.2 Server — Laptop Backend

> **[ EDIT ]** Fill in your laptop's specs and OS.

| Property | Value |
|---|---|
| Hardware | Asus X554I |
| OS | Windows 10 |
| Python version | 3.12 |
| Always-on | Yes |
| Server framework | FastAPI + Uvicorn |
| Image rendering | Pillow (PIL) |
| Process management | <!-- e.g. systemd service, tmux, Docker --> |

### 3.3 Data Sources

| Source | Provider | Auth method | Update frequency |
|---|---|---|---|
| Calendar | Google Calendar API v3 | OAuth2 (offline token) | Every render cycle |
| Stock prices | polygon.io | <!-- API key / none --> | Every day |
| Stock suggestions | Claude API (Haiku) | API key | On price change > threshold |
| Weather | Open-Meteo (free, no key) | None | Every day |
| Time / Date | System clock (laptop) | N/A | Every render cycle |

### 3.4 Communication Layer

- **Protocol:** HTTP/1.1 over local Wi-Fi
- **Image format:** PNG, 1-bit (black/white), 800×480
- **Cache control:** ETag-based — E1001 sends `If-None-Match` header; server returns `304` if unchanged
- **Security:** Local network only. No TLS required for v1.

> **[ EDIT ]** If you later expose the server externally (e.g. for remote updates), add TLS and auth token requirements here.

---

## 4. Display Layout & Visual Design

### 4.1 Grid Structure

The 800×480 canvas is divided into four zones:

```
┌─────────────────────────────────────────────────────────────────┐  y=0
│                    HEADER BAR                                   │  h=60
│  Date (left)                    Weather summary (right)         │
├──────────────────┬──────────────────────────────────────────────┤  y=60
│                  │                                              │
│   CLOCK          │   CALENDAR                                   │  h=320
│   (left panel)   │   (right panel)                              │
│                  │                                              │
├──────────────────┴──────────────────────────────────────────────┤  y=380
│                    STOCKS BAR                                   │  h=100
│  Ticker  Price  Δ%   Ticker  Price  Δ%    [AI suggestion text]  │
└─────────────────────────────────────────────────────────────────┘  y=480
         x=340 (divider)
```

**Zone definitions:**

| Zone | x | y | w | h | Content |
|---|---|---|---|---|---|
| Header | 0 | 0 | 800 | 60 | Date + weather |
| Clock | 0 | 60 | 340 | 320 | Time (large), date subtitle |
| Calendar | 340 | 60 | 460 | 320 | Up to 4 upcoming events |
| Stocks | 0 | 380 | 800 | 100 | Tracked tickers + AI note |

### 4.2 Typography

All fonts are loaded server-side from TTF files. No font constraints from the ESP32 apply.

| Element | Font | Size | Style |
|---|---|---|---|
| Clock (HH:MM) | <!-- e.g. Inter / JetBrains Mono --> | <!-- e.g. 96pt --> | Bold, tabular figures |
| Date subtitle | <!-- e.g. Inter --> | <!-- e.g. 28pt --> | Regular |
| Header — date | <!-- e.g. Inter --> | <!-- e.g. 22pt --> | Medium |
| Header — weather | <!-- e.g. Inter --> | <!-- e.g. 20pt --> | Regular |
| Calendar event title | <!-- e.g. Inter --> | <!-- e.g. 22pt --> | Medium |
| Calendar event time | <!-- e.g. JetBrains Mono --> | <!-- e.g. 20pt --> | Regular |
| Stock tickers | <!-- e.g. JetBrains Mono --> | <!-- e.g. 20pt --> | Bold |
| Stock values | <!-- e.g. JetBrains Mono --> | <!-- e.g. 19pt --> | Regular |
| AI suggestion | <!-- e.g. Inter --> | <!-- e.g. 17pt --> | Italic |

> **[ EDIT ]** Download chosen fonts as TTF from Google Fonts and place in `server/fonts/`. Update the table above with final choices.

### 4.3 Visual Style Rules

- Background: white (`#FFFFFF` → pixel value 1)
- Foreground: black (`#000000` → pixel value 0)
- Zone separators: single-pixel black lines
- No anti-aliasing (1-bit output — Pillow renders with dithering)
- Negative values in stocks: displayed with `▼` prefix (no grey — monochrome only)
- Positive values: displayed with `▲` prefix
- Icons: weather icons rendered as 1-bit bitmaps from a small local icon set

> **[ EDIT ]** Decide on icon approach: (a) text-only weather description, (b) embedded bitmap icons, or (c) Unicode weather symbols rendered via a symbol font like Nerd Fonts.

### 4.4 Partial Refresh Region

The clock partial-refresh region is defined as a fixed bounding box:

| Property | Value |
|---|---|
| x | 10 |
| y | 100 |
| width | 320 |
| height | 140 |

Only this region is redrawn on minute-tick updates. All other zones are only updated on a content change or full refresh.

---

## 5. Update Schedule & Screen Management

### 5.1 Active Windows

| Window | Start | End | Behaviour |
|---|---|---|---|
| Morning | 05:45 | 08:00 | Full refresh on wake, then partial clock tick every minute |
| Evening | 18:00 | 22:00 | Full refresh on wake, then partial clock tick every minute |

> Times are local time on the E1001 (synced via NTP at boot).

### 5.2 Maintenance Refresh

To prevent image persistence (ghosting from holding a static image too long):

| Event | Time | Refresh type |
|---|---|---|
| End of morning window | 08:00 | Full refresh → deep sleep |
| Midday maintenance | 12:00 | Full refresh → immediately back to sleep |
| End of evening window | 22:00 | Full refresh → deep sleep |

### 5.3 Refresh Decision Logic (E1001 firmware)

```
On each wake:
  IF content changed (ETag differs):
    IF only clock region changed → partial refresh (clock zone only)
    ELSE IF other data changed   → fast refresh (full screen, 1 flash)
    ELSE                         → no refresh
  IF scheduled full refresh time → full refresh regardless of content
  
After every 5 consecutive partial/fast refreshes:
  → force one full refresh (prevents ghosting accumulation)
```

### 5.4 Sleep Duration Calculation

The firmware computes sleep duration dynamically:

- **During active window:** sleep 60 seconds (1-minute clock tick)
- **At end of active window:** sleep until next scheduled event (start of next window or midday maintenance)
- **Outside all windows:** sleep until next window start
- **Time sync:** The E1001 will use NTP for  time syncronization every 24 hours to ensure accuracy and local RTL from drifting.

---

## 6. Data Integration Specifications

### 6.1 Google Calendar

**Purpose:** Display upcoming events for the current day.

**Authentication:**
- OAuth2 with offline refresh token
- Credentials stored in `server/secrets/token.json` (generated once via CLI flow)
- Scopes required: `https://www.googleapis.com/auth/calendar.readonly`

**Query parameters:**
- Calendar: Primary calendar (`primary`)
- Time range: Now → end of current day (23:59 local time)
- Max results: 5 events
- Ordered by: start time ascending

**Display rules:**
- Show up to 4 events on screen (5th is truncated)
- Format: `HH:MM  Event title` (truncate title at 15 characters)
- All-day events: displayed without a time, with label `[All day]`
- Past events (already started): shown with a strike-through or dimmed style
- Calendars to include are only 'Birthdays' from the primary GCalendar

**Error handling:** If the Calendar API is unavailable, display a descriptive error on the screen.

---

### 6.2 Stock Data & AI Suggestions

**Purpose:** Show current price and daily change for a personal watchlist. Provide a one-sentence AI suggestion per ticker.

**Tickers to track:**

> **[ EDIT ]** List your tickers here:
> - `<!-- e.g. AAPL -->`
> - `<!-- e.g. TSLA -->`
> - `<!-- e.g. SPY -->`
> - `<!-- add more -->`

**Data provider:**

**Polygon.io**

**Fields fetched per ticker:**
- Current price
- Daily change (absolute and %)
- Previous close

**Market hours handling:**
- During market hours (<!-- e.g. 09:30–16:00 ET -->): fetch every poll cycle
- Outside market hours: use last known price, display `[closed]` indicator

**AI suggestions (Claude API):**

| Property | Value |
|---|---|
| Model | `claude-haiku-4-5-20251001` |
| Max tokens | 60 |
| Trigger | Price change > <!-- e.g. 1.5% --> since last suggestion |
| Prompt template | `"[TICKER] is at $[PRICE], [CHANGE]% today. Give a one-sentence buy/hold/sell suggestion based on this movement."` |
| Display | Single line at bottom of stocks zone, italic |

> **[ EDIT ]** Adjust the price-change trigger threshold and prompt wording to match your preference. Note: Claude suggestions are informational only — not financial advice.

**Rate limiting:**
- AI suggestions: maximum <!-- e.g. 1 call per ticker per 30 minutes -->
- Stock API: respect provider's rate limits (see provider docs)

---

### 6.3 Weather

**Purpose:** Show current conditions and daily precipitation probability.

**Provider:** Open-Meteo (`https://api.open-meteo.com`) — free, no API key required.

**Location:**

- Latitude: 32.08
- Longitude: 34.78
- City: Bat Yam, Israel

**Fields fetched:**

| Field | API parameter | Display |
|---|---|---|
| Current temperature | `current=temperature_2m` | `14°C` |
| Weather condition | `current=weather_code` | Text description (mapped from WMO code) |
| Daily precipitation % | `daily=precipitation_probability_max` | `40%` |

**Update frequency:** Every day at 5:45, fetching conditions for that day.

**Units:**

Temperature: `celsius`

---

### 6.4 Clock & Date

**Purpose:** Display accurate current time and date. This is the most frequently updated element.

| Element | Format | Example |
|---|---|---|
| Time | `H:MM` (24h) | `7:42` |
| Day of week | Full name | `Wednesday` |
| Date | `DD MMM` | `29 Apr` |

**Time source:** System clock on the E1001, synced via NTP at each wake-up. NTP server: 'il.pool.ntp.org'.

**Timezone:** Asia/Jerusalem — set in firmware via Posix TZ string.

For Israel: `IST-2IDT,M3.4.4/26,M10.5.0`

---

## 7. Firmware Specification (E1001)

### 7.1 Framework & Libraries

| Item | Choice |
|---|---|
| Framework | ESPIDF |
| ePaper driver | GxEPD2 — model `GxEPD2_750_GDEY075T7` |
| PNG decoder | PNGdec |
| HTTP client | `HTTPClient` (Arduino ESP32) |
| NVS storage | `Preferences` (Arduino ESP32) — stores ETag, last refresh type count |
| NTP | `configTime()` + `esp_sntp` |
| Deep sleep | `esp_deep_sleep_start()` |

### 7.2 Firmware State Machine

```
BOOT
  │
  ├─► Connect Wi-Fi (timeout: 15s → fallback sleep 5min)
  │
  ├─► Sync NTP time
  │
  ├─► Determine current mode:
  │     ACTIVE  → poll server, update display, sleep 60s
  │     MAINTENANCE → full refresh, sleep until next window
  │     INACTIVE → sleep until next window start
  │
  ├─► [ACTIVE only] GET /display.png
  │     200 → decode PNG, select refresh mode, update display
  │     304 → skip display update
  │     Error → skip display update, log to serial
  │
  └─► Enter deep sleep (duration computed from current time)
```

### 7.3 NVS Persistent Storage

| Key | Type | Purpose |
|---|---|---|
| `last_etag` | String | ETag of last successfully displayed image |
| `refresh_count` | Int | Counter of consecutive partial/fast refreshes (reset at 5) |
| `last_full_refresh` | Long | Unix timestamp of last full refresh |

### 7.4 Build Configuration (`platformio.ini`)

```ini
[env:reterminal-e1001]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
board_build.flash_size = 32MB
board_build.psram_type = opi
board_upload.flash_size = 32MB
monitor_speed = 115200
upload_port = /dev/ttyUSB0

lib_deps =
  ZinggJM/GxEPD2
  adafruit/Adafruit GFX Library
  pschatzmann/PNGdec
  bblanchon/ArduinoJson
```

### 7.5 Configuration (`include/config.h`)

```cpp
// [ EDIT ] — fill these before building

#define WIFI_SSID       "<!-- your SSID -->"
#define WIFI_PASSWORD   "<!-- your password -->"
#define SERVER_HOST     "<!-- laptop IP or hostname -->"
#define SERVER_PORT     8080
#define SERVER_PATH     "/display.png"

#define NTP_SERVER      "pool.ntp.org"
#define TZ_STRING       "<!-- Posix TZ string -->"

#define MORNING_START_H  5
#define MORNING_START_M  45
#define MORNING_END_H    8
#define MORNING_END_M    0
#define EVENING_START_H  18
#define EVENING_START_M  0
#define EVENING_END_H    22
#define EVENING_END_M    0
#define MAINTENANCE_H    12
#define MAINTENANCE_M    0
```

---

## 8. Server Software Specification

### 8.1 Directory Structure

```
server/
├── main.py               # FastAPI app entry point
├── renderer.py           # Pillow canvas rendering logic
├── scheduler.py          # Data fetch scheduling
├── sources/
│   ├── calendar.py       # Google Calendar integration
│   ├── stocks.py         # Stock price fetching
│   ├── weather.py        # Open-Meteo integration
│   └── suggestions.py    # Claude API calls
├── fonts/
│   ├── Inter-Bold.ttf
│   ├── Inter-Regular.ttf
│   ├── Inter-Medium.ttf
│   └── JetBrainsMono-Regular.ttf
├── secrets/
│   ├── credentials.json  # Google OAuth client secret (never commit)
│   ├── token.json        # Google OAuth refresh token (never commit)
│   └── .env              # API keys (never commit)
├── cache/
│   └── display.png       # Latest rendered image
├── requirements.txt
└── CLAUDE.md             # Project context for Claude Code
```

### 8.2 API Endpoints

| Endpoint | Method | Description |
|---|---|---|
| `/display.png` | GET | Returns current rendered PNG. Supports `If-None-Match` ETag. |
| `/status` | GET | Returns JSON with last render time, data source statuses, next scheduled render. |
| `/refresh` | POST | Forces an immediate re-render and fetch of all data sources. |

### 8.3 Render Pipeline

```
scheduler triggers render()
  │
  ├─► Fetch all sources in parallel (asyncio.gather)
  │     calendar.get_events()
  │     weather.get_current()
  │     stocks.get_prices(tickers)
  │     suggestions.get_suggestion(ticker) [if triggered]
  │
  ├─► renderer.compose(data) → PIL Image (800×480, mode "1")
  │
  ├─► Compute content hash → new ETag
  │
  ├─► If ETag changed → save to cache/display.png, update stored ETag
  │
  └─► Return (image, etag)
```

### 8.4 Scheduling

| Task | Interval | Notes |
|---|---|---|
| Full render | 60 seconds (active windows) / 30 min (inactive) | Always runs to catch data changes |
| Weather fetch | Every day at 5:45 | Cached between fetches |
| Stock fetch | 5 minutes (market hours) / 30 min (closed) | <!-- [ EDIT ] adjust to API rate limits --> |
| Calendar fetch | Once a day in 5:45 | Google API quota: 1M requests/day free |
| AI suggestions | On trigger only | Max 1 per ticker per 30 min |

### 8.5 `CLAUDE.md` (for Claude Code sessions)

```markdown
# CLAUDE.md — ePaper Dashboard Server

## What this project does
Python FastAPI server that renders an 800×480 PNG dashboard
and serves it to a Seeed Studio reTerminal E1001 ePaper display via HTTP.

## Commands
- Run server: `uvicorn main:app --host 0.0.0.0 --port 8080 --reload`
- Force render: `curl -X POST http://localhost:8080/refresh`
- Check status: `curl http://localhost:8080/status`

## Key constraints
- Output image MUST be exactly 800×480 pixels, PIL mode "1" (1-bit)
- Fonts are in server/fonts/ — always use ImageFont.truetype(), never default fonts
- All data fetching is async — use httpx.AsyncClient, not requests
- Secrets are in server/secrets/.env — never hardcode keys

## Data source notes
- Google Calendar: token.json auto-refreshes, do not regenerate credentials
- Stocks: [ EDIT — describe your chosen provider and any quirks ]
- Weather: Open-Meteo, no key needed, lat/lon in .env
- Claude API: haiku model only (cost), max 60 tokens per suggestion call
```

---

## 9. Configuration & Secrets Management

### 9.1 Server `.env` file

```env
# Google
GOOGLE_CREDENTIALS_PATH=secrets/credentials.json
GOOGLE_TOKEN_PATH=secrets/token.json

# Stocks
STOCK_PROVIDER=<!-- alphavantage | yfinance | polygon -->
STOCK_API_KEY=<!-- your key, or leave empty for yfinance -->
STOCK_TICKERS=<!-- AAPL,TSLA,SPY -->

# Weather
WEATHER_LAT=<!-- latitude -->
WEATHER_LON=<!-- longitude -->
WEATHER_TIMEZONE=<!-- e.g. Asia/Jerusalem -->
WEATHER_UNITS=<!-- celsius | fahrenheit -->

# Claude
ANTHROPIC_API_KEY=<!-- your key -->
SUGGESTION_TRIGGER_PCT=<!-- e.g. 1.5 -->
SUGGESTION_COOLDOWN_MIN=<!-- e.g. 30 -->

# Server
SERVER_PORT=8080
```

### 9.2 Secrets That Must Never Be Committed

Add to `.gitignore`:
```
server/secrets/
server/.env
server/secrets/credentials.json
server/secrets/token.json
```

---

## 10. Error Handling & Fallback Behavior

| Failure scenario | Behaviour |
|---|---|
| Wi-Fi connection fails (E1001) | Retry 3 times, then sleep 5 min and retry |
| Server unreachable (E1001) | Use last displayed image, skip update, sleep normally |
| Google Calendar API error | Display last cached events with `(cached)` label |
| Stock API error / rate limit | Display last known price with `(delayed)` label |
| Claude API error | Suppress suggestion line, display nothing in that slot |
| Weather API error | Display last known weather with `(cached)` label |
| Server render exception | Serve last valid cached PNG, log error to console |
| NTP sync failure | Use RTC time if available, else display `--:--` |

> **[ EDIT ]** Add any additional failure modes specific to your environment.

---

## 11. Development Toolchain

### 11.1 Laptop / Server Side

| Tool | Purpose |
|---|---|
| Python 3.12+ | Server runtime |
| FastAPI + Uvicorn | HTTP server |
| Pillow | Image rendering |
| httpx | Async HTTP client for data sources |
| google-api-python-client | Google Calendar SDK |
| python-dotenv | `.env` file loading |
| Claude Code | AI-assisted development |

Install:
```bash
pip install fastapi uvicorn pillow httpx google-auth google-auth-oauthlib \
            google-api-python-client anthropic python-dotenv schedule
```

### 11.2 E1001 Firmware Side

| Tool | Purpose |
|---|---|
| PlatformIO CLI | Build system |
| Arduino framework | ESP32 runtime |
| GxEPD2 | ePaper display driver |
| PNGdec | Lightweight PNG decoder |
| esptool | Firmware flashing |

```bash
# Move PlatformIO off /home (already full)
export PLATFORMIO_CORE_DIR=/opt/platformio
pip install platformio --break-system-packages
```

### 11.3 Claude Code Setup

```bash
npm install -g @anthropic-ai/claude-code

# In project root:
claude  # starts session, reads CLAUDE.md automatically
```

---

## 12. Open Questions & Decisions Pending

> Use this section to track unresolved decisions. Remove items as they are decided.

| # | Question | Options | Decision |
|---|---|---|---|
| 1 | Stock data provider | Alpha Vantage / yfinance / Polygon | <!-- TBD --> |
| 2 | Weather units | Celsius / Fahrenheit | <!-- TBD --> |
| 3 | Font for clock | Inter / JetBrains Mono / other | <!-- TBD --> |
| 4 | Weather icons | Text only / bitmap icons / Unicode symbols | <!-- TBD --> |
| 5 | Additional calendars to include | Primary only / shared calendars | <!-- TBD --> |
| 6 | Stock AI suggestion trigger threshold | 1% / 1.5% / 2% daily change | <!-- TBD --> |
| 7 | Server process management | systemd / Docker / tmux / manual | <!-- TBD --> |
| 8 | E1001 IP assignment | DHCP reservation / static IP | <!-- TBD --> |
| 9 | NTP source | pool.ntp.org / router local NTP | <!-- TBD --> |
| 10 | v1 button behaviour | None / manual force-refresh on green button | <!-- TBD --> |

---

*End of document. All `<!-- EDIT -->` markers indicate places requiring your input before implementation begins.*
