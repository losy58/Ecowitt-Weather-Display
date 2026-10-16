// Melyik kijelzőre fordul a program: Waveshare ESP32-S3-Touch-LCD-7B (alap) vagy Guition JC8012P4A1C (ESP32-P4, 10,1",
// platformio.ini: [env:idojaras_p4]). A P4-en a felület 1280x800-as, 90 fokkal elforgatva (p4/kijelzo_p4.c).
// (Külön fejlécben, mert a PlatformIO .ino-átalakítója az .ino-ban a feltételes #define-okat újradefiniálásnak látja.)
#pragma once
#include <sdkconfig.h>

// A felület méreteit (helyek, szélességek, sugarak) a 7B 1024x600-as képernyőjére írtuk; a P4-en ezeket a makrók
// nagyítják 1280x800-ra: SX() a vízszintes (x, szélesség), SY() a függőleges (y, magasság), S() az egyenletes
// (körök, ikonok, iránytű, betűhöz igazodó méretek; a fontok is 1,25-szörösek, p4/font_*.c). A 7B-n mind változatlan.
#if CONFIG_IDF_TARGET_ESP32P4
#define KIJELZO_P4 1
#define FW_BOARD " (P4)"       // a programnév végére (kezelőoldal, Beállítások): melyik kijelzőre készült
#define MDNS_HOST "idojaras-p4"   // a név a hálózaton: http://idojaras-p4.local/ (hogy ne ütközzön a 7B-s kijelzővel)
#define LCD_H_RES 1280
#define LCD_V_RES 800
#define SX(v) ((v) * 5 / 4)
#define SY(v) ((v) * 4 / 3)
#define S(v)  ((v) * 5 / 4)
#define FONT_SYM lv_font_montserrat_34   // a beépített jelek (fogaskerék, X, törlés) fontja
#include "p4/kijelzo_p4.h"
#else
#define KIJELZO_P4 0
#define FW_BOARD ""
#define MDNS_HOST "idojaras"      // http://idojaras.local/
#define LCD_H_RES 1024
#define LCD_V_RES 600
#define SX(v) (v)
#define SY(v) (v)
#define S(v)  (v)
#define FONT_SYM lv_font_montserrat_28
#endif
