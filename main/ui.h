#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "air.h"
#include "history.h"

#define UI_PAGE_COUNT 6

typedef struct {
    uint16_t page_dwell_s;
    uint8_t pages_mask;
    uint16_t chart_span_min;
    uint8_t brightness;
    uint8_t night_brightness;
    uint16_t screen_timeout_s;
} ui_config_t;

void ui_init(air_history_t *hist, const ui_config_t *cfg,
             void (*set_brightness)(uint8_t));

void ui_update(const air_sample_t *s);

void ui_set_status(const char *time_str, bool wifi, bool mqtt, const char *msg,
                   int bat_pct, bool charging);

void ui_set_ip(const char *ip);
void ui_tick_1s(void);
void ui_set_on_battery(bool on_battery);
void ui_idle_debug(uint32_t *idle_s, bool *dimmed, bool *idle_shown);
void ui_goto_page(int page);
void ui_set_live_brightness(uint8_t level);
void ui_wake(void);