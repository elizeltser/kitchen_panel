# CLAUDE.md — E1001 Zephyr Firmware

## What this does
Zephyr RTOS firmware for Seeed reTerminal E1001 (ESP32-S3).
Polls the FastAPI server for a pre-rendered PNG and drives the 7.5" UC8179 ePaper panel.

## Board target
  reterminal_e1001/esp32s3/procpu

## Build & flash (Arch Linux)
  cd ~/zephyrproject
  west build -b reterminal_e1001/esp32s3/procpu ~/Documents/reTerminal/firmware
  west flash --runner esptool
  # or manually:
  esptool.py -c esp32s3 -p /dev/ttyUSB0 write_flash 0x0 build/zephyr/zephyr.bin

## Zephyr prerequisites (one-time)
  sudo pacman -S cmake ninja python python-pip
  pip install west esptool --break-system-packages
  west init ~/zephyrproject
  cd ~/zephyrproject && west update && west zephyr-export
  west sdk install

## pngle library (one-time)
  git clone https://github.com/kikuchan/pngle lib/pngle
  # Only pngle.c and pngle.h are needed

## Serial monitor
  west espressif monitor
  # or: screen /dev/ttyUSB0 115200

## Key config — edit before first build
  src/config.h — WIFI_SSID, WIFI_PASSWORD, SERVER_HOST

## ePaper SPI pins (hardware fixed — do not change)
  CLK=GPIO7  MOSI=GPIO9  CS=GPIO10  DC=GPIO8  RST=GPIO47  BUSY=GPIO48

## Buttons
  Left=GPIO4  Right=GPIO5  Green=GPIO3 (manual full-refresh wakeup)

## Staged firmware builds
  Stage 1: minimal — Wi-Fi + fetch + display. No ETag, no PM, no scheduling.
  Stage 2: full state machine — ETag, PM sleep, schedule, green button wakeup.
