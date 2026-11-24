Kurzbeschreibung der Git-Branches (Monitor SEN66)

Die folgenden Branches bauen linear aufeinander auf – jede neue Branch enthält also automatisch alle Änderungen der jeweils vorhergehenden Branch:

dimming → mqtt-sprache → dimmingT → CleanBuildSmoothFading

1. dimming (Ausgangsbasis)

Enthält alle grundlegenden Anpassungen gegenüber dem Originalprojekt: WPA3/PMF-Unterstützung (FRITZ!Box), deutsche Logs & Dokumentation, optimiertes 1,75"-AMOLED-Layout (446 px Ringe, Zentrierung, größere Schriften) sowie den ersten Ansatz für das CO5300-Hardware-Dimming (0x53 / 0x51).

2. mqtt-sprache (basiert auf dimming)

Home Assistant Mehrsprachigkeit (main/ha_mqtt.c): Standard-Sensoren mit device_class senden keinen festen spanischen Namen mehr, sodass Home Assistant sie automatisch in der eingestellten HA-Benutzersprache (Deutsch/Englisch/usw.) benennt.

Der Sensor für die Gesamtbewertung (level) übernimmt dynamisch die eingestellte Gerätesprache (Luftqualität / Air quality / Calidad del aire).

3. dimmingT (basiert auf mqtt-sprache)

Zuverlässiges Dimmen (main/display.c, main/ui.c, main/ui.h): Kombination aus CO5300-Hardware-Dimming (inkl. erneutem Senden von 0x53 nach Display-Off) und einem schwarzen LVGL-Overlay für garantiertes stufenloses Abdunkeln.

Touch-Wakeup repariert (main/ui.c): Berührungs- und Wisch-Events werden am Tileview sauber abgefangen, sodass jeder Touch das gedimmte Display sofort wieder aufweckt.

Weboberfläche (main/webcfg.c): „Abdunkeln nach“ von festem Dropdown auf ein freies Sekunden-Eingabefeld umgestellt; Live-Vorschau für beide Helligkeitsregler (Tag & Gedimmt) aktiviert.

4. CleanBuildSmoothFading (basiert auf dimmingT)

Smooth Fading (main/ui.c): Sanftes, animiertes Ausblenden (500 ms) beim Abdunkeln und Einblenden (250 ms) beim Aufwecken per Touch.

Clean Build (main/ui.c): Alle veralteten LVGL-9-Flag-Aufrufe (lv_obj_add_flag / lv_obj_remove_flag) auf die neuen dedizierten Setter umgestellt – das Projekt kompiliert nun komplett warnungsfrei.