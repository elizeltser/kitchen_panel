"""
Pillow-based renderer — composes the 800×480 1-bit PNG from live data.

Zone layout (pixels):
  Quote:     x=0,   y=0,   w=800, h=70
  Weather:   x=0,   y=70,  w=220, h=310
  Clock:     x=220, y=70,  w=580, h=220  (h=310 when no birthdays)
  Birthdays: x=220, y=290, w=580, h=90   (omitted when empty)
  Stocks:    x=0,   y=380, w=800, h=100
"""
import hashlib
import io
import math
import warnings
from datetime import datetime
from pathlib import Path
from typing import Optional
from zoneinfo import ZoneInfo

from PIL import Image, ImageDraw, ImageFont

FONTS = Path(__file__).parent / "fonts"
TZ = ZoneInfo("Asia/Jerusalem")

W, H = 800, 480
BLACK, WHITE = 0, 255
SCALE = 2  # supersample factor — render at 2× then downsample with LANCZOS

QUOTE_H = 70
CONTENT_Y = 70
WEATHER_W = 220
CLOCK_X = 220
CLOCK_W = 580
BDAY_Y = 290
BDAY_H = 90
STOCKS_Y = 380
STOCKS_H = 100


def _font(name: str, size: int) -> ImageFont.FreeTypeFont:
    p = FONTS / name
    if p.exists():
        return ImageFont.truetype(str(p), size)
    warnings.warn(f"Font missing: {p} — using PIL fallback (install fonts before production)")
    return ImageFont.load_default()


def _center(draw, font, text, cx, cy):
    bb = draw.textbbox((0, 0), text, font=font)
    # Use (left+right)/2 and (top+bottom)/2 so the visual bbox midpoint lands on (cx,cy)
    draw.text((cx - (bb[0] + bb[2]) // 2, cy - (bb[1] + bb[3]) // 2),
              text, font=font, fill=BLACK)


def _right(draw, font, text, rx, y):
    bb = draw.textbbox((0, 0), text, font=font)
    draw.text((rx - (bb[2] - bb[0]), y), text, font=font, fill=BLACK)


def _wrap(draw, font, text, max_width, max_lines=2) -> list:
    words = text.split()
    lines, cur = [], ""
    for word in words:
        test = (cur + " " + word).strip()
        if draw.textbbox((0, 0), test, font=font)[2] <= max_width:
            cur = test
        else:
            if cur:
                lines.append(cur)
            cur = word
            if len(lines) >= max_lines:
                break
    if cur and len(lines) < max_lines:
        lines.append(cur)
    return lines


def draw_dividers(draw, has_birthdays: bool, scale: int = 1):
    s = scale
    draw.line([(0, CONTENT_Y * s), (W * s, CONTENT_Y * s)], fill=BLACK, width=s)
    draw.line([(0, STOCKS_Y * s), (W * s, STOCKS_Y * s)], fill=BLACK, width=s)
    draw.line([(CLOCK_X * s, CONTENT_Y * s), (CLOCK_X * s, STOCKS_Y * s)], fill=BLACK, width=s)
    if has_birthdays:
        draw.line([(CLOCK_X * s, BDAY_Y * s), (W * s, BDAY_Y * s)], fill=BLACK, width=s)


def draw_quote_strip(draw, quote: Optional[dict], scale: int = 1):
    s = scale
    if not quote:
        quote = {"quote": "Not all those who wander are lost.", "attribution": "J.R.R. Tolkien"}

    fq = _font("PlayfairDisplay-Italic.ttf", 25 * s)
    fa = _font("Inter-Regular.ttf", 14 * s)

    lines = _wrap(draw, fq, quote["quote"], (W - 40) * s, max_lines=2)
    y = 4 * s
    lh = 24 * s
    for line in lines:
        draw.text((10 * s, y), line, font=fq, fill=BLACK)
        y += lh

    attribution = f"\u2014 {quote['attribution']}"
    _right(draw, fa, attribution, (W - 10) * s, y + 2 * s)


def _draw_moon_disc(draw, cx: float, cy: float, r: float, phase: float, scale: int = 1):
    """
    Draw moon phase disc. All coords in pre-scale pixels.
    phase: 0.0 = new moon, 0.5 = full moon, 1.0 = new moon again.

    Uses a two-shape composite: a right or left illuminated semicircle plus an
    ellipse that either subtracts (crescent) or adds (gibbous) illumination.
    """
    s = scale
    cx, cy, r = int(cx * s), int(cy * s), int(r * s)
    if r < 1:
        return

    draw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=BLACK)

    if phase < 0.5:
        f = phase / 0.5  # 0 → 1 as new → full
        draw.pieslice([cx - r, cy - r, cx + r, cy + r], -90, 90, fill=WHITE)
        if f < 0.5:  # waxing crescent
            a = int(r * (1 - 2 * f))
            if a > 0:
                draw.ellipse([cx - a, cy - r, cx + a, cy + r], fill=BLACK)
        elif f > 0.5:  # waxing gibbous
            a = int(r * (2 * f - 1))
            if a > 0:
                draw.ellipse([cx - a, cy - r, cx + a, cy + r], fill=WHITE)
    elif phase > 0.5:
        f = (phase - 0.5) / 0.5  # 0 → 1 as full → new
        draw.pieslice([cx - r, cy - r, cx + r, cy + r], 90, 270, fill=WHITE)
        if f < 0.5:  # waning gibbous
            a = int(r * (1 - 2 * f))
            if a > 0:
                draw.ellipse([cx - a, cy - r, cx + a, cy + r], fill=WHITE)
        elif f > 0.5:  # waning crescent
            a = int(r * (2 * f - 1))
            if a > 0:
                draw.ellipse([cx - a, cy - r, cx + a, cy + r], fill=BLACK)
    else:
        draw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=WHITE)

    draw.ellipse([cx - r, cy - r, cx + r, cy + r], outline=BLACK, width=scale)


