// A főoldal értékeinek frissítése, a felső sor (óra, figyelmeztetések, adat kora)
// (Az idojaras.ino része: az Arduino IDE és a PlatformIO a fájlokat ábécésorrendben, egy programmá fűzi össze.)

// Becslés a légnyomás 3 órás változásából (ha nincs internetes előrejelzés)
static bool forecast_get(int *icon, const char **l1, const char **l2) {
  float p = weather.baro_rel_hpa, r;
  if (p < 300.0f || !hist_rate(2, 36, 12, p, &r)) return false;

  if (r >= PRESS_T2)           { *icon = ICON_SUN;                          *l1 = tr("Gyorsan", "Rapidly");  *l2 = tr("javul", "improving"); }
  else if (r >= PRESS_T1)      { *icon = (p > 1015.0f) ? ICON_SUN : ICON_PARTLY; *l1 = tr("Javuló", "Improving"); *l2 = tr("idő", "weather"); }
  else if (r > -PRESS_T1) {
    if (p >= 1020.0f)          { *icon = ICON_SUN;    *l1 = tr("Tartósan", "Settled"); *l2 = tr("szép", "fair"); }
    else if (p >= 1009.0f)     { *icon = ICON_PARTLY; *l1 = tr("Stabil", "Stable");   *l2 = tr("idő", "weather"); }
    else                       { *icon = ICON_CLOUDY; *l1 = tr("Borult", "Overcast");   *l2 = tr("idő", "weather"); }
  }
  else if (r > -PRESS_T2)      { *icon = (p < 1008.0f) ? ICON_RAIN : ICON_CLOUDY; *l1 = tr("Romló", "Worsening"); *l2 = tr("idő", "weather"); }
  else                         { *icon = ICON_RAIN;   *l1 = tr("Gyors", "Rapid");    *l2 = tr("romlás", "worsening"); }
  return true;
}

// Az ikon kiválasztása: ha van előrejelzés, az; különben a sugárzásból és az esőből (durva becslés)
static int pick_icon() {
  if (weather.raining > 0.5f || weather.rain_rate_mm > 0.05f) return ICON_RAIN;
  if (weather.solar_wm2 < 3.0f)   return ICON_NIGHT;
  if (weather.solar_wm2 >= 400.0f) return ICON_SUN;
  if (weather.solar_wm2 >= 150.0f) return ICON_PARTLY;
  return ICON_CLOUDY;
}

// Hőérzet: hidegben szélhűtés (<= 10 °C, szél >= 4,8 km/h), melegben hőindex (>= 26,7 °C, páratartalom >= 40 %), különben a hőmérséklet
static float feels_like_c(float t, float rh, float v_kmh) {
  if (t <= 10.0f && v_kmh >= 4.8f) {
    float p = powf(v_kmh, 0.16f);
    return 13.12f + 0.6215f * t - 11.37f * p + 0.3965f * t * p;
  }
  if (t >= 26.7f && rh >= 40.0f) {
    float T = t * 1.8f + 32.0f, R = rh;
    float hi = -42.379f + 2.04901523f * T + 10.14333127f * R - 0.22475541f * T * R - 0.00683783f * T * T
             - 0.05481717f * R * R + 0.00122874f * T * T * R + 0.00085282f * T * R * R - 0.00000199f * T * T * R * R;
    float c = (hi - 32.0f) / 1.8f;
    return c > t ? c : t;
  }
  return t;
}

// Az utolsó adat elavult? (induláskor, adat nélkül nem)
static bool data_stale() {
  return weather.has_data && (millis() - weather.last_rx_ms) > (uint32_t)STALE_SECONDS * 1000UL;
}

// A helyiség konzoljától friss-e az adat (STALE_SECONDS-nál régebbi adatnál "--" látszik)
static bool room_fresh(int r) {
  return rooms[r].last_ms && (millis() - rooms[r].last_ms) <= (uint32_t)STALE_SECONDS * 1000UL;
}

// Szín beállítása úgy, hogy csak tényleges változásnál rajzolódjon újra
static void set_col(lv_obj_t *o, uint32_t c) {
  if (!o) return;
  lv_color_t nc = lv_color_hex(c);
  if (!lv_color_eq(lv_obj_get_style_text_color(o, LV_PART_MAIN), nc)) lv_obj_set_style_text_color(o, nc, 0);
}

