// Előzmények (7 nap, 5 perces minták, FFat), napi összesítők (havi grafikon), trendnyilak
// (Az idojaras.ino része: az Arduino IDE és a PlatformIO a fájlokat ábécésorrendben, egy programmá fűzi össze.)

// ---------------------------------------------------------------------------
// Előzmények (24 óra, 5 perces minták), mentés FFat-ra, trend, előrejelzés
// ---------------------------------------------------------------------------
constexpr uint32_t HIST_MAGIC = 0x34485357;      // "WSH4" (7 nap, a 2. hely CO2-jével)
constexpr uint32_t HIST_MAGIC_V3 = 0x33485357;   // "WSH3" (7 nap)
constexpr uint32_t HIST_MAGIC_V2 = 0x32485357;   // "WSH2" (24 óra, helyiségenkénti adatokkal)
constexpr uint32_t HIST_MAGIC_V1 = 0x31485357;   // "WSH1"
constexpr uint32_t HIST_SAVE_MS = 15UL * 60UL * 1000UL;   // ilyen gyakran mentünk (a flash kíméléséért)

static int32_t now_slot() { return (int32_t)(time(nullptr) / 300); }

static int16_t to10(float v) {
  if (isnan(v)) return HIST_NONE;
  float x = v * 10.0f;
  if (x > 32000.0f) x = 32000.0f;
  if (x < -32000.0f) x = -32000.0f;
  return (int16_t)lroundf(x);
}

static void hist_clear_entry(HistSample &e) { e.to = e.ti = e.p = e.w = e.g = e.r = e.t2 = e.t3 = e.c1 = e.c3 = e.c2 = HIST_NONE; }

static void hist_from_v3(const HistSampleV3 &o, HistSample &e) {
  hist_clear_entry(e);
  e.to = o.to; e.ti = o.ti; e.p = o.p; e.w = o.w; e.g = o.g; e.r = o.r; e.t2 = o.t2; e.t3 = o.t3; e.c1 = o.c1; e.c3 = o.c3;
}

