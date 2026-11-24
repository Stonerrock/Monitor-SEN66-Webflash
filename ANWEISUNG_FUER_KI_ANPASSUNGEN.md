KI-Anweisung: Projekt-Anpassungen für Monitor-SEN66 (AirRing)

Dieses Dokument dient als vollständige Handlungsanweisung für eine KI, falls der Upstream-Quellcode (Original-Repository von socquique/Monitor-SEN66) aktualisiert oder neu gemergt wurde.

Es beschreibt präzise alle Modifikationen, die in unserem Fork vorgenommen wurden und wiederhergestellt werden müssen.

1. Übersicht der Ziel-Hardware & Umgebung

Board: Waveshare ESP32-S3-Touch-AMOLED-1.75 (466×466 Pixel, rund).

Sensor: Sensirion SEN66 an I2C-Port 1 (Erweiterungsleiste, SDA=IO17, SCL=IO18).

Display-Treiber: CO5300 (QSPI) + CST9217 (Touch) + LVGL 9.

Sprache: Deutsch (UI, Webserver, Dokumentation und Code-Kommentare); Home Assistant MQTT Discovery dynamisch in der HA-Benutzersprache.

WLAN-Anforderung: Kompatibilität mit AVM FRITZ!Box im Modus WPA2 + WPA3 (Transition Mode) mit PMF.

2. Modifikationen Datei für Datei

Datei 1: sdkconfig.defaults

Ziel: WPA3-SAE-Kryptografie einkompilieren.

Füge folgende Zeile hinzu:

CONFIG_ESP_WIFI_ENABLE_WPA3_SAE=y


Datei 2: main/net.c

Ziel: PMF aktivieren, 802.11-Disconnect-Gründe im Klartext auf Deutsch loggen und alle Statusmeldungen übersetzen.

PMF in net_start() aktivieren:
In der Konfiguration wifi_config_t sta:

sta.sta.threshold.authmode = cfg->wifi_pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
sta.sta.pmf_cfg.capable = true;
sta.sta.pmf_cfg.required = false;


Hilfsfunktion wifi_reason_to_str() oberhalb von wifi_event() einfügen:

static const char *wifi_reason_to_str(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_EXPIRE:
        return "Authentifizierung abgelaufen";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        return "4-Wege-Handshake Timeout (Passwort pruefen)";
    case WIFI_REASON_NO_AP_FOUND:
        return "WLAN nicht gefunden (SSID/Kanal/2.4 GHz pruefen)";
    case WIFI_REASON_AUTH_FAIL:
        return "Authentifizierung fehlgeschlagen (WPA/WPA3 pruefen)";
    case WIFI_REASON_ASSOC_FAIL:
        return "Assoziierung fehlgeschlagen";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return "Handshake Timeout";
    case WIFI_REASON_CONNECTION_FAIL:
        return "Verbindungsaufbau fehlgeschlagen";
    case WIFI_REASON_BEACON_TIMEOUT:
        return "AP-Signal verloren (Beacon Timeout)";
    default:
        return "Anderer Grund";
    }
}


In wifi_event() bei WIFI_EVENT_STA_DISCONNECTED den Grund loggen:

} else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    const wifi_event_sta_disconnected_t *ev = (const wifi_event_sta_disconnected_t *)data;
    if (ev != NULL) {
        ESP_LOGW(TAG, "WLAN getrennt (Grund %u: %s)",
                 (unsigned int)ev->reason, wifi_reason_to_str(ev->reason));
    }
    s_ip[0] = '\0';
    s_state = s_portal_up ? NET_PORTAL : NET_CONNECTING;
    if (++s_fails == FAILS_BEFORE_PORTAL && !s_portal_up) {
        ESP_LOGW(TAG, "%d Fehlversuche in Folge: Konfigurationsportal wird geoeffnet", s_fails);
        start_portal();
    }
    ...
} else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    ...
    ESP_LOGI(TAG, "Verbunden, IP: %s", s_ip);
    start_sntp();
}


Alle weiteren Logmeldungen in net.c auf Deutsch umstellen:

SNTP: ESP_LOGI(TAG, "Uhrzeit ueber SNTP synchronisiert");

Portal: ESP_LOGI(TAG, "Konfigurationsportal geoeffnet: WLAN '%s' -> http://192.168.4.1", s_ap_ssid);

