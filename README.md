# reTerminal ePaper Dashboard

Personal smart dashboard on a Seeed Studio reTerminal E1001 (7.5" ePaper, 800×480).
Displays time, weather, birthday reminders, and a daily quote.

Full spec: [FSD.md](FSD.md)

---

## Server Setup (Windows)

**Prerequisites:** Python 3.12+, Git

```powershell
# Create and activate virtual environment
python -m venv .venv
.venv\Scripts\Activate.ps1

# Install dependencies
pip install -r server\requirements.txt

# Copy and fill in secrets
copy server\secrets\.env.example server\secrets\.env
# Edit .env — add STOCK_API_KEY, check Google credential paths

# Run development server
cd server
uvicorn main:app --host 0.0.0.0 --port 8080 --reload
```

Admin panel: `http://localhost:8080/admin`

Force a re-render: `curl -X POST http://localhost:8080/refresh`

View current display output in browser: `http://localhost:8080/display.png`

View calendar page: `http://localhost:8080/display/1`

---

## Running as a Windows Service (NSSM)

[NSSM](https://nssm.cc) runs the server as a Windows service with automatic restart on failure and built-in log rotation.

**Install NSSM** — download from https://nssm.cc/download, place `nssm.exe` somewhere on your PATH (e.g. `C:\tools\nssm.exe`).

**Create the service** (run PowerShell as Administrator):

```powershell
$root = "C:\Users\eli\Documents\reTerminal"
$uvicorn = "$root\.venv\Scripts\uvicorn.exe"

nssm install epaper-dashboard $uvicorn "main:app --host 0.0.0.0 --port 8080"
nssm set epaper-dashboard AppDirectory "$root\server"

# Auto-restart on failure, 5 second delay
nssm set epaper-dashboard AppRestartDelay 5000

# Log output to rotating files
nssm set epaper-dashboard AppStdout "$root\logs\server.log"
nssm set epaper-dashboard AppStderr "$root\logs\server-error.log"
nssm set epaper-dashboard AppRotateFiles 1
nssm set epaper-dashboard AppRotateBytes 1000000
nssm set epaper-dashboard AppRotateOnline 1

# Create logs folder
mkdir "$root\logs"

nssm start epaper-dashboard
```

**Managing the service:**

```powershell
nssm start   epaper-dashboard
nssm stop    epaper-dashboard
nssm restart epaper-dashboard
nssm status  epaper-dashboard
nssm remove  epaper-dashboard confirm   # uninstall
```

**Reading logs after a crash:**

```powershell
# Last 100 lines of output
test C:\Users\eli\Documents\reTerminal\logs\server.log 
```

---

## Firmware Setup (Linux / WSL required)

The Zephyr toolchain does not run on Windows. Use WSL2 (Ubuntu or Arch) or a Linux machine.

```bash
# One-time Zephyr setup
sudo pacman -S cmake ninja python python-pip   # Arch; use apt on Ubuntu
pip install west esptool --break-system-packages
west init ~/zephyrproject
cd ~/zephyrproject && west update && west zephyr-export
west sdk install

# Clone pngle PNG decoder into firmware/lib/pngle/
git clone https://github.com/kikuchan/pngle firmware/lib/pngle

# Edit Wi-Fi credentials and server IP
$EDITOR firmware/src/config.h

# Build and flash
cd /opt/zephyrproject
west build -b reterminal_e1001/esp32s3/procpu ~/Documents/reTerminal/firmware
west flash --runner esp32 --esp-device /dev/ttyUSB0
```

Serial monitor: `west espressif monitor --port /dev/ttyUSB0`

---

## Staged Rollout

| Stage | Goal |
|---|---|
| 0 | Document structure |
| 1 | Static test image on ePaper — prove hardware pipeline works |
| 2 | ETag, PM sleep, scheduling, green-button wakeup |
| 3 | Real clock + weather |
| 4 | Google Calendar birthdays |
| 5 | Quote of the day + stocks + admin panel |
