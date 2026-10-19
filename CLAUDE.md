# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

# Ecowitt Weather Display – időjárás-kijelző Ecowitt állomásokhoz

Firmware (Arduino keretrendszer, LVGL 9) két kijelzőre, ugyanabból a kódból: a Waveshare ESP32-S3-Touch-LCD-7B
(1024x600, érintős; `pio run`) és a Guition JC8012P4A1C (ESP32-P4, 1280x800; `pio run -e idojaras_p4`, lásd lent).
Az Ecowitt konzolok „customized upload” (Ecowitt protokoll) küldéseit fogadja a `http://<kijelző>/ecowitt` címen,
és főoldalt, grafikonokat, előrejelzést, részletes oldalt és Naplót mutat. Saját webes kezelőoldala is van (OTA, beállítások).

- Tároló: https://github.com/losy58/Ecowitt-Weather-Display, ág: `main` (mindkét kijelzőé). Licenc: GPL-3.0 (`LICENSE`).
  Ez a fő tároló (a felhasználó döntése, 2026.10.16.), **2026.10.16. óta nyilvános**. A régi
  `losy58/ecowitt-weather-console` (v1-v61, `vNN` verziók) 2026.10.16-án törölve a GitHubról; teljes mentése (minden ág és
  címke): Drive `Saját meghajtó/Projektek/ecowitt-weather-console-mentes-2026-10-16.bundle` (visszaállítás: `git clone <fájl>`).
- **Az előzmény egyetlen commit** (a felhasználó kérése, 2026.10.16.): a nyilvánossá tétel előtt a tárolót töröltük és
  ugyanazzal a névvel újra létrehoztuk; egyetlen commit van benne („v1.0: első nyilvános kiadás”), mintha a projekt
  egyszerre született volna. (Ugyanabban a tárolóban force push-sal újraírva a GitHub a régi commitokat azonosító
  alapján egy ideig még elérhetővé tehette volna.) A korábbi előzmény (11 commit) mentése: Drive
  `Saját meghajtó/Projektek/Ecowitt-Weather-Display-elozmeny-2026-10-16.bundle`. A többi gépen újra kell klónozni.
  Előtte ellenőrizve: a kódban és a dokumentációban nincs személyes adat (ntfy-téma, otthoni koordináta, IP, e-mail;
  a képekben EXIF/GPS); az ntfy-témát a felhasználó lecserélte. A Claude-os nyomok (Co-Authored-By sor, `CLAUDE.md`,
  `.claude/`) maradhatnak: nem titok (a felhasználó döntése). Új személyes adat ezután se kerüljön a tárolóba.
- **Kiadás (GitHub Releases)** (2026.10.16.: v1.0; 2026.10.17.: v1.1.0, v1.2.0): a felhasználóknak kész programfájlok, fordítás nélkül (README:
  „Telepítés fordítás nélkül”). Kijelzőnként a `.pio/build/<env>/firmware.factory.bin` (első telepítés USB-n a `0x0`
  címre: betöltő – a 7B-n 0x0, a P4-en 0x2000 –, partíciótábla, program; az NVS-t üresre írja, az FFat-et nem érinti)
  és a `firmware.bin` (OTA). Nevük: `Ecowitt-Weather-Display-vX.Y.Z-7B-factory.bin`, `…-7B-ota.bin`, `…-P4-factory.bin`,
  `…-P4-ota.bin`, mellé `SHA256SUMS.txt`. Menet: mindkét környezet fordítása a commitolt kódból, címke (`vX.Y.Z`) a
  commitra, `gh release create vX.Y.Z --verify-tag --title "Ecowitt Weather Display vX.Y.Z" --notes-file … <fájlok>`
  (a jegyzet magyarul és angolul).
- **Webes telepítő** (2026.10.16.): https://losy58.github.io/Ecowitt-Weather-Display/ (GitHub Pages a `main` ág `docs/`
  mappájából). ESP Web Tools (10.4.0, **saját fordítás esptool-js 0.7.0-val** a `docs/esp-web-tools/`-ban, 2026.10.19.: a kiadott
  10.4.0 esptool-js 0.6.x-e a csipfelismeréskor a `0x40001000`-t olvassa, ettől a P4 v3.2 elnémul, „Failed to
  initialize”; menet: `docs/esp-web-tools/OLVASS-EL.md`): egy „Install / Telepítés” gomb, a lapka alapján választ
  (`docs/manifest.json`: `ESP32-S3` → 7B, `ESP32-P4` → P4, a teljes kép a `0x0` címre). **`new_install_prompt_erase:
  true`** (2026.10.19.): `false`-nál az ESP Web Tools nem kérdez, hanem az Improv nélküli eszközt telepítés előtt
  **teljesen törli** (a FFat-et, vagyis az előzményt is; így veszett el a P4-é); most megkérdezi. A böngésző csak ugyanarról
  az oldalról tölthet, ezért a két `factory.bin` a `docs/firmware/`-ben is benne van (a `.gitignore` kivétele).
  **Új kiadásnál** a `docs/firmware/` fájljait, a `docs/manifest.json` nevét/verzióját és a `docs/index.html` verziószámát (két nyelven) is frissíteni kell. Az oldal
  angolul és magyarul (a böngésző nyelve szerint, váltható).
- A kód megjegyzései, a felület szövegei, a Napló üzenetei és a commit üzenetek **magyarul**, ékezetesen.
- A commit üzenet formája: `vX.Y.Z: <a változás lényege>` (az első nyilvános kiadás a v1.0); a verziószám
  `vX.Y.Z` (2026.10.17., a felhasználó döntése): kis javításnál a Z nő (v1.0.1), új funkciónál az Y (v1.1); a verziószámot a `FW_NAME`-ben
  (`idojaras_11_web.ino`, programnév: „Ecowitt Weather Display vX.Y.Z”) is át kell írni.

## Fordítás, feltöltés

PlatformIO (ha nincs a PATH-on, teljes útvonallal: `~/.platformio/penv/Scripts/pio.exe`, Macen `~/.platformio/penv/bin/pio`):

```
pio run                  # fordítás -> .pio/build/idojaras/firmware.bin
pio run -t upload        # feltöltés USB-n
pio device monitor       # soros napló, 115200
```

- OTA: a `firmware.bin` feltölthető a `http://idojaras.local/` (vagy `http://<a kijelző IP-címe>/`) frissítő oldalán.
- Tesztek nincsenek; az ellenőrzés a fordítás (cél: 0 figyelmeztetés) és a kipróbálás az eszközön.
- Az Arduino IDE is használható ugyanebből a mappából, de **a végleges fordítás PlatformIO-val készüljön**: csak a
  `platformio.ini` `custom_sdkconfig` beállításai (`SPIRAM_XIP_FROM_PSRAM`, `LCD_RGB_ISR_IRAM_SAFE`,
  `MBEDTLS_EXTERNAL_MEM_ALLOC`) akadályozzák meg, hogy flash-írás (mentés, OTA) közben elcsússzon a kép.
