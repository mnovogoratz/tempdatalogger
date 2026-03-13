#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "config.h"

// One log entry — all 4 channels + a seconds-since-boot timestamp
typedef struct {
    uint32_t timestamp_s;           // seconds since first boot (monotonic)
    float    temp[NUM_CHANNELS];    // °C; NAN = open circuit / fault
} log_entry_t;

// Initialise (call once on first boot; safe to call on every boot)
void     datalog_init(void);

// Append a new reading (overwrites oldest when full)
void     datalog_push(const float temps[NUM_CHANNELS]);

// Number of valid entries currently stored
uint16_t datalog_count(void);

// Read entry by index 0 = oldest, count-1 = newest
bool     datalog_get(uint16_t index, log_entry_t *out);

// Serialise entire log as a JSON string into buf (null-terminated)
// Returns number of bytes written (excl. null terminator)
int      datalog_to_json(char *buf, size_t buf_len);

// Serialise entire log as CSV into buf
int      datalog_to_csv(char *buf, size_t buf_len);
