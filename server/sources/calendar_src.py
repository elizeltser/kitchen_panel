"""
Google Calendar — fetch today's birthday events.

One-time OAuth setup (run from server/ directory):
  python -c "from sources.calendar_src import authorize; authorize()"
  # Opens browser, grants permission, saves token.json to secrets/
"""
import asyncio
import logging
from datetime import datetime, timedelta
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


async def get_birthdays_today() -> list:
    """Return list of birthday names for today. Returns [] on error or no events."""
    try:
        creds = _get_creds()
    except Exception as e:
        log.warning("Calendar auth failed: %s", e)
        return []

    now_local = datetime.now(TZ)
    day_start = now_local.replace(hour=0, minute=0, second=0, microsecond=0)
    day_end = now_local.replace(hour=23, minute=59, second=59, microsecond=0)

    def _fetch():
        svc = build("calendar", "v3", credentials=creds)
        result = (
            svc.events()
            .list(
                calendarId="primary",
                timeMin=day_start.isoformat(),
                timeMax=day_end.isoformat(),
                singleEvents=True,
                orderBy="startTime",
                maxResults=6,
            )
            .execute()
        )
        names = []
        for ev in result.get("items", []):
            start = ev.get("start", {})
            # All-day events have "date" key, not "dateTime"
            if "date" in start and "dateTime" not in start:
                names.append(ev.get("summary", "Unknown"))
        return names

    try:
        return await asyncio.to_thread(_fetch)
    except Exception as e:
        log.error("Calendar fetch failed: %s", e)
        return []
