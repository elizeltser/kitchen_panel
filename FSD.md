# Functional Specification: ePaper Smart Dashboard System
**Document version:** 0.2 — Draft  
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
   - 6.5 [Quote of the Day](#65-quote-of-the-day)
7. [Firmware Specification (E1001)](#7-firmware-specification-e1001)
8. [Server Software Specification](#8-server-software-specification)
9. [Configuration & Secrets Management](#9-configuration--secrets-management)
10. [Error Handling & Fallback Behavior](#10-error-handling--fallback-behavior)
11. [Development Toolchain](#11-development-toolchain)
12. [Open Questions & Decisions Pending](#12-open-questions--decisions-pending)

---

## 1. Project Overview

### 1.1 Purpose

This document describes the functional requirements of a personal smart dashboard built on a **Seeed Studio reTerminal E1001** 7.5-inch monochrome ePaper display. The system provides an at-a-glance view of time, weather, calendar events, stock portfolio status, and a daily inspirational quote during defined morning and evening periods. An AI layer provides lightweight buy/hold/sell suggestions for tracked stocks and generates the daily quote.

### 1.2 Goals

- Display accurate, readable information during two daily active windows without requiring interaction.
- Maximize screen lifespan by minimizing unnecessary full refreshes and enforcing a safe refresh schedule.
- Keep the display firmware simple and stateless — all intelligence and rendering lives on the laptop server.
- Enable future extension of data sources and layout without reflashing the device.
- Provide a pleasant shared morning and evening experience for the household.

### 1.3 Non-Goals

- This is not a real-time trading terminal. Stock suggestions are informational only.
- The display is not interactive (no touch, no button-driven navigation in v1).
- The system does not need to function when the laptop server is off or unreachable.

### 1.4 Intended Users

This project will be used specifically for our home, as a display for us to view and enjoy in our joint mornings and evenings.

---

## 2. System Architecture

### 2.1 High-Level Overview

The system follows a **server-rendered pull model**:

```
┌────────────────────────────────────┐
│    Asus A554I Laptop (Server)      │
│         Windows 10                 │
│  ┌──────────┐   ┌───────────────┐  │
│  │ Data     │   │ Image         │  │
│  │ Fetcher  │──►│ Renderer      │  │
│  │          │   │ (Pillow/PNG)  │  │
│  └──────────┘   └──────┬────────┘  │
│   Google Cal            │          │
│   Polygon.io     FastAPI HTTP      │
│   Open-Meteo     server :8080      │
│   Claude API            │          │
└────────────────────────-│──────────┘
                          │  Wi-Fi (home network, 2.4GHz)
                          │  HTTP GET /display.png
                          ▼
              ┌───────────────────────┐
              │   reTerminal E1001    │
              │  ESP-IDF firmware     │
              │                       │
              │  Poll → ETag check    │
              │  Decode PNG           │
              │  Refresh ePaper       │
              │  Deep sleep           │
              └───────────────────────┘
```

**Key design principle:** The E1001 firmware is intentionally simple and stateless. It only knows how to fetch a PNG from a fixed URL, check whether it changed, render it to the display, and sleep. All business logic, data fetching, layout decisions, and AI calls happen on the laptop server.

### 2.2 Data Flow

```
Every render cycle (laptop side):
  1. Fetch fresh data from all sources (calendar, weather, stocks, quote)
  2. Call Claude API for stock suggestion if price change threshold exceeded
  3. Composite all data into an 800×480 1-bit PNG
  4. Cache PNG with a new ETag if content changed
  5. Serve via HTTP

Every poll cycle (E1001 side):
  1. Wake from deep sleep (timer-based)
  2. Connect to Wi-Fi
  3. GET /display.png with If-None-Match: <last_etag>
  4. If 304 Not Modified → skip display update, go back to sleep
  5. If 200 OK → decode PNG, choose refresh mode, update display
  6. Store new ETag in NVS flash
  7. Compute sleep duration until next poll
  8. Enter deep sleep
```

### 2.3 Network Topology

This system runs entirely within your home Wi-Fi network. The E1001 and the laptop must be on the same local network. The server is **not exposed to the internet** — it only listens on your local network.

**To fill in this section, run the following on your Windows laptop:**

```powershell
# 1. Find your laptop's local IP address (look for Wi-Fi adapter)
ipconfig

# 2. Find your laptop's hostname (simpler alternative to using raw IP)
hostname

# 3. Verify both devices are on the same subnet — the first three
#    numbers of their IP addresses should match (e.g. 192.168.1.x)
#    The E1001's IP will appear in your router's admin page after
#    it connects, or in its serial monitor output on first boot.
```

**How to check your router admin page for the E1001's IP:**
Open a browser and go to `http://192.168.1.1` (or `http://192.168.0.1`) — this is usually your router. Log in and look for a "Connected devices" or "DHCP clients" list. Once the E1001 has booted and connected to Wi-Fi, it will appear there with a name like `espressif` or `reterminal`.

**How to verify the server is NOT exposed externally:**
```powershell
# On Windows, check what's listening and on which interface.
# Look for port 8080 — it should show 0.0.0.0:8080 (local only)
# NOT your public IP address.
netstat -an | findstr "8080"

# To find your public IP (for comparison — the server must NOT be on this):
# Visit https://whatismyip.com in a browser
# Your laptop's local IP (from ipconfig) will be different — that's correct.
```

As long as your router does not have a port-forwarding rule pointing external traffic to port 8080, your server is safely local-only. You do not need to add any rules — the default is closed.

| Item | Value |
|---|---|
| Network type | Home Wi-Fi, **2.4GHz only** (ESP32-S3 does not support 5GHz) |
| Laptop local IP | <!-- Run `ipconfig` on your Windows laptop and fill in the IPv4 address shown under "Wireless LAN adapter Wi-Fi", e.g. 192.168.1.50 --> |
| Laptop hostname | <!-- Run `hostname` on your Windows laptop, e.g. ASUS-HOME --> |
| Server port | `8080` |
| E1001 IP assignment | <!-- Recommended: set a DHCP reservation in your router so E1001 always gets the same IP. Alternatively fill in after first boot. --> |
| External exposure | None — server binds to local interface only, no port forwarding |

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
| Firmware framework | ESP-IDF (native Espressif framework) |

**Refresh modes available:**

| Mode | Duration | Flicker | Use case |
|---|---|---|---|
| Full refresh | ~3s | Multiple flashes | Window start, daily maintenance |
| Fast refresh | ~1.5s | Single flash | Content change (calendar, stocks) |
| Partial refresh | ~0.3s | None | Clock tick (time region only) |

### 3.2 Server — Laptop Backend

**Why FastAPI + Uvicorn?**
FastAPI is a modern Python web framework built on top of ASGI (Asynchronous Server Gateway Interface). It was chosen because:
- It handles async data fetching natively — all external API calls (Google Calendar, Polygon, Open-Meteo, Claude) run concurrently without blocking each other, so the render cycle is fast.
- It has built-in support for proper HTTP headers like `ETag` and `304 Not Modified`, which are central to this project's efficiency.
- It is lightweight — no database, no ORM, no complexity beyond what's needed.
- It has excellent documentation and Claude Code handles it well.

Uvicorn is the ASGI server that actually runs FastAPI. Think of FastAPI as the application and Uvicorn as the engine that runs it — similar to how a website is the content and Apache/Nginx is the server that delivers it. Together they are the standard pairing for Python async web services.

**Why not Flask / Django?**
Flask is synchronous by default, which would mean fetching calendar + weather + stocks + Claude one after another (slow). Django is far heavier than needed for a local single-endpoint image server.

**Process management on Windows 10:**
On Windows 10, the recommended approach is to run the FastAPI server as a **Windows Service** using the `NSSM` (Non-Sucking Service Manager) tool. This ensures the server starts automatically when Windows boots, restarts on crash, and runs in the background without a terminal window.

Setup steps (after the server is working manually):
```powershell
# 1. Download NSSM from https://nssm.cc/download
# 2. Open PowerShell as Administrator
# 3. Register the service:
nssm install EpaperDashboard "C:\Python312\python.exe" "-m uvicorn main:app --host 0.0.0.0 --port 8080"
nssm set EpaperDashboard AppDirectory "C:\path\to\your\server"
nssm set EpaperDashboard Start SERVICE_AUTO_START
nssm start EpaperDashboard

# To check status:
nssm status EpaperDashboard

# To stop or remove:
nssm stop EpaperDashboard
nssm remove EpaperDashboard confirm
```

Alternatively, during development, simply run in a terminal:
```powershell
cd C:\path\to\server
python -m uvicorn main:app --host 0.0.0.0 --port 8080 --reload
```

| Property | Value |
|---|---|
| Hardware | Asus A554I |
| OS | Windows 10 |
| Python version | 3.12 |
| Always-on | Yes |
| Server framework | FastAPI + Uvicorn (see rationale above) |
| Image rendering | Pillow (PIL) |
| Process management | NSSM Windows Service (production) / terminal run (development) |

### 3.3 Data Sources

| Source | Provider | Auth method | Update frequency |
|---|---|---|---|
| Calendar | Google Calendar API v3 | OAuth2 (offline token) | Once daily at 05:45, re-fetched at 18:00 |
| Stock prices | Polygon.io | API key | 05:45 and 17:30 daily (see §6.2) |
| Stock suggestions | Claude API (Haiku) | API key | On price change > threshold |
| Weather | Open-Meteo (free, no key) | None | Once daily at 05:45 |
| Quote of the day | Claude API (Haiku) | API key | Once daily at 05:45 |
| Time / Date | System clock (laptop) | N/A | Every render cycle |

### 3.4 Communication Layer

- **Protocol:** HTTP/1.1 over local Wi-Fi (home network only)
- **Image format:** PNG, 1-bit (black/white), 800×480
- **Cache control:** ETag-based — E1001 sends `If-None-Match` header; server returns `304` if unchanged
- **Security:** Local network only. No TLS required for v1. Server is not exposed externally.

If you later wish to access the dashboard remotely (e.g. to force a refresh from your phone), add a reverse proxy with HTTPS and a bearer token rather than exposing port 8080 directly.

---

## 4. Display Layout & Visual Design

### 4.1 Grid Structure

The 800×480 canvas is divided into five zones. The clock is centered, weather is left, calendar is right, stocks occupy the bottom bar, and the quote sits at the very bottom edge.

```
┌─────────────────────────────────────────────────────────────────┐  y=0
│                        DATE BAR                                 │  h=50
│              Wednesday, 29 April 2026  (centered)               │
├────────────────────┬───────────────┬────────────────────────────┤  y=50
│                    │               │                            │
│   WEATHER          │    CLOCK      │   CALENDAR                 │  h=270
│   (left panel)     │   (center)    │   (right panel)            │
│                    │               │                            │
│  ⛅ Partly Cloudy  │   07:42       │  09:00  Standup            │
│  ↑24°  ↓14°        │               │  12:30  Lunch w/ Ana       │
│  Rain 40%          │               │  15:00  Dentist            │
│  Wind 18 km/h      │               │  18:30  Birthday: Mum      │
│                    │               │                            │
├────────────────────┴───────────────┴────────────────────────────┤  y=320
│                       STOCKS BAR                                │  h=80
│  AAPL $189  ▲1.2%   TSLA $172  ▼0.8%   SPY $524  ▲0.4%        │
│  [AI: Hold AAPL — momentum positive but nearing resistance]     │
├─────────────────────────────────────────────────────────────────┤  y=400
│                       QUOTE OF THE DAY                          │  h=80
│   "The only way to do great work is to love what you do."       │
│                                             — Steve Jobs        │
└─────────────────────────────────────────────────────────────────┘  y=480
```

**Zone definitions:**

| Zone | x | y | w | h | Content |
|---|---|---|---|---|---|
| Date bar | 0 | 0 | 800 | 50 | Full date, centered |
| Weather | 0 | 50 | 240 | 270 | Icon + conditions + min/max + rain% + wind |
| Clock | 240 | 50 | 320 | 270 | Large time (HH:MM), centered vertically |
| Calendar | 560 | 50 | 240 | 270 | Up to 4 upcoming events |
| Stocks | 0 | 320 | 800 | 80 | Tickers + AI suggestion |
| Quote | 0 | 400 | 800 | 80 | Quote text + attribution |

**Dividers:** Single-pixel black horizontal lines at y=50, y=320, y=400. Single-pixel vertical lines at x=240 and x=560 (between weather/clock and clock/calendar, from y=50 to y=320).

### 4.2 Typography

All fonts are loaded server-side from TTF files. No font constraints from the ESP32 apply — any TTF/OTF available on the laptop can be used.

| Element | Font | Size | Style | Notes |
|---|---|---|---|---|
| Clock (HH:MM) | Inter | 88pt | Bold | Tabular figures (`tnum` feature) to prevent layout shift |
| Date bar | Inter | 26pt | Medium | Centered horizontally |
| Weather — icon | Nerd Font (NerdFontsSymbolsOnly) | 48pt | Regular | Single glyph mapped from WMO weather code |
| Weather — description | Inter | 18pt | Regular | e.g. "Partly Cloudy" |
| Weather — temperatures | Inter | 20pt | Bold | `↑24° ↓14°` format |
| Weather — rain / wind | Inter | 17pt | Regular | `Rain 40%  Wind 18 km/h` |
| Calendar — time | JetBrains Mono | 18pt | Regular | Fixed-width keeps alignment |
| Calendar — event title | Inter | 18pt | Medium | Truncated at 22 characters |
| Stock ticker | JetBrains Mono | 18pt | Bold | |
| Stock price/change | JetBrains Mono | 17pt | Regular | `▲` / `▼` prefix |
| AI suggestion | Inter | 15pt | Italic | Single line, truncated |
| Quote text | Playfair Display | 17pt | Italic | Adds elegance, suits quoted speech |
| Quote attribution | Inter | 14pt | Regular | Right-aligned, e.g. `— Steve Jobs` |

**Font files to download** (all free from Google Fonts or Nerd Fonts):
- `Inter-Bold.ttf`, `Inter-Medium.ttf`, `Inter-Regular.ttf` → [fonts.google.com](https://fonts.google.com/specimen/Inter)
- `JetBrainsMono-Regular.ttf`, `JetBrainsMono-Bold.ttf` → [fonts.google.com](https://fonts.google.com/specimen/JetBrains+Mono)
- `PlayfairDisplay-Italic.ttf` → [fonts.google.com](https://fonts.google.com/specimen/Playfair+Display)
- `NerdFontsSymbolsOnly.ttf` → [github.com/ryanoasis/nerd-fonts/releases](https://github.com/ryanoasis/nerd-fonts/releases) — download `NerdFontsSymbolsOnly.zip`

Place all font files in `server/fonts/`.

### 4.3 Visual Style Rules

- Background: white (`#FFFFFF` → pixel value `1`)
- Foreground: black (`#000000` → pixel value `0`)
- Zone separators: single-pixel black lines
- No anti-aliasing (1-bit output — Pillow renders with Floyd-Steinberg dithering for text)
- Stock negative values: displayed with `▼` prefix
- Stock positive values: displayed with `▲` prefix
- Weather icons: rendered via Nerd Fonts glyph mapped from WMO weather code (see §6.3)
- Past calendar events: rendered with a horizontal strike-through line drawn over the text

### 4.4 Partial Refresh Region

The clock partial-refresh region covers only the center clock zone. Only this bounding box is redrawn on minute-tick updates. All other zones are updated only on content change or scheduled full refresh.

| Property | Value |
|---|---|
| x | 240 |
| y | 50 |
| width | 320 |
| height | 270 |

---

## 5. Update Schedule & Screen Management

### 5.1 Active Windows

| Window | Start | End | Behaviour |
|---|---|---|---|
| Morning | 05:45 | 08:00 | Full refresh on wake, then partial clock tick every minute |
| Evening | 18:00 | 22:00 | Full refresh on wake, then partial clock tick every minute |

Times are local time on the E1001 (Asia/Jerusalem), synced via NTP at each boot.

### 5.2 Maintenance Refresh

To prevent image persistence (ghosting from holding a static image for extended periods), scheduled full refreshes are performed even when the screen is not actively being viewed:

| Event | Time | Refresh type |
|---|---|---|
| End of morning window | 08:00 | Full refresh → deep sleep |
| Midday maintenance | 12:00 | Full refresh → immediately back to sleep |
| End of evening window | 22:00 | Full refresh → deep sleep |

### 5.3 Refresh Decision Logic (E1001 firmware)

```
On each wake:
  IF scheduled full refresh time (08:00 / 12:00 / 22:00):
    → full refresh → deep sleep
  ELSE IF in active window:
    → GET /display.png from server
    IF 200 OK and ETag changed:
      IF only clock region changed → partial refresh (clock zone only)
      ELSE (data or layout changed) → fast refresh (full screen, 1 flash)
    IF 304 Not Modified → skip display update
  ELSE:
    → skip fetch, deep sleep until next window

After every 5 consecutive partial or fast refreshes:
  → force one full refresh to prevent ghosting accumulation
  → reset refresh counter to 0
```

### 5.4 Sleep Duration Calculation

The firmware computes sleep duration dynamically based on the current time:

- **During active window:** 60 seconds (1-minute clock tick)
- **At end of active window:** sleep until next scheduled event (midday maintenance or next window start)
- **Outside all windows:** sleep until next window start or maintenance time
- **On Wi-Fi failure:** sleep 5 minutes, then retry

### 5.5 NTP Time Synchronisation

The E1001 synchronises its clock via NTP (`il.pool.ntp.org`) at every boot from deep sleep. This ensures the local RTC does not drift over time. Deep sleep cycles are short enough (maximum ~10 hours between the end of the evening window at 22:00 and the morning wake at 05:45) that NTP sync on each boot is sufficient to keep time accurate to within a few seconds.

If NTP sync fails at boot, the firmware falls back to the RTC value and logs a warning to serial. The display will continue to show time from RTC, which may drift slightly but will self-correct on the next successful sync.

---

## 6. Data Integration Specifications

### 6.1 Google Calendar

**Purpose:** Display upcoming events for the current day in the calendar zone.

**Authentication:**
- OAuth2 with offline refresh token
- Credentials stored in `server/secrets/token.json` (generated once via CLI flow)
- Scopes required: `https://www.googleapis.com/auth/calendar.readonly`

**Query parameters:**
- Calendar: Birthdays calendar from primary Google account
- Time range: Now → 23:59 local time (current day only)
- Max results: 5 events
- Ordered by: start time ascending

**Display rules:**
- Show up to 4 events on screen (5th and beyond truncated)
- Format: `HH:MM  Event title` (truncate title at 22 characters with ellipsis)
- All-day events (birthdays): displayed with label `🎂` prefix instead of a time
- Past events (start time already passed): rendered with strike-through
- If no events today: display "No events today" in the calendar zone

**Update schedule:** Fetched once at 05:45. Re-fetched at 18:00 for the evening window to catch any events added during the day.

**Error handling:** If the Calendar API is unavailable, display `Calendar unavailable` in the calendar zone. Do not show stale data from previous day.

---

### 6.2 Stock Data & AI Suggestions

**Purpose:** Show current price and daily change for a personal watchlist. Provide a brief AI-generated buy/hold/sell note.

**Tickers to track:**

> **[ EDIT ]** List your tickers here:
> - `<!-- e.g. AAPL -->`
> - `<!-- e.g. TSLA -->`
> - `<!-- e.g. SPY -->`
> - `<!-- add more -->`

**Data provider:** Polygon.io

**Recommended update schedule:**

Stock updates are fetched at two fixed times per day rather than continuously. This approach is sensible for this dashboard because:
- US market opens at 09:30 ET (16:30 Israel time) and closes at 16:00 ET (23:00 Israel time).
- The morning window (05:45–08:00 Israel) falls **before** US market open — showing the previous day's close is the most meaningful snapshot available.
- The evening window (18:00–22:00 Israel) falls **during** US market hours — showing a mid-session update is timely and actionable.

| Fetch time (Israel) | US market state | What is shown |
|---|---|---|
| **05:45** | Pre-market / closed | Previous day closing prices + final daily change |
| **17:30** | Market open (~1hr in) | Current intraday price + change from previous close |

This gives you one meaningful morning context snapshot and one live afternoon update without hammering the API. Polygon.io free tier allows 5 API calls per minute, which is more than sufficient for this pattern.

**Fields fetched per ticker:**
- Current price
- Daily change (absolute $ and %)
- Previous close

**Market status display:**
- 05:45 fetch: append `[prev. close]` label to prices
- 17:30 fetch: show live price, no label (understood to be intraday)
- If market closed and no intraday data available: show `[closed]` indicator

**AI suggestions (Claude API):**

| Property | Value |
|---|---|
| Model | `claude-haiku-4-5-20251001` |
| Max tokens | 60 |
| Trigger | Price change > 1.5% since last suggestion, or at each scheduled fetch |
| Prompt template | `"[TICKER] is at $[PRICE], [CHANGE]% today. Give a one-sentence buy/hold/sell suggestion."` |
| Display | Single italic line in the stocks zone, shared across all tickers |
| Cooldown | Minimum 30 minutes between AI calls per ticker |

> Note: Claude suggestions are informational only — not financial advice.

**Rate limiting:**
- Stock API: 2 fetches per day (05:45 and 17:30) — well within Polygon.io free tier
- AI suggestions: maximum 1 call per ticker per 30 minutes

---

### 6.3 Weather

**Purpose:** Show today's conditions in the left weather panel.

**Provider:** Open-Meteo (`https://api.open-meteo.com`) — free, no API key required.

**Location:**
- Latitude: `32.08`
- Longitude: `34.78`
- City: Bat Yam, Israel
- Timezone: `Asia/Jerusalem`

**Fields fetched:**

| Field | API parameter | Display format |
|---|---|---|
| Current temperature | `current=temperature_2m` | `Now: 19°C` |
| Today's max temperature | `daily=temperature_2m_max` | `↑ 24°C` |
| Today's min temperature | `daily=temperature_2m_min` | `↓ 14°C` |
| Weather condition | `current=weather_code` | Icon + text (see below) |
| Daily precipitation probability | `daily=precipitation_probability_max` | `Rain 40%` |
| Wind speed | `current=wind_speed_10m` | `Wind 18 km/h` |

**Weather icon mapping (WMO code → Nerd Font glyph):**

| Condition | WMO codes | Nerd Font glyph |
|---|---|---|
| Clear / Sunny | 0 | `󰖙` (nf-md-weather_sunny) |
| Partly Cloudy | 1, 2 | `󰖕` (nf-md-weather_partly_cloudy) |
| Overcast | 3 | `󰖐` (nf-md-weather_cloudy) |
| Foggy | 45, 48 | `󰖑` (nf-md-weather_fog) |
| Drizzle | 51–57 | `󰖗` (nf-md-weather_rainy) |
| Rain | 61–67 | `󰖖` (nf-md-weather_pouring) |
| Thunderstorm | 95–99 | `󰖓` (nf-md-weather_lightning_rainy) |
| Snow | 71–77, 85–86 | `󰖘` (nf-md-weather_snowy) |
| Windy | wind_speed > 40 km/h (any) | `󰖞` (nf-md-weather_windy) |

> Icons are rendered server-side using the NerdFontsSymbolsOnly TTF at 48pt. The icon is drawn in the weather zone above the text fields.

**Update frequency:** Once daily at 05:45. Weather data for the full day (including min/max) is fetched in one API call and cached. Not re-fetched for the evening window since daily min/max do not change.

**Units:** Celsius, km/h.

---

### 6.4 Clock & Date

**Purpose:** Display accurate current time and date. The time is the most frequently updated element via partial refresh.

| Element | Format | Example | Zone |
|---|---|---|---|
| Time | `H:MM` (24h, no leading zero) | `7:42` | Clock (center) |
| Date bar | `Day, DD Month YYYY` | `Wednesday, 29 April 2026` | Date bar (top, centered) |

**Time source:** System clock on the E1001, synced via NTP at each boot from deep sleep.

**NTP server:** `il.pool.ntp.org` (Israeli NTP pool — geographically closest)

**Timezone:** `Asia/Jerusalem`
**POSIX TZ string (for firmware):** `IST-2IDT,M3.4.4/26,M10.5.0`

---

### 6.5 Quote of the Day

**Purpose:** Display an uplifting or thought-provoking quote from a well-known person in the quote zone at the bottom of the screen. The quote should feel personal, warm, and appropriate for a shared household display.

**Generation method:** Claude API call made once daily at 05:45, result cached for the full day.

| Property | Value |
|---|---|
| Model | `claude-haiku-4-5-20251001` |
| Max tokens | 100 |
| Prompt | See below |
| Cache | Stored in memory, regenerated each morning at 05:45 |
| Display | Quote text (italic, Playfair Display) + attribution (regular, right-aligned) |

**Prompt template:**
```
Give me one short, uplifting quote from a well-known historical figure, 
scientist, artist, or thinker. The quote should be about life, creativity, 
perseverance, or curiosity. Reply with ONLY the quote and attribution in 
this exact format:
"Quote text here." — Person Name
Do not add any other text.
```

**Display rules:**
- Quote text: max 120 characters. If the generated quote exceeds this, regenerate once.
- Attribution: right-aligned below the quote, prefixed with `—`
- If Claude API fails: display a hardcoded fallback quote from a local list of 10 pre-written quotes

**Fallback quote list** (stored in `server/quotes_fallback.json`):
> **[ EDIT ]** Populate this file with 10 favourite quotes as a backup.

---

## 7. Firmware Specification (E1001)

### 7.1 Framework & Libraries

The firmware is implemented using **ESP-IDF** (Espressif IoT Development Framework), the native framework for ESP32 chips. ESP-IDF is chosen over Arduino because it provides lower-level control over Wi-Fi power management, precise deep sleep timing, NVS storage, and the SPI bus — all of which are important for this project.

| Item | Choice | Notes |
|---|---|---|
| Framework | ESP-IDF v5.x | Native Espressif framework |
| ePaper driver | `epaper` component (custom or community port for UC8179) | GxEPD2 is Arduino-only; for ESP-IDF use the Espressif `esp_lcd_touch` ecosystem or a community UC8179 driver |
| PNG decoder | `pngle` (lightweight C library) | Suitable for ESP-IDF; handles 1-bit PNG decoding in constrained RAM |
| HTTP client | `esp_http_client` (built-in ESP-IDF component) | Supports custom headers for ETag |
| NVS storage | `nvs_flash` + `nvs_handle` (built-in) | Stores ETag, refresh counter, last full refresh timestamp |
| NTP | `esp_sntp` (built-in ESP-IDF component) | Configured via `sntp_setservername()` |
| Deep sleep | `esp_sleep_enable_timer_wakeup()` + `esp_deep_sleep_start()` | Built-in ESP-IDF |
| Wi-Fi | `esp_wifi` (built-in) | Station mode only |
| SPI bus | `driver/spi_master.h` (built-in) | Used to drive ePaper over SPI |

**Note on ePaper driver:** GxEPD2 is an Arduino library and cannot be used directly in ESP-IDF. The recommended approach is to use the lower-level SPI commands to the UC8179 controller directly, following the controller datasheet and the waveform lookup tables from Seeed's reference firmware. Alternatively, the Zephyr Project has a board definition for the E1001 that can serve as a driver reference.

### 7.2 Firmware State Machine

```
BOOT (from deep sleep or power-on)
  │
  ├─► Initialize SPI, NVS, GPIO
  │
  ├─► Connect Wi-Fi (timeout: 15s)
  │     SUCCESS → continue
  │     FAILURE after 3 retries →
  │       Display error message on screen: "Wi-Fi connection failed.
  │       Check network settings. Retrying in 5 min."
  │       → deep sleep 5 minutes → retry
  │
  ├─► Sync NTP time (il.pool.ntp.org)
  │     FAILURE → use RTC time, log warning
  │
  ├─► Determine current mode based on local time:
  │
  │   SCHEDULED_FULL_REFRESH (08:00 / 12:00 / 22:00):
  │     → perform full ePaper refresh (blank screen, then white)
  │     → compute sleep until next event
  │     → deep sleep
  │
  │   ACTIVE_WINDOW (05:45–08:00 or 18:00–22:00):
  │     → GET /display.png with If-None-Match: <nvs:last_etag>
  │       200 OK → decode PNG → select refresh mode → update display
  │                → store new ETag to NVS
  │       304 Not Modified → skip display update
  │       Error → skip display update, log to UART
  │     → sleep 60 seconds
  │
  │   INACTIVE (all other times):
  │     → compute sleep duration until next window or maintenance
  │     → deep sleep
  │
  └─► Enter deep sleep
```

### 7.3 Wi-Fi Error Display Requirement

If Wi-Fi fails to connect after 3 retries, the firmware must display a human-readable error message on the ePaper screen before sleeping. This is the only case where the firmware drives the display without a server-provided image.

The error screen is a simple full refresh displaying:
```
┌─────────────────────────────────────────────────────────┐
│                                                         │
│   ⚠  Wi-Fi connection failed                           │
│                                                         │
│   Could not connect to: <SSID>                          │
│   Retrying in 5 minutes.                               │
│                                                         │
│   Check that the network is available and that the     │
│   SSID and password in firmware config are correct.    │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

The error text is rendered directly on the ESP32 using the UC8179 driver with a hardcoded built-in font (no server PNG needed). This is a self-contained firmware capability.

### 7.4 Build Configuration (ESP-IDF `CMakeLists.txt` + `sdkconfig`)

ESP-IDF uses CMake as its build system, not `platformio.ini`. The project structure is:

```
firmware/
├── CMakeLists.txt           # Top-level CMake
├── sdkconfig                # Generated by `idf.py menuconfig`
├── sdkconfig.defaults       # Checked-in default config values
├── main/
│   ├── CMakeLists.txt
│   ├── main.c               # App entry point
│   ├── wifi.c / wifi.h
│   ├── epaper.c / epaper.h  # UC8179 SPI driver
│   ├── http_client.c        # PNG fetch + ETag logic
│   ├── png_decode.c         # pngle integration
│   ├── nvs_store.c          # ETag + counter persistence
│   ├── schedule.c           # Active window + sleep calculation
│   └── config.h             # User configuration (see §7.5)
└── components/
    └── pngle/               # Vendored PNG decoder library
```

**Top-level `CMakeLists.txt`:**
```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(epaper_dashboard)
```

**`sdkconfig.defaults` (key settings):**
```
# Flash
CONFIG_ESPTOOLPY_FLASHSIZE_32MB=y
CONFIG_ESPTOOLPY_FLASHMODE_DIO=y

# PSRAM (OPI PSRAM on S3R8)
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y

# Wi-Fi
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=4
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=8

# Power / Sleep
CONFIG_PM_ENABLE=y
CONFIG_FREERTOS_HZ=100

# Logging
CONFIG_LOG_DEFAULT_LEVEL_INFO=y

# NVS
CONFIG_NVS_ASSERT_ERROR_CHECK=y
```

**Build and flash commands (run in ESP-IDF terminal):**
```bash
# First time setup — activate ESP-IDF environment
. $IDF_PATH/export.sh          # Linux/Mac
# or on Windows: %IDF_PATH%\export.bat

# Configure (opens menuconfig UI)
idf.py set-target esp32s3
idf.py menuconfig

# Build
idf.py build

# Flash (E1001 connected via USB-C to /dev/ttyUSB0 on Linux)
idf.py -p /dev/ttyUSB0 flash monitor

# Flash only (no monitor)
idf.py -p /dev/ttyUSB0 flash
```

### 7.5 Configuration (`main/config.h`)

```c
// ============================================================
// [ EDIT ] — fill in before building
// ============================================================

// Wi-Fi
#define WIFI_SSID           "<!-- your home Wi-Fi SSID -->"
#define WIFI_PASSWORD       "<!-- your Wi-Fi password -->"
#define WIFI_RETRY_MAX      3
#define WIFI_TIMEOUT_MS     15000

// Server
#define SERVER_HOST         "<!-- laptop local IP, e.g. 192.168.1.50 -->"
#define SERVER_PORT         8080
#define SERVER_PATH         "/display.png"

// NTP
#define NTP_SERVER          "il.pool.ntp.org"
#define TZ_POSIX_STRING     "IST-2IDT,M3.4.4/26,M10.5.0"

// Active windows (local time, 24h)
#define MORNING_START_H     5
#define MORNING_START_M     45
#define MORNING_END_H       8
#define MORNING_END_M       0
#define EVENING_START_H     18
#define EVENING_START_M     0
#define EVENING_END_H       22
#define EVENING_END_M       0

// Maintenance full refresh times
#define MAINTENANCE_1_H     8
#define MAINTENANCE_1_M     0
#define MAINTENANCE_2_H     12
#define MAINTENANCE_2_M     0
#define MAINTENANCE_3_H     22
#define MAINTENANCE_3_M     0

// Refresh ghosting prevention
#define MAX_PARTIAL_BEFORE_FULL  5

// ePaper SPI pins (do not change — hardware fixed)
#define EPAPER_CLK_PIN      7
#define EPAPER_MOSI_PIN     9
#define EPAPER_CS_PIN       10
#define EPAPER_DC_PIN       8
#define EPAPER_RST_PIN      47
#define EPAPER_BUSY_PIN     48
```

---

## 8. Server Software Specification

### 8.1 Directory Structure

```
server/
├── main.py                  # FastAPI app entry point
├── renderer.py              # Pillow canvas rendering logic
├── scheduler.py             # Data fetch scheduling (APScheduler)
├── sources/
│   ├── calendar.py          # Google Calendar integration
│   ├── stocks.py            # Polygon.io stock fetching
│   ├── weather.py           # Open-Meteo integration
│   ├── suggestions.py       # Claude API stock suggestions
│   └── quote.py             # Claude API quote of the day
├── fonts/
│   ├── Inter-Bold.ttf
│   ├── Inter-Medium.ttf
│   ├── Inter-Regular.ttf
│   ├── JetBrainsMono-Bold.ttf
│   ├── JetBrainsMono-Regular.ttf
│   ├── PlayfairDisplay-Italic.ttf
│   └── NerdFontsSymbolsOnly.ttf
├── secrets/
│   ├── credentials.json     # Google OAuth client secret (never commit)
│   ├── token.json           # Google OAuth refresh token (never commit)
│   └── .env                 # All API keys and config (never commit)
├── cache/
│   └── display.png          # Latest rendered image
├── quotes_fallback.json     # 10 hardcoded fallback quotes
├── requirements.txt
├── nssm_install.ps1         # Windows Service setup script
└── CLAUDE.md                # Project context for Claude Code
```

### 8.2 API Endpoints

| Endpoint | Method | Description |
|---|---|---|
| `/display.png` | GET | Returns current rendered PNG. Supports `If-None-Match` ETag header. Returns `304` if unchanged. |
| `/status` | GET | Returns JSON: last render time, data source statuses, next scheduled fetch times, current ETag. |
| `/refresh` | POST | Forces immediate re-render and re-fetch of all data sources. |

### 8.3 Render Pipeline

```
scheduler triggers render()
  │
  ├─► Fetch all sources in parallel (asyncio.gather):
  │     calendar.get_events()
  │     weather.get_today()          [cached after 05:45 fetch]
  │     stocks.get_prices(tickers)   [cached after scheduled fetch]
  │     quote.get_today()            [cached after 05:45 fetch]
  │     suggestions.get_suggestion() [called only on trigger]
  │
  ├─► renderer.compose(data) → PIL Image (800×480, mode "1")
  │     draw_date_bar()
  │     draw_weather_zone()           [includes Nerd Font icon]
  │     draw_clock_zone()
  │     draw_calendar_zone()
  │     draw_stocks_zone()
  │     draw_quote_zone()
  │     draw_divider_lines()
  │
  ├─► Compute MD5 hash of PNG bytes → new ETag
  │
  ├─► If ETag changed → save to cache/display.png, update stored ETag
  │
  └─► Return (image_bytes, etag)
```

### 8.4 Scheduling

| Task | Schedule | Notes |
|---|---|---|
| Full render | Every 60s during active windows; every 30 min otherwise | Always runs — E1001 uses ETag to decide whether to refresh |
| Weather fetch | Daily at 05:45 | Full day data fetched once; includes min/max |
| Stock fetch | Daily at 05:45 and 17:30 | See §6.2 for rationale |
| Calendar fetch | Daily at 05:45 and 18:00 | Re-fetched for evening to catch new events |
| Quote generation | Daily at 05:45 | Cached all day |
| AI suggestions | On trigger: price change > 1.5% or at scheduled fetch | Cooldown 30 min per ticker |

Scheduling implemented with `APScheduler` (AsyncIOScheduler):
```bash
pip install apscheduler
```

### 8.5 `CLAUDE.md` (for Claude Code sessions)

```markdown
# CLAUDE.md — ePaper Dashboard Server

## What this project does
Python FastAPI server running on Windows 10 (Asus A554I laptop).
Renders an 800×480 1-bit PNG dashboard and serves it to a
Seeed Studio reTerminal E1001 ePaper display over local Wi-Fi via HTTP.

## Commands
- Run (development): `python -m uvicorn main:app --host 0.0.0.0 --port 8080 --reload`
- Force render:       `curl -X POST http://localhost:8080/refresh`
- Check status:       `curl http://localhost:8080/status`
- Install as service: `powershell -File nssm_install.ps1`

## Critical constraints
- Output image MUST be exactly 800×480 pixels, PIL mode "1" (1-bit, black/white)
- Fonts MUST be loaded with ImageFont.truetype() from server/fonts/ — never use default fonts
- All data fetching is async — use httpx.AsyncClient, never the `requests` library
- Secrets are in server/secrets/.env — never hardcode any key or credential in source

## Layout zones (pixels)
- Date bar:  x=0,   y=0,   w=800, h=50
- Weather:   x=0,   y=50,  w=240, h=270
- Clock:     x=240, y=50,  w=320, h=270
- Calendar:  x=560, y=50,  w=240, h=270
- Stocks:    x=0,   y=320, w=800, h=80
- Quote:     x=0,   y=400, w=800, h=80

## Data source notes
- Google Calendar: token.json auto-refreshes; do not regenerate credentials
- Stocks: Polygon.io, fetched at 05:45 (prev close) and 17:30 (intraday)
- Weather: Open-Meteo, no API key, lat=32.08 lon=34.78, fetch at 05:45 only
- Quote: Claude Haiku, generated at 05:45, cached all day
- AI suggestions: Claude Haiku, max 60 tokens, 30 min cooldown per ticker
- Fallback quotes: server/quotes_fallback.json (10 items)
```

---

## 9. Configuration & Secrets Management

### 9.1 Server `.env` file

```env
# ── Google Calendar ──────────────────────────────────────────
GOOGLE_CREDENTIALS_PATH=secrets/credentials.json
GOOGLE_TOKEN_PATH=secrets/token.json
GOOGLE_CALENDAR_ID=primary

# ── Stocks ───────────────────────────────────────────────────
STOCK_PROVIDER=polygon
STOCK_API_KEY=<!-- your Polygon.io API key -->
STOCK_TICKERS=<!-- comma-separated, e.g. AAPL,TSLA,SPY -->
STOCK_FETCH_TIMES=05:45,17:30
STOCK_SUGGESTION_TRIGGER_PCT=1.5
STOCK_SUGGESTION_COOLDOWN_MIN=30

# ── Weather ──────────────────────────────────────────────────
WEATHER_LAT=32.08
WEATHER_LON=34.78
WEATHER_TIMEZONE=Asia/Jerusalem
WEATHER_UNITS=celsius

# ── Claude API ───────────────────────────────────────────────
ANTHROPIC_API_KEY=<!-- your Anthropic API key -->
QUOTE_PROMPT_MAX_CHARS=120

# ── Server ───────────────────────────────────────────────────
SERVER_HOST=0.0.0.0
SERVER_PORT=8080
CACHE_DIR=cache
FONTS_DIR=fonts
```

### 9.2 Secrets That Must Never Be Committed

Add to `.gitignore`:
```
server/secrets/
server/.env
*.env
```

---

## 10. Error Handling & Fallback Behavior

| Failure scenario | Behaviour |
|---|---|
| **Wi-Fi fails at boot (E1001)** | Retry 3 times with 5s delay. On final failure: display Wi-Fi error screen (see §7.3), deep sleep 5 min, retry |
| **NTP sync fails (E1001)** | Use RTC time, log warning to UART serial. Continue normally — time may drift slightly |
| **Server unreachable (E1001)** | Use last displayed image (no update). Sleep and retry next minute |
| **Google Calendar API error** | Display `Calendar unavailable` in calendar zone. Do not show stale data |
| **Polygon.io error / rate limit** | Display last known price with `[delayed]` label |
| **Claude API error (suggestions)** | Suppress suggestion line; leave that slot empty |
| **Claude API error (quote)** | Select a random quote from `quotes_fallback.json` |
| **Weather API error** | Display `Weather unavailable` in weather zone |
| **Server render exception** | Log full traceback to console. Serve last valid cached PNG unchanged. Do not crash the server |
| **PNG decode error (E1001)** | Skip display update, log error to UART, sleep normally |

---

## 11. Development Toolchain

### 11.1 Laptop / Server Side (Windows 10)

| Tool | Purpose | Install |
|---|---|---|
| Python 3.12 | Server runtime | [python.org](https://python.org) |
| FastAPI + Uvicorn | HTTP server | `pip install fastapi uvicorn` |
| Pillow | Image rendering | `pip install pillow` |
| httpx | Async HTTP client | `pip install httpx` |
| APScheduler | Task scheduling | `pip install apscheduler` |
| google-api-python-client | Google Calendar SDK | `pip install google-api-python-client google-auth-oauthlib` |
| anthropic | Claude API SDK | `pip install anthropic` |
| python-dotenv | `.env` loading | `pip install python-dotenv` |
| NSSM | Windows Service manager | [nssm.cc](https://nssm.cc/download) |
| Node.js | Required for Claude Code | [nodejs.org](https://nodejs.org) |
| Claude Code | AI-assisted development | `npm install -g @anthropic-ai/claude-code` |

**Full install:**
```powershell
pip install fastapi uvicorn pillow httpx apscheduler google-api-python-client `
            google-auth-oauthlib anthropic python-dotenv
```

### 11.2 E1001 Firmware Side (developed from Arch Linux)

| Tool | Purpose | Install |
|---|---|---|
| ESP-IDF v5.x | Build system + framework | `yay -S esp-idf` (installs to `/opt/esp-idf`) |
| esptool | Firmware flashing | `pip install esptool --break-system-packages` |
| OpenOCD (Espressif fork) | JTAG debugging (optional) | [github.com/espressif/openocd-esp32](https://github.com/espressif/openocd-esp32/releases) |

```bash
# Activate ESP-IDF environment (run before any idf.py command)
source /opt/esp-idf/export.sh

# Build
cd firmware/
idf.py set-target esp32s3
idf.py build

# Flash (E1001 on /dev/ttyUSB0)
idf.py -p /dev/ttyUSB0 flash monitor
```

> **Note:** Keep `PLATFORMIO_CORE_DIR=/opt/platformio` set in `.bashrc` from the earlier disk space fix. PlatformIO is not used for this project's firmware but the env var prevents any accidental installs to `/home`.

### 11.3 Claude Code Setup

```bash
# Install (requires Node.js)
npm install -g @anthropic-ai/claude-code

# Start a session in the server directory
cd server/
claude   # reads CLAUDE.md automatically

# Start a session in the firmware directory
cd firmware/
claude   # reads firmware/CLAUDE.md (create a separate one for firmware context)
```

---

## 12. Open Questions & Decisions Pending

> Use this section to track unresolved decisions. Remove items as they are decided.

| # | Question | Options / Notes | Decision |
|---|---|---|---|
| 1 | Tickers to track | Fill in your personal watchlist in §6.2 and `.env` | <!-- TBD --> |
| 2 | Additional calendars | Currently: Birthdays from primary only. Add shared family calendar? | <!-- TBD --> |
| 3 | Stock suggestion trigger | Currently set to 1.5% daily change. Adjust in `.env` | <!-- TBD --> |
| 4 | Laptop hostname vs IP | Using static IP is more reliable. Set DHCP reservation in router for laptop MAC | <!-- TBD --> |
| 5 | E1001 IP assignment | Recommend DHCP reservation in router so IP is stable across reboots | <!-- TBD --> |
| 6 | v1 button behaviour | Green button could force a manual full refresh — useful for testing | <!-- TBD --> |
| 7 | Fallback quote list | Populate `server/quotes_fallback.json` with 10 personal favourite quotes | <!-- TBD --> |
| 8 | UC8179 driver source | Use Seeed reference firmware waveforms vs community ESP-IDF port | <!-- TBD --> |
| 9 | Quote style preference | Currently general life/creativity themes. Narrow to specific themes? | <!-- TBD --> |

---

## Appendix A — Change Log

| Version | Date | Changes |
|---|---|---|
| 0.1 | 01.05.2026 | Initial draft |
| 0.2 | 01.05.2026 | Added quote of the day (§6.5); expanded network topology with setup commands and security explanation; added server process management rationale (NSSM, FastAPI/Uvicorn choice explained); redesigned grid layout (clock center, weather left, calendar right, quote zone); added explicit weather fields (min/max temp, wind, Nerd Font icons); NTP sync every boot clarified in §5.5; firmware changed to ESP-IDF throughout §7 (libraries, build config, state machine); Wi-Fi error screen requirement added (§7.3); stock update schedule changed to 05:45 + 17:30 with rationale (§6.2); `sources/quote.py` added to directory structure; open questions updated |

---

*All `<!-- EDIT -->` and `<!-- TBD -->` markers indicate places requiring your input before implementation begins.*
