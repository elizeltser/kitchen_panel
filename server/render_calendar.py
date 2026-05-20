"""
Calendar month-view renderer for 800×480 ePaper display.
Produces a Google Calendar-style month grid.
"""
import hashlib
import io
import warnings
from datetime import date, datetime, timedelta
from pathlib import Path
from zoneinfo import ZoneInfo

from PIL import Image, ImageDraw, ImageFont

FONTS = Path(__file__).parent / "fonts"
TZ = ZoneInfo("Asia/Jerusalem")

W, H = 800, 480
BLACK, DARK_GRAY, LIGHT_GRAY, WHITE = 0, 85, 170, 255
SCALE = 2

HEADER_H = 52    # month + year title
DAY_HDR_H = 38   # day-of-week name row
GRID_Y = HEADER_H + DAY_HDR_H  # 90

DAY_NAMES = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"]
TODAY_PAD = 4    # extra radius px (pre-scale) around today's number


def _font(name: str, size: int) -> ImageFont.FreeTypeFont:
    p = FONTS / name
    if p.exists():
        return ImageFont.truetype(str(p), size)
    warnings.warn(f"Font missing: {p}")
    return ImageFont.load_default()


def _col_x(col: int) -> int:
    """Left pixel x of column col (0-based, pre-scale)."""
    return round(col * W / 7)


def _truncate(draw: ImageDraw.ImageDraw, font: ImageFont.FreeTypeFont,
              text: str, max_px: int) -> str:
    if draw.textbbox((0, 0), text, font=font)[2] <= max_px:
        return text
    while len(text) > 1:
        text = text[:-1]
        candidate = text.rstrip() + "…"
        if draw.textbbox((0, 0), candidate, font=font)[2] <= max_px:
            return candidate
    return "…"


def _build_grid(year: int, month: int) -> list:
    """
    Return a list of weeks; each week is a list of 7 (date, in_month) tuples.
    Weeks start on Sunday.
    """
    first = date(year, month, 1)
    last = date(year + (month == 12), (month % 12) + 1, 1) - timedelta(days=1)

    # weekday(): Mon=0…Sun=6 → Sunday-first column index: Sun=0…Sat=6
    first_col = (first.weekday() + 1) % 7

    cells: list[tuple[date, bool]] = []
    for i in range(first_col - 1, -1, -1):
        cells.append((first - timedelta(days=i + 1), False))
    d = first
    while d <= last:
        cells.append((d, True))
        d += timedelta(days=1)
    while len(cells) % 7:
        cells.append((d, False))
        d += timedelta(days=1)

    return [cells[i: i + 7] for i in range(0, len(cells), 7)]


