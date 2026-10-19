// Időjárás-kijelző Ecowitt állomásokhoz - Waveshare ESP32-S3-Touch-LCD-7 (1024x600), LVGL 9
// Az Ecowitt konzolok "customized upload" adatait fogadja (http://<kijelző>/ecowitt).
// Fordítás PlatformIO-val (platformio.ini): a XIP_FROM_PSRAM és LCD_RGB_ISR_IRAM_SAFE beállítás nélkül
// (pl. Arduino IDE-ből fordítva) a kép flash-írás közben (mentés, frissítés) függőlegesen elcsúszhat.
// A program fájljai (az Arduino IDE és a PlatformIO ábécésorrendben egy programmá fűzi össze őket, ezért a sorrend számít):
//   idojaras.ino                 beállítások, adattípusok, közös változók
//   idojaras_02..18_*.ino        témánként a függvények: kijelző, előzmények, előrejelzés, főoldal, konzolok, grafikonok,
//                                webes kezelőoldal, diagnosztika, érintés, fényerő panel, végül az indítás és a főciklus
//   wifi_manager.cpp/.h          WiFi-kapcsolódás és saját beállító hálózat (a beállító oldal: idojaras_13)
//   font_*.c, lv_conf.h          fontok és LVGL-beállítás
#define LV_CONF_INCLUDE_SIMPLE 1
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <lvgl.h>
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include <Wire.h>
#include <initializer_list>
#include <math.h>
#include <FFat.h>
#include <Update.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <freertos/idf_additions.h>
#include <esp_core_dump.h>
#include <esp_flash.h>
#include <esp_task_wdt.h>
#include "wifi_manager.h"

#include "kijelzo.h"          // Waveshare 7B (ESP32-S3) vagy Guition JC8012P4A1C (ESP32-P4)

// Saját, ékezetes magyar font (font_hu_28.c a sketch mappában)
LV_FONT_DECLARE(font_hu_28);
// Kis ékezetes magyar font (Roboto Medium 14, font_hu_14.c): Napló, grafikon-tengelyek, lábléc
LV_FONT_DECLARE(font_hu_14);
// Hétszegmenses számjegyek a főoldalhoz (font_seg_96.c, font_seg_72.c, font_seg_40.c a sketch mappában)
LV_FONT_DECLARE(font_seg_96);
LV_FONT_DECLARE(font_seg_72);
LV_FONT_DECLARE(font_seg_40);

// LCD_H_RES, LCD_V_RES (a felület mérete) és a skálázó makrók: kijelzo.h

#define PIN_NUM_DISP_EN   -1
#define PIN_NUM_PCLK      7
#define PIN_NUM_VSYNC     3
#define PIN_NUM_HSYNC     46
#define PIN_NUM_DE        5
#define PIN_NUM_DATA0     14
#define PIN_NUM_DATA1     38
#define PIN_NUM_DATA2     18
#define PIN_NUM_DATA3     17
#define PIN_NUM_DATA4     10
#define PIN_NUM_DATA5     39
#define PIN_NUM_DATA6     0
#define PIN_NUM_DATA7     45
#define PIN_NUM_DATA8     48
#define PIN_NUM_DATA9     47
#define PIN_NUM_DATA10    21
#define PIN_NUM_DATA11    1
#define PIN_NUM_DATA12    2
#define PIN_NUM_DATA13    42
#define PIN_NUM_DATA14    41
#define PIN_NUM_DATA15    40

WebServer my_server(80);     // minden itt van: az Ecowitt (/ecowitt) és a frissítő oldal (/ és /update) is; böngészőből: http://idojaras.local/

// ---------------------------------------------------------------------------
// Az Ecowitt által küldött adatok (metrikus egységekre átszámolva)
// ---------------------------------------------------------------------------
struct WeatherData {
  // kint
  float temp_c = 0, humidity = 0, vpd_kpa = 0;
  // szél
  float wind_kmh = 0, wind_dir = 0, wind_dir_avg10m = 0, gust_kmh = 0, max_gust_kmh = 0;
  // eső (piezo)
  float raining = 0, rain_rate_mm = 0, rain_event_mm = 0, rain_hour_mm = 0, rain_day_mm = 0;
  float rain_24h_mm = 0, rain_week_mm = 0, rain_month_mm = 0, rain_year_mm = 0;
  // nap, légnyomás
  float solar_wm2 = 0, uv_index = 0, baro_rel_hpa = 0, baro_abs_hpa = 0;
  // bent
  float temp_in_c = 0, humidity_in = 0, co2 = 0, co2_24h = 0;
  // akkuk
  float wh90_batt_v = 0, ws90_cap_v = 0, console_batt_v = 0;
  // rendszer
  float runtime_s = 0, console_heap_b = 0, interval_s = 0, ws90_ver = 0;
  char stationtype[24] = "";
  char freq[12] = "";
  char dateutc[24] = "";
  bool has_data = false;
  uint32_t last_rx_ms = 0;
} weather;

