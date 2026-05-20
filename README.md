# reTerminal ePaper Dashboard

Personal smart dashboard on a Seeed Studio reTerminal E1001 (7.5" ePaper, 800×480).
Displays time, weather, birthday reminders, stocks, and a daily quote.

Full spec: [FSD.md](FSD.md)

---

## Server Setup (Arch Linux)

```bash
source .venv/bin/activate
pip install -r server/requirements.txt

# Copy and fill in secrets
cp server/secrets/.env.example server/secrets/.env
# edit server/secrets/.env — add STOCK_API_KEY, check paths

# Download fonts (see server/fonts/README.md)

# Run development server
cd server
uvicorn main:app --host 0.0.0.0 --port 8080 --reload
```

Admin panel: `http://<your-lan-ip>:8080/admin`

Force a re-render: `curl -X POST http://localhost:8080/refresh`

---

## Running as a System Service (Arch Linux)

Set up the server to start on boot and restart automatically on failure.

**Create the service file:**

```bash
sudo nano /etc/systemd/system/epaper-dashboard.service
```

```ini
[Unit]
Description=ePaper Dashboard Server
After=network.target

[Service]
Type=simple
User=eli
WorkingDirectory=/home/eli/Documents/reTerminal/server
ExecStart=/home/eli/Documents/reTerminal/.venv/bin/uvicorn main:app --host 0.0.0.0 --port 8080
Restart=on-failure
RestartSec=5s
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

**Enable and start:**

```bash
sudo systemctl daemon-reload
sudo systemctl enable epaper-dashboard   # start on boot
sudo systemctl start epaper-dashboard
sudo systemctl status epaper-dashboard
```

**Managing the service (local or over SSH):**

```bash
sudo systemctl restart epaper-dashboard   # restart
sudo systemctl stop epaper-dashboard      # stop
sudo systemctl start epaper-dashboard     # start
```

**Reading logs** (systemd journal is a cyclic buffer — survives crashes):

```bash
journalctl -u epaper-dashboard -n 200          # last 200 lines
journalctl -u epaper-dashboard -f              # follow live
journalctl -u epaper-dashboard -b -1           # output from the previous boot (crash logs)
journalctl -u epaper-dashboard --since "1h ago"
```

---

## Firmware Setup (Arch Linux → E1001)

```bash
# One-time Zephyr setup
sudo pacman -S cmake ninja python python-pip
pip install west esptool --break-system-packages
west init ~/zephyrproject
cd ~/zephyrproject && west update && west zephyr-export
west sdk install

# Clone pngle PNG decoder into firmware/lib/pngle/
git clone https://github.com/kikuchan/pngle firmware/lib/pngle

# Edit Wi-Fi credentials and server IP
$EDITOR firmware/src/config.h

# Build and flash
cd firmware
west build -b reterminal_e1001/esp32s3/procpu ~/Documents/reTerminal/firmware
west flash --runner esp32 --esp-device /dev/ttyUSB0
```

---

## Staged rollout

| Stage | Goal |
|---|---|
| 0 | Document structure (this commit) |
| 1 | Static test image on ePaper — prove hardware pipeline works |
| 2 | ETag, PM sleep, scheduling, green-button wakeup |
| 3 | Real clock + weather |
| 4 | Google Calendar birthdays |
| 5 | Quote of the day + stocks + admin panel |
