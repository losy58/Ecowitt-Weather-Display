// Grafikonoldalak (Kint, Bent; Nap | Hét | Hónap) és a teljes képernyős grafikon
// (Az idojaras.ino része: az Arduino IDE és a PlatformIO a fájlokat ábécésorrendben, egy programmá fűzi össze.)

// ---------------------------------------------------------------------------
// Előzményoldalak (24 óra): "Kint" - 4 grafikon 2x2-ben; "Bent" - 2 széles grafikon (helyiségek hőmérséklete, CO2)
// ---------------------------------------------------------------------------
constexpr int HC_W = SX(488), HC_H = SY(262), HC_GAP = SX(8), HC_X0 = SX(20), HC_Y0 = SY(50);   // kártya
constexpr int HC_WW = 2 * HC_W + HC_GAP;                                       // széles kártya (a "Bent" oldalon)
constexpr int HC_CX = SX(58), HC_CY = SY(46), HC_CH = HC_H - HC_CY - SY(34);               // grafikon a kártyán belül
constexpr int CH_TOG_SEG = CH_TOG_W / 3;
constexpr uint32_t CC_ORANGE = 0xFF8A3D, CC_LBLUE = 0x4FC3F7, CC_BLUE = 0x3D7BFF, CC_YELLOW = 0xFFD54F, CC_PURPLE = 0xB388FF;
constexpr uint32_t CC_GREEN = 0x66BB6A, CC_PINK = 0xF06292, CC_TEAL = 0x26A69A;
static const ChartDef CHARTS[HC_COUNT] = {
  {"Hőmérséklet",   "Hőmérséklet °C", CC_ORANGE, 2, {F_TO, F_TI},        {CC_ORANGE, CC_BLUE},          {"Kint", "Bent"},             40,  false, 10,  10, PAGE_HIST,  0, 0, false, {M_TMAX, M_TMIN}, {"Max", "Min"}},
  {"Légnyomás hPa", "Légnyomás hPa",  CC_PURPLE, 1, {F_P},               {CC_PURPLE},                   {"Légnyomás"},                60,  false, 10,  10, PAGE_HIST,  1, 0, false, {M_P}, {"Átlag"}},
  {"Szél km/h",     "Szél km/h",      CC_LBLUE,  2, {F_W, F_G},          {CC_LBLUE, CC_YELLOW},         {"Szél", "Lökés"},            100, true,  10,  10, PAGE_HIST,  0, 1, false, {M_W, M_G}, {"Átlag", "Max lökés"}},
  {tr("Eső ma mm", "Rain today mm"),     "Eső mm",      CC_BLUE,   1, {F_R},               {CC_BLUE},                     {"Eső ma"},                   20,  true,  10,  10, PAGE_HIST,  1, 1, false, {M_R}, {"Napi"}},
  {"Bent °C",       "Bent °C",        CC_BLUE,   3, {F_TI, F_T2, F_T3},  {CC_BLUE, CC_GREEN, CC_PINK},  {"", "", ""},                 40,  false, 10,  10, PAGE_HIST2, 0, 0, true, {M_TI, M_T2, M_T3}, {"", "", ""}},
  {"CO2 ppm",       "CO2 ppm",        CC_TEAL,   3, {F_C1, F_C2, F_C3},  {CC_BLUE, CC_GREEN, CC_PINK},  {"", "", ""},                 400, false, 100, 1,  PAGE_HIST2, 0, 1, true, {M_C1, M_C2, M_C3}, {"", "", ""}},
};

