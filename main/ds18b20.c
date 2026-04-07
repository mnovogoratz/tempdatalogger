#include "ds18b20.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"
#include <math.h>
#include <string.h>

static const char *TAG = "ds18b20";

// ── ROM codes for each discovered sensor (8 bytes each) ──────────────────────
static uint8_t  s_rom[NUM_CHANNELS][8];
static int      s_count = 0;

// ── 1-Wire low-level primitives ───────────────────────────────────────────────
// The bus is open-drain: drive low by setting output-low; release by input mode.

static inline void bus_low(void)
{
    gpio_set_direction(ONEWIRE_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(ONEWIRE_GPIO, 0);
}

static inline void bus_release(void)
{
    gpio_set_direction(ONEWIRE_GPIO, GPIO_MODE_INPUT);
}

static inline int bus_read(void)
{
    return gpio_get_level(ONEWIRE_GPIO);
}

// Returns true if a presence pulse was detected (at least one device on bus).
static bool onewire_reset(void)
{
    bus_low();
    ets_delay_us(480);
    bus_release();
    ets_delay_us(70);
    int presence = !bus_read();   // device pulls low = present
    ets_delay_us(410);
    return presence;
}

static void onewire_write_bit(int bit)
{
    if (bit) {
        bus_low();
        ets_delay_us(6);
        bus_release();
        ets_delay_us(64);
    } else {
        bus_low();
        ets_delay_us(60);
        bus_release();
        ets_delay_us(10);
    }
}

static int onewire_read_bit(void)
{
    bus_low();
    ets_delay_us(6);
    bus_release();
    ets_delay_us(9);
    int bit = bus_read();
    ets_delay_us(55);
    return bit;
}

static void onewire_write_byte(uint8_t byte)
{
    for (int i = 0; i < 8; i++) {
        onewire_write_bit(byte & 1);
        byte >>= 1;
    }
}

static uint8_t onewire_read_byte(void)
{
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        byte |= (onewire_read_bit() << i);
    }
    return byte;
}

// ── CRC-8 (Dallas/Maxim) ─────────────────────────────────────────────────────
static uint8_t crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        for (int b = 0; b < 8; b++) {
            uint8_t mix = (crc ^ byte) & 0x01;
            crc >>= 1;
            if (mix) crc ^= 0x8C;
            byte >>= 1;
        }
    }
    return crc;
}

// ── ROM search (simplified single-pass, finds up to NUM_CHANNELS devices) ────
// Uses the 1-Wire Search ROM algorithm (command 0xF0).
static int search_rom(void)
{
    uint8_t rom[8];
    uint8_t last_discrepancy = 0;
    uint8_t last_rom[8] = {0};
    int found = 0;

    while (found < NUM_CHANNELS) {
        if (!onewire_reset()) {
            ESP_LOGW(TAG, "No presence pulse during search");
            break;
        }
        onewire_write_byte(0xF0); // SEARCH ROM

        uint8_t rom_byte_number = 0;
        uint8_t rom_byte_mask = 1;
        uint8_t last_zero = 0;
        memset(rom, 0, 8);

        for (int bit_number = 1; bit_number <= 64; bit_number++) {
            int id_bit     = onewire_read_bit();
            int comp_id_bit = onewire_read_bit();

            if (id_bit && comp_id_bit) {
                ESP_LOGW(TAG, "Search error at bit %d", bit_number);
                return found;
            }

            int search_dir;
            if (!id_bit && !comp_id_bit) {
                // Discrepancy
                if (bit_number < last_discrepancy) {
                    search_dir = (last_rom[rom_byte_number] & rom_byte_mask) ? 1 : 0;
                } else {
                    search_dir = (bit_number == last_discrepancy) ? 1 : 0;
                }
                if (!search_dir) last_zero = bit_number;
            } else {
                search_dir = id_bit;
            }

            if (search_dir) rom[rom_byte_number] |= rom_byte_mask;
            onewire_write_bit(search_dir);

            rom_byte_mask <<= 1;
            if (!rom_byte_mask) {
                rom_byte_number++;
                rom_byte_mask = 1;
            }
        }

        // Check CRC
        if (crc8(rom, 7) != rom[7]) {
            ESP_LOGW(TAG, "ROM CRC mismatch");
            break;
        }

        // Verify it's a DS18B20 (family code 0x28)
        if (rom[0] != 0x28) {
            ESP_LOGW(TAG, "Device family 0x%02X is not DS18B20 (0x28)", rom[0]);
        } else {
            memcpy(s_rom[found], rom, 8);
            ESP_LOGI(TAG, "Found DS18B20 #%d: %02X%02X%02X%02X%02X%02X%02X%02X",
                     found,
                     rom[0],rom[1],rom[2],rom[3],
                     rom[4],rom[5],rom[6],rom[7]);
            found++;
        }

        memcpy(last_rom, rom, 8);
        last_discrepancy = last_zero;
        if (!last_discrepancy) break; // all devices found
    }

    return found;
}

