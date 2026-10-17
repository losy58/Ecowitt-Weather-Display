// P4 (Guition JC8012P4A1C): a lap valós idejű órája (RX8025T, gombelemes) és Li-akkuja (IP5306 tölti).
// Óra: induláskor ebből jön az idő (WiFi és NTP nélkül is pontos az óra, és azonnal betöltődik az előzmény); az NTP-vel
// érkező pontos időt visszaírjuk bele. Akku: a feszültség a GPIO52-n (68k/100k osztó). Ha a táp kimarad, a lap akkuról
// megy tovább: ezt a feszültség esése jelzi (hálózati tápon az akku feszültsége áll, terhelés alatt percenként
// néhány század voltot esik), a táp visszatérését az emelkedése (töltés).
// Ilyenkor a fejlécben "Akkuról megy", és telefonos értesítés (a "Lemerülő / hiányzó elem" értesítéssel együtt kapcsolható).
// A 7B-n (S3) nincs ilyen: ott a függvények üresek.
// (Az idojaras.ino része: az Arduino IDE és a PlatformIO a fájlokat ábécésorrendben, egy programmá fűzi össze.)

#if KIJELZO_P4
#include <esp_sntp.h>

constexpr int BATT_PIN = 52;
constexpr float BATT_DIV = 168.0f / 100.0f;      // a 68k / 100k osztó: akkufeszültség = lábfeszültség * 1,68
constexpr uint32_t BATT_SAMPLE_MS = 2000;        // mintavétel (8 mérés átlaga)
constexpr int BATT_WIN = 60;                     // a változás ablaka: 60 minta = 2 perc
constexpr float BATT_DROP_V = 0.07f;             // ennyi esés az ablakban: elment a táp (a töltés végén legfeljebb pár század)
constexpr float BATT_RISE_V = 0.08f;             // ennyi emelkedés: visszajött (töltés)
constexpr float BATT_FALL_V = 0.03f, BATT_FULL_V = 4.10f;   // ennyi esés BATT_FULL_V alatt is: akkuról megy (lásd lent)
// A táp kihúzásakor a lap újraindul (az IP5306 átkapcsolásakor egy pillanatra nincs feszültség): ilyenkor az esés
// az indulás előtt volt, és nem látjuk. Ezért akkor is akkuról menőnek vesszük, ha a feszültség BATT_FULL_V alatt
// van, és az ablakban legalább BATT_FALL_V-ot esett: hálózati tápon töltés közben emelkedik, teli akkunál 4,12 V fölött áll.
// (mérve 2026.10.15.: hálózati tápon 10 percig 4,12 V; kihúzva 30 mp alatt 4,05 V, 1 perc alatt 3,97 V;
// visszadugva 20 mp alatt 4,12 V, majd töltés közben 4,18 V)
constexpr float BATT_LOW_V = 3.5f;               // akkuról menet e alatt: lemerülőben
// Akku nélküli lap: a mért feszültség vagy nagyon alacsony (BATT_NONE_V alatt; egy Li-akkuval a lap ennél jóval
// előbb kikapcsol), vagy ugrál (az IP5306 akku nélkül ki-be kapcsolja a töltést: két minta között BATT_JUMP_V-nál
// nagyobb változás, valódi akkunál a táp kihúzása után is csak pár század volt). Ilyenkor "nincs akku", és nincs
// áramszünet-figyelés. Ha BATT_NONE_MS-ig nem volt ilyen mérés (pl. utólag akkut tettek bele), újra figyel.
constexpr float BATT_NONE_V = 2.5f, BATT_JUMP_V = 0.25f;
constexpr uint32_t BATT_NONE_MS = 10UL * 60000UL;
static float batt_win[BATT_WIN];
static int batt_n = 0, batt_i = 0;
static float batt_min10 = 99.0f, batt_max10 = 0.0f;   // az utolsó 10 perc szélső értékei (a kezelőoldalon)
static uint32_t batt_since = 0, batt_win10_ms = 0;
static bool batt_low_sent = false;
static bool batt_none = false;                   // nincs akku (lásd BATT_NONE_V)
static uint32_t batt_none_ms = 0;                // az utolsó akku nélküli mérés ideje
static float batt_prev_raw = NAN;
static volatile bool rtc_ntp_synced = false;

static void rtc_sntp_cb(struct timeval *) { rtc_ntp_synced = true; }