def compose_calendar(month_events: list) -> tuple:
    """
    Render month-view calendar.
    month_events: list of {"date": date, "title": str}
    Returns (png_bytes, md5_etag).
    """
    today = datetime.now(TZ).date()
    year, month = today.year, today.month

    events_by_day: dict[tuple, list[str]] = {}
    for ev in month_events:
        d = ev.get("date")
        if isinstance(d, date):
            events_by_day.setdefault((d.year, d.month, d.day), []).append(ev.get("title", ""))

    weeks = _build_grid(year, month)
    n_weeks = len(weeks)
    row_h = (H - GRID_Y) // n_weeks  # e.g. 65px for a 6-week month

    S = SCALE
    img = Image.new("L", (W * S, H * S), WHITE)
    draw = ImageDraw.Draw(img)

    f_hdr  = _font("Montserrat-Regular.ttf", 28 * S)
    f_dhdr = _font("Inter-Medium.ttf",        13 * S)
    f_dnum = _font("Montserrat-Regular.ttf",  15 * S)
    f_ev   = _font("Inter-Regular.ttf",        9 * S)

    # ── Month + year title ───────────────────────────────────────
    title = datetime(year, month, 1).strftime("%B %Y")
    bb = draw.textbbox((0, 0), title, font=f_hdr)
    draw.text(
        ((W * S) // 2 - (bb[0] + bb[2]) // 2,
         (HEADER_H * S) // 2 - (bb[1] + bb[3]) // 2),
        title, font=f_hdr, fill=BLACK
    )

    # ── Divider: header / day names ──────────────────────────────
    draw.line([(0, HEADER_H * S), (W * S, HEADER_H * S)], fill=BLACK, width=S)

    # ── Day-of-week names ────────────────────────────────────────
    dh_cy = (HEADER_H + DAY_HDR_H // 2) * S
    for col, name in enumerate(DAY_NAMES):
        col_cx = ((_col_x(col) + _col_x(col + 1)) // 2) * S
        bb = draw.textbbox((0, 0), name, font=f_dhdr)
        draw.text(
            (col_cx - (bb[0] + bb[2]) // 2, dh_cy - (bb[1] + bb[3]) // 2),
            name, font=f_dhdr, fill=BLACK
        )

    # ── Divider: day names / grid ────────────────────────────────
    draw.line([(0, GRID_Y * S), (W * S, GRID_Y * S)], fill=BLACK, width=S)

    # ── Vertical column dividers ─────────────────────────────────
    for col in range(1, 7):
        x = _col_x(col) * S
        draw.line([(x, HEADER_H * S), (x, H * S)], fill=DARK_GRAY, width=1)

    # ── Horizontal row dividers ──────────────────────────────────
    for row in range(1, n_weeks):
        y = (GRID_Y + row * row_h) * S
        draw.line([(0, y), (W * S, y)], fill=DARK_GRAY, width=1)

    # Precompute day-number height (unscaled px) for event placement
    bb_ref = draw.textbbox((0, 0), "31", font=f_dnum)
    num_h = (bb_ref[3] - bb_ref[1]) // S

    NUM_PAD_X = 5   # px from cell left to day number
    NUM_PAD_Y = 4   # px from cell top to day number
    EV_H  = 11      # event bar height (unscaled px)
    EV_GAP = 2      # gap between bars
    ev_offset = NUM_PAD_Y + num_h + 2   # px from row top to first event bar
    ev_slot   = EV_H + EV_GAP
    space     = row_h - ev_offset - 1
    max_ev    = max(0, space // ev_slot)

    # ── Day cells ────────────────────────────────────────────────
    for row_idx, week in enumerate(weeks):
        row_y = GRID_Y + row_idx * row_h

        for col_idx, (d, in_month) in enumerate(week):
            cell_x = _col_x(col_idx)
            cell_w = _col_x(col_idx + 1) - cell_x
            is_today = (d == today)
            num_str = str(d.day)

            # Day number
            bb = draw.textbbox((0, 0), num_str, font=f_dnum)
            nx = (cell_x + NUM_PAD_X) * S
            ny = (row_y + NUM_PAD_Y) * S
            # Visual centre of the number text
            vcx = nx + (bb[0] + bb[2]) // 2
            vcy = ny + (bb[1] + bb[3]) // 2

            if is_today:
                r = (max(bb[2] - bb[0], bb[3] - bb[1]) // 2) + TODAY_PAD * S
                draw.ellipse([vcx - r, vcy - r, vcx + r, vcy + r], fill=BLACK)
                draw.text((nx, ny), num_str, font=f_dnum, fill=WHITE)
            else:
                draw.text((nx, ny), num_str, font=f_dnum,
                          fill=BLACK if in_month else LIGHT_GRAY)

            if not in_month:
                continue

            # Event bars
            day_events = events_by_day.get((d.year, d.month, d.day), [])
            ev_x = cell_x + 2
            ev_w = cell_w - 4

            for ev_idx, ev_title in enumerate(day_events[:max_ev]):
                ev_top = row_y + ev_offset + ev_idx * ev_slot
                if ev_top + EV_H >= row_y + row_h:
                    break
                draw.rectangle(
                    [ev_x * S, ev_top * S,
                     (ev_x + ev_w) * S, (ev_top + EV_H) * S],
                    fill=LIGHT_GRAY
                )
                text = _truncate(draw, f_ev, ev_title, (ev_w - 3) * S)
                tb = draw.textbbox((0, 0), text, font=f_ev)
                text_y = ev_top * S + (EV_H * S - (tb[3] - tb[1])) // 2
                draw.text(((ev_x + 2) * S, text_y), text, font=f_ev, fill=BLACK)

    # ── Downsample + encode ──────────────────────────────────────
    img_small = img.resize((W, H), Image.LANCZOS)
    buf = io.BytesIO()
    img_small.save(buf, format="PNG")
    png = buf.getvalue()
    return png, hashlib.md5(png).hexdigest()
