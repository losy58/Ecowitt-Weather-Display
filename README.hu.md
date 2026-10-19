# Ecowitt Weather Display

**Időjárás-kijelző Ecowitt állomásokhoz** · *[English version](README.md)*

Önálló, érintőképernyős időjárás-kijelző ESP32-re. Az Ecowitt konzolok és átjárók („customized upload”, Ecowitt
protokoll) adatait közvetlenül, a helyi hálózaton fogadja – felhő és külön szerver nélkül –, főoldalt,
grafikonokat, előrejelzést, részletes adatokat és naplót mutat. Saját webes kezelőoldala van (beállítások, OTA-frissítés,
diagnosztika). A felület (kijelző, webes kezelőoldal, telefonos
értesítések) angol és magyar nyelvű: alapból angol, a kijelzőn vagy a kezelőoldalon váltható.

Ugyanabból a kódból két kijelzőre fordul:

| Kijelző | Lapka | Felbontás | PlatformIO-környezet |
|---|---|---|---|
| Waveshare ESP32-S3-Touch-LCD-7B (7") | ESP32-S3, 16 MB flash, 8 MB PSRAM | 1024x600, RGB | `idojaras` (alapértelmezett) |
| Guition JC8012P4A1C_I_W_Y (10,1") | ESP32-P4 + ESP32-C6 (WiFi) | 1280x800, MIPI-DSI | `idojaras_p4` |

## Képernyőképek

Fényképek a kijelzőről bemutató módban (kitalált adatok, angol felület).

![Főoldal](docs/kepek/dashboard.jpg)

<table>
  <tr>
    <td width="50%"><img src="docs/kepek/forecast.jpg" alt="Előrejelzés: 4 nap és 24 óra"><br><sub>Előrejelzés: 4 nap és 24 óra</sub></td>
    <td width="50%"><img src="docs/kepek/details.jpg" alt="Részletes adatok"><br><sub>Részletes adatok</sub></td>
  </tr>
  <tr>
    <td width="50%"><img src="docs/kepek/outdoor-week.jpg" alt="Kinti grafikonok, heti nézet"><br><sub>Kinti grafikonok, heti nézet</sub></td>
    <td width="50%"><img src="docs/kepek/outdoor-month.jpg" alt="Kinti grafikonok, havi nézet"><br><sub>Kinti grafikonok, havi nézet</sub></td>
  </tr>
  <tr>
    <td width="50%"><img src="docs/kepek/indoor-week.jpg" alt="Benti hőmérséklet és CO2 helyiségenként, heti nézet"><br><sub>Benti hőmérséklet és CO2 helyiségenként, heti nézet</sub></td>
    <td width="50%"><img src="docs/kepek/settings-location.jpg" alt="Beállítások a kijelzőn: hely"><br><sub>Beállítások a kijelzőn: hely</sub></td>
  </tr>
</table>

## Tudnivalók

**Kijelző**
- **Főoldal:** kinti és benti hőmérséklet, páratartalom (száraz / komfort / nedves), CO2, szél-iránytű lökéssel,
  eső, légnyomás, UV, napsugárzás, óra, napkelte/napnyugta, holdfázis, rövid előrejelzés.
- **Előrejelzés:** 4 nap és 24 óra az [Open-Meteo](https://open-meteo.com/) alapján (kulcs nem kell, csak a hely).
- **Grafikonok:** hőmérséklet, légnyomás, szél, eső, benti hőmérséklet és CO2 napi, heti (7 nap, 5 perces minták) és
  havi (400 napos napi összesítő) nézetben; a hőmérséklet-grafikonon a látható időszak kinti minimuma és maximuma
  jelölve; koppintásra teljes képernyőn.
- **Részletes adatok** és **Napló** oldal (a program eseményei).
- **Több helyiség:** legfeljebb 3 konzol (pl. WS3910, GW3000, WN1821) benti adatai külön; a kinti adat (pl. WS90) az
  elsőtől jön, a többitől csak akkor, ha az első 2 percig hallgat.
- **Figyelmeztetések** színnel: fagy, hőség, erős lökés, magas UV, magas CO2 („Szellőztess”), lemerülő elemek,
  elavult adat.
- **Éjszakai mód:** óra szerint vagy napnyugtától napkeltéig halványabb kijelző; érintésre rövid időre felvilágít.
- **Beállítások** a kijelzőn (fogaskerék): hely, időzóna, WiFi, fényerő, nyelv, újraindítás, bemutató mód.
- **Bemutató mód:** konzolok és WiFi nélkül életszerű, kitalált adatokat mutat és magától lapoz (kiállításra,
  bemutatóra). A valódi előzményekhez nem nyúl.

**Hálózat**
- **WiFi-beállító hálózat**, ha nincs kapcsolat (`Weather-Setup`, a P4-en `Weather-Setup-P4`): a telefon magától megnyitja
  a beállító oldalt; a kijelző két QR-kóddal mutatja a lépéseket, és közben is használható (a bemutató mód egy gombnyomás).
  A hálózat később a kezelőoldalon és a kijelzőn is átállítható vagy elfelejthető.
  Mesh-hálózatban a legerősebb hozzáférési pontra csatlakozik.
- **Webes kezelőoldal** (`http://idojaras.local/`, a P4-en `http://idojaras-p4.local/`): beállítások, OTA-frissítés,
  a konzolok nyers adatai, diagnosztika (indulások oka, összeomlás-összefoglaló), opcionális jelszóval.
- **Telefonos értesítések** az [ntfy.sh](https://ntfy.sh/)-n (ingyenes, regisztráció nélkül): fagy, eső, szellőztetés,
  lemerülő elem, elakadt adat, összeomlás; egyenként ki-be kapcsolhatók.
- **Továbbküldés másik kijelzőre:** a konzolok általában csak egy címre küldenek; a kijelző a kapott adatokat
  továbbküldheti egy második kijelzőnek.

**Csak a P4-en:** a lap saját órája (RX8025T, gombelemmel) WiFi nélkül is pontos időt ad; a Li-akku feszültsége
látszik, és áramszünetről értesítést küld; hangjelzés, amikor megjelenik a „Szellőztess” (be/ki, hangerő; az éjszakai
mód időszakában csendben); **automatikus fényerő a beépített kamerával**: a kijelző a szoba fényéhez igazodik, ha
10 percig senki sem mozog előtte, elsötétül, és mozgásra felébred (a kép nem kerül ki a lapról, nem mentődik; be/ki
a kezelőoldalon).

## Szükséges eszközök

- A fenti két kijelző egyike.
- Ecowitt konzol vagy átjáró, amely tud „customized upload”-ot (Ecowitt protokoll), pl. WS3910, GW3000, WN1821,
  kinti érzékelőnek pl. WS90. Más Ecowitt-eszköz is működhet, ha ugyanazokat a mezőket küldi (a konzolok nyers adatai
  a kezelőoldal `/raw` lapján látszanak).
- 2,4 GHz-es WiFi.

## Asztali támasz

3D-nyomtatható asztali támasz a Guition JC8012P4A1C-hez (10,1"):
[Desk Stand for Guition JC8012P4A1C 10.1" ESP32-P4 Display](https://makerworld.com/hu/models/3451581-guition-jc8012p4a1c-10-1-desk-stand) a MakerWorldön. Az STL ebben a tárolóban is megvan:
[docs/desk-stand/desk-stand-ecowitt.stl](docs/desk-stand/desk-stand-ecowitt.stl).

<img src="docs/kepek/desk-stand.jpg" alt="A P4-es kijelző a 3D-nyomtatott asztali támaszban" width="600">

## Telepítés fordítás nélkül

A kész programfájlok minden [kiadáshoz](https://github.com/losy58/Ecowitt-Weather-Display/releases/latest) csatolva vannak:

| Fájl | Kijelző | Mire |
|---|---|---|
| `…-7B-factory.bin` | Waveshare 7B (ESP32-S3) | első telepítés USB-n, a `0x0` címre |
| `…-P4-factory.bin` | Guition JC8012P4A1C (ESP32-P4) | első telepítés USB-n, a `0x0` címre |
| `…-7B-ota.bin` | Waveshare 7B | a működő kijelző frissítése a webes kezelőoldalon (Firmware frissítés) |
| `…-P4-ota.bin` | Guition JC8012P4A1C | a működő kijelző frissítése a webes kezelőoldalon (Firmware frissítés) |

**Első telepítés** (USB-kábellel a kijelző USB-portjára):
- a legegyszerűbb: Chrome-ban vagy Edge-ben nyisd meg a **[webes telepítőt](https://losy58.github.io/Ecowitt-Weather-Display/)**, és kattints a *Telepítés* gombra;
  a kijelzőnek megfelelő programot magától választja ki;
- vagy böngészőből, kézzel: Chrome-ban vagy Edge-ben nyisd meg az [ESP Tool](https://espressif.github.io/esptool-js/)
  oldalt, *Connect*, válaszd ki a kijelző portját, a `factory.bin` fájlt a **`0x0`** címre add meg, *Program*;
- vagy [esptool](https://docs.espressif.com/projects/esptool/)-lal: `esptool --chip esp32s3 write-flash 0x0 <fájl>`
  (7B), ill. `esptool --chip esp32p4 write-flash 0x0 <fájl>` (P4).

A `factory.bin` a tárolt beállításokat (WiFi, hely, …) is törli: csak az első telepítéshez való. Utána lásd az
[Üzembe helyezés](#üzembe-helyezés) részt. **A frissítés** mindent megtart: az `ota.bin` fájlt a kijelző webes
kezelőoldalán kell feltölteni.

A P4-es program az ESP32-P4 v3-as lapkaváltozatára készül (v3.2-n kipróbálva; régebbi változaton nem biztos, hogy
fut), és a lap ESP32-C6 WiFi-lapkáján a gyári programot feltételezi.

## Fordítás

A végleges fordítás **PlatformIO-val** készüljön: a `platformio.ini` olyan ESP-IDF-beállításokat tartalmaz
(`custom_sdkconfig`), amelyek nélkül flash-írás (mentés, OTA) közben elcsúszik vagy villan a kép.

1. Telepítsd a [PlatformIO Core](https://platformio.org/install/cli)-t (vagy a VS Code-bővítményt).
2. Töltsd le a tárolót. A könyvtárat (LVGL 9.6.0) a PlatformIO az első fordításkor magától
   letölti.
3. Fordítás és feltöltés USB-n:
   ```
   pio run -t upload                     # Waveshare 7B (ESP32-S3)
   pio run -e idojaras_p4 -t upload      # Guition JC8012P4A1C (ESP32-P4)
   pio device monitor                    # soros napló, 115200 baud
   ```

Az első fordítás a keretrendszer újrafordítása miatt több percig tart, és a két környezet közötti váltáskor is
megismétlődik. A platform rögzítve: pioarduino, Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5.

Később a frissítés OTA-n is megy: a `.pio/build/<környezet>/firmware.bin` feltölthető a kezelőoldalon.

> A `partitions.csv` (2 x 3 MB OTA app + FFat + coredump) módosítása után az OTA és a tárolt előzmények elvesznek.

## Üzembe helyezés

1. **WiFi:** az első induláskor a kijelző saját hozzáférési pontot nyit; csatlakozz rá telefonnal, és válaszd ki a
   hálózatot. A kijelzőn megjelenik az IP-címe.
2. **Hely és időzóna:** a kijelzőn (fogaskerék) vagy a kezelőoldalon. Hely nélkül nincs napkelte/napnyugta és
   előrejelzés (IP alapján is kikereshető).
3. **A konzolok beállítása** (Ecowitt / WS View Plus alkalmazás, *Weather Services → Customized*):
   - protokoll: **Ecowitt**
   - szerver: a kijelző IP-címe (ajánlott fix IP-t adni neki a routerben)
   - útvonal: **`/ecowitt`**, port: **80**
   - küldési időköz: pl. 16-60 mp
4. **Helyiségek** a kezelőoldalon: helyiségenként név, a konzol azonosító szövege (a `model` vagy `stationtype` mezőben
   keresve, pl. `WS3910`) és az alacsony elemfeszültség határa. A beérkező küldők és azonosítóik a `/raw` lapon látszanak.
5. Igény szerint: kezelőoldal-jelszó (felhasználónév: `admin`), éjszakai mód, ntfy-téma, továbbküldés.

## Webes végpontok

| Útvonal | Leírás |
|---|---|
| `/`, `/update` | kezelőoldal, beállítások, OTA-frissítés |
| `/raw` | a konzolok utolsó küldésének összes mezője |
| `/ecowitt` | adatfogadás a konzoloktól (`?fwd=1`: továbbküldött adat, nem küldi tovább) |
| `/settings`, `/wifiscan`, `/wifiset`, `/wifistatus`, `/wifiportal`, `/wififorget`, `/ntfytest`, `/clearcrash` | a kezelőoldal műveletei |

A kezelőoldal jelszava a beállításoknál adható meg; jelszó nélkül a helyi hálózaton bárki eléri. A kijelzőt ne tedd
ki közvetlenül az internetre.

## A kód felépítése

Egyetlen Arduino-sketch, témánként `.ino` fájlokra bontva; az Arduino IDE és a PlatformIO ezeket ábécésorrendben
egy fordítási egységgé fűzi össze, ezért a számozás a sorrendet határozza meg.

| Fájl | Tartalom |
|---|---|
| `idojaras.ino` | include-ok, pin-ek, adattípusok, közös változók, hangolható konstansok (küszöbök, színek), beállítások |
| `idojaras_02`…`_17e_*.ino` | kijelző, idő és fényerő, előzmények, előrejelzés, főoldal, konzolok, továbbküldés, grafikonok, web, diagnosztika, érintés, beállítások, értesítések, bemutató, óra és akku |
| `idojaras_18_setup_loop.ino` | a felület felépítése, indulás, főciklus |
| `wifi_manager.cpp/.h` | WiFi-kapcsolódás és beállító hálózat |
| `kijelzo.h` | a kijelző kiválasztása, felbontás, skálázó makrók (`SX`, `SY`, `S`) |
| `p4/` | a P4-es lap kijelző-, érintés- és óra-kezelése, nagyobb fontok |
| `font_*.c`, `lv_conf.h` | LVGL-fontok (ékezetes Roboto, hétszegmenses DSEG7) és LVGL-beállítás |

A program egyszálú, nem blokkoló főciklussal fut (60 mp-es watchdog); csak az előrejelzés letöltése, az
ntfy-küldés és a továbbküldés megy háttérfeladatban. A beállítások az NVS-ben, az előzmények az FFat-on (`/hist.bin`,
`/days.bin`) tárolódnak. A részletes fejlesztői leírás, buktatókkal: [CLAUDE.md](CLAUDE.md).

## Harmadik féltől származó részek

| Rész | Forrás | Licenc |
|---|---|---|
| `p4/esp_lcd_jd9365.*`, `p4/esp_lcd_touch.*` | Espressif Systems | Apache-2.0 |
| `p4/gsl_point_id.*` | Silead Inc. | GPL-2.0-or-later |
| `p4/esp_lcd_touch_gsl3680.*` (a GSL3680 vezérlő firmware-jével) | a lap gyártójának példaprogramja | nincs megjelölve |
| `font_hu_*`, `p4/font_hu_*` | [Roboto](https://fonts.google.com/specimen/Roboto) | Apache-2.0 / OFL-1.1 |
| `font_seg_*`, `p4/font_seg_*` | [DSEG7 Classic](https://github.com/keshikan/DSEG) | OFL-1.1 |
| LVGL (külön telepítendő) | lvgl.io | MIT |
| `docs/esp-web-tools/` (webes telepítő, saját fordítás esptool-js 0.7.0-val) | [ESP Web Tools](https://github.com/esphome/esp-web-tools) | Apache-2.0 |

## Licenc

[GNU General Public License v3.0](LICENSE) (GPL-3.0-or-later). A program szabadon használható, módosítható és
továbbadható; a továbbadott (módosított) változat forrását is közzé kell tenni, ugyanezen licenc alatt.

A harmadik féltől származó fájlokra a saját licencük vonatkozik (lásd fent). A GSL3680 érintés-meghajtója és a vezérlő
firmware-je (`p4/esp_lcd_touch_gsl3680.*`) a lap gyártójának nyilvánosan elérhető példaprogramjából való, amely nem
jelöl meg licencet; csak azért van benne, hogy az érintés működjön. Ha a jogtulajdonos kifogásolja, kérésre eltávolítjuk.

## Köszönet

Időjárás-előrejelzés: [Open-Meteo](https://open-meteo.com/) (CC BY 4.0). IP alapú helykeresés: ipwho.is.
