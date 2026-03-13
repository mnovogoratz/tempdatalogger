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
#include "spi_max31855.h"
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

    // Wake sources: timer (scheduled log) + GPIO button
    // ESP32-S3 requires ext1 for GPIO wakeup (ext0 not supported)
    esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_DURATION_MIN * 60ULL * 1000000ULL);
    esp_sleep_enable_ext1_wakeup(1ULL << WAKE_BUTTON_GPIO, ESP_EXT1_WAKEUP_ANY_LOW);

    esp_deep_sleep_start();
    // does not return
}

// ── Main ──────────────────────────────────────────────────────────────────────
void app_main(void)
{
    // NVS required by WiFi
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    led_init();
    led_set(1); // LED on while awake

    // Configure wake button pin with pull-up (BOOT button is active-low)
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
    bool woke_by_button = (cause == ESP_SLEEP_WAKEUP_EXT1);
    bool woke_by_timer  = (cause == ESP_SLEEP_WAKEUP_TIMER);
    bool first_boot     = (cause == ESP_SLEEP_WAKEUP_UNDEFINED);

    ESP_LOGI(TAG, "Wake cause: %s",
             first_boot    ? "first boot" :
             woke_by_button ? "button"    :
             woke_by_timer  ? "timer"     : "other");

    // ── Init data log (RTC memory — survives sleep) ──────────────────────────
    datalog_init();

    // ── Read + store temperatures (on timer wake or first boot) ─────────────
    if (woke_by_timer || first_boot) {
        ESP_ERROR_CHECK(max31855_init());

        float temps[NUM_CHANNELS];
        max31855_read_all(temps);
        datalog_push(temps);

        for (int i = 0; i < NUM_CHANNELS; i++) {
            if (isnan(temps[i])) {
                ESP_LOGW(TAG, "TC%d: FAULT", i + 1);
            } else {
                ESP_LOGI(TAG, "TC%d: %.2f°C", i + 1, temps[i]);
            }
        }
    }

    // ── Start AP + HTTP ──────────────────────────────────────────────────────
    //   Always start so user can connect and view data after button press.
    //   On timer-only wakes without button, we still briefly serve for
    //   AWAKE_DURATION_MIN then go back to sleep.
    ESP_ERROR_CHECK(wifi_ap_start());
    ESP_ERROR_CHECK(http_server_start());

    // ── Active window countdown ───────────────────────────────────────────────
    // Stay awake for AWAKE_DURATION_MIN, then sleep again.
    // Button presses extend the window (handled by a simple loop here).
    int64_t awake_until_us = esp_timer_get_time()
                           + (int64_t)AWAKE_DURATION_MIN * 60LL * 1000000LL;

    ESP_LOGI(TAG, "Active for %d min. Connect to SSID '%s' → http://192.168.4.1/",
             AWAKE_DURATION_MIN, AP_SSID);

    while (esp_timer_get_time() < awake_until_us) {
        // Check if button is pressed to extend window
        // Debounce: only extend if held LOW for 50ms
        if (gpio_get_level(WAKE_BUTTON_GPIO) == 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            if (gpio_get_level(WAKE_BUTTON_GPIO) == 0) {
                awake_until_us = esp_timer_get_time()
                               + (int64_t)AWAKE_DURATION_MIN * 60LL * 1000000LL;
                ESP_LOGI(TAG, "Button pressed — awake window reset");
                // Wait for release before checking again
                while (gpio_get_level(WAKE_BUTTON_GPIO) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000)); // check every second
    }

    go_to_sleep();
}
