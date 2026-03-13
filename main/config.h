#pragma once
#include "driver/gpio.h"

// ── WiFi Access Point ────────────────────────────────────────────────────────
#define AP_SSID          "TC-Datalogger"
#define AP_PASSWORD      "logger1234"       // min 8 chars; set "" for open AP
#define AP_CHANNEL       1
#define AP_MAX_STA       4

// ── Deep Sleep ───────────────────────────────────────────────────────────────
#define SLEEP_DURATION_MIN   60             // wake to log every N minutes
#define AWAKE_DURATION_MIN   10             // stay awake N minutes after button
#define WAKE_BUTTON_GPIO     GPIO_NUM_0     // BOOT button on ESP32-S3-DevKitC

// ── SPI / MAX31855 ───────────────────────────────────────────────────────────
#define SPI_HOST         SPI2_HOST
#define PIN_CLK          GPIO_NUM_18
#define PIN_MISO         GPIO_NUM_19
#define PIN_CS_TC1       GPIO_NUM_5
#define PIN_CS_TC2       GPIO_NUM_17
#define PIN_CS_TC3       GPIO_NUM_16
#define PIN_CS_TC4       GPIO_NUM_4
#define SPI_FREQ_HZ      (5 * 1000 * 1000) // MAX31855 max 5 MHz

// ── Status LED ───────────────────────────────────────────────────────────────
#define LED_GPIO         GPIO_NUM_2

// ── Data Log ─────────────────────────────────────────────────────────────────
// Stored in RTC slow memory so it survives deep sleep
#define LOG_MAX_ENTRIES  168                // 7 days @ 1/hour
#define NUM_CHANNELS     4
