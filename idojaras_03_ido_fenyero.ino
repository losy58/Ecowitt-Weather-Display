// Helyi idő, háttérvilágítás és éjszakai mód, napi min/max
// (Az idojaras.ino része: az Arduino IDE és a PlatformIO a fájlokat ábécésorrendben, egy programmá fűzi össze.)

// ---------------------------------------------------------------------------
// Helyi idő (NTP) és háttérvilágítás
// ---------------------------------------------------------------------------
static bool get_local_tm(struct tm &t) {
  time_t now = time(nullptr);
  if (now < 1600000000) return false;   // az NTP még nem szinkronizált
  localtime_r(&now, &t);
  return true;
}

// Háttérvilágítás: a CH32V003 PWM-regisztere (0x05, I2C 0x24). A 7B-n invertált,
// a biztonságos felső határ 247 (a teljes tartomány 97 %-a). A P4-es kijelzőn a GPIO23 PWM-je.
static void backlight_write(int pct) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
#if KIJELZO_P4
  p4_backlight(pct);
#else
  int duty = pct * 247 / 100;
  uint8_t reg = BACKLIGHT_PWM_INVERTED ? (247 - duty) : duty;

  Wire.beginTransmission(0x24);
  Wire.write(0x05);
  Wire.write(reg);
  Wire.endTransmission();
#endif
}

// A mai napkelte és napnyugta (perc a helyi éjféltől); false, ha nincs hely vagy pontos idő
static bool sun_today(int *rise, int *set) {
  struct tm t;
  if (!cfg.has_loc || !get_local_tm(t)) return false;
  const time_t now = time(nullptr);
  int off = (t.tm_hour * 60 + t.tm_min) - (int)((now % 86400L) / 60L);   // a helyi idő eltérése az UTC-től (perc)
  while (off > 720) off -= 1440;
  while (off < -720) off += 1440;
  return sun_event(t.tm_yday + 1, true, off, rise) && sun_event(t.tm_yday + 1, false, off, set);
}

// Az éjszaka kezdete és vége ma (perc); napnyugta szerinti módban a napnyugtához/napkeltéhez képest, hely nélkül óra szerint
static void night_window(int *st, int *en) {
  int rise, set;
  if (cfg.n_mode == 1 && sun_today(&rise, &set)) {
    *st = ((set + cfg.n_so) % 1440 + 1440) % 1440;
    *en = ((rise + cfg.n_ro) % 1440 + 1440) % 1440;
  } else {
    *st = cfg.n_st; *en = cfg.n_en;
  }
}

// Az éjszakai mód időszakában vagyunk-e (akkor is, ha a sötétítés ki van kapcsolva; a hangjelzés csendes időszaka)
static bool in_night_window() {
  int st, en;
  night_window(&st, &en);
  if (st == en) return false;
  struct tm t;
  if (!get_local_tm(t)) return false;   // idő nélkül nappali fényerő
  const int m = t.tm_hour * 60 + t.tm_min;
  if (st > en) return m >= st || m < en;   // pl. 22:00 - 06:00 (éjfélen átnyúlik)
  return m >= st && m < en;
}

static bool is_night() {
  if (!cfg.n_on || g_demo) return false;   // bemutatón mindig nappali fényerő
  return in_night_window();
}

// a fényerő panel állításakor rövid ideig a beállított érték látszik
static int bl_preview_pct = 100;
static uint32_t bl_preview_until = 0;

static bool is_awake() {
  return (int32_t)(wake_until_ms - millis()) > 0;
}

static int target_brightness() {
  if (bl_preview_until && (int32_t)(bl_preview_until - millis()) > 0) return bl_preview_pct;
  const int cb = cam_brightness();   // P4, kamerával: a szoba fénye és a jelenlét szerint (az éjszakai időzítés helyett)
  if (cb >= 0) return cb;
  if (!is_night()) return cfg.d_pct;
  return is_awake() ? cfg.d_pct : cfg.n_pct;
}

// Fokozatos átmenet a cél fényerőre: felfelé gyorsan (kb. fél mp), lefelé lassan (kb. 2 mp)
static void backlight_task() {
  static uint32_t last_step = 0;
#if KIJELZO_P4
  static int current = P4_BL_START;   // a P4-es kijelző kisebb fényerővel indul (p4_display_init), innen fokozatosan
#else
  static int current = 100;   // induláskor a panel teljes fényerővel világít
#endif

  if (millis() - last_step < 40) return;
  last_step = millis();

  int target = target_brightness();
  if (current == target) return;

  if (target > current) current = (current + 10 < target) ? current + 10 : target;
  else                  current = (current - 2 > target) ? current - 2 : target;

  backlight_write(current);
}

// Csak akkor írjuk át a feliratot, ha a szöveg tényleg változott (kevesebb újrarajzolás)
static void set_label_if_changed(lv_obj_t *obj, const char *text) {
  const char *cur = lv_label_get_text(obj);
  if (!cur || strcmp(cur, text) != 0) lv_label_set_text(obj, text);
}

// ---------------------------------------------------------------------------
// Főoldal frissítése
// ---------------------------------------------------------------------------
static const char *const WEEKDAYS_LONG_T[2][7] = {{"Vasárnap", "Hétfő", "Kedd", "Szerda", "Csütörtök", "Péntek", "Szombat"},
                                                  {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"}};
#define WEEKDAYS_LONG TR2(WEEKDAYS_LONG_T)

static float dew_point_c(float t, float rh) {
  if (rh < 1.0f) return NAN;
  float g = logf(rh / 100.0f) + (17.62f * t) / (243.12f + t);
  return 243.12f * g / (17.62f - g);
}

// A napi legnagyobb / legkisebb kinti hőmérséklet (helyi éjfélkor nullázódik; újraindítás után a mentett érték folytatódik)
static void update_day_stats() {
  struct tm t;
  if (!get_local_tm(t)) return;   // pontos idő nélkül nem gyűjtünk
  int key = (t.tm_year + 1900) * 400 + t.tm_yday;
  if (key != co2_max_key) { co2_max_key = key; for (Room &r : rooms) r.co2_max = 0.0f; }
  for (Room &r : rooms) if (r.has_co2 && r.co2 > r.co2_max) r.co2_max = r.co2;
  if (!day_stats.valid || key != day_stats.yday) {
    day_stats.tmin = day_stats.tmax = weather.temp_c;
    day_stats.yday = key;
    day_stats.valid = true;
    return;
  }
  if (weather.temp_c < day_stats.tmin) day_stats.tmin = weather.temp_c;
  if (weather.temp_c > day_stats.tmax) day_stats.tmax = weather.temp_c;
}