// A főoldal "Bent" zónája: a kiválasztott helyiség (elavult vagy hiányzó adatnál "--"). Kinti adat nélkül is
// frissül, hogy a zónára koppintva a helyiség neve az első adatcsomag előtt is váltson.
static void update_indoor_zone() {
  if (!d_room) return;
  const Room &rm = rooms[sel_room];
  const bool rm_ok = room_fresh(sel_room);
  char b[48];
  snprintf(b, sizeof(b), tr("Bent: %s", "Indoor: %s"), rm.name);                                      set_label_if_changed(d_room, b);
  if (rm_ok) {
    snprintf(b, sizeof(b), "%.1f", rm.t);                                           set_label_if_changed(d_itemp, b);
    snprintf(b, sizeof(b), "%.0f", rm.h);                                           set_label_if_changed(d_ihum, b);
  } else { set_label_if_changed(d_itemp, "--.-"); set_label_if_changed(d_ihum, "--"); }
  if (rm_ok && rm.has_co2) {
    snprintf(b, sizeof(b), "CO2: %.0f ppm", rm.co2);                                set_label_if_changed(d_co2, b);
    snprintf(b, sizeof(b), "Max: %.0f ppm", rm.co2_max > rm.co2 ? rm.co2_max : rm.co2); set_label_if_changed(d_co2max, b);
  } else { set_label_if_changed(d_co2, "CO2: --"); set_label_if_changed(d_co2max, ""); }
  int zone = !rm_ok ? -1 : (rm.h < COMFORT_DRY_MAX) ? 0 : (rm.h <= COMFORT_WET_MIN ? 1 : 2);
  for (int i = 0; i < 3; i++) {
    lv_obj_set_style_bg_color(d_comfort[i], lv_color_hex(i == zone ? COL_BLUE : COL_BLUE_DIM), 0);
  }
}

