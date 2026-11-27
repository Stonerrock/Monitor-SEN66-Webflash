#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define SETTINGS_PAGES 6 // resumen, CO2, particulas, gases, clima, ruido

typedef struct {
    char wifi_ssid[33];
    char wifi_pass[65];

    char mqtt_uri[96];   // p.ej. "mqtt://192.168.1.10:1883"
    char mqtt_user[33];
    char mqtt_pass[65];
    char mqtt_prefix[32]; // prefijo de descubrimiento de HA, normalmente "homeassistant"

    char device_name[32]; // como aparece en Home Assistant
    char tz[48];          // cadena POSIX TZ, p.ej. "CET-1CEST,M3.5.0,M10.5.0/3"
    char ntp[64];

    uint8_t brightness;      // 1..255 (comando 0x51 del panel)
    uint8_t night_brightness;// brillo al atenuar
    uint16_t screen_timeout_s; // 0 = nunca atenuar
    uint16_t page_dwell_s;     // 0 = sin rotacion automatica
    uint8_t pages_mask;        // bit i = pagina i visible
    uint16_t chart_span_min;   // ventana de las graficas, en minutos

    int16_t temp_offset_dc;
    uint16_t altitude_m;
    bool co2_asc;            // autocalibracion del CO2

    // --- Campos nuevos SIEMPRE al final ---
    bool alarm_enabled;      // aviso sonoro al pasar el umbral de CO2
    uint16_t alarm_co2_ppm;  // umbral de disparo
    uint16_t alarm_clear_ppm;// umbral de rearme (histeresis)
    uint8_t alarm_volume;    // 0..100
    uint32_t last_fan_clean;  // epoch de la ultima limpieza de ventilador
    char lang[4];             // "es", "en" o "de"

    int16_t noise_offset_db;

    // AMOLED Burn-in-Schutz (Pixel-Shift), getrennt aktivierbar
    bool burnin_idle;        // im gedimmten Modus / Ruheansicht
    bool burnin_active;      // im ungedimmten Normalbetrieb
} settings_t;

esp_err_t settings_load(void);
esp_err_t settings_save(void);
settings_t *settings_get(void);

esp_err_t settings_save_voc_state(const void *state, size_t len);
esp_err_t settings_load_voc_state(void *state, size_t len);
void settings_defaults(settings_t *s);

static inline bool settings_page_visible(const settings_t *s, int page)
{
    return (s->pages_mask >> page) & 1;
}