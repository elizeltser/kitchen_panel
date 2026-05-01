# Functional Specification: ePaper Smart Dashboard System
**Document version:** 0.4 — Draft  
**Last updated:** 1.5.2026  
**Author:** Eli Zeltser  
**Status:** In Progress

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [System Architecture](#2-system-architecture)
3. [Component Specifications](#3-component-specifications)
4. [Display Layout & Visual Design](#4-display-layout--visual-design)
5. [Update Schedule & Screen Management](#5-update-schedule--screen-management)
6. [Data Integration Specifications](#6-data-integration-specifications)
   - 6.1 [Google Calendar](#61-google-calendar)
   - 6.2 [Stock Data, Portfolio & AI Suggestions](#62-stock-data-portfolio--ai-suggestions)
   - 6.3 [Weather](#63-weather)
   - 6.4 [Clock & Date](#64-clock--date)
   - 6.5 [Quote of the Day](#65-quote-of-the-day)
7. [Firmware Specification (E1001)](#7-firmware-specification-e1001)
8. [Server Software Specification](#8-server-software-specification)
9. [Management UI — Admin Panel](#9-management-ui--admin-panel)
10. [Configuration & Secrets Management](#10-configuration--secrets-management)
11. [Error Handling & Fallback Behavior](#11-error-handling--fallback-behavior)
12. [Development Toolchain](#12-development-toolchain)
13. [Open Questions & Decisions Pending](#13-open-questions--decisions-pending)
14. [Appendix A — Change Log](#appendix-a--change-log)

---

## 1. Project Overview

### 1.1 Purpose

This document describes the functional requirements of a personal smart dashboard built on a **Seeed Studio reTerminal E1001** 7.5-inch monochrome ePaper display. The system provides an at-a-glance view of time, weather, birthday reminders, stock portfolio status, and a daily quote during defined morning and evening periods. An AI layer provides portfolio-aware buy/hold/sell suggestions and generates the daily quote. All dashboard content and configuration is manageable via a local web admin panel.

### 1.2 Goals

- Display accurate, readable information during two daily active windows without requiring interaction.
- Maximize screen lifespan by minimizing unnecessary full refreshes and enforcing a safe refresh schedule.
- Keep the display firmware simple and stateless — all intelligence and rendering lives on the laptop server.
- Provide a local admin panel to manage quotes, stock watchlist, and portfolio without editing files manually.
- Enable future extension of data sources without reflashing the device.
- Provide a pleasant shared morning and evening experience for the household.

### 1.3 Non-Goals

- This is not a real-time trading terminal. Stock suggestions are informational only and not financial advice.
- The display is not interactive beyond the green button manual refresh (v1).
- The system does not need to function when the laptop server is off or unreachable.

### 1.4 Intended Users

This project will be used specifically for our home, as a display for us to view and enjoy in our joint mornings and evenings.

---

## 2. System Architecture

### 2.1 High-Level Overview

The system follows a **server-rendered pull model**. The laptop serves as the intelligence layer; the E1001 is a dumb display that polls for a pre-rendered image.

```
┌──────────────────────────────────────────────┐
│    Asus A554I Laptop (Server)                │
│    Windows 10 — Ethernet — 10.100.102.216    │
│    hostname: DESKTOP-NJR6V52                 │
│                                              │
│  ┌──────────────┐   ┌─────────────────────┐  │
│  │ Data Fetcher │──►│ Image Renderer      │  │
│  │              │   │ (Pillow → PNG)      │  │
│  └──────────────┘   └────────┬────────────┘  │
│   Google Calendar            │               │
│   Polygon.io          FastAPI HTTP :8080      │
│   Open-Meteo                 │               │
│   Claude API          ┌──────┴──────┐        │
│                       │ Admin Panel │        │
│  ┌────────────────┐   │ (Web UI)    │        │
│  │ portfolio.json │   └─────────────┘        │
│  │ quotes.json    │                          │
│  │ tickers.json   │                          │
│  └────────────────┘                          │
└──────────────────────────────│───────────────┘
                               │  Home LAN (router 10.100.102.1)
                               │  Laptop:  Ethernet, DHCP-reserved IP
                               │  E1001:   Wi-Fi 2.4GHz
                               │  HTTP GET /display.png
                               ▼
                   ┌───────────────────────┐
                   │   reTerminal E1001    │
                   │   Zephyr RTOS         │
                   │                       │
                   │  Poll → ETag check    │
                   │  Decode PNG           │
                   │  Refresh ePaper       │
                   │  PM sleep             │
                   └───────────────────────┘
```

### 2.2 Data Flow

```
Every render cycle (laptop side):
  1. Fetch fresh data from all sources (calendar, weather, stocks, quote)
  2. Load portfolio from portfolio.json
  3. Call Claude API for portfolio-aware suggestion if trigger conditions met
  4. Composite all data into an 800×480 1-bit PNG
  5. Cache PNG with a new ETag if content changed
  6. Serve via HTTP

Every poll cycle (E1001 side):
  1. Wake from PM sleep (timer or GPIO button)
  2. Connect to Wi-Fi
  3. GET /display.png with If-None-Match: <last_etag>
  4. If 304 → skip display update, go back to sleep
  5. If 200 → decode PNG, choose refresh mode, update display
  6. Store new ETag in NVS settings
  7. Compute sleep duration until next poll
  8. Enter PM sleep
```

### 2.3 Network Topology

The laptop's IP is **reserved via DHCP in the router** (`10.100.102.216`). This means the router always assigns the same IP to the laptop's Ethernet adapter based on its MAC address — no Windows-side static IP configuration is needed. The router handles the reservation, Windows uses normal DHCP, and the IP is stable across reboots.

**No static IP configuration is required on Windows.** The DHCP reservation in the router achieves the same result more reliably.

**How to verify the reservation is working:**
```powershell
# Should show 10.100.102.216 under Ethernet adapter
ipconfig

# Confirm server will be reachable on correct IP after start
netstat -an | findstr "8080"
```

**How to check the server is NOT exposed externally:**
As long as your router has no port-forwarding rule for port 8080, the server is local-only. The default router configuration is closed to inbound connections. To verify:
```powershell
# Check what's listening — should show 0.0.0.0:8080, not your public IP
netstat -an | findstr "8080"
# Your public IP can be checked at https://whatismyip.com — it differs from 10.100.102.216
```

| Item | Value |
|---|---|
| Network type | Home LAN — Laptop via Ethernet, E1001 via 2.4GHz Wi-Fi |
| Laptop connection | Ethernet |
| Laptop IP | `10.100.102.216` (DHCP-reserved in router — no Windows config needed) |
| Laptop hostname | `DESKTOP-NJR6V52` |
| Router gateway | `10.100.102.1` |
| Server port | `8080` |
| Admin panel port | `8080` (same server, different routes — see §9) |
| E1001 IP assignment | `10.100.102.4` (DHCP-reserved in router - no additional config needed) | 
| External exposure | None — no port forwarding, default router firewall |

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
| Buttons | Left=GPIO4, Right=GPIO5, Green=GPIO3 |
| LED | GPIO6 (active LOW) |
| Buzzer | GPIO45 |
| Firmware framework | Zephyr RTOS |

**Refresh modes:**

| Mode | Duration | Flicker | Use case |
|---|---|---|---|
| Full refresh | ~3s | Multiple flashes | Window start, daily maintenance, button press |
| Fast refresh | ~1.5s | Single flash | Content change (stocks, calendar) |
| Partial refresh | ~0.3s | None | Clock tick (time region only) |

### 3.2 Server — Laptop Backend

**FastAPI + Uvicorn** is used because it handles async data fetching natively (all API calls run concurrently), has built-in ETag/`304 Not Modified` support, and is lightweight enough to run as a background Windows service. It also hosts the admin panel on the same server process.

**Virtual environment location:** `C:\Users\Eli Zeltser\Documents\reTerminal\.venv`

```powershell
# Activate venv (required before any python/pip command)
C:\Users\Eli Zeltser\Documents\reTerminal\.venv\Scripts\Activate.ps1

# If blocked by execution policy, run once as Administrator:
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
```

**NSSM Windows Service** (run once, after development is working):
```powershell
# Open PowerShell as Administrator
$venvPython = "C:\Users\Eli Zeltser\Documents\reTerminal\.venv\Scripts\python.exe"
$appDir     = "C:\Users\Eli Zeltser\Documents\reTerminal\server"

nssm install EpaperDashboard $venvPython "-m uvicorn main:app --host 0.0.0.0 --port 8080"
nssm set EpaperDashboard AppDirectory $appDir
nssm set EpaperDashboard Start SERVICE_AUTO_START
nssm start EpaperDashboard

nssm status EpaperDashboard   # check
nssm stop   EpaperDashboard   # stop
nssm remove EpaperDashboard confirm  # remove
```

**Development run:**
```powershell
cd "C:\Users\Eli Zeltser\Documents\reTerminal\server"
python -m uvicorn main:app --host 0.0.0.0 --port 8080 --reload
```

| Property | Value |
|---|---|
| Hardware | Asus A554I |
| OS | Windows 10 |
| Python version | 3.12 |
| Always-on | Yes |
| Project root | `C:\Users\Eli Zeltser\Documents\reTerminal\` |
| Virtual environment | `C:\Users\Eli Zeltser\Documents\reTerminal\.venv` |
| Server directory | `C:\Users\Eli Zeltser\Documents\reTerminal\server\` |
| Server framework | FastAPI + Uvicorn |
| Image rendering | Pillow (PIL) |
| Admin panel | Served by same FastAPI process on `/admin` routes |
| Process management | NSSM Windows Service (production) / manual venv run (development) |

### 3.3 Data Sources

| Source | Provider | Auth | Update frequency |
|---|---|---|---|
| Calendar (birthdays) | Google Calendar API v3 | OAuth2 offline token | 05:45 and 18:00 daily |
| Stock prices | Polygon.io | API key | 05:45 and 17:30 daily |
| Stock suggestions | Claude API (Haiku) | API key | On trigger or scheduled fetch |
| Portfolio data | Local `portfolio.json` | None (local file) | On admin save |
| Weather | Open-Meteo (free, no key) | None | Once daily at 05:45 |
| Quote of the day | Claude API (Haiku) | API key | Once daily at 05:45 |
| Time / Date | E1001 system clock (NTP-synced) | N/A | Every render cycle |

### 3.4 Communication Layer

- **Protocol:** HTTP/1.1 over home LAN
- **Display image:** PNG, 1-bit (black/white), 800×480, served with ETag
- **Admin panel:** HTML pages served by FastAPI on `/admin/*` routes
- **Data API:** JSON endpoints on `/api/*` routes for admin panel interactions
- **Security:** Local network only. No TLS in v1.

---

## 4. Display Layout & Visual Design

### 4.1 Grid Structure

The 800×480 canvas has five zones. Quote fills the top. Below it: weather on the left, clock (large) in the center-right with birthdays underneath it if any exist. Stocks anchor the bottom. A narrow footer shows date and current time.

```
┌──────────────────────────────────────────────────────────────────┐ y=0
│  "The mystery of life isn't a problem to solve, but a reality    │ h=70
│   to experience."                      — Frank Herbert, Dune     │
├──────────────────┬───────────────────────────────────────────────┤ y=70
│                  │                                               │
│  ⛅              │                                               │
│  Partly Cloudy   │              07:42                            │ clock h=180
│                  │                                               │
│  ↑24°  ↓14°      │                                               │
│  Rain 40%        ├───────────────────────────────────────────────┤ y=250
│                  │  🎂 Mum          🎂 Dan                        │ bday h=70
│                  │                                               │
├──────────────────┴───────────────────────────────────────────────┤ y=320
│  AAPL $189  ▲1.2%    TSLA $172  ▼0.8%    SPY $524  ▲0.4%         │ h=90
│  Portfolio +2.3% today  ·  Hold AAPL, consider trimming TSLA     │
├──────────────────────────────────────────────────────────────────┤ y=410
│  Wednesday, 29 April 2026                              07:43     │ h=70
└──────────────────────────────────────────────────────────────────┘ y=480
```

**Zone definitions:**

| Zone | x | y | w | h | Content |
|---|---|---|---|---|---|
| Quote | 0 | 0 | 800 | 70 | Quote (italic, left) + attribution (right-aligned) |
| Weather | 0 | 70 | 220 | 250 | Icon + description + ↑max ↓min + rain% |
| Clock | 220 | 70 | 580 | 180 | Large `H:MM`, vertically centered |
| Birthdays | 220 | 250 | 580 | 70 | `🎂 Name` entries — hidden + clock expands if none |
| Stocks | 0 | 320 | 800 | 90 | Tickers + portfolio summary line + AI suggestion |
| Footer | 0 | 410 | 800 | 70 | Date (left) + current time (right) |

**Layout rules:**
- No zone title labels are shown on screen.
- If no birthdays today: Birthday zone is hidden; Clock zone expands to `h=250` (y=70 to y=320).
- Quote strip accommodates up to two lines of text at 16pt — attribution is always on the last line, right-aligned.
- Stocks zone has two text lines: line 1 = tickers, line 2 = portfolio summary + AI note.

**Dividers:**
- Horizontal at y=70 (full width)
- Horizontal at y=250 (x=220 to x=800 — separates clock from birthdays, only when birthdays exist)
- Horizontal at y=320 (full width)
- Horizontal at y=410 (full width)
- Vertical at x=220 (y=70 to y=320)

### 4.2 Partial Refresh Region

On each minute-tick, only these two regions are redrawn:

| Region | x | y | w | h |
|---|---|---|---|---|
| Clock | 220 | 70 | 580 | 180 |
| Footer time | 590 | 415 | 205 | 50 |

### 4.3 Typography

| Element | Font | Size | Style | Notes |
|---|---|---|---|---|
| Clock (H:MM) | Inter | 110pt | Bold | Tabular figures — digits don't shift layout |
| Weather icon | NerdFontsSymbolsOnly | 52pt | Regular | WMO code → glyph (see §6.3) |
| Weather description | Inter | 17pt | Regular | e.g. "Partly Cloudy" |
| Weather temp (max/min) | Inter | 20pt | Bold | `↑24°  ↓14°` on one line |
| Weather rain | Inter | 16pt | Regular | `Rain 40%` |
| Birthday entries | Inter | 20pt | Medium | `🎂 Name` |
| Stock ticker | JetBrains Mono | 18pt | Bold | |
| Stock price / change | JetBrains Mono | 17pt | Regular | `▲` / `▼` prefix |
| Stocks line 2 | Inter | 14pt | Italic | Portfolio summary + AI note, truncated |
| Quote text | Playfair Display | 16pt | Italic | Multi-line if needed (max 2 lines) |
| Quote attribution | Inter | 13pt | Regular | Right-aligned on last line, `— Name, Source` |
| Footer date | Inter | 16pt | Regular | Left-aligned |
| Footer time | Inter | 16pt | Regular | Right-aligned |

**Fonts to download** (all free):
- Inter: [fonts.google.com/specimen/Inter](https://fonts.google.com/specimen/Inter) → `Inter-Bold.ttf`, `Inter-Medium.ttf`, `Inter-Regular.ttf`
- JetBrains Mono: [fonts.google.com/specimen/JetBrains+Mono](https://fonts.google.com/specimen/JetBrains+Mono) → `JetBrainsMono-Bold.ttf`, `JetBrainsMono-Regular.ttf`
- Playfair Display: [fonts.google.com/specimen/Playfair+Display](https://fonts.google.com/specimen/Playfair+Display) → `PlayfairDisplay-Italic.ttf`
- Nerd Fonts: [github.com/ryanoasis/nerd-fonts/releases](https://github.com/ryanoasis/nerd-fonts/releases) → `NerdFontsSymbolsOnly.zip` → extract `NerdFontsSymbolsOnly-Regular.ttf`

Place all files in `C:\Users\Eli Zeltser\Documents\reTerminal\server\fonts\`.

### 4.4 Visual Style Rules

- Background: white (pixel `1`), foreground: black (pixel `0`)
- Zone dividers: single-pixel black lines
- 1-bit output: Pillow Floyd-Steinberg dithering for smooth text rendering
- Stock change: `▲` for positive, `▼` for negative
- Weather icons: Nerd Font glyphs (see §6.3)

---

## 5. Update Schedule & Screen Management

### 5.1 Active Windows

| Window | Start | End | Behaviour |
|---|---|---|---|
| Morning | 05:45 | 08:00 | Full refresh on wake, then partial clock tick every minute |
| Evening | 18:00 | 22:00 | Full refresh on wake, then partial clock tick every minute |

Times are local (Asia/Jerusalem), synced via NTP at each boot.

### 5.2 Maintenance Refresh

| Event | Time | Refresh type |
|---|---|---|
| End of morning window | 08:00 | Full refresh → sleep |
| Midday maintenance | 12:00 | Full refresh → immediately back to sleep |
| End of evening window | 22:00 | Full refresh → sleep |

### 5.3 Refresh Decision Logic

```
On each wake:
  IF green button pressed (wakeup reason = GPIO):
    → fetch /display.png unconditionally (no ETag header)
    → full refresh
    → clear FORCE_REFRESH flag
    → resume normal active/inactive loop

  IF scheduled full refresh time (08:00 / 12:00 / 22:00):
    → full refresh → sleep

  ELSE IF in active window:
    → GET /display.png with If-None-Match: <stored_etag>
    IF 200 and ETag changed:
      IF only clock+footer changed → partial refresh
      ELSE → fast refresh
    IF 304 → skip update

  ELSE:
    → sleep until next window

After 5 consecutive partial/fast refreshes:
  → force full refresh → reset counter
```

### 5.4 Sleep Duration Calculation

- **During active window:** 60 seconds
- **End of window / inactive:** sleep until next scheduled event
- **On Wi-Fi failure:** sleep 5 minutes, retry

### 5.5 NTP Time Synchronisation

Synced at every boot via `il.pool.ntp.org`. Prevents RTC drift over the max ~10-hour overnight sleep. Falls back to RTC on sync failure; self-corrects next boot.

---

## 6. Data Integration Specifications

### 6.1 Google Calendar

**Purpose:** Display today's birthday reminders below the clock.

**Auth:** OAuth2 offline refresh token. Credentials in `server\secrets\credentials.json`. Scope: `https://www.googleapis.com/auth/calendar.readonly`.

**Query:**
- Calendar: Birthdays (primary Google account)
- Time range: Start of today → 23:59 local
- All-day events only
- Max 6 results

**Display rules:**
- Format: `🎂 Name` — no time shown
- If no birthdays: Birthday zone hidden, Clock expands vertically
- If API unavailable: Birthday zone hidden silently

**Update schedule:** 05:45 and 18:00.

---

### 6.2 Stock Data, Portfolio & AI Suggestions

**Purpose:** Show live/close prices for a watchlist, summarise portfolio performance, and display an AI-generated suggestion that is aware of the actual holdings.

#### 6.2.1 Watchlist

The list of tracked tickers is stored in `server\data\tickers.json` and managed via the admin panel (see §9). It is not hardcoded in `.env`.

```json
["AAPL", "TSLA", "SPY"]
```

#### 6.2.2 Portfolio Database

Holdings are stored in `server\data\portfolio.json` and managed via the admin panel. The schema:

```json
{
  "holdings": [
    {
      "ticker": "AAPL",
      "shares": 10,
      "avg_cost": 165.00,
      "notes": "Long-term hold"
    },
    {
      "ticker": "TSLA",
      "shares": 5,
      "avg_cost": 210.00,
      "notes": "Speculative"
    }
  ],
  "last_updated": "2026-05-01T07:30:00"
}
```

At each stock fetch, the server computes for each holding:
- Current value = shares × current price
- Gain/loss = current value − (shares × avg_cost)
- Gain/loss % = gain / (shares × avg_cost) × 100
- Total portfolio value and total daily P&L

#### 6.2.3 Data Provider

**Polygon.io** — API key required.

**Update schedule:**

| Fetch time (Israel) | US market state | Shown as |
|---|---|---|
| **05:45** | Pre-market / closed | Previous day close — `[prev. close]` label |
| **17:30** | Market open (~1 hr in) | Live intraday price, no label |

#### 6.2.4 AI Suggestions (Portfolio-Aware)

The Claude API call includes the full portfolio context so suggestions are personalised to actual holdings.

| Property | Value |
|---|---|
| Model | `claude-haiku-4-5-20251001` |
| Max tokens | 80 |
| Trigger | Price change > 1.5% on any held ticker, or at each scheduled fetch |
| Cooldown | 30 min per ticker |
| Prompt template | Stored in `server\data\suggestion_prompt.txt` — editable via admin panel |

**Default prompt template** (`suggestion_prompt.txt`):
```
You are reviewing a personal stock portfolio.

Portfolio:
{portfolio_summary}

Today's market data:
{market_data}

Give a single short sentence (max 15 words) with a buy/hold/sell observation 
focused on the most significant movement. Be direct and specific.
```

The `{portfolio_summary}` token is replaced with a compact text summary of holdings, cost basis, and current P&L. The `{market_data}` token is replaced with today's prices and changes for all tracked tickers.

**Display in stocks zone (line 2):**
```
Portfolio +2.3% today  ·  Hold AAPL, consider trimming TSLA
```
Format: `Portfolio [total_pnl_pct]% today  ·  [AI suggestion text]`

If portfolio.json is empty (no holdings defined): line 2 shows only the AI suggestion without portfolio summary.

> All suggestions are informational only — not financial advice.

---

### 6.3 Weather

**Provider:** Open-Meteo — free, no API key.

**Location:** Bat Yam, Israel — Lat `32.08`, Lon `34.78`, TZ `Asia/Jerusalem`

**Fields fetched:**

| Field | API parameter | Display |
|---|---|---|
| Current temperature | `current=temperature_2m` | Inline with icon area |
| Today max temp | `daily=temperature_2m_max` | `↑ 24°C` |
| Today min temp | `daily=temperature_2m_min` | `↓ 14°C` |
| Weather condition | `current=weather_code` | Icon glyph + text description |
| Precipitation probability | `daily=precipitation_probability_max` | `Rain 40%` |

Wind speed is not fetched or displayed.

**Icon mapping (WMO code → Nerd Font glyph):**

| Condition | WMO codes | Glyph name |
|---|---|---|
| Clear / Sunny | 0 | `nf-md-weather_sunny` |
| Partly Cloudy | 1, 2 | `nf-md-weather_partly_cloudy` |
| Overcast | 3 | `nf-md-weather_cloudy` |
| Foggy | 45, 48 | `nf-md-weather_fog` |
| Drizzle | 51–57 | `nf-md-weather_rainy` |
| Rain | 61–67 | `nf-md-weather_pouring` |
| Thunderstorm | 95–99 | `nf-md-weather_lightning_rainy` |
| Snow | 71–77, 85–86 | `nf-md-weather_snowy` |

**Update frequency:** Once at 05:45. Cached all day. **Units:** Celsius.

---

### 6.4 Clock & Date

| Element | Format | Example | Location |
|---|---|---|---|
| Time (main) | `H:MM` (24h, no leading zero) | `7:42` | Clock zone |
| Time (footer) | `H:MM` | `7:42` | Footer, right-aligned |
| Date (footer) | `Day, DD Month YYYY` | `Wednesday, 29 April 2026` | Footer, left-aligned |

**NTP server:** `il.pool.ntp.org`  
**Timezone:** `Asia/Jerusalem`  
**POSIX TZ string:** `IST-2IDT,M3.4.4/26,M10.5.0`

---

### 6.5 Quote of the Day

**Purpose:** A beautiful or thought-provoking quote at the top of the display, from real people, history, or literature.

**Generation:** Claude API call once at 05:45, cached all day.

| Property | Value |
|---|---|
| Model | `claude-haiku-4-5-20251001` |
| Max tokens | 120 |
| Max displayed chars | 160 (two lines at 16pt in 800px strip) |
| Fallback | Random entry from `server\data\quotes.json` |
| Prompt template | Stored in `server\data\quote_prompt.txt` — editable via admin panel |

**Default prompt template** (`quote_prompt.txt`):
```
Give me one short, memorable quote from a well-known person or from a 
famous literary work (for example: Dune by Frank Herbert, The Alchemist 
by Paulo Coelho, a history book, a scientist, artist, or philosopher). 
The quote should be beautiful, thought-provoking, or uplifting.
Reply ONLY in this exact format — no other text:
"Quote text here." — Attribution (e.g. Name, or Name, Book Title)
```

**Fallback quote list** (`server\data\quotes.json`) — editable via admin panel:
```json
[
  {
    "quote": "The mystery of life isn't a problem to solve, but a reality to experience.",
    "attribution": "Frank Herbert, Dune"
  },
  {
    "quote": "When you want something, all the universe conspires in helping you to achieve it.",
    "attribution": "Paulo Coelho, The Alchemist"
  }
]
```

> **[ EDIT ]** Populate this list with at least 10 entries via the admin panel (see §9.1).

---

## 7. Firmware Specification (E1001)

### 7.1 Framework Decision: Zephyr RTOS

Zephyr is chosen over ESP-IDF. The deciding reason: the ZEReader open-source project already implements Zephyr with the UC8179 driver for the exact GDEY075T7 panel used in the E1001. This eliminates the highest-risk part of the firmware.

| Criterion | Zephyr | ESP-IDF |
|---|---|---|
| Official E1001 board support | ✅ `reterminal_e1001` in mainline | ✅ |
| UC8179 ePaper driver | ✅ Exists in ZEReader (same panel) | ⚠️ Must be written from scratch |
| HTTP client | ✅ `net/http_client` | ✅ `esp_http_client` |
| NTP | ✅ `net/sntp` | ✅ `esp_sntp` |
| NVS / settings | ✅ `settings` subsystem | ✅ `nvs_flash` |
| Power management | ✅ `pm` subsystem | ✅ `esp_deep_sleep` |
| Build tool | `west` (simpler) | `idf.py` |
| Hassle | **Lower** — driver exists | **Higher** — driver work required |

### 7.2 Libraries & Components

| Item | Zephyr component | Notes |
|---|---|---|
| RTOS | Zephyr v3.7+ | Board target: `reterminal_e1001` |
| ePaper driver | `drivers/display` + UC8179 | Reference: ZEReader project |
| PNG decoder | `pngle` (vendored) | Lightweight C library |
| HTTP client | `net/http_client` | ETag header support |
| Wi-Fi | `esp_wifi` via Zephyr HAL | |
| NTP | `net/sntp` | `sntp_simple()` |
| Settings/storage | `settings` (NVS backend) | ETag, refresh counter |
| Power management | `pm` subsystem | `pm_state_force(PM_STATE_SOFT_OFF)` |
| GPIO (button) | `gpio` driver | Green button wakeup |
| RTC | `rtc` driver | Fallback if NTP fails |

### 7.3 Firmware State Machine

```
BOOT (from PM sleep or power-on)
  │
  ├─► Init: display, SPI, GPIO, settings/NVS
  │
  ├─► Check wakeup reason:
  │     GPIO (green button) → set FORCE_REFRESH flag
  │     Timer              → continue normally
  │
  ├─► Connect Wi-Fi (timeout 15s, retry 3×)
  │     FAILURE → render Wi-Fi error screen → PM sleep 5 min → reboot
  │
  ├─► SNTP sync (il.pool.ntp.org)
  │     FAILURE → use RTC, log warning
  │
  ├─► Determine mode from local time:
  │
  │   SCHEDULED_FULL_REFRESH (08:00 / 12:00 / 22:00):
  │     → full ePaper refresh → sleep until next event
  │
  │   ACTIVE_WINDOW (05:45–08:00 or 18:00–22:00):
  │     IF FORCE_REFRESH:
  │       → GET /display.png (no ETag) → full refresh → clear flag
  │     ELSE:
  │       → GET /display.png with If-None-Match: <etag>
  │       200 → decode → select refresh mode → update → save ETag
  │       304 → skip
  │       Error → skip, log
  │     → PM sleep 60s
  │
  │   INACTIVE:
  │     → sleep until next window or maintenance time
  │
  └─► Enter PM sleep
```

### 7.4 Wi-Fi Error Screen

If Wi-Fi fails after 3 retries, display before sleeping:

```
┌──────────────────────────────────────────────────────────┐
│                                                          │
│   Wi-Fi connection failed                                │
│   Could not connect to: <SSID>                           │
│   Retrying in 5 minutes.                                 │
│   Check network and firmware config.h                    │
│                                                          │
└──────────────────────────────────────────────────────────┘
```

### 7.5 Green Button — Manual Full Refresh

GPIO3 is configured as a wakeup source. Button press during sleep → boot with `FORCE_REFRESH` → unconditional full refresh regardless of ETag → resume normal loop.

### 7.6 Build Configuration

**Project structure:**
```
firmware/
├── west.yml
├── CMakeLists.txt
├── prj.conf
├── app.overlay
├── src/
│   ├── main.c
│   ├── wifi.c / wifi.h
│   ├── ntp.c / ntp.h
│   ├── http_fetch.c
│   ├── png_decode.c
│   ├── epaper.c / epaper.h
│   ├── schedule.c
│   ├── button.c
│   └── config.h
├── lib/
│   └── pngle/
└── CLAUDE.md
```

**`prj.conf`:**
```
CONFIG_NETWORKING=y
CONFIG_NET_IPV4=y
CONFIG_NET_TCP=y
CONFIG_DNS_RESOLVER=y
CONFIG_NET_SOCKETS=y
CONFIG_HTTP_CLIENT=y
CONFIG_SNTP=y
CONFIG_WIFI=y
CONFIG_ESP32_WIFI=y
CONFIG_DISPLAY=y
CONFIG_SETTINGS=y
CONFIG_SETTINGS_NVS=y
CONFIG_NVS=y
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_PM=y
CONFIG_PM_DEVICE=y
CONFIG_GPIO=y
CONFIG_RTC=y
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=3
CONFIG_MAIN_STACK_SIZE=8192
CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=4096
```

**Build and flash (from Arch Linux):**
```bash
# One-time workspace setup
pip install west --break-system-packages
west init ~/zephyrproject
cd ~/zephyrproject && west update && west zephyr-export
west sdk install

# Build
cd ~/Documents/reTerminal/firmware
west build -b reterminal_e1001 .

# Flash
west flash --runner esptool
# or directly:
esptool.py -c esp32s3 -p /dev/ttyUSB0 write_flash 0x0 build/zephyr/zephyr.bin
```

### 7.7 Configuration (`src/config.h`)

```c
#define WIFI_SSID           "Eli"
#define WIFI_PASSWORD       "1020304050"
#define WIFI_RETRY_MAX      3
#define WIFI_TIMEOUT_MS     15000

#define SERVER_HOST         "10.100.102.216"
#define SERVER_PORT         8080
#define SERVER_PATH         "/display.png"

#define NTP_SERVER          "il.pool.ntp.org"
#define TZ_POSIX_STRING     "IST-2IDT,M3.4.4/26,M10.5.0"

#define MORNING_START_H     5
#define MORNING_START_M     45
#define MORNING_END_H       8
#define MORNING_END_M       0
#define EVENING_START_H     18
#define EVENING_START_M     0
#define EVENING_END_H       22
#define EVENING_END_M       0

#define MAINTENANCE_1_H     8
#define MAINTENANCE_1_M     0
#define MAINTENANCE_2_H     12
#define MAINTENANCE_2_M     0
#define MAINTENANCE_3_H     22
#define MAINTENANCE_3_M     0

#define MAX_PARTIAL_BEFORE_FULL  5
#define BUTTON_GREEN_PIN         3

// ePaper SPI (do not change — hardware fixed)
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
C:\Users\Eli Zeltser\Documents\reTerminal\
├── .venv\
├── server\
│   ├── main.py                  # FastAPI app — display + admin routes
│   ├── renderer.py              # Pillow canvas rendering
│   ├── scheduler.py             # APScheduler tasks
│   ├── sources\
│   │   ├── calendar.py
│   │   ├── stocks.py            # Polygon.io + portfolio P&L calculation
│   │   ├── weather.py
│   │   ├── suggestions.py       # Portfolio-aware Claude suggestion
│   │   └── quote.py
│   ├── admin\
│   │   ├── routes.py            # FastAPI admin API routes
│   │   └── templates\
│   │       ├── base.html
│   │       ├── quotes.html
│   │       ├── portfolio.html
│   │       └── stocks.html
│   ├── data\
│   │   ├── quotes.json          # Editable fallback quote list
│   │   ├── quote_prompt.txt     # Editable Claude quote prompt
│   │   ├── tickers.json         # Editable watchlist
│   │   ├── suggestion_prompt.txt # Editable Claude suggestion prompt
│   │   └── portfolio.json       # Holdings database
│   ├── fonts\
│   │   ├── Inter-Bold.ttf
│   │   ├── Inter-Medium.ttf
│   │   ├── Inter-Regular.ttf
│   │   ├── JetBrainsMono-Bold.ttf
│   │   ├── JetBrainsMono-Regular.ttf
│   │   ├── PlayfairDisplay-Italic.ttf
│   │   └── NerdFontsSymbolsOnly-Regular.ttf
│   ├── secrets\
│   │   ├── credentials.json     # Google OAuth — NEVER COMMIT
│   │   ├── token.json           # Google OAuth token — NEVER COMMIT
│   │   └── .env                 # API keys — NEVER COMMIT
│   ├── cache\
│   │   └── display.png
│   ├── requirements.txt
│   ├── nssm_install.ps1
│   └── CLAUDE.md
└── firmware\
    └── CLAUDE.md
```

**Virtual environment setup (first time):**
```powershell
cd "C:\Users\Eli Zeltser\Documents\reTerminal"
python -m venv .venv
.venv\Scripts\Activate.ps1
pip install fastapi uvicorn pillow httpx apscheduler jinja2 `
            google-api-python-client google-auth-oauthlib `
            anthropic python-dotenv
pip freeze > server\requirements.txt
```

(`jinja2` is added for admin panel HTML templates.)

### 8.2 API Endpoints

**Display (used by E1001 firmware):**

| Endpoint | Method | Description |
|---|---|---|
| `/display.png` | GET | Rendered PNG with ETag. Returns `304` if unchanged. |
| `/status` | GET | JSON: last render time, source statuses, next fetches, ETag. |
| `/refresh` | POST | Force immediate re-render and data re-fetch. |

**Admin API (used by admin panel — see §9):**

| Endpoint | Method | Description |
|---|---|---|
| `/api/quotes` | GET | Return full quotes list as JSON |
| `/api/quotes` | POST | Add a new quote `{quote, attribution}` |
| `/api/quotes/{id}` | PUT | Update a quote entry |
| `/api/quotes/{id}` | DELETE | Remove a quote entry |
| `/api/quote-prompt` | GET | Return current Claude quote prompt string |
| `/api/quote-prompt` | PUT | Update Claude quote prompt string |
| `/api/tickers` | GET | Return watchlist tickers as JSON array |
| `/api/tickers` | POST | Add a ticker `{ticker}` |
| `/api/tickers/{ticker}` | DELETE | Remove a ticker |
| `/api/portfolio` | GET | Return full portfolio holdings |
| `/api/portfolio` | POST | Add a holding `{ticker, shares, avg_cost, notes}` |
| `/api/portfolio/{ticker}` | PUT | Update a holding |
| `/api/portfolio/{ticker}` | DELETE | Remove a holding |
| `/api/suggestion-prompt` | GET | Return current Claude suggestion prompt |
| `/api/suggestion-prompt` | PUT | Update suggestion prompt |
| `/api/portfolio/query` | POST | Ask Claude a question about the portfolio (see §9.3) |

**Admin UI pages:**

| Route | Description |
|---|---|
| `/admin` | Admin panel home / dashboard summary |
| `/admin/quotes` | Quote list manager |
| `/admin/stocks` | Watchlist + portfolio manager |

### 8.3 Render Pipeline

```
scheduler triggers render()
  │
  ├─► Fetch in parallel (asyncio.gather):
  │     calendar.get_birthdays_today()
  │     weather.get_today()            [cached post-05:45]
  │     stocks.get_prices(tickers)     [cached post-scheduled fetch]
  │     quote.get_today()              [cached post-05:45]
  │     suggestions.maybe_get()        [only on trigger]
  │
  ├─► Load portfolio.json → compute P&L summary
  │
  ├─► renderer.compose(data) → PIL Image (800×480, mode "1")
  │     draw_quote_strip()       [y=0..70]
  │     draw_weather_zone()      [x=0..220, y=70..320]
  │     draw_clock_zone()        [x=220..800, y=70..250 or 70..320]
  │     draw_birthday_zone()     [x=220..800, y=250..320, if any]
  │     draw_stocks_zone()       [y=320..410]
  │     draw_footer()            [y=410..480]
  │     draw_dividers()
  │
  ├─► MD5 hash → ETag
  ├─► If changed → write cache\display.png
  └─► Return (bytes, etag)
```

### 8.4 Scheduling

| Task | Schedule | Notes |
|---|---|---|
| Full render | Every 60s (active windows) / every 30 min (inactive) | ETag prevents unnecessary display refresh |
| Weather fetch | Daily at 05:45 | Min/max included, cached all day |
| Stock fetch | Daily at 05:45 and 17:30 | Includes portfolio P&L calculation |
| Calendar fetch | Daily at 05:45 and 18:00 | |
| Quote generation | Daily at 05:45 | Cached all day |
| AI suggestions | On trigger (price change > 1.5%) | 30 min cooldown per ticker |

### 8.5 `server\CLAUDE.md`

```markdown
# CLAUDE.md — ePaper Dashboard Server

## What this does
FastAPI server on Windows 10 (DESKTOP-NJR6V52), Asus A554I.
IP: 10.100.102.216 (DHCP-reserved, stable).
Renders 800×480 1-bit PNG for Seeed reTerminal E1001 ePaper display.
Also hosts a local admin panel at /admin for managing quotes, stocks, portfolio.

## Activate venv first
  C:\Users\Eli Zeltser\Documents\reTerminal\.venv\Scripts\Activate.ps1

## Run (development)
  cd server
  python -m uvicorn main:app --host 0.0.0.0 --port 8080 --reload

## Useful commands
  curl -X POST http://localhost:8080/refresh
  curl http://localhost:8080/status
  # Admin panel: open http://10.100.102.216:8080/admin in browser

## Critical constraints
- Image: exactly 800×480 px, PIL mode "1" (1-bit black/white)
- Fonts: always ImageFont.truetype() from server\fonts\ — never default fonts
- HTTP: always httpx.AsyncClient — never requests library
- Secrets: load from server\secrets\.env — never hardcode
- Data files in server\data\ are user-editable via admin panel — never overwrite on startup

## Layout zones (pixels)
- Quote:     x=0,   y=0,   w=800, h=70
- Weather:   x=0,   y=70,  w=220, h=250
- Clock:     x=220, y=70,  w=580, h=180  (expands to h=250 if no birthdays)
- Birthdays: x=220, y=250, w=580, h=70   (hidden if none today)
- Stocks:    x=0,   y=320, w=800, h=90
- Footer:    x=0,   y=410, w=800, h=70

## Data files (editable via admin panel — do not hardcode their content)
- server\data\quotes.json           — fallback quote list
- server\data\quote_prompt.txt      — Claude quote generation prompt
- server\data\tickers.json          — stock watchlist
- server\data\portfolio.json        — holdings with shares + avg cost
- server\data\suggestion_prompt.txt — Claude suggestion prompt

## Data sources
- Calendar: Birthdays only, token.json auto-refreshes
- Stocks: Polygon.io at 05:45 and 17:30; P&L computed from portfolio.json
- Weather: Open-Meteo, lat=32.08 lon=34.78, no wind speed
- Quote: Claude Haiku via quote_prompt.txt, 05:45 daily, fallback from quotes.json
- Suggestions: Claude Haiku via suggestion_prompt.txt, portfolio-aware
```

---

## 9. Management UI — Admin Panel

The admin panel is served by the same FastAPI process on `/admin` routes. It is a simple local web UI — accessible from any browser on the home network at `http://10.100.102.216:8080/admin`. No authentication is required (local network only). It uses server-side HTML templates (Jinja2) with minimal JavaScript for form interactions.

### 9.1 Quote Manager (`/admin/quotes`)

Displays the current fallback quote list (`server\data\quotes.json`) and the Claude prompt template (`server\data\quote_prompt.txt`).

**Features:**
- **List view:** shows all quotes with index, text snippet, and attribution
- **Add quote:** form with two fields — quote text + attribution — submits to `POST /api/quotes`
- **Edit quote:** inline edit of text and attribution — submits to `PUT /api/quotes/{id}`
- **Delete quote:** delete button per entry — calls `DELETE /api/quotes/{id}`
- **Edit Claude prompt:** textarea showing current `quote_prompt.txt` — save button calls `PUT /api/quote-prompt`
- **Preview:** "Generate now" button calls `POST /refresh` and shows a link to the cached PNG

### 9.2 Stock & Watchlist Manager (`/admin/stocks`)

Manages tracked tickers (`tickers.json`) and portfolio holdings (`portfolio.json`) and the suggestion prompt.

**Watchlist section:**
- Current tickers listed with delete button each
- Add ticker: text field + "Add" button → `POST /api/tickers`
- Delete: `DELETE /api/tickers/{ticker}`

**Portfolio section:**
- Table of holdings: Ticker | Shares | Avg Cost | Notes | Current Price | P&L | P&L%
- Current price and P&L are computed from last cached stock fetch
- Add holding: form with Ticker, Shares, Avg Cost (USD), Notes → `POST /api/portfolio`
- Edit holding: inline edit of shares, avg cost, notes → `PUT /api/portfolio/{ticker}`
- Delete holding: `DELETE /api/portfolio/{ticker}`

**Suggestion prompt section:**
- Textarea showing `suggestion_prompt.txt`
- Save button → `PUT /api/suggestion-prompt`

### 9.3 Portfolio AI Query (`/admin/stocks` — Query section)

A text input field labelled **"Ask about your portfolio"** that lets you type a free-form question and get a Claude response about your holdings. This is separate from the display suggestion — it's a manual, interactive query.

**Example questions:**
- "How is my portfolio performing overall this week?"
- "Should I rebalance given TSLA's recent drop?"
- "What's my total unrealised gain?"

**Interaction flow:**
1. User types a question in the textarea and clicks "Ask Claude"
2. Frontend sends `POST /api/portfolio/query` with `{question: "..."}`
3. Server builds a prompt with full portfolio data + latest prices + the user's question
4. Claude Haiku responds (max 200 tokens)
5. Response displayed in a result box below the form

**Prompt sent to Claude:**
```
You are a personal finance assistant reviewing a portfolio.

Holdings:
{portfolio_detail}

Today's market data:
{market_data}

User question: {user_question}

Answer clearly and concisely (max 3 sentences).
```

This response is never shown on the ePaper display — it is admin-panel only.

---

## 10. Configuration & Secrets Management

### 10.1 Server `.env` file

Location: `C:\Users\Eli Zeltser\Documents\reTerminal\server\secrets\.env`

```env
# ── Google Calendar ──────────────────────────────────────────
GOOGLE_CREDENTIALS_PATH=secrets/credentials.json
GOOGLE_TOKEN_PATH=secrets/token.json
GOOGLE_CALENDAR_ID=primary

# ── Stocks ───────────────────────────────────────────────────
STOCK_PROVIDER=polygon
STOCK_API_KEY=<!-- your Polygon.io API key -->
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

# ── Server ───────────────────────────────────────────────────
SERVER_HOST=0.0.0.0
SERVER_PORT=8080
```

Note: `STOCK_TICKERS` is no longer in `.env` — the watchlist is managed via `server\data\tickers.json` through the admin panel.

### 10.2 User-Editable Data Files

These files live in `server\data\` and are managed through the admin panel. They are safe to edit manually too. They must never be overwritten by the application on startup.

| File | Content | Managed via |
|---|---|---|
| `quotes.json` | Fallback quote list | Admin panel → Quotes |
| `quote_prompt.txt` | Claude quote generation prompt | Admin panel → Quotes |
| `tickers.json` | Stock watchlist | Admin panel → Stocks |
| `portfolio.json` | Holdings with shares + avg cost | Admin panel → Stocks |
| `suggestion_prompt.txt` | Claude suggestion prompt | Admin panel → Stocks |

### 10.3 `.gitignore`

```
.venv/
server/secrets/
server/cache/
*.env
__pycache__/
*.pyc
build/
.west/
```

---

## 11. Error Handling & Fallback Behavior

| Failure scenario | Behaviour |
|---|---|
| **Wi-Fi fails at boot (E1001)** | Retry 3×, 5s apart → render error screen → PM sleep 5 min → retry |
| **NTP sync fails** | Use RTC, log warning. Self-corrects next boot |
| **Server unreachable** | Keep last displayed image. Sleep and retry |
| **Google Calendar API error** | Birthday zone hidden silently |
| **Polygon.io error / rate limit** | Last known price with `[delayed]` label |
| **Claude API error (suggestion)** | Suggestion line blank; portfolio P&L still shown |
| **Claude API error (quote)** | Random entry from `quotes.json` |
| **Weather API error** | Weather zone shows `—` |
| **Server render exception** | Log traceback. Serve last cached PNG. Server stays up |
| **PNG decode error (E1001)** | Skip update, log, sleep normally |
| **portfolio.json missing or empty** | Stocks zone shows tickers only, no P&L summary line |
| **Admin panel portfolio query error** | Display error message in query result box |

---

## 12. Development Toolchain

### 12.1 Laptop / Server Side (Windows 10)

| Tool | Purpose | Install |
|---|---|---|
| Python 3.12 | Runtime | [python.org](https://python.org) |
| venv | Isolation | `python -m venv .venv` |
| FastAPI + Uvicorn | HTTP server + admin routes | `pip install fastapi uvicorn` |
| Jinja2 | Admin panel HTML templates | `pip install jinja2` |
| Pillow | Image rendering | `pip install pillow` |
| httpx | Async HTTP client | `pip install httpx` |
| APScheduler | Task scheduling | `pip install apscheduler` |
| google-api-python-client | Calendar SDK | `pip install google-api-python-client google-auth-oauthlib` |
| anthropic | Claude SDK | `pip install anthropic` |
| python-dotenv | `.env` loading | `pip install python-dotenv` |
| NSSM | Windows Service manager | [nssm.cc](https://nssm.cc/download) |
| Claude Code | AI-assisted development | `npm install -g @anthropic-ai/claude-code` |

**Full install:**
```powershell
C:\Users\Eli Zeltser\Documents\reTerminal\.venv\Scripts\Activate.ps1
pip install fastapi uvicorn jinja2 pillow httpx apscheduler `
            google-api-python-client google-auth-oauthlib `
            anthropic python-dotenv
```

### 12.2 E1001 Firmware Side (Arch Linux)

| Tool | Purpose | Install |
|---|---|---|
| west | Zephyr build tool | `pip install west --break-system-packages` |
| Zephyr SDK | Xtensa toolchain | `west sdk install` |
| CMake + Ninja | Build system | `sudo pacman -S cmake ninja` |
| esptool | Flashing | `pip install esptool --break-system-packages` |

```bash
pip install west --break-system-packages
west init ~/zephyrproject
cd ~/zephyrproject && west update && west zephyr-export
west sdk install

cd ~/Documents/reTerminal/firmware
west build -b reterminal_e1001 .
west flash --runner esptool
```

> Keep `PLATFORMIO_CORE_DIR=/opt/platformio` in `.bashrc` to protect the full `/home` partition.

### 12.3 Claude Code

```powershell
# Server (Windows)
cd "C:\Users\Eli Zeltser\Documents\reTerminal\server"
claude
```
```bash
# Firmware (Arch)
cd ~/Documents/reTerminal/firmware
claude
```

---

## 13. Open Questions & Decisions Pending

| # | Question | Options / Notes | Decision |
|---|---|---|---|
| 1 | Tickers to track | Add via admin panel at `/admin/stocks` | <!-- TBD --> |
| 2 | Portfolio holdings | Enter via admin panel — shares, avg cost, notes | <!-- TBD --> |
| 3 | Stock suggestion trigger % | Currently 1.5% — adjustable in `.env` | <!-- TBD --> |
| 4 | E1001 IP assignment | Recommend DHCP reservation in router for E1001 MAC | <!-- TBD --> |
| 5 | Fallback quote list | Add at least 10 via `/admin/quotes` | <!-- TBD --> |
| 6 | Green button refresh | ✅ Resolved — implemented in §7.5 | ✅ Done |
| 7 | ZEReader UC8179 driver | Reference or fork from ZEReader repo — confirm license | <!-- TBD --> |
| 8 | Birthday zone if empty | Clock expands to fill space — confirm visual is good after first run | <!-- TBD --> |
| 9 | Admin panel auth | Currently none (local network only). Add basic password if desired. | <!-- TBD --> |

---

## Appendix A — Change Log

| Version | Date | Changes |
|---|---|---|
| 0.1 | 01.05.2026 | Initial draft |
| 0.2 | 01.05.2026 | Quote of the day; network topology; NSSM; grid redesign; weather fields; NTP; ESP-IDF firmware; Wi-Fi error screen; stock schedule |
| 0.3 | 01.05.2026 | Venv path; full Windows paths; Ethernet static IP setup; layout redesign (quote top, clock+birthdays, stocks, footer); quote sources expanded to literature; firmware to Zephyr; green button resolved |
| 0.4 | 01.05.2026 | DHCP reservation noted — static IP instructions removed (not needed); laptop IP `10.100.102.216`, gateway `10.100.102.1`, hostname `DESKTOP-NJR6V52` filled in; SERVER_HOST filled in config.h; quote strip height increased to 70px; clock zone enlarged (h=180, font 110pt); birthday zone reduced to 70px; footer increased to 70px; weather wind speed removed from display and API fetch; stocks zone enlarged to 90px with two-line format (tickers + portfolio+AI); portfolio database `portfolio.json` added with P&L calculation; stock watchlist moved from `.env` to `tickers.json` (admin-managed); Claude suggestion prompt made user-editable via `suggestion_prompt.txt`; Claude quote prompt made user-editable via `quote_prompt.txt`; new §9 Management UI / Admin Panel with Quote Manager, Watchlist+Portfolio Manager, and interactive portfolio AI query; new API routes `/api/*` and `/admin/*`; `jinja2` added to dependencies; `STOCK_TICKERS` removed from `.env`; `server\data\` directory added to directory structure |

---

*All `<!-- EDIT -->` and `<!-- TBD -->` markers indicate places requiring input before implementation begins.*
