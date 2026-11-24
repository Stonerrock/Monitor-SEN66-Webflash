#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "air.h"
#include "display.h"
#include "ha_mqtt.h"
#include "history.h"
#include "net.h"
#include "pmu_axp2101.h"
#include "mic.h"
#include "sound.h"
#include "i18n.h"
#include "rtc_pcf85063.h"
#include "sen66.h"
#include "settings.h"
#include "ui.h"
#include "version.h"
#include "webcfg.h"

static const char *TAG = "app";

#define MQTT_PERIOD_S 10
#define SAMPLE_STALE_S 15
#define SENSOR_DEAD_S 60
#define SENSOR_RETRY_S 30
#define VOC_SAVE_PERIOD_S 3600
#define BATT_MQTT_PERIOD_S 60
#define FAN_CLEAN_PERIOD_S (7 * 24 * 3600)
#define BATT_LOG_PERIOD_S 300
#define PMU_POLL_PERIOD_S 2

static air_history_t *s_hist;
static SemaphoreHandle_t s_lock;
static air_sample_t s_sample;
static int64_t s_last_read_us;
static bool s_sensor_ok;
static bool s_had_reading;
static volatile bool s_on_battery;

static time_t tm_to_utc(const struct tm *t)
{
    int y = t->tm_year + 1900;
    const unsigned m = (unsigned)t->tm_mon + 1;
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + (unsigned)t->tm_mday - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = (int64_t)era * 146097 + (int64_t)doe - 719468;
    return (time_t)(days * 86400 + t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec);
}

static void clock_from_rtc(void)
{
    struct tm tm_utc;
    esp_err_t err = rtc_pcf85063_get(&tm_utc);
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "el RTC perdio la hora (sin pila?); espero a NTP");
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "RTC no disponible: %s", esp_err_to_name(err));
        return;
    }
    const time_t t = tm_to_utc(&tm_utc);
    if (t < 1700000000) {
        ESP_LOGW(TAG, "hora del RTC no creible, espero a NTP");
        return;
    }
    struct timeval tv = {.tv_sec = t};
    settimeofday(&tv, NULL);
    ESP_LOGI(TAG, "hora tomada del RTC: %s", asctime(&tm_utc));
}

static void clock_to_rtc(void)
{
    const time_t now = time(NULL);
    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    if (rtc_pcf85063_set(&tm_utc) == ESP_OK) ESP_LOGI(TAG, "RTC puesto en hora");
}

static void voc_state_restore(void)
{
    uint8_t st[SEN66_VOC_STATE_LEN];
    if (settings_load_voc_state(st, sizeof(st)) != ESP_OK) {
        ESP_LOGI(TAG, "sin estado VOC guardado: el sensor aprendera de cero");
        return;
    }
    if (sen66_set_voc_state(st) == ESP_OK) {
        ESP_LOGI(TAG, "estado del algoritmo VOC restaurado");
    }
}

static void voc_state_save(void)
{
    uint8_t st[SEN66_VOC_STATE_LEN];
    if (sen66_get_voc_state(st) != ESP_OK) return;
    const int64_t t0 = esp_timer_get_time();
    if (settings_save_voc_state(st, sizeof(st)) == ESP_OK) {
        ESP_LOGI(TAG, "estado VOC guardado (%lld ms)",
                 (esp_timer_get_time() - t0) / 1000);
    }
}

static void sensor_configure(void)
{
    const settings_t *cfg = settings_get();
    char serial[36] = "?";
    uint8_t maj = 0, min = 0;

    if (sen66_serial(serial, sizeof(serial)) == ESP_OK &&
        sen66_version(&maj, &min) == ESP_OK) {
        ESP_LOGI(TAG, "SEN66 serie %s, firmware %u.%u", serial, maj, min);
    }

    if (cfg->temp_offset_dc != 0) {
        sen66_set_temp_offset(cfg->temp_offset_dc / 10.0f);
        ESP_LOGI(TAG, "correccion de temperatura: %+.1f C", cfg->temp_offset_dc / 10.0f);
    }
    if (cfg->altitude_m != 0) sen66_set_altitude(cfg->altitude_m);
    sen66_set_co2_asc(cfg->co2_asc);
}

