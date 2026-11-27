# Zusammenfassung aller Anpassungen am Monitor SEN66 (AirRing)

Dieses Dokument fasst alle Änderungen, Fehlerbehebungen und Optimierungen zusammen, die ausgehend vom ursprünglichen Projektstand (Upstream-Firmware) durchgeführt wurden.

---

## 1. WLAN, WPA3 & Verbindungsstabilität

### 1.1 WPA3-Personal (SAE) & Protected Management Frames (PMF)
- **Problem:** Bei Routern wie der FRITZ!Box im Mischbetrieb **WPA2 + WPA3 (Transition Mode)** schlug der Verbindungsaufbau fehl, da dem ESP32 die WPA3-SAE-Kryptografie im SDK fehlte und PMF nicht passend ausgehandelt wurde.
- **`sdkconfig.defaults`:**
  - `CONFIG_ESP_WIFI_ENABLE_WPA3_SAE=y` ergänzt, damit der WPA3-Handshake (Simultaneous Authentication of Equals) einkompiliert wird.
- **`main/net.c`:**
  - PMF-Konfiguration in `net_start()` aktiviert:
    
```c
    sta.sta.pmf_cfg.capable = true;
    sta.sta.pmf_cfg.required = false;
    
```
    Erlaubt sowohl WPA2 als auch WPA3 und Mischbetrieb.

### 1.2 Deutsche Status- und Fehlerprotokollierung (Disconnect-Reason)
- **`main/net.c`:**
  - Hilfsfunktion `wifi_reason_to_str()` hinzugefügt, die 802.11-Disconnect-Fehlercodes (`wifi_err_reason_t`) direkt in verständliche deutsche Texte übersetzt (z. B. Authentifizierung abgelaufen, 4-Wege-Handshake-Timeout / falsches Passwort, AP nicht gefunden).
  - Alle Konsolen- und Systemmeldungen (SNTP, AP-Portal, Verbindungsaufbau, Stromsparmodus) von Spanisch auf Deutsch umgestellt.

---

## 2. Weboberfläche & Lokalisierung

### 2.1 Deutsche Luftqualitätsstufen im Webpanel
- **Problem:** In der Weboberfläche erschien die Luftqualität auf Spanisch (z. B. `MALO`), da die interne API `/api/state` spanische Strings lieferte, das JavaScript-Mapping aber nur englische Schlüssel (`poor`) erwartete.
- **`main/webcfg.c`:**
  - Das Mapping-Objekt `NIV` im Frontend-JavaScript erweitert:
    
```javascript
    const NIV={good:'Gut',fair:'Mäßig',moderate:'Mittel',poor:'Schlecht',bad:'Sehr schlecht',
               BUENO:'Gut',ACEPTABLE:'Mäßig',REGULAR:'Mittel',MALO:'Schlecht','MUY MALO':'Sehr schlecht'};
    
```
  - Damit werden spanische und englische Sensorrückgaben zuverlässig auf Deutsch angezeigt.

### 2.2 Home Assistant MQTT Discovery: Automatische Mehrsprachigkeit
- **Problem:** In Home Assistant wurden die Sensor-Namen trotz deutscher Geräte-Einstellung auf Spanisch angezeigt (*Humedad*, *Temperatura*, *Ruido*, *Calidad del aire* sowie Batterie-Diagnosewerte).
- **`main/ha_mqtt.c`:**
  - Bei allen Sensoren mit Standard-`device_class` (`temperature`, `humidity`, `carbon_dioxide`, `sound_pressure`, `pm1`, `pm25`, `pm10` sowie `battery`, `voltage`, `battery_charging`, `plug`) wird im MQTT-Discovery-Payload kein hartkodiertes `"name"`-Feld mehr gesendet. Home Assistant vergibt dadurch automatisch den passenden Namen **in der im jeweiligen HA-Profil eingestellten Sprache** (z. B. Deutsch oder Englisch).
  - Für die Gesamtbewertung (`level`, ohne `device_class`) wurde die Hilfsfunktion `air_quality_label(cfg->lang)` ergänzt, die je nach Gerätesprache `"Luftqualität"` (`de`), `"Air quality"` (`en`) oder `"Calidad del aire"` (`es`) sendet.
  - *Hinweis:* Damit Home Assistant neue Discovery-Namen übernimmt, muss das Gerät einmalig unter **Einstellungen → Geräte & Dienste → MQTT** gelöscht werden.

