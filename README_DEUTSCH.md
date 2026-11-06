# AirRing — Monitor SEN66

**Druckbares Gehäuse auf MakerWorld: [AirRing](https://makerworld.com/en/models/3199590-airring-air-quality-monitor-mqtt)**

Luftqualitätsmonitor für das **Waveshare ESP32-S3-Touch-AMOLED-1.75** (466×466 rund) mit einem **Sensirion SEN66**, eigenständigem Display und automatischer Erkennung in **Home Assistant** über MQTT.

Inspiriert vom [PowerDot Air](https://makerworld.com/es/models/3029930-powerdot-air-home-assistant-air-sensor) von Scoolt96, der dasselbe auf dem Waveshare 1.46"-LCD macht. Dessen [Firmware ist closed-source](https://github.com/Scoolt96/PowerDot-fw) (das Repository enthält nur die `.bin`-Dateien), daher handelt es sich hierbei um eine komplette Neuimplementierung von Grund auf, angepasst an das runde AMOLED-Display: CO5300-Treiber über QSPI, CST9217-Touchcontroller, RTC für den Verlauf und ein Desktop-Simulator zur schnellen Entwicklung der Benutzeroberfläche.

<p align="center">
  <img src="docs/img/pagina_0.png" width="300" alt="Übersichtsbildschirm">
</p>

## Funktionsumfang

- Misst **neun Messgrößen** mit einem einzigen Sensor: CO₂, PM1.0 / PM2.5 / PM4.0 / PM10, VOC-Index, NOx-Index, Temperatur und Luftfeuchtigkeit.
- **Fünf Seiten** auf einem runden AMOLED-Display, plus eine Ruheansicht (Idle-Screen), die alle Werte auf einen Blick darstellt.
- **Automatische Erkennung in Home Assistant** via MQTT-Autodiscovery; funktioniert auch mit **HomeKit** über Homebridge ([Anleitung](docs/HOMEBRIDGE.md)).
- **Vollständig lokal**: Keine Cloud, kein Benutzerkonto, keine Telemetrie. Funktioniert auch bei Netzwerkausfall uneingeschränkt weiter — das Display arbeitet völlig autonom.
- **Weboberfläche** zur Konfiguration und **Over-the-Air-Updates (OTA)** über WLAN.
- **Akustischer Alarm** bei Überschreiten des CO₂-Schwellenwerts, mit Hysterese.
- Funktioniert **mit Akku/Batterie**, inklusive Energiesparprofil, das automatisch beim Abstecken aktiv wird (~6 Std. gemessen mit einer 1000-mAh-Zelle).
- Das Display unterstützt **Deutsch, Englisch und Spanisch**.

| Übersicht | CO₂ | Partikel |
|---|---|---|
| ![](docs/img/pagina_0.png) | ![](docs/img/pagina_1.png) | ![](docs/img/pagina_2.png) |
| **Gase** | **Klima** | **Ruhemodus** |
| ![](docs/img/pagina_3.png) | ![](docs/img/pagina_4.png) | ![](docs/img/reposo.png) |

*(Screenshots aus dem beiliegenden Simulator, der exakt dieselbe UI ausführt wie die Firmware)*

## Hardwarekomponenten

| Komponente | Hinweise |
|---|---|
| Waveshare ESP32-S3-Touch-AMOLED-1.75 | SKU 31261 (die Varianten -B und -G funktionieren ebenfalls) — [Waveshare Store](https://www.waveshare.com/esp32-s3-touch-amoled-1.75.htm) oder Variante **-G** auf Amazon: 🇪🇸 [.es](https://www.amazon.es/dp/B0F7XWWMJW?tag=capsuleradar-21) · 🇺🇸 [.com](https://www.amazon.com/dp/B0F7XWWMJW?tag=capsuleradar-20) · 🇩🇪 [.de](https://www.amazon.de/dp/B0F7XWWMJW?tag=capsulerada03-21) · 🇬🇧 [.co.uk](https://www.amazon.co.uk/dp/B0F7XWWMJW?tag=capsulerada0d-21) · 🇮🇹 [.it](https://www.amazon.it/dp/B0F7XWWMJW?tag=capsulerada08-21) · 🇫🇷 [.fr](https://www.amazon.fr/dp/B0F7XWWMJW?tag=capsulerada0e-21) · 🇯🇵 [.co.jp](https://www.amazon.co.jp/dp/B0F7XWWMJW?tag=capsuleradar-22) · 🇨🇦 [.ca](https://www.amazon.ca/dp/B0F7XWWMJW?tag=capsulerada09-20) |
| Sensirion SEN66 | 3,3 V ±5 %, I2C, inklusive 6-poligem JST-GH-Kabel — bei Distributoren erhältlich ([Mouser](https://www.mouser.de/c/?q=SEN66), [DigiKey](https://www.digikey.de/en/products/result?keywords=SEN66)); nicht auf Amazon verfügbar |
| 4 Kabel zur 8-Pin-Stiftleiste | 3V3, GND, SDA, SCL |
| 3 Schrauben M2×6 | Befestigen das Board am Blendenring — 🇪🇸 [Amazon.es](https://www.amazon.es/s?k=tornillos+M2x6&tag=capsuleradar-21) · 🇺🇸 [Amazon.com](https://www.amazon.com/s?k=M2x6+machine+screws&tag=capsuleradar-20) |
| [Gehäuse AirRing](https://makerworld.com/en/models/3199590-airring-air-quality-monitor-mqtt) | 3D-druckbar, 0,24 mm Schichthöhe, 2 Wände, 15 % Infill |
| USB-C-Kabel | Stromversorgung und Flashen |

<sub>Die Amazon-Links sind Affiliate-Links. Als Amazon-Partner erhalte ich eine kleine Provision bei qualifizierten Verkäufen.</sub>

## Verkabelung — Bitte vor dem Anschließen lesen

**Der SEN66 meldet sich auf der I2C-Adresse `0x6B`, exakt derselben Adresse wie die fest auf dem Board verlötete IMU QMI8658.** Sie können sich denselben I2C-Bus daher nicht teilen! Aus diesem Grund läuft der Sensor an einem **zweiten I2C-Bus** (Port 1) über GPIOs der Erweiterungsstiftleiste mit 100 kHz (dem Maximum des SEN66), während der On-Board-Bus (Touch, PMU, RTC, IMU, Audio) weiter mit 400 kHz arbeitet. Praktischer Nebeneffekt: Der Sensor stört die Touch-Bedienung nicht.

**Der vollständige Schaltplan befindet sich in [docs/CABLEADO.md](docs/CABLEADO.md).** Zusammenfassung:

Verkabelt wird nach der **aufgedruckten Beschriftung** der Stiftleiste, nicht nach Pin-Nummern:

| SEN66 | Kabelfarbe | → | Beschriftung auf der Platine |
|---|---|---|---|
| 1 VDD | rot | → | **3V3** (Achtung: Keinesfalls an VBUS anschließen, das sind 5 V!) |
| 2 GND | schwarz | → | **GND** |
| 3 SDA | grün | → | **IO17** |
| 4 SCL | gelb | → | **IO18** |

Das blaue und violette Kabel (Pins 5 und 6 des Sensors) bleiben unbelegt. Achtung: Die tatsächliche Reihenfolge der Stiftleiste lautet `IO18 IO17 IO16 RXD TXD 3V3 GND VBUS` und stimmt **nicht** mit der Nummerierung 1..8 der [offiziellen Hardware-Referenz](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/main/HARDWARE_REFERENCE.md) überein.

Falls abweichend verkabelt wird, muss nicht blind geraten werden: Wenn der Sensor beim Starten nicht an diesen Pins antwortet, **scannt die Firmware automatisch die möglichen Pin-Paare** der Stiftleiste ab und gibt auf der Konsole aus, welches funktioniert:

```
W (1234) sen66: no responde en SDA=17 SCL=18, barriendo header
I (1500) sen66: producto: 'SEN66'
W (1500) sen66: encontrado en SDA=16 SCL=17 (no en los de board.h); fija esos valores en BOARD_SEN66_PIN_*
```

Das funktionierende Pin-Paar wird einfach in `board.h` eingetragen, neu kompiliert und fertig. Die Prüfung begnügt sich nicht mit dem I2C-ACK der Adresse: Sie liest den Produktnamen aus und verlangt, dass dieser mit `SEN6` beginnt, um Verwechslungen mit anderen Chips auszuschließen.

Der SEN66 besitzt einen internen Lüfter und **Sensirion weist auf Stromspitzen von bis zu 350 mA hin**. Waveshare spezifiziert nicht, wie viel Strom der 3V3-Pin der Stiftleiste liefern kann; in der Praxis reicht der Step-Down-Wandler des AXP2101 aus, es empfiehlt sich jedoch, das Board mit einem USB-C-Netzteil von mindestens 1 A zu versorgen und nicht an einem ungespeisten USB-Hub zu betreiben. Treten Brownout-Resets, CRC-Fehler oder `fan_error` auf, liegt das an der Stromversorgung; Abhilfe schafft dann eine separate 3,3-V-Quelle für den Sensor mit gemeinsamer Masse (GND). Im Akkubetrieb beträgt die Laufzeit wenige Stunden, keine Tage.

## Kompilieren und Flashen

Erfordert **ESP-IDF 5.3 oder neuer** (getestet mit 5.5.2).

```bash
source ~/esp/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem1101 flash monitor
```

Der IDF Component Manager lädt automatisch LVGL 9, `esp_lvgl_port`, `esp_lcd_co5300` und `esp_lcd_touch_cst9217` herunter.

Die Partitionstabelle enthält **zwei App-Slots à 4 MB**, sodass OTA-Updates über den Webbrowser direkt ab dem ersten Flashen unterstützt werden.

## Erste Inbetriebnahme

1. Ist kein WLAN konfiguriert, öffnet das Gerät einen unverschlüsselten Access Point mit dem Namen **`SEN66-XXXXXX`** (auf dem Display angezeigt).
2. Damit verbinden und im Browser **http://192.168.4.1** aufrufen.
3. WLAN-Zugangsdaten und optional den MQTT-Broker (`mqtt://192.168.1.10:1883`) eintragen und speichern. Das Gerät startet neu und verbindet sich.
4. Ab diesem Zeitpunkt ist die Weboberfläche unter der IP-Adresse erreichbar, die auf dem Display angezeigt wird.

Ändert sich das WLAN-Passwort und die Verbindung schlägt 8 Mal hintereinander fehl, öffnet sich das Konfigurationsportal automatisch wieder, ohne die restlichen Einstellungen zu verlieren.

## Home Assistant

Es wird ein MQTT-Broker benötigt: Wie dieser eingerichtet wird und wie überprüft werden kann, ob das Gerät sendet, ist in **[docs/MQTT.md](docs/MQTT.md)** beschrieben. Danach:

1. In Home Assistant unter Einstellungen > Geräte & Dienste > **Integration hinzufügen > MQTT** auswählen und mit den Anmeldedaten des Brokers verbinden.
2. Das ist alles. **Es muss kein YAML bearbeitet werden.**

Beim Verbinden sendet die Firmware Retained-Discovery-Nachrichten unter `homeassistant/sensor/sen66-xxxxxx/<key>/config`, und Home Assistant erstellt automatisch **ein Gerät mit 11 Entitäten**: die neun Messwerte des Sensors, das Gesamtniveau der Luftqualität und die WLAN-Signalstärke als Diagnosewert.

Die **Lautstärke (Lärm)** erscheint als reguläre Entität mit der Geräteklasse `sound_pressure` in dB. In HomeKit gibt es keinen Standardtyp für Schallpegel, daher wird sie dort als Schwellenwert-Binärsensor abgebildet (siehe [docs/HOMEBRIDGE.md](docs/HOMEBRIDGE.md)).

**Bei angeschlossenem Akku erscheinen 4 weitere Entitäten**, ebenfalls als Diagnose: Akkustand in %, Zellenspannung in mV sowie zwei Binärsensoren für *Laden* und *USB-Stromversorgung*. Ist kein Akku vorhanden, werden diese **nicht veröffentlicht**, um ungenutzte Entitäten in Home Assistant zu vermeiden. Die Erkennung erfolgt beim Verbinden mit dem Broker; wird ein Akku nachträglich angeschlossen, muss das Gerät neu gestartet werden.

- Status: `sen66-xxxxxx/state`, ein JSON-Payload alle 10 Sekunden.
- Verfügbarkeit: `sen66-xxxxxx/status` mit *Last Will and Testament (LWT)*; fällt das Gerät aus, markiert Home Assistant es sofort als „nicht verfügbar“, anstatt veraltete Werte anzuzeigen.

Die standardmäßigen Geräteklassen sind gesetzt (`carbon_dioxide`, `pm25`, `pm10`, `temperature`, `humidity`…), damit Verläufe und Einheiten direkt passen. Die VOC- und NOx-Indizes besitzen keine native Geräteklasse in HA und verwenden passende Symbole.

Die Luftqualitätsentität liefert den englischen Bezeichner (`good`, `fair`, `moderate`, `poor`, `bad`), da dieser unverändert über MQTT übertragen wird, damit Automatisierungen unabhängig von der Displaysprache stabil bleiben. Wer den Text im Dashboard dennoch auf Deutsch sehen möchte, kann dies über einen Template-Sensor in `configuration.yaml` lösen:

```yaml
template:
  - sensor:
      - name: "Luftqualität (Text)"
        state: >
          {{ {'good':'Gut','fair':'Mäßig','moderate':'Mittel',
              'poor':'Schlecht','bad':'Sehr schlecht'}.get(
              states('sensor.monitor_sen66_calidad_del_aire'), 'Unbekannt') }}
```

Für Automatisierungen empfiehlt es sich, den Rohwert oder numerische Schwellenwerte zu nutzen:

```yaml
trigger:
  - platform: numeric_state
    entity_id: sensor.monitor_sen66_co2
    above: 1200
```

### Langzeitstatistik vs. Verlaufsdaten

Hierbei kommt es häufig zu Missverständnissen. Home Assistant unterscheidet zwischen **zwei verschiedenen Speicherbereichen**:

- **Langzeitstatistiken**: Ein Datensatz pro Stunde mit Minimum, Maximum und Durchschnitt. **Diese werden nie gelöscht.** Sie werden für alle Messwerte automatisch erzeugt, da die Firmware `state_class: measurement` meldet. Monats- und Jahresverläufe stehen ohne manuelle Konfiguration zur Verfügung.
- **Detaillierter Verlauf**: Jeder einzelne Messwert im 10-Sekunden-Takt. Dieser verfällt standardmäßig nach **10 Tagen**.

Ein Hochsetzen der Aufbewahrungsdauer (Recorder) dient nur dazu, in weit zurückliegende Tage im Sekundentakt hineinzoomen zu können, nicht für langfristige Trends.

Soll der detaillierte Verlauf dennoch länger gespeichert werden, kann dies in `configuration.yaml` definiert werden:

```yaml
recorder:
  purge_keep_days: 30
  commit_interval: 30
  exclude:
    entities:
      - sensor.monitor_sen66_wifi
```

Das Gerät sendet alle 10 Sekunden neue Werte, was rund **15.000 Einträge pro Tag** erzeugt. Bei 30 Tagen belegt die Datenbank hierfür etwa 80 MB. Die WLAN-Signalstärke wird ausgeschlossen, da ~1.700 tägliche Zeilen eines Diagnosewerts im Verlauf selten benötigt werden; die Live-Anzeige bleibt davon unberührt.

## HomeKit (ohne Home Assistant)

Wer anstelle von Home Assistant Apples **Home-App** und Siri nutzen möchte, kann dies über Homebridge und `homebridge-mqttthing` realisieren: derselbe MQTT-Broker ([docs/MQTT.md](docs/MQTT.md)), anderer Client. Fertige Konfigurationsbeispiele und Hinweise sind in **[docs/HOMEBRIDGE.md](docs/HOMEBRIDGE.md)** dokumentiert.

In diesem Fall bleibt das **Discovery-Präfix** in den Einstellungen des Monitors leer, da die automatische Erkennung speziell für Home Assistant ausgelegt ist.

## Sprachen

Das Display unterstützt **Deutsch, Englisch und Spanisch**; die Sprache kann in der Weboberfläche umgeschaltet werden.

Die Montserrat-Schriftarten von LVGL enthalten standardmäßig nur ASCII-Zeichen. Für die deutschen Umlaute (`ÄÖÜäöüß`) wurde eine schlanke **Fallback-Schriftart** mit genau diesen sieben Glyphen erstellt (`main/fonts/`, ca. 37 KB gesamt), die über `lv_font_t.fallback` eingebunden wird. Da die Schriftarten von LVGL im Flash-Speicher als `const` abgelegt sind, verwendet die UI kompakte RAM-Deskriptoren (~30 Bytes) mit ausgefülltem `fallback`-Feld (`main/fonts/fonts.c`).

Im Spanischen wird bewusst auf Akzente verzichtet ("Particulas", "ug/m3").

Die MQTT-Werte bleiben **stets unübersetzt**: Es werden einheitliche englische Bezeichner (`good`, `fair`, `moderate`, `poor`, `bad`) verwendet, um Kompatibilität mit Automatisierungen zu gewährleisten.

## Die Bildschirmanzeige

Sechs Seiten, **die durch Wischen gewechselt werden und nicht automatisch rotieren**. Erfolgt für die Dauer von `screen_timeout_s` keine Berührung, dimmt das Display ab und wechselt in die **Ruheansicht mit allen neun Messwerten** auf einen Blick. Bei einer Berührung kehrt die Anzeige sofort auf die zuvor geöffnete Seite zurück.

| Seite | Inhalt |
|---|---|
| Übersicht | Ampel-Ring mit dem aktuell schlechtesten Messwert und Angabe der führenden Metrik |
| CO₂ | Großer Messwert, Farbring 400–2400 ppm und Verlaufsdiagramm der letzten Stunde |
| Partikel | PM2.5 im Fokus mit Skalenring und Kurve im Hintergrund; PM1.0 / PM4.0 / PM10 darunter |
| Gase | VOC- und NOx-Indizes in zwei separaten Skalenringen |
| Klima | Temperatur und Feuchtigkeit mit zwei überlagerten Kurven und eigener Skalierung |
| Lärm | Schallpegel in dB mit Ring von 30–90 dB und Diagramm (siehe Kalibrierungshinweis) |

Dank AMOLED-Technologie entspricht Schwarz vollständig abgeschalteten Pixeln, wodurch der dunkle Hintergrund stromsparend ist und das Abdunkeln im Ruhemodus spürbar Energie spart.

### Schwellenwerte

Fünf Stufen: **GUT / MÄSSIG / MITTEL / SCHLECHT / SEHR SCHLECHT**.

| Messgröße | Schwellenwerte |
|---|---|
| CO₂ (ppm) | 800 · 1000 · 1400 · 2000 |
| PM1.0 und PM2.5 (µg/m³) | 10 · 20 · 25 · 50 |
| PM4.0 und PM10 (µg/m³) | 20 · 40 · 50 · 100 |
| VOC-Index | 150 · 250 · 400 · 450 |
| NOx-Index | 20 · 150 · 300 · 400 |

Temperatur und Luftfeuchtigkeit stellen keine Schadstoffe dar; sie werden anhand des Abstands zum Behaglichkeitsbereich (19–25 °C, 40–60 % r.F.) bewertet und **fließen nicht in die globale Luftqualitätsampel ein**.

Die Grenzwerte sind zentral in der Tabelle `k_bands` in [`main/air.c`](main/air.c) hinterlegt.

## Desktop-Simulator

Kompiliert **exakt dieselbe Benutzeroberfläche und Logik** wie die Firmware gegen SDL2 auf dem PC, inklusive simulierter Daten (steigendes CO₂, Lüftungszyklen, Kochereignisse). Änderungen an der UI lassen sich hier um ein Vielfaches schneller testen als durch wiederholtes Flashen.

```bash
brew install sdl2                      # einmalig (macOS) bzw. apt install libsdl2-dev (Linux)
idf.py build                           # einmalig, um LVGL herunterzuladen
cd sim && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/sen66_sim
```

Nützliche Optionen:

```bash
./build/sen66_sim --lang de            # auf Deutsch starten
./build/sen66_sim --page 2             # startet auf einer bestimmten Seite
./build/sen66_sim --scenario bad       # simuliert schlechte Luftqualität
./build/sen66_sim --warp 60            # 60-fache Zeitbeschleunigung
./build/sen66_sim --offset 2400        # verschiebt den Zyklusstart
./build/sen66_sim --shots /tmp/caps 5  # erstellt 5 BMP-Screenshots und beendet sich
```

## Einstellungen (Weboberfläche)

Netzwerk und MQTT, Gerätename für Home Assistant, Sprache, Zeitzone und NTP, normale und gedimmte Helligkeit, Display-Timeout, Anzeigedauer pro Seite, sichtbare Seiten (Bitmaske), Diagramm-Zeitfenster, Temperaturkorrektur, Höhenlage über dem Meeresspiegel, automatische CO₂-Kalibrierung sowie der **akustische Alarm**: Aktivierung, Alarmschwelle, Rücksetzschwelle und Lautstärke.

Die beiden Alarmschwellen sind bewusst unterschiedlich ausgelegt (Hysterese): Der Alarm ertönt beim Überschreiten von `alarm_co2_ppm` und verstummt erst wieder beim Unterschreiten von `alarm_clear_ppm`. Dieser Abstand verhindert ständiges Piepen, wenn sich der CO₂-Wert um den Grenzwert herum bewegt. Wird versehentlich ein Abschaltwert oberhalb des Einschaltwerts eingegeben, korrigiert die Software dies automatisch.

Beim Klick auf Speichern **startet das Gerät neu**: Dadurch werden geänderte Netzwerk-, MQTT- und Sensorparameter sauber und vollständig übernommen. Die Helligkeitseinstellung wird sofort live angewendet.

Zusätzlich stehen Funktionen zur **Lüfterreinigung** des Sensors, zum **Testen des Lautsprechers** und zum **Hochladen einer Firmware-Datei (`.bin`) per OTA** bereit. Die Lüfterreinigung wird zudem **automatisch einmal wöchentlich** ausgeführt (Sensirion-Empfehlung); der letzte Ausführungszeitpunkt wird im NVS hinterlegt, sodass Neustarts den Intervallzähler nicht zurücksetzen.

### CO₂-Rekalibrierung

Im Wartungsbereich befindet sich eine Schaltfläche zur manuellen Kalibrierung des CO₂-Sensors gegen eine bekannte Referenz. **Dies ist nur mit einer verlässlichen Frischluftreferenz sinnvoll**: Außenluft liegt typischerweise bei rund 420 ppm. Das Gerät wird dazu für mindestens fünf Minuten fernab von Personen im Freien betrieben und anschließend auf 420 ppm kalibriert. Falsche Werte führen zu dauerhaften Fehlmessungen, **da der Offset im internen EEPROM des Sensors gespeichert wird**.

Im Normalfall ist eine manuelle Kalibrierung nicht erforderlich: Die automatische Selbstkalibrierung (ASC) ist aktiv und gleicht den Sensor selbstständig ab, sofern er regelmäßig mit Frischluft versorgt wird.

### Sicherheitshinweis zur Weboberfläche

**Die Weboberfläche besitzt keinen Passwortschutz.** Jedes Gerät im lokalen Netzwerk kann Einstellungen ändern, das Gerät neu starten und über `/api/ota` eine **neue Firmware aufspielen**. In einem privaten Heimnetzwerk stellt dies einen bequemen Kompromiss dar, in Gastnetzen oder Umgebungen mit nicht vertrauenswürdigen Teilnehmern sollte dies jedoch berücksichtigt werden.

## Inbetriebnahme und Sensoreigenschaften

- **VOC- und NOx-Indizes benötigen Lernzeit.** Es handelt sich um adaptive Algorithmen: Der Sensor ermittelt das typische Raumklima und ordnet dieses bei 100 (VOC) bzw. 1 (NOx) ein. In den ersten Betriebsstunden sind die Werte daher noch wenig aussagekräftig.
- **Die gemessene Temperatur liegt bauartbedingt etwas zu hoch**, da sich der Sensor in einem kompakten Gehäuse neben wärmeabgebender Elektronik befindet. Über den Menüpunkt „Temperaturkorrektur“ kann die Abweichung zu einem Referenzthermometer hinterlegt werden. Der Offset wird **direkt im SEN66** verrechnet, sodass auch die relative Luftfeuchtigkeit entsprechend korrigiert wird.
  Vor dem Einstellen empfiehlt sich jedoch die Lektüre des Abschnitts [Hinweise zur Messung von Abweichungen](#hinweise-zur-messung-von-abweichungen).
- **Höhenlage (Höhenkorrektur)**: Der Luftdruck beeinflusst die optische CO₂-Messung. Hier sollte die ungefähre Höhe des Einsatzortes über dem Meeresspiegel in Metern eingetragen werden.
- **Automatische CO₂-Kalibrierung (ASC)**: Standardmäßig aktiv. Setzt voraus, dass der Raum regelmäßig gelüftet wird (~400 ppm). In dauerhaft geschlossenen Räumen ohne Frischluftzufuhr sollte ASC deaktiviert und gelegentlich eine manuelle Frischluftkalibrierung durchgeführt werden.

### Der Schallpegelmesser

Auf der Platine befinden sich **zwei Mikrofone**, die über einen separaten Audio-ADC **ES7210** an I2C-Adresse `0x40` angebunden sind (der ES8311 dient nur als Audio-Ausgang). Der Schallpegel wird als reguläre Metrik erfasst und profitiert direkt von Verlaufsdiagrammen, Web-Dashboard und Home-Assistant-Integration. **Der Lärmpegel fließt nicht in die Luftqualitätsbewertung ein.**

**Der Pegel ist vorkalibriert** (Stand: September 2026). Vom Mikrofon wird zunächst ein relativer Pegel bezogen auf die Vollaussteuerung (dBFS, stets negativ) geliefert. Die Umrechnung in dB SPL erfordert die Kenntnis von Mikrofonempfindlichkeit und Codec-Verstärkung; der Standard-Offset liegt bei **112 dB** (ermittelt im Vergleich mit einer Schallpegel-Mess-App unter Mittelung über definierte Zeitfenster):

| Szenario | Referenz | Monitor | Offset |
|---|---|---|---|
| Raumruhe | 54,6 dB | −57,8 dBFS | 112,4 |
| Musik | 64,2 dB | −48,1 dBFS | 112,2 |

Die geringe Differenz über einen Bereich von 10 dB zeigt, dass es sich um eine **konstante Verschiebung** und nicht um eine Steigungsabweichung handelt, weshalb ein linearer Offset gut geeignet ist. Über das Feld „Lärm-Kalibrierung“ im Webpanel kann dieser Wert bei Bedarf angepasst werden.

**Wichtige Hinweise zur Schallpegelmessung:**
- **Momentanwert statt Langzeitmittelung:** Das Gerät gibt den RMS-Wert der jeweils letzten 125 ms aus („Fast“-Bewertung eines Schallpegelmessers). Der Wert schwankt daher spürbar.
- **Grundrauschen:** Das systembedingte Grundrauschen liegt bei etwa 43 dB (−69,2 dBFS bei angehaltenem Lüfter). Werte darunter kann das Mikrofon nicht auflösen.
- **Keine A-Bewertung (dBA):** Die Berechnung erfolgt als ungewichteter Breitband-RMS. Im Gegensatz zu Messgeräten mit A-Filter (dBA), die tiefe Frequenzen dämpfen, erfasst das Gerät auch tieffrequente Hintergrundgeräusche (z. B. Brummen von Kühlschränken oder Netzteilen). Daher ist der Messwert vor allem ab ca. 50–55 dB aussagekräftig.
- **Lüftereinfluss des SEN66:** Der interne Lüfter des SEN66 erzeugt im Gehäuse einen Eigenschallpegel von ca. 36 dB SPL und liegt damit deutlich unter dem typischen Grundrauschen normaler Wohnräume.

### Gehäusedesign und Luftführung

Der SEN66 besitzt **zwei Einlässe und einen Auslass** auf derselben Gehäuseseite: Die quadratische Öffnung und die runde Membran sind Einlässe, der Lüfterauslass ist der Ausblasbereich. Sensirion formuliert hierzu zwei wesentliche Anforderungen ([Design-Richtlinien](https://sensirion.com/media/documents/EA641247/6977159F/PS_AN_SEN6x_Mechanical_Design_and_Assembly_Guidelines_D1.pdf)):

- **Trennung von Auslass und Einlässen**, um eine direkte Rückzirkulation der gerade ausgestoßenen Luft zu verhindern.
- **Vollständige Abdichtung gegenüber dem Gehäuseinneren**, damit der Sensor keine erwärmte Gehäuseluft von ESP32 und Display ansaugt.

Bei Kanalbauten betragen die empfohlenen Mindestquerschnitte **56 mm² je Einlass und 148 mm² für den Auslass**. Die Ausrichtung mit den Öffnungen nach oben sollte vermieden werden, um Staubeintrag zu minimieren.

**Das AirRing-Gehäuse erfüllt diese Anforderungen nachweislich:**
- *Kein Ansaugen warmer Innenluft*: Die Temperaturdifferenz zu einem externen Referenzthermometer liegt bei lediglich **+0,4 °C**.
- *Keine Rückzirkulation*: Abklingkurven von Partikelmessungen nach Rauchtests zeigen identische Zeitkonstanten wie frei platzierte Referenzsensoren (Abklingzeit 9,7 min vs. 8,7 min).

### Hinweise zur Messung von Abweichungen

Vergleichsmessungen über 22 Stunden mit Referenzgeräten am selben Tisch verdeutlichen typische Messdynamiken:

- **Zwei Geräte zeigen nur die Differenz, nicht wer recht hat.** Ein Vergleich mit einem einzelnen Zweitgerät verleitet schnell dazu, fälschlicherweise am Monitor gegenzusteuern.
- **Messungen über volle 24-Stunden-Zyklen durchführen:** Tag-Nacht-Unterschiede und thermische Einflüsse verändern Differenzen im Tagesverlauf; punktuelle Momentaufnahmen führen leicht zu Fehlkorrekturen.
- **Sensoren bei CO₂ nicht voreilig verstellen:** In gut belüfteten Räumen verharren Sensoren über Stunden nahe der Frischluftgrenze von ca. 400–420 ppm, bevor sie bei geschlossenen Fenstern und Anwesenheit von Personen wieder kontinuierlich ansteigen.

## Projektstruktur

```
main/
  app_main.c      Startreihenfolge, FreeRTOS-Tasks und Systemzeit
  board.h         Verifizierte Pinbelegung der Platine
  display.c       CO5300 über QSPI + CST9217 + LVGL-Portierung
  sen66.c         I2C-Treiber für den Sensor (eigener Bus, CRC-8, Auto-Scan)
  air.c           Kernlogik: Metriken, Schwellenwerte, Stufen, Farben
  history.c       Ringpuffer für 24-Stunden-Verlauf im PSRAM
  ui.c            Die fünf Anzeigeseiten (reines LVGL, ohne IDF-Abhängigkeiten)
  net.c           WLAN (Station + Konfigurations-AP) + SNTP
  ha_mqtt.c       Home-Assistant-Autodiscovery und MQTT-Publisher
  webcfg.c        Integrierter Webserver, JSON-API und OTA-Handler
  settings.c      Dauerhafte Speicherung im NVS
  rtc_pcf85063.c  Echtzeituhr (RTC für autarke Uhrzeit ohne Netzwerk)
sim/              SDL2-Simulator für PC (nutzt air.c, history.c und ui.c)
```

`air.c`, `history.c` und `ui.c` sind frei von ESP-IDF-spezifischem Code und können daher unverändert im Desktop-Simulator kompiliert werden.

## Status

Kompiliert fehlerfrei (ESP-IDF 5.5.2, ca. 1,5 MB Binary, keine Compiler-Warnungen) und die UI ist im Simulator seitenweise geprüft. Nach dem Einbau in das Gehäuse kann die Ausrichtung über `BOARD_LCD_ROTATION` an die Lage der USB-C-Buchse angepasst werden.

## Lizenz

**[PolyForm Noncommercial 1.0.0](LICENSE).** Die Software darf für **alle nicht-kommerziellen Zwecke** frei genutzt, verändert und weitergegeben werden: private Nutzung, Bildung, Forschung und gemeinnützige Organisationen. Ein Verkauf oder die Integration in kommerzielle Produkte und kostenpflichtige Dienste ist nicht gestattet.

Hinweis: Es handelt sich hierbei um ein privates Heimprojekt und **kein zertifiziertes Messinstrument**. Nicht für sicherheitsrelevante Zwecke einsetzen.

### Ausnahmen von dieser Lizenz

- Die **Schriftarten** in `main/fonts/` basieren auf Montserrat und stehen unter der [SIL Open Font License (OFL 1.1)](main/fonts/NOTICE.md).
- Die **Abhängigkeiten** (ESP-IDF, LVGL, Espressif- und Waveshare-Komponenten) behalten ihre jeweiligen Lizenzen.
- Das **3D-Druckmodell** unterliegt einer separaten Lizenz auf MakerWorld.

## Danksagung

- **[PowerDot Air](https://makerworld.com/es/models/3029930-powerdot-air-home-assistant-air-sensor)** von Scoolt96 auf dem Waveshare 1.46" LCD für die ursprüngliche Inspiration.
- **Sensirion** für die Veröffentlichung offener Treiber und Dokumentationen. Das I2C-Protokoll wurde anhand von [raspberry-pi-i2c-sen66](https://github.com/Sensirion/raspberry-pi-i2c-sen66) verifiziert.
- **Espressif** für das ESP-IDF-Framework und die Audiotreiber-Komponenten.
- **Waveshare** für die Bereitstellung der [Hardware-Referenz](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75) des Boards.