// ── Public API ────────────────────────────────────────────────────────────────

esp_err_t ds18b20_init(void)
{
    // Configure GPIO: input with pull-up (external 4.7kΩ recommended too)
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << ONEWIRE_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    if (!onewire_reset()) {
        ESP_LOGE(TAG, "No 1-Wire devices found on GPIO%d", ONEWIRE_GPIO);
        return ESP_ERR_NOT_FOUND;
    }

    s_count = search_rom();
    if (s_count == 0) {
        ESP_LOGE(TAG, "No DS18B20 sensors found");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "%d DS18B20 sensor(s) found", s_count);
    return ESP_OK;
}

void ds18b20_convert_all(void)
{
    // SKIP ROM (0xCC) broadcasts convert to all devices simultaneously
    onewire_reset();
    onewire_write_byte(0xCC); // SKIP ROM
    onewire_write_byte(0x44); // CONVERT T

    // Wait for conversion — 750 ms at 12-bit resolution
    // DS18B20 holds the line low while converting; poll or just wait.
    int wait_ms = (DS18B20_RESOLUTION == 9)  ?  94 :
                  (DS18B20_RESOLUTION == 10) ? 188 :
                  (DS18B20_RESOLUTION == 11) ? 375 : 750;
    vTaskDelay(pdMS_TO_TICKS(wait_ms + 10)); // +10ms margin
}

float ds18b20_read(int index)
{
    if (index >= s_count) return NAN;

    // Address specific sensor by ROM
    onewire_reset();
    onewire_write_byte(0x55);           // MATCH ROM
    for (int i = 0; i < 8; i++) {
        onewire_write_byte(s_rom[index][i]);
    }
    onewire_write_byte(0xBE);           // READ SCRATCHPAD

    uint8_t scratchpad[9];
    for (int i = 0; i < 9; i++) {
        scratchpad[i] = onewire_read_byte();
    }

    // Verify CRC
    if (crc8(scratchpad, 8) != scratchpad[8]) {
        ESP_LOGW(TAG, "Scratchpad CRC error on sensor %d", index);
        return NAN;
    }

    // Convert raw to °C — 16-bit two's complement, LSB = 0.0625°C
    int16_t raw = (int16_t)(scratchpad[1] << 8 | scratchpad[0]);

    // Mask unused bits based on resolution
    if (DS18B20_RESOLUTION == 9)       raw &= ~0x07;
    else if (DS18B20_RESOLUTION == 10) raw &= ~0x03;
    else if (DS18B20_RESOLUTION == 11) raw &= ~0x01;

    float temp = (float)raw / 16.0f;
    ESP_LOGD(TAG, "Sensor %d: %.4f°C (raw=0x%04X)", index, temp, (uint16_t)raw);
    return temp;
}

void ds18b20_read_all(float out[NUM_CHANNELS])
{
    ds18b20_convert_all();
    for (int i = 0; i < NUM_CHANNELS; i++) {
        out[i] = (i < s_count) ? ds18b20_read(i) : NAN;
    }
}

int ds18b20_count(void)
{
    return s_count;
}