constexpr float F2C   = 5.0f / 9.0f;
constexpr float MPH   = 1.60934f;   // mph  -> km/h
constexpr float INCH  = 25.4f;      // in   -> mm
constexpr float INHG  = 33.8639f;   // inHg -> hPa
constexpr float INHG_KPA = 3.38639f; // inHg -> kPa (az állomás a VPD-t is inHg-ben küldi)

// kulcs -> cél változó: érték = bejövő * mul + add
struct FloatField { const char *key; float *dst; float mul; float add; };

static const FloatField FLOAT_FIELDS[] = {
  {"tempf",            &weather.temp_c,          F2C,  -32.0f * F2C},
  {"humidity",         &weather.humidity,        1.0f, 0.0f},
  {"vpd",              &weather.vpd_kpa,         INHG_KPA, 0.0f},

  {"windspeedmph",     &weather.wind_kmh,        MPH,  0.0f},
  {"winddir",          &weather.wind_dir,        1.0f, 0.0f},
  {"winddir_avg10m",   &weather.wind_dir_avg10m, 1.0f, 0.0f},
  {"windgustmph",      &weather.gust_kmh,        MPH,  0.0f},
  {"maxdailygust",     &weather.max_gust_kmh,    MPH,  0.0f},

  // piezo esőérzékelő (WS90)
  {"srain_piezo",      &weather.raining,         1.0f, 0.0f},
  {"rrain_piezo",      &weather.rain_rate_mm,    INCH, 0.0f},
  {"erain_piezo",      &weather.rain_event_mm,   INCH, 0.0f},
  {"hrain_piezo",      &weather.rain_hour_mm,    INCH, 0.0f},
  {"drain_piezo",      &weather.rain_day_mm,     INCH, 0.0f},
  {"last24hrain_piezo",&weather.rain_24h_mm,     INCH, 0.0f},
  {"wrain_piezo",      &weather.rain_week_mm,    INCH, 0.0f},
  {"mrain_piezo",      &weather.rain_month_mm,   INCH, 0.0f},
  {"yrain_piezo",      &weather.rain_year_mm,    INCH, 0.0f},
  // billenőcsészés esőmérő (ha az állomás ilyet küld)
  {"rainratein",       &weather.rain_rate_mm,    INCH, 0.0f},
  {"eventrainin",      &weather.rain_event_mm,   INCH, 0.0f},
  {"hourlyrainin",     &weather.rain_hour_mm,    INCH, 0.0f},
  {"dailyrainin",      &weather.rain_day_mm,     INCH, 0.0f},
  {"weeklyrainin",     &weather.rain_week_mm,    INCH, 0.0f},
  {"monthlyrainin",    &weather.rain_month_mm,   INCH, 0.0f},
  {"yearlyrainin",     &weather.rain_year_mm,    INCH, 0.0f},

  {"solarradiation",   &weather.solar_wm2,       1.0f, 0.0f},
  {"uv",               &weather.uv_index,        1.0f, 0.0f},
  {"baromrelin",       &weather.baro_rel_hpa,    INHG, 0.0f},
  {"baromabsin",       &weather.baro_abs_hpa,    INHG, 0.0f},

  // a benti mezők (tempinf, humidityin, co2in, co2in_24h, console_batt) konzolonként külön: lásd ROOMS
  {"wh90batt",         &weather.wh90_batt_v,     1.0f, 0.0f},
  {"ws90cap_volt",     &weather.ws90_cap_v,      1.0f, 0.0f},

  {"runtime",          &weather.runtime_s,       1.0f, 0.0f},
  {"heap",             &weather.console_heap_b,  1.0f, 0.0f},
  {"interval",         &weather.interval_s,      1.0f, 0.0f},
  {"ws90_ver",         &weather.ws90_ver,        1.0f, 0.0f},
};

// ---------------------------------------------------------------------------
// Konzolok / helyiségek. Mindhárom konzol a WS90 adatait is továbbküldi; a kinti adatot az elsőtől (Nappali)
// vesszük, a többitől csak akkor, ha az első OUTDOOR_FAILOVER_S másodperce hallgat. A benti adat helyiségenként külön.
// name, match, low_v: a kezelőoldalon állítható (NVS "cfg": r0n/r0m/r0v ... r2n/r2m/r2v), alapértéke lent
// match: a konzol "model" vagy "stationtype" mezőjében keresett szöveg (üres: a hely nincs használatban)
// low_v: e alatti "console_batt" érték esetén akku-figyelmeztetés (0 = nincs figyelés)
// ---------------------------------------------------------------------------
struct Room {
  char name[32]; char match[24]; float low_v;
  float t, h, co2, co2_24h, batt, co2_max;
  bool has_co2, has_batt;   // has_batt: küld-e a konzol akku-mezőt (a GW3000A nem)
  uint32_t last_ms;
};
constexpr int ROOM_COUNT = 3;
static Room rooms[ROOM_COUNT] = {
  {"Nappali",   "WS3910", 3.6f},   // saját akku, teli kb. 4,5 V
  {"Iroda",     "GW3000", 0.0f},   // nincs akkuja
  {"Hálószoba", "WN1821", 2.4f},   // tápegységről megy, tartaléknak 2 ceruzaelem (névleges 3,0 V)
};
constexpr int OUTDOOR_FAILOVER_S = 120;
static int sel_room = 0;
static bool room_fresh(int r);   // a főoldal "Bent" zónája ezt a helyiséget mutatja (koppintással váltható)

