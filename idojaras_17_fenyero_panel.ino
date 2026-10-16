// Fényerő és éjszakai mód panel (koppintás az órára)
// (Az idojaras.ino része: az Arduino IDE és a PlatformIO a fájlokat ábécésorrendben, egy programmá fűzi össze.)

// ---------------------------------------------------------------------------
// Fényerő panel: a főoldalon az órára koppintva nyílik. Éjszakai mód be/ki, az éjszaka kezdete és vége
// (15 perces lépésekben), éjszakai és nappali fényerő. A változások a panel bezárásakor kerülnek a memóriába (NVS),
// 20 mp tétlenség után magától bezárul.
// ---------------------------------------------------------------------------
constexpr int BP_X = SX(192), BP_Y = SY(14), BP_W = SX(640), BP_H = SY(572);            // a panel helye és mérete
constexpr int BP_BTN_W = SX(96), BP_BTN_H = SY(60), BP_TOG_W = SX(206);             // gombok
constexpr int BP_ROW0 = SY(60), BP_ROW_H = SY(70);                             // az első sor teteje, sortávolság
constexpr int BP_CLOSE_Y = BP_ROW0 + 6 * BP_ROW_H + SY(10);
constexpr int BP_BX = SX(406);                                             // a gombok bal széle (a panelen belül)
constexpr int BP_TIME_STEP = 15;                                        // perc
constexpr uint32_t BP_IDLE_MS = 20000;
enum { BP_R_ON, BP_R_MODE, BP_R_START, BP_R_END, BP_R_NIGHT, BP_R_DAY, BP_R_COUNT };
constexpr int SUN_OFS_MIN = -120, SUN_OFS_MAX = 180;                   // a napnyugta/napkelte eltolás határai (perc)
static const int BL_STEPS[] = {1, 2, 3, 5, 8, 10, 15, 20, 30, 40, 50, 60, 70, 80, 90, 100};
static lv_obj_t *bp_ov = NULL, *bp_tog[BP_R_COUNT] = {}, *bp_tog_l[BP_R_COUNT] = {}, *bp_lbl[BP_R_COUNT] = {};
static bool bp_dirty = false;
static uint32_t bp_last_ms = 0;

static int bl_step(int pct, int dir, int min_pct) {
  const int n = sizeof(BL_STEPS) / sizeof(BL_STEPS[0]);
  if (dir > 0) { for (int i = 0; i < n; i++) if (BL_STEPS[i] > pct) return BL_STEPS[i]; return 100; }
  for (int i = n - 1; i >= 0; i--) if (BL_STEPS[i] < pct) return BL_STEPS[i] < min_pct ? min_pct : BL_STEPS[i];
  return min_pct;
}

// időpont léptetése (perc a nap kezdetétől), a 15 perces rácsra igazítva, éjfélen átfordulva
static int time_step(int m, int dir) {
  m = (dir > 0) ? (m / BP_TIME_STEP + 1) * BP_TIME_STEP : ((m + BP_TIME_STEP - 1) / BP_TIME_STEP - 1) * BP_TIME_STEP;
  return (m % 1440 + 1440) % 1440;
}

static int bp_row_y(int r) { return BP_ROW0 + r * BP_ROW_H; }

// ---- az éjszakai mód sorai: közös a fényerő panel és a Beállítások / Éjszaka fül számára ----
static bool night_row_toggle(int r) { return r == BP_R_ON || r == BP_R_MODE; }

