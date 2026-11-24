#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "fonts/fonts.h"
#include "i18n.h"

#define SCR_W 466
#define SCR_H 466
#define CHART_POINTS 60

#define EDGE 10
#define UI_X_OFFSET 4
#define RING_D 446
#define RING_INNER_R ((RING_D) / 2 - 22)

#define FADE_DIM_MS     500
#define FADE_WAKE_MS    250
#define BURNIN_PERIOD_S 60

enum { PAGE_SUMMARY = 0, PAGE_CO2, PAGE_PM, PAGE_GAS, PAGE_CLIMATE, PAGE_NOISE };

typedef struct {
    lv_obj_t *arc;
    lv_obj_t *value;
    lv_obj_t *unit;
    lv_obj_t *caption;
} gauge_t;

static air_history_t *s_hist;
static ui_config_t s_cfg;
static void (*s_set_brightness)(uint8_t);

static lv_obj_t *s_root;
static lv_obj_t *s_tv;
static lv_obj_t *s_status_time;
static lv_obj_t *s_status_msg;
static lv_obj_t *s_status_net;
static lv_obj_t *s_status_bat;
static lv_obj_t *s_dim_overlay;
static uint8_t s_cur_brightness = 255;

static lv_obj_t *s_idle_panel;
static lv_obj_t *s_idle_val[AIR_METRIC_COUNT];
static lv_obj_t *s_idle_level;
static lv_obj_t *s_idle_noise_name;
static lv_obj_t *s_idle_ip;
static bool s_idle_shown;
static lv_obj_t *s_dots[UI_PAGE_COUNT];

static int s_page_col[UI_PAGE_COUNT];
static int s_col_page[UI_PAGE_COUNT];
static int s_cols;
static int s_cur_col;

static gauge_t s_g_summary, s_g_co2, s_g_pm, s_g_voc, s_g_nox, s_g_noise;
static lv_obj_t *s_sum_center, *s_sum_sub;
static lv_obj_t *s_sum_name[4], *s_sum_val[4];
static lv_obj_t *s_pm_level, *s_pm_name[3], *s_pm_val[3];
static lv_obj_t *s_co2_level, *s_co2_chart;
static lv_chart_series_t *s_co2_ser;
static lv_obj_t *s_pm_chart;
static lv_chart_series_t *s_pm_ser;
static lv_obj_t *s_noise_level, *s_noise_chart;
static lv_chart_series_t *s_noise_ser;
static lv_obj_t *s_cl_temp, *s_cl_hum, *s_cl_chart;
static lv_chart_series_t *s_cl_ser, *s_cl_ser_hum;
static lv_obj_t *s_gas_chart;
static lv_chart_series_t *s_gas_ser_voc, *s_gas_ser_nox;

static uint32_t s_idle_s;
static bool s_dimmed;
static bool s_on_battery;
static uint32_t s_burnin_s;
static uint8_t s_burnin_step;
#define UI_BATTERY_TIMEOUT_S 30
static air_sample_t s_last;

// ------------------------------------------------------------------ helpers
static lv_color_t col(uint32_t rgb) { return lv_color_hex(rgb); }

static void burnin_reset(void)
{
    s_burnin_s = 0;
    if (s_burnin_step != 0) {
        s_burnin_step = 0;
        if (s_root) lv_obj_set_pos(s_root, 0, 0);
    }
}

static void apply_brightness_immediate(uint8_t level)
{
    s_cur_brightness = level;
    if (s_dim_overlay) {
        lv_opa_t opa = (level == 0) ? LV_OPA_COVER : (lv_opa_t)(255 - level);
        lv_obj_set_style_bg_opa(s_dim_overlay, opa, 0);
    }
    if (s_set_brightness) {
        s_set_brightness(level);
    }
}

static void fade_exec_cb(void *var, int32_t v)
{
    (void)var;
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    uint8_t lvl = (uint8_t)v;
    if (lvl == s_cur_brightness) return;
    apply_brightness_immediate(lvl);
}

static void fade_brightness(uint8_t target, uint32_t duration_ms)
{
    lv_anim_delete(&s_cur_brightness, fade_exec_cb);
    if (duration_ms == 0 || s_cur_brightness == target) {
        apply_brightness_immediate(target);
        return;
    }

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &s_cur_brightness);
    lv_anim_set_exec_cb(&a, fade_exec_cb);
    lv_anim_set_values(&a, s_cur_brightness, target);
    lv_anim_set_duration(&a, duration_ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void fmt_value(char *buf, size_t len, air_metric_t m, float v)
{
    if (isnan(v)) { snprintf(buf, len, "--"); return; }
    snprintf(buf, len, "%.*f", air_metric_decimals(m), v);
}

static lv_obj_t *plain(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_scrollable(o, false);
    return o;
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col(color), 0);
    lv_label_set_text(l, "");
    return l;
}