// a beérkezett küldők (a frissítő oldalon listázva)
struct Sender { uint32_t ip; char st[28]; char model[28]; int room; uint32_t last_ms; uint32_t count; String raw; };   // raw: az utolsó küldés összes mezője (/raw oldal)
constexpr int SENDER_MAX = 4;
static Sender senders[SENDER_MAX] = {};

// ---------------------------------------------------------------------------
// UI: 3 oszlop x 4 sor kártya, a szín a témát jelzi
// ---------------------------------------------------------------------------
enum LineId {
  L_TEMP, L_HUM, L_VPD,
  L_WIND, L_DIR, L_DIRAVG,
  L_GUST, L_MAXGUST,
  L_RAINING, L_RATE, L_EVENT,
  L_HOUR, L_DAY, L_24H,
  L_WEEK, L_MONTH, L_YEAR,
  L_SOLAR, L_UV,
  L_BARO, L_BAROABS,
  L_TIN, L_HIN, L_TIN3,
  L_CO2, L_CO2_24H, L_CO2_3,
  L_BATT_WS, L_BATT_CAP, L_BATT_CON,
  L_SYS1, L_SYS2, L_SYS3,
  L_COUNT
};

static lv_obj_t *lbl_title = NULL;
static lv_obj_t *lbl_clock = NULL;
static lv_obj_t *lbl_info = NULL;
static lv_obj_t *lbl_warn = NULL;
static lv_obj_t *lbl[L_COUNT] = {};

static lv_style_t style_card;

constexpr int CARD_W = SX(326), CARD_H = SY(124), CARD_GAP = SX(8);
constexpr int CARD_X0 = SX(20), CARD_Y0 = SY(50), LINE_H = SY(34);

// A típusoknak a függvények előtt kell állniuk (az Arduino automatikus prototípusai miatt)
struct LineDef { int idx; const char *text; };

// 5 perces mintavétel, 24 óra = 288 minta (a ritka adatsorok miatt minden mező egész szám, tizedekben)
constexpr int HIST_DAY = 288;              // 5 perces minták egy napra
constexpr int HIST_N = 7 * HIST_DAY;      // 7 nap (a heti grafikonhoz)
constexpr int16_t HIST_NONE = INT16_MIN;
// kint °C, nappali °C, nyomás hPa, szél km/h, lökés km/h, napi eső mm, iroda °C, hálószoba °C (mind x10), CO2 nappali / hálószoba (ppm)
// (a c2 a 2. hely CO2-je, később került a végére)
struct HistSample { int16_t to, ti, p, w, g, r, t2, t3, c1, c3, c2; };
struct HistSampleV3 { int16_t to, ti, p, w, g, r, t2, t3, c1, c3; };   // WSH2/WSH3 formátum (átalakítva betöltjük)
struct HistSampleV1 { int16_t to, ti, p, w, g, r; };   // a legrégebbi fájlformátum (átalakítva betöltjük)
struct HistFile { uint32_t magic; uint32_t count; int32_t last_slot; float tmin; float tmax; int32_t day_key; uint32_t day_valid; };

// Előzmény-grafikonok (a tábla: CHARTS)
enum { HC_TEMP, HC_PRESS, HC_WIND, HC_RAIN, HC_ITEMP, HC_CO2, HC_COUNT };

// az előzménymezők (hs_get) sorszáma
enum { F_TO, F_TI, F_P, F_W, F_G, F_R, F_T2, F_T3, F_C1, F_C3, F_C2, F_COUNT };
enum { RC_NONE, RC_OTA, RC_PORTAL, RC_PANEL, RC_WIFISET, RC_MANUAL };   // a program saját újraindításainak oka (diagnosztika)
void diag_mark_restart(int cause);   // idojaras_12: a program által kezdeményezett újraindítás oka (ESP.restart() előtt)
bool diag_display_failed();   // idojaras_12: a kijelző nem indult (feljegyzi; true: újraindítással újra kell próbálni)
// a napi összesítők mezői (havi grafikon); a grafikon-adatsorban az indexük F_COUNT + M_*
// (az M_C2 a végén: így a DaySum mérete nem változott, a régi DAY1 fájl átalakítás nélkül beolvasható)
enum { M_TMAX, M_TMIN, M_P, M_W, M_G, M_R, M_TI, M_T2, M_T3, M_C1, M_C3, M_C2, M_COUNT };
constexpr int DAYS_N = 400;
constexpr uint32_t DAYS_MAGIC = 0x32594144;      // "DAY2" (a 2. hely CO2-jével)
constexpr uint32_t DAYS_MAGIC_V1 = 0x31594144;   // "DAY1"
struct DaySum { int32_t dnum; int16_t v[M_COUNT]; };   // dnum: a nap sorszáma (0 = üres hely); v: M_* sorrendben
struct DaysFile { uint32_t magic, count; };
static_assert(sizeof(DaySum) == 28, "a DaySum mérete megváltozott: új DAYS_MAGIC és átalakítás kell");