static void night_row_text(int r, char *b, size_t n) {
  const bool sun = cfg.n_mode == 1;
  switch (r) {
    case BP_R_ON:    snprintf(b, n, tr("Éjszakai mód", "Night mode")); break;
    case BP_R_MODE:  snprintf(b, n, sun && !cfg.has_loc ? tr("Időzítés (hely kell!)", "Schedule (needs location!)") : tr("Időzítés", "Schedule")); break;
    case BP_R_START: if (sun) snprintf(b, n, tr("Kezdete: napnyugta %+d p", "Start: sunset %+d min"), cfg.n_so);
                     else     snprintf(b, n, tr("Kezdete: %02d:%02d", "Start: %02d:%02d"), cfg.n_st / 60, cfg.n_st % 60);
                     break;
    case BP_R_END:   if (sun) snprintf(b, n, tr("Vége: napkelte %+d p", "End: sunrise %+d min"), cfg.n_ro);
                     else     snprintf(b, n, tr("Vége: %02d:%02d", "End: %02d:%02d"), cfg.n_en / 60, cfg.n_en % 60);
                     break;
    case BP_R_NIGHT: snprintf(b, n, tr("Éjszakai fényerő: %d %%", "Night brightness: %d %%"), cfg.n_pct); break;
    default:         snprintf(b, n, tr("Nappali fényerő: %d %%", "Day brightness: %d %%"), cfg.d_pct); break;
  }
}

// a kapcsoló gomb felirata és színe
static const char *night_toggle_text(int r) { return r == BP_R_ON ? (cfg.n_on ? tr("BE", "ON") : tr("KI", "OFF")) : (cfg.n_mode ? tr("Napnyugta", "Sunset") : tr("Óra szerint", "By clock")); }
static uint32_t night_toggle_color(int r) { return r == BP_R_ON ? (cfg.n_on ? 0x2E7D32 : 0x555555) : (cfg.n_mode ? 0x1F5A6A : 0x555555); }

static int clamp_ofs(int v) { return v < SUN_OFS_MIN ? SUN_OFS_MIN : v > SUN_OFS_MAX ? SUN_OFS_MAX : v; }

// a sor művelete: kapcsolónál dir = 0, a -/+ gomboknál -1 / +1
static void night_row_action(int r, int dir) {
  const bool sun = cfg.n_mode == 1;
  switch (r) {
    case BP_R_ON:    cfg.n_on = !cfg.n_on; break;
    case BP_R_MODE:  cfg.n_mode = cfg.n_mode ? 0 : 1; break;
    case BP_R_START: if (sun) cfg.n_so = clamp_ofs(cfg.n_so + dir * BP_TIME_STEP); else cfg.n_st = time_step(cfg.n_st, dir); break;
    case BP_R_END:   if (sun) cfg.n_ro = clamp_ofs(cfg.n_ro + dir * BP_TIME_STEP); else cfg.n_en = time_step(cfg.n_en, dir); break;
    case BP_R_NIGHT:
      cfg.n_pct = bl_step(cfg.n_pct, dir, 1);
      bl_preview_pct = cfg.n_pct; bl_preview_until = millis() + 3000;   // 3 mp-ig így világít, hogy látszódjon
      break;
    default:
      cfg.d_pct = bl_step(cfg.d_pct, dir, 10);
      bl_preview_pct = cfg.d_pct; bl_preview_until = millis() + 3000;
      break;
  }
}

static lv_obj_t *bp_button(lv_obj_t *parent, int x, int y, int w, const char *txt, uint32_t bg, lv_obj_t **lbl_out) {
  lv_obj_t *b = make_shape(parent, x, y, w, BP_BTN_H, S(12), bg);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, &font_hu_28, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
  lv_obj_center(l);
  if (lbl_out) *lbl_out = l;
  return b;
}

static void bl_panel_refresh() {
  if (!bp_ov) return;
  char b[48];
  for (int r = 0; r < BP_R_COUNT; r++) {
    night_row_text(r, b, sizeof(b));
    set_label_if_changed(bp_lbl[r], b);
    if (night_row_toggle(r)) {
      set_label_if_changed(bp_tog_l[r], night_toggle_text(r));
      lv_obj_set_style_bg_color(bp_tog[r], lv_color_hex(night_toggle_color(r)), 0);
    }
  }
}

