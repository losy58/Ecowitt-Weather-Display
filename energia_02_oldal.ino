// Energia oldal a kijelzőn: bal felül az energiaáramlás (napelem, hálózat, ház, boyler; az aktív vonalakon mozgó pöttyök
// mutatják az irányt), alatta a mai nap diagramja (0-24 óra: a napelem termelése zölden, a ház saját fogyasztása a
// termelésből kéken, a hálózati vétel lilán, a kéknek egymásra rakva), jobbra az összesítő (termelés, fázisok, mai
// vétel / visszatáplálás, boyler). Nem ugrik vissza magától a főoldalra. Csak bekapcsolt energiánál van a
// lapozásban (en_page_enabled()); az elemei az első megnyitáskor jönnek létre (az LVGL memóriája véges).
// (Az idojaras.ino része: az Arduino IDE és a PlatformIO a fájlokat ábécésorrendben, egy programmá fűzi össze. Ez a fájl
// az idojaras_02..18 előtt jön: azok static függvényeit csak az alábbi előre deklarálással használja.)

static lv_obj_t *make_group(lv_obj_t *parent, int x, int y, int w, int h);                       // idojaras_08
static lv_obj_t *make_shape(lv_obj_t *parent, int x, int y, int w, int h, int radius, uint32_t color);
static lv_obj_t *make_zone(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg);
static lv_obj_t *dlabel(lv_obj_t *parent, const char *txt, const lv_font_t *font, uint32_t color, int x, int y);
static lv_obj_t *dlabel_c(lv_obj_t *parent, const char *txt, const lv_font_t *font, uint32_t color, int x, int y, int w);
static lv_obj_t *dlabel_r(lv_obj_t *parent, const char *txt, const lv_font_t *font, uint32_t color, int right_x, int y, int w);
static void set_label_if_changed(lv_obj_t *obj, const char *text);                               // idojaras_03
static void show_page(int p);                                                                    // idojaras_14

constexpr uint32_t EN_COL_GREEN = 0x7CD67C;        // visszatáplálás
constexpr uint32_t EN_COL_NODE = 0x1E1E1E;
constexpr float EN_FLOW_MIN_W = 20.0f;             // ennél kisebb áramlás: a vonal halvány, nincs pötty
constexpr float EN_BOIL_ON_W = 50.0f;              // a boyler ettől "fűt"
constexpr uint32_t EN_DOT_MS = 1600;               // egy pötty ennyi idő alatt ér végig a vonalon

// a zónák képernyő-koordinátákban: bal felül az áramlás, alatta a diagram, jobbra az összesítő (teljes magasságban)
constexpr int EN_ZL_X = SX(20), EN_ZL_W = SX(600), EN_Z_Y = SY(50), EN_Z_H = SY(540), EN_ZL_H = SY(250);
constexpr int EN_ZC_Y = SY(308), EN_ZC_H = SY(282);
constexpr int EN_ZR_X = SX(628), EN_ZR_W = SX(376);
// a diagram (a diagram zónáján belül): 00:00-24:00, a mai 5 perces szeletekből (en_pv)
constexpr int EN_CH_FROM = 0, EN_CH_N = 24 * 12;
constexpr int EN_CH_X = SX(62), EN_CH_Y = SY(52), EN_CH_W = EN_ZL_W - SX(62) - SX(16), EN_CH_H = SY(200);
constexpr uint32_t EN_CH_GRID = 0x3A3A3A;
constexpr uint32_t EN_COL_PV = 0x5CC85C, EN_COL_SELF = COL_BLUE, EN_COL_IMP = 0x8A5CF5;   // termelés, saját fogyasztás, vétel
constexpr int EN_ROW = SY(36);                     // a jobb oldali sorok távolsága (font_hu_28)

// a körök (a bal zónán belül): középpont és átmérő
enum { EN_N_SOLAR, EN_N_GRID, EN_N_HOUSE, EN_N_BOIL, EN_N_COUNT };
static const int EN_NODE_X[EN_N_COUNT] = {SX(300), SX(70), SX(300), SX(530)};
static const int EN_NODE_Y[EN_N_COUNT] = {SY(58), SY(190), SY(190), SY(190)};
constexpr int EN_NODE_D = S(110);

