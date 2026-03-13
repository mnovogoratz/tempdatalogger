#pragma once
#include "esp_err.h"

// Start the WiFi soft-AP.  Call once after nvs_flash_init().
esp_err_t wifi_ap_start(void);

// Stop the soft-AP and release resources.
void wifi_ap_stop(void);