// Egy grafikon leírása: legfeljebb 3 sorozat (mező, szín, név). A tengely legalább "span" széles, "step"-re kerekítve;
// a tárolt érték / div = a kiírt érték (a hőmérséklet stb. tizedekben tárolva: div 10; a CO2 ppm-ben: div 1).
struct ChartDef {
  const char *title, *fs_title; uint32_t frame;
  int n; int field[3]; uint32_t col[3]; const char *name[3];
  int32_t span; bool zero; int32_t step; int32_t div;
  int page, col_, row_; bool wide;
  int mfield[3]; const char *mname[3];   // havi nézet: a napi összesítő mezői (M_*) és nevük
};

// ---------------------------------------------------------------------------
// Idő és éjszakai fényerő - ezeket a értékeket szabadon átírhatod
// ---------------------------------------------------------------------------
constexpr const char *TZ_DEFAULT = "UTC0";   // ha nincs beállítva időzóna (a beállítások a frissítő oldalon: http://idojaras.local/)
// az éjszakai mód (be/ki, időszak, fényerők) a beállításoknál: Config n_on, n_st, n_en, n_pct, d_pct
constexpr bool BACKLIGHT_PWM_INVERTED = true;  // 7B: a PWM-regiszter 0 értéke a legfényesebb
constexpr int  LCD_PCLK_MHZ           = 18;    // a kijelző képpont-órajele (MHz); 18 MHz-en kb. 20 kép/mp
constexpr size_t INTERNAL_RAM_RESERVE = 150 * 1024;  // ennyi belso RAM maradjon a WiFi-nek
constexpr int  WAKE_SECONDS           = 15;    // éjszaka érintésre ennyi ideig világít a kijelző, utána újra elalszik
// P4: automatikus fényerő a kamerával (cfg.cam_on, idojaras_17e): a szoba fénye (a kamera simított fényértéke) szerint az
// éjszakai és a nappali fényerő között; CAM_L_DARK alatt az éjszakai, CAM_L_BRIGHT fölött a nappali, köztük
// logaritmikusan. Ha CAM_IDLE_S ideig senki sem mozog a kijelző előtt (és érintés sincs), az éjszakai fényerőre sötétül.
// (Mérve 2026.10.17.: délelőtt, borús időben a szobában 0,04-0,35; este lámpafénynél 0,021, lámpa nélkül 0,0001.
// A felhasználó kérése: lámpafénynél (0,021) 20 % legyen, 3 / 100 % éjszakai / nappali fényerővel; ehhez a sötét
// határ 0,0009, és így a délelőtti 0,047-nél kb. 33 %.)
constexpr float CAM_L_DARK = 0.0009f, CAM_L_BRIGHT = 0.3f;
constexpr uint32_t CAM_IDLE_S = 600;
constexpr bool TOUCH_SWIPE_INVERT     = false; // true: az ujjhúzás iránya fordítva lapoz
// Hely és időzóna: a frissítő oldalon (vagy a WiFi-beállító portálon) adható meg, a "cfg" NVS-névtérben tárolva.
// Hely nélkül a napkelte/napnyugta "--:--", és a főoldal felső sorában figyelmeztetés látszik.
// pw: a kezelőoldal jelszava (felhasználónév: admin); üres = nincs jelszó
// éjszakai mód: n_on be/ki, n_st..n_en (perc, éjfélen átnyúlhat), n_pct éjszakai és d_pct nappali fényerő (%)
// n_mode: 0 = óra szerint (n_st..n_en), 1 = napnyugta szerint: napnyugta + n_so perctől napkelte + n_ro percig (hely kell hozzá)
// nt_topic: az ntfy.sh téma (üres: nincs értesítés); nt_mask: mely értesítések mennek (NT_* bitek)
// snd_on: P4: hangjelzés, amikor megjelenik a "Szellőztess" (az éjszakai mód időszakában nem); snd_vol: 0 halk, 1 közepes, 2 hangos
// cam_on: P4: automatikus fényerő a kamerával (a szoba fénye és a jelenlét szerint; lásd CAM_L_DARK)
// fw_on, fw_host: a konzolok adatainak továbbküldése egy másik kijelzőre (be/ki; cél: gépnév vagy IP, esetleg :port)
struct Config { bool has_loc; double lat, lon; char tz[64]; char pw[33]; bool n_on; int n_st, n_en, n_pct, d_pct; int n_mode, n_so, n_ro;
                char nt_topic[65]; int nt_mask; bool fw_on; char fw_host[65]; bool snd_on; int snd_vol; bool cam_on; };
