# KI-Anweisung: Projekt-Anpassungen für Monitor-SEN66 (AirRing)

Dieses Dokument dient als **vollständige Handlungsanweisung für eine KI**, falls der Upstream-Quellcode (Original-Repository von `socquique/Monitor-SEN66`) aktualisiert oder neu gemergt wurde.

Es beschreibt präzise alle Modifikationen, die in unserem Fork vorgenommen wurden und wiederhergestellt werden müssen.

---

## 1. Übersicht der Ziel-Hardware & Umgebung

- **Board:** Waveshare ESP32-S3-Touch-AMOLED-1.75 (466×466 Pixel, rund).
- **Sensor:** Sensirion SEN66 an I2C-Port 1 (Erweiterungsleiste, SDA=IO17, SCL=IO18).
- **Display-Treiber:** CO5300 (QSPI) + CST9217 (Touch) + LVGL 9.
- **Sprache:** Deutsch (UI, Webserver, Dokumentation und Code-Kommentare).
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

2. **Hilfsfunktion `wifi_reason_to_str()` oberhalb von `wifi_event()` einfügen:**
   ```c
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
   ```

3. **In `wifi_event()` bei `WIFI_EVENT_STA_DISCONNECTED` den Grund loggen:**
   ```c
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
   ```

4. **Alle weiteren Logmeldungen in `net.c` auf Deutsch umstellen:**
   - SNTP: `ESP_LOGI(TAG, "Uhrzeit ueber SNTP synchronisiert");`
   - Portal: `ESP_LOGI(TAG, "Konfigurationsportal geoeffnet: WLAN '%s' -> http://192.168.4.1", s_ap_ssid);`
   - Ohne Netz: `ESP_LOGW(TAG, "Kein WLAN konfiguriert");`
   - Verbindungsaufbau: `ESP_LOGI(TAG, "Verbinde mit '%s'...", cfg->wifi_ssid);`
   - Stromsparmodus: `ESP_LOGI(TAG, "WLAN-Stromsparmodus %s", enabled ? "aktiviert" : "deaktiviert");`

---

### Datei 3: `main/webcfg.c`

**Ziel:** Spanische Luftqualitätsrückgaben (`MALO`, `BUENO` etc.) im Webpanel auf Deutsch mappen.

Erweitere im JavaScript der Weboberfläche das Objekt `NIV` (ca. Zeile 197):
```javascript
const NIV={good:'Gut',fair:'Mäßig',moderate:'Mittel',poor:'Schlecht',bad:'Sehr schlecht',
           BUENO:'Gut',ACEPTABLE:'Mäßig',REGULAR:'Mittel',MALO:'Schlecht','MUY MALO':'Sehr schlecht'};
```

---

### Datei 4: `main/ui.c`

**Ziel:** 1,75" AMOLED optimal ausnutzen (446 px Ringe), Display zentrieren (+4 px), Ringe synchronisieren, Schriften massiv vergrößern und Layouts optimieren.

#### A. Konstanten am Dateianfang:
```c
// Randabstand, damit nichts auf den gebogenen Rand des runden Displays faellt.
#define EDGE 10

// Horizontale Verschiebung (in Pixeln) zur Zentrierung auf dem runden Display
#define UI_X_OFFSET 4

// Maximaler Ring-Durchmesser zur vollstaendigen Ausnutzung der 1.75" Diagonale (466x466)
#define RING_D 446

// Nutzbarer Innenraum im Ring
#define RING_INNER_R ((RING_D) / 2 - 22)
```

#### B. `mini_create()`:
Schrift des Beschriftungs-Labels von 14 auf `ui_font_22` vergrößern, `UI_X_OFFSET` berücksichtigen:
```c
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
```

#### C. `gauge_create()`:
- `lv_obj_align(g->arc, LV_ALIGN_CENTER, UI_X_OFFSET, 0);` statt `lv_obj_center`.
- **Wichtig:** Identische Dicke für `LV_PART_MAIN` und `LV_PART_INDICATOR` (`thickness`), damit der bunte Ring deckungsgleich auf dem blau-grauen Hintergrundring liegt:
  ```c
  lv_obj_set_style_arc_width(g->arc, thickness, LV_PART_MAIN);
  lv_obj_set_style_arc_width(g->arc, thickness, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(g->arc, col(0x1e293b), LV_PART_MAIN);
  lv_obj_set_style_arc_color(g->arc, col(0x22c55e), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(g->arc, true, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(g->arc, true, LV_PART_INDICATOR);
  ```
- Beschriftungen auf `UI_X_OFFSET` zentrieren.

#### D. Statusleiste & Chrome (`build_chrome`):
- Uhrzeit: **48 px (`ui_font_48`)** bei `y = -170`.
- WiFi: **16 px (`ui_font_16`)** bei `y = -128`.
- Akku: **16 px (`ui_font_16`)** bei `y = 156`.
- Seitenpunkte (`layout_dots`): `x0 = -(s_cols - 1) * gap / 2 + UI_X_OFFSET;`
- Alle Positionen nutzen `UI_X_OFFSET`.