static const int OT_RX = SX(300);   // a kinti hőmérséklet és páratartalom jobb széle a zónában
static void update_dashboard() {
  if (!d_otemp) return;
  if (!weather.has_data) { update_indoor_zone(); return; }   // az első adatcsomagig a kinti "--" jelek maradnak
  const WeatherData &w = weather;
  char b[96];

  // ikon és előrejelzés: eső esetén az eső, különben a nyomástrendből jövő előrejelzés (éjjel a nap helyett hold)
  // internetes előrejelzés (délután 3-ig a mai, utána a holnapi nap), ha nincs: becslés a nyomástrendből
  int fi = ICON_CLOUDY;
  const char *f1 = "", *f2 = "";
  char fcl[48] = "";
  const int fsi = fc_first_today();
  if (fsi >= 0) {
    struct tm t;
    bool tomorrow = get_local_tm(t) && t.tm_hour >= 15 && fsi + 1 < fc.n;
    const FcDay &d = fc.day[fsi + (tomorrow ? 1 : 0)];
    fi = wx_dash_icon(wx_type(d.code));
    if (isnan(d.pprob)) snprintf(fcl, sizeof(fcl), "%s %.0f/%.0f°", tomorrow ? tr("Holnap", "Tomorrow") : tr("Ma", "Today"), d.tmax, d.tmin);
    else                snprintf(fcl, sizeof(fcl), "%s %.0f/%.0f°, %.0f%%", tomorrow ? tr("Holnap", "Tomorrow") : tr("Ma", "Today"), d.tmax, d.tmin, d.pprob);
  }
  bool have_fc = fsi >= 0 || forecast_get(&fi, &f1, &f2);
  bool raining = (w.raining > 0.5f || w.rain_rate_mm > 0.05f);
  int ic;
  if (raining)      ic = ICON_RAIN;
  else if (have_fc) ic = (w.solar_wm2 < 3.0f && (fi == ICON_SUN || fi == ICON_PARTLY)) ? ICON_NIGHT : fi;
  else              ic = pick_icon();
  if (fcl[0]) set_label_if_changed(d_fc1, fcl);
  else { snprintf(b, sizeof(b), "%s %s", have_fc ? f1 : tr("Adatot", "Collecting"), have_fc ? f2 : tr("gyűjtök", "data")); set_label_if_changed(d_fc1, b); }
  update_trends();
  for (int i = 0; i < ICON_COUNT; i++) {
    lv_obj_set_hidden(d_icons[i], i != ic);
  }

  // bal felül
  snprintf(b, sizeof(b), "%.0f", w.baro_rel_hpa);                                   set_label_if_changed(d_press, b);
  snprintf(b, sizeof(b), w.wind_kmh < 10.0f ? "%.1f" : "%.0f", w.wind_kmh);          set_label_if_changed(d_wnum, b);
  set_label_if_changed(d_wdir, compass(w.wind_dir));
  snprintf(b, sizeof(b), tr("Lökés: %.0f", "Gust: %.0f"), w.gust_kmh);                                set_label_if_changed(d_wgust, b);
  wind_needle_set(w.wind_dir);
  snprintf(b, sizeof(b), tr("Eső ma: %.1f mm", "Rain today: %.1f mm"), w.rain_day_mm);                         set_label_if_changed(d_rain, b);
  snprintf(b, sizeof(b), "%.0f", w.uv_index);                                        set_label_if_changed(d_uv, b);   // UV-index (a színe is ehhez igazodik)

  // színek: figyelmeztetés szerint, elavult adatnál elhalványítva
  const bool stale = data_stale();
  auto tint = [&](uint32_t base, uint32_t bg) { return stale ? mix_col(bg, base, 40) : base; };
  uint32_t c_otemp = (w.temp_c <= WARN_FROST_C) ? COL_ICE : (w.temp_c >= WARN_HOT_C ? COL_WARN : COL_ORANGE);
  uint32_t c_uv    = (w.uv_index >= WARN_UV_HIGH) ? COL_WARN : (w.uv_index >= WARN_UV_MID ? COL_ORANGE : COL_TEXT);
  const Room &rm = rooms[sel_room];
  const bool rm_ok = room_fresh(sel_room);
  uint32_t c_co2   = (rm_ok && rm.has_co2 && rm.co2 >= WARN_CO2_HIGH) ? COL_WARN : (rm_ok && rm.has_co2 && rm.co2 >= WARN_CO2_MID ? COL_ORANGE : COL_BLUE_SOFT);
  uint32_t c_gust  = (w.gust_kmh >= WARN_GUST_KMH) ? COL_WARN : COL_TEXT;
  set_col(d_otemp, tint(c_otemp, COL_BG_BL));
  set_col(d_ohum,  tint(COL_ORANGE, COL_BG_BL));
  set_col(d_dew,   tint(COL_TEXT, COL_BG_BL));
  set_col(d_tmin,  tint(COL_TEXT, COL_BG_BL));
  set_col(d_tmax,  tint(COL_TEXT, COL_BG_BL));
  set_col(d_feels, tint(COL_TEXT, COL_BG_BL));
  set_col(d_uv,    tint(c_uv, COL_BG_BL));
  set_col(d_co2max, tint(COL_BLUE_SOFT, COL_BG_BR));
  set_col(d_itemp, tint(COL_BLUE, COL_BG_BR));
  set_col(d_ihum,  tint(COL_BLUE, COL_BG_BR));
  set_col(d_co2,   tint(c_co2, COL_BG_BR));
  set_col(d_press, tint(0xE8E8E8, COL_BG_TL));
  set_col(d_fc1,   tint(COL_TEXT, COL_BG_TL));
  set_col(d_rain,  tint(COL_TEXT, COL_BG_TL));
  set_col(d_wnum,  tint(0xE8E8E8, COL_BG_TL));
  set_col(d_wgust, tint(c_gust, COL_BG_TL));
  set_col(d_wdir,  tint(0x4FC3F7, COL_BG_TL));

  // kint
  {
    // az előjelet külön rajzoljuk (téglalap), a számjegyek az abszolút értéket mutatják
    const bool neg = w.temp_c <= -0.05f;
    snprintf(b, sizeof(b), "%.1f", fabsf(w.temp_c));
    set_label_if_changed(d_otemp, b);
    int digits = (int)strlen(b) - 1;                       // a tizedespont nem foglal helyet
    int start = OT_RX - (int)(digits * S(78.3f) + 0.5f);   // a font_seg_96 számjegyszélessége
    lv_obj_set_x(d_ominus, start - S(30));
    lv_obj_set_hidden(d_ominus, !neg);
    lv_obj_set_style_bg_color(d_ominus, lv_color_hex(tint(c_otemp, COL_BG_BL)), 0);
  }
  snprintf(b, sizeof(b), "%.1f", feels_like_c(w.temp_c, w.humidity, w.wind_kmh));  set_label_if_changed(d_feels, b);
  snprintf(b, sizeof(b), "%.0f", w.humidity);                                       set_label_if_changed(d_ohum, b);
  if (day_stats.valid) {
    snprintf(b, sizeof(b), "%.1f", day_stats.tmin);  set_label_if_changed(d_tmin, b);
    snprintf(b, sizeof(b), "%.1f", day_stats.tmax);  set_label_if_changed(d_tmax, b);
  } else { set_label_if_changed(d_tmin, "--"); set_label_if_changed(d_tmax, "--"); }
  float dp = dew_point_c(w.temp_c, w.humidity);
  if (isnan(dp)) snprintf(b, sizeof(b), "--");
  else           snprintf(b, sizeof(b), "%.1f", dp);
  set_label_if_changed(d_dew, b);
  update_indoor_zone();
}

