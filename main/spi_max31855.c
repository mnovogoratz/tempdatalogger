#include "spi_max31855.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "max31855";

// CS pin for each channel
static const gpio_num_t cs_pins[NUM_CHANNELS] = {
    PIN_CS_TC1, PIN_CS_TC2, PIN_CS_TC3, PIN_CS_TC4
};

static spi_device_handle_t s_devs[NUM_CHANNELS];

esp_err_t max31855_init(void)
{
    // Configure the SPI bus (CLK + MISO only; MAX31855 is read-only)
    spi_bus_config_t bus_cfg = {
        .miso_io_num   = PIN_MISO,
        .mosi_io_num   = -1,            // not used
        .sclk_io_num   = PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4,
    };
    esp_err_t ret = spi_bus_initialize(SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Add one device per channel with its own CS pin
    for (int i = 0; i < NUM_CHANNELS; i++) {
        spi_device_interface_config_t dev_cfg = {
            .clock_speed_hz = SPI_FREQ_HZ,
            .mode           = 0,            // CPOL=0, CPHA=0
            .spics_io_num   = cs_pins[i],
            .queue_size     = 1,
            .flags          = 0,
        };
        ret = spi_bus_add_device(SPI_HOST, &dev_cfg, &s_devs[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Add device %d failed: %s", i, esp_err_to_name(ret));
            return ret;
        }
        ESP_LOGI(TAG, "MAX31855 ch%d on CS GPIO%d", i, cs_pins[i]);
    }
    return ESP_OK;
}

// MAX31855 32-bit frame:
//  [31:18] Thermocouple temp (14-bit, 0.25°C LSB, sign-extended)
//  [17]    Reserved
//  [16]    Fault flag
//  [15:4]  Internal (junction) temp
//  [3]     Reserved
//  [2]     SCV fault (short to VCC)
//  [1]     SCG fault (short to GND)
//  [0]     OC fault  (open circuit)
float max31855_read(int channel)
{
    if (channel < 0 || channel >= NUM_CHANNELS) return NAN;

    uint8_t rx[4] = {0};
    spi_transaction_t txn = {
        .length    = 32,
        .rxlength  = 32,
        .rx_buffer = rx,
    };

    esp_err_t ret = spi_device_transmit(s_devs[channel], &txn);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI transmit ch%d failed: %s", channel, esp_err_to_name(ret));
        return NAN;
    }

    uint32_t raw = ((uint32_t)rx[0] << 24) |
                   ((uint32_t)rx[1] << 16) |
                   ((uint32_t)rx[2] << 8)  |
                    (uint32_t)rx[3];

    // Check fault bit (bit 16)
    if (raw & (1 << 16)) {
        uint8_t fault = raw & 0x07;
        ESP_LOGW(TAG, "ch%d fault: OC=%d SCG=%d SCV=%d",
                 channel,
                 (fault >> 0) & 1,
                 (fault >> 1) & 1,
                 (fault >> 2) & 1);
        return NAN;
    }

    // Extract 14-bit thermocouple temperature (bits 31..18)
    int32_t tc_raw = (int32_t)(raw >> 18);
    // Sign-extend from 14 bits
    if (tc_raw & (1 << 13)) {
        tc_raw |= ~((1 << 14) - 1);
    }
    float temp_c = (float)tc_raw * 0.25f;
    ESP_LOGD(TAG, "ch%d = %.2f°C (raw=0x%08lX)", channel, temp_c, (unsigned long)raw);
    return temp_c;
}

void max31855_read_all(float out[NUM_CHANNELS])
{
    for (int i = 0; i < NUM_CHANNELS; i++) {
        out[i] = max31855_read(i);
    }
}
