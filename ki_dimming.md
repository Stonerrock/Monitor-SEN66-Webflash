# Anleitung: Dynamische Helligkeitssteuerung (Dimming) für Waveshare AMOLED 1.75\" (CO5300)

Dieses Dokument dient als Handlungsanweisung für eine KI oder Entwickler, falls die Helligkeitsregelung / das Dimmen für das runde 1,75\" AMOLED-Display erneut implementiert, debuggt oder erweitert werden muss.

---

## 1. Hardware- und Systemkontext

- **Entwicklungsboard:** Waveshare ESP32-S3-Touch-AMOLED-1.75 (SKU 31261)
- **Display:** Rundes AMOLED, 466×466 Pixel, QSPI-Interface
- **Display-Controller-IC:** CO5300 (Treiber-Komponente `esp_lcd_co5300`)
- **Framework:** ESP-IDF 5.x + LVGL 9 (`esp_lvgl_port`)
- **Relevante Quelldatei:** `main/display.c` (Hardware-Ansteuerung) und `main/ui.c` (UI-Callbacks)

---

## 2. Problemstellung & Fehlerbild

**Symptom:**
Das Display reagiert bei der Helligkeitseinstellung binär:
- Helligkeitswert = `0`: Display ist aus.
- Helligkeitswert `> 0` (z. B. 1 bis 255): Display ist an, leuchtet jedoch immer mit konstanter, voller Helligkeit. Schieberegler im Webinterface oder der UI haben keine stufenlose Wirkung.

**Ursache:**
- In `main/display.c` wird bei `level == 0` die Funktion `esp_lcd_panel_disp_on_off(s_panel, false)` aufgerufen (DCS-Befehl `0x28` / DISPOFF).
- Bei `level > 0` wird `esp_lcd_panel_disp_on_off(s_panel, true)` aufgerufen (DCS-Befehl `0x29` / DISPON) und anschließend das MIPI-DCS-Register `0x51` (`LCD_CMD_BRIGHTNESS`) geschrieben.
- **Der CO5300-Controller ignoriert das Register `0x51` standardmäßig komplett**, solange der Helligkeitssteuerungs-Block nicht über das DCS-Register **`0x53` (`WRCTRLD` / Write Display Control)** scharfgeschaltet wurde.
  - Bit 5 (`BCTRL`): `0` = Helligkeitssteuerung blockiert (Register 0x51 inaktiv), `1` = Helligkeitssteuerung freigegeben (`0x20`).

---

## 3. Handlungsanweisung: Hardware-Dimming (CO5300)

### Schritt 3.1: Register-Definitionen in `main/display.c`
In `main/display.c` sicherstellen, dass sowohl `0x51` als auch `0x53` deklariert sind und der Initialisierungsstatus getrackt wird:

```c
#define LCD_CMD_BRIGHTNESS 0x51 // MIPI DCS: Write Display Brightness (1 Byte: 0..255)
#define LCD_CMD_WRCTRLD    0x53 // MIPI DCS: Write CTRL Display (BCTRL Freigabe)

static bool s_bctrl_init = false;
```

### Schritt 3.2: Freischaltung in `display_set_brightness`
In der Funktion `display_set_brightness(uint8_t level)` in `main/display.c` muss vor dem Senden von `0x51` das Register `0x53` mit `0x20` (BCTRL = 1) beschrieben werden:

```c
void display_set_brightness(uint8_t level)
{
    if (!s_panel_io) return;

    // Level 0: Display komplett ausschalten (echtes Schwarz / Energiesparen auf AMOLED)
    if (level == 0) {
        if (s_disp_on && s_panel) {
            esp_lcd_panel_disp_on_off(s_panel, false);
            s_disp_on = false;
        }
        return;
    }

    // Display reaktivieren, falls es zuvor aus war
    if (!s_disp_on && s_panel) {
        esp_lcd_panel_disp_on_off(s_panel, true);
        s_disp_on = true;
    }

    // CO5300 BCTRL aktivieren, falls noch nicht geschehen
    if (!s_bctrl_init) {
        esp_lcd_panel_io_tx_param(s_panel_io, LCD_CMD_WRCTRLD, (uint8_t[]){0x20}, 1);
        s_bctrl_init = true;
    }

    // Stufenlose Helligkeit von 1 bis 255 setzen
    esp_lcd_panel_io_tx_param(s_panel_io, LCD_CMD_BRIGHTNESS, (uint8_t[]){level}, 1);
}
```

