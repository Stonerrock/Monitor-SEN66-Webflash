# Monitor SEN66 — Notizen für die Arbeit in diesem Repository

Luftqualitätsmonitor: Waveshare ESP32-S3-Touch-AMOLED-1.75 (466×466 rund, CO5300 + CST9217) + Sensirion SEN66, mit MQTT-Autodiscovery für Home Assistant. ESP-IDF 5.5 + LVGL 9. Siehe [README_DEUTSCH.md](README_DEUTSCH.md) für die Nutzung.

Es gibt den Skill `esp32-desk-gadget` mit dem Hardware-Spickzettel dieses Boards und den LVGL-Performance-Erkenntnissen: **vor dem Bearbeiten von Display, Touch, PMU, RTC oder Audio konsultieren** und neue Erkenntnisse ergänzen.

## Invarianten, die nicht gebrochen werden dürfen

- **`main/board.h` ist die maßgebliche Quelle (Source of Truth) für das Pinout**, verifiziert in vier früheren Projekten und gegen die [offizielle Hardware-Referenz](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/main/HARDWARE_REFERENCE.md). Pins nicht neu herleiten oder nach Augenmaß „korrigieren“.
- **Von der Stiftleiste (Header) sind nur GPIO16, 17 und 18 frei** (plus UART0 an 43/44). GPIO13 ist `LCD_TE` und GPIO21 ist `QMI_INT2`: Sie wirken frei, sind es aber nicht.
- **Der SEN66 gehört auf den I2C-Bus 1, niemals auf Bus 0.** Er teilt sich die Adresse `0x6B` mit der IMU QMI8658 des Boards. Und mit 100 kHz betreiben: Das ist sein Maximum.
- **`air.c`, `history.c` und `ui.c` dürfen keinerlei ESP-IDF-Header einbinden.** Das ermöglicht es dem Desktop-Simulator, exakt dieselbe UI und dieselbe Logik zu kompilieren. Wenn die UI Plattformfunktionen benötigt (Helligkeit, Uhrzeit), geschieht dies per Callback oder Übergabeparameter, nicht über `#include "esp_*.h"`.
- **Das Display wird initialisiert, BEVOR WLAN gestartet wird** (`app_main`). DMA-Puffer benötigen zusammenhängenden internen RAM, TLS ebenso; startet WLAN zuerst, beansprucht es diesen Speicher für sich.
- **Ausschließlich ASCII in den UI-Texten.** Die Montserrat-Schriften von LVGL enthalten keine Akzente, kein `¿` und kein Mikro-Symbol (µ). Daher steht auf dem Display „Particulas“ und „ug/m3“, während über MQTT sauber „µg/m³“ und „°C“ übertragen werden (`air_metric_unit_ui` vs. `air_metric_unit_ha`).
- **Niemals aus der UI-Schleife heraus ins NVS schreiben**: Das friert beide Kerne für ca. 1 Sekunde ein. `settings_save()` wird ausschließlich vom Webserver aufgerufen.

## Erkenntnisse zum runden Display, für die bereits Lehrgeld gezahlt wurde

