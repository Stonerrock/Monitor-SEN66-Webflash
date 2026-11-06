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

// Randabstand, damit nichts auf den gebogenen Rand des runden Displays faellt.
#define EDGE 10

// Horizontale Verschiebung (in Pixeln) zur Zentrierung auf dem runden Display
#define UI_X_OFFSET 4

// Maximaler Ring-Durchmesser zur vollstaendigen Ausnutzung der 1.75" Diagonale (466x466)
#define RING_D 446

// Nutzbarer Innenraum im Ring: Aussenradius abzueglich Bogendicke.
// Dient als Referenz fuer die Koordinaten der Seiten.
#define RING_INNER_R ((RING_D) / 2 - 22)

// Laerm/Schallpegel steht AM ENDE, um die bereits gespeicherten Bits von pages_mask nicht zu verschieben.
enum { PAGE_SUMMARY = 0, PAGE_CO2, PAGE_PM, PAGE_GAS, PAGE_CLIMATE, PAGE_NOISE };

// Wiederverwendbare Kreisanzeige: 270-Grad-Bogen + Wert + Einheit.
typedef struct {
    lv_obj_t *arc;
    lv_obj_t *value;
    lv_obj_t *unit;
    lv_obj_t *caption;
} gauge_t;

static air_history_t *s_hist;
static ui_config_t s_cfg;
static void (*s_set_brightness)(uint8_t);

static lv_obj_t *s_tv;
static lv_obj_t *s_status_time;
static lv_obj_t *s_status_msg;
static lv_obj_t *s_status_net;
static lv_obj_t *s_status_bat;

// Ruhemodus: Bei Inaktivitaet wird das Display gedimmt und zeigt ALLE
// Messwerte auf einen Blick. Dies ist ein separates Panel ueber dem Tileview,
// keine zusaetzliche Seite: So wird es beim Wischen uebersprungen und beim Aufwachen
// kehrt man exakt zu der Seite zurueck, auf der man war.
static lv_obj_t *s_idle_panel;
static lv_obj_t *s_idle_val[AIR_METRIC_COUNT];
static lv_obj_t *s_idle_level;
static lv_obj_t *s_idle_noise_name;
static lv_obj_t *s_idle_ip;
static bool s_idle_shown;
static lv_obj_t *s_dots[UI_PAGE_COUNT];

// Seite -> Spalte im Tileview (-1 wenn Seite ausgeblendet ist)
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
#define UI_BATTERY_TIMEOUT_S 30
static air_sample_t s_last;

// ------------------------------------------------------------------ helpers
static lv_color_t col(uint32_t rgb) { return lv_color_hex(rgb); }

static void fmt_value(char *buf, size_t len, air_metric_t m, float v)
{
    if (isnan(v)) { snprintf(buf, len, "--"); return; }
    snprintf(buf, len, "%.*f", air_metric_decimals(m), v);
}

