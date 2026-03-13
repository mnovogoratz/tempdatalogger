#include "datalog.h"
#include "esp_attr.h"
#include "esp_log.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "datalog";

// ── RTC slow-memory storage (survives deep sleep) ────────────────────────────
static RTC_DATA_ATTR log_entry_t s_entries[LOG_MAX_ENTRIES];
static RTC_DATA_ATTR uint16_t    s_head;        // next write position
static RTC_DATA_ATTR uint16_t    s_count;        // valid entries (0..LOG_MAX)
static RTC_DATA_ATTR uint32_t    s_elapsed_s;    // monotonic seconds counter
static RTC_DATA_ATTR uint8_t     s_init_flag;    // 0xAB when initialised

#define INIT_MAGIC 0xAB

void datalog_init(void)
{
    if (s_init_flag != INIT_MAGIC) {
        memset(s_entries, 0, sizeof(s_entries));
        s_head      = 0;
        s_count     = 0;
        s_elapsed_s = 0;
        s_init_flag = INIT_MAGIC;
        ESP_LOGI(TAG, "Log initialised (first boot)");
    } else {
        // Advance elapsed time by one sleep period on each scheduled wake
        s_elapsed_s += (SLEEP_DURATION_MIN * 60);
        ESP_LOGI(TAG, "Log resumed — %u entries, elapsed %lu s",
                 s_count, (unsigned long)s_elapsed_s);
    }
}

void datalog_push(const float temps[NUM_CHANNELS])
{
    log_entry_t *e = &s_entries[s_head];
    e->timestamp_s = s_elapsed_s;
    for (int i = 0; i < NUM_CHANNELS; i++) {
        e->temp[i] = temps[i];
    }

    s_head = (s_head + 1) % LOG_MAX_ENTRIES;
    if (s_count < LOG_MAX_ENTRIES) s_count++;

    ESP_LOGI(TAG, "Pushed entry @ %lu s: %.1f %.1f %.1f %.1f",
             (unsigned long)s_elapsed_s,
             temps[0], temps[1], temps[2], temps[3]);
}

uint16_t datalog_count(void)
{
    return s_count;
}

bool datalog_get(uint16_t index, log_entry_t *out)
{
    if (index >= s_count) return false;
    // index 0 = oldest entry
    uint16_t real_idx;
    if (s_count < LOG_MAX_ENTRIES) {
        real_idx = index;
    } else {
        // buffer is full — oldest is at s_head
        real_idx = (s_head + index) % LOG_MAX_ENTRIES;
    }
    *out = s_entries[real_idx];
    return true;
}

// ── Serialisers ──────────────────────────────────────────────────────────────

int datalog_to_json(char *buf, size_t buf_len)
{
    int pos = 0;
    pos += snprintf(buf + pos, buf_len - pos, "{\"entries\":[");

    for (uint16_t i = 0; i < s_count; i++) {
        log_entry_t e;
        datalog_get(i, &e);

        // Format each channel — use null for NaN (fault/open)
        char ch[NUM_CHANNELS][16];
        for (int c = 0; c < NUM_CHANNELS; c++) {
            if (isnan(e.temp[c])) {
                snprintf(ch[c], sizeof(ch[c]), "null");
            } else {
                snprintf(ch[c], sizeof(ch[c]), "%.1f", e.temp[c]);
            }
        }

        pos += snprintf(buf + pos, buf_len - pos,
            "%s{\"t\":%lu,\"c\":[%s,%s,%s,%s]}",
            (i > 0 ? "," : ""),
            (unsigned long)e.timestamp_s,
            ch[0], ch[1], ch[2], ch[3]);

        if (pos >= (int)(buf_len - 64)) break; // safety margin
    }

    pos += snprintf(buf + pos, buf_len - pos, "]}");
    return pos;
}

int datalog_to_csv(char *buf, size_t buf_len)
{
    int pos = 0;
    pos += snprintf(buf + pos, buf_len - pos,
                    "time_s,TC1_C,TC2_C,TC3_C,TC4_C\r\n");

    for (uint16_t i = 0; i < s_count; i++) {
        log_entry_t e;
        datalog_get(i, &e);

        pos += snprintf(buf + pos, buf_len - pos, "%lu",
                        (unsigned long)e.timestamp_s);
        for (int c = 0; c < NUM_CHANNELS; c++) {
            if (isnan(e.temp[c])) {
                pos += snprintf(buf + pos, buf_len - pos, ",");
            } else {
                pos += snprintf(buf + pos, buf_len - pos, ",%.1f", e.temp[c]);
            }
        }
        pos += snprintf(buf + pos, buf_len - pos, "\r\n");
        if (pos >= (int)(buf_len - 64)) break;
    }
    return pos;
}