- Die Flush-Bereiche (Volcado-Fenster) müssen **auf 2 px ausgerichtet sein**, sonst zeichnet das AMOLED grüne Linien an den Rändern (`round_area_cb` in `display.c`).
- Beim Systemstart müssen die **480×480 des GRAMs mit Schwarz gelöscht werden**: Der Speicher des Panels ist größer als die 466 sichtbaren Pixel und `set_gap` deckt nicht alle Rotationsfälle ab.
- **Nutzbarer Radius**: `RING_D` beträgt 380, und Uhrzeit sowie Netzwerkstatus liegen **INNERHALB** des Rings (`LV_ALIGN_CENTER, -146` und `-124`). Zuvor lag der Durchmesser bei 322, da bei größerem Durchmesser der Kreisbogen durch die Statuszeile lief und der Text über der Farbe unlesbar wurde; die Statusanzeige nach innen zu verlegen, ermöglichte es erst, den Bogen fast bis an den Rand zu ziehen.
- **Der freie Raum innerhalb des Rings ist `RING_INNER_R` = 168**, nicht 190: `RING_D/2` ist der äußere Rand, von dem die 22 px Dicke des Bogens abgezogen werden müssen. Daraus leiten sich alle Koordinaten der Seiten ab.
- **Diagramme gehen vollflächig bis zum Rand und liegen unter allem**: 430 px Breite (breiter als der Ring selbst), VOR den anderen Elementen erstellt, damit sie im Hintergrund liegen. Die Linie verläuft hinter den Schenkeln des Bogens und wird durch das Glas beschnitten, was dem PowerDot-Effekt entspricht. Achtung: Läuft sie durch die Mitte, verdeckt sie die Ziffer – sie muss als unteres Band (y ≈ +100) platziert werden (getestet).
- **Dieses untere Band schneidet hineinragenden Text ab.** Mit 90 px Höhe, zentriert bei +100, belegt das Diagramm den Bereich von +55 bis +145. Auf zwei Seiten liegt dort Text: Gase (der Hinweis „100 = normales Umfeld“ bei +118) und Partikel (die Zahlen für PM1.0/PM4.0/PM10 von +57 bis +97). Auf beiden Seiten durchstrich die Kurve den Text komplett. Lösung: Ein deckender Hintergrund in Displayfarbe hinter dem Text, wie es der Ring bei CO₂ bereits tut; die Linie läuft dahinter durch und schaut durch Lücken hervor. Vor dem Hinzufügen eines Diagramms zu einer Seite immer prüfen, was zwischen +55 und +145 liegt.
- **Die beiden Ringe sind nicht identisch.** Der Übersichtsbildschirm (Ampel) wird vollflächig gezeichnet und hat einen Indikator in derselben Dicke wie die Spur; die Messwert-Bögen haben einen um 8 px dünneren Indikator, da bei gleicher Dicke ein kleiner Messwert wie ein abgesetzter Klecks bzw. Renderfehler wirkt.
- `lv_chart` arbeitet mit Ganzzahlen (Integers). Für Werte mit Dezimalstellen (Temperatur, Feinstaub) muss vor dem Eintragen **mit 10 skaliert werden**, da die Kurve sonst stufig dargestellt wird (`chart_refresh`).

## Arbeitsablauf (Workflow)

UI-Änderungen im Simulator testen, nicht durch ständiges Flashen:

```bash
cd sim && cmake --build build -j && ./build/sen66_sim --scenario bad
./build/sen66_sim --shots /tmp/caps 5   # Screenshots zur visuellen Kontrolle
```

Der Simulator setzt voraus, dass `idf.py build` zuvor mindestens einmal ausgeführt wurde, damit der Component Manager LVGL in `managed_components/` abgelegt hat.

Firmware:

```bash
source ~/esp/esp-idf/export.sh && idf.py build
idf.py -p /dev/cu.usbmodem1101 flash monitor
```

### Veröffentlichung über den Web-Installer

`flash.html` auf GitHub Pages ist die Installationsmethode für Anwender, die nicht selbst kompilieren. **Es gibt keine CI**: Das Repository hat keine `.github/workflows`, nichts wird automatisch veröffentlicht. Alles liegt im Branch `gh-pages` und wird manuell hochgeladen:

1. Merge in `main` und **`APP_VERSION` in `main/version.h` erhöhen**. Der Installer stellt die Version bereit, die in `firmware/manifest.json` steht; wird dies vergessen, liefert die Website unterschiedliche Binärdateien unter derselben Versionsnummer aus.
2. **Die Binärdatei für den Web-Installer ist NICHT dieselbe wie für OTA.** Das Manifest deklariert einen einzelnen Teil bei `offset: 0`, also ein zusammengeführtes Gesamt-Image (Bootloader + Partitionstabelle + App). `build/monitor_sen66.bin` ist nur die App: Bei Offset 0 geflasht führt dies zu einem System, das nicht bootet. Es wird `idf.py merge-bin` benötigt.
3. Commit im Branch `gh-pages`: Die neue `.bin` nach `firmware/` legen und `manifest.json` mit aktualisierter `version` und `path` versehen.
4. **Die Screenshots in `img/` veralten ebenfalls.** Sie zeigen die Ansichten der UI, und `index.html` nennt die Anzahl im Text („Fünf Ansichten“, „neun Messgrößen“). Sie werden mit dem Simulator in nativer Panelgröße neu generiert:

   ```bash
   ./build/sen66_sim --page N --warp 1 --offset 900 --scenario good --shots DIR 1
   ```

   Ohne `--page` und mit 4 Aufnahmen landet der dritte Screenshot bereits im Ruhemodus.

