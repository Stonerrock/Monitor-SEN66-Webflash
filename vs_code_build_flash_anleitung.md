# Kurzanleitung: Bauen & Flashen mit ESP-IDF v6.1 in VS Code

Diese Anleitung dient als Schnelleinstieg zum Einrichten der Umgebung, Bauen und Flashen des **Monitor SEN66** Projekts über VS Code oder die Windows Eingabeaufforderung (CMD).

---

## Voraussetzungen

- **ESP-IDF v6.1** installiert unter `C:\esp\v6.1\esp-idf`
- **VS Code** mit integriertem Terminal (CMD) oder Windows Command Prompt
- ESP32-S3 Board per USB angeschlossen

---

## Schritt-für-Schritt-Anleitung

### 1. Terminal öffnen & Umgebung laden
Öffne das integrierte Terminal in VS Code (`Strg` + `` ` ``) oder die Standard-Eingabeaufforderung (CMD) im Projektverzeichnis:

```cmd
C:\esp\v6.1\esp-idf\export.bat
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

---

## Nützliche Befehle

| Befehl | Aktion |
| :--- | :--- |
| `idf.py build` | Nur geänderte Dateien neu kompilieren |
| `idf.py flash` | Firmware flashen ohne den Monitor zu starten |
| `idf.py monitor` | Seriellen Log-Monitor separat öffnen |
| `idf.py fullclean` | Build-Verzeichnis und Cache komplett löschen |
| `idf.py erase-flash` | Gesamten NVS- und Flash-Speicher löschen |