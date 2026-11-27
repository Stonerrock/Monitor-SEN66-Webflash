# KI-Anweisung: Projekt-Anpassungen für Monitor-SEN66 (AirRing)

Dieses Dokument dient als **vollständige Handlungsanweisung für eine KI**, falls der Upstream-Quellcode (Original-Repository von `socquique/Monitor-SEN66`) aktualisiert oder neu gemergt wurde.

Es beschreibt präzise alle Modifikationen, die in unserem Fork vorgenommen wurden und wiederhergestellt werden müssen.

---

## 1. Übersicht der Ziel-Hardware & Umgebung

- **Board:** Waveshare ESP32-S3-Touch-AMOLED-1.75 (466×466 Pixel, rund).
- **Sensor:** Sensirion SEN66 an I2C-Port 1 (Erweiterungsleiste, SDA=IO17, SCL=IO18).
- **Display-Treiber:** CO5300 (QSPI) + CST9217 (Touch) + LVGL 9.
- **Sprache:** Deutsch (UI, Webserver, Dokumentation und Code-Kommentare); Home Assistant MQTT Discovery dynamisch in der HA-Benutzersprache.
- **WLAN-Anforderung:** Kompatibilität mit AVM FRITZ!Box im Modus **WPA2 + WPA3 (Transition Mode)** mit PMF.

---

## 2. Modifikationen Datei für Datei

### Datei 1: `sdkconfig.defaults`

**Ziel:** WPA3-SAE-Kryptografie einkompilieren.

Füge folgende Zeile hinzu:
```ini
CONFIG_ESP_WIFI_ENABLE_WPA3_SAE=y
```

---

### Datei 2: `main/net.c`

**Ziel:** PMF aktivieren, 802.11-Disconnect-Gründe im Klartext auf Deutsch loggen und alle Statusmeldungen übersetzen.

1. **PMF in `net_start()` aktivieren:**
   In der Konfiguration `wifi_config_t sta`:
   
```c
   sta.sta.threshold.authmode = cfg->wifi_pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
   sta.sta.pmf_cfg.capable = true;
   sta.sta.pmf_cfg.required = false;
   
```

2. **Hilfsfunktion `wifi_reason_to_str()` oberhalb von `wifi_event()` einfügen** und in `WIFI_EVENT_STA_DISCONNECTED` loggen; alle Status-Logs (`net.c`) auf Deutsch umstellen.

---

### Datei 3: `main/ha_mqtt.c`

**Ziel:** Automatische Mehrsprachigkeit in Home Assistant (keine hartkodierten spanischen Namen).

1. **Standard-Sensoren mit `device_class` ohne `"name"`-Feld publizieren:**
   - In `publish_discovery_one(air_metric_t m)`: `"name"` nur senden, wenn `device_class(m)` leer ist (`!dclass[0]`). Bei Standard-Klassen übernimmt Home Assistant automatisch die Benennung in der HA-Benutzersprache.
   - In `publish_discovery_battery()`: Bei allen 4 Batterie-Entitäten (`battery`, `bat_mv`, `charging`, `usb`) das `"name"`-Feld entfernen.
2. **Luftqualität (`level`) sprachabhängig über `cfg->lang` benennen:**
   - Hilfsfunktion `air_quality_label(const char *lang)` einfügen (`"de"` → `"Luftqualität"`, `"en"` → `"Air quality"`, Fallback `"Calidad del aire"`) und in `publish_discovery_extras()` für `level/config` verwenden.

---

### Datei 4: `main/display.c`

**Ziel:** Zuverlässige CO5300-Helligkeitsfreigabe (`0x53`) nach jedem Display-Off.

- In `display_set_brightness(uint8_t level)` beim Abschalten (`level == 0`) das Flag `s_bctrl_init = false;` zurücksetzen, damit nach dem Aufwecken das Register `LCD_CMD_WRCTRLD` (`0x53`) mit `0x20` erneut gesendet wird.

---

### Datei 5: `main/settings.h`, `main/settings.c` & `main/app_main.c`

**Ziel:** Persistente Optionen für AMOLED Burn-in-Schutz (NVS-abwärtskompatibel).

- **Am Ende von `settings_t` in `main/settings.h` anhängen:**
  
```c
  bool burnin_idle;        // im gedimmten Modus / Ruheansicht
  bool burnin_active;      // im ungedimmten Normalbetrieb
  
```
- **In `settings_defaults()` (`main/settings.c`):** `s->burnin_idle = true; s->burnin_active = false;` setzen.
- **In `app_main()` (`main/app_main.c`):** Beide Felder beim Erzeugen von `ui_config_t uicfg` an `ui_init()` übergeben.

---

### Datei 6: `main/webcfg.c`

**Ziel:** Deutsche Luftqualitätsstufen, frei einstellbare Dimmer-Zeit, Burn-in-Checkboxen, Live-Vorschau für beide Helligkeitsregler und Übernahme ohne Neustart.

1. **Mapping `NIV` im Frontend-JavaScript erweitern** (englische + spanische Schlüssel → Deutsch).
2. **Freies Zahlenfeld für „Abdunkeln nach“:**
   `<input name=screen_timeout_s type=number min=0 max=86400>` statt `<select>`.
3. **Burn-in-Schutz Checkboxen:**
   `bi_idle` (*Im gedimmten Modus*) und `bi_act` (*Im ungedimmten Modus*) im Formular, in `h_settings_get()`, `h_settings_post()` und `h_backup()` (JSON-Puffer auf 1024 Byte erhöhen).