// becsült töltöttség (%) terhelés alatti feszültségből (Li-ion, durva közelítés)
static int batt_pct_of(float v) {
  static const float V[] = {3.30f, 3.50f, 3.65f, 3.75f, 3.85f, 4.00f, 4.15f};
  static const int P[] = {0, 5, 20, 40, 60, 80, 100};
  if (v <= V[0]) return 0;
  for (int i = 1; i < 7; i++)
    if (v < V[i]) return P[i - 1] + (int)((v - V[i - 1]) / (V[i] - V[i - 1]) * (P[i] - P[i - 1]));
  return 100;
}

// röviden, a kijelző Beállítások / Rendszer fülére
static const char *batt_short() {
  static char b[80];
  if (isnan(batt_v)) return tr("Akku: nincs mérés", "Battery: no reading");
  if (batt_none) return tr("Akku: nincs", "Battery: none");
  snprintf(b, sizeof(b), tr("Akku: %.2f V (kb. %d %%), %s", "Battery: %.2f V (about %d %%), %s"), batt_v, batt_pct,
           batt_on ? tr("akkuról megy", "on battery") : tr("hálózati táp", "mains power"));
  return b;
}

static const char *batt_text() {
  static char b[160];
  if (isnan(batt_v)) return tr("nincs mérés", "no reading");
  if (batt_none) {
    snprintf(b, sizeof(b), tr("nincs akku (mért: %.2f V)", "no battery (measured: %.2f V)"), batt_v);
    return b;
  }
  snprintf(b, sizeof(b), tr("%.2f V (kb. %d %%), %s; az utolsó 10 percben %.2f - %.2f V",
                            "%.2f V (about %d %%), %s; last 10 minutes %.2f - %.2f V"),
           batt_v, batt_pct, batt_on ? tr("akkuról megy", "on battery") : tr("hálózati táp", "mains power"),
           batt_min10 < 98.0f ? batt_min10 : batt_v, batt_max10 > 0.0f ? batt_max10 : batt_v);
  return b;
}

// Automatikus fényerő a kamerával (cfg.cam_on; lásd CAM_L_DARK az idojaras.ino-ban). A backlight_task()-ból hívva
// (40 ms-onként). Jelenlét: mozgás a kamera előtt vagy érintés; CAM_IDLE_S ideig egyik sem: az éjszakai fényerő.
static bool cam_away = false;    // senki sincs a kijelző előtt (elsötétült)
static int cam_pct = -1;         // a fény szerinti fényerő (a jelenléttől függetlenül; a kezelőoldalra is)

static int cam_brightness() {
  static uint32_t seen_ms = 0;
  static bool seen_init = false;
  if (!cfg.cam_on || g_demo) { cam_away = false; cam_pct = -1; return -1; }
  p4_cam_stat_t c;
  p4_cam_get(&c);
  if (!c.ok || c.light_avg <= 0.0f) { cam_away = false; cam_pct = -1; return -1; }
  if (!seen_init || c.motion_age_s < 2 || is_awake()) { seen_ms = millis(); seen_init = true; }
  const bool away = millis() - seen_ms >= CAM_IDLE_S * 1000UL;
  if (away != cam_away) {
    cam_away = away;
    LogOut.println(away ? "Kamera: senki sincs a kijelző előtt, sötétítés" : "Kamera: mozgás a kijelző előtt, ébresztés");
  }
  // a fény szerinti fényerő: logaritmikusan az éjszakai és a nappali között (ahogy a szem érzékeli)
  float t = (logf(c.light_avg) - logf(CAM_L_DARK)) / (logf(CAM_L_BRIGHT) - logf(CAM_L_DARK));
  t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
  const int lo = min(cfg.n_pct, cfg.d_pct), hi = max(cfg.n_pct, cfg.d_pct);
  const int pct = (int)lroundf(lo * powf((float)hi / lo, t));
  if (cam_pct < 0 || abs(pct - cam_pct) >= 2 || pct == lo || pct == hi) cam_pct = pct;   // a kis ingadozás ne látsszon
  return cam_away ? cfg.n_pct : cam_pct;
}

static bool cam_asleep() { return cam_away; }

