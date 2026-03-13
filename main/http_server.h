#pragma once
#include "esp_err.h"

// Start the HTTP server.  Call after wifi_ap_start().
esp_err_t http_server_start(void);

// Stop the HTTP server.
void http_server_stop(void);