## Bereits auf echter Hardware verifiziert (20.08.2026)

Erster realer Systemstart: Display, Touch, RTC und SEN66 (Seriennummer 0123456789ABCDEF, Firmware 4.1) laufen an GPIO17/18. Gewonnene Erkenntnisse:

- **Der LVGL-Puffer darf keine feste Zeilenzahl haben.** Auf diesem Board beträgt der größte zusammenhängende interne DMA-Speicherblock **144 KB** (nicht die ~192 KB von Hamlet), sodass die übernommenen 185 Zeilen nicht passen und `lvgl_port_add_disp()` in einer Boot-Schleife abbricht. `display.c` berechnet die Größe nun zur Laufzeit und hält 56 KB Reserve für das restliche System vor. Niemals wieder fest im Code verdrahten.
- **Den freien internen Heap im Blick behalten, den `app_main` am Ende ausgibt.** Mit maximalem Puffer blieben nur 26 KB übrig – viel zu knapp für WLAN im Station-Modus + MQTT + OTA. Mit 96 Zeilen bleiben 42 KB frei. Wenn gespart werden muss, dann am Display-Puffer: 96 und 114 Zeilen rendern in denselben 5 Teilen pro Frame.
- **Der Siebdruck auf der Stiftleiste gilt vor der Dokumentation.** Siehe `board.h`.
- **I2C-Bus vor dem Öffnen zurücksetzen / wiederherstellen** (`bus_recover()` in `sen66.c`). Ein ESP32-Reset mitten in einer Transaktion führt dazu, dass der SEN66 die SDA-Leitung auf Masse hält und der Sensor bis zum nächsten Power-Cycle als „nicht erkannt“ gilt. Dies passiert bei jedem `idf.py flash`. Manuelle SCL-Taktimpulse + STOP-Bedingung beheben das.
- **Ein optimistischer Log-Eintrag ist keine Überprüfung.** Zweimal in derselben Sitzung passiert: `net.c` meldete „Portal geöffnet“ bei deaktiviertem Funkmodul, und `webcfg.c` gab 192.168.4.1 aus, obwohl das Gerät im Heimnetzwerk war. Nur loggen, was tatsächlich verifiziert wurde, nicht was beabsichtigt war.

## Getroffene Entscheidungen

- **Das Webinterface besitzt bewusst kein Passwort.** Vertrauenswürdiges Heimnetzwerk, und OTA-Updates sind im Gegenzug denkbar einfach. Dies ist im README dokumentiert, damit klar ist, dass es sich um eine bewusste Entscheidung und kein Versäumnis handelt.
- **Der Lüfter wird bei Akkubetrieb NICHT zyklisch abgeschaltet.** Das brächte zwar die größte Ersparnis, aber im Stillstand kann der Sensor nichts messen (siehe Kommentar in `app_main.c`). Gemessene Akkulaufzeit im Vollbetrieb: ~5 h mit einer 1000-mAh-Zelle.

## Ausstehend

1. Fotos der Verkabelung im MakerWorld-Modelleintrag. Der Text ist bereits eingepflegt (22.08.2026); es fehlt noch ein Foto der verkabelten Stiftleiste, um letzte Unklarheiten zu beseitigen.

## Erledigt (zur Referenz)

- **Schallpegelmesser (1.4.0)**. Die Mikrofone sind an einen **ES7210 bei 0x40** angebunden, nicht an den ES8311. Eigener Treiber (der offizielle nutzt noch die alte I2C-API). Drei Stolpersteine, jeweils ein OTA-Zyklus:
  1. Das Abfragen mit einer erfundenen Register-ID führt zu NACK (`i2c_master_probe` nutzen);
  2. Der I2S-Kanal muss aktiviert werden, BEVOR der Codec konfiguriert wird;
  3. Und vor allem: **Im Vollduplex-Betrieb generiert der SENDE-Kanal die Taktsignale**, den `sound.c` bisher nur während der Wiedergabe eines Warntons eingeschaltet hatte – ohne MCLK liefert der ES7210 keinerlei Daten. Nun bleibt er dauerhaft aktiv und der Verstärker wird weiterhin zwischen Tönen stummgeschaltet.
