// Bemutató mód: konzolok (és akár WiFi) nélkül kitalált, de életszerű adatokkal tölti fel a valódi oldalakat (főoldal,
// előrejelzés, grafikonok napi/heti/havi nézetben, részletes adatok), és magától lapoz. Bemutatóhoz, ahol nincs érzékelő.
// A kijelző Beállítások / Rendszer fülén kapcsolható (NVS "cfg"/"demo"), újraindítással. Közben a valódi előzményfájlokat
// nem írja (hist_save, days_save), nem kér előrejelzést, nem küld értesítést, és a konzolok küldéseit nem használja.
// Az adatok az időből számolódnak (napszak, évszak, lassú hullámok), így a grafikonok és a pillanatnyi értékek egyeznek.
// (Az idojaras.ino része: az Arduino IDE és a PlatformIO a fájlokat ábécésorrendben, egy programmá fűzi össze.)

constexpr uint32_t DEMO_TICK_MS = 16000;                    // ilyen gyakran "érkezik" új adat, mint egy konzoltól
constexpr uint32_t DEMO_FC_MS = 30UL * 60UL * 1000UL;       // az előrejelzés újraszámolása (az órás rész az idővel halad)
constexpr double DEMO_LAT = 48.1035, DEMO_LON = 20.7784;    // a bemutató helye, ha nincs beállítva (Miskolc)
constexpr float DEMO_PI = 3.14159265f, DEMO_PI2 = 6.2831853f;
// lapozás: oldal, grafikon-nézet (0: nap, 1: hét, 2: hónap), másodperc
static const int DEMO_STEPS[][3] = {
  {PAGE_DASH, 0, 20}, {PAGE_ENERGY, 0, 12}, {PAGE_FC, 0, 12}, {PAGE_HIST, 0, 12}, {PAGE_HIST2, 0, 12},
  {PAGE_HIST, 1, 10}, {PAGE_HIST2, 1, 10}, {PAGE_HIST, 2, 10}, {PAGE_DETAILS, 0, 12},
};
constexpr int DEMO_STEP_N = sizeof(DEMO_STEPS) / sizeof(DEMO_STEPS[0]);
static int demo_step = 0;
static uint32_t demo_last_tick = 0, demo_last_fc = 0;

// determinisztikus "zaj" (-1..1): ugyanarra az időre mindig ugyanaz, így az újraszámolt grafikon nem ugrál
static float demo_noise(uint32_t a, uint32_t b) {
  uint32_t h = a * 2654435761u ^ (b + 0x9E3779B9u) * 40503u;
  h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
  return (float)(h & 0xFFFF) / 32767.5f - 1.0f;
}

// harang alakú csúcs a nap c. órája körül (éjfélen át is)
static float demo_bump(float h, float c, float w) {
  float d = fabsf(h - c);
  if (d > 12.0f) d = 24.0f - d;
  return expf(-(d / w) * (d / w));
}

// esős nap: hetente legalább egy, és véletlenszerűen további napok; délután 4 órás eső
static bool demo_rain_day(int32_t dn) { return dn % 7 == 3 || demo_noise((uint32_t)dn, 77) > 0.6f; }

static float demo_rain_rate(int32_t dn, float h) {   // mm/h
  if (!demo_rain_day(dn) || h < 13.5f || h > 17.5f) return 0.0f;
  return (1.5f + 1.25f * (demo_noise((uint32_t)dn, 78) + 1.0f)) * sinf((h - 13.5f) * DEMO_PI / 4.0f);
}

static int32_t demo_dn(const struct tm &lt) { return day_num(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday); }

