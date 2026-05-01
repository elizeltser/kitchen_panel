# Functional Specification: ePaper Smart Dashboard System
**Document version:** 0.5 — Draft  
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
   - 6.2 [Stock Data & Portfolio](#62-stock-data--portfolio)
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

This document describes the functional requirements of a personal smart dashboard built on a **Seeed Studio reTerminal E1001** 7.5-inch monochrome ePaper display. The system provides an at-a-glance view of time, weather, birthday reminders, stock portfolio status, and a daily quote during defined morning and evening periods. All dashboard content and configuration is manageable via a local web admin panel.

### 1.2 Goals

- Display accurate, readable information during two daily active windows without requiring interaction.
- Maximize screen lifespan by minimizing unnecessary full refreshes and enforcing a safe refresh schedule.
- Keep the display firmware simple and stateless — all intelligence and rendering lives on the laptop server.
- Provide a local admin panel to manage quotes, stock watchlist, and portfolio without editing files manually.
- Enable future extension of data sources without reflashing the device.
- Provide a pleasant shared morning and evening experience for the household.

### 1.3 Non-Goals

- This is not a real-time trading terminal. Stock data is informational only and not financial advice.
- The display is not interactive beyond the green button manual refresh (v1).
- The system does not need to function when the laptop server is off or unreachable.
- No Claude API key is used in this version — all AI features are deferred to a future version.

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
│                       ┌──────┴──────┐        │
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
  1. Fetch fresh data from all sources (calendar, weather, stocks)
  2. Load portfolio from portfolio.json — compute P&L summary
  3. Select today's quote from quotes.json (random, date-seeded)
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

The laptop's IP is **reserved via DHCP in the router** (`10.100.102.216`). No Windows-side static IP configuration is needed — the router always assigns the same IP to the laptop's Ethernet adapter based on its MAC address.

**Verify the reservation is working:**
```powershell
# Should show 10.100.102.216 under Ethernet adapter
ipconfig

# After starting the server, confirm it's listening:
netstat -an | findstr "8080"
```

The server is not exposed to the internet. As long as no port-forwarding rule exists for port 8080 in the router, all traffic is local only. Your public IP (check at `https://whatismyip.com`) will differ from `10.100.102.216`.

| Item | Value |
|---|---|
| Laptop connection | Ethernet |
| Laptop IP | `10.100.102.216` (DHCP-reserved — no Windows config needed) |
| Laptop hostname | `DESKTOP-NJR6V52` |
| Router gateway | `10.100.102.1` |
| Server port | `8080` |
| Admin panel | Same port, routes `/admin/*` |
| E1001 IP | `10.100.102.4` (DHCP-reserved in router) |
| External exposure | None — no port forwarding |

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

**FastAPI + Uvicorn** handles async data fetching natively (all API calls run concurrently), has built-in ETag/`304 Not Modified` support, and hosts the admin panel on the same process.

**Virtual environment:** `C:\Users\Eli Zeltser\Documents\reTerminal\.venv`

```powershell
# Activate venv (required before any python/pip command)
C:\Users\Eli Zeltser\Documents\reTerminal\.venv\Scripts\Activate.ps1

# If blocked by execution policy, run once as Administrator:
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
```

**NSSM Windows Service** (run once, after development is working):
```powershell
$venvPython = "C:\Users\Eli Zeltser\Documents\reTerminal\.venv\Scripts\python.exe"
$appDir     = "C:\Users\Eli Zeltser\Documents\reTerminal\server"

nssm install EpaperDashboard $venvPython "-m uvicorn main:app --host 0.0.0.0 --port 8080"
nssm set EpaperDashboard AppDirectory $appDir
nssm set EpaperDashboard Start SERVICE_AUTO_START
nssm start EpaperDashboard

nssm status EpaperDashboard
nssm stop   EpaperDashboard
nssm remove EpaperDashboard confirm
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
| Admin panel | Same FastAPI process, `/admin` routes |
| Process management | NSSM Windows Service (production) / manual venv run (development) |

### 3.3 Data Sources

| Source | Provider | Auth | Update frequency |
|---|---|---|---|
| Calendar (birthdays) | Google Calendar API v3 | OAuth2 offline token | 05:45 and 18:00 daily |
| Stock prices | Polygon.io | API key | 05:45 and 17:30 daily |
| Portfolio data | Local `portfolio.json` | None | On admin save |
| Weather | Open-Meteo (free, no key) | None | Once daily at 05:45 |
| Quote of the day | Local `quotes.json` | None | Daily random selection at 05:45 |
| Time / Date | E1001 system clock (NTP-synced) | N/A | Every render cycle |

### 3.4 Communication Layer

- **Protocol:** HTTP/1.1 over home LAN
- **Display image:** PNG, 1-bit (black/white), 800×480, served with ETag
- **Admin panel:** HTML pages on `/admin/*` routes (Jinja2 templates)
- **Data API:** JSON endpoints on `/api/*` routes for admin interactions
- **Security:** Local network only. No TLS in v1.

---

## 4. Display Layout & Visual Design

### 4.1 Grid Structure

The footer is removed. The canvas has four zones: quote at top, weather left, clock+date+birthdays right, and stocks at the bottom. With the footer gone, both the weather and clock zones are taller.

```
┌──────────────────────────────────────────────────────────────────┐ y=0
│  "The mystery of life isn't a problem to solve, but a reality    │ h=70
│   to experience."                      — Frank Herbert, Dune     │
├──────────────────┬───────────────────────────────────────────────┤ y=70
│                  │                                               │
│  ⛅              │              07:42                            │
│  Partly Cloudy   │                                               │ h=310
│                  │          Wednesday, April 6                   │
│  ↑24°  ↓14°      │                                               │
│  Rain 40%        ├───────────────────────────────────────────────┤ y=290
│                  │  🎂 Mum          🎂 Dan                        │ h=90
│                  │                                               │
├──────────────────┴───────────────────────────────────────────────┤ y=380
│  AAPL $189  ▲1.2%    TSLA $172  ▼0.8%    SPY $524  ▲0.4%         │ h=100
│  Portfolio: $4,712 total  ·  Today: +$42 (+0.9%)                 │
└──────────────────────────────────────────────────────────────────┘ y=480
```

**Zone definitions:**

| Zone | x | y | w | h | Content |
|---|---|---|---|---|---|
| Quote | 0 | 0 | 800 | 70 | Quote italic (left) + attribution (right-aligned) |
| Weather | 0 | 70 | 220 | 310 | Icon + description + ↑max ↓min + rain% |
| Clock | 220 | 70 | 580 | 220 | Large `H:MM` centered, date below |
| Birthdays | 220 | 290 | 580 | 90 | `🎂 Name` entries — hidden + clock expands if none |
| Stocks | 0 | 380 | 800 | 100 | Tickers line + portfolio summary line |

**Layout rules:**
- No zone title labels are shown on screen.
- If no birthdays today: Birthday zone is hidden; Clock zone expands to `h=310` (y=70 to y=380).
- Quote strip supports up to two lines of text at 16pt; attribution is always right-aligned on the last line.
- Stocks zone has two text lines: line 1 = tickers with prices, line 2 = portfolio summary placeholder.
- The date (`Wednesday, April 6`) is displayed directly below the clock time within the clock zone, centered.

**Dividers:**
- Horizontal at y=70 (full width)
- Horizontal at y=290 (x=220 to x=800 — separates clock from birthdays, only when birthdays exist)
- Horizontal at y=380 (full width)
- Vertical at x=220 (y=70 to y=380)

### 4.2 Partial Refresh Region

On each minute-tick, only the clock sub-region is redrawn (clock time + date below it). The footer is removed so there is no second partial region.

| Region | x | y | w | h |
|---|---|---|---|---|
| Clock + date | 220 | 70 | 580 | 220 |

### 4.3 Typography

| Element | Font | Size | Style | Notes |
|---|---|---|---|---|
| Clock (H:MM) | Montserrat | 120pt | Bold | Tabular figures — digits don't shift layout |
| Date below clock | Montserrat | 24pt | Regular | `Day, Month D` format — `Wednesday, April 6` |
| Weather icon | NerdFontsSymbolsOnly | 52pt | Regular | WMO code → glyph (see §6.3) |
| Weather description | Inter | 17pt | Regular | e.g. "Partly Cloudy" |
| Weather temp (max/min) | Inter | 20pt | Bold | `↑24°  ↓14°` on one line |
| Weather rain | Inter | 16pt | Regular | `Rain 40%` |
| Birthday entries | Inter | 20pt | Medium | `🎂 Name` |
| Stock ticker | JetBrains Mono | 18pt | Bold | |
| Stock price / change | JetBrains Mono | 17pt | Regular | `▲` / `▼` prefix |
| Stocks line 2 | Inter | 15pt | Regular | Portfolio summary (static placeholder for now) |
| Quote text | Playfair Display | 16pt | Italic | Multi-line, max 2 lines |
| Quote attribution | Inter | 13pt | Regular | Right-aligned, `— Name, Source` |

**Fonts to download** (all free from Google Fonts / Nerd Fonts):
- Montserrat: [fonts.google.com/specimen/Montserrat](https://fonts.google.com/specimen/Montserrat) → `Montserrat-Bold.ttf`, `Montserrat-Regular.ttf`
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
      IF only clock+date region changed → partial refresh
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

Synced at every boot via `il.pool.ntp.org`. Prevents RTC drift over the max ~10-hour overnight sleep (22:00 → 05:45). Falls back to RTC on failure; self-corrects next boot.

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
- If no birthdays: Birthday zone hidden, Clock zone expands vertically to fill y=70 to y=380
- If API unavailable: Birthday zone hidden silently

**Update schedule:** 05:45 and 18:00.

---

### 6.2 Stock Data & Portfolio

**Purpose:** Show live or previous-close prices for a watchlist, and display a static portfolio summary on screen.

#### 6.2.1 Watchlist

Tracked tickers stored in `server\data\tickers.json`, managed via the admin panel. Not hardcoded anywhere.

```json
["AAPL", "TSLA", "SPY"]
```

#### 6.2.2 Portfolio Database

Holdings stored in `server\data\portfolio.json`, managed via the admin panel. Schema:

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

| Fetch time (Israel) | US market state | Shown as |
|---|---|---|
| **05:45** | Pre-market / closed | Previous day close — `[prev. close]` label |
| **17:30** | Market open (~1 hr in) | Live intraday price, no label |

#### 6.2.4 Display on Screen (v1 — Static Placeholder)

The stocks zone shows two lines:

**Line 1 — ticker prices** (live from Polygon.io):
```
AAPL $189  ▲1.2%    TSLA $172  ▼0.8%    SPY $524  ▲0.4%
```

**Line 2 — portfolio summary** (computed from `portfolio.json` + current prices):
```
Portfolio: $4,712 total  ·  Today: +$42 (+0.9%)
```

If `portfolio.json` has no holdings, line 2 is blank.

This is a static display — no AI suggestions or recommendations are shown in this version. The portfolio data and P&L calculation are fully implemented; only the AI-generated suggestion text is deferred to a future version when a Claude API key is available.

> All stock data is informational only — not financial advice.

---

### 6.3 Weather

**Provider:** Open-Meteo — free, no API key.

**Location:** Bat Yam, Israel — Lat `32.08`, Lon `34.78`, TZ `Asia/Jerusalem`

**Fields fetched:**

| Field | API parameter | Display |
|---|---|---|
| Current temperature | `current=temperature_2m` | Inline with icon |
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

The clock and date are both displayed within the clock zone — time large and centered, date directly below it in a smaller font.

| Element | Format | Example | Location |
|---|---|---|---|
| Time | `H:MM` (24h, no leading zero) | `7:42` | Clock zone, large centered |
| Date | `Day, Month D` | `Wednesday, April 6` | Clock zone, below time, centered |

**NTP server:** `il.pool.ntp.org`  
**Timezone:** `Asia/Jerusalem`  
**POSIX TZ string:** `IST-2IDT,M3.4.4/26,M10.5.0`

No year is shown in the date — day name and month+day only, as this feels more natural at a glance.

---

### 6.5 Quote of the Day

**Purpose:** Display a beautiful or thought-provoking quote at the top of the screen, from real people, history, or literature (Dune, The Alchemist, etc.).

**Source:** A pre-prepared local file `server\data\quotes.json`. A random quote is selected each morning at 05:45, seeded by the date, so the same quote shows all day and changes each morning. No Claude API is used.

**Selection logic:** `random.seed(today's date as integer) → random.choice(quotes list)`

| Property | Value |
|---|---|
| Source | `server\data\quotes.json` |
| Selection | Date-seeded random — same quote all day, new one each morning |
| Max displayed chars | 160 (two lines at 16pt in 800px strip) |
| Fallback | If `quotes.json` is missing or empty: display a single hardcoded default quote |

**Quote file format** (`server\data\quotes.json`) — upload or edit via admin panel:
```json
[
  {
    "quote": "The mystery of life isn't a problem to solve, but a reality to experience.",
    "attribution": "Frank Herbert, Dune"
  },
  {
    "quote": "When you want something, all the universe conspires in helping you to achieve it.",
    "attribution": "Paulo Coelho, The Alchemist"
  },
  {
    "quote": "The only true wisdom is in knowing you know nothing.",
    "attribution": "Socrates"
  }
]
```

**Preparation:** Use [claude.ai](https://claude.ai) (which you have access to) to generate a large collection of quotes in this exact JSON format, then upload the file via the admin panel. Aim for at least 100 entries for good variety across the year.

> **[ EDIT ]** The quotes file needs to be prepared and uploaded before the system is fully operational. See §9.1 for how to upload it via the admin panel.

---

## 7. Firmware Specification (E1001)

The firmware platform is **Zephyr RTOS**, chosen because Zephyr has official board support for the `reterminal_e1001` and an existing UC8179 ePaper driver compatible with the GDEY075T7 panel (via the ZEReader open-source project).

### 7.1 Libraries & Components

| Item | Zephyr component | Notes |
|---|---|---|
| RTOS | Zephyr v3.7+ | Board target: `reterminal_e1001` |
| ePaper driver | `drivers/display` + UC8179 | Reference: ZEReader project on GitHub |
| PNG decoder | `pngle` (vendored) | Lightweight C library |
| HTTP client | `net/http_client` | ETag header support |
| Wi-Fi | `esp_wifi` via Zephyr HAL | |
| NTP | `net/sntp` | `sntp_simple()` |
| Settings/storage | `settings` (NVS backend) | Stores ETag, refresh counter |
| Power management | `pm` subsystem | `pm_state_force(PM_STATE_SOFT_OFF)` |
| GPIO (button) | `gpio` driver | Green button wakeup |
| RTC | `rtc` driver | Fallback if NTP fails |

### 7.2 Firmware State Machine

```
BOOT (from PM sleep or power-on)
  │
  ├─► Init: display, SPI, GPIO, settings/NVS
  │
  ├─► Check wakeup reason:
  │     GPIO (green button) → set FORCE_REFRESH flag
  │     Timer              → continue normally
  │
  ├─► Connect Wi-Fi (timeout 15s, retry 5×)
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

### 7.3 Wi-Fi Error Screen

If Wi-Fi fails after 5 retries, display before sleeping:

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

### 7.4 Green Button — Manual Full Refresh

GPIO3 is configured as a wakeup source. Button press during sleep → boot with `FORCE_REFRESH` set → unconditional full refresh regardless of ETag → resume normal loop.

### 7.5 Build Configuration

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
pip install west --break-system-packages
west init ~/zephyrproject
cd ~/zephyrproject && west update && west zephyr-export
west sdk install

cd ~/Documents/reTerminal/firmware
west build -b reterminal_e1001 .
west flash --runner esptool
# or:
esptool.py -c esp32s3 -p /dev/ttyUSB0 write_flash 0x0 build/zephyr/zephyr.bin
```

### 7.6 Configuration (`src/config.h`)

```c
#define WIFI_SSID           "Eli"
#define WIFI_PASSWORD       "1020304050"
#define WIFI_RETRY_MAX      5
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
│   │   ├── calendar.py          # Google Calendar birthday fetch
│   │   ├── stocks.py            # Polygon.io + portfolio P&L calculation
│   │   ├── weather.py           # Open-Meteo fetch
│   │   └── quote.py             # Date-seeded random quote selection
│   ├── admin\
│   │   ├── routes.py            # FastAPI admin API routes
│   │   └── templates\
│   │       ├── base.html
│   │       ├── quotes.html
│   │       ├── portfolio.html
│   │       └── stocks.html
│   ├── data\
│   │   ├── quotes.json          # Quote library — upload via admin panel
│   │   ├── tickers.json         # Stock watchlist
│   │   └── portfolio.json       # Holdings database
│   ├── fonts\
│   │   ├── Montserrat-Bold.ttf
│   │   ├── Montserrat-Regular.ttf
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

Note: `quote_prompt.txt` and `suggestion_prompt.txt` are removed — no Claude API is used in this version.

**Virtual environment setup (first time):**
```powershell
cd "C:\Users\Eli Zeltser\Documents\reTerminal"
python -m venv .venv
.venv\Scripts\Activate.ps1
pip install fastapi uvicorn pillow httpx apscheduler jinja2 `
            google-api-python-client google-auth-oauthlib `
            python-dotenv
pip freeze > server\requirements.txt
```

Note: `anthropic` package is not installed in this version.

### 8.2 API Endpoints

**Display (used by E1001 firmware):**

| Endpoint | Method | Description |
|---|---|---|
| `/display.png` | GET | Rendered PNG with ETag. Returns `304` if unchanged. |
| `/status` | GET | JSON: last render time, source statuses, next fetches, ETag. |
| `/refresh` | POST | Force immediate re-render and data re-fetch. |

**Admin API (used by admin panel):**

| Endpoint | Method | Description |
|---|---|---|
| `/api/quotes` | GET | Return full quotes list as JSON |
| `/api/quotes` | POST | Add a new quote `{quote, attribution}` |
| `/api/quotes/{id}` | PUT | Update a quote entry |
| `/api/quotes/{id}` | DELETE | Remove a quote entry |
| `/api/quotes/upload` | POST | Upload a full `quotes.json` file to replace the current list |
| `/api/tickers` | GET | Return watchlist tickers |
| `/api/tickers` | POST | Add a ticker `{ticker}` |
| `/api/tickers/{ticker}` | DELETE | Remove a ticker |
| `/api/portfolio` | GET | Return full portfolio holdings |
| `/api/portfolio` | POST | Add a holding `{ticker, shares, avg_cost, notes}` |
| `/api/portfolio/{ticker}` | PUT | Update a holding |
| `/api/portfolio/{ticker}` | DELETE | Remove a holding |

**Admin UI pages:**

| Route | Description |
|---|---|
| `/admin` | Admin panel home |
| `/admin/quotes` | Quote library manager |
| `/admin/stocks` | Watchlist + portfolio manager |

### 8.3 Render Pipeline

```
scheduler triggers render()
  │
  ├─► Fetch in parallel (asyncio.gather):
  │     calendar.get_birthdays_today()
  │     weather.get_today()            [cached post-05:45]
  │     stocks.get_prices(tickers)     [cached post-scheduled fetch]
  │
  ├─► quote.get_today()               [date-seeded from quotes.json]
  ├─► Load portfolio.json → compute P&L summary
  │
  ├─► renderer.compose(data) → PIL Image (800×480, mode "1")
  │     draw_quote_strip()       [y=0..70]
  │     draw_weather_zone()      [x=0..220, y=70..380]
  │     draw_clock_zone()        [x=220..800, y=70..290 or 70..380]
  │     draw_birthday_zone()     [x=220..800, y=290..380, if any]
  │     draw_stocks_zone()       [y=380..480]
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
| Calendar fetch | Daily at 05:45 and 18:00 | Re-fetched for evening window |
| Quote selection | Daily at 05:45 | Date-seeded random pick from quotes.json |

### 8.5 `server\CLAUDE.md`

```markdown
# CLAUDE.md — ePaper Dashboard Server

## What this does
FastAPI server on Windows 10 (DESKTOP-NJR6V52), Asus A554I.
IP: 10.100.102.216 (DHCP-reserved, stable).
Renders 800×480 1-bit PNG for Seeed reTerminal E1001 ePaper display.
Hosts admin panel at /admin for managing quotes, stocks, portfolio.

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
- Data files in server\data\ are user-managed — never overwrite on startup
- No Claude/Anthropic API is used in this version

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

## Data files (never overwrite on startup)
- server\data\quotes.json     — quote library (upload via /api/quotes/upload)
- server\data\tickers.json    — stock watchlist
- server\data\portfolio.json  — holdings with shares + avg cost

## Data sources
- Calendar: Birthdays only, token.json auto-refreshes
- Stocks: Polygon.io at 05:45 and 17:30; P&L from portfolio.json
- Weather: Open-Meteo, lat=32.08 lon=34.78, no wind speed
- Quote: date-seeded random from quotes.json, no Claude API
```

---

## 9. Management UI — Admin Panel

The admin panel is served by the same FastAPI process on `/admin` routes. Accessible from any browser on the home network at `http://10.100.102.216:8080/admin`. No authentication required (local network only). Uses Jinja2 HTML templates.

### 9.1 Quote Manager (`/admin/quotes`)

Manages the quote library stored in `server\data\quotes.json`.

**Features:**
- **Upload file:** file upload button to replace the entire `quotes.json` with a new prepared file. This is the primary workflow — prepare a large file using [claude.ai](https://claude.ai), then upload it here.
- **List view:** table showing all quotes with index, text snippet, and attribution
- **Add quote:** form with quote text + attribution fields
- **Edit quote:** inline edit per entry
- **Delete quote:** delete button per entry
- **Preview:** "Refresh display now" button triggers `POST /refresh`
- **Stats:** shows total number of quotes in library and today's selected quote index

**Recommended quote file preparation workflow:**
1. Open [claude.ai](https://claude.ai)
2. Ask Claude to generate 100+ quotes in the exact JSON format shown in §6.5
3. Specify themes: literature (Dune, Alchemist), science, history, philosophy
4. Copy the JSON output to a file named `quotes.json`
5. Upload via this admin panel page

### 9.2 Stock & Watchlist Manager (`/admin/stocks`)

Manages `tickers.json` and `portfolio.json`.

**Watchlist section:**
- Current tickers listed with delete button
- Add ticker: text field + "Add" button

**Portfolio section:**
- Table: Ticker | Shares | Avg Cost | Notes | Current Price | Value | P&L | P&L%
- Current price and P&L computed from last cached stock fetch
- Add / edit / delete holdings
- If no stock fetch has occurred yet, Current Price and P&L columns show `—`

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

# ── Weather ──────────────────────────────────────────────────
WEATHER_LAT=32.08
WEATHER_LON=34.78
WEATHER_TIMEZONE=Asia/Jerusalem
WEATHER_UNITS=celsius

# ── Server ───────────────────────────────────────────────────
SERVER_HOST=0.0.0.0
SERVER_PORT=8080
```

No `ANTHROPIC_API_KEY` in this version.

### 10.2 User-Editable Data Files

| File | Content | Managed via |
|---|---|---|
| `data\quotes.json` | Quote library | Admin panel → Quotes (upload or individual edit) |
| `data\tickers.json` | Stock watchlist | Admin panel → Stocks |
| `data\portfolio.json` | Holdings with shares + avg cost | Admin panel → Stocks |

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
| **Wi-Fi fails at boot (E1001)** | Retry 5×, 5s apart → render error screen → PM sleep 5 min → retry |
| **NTP sync fails** | Use RTC, log warning. Self-corrects next boot |
| **Server unreachable** | Keep last displayed image. Sleep and retry |
| **Google Calendar API error** | Birthday zone hidden silently |
| **Polygon.io error / rate limit** | Last known price with `[delayed]` label |
| **Weather API error** | Weather zone shows `—` |
| **Server render exception** | Log traceback. Serve last cached PNG. Server stays up |
| **PNG decode error (E1001)** | Skip update, log, sleep normally |
| **quotes.json missing or empty** | Display hardcoded default quote: `"Not all those who wander are lost." — J.R.R. Tolkien` |
| **portfolio.json missing or empty** | Stocks zone shows tickers only, line 2 blank |

---

## 12. Development Toolchain

### 12.1 Laptop / Server Side (Windows 10)

| Tool | Purpose | Install |
|---|---|---|
| Python 3.12 | Runtime | [python.org](https://python.org) |
| venv | Isolation | `python -m venv .venv` |
| FastAPI + Uvicorn | HTTP server + admin routes | `pip install fastapi uvicorn` |
| Jinja2 | Admin panel templates | `pip install jinja2` |
| Pillow | Image rendering | `pip install pillow` |
| httpx | Async HTTP client | `pip install httpx` |
| APScheduler | Task scheduling | `pip install apscheduler` |
| google-api-python-client | Calendar SDK | `pip install google-api-python-client google-auth-oauthlib` |
| python-dotenv | `.env` loading | `pip install python-dotenv` |
| NSSM | Windows Service manager | [nssm.cc](https://nssm.cc/download) |
| Claude Code | AI-assisted development | `npm install -g @anthropic-ai/claude-code` |

**Full install:**
```powershell
C:\Users\Eli Zeltser\Documents\reTerminal\.venv\Scripts\Activate.ps1
pip install fastapi uvicorn jinja2 pillow httpx apscheduler `
            google-api-python-client google-auth-oauthlib python-dotenv
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

> Keep `PLATFORMIO_CORE_DIR=/opt/platformio` in `.bashrc`.

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
| 3 | E1001 IP | DHCP-reserved at `10.100.102.4` | ✅ Done |
| 4 | Quotes file preparation | Use claude.ai to generate 100+ quotes, upload via admin panel | <!-- TBD --> |
| 5 | Green button refresh | ✅ Resolved — full refresh on button press | ✅ Done |
| 6 | ZEReader UC8179 driver | Reference or fork from ZEReader repo — confirm license | <!-- TBD --> |
| 7 | Birthday zone if empty | Clock+date expand to fill space — confirm visually after first run | <!-- TBD --> |
| 8 | Admin panel auth | Currently none (LAN only). Add basic password if desired in future. | <!-- TBD --> |
| 9 | Claude API / AI features | Deferred — re-enable suggestion_prompt.txt and quote AI when API key available | <!-- Future v2 --> |

---

## Appendix A — Change Log

| Version | Date | Changes |
|---|---|---|
| 0.1 | 01.05.2026 | Initial draft |
| 0.2 | 01.05.2026 | Quote of the day; network topology; NSSM; grid redesign; weather fields; NTP; ESP-IDF firmware; Wi-Fi error screen; stock schedule |
| 0.3 | 01.05.2026 | Venv path; full Windows paths; Ethernet static IP setup; layout redesign; quote sources expanded to literature; firmware to Zephyr; green button resolved |
| 0.4 | 01.05.2026 | DHCP reservation confirmed; laptop IP/hostname/gateway filled in; quote strip h=70; clock enlarged (110pt); birthday zone reduced; footer added (70px); wind speed removed; stocks zone 90px; portfolio.json added; tickers.json admin-managed; Claude prompt files added; admin panel §9 with portfolio AI query; jinja2 added |
| 0.5 | 01.05.2026 | Claude API removed (no key available) — quotes now use local `quotes.json` with date-seeded random selection; stock AI suggestions replaced with static P&L summary placeholder; `quote_prompt.txt` and `suggestion_prompt.txt` removed; `anthropic` package removed from dependencies; footer zone removed — canvas is now Quote/Weather/Clock+Date+Birthdays/Stocks only; clock zone enlarged (h=220); weather zone enlarged (h=310); stocks zone enlarged (h=100); date moved into clock zone below the time; clock font changed from Inter to **Montserrat** (Bold 120pt for time, Regular 24pt for date); date format changed to `Wednesday, April 6` (no year); partial refresh region updated (clock+date only, no footer); §7.1 comparison table removed — Zephyr choice stated concisely; `WIFI_RETRY_MAX` changed from 3 to **5**; Wi-Fi error screen updated to reflect 5 retries; admin panel §9.1 updated to file-upload workflow with claude.ai preparation instructions; `/api/quotes/upload` endpoint added; fallback quote added to error handling table; `portfolio/query` Claude endpoint removed from admin panel and API table |

---

*All `<!-- EDIT -->` and `<!-- TBD -->` markers indicate places requiring input before implementation begins.*