// a kamera mérései a kezelőoldalra (a kép nem kerül ki a lapról)
static const char *cam_text() {
  static char b[400];
  p4_cam_stat_t c;
  p4_cam_get(&c);
  if (!c.ok) {
    const char *e = strncmp(c.err, "Kamera: ", 8) == 0 ? c.err + 8 : c.err;   // a Napló üzenete, a "Kamera: " nélkül
    snprintf(b, sizeof(b), tr("nem működik: %s", "not working: %s"), e[0] ? e : tr("nem indult", "not started"));
    return b;
  }
  if (!c.frames) {
    snprintf(b, sizeof(b), tr("indul... (%u kép elmaradt)", "starting... (%u frames missed)"), (unsigned)c.errors);
    return b;
  }
  char ago[48];
  if (c.motion_age_s == UINT32_MAX) strlcpy(ago, tr("még nem volt", "none yet"), sizeof(ago));
  else snprintf(ago, sizeof(ago), tr("%u mp-e", "%u s ago"), (unsigned)c.motion_age_s);
  char auto_s[96];
  if (!cfg.cam_on) strlcpy(auto_s, tr("automatikus fényerő: kikapcsolva", "automatic brightness: off"), sizeof(auto_s));
  else if (cam_pct < 0) strlcpy(auto_s, tr("automatikus fényerő: még nincs mérés", "automatic brightness: no reading yet"), sizeof(auto_s));
  else if (cam_away) snprintf(auto_s, sizeof(auto_s), tr("senki sincs előtte: %d %%", "nobody in front: %d %%"), cfg.n_pct);
  else snprintf(auto_s, sizeof(auto_s), tr("automatikus fényerő: %d %%", "automatic brightness: %d %%"), cam_pct);
  snprintf(b, sizeof(b), tr("fényérték %.3g (simítva %.3g; világosság %d, expozíció %.1f ms, erősítés %.1fx); mozgás %.1f %%, "
                            "utoljára %s; %s; %u kép, %u elmaradt",
                            "light value %.3g (smoothed %.3g; brightness %d, exposure %.1f ms, gain %.1fx); motion %.1f %%, "
                            "last %s; %s; %u frames, %u missed"),
           c.light, c.light_avg, c.mean, c.exp_ms, c.gain, c.motion, ago, auto_s, (unsigned)c.frames, (unsigned)c.errors);
  return b;
}

// Az app_setup()-ból (az érintés után, az időzóna beállítása után, a WiFi előtt)
static void p4_hw_setup() {
  sntp_set_time_sync_notification_cb(rtc_sntp_cb);
  int y, mo, d, h, mi, s;
  if (p4_rtc_read(&y, &mo, &d, &h, &mi, &s) && y >= 2024) {
    if (time(nullptr) < 1600000000) {
      struct timeval tv = {(time_t)(day_num(y, mo, d) - 1) * 86400 + h * 3600 + mi * 60 + s, 0};
      settimeofday(&tv, NULL);
    }
    LogOut.printf("Óra (RX8025T): %04d.%02d.%02d %02d:%02d UTC, az idő innen (az NTP pontosítja)\n", y, mo, d, h, mi);
  } else {
    LogOut.println("Óra (RX8025T): nincs érvényes idő (az NTP után beállítjuk)");
  }
  analogSetPinAttenuation(BATT_PIN, ADC_11db);
  p4_audio_init(p4_log);   // hangszóró (szellőztetés-jelzés)
}

static void batt_event(bool on) {
  batt_on = on;
  const uint32_t mins = (millis() - batt_since) / 60000UL;
  batt_since = millis();
  batt_low_sent = false;
  char msg[160];
  if (on) {
    LogOut.printf("Áramszünet: akkuról megy (%.2f V)\n", batt_v);
    snprintf(msg, sizeof(msg), tr("A P4 kijelző akkuról megy (%.2f V, kb. %d %%).", "The P4 display runs on battery (%.2f V, about %d %%)."), batt_v, batt_pct);
    if (!g_demo && (cfg.nt_mask & NT_BATT)) nt_push(tr("Áramszünet", "Power failure"), msg, 4, "electric_plug");
  } else {
    LogOut.printf("Újra van táp (%u percig ment akkuról, %.2f V)\n", (unsigned)mins, batt_v);
    snprintf(msg, sizeof(msg), tr("A P4 kijelző %u percig ment akkuról; most újra hálózati tápról megy.", "The P4 display ran on battery for %u minutes; it is on mains power again."), (unsigned)mins);
    if (!g_demo && (cfg.nt_mask & NT_BATT)) nt_push(tr("Újra van táp", "Power is back"), msg, 3, "white_check_mark");
  }
  update_status_bar();
}

// Hangjelzés, amikor egy helyiségnél bekapcsol a "Szellőztess" (co2_vent, a fejléc állapota), ha be van kapcsolva
// (cfg.snd_on); az éjszakai mód időszakában nem (akkor sem, ha a sötétítés ki van kapcsolva)
static void vent_beep_task() {
  static bool prev[ROOM_COUNT] = {};
  bool fire = false;
  for (int i = 0; i < ROOM_COUNT; i++) {
    if (co2_vent[i] && !prev[i]) fire = true;
    prev[i] = co2_vent[i];
  }
  if (fire && cfg.snd_on && !in_night_window()) { LogOut.println("Szellőztess: hangjelzés"); p4_beep(SND_AMP[cfg.snd_vol]); }
}