// Az értékek egy időpontra (t: epoch, lt: ugyanez helyi időben)
static void demo_eval(time_t t, const struct tm &lt, DemoVals &v) {
  const int32_t dn = demo_dn(lt);
  const float h = lt.tm_hour + lt.tm_min / 60.0f + lt.tm_sec / 3600.0f;
  const float x = (float)(dn % 10000) + h / 24.0f;   // folytonos napszámláló a lassú hullámokhoz
  const float doy = (float)lt.tm_yday;
  const uint32_t s = (uint32_t)(t / 300);
  const float season = sinf(DEMO_PI2 * (doy - 80.0f) / 365.25f);   // tavaszi napéjegyenlőségtől: nyáron 1, télen -1
  const float day_wave = sinf(DEMO_PI2 * (h - 9.0f) / 24.0f);       // 15 órakor a legmelegebb, 3 órakor a leghidegebb

  v.rate = demo_rain_rate(dn, h);
  const bool rain = v.rate > 0.0f;
  const float mean = 11.0f - 10.5f * cosf(DEMO_PI2 * (doy - 20.0f) / 365.25f);
  const float amp = 4.0f + 1.5f * (1.0f + sinf(x * 1.7f));
  const float off = 2.5f * sinf(x * 0.9f) + 1.5f * sinf(x * 2.3f);
  v.to = mean + off + amp * day_wave - (rain ? 2.0f : 0.0f) + 0.15f * demo_noise(s, 1);
  v.rh = constrain(70.0f - 22.0f * day_wave + (rain ? 18.0f : 0.0f) + 2.0f * demo_noise(s, 2), 25.0f, 99.0f);
  v.p = 1014.0f + 7.0f * sinf(x * DEMO_PI2 / 4.3f) + 3.0f * sinf(x * DEMO_PI2 / 1.7f) + 0.1f * demo_noise(s, 3);
  const float wd = sinf(DEMO_PI * (h - 8.0f) / 12.0f);
  v.w = fmaxf(0.0f, 4.0f + 7.0f * fmaxf(0.0f, wd) + 2.0f * (1.0f + sinf(x * 1.3f)) + 1.5f * demo_noise(s, 4));
  v.g = v.w * 1.5f + 2.5f + 2.0f * fabsf(demo_noise(s, 5));
  v.dir = fmodf(270.0f + 40.0f * sinf(x * 0.7f) + 15.0f * demo_noise(s, 6) + 360.0f, 360.0f);
  const float daylen = 12.0f + 3.8f * season, rise = 11.8f - daylen / 2.0f;
  const float peak = (450.0f + 400.0f * (0.5f + 0.5f * season)) * (0.75f + 0.25f * sinf(x * 3.1f));
  v.sol = (h > rise && h < rise + daylen) ? peak * sinf(DEMO_PI * (h - rise) / daylen) * (rain ? 0.3f : 1.0f) : 0.0f;

  v.ti[0] = 22.3f + 0.6f * sinf(DEMO_PI2 * (h - 14.0f) / 24.0f) + 0.3f * sinf(x * 1.1f) + 0.05f * demo_noise(s, 7);
  v.ti[1] = 21.4f + 0.9f * sinf(DEMO_PI2 * (h - 15.0f) / 24.0f) + 0.2f * sinf(x * 0.8f) + 0.05f * demo_noise(s, 8);
  v.ti[2] = 20.2f + 0.5f * sinf(DEMO_PI2 * (h - 18.0f) / 24.0f) + 0.2f * sinf(x * 1.4f) + 0.05f * demo_noise(s, 9);
  v.hi[0] = 47.0f + 4.0f * sinf(DEMO_PI2 * (h - 20.0f) / 24.0f);
  v.hi[1] = 44.0f + 3.0f * sinf(DEMO_PI2 * (h - 12.0f) / 24.0f);
  v.hi[2] = 50.0f + 6.0f * demo_bump(h, 6.0f, 3.0f);
  // CO2: a nappaliban este és reggel magasabb, az irodában napközben, a hálószobában éjjel nő, reggel szellőztetnek
  v.co2[0] = 520.0f + 380.0f * demo_bump(h, 19.5f, 3.0f) + 160.0f * demo_bump(h, 8.0f, 1.5f) + 10.0f * demo_noise(s, 10);
  v.co2[1] = 480.0f + 330.0f * demo_bump(h, 11.0f, 3.0f) + 200.0f * demo_bump(h, 15.0f, 2.0f) + 10.0f * demo_noise(s, 11);
  float r = 0.0f;
  if (h >= 22.0f) r = (h - 22.0f) / 8.5f;
  else if (h < 6.5f) r = (h + 2.0f) / 8.5f;
  else if (h < 7.5f) r = 1.0f - (h - 6.5f);
  v.co2[2] = 460.0f + 780.0f * r + 10.0f * demo_noise(s, 12);
}

