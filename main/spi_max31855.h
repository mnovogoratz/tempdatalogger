#pragma once
#include <math.h>
#include "driver/spi_master.h"
#include "config.h"

// Initialise SPI bus and add all four MAX31855 devices
esp_err_t max31855_init(void);

// Read temperature for channel 0-3.
// Returns temperature in °C, or NAN on fault/open-circuit.
float max31855_read(int channel);

// Read all four channels into out[4]
void max31855_read_all(float out[NUM_CHANNELS]);
