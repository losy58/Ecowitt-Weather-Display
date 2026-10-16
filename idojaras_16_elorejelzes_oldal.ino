// Az Előrejelzés oldal (3 nap és a következő 12 óra)
// (Az idojaras.ino része: az Arduino IDE és a PlatformIO a fájlokat ábécésorrendben, egy programmá fűzi össze.)

// ---------------------------------------------------------------------------
// Előrejelzés oldal: három oszlop (ma, holnap, holnapután)
// ---------------------------------------------------------------------------
constexpr int FCOL_W = SX(324), FCOL_H = SY(330), FCOL_Y = SY(50), FCOL_GAP = SX(8);
constexpr int FH_Y = SY(388), FH_H = SY(172), FH_N = 12, FH_W = (LCD_H_RES - SX(40)) / FH_N;   // a "következő 12 óra" sáv
struct FcCol { lv_obj_t *zone, *day, *icon[WX_COUNT], *desc, *tmax, *tmin, *rain, *rain2, *wind; };
struct FhCell { lv_obj_t *hour, *temp, *pp, *mm; };
static FhCell fhc[FH_N] = {};
static lv_obj_t *fh_zone = NULL;   // icon[t]: csak ha már kellett (az LVGL memóriája véges)
static FcCol fcc[3] = {};
static lv_obj_t *fc_msg = NULL, *fc_info = NULL;
static const char *const MONTHS_SHORT_T[2][12] = {{"jan.", "febr.", "márc.", "ápr.", "máj.", "jún.", "júl.", "aug.", "szept.", "okt.", "nov.", "dec."},
                                                  {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"}};
#define MONTHS_SHORT TR2(MONTHS_SHORT_T)

// Időjárás-ikon egy S(230) x S(112)-es csoportba (a főoldal ikonjainak mintájára, középre igazítva)
static void make_wx_icon(lv_obj_t *g, int t) {
  const uint32_t CLOUD = 0xA9C3D6, CLOUD2 = 0x8FB0C8, DARK = 0x7F98AD, DROP = 0x4FC3F7;
  switch (t) {
    case WX_SUN:
      make_sun(g, S(59), 0, S(112), S(112), S(56), S(56), S(25), S(34), S(53), S(4.5f), 12, -1);
      break;
    case WX_PARTLY:
      make_sun(g, S(24), S(4), S(88), S(88), S(44), S(44), S(18), S(25), S(41), S(3.5f), 12, -1);
      make_cloud(g, S(94), S(40), S(120), CLOUD);
      break;
    case WX_CLOUDY:
      make_cloud(g, S(30), S(10), S(130), CLOUD);
      make_cloud(g, S(120), S(46), S(90), CLOUD2);
      break;
    case WX_FOG:
      make_cloud(g, S(50), 0, S(130), 0x9AA7B0);
      make_shape(g, S(40), S(80), S(150), S(8), S(4), 0x9AA7B0);
      make_shape(g, S(60), S(96), S(130), S(8), S(4), 0x8A969E);
      break;
    case WX_RAIN:
      make_cloud(g, S(50), S(4), S(130), DARK);
      make_shape(g, S(74), S(84), S(6), S(20), S(3), DROP);
      make_shape(g, S(108), S(90), S(6), S(20), S(3), DROP);
      make_shape(g, S(142), S(84), S(6), S(20), S(3), DROP);
      break;
    case WX_SNOW:
      make_cloud(g, S(50), S(4), S(130), 0x9FB4C6);
      make_shape(g, S(72), S(86), S(12), S(12), LV_RADIUS_CIRCLE, 0xFFFFFF);
      make_shape(g, S(106), S(96), S(12), S(12), LV_RADIUS_CIRCLE, 0xFFFFFF);
      make_shape(g, S(140), S(86), S(12), S(12), LV_RADIUS_CIRCLE, 0xFFFFFF);
      break;
    default:   // zivatar: sötét felhő, cikcakkos villám
      make_cloud(g, S(50), S(4), S(130), 0x6E8496);
      make_shape(g, S(112), S(72), S(10), S(16), S(2), 0xFFD54F);
      make_shape(g, S(104), S(86), S(18), S(8), S(2), 0xFFD54F);
      make_shape(g, S(104), S(92), S(10), S(18), S(2), 0xFFD54F);
      break;
  }
}

static void create_forecast_page() {
  const lv_font_t *hu = &font_hu_28;
  for (int k = 0; k < 3; k++) {
    FcCol &c = fcc[k];
    c.zone = make_zone(page_fc, SX(20) + k * (FCOL_W + FCOL_GAP), FCOL_Y, FCOL_W, FCOL_H, COL_BG_TL);
    c.day  = dlabel_c(c.zone, "", hu, COL_YELLOW, 0, SY(8), FCOL_W);     // pl. "Holnap, okt. 12."
    c.desc = dlabel_c(c.zone, "", hu, COL_TEXT, 0, SY(156), FCOL_W);
    c.tmax = dlabel_r(c.zone, "", hu, COL_ORANGE, FCOL_W / 2 - SX(10), SY(192), SX(150));   // max / min egy sorban
    dlabel_c(c.zone, "/", hu, COL_GREY, FCOL_W / 2 - SX(10), SY(192), SX(20));
    c.tmin = dlabel(c.zone, "", hu, COL_BLUE, FCOL_W / 2 + SX(10), SY(192));
    c.rain  = dlabel_c(c.zone, "", hu, COL_TEXT, 0, SY(228), FCOL_W);
    c.rain2 = dlabel_c(c.zone, "", hu, COL_TEXT, 0, SY(260), FCOL_W);
    c.wind  = dlabel_c(c.zone, "", hu, COL_TEXT, 0, SY(292), FCOL_W);
    // egy sor sosem törhet a következőre: ami nem fér ki, "..."-tal végződik
    lv_obj_t *one_line[] = {c.day, c.desc, c.rain, c.rain2, c.wind};
    for (lv_obj_t *l : one_line) lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_hidden(c.zone, true);
  }
  // következő 12 óra: óra, hőmérséklet, eső esélye és mennyisége
  fh_zone = make_zone(page_fc, SX(20), FH_Y, LCD_H_RES - SX(40), FH_H, COL_BG_TL);
  for (int i = 0; i < FH_N; i++) {
    const int x = i * FH_W;
    fhc[i].hour = dlabel_c(fh_zone, "", hu, COL_GREY, x, SY(10), FH_W);
    fhc[i].temp = dlabel_c(fh_zone, "", hu, COL_ORANGE, x, SY(52), FH_W);
    fhc[i].pp   = dlabel_c(fh_zone, "", hu, COL_BLUE, x, SY(94), FH_W);
    lv_obj_t *mm = lv_label_create(fh_zone);
    lv_obj_set_style_text_font(mm, &font_hu_14, 0);
    lv_obj_set_style_text_color(mm, lv_color_hex(COL_GREY), 0);
    lv_obj_set_width(mm, FH_W);
    lv_obj_set_style_text_align(mm, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(mm, x, SY(134));
    lv_label_set_text(mm, "");
    fhc[i].mm = mm;
  }
  lv_obj_set_hidden(fh_zone, true);
  fc_msg  = dlabel_c(page_fc, "", hu, COL_TEXT, 0, SY(280), LCD_H_RES);
  fc_info = small_label(page_fc, "", SX(20), SY(572), LCD_H_RES - SX(40), LV_TEXT_ALIGN_LEFT);
}

static void fc_update_ui() {
  if (!page_fc) return;
  char b[64];
  const int si = fc_first_today();
  const bool ok = cfg.has_loc && fc.valid && si >= 0;
  const char *msg = "";
  if (!cfg.has_loc)    msg = tr("Az előrejelzéshez add meg a helyet: " MDNS_HOST ".local", "Set the location for the forecast: " MDNS_HOST ".local");
  else if (!fc.valid)  msg = fc_err[0] ? tr("Az előrejelzés most nem érhető el", "The forecast is not available right now") : tr("Előrejelzés betöltése...", "Loading forecast...");
  else if (si < 0)     msg = tr("Az előrejelzés elavult, frissítés...", "The forecast is outdated, refreshing...");
  set_label_if_changed(fc_msg, msg);

  struct tm now;
  const bool have_t = get_local_tm(now);
  time_t today_noon = 0;
  if (have_t) { struct tm x = now; x.tm_hour = 12; x.tm_min = x.tm_sec = 0; today_noon = mktime(&x); }

  for (int k = 0; k < 3; k++) {
    FcCol &c = fcc[k];
    const int di = si + k;
    const bool on = ok && di < fc.n;
    lv_obj_set_hidden(c.zone, !on);
    if (!on) continue;
    const FcDay &d = fc.day[di];

    struct tm x = {};
    x.tm_year = d.y - 1900; x.tm_mon = d.m - 1; x.tm_mday = d.d; x.tm_hour = 12; x.tm_isdst = -1;
    time_t dt = mktime(&x);
    int diff = have_t ? (int)lround(difftime(dt, today_noon) / 86400.0) : k;
    const char *dn = diff == 0 ? tr("Ma", "Today") : diff == 1 ? tr("Holnap", "Tomorrow") : WEEKDAYS_LONG[x.tm_wday];
    snprintf(b, sizeof(b), tr("%s, %s %d.", "%s, %s %d"), dn, MONTHS_SHORT[(d.m - 1) % 12], d.d);
    set_label_if_changed(c.day, b);

    const int t = wx_type(d.code);
    if (!c.icon[t]) {   // az ikont az első használatkor rajzoljuk meg, utána megmarad
      c.icon[t] = make_group(c.zone, (FCOL_W - S(230)) / 2, SY(40), S(230), S(112));
      make_wx_icon(c.icon[t], t);
    }
    for (int i = 0; i < WX_COUNT; i++) if (c.icon[i]) lv_obj_set_hidden(c.icon[i], i != t);
    set_label_if_changed(c.desc, wx_text(d.code));

    snprintf(b, sizeof(b), "%.1f°", d.tmax);  set_label_if_changed(c.tmax, b);
    snprintf(b, sizeof(b), "%.1f°", d.tmin);  set_label_if_changed(c.tmin, b);
    if (isnan(d.pprob)) snprintf(b, sizeof(b), tr("Csapadék: --", "Precipitation: --"));
    else                snprintf(b, sizeof(b), tr("Csapadék: %.0f%%", "Precipitation: %.0f%%"), d.pprob);
    set_label_if_changed(c.rain, b);
    snprintf(b, sizeof(b), tr("Mennyiség: %.1f mm", "Amount: %.1f mm"), d.psum);
    set_label_if_changed(c.rain2, b);
    snprintf(b, sizeof(b), tr("Szél: max. %.0f km/h", "Wind: max %.0f km/h"), d.wind);
    set_label_if_changed(c.wind, b);
  }

  // következő 12 óra
  const int h0 = fc_first_hour();
  const bool hon = ok && h0 >= 0;
  lv_obj_set_hidden(fh_zone, !hon);
  for (int i = 0; hon && i < FH_N; i++) {
    const int hi = h0 + i;
    if (hi >= fc.nh) {
      set_label_if_changed(fhc[i].hour, ""); set_label_if_changed(fhc[i].temp, "");
      set_label_if_changed(fhc[i].pp, "");   set_label_if_changed(fhc[i].mm, "");
      continue;
    }
    const FcHour &h = fc.hour[hi];
    snprintf(b, sizeof(b), "%d:00", h.h);  set_label_if_changed(fhc[i].hour, b);
    snprintf(b, sizeof(b), "%.0f°", h.t);  set_label_if_changed(fhc[i].temp, b);
    if (isnan(h.pp)) b[0] = 0; else snprintf(b, sizeof(b), "%.0f%%", h.pp);
    set_label_if_changed(fhc[i].pp, b);
    // az eső esélye színnel is: halvány alacsonynál, erős kék 60 % fölött
    const uint32_t pc = isnan(h.pp) || h.pp < 30.0f ? COL_GREY : h.pp < 60.0f ? 0x7FB4D0 : 0x3EC6FF;
    lv_obj_set_style_text_color(fhc[i].pp, lv_color_hex(pc), 0);
    if (!isnan(h.mm) && h.mm >= 0.05f) snprintf(b, sizeof(b), "%.1f mm", h.mm); else b[0] = 0;
    set_label_if_changed(fhc[i].mm, b);
  }

  b[0] = 0;
  if (fc.valid && fc.fetched > 1600000000) {
    struct tm ft;
    localtime_r(&fc.fetched, &ft);
    snprintf(b, sizeof(b), tr("Forrás: Open-Meteo.com (CC BY 4.0), frissítve %02d:%02d%s", "Source: Open-Meteo.com (CC BY 4.0), updated %02d:%02d%s"), ft.tm_hour, ft.tm_min,
             fc_err[0] ? tr(" (utolsó próba hibás)", " (last attempt failed)") : "");
  } else if (fc_err[0]) {
    snprintf(b, sizeof(b), "Open-Meteo: %s", fc_err);
  }
  set_label_if_changed(fc_info, b);
}
