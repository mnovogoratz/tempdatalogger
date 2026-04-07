#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "config.h"
#include "datalog.h"
#include "ds18b20.h"
#include "wifi_ap.h"
#include "http_server.h"

static const char *TAG = "main";

// ── LED helpers ───────────────────────────────────────────────────────────────
static void led_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
}
static void led_set(int on) { gpio_set_level(LED_GPIO, on); }

// ── Deep sleep entry ──────────────────────────────────────────────────────────
static void go_to_sleep(void)
{
    ESP_LOGI(TAG, "Entering deep sleep for %d min", SLEEP_DURATION_MIN);
    led_set(0);
    http_server_stop();
    wifi_ap_stop();

    esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_DURATION_MIN * 60ULL * 1000000ULL);
    // Original ESP32 uses ext0 for single-pin GPIO wakeup (active-low)
    esp_sleep_enable_ext0_wakeup(WAKE_BUTTON_GPIO, 0);

    esp_deep_sleep_start();
    // does not return
}

// ── Main ──────────────────────────────────────────────────────────────────────
void app_main(void)
{
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    led_init();
    led_set(1);

    // Configure wake button with pull-up (BOOT button is active-low)
    gpio_config_t btn_io = {
        .pin_bit_mask = (1ULL << WAKE_BUTTON_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_io);

    // ── Determine why we woke ────────────────────────────────────────────────
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    bool woke_by_button = (cause == ESP_SLEEP_WAKEUP_EXT0);
    bool woke_by_timer  = (cause == ESP_SLEEP_WAKEUP_TIMER);
    bool first_boot     = (cause == ESP_SLEEP_WAKEUP_UNDEFINED);

    ESP_LOGI(TAG, "Wake cause: %s",
             first_boot     ? "first boot"  :
             woke_by_button ? "button"      :
             woke_by_timer  ? "timer"       : "other");

    // ── Init data log ────────────────────────────────────────────────────────
    datalog_init();

    // ── Read + store temperatures on timer wake or first boot ────────────────
    if (woke_by_timer || first_boot) {
        esp_err_t ret = ds18b20_init();
        if (ret == ESP_OK) {
            float temps[NUM_CHANNELS];
            ds18b20_read_all(temps);
            datalog_push(temps);

            for (int i = 0; i < NUM_CHANNELS; i++) {
                if (isnan(temps[i])) {
                    ESP_LOGW(TAG, "Sensor %d: not found / error", i + 1);
                } else {
                    ESP_LOGI(TAG, "Sensor %d: %.4f°C", i + 1, temps[i]);
                }
            }
        } else {
            ESP_LOGE(TAG, "DS18B20 init failed — logging NaN for this cycle");
            float nans[NUM_CHANNELS];
            for (int i = 0; i < NUM_CHANNELS; i++) nans[i] = NAN;
            datalog_push(nans);
        }
    }

    // ── Decide awake duration based on wake cause ────────────────────────────
    if (woke_by_timer && !woke_by_button) {
        // Timer-only wake: just log and go back to sleep after 5 seconds.
        // No AP or HTTP server needed — saves power and is faster.
        ESP_LOGI(TAG, "Timer wake: sleeping again in %d seconds",
                 AWAKE_DURATION_TIMER_S);
        vTaskDelay(pdMS_TO_TICKS(AWAKE_DURATION_TIMER_S * 1000));
        go_to_sleep();
        return; // unreachable, but satisfies compiler
    }

    // ── Button wake (or first boot): start AP + HTTP for user access ─────────
    ESP_ERROR_CHECK(wifi_ap_start());
    ESP_ERROR_CHECK(http_server_start());

    int64_t awake_until_us = esp_timer_get_time()
                           + (int64_t)AWAKE_DURATION_BUTTON_MIN * 60LL * 1000000LL;

    ESP_LOGI(TAG, "Active for %d min. Connect to SSID '%s' → http://192.168.4.1/",
             AWAKE_DURATION_BUTTON_MIN, AP_SSID);

    while (esp_timer_get_time() < awake_until_us) {
        // Debounced button check — each confirmed press resets the timer
        if (gpio_get_level(WAKE_BUTTON_GPIO) == 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            if (gpio_get_level(WAKE_BUTTON_GPIO) == 0) {
                awake_until_us = esp_timer_get_time()
                               + (int64_t)AWAKE_DURATION_BUTTON_MIN * 60LL * 1000000LL;
                ESP_LOGI(TAG, "Button pressed — awake window reset");
                while (gpio_get_level(WAKE_BUTTON_GPIO) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    go_to_sleep();
}
