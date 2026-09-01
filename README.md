# AroNet

ESP32-S3 touch display project built on the CrowPanel 5" Advance V1.0 hardware.

## Hardware

- **MCU**: ESP32-S3 (16MB flash, 8MB PSRAM)
- **Display**: 800×480 RGB LCD (ST7262 controller), 16-bit parallel, 18MHz pixel clock
- **Touch**: GT911 capacitive touch controller (I2C, up to 5 points)
- **Backlight**: Version-dependent (TCA9534 v1.0 / STC8H1K28 v1.1+)

## Project Origin

Display driver, touch driver, and ESP-IDF 6.0.2 setup copied from **EntalD1** (proven working configuration). All EntalD1-specific code (parameter system, EEZ Studio UI, ESP-NOW, WiFi, alarms, cloud, etc.) has been stripped out. This is a clean starting point.

## Build

Requires ESP-IDF 6.0.2.

```powershell
# Load ESP-IDF environment
& "$env:USERPROFILE\esp\v6.0.2\esp-idf\export.ps1"

# First time
idf.py set-target esp32s3
idf.py build

# Flash
idf.py -p COMx flash monitor
```

## System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    5x ESP32-S3 Displays                      │
│         (800×480 LVGL Touch Screens)                         │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐    │
│  │ DISPLAY  │  │ DISPLAY  │  │ DISPLAY  │  │ DISPLAY  │    │
│  │   01     │  │   02     │  │   03     │  │   04     │    │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘    │
└───────┼─────────────┼─────────────┼─────────────┼───────────┘
        │             │             │             │
        │ WiFi REST   │ WiFi REST   │ WiFi REST   │ WiFi REST
        │ Calls       │ Calls       │ Calls       │ Calls
        │             │             │             │
        └─────────────┴─────────────┴─────────────┘
                       │
                       ▼
        ┌──────────────────────────────┐
        │   Flask REST API Server      │
        │   (RPi 3A+ or localhost)     │
        │                              │
        │  GET  /api/devices/status   │
        │  POST /api/jobs/complete    │
        │  GET  /api/jobs             │
        │  POST /api/parts            │
        │  POST /api/products         │
        │  POST /api/operations       │
        │  GET  /api/dashboard/stats  │
        └──────────────┬───────────────┘
                       │
                       ▼
        ┌──────────────────────────────┐
        │  SQLite Database             │
        │                              │
        │  • Parts (inventory)         │
        │  • Operations (job types)    │
        │  • Products (SKUs)           │
        │  • Job Queue                 │
        │  • Device Status             │
        │  • Audit Log                 │
        └──────────────────────────────┘
                       ▲
                       │
                       │ Browser (HTTP)
                       │
        ┌──────────────────────────────┐
        │  HTML Web Dashboard          │
        │                              │
        │  • Add/edit parts            │
        │  • Define operations         │
        │  • Create products           │
        │  • Monitor jobs/devices      │
        │  • Real-time stats           │
        └──────────────────────────────┘
```

## Project Structure

```
AroNet/
├── CMakeLists.txt
├── README.md (this file)
│
├── main/                      # ESP32-S3 Firmware
│   ├── main.c                 # Entry point, LVGL tick/task loop
│   ├── aronet_config.h        # Hardware version, timing, firmware version
│   ├── aronet_device_client.h # API client header (device ↔ backend)
│   ├── aronet_device_gui.c    # LVGL GUI screens & event handlers
│   ├── display_driver.c/h     # RGB LCD panel init, backlight
│   ├── display_init.c/h       # Full display system init (LVGL + panel + touch)
│   ├── touch_driver.c/h       # GT911 I2C driver
│   ├── hw_version.c/h         # STC8H1K28 protocol detection
│   ├── screen_orientation.c/h # Standard / 180° flipped orientation
│   └── idf_component.yml      # LVGL 9.2 dependency
│
├── backend/                   # Flask REST API + SQLite
│   ├── app.py                 # Flask application with all endpoints
│   ├── database.py            # SQLite schema & initialization
│   ├── requirements.txt       # Python dependencies
│   ├── templates/
│   │   └── dashboard.html     # Web UI for inventory management
│   ├── README.md              # Backend setup instructions
│   ├── start.bat              # Windows startup script
│   └── start.sh               # Linux/Mac startup script
│
└── build/                     # ESP-IDF build output
```

## Quick Start - Today

### Backend (10 minutes)
```powershell
cd backend
./start.bat  # Windows
# Open browser: http://localhost:5000
```

### ESP32 Firmware (ongoing)
1. Review `main/aronet_device_client.h` - device API
2. Review `main/aronet_device_gui.c` - GUI example
3. Integrate into main.c
4. Flash & test
