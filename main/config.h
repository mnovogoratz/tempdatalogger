#pragma once
#include "driver/gpio.h"

// ── WiFi Access Point ────────────────────────────────────────────────────────
#define AP_SSID          "TC-Datalogger"
#define AP_PASSWORD      "logger1234"       // min 8 chars; "" for open AP
#define AP_CHANNEL       1
#define AP_MAX_STA       4

// ── Deep Sleep ───────────────────────────────────────────────────────────────
#define SLEEP_DURATION_MIN        60        // wake to log every N minutes
#define AWAKE_DURATION_TIMER_S     5        // seconds awake on scheduled (timer) wake
#define AWAKE_DURATION_BUTTON_MIN 10        // minutes awake on button wake
#define WAKE_BUTTON_GPIO     GPIO_NUM_0     // BOOT button on ESP32-S3-DevKitC

// ── 1-Wire / DS18B20 ─────────────────────────────────────────────────────────
#define ONEWIRE_GPIO         GPIO_NUM_4     // single data wire, all 4 sensors
#define DS18B20_RESOLUTION   12            // bits: 9=94ms, 10=188ms, 11=375ms, 12=750ms

// ── Status LED ───────────────────────────────────────────────────────────────
#define LED_GPIO             GPIO_NUM_2

// ── Data Log ─────────────────────────────────────────────────────────────────
#define LOG_MAX_ENTRIES  168               // 7 days @ 1/hour
#define NUM_CHANNELS     4
