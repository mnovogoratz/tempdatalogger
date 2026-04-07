#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "config.h"

// Initialise the 1-Wire bus and enumerate up to NUM_CHANNELS DS18B20 sensors.
// Returns ESP_OK if at least one sensor is found.
esp_err_t ds18b20_init(void);

// Trigger a temperature conversion on all sensors simultaneously,
// then block until the conversion is complete (up to 750 ms at 12-bit).
void ds18b20_convert_all(void);

// Read the converted temperature for sensor index (0-based).
// Returns temperature in °C, or NAN if the sensor is missing / CRC error.
float ds18b20_read(int index);

// Convenience: convert + read all channels into out[NUM_CHANNELS].
void ds18b20_read_all(float out[NUM_CHANNELS]);

// Number of sensors found during init.
int ds18b20_count(void);
