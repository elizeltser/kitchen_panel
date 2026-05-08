cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(epaper_dashboard)

target_sources(app PRIVATE
  src/main.c
  src/wifi.c
  src/ntp.c
  src/http_fetch.c
  src/png_decode.c
  src/epaper.c
  src/schedule.c
  src/button.c
  src/nvs_store.c
  src/screensaver.c
  lib/pngle/pngle.c
  lib/pngle/miniz.c
)

target_include_directories(app PRIVATE src lib/pngle)