enum { NT_FROST = 1, NT_RAIN = 2, NT_CO2 = 4, NT_BATT = 8, NT_STALE = 16, NT_CRASH = 32, NT_ALL = 63 };
static Config cfg = {false, 0.0, 0.0, "", "", true, 22 * 60, 6 * 60, 2, 100, 0, 30, 0, "", NT_ALL, false, "", true, 1, true};
struct NtMsg { char title[64]; char msg[200]; char tags[24]; uint8_t prio; };   // egy telefonos értesítés (idojaras_17c)
static bool co2_vent[3] = {};   // helyiségenként: "szellőztess" állapot (hiszterézissel; a felső sor és az értesítés is ezt használja)
constexpr size_t FW_BODY = 3072;   // egy továbbküldött konzol-küldés legnagyobb mérete (urlencoded)
struct FwMsg { char url[96]; size_t len; char body[FW_BODY]; };   // egy továbbküldendő küldés (idojaras_06b, PSRAM-ban)
static const char *nt_status_text();   // (a mutatót visszaadó függvényekhez a PlatformIO nem mindig készít prototípust)
static const char *fw_status_text();

// Nyelv: 0 = magyar, 1 = angol (NVS "cfg"/"lang", induláskor olvassuk; a felület indításkor épül fel, ezért a váltás újraindítással jár)
static uint8_t g_lang = 0;
static inline const char *tr(const char *hu, const char *en) { return g_lang ? en : hu; }

// Bemutató mód (NVS "cfg"/"demo", a kijelző Beállítások / Rendszer fülén kapcsolható, újraindítással): konzolok és WiFi
// nélkül kitalált adatokat mutat, és magától lapoz (idojaras_17d). A valódi előzményekhez nem nyúl (nem ment).
static bool g_demo = false;
static bool demo_loc = false;   // a bemutató saját helye (ha nincs beállítva hely); nem kerül mentésre
// P4: a lap Li-akkuja (idojaras_17e): feszültség (NAN: nincs mérés), becsült töltöttség, akkuról megy-e (elment a táp)
static float batt_v = NAN;
static int batt_pct = -1;
static bool batt_on = false;
static const char *batt_text();
static const char *cam_text();     // a kamera mérései a kezelőoldalra (P4; a 7B-n üres)
static int cam_brightness();       // P4: a kamera szerinti fényerő (%); -1: nincs (kikapcsolva, nem működik; a 7B-n mindig)
static bool cam_asleep();          // P4: a kamera szerint senki sincs a kijelző előtt (elsötétült)
static const char *batt_short();   // a kijelző Rendszer fülére (P4; a 7B-n üres)
struct ExtMark { lv_obj_t *dot, *lbl; };   // a kinti min/max jele a Hőmérséklet grafikonon: pont és felirat (idojaras_10)
struct DemoVals { float to, rh, p, w, g, dir, sol, rate, ti[3], hi[3], co2[3]; };   // a bemutató egy időpontra számolt értékei
#define TR2(arr) (arr[g_lang])   // kétnyelvű tömb: arr[0] magyar, arr[1] angol

// P4: amíg él, a kamera képfolyama áll (p4_cam_pause): minden flash-írás (Preferences-mentés, előzmény, OTA) köré kell.
// A kamera DMA-megszakítása flash-írás közben is fut (a kijelző miatt az egész GDMA "cache-biztos"), és ha ekkor ér véget
// egy kép, a PSRAM-ban lévő képpufferen végzett cache-művelet összeomlást okoz (2026.10.17.: két OTA közben is,
// "ipc0", ok 25 = cache-hiba). A Preferences-objektum ELŐTT kell létrehozni, hogy utána szűnjön meg. A 7B-n üres.
struct CamPause {
#if KIJELZO_P4
  CamPause() { p4_cam_pause(true); }
  ~CamPause() { p4_cam_pause(false); }
#endif
};

