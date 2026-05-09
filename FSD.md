# Functional Specification: ePaper Smart Dashboard System
**Document version:** 0.6 — Draft  
**Last updated:** 9.5.2026  
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
   - 6.6 [Moon Phase](#66-moon-phase)
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

This document describes the functional requirements of a personal smart dashboard built on a **Seeed Studio reTerminal E1001** 7.5-inch monochrome ePaper display. The system provides an at-a-glance view of time, weather, birthday reminders, moon phase, and a daily quote during defined morning and evening periods. All dashboard content and configuration is manageable via a local web admin panel.

### 1.2 Goals

- Display accurate, readable information during two daily active windows without requiring interaction.
- Maximize screen lifespan by minimizing unnecessary full refreshes and enforcing a safe refresh schedule.
- Keep the display firmware simple and stateless — all intelligence and rendering lives on the laptop server.
- Provide a local admin panel to manage quotes, stock watchlist, and portfolio without editing files manually.
- Enable future extension of data sources without reflashing the device.
- Provide a pleasant shared morning and evening experience for the household.
- Display a screensaver during inactive hours to prevent burn-in and provide ambient visual interest.

### 1.3 Non-Goals

- This is not a real-time trading terminal. Stock data is informational only and not financial advice.
- The display is not interactive beyond the hardware buttons (v1).
- The system does not need to function when the server is off or unreachable.
- No Claude API key is used in this version — all AI features are deferred to a future version.

### 1.4 Intended Users

This project will be used specifically for our home, as a display for us to view and enjoy in our joint mornings and evenings.

---

## 2. System Architecture

### 2.1 High-Level Overview

The system follows a **server-rendered pull model**. The Arch Linux laptop serves as the intelligence layer; the E1001 is a dumb display that polls for a pre-rendered image.

```
┌──────────────────────────────────────────────┐
│    Arch Linux Laptop (Server)                │
│    Home LAN — SERVER_HOST in config.h        │
│                                              │
│  ┌──────────────┐   ┌─────────────────────┐  │
│  │ Data Fetcher │──►│ Image Renderer      │  │
│  │              │   │ (Pillow → PNG)      │  │
│  └──────────────┘   └────────┬────────────┘  │
│   Google Calendar            │               │
│   Polygon.io          FastAPI HTTP :8080      │
│   Open-Meteo                 │               │
│   ephem (moon)        ┌──────┴──────┐        │
│                       │ Admin Panel │        │
│  ┌────────────────┐   │ (Web UI)    │        │
│  │ portfolio.json │   └─────────────┘        │
│  │ quotes.json    │                          │
│  │ tickers.json   │                          │
│  │ screensavers/  │                          │
│  └────────────────┘                          │
└──────────────────────────────│───────────────┘
                               │  Home LAN (router 10.100.102.1)
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
Every render cycle (server side):
  1. Fetch fresh data from all sources (calendar, weather, stocks, moon)
  2. Load portfolio from portfolio.json — compute P&L summary
  3. Select today's quote from quotes.json (random, date-seeded)
  4. Composite all data into an 800×480 grayscale PNG (2× supersampled)
  5. Cache PNG with a new ETag if content changed
  6. Serve via HTTP

Every poll cycle (E1001 side):
  1. Wake from PM sleep (timer or GPIO button)
  2. Connect to Wi-Fi
  3. Determine mode from local time
  4. If active window: GET /display.png with If-None-Match: <last_etag>
     If 304 → skip display update, go back to sleep
     If 200 → decode PNG, refresh display, store new ETag in NVS
  5. If inactive, timer wake: GET /screensaver/{random_index} → display screensaver
  6. If inactive, button wake: show requested display buffer for 60s
  7. If NTP sync time (0:00): clock already updated, no display action
  8. Compute sleep duration until next scheduled event
  9. Enter PM sleep
```

### 2.3 Network Topology

| Item | Value |
|---|---|
| Server | Arch Linux laptop |
| Server IP | Set as `SERVER_HOST` in `firmware/src/config.h` |
| Router gateway | `10.100.102.1` |
| Server port | `8080` |
| Admin panel | Same port, routes `/admin/*` |
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
| Full refresh | ~3s | Multiple flashes | Window start, button press, screensaver exit |
| Fast refresh | ~1.5s | Single flash | Content change |
| Partial refresh | ~0.3s | None | Clock tick |

### 3.2 Server — Arch Linux Laptop

**FastAPI + Uvicorn** handles async data fetching natively (all API calls run concurrently), has built-in ETag/`304 Not Modified` support, and hosts the admin panel on the same process.

**Quick start:**
```bash
source ~/Documents/reTerminal/.venv/bin/activate
cd ~/Documents/reTerminal/server
uvicorn main:app --host 0.0.0.0 --port 8080 --reload
```

| Property | Value |
|---|---|
| OS | Arch Linux |
| Python version | 3.12+ |
| Always-on | Yes |
| Project root | `~/Documents/reTerminal/` |
| Virtual environment | `~/Documents/reTerminal/.venv` |
| Server directory | `~/Documents/reTerminal/server/` |
| Server framework | FastAPI + Uvicorn |
| Image rendering | Pillow (PIL), grayscale mode, 2× supersampling |
| Admin panel | Same FastAPI process, `/admin` routes |

### 3.3 Data Sources

| Source | Provider | Auth | Update frequency |
|---|---|---|---|
| Calendar (birthdays) | Google Calendar API v3 | OAuth2 offline token | 05:45 and 18:00 daily |
| Stock prices | Polygon.io | API key | 05:45 and 17:30 daily |
| Portfolio data | Local `portfolio.json` | None | On admin save |
| Weather | Open-Meteo (free, no key) | None | Once daily at 05:45 |
| Quote of the day | Local `quotes.json` | None | Daily random selection at 05:45 |
| Moon phase | `ephem` library (local) | None | Daily at 05:45 |
| Time / Date | E1001 system clock (NTP-synced) | N/A | Every render cycle |
| Screensavers | Local `data/screensavers/*.png` | None | Manually managed |

### 3.4 Communication Layer

- **Protocol:** HTTP/1.1 over home LAN
- **Display image:** PNG, 8-bit grayscale (4-level dithered), 800×480, served with ETag
- **Screensaver images:** PNG, 8-bit grayscale (4-level dithered), 800×480, served by index
- **Admin panel:** HTML pages on `/admin/*` routes (Jinja2 templates)
- **Data API:** JSON endpoints on `/api/*` routes for admin interactions
- **Security:** Local network only. No TLS in v1.

---

## 4. Display Layout & Visual Design

### 4.1 Grid Structure

The canvas has four zones: quote at top, weather left (with moon phase at bottom of that column), clock+date+reminders right. Stocks are fetched but not currently rendered on screen.

```
┌──────────────────────────────────────────────────────────────────┐ y=0
│  "The mystery of life isn't a problem to solve, but a reality    │ h=90
│   to experience."                      — Frank Herbert, Dune     │
├──────────────────────┬───────────────────────────────────────────┤ y=90
│                      │                                           │
│  ⛅                  │              7:42                         │
│  Partly Cloudy       │                                           │ h=260
│                      │          Wednesday, April 6               │
│  ↑24°  ↓14°          │                                           │
│  Rain 40%            ├───────────────────────────────────────────┤ y=350
│                      │   🎂 Mum's birthday is today              │ h=130
│                      │   🎂 Dan's birthday is in 3 days          │
│  🌙 (moon disc)      │                                           │
├──────────────────────┴───────────────────────────────────────────┤ y=480
```

**Zone definitions:**

| Zone | x | y | w | h | Content |
|---|---|---|---|---|---|
| Quote | 0 | 0 | 800 | 90 | Quote italic (left) + attribution (right-aligned) |
| Weather | 0 | 90 | 200 | 390 | Icon + description + ↑max ↓min + rain% + moon disc |
| Clock | 200 | 90 | 600 | 260 | Large `H:MM` centered, date below |
| Reminders | 200 | 350 | 600 | 130 | Birthday/reminder entries — hidden + clock expands if none |
| Stocks | — | — | — | — | Fetched, not rendered (reserved for future version) |

**Layout rules:**
- No zone title labels are shown on screen.
- If no reminders today: Reminder zone is hidden; Clock zone expands to `h=390` (y=90 to y=480).
- Quote strip supports up to two lines of text at 25pt; attribution is always right-aligned on the last line.
- The time shown is always rendered as **now + 1 minute** so the displayed time remains accurate once the ePaper finishes refreshing.

**Dividers:**
- Horizontal at y=90 (full width)
- Vertical at x=200 (y=90 to y=480)
- Horizontal at y=350 (x=200 to x=800 — separates clock from reminders, only when reminders exist)

### 4.2 Partial Refresh Region

On each minute-tick, only the clock sub-region is redrawn (clock time + date below it).

| Region | x | y | w | h |
|---|---|---|---|---|
| Clock + date | 200 | 90 | 600 | 260 |

### 4.3 Typography

| Element | Font | Size | Style | Notes |
|---|---|---|---|---|
| Clock (H:MM) | Montserrat | 148pt | Regular | No leading zero on hour |
| Date below clock | Montserrat | 32pt | Regular | `Day, Month D` format — `Wednesday, April 6` |
| Weather icon | NerdFontsSymbolsOnly | 72pt | Regular | WMO code → glyph (see §6.3) |
| Weather description | Inter | 22pt | Regular | e.g. "Partly Cloudy" |
| Weather temp (max/min) | Montserrat | 30pt | Bold | `↑24°  ↓14°` on one line |
| Weather rain | Inter | 20pt | Regular | `Rain 40%` |
| Reminder entries | Inter | 20pt | Medium | Icon + text, up to 3 entries |
| Quote text | Playfair Display | 25pt | Italic | Multi-line, max 2 lines |
| Quote attribution | Inter | 14pt | Regular | Right-aligned, `— Name, Source` |

**Fonts** (all free from Google Fonts / Nerd Fonts):
- Montserrat: `Montserrat-Regular.ttf`
- Inter: `Inter-Medium.ttf`, `Inter-Regular.ttf`
- Playfair Display: `PlayfairDisplay-Italic.ttf`
- Nerd Fonts: `NerdFontsSymbolsOnly-Regular.ttf`

Place all files in `server/fonts/`.

### 4.4 Visual Style Rules

- Background: white, foreground: black
- Zone dividers: single-pixel black lines
- **Rendering pipeline:** Pillow draws on a 2× canvas (1600×960) in `"L"` (8-bit grayscale) mode, then downsamples to 800×480 with LANCZOS for smooth text and clean icon rendering.
- **Output format:** PNG, 4-level grayscale dithered to the UC8179 greyscale palette (luminance values 0, 85, 170, 255).
- Firmware decodes using `png_decode_4gray()` and drives the panel in 4-gray mode.
- Stock change: `▲` for positive, `▼` for negative (in admin panel / status, not rendered on screen)
- Weather icons: Nerd Font glyphs (see §6.3)

---

## 5. Update Schedule & Screen Management

### 5.1 Active Windows

| Window | Start | End | Behaviour |
|---|---|---|---|
| Morning | 05:45 | 08:00 | Poll server every 60s; ETag check to skip unchanged content |
| Evening | 18:00 | 22:00 | Poll server every 60s; ETag check to skip unchanged content |

Times are local (Asia/Jerusalem), synced via NTP.

### 5.2 Scheduled Events (Inactive Periods)

There are no forced maintenance screen refreshes. During inactive periods the device uses long sleeps, waking only at scheduled events:

| Event | Time | Action |
|---|---|---|
| NTP sync | 00:00 | Clock updated internally — **no display change** |
| Morning start | 05:45 | Enter active window, begin polling |
| Evening start | 18:00 | Enter active window, begin polling |

Between events the device shows a screensaver on each timer wake (see §5.5).

### 5.3 Refresh Decision Logic

```
On each wake:
  Determine mode from local time → MODE_ACTIVE_WINDOW / MODE_NTP_SYNC_ONLY / MODE_INACTIVE

  MODE_NTP_SYNC_ONLY (00:00):
    → NTP sync ran at boot — no display action
    → sleep until 05:45

  MODE_ACTIVE_WINDOW (05:45–08:00 or 18:00–22:00):
    IF green button pressed:
      → GET /display.png unconditionally (no ETag) → full refresh → sleep 60s
    ELSE IF left/right button pressed:
      → cycle buf_index in NVS → GET /display/{buf_index} → full refresh → sleep 60s
    ELSE (timer wake):
      → GET /display.png with If-None-Match: <stored_etag>
      200 and ETag changed → decode → refresh display → save ETag
      304 → skip update
    → sleep 60s

  MODE_INACTIVE:
    IF button pressed:
      → show requested display (or /display.png for green) → sleep 60s
      (next timer wake will show screensaver and sleep until next event)
    ELSE (timer wake):
      → GET /screensaver/{random_index} → display screensaver
      → sleep until next scheduled event

After 5 consecutive partial/fast refreshes during active window:
  → force full refresh → reset counter
```

### 5.4 Sleep Duration Calculation

- **During active window:** 60 seconds
- **Timer wake at NTP sync or inactive:** sleep until next event in `{00:00, 05:45, 18:00}`. After the last event of the day (18:00), the next target is 00:00.
- **On Wi-Fi failure:** sleep 5 minutes, retry

### 5.5 Screensaver Mode

During inactive timer wakes, the device fetches and displays one screensaver image instead of the main dashboard:

1. Firmware calls `GET /screensaver/count` → server returns count of PNGs in `data/screensavers/`
2. Firmware picks `index = sys_rand32_get() % count`
3. Firmware calls `GET /screensaver/{index}` → server returns the PNG file
4. PNG is decoded and displayed with a 4-gray full refresh
5. The `ss_last` flag is set in NVS so the next main-display fetch forces a full refresh to clear screensaver ghosting

Screensavers are stored as numbered PNGs (`0.png`, `1.png`, …) in `server/data/screensavers/`. Managed via `tools/screensaver_converter.py` (see §12.3).

### 5.6 Display Buffer Rotation

Left and right buttons cycle through display buffers stored on the server. The current buffer index (`buf_index`) is persisted in NVS across sleeps.

| Button | Effect |
|---|---|
| Left | `buf_index--` (min 0) |
| Right | `buf_index++` |
| Green | Reset to main display (`/display.png`), clear ETag, force full refresh |

The firmware fetches `/display/{buf_index}` when index is changed. The server currently implements `/display/0` only (returns 404 for n > 0, which causes the firmware to reset `buf_index` to 0 and fall back to `/display.png`). Additional display buffers can be added server-side in a future version.

### 5.7 NTP Time Synchronisation

NTP sync via `il.pool.ntp.org` runs at **every boot** (device wakes at 00:00 specifically for this once per day). Prevents RTC drift during the max ~5-hour overnight sleep (22:00 → 00:00 → 05:45). Falls back to RTC on failure; self-corrects next boot. The 00:00 wake does **not** trigger any screen update.

---

## 6. Data Integration Specifications

### 6.1 Google Calendar

**Purpose:** Display today's birthday reminders and upcoming events below the clock.

**Auth:** OAuth2 offline refresh token. Credentials in `server/secrets/credentials.json`. Scope: `https://www.googleapis.com/auth/calendar.readonly`.

**Query:**
- Calendar: Birthdays (primary Google account)
- Time range: Start of today → 23:59 local
- All-day events only
- Max 6 results

**Display rules:**
- Format: `🎂 Name's birthday is today` / `🎂 Name's birthday is in N days`
- Non-birthday events: plain event name shown without icon
- Up to 3 entries shown
- If no reminders: Reminder zone hidden, Clock zone expands to fill y=90 to y=480
- If API unavailable: Reminder zone hidden silently

**Update schedule:** 05:45 and 18:00.

---

### 6.2 Stock Data & Portfolio

**Purpose:** Track watchlist prices and portfolio P&L. Data is fetched and cached but **not currently rendered on screen** (reserved for a future version).

#### 6.2.1 Watchlist

Tracked tickers stored in `server/data/tickers.json`, managed via the admin panel.

```json
["AAPL", "TSLA", "SPY"]
```

#### 6.2.2 Portfolio Database

Holdings stored in `server/data/portfolio.json`, managed via the admin panel. Schema:

```json
{
  "holdings": [
    {
      "ticker": "AAPL",
      "shares": 10,
      "avg_cost": 165.00,
      "notes": "Long-term hold"
    }
  ],
  "last_updated": "2026-05-01T07:30:00"
}
```

#### 6.2.3 Data Provider

**Polygon.io** — API key required.

| Fetch time (Israel) | US market state | Shown as |
|---|---|---|
| **05:45** | Pre-market / closed | Previous day close |
| **17:30** | Market open (~1 hr in) | Live intraday price |

#### 6.2.4 Display on Screen

Stocks are not rendered in the current version. The stocks zone coordinates are reserved at `y=480` (off-screen) in the renderer. Portfolio P&L is viewable in the admin panel at `/admin/stocks`. Stock rendering will return in a future version.

---

### 6.3 Weather

**Provider:** Open-Meteo — free, no API key.

**Location:** Bat Yam, Israel — Lat `32.08`, Lon `34.78`, TZ `Asia/Jerusalem`

**Fields fetched:**

| Field | API parameter | Display |
|---|---|---|
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

The rendered time is **now + 1 minute** so the display remains accurate by the time the ePaper finishes refreshing.

No year is shown — day name and month+day only.

---

### 6.5 Quote of the Day

**Purpose:** Display a beautiful or thought-provoking quote at the top of the screen.

**Source:** `server/data/quotes.json`. A random quote is selected each morning at 05:45, seeded by the date, so the same quote shows all day and changes each morning.

**Selection logic:** `random.seed(today's date as integer) → random.choice(quotes list)`

| Property | Value |
|---|---|
| Source | `server/data/quotes.json` |
| Selection | Date-seeded random — same quote all day, new one each morning |
| Fallback | If `quotes.json` is missing or empty: `"Not all those who wander are lost." — J.R.R. Tolkien` |

**Quote file format:**
```json
[
  {
    "quote": "The mystery of life isn't a problem to solve, but a reality to experience.",
    "attribution": "Frank Herbert, Dune"
  }
]
```

---

### 6.6 Moon Phase

**Purpose:** Display a visual disc in the weather column showing the current lunar phase.

**Library:** `ephem` (local computation — no network call required)

**Calculation:** Phase is expressed as a float `0.0` (new moon) → `0.5` (full moon) → `1.0` (new moon again), computed from the ratio of days elapsed since the previous new moon to the full cycle length.

**Rendering in the weather zone:**

| Property | Value |
|---|---|
| Position Y | ≈ y=420 (lower portion of weather column) |
| Position X | Tracks phase: new moon near left, full moon at centre, back to left |
| Disc radius | Bell curve (sin): min 8px at new moon, max 34px at full moon |
| Style | Black disc with white illuminated region; outline ring |

The disc always appears regardless of whether weather data is available. It updates once daily at 05:45 with the morning fetch.

---

## 7. Firmware Specification (E1001)

The firmware platform is **Zephyr RTOS**, chosen because Zephyr has official board support for the `reterminal_e1001` and an existing UC8179 ePaper driver.

### 7.1 Libraries & Components

| Item | Zephyr component | Notes |
|---|---|---|
| RTOS | Zephyr v3.7+ | Board target: `reterminal_e1001/esp32s3/procpu` |
| ePaper driver | `drivers/display` + UC8179 | 4-gray mode |
| PNG decoder | `pngle` (vendored) | Lightweight C library, `png_decode_4gray()` |
| HTTP client | `net/http_client` | ETag header support |
| Wi-Fi | `esp_wifi` via Zephyr HAL | |
| NTP | `net/sntp` | `sntp_simple()` |
| Settings/storage | `settings` (NVS backend) | ETag, buf_index, partial_count, ss_last |
| Power management | `pm` subsystem | `sys_poweroff()` + `esp_sleep_enable_timer_wakeup()` |
| GPIO (buttons) | `gpio` driver | Left, Right, Green wakeup sources |

### 7.2 Firmware State Machine

```
BOOT (from PM sleep or power-on)
  │
  ├─► Init: display (4-gray), SPI, GPIO, NVS
  │
  ├─► Detect wakeup source:
  │     GPIO (green) → green_wake flag
  │     GPIO (left)  → left_wake flag  → cycle buf_index--
  │     GPIO (right) → right_wake flag → cycle buf_index++
  │     Timer        → continue normally
  │
  ├─► Connect Wi-Fi (timeout 15s, retry 5×)
  │     FAILURE → render Wi-Fi error screen → PM sleep 5 min → reboot
  │
  ├─► SNTP sync (il.pool.ntp.org)
  │     FAILURE → use RTC, log warning
  │
  ├─► Determine mode from local time:
  │
  │   MODE_NTP_SYNC_ONLY (00:00):
  │     → NTP already ran above — no display action
  │     → sleep until 05:45
  │
  │   MODE_INACTIVE:
  │     IF button wake:
  │       → fetch /display.png or /display/{buf_index} → full refresh
  │       → sleep 60s  (next timer wake will show screensaver)
  │     ELSE (timer wake):
  │       → screensaver_show_one(rand, ...)
  │         GET /screensaver/count → pick random index
  │         GET /screensaver/{index} → decode 4-gray → refresh
  │       → sleep until next event in {00:00, 05:45, 18:00}
  │
  │   MODE_ACTIVE_WINDOW (05:45–08:00 or 18:00–22:00):
  │     IF ss_last (returning from screensaver):
  │       → force full refresh, reset partial_count, clear ss_last
  │     IF green wake:
  │       → GET /display.png (no ETag) → full refresh → sleep 60s
  │     ELSE IF left/right wake:
  │       → GET /display/{buf_index} → full refresh → sleep 60s
  │     ELSE (timer):
  │       → GET /display.png with If-None-Match: <etag>
  │       200 → decode → refresh → save ETag
  │       304 → skip
  │     → sleep 60s
  │
  └─► Enter PM sleep (esp_sleep_enable_timer_wakeup + sys_poweroff)
```

### 7.3 Wi-Fi Error Screen

If Wi-Fi fails after 5 retries, display before sleeping:

```
┌──────────────────────────────────────────────────────────┐
│                                                          │
│   Wi-Fi connection failed                                │
│   Could not connect to: <SSID>                           │
│   Retrying in 5 minutes.                                 │
│                                                          │
└──────────────────────────────────────────────────────────┘
```

### 7.4 Button Behaviour

| Button | Active window | Inactive |
|---|---|---|
| Green (GPIO3) | Force full refresh of main display | Show main display for 60s |
| Left (GPIO4) | `buf_index--`, fetch `/display/{n}`, full refresh | Show previous buffer for 60s |
| Right (GPIO5) | `buf_index++`, fetch `/display/{n}`, full refresh | Show next buffer for 60s |

All button wakes trigger a 60s sleep after displaying; the following timer wake returns to normal mode behaviour (screensaver if inactive).

### 7.5 NVS Persisted State

| Key | Type | Purpose |
|---|---|---|
| `etag` | string (64B) | Last ETag received from server |
| `buf_index` | uint8 | Current display buffer index |
| `partial_count` | uint8 | Consecutive partial refreshes before forced full |
| `ss_last` | uint8 | 1 if previous display was a screensaver |

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
│   ├── config.h
│   ├── wifi.c / wifi.h
│   ├── ntp.c / ntp.h
│   ├── http_fetch.c / http_fetch.h
│   ├── png_decode.c / png_decode.h
│   ├── epaper.c / epaper.h
│   ├── schedule.c / schedule.h
│   ├── button.c / button.h
│   ├── screensaver.c / screensaver.h
│   └── nvs_store.c / nvs_store.h
└── lib/
    └── pngle/
```

**Build and flash (from Arch Linux):**
```bash
cd ~/zephyrproject
west build -b reterminal_e1001/esp32s3/procpu ~/Documents/reTerminal/firmware
west flash --runner esptool
# or manually:
esptool.py -c esp32s3 -p /dev/ttyUSB0 write_flash 0x0 build/zephyr/zephyr.bin
```

### 7.7 Configuration (`src/config.h`)

```c
#define WIFI_SSID           "Eli"
#define WIFI_PASSWORD       "1020304050"
#define WIFI_RETRY_MAX      5
#define WIFI_TIMEOUT_MS     15000

#define SERVER_HOST         "10.100.102.4"   // Arch Linux server LAN IP
#define SERVER_PORT         8080
#define SERVER_PATH         "/display.png"

#define NTP_SERVER          "il.pool.ntp.org"
#define TZ_POSIX_STRING     "IST-2IDT,M3.4.4/26,M10.5.0"

// Active windows (24h local time)
#define MORNING_START_H     5
#define MORNING_START_M     45
#define MORNING_END_H       8
#define MORNING_END_M       0
#define EVENING_START_H     18
#define EVENING_START_M     0
#define EVENING_END_H       22
#define EVENING_END_M       0

// Daily NTP sync (no screen refresh)
#define NTP_SYNC_H          0
#define NTP_SYNC_M          0

#define MAX_PARTIAL_BEFORE_FULL  5
#define POLL_INTERVAL_ACTIVE_S   60
#define POLL_INTERVAL_INACTIVE_S 1800

// Buttons
#define BUTTON_GREEN_PIN    3
#define BUTTON_LEFT_PIN     4
#define BUTTON_RIGHT_PIN    5

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
~/Documents/reTerminal/
├── .venv/
├── server/
│   ├── main.py                  # FastAPI app — all HTTP routes
│   ├── renderer.py              # Pillow canvas rendering (2× supersampling)
│   ├── scheduler.py             # APScheduler tasks (fetches + clock tick)
│   ├── state.py                 # Shared in-memory state (PNG bytes, ETag, source data)
│   ├── sources/
│   │   ├── calendar_src.py      # Google Calendar birthday fetch
│   │   ├── stocks.py            # Polygon.io + portfolio P&L calculation
│   │   ├── weather.py           # Open-Meteo fetch
│   │   ├── quote.py             # Date-seeded random quote selection
│   │   └── moonphase.py         # ephem moon phase computation
│   ├── admin/
│   │   ├── routes.py            # FastAPI admin API routes
│   │   └── templates/
│   │       ├── base.html
│   │       ├── quotes.html
│   │       ├── stocks.html
│   │       └── portfolio.html
│   ├── data/
│   │   ├── quotes.json          # Quote library
│   │   ├── tickers.json         # Stock watchlist
│   │   ├── portfolio.json       # Holdings database
│   │   └── screensavers/        # Numbered PNG files: 0.png, 1.png, …
│   ├── fonts/
│   │   ├── Montserrat-Regular.ttf
│   │   ├── Inter-Medium.ttf
│   │   ├── Inter-Regular.ttf
│   │   ├── PlayfairDisplay-Italic.ttf
│   │   └── NerdFontsSymbolsOnly-Regular.ttf
│   ├── secrets/
│   │   ├── credentials.json     # Google OAuth — NEVER COMMIT
│   │   ├── token.json           # Google OAuth token — NEVER COMMIT
│   │   └── .env                 # API keys — NEVER COMMIT
│   └── requirements.txt
└── tools/
    └── screensaver_converter.py # GUI tool to prepare screensaver PNGs
```

### 8.2 API Endpoints

**Display (used by E1001 firmware):**

| Endpoint | Method | Description |
|---|---|---|
| `/display.png` | GET | Rendered PNG with ETag. Returns `304` if unchanged. |
| `/display/{n}` | GET | Nth display buffer. Currently only `n=0` is implemented; returns `404` for others. |
| `/screensaver/count` | GET | JSON `{"count": N}` — number of screensaver PNGs available. |
| `/screensaver/{n}` | GET | Returns screensaver PNG file number `n`. |
| `/status` | GET | JSON: last render time, source statuses, ETag. |
| `/refresh` | POST | Force immediate re-render. |

**Admin API:**

| Endpoint | Method | Description |
|---|---|---|
| `/api/quotes` | GET | Return full quotes list |
| `/api/quotes` | POST | Add a new quote `{quote, attribution}` |
| `/api/quotes/{idx}` | PUT | Update a quote entry |
| `/api/quotes/{idx}` | DELETE | Remove a quote entry |
| `/api/quotes/upload` | POST | Upload a full `quotes.json` file |
| `/api/tickers` | GET | Return watchlist tickers |
| `/api/tickers` | POST | Add a ticker `{ticker}` |
| `/api/tickers/{ticker}` | DELETE | Remove a ticker |
| `/api/portfolio` | GET | Return full portfolio holdings |
| `/api/portfolio` | POST | Add a holding `{ticker, shares, avg_cost, notes}` |
| `/api/portfolio/{ticker}` | PUT | Update a holding |
| `/api/portfolio/{ticker}` | DELETE | Remove a holding |
| `/api/refetch/{source}` | POST | Force re-fetch of a named source (`weather`, `calendar`, `stocks`, `quote`, `moon`) and re-render. |

**Admin UI pages:**

| Route | Description |
|---|---|
| `/admin` | Admin panel home — shows source statuses |
| `/admin/quotes` | Quote library manager |
| `/admin/stocks` | Watchlist + portfolio manager |

### 8.3 Render Pipeline

```
scheduler triggers do_render() (or /refresh POST)
  │
  ├─► Uses cached data in state.py:
  │     state.weather_data     (from Open-Meteo, refreshed at 05:45)
  │     state.calendar_data    (from Google Calendar, refreshed 05:45 / 18:00)
  │     state.stock_data       (from Polygon.io, refreshed 05:45 / 17:30)
  │     state.quote_data       (date-seeded from quotes.json, refreshed 05:45)
  │     state.moon_phase       (from ephem, refreshed 05:45)
  │
  ├─► renderer.compose(weather, birthdays, stocks, quote, moon_phase)
  │     Render at 2× (1600×960) in PIL "L" grayscale mode:
  │       draw_quote_strip()     [y=0..90]
  │       draw_weather_zone()    [x=0..200, y=90..480] incl. moon disc at y≈420
  │       draw_clock_zone()      [x=200..800, y=90..350 or 90..480]
  │       draw_birthday_zone()   [x=200..800, y=350..480, if any]
  │       draw_dividers()
  │     Downsample to 800×480 with LANCZOS
  │
  ├─► MD5 hash → ETag
  ├─► Store png_bytes and etag in state.py (in-memory)
  └─► Update last_render_time
```

### 8.4 Scheduling

| Task | Schedule | Notes |
|---|---|---|
| Morning fetch | 05:45 daily | All sources in parallel: weather, calendar, stocks, quote, moon phase → then render |
| Evening calendar re-fetch | 18:00 daily | Calendar re-queried for evening window |
| Evening stocks re-fetch | 17:30 daily | Stock prices refreshed before evening window |
| Clock tick render | Every 60s | Re-render so time stays current; ETag changes only if content differs |

---

## 9. Management UI — Admin Panel

The admin panel is served by the same FastAPI process on `/admin` routes. No authentication required (local network only). Uses Jinja2 HTML templates.

### 9.1 Quote Manager (`/admin/quotes`)

Manages the quote library stored in `server/data/quotes.json`.

**Features:**
- **Upload file:** replace the entire `quotes.json` with a prepared file
- **List view:** table showing all quotes with index, text snippet, and attribution
- **Add / Edit / Delete** individual quotes
- **Today's quote:** shows which quote is currently selected and total count

### 9.2 Stock & Watchlist Manager (`/admin/stocks`)

Manages `tickers.json` and `portfolio.json`. Stock prices are fetched and P&L is computed; the stocks zone is not rendered on screen in the current version but all data is accessible here.

**Watchlist section:** list current tickers with delete; add new ticker.

**Portfolio section:** table with Ticker | Shares | Avg Cost | Notes | Current Price | Value | P&L | P&L%. Add, edit, delete holdings.

### 9.3 Screensaver Management

Screensavers are managed via the file system, not the admin panel. Use `tools/screensaver_converter.py` to prepare images (see §12.3), then save the output PNGs to `server/data/screensavers/` with sequential numeric names (`0.png`, `1.png`, …).

The admin panel home at `/admin` shows `source_statuses` including moon phase status.

---

## 10. Configuration & Secrets Management

### 10.1 Server `.env` file

Location: `server/secrets/.env`

```env
# ── Google Calendar ──────────────────────────────────────────
GOOGLE_CREDENTIALS_PATH=secrets/credentials.json
GOOGLE_TOKEN_PATH=secrets/token.json
GOOGLE_CALENDAR_ID=primary

# ── Stocks ───────────────────────────────────────────────────
STOCK_PROVIDER=polygon
STOCK_API_KEY=<!-- your Polygon.io API key -->
STOCK_FETCH_TIMES=05:45,17:30

# ── Weather ──────────────────────────────────────────────────
WEATHER_LAT=32.08
WEATHER_LON=34.78
WEATHER_TIMEZONE=Asia/Jerusalem
WEATHER_UNITS=celsius

# ── Server ───────────────────────────────────────────────────
SERVER_HOST=0.0.0.0
SERVER_PORT=8080
```

### 10.2 User-Editable Data Files

| File | Content | Managed via |
|---|---|---|
| `data/quotes.json` | Quote library | Admin panel → Quotes |
| `data/tickers.json` | Stock watchlist | Admin panel → Stocks |
| `data/portfolio.json` | Holdings with shares + avg cost | Admin panel → Stocks |
| `data/screensavers/*.png` | Screensaver images | `tools/screensaver_converter.py` |

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
| **Google Calendar API error** | Reminder zone hidden silently |
| **Polygon.io error / rate limit** | Last known price with `[delayed]` label |
| **Weather API error** | Weather zone shows `—` |
| **Moon phase error** | Moon disc not drawn; rest of weather zone unaffected |
| **Server render exception** | Log traceback. Serve last cached PNG. Server stays up |
| **PNG decode error (E1001)** | Skip update, log, sleep normally |
| **No screensavers available** | Screensaver step silently skipped; device sleeps normally |
| **Screensaver 404 (E1001)** | Screensaver step skipped; device sleeps normally |
| **`quotes.json` missing or empty** | Display hardcoded default: `"Not all those who wander are lost." — J.R.R. Tolkien` |
| **`portfolio.json` missing or empty** | Portfolio P&L hidden in admin panel; stocks line 2 blank |

---

## 12. Development Toolchain

### 12.1 Server Side (Arch Linux)

| Tool | Purpose | Install |
|---|---|---|
| Python 3.12+ | Runtime | `sudo pacman -S python` |
| venv | Isolation | `python -m venv .venv` |
| FastAPI + Uvicorn | HTTP server + admin routes | `pip install fastapi uvicorn` |
| Jinja2 | Admin panel templates | `pip install jinja2` |
| Pillow | Image rendering | `pip install pillow` |
| httpx | Async HTTP client | `pip install httpx` |
| APScheduler | Task scheduling | `pip install apscheduler` |
| google-api-python-client | Calendar SDK | `pip install google-api-python-client google-auth-oauthlib` |
| python-dotenv | `.env` loading | `pip install python-dotenv` |
| ephem | Moon phase calculation | `pip install ephem` |
| tkinter | Screensaver converter GUI | `sudo pacman -S tk` |

**Full install:**
```bash
source .venv/bin/activate
pip install fastapi uvicorn jinja2 pillow httpx apscheduler \
            google-api-python-client google-auth-oauthlib python-dotenv ephem
```

### 12.2 E1001 Firmware Side (Arch Linux)

| Tool | Purpose | Install |
|---|---|---|
| west | Zephyr build tool | `pip install west --break-system-packages` |
| Zephyr SDK | Xtensa toolchain | `west sdk install` |
| CMake + Ninja | Build system | `sudo pacman -S cmake ninja` |
| esptool | Flashing | `pip install esptool --break-system-packages` |

```bash
west init ~/zephyrproject
cd ~/zephyrproject && west update && west zephyr-export
west sdk install

cd ~/Documents/reTerminal/firmware
west build -b reterminal_e1001/esp32s3/procpu .
west flash --runner esptool
```

**Serial monitor:**
```bash
west espressif monitor
# or: screen /dev/ttyUSB0 115200
```

### 12.3 Screensaver Converter (`tools/screensaver_converter.py`)

A Tkinter GUI tool to convert photos into screensaver PNGs for the ePaper display.

**Workflow:**
1. `python tools/screensaver_converter.py`
2. Pick a photo (JPEG or PNG)
3. Position the 5:3 crop box over the desired area (click or drag)
4. Preview the 4-level grayscale result
5. Save to `server/data/screensavers/` with a sequential numeric name

**Processing:**
- Crops to a 5:3 region (matching the 800×480 panel ratio)
- Resizes to 800×480 with LANCZOS
- Converts to 4-level grayscale (`L=0, 85, 170, 255`) with Floyd-Steinberg dithering

### 12.4 Claude Code

```bash
cd ~/Documents/reTerminal
claude
```

---

## 13. Open Questions & Decisions Pending

| # | Question | Options / Notes | Decision |
|---|---|---|---|
| 1 | Tickers to track | Add via admin panel at `/admin/stocks` | <!-- TBD --> |
| 2 | Portfolio holdings | Enter via admin panel — shares, avg cost, notes | <!-- TBD --> |
| 3 | Quotes file preparation | Use claude.ai to generate 100+ quotes, upload via admin panel | <!-- TBD --> |
| 4 | Green button refresh | ✅ Resolved — full refresh on button press | ✅ Done |
| 5 | Birthday zone if empty | Clock+date expand to fill space — confirmed in code | ✅ Done |
| 6 | Admin panel auth | Currently none (LAN only). Add basic password if desired in future. | <!-- TBD --> |
| 7 | Claude API / AI features | Deferred — re-enable when API key available | <!-- Future v2 --> |
| 8 | Stocks zone on screen | Currently off-screen (y=480). Render in a future version when layout is revised. | <!-- Future v2 --> |
| 9 | Multiple display buffers | Server-side `/display/{n}` only implements n=0. Add additional buffer modes. | <!-- Future --> |
| 10 | Screensaver admin UI | Currently managed via file system + converter tool. Add admin panel UI? | <!-- TBD --> |

---

## Appendix A — Change Log

| Version | Date | Changes |
|---|---|---|
| 0.1 | 01.05.2026 | Initial draft |
| 0.2 | 01.05.2026 | Quote of the day; network topology; NSSM; grid redesign; weather fields; NTP; ESP-IDF firmware; Wi-Fi error screen; stock schedule |
| 0.3 | 01.05.2026 | Venv path; full Windows paths; Ethernet static IP setup; layout redesign; quote sources expanded to literature; firmware to Zephyr; green button resolved |
| 0.4 | 01.05.2026 | DHCP reservation confirmed; laptop IP/hostname/gateway filled in; quote strip h=70; clock enlarged (110pt); birthday zone reduced; footer added (70px); wind speed removed; stocks zone 90px; portfolio.json added; tickers.json admin-managed; Claude prompt files added; admin panel §9 with portfolio AI query; jinja2 added |
| 0.5 | 01.05.2026 | Claude API removed; quotes use local date-seeded JSON; stock AI replaced with P&L summary; footer removed; clock zone enlarged; date in clock zone; Montserrat font; partial refresh region updated; Wi-Fi retry count set to 5; admin panel updated to file-upload workflow; `/api/quotes/upload` added; fallback quote added |
| 0.6 | 09.05.2026 | Server moved to Arch Linux (removed Windows/NSSM); layout zones updated (quote h=90, weather w=200 y=90, clock x=200 w=600 h=260, reminders y=350 h=130); stocks zone removed from screen (data still fetched); rendering changed to PIL "L" grayscale mode with 2× supersampling + LANCZOS; font sizes updated throughout; moon phase feature added (ephem, disc in weather column); screensaver feature added (inactive timer wakes → random screensaver from data/screensavers/); display buffer rotation added (left/right buttons, buf_index in NVS); maintenance refreshes (08:00 / 12:00 / 22:00) removed; NTP sync moved to single daily 00:00 wakeup with no screen refresh; MODE_SCHEDULED_REFRESH replaced by MODE_NTP_SYNC_ONLY; schedule events reduced to {00:00, 05:45, 18:00}; state.py module added; /screensaver/* and /api/refetch/* endpoints added; tools/screensaver_converter.py added; time rendered as now+1min |

---

*All `<!-- TBD -->` markers indicate decisions still pending.*