static lv_obj_t *plain(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
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

// Sekundaerwert: kleine Beschriftung oben und der Wert allein darunter.
// Durch die Trennung von Einheit und Zahl kann der Wert groesser dargestellt werden,
// was innerhalb eines Rings wichtig ist.
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
    lv_arc_set_bg_angles(g->arc, 135, 45); // 270 Grad, Oeffnung unten
    lv_arc_set_range(g->arc, 0, 1000);
    lv_arc_set_value(g->arc, 0);
    lv_obj_remove_style(g->arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(g->arc, LV_OBJ_FLAG_CLICKABLE);
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
    // Wertbreite aendert sich dynamisch: Einheit darunter neu ausrichten
    lv_obj_align_to(g->unit, g->value, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
}

static void chart_create(lv_obj_t **out_chart, lv_chart_series_t **out_ser,
                         lv_obj_t *parent, int w, int h, uint32_t color)
{
    lv_obj_t *c = lv_chart_create(parent);
    lv_obj_set_size(c, w, h);
    lv_chart_set_type(c, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(c, CHART_POINTS);
    lv_chart_set_div_line_count(c, 0, 0); // Ohne Gitter: nur die Linie
    lv_chart_set_update_mode(c, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_set_style_line_color(c, col(0x1e293b), LV_PART_MAIN);
    lv_obj_set_style_line_width(c, 2, LV_PART_ITEMS);
    lv_obj_set_style_width(c, 0, LV_PART_INDICATOR);  // Keine Datenpunkte
    lv_obj_set_style_height(c, 0, LV_PART_INDICATOR);
    *out_ser = lv_chart_add_series(c, col(color), LV_CHART_AXIS_PRIMARY_Y);
    *out_chart = c;
}

// Zweite Kurve auf demselben Diagramm mit eigener vertikaler Skala:
// Dient der Ueberlagerung unterschiedlicher Wertebereiche (z. B. Temperatur und Feuchte).
static lv_chart_series_t *chart_add_secondary(lv_obj_t *c, uint32_t color)
{
    return lv_chart_add_series(c, col(color), LV_CHART_AXIS_SECONDARY_Y);
}

// Uebertraegt das Zeitfenster aus dem Verlauf in die Datenreihe, mit Luecken bei fehlenden Daten.
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
    // lv_chart arbeitet mit Ganzzahlen: Bei Metriken mit Dezimalstellen (Temperatur
    // in Zehntelgraden) skalieren wir mit x10, damit die Kurve nicht treppenartig wirkt.
    const float k = air_metric_decimals(m) ? 10.0f : 1.0f;

    // 10 % Toleranzbereich und Mindestabstand, damit eine flache Linie nicht direkt am Rand klebt
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
    // In der Uebersicht steht im Zentrum Text, keine Zahl
    lv_obj_add_flag(s_g_summary.value, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_g_summary.unit, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_g_summary.caption, LV_OBJ_FLAG_HIDDEN);
    // Dieser Ring misst nicht: Er dient als Ampel und wird vollstaendig gefuellt.
    // Gleiche Dicke fuer Anzeige und Spur, damit kein Rand hervorblitzt.
    lv_obj_set_style_arc_width(s_g_summary.arc, 22, LV_PART_INDICATOR);

    s_sum_center = make_label(tile, &ui_font_48, 0xffffff);
    lv_obj_align(s_sum_center, LV_ALIGN_CENTER, UI_X_OFFSET, -76);
    s_sum_sub = make_label(tile, &ui_font_28, 0x94a3b8);
    lv_obj_align(s_sum_sub, LV_ALIGN_CENTER, UI_X_OFFSET, -32);

    // 2x2-Raster von Sekundaerwerten mit vergroessertem Padding und 28px-Werten
    static const int dx[4] = {-96, 96, -96, 96};
    static const int dy[4] = {18, 18, 86, 86};
    for (int i = 0; i < 4; i++) {
        mini_create(tile, &s_sum_name[i], &s_sum_val[i], dx[i], dy[i],
                    &ui_font_28);
    }
}

static void build_co2(lv_obj_t *tile)
{
    // Das Diagramm wird ZUERST erstellt, damit es im Hintergrund liegt: Es verlaeuft
    // ueber das gesamte Display, Ring und Zahl werden darueber gezeichnet.
    // Der Ring ueberdeckt die Kurve, sodass die Linie darunter durchzulaufen scheint.
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

// Tuerkis: Die anderen Farben sind bereits belegt (CO2 blau, VOC violett, NOx hellblau, Laerm bernsteingelb).
#define PM_COL 0x2dd4bf

static void build_pm(lv_obj_t *tile)
{
    // Das Diagramm ZUERST im Hintergrund: Nur PM2.5, da dieser Wert die Seite dominiert
    // und gesundheitlich am wichtigsten ist; vier fast identische Kurven waeren unuebersichtlich.
    chart_create(&s_pm_chart, &s_pm_ser, tile, 430, 90, PM_COL);
    lv_obj_align(s_pm_chart, LV_ALIGN_CENTER, UI_X_OFFSET, 108);

    // Gleicher visueller Aufbau wie CO2: Ring und grosse Ziffer.
    // PM2.5 steht im Fokus, die anderen drei Werte begleiten darunter.
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
        // Die drei Zahlen liegen INNERHALB des Diagrammbands (+55..+145).
        // Ein deckender schwarzer Hintergrund sorgt dafuer, dass die Kurve dahinter durchlaeuft
        // und die Beschriftung lesbar bleibt.
        for (lv_obj_t *o = s_pm_name[i]; o; o = (o == s_pm_name[i]) ? s_pm_val[i] : NULL) {
            lv_obj_set_style_bg_color(o, col(0x000000), 0);
            lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
            lv_obj_set_style_pad_hor(o, 5, 0);
        }
    }
}

// Zentrale Farbdefinitionen: Diagrammlinie und Ringbeschriftung gehoeren zur selben Metrik.
#define GAS_COL_VOC 0xa78bfa
#define GAS_COL_NOX 0x38bdf8

static void build_gas(lv_obj_t *tile)
{
    // Gleicher Aufbau wie CO2 und Klima: Diagramm ZUERST im Hintergrund im unteren Bereich (y~+100).
    // VOC auf Primaerachse, NOx auf Sekundaerachse (gleiches Diagramm, getrennte Skalierung).
    chart_create(&s_gas_chart, &s_gas_ser_voc, tile, 430, 90, GAS_COL_VOC);
    s_gas_ser_nox = chart_add_secondary(s_gas_chart, GAS_COL_NOX);
    lv_obj_align(s_gas_chart, LV_ALIGN_CENTER, UI_X_OFFSET, 108);

    lv_obj_t *title = make_label(tile, &ui_font_28, 0x94a3b8);
    lv_label_set_text(title, T(STR_PAGE_GASES));
    lv_obj_align(title, LV_ALIGN_CENTER, UI_X_OFFSET, -88);

    // Zwei vergroesserte Ringe (188 px), nach links und rechts gespreizt (x = +-106)
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
    // Deckender Hintergrund in Displayfarbe, damit die Diagrammlinie dahinter
    // durchlaeuft und den Text nicht unleserlich macht.
    lv_obj_set_style_bg_color(hint, col(0x000000), 0);
    lv_obj_set_style_bg_opa(hint, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(hint, 6, 0);
}

static void build_climate(lv_obj_t *tile)
{
    // Spaltenkoepfe in 28 px Kurvenfarbe, weiter nach aussen gesetzt (-116 / +116)
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

    // Ohne Ring kann diese Seite ein breites Diagramm nutzen. Beide Kurven teilen sich
    // das Diagramm, nutzen aber getrennte vertikale Skalen (Primaer- und Sekundaerachse).
    chart_create(&s_cl_chart, &s_cl_ser, tile, 430, 110, 0xf59e0b);
    s_cl_ser_hum = chart_add_secondary(s_cl_chart, 0x38bdf8);
    lv_obj_align(s_cl_chart, LV_ALIGN_CENTER, UI_X_OFFSET, 86);
}

// ------------------------------------------------------------------ Status & Ruheansicht
// Die neun Messgroessen im 3x3-Raster mit Gesamtbewertung oben.
static void build_idle(lv_obj_t *scr)
{
    s_idle_panel = plain(scr);
    lv_obj_set_size(s_idle_panel, SCR_W, SCR_H);
    lv_obj_center(s_idle_panel);
    lv_obj_add_flag(s_idle_panel, LV_OBJ_FLAG_HIDDEN);
    // Keine Beruehrungen abfangen: Geste muss durchgehen, um den Bildschirm aufzuwecken.
    lv_obj_remove_flag(s_idle_panel, LV_OBJ_FLAG_CLICKABLE);

    s_idle_level = make_label(s_idle_panel, &ui_font_48, 0xffffff);
    lv_obj_align(s_idle_level, LV_ALIGN_CENTER, UI_X_OFFSET, -100);

    // Laerm steht separat unter der Ampel und NICHT im Raster: Es gehoert nicht zur
    // chemischen Luftqualitaet und fliesst nicht in die Gesamtbewertung ein.
    s_idle_noise_name = make_label(s_idle_panel, &ui_font_16, 0x64748b);
    lv_obj_align(s_idle_noise_name, LV_ALIGN_CENTER, -36 + UI_X_OFFSET, -62);
    s_idle_val[AIR_NOISE] = make_label(s_idle_panel, &ui_font_22, 0xffffff);
    lv_obj_align(s_idle_val[AIR_NOISE], LV_ALIGN_CENTER, 32 + UI_X_OFFSET, -62);

    // IP-Adresse ganz unten ueber der Akkuanzeige: Nuetzlich fuer den Zugriff auf das Web-Panel.
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
        // Zeilenabstand so gewaehlt, dass die unterste Zeile nicht mit der Akkuanzeige kollidiert
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

// Gleicher Aufbau wie CO2: Diagramm vollflaechig im Hintergrund, Ring darueber (in Bernstein).
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

static void build_chrome(lv_obj_t *scr)
{
    // Grosse Uhrzeit und Netzwerkstatus INNERHALB des Rings
    s_status_time = make_label(scr, &ui_font_48, 0xe2e8f0);
    lv_obj_align(s_status_time, LV_ALIGN_CENTER, UI_X_OFFSET, -170);

    s_status_net = make_label(scr, &ui_font_16, 0x475569);
    lv_obj_align(s_status_net, LV_ALIGN_CENTER, UI_X_OFFSET, -128);

    // Die Akkuanzeige liegt unten im freien Bereich der Bogenoeffnung:
    // Stoert keine Seite und ist sofort ablesbar.
    s_status_bat = make_label(scr, &ui_font_16, 0x94a3b8);
    lv_obj_align(s_status_bat, LV_ALIGN_CENTER, UI_X_OFFSET, 156);

    s_status_msg = make_label(scr, &ui_font_16, 0xfacc15);
    lv_obj_align(s_status_msg, LV_ALIGN_BOTTOM_MID, UI_X_OFFSET, -52);

    for (int i = 0; i < UI_PAGE_COUNT; i++) {
        s_dots[i] = lv_obj_create(scr);
        lv_obj_remove_style_all(s_dots[i]);
        lv_obj_set_size(s_dots[i], 7, 7);
        lv_obj_set_style_radius(s_dots[i], 4, 0);
        lv_obj_set_style_bg_opa(s_dots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(s_dots[i], col(0x334155), 0);
        lv_obj_add_flag(s_dots[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void layout_dots(void)
{
    const int gap = 16;
    const int x0 = -(s_cols - 1) * gap / 2 + UI_X_OFFSET;
    for (int c = 0; c < s_cols; c++) {
        lv_obj_t *d = s_dots[c];
        lv_obj_remove_flag(d, LV_OBJ_FLAG_HIDDEN);
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

    ui_fonts_init(); // Kopien der Montserrat-Schriften mit deutschen Umlauten

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, col(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(scr, on_activity, LV_EVENT_PRESSED, NULL);

    s_tv = lv_tileview_create(scr);
    lv_obj_set_size(s_tv, SCR_W, SCR_H);
    lv_obj_set_style_bg_opa(s_tv, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_tv, 0, 0);
    lv_obj_set_scrollbar_mode(s_tv, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_tv, on_tile_change, LV_EVENT_VALUE_CHANGED, NULL);

    // Nur aktivierte Seiten werden in ihrer natuerlichen Reihenfolge erzeugt.
    // Eine Aenderung der Maske erfordert einen Neustart (Webserver startet beim Speichern neu).
    s_cols = 0;
    for (int p = 0; p < UI_PAGE_COUNT; p++) {
        s_page_col[p] = -1;
        if (!((s_cfg.pages_mask >> p) & 1)) continue;
        lv_obj_t *tile = lv_tileview_add_tile(s_tv, s_cols, 0, LV_DIR_HOR);
        lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
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
    if (s_cols == 0) { // Leere Maske: mindestens die Uebersicht anzeigen
        lv_obj_t *tile = lv_tileview_add_tile(s_tv, 0, 0, LV_DIR_HOR);
        build_summary(tile);
        s_page_col[PAGE_SUMMARY] = 0;
        s_col_page[0] = PAGE_SUMMARY;
        s_cols = 1;
    }

    build_idle(scr);
    build_chrome(scr);
    layout_dots();
    update_dots();
    if (s_set_brightness) s_set_brightness(s_cfg.brightness);
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
            // Der Ring der Uebersicht misst nichts: Er dient als Farbbewertung (Ampel).
            // Er wird vollstaendig gefuellt (nur leer, solange keine Daten vorliegen).
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
            // Abgekuerzte Beschriftungen, damit sie innerhalb des Rings nicht ueberstehen
            for (int i = 0; i < 4; i++) {
                // Beschriftung + Einheit: Die Zahl steht separat darunter fuer maximale Groesse
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
        // Die Akkusymbole stammen direkt aus den Schriften von LVGL (Symbols)
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
    // Beim ANSTECKEN einschalten: Man tritt an das Geraet heran und moechte Werte sehen.
    // Beim Abstecken NICHT aufwecken, um sofort Energie zu sparen.
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

void ui_wake(void)
{
    // Aktivitaet in LVGL registrieren: Bei manuellem Aufwecken wird der Inaktivitaetszaehler
    // zurueckgesetzt, damit das Display nicht im naechsten Tick wieder abschaltet.
    lv_display_trigger_activity(NULL);
    s_idle_s = 0;
    if (s_idle_shown) {
        s_idle_shown = false;
        lv_obj_add_flag(s_idle_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_tv, LV_OBJ_FLAG_HIDDEN); // Kehrt zur vorherigen Seite zurueck
    }
    if (s_dimmed) {
        s_dimmed = false;
        if (s_set_brightness) s_set_brightness(s_cfg.brightness);
    }
}

void ui_set_ip(const char *ip)
{
    if (!s_idle_ip) return;
    lv_label_set_text(s_idle_ip, (ip && ip[0]) ? ip : "");
}

void ui_tick_1s(void)
{
    // Die Inaktivitaet wird von LVGL verwaltet:
    // Der Klick-Event wird auf dem Screen-Objekt registriert.
    const uint32_t inactivo_ms = lv_display_get_inactive_time(NULL);
    s_idle_s = inactivo_ms / 1000;

    // Abdunkeln bei Inaktivitaet (bei AMOLED spart Abschalten spuerbar Strom).
    // Im Akkubetrieb schaltet das Display vollstaendig ab.
    const uint32_t timeout = s_on_battery
        ? (s_cfg.screen_timeout_s && s_cfg.screen_timeout_s < UI_BATTERY_TIMEOUT_S
               ? s_cfg.screen_timeout_s : UI_BATTERY_TIMEOUT_S)
        : s_cfg.screen_timeout_s;
    const uint8_t dim_to = s_on_battery ? 0 : s_cfg.night_brightness;

    if (s_dimmed && inactivo_ms < 1500) {
        ui_wake(); // Beruehrung erkannt: Aufwecken und vorherige Seite anzeigen
    } else if (timeout && !s_dimmed && s_idle_s >= timeout) {
        s_dimmed = true;
        if (s_set_brightness) s_set_brightness(dim_to);
        // Im Akkubetrieb schaltet das Display ganz ab, daher kein Ruhebildschirm;
        // am Netzteil wird gedimmt und die Uebersicht eingeblendet.
        if (!s_on_battery && s_idle_panel) {
            s_idle_shown = true;
            lv_obj_add_flag(s_tv, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(s_idle_panel, LV_OBJ_FLAG_HIDDEN);
        }
    }
    // Keine automatische Seitenrotation: Seiten werden manuell per Wischgeste gewechselt.
}
