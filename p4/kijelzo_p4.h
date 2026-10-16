// Guition JC8012P4A1C (ESP32-P4, 10,1" 800x1280 MIPI-DSI kijelző, GSL3680 érintés): kijelző, érintés, háttérvilágítás
// Csak a P4-es fordításban (platformio.ini, [env:idojaras_p4]) kerül be; az Arduino IDE a p4/ mappát nem fordítja.
//
// A program felülete fekvő (1280x800, kijelzo.h). A kijelző álló (800x1280), ezért a kép 90 fokkal elforgatva kerül
// a panelre (kisebb felületnél a közepére, körben fekete kerettel); az érintés koordinátáit ugyanígy vissza kell számolni.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define P4_BL_START 30   // a háttérvilágítás induláskor (%), innen húzza fel a backlight_task()
#define P4_LVGL_LINES 200   // az LVGL rajzpufferének magassága (sor, PSRAM-ban): egy kép 800/200 = 4 csíkban

// A kijelző indítása (MIPI-DSI tápja, JD9365 panel, háttérvilágítás). log: üzenetek a Naplóba. false: hiba.
bool p4_display_init(int ui_w, int ui_h, void (*log)(const char *msg));
// Egy téglalap (a felület koordinátáiban, x2/y2 is benne van) kirajzolása RGB565 pixelekből
void p4_display_flush(int x1, int y1, int x2, int y2, const uint16_t *px);
// A háttérvilágítás (0..100 %)
void p4_backlight(int pct);

// Az érintésvezérlő indítása (GSL3680, I2C). false: nem található.
bool p4_touch_init(void (*log)(const char *msg));
// Az érintés helye a felület koordinátáiban; false, ha nincs érintés (vagy a fekete kereten kívül esik)
bool p4_touch_read(int *x, int *y);

// Az RX8025T óra (UTC) olvasása és beállítása; az érintés buszán, ezért a p4_touch_init() után. false: nincs óra,
// vagy az ideje érvénytelen (lemerült gombelem, még nem volt beállítva). wday: 0 = vasárnap.
bool p4_rtc_read(int *y, int *mo, int *d, int *h, int *mi, int *s);
bool p4_rtc_write(int y, int mo, int d, int h, int mi, int s, int wday);

// Hang: az ES8311 kodek és az I2S indítása (az érintés után); hangjelzés lejátszása a háttérben (nem vár)
bool p4_audio_init(void (*log)(const char *msg));
void p4_beep(float amp);   // amp: 0..1

#ifdef __cplusplus
}
#endif