// Nézet: napi (a mai nap 0-24 óra, 5 perces pontok), heti (az utolsó 7 nap a maival, félórás átlagok)
// vagy havi (a naptári hónap 1-jétől az utolsó napjáig, napi összesítők)
constexpr int CH_N_DAY = HIST_DAY, CH_N_WEEK = 7 * 48, CH_N_MAX = CH_N_WEEK;
constexpr int CD_ROWS = (int)F_COUNT + (int)M_COUNT;
constexpr int CH_X_LABELS = 7;          // tengelyfeliratok: napi nézetben 5, hetiben 7 látszik
static int ch_mode = 0;                 // 0: napi, 1: heti, 2: havi
// a folyó hónap napjainak száma (pontos idő nélkül 30)
static int month_len() {
  struct tm t;
  if (!get_local_tm(t)) return 30;
  t.tm_mon += 1; t.tm_mday = 0; t.tm_hour = 12; t.tm_isdst = -1;   // a következő hónap 0. napja = e hónap utolsó napja
  mktime(&t);
  return t.tm_mday;
}
static int ch_n() { return ch_mode == 2 ? month_len() : ch_mode ? CH_N_WEEK : CH_N_DAY; }
// a k. sorozat adatsorának indexe a cd tömbben az aktuális nézetben
static int chf(const ChartDef &d, int k) { return ch_mode == 2 ? F_COUNT + d.mfield[k] : d.field[k]; }
// a CHARTS tábla magyar szövegei angolul (a tábla fájlszintű, a nyelv betöltése előtt jön létre, ezért felhasználáskor fordítjuk)
static const char *chart_tr(const char *hu) {
  static const char *const T[][2] = {
    {"Hőmérséklet", "Temperature"}, {"Hőmérséklet °C", "Temperature °C"}, {"Légnyomás hPa", "Pressure hPa"}, {"Szél km/h", "Wind km/h"},
    {"Eső ma mm", "Rain today mm"}, {"Eső mm", "Rain mm"}, {"Bent °C", "Indoor °C"}, {"Kint", "Outdoor"}, {"Bent", "Indoor"},
    {"Légnyomás", "Pressure"}, {"Átlag", "Average"}, {"Szél", "Wind"}, {"Lökés", "Gust"}, {"Max lökés", "Max gust"},
    {"Eső ma", "Rain today"}, {"Napi", "Daily"}};
  if (!g_lang) return hu;
  for (const auto &p : T) if (strcmp(p[0], hu) == 0) return p[1];
  return hu;
}

static lv_obj_t *ch_chart[HC_COUNT] = {}, *ch_hi[HC_COUNT] = {}, *ch_lo[HC_COUNT] = {}, *ch_v[HC_COUNT][3] = {};
static lv_obj_t *ch_x[HC_COUNT][CH_X_LABELS] = {};
static lv_obj_t *ch_title[HC_COUNT] = {};
static lv_chart_series_t *ch_se[HC_COUNT][3] = {};
static lv_obj_t *ch_toggle = NULL, *ch_tog[3] = {};   // Nap | Hét | Hónap kapcsoló a felső sorban
static int32_t (*cd)[CH_N_MAX] = NULL;  // a grafikonok adatsorai (PSRAM, CD_ROWS x CH_N_MAX)
static DaySum ch_today = {};            // a mai nap összesítője (a havi nézet értékfeliratai)
static bool ch_today_ok = false;

static lv_obj_t *small_label(lv_obj_t *parent, const char *txt, int x, int y, int w, lv_text_align_t al) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, &font_hu_14, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(COL_GREY), 0);
  lv_obj_set_width(l, w);
  lv_obj_set_style_text_align(l, al, 0);
  lv_obj_set_pos(l, x, y);
  return l;
}

static void card_rect(const ChartDef &d, int *x, int *y, int *w) {
  *w = d.wide ? HC_WW : HC_W;
  *x = HC_X0 + d.col_ * (HC_W + HC_GAP);
  *y = HC_Y0 + d.row_ * (HC_H + HC_GAP);
}