Ohne Netz: ESP_LOGW(TAG, "Kein WLAN konfiguriert");

Verbindungsaufbau: ESP_LOGI(TAG, "Verbinde mit '%s'...", cfg->wifi_ssid);

Stromsparmodus: ESP_LOGI(TAG, "WLAN-Stromsparmodus %s", enabled ? "aktiviert" : "deaktiviert");

Datei 3: main/ha_mqtt.c

Ziel: Automatische Mehrsprachigkeit in Home Assistant (keine hartkodierten spanischen Namen).

Standard-Sensoren mit device_class ohne "name"-Feld publizieren:

In publish_discovery_one(air_metric_t m): Das Feld "name" nur senden, wenn device_class(m) leer ist (!dclass[0], z. B. für VOC, NOx, PM4). Bei Sensoren mit device_class (pm1, pm25, pm10, humidity, temperature, carbon_dioxide, sound_pressure) übernimmt Home Assistant automatisch die Benennung in der Benutzersprache.

In publish_discovery_battery(): Bei allen 4 Batterie-Entitäten (battery, bat_mv, charging, usb) das "name"-Feld komplett entfernen, da alle eine Standard-device_class besitzen.

Luftqualität (level) sprachabhängig über cfg->lang benennen:

Hilfsfunktion einfügen (ohne Abhängigkeit zu i18n.h):

static const char *air_quality_label(const char *lang)
{
    if (lang) {
        if (strcmp(lang, "de") == 0) return "Luftqualität";
        if (strcmp(lang, "en") == 0) return "Air quality";
    }
    return "Calidad del aire";
}


In publish_discovery_extras() für level/config "name":"%s" mit air_quality_label(cfg->lang) befüllen.

Datei 4: main/display.c

Ziel: Zuverlässige CO5300-Helligkeitsfreigabe (0x53) nach jedem Display-Off.

In display_set_brightness(uint8_t level) beim Abschalten (level == 0) das Flag s_bctrl_init = false; zurücksetzen, damit nach dem Aufwecken (esp_lcd_panel_disp_on_off(s_panel, true)) das Register LCD_CMD_WRCTRLD (0x53) mit 0x20 erneut gesendet wird.

Datei 5: main/webcfg.c

Ziel: Deutsche Luftqualitätsstufen, frei einstellbare Dimmer-Zeit in Sekunden und Live-Vorschau für beide Helligkeitsregler inkl. LVGL-Overlay.

Mapping NIV im Frontend-JavaScript erweitern:

const NIV={good:'Gut',fair:'Mäßig',moderate:'Mittel',poor:'Schlecht',bad:'Sehr schlecht',
           BUENO:'Gut',ACEPTABLE:'Mäßig',REGULAR:'Mittel',MALO:'Schlecht','MUY MALO':'Sehr schlecht'};


Freies Zahlenfeld für „Abdunkeln nach“:

Im HTML das <select name=screen_timeout_s> ersetzen durch:

<div><label>Abdunkeln nach (Sekunden, 0 = Nie)</label><input name=screen_timeout_s type=number min=0 max=86400></div>


In h_settings_post() auf 0..65535 begrenzen.

Live-Vorschau für beide Helligkeitsregler inkl. LVGL-Overlay:

#include "esp_lvgl_port.h" einbinden.

In JS vivo(e): Sowohl bei brightness als auch bei night_brightness fetch('/api/brightness?v='+e.value,{method:'POST'}) senden.

In h_brightness(): Wertebereich 0..255 zulassen (inkl. 0 = Display aus) und unter lvgl_port_lock(50) ui_set_live_brightness((uint8_t)b) aufrufen (Fallback: display_set_brightness). Ebenso in h_settings_post().

Datei 6: main/ui.h & main/ui.c

Ziel: 1,75" AMOLED optimal ausnutzen (446 px Ringe), Display zentrieren (+4 px), Ringe synchronisieren, Schriften vergrößern, Software-Dimming + Smooth Fading, funktionierendes Touch-Wakeup und warnungsfreier LVGL-9-Build.

A. Header main/ui.h:

Neue Funktion deklarieren:

void ui_set_live_brightness(uint8_t level);


B. Konstanten am Dateianfang von main/ui.c:

#define EDGE 10
#define UI_X_OFFSET 4
#define RING_D 446
#define RING_INNER_R ((RING_D) / 2 - 22)

#define FADE_DIM_MS  500
#define FADE_WAKE_MS 250


