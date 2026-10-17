// Beállítások képernyő (a főoldal jobb felső sarkában a fogaskerékkel nyílik): éjszakai mód, hely (számbillentyűzet),
// időzóna, WiFi (beállító portál) és rendszer (adatok, újraindítás). A változások bezáráskor kerülnek az NVS-be,
// ugyanoda, ahova a webes kezelőoldal ment.
// (Az idojaras.ino része: az Arduino IDE és a PlatformIO a fájlokat ábécésorrendben, egy programmá fűzi össze.)

constexpr int ST_TAB_X = SX(20), ST_TAB_Y = SY(84), ST_TAB_W = SX(220), ST_TAB_H = SY(70), ST_TAB_GAP = SY(10);
constexpr int ST_CX = SX(270);                     // a tartalom bal széle
constexpr int ST_ROW0 = SY(84), ST_ROW_H = SY(76);     // az éjszaka fül sorai
constexpr int ST_BTN_X = SX(770), ST_BTN_W = SX(96), ST_BTN_H = SY(60), ST_TOG_W = SX(206);
constexpr int ST_XB_W = S(84), ST_XB_H = SY(60);     // bezáró gomb (jobb felső sarok)
constexpr int ST_KX = SX(620), ST_KY = SY(96), ST_KW = SX(84), ST_KH = SY(70), ST_KG = SX(10);   // számbillentyűzet
constexpr int ST_FW = SX(320), ST_FH = SY(84), ST_FGAP = SY(26);     // a hely mezői
constexpr int ST_CF_X = SX(262), ST_CF_Y = SY(170), ST_CF_W = SX(500), ST_CF_H = SY(250);    // megerősítő ablak
constexpr int ST_ACT_W = SX(380);     // a WiFi / Újraindítás gomb szélessége
constexpr uint32_t ST_IDLE_MS = 120000;            // ennyi tétlenség után bezárul (és ment)
enum { ST_T_NIGHT, ST_T_LOC, ST_T_TZ, ST_T_WIFI, ST_T_SYS, ST_T_COUNT };
enum { ST_A_NONE, ST_A_PORTAL, ST_A_RESTART, ST_A_LANG, ST_A_DEMO };
constexpr int ST_LANG_X = ST_CX + ST_ACT_W + SX(20), ST_LANG_W = SX(330);
constexpr int ST_TZB_W = SX(110), ST_TZB_H = SY(70), ST_TZR_X = ST_CX + SX(620);   // időzóna: lapozó gombok
constexpr int ST_WIFI_Y = SY(300), ST_SYS_Y = SY(440), ST_TXT_W = SX(730), ST_DEMO_Y = SY(360);   // ST_DEMO_Y: a Bemutató gomb (Rendszer fül)          // a WiFi / Rendszer fül gombjai, a szövegek szélessége
constexpr int ST_CF_BX = SX(40), ST_CF_BY = ST_CF_H - SY(90), ST_CF_BW = SX(180);  // a megerősítő ablak gombjai (az ablakon belül)   // a nyelvváltó gomb (Rendszer fül)
static const char *const ST_TAB_NAME_T[2][ST_T_COUNT] = {{"Éjszaka", "Hely", "Időzóna", "WiFi", "Rendszer"},
                                                         {"Night", "Location", "Time zone", "WiFi", "System"}};
#define ST_TAB_NAME TR2(ST_TAB_NAME_T)
static const char *ST_KEYS[16] = {"7", "8", "9", LV_SYMBOL_BACKSPACE, "4", "5", "6", "-", "1", "2", "3", ".", "0", "C", "", ""};