static void mini_create(lv_obj_t *parent, lv_obj_t **name, lv_obj_t **value,
                        int dx, int dy, const lv_font_t *font_value)
{
    *name = make_label(parent, &ui_font_22, 0x64748b);
    lv_obj_set_style_text_align(*name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(*name, LV_ALIGN_CENTER, dx + UI_X_OFFSET, dy);
    *value = make_label(parent, font_value, 0xffffff);
    lv_obj_set_style_text_align(*value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(*value, LV_ALIGN_CENTER, dx + UI_X_OFFSET, dy + 26);
}

static void gauge_create(gauge_t *g, lv_obj_t *parent, int size, int thickness,
                         const lv_font_t *font_value, const lv_font_t *font_small)
{
    g->arc = lv_arc_create(parent);
    lv_obj_set_size(g->arc, size, size);
    lv_obj_align(g->arc, LV_ALIGN_CENTER, UI_X_OFFSET, 0);
    lv_arc_set_rotation(g->arc, 0);
    lv_arc_set_bg_angles(g->arc, 135, 45);
    lv_arc_set_range(g->arc, 0, 1000);
    lv_arc_set_value(g->arc, 0);
    lv_obj_remove_style(g->arc, NULL, LV_PART_KNOB);
    lv_obj_set_clickable(g->arc, false);
    lv_obj_set_style_arc_width(g->arc, thickness, LV_PART_MAIN);
    lv_obj_set_style_arc_width(g->arc, thickness, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g->arc, col(0x1e293b), LV_PART_MAIN);
    lv_obj_set_style_arc_color(g->arc, col(0x22c55e), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(g->arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(g->arc, true, LV_PART_INDICATOR);

    g->value = make_label(parent, font_value, 0xffffff);
    lv_obj_align(g->value, LV_ALIGN_CENTER, UI_X_OFFSET, -10);
    g->unit = make_label(parent, font_small, 0x94a3b8);
    lv_obj_align_to(g->unit, g->value, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
    g->caption = make_label(parent, font_small, 0x64748b);
    lv_obj_align(g->caption, LV_ALIGN_CENTER, UI_X_OFFSET, -size / 2 + thickness + 22);
}

static void gauge_update(gauge_t *g, air_metric_t m, float v)
{
    const air_level_t lvl = air_level(m, v);
    lv_obj_set_style_arc_color(g->arc, col(air_level_color(lvl)), LV_PART_INDICATOR);

    const float lo = air_gauge_min(m), hi = air_gauge_max(m);
    int32_t pos = 0;
    if (!isnan(v) && hi > lo) {
        float f = (v - lo) / (hi - lo);
        if (f < 0) f = 0;
        if (f > 1) f = 1;
        pos = (int32_t)(f * 1000.0f);
    }
    lv_arc_set_value(g->arc, pos);

    char buf[16];
    fmt_value(buf, sizeof(buf), m, v);
    lv_label_set_text(g->value, buf);
    lv_label_set_text(g->unit, air_metric_unit_ui(m));
    lv_label_set_text(g->caption, i18n_metric(m));
    lv_obj_align_to(g->unit, g->value, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
}

static void chart_create(lv_obj_t **out_chart, lv_chart_series_t **out_ser,
                         lv_obj_t *parent, int w, int h, uint32_t color)
{
    lv_obj_t *c = lv_chart_create(parent);
    lv_obj_set_size(c, w, h);
    lv_chart_set_type(c, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(c, CHART_POINTS);
    lv_chart_set_div_line_count(c, 0, 0);
    lv_chart_set_update_mode(c, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_set_style_line_color(c, col(0x1e293b), LV_PART_MAIN);
    lv_obj_set_style_line_width(c, 2, LV_PART_ITEMS);
    lv_obj_set_style_width(c, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(c, 0, LV_PART_INDICATOR);
    lv_obj_set_clickable(c, false);
    *out_ser = lv_chart_add_series(c, col(color), LV_CHART_AXIS_PRIMARY_Y);
    *out_chart = c;
}

static lv_chart_series_t *chart_add_secondary(lv_obj_t *c, uint32_t color)
{
    return lv_chart_add_series(c, col(color), LV_CHART_AXIS_SECONDARY_Y);
}

static void chart_refresh_axis(lv_obj_t *chart, lv_chart_series_t *ser,
                               air_metric_t m, lv_chart_axis_t axis)
{
    if (!s_hist) return;
    const uint32_t span = (uint32_t)s_cfg.chart_span_min * 60;

    float pts[CHART_POINTS];
    air_history_series(s_hist, m, span, pts, CHART_POINTS);

    float lo, hi;
    if (!air_history_range(s_hist, m, span, &lo, &hi)) {
        lo = air_gauge_min(m);
        hi = air_gauge_max(m);
    }
    const float k = air_metric_decimals(m) ? 10.0f : 1.0f;

    float pad = (hi - lo) * 0.1f;
    if (pad < 0.5f) pad = 0.5f;
    lv_chart_set_range(chart, axis,
                       (int32_t)floorf((lo - pad) * k),
                       (int32_t)ceilf((hi + pad) * k));

    for (int i = 0; i < CHART_POINTS; i++) {
        lv_chart_set_value_by_id(chart, ser, i,
                                 isnan(pts[i]) ? LV_CHART_POINT_NONE
                                               : (int32_t)lrintf(pts[i] * k));
    }
}

static void chart_refresh(lv_obj_t *chart, lv_chart_series_t *ser, air_metric_t m)
{
    chart_refresh_axis(chart, ser, m, LV_CHART_AXIS_PRIMARY_Y);
}

// -------------------------------------------------------------------- Seiten
static void build_summary(lv_obj_t *tile)
{
    gauge_create(&s_g_summary, tile, RING_D, 22,
                 &ui_font_48, &ui_font_28);
    lv_obj_set_hidden(s_g_summary.value, true);
    lv_obj_set_hidden(s_g_summary.unit, true);
    lv_obj_set_hidden(s_g_summary.caption, true);
    lv_obj_set_style_arc_width(s_g_summary.arc, 22, LV_PART_INDICATOR);

    s_sum_center = make_label(tile, &ui_font_48, 0xffffff);
    lv_obj_align(s_sum_center, LV_ALIGN_CENTER, UI_X_OFFSET, -76);
    s_sum_sub = make_label(tile, &ui_font_28, 0x94a3b8);
    lv_obj_align(s_sum_sub, LV_ALIGN_CENTER, UI_X_OFFSET, -32);

    static const int dx[4] = {-96, 96, -96, 96};
    static const int dy[4] = {18, 18, 86, 86};
    for (int i = 0; i < 4; i++) {
        mini_create(tile, &s_sum_name[i], &s_sum_val[i], dx[i], dy[i],
                    &ui_font_28);
    }
}

static void build_co2(lv_obj_t *tile)
{
    chart_create(&s_co2_chart, &s_co2_ser, tile, 430, 90, 0x38bdf8);
    lv_obj_align(s_co2_chart, LV_ALIGN_CENTER, UI_X_OFFSET, 108);

    gauge_create(&s_g_co2, tile, RING_D, 22,
                 &ui_font_48, &ui_font_28);
    lv_obj_set_style_text_font(s_g_co2.caption, &ui_font_28, 0);
    lv_obj_align(s_g_co2.caption, LV_ALIGN_CENTER, UI_X_OFFSET, -78);
    lv_obj_align(s_g_co2.value, LV_ALIGN_CENTER, UI_X_OFFSET, -22);

    s_co2_level = make_label(tile, &ui_font_28, 0xffffff);
    lv_obj_align(s_co2_level, LV_ALIGN_CENTER, UI_X_OFFSET, 48);
}

#define PM_COL 0x2dd4bf

static void build_pm(lv_obj_t *tile)
{
    chart_create(&s_pm_chart, &s_pm_ser, tile, 430, 90, PM_COL);
    lv_obj_align(s_pm_chart, LV_ALIGN_CENTER, UI_X_OFFSET, 108);

    gauge_create(&s_g_pm, tile, RING_D, 22,
                 &ui_font_48, &ui_font_28);
    lv_obj_set_style_text_font(s_g_pm.caption, &ui_font_28, 0);
    lv_obj_align(s_g_pm.caption, LV_ALIGN_CENTER, UI_X_OFFSET, -82);
    lv_obj_align(s_g_pm.value, LV_ALIGN_CENTER, UI_X_OFFSET, -26);

    s_pm_level = make_label(tile, &ui_font_28, 0xffffff);
    lv_obj_align(s_pm_level, LV_ALIGN_CENTER, UI_X_OFFSET, 44);

    static const int dx[3] = {-114, 0, 114};
    for (int i = 0; i < 3; i++) {
        mini_create(tile, &s_pm_name[i], &s_pm_val[i], dx[i], 76,
                    &ui_font_28);
        for (lv_obj_t *o = s_pm_name[i]; o; o = (o == s_pm_name[i]) ? s_pm_val[i] : NULL) {
            lv_obj_set_style_bg_color(o, col(0x000000), 0);
            lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
            lv_obj_set_style_pad_hor(o, 5, 0);
        }
    }
}

#define GAS_COL_VOC 0xa78bfa
#define GAS_COL_NOX 0x38bdf8

static void build_gas(lv_obj_t *tile)
{
    chart_create(&s_gas_chart, &s_gas_ser_voc, tile, 430, 90, GAS_COL_VOC);
    s_gas_ser_nox = chart_add_secondary(s_gas_chart, GAS_COL_NOX);
    lv_obj_align(s_gas_chart, LV_ALIGN_CENTER, UI_X_OFFSET, 108);

    lv_obj_t *title = make_label(tile, &ui_font_28, 0x94a3b8);
    lv_label_set_text(title, T(STR_PAGE_GASES));
    lv_obj_align(title, LV_ALIGN_CENTER, UI_X_OFFSET, -88);

    lv_obj_t *left = plain(tile);
    lv_obj_set_size(left, 196, 196);
    lv_obj_align(left, LV_ALIGN_CENTER, -106 + UI_X_OFFSET, 14);
    gauge_create(&s_g_voc, left, 188, 18, &ui_font_48, &ui_font_22);
    lv_obj_set_style_text_color(s_g_voc.caption, col(GAS_COL_VOC), 0);
    lv_obj_set_style_text_color(s_g_voc.value, col(GAS_COL_VOC), 0);

    lv_obj_t *right = plain(tile);
    lv_obj_set_size(right, 196, 196);
    lv_obj_align(right, LV_ALIGN_CENTER, 106 + UI_X_OFFSET, 14);
    gauge_create(&s_g_nox, right, 188, 18, &ui_font_48, &ui_font_22);
    lv_obj_set_style_text_color(s_g_nox.caption, col(GAS_COL_NOX), 0);
    lv_obj_set_style_text_color(s_g_nox.value, col(GAS_COL_NOX), 0);

    lv_obj_t *hint = make_label(tile, &ui_font_22, 0x64748b);
    lv_label_set_text(hint, T(STR_GAS_HINT));
    lv_obj_align(hint, LV_ALIGN_CENTER, UI_X_OFFSET, 122);
    lv_obj_set_style_bg_color(hint, col(0x000000), 0);
    lv_obj_set_style_bg_opa(hint, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(hint, 6, 0);
}

static void build_climate(lv_obj_t *tile)
{
    lv_obj_t *lbl_temp = make_label(tile, &ui_font_28, 0xf59e0b);
    lv_label_set_text(lbl_temp, i18n_metric(AIR_TEMP));
    lv_obj_align(lbl_temp, LV_ALIGN_CENTER, -116 + UI_X_OFFSET, -72);

    s_cl_temp = make_label(tile, &ui_font_48, 0xf59e0b);
    lv_obj_align(s_cl_temp, LV_ALIGN_CENTER, -116 + UI_X_OFFSET, -22);

    lv_obj_t *lbl_hum = make_label(tile, &ui_font_28, 0x38bdf8);
    lv_label_set_text(lbl_hum, i18n_metric(AIR_HUM));
    lv_obj_align(lbl_hum, LV_ALIGN_CENTER, 116 + UI_X_OFFSET, -72);

    s_cl_hum = make_label(tile, &ui_font_48, 0x38bdf8);
    lv_obj_align(s_cl_hum, LV_ALIGN_CENTER, 116 + UI_X_OFFSET, -22);

    chart_create(&s_cl_chart, &s_cl_ser, tile, 430, 110, 0xf59e0b);
    s_cl_ser_hum = chart_add_secondary(s_cl_chart, 0x38bdf8);
    lv_obj_align(s_cl_chart, LV_ALIGN_CENTER, UI_X_OFFSET, 86);
}

// ------------------------------------------------------------------ Status & Ruheansicht
static void build_idle(lv_obj_t *parent)
{
    s_idle_panel = plain(parent);
    lv_obj_set_size(s_idle_panel, SCR_W, SCR_H);
    lv_obj_center(s_idle_panel);
    lv_obj_set_hidden(s_idle_panel, true);
    lv_obj_set_clickable(s_idle_panel, false);

    s_idle_level = make_label(s_idle_panel, &ui_font_48, 0xffffff);
    lv_obj_align(s_idle_level, LV_ALIGN_CENTER, UI_X_OFFSET, -100);

    s_idle_noise_name = make_label(s_idle_panel, &ui_font_16, 0x64748b);
    lv_obj_align(s_idle_noise_name, LV_ALIGN_CENTER, -36 + UI_X_OFFSET, -62);
    s_idle_val[AIR_NOISE] = make_label(s_idle_panel, &ui_font_22, 0xffffff);
    lv_obj_align(s_idle_val[AIR_NOISE], LV_ALIGN_CENTER, 32 + UI_X_OFFSET, -62);

    s_idle_ip = make_label(s_idle_panel, &ui_font_16, 0x475569);
    lv_obj_set_style_text_align(s_idle_ip, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_idle_ip, LV_ALIGN_CENTER, UI_X_OFFSET, 142);

    static const air_metric_t orden[9] = {
        AIR_CO2,  AIR_TEMP, AIR_HUM,
        AIR_PM1,  AIR_PM25, AIR_PM4,
        AIR_PM10, AIR_VOC,  AIR_NOX,
    };
    for (int i = 0; i < 9; i++) {
        const int dx = (i % 3 - 1) * 122 + UI_X_OFFSET;
        const int dy = -36 + (i / 3) * 64;
        lv_obj_t *name = make_label(s_idle_panel, &ui_font_16, 0x64748b);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(name, i18n_metric(orden[i]));
        lv_obj_align(name, LV_ALIGN_CENTER, dx, dy);

        s_idle_val[orden[i]] = make_label(s_idle_panel, &ui_font_28, 0xffffff);
        lv_obj_set_style_text_align(s_idle_val[orden[i]], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_idle_val[orden[i]], LV_ALIGN_CENTER, dx, dy + 24);
    }
}

static void build_noise(lv_obj_t *tile)
{
    chart_create(&s_noise_chart, &s_noise_ser, tile, 430, 90, 0xfacc15);
    lv_obj_align(s_noise_chart, LV_ALIGN_CENTER, UI_X_OFFSET, 108);

    gauge_create(&s_g_noise, tile, RING_D, 22, &ui_font_48, &ui_font_28);
    lv_obj_set_style_text_font(s_g_noise.caption, &ui_font_28, 0);
    lv_obj_align(s_g_noise.caption, LV_ALIGN_CENTER, UI_X_OFFSET, -78);
    lv_obj_align(s_g_noise.value, LV_ALIGN_CENTER, UI_X_OFFSET, -22);

    s_noise_level = make_label(tile, &ui_font_28, 0xffffff);
    lv_obj_align(s_noise_level, LV_ALIGN_CENTER, UI_X_OFFSET, 48);
}

static void build_chrome(lv_obj_t *parent)
{
    s_status_time = make_label(parent, &ui_font_48, 0xe2e8f0);
    lv_obj_align(s_status_time, LV_ALIGN_CENTER, UI_X_OFFSET, -170);

    s_status_net = make_label(parent, &ui_font_16, 0x475569);
    lv_obj_align(s_status_net, LV_ALIGN_CENTER, UI_X_OFFSET, -128);

    s_status_bat = make_label(parent, &ui_font_16, 0x94a3b8);
    lv_obj_align(s_status_bat, LV_ALIGN_CENTER, UI_X_OFFSET, 156);

    s_status_msg = make_label(parent, &ui_font_16, 0xfacc15);
    lv_obj_align(s_status_msg, LV_ALIGN_BOTTOM_MID, UI_X_OFFSET, -52);

    for (int i = 0; i < UI_PAGE_COUNT; i++) {
        s_dots[i] = lv_obj_create(parent);
        lv_obj_remove_style_all(s_dots[i]);
        lv_obj_set_size(s_dots[i], 7, 7);
        lv_obj_set_style_radius(s_dots[i], 4, 0);
        lv_obj_set_style_bg_opa(s_dots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(s_dots[i], col(0x334155), 0);
        lv_obj_set_hidden(s_dots[i], true);
    }
}

static void layout_dots(void)
{
    const int gap = 16;
    const int x0 = -(s_cols - 1) * gap / 2 + UI_X_OFFSET;
    for (int c = 0; c < s_cols; c++) {
        lv_obj_t *d = s_dots[c];
        lv_obj_set_hidden(d, false);
        lv_obj_align(d, LV_ALIGN_BOTTOM_MID, x0 + c * gap, -28);
    }
}

static void update_dots(void)
{
    for (int c = 0; c < s_cols; c++) {
        lv_obj_set_style_bg_color(s_dots[c], col(c == s_cur_col ? 0xe2e8f0 : 0x334155), 0);
    }
}

static void on_tile_change(lv_event_t *e)
{
    (void)e;
    lv_obj_t *act = lv_tileview_get_tile_active(s_tv);
    for (int c = 0; c < s_cols; c++) {
        if (lv_obj_get_child(s_tv, c) == act) { s_cur_col = c; break; }
    }
    update_dots();
    ui_wake();
}

static void on_activity(lv_event_t *e)
{
    (void)e;
    ui_wake();
}

// -------------------------------------------------------------------- API
void ui_init(air_history_t *hist, const ui_config_t *cfg,
             void (*set_brightness)(uint8_t))
{
    s_hist = hist;
    s_cfg = *cfg;
    s_set_brightness = set_brightness;
    air_sample_clear(&s_last);

    ui_fonts_init();

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, col(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(scr, on_activity, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, on_activity, LV_EVENT_CLICKED, NULL);

    // Gemeinsamer Container fuer alle UI-Elemente, damit Pixel-Shift (Burn-in-Schutz)
    // die gesamte Ansicht sauber um +-3 px verschieben kann.
    s_root = plain(scr);
    lv_obj_set_size(s_root, SCR_W, SCR_H);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_event_bubble(s_root, true);

    s_tv = lv_tileview_create(s_root);
    lv_obj_set_size(s_tv, SCR_W, SCR_H);
    lv_obj_set_style_bg_opa(s_tv, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_tv, 0, 0);
    lv_obj_set_scrollbar_mode(s_tv, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_tv, on_tile_change, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_tv, on_activity, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_tv, on_activity, LV_EVENT_SCROLL_BEGIN, NULL);

    s_cols = 0;
    for (int p = 0; p < UI_PAGE_COUNT; p++) {
        s_page_col[p] = -1;
        if (!((s_cfg.pages_mask >> p) & 1)) continue;
        lv_obj_t *tile = lv_tileview_add_tile(s_tv, s_cols, 0, LV_DIR_HOR);
        lv_obj_set_scrollable(tile, false);
        lv_obj_set_event_bubble(tile, true);
        switch (p) {
        case PAGE_SUMMARY: build_summary(tile); break;
        case PAGE_CO2:     build_co2(tile);     break;
        case PAGE_PM:      build_pm(tile);      break;
        case PAGE_GAS:     build_gas(tile);     break;
        case PAGE_CLIMATE: build_climate(tile); break;
        case PAGE_NOISE:   build_noise(tile);   break;
        default: break;
        }
        s_page_col[p] = s_cols;
        s_col_page[s_cols] = p;
        s_cols++;
    }
    if (s_cols == 0) {
        lv_obj_t *tile = lv_tileview_add_tile(s_tv, 0, 0, LV_DIR_HOR);
        lv_obj_set_event_bubble(tile, true);
        build_summary(tile);
        s_page_col[PAGE_SUMMARY] = 0;
        s_col_page[0] = PAGE_SUMMARY;
        s_cols = 1;
    }

    build_idle(s_root);
    build_chrome(s_root);
    layout_dots();
    update_dots();

    // Software-Dimming-Overlay auf lv_layer_top() fuer stufenloses Smooth Fading.
    s_dim_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_dim_overlay, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(s_dim_overlay, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_dim_overlay, 0, 0);
    lv_obj_set_style_radius(s_dim_overlay, 0, 0);
    lv_obj_set_clickable(s_dim_overlay, false);
    lv_obj_set_style_bg_opa(s_dim_overlay, LV_OPA_TRANSP, 0);

    apply_brightness_immediate(s_cfg.brightness);
}

void ui_apply_config(const ui_config_t *cfg)
{
    if (!cfg) return;
    s_cfg.brightness = cfg->brightness;
    s_cfg.night_brightness = cfg->night_brightness;
    s_cfg.screen_timeout_s = cfg->screen_timeout_s;
    s_cfg.chart_span_min = cfg->chart_span_min;
    s_cfg.burnin_idle = cfg->burnin_idle;
    s_cfg.burnin_active = cfg->burnin_active;

    if (s_dimmed && !s_on_battery && s_cfg.screen_timeout_s == 0) {
        ui_wake();
    } else if (s_dimmed) {
        fade_brightness(s_on_battery ? 0 : s_cfg.night_brightness, 0);
    } else {
        fade_brightness(s_cfg.brightness, 0);
    }

    const bool shift_en = (s_cur_brightness > 0) &&
                          (s_dimmed ? s_cfg.burnin_idle : s_cfg.burnin_active);
    if (!shift_en) burnin_reset();
}

void ui_update(const air_sample_t *s)
{
    s_last = *s;
    char buf[48], buf2[16];

    if (s_idle_panel) {
        const air_level_t glob = air_overall(s);
        lv_label_set_text(s_idle_level, i18n_level(glob));
        if (s_idle_noise_name) lv_label_set_text(s_idle_noise_name, i18n_metric(AIR_NOISE));
        lv_obj_set_style_text_color(s_idle_level, col(air_level_color(glob)), 0);
        for (int m = 0; m < AIR_METRIC_COUNT; m++) {
            if (!s_idle_val[m]) continue;
            fmt_value(buf2, sizeof(buf2), (air_metric_t)m, s->v[m]);
            lv_label_set_text(s_idle_val[m], buf2);
            lv_obj_set_style_text_color(s_idle_val[m],
                col(air_level_color(air_level((air_metric_t)m, s->v[m]))), 0);
        }
    }

    for (int p = 0; p < UI_PAGE_COUNT; p++) {
        if (s_page_col[p] < 0) continue;
        switch (p) {
        case PAGE_SUMMARY: {
            const air_level_t lvl = air_overall(s);
            const air_metric_t drv = air_overall_driver(s);
            lv_obj_set_style_arc_color(s_g_summary.arc, col(air_level_color(lvl)),
                                       LV_PART_INDICATOR);
            lv_arc_set_value(s_g_summary.arc, lvl == AIR_LVL_UNKNOWN ? 0 : 1000);
            lv_label_set_text(s_sum_center, i18n_level(lvl));
            lv_obj_set_style_text_color(s_sum_center, col(air_level_color(lvl)), 0);
            if (lvl == AIR_LVL_UNKNOWN) {
                lv_label_set_text(s_sum_sub, T(STR_WAITING));
            } else if (lvl == AIR_LVL_GOOD) {
                lv_label_set_text(s_sum_sub, T(STR_ALL_OK));
            } else {
                snprintf(buf, sizeof(buf), T(STR_DRIVEN_BY), i18n_metric(drv));
                lv_label_set_text(s_sum_sub, buf);
            }
            static const air_metric_t mini[4] = {AIR_CO2, AIR_PM25, AIR_TEMP, AIR_HUM};
            for (int i = 0; i < 4; i++) {
                const char *rot = (mini[i] == AIR_TEMP) ? T(STR_LBL_TEMP)
                                : (mini[i] == AIR_HUM)  ? T(STR_LBL_HUM)
                                                        : i18n_metric(mini[i]);
                snprintf(buf, sizeof(buf), "%s %s", rot, air_metric_unit_ui(mini[i]));
                lv_label_set_text(s_sum_name[i], buf);
                fmt_value(buf2, sizeof(buf2), mini[i], s->v[mini[i]]);
                lv_label_set_text(s_sum_val[i], buf2);
                lv_obj_set_style_text_color(s_sum_val[i],
                    col(air_level_color(air_level(mini[i], s->v[mini[i]]))), 0);
            }
            break;
        }
        case PAGE_CO2: {
            gauge_update(&s_g_co2, AIR_CO2, s->v[AIR_CO2]);
            lv_obj_align(s_g_co2.value, LV_ALIGN_CENTER, 0, -44);
            lv_obj_align_to(s_g_co2.unit, s_g_co2.value, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
            const air_level_t lvl = air_level(AIR_CO2, s->v[AIR_CO2]);
            lv_label_set_text(s_co2_level, i18n_level(lvl));
            lv_obj_set_style_text_color(s_co2_level, col(air_level_color(lvl)), 0);
            chart_refresh(s_co2_chart, s_co2_ser, AIR_CO2);
            break;
        }
        case PAGE_NOISE: {
            gauge_update(&s_g_noise, AIR_NOISE, s->v[AIR_NOISE]);
            lv_obj_align(s_g_noise.value, LV_ALIGN_CENTER, 0, -44);
            lv_obj_align_to(s_g_noise.unit, s_g_noise.value, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
            const air_level_t lvl = air_level(AIR_NOISE, s->v[AIR_NOISE]);
            lv_label_set_text(s_noise_level, i18n_level(lvl));
            lv_obj_set_style_text_color(s_noise_level, col(air_level_color(lvl)), 0);
            chart_refresh(s_noise_chart, s_noise_ser, AIR_NOISE);
            break;
        }
        case PAGE_PM: {
            gauge_update(&s_g_pm, AIR_PM25, s->v[AIR_PM25]);
            lv_obj_align(s_g_pm.value, LV_ALIGN_CENTER, 0, -48);
            lv_obj_align_to(s_g_pm.unit, s_g_pm.value, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
            const air_level_t lvl = air_level(AIR_PM25, s->v[AIR_PM25]);
            lv_label_set_text(s_pm_level, i18n_level(lvl));
            lv_obj_set_style_text_color(s_pm_level, col(air_level_color(lvl)), 0);

            chart_refresh(s_pm_chart, s_pm_ser, AIR_PM25);

            static const air_metric_t otras[3] = {AIR_PM1, AIR_PM4, AIR_PM10};
            for (int i = 0; i < 3; i++) {
                const float v = s->v[otras[i]];
                lv_label_set_text(s_pm_name[i], i18n_metric(otras[i]));
                fmt_value(buf2, sizeof(buf2), otras[i], v);
                lv_label_set_text(s_pm_val[i], buf2);
                lv_obj_set_style_text_color(s_pm_val[i],
                    col(air_level_color(air_level(otras[i], v))), 0);
            }
            break;
        }
        case PAGE_GAS:
            gauge_update(&s_g_voc, AIR_VOC, s->v[AIR_VOC]);
            gauge_update(&s_g_nox, AIR_NOX, s->v[AIR_NOX]);
            chart_refresh_axis(s_gas_chart, s_gas_ser_voc, AIR_VOC, LV_CHART_AXIS_PRIMARY_Y);
            chart_refresh_axis(s_gas_chart, s_gas_ser_nox, AIR_NOX, LV_CHART_AXIS_SECONDARY_Y);
            break;
        case PAGE_CLIMATE: {
            fmt_value(buf2, sizeof(buf2), AIR_TEMP, s->v[AIR_TEMP]);
            snprintf(buf, sizeof(buf), "%s °C", buf2);
            lv_label_set_text(s_cl_temp, buf);
            fmt_value(buf2, sizeof(buf2), AIR_HUM, s->v[AIR_HUM]);
            snprintf(buf, sizeof(buf), "%s %%", buf2);
            lv_label_set_text(s_cl_hum, buf);
            chart_refresh_axis(s_cl_chart, s_cl_ser, AIR_TEMP, LV_CHART_AXIS_PRIMARY_Y);
            chart_refresh_axis(s_cl_chart, s_cl_ser_hum, AIR_HUM, LV_CHART_AXIS_SECONDARY_Y);
            break;
        }
        default: break;
        }
    }
}

void ui_set_status(const char *time_str, bool wifi, bool mqtt, const char *msg,
                   int bat_pct, bool charging)
{
    if (!s_status_time) return;
    lv_label_set_text(s_status_time, time_str ? time_str : "");

    char net[32];
    snprintf(net, sizeof(net), "%s%s%s",
             wifi ? "wifi" : T(STR_NO_WIFI),
             (wifi && mqtt) ? " + " : "",
             (wifi && mqtt) ? "mqtt" : "");
    lv_label_set_text(s_status_net, net);
    lv_obj_set_style_text_color(s_status_net, col(wifi ? (mqtt ? 0x22c55e : 0x94a3b8)
                                                      : 0x64748b), 0);

    lv_label_set_text(s_status_msg, msg ? msg : "");

    if (bat_pct < 0) {
        lv_label_set_text(s_status_bat, "");
    } else {
        const char *icono = charging      ? LV_SYMBOL_CHARGE
                          : bat_pct >= 80 ? LV_SYMBOL_BATTERY_FULL
                          : bat_pct >= 55 ? LV_SYMBOL_BATTERY_3
                          : bat_pct >= 30 ? LV_SYMBOL_BATTERY_2
                          : bat_pct >= 12 ? LV_SYMBOL_BATTERY_1
                                          : LV_SYMBOL_BATTERY_EMPTY;
        char buf[24];
        snprintf(buf, sizeof(buf), "%s %d%%", icono, bat_pct);
        lv_label_set_text(s_status_bat, buf);
        const uint32_t color = charging     ? 0x22c55e
                             : bat_pct < 12 ? 0xef4444
                             : bat_pct < 30 ? 0xf97316
                                            : 0x94a3b8;
        lv_obj_set_style_text_color(s_status_bat, col(color), 0);
    }
}

void ui_set_on_battery(bool on_battery)
{
    if (on_battery == s_on_battery) return;
    s_on_battery = on_battery;
    if (!on_battery) ui_wake();
}

void ui_idle_debug(uint32_t *idle_s, bool *dimmed, bool *idle_shown)
{
    *idle_s = s_idle_s;
    *dimmed = s_dimmed;
    *idle_shown = s_idle_shown;
}

void ui_goto_page(int page)
{
    if (page < 0 || page >= UI_PAGE_COUNT || s_page_col[page] < 0) return;
    s_cur_col = s_page_col[page];
    lv_tileview_set_tile_by_index(s_tv, s_cur_col, 0, LV_ANIM_OFF);
    update_dots();
    ui_wake();
}

void ui_set_live_brightness(uint8_t level)
{
    fade_brightness(level, 0);
}

void ui_wake(void)
{
    lv_display_trigger_activity(NULL);
    s_idle_s = 0;
    if (s_idle_shown) {
        s_idle_shown = false;
        lv_obj_set_hidden(s_idle_panel, true);
        lv_obj_set_hidden(s_tv, false);
    }
    if (s_dimmed) {
        s_dimmed = false;
        fade_brightness(s_cfg.brightness, FADE_WAKE_MS);
    }
    if (!s_cfg.burnin_active) {
        burnin_reset();
    }
}

void ui_set_ip(const char *ip)
{
    if (!s_idle_ip) return;
    lv_label_set_text(s_idle_ip, (ip && ip[0]) ? ip : "");
}

void ui_tick_1s(void)
{
    const uint32_t inactivo_ms = lv_display_get_inactive_time(NULL);
    s_idle_s = inactivo_ms / 1000;

    const uint32_t timeout = s_on_battery
        ? (s_cfg.screen_timeout_s && s_cfg.screen_timeout_s < UI_BATTERY_TIMEOUT_S
               ? s_cfg.screen_timeout_s : UI_BATTERY_TIMEOUT_S)
        : s_cfg.screen_timeout_s;
    const uint8_t dim_to = s_on_battery ? 0 : s_cfg.night_brightness;

    if (s_dimmed && inactivo_ms < 1500) {
        ui_wake();
    } else if (timeout && !s_dimmed && s_idle_s >= timeout) {
        s_dimmed = true;
        fade_brightness(dim_to, FADE_DIM_MS);
        if (!s_on_battery && s_idle_panel) {
            s_idle_shown = true;
            lv_obj_set_hidden(s_tv, true);
            lv_obj_set_hidden(s_idle_panel, false);
        }
    }

    // AMOLED Burn-in-Schutz (Pixel-Shift +-3 px alle 60 s)
    const bool shift_en = (s_cur_brightness > 0) &&
                          (s_dimmed ? s_cfg.burnin_idle : s_cfg.burnin_active);
    if (!shift_en) {
        burnin_reset();
    } else if (++s_burnin_s >= BURNIN_PERIOD_S) {
        static const int8_t OFF[][2] = {
            { 0,  0}, { 3,  0}, { 3,  3}, { 0,  3},
            {-3,  3}, {-3,  0}, {-3, -3}, { 0, -3}, { 3, -3},
        };
        s_burnin_s = 0;
        s_burnin_step = (s_burnin_step + 1) % (sizeof(OFF) / sizeof(OFF[0]));
        if (s_root) {
            lv_obj_set_pos(s_root, OFF[s_burnin_step][0], OFF[s_burnin_step][1]);
        }
    }
}