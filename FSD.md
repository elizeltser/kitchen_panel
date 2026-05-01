# Functional Specification: ePaper Smart Dashboard System
**Document version:** 0.3 — Draft  
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
13. [Appendix A — Change Log](#appendix-a--change-log)

---

## 1. Project Overview

### 1.1 Purpose

This document describes the functional requirements of a personal smart dashboard built on a **Seeed Studio reTerminal E1001** 7.5-inch monochrome ePaper display. The system provides an at-a-glance view of time, weather, birthday reminders, stock portfolio status, and a daily quote during defined morning and evening periods. An AI layer provides lightweight buy/hold/sell suggestions for tracked stocks and generates the daily quote.

### 1.2 Goals

- Display accurate, readable information during two daily active windows without requiring interaction.
- Maximize screen lifespan by minimizing unnecessary full refreshes and enforcing a safe refresh schedule.
- Keep the display firmware simple and stateless — all intelligence and rendering lives on the laptop server.
- Enable future extension of data sources and layout without reflashing the device.
- Provide a pleasant shared morning and evening experience for the household.

### 1.3 Non-Goals

- This is not a real-time trading terminal. Stock suggestions are informational only.
- The display is not interactive beyond the green button manual refresh (v1).
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
│    Windows 10 — Ethernet connected │
│                                    │
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
                          │  Home network (router)
                          │  Laptop: Ethernet (static IP)
                          │  E1001:  Wi-Fi 2.4GHz
                          │  HTTP GET /display.png
                          ▼
              ┌───────────────────────┐
              │   reTerminal E1001    │
              │   Zephyr RTOS firmware│
              │                       │
              │  Poll → ETag check    │
              │  Decode PNG           │
              │  Refresh ePaper       │
              │  System off (PM)      │
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
  1. Wake from system-off / power-managed sleep
  2. Connect to Wi-Fi
  3. GET /display.png with If-None-Match: <last_etag>
  4. If 304 Not Modified → skip display update, go back to sleep
  5. If 200 OK → decode PNG, choose refresh mode, update display
  6. Store new ETag in retained RAM or settings subsystem
  7. Compute sleep duration until next poll
  8. Enter power-managed sleep
```

### 2.3 Network Topology

The laptop connects to the home router via **Ethernet** (Wi-Fi adapter is non-functional) with a **static IP**. The E1001 connects via **2.4GHz Wi-Fi** (ESP32-S3 does not support 5GHz). Both are on the same home LAN subnet. The server is **not exposed to the internet**.

#### Setting a Static IP on Windows 10 (Ethernet)

A static IP ensures the E1001 always knows where to find the server, even after reboots.

```powershell
# Step 1: Find the name of your Ethernet adapter
ipconfig

# Look for "Ethernet adapter Ethernet" or similar.
# Note the current IPv4 address (e.g. 192.168.1.XX) and Default Gateway.
# Your static IP should be in the same range but outside the router's DHCP pool
# (e.g. if DHCP gives out .100–.200, choose .50).
```

**Via Windows Settings (GUI — recommended):**
1. Open **Settings → Network & Internet → Ethernet → Change adapter options**
2. Right-click your Ethernet adapter → **Properties**
3. Select **Internet Protocol Version 4 (TCP/IPv4)** → **Properties**
4. Select **Use the following IP address** and fill in:
   - IP address: `<!-- e.g. 192.168.1.50 -->` ← choose a free address in your subnet
   - Subnet mask: `255.255.255.0`
   - Default gateway: `<!-- your router's IP, e.g. 192.168.1.1 -->`
   - Preferred DNS: `8.8.8.8`
   - Alternate DNS: `8.8.4.4`
5. Click OK → Close

**Verify it worked:**
```powershell
ipconfig
# Should now show your chosen static IP under Ethernet adapter
ping 8.8.8.8
# Should succeed — confirms internet still works
```

**How to find your router's IP (gateway):**
```powershell
ipconfig | findstr "Default Gateway"
```

**How to check the server is NOT exposed externally:**
```powershell
# After starting the server, check what's listening on port 8080.
# It should bind to 0.0.0.0:8080 — this means all local interfaces,
# but only reachable within your LAN, NOT from the internet,
# as long as your router has no port-forwarding rule for 8080.
netstat -an | findstr "8080"

# Your public IP (do NOT see this in the netstat output above):
# Visit https://whatismyip.com — it will be different from your LAN IP.
```

Your router's firewall blocks inbound connections by default. As long as you have not added a port-forwarding rule for port 8080, the server is safely internal.

| Item | Value |
|---|---|
| Network type | Home LAN — Laptop via Ethernet, E1001 via 2.4GHz Wi-Fi |
| Laptop connection | Ethernet |
| Laptop static IP | <!-- Fill in after running ipconfig — e.g. 192.168.1.50 --> |
| Laptop hostname | <!-- Run `hostname` in PowerShell — e.g. ASUS-HOME --> |
| Server port | `8080` |
| Router gateway | <!-- e.g. 192.168.1.1 — run `ipconfig \| findstr "Default Gateway"` --> |
| E1001 IP assignment | <!-- Recommend DHCP reservation in router for E1001's MAC address --> |
| External exposure | None — no port forwarding, default router firewall applies |

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

**Refresh modes available:**

| Mode | Duration | Flicker | Use case |
|---|---|---|---|
| Full refresh | ~3s | Multiple flashes | Window start, daily maintenance, button press |
| Fast refresh | ~1.5s | Single flash | Content change (stocks, calendar) |
| Partial refresh | ~0.3s | None | Clock tick (time region only) |

### 3.2 Server — Laptop Backend

**FastAPI + Uvicorn** is used because:
- Native async support lets all external API calls (Google Calendar, Polygon, Open-Meteo, Claude) run concurrently — fast render cycles.
- Built-in support for ETag / `304 Not Modified` HTTP headers, central to this project's efficiency.
- Lightweight — no database, no ORM overhead.
- Runs cleanly inside a Python virtual environment.

**Process management on Windows 10:**
The server runs as a **Windows Service** via NSSM (Non-Sucking Service Manager), ensuring it starts on boot and restarts on crash. During development, run manually from the activated virtual environment.

**Virtual environment location:** `C:\Users\Eli Zeltser\Documents\reTerminal\.venv`

All Python commands below assume this venv is activated:
```powershell
# Activate venv (run this first in any new terminal session)
C:\Users\Eli Zeltser\Documents\reTerminal\.venv\Scripts\Activate.ps1

# If execution policy blocks this, run once as Administrator:
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
```

**NSSM service setup** (run once after development is complete):
```powershell
# Download NSSM from https://nssm.cc/download, place nssm.exe somewhere accessible.
# Open PowerShell as Administrator:

$venvPython = "C:\Users\Eli Zeltser\Documents\reTerminal\.venv\Scripts\python.exe"
$appDir     = "C:\Users\Eli Zeltser\Documents\reTerminal\server"

nssm install EpaperDashboard $venvPython "-m uvicorn main:app --host 0.0.0.0 --port 8080"
nssm set EpaperDashboard AppDirectory $appDir
nssm set EpaperDashboard Start SERVICE_AUTO_START
nssm start EpaperDashboard

# Check status / stop / remove:
nssm status EpaperDashboard
nssm stop   EpaperDashboard
nssm remove EpaperDashboard confirm
```

**Development run** (from activated venv):
```powershell
cd "C:\Users\Eli Zeltser\Documents\reTerminal\server"
python -m uvicorn main:app --host 0.0.0.0 --port 8080 --reload
```

| Property | Value |
|---|---|
| Hardware | Asus A554I |
| OS | Windows 10 |
| Python version | 3.12 |
| Always-on | Yes (plugged in, screen can sleep) |
| Project root | `C:\Users\Eli Zeltser\Documents\reTerminal\` |
| Virtual environment | `C:\Users\Eli Zeltser\Documents\reTerminal\.venv` |
| Server directory | `C:\Users\Eli Zeltser\Documents\reTerminal\server\` |
| Server framework | FastAPI + Uvicorn |
| Image rendering | Pillow (PIL) |
| Process management | NSSM Windows Service (production) / manual venv run (development) |

### 3.3 Data Sources

| Source | Provider | Auth method | Update frequency |
|---|---|---|---|
| Calendar (birthdays) | Google Calendar API v3 | OAuth2 (offline token) | Once daily at 05:45, re-fetched at 18:00 |
| Stock prices | Polygon.io | API key | 05:45 and 17:30 daily (see §6.2) |
| Stock suggestions | Claude API (Haiku) | API key | On price change > threshold |
| Weather | Open-Meteo (free, no key) | None | Once daily at 05:45 |
| Quote of the day | Claude API (Haiku) | API key | Once daily at 05:45 |
| Time / Date | System clock on E1001 (NTP-synced) | N/A | Every render cycle |

### 3.4 Communication Layer

- **Protocol:** HTTP/1.1 over home LAN
- **Image format:** PNG, 1-bit (black/white), 800×480
- **Cache control:** ETag-based — E1001 sends `If-None-Match` header; server returns `304` if unchanged
- **Security:** Local network only. No TLS in v1. Server not exposed externally.

---

## 4. Display Layout & Visual Design

### 4.1 Grid Structure

The 800×480 canvas is divided into four zones. The quote occupies the top strip with no label. Below it, weather sits left, clock and birthday reminders are stacked center, and the stocks bar anchors the bottom.

```
┌──────────────────────────────────────────────────────────────────┐ y=0
│  "The mystery of life isn't a problem to solve..."               │ h=50
│                                         — Frank Herbert, Dune    │
├───────────────────┬──────────────────────────────────────────────┤ y=50
│                   │                                              │
│  ⛅               │              07:42                           │
│  Partly Cloudy    │                                              │ h=270
│                   │──────────────────────────────────────────────│
│  ↑24°  ↓14°       │  🎂 Mum         🎂 Dan                       │
│  Rain 40%         │                                              │
│  Wind 18 km/h     │                                              │
│                   │                                              │
├───────────────────┴──────────────────────────────────────────────┤ y=320
│  AAPL $189  ▲1.2%   TSLA $172  ▼0.8%   SPY $524  ▲0.4%          │ h=80
│  Hold AAPL — momentum positive but watch resistance at $192      │
├──────────────────────────────────────────────────────────────────┤ y=400
│  Wednesday, 29 April 2026                              18:04     │
└──────────────────────────────────────────────────────────────────┘ y=480
```

**Notes on the layout:**
- No zone labels are shown on screen (no "WEATHER", "CLOCK" headings).
- The quote strip at top has no title — text begins immediately.
- Clock and birthday reminders share the right panel, stacked vertically: clock fills the upper portion, birthdays (if any) appear below a thin separator line.
- If there are no birthdays today, the right panel shows only the clock, vertically centered.
- The date and time at the very bottom serve as a persistent footer — date left-aligned, current time right-aligned in a smaller size.

**Zone definitions:**

| Zone | x | y | w | h | Content |
|---|---|---|---|---|---|
| Quote | 0 | 0 | 800 | 50 | Quote text (left) + attribution (right-aligned) |
| Weather | 0 | 50 | 240 | 270 | Icon + description + ↑max ↓min + rain% + wind |
| Clock | 240 | 50 | 560 | 160 | Large time `H:MM`, vertically centered |
| Birthdays | 240 | 210 | 560 | 110 | 🎂 Birthday names, one per entry, small font |
| Stocks | 0 | 320 | 800 | 80 | Tickers + AI suggestion line |
| Footer | 0 | 400 | 800 | 80 | Date (left) + current time (right), smaller font |

**Dividers:**
- Horizontal line at y=50 (full width)
- Horizontal line at y=210 (x=240 to x=800 only — separates clock from birthdays)
- Horizontal line at y=320 (full width)
- Horizontal line at y=400 (full width)
- Vertical line at x=240, from y=50 to y=320

### 4.2 Partial Refresh Region

On each minute-tick, only the clock sub-region and the footer time are redrawn. This avoids touching the weather, quote, or stocks zones unnecessarily.

| Region | x | y | w | h |
|---|---|---|---|---|
| Clock | 240 | 50 | 560 | 160 |
| Footer time | 600 | 405 | 195 | 40 |

### 4.3 Typography

All fonts are loaded server-side as TTF files. No constraints from the ESP32 apply.

| Element | Font | Size | Style | Notes |
|---|---|---|---|---|
| Clock (H:MM) | Inter | 96pt | Bold | Tabular figures (`tnum`) — digits don't shift width |
| Weather icon | NerdFontsSymbolsOnly | 52pt | Regular | Single glyph from WMO code map |
| Weather description | Inter | 17pt | Regular | e.g. "Partly Cloudy" |
| Weather temp (max/min) | Inter | 20pt | Bold | `↑24°  ↓14°` on one line |
| Weather rain / wind | Inter | 16pt | Regular | `Rain 40%   Wind 18 km/h` |
| Birthday entries | Inter | 20pt | Medium | `🎂 Name` — up to 3 per line, wraps if needed |
| Stock ticker | JetBrains Mono | 18pt | Bold | |
| Stock price / change | JetBrains Mono | 17pt | Regular | `▲` / `▼` prefix |
| AI suggestion | Inter | 14pt | Italic | Single line, truncated to fit |
| Quote text | Playfair Display | 16pt | Italic | Left portion of quote strip |
| Quote attribution | Inter | 13pt | Regular | Right-aligned, `— Name, Source` |
| Footer date | Inter | 16pt | Regular | Left-aligned |
| Footer time | Inter | 16pt | Regular | Right-aligned |

**Font files to download** (all free):
- Inter: [fonts.google.com/specimen/Inter](https://fonts.google.com/specimen/Inter) — download `Inter-Bold.ttf`, `Inter-Medium.ttf`, `Inter-Regular.ttf`
- JetBrains Mono: [fonts.google.com/specimen/JetBrains+Mono](https://fonts.google.com/specimen/JetBrains+Mono) — download `JetBrainsMono-Bold.ttf`, `JetBrainsMono-Regular.ttf`
- Playfair Display: [fonts.google.com/specimen/Playfair+Display](https://fonts.google.com/specimen/Playfair+Display) — download `PlayfairDisplay-Italic.ttf`
- Nerd Fonts: [github.com/ryanoasis/nerd-fonts/releases](https://github.com/ryanoasis/nerd-fonts/releases) — download `NerdFontsSymbolsOnly.zip`, extract `NerdFontsSymbolsOnly-Regular.ttf`

Place all files in `C:\Users\Eli Zeltser\Documents\reTerminal\server\fonts\`.

### 4.4 Visual Style Rules

- Background: white (pixel `1`)
- Foreground: black (pixel `0`)
- Zone dividers: single-pixel black lines
- 1-bit output: Pillow renders with Floyd-Steinberg dithering for smooth text edges
- Stock negative change: `▼` prefix
- Stock positive change: `▲` prefix
- Weather icons: Nerd Font glyphs mapped from WMO weather code (see §6.3)

---

## 5. Update Schedule & Screen Management

### 5.1 Active Windows

| Window | Start | End | Behaviour |
|---|---|---|---|
| Morning | 05:45 | 08:00 | Full refresh on wake, then partial clock tick every minute |
| Evening | 18:00 | 22:00 | Full refresh on wake, then partial clock tick every minute |

Times are local time on the E1001 (Asia/Jerusalem), synced via NTP at each boot.

### 5.2 Maintenance Refresh

| Event | Time | Refresh type |
|---|---|---|
| End of morning window | 08:00 | Full refresh → sleep |
| Midday maintenance | 12:00 | Full refresh → immediately back to sleep |
| End of evening window | 22:00 | Full refresh → sleep |

### 5.3 Refresh Decision Logic

```
On each wake:
  IF green button was pressed (wakeup reason = GPIO):
    → force full refresh regardless of ETag
    → fetch new image unconditionally
    → reset to ACTIVE_WINDOW loop if within window, else sleep

  IF scheduled full refresh time (08:00 / 12:00 / 22:00):
    → full refresh → sleep

  ELSE IF in active window:
    → GET /display.png with If-None-Match: <stored_etag>
    IF 200 OK and ETag changed:
      IF only clock/footer regions changed → partial refresh
      ELSE → fast refresh (full screen, single flash)
    IF 304 Not Modified → skip display update

  ELSE:
    → skip fetch, sleep until next window

After every 5 consecutive partial/fast refreshes:
  → force one full refresh → reset counter
```

### 5.4 Sleep Duration Calculation

- **During active window:** 60 seconds
- **End of active window / inactive period:** sleep until next scheduled event
- **On Wi-Fi failure:** sleep 5 minutes, then retry

### 5.5 NTP Time Synchronisation

The E1001 synchronises its clock via NTP (`il.pool.ntp.org`) at every boot. This prevents RTC drift accumulating over the maximum ~10-hour overnight sleep (22:00 → 05:45). If NTP fails, the firmware falls back to the RTC and logs a warning. Time self-corrects on the next successful sync.

---

## 6. Data Integration Specifications

### 6.1 Google Calendar

**Purpose:** Display today's birthday reminders below the clock.

**Authentication:**
- OAuth2 with offline refresh token
- Credentials in `server\secrets\credentials.json` (generated once via CLI flow)
- Scopes: `https://www.googleapis.com/auth/calendar.readonly`

**Query:**
- Calendar: Birthdays (from primary Google account)
- Time range: Start of today → 23:59 local time
- Only all-day events (birthdays have no time component)
- Max results: 6

**Display rules:**
- Show birthday names only — no time, no date (it's today by definition)
- Format: `🎂 Name` — comma-separated if multiple fit on one line, or wrap to next line
- If no birthdays today: birthday sub-zone is hidden; clock expands to fill full right panel (y=50 to y=320)
- If Calendar API unavailable: hide birthday zone silently (no error text shown here)

**Update schedule:** 05:45 and 18:00.

---

### 6.2 Stock Data & AI Suggestions

**Purpose:** Show price and daily change for a personal watchlist. Provide a one-line AI note.

**Tickers to track:**

> **[ EDIT ]** List your tickers:
> - `<!-- e.g. AAPL -->`
> - `<!-- e.g. TSLA -->`
> - `<!-- e.g. SPY -->`

**Data provider:** Polygon.io

**Update schedule rationale:**

| Fetch time (Israel) | US market state | What is shown |
|---|---|---|
| **05:45** | Pre-market / closed | Previous day closing prices (`[prev. close]` label) |
| **17:30** | Market open (~1 hr in) | Live intraday price, no label |

**AI suggestions:**

| Property | Value |
|---|---|
| Model | `claude-haiku-4-5-20251001` |
| Max tokens | 60 |
| Trigger | Price change > 1.5% since last suggestion, or at each scheduled fetch |
| Prompt | `"[TICKER] at $[PRICE], [CHANGE]% today. One-sentence buy/hold/sell suggestion."` |
| Display | Single italic line in stocks zone |
| Cooldown | 30 min per ticker |

> Stock suggestions are informational only — not financial advice.

---

### 6.3 Weather

**Provider:** Open-Meteo — free, no API key.

**Location:** Bat Yam, Israel — Lat `32.08`, Lon `34.78`, TZ `Asia/Jerusalem`

**Fields fetched:**

| Field | API parameter | Display |
|---|---|---|
| Current temperature | `current=temperature_2m` | Shown in icon area as `Now 19°` |
| Today max temp | `daily=temperature_2m_max` | `↑ 24°C` |
| Today min temp | `daily=temperature_2m_min` | `↓ 14°C` |
| Weather condition | `current=weather_code` | Icon glyph + description text |
| Precipitation probability | `daily=precipitation_probability_max` | `Rain 40%` |
| Wind speed | `current=wind_speed_10m` | `Wind 18 km/h` |

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
| Windy (wind > 40 km/h) | any | `nf-md-weather_windy` |

**Update frequency:** Once at 05:45. Cached all day (daily min/max do not change).

**Units:** Celsius, km/h.

---

### 6.4 Clock & Date

| Element | Format | Example | Location |
|---|---|---|---|
| Time (main) | `H:MM` (24h, no leading zero) | `7:42` | Clock zone (large, center) |
| Time (footer) | `H:MM` | `7:42` | Footer, right-aligned |
| Date (footer) | `Day, DD Month YYYY` | `Wednesday, 29 April 2026` | Footer, left-aligned |

**NTP server:** `il.pool.ntp.org`
**Timezone:** `Asia/Jerusalem`
**POSIX TZ string:** `IST-2IDT,M3.4.4/26,M10.5.0`

---

### 6.5 Quote of the Day

**Purpose:** Display a short, thought-provoking or beautiful quote at the top of the screen. Quotes may come from historical figures, scientists, artists, thinkers, or from literature — including novels such as Dune, The Alchemist, or historical works.

**Generation:** Claude API call once at 05:45, cached all day.

| Property | Value |
|---|---|
| Model | `claude-haiku-4-5-20251001` |
| Max tokens | 100 |
| Max displayed characters | 120 (regenerate once if exceeded) |
| Cache | In-memory, regenerated each morning |
| Fallback | Random entry from `server\quotes_fallback.json` |

**Prompt template:**
```
Give me one short, memorable quote from a well-known person or from a 
famous literary work (for example: Dune by Frank Herbert, The Alchemist 
by Paulo Coelho, a history book, a scientist, artist, or philosopher). 
The quote should be beautiful, thought-provoking, or uplifting.
Reply ONLY in this exact format — no other text:
"Quote text here." — Attribution (e.g. Name, or Name, Book Title)
```

**Display:**
- Quote text: Playfair Display Italic, left-aligned in quote strip
- Attribution: Inter Regular, right-aligned, prefixed `—`
- No zone title or border label

**Fallback list** (stored in `server\quotes_fallback.json`):
> **[ EDIT ]** Populate with 10 favourite quotes. Example entries:
> ```json
> [
>   {"quote": "The mystery of life isn't a problem to solve, but a reality to experience.", "attribution": "Frank Herbert, Dune"},
>   {"quote": "When you want something, all the universe conspires in helping you to achieve it.", "attribution": "Paulo Coelho, The Alchemist"}
> ]
> ```

---

## 7. Firmware Specification (E1001)

### 7.1 Framework Decision: Zephyr RTOS

**Zephyr is chosen over ESP-IDF for this project.** Here is the reasoning:

| Criterion | Zephyr | ESP-IDF |
|---|---|---|
| E1001 board support | ✅ Official board definition (`reterminal_e1001`) in mainline Zephyr | ✅ Supported |
| UC8179 ePaper driver | ✅ Already exists — used by the ZEReader open-source project for the exact same GDEY075T7 panel | ⚠️ Must be written from scratch or ported |
| PNG decoding | ✅ `pngle` integrates cleanly | ✅ Same |
| HTTP client | ✅ `net/http_client` Zephyr subsystem | ✅ `esp_http_client` |
| Wi-Fi | ✅ ESP32 Wi-Fi via `esp_wifi` HAL (same underlying driver) | ✅ Native |
| Deep sleep / PM | ✅ Zephyr Power Management subsystem (`pm_state_force`) | ✅ `esp_deep_sleep_start()` |
| Build system | `west` (CMake-based, simpler) | `idf.py` (CMake-based) |
| GPIO button interrupt | ✅ `gpio_pin_interrupt_configure` | ✅ GPIO ISR |
| Learning curve | Lower for someone new — cleaner HAL abstractions | Higher — more boilerplate |
| Hassle factor | **Lower** — UC8179 driver already exists for this exact panel | **Higher** — driver work required |

**The UC8179 driver existence in Zephyr is the deciding factor.** The ZEReader project (an open-source e-reader built on Zephyr) uses the exact same GDEY075T7 display with the UC8179 controller and Zephyr's `display` subsystem. This existing code can be referenced or adapted directly, eliminating the highest-risk part of the firmware.

### 7.2 Libraries & Components

| Item | Zephyr component | Notes |
|---|---|---|
| RTOS | Zephyr v3.7+ | Use `reterminal_e1001` board target |
| Build system | `west` | Zephyr's meta-tool (wraps CMake) |
| ePaper display | `drivers/display` subsystem + UC8179 driver | Reference: ZEReader project on GitHub |
| PNG decoder | `pngle` (vendored C library) | Tiny, RAM-friendly, works in Zephyr |
| HTTP client | `net/http_client` Zephyr subsystem | Supports custom request headers (ETag) |
| Wi-Fi | `esp_wifi` via Zephyr HAL | Standard for ESP32 targets |
| NTP / SNTP | `net/sntp` Zephyr subsystem | `sntp_simple()` call |
| Settings / storage | `settings` subsystem (NVS backend) | Stores ETag, refresh counter |
| Power management | `pm` subsystem (`pm_state_force(PM_STATE_SOFT_OFF)`) | Controls sleep states |
| GPIO (button) | `gpio` driver + `gpio_pin_interrupt_configure()` | Green button wakeup |
| Real-time clock | `rtc` driver (onboard RTC) | Fallback if NTP fails |

### 7.3 Firmware State Machine

```
BOOT (from sleep or power-on)
  │
  ├─► Init: display, SPI, GPIO, settings/NVS
  │
  ├─► Check wakeup reason:
  │     GPIO (green button) → set FORCE_REFRESH flag
  │     Timer / normal boot → continue
  │
  ├─► Connect Wi-Fi (timeout 15s, retry 3×)
  │     FAILURE:
  │       → render Wi-Fi error screen on ePaper (see §7.4)
  │       → sleep 5 minutes → reboot
  │
  ├─► SNTP sync (il.pool.ntp.org)
  │     FAILURE → use RTC, log warning
  │
  ├─► Determine mode from current local time:
  │
  │   SCHEDULED_FULL_REFRESH (08:00 / 12:00 / 22:00):
  │     → full ePaper refresh (clear to white)
  │     → sleep until next event
  │
  │   ACTIVE_WINDOW (05:45–08:00 or 18:00–22:00):
  │     IF FORCE_REFRESH flag:
  │       → GET /display.png unconditionally (no ETag header)
  │       → full refresh on display
  │       → clear FORCE_REFRESH
  │     ELSE:
  │       → GET /display.png with If-None-Match: <settings:last_etag>
  │       200 OK → decode PNG → select refresh mode → update display
  │                → save new ETag to settings
  │       304 Not Modified → skip display update
  │       Error → skip update, log to console
  │     → sleep 60 seconds (Zephyr PM soft-off, timer wakeup)
  │
  │   INACTIVE:
  │     → compute sleep duration until next window or maintenance
  │     → sleep (PM soft-off)
  │
  └─► Enter sleep
```

### 7.4 Wi-Fi Error Screen

If Wi-Fi fails after retries, display this before sleeping:

```
┌──────────────────────────────────────────────────────────┐
│                                                          │
│   Wi-Fi connection failed                                │
│                                                          │
│   Could not connect to: <SSID>                           │
│   Retrying in 5 minutes.                                 │
│                                                          │
│   Check network availability and firmware config.h       │
│                                                          │
└──────────────────────────────────────────────────────────┘
```

Rendered using the UC8179 driver's built-in text rendering directly — no server PNG required.

### 7.5 Green Button — Manual Full Refresh

The green button (GPIO3) is configured as a wakeup source. When pressed during sleep, it triggers a boot with `FORCE_REFRESH` flag set, causing an unconditional full refresh of the display regardless of ETag. This is useful for:
- Testing after firmware changes
- Forcing a display update after a long inactive period
- Manually recovering from a ghosted screen

```
Button press during sleep:
  → Zephyr GPIO wakeup → boot → FORCE_REFRESH = true
  → fetch /display.png (no If-None-Match header)
  → full refresh
  → continue normal active/inactive loop
```

### 7.6 Build Configuration

**Project structure:**
```
firmware/
├── west.yml                  # Zephyr manifest (pins Zephyr version)
├── CMakeLists.txt            # Top-level CMake
├── prj.conf                  # Zephyr Kconfig options
├── app.overlay               # Device tree overlay (board customisation)
├── src/
│   ├── main.c                # App entry point, state machine
│   ├── wifi.c / wifi.h       # Wi-Fi connect/disconnect
│   ├── ntp.c / ntp.h         # SNTP sync
│   ├── http_fetch.c          # PNG fetch with ETag support
│   ├── png_decode.c          # pngle integration
│   ├── epaper.c / epaper.h   # UC8179 display control (full/fast/partial)
│   ├── schedule.c            # Active window + sleep time calculation
│   ├── button.c              # Green button GPIO wakeup
│   └── config.h              # User configuration
├── lib/
│   └── pngle/                # Vendored PNG decoder
└── CLAUDE.md                 # Firmware project context for Claude Code
```

**`prj.conf` (Kconfig):**
```
# Networking
CONFIG_NETWORKING=y
CONFIG_NET_IPV4=y
CONFIG_NET_TCP=y
CONFIG_DNS_RESOLVER=y
CONFIG_NET_SOCKETS=y
CONFIG_HTTP_CLIENT=y
CONFIG_SNTP=y

# Wi-Fi (ESP32)
CONFIG_WIFI=y
CONFIG_ESP32_WIFI=y

# Display
CONFIG_DISPLAY=y

# Settings (NVS backend for ETag storage)
CONFIG_SETTINGS=y
CONFIG_SETTINGS_NVS=y
CONFIG_NVS=y
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y

# Power Management
CONFIG_PM=y
CONFIG_PM_DEVICE=y

# GPIO
CONFIG_GPIO=y

# RTC
CONFIG_RTC=y

# Logging
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=3

# Stack sizes (HTTP + PNG decode need headroom)
CONFIG_MAIN_STACK_SIZE=8192
CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=4096
```

**Build and flash commands (from Arch Linux):**
```bash
# Install west (Zephyr's build tool) and SDK
pip install west --break-system-packages
west init ~/zephyrproject
cd ~/zephyrproject
west update
west zephyr-export

# Install Zephyr SDK (toolchain)
# Download from https://github.com/zephyrproject-rtos/sdk-ng/releases
# Choose zephyr-sdk-0.17.x-linux-x86_64.tar.xz
cd ~/zephyrproject
west sdk install

# Clone and build the firmware
cd ~/Documents/reTerminal/firmware
west build -b reterminal_e1001 .

# Flash via USB
west flash --runner esptool
# or:
esptool.py -c esp32s3 -p /dev/ttyUSB0 write_flash 0x0 build/zephyr/zephyr.bin
```

### 7.7 Configuration (`src/config.h`)

```c
// ============================================================
// [ EDIT ] — fill in before building
// ============================================================

#define WIFI_SSID           "Eli"
#define WIFI_PASSWORD       "1020304050"
#define WIFI_RETRY_MAX      3
#define WIFI_TIMEOUT_MS     15000

#define SERVER_HOST         "<!-- laptop static IP, e.g. 192.168.1.50 -->"
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

// Maintenance full refresh times
#define MAINTENANCE_1_H     8
#define MAINTENANCE_1_M     0
#define MAINTENANCE_2_H     12
#define MAINTENANCE_2_M     0
#define MAINTENANCE_3_H     22
#define MAINTENANCE_3_M     0

// Ghosting prevention
#define MAX_PARTIAL_BEFORE_FULL  5

// Green button GPIO (wakeup source)
#define BUTTON_GREEN_PIN    3

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
C:\Users\Eli Zeltser\Documents\reTerminal\
├── .venv\                       # Python virtual environment (never commit)
├── server\
│   ├── main.py                  # FastAPI app entry point
│   ├── renderer.py              # Pillow canvas + zone drawing
│   ├── scheduler.py             # APScheduler task definitions
│   ├── sources\
│   │   ├── calendar.py          # Google Calendar — birthday fetch
│   │   ├── stocks.py            # Polygon.io price fetch
│   │   ├── weather.py           # Open-Meteo fetch
│   │   ├── suggestions.py       # Claude API — stock suggestion
│   │   └── quote.py             # Claude API — daily quote
│   ├── fonts\
│   │   ├── Inter-Bold.ttf
│   │   ├── Inter-Medium.ttf
│   │   ├── Inter-Regular.ttf
│   │   ├── JetBrainsMono-Bold.ttf
│   │   ├── JetBrainsMono-Regular.ttf
│   │   ├── PlayfairDisplay-Italic.ttf
│   │   └── NerdFontsSymbolsOnly-Regular.ttf
│   ├── secrets\
│   │   ├── credentials.json     # Google OAuth client secret — NEVER COMMIT
│   │   ├── token.json           # Google OAuth refresh token — NEVER COMMIT
│   │   └── .env                 # All API keys — NEVER COMMIT
│   ├── cache\
│   │   └── display.png          # Latest rendered image
│   ├── quotes_fallback.json     # 10 fallback quotes
│   ├── requirements.txt
│   ├── nssm_install.ps1         # Windows Service setup script
│   └── CLAUDE.md                # Server project context for Claude Code
└── firmware\                    # Zephyr firmware project (see §7)
    └── CLAUDE.md                # Firmware project context for Claude Code
```

**Virtual environment setup (first time):**
```powershell
cd "C:\Users\Eli Zeltser\Documents\reTerminal"
python -m venv .venv
.venv\Scripts\Activate.ps1
pip install fastapi uvicorn pillow httpx apscheduler `
            google-api-python-client google-auth-oauthlib `
            anthropic python-dotenv
pip freeze > server\requirements.txt
```

### 8.2 API Endpoints

| Endpoint | Method | Description |
|---|---|---|
| `/display.png` | GET | Rendered PNG with ETag. Returns `304` if unchanged. |
| `/status` | GET | JSON: last render time, data source statuses, next scheduled fetches, ETag. |
| `/refresh` | POST | Force immediate re-render and data re-fetch. |

### 8.3 Render Pipeline

```
scheduler triggers render()
  │
  ├─► Fetch in parallel (asyncio.gather):
  │     calendar.get_birthdays_today()
  │     weather.get_today()           [cached post-05:45]
  │     stocks.get_prices(tickers)    [cached post-scheduled-fetch]
  │     quote.get_today()             [cached post-05:45]
  │     suggestions.maybe_get()       [only if trigger conditions met]
  │
  ├─► renderer.compose(data) → PIL Image (800×480, mode "1")
  │     draw_quote_strip()            [y=0..50]
  │     draw_weather_zone()           [x=0..240, y=50..320]
  │     draw_clock_zone()             [x=240..800, y=50..210]
  │     draw_birthday_zone()          [x=240..800, y=210..320, if any]
  │     draw_stocks_zone()            [y=320..400]
  │     draw_footer()                 [y=400..480]
  │     draw_dividers()
  │
  ├─► MD5 hash of PNG bytes → ETag
  │
  ├─► If ETag changed → write cache\display.png, update stored ETag
  │
  └─► Return (bytes, etag)
```

### 8.4 Scheduling

| Task | Schedule | Notes |
|---|---|---|
| Full render | Every 60s during active windows; every 30 min otherwise | E1001 uses ETag — no wasted refresh |
| Weather fetch | Daily at 05:45 | Daily min/max included; cached all day |
| Stock fetch | Daily at 05:45 and 17:30 | See §6.2 |
| Calendar fetch | Daily at 05:45 and 18:00 | Re-fetched for evening window |
| Quote generation | Daily at 05:45 | Cached all day |
| AI suggestions | On trigger (price change > 1.5%) | 30 min cooldown per ticker |

### 8.5 `server\CLAUDE.md`

```markdown
# CLAUDE.md — ePaper Dashboard Server

## What this does
FastAPI server on Windows 10, Asus A554I.
Renders 800×480 1-bit PNG dashboard for Seeed reTerminal E1001 over HTTP.
All Python runs inside the venv at:
  C:\Users\Eli Zeltser\Documents\reTerminal\.venv

## Activate venv before any pip or python command
  .venv\Scripts\Activate.ps1

## Run (development)
  cd server
  python -m uvicorn main:app --host 0.0.0.0 --port 8080 --reload

## Useful commands
  curl -X POST http://localhost:8080/refresh
  curl http://localhost:8080/status

## Critical constraints
- Image: exactly 800×480 px, PIL mode "1" (1-bit)
- Fonts: always ImageFont.truetype() from server\fonts\ — never default fonts
- HTTP: always httpx.AsyncClient, never `requests`
- Secrets: never hardcode — always load from server\secrets\.env

## Layout zones (pixels)
- Quote:     x=0,   y=0,   w=800, h=50
- Weather:   x=0,   y=50,  w=240, h=270
- Clock:     x=240, y=50,  w=560, h=160
- Birthdays: x=240, y=210, w=560, h=110  (hidden if none today)
- Stocks:    x=0,   y=320, w=800, h=80
- Footer:    x=0,   y=400, w=800, h=80

## Data sources
- Calendar: Birthdays only, token.json auto-refreshes
- Stocks: Polygon.io, fetched at 05:45 and 17:30
- Weather: Open-Meteo, lat=32.08 lon=34.78, fetch at 05:45
- Quote: Claude Haiku, 100 tokens, 05:45 daily, fallback in quotes_fallback.json
- AI suggestions: Claude Haiku, 60 tokens, 30 min cooldown
```

---

## 9. Configuration & Secrets Management

### 9.1 Server `.env` file

Location: `C:\Users\Eli Zeltser\Documents\reTerminal\server\secrets\.env`

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
QUOTE_MAX_CHARS=120

# ── Server ───────────────────────────────────────────────────
SERVER_HOST=0.0.0.0
SERVER_PORT=8080
```

### 9.2 `.gitignore`

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

## 10. Error Handling & Fallback Behavior

| Failure scenario | Behaviour |
|---|---|
| **Wi-Fi fails at boot (E1001)** | Retry 3×, 5s apart. Then: render error screen, sleep 5 min, retry |
| **NTP sync fails** | Use RTC, log warning. Self-corrects next boot |
| **Server unreachable** | Keep last displayed image. Sleep normally and retry |
| **Google Calendar API error** | Hide birthday zone silently — no error text |
| **Polygon.io error / rate limit** | Show last known price with `[delayed]` label |
| **Claude API error (suggestion)** | Leave suggestion line blank |
| **Claude API error (quote)** | Random entry from `quotes_fallback.json` |
| **Weather API error** | Show `—` in weather zone (no cached stale data shown) |
| **Server render exception** | Log traceback. Serve last cached PNG. Server does not crash |
| **PNG decode error (E1001)** | Skip update. Log to console. Sleep normally |

---

## 11. Development Toolchain

### 11.1 Laptop / Server Side (Windows 10)

| Tool | Purpose | Install |
|---|---|---|
| Python 3.12 | Runtime | [python.org](https://python.org) |
| venv | Isolation | Built into Python: `python -m venv .venv` |
| FastAPI + Uvicorn | HTTP server | `pip install fastapi uvicorn` |
| Pillow | Image rendering | `pip install pillow` |
| httpx | Async HTTP client | `pip install httpx` |
| APScheduler | Scheduling | `pip install apscheduler` |
| google-api-python-client | Calendar SDK | `pip install google-api-python-client google-auth-oauthlib` |
| anthropic | Claude SDK | `pip install anthropic` |
| python-dotenv | `.env` loading | `pip install python-dotenv` |
| NSSM | Windows Service | [nssm.cc](https://nssm.cc/download) |
| Claude Code | AI dev | `npm install -g @anthropic-ai/claude-code` (requires Node.js) |

### 11.2 E1001 Firmware Side (Arch Linux)

| Tool | Purpose | Install |
|---|---|---|
| west | Zephyr build tool | `pip install west --break-system-packages` |
| Zephyr SDK | ARM/Xtensa toolchain | Via `west sdk install` |
| CMake + Ninja | Build system | `sudo pacman -S cmake ninja` |
| esptool | Flashing | `pip install esptool --break-system-packages` |
| Python deps | Zephyr scripts | `pip install -r ~/zephyrproject/zephyr/scripts/requirements.txt --break-system-packages` |

```bash
# Full Zephyr workspace init (one time)
pip install west --break-system-packages
west init ~/zephyrproject
cd ~/zephyrproject && west update && west zephyr-export
west sdk install

# Build
cd ~/Documents/reTerminal/firmware
west build -b reterminal_e1001 .

# Flash
west flash --runner esptool
```

> Keep `PLATFORMIO_CORE_DIR=/opt/platformio` in `.bashrc` — PlatformIO is not used here but the var prevents accidental installs to the full `/home` partition.

### 11.3 Claude Code

```bash
# In server directory (Windows)
cd "C:\Users\Eli Zeltser\Documents\reTerminal\server"
claude     # reads server\CLAUDE.md

# In firmware directory (Arch Linux)
cd ~/Documents/reTerminal/firmware
claude     # reads firmware\CLAUDE.md
```

---

## 12. Open Questions & Decisions Pending

| # | Question | Options / Notes | Decision |
|---|---|---|---|
| 1 | Tickers to track | Fill in §6.2 and `.env` | <!-- TBD --> |
| 2 | Stock suggestion trigger % | Currently 1.5%. Adjust in `.env` | <!-- TBD --> |
| 3 | E1001 IP assignment | Recommend DHCP reservation in router for E1001 MAC | <!-- TBD --> |
| 4 | Laptop static IP | Fill in §2.3 after running `ipconfig` | <!-- TBD --> |
| 5 | Fallback quote list | Populate `quotes_fallback.json` with 10 favourites (see §6.5) | <!-- TBD --> |
| 6 | Green button refresh | **Decided: implemented.** Full refresh on button press. See §7.5. | ✅ Done |
| 7 | ZEReader UC8179 driver | Reference or fork from [github.com/teslabs/zereader](https://github.com/teslabs/zereader) | <!-- TBD — confirm license --> |
| 8 | Birthday zone if empty | Currently: clock expands vertically. Confirm this looks good. | <!-- TBD --> |

---

## Appendix A — Change Log

| Version | Date | Changes |
|---|---|---|
| 0.1 | 01.05.2026 | Initial draft |
| 0.2 | 01.05.2026 | Added quote of the day; expanded network topology; NSSM + FastAPI rationale; redesigned grid; weather min/max/wind/icons; NTP sync §5.5; ESP-IDF firmware; Wi-Fi error screen; stock schedule 05:45+17:30 |
| 0.3 | 01.05.2026 | Virtual environment path specified (`C:\Users\Eli Zeltser\Documents\reTerminal\.venv`); all server paths updated to full Windows path; Ethernet static IP setup with full step-by-step commands; layout redesigned: quote top, clock+birthdays merged center, birthdays-only calendar, stocks bottom, footer date+time; quote sources expanded to include literary works (Dune, The Alchemist, history); firmware changed from ESP-IDF to Zephyr RTOS (rationale: official E1001 board support + existing UC8179 driver in ZEReader); all §7 rewritten for Zephyr (`prj.conf`, `west.yml`, west commands, Zephyr PM subsystem); green button wakeup specified and resolved from open questions; directory structure updated to show both `server\` and `firmware\` under project root |

---

*All `<!-- EDIT -->` and `<!-- TBD -->` markers indicate places requiring input before implementation begins.*