*Hinweis:* Sollte ein CO5300-Panel-Modul nach Reset den Status von `0x53` verlieren, kann das Senden von `0x20` auch bei jedem Einschaltvorgang (`!s_disp_on`) wiederholt werden.

---

## 4. Alternative / Ergänzung: Software-Dimming via LVGL

Falls bei sehr geringen Helligkeitsstufen (z. B. Nachtbetrieb `level = 1..20`) Farbstiche (typisches OLED-Mura-Phänomen bei Niedrigstrom) auftreten oder ein Panel kein feines analoges Abdimmen unterstützt:

### Konzept:
Ein vollflächiges schwarzes Overlay auf `lv_layer_top()` legen, dessen Deckkraft (`LV_OPA_0` bis `LV_OPA_COVER`) invers zur gewünschten Helligkeit gesteuert wird. Da beim AMOLED schwarze Pixel keine Emission haben, führt die softwareseitige Abdunklung direkt zu reduzierter Leuchtkraft und geringerem Stromverbrauch.

### Umsetzungsschritte in LVGL:
1. Auf `lv_layer_top()` ein unklickbares Objekt erzeugen:
   ```c
   static lv_obj_t *s_dim_overlay = NULL;

   void ui_dim_init(void) {
       s_dim_overlay = lv_obj_create(lv_layer_top());
       lv_obj_set_size(s_dim_overlay, LV_PCT(100), LV_PCT(100));
       lv_obj_set_style_bg_color(s_dim_overlay, lv_color_black(), 0);
       lv_obj_set_style_border_width(s_dim_overlay, 0, 0);
       lv_obj_set_style_radius(s_dim_overlay, 0, 0);
       lv_obj_remove_flag(s_dim_overlay, LV_OBJ_FLAG_CLICKABLE);
       lv_obj_set_style_bg_opa(s_dim_overlay, LV_OPA_TRANSP, 0);
   }
   ```
2. In der Dimming-Funktion die Deckkraft berechnen:
   ```c
   void ui_set_software_dimming(uint8_t brightness_255) {
       if (!s_dim_overlay) return;
       // Bei 255 -> OPA 0 (voll transparent), bei 0 -> OPA COVER (voll schwarz)
       lv_opa_t opa = (lv_opa_t)(255 - brightness_255);
       lv_obj_set_style_bg_opa(s_dim_overlay, opa, 0);
   }
   ```

---

## 5. Wichtige Projekt-Invarianten (Nicht verletzen!)

1. **Keine ESP-IDF-Header in `main/ui.c`:**
   `main/ui.c` muss simulator-kompatibel bleiben (`sim/main.c`). Helligkeitsfunktionen der Plattform dürfen ausschließlich über Funktionszeiger (z. B. `void (*set_brightness)(uint8_t)`) an die UI übergeben werden.
2. **Level 0 bleibt Display-Off:**
   AMOLED-Pixel emittieren selbst bei `brightness = 1` noch minimales Licht. Um den Bildschirm nachts oder bei Akkubetrieb vollständig dunkel zu schalten, muss bei `0` zwingend `esp_lcd_panel_disp_on_off(s_panel, false)` ausgeführt werden.
3. **NVS-Schreibvorgänge trennen:**
   Beim Verstellen der Helligkeit über Webinterface oder Schieberegler darf `settings_save()` **niemals** aus dem UI-Thread aufgerufen werden, da NVS-Writes das System für ca. 1 Sekunde blockieren.