static lv_obj_t *st_ov = NULL, *st_tab[ST_T_COUNT] = {}, *st_grp[ST_T_COUNT] = {};
static int st_cur = ST_T_NIGHT, st_action = ST_A_NONE;
static uint32_t st_last_ms = 0, st_info_ms = 0;
static bool st_night_dirty = false, st_loc_dirty = false, st_tz_dirty = false;
static lv_obj_t *st_n_tog[BP_R_COUNT] = {}, *st_n_tog_l[BP_R_COUNT] = {}, *st_n_lbl[BP_R_COUNT] = {};
static char st_ll[2][16] = {};                     // a hely mezőinek szövege (szélesség, hosszúság)
static int st_field = 0;
static lv_obj_t *st_f_box[2] = {}, *st_f_val[2] = {}, *st_loc_msg = NULL;
static int st_tz_idx = -1;
static char st_tz[64] = "";
static lv_obj_t *st_tz_name = NULL, *st_tz_posix = NULL, *st_wifi_info = NULL, *st_sys_info = NULL;
static lv_obj_t *st_snd = NULL, *st_snd_l = NULL;   // P4: hangjelzés be/ki (Rendszer fül)
static lv_obj_t *st_cf = NULL, *st_cf_txt = NULL, *st_cf_yes = NULL, *st_cf_no = NULL;   // megerősítő ablak (gombfeliratok)

static int st_tz_count() { return (int)(sizeof(TZ_LIST) / sizeof(TZ_LIST[0])); }

static lv_obj_t *st_text(lv_obj_t *parent, const char *txt, const lv_font_t *font, uint32_t col, int x, int y, int w) {
  lv_obj_t *l = dlabel(parent, txt, font, col, x, y);
  if (w > 0) lv_obj_set_width(l, w);
  return l;
}

// a hely mezőinek ellenőrzése: mindkettő üres (= nincs hely) vagy mindkettő érvényes szám
static bool st_loc_valid(double *lat, double *lon, bool *empty) {
  *empty = !st_ll[0][0] && !st_ll[1][0];
  if (*empty) return true;
  char *e1, *e2;
  *lat = strtod(st_ll[0], &e1);
  *lon = strtod(st_ll[1], &e2);
  return st_ll[0][0] && st_ll[1][0] && !*e1 && !*e2 && *lat >= -90 && *lat <= 90 && *lon >= -180 && *lon <= 180 &&
         !(*lat == 0 && *lon == 0);
}

static void st_refresh() {
  if (!st_ov) return;
  char b[96];
  if (st_snd) {   // P4: hangjelzés be/ki
    set_label_if_changed(st_snd_l, cfg.snd_on ? tr("Hangjelzés: be", "Sound: on") : tr("Hangjelzés: ki", "Sound: off"));
    lv_obj_set_style_bg_color(st_snd, lv_color_hex(cfg.snd_on ? 0x2E7D32 : 0x555555), 0);
  }
  for (int t = 0; t < ST_T_COUNT; t++) {
    lv_obj_set_hidden(st_grp[t], t != st_cur);
    lv_obj_set_style_bg_color(st_tab[t], lv_color_hex(t == st_cur ? 0x5A4A10 : 0x3A3A3A), 0);
  }
  // éjszaka
  for (int r = 0; r < BP_R_COUNT; r++) {   // ugyanazok a sorok, mint a fényerő panelen
    night_row_text(r, b, sizeof(b));
    set_label_if_changed(st_n_lbl[r], b);
    if (night_row_toggle(r)) {
      set_label_if_changed(st_n_tog_l[r], night_toggle_text(r));
      lv_obj_set_style_bg_color(st_n_tog[r], lv_color_hex(night_toggle_color(r)), 0);
    }
  }
  // hely
  for (int i = 0; i < 2; i++) {
    set_label_if_changed(st_f_val[i], st_ll[i][0] ? st_ll[i] : "--");
    lv_obj_set_style_border_color(st_f_box[i], lv_color_hex(i == st_field ? COL_YELLOW : 0x555555), 0);
  }
  double la, lo;
  bool empty;
  const bool ok = st_loc_valid(&la, &lo, &empty);
  set_label_if_changed(st_loc_msg, empty ? tr("Mindkét mező üres: nincs hely (a napkelte és az előrejelzés nem működik).", "Both fields empty: no location (sunrise and forecast will not work).")
                                         : ok ? tr("Érvényes hely. Bezáráskor mentődik.", "Valid location. Saved when you close.")
                                              : tr("Hibás érték: szélesség -90..90, hosszúság -180..180 (tizedespont: \".\").", "Invalid value: latitude -90..90, longitude -180..180 (decimal point: \".\")."));
  lv_obj_set_style_text_color(st_loc_msg, lv_color_hex(ok ? COL_GREY : COL_WARN), 0);
  // időzóna
  if (st_tz_idx >= 0) set_label_if_changed(st_tz_name, tr(TZ_LIST[st_tz_idx].name, TZ_LIST[st_tz_idx].name_en));
  else                set_label_if_changed(st_tz_name, tr("Egyéni időzóna (a kezelőoldalon beállítva)", "Custom time zone (set on the web page)"));
  snprintf(b, sizeof(b), "POSIX: %s", st_tz);
  set_label_if_changed(st_tz_posix, b);
}

