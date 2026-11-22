# Kurzanleitung: Bauen & Flashen mit ESP-IDF v5.5.2 in VS Code

Diese Anleitung dient als Schnelleinstieg zum Einrichten der Umgebung, Bauen und Flashen des **Monitor SEN66** Projekts über VS Code oder die Windows Eingabeaufforderung (CMD).

---

## Voraussetzungen

- **ESP-IDF v5.5.2** installiert (in diesem Setup unter `C:\Users\stone\esp\v5.5.2\esp-idf`)
- **VS Code** mit integriertem Terminal (CMD) oder Windows Command Prompt
- ESP32-S3 Board per USB angeschlossen

---

## Schritt-für-Schritt-Anleitung

### 1. Terminal öffnen & Umgebung laden
Öffne das integrierte Terminal in VS Code (`Strg` + `` ` ``) oder die Standard-Eingabeaufforderung (CMD) im Projektverzeichnis:

```cmd
C:\Users\stone\esp\v5.5.2\esp-idf\export.bat
```

### 2. Ziel-Hardware festlegen
Setze die Ziel-Architektur auf den ESP32-S3 (nur einmalig nötig oder nach dem Löschen des Build-Ordners):

```cmd
idf.py set-target esp32s3
```

### 3. Projekt bauen
Kompiliere die Firmware und überprüfe die Abhängigkeiten:

```cmd
idf.py build
```

### 4. Flashen & Seriellen Monitor starten
Übertrage die kompilierte Firmware auf den ESP32-S3 und öffne direkt den Log-Monitor:

```cmd
idf.py flash monitor
```

> **Hinweis:** Um den seriellen Monitor jederzeit zu beenden, drücke `Strg` + `]`.

### Fehlerbehebung: „Found 0 serial ports“

Wenn `esptool.py` mit `Found 0 serial ports` abbricht, wurde noch keine Verbindung
zum ESP32 versucht: Windows hat keinen seriellen Anschluss für das Board
bereitgestellt. Das ist kein Build-Fehler.

1. Verwende ein **USB-C-Datenkabel** (ein reines Ladekabel reicht nicht) und
   verbinde das Board direkt mit dem PC, nicht über einen Hub.
2. Öffne den **Geräte-Manager** und prüfe unter „Anschlüsse (COM & LPT)“, ob
   beim An- und Abstecken ein Anschluss wie `COM5` erscheint. Falls nicht,
   prüfe auch „Andere Geräte“ auf ein unbekanntes USB-Gerät. Je nach erkanntem
   USB-Gerät kann ein passender USB-seriell-Treiber nötig sein.
3. Sobald ein COM-Port angezeigt wird, wähle ihn in der ESP-IDF-Erweiterung
   als seriellen Port aus oder gib ihn beim Flashen ausdrücklich an:

   ```cmd
   idf.py -p COM5 flash monitor
   ```

   Ersetze `COM5` durch den Port aus dem Geräte-Manager. Wenn der Port
   vorhanden ist, die Verbindung aber weiterhin scheitert, Board zurücksetzen
   und erneut versuchen; nur dann ist der Boot-Modus als nächste Ursache zu
   prüfen.

---

## Nützliche Befehle

| Befehl | Aktion |
| :--- | :--- |
| `idf.py build` | Nur geänderte Dateien neu kompilieren |
| `idf.py flash` | Firmware flashen ohne den Monitor zu starten |
| `idf.py monitor` | Seriellen Log-Monitor separat öffnen |
| `idf.py fullclean` | Build-Verzeichnis und Cache komplett löschen |
| `idf.py erase-flash` | Gesamten NVS- und Flash-Speicher löschen |