static void cfg_load() {
  Preferences p;
  p.begin("cfg", true);
  cfg.has_loc = p.isKey("lat") && p.isKey("lon");
  cfg.lat = p.getDouble("lat", 0.0);
  cfg.lon = p.getDouble("lon", 0.0);
  String tz = p.getString("tz", TZ_DEFAULT);
  String pw = p.getString("pw", "");
  cfg.n_on  = p.getBool("n_on", true);
  cfg.n_st  = p.getInt("n_st", 22 * 60);
  cfg.n_en  = p.getInt("n_en", 6 * 60);
  cfg.n_pct = p.getInt("n_pct", 2);
  cfg.d_pct = p.getInt("d_pct", 100);
  cfg.n_mode = p.getInt("n_mode", 0);
  cfg.n_so  = p.getInt("n_so", 30);
  cfg.n_ro  = p.getInt("n_ro", 0);
  strlcpy(cfg.nt_topic, p.getString("nt_t", "").c_str(), sizeof(cfg.nt_topic));
  cfg.nt_mask = p.getInt("nt_m", NT_ALL);
  cfg.fw_on = p.getBool("fw_on", false);
  cfg.snd_on = p.getBool("snd", true);
  cfg.snd_vol = constrain(p.getInt("sndv", 1), 0, 2);
  cfg.cam_on = p.getBool("cam", true);
  strlcpy(cfg.fw_host, p.getString("fw_h", "").c_str(), sizeof(cfg.fw_host));
  // helyiségek (alapérték: a rooms[] kezdőértéke)
  for (int i = 0; i < ROOM_COUNT; i++) {
    char k[8];
    static const char *EN_ROOM[3] = {"Living room", "Office", "Bedroom"};   // angol felületnél az alapértelmezett nevek
    snprintf(k, sizeof(k), "r%dn", i); strlcpy(rooms[i].name,  p.getString(k, g_lang ? EN_ROOM[i] : rooms[i].name).c_str(),  sizeof(rooms[i].name));
    snprintf(k, sizeof(k), "r%dm", i); strlcpy(rooms[i].match, p.getString(k, rooms[i].match).c_str(), sizeof(rooms[i].match));
    snprintf(k, sizeof(k), "r%dv", i); rooms[i].low_v = p.getFloat(k, rooms[i].low_v);
  }
  p.end();
  strlcpy(cfg.pw, pw.c_str(), sizeof(cfg.pw));
  strlcpy(cfg.tz, tz.length() ? tz.c_str() : TZ_DEFAULT, sizeof(cfg.tz));
}

static void cfg_save_night() {
  CamPause cam_pause_;   // P4: a kamera áll a mentés alatt
  Preferences p;
  p.begin("cfg", false);
  p.putBool("n_on", cfg.n_on);
  p.putInt("n_st", cfg.n_st);
  p.putInt("n_en", cfg.n_en);
  p.putInt("n_pct", cfg.n_pct);
  p.putInt("d_pct", cfg.d_pct);
  p.putInt("n_mode", cfg.n_mode);
  p.putInt("n_so", cfg.n_so);
  p.putInt("n_ro", cfg.n_ro);
  p.end();
}

constexpr float SND_AMP[3] = {0.02f, 0.08f, 0.2f};   // a hangjelzés erőssége: halk, közepes, hangos
static void snd_save() {   // P4: a hangjelzés be/ki és hangereje
  CamPause cam_pause_;   // P4: a kamera áll a mentés alatt
  Preferences p;
  p.begin("cfg", false);
  p.putBool("snd", cfg.snd_on);
  p.putInt("sndv", cfg.snd_vol);
  p.end();
}

static void lang_save(int lang) {
  CamPause cam_pause_;   // P4: a kamera áll a mentés alatt
  Preferences p;
  p.begin("cfg", false);
  p.putInt("lang", lang ? 1 : 0);
  p.end();
}

static void lang_load() {
  Preferences p;
  p.begin("cfg", true);
  // alapból angol; a nyelv mentése nélküli, de már beállított (a régi, magyar alapértékű programmal használt) kijelző magyar marad
  if (p.isKey("lang")) g_lang = p.getInt("lang", 1) ? 1 : 0;
  else g_lang = (p.isKey("tz") || p.isKey("lat") || p.isKey("r0n")) ? 0 : 1;
  g_demo = p.getBool("demo", false);
  p.end();
}

static void demo_save(bool on) {
  CamPause cam_pause_;   // P4: a kamera áll a mentés alatt
  Preferences p;
  p.begin("cfg", false);
  p.putBool("demo", on);
  p.end();
}

static void cfg_apply_tz() {
  setenv("TZ", cfg.tz, 1);
  tzset();
}
// Az állomás típusa és az IP fix helyen áll: a legszélesebb óra ("Szo 2026.10.03 20:41:07") után, két szóköznyi hézaggal.
// Ha a dátum/idő formátumát vagy a fontot átírod, ezt az értéket (pixel) újra kell számolni.
constexpr int  INFO_X                 = 357;

// ---------------------------------------------------------------------------
// Főoldal (dashboard) - négy zóna: időjárás, óra, kint, bent
// ---------------------------------------------------------------------------
constexpr bool  PAGE_ROTATE      = false;   // true: a három oldal érintés nélkül is időzítve váltakozik
constexpr int   DASH_SECONDS     = 30;      // (PAGE_ROTATE esetén) ennyi ideig látszik a főoldal
constexpr int   DETAILS_SECONDS  = 10;      // (PAGE_ROTATE esetén) ennyi ideig a részletes oldal
constexpr int   DETAILS_RETURN_SECONDS = 60; // a grafikon- és a részletes oldal ennyi nyugalom után visszaváltozik a főoldalra (0 = soha)
constexpr float COMFORT_DRY_MAX  = 40.0f;   // benti páratartalom: ez alatt "száraz"
constexpr float COMFORT_WET_MIN  = 60.0f;   // ... ez fölött "nedves", a kettő között "komfort"