C. Kombiniertes Hardware-/Software-Dimming & Smooth Fading:

Schwarzes Overlay s_dim_overlay auf lv_layer_top() erzeugen (Größe SCR_W × SCR_H, lv_obj_set_clickable(s_dim_overlay, false)).

apply_brightness_immediate(uint8_t level) setzt die Overlay-Deckkraft (level == 0 ? LV_OPA_COVER : (255 - level)) und ruft den Hardware-Callback s_set_brightness(level) auf.

fade_brightness(uint8_t target, uint32_t duration_ms) animiert s_cur_brightness über lv_anim_t (lv_anim_path_ease_out).

ui_tick_1s() dimmt mit FADE_DIM_MS (500 ms).

ui_wake() weckt mit FADE_WAKE_MS (250 ms).

ui_set_live_brightness(level) setzt sofort (duration_ms = 0).

D. Touch-Wakeup reparieren:

on_activity auf scr (LV_EVENT_PRESSED, LV_EVENT_CLICKED) und auf s_tv (LV_EVENT_PRESSED, LV_EVENT_SCROLL_BEGIN) registrieren.

Auf allen erzeugten Tiles lv_obj_set_event_bubble(tile, true) setzen.

In chart_create() lv_obj_set_clickable(c, false) setzen.

In on_tile_change() zusätzlich ui_wake() aufrufen.

E. Layout, Typografie & Seiten-Optimierungen:

mini_create(): Label-Schrift ui_font_22, mit UI_X_OFFSET.

gauge_create(): Zentriert auf UI_X_OFFSET; identische Dicke (thickness) und abgerundete Enden für LV_PART_MAIN und LV_PART_INDICATOR.

build_chrome(): Uhrzeit ui_font_48 (y=-170), WiFi ui_font_16 (y=-128), Akku ui_font_16 (y=156), Seitenpunkte mit UI_X_OFFSET.

Übersicht (build_summary): Status ui_font_48 (y=-76), Sub ui_font_28 (y=-32), 2×2-Raster (dx = ±96, dy = 18 / 86, ui_font_28).

CO₂ (build_co2) & Lärm (build_noise): Ring RING_D, Caption ui_font_28 (y=-78), Wert ui_font_48 (y=-22), Level ui_font_28 (y=48), Diagramm y=108.

Partikel (build_pm): Ring RING_D, Caption ui_font_28 (y=-82), Wert ui_font_48 (y=-26), Level ui_font_28 (y=44), PM1/4/10 dx = {-114, 0, 114} bei y=76 (ui_font_28), Diagramm y=108.

Gase (build_gas): Titel ui_font_28 (y=-88), Ringe 188 px (Container 196×196) bei x = ±106, Einheit/Caption/Hinweis ui_font_22, Diagramm y=108.

Klima (build_climate): Zwei farbige Spaltenköpfe („Temperatur“ Orange, „Feuchte“ Blau, ui_font_28, x = ±116, y = -72), Werte ui_font_48 (y=-22), echtes Grad-Zeichen "%s °C", Diagramm y=86.

Ruhemodus (build_idle): Status ui_font_48 (y=-100), Lärm ui_font_16 / ui_font_22 (y=-62), 3×3-Raster mit Spaltenbreite 122 px (ui_font_16 / ui_font_28), IP unten ui_font_16 (y=142).

F. Clean Build (LVGL 9) & Sprache:

Keine veralteten lv_obj_add_flag / lv_obj_remove_flag-Aufrufe verwenden; stattdessen konsequent lv_obj_set_hidden(), lv_obj_set_clickable(), lv_obj_set_scrollable() und lv_obj_set_event_bubble() nutzen.

Alle Kommentare in main/ui.c auf Deutsch halten.

Invarianten beachten: Keine ESP-IDF-Header in main/ui.c einbinden (Simulator-Kompatibilität erhalten).

3. Wichtige Fallstricke bei der Umsetzung

Keine Backslash-Escapes in String-Literals erzeugen:
Strings im C-Code müssen sauber als "%s %s", "%s °C" und "" geschrieben werden (nicht \").

Windows Zeilenenden (CRLF vs. LF):
Beim Patchen von Dateien immer auf das Zeilenende-Format der lokalen Datei achten.

Partitionierung & OTA:
Die partitions.csv mit zwei 4-MB-App-Slots unverändert beibehalten.