def draw_weather_zone(draw, weather: Optional[dict], moon_phase: Optional[float] = None, scale: int = 1):
    s = scale
    cx = (WEATHER_W // 2) * s
    if not weather:
        _center(draw, _font("Inter-Regular.ttf", 20 * s), "\u2014", cx, (CONTENT_Y + 155) * s)
    else:
        y = (CONTENT_Y + 16) * s
        fi = _font("NerdFontsSymbolsOnly-Regular.ttf", 72 * s)
        fd = _font("Inter-Regular.ttf", 25 * s)
        ft = _font("Montserrat-Bold.ttf", 35 * s)
        fr = _font("Inter-Regular.ttf", 20 * s)

        icon = weather.get("icon", "")
        if icon:
            _center(draw, fi, icon, cx, y + 26 * s)
            y += 80 * s

        _center(draw, fd, weather.get("description", ""), cx, y)
        y += 40 * s

        tmax = weather.get("temp_max")
        tmin = weather.get("temp_min")
        if tmax is not None and tmin is not None:
            _center(draw, ft, f"\u2191{tmax:.0f}\u00b0  \u2193{tmin:.0f}\u00b0", cx, y)
            y += 40 * s

        rain = weather.get("rain_pct")
        if rain is not None:
            _center(draw, fr, f"Rain {rain:.0f}%", cx, y)

    # Moon phase disc \u2014 drawn regardless of weather data availability.
    # Horizontal: phase 0 (new) near left edge \u2192 phase 0.5 (full) at centre \u2192
    #             phase 1 (new again) near right edge.
    # Size: bell curve (sin) \u2014 largest at full moon, smallest at new.
    if moon_phase is not None:
        R_MIN, R_MAX = 8, 34
        r = R_MIN + (R_MAX - R_MIN) * math.sin(moon_phase * math.pi)
        # cx range 15\u2013205 keeps disc inside the 220 px weather column at all sizes
        moon_cx = 15 + moon_phase * 190
        # Vertically centred in the gap between rain text (~y=256) and stocks line (y=380)
        moon_cy = 318
        _draw_moon_disc(draw, moon_cx, moon_cy, r, moon_phase, scale=s)


def draw_clock_zone(draw, has_birthdays: bool, scale: int = 1):
    s = scale
    now = datetime.now(TZ)
    time_str = f"{now.hour}:{now.minute:02d}"
    date_str = now.strftime("%A, %B %-d")

    ft = _font("Montserrat-Regular.ttf", 134 * s)
    fd = _font("Montserrat-Regular.ttf", 30 * s)

    clock_bottom = BDAY_Y if has_birthdays else STOCKS_Y
    clock_h = clock_bottom - CONTENT_Y
    cx = (CLOCK_X + CLOCK_W // 2) * s
    mid_y = (CONTENT_Y + clock_h // 2) * s

    _center(draw, ft, time_str, cx, mid_y - 22 * s)
    _center(draw, fd, date_str, cx, mid_y + 78 * s)


def draw_birthday_zone(draw, birthdays: list, scale: int = 1):
    if not birthdays:
        return
    s = scale
    f = _font("Inter-Medium.ttf", 20 * s)
    total = min(len(birthdays), 3)
    start_y = (BDAY_Y + (BDAY_H - total * 28) // 2) * s
    cx = (CLOCK_X + CLOCK_W // 2) * s
    for i, name in enumerate(birthdays[:3]):
        _center(draw, f, f"\U0001f382 {name}", cx, start_y + i * 28 * s + 10 * s)


def draw_stocks_zone(draw, stocks: Optional[dict], scale: int = 1):
    s = scale
    ft = _font("JetBrainsMono-Bold.ttf", 18 * s)
    fs = _font("Inter-Regular.ttf", 15 * s)

    if not stocks:
        _center(draw, fs, "Stock data pending", (W // 2) * s, (STOCKS_Y + 30) * s)
        return

    parts = []
    for t in stocks.get("tickers", []):
        arrow = "\u25b2" if t["change_pct"] >= 0 else "\u25bc"
        parts.append(f"{t['symbol']} ${t['price']:.2f}  {arrow}{abs(t['change_pct']):.1f}%")
    line1 = "    ".join(parts)
    _center(draw, ft, line1, (W // 2) * s, (STOCKS_Y + 28) * s)

    summary = stocks.get("portfolio_summary", "")
    if summary:
        _center(draw, fs, summary, (W // 2) * s, (STOCKS_Y + 68) * s)


def compose(weather=None, birthdays=None, stocks=None, quote=None, moon_phase=None) -> tuple:
    """Render full 800×480 dashboard. Returns (png_bytes, md5_etag)."""
    if birthdays is None:
        birthdays = []

    S = SCALE
    img = Image.new("L", (W * S, H * S), color=WHITE)
    draw = ImageDraw.Draw(img)

    has_bdays = len(birthdays) > 0

    draw_quote_strip(draw, quote, scale=S)
    draw_weather_zone(draw, weather, moon_phase=moon_phase, scale=S)
    draw_clock_zone(draw, has_bdays, scale=S)
    if has_bdays:
        draw_birthday_zone(draw, birthdays, scale=S)
    draw_stocks_zone(draw, stocks, scale=S)
    draw_dividers(draw, has_bdays, scale=S)

    img_small = img.resize((W, H), Image.LANCZOS)
    buf = io.BytesIO()
    img_small.save(buf, format="PNG")
    png = buf.getvalue()
    return png, hashlib.md5(png).hexdigest()