// a WiFi és a Rendszer fül adatai (másodpercenként)
static void st_refresh_info() {
  if (!st_ov) return;
  char b[400];
  if (WiFi.status() == WL_CONNECTED)
    snprintf(b, sizeof(b), tr("Hálózat: %s\nJelerősség: %d dBm (adóteljesítmény: %s)\nIP-cím: %s\nNév a hálózaton: " MDNS_HOST ".local", "Network: %s\nSignal: %d dBm (TX power: %s)\nIP address: %s\nName on the network: " MDNS_HOST ".local"),
             WiFi.SSID().c_str(), (int)WiFi.RSSI(), wifiTxName(), WiFi.localIP().toString().c_str());
  else
    snprintf(b, sizeof(b), tr("Nincs WiFi-kapcsolat.", "No WiFi connection."));
  set_label_if_changed(st_wifi_info, b);

  const uint32_t up = millis() / 1000;
  // a memória egy sorban, hogy a P4-en az akku is elférjen (a Bemutató gomb fölött legfeljebb 7 sor)
  snprintf(b, sizeof(b), tr("Program: %s\nIP-cím: %s\nÜzemidő: %u nap %u óra %u perc\nSzabad RAM: %u KB, LVGL: %u %% foglalt\n"
                            "Legutóbbi indulás: %s\nTelefonos értesítés: %s%s%s",
                            "Firmware: %s\nIP address: %s\nUptime: %u d %u h %u min\nFree RAM: %u KB, LVGL: %u %% used\n"
                            "Last start: %s\nPhone notifications: %s%s%s"),
           FW_NAME, WiFi.localIP().toString().c_str(), (unsigned)(up / 86400), (unsigned)(up / 3600 % 24), (unsigned)(up / 60 % 60),
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024), lvgl_used_pct(), reset_text(boots[0].reason, boots[0].cause),
           cfg.nt_topic[0] ? tr("be (kezelőoldalon állítható)", "on (set on the web page)") : tr("ki (kezelőoldalon állítható)", "off (set on the web page)"),
           KIJELZO_P4 ? "\n" : "", batt_short());
  set_label_if_changed(st_sys_info, b);
}