4. **Live-Vorschau für beide Helligkeitsregler inkl. LVGL-Overlay:**
   `#include "esp_lvgl_port.h"`; JS `vivo(e)` sendet für `brightness` und `night_brightness` an `/api/brightness`; `h_brightness()` erlaubt `0..255` und ruft unter `lvgl_port_lock(50)` `ui_set_live_brightness()` auf.
5. **Einstellungen ohne Neustart übernehmen:**
   - In `h_settings_post()` vor dem Überschreiben `const settings_t prev = *c;` sichern.
   - Nach dem Parsen `ui_apply_config(&uicfg)` unter `lvgl_port_lock(50)` sowie `sound_set_volume(c->alarm_volume)` aufrufen und `settings_save()` ausführen.
   - `need_reboot` nur dann auf `true` setzen, wenn sich `wifi_ssid`, `wifi_pass`, `mqtt_uri`, `mqtt_user`, `mqtt_pass`, `mqtt_prefix`, `device_name`, `lang`, `tz`, `ntp`, `pages_mask`, `temp_offset_dc`, `altitude_m` oder `co2_asc` geändert haben.
   - HTTP-Antwort `"reboot"` (mit `reboot_task`) bzw. `"ok"` (ohne Neustart); Frontend-Meldung entsprechend anpassen und Button-Text auf `Speichern` ändern.

---

### Datei 7: `main/ui.h` & `main/ui.c`

**Ziel:** 1,75" AMOLED optimal ausnutzen (446 px Ringe), Display zentrieren (+4 px), Ringe synchronisieren, Schriften vergrößern, Software-Dimming + Smooth Fading, Touch-Wakeup, Burn-in-Schutz (Pixel-Shift), Live-Config-Übernahme und warnungsfreier LVGL-9-Build.

#### A. Header `main/ui.h`:
- `ui_config_t` um `bool burnin_idle;` und `bool burnin_active;` erweitern.
- Neue API-Funktionen deklarieren:
  
```c
  void ui_apply_config(const ui_config_t *cfg);
  void ui_set_live_brightness(uint8_t level);
  
```

#### B. Konstanten am Dateianfang von `main/ui.c`:
```c
#define EDGE 10
#define UI_X_OFFSET 4
#define RING_D 446
#define RING_INNER_R ((RING_D) / 2 - 22)

#define FADE_DIM_MS     500
#define FADE_WAKE_MS    250
#define BURNIN_PERIOD_S 60
```

#### C. Kombiniertes Hardware-/Software-Dimming & Smooth Fading:
- Schwarzes Overlay `s_dim_overlay` auf `lv_layer_top()` (unklickbar).
- `apply_brightness_immediate(level)` steuert Overlay-Deckkraft (`255 - level`, bei `0` → `LV_OPA_COVER`) und `s_set_brightness(level)`.
- `fade_brightness(target, duration_ms)` animiert weich per `lv_anim_t` (`lv_anim_path_ease_out`): `FADE_DIM_MS` beim Abdunkeln, `FADE_WAKE_MS` beim Aufwecken, `0 ms` für Live-Regler.

#### D. Touch-Wakeup & Burn-in-Schutz (Pixel-Shift):
- Alle UI-Inhalte (`s_tv`, `s_idle_panel`, Chrome) in einem gemeinsamen Container `s_root` (`SCR_W × SCR_H`, `lv_obj_set_event_bubble(s_root, true)`) platzieren.
- `on_activity` auf `scr` und `s_tv` registrieren; Event-Bubbling auf allen Tiles aktivieren; Diagramme nicht-klickbar machen.
- In `ui_tick_1s()` bei aktivem Schutz (`s_cur_brightness > 0` und `s_dimmed ? s_cfg.burnin_idle : s_cfg.burnin_active`) alle 60 s `s_root` über eine 9-Punkte-Tabelle um `±3 px` verschieben; sonst `burnin_reset()` auf `(0, 0)`.

#### E. Live-Übernahme (`ui_apply_config`):
- Aktualisiert `brightness`, `night_brightness`, `screen_timeout_s`, `chart_span_min`, `burnin_idle`, `burnin_active` direkt im laufenden Betrieb, passt Helligkeit/Wake-Zustand sofort an und setzt bei deaktiviertem Burn-in-Schutz den Pixel-Shift zurück.

#### F. Layout, Typografie, Clean Build (LVGL 9) & Sprache:
- Ringe 446 px, `UI_X_OFFSET = 4`, synchrone Ringbreite (`22 px`), vergrößerte Schriften (`ui_font_48` / `28` / `22` / `16`), getrennte farbige Spaltenköpfe auf der Klima-Seite, echtes Grad-Zeichen `"%s °C"`.
- Ausschließlich moderne LVGL-9-Setter (`lv_obj_set_hidden`, `lv_obj_set_clickable`, `lv_obj_set_scrollable`, `lv_obj_set_event_bubble`) verwenden – keine `deprecated`-Warnungen.
- Alle Kommentare in `main/ui.c` auf Deutsch; keine ESP-IDF-Header in `main/ui.c` einbinden (Simulator-Kompatibilität erhalten).

---

## 3. Wichtige Fallstricke bei der Umsetzung

1. **Keine Backslash-Escapes in String-Literals erzeugen:**
   Strings im C-Code müssen sauber als `"%s %s"`, `"%s °C"` und `""` geschrieben werden (nicht `\"`).
2. **Windows Zeilenenden (CRLF vs. LF):**
   Beim Patchen von Dateien immer auf das Zeilenende-Format der lokalen Datei achten.
3. **Partitionierung & OTA:**
   Die `partitions.csv` mit zwei 4-MB-App-Slots unverändert beibehalten.