- **Geräuschpegel ist mit 112 dB Offset kalibriert** (09.09.2026), gemessen mit einer Smartphone-Schallpegel-App direkt am Gerät bei zwei um 10 dB versetzten Stützpunkten:
  - Umgebung: 54,6 dB vs. −57,8 dBFS;
  - Musik: 64,2 vs. −48,1 dBFS.
  Daraus ergeben sich 112,4 und 112,2 dB – also eine **konstante Verschiebung ohne Steigungsabweichung**, was sich exakt durch eine Konstante korrigieren lässt. Die werksseitigen 120 stammten aus einem Vergleich mit einem Qingping, der **nicht aussagekräftig** war: 1.983 Messpunkte ergaben eine Korrelation von nur 0,583. Beim finalen Test stieg der Monitor um 26 dB über sein Grundrauschen, während der Qingping unverändert bei 36 dB blieb. **Schall verteilt sich im Raum nicht homogen wie CO₂ oder Temperatur**; zwei Geräte an unterschiedlichen Positionen lassen sich daher nicht direkt vergleichen. Über Zehn-Sekunden-Fenster muss auf beiden Seiten simultan nach Energie gemittelt werden: Zwei Momentanwerte bei Musik ergaben je nach Sekunde 95 bzw. 111 dB.
- **Der Lüfter des SEN66 erhöht das Grundrauschen um 0,9 dB** (09.09.2026), d. h. der Lüfter allein erzeugt ca. **36 dB SPL**: wahrnehmbar, aber 15 dB unter einem normalen Raumgeräuschpegel. Acht alternierende 45-Sekunden-Fenster, ausgewertet nach dem **Minimum**:
  - EIN: −68,0, −68,0, −68,4, −69,1, −69,3 (Mittelwert −68,6)
  - AUS: −69,2, −69,6, −69,6 (Mittelwert −69,5).
  Alle drei AUS-Werte lagen unter den fünf EIN-Werten bei einem Viertel der Streuung. **Dies korrigiert den ersten Versuch vom August**, der mit kurzen Fenstern +5,2 dB und dann −5,6 dB (physikalisch unmöglich) ergab und als „nicht messbar“ abgehakt worden war: Die Methode war fehlerhaft, nicht der Lüfter. Erfolgsfaktoren: 45-Sekunden-Fenster, Minimum oder 5. Perzentil als statistische Größe und **A/B/A/B abwechselnd messen, statt Block A und dann Block B**.
- **Der übertragene Messwert ist ein Momentanwert** (125-ms-RMS), kein gleitender Durchschnitt. **Das Grundrauschen des Geräts liegt bei 43 dB** und es gibt **keine A-Bewertung**: Die Berechnung ist ein reiner ungewichteter RMS-Wert. Diese drei Faktoren erklären, warum das Gerät 53 dB anzeigt, wo ein Haushalts-Schallpegelmesser 37 anzeigt, ohne dass einer davon falsch misst. Die A-Kurve dämpft vor allem tiefe Frequenzen, die in ruhigen Räumen dominieren; der Unterschied ist unten groß und bei Musik gering – bei 75 dB stimmt der Wert bis auf 2 dB mit dem Qingping überein. **Wie viel davon Grundrauschen und wie viel fehlende Filterung ist, lässt sich mit den vorhandenen Daten nicht trennen**: Eine Smartphone-App misst minimale 28 dB, wo der Monitor nicht unter 43 dB fällt. Für die Praxis bedeutet das: Unterhalb von ~50 dB ist der Messwert wenig aussagekräftig. Sollte der untere Messbereich künftig relevant sein, muss eine A-Bewertung in `mic.c` implementiert werden.
- **Die Luftführung des AirRing-Gehäuses ist messtechnisch verifiziert** (22.08.2026) und nicht nur angenommen. Der SEN66 besitzt zwei Einlässe (rechteckige Öffnung und Membran) sowie einen Auslass (den Lüfter); Sensirion verlangt, diese voneinander zu trennen und vom Geräteinneren zu isolieren. Beides wurde durch Messungen bestätigt:
  - +0,4 °C gegenüber einem separaten Thermometer schließt aus, dass warme Luft aus dem Gehäuseinneren angesaugt wird.
  - Ein Räucherstäbchen-Test ergab eine Abklingkonstante für PM2.5 von **9,7 min gegenüber 8,7 min** beim Referenzsensor – damit ist ausgeschlossen, dass der Sensor seine eigene Abluft wieder einsaugt. Messdaten auf dem DietPi unter `/root/prueba-humo-20260822.csv`.
