# TC Datalogger

A self-contained 4-channel thermocouple datalogger built on the **ESP32-S3** using **ESP-IDF (C)**. The device logs temperatures hourly, spends most of its time in deep sleep to conserve power, and serves a live web dashboard over its own WiFi access point — no internet connection or home assistant required.

---

## Features

- **4-channel thermocouple input** via MAX31855 (SPI)
- **Hourly logging** with up to 168 entries (7 days) stored in RTC memory — survives deep sleep
- **Deep sleep** between readings (~20 µA idle current)
- **BOOT button** (GPIO0) wakes the device and resets a 10-minute active window
- **Built-in WiFi AP** — connect directly, no router needed
- **Web dashboard** at `http://192.168.4.1` with:
  - Live per-channel temperature cards
  - Line chart of historical readings
  - Timestamped readings table
  - CSV download

---

## Hardware

| Component | Details |
|---|---|
| Microcontroller | ESP32-S3-DevKitC |
| Thermocouple amplifiers | MAX31855 × 4 |
| Wake button | Onboard BOOT button (GPIO0) |
| Status LED | Onboard LED (GPIO2) |

### Pin Assignment

| Signal | GPIO |
|---|---|
| SPI CLK | GPIO18 |
| SPI MISO | GPIO19 |
| CS — TC1 | GPIO5 |
| CS — TC2 | GPIO17 |
| CS — TC3 | GPIO16 |
| CS — TC4 | GPIO4 |
| Wake button | GPIO0 (BOOT) |
| Status LED | GPIO2 |

### Wiring the MAX31855 Modules

Each MAX31855 module shares the CLK and MISO lines but has its own CS pin. The MAX31855 is **read-only** — MOSI is not needed.

```
ESP32-S3          MAX31855 (×4, each)
─────────         ───────────────────
GPIO18  ────────► CLK
GPIO19  ◄──────── MISO
GPIO5   ────────► CS   (TC1 only)
GPIO17  ────────► CS   (TC2 only)
GPIO16  ────────► CS   (TC3 only)
GPIO4   ────────► CS   (TC4 only)
3.3V    ────────► VCC
GND     ────────► GND
```

---

## Project Structure

```
datalogger/
├── CMakeLists.txt
├── sdkconfig.defaults
└── main/
    ├── CMakeLists.txt
    ├── config.h          # All tunable settings (pins, SSID, sleep durations)
    ├── main.c            # Entry point, wakeup detection, active window
    ├── spi_max31855.c/h  # SPI driver for MAX31855
    ├── datalog.c/h       # RTC ring buffer (survives deep sleep)
    ├── wifi_ap.c/h       # Soft access point
    └── http_server.c/h   # Web dashboard + JSON/CSV endpoints
```

---

## Prerequisites

- [VS Code](https://code.visualstudio.com/)
- [ESP-IDF Extension for VS Code](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension) (v5.x recommended)
- ESP32-S3-DevKitC board
- USB cable (data-capable, not charge-only)
- CP210x or CH340 USB driver (depends on your board's USB chip)

---

## Installation

### 1. Clone the repository

```bash
git clone https://github.com/YOUR_USERNAME/tc-datalogger.git
cd tc-datalogger
```

### 2. Install the ESP-IDF extension

In VS Code, open the Extensions panel (`Ctrl+Shift+X`), search for **ESP-IDF**, and install the Espressif Systems extension.

Then open the Command Palette (`Ctrl+Shift+P`) and run:
```
ESP-IDF: Configure ESP-IDF Extension
```
Choose **Express** setup and let it install ESP-IDF v5.x, the toolchain, and Python dependencies. This takes a few minutes on first run.

### 3. Open the project

```
File → Open Folder → select the datalogger/ folder
```

### 4. Set the target device

Click the chip icon in the VS Code status bar (bottom), or:
```
Command Palette → ESP-IDF: Set Espressif Device Target → esp32s3
```

### 5. Configure settings (optional)

All user-facing settings live in `main/config.h`. Edit before flashing:

```c
#define AP_SSID             "TC-Datalogger"   // WiFi network name
#define AP_PASSWORD         "logger1234"       // min 8 chars; "" for open AP
#define SLEEP_DURATION_MIN  60                 // log interval in minutes
#define AWAKE_DURATION_MIN  10                 // active window after button press
```

### 6. Build

Click the **build (cylinder) icon** in the status bar, or:
```
Command Palette → ESP-IDF: Build your Project
```

First build takes 1–3 minutes. A successful build ends with:
```
Project build complete.
```

### 7. Flash

Plug in your ESP32-S3 via USB. Select your COM port in the status bar, then click the **flash (lightning bolt) icon**, or:
```
Command Palette → ESP-IDF: Flash your Project → UART
```

### 8. Verify

Open the serial monitor (screen icon in status bar) to confirm the device booted correctly:
```
I (342) main: Wake cause: first boot
I (500) wifi_ap: AP started — SSID: TC-Datalogger  IP: 192.168.4.1
I (510) http: HTTP server running at http://192.168.4.1/
```

`FAULT` readings on all channels are expected until thermocouples are wired up.

---

## Usage

1. Press the **BOOT button** on the ESP32-S3-DevKitC to wake the device
2. On your phone or laptop, connect to WiFi network **`TC-Datalogger`**
3. Open a browser and navigate to **`http://192.168.4.1`**
   - Use `http://` explicitly — the device does not support HTTPS
   - On Android, if prompted about "no internet access", tap **Stay connected**
4. The dashboard loads with live readings, a chart, and the full log table
5. Use the **Download CSV** button to export all readings
6. After 10 minutes of inactivity the device returns to deep sleep and the AP disappears

---

## Device Behavior

```
First boot / timer wake
  └─► Read all 4 thermocouples
  └─► Store reading in RTC log
  └─► Start WiFi AP + HTTP server
  └─► Stay awake for 10 minutes
  └─► Return to deep sleep (60 min)

Button wake (BOOT / GPIO0)
  └─► Start WiFi AP + HTTP server
  └─► Stay awake for 10 minutes
  └─► Each button press resets the 10-min countdown
  └─► Return to deep sleep
```

---

## API Endpoints

| Endpoint | Description |
|---|---|
| `GET /` | HTML dashboard |
| `GET /data.json` | All log entries as JSON |
| `GET /data.csv` | All log entries as CSV download |

### JSON format

```json
{
  "entries": [
    { "t": 0,    "c": [24.5, 23.0, null, 25.1] },
    { "t": 3600, "c": [25.0, 23.5, 24.0, 25.5] }
  ]
}
```

`t` is seconds since first boot. `null` indicates an open-circuit or fault condition.

---

## Troubleshooting

**Dashboard doesn't load**
Make sure you're using `http://` not `https://`. Some browsers auto-upgrade — try Firefox or a private/incognito window.

**All channels show FAULT**
Expected if no thermocouples are connected. Check wiring if sensors are attached — verify CLK, MISO, and CS connections and that VCC is 3.3V not 5V.

**Button constantly triggers "awake window reset"**
The wake button GPIO may be floating. Ensure you're using GPIO0 (the BOOT button) as set in `config.h`, and that `GPIO_PULLUP_ENABLE` is configured (it is by default).

**Build error: `unknown type name 'gpio_num_t'`**
Ensure `main/config.h` has `#include "driver/gpio.h"` at the top.

**Device not detected on COM port**
You may need the USB driver for your board's USB-to-serial chip. Check Device Manager (Windows) for unknown devices and install the CP210x or CH340 driver accordingly.

---

## License

MIT