// a kezelőoldal hangerő-választója (és "Meghallgatás" gombja): a hangjelzés a megadott hangerővel (v=0..2), mentés nélkül
static void handle_beep_test() {
  if (!ota_auth_ok()) { request_auth(); return; }
  const int v = cur_srv->hasArg("v") ? constrain((int)cur_srv->arg("v").toInt(), 0, 2) : cfg.snd_vol;
  p4_beep(SND_AMP[v]);
  cur_srv->send(204, "text/plain", "");
}

// A loop()-ból
static void p4_hw_task() {
  vent_beep_task();
  // az NTP után (és óránként, amikor az NTP újra szinkronizál) az óra beállítása
  if (rtc_ntp_synced) {
    rtc_ntp_synced = false;
    const time_t now = time(nullptr);
    struct tm u;
    gmtime_r(&now, &u);
    static bool logged = false;
    const bool ok = p4_rtc_write(u.tm_year + 1900, u.tm_mon + 1, u.tm_mday, u.tm_hour, u.tm_min, u.tm_sec, u.tm_wday);
    if (!logged || !ok) { logged = true; LogOut.println(ok ? "Óra (RX8025T): beállítva az NTP-ből" : "Óra (RX8025T): beállítás sikertelen"); }
  }

  // akkufeszültség
  static uint32_t last = 0;
  if (last && millis() - last < BATT_SAMPLE_MS) return;
  last = millis();
  uint32_t mv = 0;
  for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(BATT_PIN);
  const float v = mv / 8.0f / 1000.0f * BATT_DIV;
  // akku nélküli lap?
  if (v < BATT_NONE_V || (!isnan(batt_prev_raw) && fabsf(v - batt_prev_raw) > BATT_JUMP_V)) {
    batt_none_ms = millis();
    if (!batt_none) {
      batt_none = true;
      LogOut.printf("Akku: nincs (mért: %.2f V)\n", v);
      batt_n = batt_i = 0;   // az akku figyelése elölről, ha később lesz akku
      batt_low_sent = false;
      if (batt_on) { batt_on = false; update_status_bar(); }   // a korábbi "akkuról megy" téves volt (értesítés nélkül)
    }
  } else if (batt_none && millis() - batt_none_ms > BATT_NONE_MS) {
    batt_none = false;
    LogOut.printf("Akku: van (%.2f V)\n", v);
  }
  batt_prev_raw = v;
  batt_v = isnan(batt_v) ? v : batt_v * 0.5f + v * 0.5f;
  batt_pct = batt_pct_of(batt_v);
  if (millis() - batt_win10_ms > 600000UL) { batt_win10_ms = millis(); batt_min10 = batt_max10 = batt_v; }
  batt_min10 = fminf(batt_min10, batt_v); batt_max10 = fmaxf(batt_max10, batt_v);

  if (batt_none) return;   // nincs akku: nincs áramszünet- és lemerülés-figyelés

  // esés / emelkedés az utolsó 2 perchez képest
  if (batt_n >= 15) {   // induláskor már fél perc után (az ablak addig csak részben telt meg)
    float mx = 0.0f, mn = 99.0f;
    for (int i = 0; i < batt_n; i++) { mx = fmaxf(mx, batt_win[i]); mn = fminf(mn, batt_win[i]); }
    if (!batt_on && (batt_v < mx - BATT_DROP_V || (batt_v < BATT_FULL_V && batt_v < mx - BATT_FALL_V))) batt_event(true);
    else if (batt_on && batt_v > mn + BATT_RISE_V) batt_event(false);
  }
  batt_win[batt_i] = batt_v;
  batt_i = (batt_i + 1) % BATT_WIN;
  if (batt_n < BATT_WIN) batt_n++;

  if (batt_on && !batt_low_sent && batt_v < BATT_LOW_V) {
    batt_low_sent = true;
    LogOut.printf("Akku lemerülőben (%.2f V)\n", batt_v);
    if (!g_demo && (cfg.nt_mask & NT_BATT)) nt_push(tr("Lemerül az akku", "Battery low"), tr("A P4 kijelző akkuja lemerülőben, hamarosan kikapcsol.", "The P4 display's battery is running low; it will switch off soon."), 4, "battery");
  }
}

#else
static int cam_brightness() { return -1; }
static bool cam_asleep() { return false; }
static const char *cam_text() { return ""; }
static const char *batt_text() { return ""; }
static const char *batt_short() { return ""; }
static void p4_hw_setup() {}
static void p4_hw_task() {}
#endif