// A főoldal órája, dátuma és holdfázisa (a felső sáv frissítésével együtt, 5x másodpercenként)
static void update_dashboard_time() {
  if (!d_clock) return;
  struct tm t;
  char b[24];

  if (get_local_tm(t)) {
    snprintf(b, sizeof(b), "%02d:%02d", t.tm_hour, t.tm_min);          set_label_if_changed(d_clock, b);
    snprintf(b, sizeof(b), ":%02d", t.tm_sec);                          set_label_if_changed(d_secs, b);
    snprintf(b, sizeof(b), "%02d.%02d.", t.tm_mon + 1, t.tm_mday);      set_label_if_changed(d_date, b);
    set_label_if_changed(d_week, WEEKDAYS_LONG[t.tm_wday]);
    snprintf(b, sizeof(b), "%d", t.tm_year + 1900);                     set_label_if_changed(d_year, b);

    update_sun_labels(t);

    static int last_moon_key = -1;
    int mkey = t.tm_yday * 24 + t.tm_hour;
    if (mkey != last_moon_key) {
      last_moon_key = mkey;
      moon_draw(moon_phase(time(nullptr)));
    }
  } else {
    set_label_if_changed(d_clock, "--:--");
    set_label_if_changed(d_secs, ":--");
    set_label_if_changed(d_date, "--.--.");
    set_label_if_changed(d_week, "");
    set_label_if_changed(d_year, "");
  }
}