static void create_settings_screen() {
  const lv_font_t *hu = &font_hu_28, *sm = &font_hu_14;
  st_ov = make_group(lv_screen_active(), 0, 0, LCD_H_RES, LCD_V_RES);
  lv_obj_t *card = make_shape(st_ov, 0, 0, LCD_H_RES, LCD_V_RES, 0, 0x161616);
  dlabel(card, tr("Beállítások", "Settings"), hu, COL_YELLOW, SX(20), SY(20));
  lv_obj_t *xb = make_shape(card, LCD_H_RES - ST_XB_W - SX(12), SY(10), ST_XB_W, ST_XB_H, S(12), 0x3A3A3A);
  lv_obj_t *xl = lv_label_create(xb);
  lv_label_set_text(xl, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_font(xl, &FONT_SYM, 0);
  lv_obj_set_style_text_color(xl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_center(xl);

  for (int t = 0; t < ST_T_COUNT; t++) {
    st_tab[t] = bp_button(card, ST_TAB_X, ST_TAB_Y + t * (ST_TAB_H + ST_TAB_GAP), ST_TAB_W, ST_TAB_NAME[t], 0x3A3A3A, NULL);
    lv_obj_set_height(st_tab[t], ST_TAB_H);
    st_grp[t] = make_group(card, 0, 0, LCD_H_RES, LCD_V_RES);
  }

  // Éjszaka: ugyanazok a sorok, mint a fényerő panelen
  lv_obj_t *g = st_grp[ST_T_NIGHT];
  for (int r = 0; r < BP_R_COUNT; r++) {
    const int y = ST_ROW0 + r * ST_ROW_H;
    st_n_lbl[r] = dlabel(g, "", hu, COL_TEXT, ST_CX, y + SY(14));
    if (night_row_toggle(r)) {
      st_n_tog[r] = bp_button(g, ST_BTN_X, y, ST_TOG_W, "", 0x555555, &st_n_tog_l[r]);
    } else {
      bp_button(g, ST_BTN_X, y, ST_BTN_W, "-", 0x3A3A3A, NULL);
      bp_button(g, ST_BTN_X + ST_TOG_W - ST_BTN_W, y, ST_BTN_W, "+", 0x3A3A3A, NULL);
    }
  }
  st_text(g, tr("Időzítés: óra szerint (fix időpontok) vagy napnyugta szerint (napnyugta + eltolás - napkelte + eltolás, "
                "15 perces lépésekben; hely kell hozzá). Gyors elérés: koppintás az órára a főoldalon.",
                "Schedule: by clock (fixed times) or by sunset (sunset + offset to sunrise + offset, in 15-minute steps; "
                "needs the location). Quick access: tap the clock on the main page."),
          sm, COL_GREY, ST_CX, ST_ROW0 + 6 * ST_ROW_H + SY(4), ST_TXT_W);

  // Hely: két mező és számbillentyűzet
  g = st_grp[ST_T_LOC];
  const char *fl[2] = {tr("Földrajzi szélesség", "Latitude"), tr("Földrajzi hosszúság", "Longitude")};
  for (int i = 0; i < 2; i++) {
    st_f_box[i] = make_shape(g, ST_CX, ST_KY + i * (ST_FH + ST_FGAP), ST_FW, ST_FH, S(10), 0x262626);
    lv_obj_set_style_border_width(st_f_box[i], S(2), 0);
    dlabel(st_f_box[i], fl[i], sm, COL_GREY, SX(12), SY(6));
    st_f_val[i] = dlabel(st_f_box[i], "", hu, COL_TEXT, SX(12), SY(34));
  }
  st_loc_msg = st_text(g, "", sm, COL_GREY, ST_CX, ST_KY + 2 * (ST_FH + ST_FGAP) + SY(4), ST_FW);
  st_text(g, tr("Koppints a mezőre, majd írd be a számot. Példa (Budapest): 47.4979 és 19.0402. "
                "A webes kezelőoldalon IP alapján is megkereshető.",
                "Tap a field, then type the number. Example (London): 51.5074 and -0.1278. "
                "It can also be looked up by IP on the web page."), sm, COL_GREY, ST_CX, SY(470), ST_FW);
  for (int k = 0; k < 14; k++) {
    const int x = ST_KX + (k % 4) * (ST_KW + ST_KG), y = ST_KY + (k / 4) * (ST_KH + ST_KG);
    lv_obj_t *b = make_shape(g, x, y, ST_KW, ST_KH, S(10), k == 13 ? 0x5A3A3A : 0x3A3A3A);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, ST_KEYS[k]);
    lv_obj_set_style_text_font(l, k == 3 ? &FONT_SYM : hu, 0);   // a törlés jele a beépített szimbólumfontból
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(l);
  }

  // Időzóna: lapozás a listában
  g = st_grp[ST_T_TZ];
  st_tz_name = dlabel_c(g, "", hu, COL_TEXT, ST_CX + SX(130), ST_KY + SY(18), SX(480));
  lv_label_set_long_mode(st_tz_name, LV_LABEL_LONG_WRAP);
  lv_obj_t *lb = make_shape(g, ST_CX, ST_KY, ST_TZB_W, ST_TZB_H, S(10), 0x3A3A3A), *rb = make_shape(g, ST_TZR_X, ST_KY, ST_TZB_W, ST_TZB_H, S(10), 0x3A3A3A);
  lv_obj_t *ll = lv_label_create(lb), *rl = lv_label_create(rb);
  lv_label_set_text(ll, LV_SYMBOL_LEFT); lv_label_set_text(rl, LV_SYMBOL_RIGHT);
  lv_obj_set_style_text_font(ll, &FONT_SYM, 0); lv_obj_set_style_text_font(rl, &FONT_SYM, 0);
  lv_obj_set_style_text_color(ll, lv_color_hex(0xFFFFFF), 0); lv_obj_set_style_text_color(rl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_center(ll); lv_obj_center(rl);
  st_tz_posix = st_text(g, "", sm, COL_GREY, ST_CX, ST_KY + SY(110), ST_TXT_W);
  st_text(g, tr("A nyári/téli időszámítás az időzónában benne van. Bezáráskor mentődik.", "Daylight saving time is included in the time zone. Saved when you close."), sm, COL_GREY, ST_CX, ST_KY + SY(140), ST_TXT_W);

  // WiFi
  g = st_grp[ST_T_WIFI];
  st_wifi_info = st_text(g, "", hu, COL_TEXT, ST_CX, ST_KY, ST_TXT_W);
  bp_button(g, ST_CX, ST_WIFI_Y, ST_ACT_W, tr("WiFi beállítása...", "Set up WiFi..."), 0x1F4A6A, NULL);
  st_text(g, tr("Újraindul, és 5 percre elindítja a \"Weather-Setup\" hálózatot: csatlakozz rá telefonnal, válaszd ki a hálózatot "
                "és add meg a jelszavát. Ha nem adsz meg újat, 5 perc múlva visszacsatlakozik a mentett hálózatra.",
                "Restarts and opens the \"Weather-Setup\" network for 5 minutes: connect your phone to it, choose your network "
                "and enter its password. If you do not set a new one, it reconnects to the saved network after 5 minutes."),
          sm, COL_GREY, ST_CX, ST_WIFI_Y + SY(80), ST_TXT_W);

  // Rendszer
  g = st_grp[ST_T_SYS];
  st_sys_info = st_text(g, "", hu, COL_TEXT, ST_CX, ST_KY, ST_TXT_W);
  bp_button(g, ST_CX, ST_SYS_Y, ST_ACT_W, tr("Újraindítás...", "Restart..."), 0x6A2A1F, NULL);
  bp_button(g, ST_LANG_X, ST_SYS_Y, ST_LANG_W, tr("Language: English", "Nyelv: magyar"), 0x2A4A2A, NULL);   // a másik nyelvre vált
#if KIJELZO_P4
  st_snd = bp_button(g, ST_LANG_X, ST_DEMO_Y, ST_LANG_W, "", 0x555555, &st_snd_l);
#endif
  bp_button(g, ST_CX, ST_DEMO_Y, ST_ACT_W, g_demo ? tr("Bemutató kikapcsolása...", "Turn off demo...") : tr("Bemutató mód...", "Demo mode..."),
            0x4A3A6A, NULL);
  st_text(g, tr("A kezelőoldal jelszava és a percre pontos időpontok a webes kezelőoldalon állíthatók: http://" MDNS_HOST ".local/",
                "The web page password and minute-exact times can be set on the web page: http://" MDNS_HOST ".local/"),
          sm, COL_GREY, ST_CX, ST_SYS_Y + SY(80), ST_TXT_W);

  // megerősítő ablak
  st_cf = make_group(st_ov, 0, 0, LCD_H_RES, LCD_V_RES);
  lv_obj_set_style_bg_color(st_cf, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(st_cf, LV_OPA_60, 0);
  lv_obj_t *cb = make_shape(st_cf, ST_CF_X, ST_CF_Y, ST_CF_W, ST_CF_H, S(16), 0x1E1E1E);
  lv_obj_set_style_border_width(cb, S(2), 0);
  lv_obj_set_style_border_color(cb, lv_color_hex(COL_YELLOW), 0);
  st_cf_txt = dlabel_c(cb, "", hu, COL_TEXT, SX(20), SY(40), ST_CF_W - SX(40));
  lv_label_set_long_mode(st_cf_txt, LV_LABEL_LONG_WRAP);
  bp_button(cb, ST_CF_BX, ST_CF_BY, ST_CF_BW, tr("Igen", "Yes"), 0x2E7D32, &st_cf_yes);
  bp_button(cb, ST_CF_W - ST_CF_BX - ST_CF_BW, ST_CF_BY, ST_CF_BW, tr("Mégse", "Cancel"), 0x555555, &st_cf_no);
  lv_obj_set_hidden(st_cf, true);
  lv_obj_set_hidden(st_ov, true);
}

static bool settings_is_open() { return st_ov && !lv_obj_is_hidden(st_ov); }

static void settings_open() {
  if (!st_ov) create_settings_screen();   // az első megnyitáskor jön létre (az LVGL memóriája véges)
  if (!st_ov) return;
  st_night_dirty = st_loc_dirty = st_tz_dirty = false;
  st_action = ST_A_NONE;
  st_field = 0;
  if (cfg.has_loc) {
    snprintf(st_ll[0], sizeof(st_ll[0]), "%.4f", cfg.lat);
    snprintf(st_ll[1], sizeof(st_ll[1]), "%.4f", cfg.lon);
  } else {
    st_ll[0][0] = st_ll[1][0] = 0;
  }
  strlcpy(st_tz, cfg.tz, sizeof(st_tz));
  st_tz_idx = -1;
  for (int i = 0; i < st_tz_count(); i++) if (strcmp(TZ_LIST[i].posix, st_tz) == 0) st_tz_idx = i;
  st_last_ms = st_info_ms = millis();
  lv_obj_set_hidden(st_cf, true);
  st_refresh();
  st_refresh_info();
  lv_obj_set_hidden(st_ov, false);
}

static void settings_close() {
  if (!settings_is_open()) return;
  lv_obj_set_hidden(st_ov, true);
  bl_preview_until = 0;
  if (st_night_dirty) cfg_save_night();
  if (st_tz_dirty) {
    CamPause cam_pause_;   // P4: a kamera áll a mentés alatt
    Preferences p;
    p.begin("cfg", false);
    p.putString("tz", st_tz);
    p.end();
  }
  double la = 0, lo = 0;
  bool empty;
  if (st_loc_dirty && st_loc_valid(&la, &lo, &empty)) {
    CamPause cam_pause_;   // P4: a kamera áll a mentés alatt
    Preferences p;
    p.begin("cfg", false);
    if (empty) { p.remove("lat"); p.remove("lon"); }
    else       { p.putDouble("lat", la); p.putDouble("lon", lo); }
    p.end();
    fc.valid = false; fc_force = true;   // új hely: új előrejelzés
    sun_last_key = -1;
  }
  if (st_night_dirty || st_tz_dirty || st_loc_dirty) {
    cfg_load();
    cfg_apply_tz();
    LogOut.println("Beállítások mentve (a kijelzőn)");
  }
  if (st_action == ST_A_LANG) lang_save(!g_lang);
  if (st_action == ST_A_DEMO) demo_save(!g_demo);
  if (st_action == ST_A_RESTART || st_action == ST_A_PORTAL || st_action == ST_A_LANG || st_action == ST_A_DEMO) {
    LogOut.println(st_action == ST_A_PORTAL ? "WiFi beállítása: újraindítás a portálhoz" :
                   st_action == ST_A_DEMO ? (g_demo ? "Bemutató mód ki: újraindítás" : "Bemutató mód be: újraindítás") : "Kézi újraindítás");
    hist_save();
    delay(300);
    diag_mark_restart(st_action == ST_A_PORTAL ? RC_WIFISET : RC_MANUAL);
    ESP.restart();
  }
}

static void st_key(int k) {
  char *f = st_ll[st_field];
  size_t n = strlen(f);
  const char *key = ST_KEYS[k];
  if (k == 3) { if (n) f[n - 1] = 0; }
  else if (k == 13) f[0] = 0;
  else if (key[0] == '-') { if (n == 0) { f[0] = '-'; f[1] = 0; } }   // mínusz csak az elején
  else if (key[0] == '.') { if (!strchr(f, '.') && n < sizeof(st_ll[0]) - 1) { f[n] = '.'; f[n + 1] = 0; } }
  else if (n < 10) { f[n] = key[0]; f[n + 1] = 0; }
  st_loc_dirty = true;
}

// Koppintás a nyitott beállítás-képernyőn (képernyő-koordináták)
static void settings_tap(int x, int y) {
  st_last_ms = millis();
  if (!lv_obj_is_hidden(st_cf)) {   // megerősítő ablak
    const int bx = ST_CF_X, by = ST_CF_Y + ST_CF_BY;
    if (in_rect(x, y, bx + ST_CF_BX, by, ST_CF_BW, ST_BTN_H)) { settings_close(); return; }          // Igen (a művelet bezáráskor fut)
    if (in_rect(x, y, bx + ST_CF_W - ST_CF_BX - ST_CF_BW, by, ST_CF_BW, ST_BTN_H)) { st_action = ST_A_NONE; lv_obj_set_hidden(st_cf, true); }
    return;
  }
  if (in_rect(x, y, LCD_H_RES - ST_XB_W - SX(12), SY(10), ST_XB_W, ST_XB_H)) { settings_close(); return; }
  for (int t = 0; t < ST_T_COUNT; t++) {
    if (in_rect(x, y, ST_TAB_X, ST_TAB_Y + t * (ST_TAB_H + ST_TAB_GAP), ST_TAB_W, ST_TAB_H)) {
      st_cur = t; st_info_ms = 0; st_refresh(); st_refresh_info(); return;
    }
  }
  switch (st_cur) {
    case ST_T_NIGHT: {
      const int plus_x = ST_BTN_X + ST_TOG_W - ST_BTN_W;
      for (int r = 0; r < BP_R_COUNT; r++) {
        const int ry = ST_ROW0 + r * ST_ROW_H;
        if (night_row_toggle(r)) {
          if (in_rect(x, y, ST_BTN_X, ry, ST_TOG_W, ST_BTN_H)) { night_row_action(r, 0); st_night_dirty = true; }
          continue;
        }
        const int dir = in_rect(x, y, ST_BTN_X, ry, ST_BTN_W, ST_BTN_H) ? -1 : in_rect(x, y, plus_x, ry, ST_BTN_W, ST_BTN_H) ? 1 : 0;
        if (dir) { night_row_action(r, dir); st_night_dirty = true; }
      }
      break;
    }
    case ST_T_LOC:
      for (int i = 0; i < 2; i++) if (in_rect(x, y, ST_CX, ST_KY + i * (ST_FH + ST_FGAP), ST_FW, ST_FH)) st_field = i;
      for (int k = 0; k < 14; k++)
        if (in_rect(x, y, ST_KX + (k % 4) * (ST_KW + ST_KG), ST_KY + (k / 4) * (ST_KH + ST_KG), ST_KW, ST_KH)) st_key(k);
      break;
    case ST_T_TZ: {
      int dir = in_rect(x, y, ST_CX, ST_KY, ST_TZB_W, ST_TZB_H) ? -1 : in_rect(x, y, ST_TZR_X, ST_KY, ST_TZB_W, ST_TZB_H) ? 1 : 0;
      if (dir) {
        const int n = st_tz_count();
        st_tz_idx = st_tz_idx < 0 ? 0 : (st_tz_idx + dir + n) % n;
        strlcpy(st_tz, TZ_LIST[st_tz_idx].posix, sizeof(st_tz));
        st_tz_dirty = true;
      }
      break;
    }
    case ST_T_WIFI:
      if (in_rect(x, y, ST_CX, ST_WIFI_Y, ST_ACT_W, ST_BTN_H)) {
        st_action = ST_A_PORTAL;
        lv_label_set_text(st_cf_yes, tr("Igen", "Yes")); lv_label_set_text(st_cf_no, tr("Mégse", "Cancel"));
        lv_label_set_text(st_cf_txt, tr("WiFi beállítása: a kijelző újraindul, és 5 percre elindítja a beállító portált. Mehet?", "WiFi setup: the display restarts and opens the setup portal for 5 minutes. Continue?"));
        lv_obj_set_hidden(st_cf, false);
      }
      break;
    case ST_T_SYS:
      if (st_snd && in_rect(x, y, ST_LANG_X, ST_DEMO_Y, ST_LANG_W, ST_BTN_H)) {   // azonnal mentjük
        cfg.snd_on = !cfg.snd_on;
        snd_save();
        LogOut.printf("Hangjelzés: %s (a kijelzőn)\n", cfg.snd_on ? "be" : "ki");
      } else if (in_rect(x, y, ST_CX, ST_SYS_Y, ST_ACT_W, ST_BTN_H)) {
        st_action = ST_A_RESTART;
        lv_label_set_text(st_cf_yes, tr("Igen", "Yes")); lv_label_set_text(st_cf_no, tr("Mégse", "Cancel"));
        lv_label_set_text(st_cf_txt, tr("Biztosan újraindítod a kijelzőt?", "Restart the display?"));
        lv_obj_set_hidden(st_cf, false);
      } else if (in_rect(x, y, ST_LANG_X, ST_SYS_Y, ST_LANG_W, ST_BTN_H)) {
        st_action = ST_A_LANG;   // a megerősítő szöveg és a gombok a választandó nyelven
        lv_label_set_text(st_cf_yes, g_lang ? "Igen" : "Yes"); lv_label_set_text(st_cf_no, g_lang ? "Mégse" : "Cancel");
        lv_label_set_text(st_cf_txt, g_lang ? "A felület magyarra vált, a kijelző újraindul. Mehet?"
                                            : "Switch the display to English? The display restarts.");
        lv_obj_set_hidden(st_cf, false);
      } else if (in_rect(x, y, ST_CX, ST_DEMO_Y, ST_ACT_W, ST_BTN_H)) {
        st_action = ST_A_DEMO;
        lv_label_set_text(st_cf_yes, tr("Igen", "Yes")); lv_label_set_text(st_cf_no, tr("Mégse", "Cancel"));
        lv_label_set_text(st_cf_txt, g_demo ? tr("Kilépés a bemutatóból: a kijelző újraindul, és újra a konzolok adatait mutatja. Mehet?",
                                                 "Leave demo mode? The display restarts and shows the consoles' data again.")
                                            : tr("Bemutató: kitalált adatok, magától lapozó oldalak (a valódi előzmények megmaradnak). "
                                                 "A kijelző újraindul. Mehet?",
                                                 "Demo: made-up data, pages turn by themselves (real history is kept). The display restarts. Continue?"));
        lv_obj_set_hidden(st_cf, false);
      }
      break;
  }
  st_refresh();
}

static void settings_task() {
  if (!settings_is_open()) return;
  if (millis() - st_last_ms > ST_IDLE_MS) { st_action = ST_A_NONE; settings_close(); return; }
  if ((st_cur == ST_T_WIFI || st_cur == ST_T_SYS) && millis() - st_info_ms >= 1000) { st_info_ms = millis(); st_refresh_info(); }
}