- A könyvtárakat a PlatformIO maga tölti le (`lib_deps`: lvgl 9.6.0, a `.pio/libdeps/` mappába; a WiFiManager v1.3.0 óta nem kell).
  Platform: pioarduino, Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5. Az Arduino IDE-hez ugyanezeket a verziókat kell
  a Könyvtárkezelőből telepíteni.
- Windowson a `-I"${PROJECT_DIR}"` idézőjele kell (nélküle eltűnnek a `\` jelek, és az lvgl nem találja a
  `lv_conf.h`-t).
- A `partitions.csv`-t (16 MB, 2 x 3 MB OTA app + FFat + coredump) ne változtasd: elveszne az OTA és a tárolt előzmény.
- A `build_src_filter` csak a gyökér `*.ino`, `wifi_manager.cpp` és `font_*.c` fájljait fordítja; új forrásfájlnál ezt bővíteni kell.

## Felépítés

**Egyetlen sketch, sok `.ino` fájlra bontva.** Az Arduino IDE és a PlatformIO a `.ino` fájlokat ábécésorrendben
egy fordítási egységgé fűzi össze, ezért:
- `idojaras.ino` az első: include-ok, pin-ek, minden adattípus (`struct`, `enum`), a közös globális változók,
  a hangolható konstansok (küszöbök, színek, időzítések) és a `Config`. **Új típust ide kell tenni**, a függvények elé
  (az Arduino automatikus prototípusai miatt). A `setup()`/`loop()` is itt marad (csak `app_setup()`/`app_loop()`-ot hív),
  mert a PlatformIO a `setup()`-ot tartalmazó fájlt teszi előre.
- `idojaras_02..18_*.ino`: témánként a függvények (kijelző-meghajtó, előzmények, előrejelzés, főoldal, konzolok,
  rajzolás, grafikonok, web, diagnosztika, érintés, fényerő panel, `06b`: a konzolok adatainak továbbküldése másik
  kijelzőre, `17b`: Beállítások képernyő a fogaskerékkel,
  `17c`: telefonos értesítések az ntfy.sh-n, háttérfeladatból, `17d`: bemutató mód, `17e`: P4: óra, akku, hang,
  kamerás fényerő, `17f`: „Nincs WiFi” panel).
  A számozás határozza meg a sorrendet (a `17b` a `17_` után jön, mert `_` < `b`);
  egy fájl csak a korábban definiált (vagy előre deklarált) `static` függvényeket használhatja.
- `idojaras_18_setup_loop.ino`: `create_ui()`, `app_setup()` (kijelző → érintés → FFat → beállítások → WiFi → webszerver
  útvonalai → watchdog) és `app_loop()`.
- `wifi_manager.cpp/.h`: az egyetlen külön fordított C++ fájl; WiFi-kapcsolódás és saját beállító hálózat (állapotgép,
  a `loop()`-ból léptetve, lásd „WiFi”); a beállító oldal és a WiFi-végpontok az `idojaras_13`-ban.
- `font_*.c`: LVGL fontok (ékezetes `font_hu_14/28`, hétszegmenses `font_seg_*`; a P4-é nagyobb, a `p4/`-ben); `lv_conf.h`: LVGL-beállítás.

**Futási modell:** egyszálú főciklus. Az `app_loop()` a `my_server.handleClient()`, a `lv_timer_handler()` és a
`*_task()` függvények (nem FreeRTOS-feladatok, hanem nem blokkoló, millis-alapú lépések) sorozata; a loop-ot 60 mp-es
watchdog figyeli, tehát semmi nem blokkolhat. Az LVGL-t csak a főszál hívhatja. Kivétel az Open-Meteo lekérés
(`idojaras_05_elorejelzes.ino`): háttérfeladat (`fc_worker`, PSRAM-veremmel) tölti `fc_in`-be, és a főszál veszi át
`fc`-be (`fc_mux`, `fc_ready`). Szintén háttérfeladat az ntfy-küldés (`nt_worker`) és a továbbküldés (`fw_worker`).

**Adatfolyam:** a konzolok HTTP POST-jait a `FLOAT_FIELDS` tábla alakítja metrikusra a `weather` struktúrába.
Három konzol küld (`rooms[]`, a `model`/`stationtype` mező alapján azonosítva): a kinti (WS90) adat az elsőtől jön,
a többitől csak `OUTDOOR_FAILOVER_S` hallgatás után; a benti adat helyiségenként külön.

**Továbbküldés másik kijelzőre:** a konzolok általában csak egy címre küldenek (most a 7B-re), ezért a
7B továbbküldi az adatokat a P4-nek. **Mindkét kijelző tud továbbküldeni** (közös kód; a felhasználó kérése): ha a konzolok
a P4-re küldenek, a P4-en kell bekapcsolni, a 7B céllal. A beállítás csak a webes kezelőoldalon van, a kijelzőn nincs. A kezelőoldalon „Továbbküldés másik kijelzőre”: be/ki kapcsoló (NVS `cfg`/`fw_on`,
alapból ki; a felhasználó kérése) és cél (`cfg`/`fw_h`, pl. `idojaras-p4.local` vagy egy IP-cím, esetleg `:port`);
kikapcsolva vagy üres céllal nincs továbbküldés, a cél kikapcsolva is megmarad. A `handle_ecowitt_post()` a
`fw_push()`-sal a beérkezett mezőket urlencoded alakban egy 4 elemes sorba teszi (PSRAM-puffer, `FwMsg`, legfeljebb
`FW_BODY` bájt), és az `fw_worker` háttérfeladat (PSRAM-verem) HTTP POST-tal küldi a cél `/ecowitt?fwd=1` címére
(2 mp kapcsolódás, 3 mp válasz). Ha a cél nem érhető el, a sor betelik, az újak elmaradnak; a főciklus nem vár. A
`fwd` mezős kérést a fogadó nem küldi tovább (nincs körbeküldés). A kezelőoldalon az utolsó továbbküldés eredménye és a
sikeres/hibás/elmaradt darabszám látszik. A P4-en nem kell beállítani semmit: a konzolokat a `model`/`stationtype`
alapján ismeri fel (a küldő IP-je ott mindig a 7B-é, ezért a `/raw` lapon a küldők a `stationtype` szerint válnak
szét). Az ntfy-értesítéseket elég az egyik kijelzőn bekapcsolni.

**Bemutató mód** (a felhasználó kérése): bemutatóhoz, konzolok és akár WiFi nélkül. Csak kézzel
kapcsolható, a kijelzőn: Beállítások / Rendszer / „Bemutató mód...” (ill. „Bemutató kikapcsolása...”), megerősítés után
újraindul (NVS `cfg`/`demo`, `g_demo`). Ilyenkor (`idojaras_17d_bemutato.ino`):
- a valódi oldalak kitalált, de életszerű adatokkal (`demo_eval()`: napszak, évszak, lassú hullámok, délutáni esős napok,
  helyiségenként CO2-menet), 16 mp-enként „új adat”; a 7 napos előzmény és a 400 napos napi összesítő is ebből készül,
  így a grafikonok (nap/hét/hónap) és a pillanatnyi értékek egyeznek; előrejelzés is (4 nap, 24 óra);
- magától lapoz (`DEMO_STEPS`: főoldal, előrejelzés, grafikonok napi/heti/havi nézetben, részletes adatok); érintés
  után a lépés ideje újraindul, nyitott Beállítások / fényerő panel / nagy grafikon alatt áll;
- **a valódi előzményfájlokat nem írja** (`hist_save`, `days_save` kilép), nincs Open-Meteo-lekérés, ntfy-értesítés,
  továbbküldés, és a konzolok küldéseit sem használja (az `/ecowitt` csak OK-t válaszol);
- induláskor nem vár WiFi-re és nincs beállító portál: ha van mentett hálózat, a háttérben csatlakozik
  (`wifiBeginNoWait()`; kezelőoldal, OTA, pontos idő). Pontos idő nélkül a fordítás idejéről indul az óra; ha később
  megjön az NTP-idő (az idő ugrik), az adatok újraszámolódnak;
- hely nélkül Miskolc (`DEMO_LAT/LON`, nem mentődik: `demo_loc`), alapértelmezett időzóna helyett CET;
- nincs éjszakai mód (mindig nappali fényerő); a fejlécben „BEMUTATÓ (kitalált adatok)”, a kezelőoldalon figyelmeztetés.

**Grafikonok** (`idojaras_10`): a Hőmérséklet grafikonon a látható időszak (nap / 7 nap / hónap) legmagasabb és
legalacsonyabb kinti pontján fehér jel és „max …” / „min …” felirat (`ext_place()`, a kártyán és teljes képernyőn;
havi nézetben a napi maximumok legmagasabbja és a napi minimumok legalacsonyabbja; v1.2.0, a felhasználó kérése).
A felirat a pont fölött / alatt, ha ott nem fér el a grafikonon belül, mellette.

**Tárolás:**
- NVS (`Preferences`, `"cfg"` névtér): hely, időzóna, kezelőoldal jelszava, éjszakai mód.
- FFat: `/hist.bin` (7 nap 5 perces minta, `HistSample`, PSRAM-ban tartva) és `/days.bin` (400 napi összesítő, havi
  grafikonhoz). A fájlok fejlécében `magic` verziószám van; ha a formátum változik, új `magic` kell, és a régit
  betöltéskor át kell alakítani (lásd `HIST_MAGIC_V1/V2` kezelése) – különben a felhasználó elveszti az előzményeit.
- Diagnosztika: az utolsó indulások oka és a coredump partícióba mentett összeomlás (`idojaras_12_diagnosztika.ino`).

**Webes végpontok** (`app_setup()`): `/` és `/update` (kezelőoldal + OTA), `/settings`, `/raw` (konzolok nyers adatai),
`/ecowitt` (adatfogadás; `?fwd=1`: továbbküldött adat), `/clearcrash`, `/coredump` (a teljes mentett összeomlás letöltése), `/ntfytest`, `/beeptest` (P4), `/wifiscan`, `/wifiset`, `/wifistatus`, `/wifiportal`, `/wififorget`; minden ismeretlen cím a beállító hálózaton a
beállító oldalra irányít (`onNotFound`). mDNS név: `idojaras`.

**Memória:** a belső RAM szűkös (`INTERNAL_RAM_RESERVE` marad a WiFi-nek); a nagy pufferek, az LVGL és az mbedTLS
a PSRAM-ot használja. Nagy új foglalást PSRAM-ba tegyél (`MALLOC_CAP_SPIRAM`).

## Munkamenet

- **Nyelv** (2026.10.16., a felhasználó kérése): egy beállítás (`g_lang`, NVS `cfg`/`lang`) a kijelzőnek, a webes
  kezelőoldalnak, a telefonos értesítéseknek és a WiFi-kezelő kijelzőre írt üzeneteinek (`wifiSetEnglish()`); minden
  felhasználónak szóló szöveg `tr("magyar", "angol")`. **Alapból angol;** ha nincs mentett nyelv, de van már beállítás
  (`tz`, `lat` vagy `r0n`), magyar marad (a régi, magyar alapértékű programmal használt kijelzők; `lang_load()`). A
  `lang_load()` a `diag_init()` előtt fut (a diagnosztika szövegei már a nyelvtől függnek). A Napló és a soros napló
  magyar marad. Új szövegnél mindkét nyelv kell.

- **README-képek** (2026.10.16.): `docs/kepek/*.jpg`, mindkét README „Képernyőképek” / „Screenshots” részében.
  A felhasználó telefonos fotói a Google Drive `Saját meghajtó/Projektek` mappájából (HEIC), bemutató módban, angol
  felülettel; `sips -s format jpeg -s formatOptions 82 -Z 1200`. **A telefonos fotókban GPS-helyadat és EXIF van
  (az otthon címe!): feltöltés előtt ki kell venni** (a JPEG APP1/EXIF/XMP részeit; ellenőrzés: `mdls -name kMDItemLatitude`).

- Két gépen folyik a fejlesztés (Mac és Windows): munka előtt `git pull --ff-only`, a végén `git push origin main --tags`.
  Az eszközre egyszerre csak az egyik gépről, mindig a legfrissebb kódból tölts fel.
- Menet: módosítás → `pio run` és `pio run -e idojaras_p4` (mindkettő 0 figyelmeztetés) → OTA-feltöltés mindkét kijelzőre → ellenőrzés a kezelőoldalon („Futó program:” (angolul „Running firmware:”) sor)
  → a felhasználó kipróbálja → ha jóváhagyta: commit `vX.Y.Z: ...`, `git tag vX.Y.Z`, push.
- OTA parancssorból (ha nincs jelszó beállítva a kezelőoldalon):
  `curl -F "firmware=@idojaras-vX.Y.Z.bin" "http://idojaras.local/update?size=<bájt>"` (a P4-re ugyanígy az
  `idojaras-p4-vX.Y.Z.bin`, az `idojaras-p4.local`-ra); utána a „Futó program:” (angolul „Running firmware:”) sort ellenőrizd.
  Ha a kezelőoldalon jelszó van, a nevében ne jelentkezz be: a `.bin`-t készítsd elő, a felhasználó tölti fel böngészőből.
- A projektmappában csak az aktuális `idojaras-vX.Y.Z.bin` és `idojaras-vX.Y.Z.elf` maradjon (mindkettő gitignore-os);
  az `.elf` kell az összeomlás visszafejtéséhez.

## Tapasztalatok, buktatók

**Kijelző / memória**
- A belső RAM-ból kb. 80 KB szabad, az LVGL kb. 40 %-on áll (a kezelőoldalon látszik). Ha egy új oldal sok LVGL-objektumot
  hoz létre, induláskor elfogyhat az LVGL memóriája → összeomlás az első kirajzolásnál (`lv_memset`/`lv_draw_sw_fill`).
  Ritkán látszó elemeket (pl. az előrejelzés ikonjai) első használatkor hozz létre, ne előre.
- Egy kártyasorba (`font_hu_28`, 326 px-es kártya) kb. 306 px fér; hosszabb szövegnél mérj (a font `adv_w` értékeiből).
  Egysoros feliratnál szélesség + `LV_LABEL_LONG_DOT` **és rögzített magasság** (kb. 34 px) kell; magasság nélkül
  a DOT mód nem vág, hanem új sorba tör, és rácsúszik a lenti elemre.
- `snprintf` HTML-darabokhoz bőven méretezett puffer kell: egy levágott `<input>` mező (pl. hiányzó `d_pct`)
  mentéskor 0-t küld, és hibát okoz. A mentés hiányzó mezőnél tartsa meg a régi értéket.

**WiFi**
- A házban több hozzáférési pont van ugyanazzal a hálózatnévvel (mesh). Az ESP32 alapból az **elsőként talált** AP-ra
  csatlakozik, ami gyakran egy távoli, gyenge (−76 dBm) volt: induláskor sikertelen csatlakozás (ok: 0), beállító portál,
  akadozó OTA. A megoldás: `WIFI_ALL_CHANNEL_SCAN` + `WIFI_CONNECT_AP_BY_SIGNAL`, **és** a `WiFi.begin(ssid, pass)`
  kifejezett hívása a mentett adatokkal (`beginSaved()`), mert a paraméter nélküli `WiFi.begin()` figyelmen kívül hagyja
  a rendezést. (Korábban tévesen a túl erős jelnek tulajdonítottuk; a 19,5 dBm a jó AP-n gond nélkül működik.)
- Tartósan gyenge jelnél (−80 dBm alatt 2 percig) a `wifiTune()` újracsatlakozik a legerősebb AP-ra (10 percenként),
  −75 dBm alatt feljebb kapcsolja az adóteljesítményt (NVS `cfg`/`txp`). A −65…−75 dBm még jó kapcsolat (ott ne bontsd).
  A WiFi energiatakarékos módja ki van kapcsolva.
- A webes kezelőoldalon is átállítható a hálózat (`/wifiscan`, `/wifiset`, `/wifistatus`): sikertelen csatlakozásnál
  a `wifiSwitchTo()` visszaállítja a régi hálózat mentett adatait.
- **Saját beállító hálózat** (v1.3.0, 2026.10.19., a felhasználó kérése egy bemutató után, ahol új helyen a kijelző
  használhatatlan volt: a portál alatt nem reagált, nem írta ki az IP-t, nem listázta a hálózatokat, vegyes nyelvű volt).
  Az indulás nem vár a WiFi-re: a `wifiStart()` / `wifiLoop()` állapotgép 3 próbát tesz (15 mp, három adóteljesítmény),
  közben a kijelző működik. Ha nem sikerül (vagy nincs mentett hálózat), elindul a jelszó nélküli `Weather-Setup`
  (P4: `Weather-Setup-P4`) hálózat, 192.168.4.1, DNS-sel minden név ide mutat (a telefon magától megnyitja a beállító
  oldalt), és a kijelzőn a „Nincs WiFi” panel (`idojaras_17f`): két QR-kód (csatlakozás, oldal), lépések, gombok:
  Bemutató mód (második koppintásra, újraindul), Újrapróbálás, Bezárás. Bezárva a felső sor „Nincs WiFi - beállítás”
  feliratára koppintva újranyílik. A mentett hálózatot 2 percenként újrapróbálja, de csak ha senki sincs a beállító
  hálózaton, és hálózatkeresés után 3 percig nem: **csatlakozási próba közben az ESP32 nem keres hálózatot** (ezért nem
  jött lista a régi WiFiManager-portálon), és ilyenkor az automatikus újracsatlakozás is ki van kapcsolva. Kapcsolat után
  a beállító hálózat még 1 percig nyitva marad. Nincs újraindulás-kör.
- A beállító oldal (`handle_portal_page()`) a beállító hálózatról jövő kérésre (`portal_client()`: a kapcsolat helyi
  címe a `softAPIP`) jelszó nélkül látszik, nyelve választható (`?l=hu` / `?l=en`, csak az oldalé); a hálózatlista
  magától betöltődik, a hely is megadható. Ugyanaz az űrlap (`wifi_form_html()`), mint a kezelőoldalon.
- Kérésre (Beállítások / WiFi „WiFi beállítása...”, kezelőoldal) a beállító hálózat 5 percre nyílik, újraindulás nélkül,
  a kapcsolat megmarad. „WiFi elfelejtése” (kijelző, kezelőoldal `/wififorget`): `WiFi.disconnect(false, true)`, utána
  beállító hálózat.
- `WiFi.SSID()` az arduino-esp32 3.x-ben csatlakozás előtt üres → a mentett hálózatot `esp_wifi_get_config()`-gal kell
  olvasni (különben a program azt hiszi, nincs mentett WiFi, és azonnal a portált indítja, próbák nélkül).
- **A P4-en a mentett WiFi-hálózatot a WiFi-csip (ESP32-C6) tárolja**, a saját flashében (ESP-Hosted, `esp_wifi_remote`):
  a P4 teljes újratelepítése (factory.bin, üres NVS) nem törli, a kijelző utána is csatlakozik. Az első indulás
  kipróbálásához előbb a „WiFi elfelejtése” kell. A 7B-n a teljes telepítés a WiFi-t is törli.
  **Buktató:** a C6 a futás közben kapott beállítást (új hálózat, törlés) csak a memóriájában változtatta, a flashébe
  nem írta: a „WiFi elfelejtése” után tápciklusra a régi hálózat visszajött. Ezért a `wifiStart()`, a `wifiSwitchTo()` és
  a `wifiForget()` kifejezetten `esp_wifi_set_storage(WIFI_STORAGE_FLASH)`-t hív (`storageFlash()`); a felejtés után
  visszaolvas, és ha a hálózat még megvan, `esp_wifi_restore()`. Így kipróbálva (2026.10.19.): felejtés + tápciklus után
  beállító hálózat, telefonos beállítás + tápciklus után visszacsatlakozik.

**PlatformIO / .ino összefűzés**
- A PlatformIO azt a fájlt teszi előre, amelyikben `void setup(` vagy `void loop(` van – ezért marad mindkettő az
  `idojaras.ino`-ban.
- A prototípusokat az első függvény elé szúrja, **kivéve** ha egy függvénynév előtte zárójel elé kerül (pl. egy
  megjegyzésben „setup/loop)”) – ilyenkor az `#include`-ok elé teszi őket, és minden típus „nem deklarált” lesz.
- Új típus (`struct`/`enum`), amely függvény paraméterében szerepel, csak az `idojaras.ino`-ba kerülhet (a prototípusok elé).
- Mutatót visszaadó függvényhez (pl. `static const char *f()`) a PlatformIO nem mindig készít prototípust: ha korábbi
  fájlból hívod, az `idojaras.ino`-ban előre deklarálni kell.
- A Mac Minin a tárolók 2026.10.16. óta a `~/GitHub`-ban vannak, az iCloudon kívül (a régi `~/Documents/GitHub`
  szimbolikus link rá). Ha egy gépen a tároló iCloud alatt van, félbeszakadt fordítás vagy ágváltás után ütközési
  másolatok maradhatnak (`idojaras.ino 2.cpp`, `idojaras_17d_bemutato 2.ino`, `p4/kijelzo_p4 2.c`), és a fordítás
  „redefinition” / „multiple definition” hibával áll le. Ha a másolat azonos az eredetivel (`cmp`), törölhető.

**Eszköz, USB**
- Az eszköz natív USB-portja (Mac: `/dev/cu.usbmodem…`, Windows: `COMx`) megnyitása **újraindítja** a kijelzőt
  (USB-Serial-JTAG), és a legutóbbi mentés óta gyűlt előzmény (legfeljebb 15 perc) elveszik. Soros naplót csak akkor
  olvass, ha tényleg kell. Az USB-s feltöltéshez az Arduino IDE soros monitorát be kell zárni (foglalja a portot).
- A Mac USB-portjáról táplálva a kijelző induláskor elakadhat (kevés áram); üzemszerűen a saját tápjáról menjen.
- Összeomlás visszafejtése: a kezelőoldal Diagnosztika részének címei az adott verzió `.elf`-jével:
  `xtensa-esp-elf-addr2line -pfiaC -e idojaras-vX.Y.Z.elf <címek>` (a PlatformIO `toolchain-xtensa-esp-elf/bin` mappájából).

**Külső szolgáltatások**
- Open-Meteo: néha átmeneti HTTP 503 → hiba után 1 perc múlva újrapróbál (3-szor), utána 5 percenként. A lekérés
  csak a pontos idő (NTP) után indul (a napok kiválasztásához kell).
- Az ip-api.com erről a hálózatról nem érhető el; az IP alapú helykeresés az ipwho.is-t használja (tartalék: ip-api.com).

**Fontok**
- `font_hu_28` (Roboto Black) és `font_hu_14` (Roboto Medium): `npx lv_font_conv@1.5.3 --bpp 4 --size <méret>
  --no-compress --font <ttf> --range 32-127 --symbols "ÁÉÍÓÖŐÚÜŰáéíóöőúüű°²" --format lvgl --lv-font-name <név> -o <név>.c`
  (Node.js kell). A generált fájl elején az include-blokkot a meglévő fontokéhoz kell igazítani (`__has_include("lvgl.h")`).

## ESP32-P4 változat (Guition JC8012P4A1C)

A 10,1"-os Guition **JC8012P4A1C_I_W_Y** lap (ESP32-P4 v3.2, 16 MB flash, 800x1280 MIPI-DSI kijelző JD9365-tel,
GSL3680 érintés, WiFi az ESP32-C6-on át, ESP-Hosted/SDIO). A 7B-s (S3) változat ugyanabból a kódból, változatlanul fordul (`pio run`). Mindkét kijelzőn
ugyanaz a verzió fusson.

**Állapot (2026.10.15.):** a kijelző, a háttérvilágítás, az érintés, a WiFi, a webes kezelőoldal és az OTA működik;
a felület a teljes 1280x800-at kihasználja.

- Mindkét környezetnek van `custom_sdkconfig`-ja, ezért a pioarduino a környezetek közti váltáskor újratelepíti a
  keretrendszert, és az ESP-IDF könyvtárait újrafordítja („Reinstall Arduino framework”, „Compile Arduino IDF libs”,
  kb. 3-4 perc), és törli a másik `.pio/build/<env>` mappáját: a `.bin`-t/`.elf`-et közvetlenül a saját fordítása után
  kell kimásolni (P4: `idojaras-p4-vX.Y.Z.bin`). A hibrid fordítás fő komponense a `.dummy/` mappa (a tárolóban van, lásd
  „Kék villanás”).
- Fordítás: `pio run -e idojaras_p4`, feltöltés: `pio run -e idojaras_p4 -t upload --upload-port /dev/cu.usbmodem…`
  (a lap natív USB-portja, a kapcsolási rajzon USB2/„Full Speed USB”), vagy OTA: `http://idojaras-p4.local/`
  Az mDNS-név `idojaras-p4`, hogy ne ütközzön a 7B-vel; a programnév végén „(P4)”.
- Kód: `kijelzo.h` (melyik kijelző: `KIJELZO_P4`, `FW_BOARD`), `p4/kijelzo_p4.c/.h` (kijelző, érintés, háttérvilágítás,
  óra, hang), `p4/kamera_p4.c` (kamera: fény és jelenlét),
  a gyártói meghajtók a `p4/`-ben (`esp_lcd_jd9365`, `esp_lcd_touch`, `esp_lcd_touch_gsl3680`, `gsl_point_id`).
  A `p4/` mappát az Arduino IDE nem fordítja, csak a PlatformIO (`build_src_filter`). Az `.ino`-kban `#if KIJELZO_P4`.
- **A felület a P4-en 1280x800-as** (2026.10.15.; előtte 1024x600 a kép közepén, fekete kerettel), 90 fokkal
  elforgatva. Irány: `ROT_FLIP` (`p4/kijelzo_p4.c`), most 1; a 0-val fejjel lefelé állt.
- **Skálázás:** a kódban minden hely és méret a 7B 1024x600-as képernyőjére van írva, és a `kijelzo.h` makrói
  nagyítják a P4-en: `SX()` vízszintes (x, szélesség, x1,25), `SY()` függőleges (y, magasság, x1,333), `S()` egyenletes
  (x1,25: körök, ikonok, iránytű, vonalvastagság, és a betűhöz igazodó eltolások, pl. a „°C” a számjegy mellett).
  A 7B-n mindhárom változatlanul adja vissza az értéket, ott a kód számszerűen ugyanaz. **Új elemnél minden számot
  ilyen makróba kell tenni** (ami a betűmérethez igazodik: `S()`; a koppintás-ellenőrzésben ugyanaz a kifejezés, mint
  a rajzolásnál). A `LCD_H_RES`/`LCD_V_RES` is a `kijelzo.h`-ban van.
- **Fontok a P4-en:** a `p4/font_*.c` ugyanazokkal a nevekkel, 1,25-szörös méretben (hu: 18 és 35 px, seg: 50, 90,
  120 px); a P4 `build_src_filter`-e ezeket fordítja a gyökérbeliek helyett, a kódban nincs különbség. A beépített jelek
  (fogaskerék, X, nyilak, törlés) fontja `FONT_SYM` (7B: Montserrat 28, P4: 34). Generálás: lásd „Fontok”
  (`--size 18/35` Roboto, `--size 50/90/120 --symbols "0123456789:.-°²"` dseg7-classic-latin-700-normal.ttf,
  `--lv-font-name font_seg_40` stb.); a TTF-ek: Roboto (Google Fonts) és DSEG7 Classic (github.com/keshikan/DSEG). Az lv_font_conv 1.5.3 a `--stride 1 --align 1` kapcsolót nem ismeri, nélküle a kép ugyanaz.
- Az LVGL rajzpuffere a P4-en a PSRAM-ban van, `P4_LVGL_LINES` (200) soros: egy kép 4 csíkban rajzolódik ki
  (1024x600-on, 150 sorral 90-140 ms volt). 8 soros (a 7B-n használt) pufferrel a grafikonos oldalak 75 csíkban,
  400-500 ms alatt (lassú lapozás). A forgatás
  puffere is a PSRAM-ba kerül (32 KB fölött), mert a belső RAM a WiFi-nek és a TLS-nek kell (a P4-en a mbedTLS belsőből foglal).
- A panel a képpufferbe DMA2D-vel, a háttérben másol: a következő rajzolás előtt meg kell várni az előző végét
  (`on_color_trans_done` + szemafor), különben „previous draw operation is not finished”, és részek elvesznek.
- Háttérvilágítás: GPIO23 PWM (20 kHz, 10 bit, magas = világít). **30 %-ról indul** (`P4_BL_START`), a
  `backlight_task()` húzza fel: azonnali 100 % + WiFi mellett a Mac USB-portjáról táplálva **brownout** (újraindulás).
  Éles használatra saját töltő kell (a gyártó ≥600 mA-t ír; 2 A-es töltőről nem volt brownout).
- A soros napló a natív USB-porton (`ARDUINO_USB_CDC_ON_BOOT=1`); induláskor legfeljebb 2 mp-et vár a gépre, hogy az
  eleje is látsszon.
- A RISC-V-n az összeomlás-összefoglaló más (nincs hívási lánc): `mcause`, `mtval`, `ra` (`idojaras_12`).
  Visszafejtés: `riscv32-esp-elf-addr2line` (`toolchain-riscv32-esp`).
- **Óra és akku a lapon** (`idojaras_17e_ora_akku.ino`; a kapcsolási rajz `1_PWR.png`, `4_CONN.png`):
  - **Óra:** RX8025T az érintés I2C-buszán (0x32), saját CR1220 gombelemmel; UTC-t tárolunk benne
    (`p4_rtc_read/write`, `p4/kijelzo_p4.c`). Induláskor ebből jön az idő (`p4_hw_setup()`), így WiFi és NTP nélkül
    is pontos az óra, és az előzmény azonnal betöltődik; az NTP-szinkron után (`sntp_set_time_sync_notification_cb`,
    óránként) visszaírjuk. Az óra VLF-jelzője (lemerült gombelem / még nem volt beállítva) esetén nem használjuk.
    **Gombelem nélkül** (vagy lemerült elemmel) az óra a táp kimaradásakor elfelejti az időt: induláskor nincs idő,
    mint a 7B-n (NTP-re vár, addig nincs előzmény-betöltés és előrejelzés); az NTP után beállítjuk, és tápon jár tovább.
  - **Akku:** Li-akku, az IP5306 tölti; a feszültsége a GPIO52-n (68k/100k osztó, `analogReadMilliVolts` x 1,68),
    2 mp-enként. A kezelőoldalon „Akku:” sor (feszültség, becsült %, hálózati táp / akkuról megy, 10 perces min-max);
    akkuról menet a fejlécben „Akkuról megy: NN %”. Értesítés (a „Lemerülő / hiányzó elem” jelölővel együtt):
    „Áramszünet”, „Újra van táp” (mennyi ideig ment akkuról), „Lemerül az akku” (3,5 V alatt). Az ntfy-téma a P4-en
    alapból üres: ha kell, ott is be kell állítani.
  - **Akku nélküli lap** (v1.0.1, a felhasználó kérése): ha a mért feszültség 2,5 V alatti, vagy két minta között
    0,25 V-nál többet ugrik (az IP5306 akku nélkül ki-be kapcsolhatja a töltést), „nincs akku” (`batt_none`): a
    kezelőoldalon „nincs akku (mért: … V)”, a Rendszer fülön „Akku: nincs”, és nincs áramszünet- / lemerülés-értesítés.
    10 perc ilyen mérés nélkül újra figyel (utólag betett akku). Akku nélkül még nem volt kipróbálva (mit mér a GPIO52).
  - **Mérések (2026.10.15.):** hálózati tápon, teli akkuval 4,12 V (10 percig állandó), töltés közben 4,18-4,19 V;
    akkuról 30 mp alatt 4,05 V, 1 perc alatt 3,97 V, utána lassabban esik; visszadugva 20 mp alatt 4,12 V.
    Felismerés: akkuról megy, ha 2 percen belül 0,07 V-ot esik, vagy 4,10 V alatt 0,03 V-ot (`BATT_*`); visszajött
    a táp, ha 0,08 V-ot emelkedik.
  - **A táp kihúzásakor a lap újraindul** (az IP5306 átkapcsolásakor egy pillanatra nincs feszültség; a Diagnosztikában
    „bekapcsolás / áramszünet”), a kép pár másodpercre fekete; utána akkuról megy tovább. Az utolsó mentés óta gyűlt
    előzmény (legfeljebb 15 perc) elveszik. A táp visszadugásakor nincs újraindulás.
  - A P4 újraindulása után a 7B továbbküldése egyszer kb. 5 percig sikertelen volt („connection refused”), a Macről
    közben elérhető volt (valószínűleg a 7B elavult ARP-bejegyzése); magától helyreállt.
- Az első feltöltés előtt érdemes a gyári programot teljesen kimenteni (`esptool -p <port> read-flash 0 ALL <fájl>`,
  16 MB; a tárolóban nincs). Visszaírás: `esptool -p <port> write-flash 0 <fájl>`. A gyártó csomagja
  (`JC8012P4A1C_I_W_Y`): kapcsolási rajz, demók, ESP-IDF 6.0.2-es forrás.

**Érintés (GSL3680, I2C 0x40, RST GPIO22, INT GPIO21, a buszon az ES8311 kodek és az RX8025T óra is):**
- Meghajtó: a gyártó `esp_brookesia_phone` példájának saját másolata (ez fut a gyári programban), javítva: a 44 bájtos
  olvasás 24 bájtos tömbbe ment (veremsérülés, újraindulás); a régebbi `esp_lcd_touch`-hoz igazítva. Az I2C-busz mint a
  gyári BSP-ben: I2C 1, belső felhúzás nélkül, 400 kHz. Az Arduinós gyártói meghajtót ne használd: az INT lábat a
  firmware betöltése alatt alacsony kimenetként tartja, miközben azt a vezérlő hajtja.
- A vezérlő firmware-jét (4356 sor) a program minden induláskor betölti. Az indítás utáni `0xb0` ellenőrzés ezen a lapon
  akkor is 0-t ad (nem `5a5a5a5a`), ha az érintés működik: a meghajtó csak figyelmeztet, nem hiba.
- **2026.10.14-én az érintés egyik programmal sem működött** (a mienkkel, a gyártó ESP-IDF 6.1-gyel fordított
  `lvgl_demo_v9` példájával és a gyári programmal sem): a vezérlő válaszolt, de a `0xbc` állapot `1a5a0002` volt, és a
  `0x80`-tól olvasott adat mindig 0. **Teljes áramtalanítás és az érintőpanel szalagkábelének ellenőrzése után
  (2026.10.15.) a gyári programmal és a mienkkel is működik** (`0xbc` = 0). Ha az érintés megint nem megy, előbb ezt kell kipróbálni, nem a kódot.
- Koordináták: a GSL3680 a panel 800x1280-as rendszerében ad (a meghajtóban x és y tükrözve); a `p4_touch_read()`
  ebből számolja a felület pontját (`ROT_FLIP` = 1), a fekete keretre eső érintést eldobja. A négy sarokkal ellenőrizve.

- **Kék villanás flash-írás alatt – javítva:** az előzménymentés („Előzmény mentve (flash)”) vagy
  az OTA alatt a kép egy pillanatra világoskék volt, mert a képkockákat adagoló DSI-megszakítás flash-írás alatt nem
  futott. Javítás: `custom_sdkconfig` `CONFIG_LCD_DSI_ISR_CACHE_SAFE=y` a P4-nek (OTA közben ellenőrizve: nincs villanás).
  **Buktató:** a `custom_sdkconfig` miatt a keretrendszer újrafordul, és a komponenskezelő a legújabb `esp_hosted`
  (2.13.0) és `esp_wifi_remote` (1.6.5) verziót töltené le; ezek nem illenek a WiFi-chip (ESP32-C6) gyári programjához:
  a WiFi nem indul (`esp_wifi_init 0x102: ESP_ERR_INVALID_ARG`), a P4 csak USB-n állítható vissza. Ezért a kettő a
  gyári Arduino-keretrendszerével azonos verzióra (2.12.13, 1.6.4) van rögzítve a `.dummy/idf_component.yml`-ben
  (csak `target == esp32p4`). A `custom_component_remove/add` erre nem jó: a kivett komponens fejléceit törli a
  keretrendszerből. A komponenslista változása magában nem indít újrafordítást, csak a `custom_sdkconfig` szövegének
  változása. Ha az Arduino-keretrendszer frissül, a gyári verziót a `framework-arduinoespressif32-libs/esp32p4/versions.txt`
  mutatja; a rögzítést ahhoz kell igazítani.

- **Akku a kijelzőn** (2026.10.16.): a Beállítások / Rendszer fülön is (`batt_short()`); ehhez a „Szabad RAM” és az
  „LVGL” egy sorba került, mert a Bemutató gomb fölött legfeljebb 7 sor fér el.
- **Hangjelzés „Szellőztess”-kor** (2026.10.16., a felhasználó kérése): amikor egy helyiségnél bekapcsol a `co2_vent[]`,
  egyszer szól egy „ding-ding-dong” (`vent_beep_task()`, `idojaras_17e`). Az éjszakai mód időszakában nem
  (`in_night_window()`: óra vagy napnyugta szerint, akkor is, ha a sötétítés ki van kapcsolva; a felhasználó döntése:
  nincs külön időzítés). Be/ki: webes Beállítások és a kijelző Rendszer fülén (azonnal ment); hangerő: csak a weben,
  halk / közepes / hangos = 0,02 / 0,08 / 0,2 jelerősség (`SND_AMP`, a felhasználó választása; a 0,35 túl hangos volt);
  NVS `cfg`/`snd`, `sndv`. A hangerő-választó választáskor megszólal (`POST /beeptest?v=0..2`, mentés nélkül), mellette
  „Meghallgatás” gomb. A 7B-n nincs hangszóró: ott nincs se kapcsoló, se hang.
  Hardver (`p4/kijelzo_p4.c`, `p4_audio_init()`, `p4_beep()`): ES8311 kodek az érintés I2C-buszán (0x18), I2S-szolga,
  16 kHz, 16 bit, MCLK = 256 x fs (MCLK GPIO13, BCLK GPIO12, LRCK GPIO10, adat GPIO9); a regiszterek kézzel (a
  keretrendszerben nincs esp_codec_dev); NS4150 erősítő PA_CTRL = GPIO20, csak lejátszás alatt bekapcsolva; a hangot
  háttérfeladat játssza le (a főciklus nem vár). Hangszóró a CN3-on (`7_CODEC.png`, `3_ESP32-P4.png`).

- **Kamera: automatikus fényerő és jelenlét** (v1.1.0, 2026.10.17., a felhasználó kérése). A lapon előre néző kamera
  van a fekvő kijelző tetején: **OmniVision OV02C10** (2 MP, MIPI-CSI; FPC5, `4_CONN.png`), az érintés I2C-buszán (0x36),
  reset/kikapcsoló láb nélkül, saját órajellel.
  - Meghajtó: `p4/kamera_p4.c`, saját, rövid. A keretrendszer `esp_cam_sensor`-ában (2.4.0) nincs OV02C10, csak a gyártó
    saját másolatában; abból csak a regiszterlista kell (`p4/ov02c10_1080p_1lane.h`: 1920x1080, RAW10, 1 adatsáv,
    405 Mbit/s, mint a gyári `video_lcd_display` demóban). Az esp_video (V4L2) helyett közvetlenül a CSI-vezérlő és az
    ISP (RAW10 → RAW8, színkeverés nélkül), a kép a PSRAM-ba. A képmagasság (VTS) 8730 sor: kb. 4 kép/mp, legfeljebb
    245 ms expozíció. Az ISP órajele 240 MHz kell (80 MHz-en nem jött kép: a pixelórajel 81 MHz); előbb a CSI-vezérlő,
    utána az ISP jön létre (mint az esp_video-ban). A kamera az `app_setup()` végén indul,
    bemutató módban nem.
  - Mérés: háttérfeladat (`cam_worker`, PSRAM-verem), fél másodpercenként egy képből 64x36-os rács. **Fényérték** =
    világosság / (expozíció * erősítés), logaritmikus mozgóátlaggal simítva (kb. 3 mp). **Mozgás** = az előző rácshoz
    képest megváltozott cellák aránya; 1 % fölött mozgás (4 méterről is észrevesz; a felhasználónak így jó). Saját
    expozíció-szabályozás (előbb az idő, utána az analóg erősítés 0x3508, 1x = 0x100), csillapítva (egy lépésben a
    hiányzó arány gyöke). A kép nem kerül ki a lapról, nem mentődik. A kezelőoldalon „Kamera:” sor (fényérték, mozgás,
    automatikus fényerő); indítási hibánál az ESP-IDF első hibaüzenete is látszik.
  - Fényerő (`cam_brightness()`, `idojaras_17e`; NVS `cfg`/`cam`, alapból be, csak a webes Beállításokban): a simított
    fényérték szerint az éjszakai (`CAM_L_DARK` = 0,0009 alatt) és a nappali fényerő (`CAM_L_BRIGHT` = 0,3 fölött)
    között, logaritmikusan. `CAM_IDLE_S` (10 perc) mozgás és érintés nélkül: az éjszakai fényerőre sötétül
    („senki sincs előtte”), mozgásra vagy érintésre felébred; alvó kijelzőn az első érintés nem lapoz. Bekapcsolva ez
    váltja ki az éjszakai mód időzítését (a hangjelzés csendes időszaka marad). Mérve: délelőtt, borús időben a szobában
    0,04-0,35, este lámpafénynél 0,021, lámpa nélkül 0,0001. A felhasználó kérése (3 / 100 % éjszakai / nappali
    fényerővel): lámpafénynél 20 % legyen (v1.1.1; előtte 0,005 volt a sötét határ, és ott 11 % jött ki).
  - **Buktató: flash-írás alatt a kamerának állnia kell.** A kijelző `LCD_DSI_ISR_CACHE_SAFE`-je miatt a GDMA
    megszakítása flash-írás alatt is fut, és ha ekkor ér véget egy kép, a CSI-meghajtó a PSRAM-ban lévő pufferen
    cache-műveletet végez: összeomlás (`ipc0`, `spi_flash_op_block_func`, ok 25 = cache-hiba; két OTA közben is). Ezért
    minden flash-írás köré `CamPause` kell (`idojaras.ino`; a `Preferences` objektum ELÉ), a `wifi_manager.cpp`-ben és az
    OTA-nál `p4_cam_pause()`. A szünet: előbb a CSI-vezérlő áll le (`esp_cam_ctlr_stop`), utána a kamera; folytatás
    fordítva. Ha csak a kamera állt le, a DMA egy félkész képen ragadt, és utána nem jött több kép. **Új mentésnél
    (NVS, FFat) a `CamPause`-t nem szabad kihagyni.** Általános horog nincs (az esp_flash a `spi_flash_guard`-ot
    megkerüli). A flash-*olvasás* is kikapcsolja a cache-t (pl. a NVS-olvasás a kezelőoldal minden betöltésekor:
    `wifiTxName()`); ez rövid, eddig nem okozott gondot, de elvben ugyanaz a kockázat.
  - **Buktató: a CSI-meghajtó tartalékpuffere kicsi** (v1.1.1). A meghajtó a saját tartalékpufferét (ahová akkor ír,
    ha nincs kiadott puffer) a *kimeneti* képmérethez foglalja (RAW8: 2,07 MB), a DMA-átvitel méretét viszont a
    *bemeneti* méretből számolja (RAW10: 2,59 MB). A túlírás felülírta a szomszédos PSRAM-ot: 2026.10.17. este az
    akkumérés ADC-adatai sérültek (`0x10111110`: sötét képadat), és az `analogReadMilliVolts` `assert`-tel leállt
    (`loopTask`, ok 2). Ezért `bk_buffer_dis = 1`, és az `on_get_new_trans` mindig a saját, 10 bites méretű pufferünket
    adja; a feladat a kép végére vár (a képek közti kb. 215 ms szünetben olvas). A coredump USB-n kiolvasható
    (`esptool read_flash 0xFF0000 0x10000`), és a `esp-coredump --chip esp32p4 info_corefile -t raw` mutatja a teljes
    hívási láncot (a PATH-on kell a `tool-riscv32-esp-elf-gdb/bin`).
  - **Buktató: a CSI-vezérlő csak a képek közti szünetben állhat le** (v1.2.2). A `p4_cam_pause()` egy kép végét
    megvárja (`s_gap`, legfeljebb 400 ms), és csak utána hívja az `esp_cam_ctlr_stop()`-ot. Kép közben leállítva (a
    megszakítással versenyezve) a DMA néha rossz címre indult újra: 2026.10.18-án kétszer képadat (0x0f-0x13, a
    feketeszint) került az `fc` és `ntfy` feladat PSRAM-vermébe, összeomlás a „kamera” feladatban (PC 0x20000000, minden
    regiszter 0), pontosan a 15 percenkénti előzménymentéskor. A kamera feladatának verme azóta belső RAM-ban van, a
    képpuffer mögött 64 KB-os védősáv (ha a DMA túlír, a kezelőoldalon „DMA-túlírás: N”).
  - Összeomlás-adat USB nélkül: `curl -o coredump.bin http://idojaras-p4.local/coredump`; a fájl 24. bájtjától ELF
    core (`tail -c +25`), `riscv32-esp-elf-gdb <a verzió .elf-je> core.elf`; vagy
    `python -m esp_coredump --chip esp32p4 info_corefile -t raw -c coredump.bin <elf>`.
  - `custom_sdkconfig`: `CONFIG_CAM_CTLR_MIPI_CSI_ISR_CACHE_SAFE=y` (enélkül a CSI-meghajtó a PSRAM-ba foglal, és a
    GDMA elutasítja: „user context not in internal RAM”). A WiFi-komponensek rögzítése maradt (2.12.13 / 1.6.4).
  - Kamera nélküli (vagy nem válaszoló) lapon a `p4_cam_init()` az azonosító olvasásánál feladja: nincs automatikus
    fényerő (a régi éjszakai mód érvényes), a `CamPause` nem csinál semmit; a kezelőoldalon „Kamera: nem működik:
    nincs (vagy nem válaszol…)”, a Beállításokban a kapcsoló mellett figyelmeztetés.
  - USB-s feltöltés: a natív USB-port (`/dev/cu.usbmodem…`); a lap UART-portján (CH34x, `/dev/cu.usbserial-…`) az
    esptool nem tud csatlakozni. OTA után a lap az `app1`-ről is indulhat: kézi esptool-feltöltésnél a `boot_app0.bin`-t
    is írni kell a `0xe000`-ra (az app a `0x10000`-ra).

- **Kijelző nem indul** (v1.2.2): ha a JD9365 panel induláskor nem válaszol (a meghajtó a `panel_init()`-ben kiolvassa
  az azonosítóját), a program 2 mp múlva újraindítja a lapot (`RC_PANEL`), legfeljebb 3-szor egymás után
  (`panel_tries`, RTC-memória); a kezelőoldalon az indulás mellett „– a kijelző nem indult” (`BootRec.flags`).

**Nyitott:**
- Egy WATCHDOG-újraindulás volt (2026.10.14. 23:15, a Mac-ről táplálva, az érintés-hibakeresés közben);
  még nincs kivizsgálva. Töltőről 15 óra alatt nem fordult elő.
- **2026.10.18. 17:47, a P4 másik tápra dugásakor** (egy „okos” töltőre; 14:21-kor egy másikra dugva FESZÜLTSÉGESÉS):
  villanás, megszakítás-watchdog (ok 24 = `Interrupt wdt timeout on CPU0`; a 0. mag az `IDLE0`-ban, a
  `esp_cpu_wait_for_intr()`-ben várt, nem jött óraütés; a saját kód egyik szálban sem szerepelt), utána az
  újraindulás eljutott a főciklusig, de a kép fekete maradt, **világító háttérrel** (tehát a `p4_display_init()`
  sikeres volt: a v1.2.2-es újrapróbálás ezt nem fogja meg). Táp ki/be után rendben. Ugyanazon a tápon öt ki-be
  dugásnál nem jött elő. Feltevés: a panel a rádugáskor ingadozó feszültségtől elvesztette a beállításait. Lehetséges
  javítás: a panel állapotának olvasása (DCS `0x0A`, power mode) induláskor és időnként, hibánál a panel újraindítása.