static void create_bl_panel() {
  bp_ov = make_group(lv_screen_active(), 0, 0, LCD_H_RES, LCD_V_RES);
  lv_obj_set_style_bg_color(bp_ov, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(bp_ov, LV_OPA_60, 0);
  lv_obj_t *p = make_shape(bp_ov, BP_X, BP_Y, BP_W, BP_H, S(16), 0x1E1E1E);
  lv_obj_set_style_border_width(p, S(2), 0);
  lv_obj_set_style_border_color(p, lv_color_hex(COL_YELLOW), 0);

  dlabel(p, tr("Fényerő és éjszakai mód", "Brightness and night mode"), &font_hu_28, COL_YELLOW, SX(24), SY(14));
  for (int r = 0; r < BP_R_COUNT; r++) {
    const int y = bp_row_y(r);
    bp_lbl[r] = dlabel(p, "", &font_hu_28, COL_TEXT, SX(24), y + SY(14));
    if (night_row_toggle(r)) {
      bp_tog[r] = bp_button(p, BP_BX, y, BP_TOG_W, "", 0x555555, &bp_tog_l[r]);
    } else {
      bp_button(p, BP_BX, y, BP_BTN_W, "-", 0x3A3A3A, NULL);
      bp_button(p, BP_BX + BP_TOG_W - BP_BTN_W, y, BP_BTN_W, "+", 0x3A3A3A, NULL);
    }
  }
  bp_button(p, (BP_W - SX(240)) / 2, BP_CLOSE_Y, SX(240), tr("Bezár", "Close"), 0x5A4A10, NULL);
  lv_obj_set_hidden(bp_ov, true);
}

static bool bl_panel_open() { return bp_ov && !lv_obj_is_hidden(bp_ov); }

static void bl_panel_show() {
  if (!bp_ov) return;
  bp_dirty = false;
  bp_last_ms = millis();
  bl_panel_refresh();
  lv_obj_set_hidden(bp_ov, false);
}

static void bl_panel_close() {
  if (!bl_panel_open()) return;
  lv_obj_set_hidden(bp_ov, true);
  bl_preview_until = 0;
  if (bp_dirty) {
    cfg_save_night();
    if (cfg.n_mode)
      LogOut.printf("Éjszakai mód mentve: %s, napnyugta %+d p - napkelte %+d p, éjjel %d %%, nappal %d %%\n", cfg.n_on ? "be" : "ki",
                    cfg.n_so, cfg.n_ro, cfg.n_pct, cfg.d_pct);
    else
      LogOut.printf("Éjszakai mód mentve: %s, %02d:%02d-%02d:%02d, éjjel %d %%, nappal %d %%\n", cfg.n_on ? "be" : "ki",
                    cfg.n_st / 60, cfg.n_st % 60, cfg.n_en / 60, cfg.n_en % 60, cfg.n_pct, cfg.d_pct);
  }
  bp_dirty = false;
}

static bool in_rect(int x, int y, int rx, int ry, int rw, int rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }

// Koppintás a nyitott panelen (képernyő-koordináták)
static void bl_panel_tap(int x, int y) {
  bp_last_ms = millis();
  if (!in_rect(x, y, BP_X, BP_Y, BP_W, BP_H)) { bl_panel_close(); return; }   // mellé koppintva bezárul
  const int lx = x - BP_X, ly = y - BP_Y;
  if (in_rect(lx, ly, (BP_W - SX(240)) / 2, BP_CLOSE_Y, SX(240), BP_BTN_H)) { bl_panel_close(); return; }

  const int plus_x = BP_BX + BP_TOG_W - BP_BTN_W;
  for (int r = 0; r < BP_R_COUNT; r++) {
    const int ry = bp_row_y(r);
    if (night_row_toggle(r)) {
      if (in_rect(lx, ly, BP_BX, ry, BP_TOG_W, BP_BTN_H)) { night_row_action(r, 0); bp_dirty = true; }
      continue;
    }
    const int dir = in_rect(lx, ly, BP_BX, ry, BP_BTN_W, BP_BTN_H) ? -1 : in_rect(lx, ly, plus_x, ry, BP_BTN_W, BP_BTN_H) ? 1 : 0;
    if (dir) { night_row_action(r, dir); bp_dirty = true; }
  }
  bl_panel_refresh();
}

static void bl_panel_task() {
  if (bl_panel_open() && millis() - bp_last_ms > BP_IDLE_MS) bl_panel_close();
}