### 2.3 Einstellungen ohne Neustart übernehmen
- **Problem:** Ein Klick auf „Speichern“ löste bisher immer einen kompletten Neustart (`esp_restart()`) aus – auch wenn nur Helligkeit, Dimmer-Zeit, Burn-in-Schutz oder Alarmlautstärke geändert wurden.
- **`main/webcfg.c`, `main/ui.h`, `main/ui.c`:**
  - Neue Funktion `ui_apply_config()` übernimmt Änderungen an Helligkeit, gedimmter Helligkeit, Timeout, Diagramm-Zeitfenster und Burn-in-Schutz sofort im laufenden Betrieb.
  - Auch Alarm-Grenzen, Lautstärke und Lärm-Kalibrierung wirken unmittelbar ohne Neustart.
  - Ein Neustart wird nur noch ausgelöst, wenn sich WLAN-, MQTT-, Sprach-, Zeitzonen-, Seiten- oder Sensor-Hardware-Einstellungen (`temp_offset`, `altitude_m`, `co2_asc`) geändert haben.

---

## 3. Vollständige deutsche Dokumentation

### 3.1 `README_DEUTSCH.md`
- Die gesamte Dokumentation wurde als eigenständige Datei **`README_DEUTSCH.md`** ins Deutsche übersetzt:
  - Hardwareliste, Teile, Bezugsquellen und Hinweise
  - Exakte Pin-Verkabelungstabelle (SEN66 ↔ Erweiterungsleiste) inklusive I2C-Besonderheiten (Bus-Trennung wegen Adresskollision mit IMU `0x6B`)
  - Kompilierung mit ESP-IDF, Flashen, Ersteinrichtung über Captive Portal
  - Home-Assistant-MQTT-Autodiscovery und HomeKit/Homebridge-Anbindung
  - Langzeitstatistik vs. detaillierter Verlauf (Recorder-Konfiguration)
  - Sensor-Kalibrierung (CO₂-Frischluftabgleich, Temperatur-Offset, ES7210-Schallpegelkalibrierung)
  - Gehäusedesign und Luftführung

---

## 4. Display-Optimierung für 1,75" AMOLED (466×466)

Die Benutzeroberfläche in **`main/ui.c`** wurde grundlegend überarbeitet, um die 1,75"-Diagonale optimal auszunutzen, die Lesbarkeit zu verbessern und Hardware-Ungenauigkeiten auszugleichen.

### 4.1 Bessere Ausnutzung der Bildschirmdiagonale (Edge-to-Edge)
- **Problem:** Die Skalenringe hatten im Original einen Durchmesser von nur `380 px`. Bei einem 466×466-Display blieben rundherum über 43 Pixel schwarzer, ungenutzter Rand, während die Diagramme fast bis an den Rand gingen.
- **Änderung:**
  - Ring-Durchmesser `RING_D` schrittweise von 380 px über 412 px und 430 px auf **`446 px`** vergrößert.
  - Randbegrenzung `EDGE` von 34 px auf 10 px reduziert.
  - Die Skalenbögen reichen nun bis knapp an den Rand des runden AMOLED-Glases heran und füllen das Display harmonisch aus.

### 4.2 Zentrierung des Bildschirms (`UI_X_OFFSET`)
- **Problem:** Auf dem runden Display wirkte die gesamte Benutzeroberfläche um wenige Pixel zu weit nach links versetzt.
- **Änderung:**
  - Zentrale Konstante `#define UI_X_OFFSET 4` eingeführt.
  - Alle Hauptelemente (Skalenringe, Zentrumstexte, Diagramme, Statusleiste oben, Akkuanzeige und Dots unten) wurden um 4 Pixel nach rechts gerückt und sitzen nun exakt konzentrisch im runden Glas/Bezel.