// helyi dél a mai napon (a napok léptetéséhez: +-1 óra nyári időszámítás mellett is ugyanaz a nap marad)
static time_t demo_today_noon() {
  time_t now = time(nullptr);
  struct tm lt;
  localtime_r(&now, &lt);
  lt.tm_hour = 12; lt.tm_min = 0; lt.tm_sec = 0; lt.tm_isdst = -1;
  return mktime(&lt);
}

// Az előzmények (7 nap, 5 perces minták) és a napi összesítők (havi grafikon) kitöltése
static void demo_generate() {
  if (!hist || time(nullptr) < 1600000000) return;
  const int32_t ns = now_slot(), today = today_num();
  hist_clear_all();
  float rain_acc = 0.0f, tmin = 1e9f, tmax = -1e9f, co2max[ROOM_COUNT] = {};
  int32_t cur_dn = -1;
  for (int32_t s = ns - HIST_N + 1; s <= ns; s++) {
    const time_t t = (time_t)s * 300;
    struct tm lt;
    localtime_r(&t, &lt);
    DemoVals v;
    demo_eval(t, lt, v);
    const int32_t dn = demo_dn(lt);
    if (dn != cur_dn) { cur_dn = dn; rain_acc = 0.0f; }
    rain_acc += v.rate / 12.0f;
    HistSample &e = hist[s % HIST_N];
    e.to = to10(v.to); e.p = to10(v.p); e.w = to10(v.w); e.g = to10(v.g); e.r = to10(rain_acc);
    e.ti = rooms[0].match[0] ? to10(v.ti[0]) : HIST_NONE;
    e.t2 = rooms[1].match[0] ? to10(v.ti[1]) : HIST_NONE;
    e.t3 = rooms[2].match[0] ? to10(v.ti[2]) : HIST_NONE;
    e.c1 = rooms[0].match[0] ? (int16_t)lroundf(v.co2[0]) : HIST_NONE;
    e.c2 = rooms[1].match[0] ? (int16_t)lroundf(v.co2[1]) : HIST_NONE;
    e.c3 = rooms[2].match[0] ? (int16_t)lroundf(v.co2[2]) : HIST_NONE;
    if (dn == today) {
      tmin = fminf(tmin, v.to); tmax = fmaxf(tmax, v.to);
      for (int i = 0; i < ROOM_COUNT; i++) co2max[i] = fmaxf(co2max[i], v.co2[i]);
    }
  }
  hist_last_slot = ns;
  hist_loaded = true;
  hist_dirty = false;
  struct tm lt;
  get_local_tm(lt);
  day_stats.tmin = tmin; day_stats.tmax = tmax; day_stats.yday = (lt.tm_year + 1900) * 400 + lt.tm_yday; day_stats.valid = tmin < tmax;
  co2_max_key = day_stats.yday;
  for (int i = 0; i < ROOM_COUNT; i++) rooms[i].co2_max = co2max[i];

  // napi összesítők a régebbi napokra (a 7 napos előzményből a days_update() számolja a többit)
  if (!days) days = (DaySum *)heap_caps_calloc(DAYS_N, sizeof(DaySum), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (days) {
    memset(days, 0, sizeof(DaySum) * DAYS_N);
    const time_t noon = demo_today_noon();
    for (int b = 7; b < DAYS_N; b++) {
      const time_t tn = noon - (time_t)b * 86400;
      struct tm dl;
      localtime_r(&tn, &dl);
      float mx = -1e9f, mn = 1e9f, ps = 0, ws = 0, gx = 0, rs = 0, ts[ROOM_COUNT] = {}, cx[ROOM_COUNT] = {};
      for (int k = 0; k < 24; k++) {   // óránként
        struct tm hl = dl;
        hl.tm_hour = k;
        DemoVals v;
        demo_eval(tn + (k - 12) * 3600, hl, v);
        mx = fmaxf(mx, v.to); mn = fminf(mn, v.to); ps += v.p; ws += v.w; gx = fmaxf(gx, v.g); rs += v.rate;
        for (int i = 0; i < ROOM_COUNT; i++) { ts[i] += v.ti[i]; cx[i] = fmaxf(cx[i], v.co2[i]); }
      }
      DaySum &d = days[(today - b) % DAYS_N];
      d.dnum = today - b;
      d.v[M_TMAX] = to10(mx); d.v[M_TMIN] = to10(mn); d.v[M_P] = to10(ps / 24); d.v[M_W] = to10(ws / 24);
      d.v[M_G] = to10(gx); d.v[M_R] = to10(rs);
      const int mt[ROOM_COUNT] = {M_TI, M_T2, M_T3}, mc[ROOM_COUNT] = {M_C1, M_C2, M_C3};
      for (int i = 0; i < ROOM_COUNT; i++) {
        d.v[mt[i]] = rooms[i].match[0] ? to10(ts[i] / 24) : HIST_NONE;
        d.v[mc[i]] = rooms[i].match[0] ? (int16_t)lroundf(cx[i]) : HIST_NONE;
      }
    }
    days_update();
  }
  LogOut.println("Bemutató: előzmények kitöltve (7 nap, és havi összesítők)");
}

// Előrejelzés: 4 nap és 24 óra a mostani órától
static void demo_forecast() {
  const time_t now = time(nullptr);
  if (now < 1600000000) return;
  demo_last_fc = millis();
  Forecast f = {};
  f.valid = true;
  f.n = FC_DAYS;
  const time_t noon = demo_today_noon();
  static const int CODES[4] = {0, 1, 2, 3};
  for (int i = 0; i < FC_DAYS; i++) {
    const time_t tn = noon + (time_t)i * 86400;
    struct tm dl;
    localtime_r(&tn, &dl);
    const int32_t dn = demo_dn(dl);
    FcDay &d = f.day[i];
    d.y = dl.tm_year + 1900; d.m = dl.tm_mon + 1; d.d = dl.tm_mday;
    float mx = -1e9f, mn = 1e9f, wx = 0, rs = 0;
    for (int k = 0; k < 24; k++) {
      struct tm hl = dl;
      hl.tm_hour = k;
      DemoVals v;
      demo_eval(tn + (k - 12) * 3600, hl, v);
      mx = fmaxf(mx, v.to); mn = fminf(mn, v.to); wx = fmaxf(wx, v.w); rs += v.rate;
    }
    d.tmax = mx; d.tmin = mn; d.wind = wx; d.psum = rs;
    d.pprob = demo_rain_day(dn) ? 85.0f : 10.0f + 7.5f * (demo_noise((uint32_t)dn, 80) + 1.0f);
    d.code = demo_rain_day(dn) ? (rs > 8.0f ? 63 : 61) : CODES[constrain((int)((demo_noise((uint32_t)dn, 79) + 1.0f) * 2.0f), 0, 3)];
  }
  f.nh = FC_HOURS;
  const time_t t0 = now - now % 3600;
  for (int k = 0; k < FC_HOURS; k++) {
    const time_t t = t0 + (time_t)k * 3600;
    struct tm hl;
    localtime_r(&t, &hl);
    DemoVals v;
    demo_eval(t, hl, v);
    const int32_t dn = demo_dn(hl);
    FcHour &h = f.hour[k];
    h.y = hl.tm_year + 1900; h.m = hl.tm_mon + 1; h.d = hl.tm_mday; h.h = hl.tm_hour;
    h.t = v.to; h.mm = v.rate;
    h.pp = v.rate > 0.0f ? 90.0f : (demo_rain_day(dn) && hl.tm_hour >= 11 && hl.tm_hour < 13) ? 60.0f : 5.0f;
    h.code = v.rate > 0.0f ? 61 : demo_rain_day(dn) ? 3 : CODES[constrain((int)((demo_noise((uint32_t)dn, 79) + 1.0f) * 2.0f), 0, 3)];
  }
  f.fetched = now;
  fc = f;
  fc_err[0] = 0;
  fc_update_ui();
  update_dashboard();
}

// Eső összege (mm) t1..t2 között, 5 perces lépésekkel
static float demo_rain_sum(time_t t1, time_t t2) {
  float sum = 0.0f;
  for (time_t t = t1 - t1 % 300; t < t2; t += 300) {
    struct tm lt;
    localtime_r(&t, &lt);
    sum += demo_rain_rate(demo_dn(lt), lt.tm_hour + lt.tm_min / 60.0f) / 12.0f;
  }
  return sum;
}

// Egy "konzolküldés": a pillanatnyi értékek a weather és a rooms[] mezőibe, mint a handle_ecowitt_post()-ban
static void demo_tick() {
  const time_t now = time(nullptr);
  if (now < 1600000000) return;
  struct tm lt;
  localtime_r(&now, &lt);
  DemoVals v;
  demo_eval(now, lt, v);
  WeatherData &w = weather;
  const float jit = demo_noise((uint32_t)now, 13);   // a szél élénkebb legyen, mint az 5 perces minta
  w.temp_c = v.to; w.humidity = v.rh;
  const float es = 0.6108f * expf(17.27f * v.to / (v.to + 237.3f));
  w.vpd_kpa = es * (1.0f - v.rh / 100.0f);
  w.wind_kmh = fmaxf(0.0f, v.w + 1.2f * jit);
  w.gust_kmh = fmaxf(w.wind_kmh, v.g + 1.5f * jit);
  w.wind_dir = fmodf(v.dir + 10.0f * jit + 360.0f, 360.0f);
  w.wind_dir_avg10m = v.dir;
  float gmax = w.gust_kmh;   // a mai legnagyobb lökés az előzményekből
  for (int32_t s = midnight_slot(0); s <= hist_last_slot; s++)
    if (hist_valid_slot(s) && hist[s % HIST_N].g != HIST_NONE) gmax = fmaxf(gmax, hist[s % HIST_N].g / 10.0f);
  w.max_gust_kmh = gmax;

  const time_t midnight = now - (lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec);
  w.raining = v.rate > 0.0f ? 1.0f : 0.0f;
  w.rain_rate_mm = v.rate;
  w.rain_day_mm = demo_rain_sum(midnight, now);
  w.rain_event_mm = w.rain_day_mm;
  w.rain_hour_mm = demo_rain_sum(now - 3600, now);
  w.rain_24h_mm = demo_rain_sum(now - 86400, now);
  // heti, havi, éves összeg a napi összesítőkből (+ a mai nap)
  const int32_t today = today_num();
  float week = w.rain_day_mm, month = w.rain_day_mm, year = w.rain_day_mm;
  for (int b = 1; b < DAYS_N; b++) {
    const DaySum *d = day_get(today - b);
    if (!d || d->v[M_R] == HIST_NONE) continue;
    const float r = d->v[M_R] / 10.0f;
    if (b < 7) week += r;
    if (b < lt.tm_mday) month += r;
    if (b <= lt.tm_yday) year += r;
  }
  w.rain_week_mm = week; w.rain_month_mm = month; w.rain_year_mm = year;

  w.solar_wm2 = v.sol;
  w.uv_index = roundf(v.sol / 110.0f);
  w.baro_rel_hpa = v.p;
  w.baro_abs_hpa = v.p - 13.0f;
  w.wh90_batt_v = 3.12f; w.ws90_cap_v = 5.3f;
  w.runtime_s = 19.0f * 86400.0f + millis() / 1000.0f;
  w.console_heap_b = 120000; w.interval_s = 16; w.ws90_ver = 157;
  strlcpy(w.stationtype, "DEMO", sizeof(w.stationtype));
  strlcpy(w.freq, "868M", sizeof(w.freq));
  struct tm ut;
  gmtime_r(&now, &ut);
  strftime(w.dateutc, sizeof(w.dateutc), "%Y-%m-%d %H:%M:%S", &ut);

  static const float BATT[ROOM_COUNT] = {4.42f, 0.0f, 2.96f};   // az iroda konzolja (GW3000) akku nélküli
  for (int i = 0; i < ROOM_COUNT; i++) {
    Room &r = rooms[i];
    if (!r.match[0]) { r.last_ms = 0; continue; }
    r.t = v.ti[i]; r.h = v.hi[i]; r.co2 = v.co2[i]; r.co2_24h = 650.0f + 60.0f * i;
    r.has_co2 = true;
    r.has_batt = BATT[i] > 0.0f;
    r.batt = BATT[i];
    r.last_ms = millis();
  }
  w.temp_in_c = rooms[0].t; w.humidity_in = rooms[0].h; w.co2 = rooms[0].co2; w.co2_24h = rooms[0].co2_24h;
  w.console_batt_v = rooms[0].batt;
  w.has_data = true;
  w.last_rx_ms = millis();

  hist_record();
  update_day_stats();
  update_ui_labels();
  update_status_bar();
}

// Az Energia oldal kitalált adatai (a felhasználó kérése, 2026.11.08.: bemutató módban is legyen rajta adat).
// A valódi lekérés (en_worker) bemutató módban nem indul el; ugyanúgy az időből számolunk, mint a demo_eval()-nál, és a
// napelem termelését a demo_eval() napsugárzás-görbéjéből (v.sol) vesszük, hogy egy felhős/esős nap a termelést is
// visszafogja. A bojler a reggeli Eco-ablakban (5:00-5:30) és napelem-többletnél fűt, mint az igazi automatikánál.
constexpr float DEMO_EN_PV_SCALE = 4.0f;           // v.sol -> napelem teljesítmény (egy kb. 4-5 kWp rendszernek megfelelő)
constexpr float DEMO_EN_LOAD_BASE_W = 280.0f;      // a ház alapterhelése reggeli/esti csúccsal
constexpr float DEMO_EN_BOIL_W = 2000.0f;
constexpr float DEMO_EN_SURPLUS_ON_W = 1200.0f;    // ennyi napelem-többlet fölött fűt a bojler
constexpr float DEMO_EN_BASE_IMP = 18342.0f, DEMO_EN_BASE_EXP = 6120.0f, DEMO_EN_BASE_BOIL = 7946.0f;   // kezdő (élethosszi) mérőállások
constexpr uint32_t DEMO_EN_TICK_MS = 4000;         // ilyen gyakran frissül (a valódi P1 2 mp-enként, de itt elég)
static uint32_t demo_en_last_tick = 0;

static float demo_en_load_w(float h, uint32_t s) {
  return DEMO_EN_LOAD_BASE_W + 900.0f * demo_bump(h, 8.0f, 1.5f) + 1300.0f * demo_bump(h, 19.0f, 2.5f) + 60.0f * demo_noise(s, 20);
}

// egy időpont adatai: napelem, fogyasztás, bojler, hálózat (W; a hálózatnál + vétel, - visszatáplálás)
static void demo_en_eval(time_t t, const struct tm &lt, float &pv_w, float &load_w, float &boil_w, float &grid_w) {
  DemoVals v;
  demo_eval(t, lt, v);
  const float h = lt.tm_hour + lt.tm_min / 60.0f + lt.tm_sec / 3600.0f;
  const uint32_t s = (uint32_t)(t / 300);
  pv_w = fmaxf(0.0f, v.sol * DEMO_EN_PV_SCALE);
  load_w = demo_en_load_w(h, s);
  const bool morning = h >= 5.0f && h < 5.5f;
  const bool solar = pv_w - load_w > DEMO_EN_SURPLUS_ON_W;
  boil_w = (morning || solar) ? DEMO_EN_BOIL_W + 60.0f * demo_noise(s, 21) : 2.0f;
  grid_w = load_w + boil_w - pv_w;
}

static int32_t demo_en_key(const struct tm &lt) { return (lt.tm_year + 1900) * 400 + lt.tm_yday; }

// A mai nap diagramja (en_pv) és a mai/összes mérőállás kitöltése/frissítése, éjféltől a jelen percig (a valódihoz
// hasonlóan az azutáni szeleteket üresen hagyja). Induláskor, időugrás után és négy másodpercenként is újraszámolja
// (az ESP32-nek ez semmiség, és így a kijelzett adat sosem marad el a pillanatnyitól).
static void demo_energy_generate() {
  const time_t now = time(nullptr);
  if (now < 1600000000) return;
  struct tm lt;
  localtime_r(&now, &lt);
  const int32_t key = demo_en_key(lt);
  en_pv_day(key);
  const int ns = constrain((lt.tm_hour * 60 + lt.tm_min) / 5, 0, EN_PV_N - 1);
  const time_t midnight = now - (lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec);
  double imp_sum = 0, exp_sum = 0, boil_sum = 0, pv_sum = 0;
  for (int s = 0; s <= ns; s++) {
    const time_t t = midnight + (time_t)s * 300;
    struct tm slt;
    localtime_r(&t, &slt);
    float pv_w, load_w, boil_w, grid_w;
    demo_en_eval(t, slt, pv_w, load_w, boil_w, grid_w);
    en_pv.w[s] = (uint16_t)constrain((int)lroundf(pv_w), 0, 65000);
    en_pv.net[s] = (int16_t)constrain((int)lroundf(grid_w), -32000, 32000);
    en_pv.boil[s] = (int16_t)constrain((int)lroundf(boil_w), 0, 32000);
    pv_sum += pv_w * (5.0 / 60.0 / 1000.0);
    boil_sum += boil_w * (5.0 / 60.0 / 1000.0);
    if (grid_w > 0) imp_sum += grid_w * (5.0 / 60.0 / 1000.0); else exp_sum += -grid_w * (5.0 / 60.0 / 1000.0);
  }
  en_pv_last = ns;
  en_pv_ver++;
  en_day = {key, 0, DEMO_EN_BASE_IMP, DEMO_EN_BASE_EXP, DEMO_EN_BASE_BOIL};

  float pv_now, load_now, boil_now, grid_now;
  demo_en_eval(now, lt, pv_now, load_now, boil_now, grid_now);
  const float h_now = lt.tm_hour + lt.tm_min / 60.0f + lt.tm_sec / 3600.0f;
  const bool morning_now = h_now >= 5.0f && h_now < 5.5f;
  const bool solar_now = pv_now - load_now > DEMO_EN_SURPLUS_ON_W;
  const bool heat_now = morning_now || solar_now;
  char upd[24];
  struct tm gt = lt;
  gt.tm_min -= gt.tm_min % 5; gt.tm_sec = 0;
  strftime(upd, sizeof(upd), "%Y-%m-%d %H:%M:%S", &gt);

  portENTER_CRITICAL(&en_mux);
  EnData &d = en_data;
  d.p1_ms = d.sk_ms = d.gw_ms = d.bl_ms = d.rb_ms = millis();
  d.rb_auto = heat_now; d.rb_on_w = (int)DEMO_EN_SURPLUS_ON_W; d.rb_t0 = 48; d.rb_t1 = 75; d.rb_err[0] = 0;
  d.grid_w = grid_now;
  d.u[0] = 235.0f + 2.0f * demo_noise((uint32_t)(now / 300), 22);
  d.u[1] = 236.0f + 2.0f * demo_noise((uint32_t)(now / 300), 23);
  d.u[2] = 234.0f + 2.0f * demo_noise((uint32_t)(now / 300), 24);
  d.i[0] = fabsf(grid_now) / 230.0f; d.i[1] = 0.3f; d.i[2] = 0.2f;
  d.imp_kwh = DEMO_EN_BASE_IMP + (float)imp_sum; d.exp_kwh = DEMO_EN_BASE_EXP + (float)exp_sum;
  d.tariff = (lt.tm_hour >= 6 && lt.tm_hour < 22) ? 1 : 2;
  d.boil_w = boil_now; d.boil_kwh = DEMO_EN_BASE_BOIL + (float)boil_sum;
  d.pv_w = pv_now; d.pv_day = (float)pv_sum; d.pv_month = d.pv_day * lt.tm_mday * 0.82f; d.pv_year = d.pv_day * (lt.tm_yday + 1) * 0.78f;
  strlcpy(d.gw_upd, upd, sizeof(d.gw_upd));
  d.plant_id = 1; d.gw_remote = false;
  d.boil_temp = heat_now ? 68.0f : 44.0f; d.boil_water_pct = heat_now ? 85.0f : 55.0f;
  strlcpy(d.boil_state, heat_now ? "heating" : "warm", sizeof(d.boil_state));
  strlcpy(d.boil_mode, "manmenu", sizeof(d.boil_mode));
  d.boil_set = solar_now ? 75.0f : 48.0f; d.boil_sw = 1; d.boil_fault = 0;
  d.p1_err[0] = d.sk_err[0] = d.gw_err[0] = d.bl_err[0] = 0;
  portEXIT_CRITICAL(&en_mux);
}

// a loop()-ból (demo_task hívja): az Energia oldal adatainak ébren tartása, nem csak a lapozás 16 mp-es ütemében
static void demo_energy_task() {
  if (millis() - demo_en_last_tick < DEMO_EN_TICK_MS && demo_en_last_tick != 0) return;
  demo_en_last_tick = millis();
  demo_energy_generate();
}

// a fordítás ideje (helyi idő), ha nincs pontos idő (WiFi nélkül)
static time_t demo_build_time() {
  static const char MONTHS[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  const char mon[4] = {__DATE__[0], __DATE__[1], __DATE__[2], 0};
  const char *p = strstr(MONTHS, mon);
  struct tm t = {};
  t.tm_mon = p ? (int)(p - MONTHS) / 3 : 0;
  t.tm_mday = atoi(__DATE__ + 4);
  t.tm_year = atoi(__DATE__ + 7) - 1900;
  t.tm_hour = atoi(__TIME__);
  t.tm_min = atoi(__TIME__ + 3);
  t.tm_isdst = -1;
  return mktime(&t);
}

// Az app_setup()-ból, a WiFi-kapcsolódás helyett: a bemutató indítása
static void demo_start() {
  LogOut.println("Bemutató mód: kitalált adatok (a valódi előzmények nem változnak)");
  if (!cfg.has_loc) { cfg.has_loc = true; cfg.lat = DEMO_LAT; cfg.lon = DEMO_LON; demo_loc = true; }
  if (strcmp(cfg.tz, TZ_DEFAULT) == 0) { strlcpy(cfg.tz, "CET-1CEST,M3.5.0,M10.5.0/3", sizeof(cfg.tz)); cfg_apply_tz(); }
  // ha van mentett WiFi, a háttérben csatlakozik (kezelőoldal, OTA, pontos idő), de nem várunk rá, és nincs beállító portál
  if (wifiBeginNoWait()) configTzTime(cfg.tz, "pool.ntp.org", "time.google.com");
  if (time(nullptr) < 1600000000) {
    struct timeval tv = {demo_build_time(), 0};
    settimeofday(&tv, NULL);
  }
  demo_generate();
  demo_forecast();
  demo_tick();
  demo_energy_generate();
  demo_last_tick = millis();
}

// automatikus lapozás (érintés után a lépés ideje újraindul; nyitott Beállítások, fényerő panel vagy nagy grafikon alatt áll)
static void demo_rotate() {
  if (settings_is_open() || bl_panel_open() || fs_idx >= 0) return;
  if (millis() - page_since < (uint32_t)DEMO_STEPS[demo_step][2] * 1000UL) return;
  demo_step = (demo_step + 1) % DEMO_STEP_N;
  const int p = DEMO_STEPS[demo_step][0], m = DEMO_STEPS[demo_step][1];
  const bool hist_page = p == PAGE_HIST || p == PAGE_HIST2;
  if (hist_page && m != ch_mode) ch_set_mode(m);
  if (p != current_page) {
    show_page(p);
  } else {
    if (hist_page) hist_update_charts();
    page_since = millis();
  }
}

// A loop()-ból
static void demo_task() {
  if (!g_demo || time(nullptr) < 1600000000) return;
  const int32_t s = now_slot();
  if (hist_last_slot < 0 || s < hist_last_slot || s - hist_last_slot > 12) {   // az idő ugrott (pl. megjött a pontos idő)
    LogOut.println("Bemutató: megváltozott az idő, az adatok újraszámolva");
    demo_generate();
    demo_forecast();
    demo_tick();
    demo_energy_generate();
    demo_last_tick = millis();
  }
  if (!fc.valid || millis() - demo_last_fc >= DEMO_FC_MS) demo_forecast();
  if (millis() - demo_last_tick >= DEMO_TICK_MS) { demo_last_tick = millis(); demo_tick(); }
  demo_energy_task();
  demo_rotate();
}
