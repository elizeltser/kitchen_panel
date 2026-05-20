"""
Google Calendar — fetch birthday events.

One-time OAuth setup (run from server/ directory):
  python -c "from sources.calendar_src import authorize; authorize()"
  # Opens browser, grants permission, saves token.json to secrets/
"""
import asyncio
import logging
from datetime import date, datetime, timedelta
from pathlib import Path
from zoneinfo import ZoneInfo

from google.auth.transport.requests import Request
from google.oauth2.credentials import Credentials
from google_auth_oauthlib.flow import InstalledAppFlow
from googleapiclient.discovery import build

log = logging.getLogger(__name__)

SCOPES = ["https://www.googleapis.com/auth/calendar.readonly"]
SECRETS = Path(__file__).parent.parent / "secrets"
CREDS_FILE = SECRETS / "credentials.json"
TOKEN_FILE = SECRETS / "token.json"
TZ = ZoneInfo("Asia/Jerusalem")

# How far ahead to scan for upcoming-birthday reminders (days).
LOOKAHEAD_DAYS = 7


def _get_creds() -> Credentials:
    creds = None
    if TOKEN_FILE.exists():
        creds = Credentials.from_authorized_user_file(str(TOKEN_FILE), SCOPES)
    if not creds or not creds.valid:
        if creds and creds.expired and creds.refresh_token:
            creds.refresh(Request())
            TOKEN_FILE.write_text(creds.to_json())
        else:
            raise RuntimeError(
                "No valid Google token. Run: "
                'python -c "from sources.calendar_src import authorize; authorize()"'
            )
    return creds


def authorize():
    """Interactive OAuth flow — run once from the command line."""
    if not CREDS_FILE.exists():
        raise FileNotFoundError(
            f"credentials.json not found at {CREDS_FILE}\n"
            "Download it from Google Cloud Console → APIs & Services → Credentials"
        )
    flow = InstalledAppFlow.from_client_secrets_file(str(CREDS_FILE), SCOPES)
    creds = flow.run_local_server(port=0)
    TOKEN_FILE.write_text(creds.to_json())
    print(f"Token saved to {TOKEN_FILE}")


async def get_month_events() -> list:
    """
    Return all events for the current month as [{"date": date, "title": str}].
    Queries the primary calendar. Returns [] on auth error.
    """
    try:
        creds = _get_creds()
    except Exception as e:
        log.warning("Calendar auth failed: %s", e)
        return []

    today = datetime.now(TZ).date()
    first = date(today.year, today.month, 1)
    last = date(today.year + (today.month == 12),
                (today.month % 12) + 1, 1) - timedelta(days=1)

    def _fetch():
        svc = build("calendar", "v3", credentials=creds)
        time_min = datetime(first.year, first.month, first.day, tzinfo=TZ).isoformat()
        time_max = datetime(last.year, last.month, last.day, 23, 59, 59, tzinfo=TZ).isoformat()
        result = (
            svc.events()
            .list(
                calendarId="primary",
                timeMin=time_min,
                timeMax=time_max,
                singleEvents=True,
                orderBy="startTime",
                maxResults=200,
            )
            .execute()
        )
        events = []
        for ev in result.get("items", []):
            title = ev.get("summary", "").strip()
            if not title:
                continue
            start_str = ev["start"].get("date") or ev["start"].get("dateTime", "")[:10]
            try:
                ev_date = date.fromisoformat(start_str)
            except ValueError:
                continue
            events.append({"date": ev_date, "title": title})
        return events

    try:
        return await asyncio.to_thread(_fetch)
    except Exception as e:
        log.error("Calendar month fetch failed: %s", e)
        return []


async def get_birthdays_today() -> list:
    """
    Return reminder strings for birthday events.

    Queries the next LOOKAHEAD_DAYS days. Events that are today are always
    included. Events that are N days away are included only when they have a
    reminder override whose advance notice (in days) matches N — i.e. the
    reminder fires today.

    Returns [] on auth error or no matching events.
    """
    try:
        creds = _get_creds()
    except Exception as e:
        log.warning("Calendar auth failed: %s", e)
        return []

    now_local = datetime.now(TZ)
    today = now_local.replace(hour=0, minute=0, second=0, microsecond=0)
    window_end = today + timedelta(days=LOOKAHEAD_DAYS)

    def _fetch():
        svc = build("calendar", "v3", credentials=creds)
        result = (
            svc.events()
            .list(
                calendarId="primary",
                timeMin=today.isoformat(),
                timeMax=window_end.isoformat(),
                singleEvents=True,
                orderBy="startTime",
                maxResults=20,
            )
            .execute()
        )
        reminders = []
        for ev in result.get("items", []):
            name = ev.get("summary", "")
            start_str = ev["start"].get("date") or ev["start"].get("dateTime", "")[:10]
            try:
                event_date = datetime.fromisoformat(start_str).date()
            except ValueError:
                continue

            days_until = (event_date - today.date()).days

            if days_until == 0:
                reminders.append(f"{name} is today!")
                continue

            for rem in ev.get("reminders", {}).get("overrides", []):
                rem_days = rem.get("minutes", 0) // 1440
                if rem_days == days_until:
                    label = "day" if days_until == 1 else "days"
                    reminders.append(f"{name} is in {days_until} {label}")

        return reminders

    try:
        return await asyncio.to_thread(_fetch)
    except Exception as e:
        log.error("Calendar fetch failed: %s", e)
        return []