// Felső sor: balra a helyi dátum és idő, fix helyen az állomás típusa és az IP, jobbra az utolsó adat kora
static const char *const WEEKDAYS_T[2][7] = {{"V", "H", "K", "Sze", "Cs", "P", "Szo"},   // vasárnap ... szombat
                                             {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"}};
#define WEEKDAYS TR2(WEEKDAYS_T)

static void update_status_bar() {
  if (!lbl_title || !lbl_clock || !lbl_info || !lbl_warn) return;

  // Egységes fejléc minden oldalon: bal: az oldal neve (lbl_info, a show_page() állítja), közép: figyelmeztetések
  // (a grafikonoldalakon a Nap | Hét | Hónap kapcsoló után), jobb: óra (a főoldalon nem), az adat kora, fogaskerék
  struct tm t;
  char clk[24];
  if (get_local_tm(t)) snprintf(clk, sizeof(clk), "%s %02d:%02d", WEEKDAYS[t.tm_wday], t.tm_hour, t.tm_min);
  else                 snprintf(clk, sizeof(clk), "--:--");

  char age[24] = "";   // az utolsó adat kora
  if (!weather.has_data) {
    snprintf(age, sizeof(age), tr("nincs adat", "no data"));
  } else {
    unsigned long a = (millis() - weather.last_rx_ms) / 1000UL;
    if (a < 120) snprintf(age, sizeof(age), tr("adat: %lu mp", "data: %lu s"), a);
    else         snprintf(age, sizeof(age), tr("adat: %lu perc", "data: %lu min"), a / 60UL);
  }

  // figyelmeztetések (minden oldalon): WiFi, szellőztetés, akku, hely
  char warn[160] = "";
  if (g_demo) strlcpy(warn, tr("BEMUTATÓ (kitalált adatok)", "DEMO (made-up data)"), sizeof(warn));
  else if (WiFi.status() != WL_CONNECTED) strlcpy(warn, tr("WiFi: nincs kapcsolat", "WiFi: not connected"), sizeof(warn));
  if (batt_on) {   // P4: elment a táp, a lap akkuról megy
    char b[48];
    snprintf(b, sizeof(b), tr("Akkuról megy: %d %%", "On battery: %d %%"), batt_pct);
    if (warn[0]) strlcat(warn, "   ", sizeof(warn));
    strlcat(warn, b, sizeof(warn));
  }
  if (weather.has_data) {
    const char *names[2 + ROOM_COUNT];
    int n = 0;
    if (weather.wh90_batt_v > 0.1f && weather.wh90_batt_v < LOW_WS90_V)       names[n++] = "WS90";
    if (weather.ws90_cap_v > 0.1f && weather.ws90_cap_v < LOW_CAP_V)          names[n++] = tr("kond.", "cap.");
    static char nobatt[ROOM_COUNT][32];
    for (int i = 0; i < ROOM_COUNT; i++) {
      const Room &r = rooms[i];
      if (!room_fresh(i) || !r.has_batt || r.low_v <= 0.0f || r.batt >= r.low_v) continue;
      if (r.batt < 0.5f) { snprintf(nobatt[i], sizeof(nobatt[i]), tr("%s (nincs elem)", "%s (no battery)"), r.name); names[n++] = nobatt[i]; }
      else names[n++] = r.name;
    }
    if (n) {
      if (warn[0]) strlcat(warn, "   ", sizeof(warn));
      strlcat(warn, tr("Akku: ", "Battery: "), sizeof(warn));
      for (int i = 0; i < n; i++) {
        if (i) strlcat(warn, ", ", sizeof(warn));
        strlcat(warn, names[i], sizeof(warn));
      }
    }
  }
  // szellőztetés: a CO2-t mérő helyiségek közül, ahol magas (hiszterézissel)
  {
    bool *vent = co2_vent;
    char v[64] = "";
    for (int i = 0; i < ROOM_COUNT; i++) {
      const Room &r = rooms[i];
      if (!room_fresh(i) || !r.has_co2) { vent[i] = false; continue; }
      if (r.co2 >= CO2_VENT_ON) vent[i] = true;
      else if (r.co2 < CO2_VENT_OFF) vent[i] = false;
      if (!vent[i]) continue;
      strlcat(v, v[0] ? ", " : tr("Szellőztess: ", "Ventilate: "), sizeof(v));
      strlcat(v, r.name, sizeof(v));
    }
    if (v[0]) {   // elöl, mert ez a legfontosabb
      char rest[160];
      strlcpy(rest, warn, sizeof(rest));
      strlcpy(warn, v, sizeof(warn));
      if (rest[0]) { strlcat(warn, "   ", sizeof(warn)); strlcat(warn, rest, sizeof(warn)); }
    }
  }
  // eső / fagy az órás előrejelzésből: a főoldal Kint zónájának fejlécében
  {
    char fa[64];
    fc_alerts(fa, sizeof(fa));
    if (d_kalert) set_label_if_changed(d_kalert, fa);
  }
  if (!cfg.has_loc) {
    if (warn[0]) strlcat(warn, "   ", sizeof(warn));
    strlcat(warn, tr("Hely: " MDNS_HOST ".local", "Location: " MDNS_HOST ".local"), sizeof(warn));
  }
  set_label_if_changed(lbl_warn, warn);
  // a főoldal címe: csak figyelmeztetés nélkül (különben annak adja át a helyét)
  if (current_page == PAGE_DASH) set_label_if_changed(lbl_info, warn[0] ? "" : tr("Időjárás-állomás", "Weather Station"));

  // az adat elavulásának változásakor a főoldal színei frissülnek
  static bool last_stale = false;
  bool stale_now = data_stale();
  if (stale_now != last_stale) {
    last_stale = stale_now;
    update_dashboard();
  }

  set_label_if_changed(lbl_clock, clk);
  set_label_if_changed(lbl_title, age);
  // jobb oldal: fogaskerék | adat kora | óra (jobbról balra egymás mellé)
  lv_obj_align(lbl_title, LV_ALIGN_TOP_RIGHT, -SX(64), SY(8));
  lv_obj_update_layout(lbl_title);
  const bool show_clock = current_page != PAGE_DASH;   // a főoldalon az óra nagyban látszik
  lv_obj_set_hidden(lbl_clock, !show_clock);
  if (show_clock) { lv_obj_align_to(lbl_clock, lbl_title, LV_ALIGN_OUT_LEFT_MID, -SX(24), 0); lv_obj_update_layout(lbl_clock); }
  // a figyelmeztetések helye: az oldal neve (vagy a kapcsoló) és a jobb oldali elemek között
  lv_obj_update_layout(lbl_info);
  int wl = SX(20);
  if (lv_label_get_text(lbl_info)[0]) wl = lv_obj_get_x(lbl_info) + lv_obj_get_width(lbl_info) + SX(24);
  if (current_page == PAGE_HIST || current_page == PAGE_HIST2) wl = CH_TOG_X + CH_TOG_W + SX(20);
  const int wr = (show_clock ? lv_obj_get_x(lbl_clock) : lv_obj_get_x(lbl_title)) - SX(20);
  lv_obj_set_pos(lbl_warn, wl, SY(8));
  lv_obj_set_size(lbl_warn, wr > wl ? wr - wl : 0, SY(34));

  update_dashboard_time();
}