constexpr uint32_t COL_BG_TL = 0x161616, COL_BG_TR = 0x1F1E12, COL_BG_BL = 0x2A1100, COL_BG_BR = 0x001D33;
constexpr uint32_t COL_YELLOW = 0xFFE57A, COL_YELLOW_DIM = 0x35331B;
constexpr uint32_t COL_ORANGE = 0xFF9A1F, COL_ORANGE_DIM = 0x4A2706, COL_ORANGE_SOFT = 0xFFB866;
constexpr uint32_t COL_BLUE   = 0x3EC6FF, COL_BLUE_DIM   = 0x0E3A58, COL_BLUE_SOFT   = 0x7FB4D0;
constexpr uint32_t COL_GREY   = 0x9A9A9A, COL_TEXT = 0xD8D8D8, COL_WHITE_DIM = 0x2A2A2A;

// Figyelmeztetések (színváltás) és elavult adat
constexpr int   STALE_SECONDS  = 300;      // ennyi idő után az utolsó adat "elavult": az értékek elhalványulnak
constexpr float WARN_FROST_C   = 0.0f;     // kint ennyi vagy ennél hidegebb: a hőmérséklet jégkékre vált
constexpr float WARN_HOT_C     = 30.0f;    // kint ennyi vagy ennél melegebb: a hőmérséklet pirosra vált
constexpr float WARN_GUST_KMH  = 50.0f;    // ennél erősebb lökés: piros
constexpr float WARN_UV_MID    = 6.0f;     // UV: ettől narancs
constexpr float WARN_UV_HIGH   = 8.0f;     // UV: ettől piros
constexpr float WARN_CO2_MID   = 1000.0f;  // CO2 (ppm): ettől narancs
constexpr float WARN_CO2_HIGH  = 1500.0f;  // CO2 (ppm): ettől piros
constexpr float CO2_VENT_ON    = 1000.0f;  // ettől "Szellőztess: <helyiség>" a főoldal felső sorában ...
constexpr float CO2_VENT_OFF   = 900.0f;   // ... és ez alatt tűnik el (a kettő közötti sáv megakadályozza a villogást)
constexpr float LOW_WS90_V     = 2.5f;     // a WS90 elemfeszültsége e alatt "alacsony akku" (egy friss elemes WS90 kb. 3,0 V-ot küld)
constexpr float LOW_CAP_V      = 3.0f;     // a WS90 kondenzátorának feszültsége e alatt figyelmeztet (most kb. 5,3 V)
constexpr uint32_t COL_WARN = 0xFF5A3C, COL_ICE = 0xBFE9FF;

// A ki nem világító ("szellem") szegmensek erőssége: a háttér és a számjegy színe közötti keverés százaléka (0 = láthatatlan)
constexpr int GHOST_PCT = 0;
constexpr uint32_t mix_col(uint32_t bg, uint32_t fg, int pct) {
  return ((((bg >> 16) & 0xFF) * (100 - pct) + ((fg >> 16) & 0xFF) * pct) / 100) << 16 |
         ((((bg >> 8) & 0xFF) * (100 - pct) + ((fg >> 8) & 0xFF) * pct) / 100) << 8 |
         (((bg & 0xFF) * (100 - pct) + (fg & 0xFF) * pct) / 100);
}
constexpr uint32_t GHOST_TL = mix_col(COL_BG_TL, 0xE8E8E8, GHOST_PCT);
constexpr uint32_t GHOST_TR = mix_col(COL_BG_TR, COL_YELLOW, GHOST_PCT);
constexpr uint32_t GHOST_BL = mix_col(COL_BG_BL, COL_ORANGE, GHOST_PCT);
constexpr uint32_t GHOST_BR = mix_col(COL_BG_BR, COL_BLUE, GHOST_PCT);

// zónák: felül két hőmérséklet-zóna, alul előrejelzés | szél-iránytű | óra
constexpr int MOON_D = S(56);   // holdfázis-ikon átmérője
constexpr int ZT_Y = SY(48), ZT_H = SY(244), ZT_X0 = SX(20);                                         // felső sor
constexpr int ZTK_W = SX(536), ZTB_W = SX(440), ZT_X1 = SX(20 + 536 + 8);                               // Kint (széles) és Bent (keskeny)
constexpr int ZB_Y = SY(300), ZB_H = SY(286);                                                    // alsó sor
constexpr int ZF_X = SX(20), ZF_W = SX(326), ZC_X = SX(354), ZC_W = SX(286), ZK_X = SX(648), ZK_W = SX(356);      // előrejelzés, iránytű, óra