static void fan_clean_check(void)
{
    const time_t ahora = time(NULL);
    if (ahora < 1700000000) return;

    settings_t *cfg = settings_get();
    if (cfg->last_fan_clean == 0) {
        cfg->last_fan_clean = (uint32_t)ahora;
        settings_save();
        return;
    }
    if ((uint32_t)ahora - cfg->last_fan_clean < FAN_CLEAN_PERIOD_S) return;

    ESP_LOGI(TAG, "limpieza semanal del ventilador");
    if (sen66_fan_clean() == ESP_OK) {
        cfg->last_fan_clean = (uint32_t)ahora;
        settings_save();
    }
}

static void alarm_check(float co2)
{
    static bool disparada;
    const settings_t *cfg = settings_get();
    if (!cfg->alarm_enabled || !sound_available() || isnan(co2)) return;

    if (!disparada && co2 >= cfg->alarm_co2_ppm) {
        disparada = true;
        ESP_LOGW(TAG, "CO2 %.0f ppm: aviso", co2);
        sound_play(SOUND_ALERT);
    } else if (disparada && co2 <= cfg->alarm_clear_ppm) {
        disparada = false;
        ESP_LOGI(TAG, "CO2 %.0f ppm: ventilado", co2);
        sound_play(SOUND_CLEAR);
    }
}

static volatile uint16_t s_recal_ppm;
static char s_recal_msg[96];

static void recal_set_msg(const char *m)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(s_recal_msg, m, sizeof(s_recal_msg));
    xSemaphoreGive(s_lock);
}

static bool recal_request(uint16_t ppm)
{
    if (ppm < 400 || ppm > 2000) return false;
    if (!s_sensor_ok || s_recal_ppm != 0) return false;
    recal_set_msg("recalibrando...");
    s_recal_ppm = ppm;
    return true;
}

static void recal_status(char *out, size_t len)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(out, s_recal_msg, len);
    xSemaphoreGive(s_lock);
}

static volatile int s_fan_request;

static bool fan_request(bool on)
{
    if (!s_sensor_ok) return false;
    s_fan_request = on ? 1 : -1;
    return true;
}

static void recal_run(uint16_t ppm)
{
    char msg[96];

    uint8_t voc[SEN66_VOC_STATE_LEN];
    const bool tengo_voc = (sen66_get_voc_state(voc) == ESP_OK);

    uint16_t bruto = 0xFFFF;
    esp_err_t err = sen66_stop();
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(600));
        err = sen66_forced_co2_recal(ppm, &bruto);
    }

    if (err == ESP_OK && bruto != 0xFFFF) {
        const int corr = (int)bruto - 0x8000;
        snprintf(msg, sizeof(msg), "recalibrado a %u ppm (correccion %+d ppm)", ppm, corr);
        ESP_LOGI(TAG, "%s", msg);
    } else {
        snprintf(msg, sizeof(msg), "la recalibracion fallo (%s)", esp_err_to_name(err));
        ESP_LOGW(TAG, "%s", msg);
    }

    if (tengo_voc) sen66_set_voc_state(voc);
    if (sen66_start() != ESP_OK) {
        strlcat(msg, " y el sensor no rearranca", sizeof(msg));
        ESP_LOGE(TAG, "el sensor no rearranca tras recalibrar");
        s_sensor_ok = false;
    }
    recal_set_msg(msg);
}

static void sensor_recover(void)
{
    ESP_LOGW(TAG, "el sensor lleva %d s sin responder: reiniciando el bus", SENSOR_DEAD_S);
    sen66_deinit();
    vTaskDelay(pdMS_TO_TICKS(200));
    if (sen66_init(false) != ESP_OK) return;
    sensor_configure();
    voc_state_restore();
    if (sen66_start() != ESP_OK) { sen66_deinit(); return; }
    s_sensor_ok = true;
    ESP_LOGI(TAG, "sensor recuperado");
}