### 4.3 Synchronisierung von Messwert-Bogen und Hintergrundspur
- **Problem:** Der farbige Messwertbogen (`LV_PART_INDICATOR`) war im Original 8 Pixel schmäler definiert als die blau-graue Hintergrundspur (`LV_PART_MAIN`), wodurch er asymmetrisch und verschoben wirkte.
- **Änderung:**
  - Breite von `LV_PART_MAIN` und `LV_PART_INDICATOR` auf identische Dicke (`thickness = 22 px`) vereinheitlicht.
  - Beide Teile mit synchronisierten abgerundeten Enden (`lv_obj_set_style_arc_rounded(..., true)`) versehen.
  - Der farbige Bogen überdeckt die Hintergrundspur nun absolut deckungsgleich.

### 4.4 Vergrößerung der Typografie & Optimierung aller Seiten

| Element / Seite | Original | Neu | Besonderheiten |
|---|---|---|---|
| **Uhrzeit (Statusleiste)** | 16 px (`ui_font_16`), y = -146 | **48 px (`ui_font_48`)**, y = -170 | Groß und präsent oben im Bogen zentriert |
| **WiFi / MQTT Status** | 14 px (`ui_font_14`), y = -124 | **16 px (`ui_font_16`)**, y = -128 | Klar lesbar direkt unter der Uhrzeit |
| **Akkuanzeige unten** | 16 px, y = 148 | **16 px**, y = 156 | Weiter nach unten in die Bogenöffnung versetzt |
| **Übersicht (Seite 1)** | Status: 28 px, Sub: 16 px<br>Werte: 22 px, Namen: 14 px | **Status: 48 px, Sub: 28 px**<br>**Werte: 28 px, Namen: 22 px** | 2×2-Rasterabstand vergrößert (`dx = ±96`, `dy = 18 / 86`) |
| **CO₂ (Seite 2)** | Einheit: 16 px, Level: 22 px<br>Wert y = -44, Level y = 30 | **Einheit: 28 px, Level: 28 px**<br>**Wert y = -22, Level y = 48** | Inhalte nach unten abgesenkt, Diagramm bei y = 108 |
| **Partikel (Seite 3)** | Einheit: 16 px, Level: 22 px<br>PM1/4/10: 22 px / 14 px, dx = ±95 | **Einheit: 28 px, Level: 28 px**<br>**PM1/4/10: 28 px / 22 px, dx = ±114** | Breiter aufgeteilt, Werte deutlich größer |
| **Gase (Seite 4)** | Ringe: 172 px, x = ±94<br>Titel: 16 px, Hinweistext: 14 px | **Ringe: 188 px, x = ±106**<br>**Titel: 28 px, Hinweistext: 22 px** | Ringe vergrößert und weiter nach außen gerückt |
| **Klima (Seite 5)** | Titel "Klima" (16 px), Werte 48 px<br>Format `"%s C"`, x = ±96 | **Titel "Temperatur" & "Feuchte" (28 px)**<br>**Werte 48 px, Format `"%s °C"`, x = ±116** | Getrennte Spaltenköpfe in Kurvenfarbe, echtes Grad-Symbol |
| **Lärm (Seite 6)** | Einheit: 16 px, Level: 22 px<br>Wert y = -44, Level y = 30 | **Einheit: 28 px, Level: 28 px**<br>**Wert y = -22, Level y = 48** | Diagramm bei y = 108 abgesenkt |
| **Ruhemodus (Idle)** | Status: 28 px, Werte: 22 px<br>Namen: 14 px, Spalten: 112 px | **Status: 48 px, Werte: 28 px**<br>**Namen: 16 px, Spalten: 122 px** | Lärmanzeige & IP-Adresse ebenfalls vergrößert |

---

## 5. Zuverlässiges Dimmen, Touch-Wakeup, Smooth Fading & Burn-in-Schutz

### 5.1 Kombiniertes Hardware- & Software-Dimming
- **Problem:** Trotz CO5300-Hardwarebefehlen (`0x53` / `0x51`) reagierte das AMOLED-Display bei Helligkeitsänderungen teilweise nur binär (an/aus) statt stufenlos.
- **`main/display.c`:**
  - Beim Abschalten (`level == 0`) wird `s_bctrl_init = false;` zurückgesetzt, damit beim erneuten Einschalten das Freigaberegister `0x53` (`0x20`) sicher erneut an den CO5300 gesendet wird.