enum { ICON_SUN, ICON_PARTLY, ICON_CLOUDY, ICON_RAIN, ICON_NIGHT, ICON_COUNT };
enum { PAGE_DASH, PAGE_FC, PAGE_HIST, PAGE_HIST2, PAGE_DETAILS, PAGE_LOG, PAGE_COUNT };

static lv_obj_t *page_dash = NULL, *page_details = NULL, *page_hist = NULL, *page_log = NULL, *lbl_log = NULL;
static lv_obj_t *d_clock = NULL, *d_secs = NULL, *d_date = NULL, *d_week = NULL, *d_year = NULL;
static lv_obj_t *d_press = NULL, *d_rain = NULL, *d_uv = NULL;
static lv_obj_t *d_otemp = NULL, *d_ohum = NULL, *d_dew = NULL;
static lv_obj_t *d_kalert = NULL;
constexpr int CH_TOG_X = SX(160), CH_TOG_Y = SY(4), CH_TOG_W = SX(300), CH_TOG_H = SY(40);      // Nap | Hét | Hónap kapcsoló (képernyő-koordináták)
static lv_obj_t *lbl_gear = NULL;
static lv_obj_t *tap_dot = NULL;    // koppintáskor rövid ideig felvillanó kör (visszajelzés); a legfelső rétegen
static uint32_t tap_dot_until = 0;   // fogaskerék a főoldal felső sorának jobb szélén: a Beállítások képernyő   // a Kint zóna fejlécében: eső-/fagyfigyelmeztetés az órás előrejelzésből
static lv_obj_t *d_itemp = NULL, *d_ihum = NULL, *d_co2 = NULL, *d_co2max = NULL, *d_room = NULL;
static lv_obj_t *d_tmin = NULL, *d_tmax = NULL, *d_otemp_g = NULL, *d_ominus = NULL;
static lv_obj_t *d_comfort[3] = {};
static lv_obj_t *d_icons[ICON_COUNT] = {};
static int current_page = PAGE_DASH;
static uint32_t page_since = 0;

struct DayStats { float tmin; float tmax; int yday; bool valid; };   // yday: év*400 + az év napja
static DayStats day_stats = {0.0f, 0.0f, -1, false};
static int co2_max_key = -1;       // a helyiségenkénti napi CO2-maximum (Room::co2_max) napja; újraindításkor nullázódik

// trendnyilak (3 db) és előrejelzés
constexpr int ARW = S(36);                                  // nyíl mérete (pixel)
enum { ARR_PRESS, ARR_OTEMP, ARR_ITEMP, ARR_COUNT };
static lv_obj_t *arrow_cv[ARR_COUNT] = {};
static int arrow_level[ARR_COUNT] = {99, 99, 99};         // 99 = még nem rajzoltuk
static lv_obj_t *d_fc1 = NULL;
static lv_obj_t *d_sunrise = NULL, *d_sunset = NULL;
static lv_obj_t *moon_canvas = NULL;
static lv_obj_t *d_feels = NULL;
static lv_obj_t *d_wnum = NULL, *d_wdir = NULL, *d_wgust = NULL, *wind_cv = NULL;
static int wind_drawn = -999;

// előzmények: 288 db 5 perces minta, az abszolút "szelet" (epoch/300) modulo 288 a helye
static HistSample *hist = NULL;           // HIST_N elem a PSRAM-ban (hist_alloc)
static int32_t hist_last_slot = -1;
static bool hist_loaded = false, hist_dirty = false, ffat_ok = false;

// ébredés: éjszaka érintésre addig világít a kijelző
static uint32_t wake_until_ms = 0;
static bool touch_down_event = false;

// második előzményoldal ("Bent"); a grafikonkártyák adatai a CHARTS táblában
static lv_obj_t *page_hist2 = NULL;
// teljes képernyős grafikon (egy kártyára koppintva nyílik)
static lv_obj_t *fs_page = NULL, *fs_card = NULL, *fs_chart = NULL, *fs_title = NULL;
static lv_obj_t *fs_ylab[5] = {}, *fs_xlab[7] = {}, *fs_sw[3] = {}, *fs_v[3] = {};
static lv_chart_series_t *fs_se[3] = {};
static int fs_idx = -1;                      // melyik grafikon látszik teljes képernyőn (-1: egyik sem)
static void fs_refresh();
static void fs_close();

static uint32_t my_tick_cb(void) {
  return millis();
}

void disp_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map) {
#if KIJELZO_P4
  p4_display_flush(area->x1, area->y1, area->x2, area->y2, (const uint16_t *)px_map);
#else
  esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) lv_display_get_user_data(disp);
  esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
#endif
  lv_display_flush_ready(disp);
}

// Az Arduino belépési pontjai. A tényleges indítás és főciklus az idojaras_18_setup_loop.ino-ban van; ezeknek a főfájlban
// kell lenniük, mert a PlatformIO a setup()-ot tartalmazó fájlt teszi az összefűzés elejére.
void setup() { app_setup(); }
void loop() { app_loop(); }