// Egy grafikonkártya: cím + sorozatonként az aktuális érték (a sorozat színével) + vonaldiagram + tengelyfeliratok
static void make_chart_card(int idx) {
  const ChartDef &d = CHARTS[idx];
  int x, y, w;
  card_rect(d, &x, &y, &w);
  lv_obj_t *card = lv_obj_create(d.page == PAGE_HIST2 ? page_hist2 : page_hist);
  lv_obj_add_style(card, &style_card, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(d.frame), 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_set_size(card, w, HC_H);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_scrollable(card, false);

  ch_title[idx] = dlabel(card, chart_tr(d.title), &font_hu_28, d.frame, SX(12), SY(6));
  const int lw = d.wide ? SX(210) : SX(150);   // egy értékfelirat szélessége; jobbról balra sorakoznak
  for (int k = 0; k < d.n; k++)
    ch_v[idx][k] = dlabel_r(card, "", &font_hu_28, d.col[k], w - SX(14) - (d.n - 1 - k) * lw, SY(6), lw);

  const int cw = w - HC_CX - SX(14);
  lv_obj_t *ch = lv_chart_create(card);
  lv_obj_set_pos(ch, HC_CX, HC_CY);
  lv_obj_set_size(ch, cw, HC_CH);
  lv_chart_set_type(ch, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(ch, CH_N_DAY);
  lv_chart_set_div_line_count(ch, 5, 5);
  lv_obj_set_style_bg_opa(ch, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ch, 0, 0);
  lv_obj_set_style_pad_all(ch, 0, 0);
  lv_obj_set_style_radius(ch, 0, 0);
  lv_obj_set_style_line_color(ch, lv_color_hex(0x3A3A3A), LV_PART_MAIN);
  lv_obj_set_style_line_width(ch, 1, LV_PART_MAIN);
  lv_obj_set_style_line_width(ch, S(2), LV_PART_ITEMS);
  lv_obj_set_style_size(ch, 0, 0, LV_PART_INDICATOR);
  lv_obj_set_scrollable(ch, false);
  for (int k = 0; k < d.n; k++) {
    ch_se[idx][k] = lv_chart_add_series(ch, lv_color_hex(d.col[k]), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_series_ext_y_array(ch, ch_se[idx][k], cd[chf(d, k)]);
  }
  ch_chart[idx] = ch;

  ch_hi[idx] = small_label(card, "", SX(2), HC_CY - S(8), SX(50), LV_TEXT_ALIGN_RIGHT);
  ch_lo[idx] = small_label(card, "", SX(2), HC_CY + HC_CH - S(8), SX(50), LV_TEXT_ALIGN_RIGHT);
  for (int k = 0; k < CH_X_LABELS; k++) ch_x[idx][k] = small_label(card, "", HC_CX, HC_CY + HC_CH + SY(8), SX(60), LV_TEXT_ALIGN_CENTER);
}

static int16_t hs_get(const HistSample &e, int f) {
  switch (f) {
    case F_TO: return e.to;
    case F_TI: return e.ti;
    case F_P:  return e.p;
    case F_W:  return e.w;
    case F_G:  return e.g;
    case F_R:  return e.r;
    case F_T2: return e.t2;
    case F_T3: return e.t3;
    case F_C1: return e.c1;
    case F_C2: return e.c2;
    default:   return e.c3;
  }
}

// Az aktuális (élő) érték egy mezőhöz, kiírási egységben; false, ha nincs friss adat
static bool cur_value(int f, float *v) {
  const WeatherData &w = weather;
  switch (f) {
    case F_TO: *v = w.temp_c;       return w.has_data;
    case F_P:  *v = w.baro_rel_hpa; return w.has_data;
    case F_W:  *v = w.wind_kmh;     return w.has_data;
    case F_G:  *v = w.gust_kmh;     return w.has_data;
    case F_R:  *v = w.rain_day_mm;  return w.has_data;
    case F_TI: *v = rooms[0].t;     return room_fresh(0);
    case F_T2: *v = rooms[1].t;     return room_fresh(1);
    case F_T3: *v = rooms[2].t;     return room_fresh(2);
    case F_C1: *v = rooms[0].co2;   return room_fresh(0) && rooms[0].has_co2;
    case F_C2: *v = rooms[1].co2;   return room_fresh(1) && rooms[1].has_co2;
    default:   *v = rooms[2].co2;   return room_fresh(2) && rooms[2].has_co2;
  }
}

// A helyi éjfél szelete days_back nappal ezelőtt (-1: nincs pontos idő)
static int32_t midnight_slot(int days_back) {
  struct tm t;
  if (!get_local_tm(t)) return -1;
  t.tm_mday -= days_back; t.tm_hour = t.tm_min = t.tm_sec = 0; t.tm_isdst = -1;
  return (int32_t)(mktime(&t) / 300);
}

// A grafikon adatsora: napi nézetben a mai nap 5 perces szeletei (a jövő üres), hetiben 7 nap félórás (6 szeletes)
// csoportjai: a lökésnél és a napi esőnél a csoport legnagyobb értéke, a többinél az átlag
static void hist_fill(int32_t *dst, int field) {
  const int n = ch_n(), per = ch_mode ? 6 : 1;
  const int32_t start = midnight_slot(ch_mode ? 6 : 0), ns = now_slot();
  const bool use_max = (field == F_G || field == F_R);
  for (int i = 0; i < n; i++) {
    int32_t sum = 0, mx = INT32_MIN; int cnt = 0;
    for (int k = 0; start >= 0 && k < per; k++) {
      int32_t slot = start + i * per + k;
      if (slot > ns || !hist_valid_slot(slot)) continue;
      int16_t v = hs_get(hist[slot % HIST_N], field);
      if (v == HIST_NONE) continue;
      sum += v; cnt++; if (v > mx) mx = v;
    }
    dst[i] = !cnt ? LV_CHART_POINT_NONE : use_max ? mx : (int32_t)lroundf((float)sum / cnt);
  }
}

// A vízszintes tengely feliratai: napi nézetben 0, 6, 12, 18, 24 óra, hetiben a napok rövid neve (a mai: "ma")
// havi nézet: ezeknél a napoknál van felirat (az utolsó a hónap utolsó napja)
static int month_x_day(int k) {
  static const int D[6] = {1, 5, 10, 15, 20, 25};
  return k < 6 ? D[k] : month_len();
}

static const char *ch_x_text(int k, char *buf, size_t n) {
  if (ch_mode == 2) {
    snprintf(buf, n, "%d.", month_x_day(k));
    return buf;
  }
  if (!ch_mode) {
    if (k > 4) return "";
    snprintf(buf, n, "%d:00", k * 6);
    return buf;
  }
  struct tm t;
  if (!get_local_tm(t)) return "";
  if (k == 6) return tr("ma", "today");
  return WEEKDAYS[(t.tm_wday + 7 - (6 - k)) % 7];
}

// x pozíció (a felirat közepe) a grafikon szélességén belül; a szélső feliratok nem lóghatnak ki (margin)
static int ch_x_pos(int k, int w, int margin) {
  int x = ch_mode == 2 ? w * (2 * (month_x_day(k) - 1) + 1) / (2 * month_len())   // havinál a nap pontja
        : ch_mode ? w * (2 * k + 1) / 14 : w * k / 4;     // hetinél a nap közepe
  return x < margin ? margin : x > w - margin ? w - margin : x;
}

// Tengelytartomány a grafikon sorozataihoz (step-re kerekítve, legalább span széles)
static void chart_bounds(const ChartDef &d, int32_t *lo_out, int32_t *hi_out) {
  int32_t mn = INT32_MAX, mx = INT32_MIN;
  for (int k = 0; k < d.n; k++) {
    const int32_t *a = cd[chf(d, k)];   // az aktuális nézet adatsora
    for (int i = 0; i < ch_n(); i++) {
      if (a[i] == LV_CHART_POINT_NONE) continue;
      if (a[i] < mn) mn = a[i];
      if (a[i] > mx) mx = a[i];
    }
  }
  if (mn > mx) { mn = 0; mx = 0; }
  const float st = (float)d.step;
  int32_t lo = d.zero ? 0 : (int32_t)floorf(mn / st) * d.step;
  int32_t hi = (int32_t)ceilf(mx / st) * d.step;
  if (hi - lo < d.span) {
    if (d.zero) {
      hi = d.span;
    } else {
      lo = (int32_t)floorf(((mn + mx) / 2.0f - d.span / 2.0f) / st) * d.step;
      hi = lo + d.span;
      while (hi < mx) hi += d.step;
      while (lo > mn) lo -= d.step;
    }
  }
  *lo_out = lo; *hi_out = hi;
}

// Egy sorozat felirata: egysorozatos grafikonnál csak az érték, különben "név érték"
static void series_text(const ChartDef &d, int k, bool with_name, char *b, size_t n) {
  float v;
  char num[16];
  bool ok;
  if (ch_mode == 2) {   // havi nézet: a mai nap összesítője
    const int16_t r = ch_today.v[d.mfield[k]];
    ok = ch_today_ok && r != HIST_NONE;
    v = r / (float)d.div;
  } else {
    ok = cur_value(d.field[k], &v);
  }
  const char *nm = chart_tr(ch_mode == 2 ? d.mname[k] : d.name[k]);
  // a Bent és a CO2 grafikon sorozatai a helyiségek (nevük a beállításokból; a nem használt / CO2-t nem mérő hely üres)
  const bool room_chart = (&d == &CHARTS[HC_ITEMP] || &d == &CHARTS[HC_CO2]);
  if (room_chart) {
    nm = rooms[k].name;
    if (!rooms[k].match[0] || (&d == &CHARTS[HC_CO2] && !rooms[k].has_co2)) { b[0] = 0; return; }
  }
  if (!ok)                        snprintf(num, sizeof(num), "--");
  else if (d.div == 1)            snprintf(num, sizeof(num), "%.0f", v);
  else                            snprintf(num, sizeof(num), "%.1f", v);
  if (with_name) snprintf(b, n, "%s %s", nm, num);
  else           snprintf(b, n, "%s", num);
}

// Havi adatsor: a naptári hónap napjainak összesítője (a mai nap élőben számolva, a jövő üres)
static void month_fill(int32_t *dst, int m) {
  struct tm t;
  const bool ok = get_local_tm(t);
  const int len = month_len();
  for (int i = 0; i < len; i++) {
    const DaySum *s = NULL;
    if (ok) {
      const int32_t dn = day_num(t.tm_year + 1900, t.tm_mon + 1, i + 1);
      s = (i + 1 == t.tm_mday) ? (ch_today_ok ? &ch_today : NULL) : (i + 1 < t.tm_mday ? day_get(dn) : NULL);
    }
    dst[i] = (!s || s->v[m] == HIST_NONE) ? LV_CHART_POINT_NONE : s->v[m];
  }
}

static void hist_update_charts() {
  if (!cd || !ch_chart[0]) return;
  if (ch_mode == 2) {
    ch_today_ok = day_from_hist(0, ch_today);
    for (int m = 0; m < M_COUNT; m++) month_fill(cd[F_COUNT + m], m);
    for (int i = 0; i < HC_COUNT; i++)   // hónapváltáskor más a napok száma
      if (ch_chart[i] && (int)lv_chart_get_point_count(ch_chart[i]) != month_len()) ch_set_mode(2);
  } else {
    for (int f = 0; f < F_COUNT; f++) hist_fill(cd[f], f);
  }

  char b[32];
  for (int i = 0; i < HC_COUNT; i++) {
    const ChartDef &d = CHARTS[i];
    const int cw = (d.wide ? HC_WW : HC_W) - HC_CX - SX(14);
    for (int k = 0; k < CH_X_LABELS; k++) {
      set_label_if_changed(ch_x[i][k], ch_x_text(k, b, sizeof(b)));
      lv_obj_set_x(ch_x[i][k], HC_CX + ch_x_pos(k, cw, SX(22)) - SX(30));
    }
    int32_t lo, hi;
    chart_bounds(d, &lo, &hi);
    lv_chart_set_axis_range(ch_chart[i], LV_CHART_AXIS_PRIMARY_Y, lo, hi);
    snprintf(b, sizeof(b), "%d", (int)(hi / d.div));  set_label_if_changed(ch_hi[i], b);
    snprintf(b, sizeof(b), "%d", (int)(lo / d.div));  set_label_if_changed(ch_lo[i], b);
    lv_chart_refresh(ch_chart[i]);
    for (int k = 0; k < d.n; k++) {
      series_text(d, k, d.n > 1, b, sizeof(b));
      set_label_if_changed(ch_v[i][k], b);
    }
  }
  if (fs_idx >= 0) fs_refresh();
}

static void create_history_page() {
  cd = (int32_t (*)[CH_N_MAX])heap_caps_malloc(sizeof(int32_t) * CD_ROWS * CH_N_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!cd) { LogOut.println("Grafikon: nincs elég memória"); return; }
  for (int f = 0; f < CD_ROWS; f++)
    for (int i = 0; i < CH_N_MAX; i++) cd[f][i] = LV_CHART_POINT_NONE;
  for (int i = 0; i < HC_COUNT; i++) make_chart_card(i);

  // Nap | Hét | Hónap kapcsoló: a grafikonoldalakon a felső sorban (a konzol típusa és az IP helyén)
  ch_toggle = make_group(lv_screen_active(), CH_TOG_X, CH_TOG_Y, CH_TOG_W, CH_TOG_H);
  const char *tt[3] = {tr("Nap", "Day"), tr("Hét", "Week"), tr("Hónap", "Month")};
  for (int k = 0; k < 3; k++) {
    ch_tog[k] = make_shape(ch_toggle, k * CH_TOG_SEG + SX(2), 0, CH_TOG_SEG - SX(4), CH_TOG_H, S(8), 0x3A3A3A);
    lv_obj_t *l = lv_label_create(ch_tog[k]);
    lv_label_set_text(l, tt[k]);
    lv_obj_set_style_text_font(l, &font_hu_28, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(l);
  }
  lv_obj_set_hidden(ch_toggle, true);
  ch_set_mode(0);
}

// A napi/heti nézet beállítása (pontszám, függőleges osztóvonalak, kapcsoló színei)
static void ch_set_mode(int mode) {
  ch_mode = mode;
  const int n = ch_n();
  for (int i = 0; i < HC_COUNT; i++) {
    if (!ch_chart[i]) continue;
    lv_chart_set_point_count(ch_chart[i], n);
    lv_chart_set_div_line_count(ch_chart[i], 5, ch_mode == 2 ? 7 : ch_mode ? 8 : 5);
    lv_chart_set_type(ch_chart[i], (ch_mode == 2 && i == HC_RAIN) ? LV_CHART_TYPE_BAR : LV_CHART_TYPE_LINE);   // havi eső: oszlopok
    for (int k = 0; k < CHARTS[i].n; k++) lv_chart_set_series_ext_y_array(ch_chart[i], ch_se[i][k], cd[chf(CHARTS[i], k)]);
  }
  // az eső kártya címe: napi nézetben a mai eső, hetiben és haviban napi összegek
  if (ch_title[HC_RAIN]) lv_label_set_text(ch_title[HC_RAIN], ch_mode ? tr("Napi eső mm", "Daily rain mm") : tr("Eső ma mm", "Rain today mm"));
  if (ch_toggle)
    for (int k = 0; k < 3; k++) lv_obj_set_style_bg_color(ch_tog[k], lv_color_hex(k == ch_mode ? 0x5A4A10 : 0x3A3A3A), 0);
}


// ---------------------------------------------------------------------------
// Teljes képernyős grafikon: a grafikonkártyára koppintva nyílik, a jobb felső sarokban az X zárja be
// ---------------------------------------------------------------------------
constexpr int FS_CX = SX(100), FS_CY = SY(90), FS_CW = LCD_H_RES - FS_CX - SX(36), FS_CH = SY(410);      // grafikon a teljes képernyős kártyán
constexpr int FS_XBTN = S(84);                                                              // az X gomb mérete
constexpr int FS_CLOSE_ZONE = S(140);                                                       // a bezáró koppintás érzékeny sarka (px)
constexpr int FS_LEG_X = SX(110), FS_LEG_Y = SY(52), FS_LEG_W = SX(260);                             // jelmagyarázat: a cím alatti sorban

static void create_fullscreen_chart() {
  fs_page = make_group(lv_screen_active(), 0, 0, LCD_H_RES, LCD_V_RES);
  lv_obj_t *card = lv_obj_create(fs_page);
  fs_card = card;
  lv_obj_add_style(card, &style_card, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_set_size(card, LCD_H_RES, LCD_V_RES);
  lv_obj_set_pos(card, 0, 0);
  lv_obj_set_style_radius(card, 0, 0);
  lv_obj_set_scrollable(card, false);

  fs_title = dlabel(card, "", &font_hu_28, COL_TEXT, SX(20), SY(14));
  // jelmagyarázat: színes vonaljel + a sorozat neve és aktuális értéke
  for (int k = 0; k < 3; k++) {
    fs_sw[k] = make_shape(card, FS_LEG_X + k * FS_LEG_W, FS_LEG_Y + S(13), S(30), S(8), S(4), COL_TEXT);
    fs_v[k]  = dlabel(card, "", &font_hu_28, COL_TEXT, FS_LEG_X + k * FS_LEG_W + S(38), FS_LEG_Y);
  }

  // bezáró gomb (X) a jobb felső sarokban
  lv_obj_t *xb = make_shape(card, LCD_H_RES - FS_XBTN - SX(12), SY(10), FS_XBTN, FS_XBTN - S(24), S(12), 0x3A3A3A);
  lv_obj_t *xl = lv_label_create(xb);
  lv_label_set_text(xl, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_font(xl, &FONT_SYM, 0);
  lv_obj_set_style_text_color(xl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_center(xl);

  fs_chart = lv_chart_create(card);
  lv_obj_set_pos(fs_chart, FS_CX, FS_CY);
  lv_obj_set_size(fs_chart, FS_CW, FS_CH);
  lv_chart_set_type(fs_chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(fs_chart, CH_N_DAY);
  lv_chart_set_div_line_count(fs_chart, 5, 5);     // 5 vonal = a szélek és a negyedek (a feliratok ezekhez igazodnak)
  lv_obj_set_style_bg_opa(fs_chart, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(fs_chart, 0, 0);
  lv_obj_set_style_pad_all(fs_chart, 0, 0);
  lv_obj_set_style_radius(fs_chart, 0, 0);
  lv_obj_set_style_line_color(fs_chart, lv_color_hex(0x3A3A3A), LV_PART_MAIN);
  lv_obj_set_style_line_width(fs_chart, 1, LV_PART_MAIN);
  lv_obj_set_style_line_width(fs_chart, S(4), LV_PART_ITEMS);
  lv_obj_set_style_size(fs_chart, 0, 0, LV_PART_INDICATOR);
  lv_obj_set_scrollable(fs_chart, false);

  for (int k = 0; k < 5; k++)
    fs_ylab[k] = dlabel_r(card, "", &font_hu_28, COL_GREY, FS_CX - SX(10), FS_CY + (FS_CH - 1) * k / 4 - S(16), SX(84));
  for (int k = 0; k < 7; k++)
    fs_xlab[k] = dlabel_c(card, "", &font_hu_28, COL_GREY, FS_CX - SX(50), FS_CY + FS_CH + SY(12), SX(100));
  lv_obj_set_hidden(fs_page, true);
}

// Az adatok és a tengelyfeliratok frissítése (a hist_update_charts már feltöltötte a cd tömböket)
static void fs_refresh() {
  if (fs_idx < 0 || !fs_chart) return;
  const ChartDef &d = CHARTS[fs_idx];
  int32_t lo, hi;
  chart_bounds(d, &lo, &hi);
  lv_chart_set_axis_range(fs_chart, LV_CHART_AXIS_PRIMARY_Y, lo, hi);

  lv_chart_set_point_count(fs_chart, ch_n());
  lv_chart_set_div_line_count(fs_chart, 5, ch_mode == 2 ? 7 : ch_mode ? 8 : 5);
  lv_chart_set_type(fs_chart, (ch_mode == 2 && fs_idx == HC_RAIN) ? LV_CHART_TYPE_BAR : LV_CHART_TYPE_LINE);
  for (int k = 0; k < 3; k++) if (fs_se[k]) lv_chart_set_series_ext_y_array(fs_chart, fs_se[k], cd[chf(d, k)]);
  char t[48];
  static const char *const MONTHS_LONG_T[2][12] = {{"január", "február", "március", "április", "május", "június",
                                                    "július", "augusztus", "szeptember", "október", "november", "december"},
                                                   {"January", "February", "March", "April", "May", "June",
                                                    "July", "August", "September", "October", "November", "December"}};
  const char *const *MONTHS_LONG = TR2(MONTHS_LONG_T);
  struct tm lt;
  const char *per = ch_mode == 1 ? tr("7 nap", "7 days") : tr("ma", "today");
  if (ch_mode == 2) per = get_local_tm(lt) ? MONTHS_LONG[lt.tm_mon] : tr("hónap", "month");
  snprintf(t, sizeof(t), "%s (%s)", chart_tr(d.fs_title), per);
  set_label_if_changed(fs_title, t);
  for (int k = 0; k < 7; k++) {
    set_label_if_changed(fs_xlab[k], ch_x_text(k, t, sizeof(t)));
    lv_obj_set_x(fs_xlab[k], FS_CX + ch_x_pos(k, FS_CW - 1, SX(40)) - SX(50));
  }
  for (int k = 0; k < 5; k++) {
    float v = (hi - (hi - lo) * k / 4.0f) / (float)d.div;
    if (d.div > 1 && (hi - lo) <= 100) snprintf(t, sizeof(t), "%.1f", v); else snprintf(t, sizeof(t), "%.0f", v);
    set_label_if_changed(fs_ylab[k], t);
  }
  for (int k = 0; k < 3; k++) {
    if (k < d.n) series_text(d, k, true, t, sizeof(t)); else t[0] = 0;
    set_label_if_changed(fs_v[k], t);
  }
  lv_chart_refresh(fs_chart);
}

static void fs_open(int idx) {
  if (idx < 0 || idx >= HC_COUNT || !fs_page || !cd) return;
  hist_update_charts();   // friss adatsorok
  const ChartDef &d = CHARTS[idx];

  for (int k = 0; k < 3; k++) {
    if (fs_se[k]) { lv_chart_remove_series(fs_chart, fs_se[k]); fs_se[k] = NULL; }
  }
  for (int k = 0; k < d.n; k++) {
    fs_se[k] = lv_chart_add_series(fs_chart, lv_color_hex(d.col[k]), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_series_ext_y_array(fs_chart, fs_se[k], cd[chf(d, k)]);
  }

  lv_obj_set_style_text_color(fs_title, lv_color_hex(d.frame), 0);
  lv_obj_set_style_border_color(fs_card, lv_color_hex(d.frame), 0);
  for (int k = 0; k < 3; k++) {
    bool on = k < d.n;
    lv_obj_set_hidden(fs_sw[k], !on);
    lv_obj_set_hidden(fs_v[k], !on);
    if (!on) continue;
    lv_obj_set_style_bg_color(fs_sw[k], lv_color_hex(d.col[k]), 0);
    lv_obj_set_style_text_color(fs_v[k], lv_color_hex(d.col[k]), 0);
  }

  fs_idx = idx;
  fs_refresh();
  lv_obj_set_hidden(fs_page, false);
}

static void fs_close() {
  fs_idx = -1;
  if (fs_page) lv_obj_set_hidden(fs_page, true);
}

// Melyik grafikonkártya van a megadott pontban az aktuális oldalon (-1: egyik sem)
static int hist_card_at(int x, int y) {
  for (int i = 0; i < HC_COUNT; i++) {
    const ChartDef &d = CHARTS[i];
    if (d.page != current_page) continue;
    int cx, cy, cw;
    card_rect(d, &cx, &cy, &cw);
    if (x >= cx && x < cx + cw && y >= cy && y < cy + HC_H) return i;
  }
  return -1;
}