- **`main/ui.c` & `main/ui.h`:**
  - Vollflächiges, unklickbares schwarzes Software-Dimming-Overlay (`s_dim_overlay`) auf `lv_layer_top()` ergänzt. Die Deckkraft wird invers zur Helligkeit (`255 - level`) gesteuert und mit der Hardware-Helligkeit kombiniert. Dadurch lässt sich das AMOLED zuverlässig stufenlos bis komplett schwarz dimmen.
  - Neue Funktion `ui_set_live_brightness(uint8_t level)` für die Live-Vorschau aus dem Web-Interface.

### 5.2 Touch-Wakeup & frei einstellbare Dimmer-Zeit
- **Problem:** Das `lv_tileview` fing Touch-Eingaben ab, sodass Berührungen nicht zuverlässig an das Hintergrund-Screen-Event weitergeleitet wurden. Zudem waren nur feste Zeitstufen im Dropdown wählbar.
- **`main/ui.c`:**
  - Touch- und Scroll-Events (`LV_EVENT_PRESSED`, `LV_EVENT_CLICKED`, `LV_EVENT_SCROLL_BEGIN`) direkt am `s_tv` (Tileview) sowie Event-Bubbling auf den einzelnen Tiles aktiviert; Diagramme auf nicht-klickbar gestellt. Jede Berührung weckt das Display sofort auf und setzt den Timer zurück.
- **`main/webcfg.c`:**
  - Das Feld **„Abdunkeln nach“** wurde von einem festen Dropdown in ein freies Zahlenfeld in Sekunden (`0` = Nie, bis `86400`) geändert.
  - Beide Schieberegler (**Helligkeit** und **Gedimmte Helligkeit**) steuern über `/api/brightness` jetzt sofort live das Display inkl. LVGL-Overlay (auch Wert `0` = komplett aus).

### 5.3 Sanftes Ein- und Ausblenden (Smooth Fading)
- **`main/ui.c`:**
  - Helligkeitswechsel beim automatischen Abdunkeln (`500 ms`, `FADE_DIM_MS`) und beim Aufwecken per Touch (`250 ms`, `FADE_WAKE_MS`) erfolgen nun über eine weiche LVGL-Animation (`lv_anim_t` mit `lv_anim_path_ease_out`).
  - Der Live-Schieberegler im Web-Interface reagiert weiterhin ohne Verzögerung (`duration_ms = 0`).

### 5.4 AMOLED Burn-in-Schutz (Pixel-Shift)
- **`main/settings.h`, `main/settings.c`, `main/app_main.c`, `main/ui.h`, `main/ui.c`, `main/webcfg.c`:**
  - Alle sichtbaren UI-Elemente liegen in einem gemeinsamen Container `s_root`.
  - Über zwei getrennte Optionen im Web-Interface (**Im gedimmten Modus**, Standard: an; **Im ungedimmten Modus**, Standard: aus) lässt sich ein sanfter Pixel-Shift aktivieren, der die Anzeige alle 60 Sekunden unmerklich um ±3 Pixel auf einer 9-Punkte-Bahn verschiebt.
  - Beim Aufwecken (sofern im ungedimmten Modus deaktiviert) springt das Display sofort wieder exakt zentriert auf `(0, 0)` zurück.
  - NVS-abwärtskompatibel (neue Felder am Ende von `settings_t`, inkl. Backup/Restore-Unterstützung).

---

## 6. Bereinigung des Quellcodes & Clean Build

- **Kommentare in `main/ui.c`:** Alle Quellcode-Kommentare im UI-Modul wurden von Spanisch ins Deutsche übersetzt.
- **Bereinigung von Sonderzeichen:** Entfernung fehlerhafter Backslash-Escapes in String-Literals (`"%s %s"`, `"%s °C"`, `""`).
- **LVGL-9-Kompatibilität (Clean Build):** Alle veralteten `lv_obj_add_flag()`- und `lv_obj_remove_flag()`-Aufrufe in `main/ui.c` wurden durch die modernen LVGL-9-Setter (`lv_obj_set_hidden`, `lv_obj_set_clickable`, `lv_obj_set_scrollable`, `lv_obj_set_event_bubble`) ersetzt. Das Projekt kompiliert damit komplett ohne `deprecated`-Warnungen.