// az áramlások (mindig a forrástól a cél felé)
enum { EN_F_SOLAR_HOUSE, EN_F_SOLAR_GRID, EN_F_GRID_HOUSE, EN_F_HOUSE_BOIL, EN_F_COUNT };
static const uint8_t EN_FLOW_FROM[EN_F_COUNT] = {EN_N_SOLAR, EN_N_SOLAR, EN_N_GRID, EN_N_HOUSE};
static const uint8_t EN_FLOW_TO[EN_F_COUNT]   = {EN_N_HOUSE, EN_N_GRID, EN_N_HOUSE, EN_N_BOIL};
static const uint32_t EN_FLOW_COL[EN_F_COUNT] = {COL_YELLOW, EN_COL_GREEN, EN_COL_IMP, COL_ORANGE_SOFT};   // a vétel lila, mint a diagramon

// EnNode, EnFlow, EN_DOTS: idojaras.ino
static lv_obj_t *page_en = NULL;
static EnNode en_nodes[EN_N_COUNT] = {};
static EnFlow en_flows[EN_F_COUNT] = {};
static lv_obj_t *en_r_pv[3] = {}, *en_r_grid = NULL, *en_r_ph[3] = {}, *en_r_gday[2] = {}, *en_r_boil = NULL, *en_r_status = NULL;
static bool en_page_visible = false;
static lv_obj_t *en_ch_cv = NULL, *en_ch_peak = NULL, *en_ch_ylab[3] = {};
static uint32_t en_ch_ver = 0;   // a kirajzolt diagram en_pv_ver-je

// van-e energia oldal a lapozásban
static bool en_page_enabled() { return en_cfg.on && !g_demo; }

// a következő / előző oldal (a kikapcsolt energia oldalt kihagyja)
static int page_step(int p, int dir) {
  do { p = (p + dir + PAGE_COUNT) % PAGE_COUNT; } while (p == PAGE_ENERGY && !en_page_enabled());
  return p;
}

// pötty mozgatása a vonal mentén (v: 0..1000)
static void en_dot_cb(lv_anim_t *a, int32_t v) {
  const EnFlow *f = (const EnFlow *)lv_anim_get_user_data(a);
  const int r = S(7);
  const int x = (int)(f->pts[0].x + (f->pts[1].x - f->pts[0].x) * v / 1000);
  const int y = (int)(f->pts[0].y + (f->pts[1].y - f->pts[0].y) * v / 1000);
  lv_obj_set_pos((lv_obj_t *)a->var, x - r, y - r);
}

// a pöttyök indítása / leállítása (csak látható oldalon és aktív áramlásnál mozognak)
static void en_flow_anim(EnFlow &f, bool run) {
  for (int k = 0; k < EN_DOTS; k++) {
    lv_anim_delete(f.dot[k], NULL);
    lv_obj_set_hidden(f.dot[k], !run);
    if (!run) continue;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, f.dot[k]);
    lv_anim_set_user_data(&a, &f);
    lv_anim_set_custom_exec_cb(&a, en_dot_cb);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, EN_DOT_MS);
    lv_anim_set_delay(&a, k * EN_DOT_MS / EN_DOTS);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
  }
}

static lv_obj_t *en_one_line(lv_obj_t *l, int w, int h) {
  lv_obj_set_size(l, w, h);   // egysoros: a magasság is kell, különben a DOT mód új sorba tör
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  return l;
}