static bool hist_alloc() {
  if (!hist) hist = (HistSample *)heap_caps_malloc(sizeof(HistSample) * HIST_N, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (hist) for (int i = 0; i < HIST_N; i++) hist_clear_entry(hist[i]);
  return hist != NULL;
}

static void hist_clear_all() {
  for (int i = 0; i < HIST_N; i++) hist_clear_entry(hist[i]);
  hist_last_slot = -1;
}

// A "szeletek" között kimaradt helyek törlése, hogy a 24 órával ezelőtti adat ne maradjon bent
static void hist_advance(int32_t s) {
  if (hist_last_slot < 0 || s - hist_last_slot >= HIST_N) {
    for (int i = 0; i < HIST_N; i++) hist_clear_entry(hist[i]);
  } else {
    for (int32_t k = hist_last_slot + 1; k <= s; k++) hist_clear_entry(hist[k % HIST_N]);
  }
  hist_last_slot = s;
}

static bool hist_valid_slot(int32_t slot) {
  return hist && hist_last_slot >= 0 && slot <= hist_last_slot && slot > hist_last_slot - HIST_N;
}

// Induláskor (az NTP-idő megérkezése után) egyszer betöltjük a mentett előzményeket és a napi min/max értéket
static void hist_ensure_loaded() {
  if (hist_loaded || !hist) return;
  struct tm t;
  if (!get_local_tm(t)) return;
  hist_loaded = true;
  days_load();   // a napi összesítők (havi grafikon) az előzményfájltól függetlenül
  hist_clear_all();
  if (!ffat_ok) return;

  File f = FFat.open("/hist.bin", FILE_READ);
  if (!f) { LogOut.println("Előzmény: nincs mentett fájl (első indítás)"); return; }

  HistFile h;
  bool ok = f.read((uint8_t *)&h, sizeof(h)) == sizeof(h) && h.last_slot >= 0 && h.last_slot <= now_slot() + 1;
  const size_t fsz = f.size();
  if (ok && h.magic == HIST_MAGIC && h.count == (uint32_t)HIST_N && fsz == sizeof(HistFile) + sizeof(HistSample) * HIST_N) {
    ok = f.read((uint8_t *)hist, sizeof(HistSample) * HIST_N) == sizeof(HistSample) * HIST_N;
  } else if (ok && h.magic == HIST_MAGIC_V3 && h.count == (uint32_t)HIST_N && fsz == sizeof(HistFile) + sizeof(HistSampleV3) * HIST_N) {
    // 7 napos formátum a 2. hely CO2-je nélkül: elemenként átalakítjuk
    for (int i = 0; i < HIST_N && ok; i++) {
      HistSampleV3 o;
      ok = f.read((uint8_t *)&o, sizeof(o)) == sizeof(o);
      if (ok) hist_from_v3(o, hist[i]);
    }
    if (ok) { hist_dirty = true; LogOut.println("Előzmény: WSH3 formátum átalakítva WSH4-re"); }
  } else if (ok && h.count == (uint32_t)HIST_DAY &&
             ((h.magic == HIST_MAGIC_V2 && fsz == sizeof(HistFile) + sizeof(HistSampleV3) * HIST_DAY) ||
              (h.magic == HIST_MAGIC_V1 && fsz == sizeof(HistFile) + sizeof(HistSampleV1) * HIST_DAY))) {
    // régi, 24 órás formátum: a szeletek (slot % 288) helyéről átrakjuk az új, 7 napos gyűrűbe (slot % HIST_N)
    const bool v1 = h.magic == HIST_MAGIC_V1;
    for (int j = 0; j < HIST_DAY && ok; j++) {
      HistSample e;
      hist_clear_entry(e);
      if (v1) {
        HistSampleV1 o;
        ok = f.read((uint8_t *)&o, sizeof(o)) == sizeof(o);
        e.to = o.to; e.ti = o.ti; e.p = o.p; e.w = o.w; e.g = o.g; e.r = o.r;
      } else {
        HistSampleV3 o;
        ok = f.read((uint8_t *)&o, sizeof(o)) == sizeof(o);
        if (ok) hist_from_v3(o, e);
      }
      // a j. hely a (last_slot - 287 .. last_slot) tartomány azon szelete, amelyre slot % 288 == j
      int32_t slot = h.last_slot - ((h.last_slot - j) % HIST_DAY + HIST_DAY) % HIST_DAY;
      if (ok && slot >= 0) hist[slot % HIST_N] = e;
    }
    if (ok) { hist_dirty = true; LogOut.println("Előzmény: 24 órás formátum átalakítva 7 naposra"); }
  } else {
    ok = false;
  }
  f.close();
  if (!ok) {
    hist_clear_all();
    LogOut.println("Előzmény: a mentett fájl érvénytelen, új kezdődik");
    return;
  }

  hist_last_slot = h.last_slot;
  int key = (t.tm_year + 1900) * 400 + t.tm_yday;
  if (h.day_valid && h.day_key == key) {
    day_stats.tmin = h.tmin; day_stats.tmax = h.tmax; day_stats.yday = key; day_stats.valid = true;
  }
  LogOut.printf("Előzmény betöltve (utolsó minta: %d perce)\n", (int)((now_slot() - hist_last_slot) * 5));
  days_update();
  update_dashboard();
}

static void hist_save() {
  if (g_demo || !hist || !ffat_ok || !hist_loaded || !hist_dirty) return;   // a bemutató adatai nem kerülnek a flash-re
  File f = FFat.open("/hist.bin", FILE_WRITE);
  if (!f) { LogOut.println("Előzmény: mentés sikertelen (a fájl nem nyitható)"); return; }
  HistFile h;
  h.magic = HIST_MAGIC; h.count = HIST_N; h.last_slot = hist_last_slot;
  h.tmin = day_stats.tmin; h.tmax = day_stats.tmax; h.day_key = day_stats.yday; h.day_valid = day_stats.valid ? 1u : 0u;
  bool ok = f.write((const uint8_t *)&h, sizeof(h)) == sizeof(h) &&
            f.write((const uint8_t *)hist, sizeof(HistSample) * HIST_N) == sizeof(HistSample) * HIST_N;
  f.close();
  // A PlatformIO-s fordításban (XIP_FROM_PSRAM, LCD_RGB_ISR_IRAM_SAFE) a flash-írás nem zavarja a kijelzőt,
  // ezért itt nincs újraépítés; ha mégis zavar lenne, a képfolyam-figyelő (glitch_task) észleli és javítja.
  if (ok) LogOut.println("Előzmény mentve (flash)");
  days_update();
  days_save();
  if (ok) hist_dirty = false;
  else LogOut.println("Előzmény: mentés sikertelen (írási hiba)");
}

// Új adat érkezett: az aktuális 5 perces szelet frissítése (a lökésnél a szelet maximuma marad)
static void hist_record() {
  if (!hist) return;
  struct tm t;
  if (!get_local_tm(t)) return;
  hist_ensure_loaded();
  int32_t s = now_slot();
  if (hist_last_slot >= 0 && s < hist_last_slot) return;   // az óra visszaugrott, nem írunk
  if (s > hist_last_slot) hist_advance(s);

  HistSample &e = hist[s % HIST_N];
  e.to = to10(weather.temp_c);
  e.ti = room_fresh(0) ? to10(rooms[0].t) : HIST_NONE;
  e.t2 = room_fresh(1) ? to10(rooms[1].t) : HIST_NONE;
  e.t3 = room_fresh(2) ? to10(rooms[2].t) : HIST_NONE;
  e.c1 = (room_fresh(0) && rooms[0].has_co2) ? (int16_t)lroundf(rooms[0].co2) : HIST_NONE;
  e.c3 = (room_fresh(2) && rooms[2].has_co2) ? (int16_t)lroundf(rooms[2].co2) : HIST_NONE;
  e.c2 = (room_fresh(1) && rooms[1].has_co2) ? (int16_t)lroundf(rooms[1].co2) : HIST_NONE;
  e.p  = weather.baro_rel_hpa > 300.0f ? to10(weather.baro_rel_hpa) : HIST_NONE;
  e.w  = to10(weather.wind_kmh);
  int16_t g = to10(weather.gust_kmh);
  if (e.g == HIST_NONE || g > e.g) e.g = g;
  e.r  = to10(weather.rain_day_mm);
  hist_dirty = true;
}

static void hist_task() {
  static uint32_t last_save = 0, last_days = 0;
  hist_ensure_loaded();
  if (hist_loaded && (last_days == 0 || millis() - last_days >= 3600000UL)) { last_days = millis(); days_update(); }
  if (millis() - last_save >= HIST_SAVE_MS) {
    last_save = millis();
    hist_save();
  }
}

// ---------------------------------------------------------------------------
// Napi összesítők (a havi grafikonhoz): napi max/min kint, átlagnyomás, átlagszél, max lökés, napi eső,
// helyiségenkénti átlaghőmérséklet és max CO2. A 7 napos 5 perces előzményekből számoljuk (óránként és mentéskor),
// a régebbi napok a /days.bin fájlban maradnak meg (DAYS_N nap, kb. 13 hónap).
// ---------------------------------------------------------------------------
static DaySum *days = NULL;                      // DAYS_N elem a PSRAM-ban, a helye: dnum % DAYS_N
static bool days_dirty = false;

// A helyi dátum napsorszáma (1970. jan. 1. = 0, de az üres hely miatt +1)
static int32_t day_num(int y, int m, int d) {
  y -= m <= 2;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const int32_t yoe = y - era * 400;
  const int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468 + 1;
}

static int32_t today_num() {
  struct tm t;
  if (!get_local_tm(t)) return 0;
  return day_num(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
}

// Egy nap összesítője az 5 perces előzményekből (days_back: 0 = ma); false, ha nincs adat
static bool day_from_hist(int days_back, DaySum &out) {
  const int32_t start = midnight_slot(days_back), end = midnight_slot(days_back - 1);
  if (start < 0 || end <= start) return false;
  int32_t sum[M_COUNT] = {}, cnt[M_COUNT] = {};
  int16_t mx[M_COUNT], mn[M_COUNT];
  for (int i = 0; i < M_COUNT; i++) { mx[i] = INT16_MIN; mn[i] = INT16_MAX; }
  bool any = false;
  for (int32_t slot = start; slot < end; slot++) {
    if (!hist_valid_slot(slot)) continue;
    const HistSample &e = hist[slot % HIST_N];
    auto add = [&](int m, int16_t v) {
      if (v == HIST_NONE) return;
      sum[m] += v; cnt[m]++; if (v > mx[m]) mx[m] = v; if (v < mn[m]) mn[m] = v; any = true;
    };
    add(M_TMAX, e.to); add(M_TMIN, e.to); add(M_P, e.p); add(M_W, e.w); add(M_G, e.g); add(M_R, e.r);
    add(M_TI, e.ti); add(M_T2, e.t2); add(M_T3, e.t3); add(M_C1, e.c1); add(M_C3, e.c3); add(M_C2, e.c2);
  }
  if (!any) return false;
  for (int m = 0; m < M_COUNT; m++) {
    if (!cnt[m]) { out.v[m] = HIST_NONE; continue; }
    switch (m) {
      case M_TMAX: case M_G: case M_R: case M_C1: case M_C2: case M_C3: out.v[m] = mx[m]; break;   // legnagyobb (a napi eső a nap végére összegződik)
      case M_TMIN: out.v[m] = mn[m]; break;
      default:     out.v[m] = (int16_t)lroundf((float)sum[m] / cnt[m]); break;       // átlag
    }
  }
  return true;
}

// Az előzményekben lévő napok (ma és az előző 6) összesítőinek frissítése
static void days_update() {
  if (!days || !hist) return;
  const int32_t tn = today_num();
  if (!tn) return;
  for (int b = 0; b < 7; b++) {
    DaySum s;
    if (!day_from_hist(b, s)) continue;
    s.dnum = tn - b;
    DaySum &slot = days[s.dnum % DAYS_N];
    if (slot.dnum != s.dnum || memcmp(slot.v, s.v, sizeof(s.v)) != 0) { slot = s; days_dirty = true; }
  }
}

static const DaySum *day_get(int32_t dnum) {
  if (!days || dnum <= 0) return NULL;
  const DaySum &s = days[dnum % DAYS_N];
  return s.dnum == dnum ? &s : NULL;
}

static void days_load() {
  if (!days) {
    days = (DaySum *)heap_caps_calloc(DAYS_N, sizeof(DaySum), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!days) { LogOut.println("Napi összesítők: nincs elég memória"); return; }
  }
  if (!ffat_ok) return;
  File f = FFat.open("/days.bin", FILE_READ);
  if (!f) return;
  DaysFile h;
  bool ok = f.read((uint8_t *)&h, sizeof(h)) == sizeof(h) && (h.magic == DAYS_MAGIC || h.magic == DAYS_MAGIC_V1) &&
            h.count == (uint32_t)DAYS_N && f.size() == sizeof(h) + sizeof(DaySum) * DAYS_N &&
            f.read((uint8_t *)days, sizeof(DaySum) * DAYS_N) == sizeof(DaySum) * DAYS_N;
  f.close();
  if (ok && h.magic == DAYS_MAGIC_V1) {   // DAY1: a 2. hely CO2-je (M_C2) a régi kitöltő bájtok helyén van, üresre állítjuk
    for (int i = 0; i < DAYS_N; i++) days[i].v[M_C2] = HIST_NONE;
    days_dirty = true;
  }
  if (!ok) { memset(days, 0, sizeof(DaySum) * DAYS_N); LogOut.println("Napi összesítők: a fájl érvénytelen, új kezdődik"); }
}

static void days_save() {
  if (g_demo || !days || !ffat_ok || !days_dirty) return;
  File f = FFat.open("/days.bin", FILE_WRITE);
  if (!f) return;
  DaysFile h = {DAYS_MAGIC, (uint32_t)DAYS_N};
  bool ok = f.write((const uint8_t *)&h, sizeof(h)) == sizeof(h) &&
            f.write((const uint8_t *)days, sizeof(DaySum) * DAYS_N) == sizeof(DaySum) * DAYS_N;
  f.close();
  if (ok) days_dirty = false;
}

// Változási sebesség: a legrégebbi, legalább min_slots és legfeljebb max_slots korú mintából,
// max_slots időtartamra átszámolva (field: 0 = kint, 1 = bent, 2 = nyomás)
static bool hist_rate(int field, int max_slots, int min_slots, float cur, float *rate) {
  int32_t ns = now_slot();
  for (int back = max_slots; back >= min_slots; back--) {
    int32_t slot = ns - back;
    if (!hist_valid_slot(slot)) continue;
    const HistSample &e = hist[slot % HIST_N];
    int16_t v = (field == 0) ? e.to : (field == 1 ? e.ti : e.p);
    if (v == HIST_NONE) continue;
    *rate = (cur - v / 10.0f) * (float)max_slots / (float)back;
    return true;
  }
  return false;
}

// ---- trendnyilak: öt állás (nagyon emelkedik, emelkedik, állandó, csökken, nagyon csökken) ----
constexpr float PRESS_T1 = 1.0f, PRESS_T2 = 3.5f;   // hPa / 3 óra: állandó alatta, gyors fölötte
constexpr float OTEMP_T1 = 0.5f, OTEMP_T2 = 2.0f;   // °C / óra (kint)
constexpr float ITEMP_T1 = 0.3f, ITEMP_T2 = 1.0f;   // °C / óra (bent)
constexpr int   ARROW_NONE = 50;                    // még nincs elég adat: a nyíl rejtve marad

static int trend_level(float r, float t1, float t2) {
  float a = fabsf(r);
  int l = a < t1 ? 0 : (a < t2 ? 1 : 2);
  return r < 0 ? -l : l;
}

static lv_obj_t *arrow_create(lv_obj_t *parent, int x, int y) {
  void *buf = heap_caps_aligned_alloc(64, ARW * ARW * 4 + 256, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!buf) return NULL;
  lv_obj_t *cv = lv_canvas_create(parent);
  lv_canvas_set_buffer(cv, buf, ARW, ARW, LV_COLOR_FORMAT_ARGB8888);
  lv_canvas_fill_bg(cv, lv_color_black(), LV_OPA_TRANSP);
  lv_obj_set_pos(cv, x, y);
  lv_obj_set_hidden(cv, true);
  return cv;
}

// A nyilat pixelenként számoljuk (3x3 átlagolás): a szög = szint * 45 fok, 0 = jobbra, +2 = fel
static void arrow_set(int idx, int level, uint32_t col) {
  lv_obj_t *cv = arrow_cv[idx];
  if (!cv || level == arrow_level[idx]) return;
  arrow_level[idx] = level;

  if (level == ARROW_NONE) { lv_obj_set_hidden(cv, true); return; }

  lv_color_t c = lv_color_hex(level == 0 ? COL_GREY : col);
  const float ang = level * 3.14159265f / 4.0f;
  const float ca = cosf(ang), sa = sinf(ang);
  const float k = ARW / 36.0f, ctr = ARW / 2.0f;

  for (int y = 0; y < ARW; y++) {
    for (int x = 0; x < ARW; x++) {
      int cov = 0;
      for (int sy = 0; sy < 3; sy++) {
        for (int sx = 0; sx < 3; sx++) {
          float dx = (x + (sx + 0.5f) / 3.0f - ctr) / k;
          float dy = (ctr - (y + (sy + 0.5f) / 3.0f)) / k;
          float u = dx * ca + dy * sa;
          float v = -dx * sa + dy * ca;
          float av = v < 0 ? -v : v;
          bool shaft = (u >= -12.0f && u <= 4.0f && av <= 2.6f);
          bool head  = (u > 4.0f && u <= 14.0f && av <= (14.0f - u) * 0.75f);
          if (shaft || head) cov++;
        }
      }
      lv_canvas_set_px(cv, x, y, c, (lv_opa_t)(cov * 255 / 9));
    }
  }
  lv_obj_set_hidden(cv, false);
  lv_obj_invalidate(cv);
}

static void update_trends() {
  float r;
  int l;

  l = hist_rate(2, 36, 12, weather.baro_rel_hpa, &r) && weather.baro_rel_hpa > 300.0f ? trend_level(r, PRESS_T1, PRESS_T2) : ARROW_NONE;
  arrow_set(ARR_PRESS, l, 0xE8E8E8);
  l = hist_rate(0, 12, 6, weather.temp_c, &r) ? trend_level(r, OTEMP_T1, OTEMP_T2) : ARROW_NONE;
  arrow_set(ARR_OTEMP, l, COL_ORANGE);
}