#### E. Seite 1: Übersicht (`build_summary`):
- Hauptstatus (`s_sum_center`): **`ui_font_48`** bei `y = -76`.
- Untertitel (`s_sum_sub`): **`ui_font_28`** bei `y = -32`.
- 2×2-Raster mit großem Abstand: `dx = {-96, 96, -96, 96}`, `dy = {18, 18, 86, 86}`.
- Werte in **`ui_font_28`** (über `mini_create`).

#### F. Seite 2: CO₂ (`build_co2`):
- Diagramm: `lv_obj_align(s_co2_chart, LV_ALIGN_CENTER, UI_X_OFFSET, 108);`
- Gauge: `gauge_create(&s_g_co2, tile, RING_D, 22, &ui_font_48, &ui_font_28);`
- Bogenbeschriftung ("CO2"): **`ui_font_28`** bei `y = -78`.
- Messwert: nach unten gerückt auf `y = -22` (`ui_font_48`).
- Statuslevel ("GUT" etc.): **`ui_font_28`** bei `y = 48`.

#### G. Seite 3: Partikel (`build_pm`):
- Diagramm: `lv_obj_align(s_pm_chart, LV_ALIGN_CENTER, UI_X_OFFSET, 108);`
- Gauge für PM2.5 im Zentrum (Ring, Beschriftung und Wert vergrößert):
  ```c
  gauge_create(&s_g_pm, tile, RING_D, 22, &ui_font_48, &ui_font_28);
  lv_obj_set_style_text_font(s_g_pm.caption, &ui_font_28, 0);
  lv_obj_align(s_g_pm.caption, LV_ALIGN_CENTER, UI_X_OFFSET, -82);
  lv_obj_align(s_g_pm.value, LV_ALIGN_CENTER, UI_X_OFFSET, -26);
  ```
- Statuslevel ("GUT" etc.): **`ui_font_28`** bei `y = 44`.
- Untere 3 Werte (PM1.0, PM4.0, PM10): breiter aufgeteilt mit `dx = {-114, 0, 114}` bei `y = 76` in `&ui_font_28`.
- In `ui_update()` unter `case PAGE_PM:`:
  ```c
  gauge_update(&s_g_pm, AIR_PM25, s->v[AIR_PM25]);
  lv_obj_align(s_g_pm.value, LV_ALIGN_CENTER, 0, -48);
  lv_obj_align_to(s_g_pm.unit, s_g_pm.value, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
  ```

#### H. Seite 4: Gase (`build_gas`):
- Titel: **`ui_font_28`** bei `y = -88`.
- Ringe (VOC & NOx): Größe von 172 auf **188 px** vergrößert, Container 196×196, weiter nach außen gerückt (`x = ±106`).
- Einheit & Bogenbeschriftung: **`ui_font_22`**.
- Hinweistext unten: **`ui_font_22`** bei `y = 122`.

#### I. Seite 5: Klima (`build_climate`):
- Kein einzelner Titel "Klima", sondern **zwei Spaltenköpfe in jeweiliger Kurvenfarbe**:
  - Links: „Temperatur“ (`lbl_temp`) in Orange (`0xf59e0b`, `ui_font_28`) bei `x = -116, y = -72`.
  - Rechts: „Feuchte“ (`lbl_hum`) in Blau (`0x38bdf8`, `ui_font_28`) bei `x = +116, y = -72`.
- Große Werte darunter bei `y = -22` (**`ui_font_48`**).
- Diagramm bei `y = 86`.
- **Wichtig in `ui_update()`:** Echtes Grad-Zeichen verwenden:
  ```c
  snprintf(buf, sizeof(buf), "%s °C", buf2);
  ```

#### J. Seite 6: Lärm (`build_noise`):
- Gauge: `gauge_create(&s_g_noise, tile, RING_D, 22, &ui_font_48, &ui_font_28);`
- Bogenbeschriftung ("Lärm"): **`ui_font_28`** bei `y = -78`.
- Messwert: `ui_font_48` bei `y = -22`.
- Statuslevel: **`ui_font_28`** bei `y = 48`.
- Diagramm: bei `y = 108`.

#### K. Ruhemodus (`build_idle`):
- Gesamtstatus (`s_idle_level`): **`ui_font_48`** bei `y = -100`.
- Lärm: Name `ui_font_16`, Wert **`ui_font_22`** bei `y = -62`.
- 3×3-Matrix:
  - Spaltenbreite von 112 auf **122 px** (`dx = (i % 3 - 1) * 122 + UI_X_OFFSET`).
  - Namen: **`ui_font_16`**.
  - Werte: **`ui_font_28`**.
- IP-Adresse unten: **`ui_font_16`** bei `y = 142`.

#### L. Deutsche Quellcode-Kommentare:
Alle Kommentare in `main/ui.c` müssen auf Deutsch verfasst sein.

---

## 3. Wichtige Fallstricke bei der Umsetzung

1. **Keine Backslash-Escapes in String-Literals erzeugen:**
   Strings im C-Code müssen sauber als `"%s %s"`, `"%s °C"` und `""` geschrieben werden (nicht `\"`).
2. **Windows Zeilenenden (CRLF vs. LF):**
   Beim Patchen von Dateien immer auf das Zeilenende-Format der lokalen Datei achten.
3. **Partitionierung & OTA:**
   Die `partitions.csv` mit zwei 4-MB-App-Slots unverändert beibehalten.