- **Es wird bewusst KEIN Offset angewendet, weder für Temperatur noch für CO₂** (22.08.2026): 22 Stunden Vergleichsmessung gegen einen Qingping Air Monitor 2 und ein separates Thermometer, alle drei auf demselben Tisch. Die vollständige Herleitung steht im README unter „Über das Messen von Abweichungen“, die Rohdaten liegen auf dem DietPi unter `/root/comparacion-20260821.csv`. Zusammenfassend:
  - Gegenüber der Referenz liegt der SEN66 bei **+0,4 °C**, voll innerhalb seiner Spezifikation (±0,5 °C). Die beobachtete Feuchteabweichung stammte **vom Qingping** (+8,9 %), nicht vom SEN66.
  - Die Temperaturdifferenz **ist nicht konstant**: Sie schwankt je nach Tageszeit zwischen +0,20 und +1,30 °C. Ein fester Offset passt zu einer Stunde und liegt zur nächsten daneben.
  - Beim CO₂ ist die Abweichung eine **Steigungsabweichung, kein Nullpunktfehler**: `SEN66 = 1,20 × Qingping − 138`, Schnittpunkt bei 675 ppm. Darunter zeigt der SEN66 weniger an, darüber mehr.
  - Die automatische Basiskalibrierung (ASC) des CO₂ **arbeitet korrekt**: 403 ppm an einem Nachmittag sahen zunächst nach einem feststeckenden Nullpunkt aus, der nächtliche Verlauf (403 → 831 bei Sonnenaufgang) bewies jedoch, dass der Raum am Nachmittag schlicht gründlich gelüftet war.
- **Drei methodische Erkenntnisse aus dieser Messreihe**:
  1. Zwei Geräte zu vergleichen zeigt nur den Unterschied, nie wer recht hat;
  2. Es muss ein kompletter 24-Stunden-Zyklus erfasst werden, da Differenzen oft nur tagsüber konstant wirken;
  3. Es muss bei unterschiedlichen Konzentrationen gemessen werden, da ein einzelner Messbereich eine Steigungsabweichung leicht als Nullpunktfehler fehlinterpretiert.
- **CO₂-Alarm und manuelle Rekalibrierung** wurden in Version 1.2.0 im Webinterface ergänzt. Die Rekalibrierung läuft nicht im Thread des Webservers: Der Befehl erfordert eine gestoppte Messung, daher stellt das Webinterface den Auftrag in eine Warteschlange und `sensor_task` führt ihn aus. Zwei Details aus der Sensirion-Dokumentation, die nicht aus dem Code ersichtlich sind: Der zurückgegebene Korrekturwert hat einen **Offset von 0x8000**, und zwischen Stoppen und Rekalibrieren müssen **600 ms Wartezeit** eingehalten werden.
- **Der Rekalibrierungspfad ist noch nicht auf realer Hardware getestet**: Das Auslösen schreibt in das EEPROM des Sensors und ist nur im Freien bei 420 ppm sinnvoll. Validierung, Warteschlange und das Abweisen unplausibler Referenzwerte sind jedoch getestet.
- **Niemals mqttthing-Zubehör über das Homebridge-Formular bearbeiten**: Es löscht den gesamten `topics`-Block, da es die `apply`-Funktionen nicht abbilden kann. Ist passiert und führte dazu, dass alle vier Zubehörteile keine Daten mehr empfingen. Siehe `docs/HOMEBRIDGE.md`.
- **Der über MQTT übertragene Statuslevel ist ein englischer Token** (`good`…`bad`), nicht der übersetzte String. Eine frühere Änderung hatte das Homebridge-Mapping beschädigt, während das README noch den alten Zustand beschrieb.