static void sensor_task(void *arg)
{
    (void)arg;
    int64_t next_mqtt_us = 0;
    int64_t next_retry_us = 0;
    int64_t next_batt_us = 0;
    int64_t next_pmu_us = 0;
    int64_t next_voc_us = esp_timer_get_time() + (int64_t)VOC_SAVE_PERIOD_S * 1000000;
    int64_t alive_since_us = esp_timer_get_time();

    bool on_battery = false;

    for (;;) {
        const int64_t now = esp_timer_get_time();

        if (pmu_available() && now >= next_pmu_us) {
            next_pmu_us = now + (int64_t)PMU_POLL_PERIOD_S * 1000000;
            pmu_status_t b;
            if (pmu_read(&b) == ESP_OK) {
                const bool sin_usb = b.present && !b.vbus;
                if (sin_usb != on_battery) {
                    on_battery = sin_usb;
                    ESP_LOGW(TAG, "perfil de %s", on_battery ? "BATERIA" : "red");
                    net_set_power_save(on_battery);
                    s_on_battery = on_battery;
                }
                if (b.present && now >= next_batt_us) {
                    next_batt_us = now + (int64_t)BATT_LOG_PERIOD_S * 1000000;
                    ESP_LOGI(TAG, "bateria %d%% (%u mV)%s", b.percent, b.millivolts,
                             b.charging ? " cargando" : b.vbus ? " con USB" : " EN BATERIA");
                }
            }
        }

        if (s_recal_ppm != 0) {
            recal_run(s_recal_ppm);
            s_recal_ppm = 0;
        }

        if (s_fan_request != 0) {
            const bool encender = s_fan_request > 0;
            s_fan_request = 0;
            const esp_err_t e = encender ? sen66_start() : sen66_stop();
            ESP_LOGW(TAG, "medicion %s a mano: %s", encender ? "arrancada" : "parada",
                     esp_err_to_name(e));
        }

        if (s_sensor_ok) {
            air_sample_t fresh;
            air_sample_clear(&fresh);
            const esp_err_t err = sen66_read(&fresh);

            if (mic_available()) {
                const float dbfs = mic_level_dbfs();
                if (!isnan(dbfs)) {
                    fresh.v[AIR_NOISE] = dbfs + settings_get()->noise_offset_db;
                }
            }

            if (err == ESP_OK) {
                s_last_read_us = esp_timer_get_time();
                s_had_reading = true;
                alarm_check(fresh.v[AIR_CO2]);
                xSemaphoreTake(s_lock, portMAX_DELAY);
                s_sample = fresh;
                xSemaphoreGive(s_lock);
                air_history_push(s_hist, &fresh, time(NULL));
            } else if (err != ESP_ERR_NOT_FINISHED) {
                ESP_LOGW(TAG, "lectura fallida: %s", esp_err_to_name(err));
            }
        }

        const int64_t ref = s_last_read_us ? s_last_read_us : alive_since_us;
        const bool vivo = s_sensor_ok && (now - ref) < (int64_t)SENSOR_DEAD_S * 1000000;

        ha_mqtt_set_available(vivo);
        if (!vivo && now >= next_retry_us) {
            next_retry_us = now + (int64_t)SENSOR_RETRY_S * 1000000;
            sensor_recover();
            alive_since_us = esp_timer_get_time();
        }

        if (vivo && now >= next_voc_us) {
            next_voc_us = now + (int64_t)VOC_SAVE_PERIOD_S * 1000000;
            voc_state_save();
            fan_clean_check();
        }

        if (vivo && now >= next_mqtt_us) {
            next_mqtt_us = now + (int64_t)(on_battery ? BATT_MQTT_PERIOD_S : MQTT_PERIOD_S) * 1000000;
            air_sample_t snap;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            snap = s_sample;
            xSemaphoreGive(s_lock);
            ha_mqtt_publish(&snap);
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void get_sample_for_web(air_sample_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_sample;
    xSemaphoreGive(s_lock);
    if (out->valid && s_last_read_us) {
        out->age_s = (uint32_t)((esp_timer_get_time() - s_last_read_us) / 1000000);
    }
}

static void status_message(char *buf, size_t len)
{
    const uint32_t age = s_last_read_us
        ? (uint32_t)((esp_timer_get_time() - s_last_read_us) / 1000000) : UINT32_MAX;

    if (!s_sensor_ok) {
        snprintf(buf, len, "%s", T(STR_NO_SENSOR));
    } else if (!s_had_reading) {
        snprintf(buf, len, "%s", T(STR_WARMING));
    } else if (age > SAMPLE_STALE_S) {
        snprintf(buf, len, "%s", T(STR_NOT_RESPONDING));
    } else if (net_state() == NET_PORTAL) {
        snprintf(buf, len, T(STR_SETUP_AT), net_ap_ssid());
    } else {
        buf[0] = '\0';
    }
}

static void ui_timer_cb(lv_timer_t *t)
{
    (void)t;
    air_sample_t snap;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snap = s_sample;
    xSemaphoreGive(s_lock);

    char time_str[8] = "--:--";
    const time_t now = time(NULL);
    if (now > 1700000000) {
        struct tm lt;
        localtime_r(&now, &lt);
        strftime(time_str, sizeof(time_str), "%H:%M", &lt);
    }

    char msg[48];
    status_message(msg, sizeof(msg));

    ui_update(&snap);
    int bat_pct = -1;
    bool charging = false;
    pmu_status_t b;
    if (pmu_available() && pmu_read(&b) == ESP_OK && b.present) {
        bat_pct = b.percent;
        charging = b.charging;
    }
    ui_set_ip(net_ip());
    ui_set_status(time_str, net_state() == NET_CONNECTED, ha_mqtt_connected(), msg,
                  bat_pct, charging);
    ui_set_on_battery(s_on_battery);
    ui_tick_1s();
}

void app_main(void)
{
    ESP_LOGI(TAG, "%s v%s", APP_NAME, APP_VERSION);

    ESP_ERROR_CHECK(settings_load());
    const settings_t *cfg = settings_get();
    i18n_set_lang(i18n_lang_from_code(cfg->lang));
    setenv("TZ", cfg->tz, 1);
    tzset();

    s_lock = xSemaphoreCreateMutex();
    air_sample_clear(&s_sample);

    s_hist = heap_caps_malloc(sizeof(air_history_t), MALLOC_CAP_SPIRAM);
    if (!s_hist) s_hist = malloc(sizeof(air_history_t));
    ESP_ERROR_CHECK(s_hist ? ESP_OK : ESP_ERR_NO_MEM);
    air_history_init(s_hist, 60);

    ESP_ERROR_CHECK(display_init());

    ESP_ERROR_CHECK(rtc_pcf85063_init(display_i2c_bus()));
    pmu_init(display_i2c_bus());
    pmu_set_charge_current(500);
    if (sound_init(display_i2c_bus()) == ESP_OK) {
        sound_set_volume(settings_get()->alarm_volume);
        mic_init(display_i2c_bus());
    }
    clock_from_rtc();
    net_set_time_sync_cb(clock_to_rtc);

    s_sensor_ok = (sen66_init(true) == ESP_OK);
    if (s_sensor_ok) {
        sensor_configure();
        voc_state_restore();
        if (sen66_start() != ESP_OK) {
            ESP_LOGE(TAG, "no arranco la medicion");
            s_sensor_ok = false;
        }
    }

    const ui_config_t uicfg = {
        .page_dwell_s = cfg->page_dwell_s,
        .pages_mask = cfg->pages_mask,
        .chart_span_min = cfg->chart_span_min,
        .brightness = cfg->brightness,
        .night_brightness = cfg->night_brightness,
        .screen_timeout_s = cfg->screen_timeout_s,
        .burnin_idle = cfg->burnin_idle,
        .burnin_active = cfg->burnin_active,
    };
    lvgl_port_lock(0);
    ui_init(s_hist, &uicfg, display_set_brightness);
    lv_timer_create(ui_timer_cb, 1000, NULL);
    lvgl_port_unlock();

    ESP_ERROR_CHECK(net_init());
    ESP_ERROR_CHECK(net_start());
    ESP_ERROR_CHECK(webcfg_start(get_sample_for_web));
    webcfg_set_co2_recal(recal_request, recal_status);
    webcfg_set_fan(fan_request);
    ha_mqtt_start();

    if (pmu_available()) {
        pmu_status_t b;
        if (pmu_read(&b) == ESP_OK) {
            ESP_LOGI(TAG, "bateria: %s, %d%%, %u mV%s%s",
                     b.present ? "presente" : "ausente", b.percent, b.millivolts,
                     b.charging ? ", cargando" : "", b.vbus ? ", con USB" : "");
        }
        uint16_t in_ma = 0, chg_ma = 0;
        if (pmu_charge_limits(&in_ma, &chg_ma) == ESP_OK) {
            ESP_LOGI(TAG, "limites: entrada USB %u mA, carga %u mA", in_ma, chg_ma);
        }
    }

    xTaskCreatePinnedToCore(sensor_task, "sen66", 4096, NULL, 5, NULL, 1);

    ESP_LOGI(TAG, "arranque completo (heap interno libre: %u KB, PSRAM: %u KB)",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}