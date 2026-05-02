"""
Open-Meteo weather — free, no API key.
Location: Bat Yam, Israel — lat=32.08, lon=34.78
"""
import logging

import httpx

log = logging.getLogger(__name__)

URL = (
    "https://api.open-meteo.com/v1/forecast"
    "?latitude=32.08&longitude=34.78"
    "&current=temperature_2m,weather_code"
    "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max"
    "&timezone=Asia%2FJerusalem&forecast_days=1"
)

# WMO weather code → (Nerd Font glyph, description)
# Glyphs from NerdFontsSymbolsOnly — nf-md-weather_* set
WMO: dict = {
    0:  ("\U000F0599", "Clear"),
    1:  ("\U000F0595", "Mainly Clear"),
    2:  ("\U000F0595", "Partly Cloudy"),
    3:  ("\U000F0590", "Overcast"),
    45: ("\U000F0591", "Foggy"),
    48: ("\U000F0591", "Icy Fog"),
    51: ("\U000F0597", "Light Drizzle"),
    53: ("\U000F0597", "Drizzle"),
    55: ("\U000F0597", "Heavy Drizzle"),
    61: ("\U000F0596", "Light Rain"),
    63: ("\U000F0596", "Rain"),
    65: ("\U000F0596", "Heavy Rain"),
    71: ("\U000F059B", "Light Snow"),
    73: ("\U000F059B", "Snow"),
    75: ("\U000F059B", "Heavy Snow"),
    77: ("\U000F059B", "Snow Grains"),
    80: ("\U000F0596", "Showers"),
    81: ("\U000F0596", "Showers"),
    82: ("\U000F0596", "Heavy Showers"),
    85: ("\U000F059B", "Snow Showers"),
    86: ("\U000F059B", "Heavy Snow Showers"),
    95: ("\U000F067E", "Thunderstorm"),
    96: ("\U000F067E", "Thunderstorm + Hail"),
    99: ("\U000F067E", "Thunderstorm + Hail"),
}


async def get_today() -> dict:
    async with httpx.AsyncClient(timeout=10) as client:
        r = await client.get(URL)
        r.raise_for_status()
        d = r.json()

    code = d["current"]["weather_code"]
    icon, desc = WMO.get(code, ("", f"WMO {code}"))

    return {
        "icon": icon,
        "description": desc,
        "temp_max": d["daily"]["temperature_2m_max"][0],
        "temp_min": d["daily"]["temperature_2m_min"][0],
        "rain_pct": d["daily"]["precipitation_probability_max"][0],
    }