static void en_page_create() {
  page_en = make_group(lv_screen_active(), 0, 0, LCD_H_RES, LCD_V_RES);
  lv_obj_move_to_index(page_en, lv_obj_get_index(page_dash));   // a többi oldal közé (a fejléc és a panelek alá)
  const lv_font_t *hu = &font_hu_28, *sm = &font_hu_14;

  // ---- bal: áramlás ----
  lv_obj_t *zl = make_zone(page_en, EN_ZL_X, EN_Z_Y, EN_ZL_W, EN_ZL_H, COL_BG_TL);
  for (int i = 0; i < EN_F_COUNT; i++) {   // előbb a vonalak és a pöttyök, hogy a körök eltakarják a végüket
    EnFlow &f = en_flows[i];
    f.pts[0].x = EN_NODE_X[EN_FLOW_FROM[i]]; f.pts[0].y = EN_NODE_Y[EN_FLOW_FROM[i]];
    f.pts[1].x = EN_NODE_X[EN_FLOW_TO[i]];   f.pts[1].y = EN_NODE_Y[EN_FLOW_TO[i]];
    f.line = lv_line_create(zl);
    lv_line_set_points(f.line, f.pts, 2);
    lv_obj_set_style_line_width(f.line, S(6), 0);
    lv_obj_set_style_line_rounded(f.line, true, 0);
    lv_obj_set_style_line_color(f.line, lv_color_hex(COL_WHITE_DIM), 0);
    for (int k = 0; k < EN_DOTS; k++) {
      f.dot[k] = make_shape(zl, 0, 0, S(14), S(14), LV_RADIUS_CIRCLE, EN_FLOW_COL[i]);
      lv_obj_set_hidden(f.dot[k], true);
    }
  }
  for (int n = 0; n < EN_N_COUNT; n++) {
    EnNode &nd = en_nodes[n];
    const int d = EN_NODE_D;
    nd.ring = make_shape(zl, EN_NODE_X[n] - d / 2, EN_NODE_Y[n] - d / 2, d, d, LV_RADIUS_CIRCLE, EN_COL_NODE);
    lv_obj_set_style_border_width(nd.ring, S(4), 0);
    lv_obj_set_style_border_color(nd.ring, lv_color_hex(COL_WHITE_DIM), 0);
    const int iw = d - S(8);   // a belső szélesség (a szegély nélkül; a gyerekek helye a szegélyen belülről számít)
    nd.title = en_one_line(dlabel_c(nd.ring, "", sm, COL_GREY, 0, S(10), iw), iw, S(20));
    nd.val   = en_one_line(dlabel_c(nd.ring, "--", hu, COL_TEXT, 0, S(32), iw), iw, S(36));
    nd.sub   = en_one_line(dlabel_c(nd.ring, "", sm, COL_GREY, 0, S(68), iw), iw, S(20));
    // a kör belsejében a szélek felé kevesebb hely van
    lv_obj_set_style_pad_hor(nd.title, S(6), 0);
    lv_obj_set_style_pad_hor(nd.sub, S(6), 0);
  }
  static const char *const NODE_T[2][EN_N_COUNT] = {{"Napelem", "Hálózat", "Ház", "Boyler"}, {"Solar", "Grid", "House", "Heater"}};
  for (int n = 0; n < EN_N_COUNT; n++) lv_label_set_text_static(en_nodes[n].title, NODE_T[g_lang][n]);
  // az áramlások értéke a vonal közepén (a zóna színű háttér kitakarja alatta a vonalat)
  for (int i = 0; i < EN_F_COUNT; i++) {
    EnFlow &f = en_flows[i];
    const int w = SX(62);   // a feliratnak csak a szöveg helye kell: a háttere kitakarja a vonalat
    int mx = (int)(f.pts[0].x + f.pts[1].x) / 2, my = (int)(f.pts[0].y + f.pts[1].y) / 2;
    if (i == EN_F_SOLAR_HOUSE) mx += w / 2 + SX(10);                // a függőleges vonal mellett, jobbra
    f.val = en_one_line(dlabel_c(zl, "", sm, COL_GREY, mx - w / 2, my - S(12), w), w, S(24));
    if (i == EN_F_SOLAR_HOUSE) lv_obj_set_style_text_align(f.val, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_bg_color(f.val, lv_color_hex(COL_BG_TL), 0);
    lv_obj_set_style_bg_opa(f.val, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_ver(f.val, S(2), 0);
  }

  // ---- bal alul: a mai termelés (vászon; csak új adatnál rajzoljuk újra) ----
  lv_obj_t *zc = make_zone(page_en, EN_ZL_X, EN_ZC_Y, EN_ZL_W, EN_ZC_H, COL_BG_TL);
  dlabel(zc, tr("Ma", "Today"), hu, COL_YELLOW, SX(16), SY(6));
  en_ch_peak = dlabel(zc, "", sm, COL_GREY, SX(80), SY(16));
  lv_obj_set_size(en_ch_peak, SX(190), S(20));
  lv_label_set_long_mode(en_ch_peak, LV_LABEL_LONG_DOT);
  {   // jelmagyarázat jobbra: termelés, saját fogyasztás, hálózati vétel
    static const char *const LEG[2][3] = {{"termelés", "saját", "vétel"}, {"solar", "own use", "import"}};
    static const uint32_t LC[3] = {EN_COL_PV, EN_COL_SELF, EN_COL_IMP};
    for (int k = 0; k < 3; k++) en_one_line(dlabel_r(zc, LEG[g_lang][k], sm, LC[k], EN_ZL_W - SX(16) - (2 - k) * SX(84), SY(16), SX(80)), SX(80), S(20));
  }
  const size_t bytes = (size_t)EN_CH_W * EN_CH_H * 2 + 256;
  void *buf = heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (buf) {
    en_ch_cv = lv_canvas_create(zc);
    lv_canvas_set_buffer(en_ch_cv, buf, EN_CH_W, EN_CH_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(en_ch_cv, EN_CH_X, EN_CH_Y);
  }
  for (int k = 0; k < 3; k++) en_ch_ylab[k] = dlabel_r(zc, "", sm, COL_GREY, EN_CH_X - SX(8), EN_CH_Y + k * EN_CH_H / 2 - S(9), SX(56));
  for (int k = 0; k <= 4; k++) {   // 00, 06, 12, 18, 24 óra
    char hb[4];
    snprintf(hb, sizeof(hb), "%02d", 6 * k);
    lv_obj_t *l = dlabel_c(zc, "", sm, COL_GREY, EN_CH_X + k * (EN_CH_W - 1) / 4 - SX(20), EN_CH_Y + EN_CH_H + SY(6), SX(40));
    lv_label_set_text(l, hb);
  }
  en_ch_ver = 0;

  // ---- jobb: összesítő ----
  lv_obj_t *zr = make_zone(page_en, EN_ZR_X, EN_Z_Y, EN_ZR_W, EN_Z_H, COL_BG_TL);
  const int lw = EN_ZR_W - SX(32), x0 = SX(16);
  int y = SY(12);
  dlabel(zr, tr("Napelem", "Solar"), hu, COL_YELLOW, x0, y); y += EN_ROW;
  for (int k = 0; k < 3; k++) { en_r_pv[k] = en_one_line(dlabel(zr, "", hu, COL_TEXT, x0, y), lw, EN_ROW); y += EN_ROW; }
  y += SY(14);
  en_r_grid = en_one_line(dlabel(zr, tr("Hálózat", "Grid"), hu, COL_ORANGE, x0, y), lw, EN_ROW); y += EN_ROW;   // + a tarifa
  for (int k = 0; k < 3; k++) { en_r_ph[k] = en_one_line(dlabel(zr, "", hu, COL_TEXT, x0, y), lw, EN_ROW); y += EN_ROW; }
  for (int k = 0; k < 2; k++) { en_r_gday[k] = en_one_line(dlabel(zr, "", hu, COL_TEXT, x0, y), lw, EN_ROW); y += EN_ROW; }
  y += SY(14);
  dlabel(zr, tr("Boyler", "Water heater"), hu, COL_ORANGE_SOFT, x0, y); y += EN_ROW;
  en_r_boil = en_one_line(dlabel(zr, "", hu, COL_TEXT, x0, y), lw, EN_ROW); y += EN_ROW;
  // alul: az adatok kora vagy a hiba (két sor)
  en_r_status = dlabel(zr, "", sm, COL_GREY, x0, EN_Z_H - SY(52));
  lv_obj_set_size(en_r_status, lw, SY(44));
  lv_label_set_long_mode(en_r_status, LV_LABEL_LONG_DOT);
}

// valcol != 0: a szám is ilyen színű (a hálózatnál: lila vétel, zöld visszatáplálás)
static void en_node_set(int n, const char *val, const char *sub, uint32_t border, bool on, uint32_t valcol = 0) {
  EnNode &nd = en_nodes[n];
  set_label_if_changed(nd.val, val);
  set_label_if_changed(nd.sub, sub);
  lv_obj_set_style_border_color(nd.ring, lv_color_hex(on ? border : COL_WHITE_DIM), 0);
  lv_obj_set_style_text_color(nd.val, lv_color_hex(!on ? COL_GREY : valcol ? valcol : COL_TEXT), 0);
}

static void en_flow_set(int i, float w, bool known) {
  EnFlow &f = en_flows[i];
  const bool act = known && w >= EN_FLOW_MIN_W;
  char b[16] = "";
  if (act) snprintf(b, sizeof(b), "%.0f W", w);
  set_label_if_changed(f.val, b);
  lv_obj_set_hidden(f.val, !act);
  lv_obj_set_style_line_color(f.line, lv_color_hex(act ? EN_FLOW_COL[i] : COL_WHITE_DIM), 0);
  lv_obj_set_style_text_color(f.val, lv_color_hex(EN_FLOW_COL[i]), 0);
  const bool run = act && en_page_visible;
  if (run != f.active) { f.active = run; en_flow_anim(f, run); }
}

// "12:34" a Growatt "2026-10-21 12:34:56" idejéből
static const char *en_hhmm(const char *upd) {
  const char *sp = strchr(upd, ' ');
  static char b[6];
  if (!sp || strlen(sp + 1) < 5) return "?";
  memcpy(b, sp + 1, 5); b[5] = 0;
  return b;
}

// egy vászon-oszlop értéke a szeletekből (a szomszédos szeletek közt egyenesen; NAN: nincs adat)
static float en_ch_pv(int x) {
  const float pos = EN_CH_FROM + (float)x * EN_CH_N / EN_CH_W;
  const int s0 = (int)pos, s1 = s0 + 1 < EN_PV_N ? s0 + 1 : s0;
  if (en_pv.w[s0] == EN_PV_NONE) return NAN;
  const float v0 = en_pv.w[s0], v1 = en_pv.w[s1] == EN_PV_NONE ? v0 : en_pv.w[s1];
  return v0 + (v1 - v0) * (pos - s0);
}
static float en_ch_net(int x) {
  const float pos = EN_CH_FROM + (float)x * EN_CH_N / EN_CH_W;
  const int s0 = (int)pos, s1 = s0 + 1 < EN_PV_N ? s0 + 1 : s0;
  if (en_pv.net[s0] == EN_NET_NONE) return NAN;
  const float v0 = en_pv.net[s0], v1 = en_pv.net[s1] == EN_NET_NONE ? v0 : en_pv.net[s1];
  return v0 + (v1 - v0) * (pos - s0);
}
// termelés (p), saját fogyasztás (self) és vétel (imp), W: a hálózati előjeles érték és a termelés alapján
static void en_ch_split(float pv, float net, float &p, float &self, float &imp) {
  p = isnan(pv) ? 0 : pv;
  if (isnan(net)) { self = p; imp = 0; return; }
  imp = net > 0 ? net : 0;
  const float ex = net < 0 ? -net : 0;
  self = p > ex ? p - ex : 0;
}

// A mai nap kirajzolása (csak ha új adat jött, vagy változott a skála / a nap)
static void en_chart_update() {
  if (!en_ch_cv) return;
  struct tm t;
  const bool today = get_local_tm(t) && en_pv.key == (t.tm_year + 1900) * 400 + t.tm_yday;
  int peak = -1, peak_s = 0;
  float maxv = 0;
  if (today) for (int s = 0; s < EN_PV_N; s++) {
    if (en_pv.w[s] != EN_PV_NONE && en_pv.w[s] > peak) { peak = en_pv.w[s]; peak_s = s; }
    float p, self, imp;
    en_ch_split(en_pv.w[s] == EN_PV_NONE ? NAN : en_pv.w[s], en_pv.net[s] == EN_NET_NONE ? NAN : en_pv.net[s], p, self, imp);
    if (p > maxv) maxv = p;
    if (self + imp > maxv) maxv = self + imp;
  }
  // a skála a termeléshez igazodik: a beállított kWp (ha van, és a nap csúcsa nem nagyobb), különben a mai termelési csúcs
  // "szép" értékre kerekítve (0,5 / 1 / 1,5 / 2 kW, felette egész kW-ra); a nagyobb fogyasztás a diagram tetején levágva látszik.
  // Amíg nincs érdemi termelés (< 100 W), a fogyasztás csúcsa adja a skálát (min. 1 kW).
  int top;
  if (en_cfg.kwp > 0) {
    top = (int)lroundf(en_cfg.kwp * 1000.0f);
    if (peak > top) top = ((peak + 999) / 1000) * 1000;
  } else if (peak >= 100) {
    top = peak <= 500 ? 500 : peak <= 1000 ? 1000 : peak <= 1500 ? 1500 : peak <= 2000 ? 2000 : ((peak + 999) / 1000) * 1000;
  } else {
    top = ((int)ceilf(maxv / 1000.0f)) * 1000;
    if (top < 1000) top = 1000;
  }
  const uint32_t ver = en_pv_ver * 31u + (uint32_t)top + (today ? 1u : 0u);
  if (ver == en_ch_ver) return;
  en_ch_ver = ver;

  char b[48];
  for (int k = 0; k < 3; k++) {
    const int w = top * (2 - k) / 2;
    if (k == 2) strlcpy(b, "0", sizeof(b));
    else if (w < 1000) snprintf(b, sizeof(b), "%d W", w);
    else snprintf(b, sizeof(b), "%.1f kW", w / 1000.0f);
    set_label_if_changed(en_ch_ylab[k], b);
  }
  if (peak > 0) snprintf(b, sizeof(b), tr("csúcs %.1f kW (%02d:%02d)", "peak %.1f kW (%02d:%02d)"), peak / 1000.0f, peak_s / 12, peak_s % 12 * 5);
  else b[0] = 0;
  set_label_if_changed(en_ch_peak, b);

  // háttér, rácsvonalak (0, fél, tető; 4 óránként), majd oszloponként: halvány zöld a termelés, rajta alulról a kék (saját
  // fogyasztás), arra egymásra rakva a lila (vétel), a termelés felső széle fényes zöld
  const lv_color_t bg = lv_color_hex(COL_BG_TL), grid = lv_color_hex(EN_CH_GRID);
  const lv_color_t c_edge = lv_color_hex(EN_COL_PV), c_pv = lv_color_hex(mix_col(COL_BG_TL, EN_COL_PV, 55));
  const lv_color_t c_self = lv_color_hex(EN_COL_SELF), c_imp = lv_color_hex(EN_COL_IMP);
  lv_canvas_fill_bg(en_ch_cv, bg, LV_OPA_COVER);
  for (int k = 0; k < 3; k++) {
    const int y = k == 2 ? EN_CH_H - 1 : k * (EN_CH_H - 1) / 2;
    for (int x = 0; x < EN_CH_W; x++) lv_canvas_set_px(en_ch_cv, x, y, grid, LV_OPA_COVER);
  }
  for (int k = 0; k <= 4; k++) {
    const int x = k * (EN_CH_W - 1) / 4;
    for (int y = 0; y < EN_CH_H; y++) lv_canvas_set_px(en_ch_cv, x, y, grid, LV_OPA_COVER);
  }
  if (today) {
    const int lw = S(2);   // a felső szél vastagsága
    const float k = (float)(EN_CH_H - 1) / top;
    for (int x = 0; x < EN_CH_W; x++) {
      const float pv = en_ch_pv(x), net = en_ch_net(x);
      if (isnan(pv) && isnan(net)) continue;
      float p, self, imp;
      en_ch_split(pv, net, p, self, imp);
      const int hp = min((int)(p * k), EN_CH_H - 1), hs = min((int)(self * k), EN_CH_H - 1), hh = min((int)((self + imp) * k), EN_CH_H - 1);   // a tetején levágva
      for (int y = 0; y <= hp; y++) lv_canvas_set_px(en_ch_cv, x, EN_CH_H - 1 - y, c_pv, LV_OPA_COVER);
      for (int y = 0; y <= hs; y++) lv_canvas_set_px(en_ch_cv, x, EN_CH_H - 1 - y, c_self, LV_OPA_COVER);
      for (int y = hs + 1; y <= hh; y++) lv_canvas_set_px(en_ch_cv, x, EN_CH_H - 1 - y, c_imp, LV_OPA_COVER);
      if (hp > 0) for (int y = hp - lw + 1; y <= hp; y++) if (y >= 0) lv_canvas_set_px(en_ch_cv, x, EN_CH_H - 1 - y, c_edge, LV_OPA_COVER);
    }
  }
  lv_obj_invalidate(en_ch_cv);
}

// Az oldal frissítése (a látható oldalon másodpercenként)
static void en_page_update() {
  if (!page_en) return;
  const EnData d = en_snapshot();
  const bool p1 = en_fresh(d.p1_ms, EN_LOCAL_STALE_MS), sk = en_fresh(d.sk_ms, EN_LOCAL_STALE_MS), gw = en_fresh(d.gw_ms, EN_GW_STALE_MS);
  const float imp = p1 && d.grid_w > 0 ? d.grid_w : 0, exp = p1 && d.grid_w < 0 ? -d.grid_w : 0;
  const float house = en_house_w(d);
  char v[24], s[40];

  // körök
  if (gw) snprintf(v, sizeof(v), "%.0f W", d.pv_w); else strlcpy(v, "--", sizeof(v));
  if (gw && d.gw_upd[0]) snprintf(s, sizeof(s), tr("felhő %s", "cloud %s"), en_hhmm(d.gw_upd)); else s[0] = 0;
  en_node_set(EN_N_SOLAR, v, s, COL_YELLOW, gw && d.pv_w >= EN_FLOW_MIN_W);
  if (p1) snprintf(v, sizeof(v), "%.0f W", fabsf(d.grid_w)); else strlcpy(v, "--", sizeof(v));
  en_node_set(EN_N_GRID, v, !p1 ? "" : exp > 0 ? tr("leadás", "export") : tr("vétel", "import"),
              exp > 0 ? EN_COL_GREEN : EN_COL_IMP, p1, exp > 0 ? EN_COL_GREEN : EN_COL_IMP);
  if (!isnan(house)) snprintf(v, sizeof(v), "%.0f W", house); else strlcpy(v, "--", sizeof(v));
  en_node_set(EN_N_HOUSE, v, isnan(house) ? "" : tr("becsült", "estimated"), COL_BLUE, !isnan(house));
  if (sk) snprintf(v, sizeof(v), "%.0f W", d.boil_w); else strlcpy(v, "--", sizeof(v));
  en_node_set(EN_N_BOIL, v, !sk ? "" : d.boil_w >= EN_BOIL_ON_W ? tr("fűt", "heating") : tr("áll", "idle"),
              COL_ORANGE_SOFT, sk && d.boil_w >= EN_BOIL_ON_W);

  // áramlások: a visszatáplálás a napelemből jön, a ház a napelem maradékát és a vételt kapja
  en_flow_set(EN_F_SOLAR_HOUSE, d.pv_w - exp, gw);
  en_flow_set(EN_F_SOLAR_GRID, exp, p1);
  en_flow_set(EN_F_GRID_HOUSE, imp, p1);
  en_flow_set(EN_F_HOUSE_BOIL, d.boil_w, sk);

  // összesítő
  char b[48];
  const bool pvd = d.gw_ms && !isnan(d.pv_day);
  if (pvd || !en_cfg.gt[0]) {
    if (pvd) snprintf(b, sizeof(b), tr("Ma: %.1f kWh", "Today: %.1f kWh"), d.pv_day); else strlcpy(b, tr("Nincs napelem-adat", "No solar data"), sizeof(b));
    set_label_if_changed(en_r_pv[0], b);
    if (pvd) snprintf(b, sizeof(b), tr("A hónapban: %.0f kWh", "This month: %.0f kWh"), d.pv_month); else b[0] = 0;
    set_label_if_changed(en_r_pv[1], b);
    if (pvd) snprintf(b, sizeof(b), tr("Az évben: %.0f kWh", "This year: %.0f kWh"), d.pv_year); else b[0] = 0;
    set_label_if_changed(en_r_pv[2], b);
  } else {
    set_label_if_changed(en_r_pv[0], tr("Ma: -- kWh", "Today: -- kWh"));
    set_label_if_changed(en_r_pv[1], tr("A hónapban: -- kWh", "This month: -- kWh"));
    set_label_if_changed(en_r_pv[2], tr("Az évben: -- kWh", "This year: -- kWh"));
  }
  if (p1) snprintf(b, sizeof(b), tr("Hálózat (T%d)", "Grid (T%d)"), d.tariff); else strlcpy(b, tr("Hálózat", "Grid"), sizeof(b));
  set_label_if_changed(en_r_grid, b);
  for (int k = 0; k < 3; k++) {
    if (p1) snprintf(b, sizeof(b), "L%d: %.1f V  %.0f A", k + 1, d.u[k], d.i[k]);
    else snprintf(b, sizeof(b), "L%d: --", k + 1);
    set_label_if_changed(en_r_ph[k], b);
    lv_obj_set_style_text_color(en_r_ph[k], lv_color_hex(p1 && d.u[k] > EN_OVERVOLT_V ? COL_WARN : COL_TEXT), 0);
  }
  const float ti = en_today(d.imp_kwh, en_day.imp), te = en_today(d.exp_kwh, en_day.exp), tb = en_today(d.boil_kwh, en_day.boil);
  if (!isnan(ti)) snprintf(b, sizeof(b), tr("Ma vett: %.1f kWh", "Imported today: %.1f kWh"), ti); else strlcpy(b, tr("Ma vett: -- kWh", "Imported today: -- kWh"), sizeof(b));
  set_label_if_changed(en_r_gday[0], b);
  if (!isnan(te)) snprintf(b, sizeof(b), tr("Ma adott: %.1f kWh", "Exported today: %.1f kWh"), te); else strlcpy(b, tr("Ma adott: -- kWh", "Exported today: -- kWh"), sizeof(b));
  set_label_if_changed(en_r_gday[1], b);
  if (!isnan(tb)) snprintf(b, sizeof(b), tr("Ma: %.2f kWh", "Today: %.2f kWh"), tb); else strlcpy(b, tr("Ma: -- kWh", "Today: -- kWh"), sizeof(b));
  set_label_if_changed(en_r_boil, b);
  en_chart_update();

  // állapot: az első hiba, különben a napelem-adat kora
  char st[160];
  const char *err = (en_cfg.p1[0] && d.p1_err[0]) ? d.p1_err : (en_cfg.sk[0] && d.sk_err[0]) ? d.sk_err : (en_cfg.gt[0] && d.gw_err[0]) ? d.gw_err : "";
  const char *who = err == d.p1_err ? tr("Hálózat", "Grid") : err == d.sk_err ? tr("Boyler", "Water heater") : tr("Napelem", "Solar");
  if (err[0]) snprintf(st, sizeof(st), tr("%s hiba: %s", "%s error: %s"), who, err);
  else if (en_day.since > 0 && en_day.key >= 0)
    snprintf(st, sizeof(st), tr("A mai értékek %02d:%02d óta (az első indulás ma).", "Today's values since %02d:%02d (first start today)."),
             (int)(en_day.since / 60), (int)(en_day.since % 60));
  else if (d.gw_ms) snprintf(st, sizeof(st), tr("A napelem adata %u perces (a felhő 5 percenként frissül).",
                                                "The solar data is %u min old (the cloud updates every 5 min)."),
                             (unsigned)((millis() - d.gw_ms) / 60000));
  else st[0] = 0;
  set_label_if_changed(en_r_status, st);
  lv_obj_set_style_text_color(en_r_status, lv_color_hex(err[0] ? COL_WARN : COL_GREY), 0);
}

// show_page()-ből: az oldal megjelenítése / elrejtése (az első megjelenítéskor jön létre)
static void en_page_show(bool vis) {
  if (vis && !page_en) en_page_create();
  if (!page_en) return;
  en_page_visible = vis;
  lv_obj_set_hidden(page_en, !vis);
  if (vis) en_page_update();
  else for (EnFlow &f : en_flows) if (f.active) { f.active = false; en_flow_anim(f, false); }
}

// page_task()-ból: a látható oldal frissítése másodpercenként; kikapcsolt energiánál vissza a főoldalra
static void en_page_task() {
  if (current_page != PAGE_ENERGY) return;
  if (!en_page_enabled()) { show_page(PAGE_DASH); return; }
  static uint32_t last = 0;
  if (millis() - last < 1000) return;
  last = millis();
  en_page_update();
}
