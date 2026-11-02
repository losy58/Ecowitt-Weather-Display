// Energia oldal a kijelzőn: bal felül az energiaáramlás (egy képben a napelem+ház+bojler, mellette a Hálózat
// villanyoszlopa és doboza; az aktív hálózat-ház vonalon mozgó pöttyök mutatják az irányt), alatta a mai nap diagramja
// (0-24 óra: a napelem termelése zölden, a ház saját fogyasztása a termelésből kéken, a hálózati vétel lilán, a
// kéknek egymásra rakva), jobbra az összesítő (termelés, fázisok, mai vétel / visszatáplálás, bojler). Nem ugrik
// vissza magától a főoldalra. Csak bekapcsolt energiánál van a lapozásban (en_page_enabled()); az elemei az első
// megnyitáskor jönnek létre (az LVGL memóriája véges).
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

constexpr uint32_t EN_COL_GREEN = 0x7CD67C;        // visszatáplálás / a bojler készenléti állapota
constexpr uint32_t EN_COL_NODE = 0x1E1E1E;
constexpr float EN_FLOW_MIN_W = 20.0f;             // ennél kisebb áramlás: a vonal halvány, nincs pötty
constexpr float EN_BOIL_HEAT_W = 3.0f;             // ennél nagyobb fogyasztásnál a bojler "fűt" (alatta: készenlét, 2-3 W)
constexpr uint32_t EN_DOT_MS = 1600;               // egy pötty ennyi idő alatt ér végig a vonalon
constexpr uint32_t EN_COL_IMP = 0x8A5CF5, EN_COL_BOIL = 0xE54B4B;   // hálózati vétel (lila), bojler fűtés (piros)

// a zónák képernyő-koordinátákban: bal felül az áramlás, alatta a diagram, jobbra az összesítő (teljes magasságban)
constexpr int EN_ZL_X = SX(20), EN_ZL_W = SX(600), EN_Z_Y = SY(50), EN_Z_H = SY(540), EN_ZL_H = SY(250);
constexpr int EN_ZC_Y = SY(308), EN_ZC_H = SY(282);
constexpr int EN_ZR_X = SX(628), EN_ZR_W = SX(376);
// a diagram (a diagram zónáján belül): 00:00-24:00, a mai 5 perces szeletekből (en_pv)
constexpr int EN_CH_FROM = 0, EN_CH_N = 24 * 12;
constexpr int EN_CH_X = SX(62), EN_CH_Y = SY(52), EN_CH_W = EN_ZL_W - SX(62) - SX(16), EN_CH_H = SY(200);
constexpr uint32_t EN_CH_GRID = 0x3A3A3A;
constexpr uint32_t EN_COL_PV = 0x5CC85C, EN_COL_SELF = COL_BLUE;   // termelés, saját fogyasztás (a vétel: EN_COL_IMP)
constexpr int EN_ROW = SY(36);                     // a jobb oldali sorok távolsága (font_hu_28)

// ---- Hálózat: villanyoszlop + doboz (bal oldalon) ----
// Az oszlop teteje a napelemes tető csúcsával, a doboz alja a jobb oldali kártya aljával van egy magasságban (az
// oszlop ezért karcsúbb, mint a doboz: lásd EN_GRID_PYLON_H). Az oszlop vonalai és pöttyei a Hálózat állapotszínét
// követik (en_page_update), nem statikus szürke fémszerkezet.
constexpr int EN_GRID_CX = SX(90);                                 // az oszlop/doboz középpontja
constexpr int EN_GRID_TOP = SY(15);                                // az oszlop teteje (= a tető csúcsa)
constexpr int EN_GRID_BOX_W = SX(140), EN_GRID_BOX_H = SY(80);      // a Hálózat doboz mérete
constexpr int EN_GRID_BOTTOM = SY(240);                            // a doboz alja (= a jobb oldali kártya alja)
constexpr int EN_GRID_PYLON_H = EN_GRID_BOTTOM - EN_GRID_BOX_H - EN_GRID_TOP;   // az oszlop magassága (a doboz tetejéig)
constexpr int EN_GRID_HALF = SX(30);                                // a lábak félszélessége az oszlop alján
constexpr int EN_GRID_ARM_OUT = SX(22);                             // a karok kiállása a lábaktól
constexpr int EN_GRID_DROP = SY(10);                                // a szigetelő alatti rövid leágazás
// az oszlop egy adott magassági %-ánál (0: csúcs, 100: az oszlop alja) a lábak y-koordinátája / félszélessége
constexpr int en_py_y(int pct)    { return EN_GRID_TOP + EN_GRID_PYLON_H * pct / 100; }
constexpr int en_py_half(int pct) { return EN_GRID_HALF * pct / 100; }
constexpr int EN_PY_RUNG1 = 38, EN_PY_RUNG2 = 72, EN_PY_ARM = 18;   // keresztrudak / karok magassága (%)
static const lv_point_precise_t EN_PYLON_LEG_L[2] = {{EN_GRID_CX, EN_GRID_TOP}, {EN_GRID_CX - EN_GRID_HALF, EN_GRID_TOP + EN_GRID_PYLON_H}};
static const lv_point_precise_t EN_PYLON_LEG_R[2] = {{EN_GRID_CX, EN_GRID_TOP}, {EN_GRID_CX + EN_GRID_HALF, EN_GRID_TOP + EN_GRID_PYLON_H}};
static const lv_point_precise_t EN_PYLON_RUNG1[2] = {{EN_GRID_CX - en_py_half(EN_PY_RUNG1), en_py_y(EN_PY_RUNG1)}, {EN_GRID_CX + en_py_half(EN_PY_RUNG1), en_py_y(EN_PY_RUNG1)}};
static const lv_point_precise_t EN_PYLON_RUNG2[2] = {{EN_GRID_CX - en_py_half(EN_PY_RUNG2), en_py_y(EN_PY_RUNG2)}, {EN_GRID_CX + en_py_half(EN_PY_RUNG2), en_py_y(EN_PY_RUNG2)}};
static const lv_point_precise_t EN_PYLON_X1A[2] = {{EN_GRID_CX - en_py_half(EN_PY_RUNG1), en_py_y(EN_PY_RUNG1)}, {EN_GRID_CX + en_py_half(EN_PY_RUNG2), en_py_y(EN_PY_RUNG2)}};
static const lv_point_precise_t EN_PYLON_X1B[2] = {{EN_GRID_CX + en_py_half(EN_PY_RUNG1), en_py_y(EN_PY_RUNG1)}, {EN_GRID_CX - en_py_half(EN_PY_RUNG2), en_py_y(EN_PY_RUNG2)}};
static const lv_point_precise_t EN_PYLON_X2A[2] = {{EN_GRID_CX - en_py_half(EN_PY_RUNG2), en_py_y(EN_PY_RUNG2)}, {EN_GRID_CX + EN_GRID_HALF, EN_GRID_TOP + EN_GRID_PYLON_H}};
static const lv_point_precise_t EN_PYLON_X2B[2] = {{EN_GRID_CX + en_py_half(EN_PY_RUNG2), en_py_y(EN_PY_RUNG2)}, {EN_GRID_CX - EN_GRID_HALF, EN_GRID_TOP + EN_GRID_PYLON_H}};
constexpr int EN_PY_ARM_Y = en_py_y(EN_PY_ARM), EN_PY_ARM_HALF = en_py_half(EN_PY_ARM);
static const lv_point_precise_t EN_PYLON_ARM_L[2] = {{EN_GRID_CX - EN_PY_ARM_HALF - EN_GRID_ARM_OUT, EN_PY_ARM_Y}, {EN_GRID_CX - EN_PY_ARM_HALF, EN_PY_ARM_Y}};
static const lv_point_precise_t EN_PYLON_ARM_R[2] = {{EN_GRID_CX + EN_PY_ARM_HALF + EN_GRID_ARM_OUT, EN_PY_ARM_Y}, {EN_GRID_CX + EN_PY_ARM_HALF, EN_PY_ARM_Y}};
static const lv_point_precise_t EN_PYLON_DROP_L[2] = {{EN_GRID_CX - EN_PY_ARM_HALF - EN_GRID_ARM_OUT, EN_PY_ARM_Y}, {EN_GRID_CX - EN_PY_ARM_HALF - EN_GRID_ARM_OUT, EN_PY_ARM_Y + EN_GRID_DROP}};
static const lv_point_precise_t EN_PYLON_DROP_R[2] = {{EN_GRID_CX + EN_PY_ARM_HALF + EN_GRID_ARM_OUT, EN_PY_ARM_Y}, {EN_GRID_CX + EN_PY_ARM_HALF + EN_GRID_ARM_OUT, EN_PY_ARM_Y + EN_GRID_DROP}};

// ---- Napelem + Ház + Bojler: egy közös kártya a Hálózattól jobbra ----
// A tető (nyitott vonal, mindig sárga) és a napelem termelése egy térben van (nincs vonal köztük); alatta egy
// válaszvonal (a ház energiaszínét követi) választja el a Ház és a Bojler részt. A Ház aljának/oldalainak kerete is
// a ház energiaszínét követi (lila: hálózatról, kék: hálózatról és napelemről is, zöld: csak napelemről).
constexpr int EN_HC_X = SX(200), EN_HC_W = SX(360);
constexpr int EN_HC_TOP = EN_GRID_TOP + SY(42), EN_HC_BOTTOM = EN_GRID_BOTTOM;   // a tető magassága 42; az alja a Hálózat doboz aljával egyezik
constexpr int EN_HC_DIV_Y = EN_HC_TOP + SY(38);   // a napelem termelése alatti válaszvonal: csak annyi hely a tető alatt, amennyi a szövegnek kell
constexpr int EN_ROOF_APEX_X = EN_HC_X + EN_HC_W / 2, EN_ROOF_APEX_Y = EN_GRID_TOP;
static const lv_point_precise_t EN_ROOF_PTS[3] = {{EN_HC_X, EN_HC_TOP}, {EN_ROOF_APEX_X, EN_ROOF_APEX_Y}, {EN_HC_X + EN_HC_W, EN_HC_TOP}};
// nap a tető jobb oldalán, az ereszen túl (8 sugár, teljes körben; nem állapotfüggő, mindig besüt)
constexpr int EN_SUN_CX = EN_HC_X + EN_HC_W + SX(15), EN_SUN_CY = SY(30);
constexpr int EN_SUN_D = S(16), EN_SUN_GAP = S(3), EN_SUN_RAY = S(10);
static lv_point_precise_t en_sun_ray_pts[8][2];   // en_page_create tölti fel (sin/cos kell, nem constexpr)

constexpr int EN_HOUSE_TXT_X = EN_HC_X + SX(15), EN_HOUSE_TXT_W = SX(135);
// a Bojler doboza a Ház részben jobbra, szinte a teljes magasságban (hőfok/cél, üzemmód, melegvíz, teljesítmény, kéz/A jel);
// rákoppintva a Beállítások Bojler füle nyílik
constexpr int EN_BOIL_W = SX(196), EN_BOIL_X = EN_HC_X + EN_HC_W - SX(12) - EN_BOIL_W, EN_BOIL_Y = EN_HC_DIV_Y + SY(7);
constexpr int EN_BOIL_H = EN_HC_BOTTOM - SY(7) - EN_BOIL_Y;
constexpr int EN_BOIL_BADGE = S(24);   // a kéz / "A" jel köre a doboz jobb felső sarkában

// a hálózat-ház áramlás (a vonal mindig a Hálózat doboz jobb szélétől a kártya bal széléig fut, a doboz
// magasságának közepén); exportnál megfordul (zöld, EnFlow::rev), vételnél lila. A napelem mindig a házba folyik,
// a bojler a házban van (nincs hozzá külön vonal/vezeték) - ezekhez nincs rajzolt áramlás.
enum { EN_F_GRID_HOUSE, EN_F_COUNT };
constexpr int EN_FLOW_Y = EN_GRID_BOTTOM - EN_GRID_BOX_H / 2;
constexpr int EN_FLOW_X0 = EN_GRID_CX + EN_GRID_BOX_W / 2, EN_FLOW_X1 = EN_HC_X;

// EnFlow, EN_DOTS: idojaras.ino. A Hálózat és a Bojler doboza egyszerű bordás téglalap (ring+val[+sub]); a Napelem
// és a Ház a kártyában sima szöveg, keret nélkül.
static lv_obj_t *page_en = NULL;
static lv_obj_t *en_house_border[4] = {};   // bal-alsó, jobb-alsó, alsó, válaszvonal: a ház energiaszínét követik
static lv_obj_t *en_grid_lines[12] = {}, *en_grid_dots[2] = {};   // a villanyoszlop elemei: a színük a Hálózat állapotát követi
static lv_obj_t *en_grid_box = NULL, *en_grid_val = NULL, *en_grid_sub = NULL;
static lv_obj_t *en_boil_box = NULL, *en_boil_val = NULL, *en_boil_sub = NULL, *en_boil_w = NULL, *en_boil_mode = NULL;
static lv_obj_t *en_boil_badge = NULL, *en_boil_badge_l = NULL;   // kéz: kézi vezérlés, "A": napelemes automatika
static lv_obj_t *en_pv_val = NULL;
static lv_obj_t *en_pv_lines[11] = {}, *en_pv_sun = NULL;   // tető, 8 napsugár, 2 oldalvonal + a nap köre: sárga, ha van napelem-adat, egyébként szürke
static lv_obj_t *en_house_val = NULL, *en_house_sub = NULL;
static EnFlow en_flows[EN_F_COUNT] = {};
static lv_obj_t *en_r_pv[3] = {}, *en_r_grid = NULL, *en_r_ph[3] = {}, *en_r_gday[2] = {}, *en_r_boil = NULL, *en_r_status = NULL;
static bool en_page_visible = false;
constexpr int EN_CH_YSTEP = 2000;   // a függőleges tengely 2 kW-onként van beosztva
constexpr int EN_CH_YLAB_MAX = 6;   // 0, 2, 4, 6, 8, 10 kW (amennyi a "tető" alatt elfér)
static lv_obj_t *en_ch_cv = NULL, *en_ch_peak = NULL, *en_ch_ylab[EN_CH_YLAB_MAX] = {};
static uint32_t en_ch_ver = 0;   // a kirajzolt diagram en_pv_ver-je
// teljes képernyős diagram (a kis diagramra koppintva nyílik, az X zárja; en_14_oldalak és en_15_erintes is használja)
constexpr int EN_FSCH_XBTN = S(84), EN_FSCH_CLOSE_ZONE = S(140);
constexpr int EN_FSCH_X = SX(90), EN_FSCH_Y = SY(140), EN_FSCH_W = LCD_H_RES - EN_FSCH_X - SX(30), EN_FSCH_H = SY(400);
static bool en_fs_open = false;
static lv_obj_t *en_fs_page = NULL, *en_fsch_cv = NULL, *en_fsch_peak = NULL, *en_fsch_ylab[EN_CH_YLAB_MAX] = {};

// van-e energia oldal a lapozásban
static bool en_page_enabled() { return en_cfg.on && !g_demo; }

// a következő / előző oldal (a kikapcsolt energia oldalt kihagyja)
static int page_step(int p, int dir) {
  do { p = (p + dir + PAGE_COUNT) % PAGE_COUNT; } while (p == PAGE_ENERGY && !en_page_enabled());
  return p;
}

// pötty mozgatása a vonal mentén (v: 0..1000); f->rev: fordított irányban (pts[1] -> pts[0], pl. visszatáplálásnál)
static void en_dot_cb(lv_anim_t *a, int32_t v) {
  const EnFlow *f = (const EnFlow *)lv_anim_get_user_data(a);
  const int32_t vv = f->rev ? 1000 - v : v;
  const int r = S(7);
  const int x = (int)(f->pts[0].x + (f->pts[1].x - f->pts[0].x) * vv / 1000);
  const int y = (int)(f->pts[0].y + (f->pts[1].y - f->pts[0].y) * vv / 1000);
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

// a villanyoszlop egy vonala/pöttye (12 vonal + 2 szigetelő); a tömbökben tartjuk, hogy a színüket együtt frissíthessük
static lv_obj_t *en_pylon_line(lv_obj_t *parent, const lv_point_precise_t *pts, int w) {
  lv_obj_t *ln = lv_line_create(parent);
  lv_line_set_points(ln, pts, 2);
  lv_obj_set_style_line_width(ln, w, 0);
  lv_obj_set_style_line_rounded(ln, true, 0);
  lv_obj_set_style_line_color(ln, lv_color_hex(COL_WHITE_DIM), 0);
  return ln;
}

static void en_page_create() {
  page_en = make_group(lv_screen_active(), 0, 0, LCD_H_RES, LCD_V_RES);
  lv_obj_move_to_index(page_en, lv_obj_get_index(page_dash));   // a többi oldal közé (a fejléc és a panelek alá)
  const lv_font_t *hu = &font_hu_28, *sm = &font_hu_14;

  // ---- bal: áramlás ----
  lv_obj_t *zl = make_zone(page_en, EN_ZL_X, EN_Z_Y, EN_ZL_W, EN_ZL_H, COL_BG_TL);

  // a hálózat-ház vonal és pöttyei (előbb, hogy a doboz/kártya eltakarja a végüket)
  for (int i = 0; i < EN_F_COUNT; i++) {
    EnFlow &f = en_flows[i];
    f.pts[0] = {EN_FLOW_X0, EN_FLOW_Y}; f.pts[1] = {EN_FLOW_X1, EN_FLOW_Y};
    f.line = lv_line_create(zl);
    lv_line_set_points(f.line, f.pts, 2);
    lv_obj_set_style_line_width(f.line, S(6), 0);
    lv_obj_set_style_line_rounded(f.line, true, 0);
    lv_obj_set_style_line_color(f.line, lv_color_hex(COL_WHITE_DIM), 0);
    for (int k = 0; k < EN_DOTS; k++) {
      f.dot[k] = make_shape(zl, 0, 0, S(14), S(14), LV_RADIUS_CIRCLE, EN_COL_IMP);
      lv_obj_set_hidden(f.dot[k], true);
    }
    f.val = en_one_line(dlabel(zl, "", sm, COL_GREY, 0, 0), SX(10), S(10));
    lv_obj_set_hidden(f.val, true);   // az érték már a Hálózat dobozban és a Ház szövegében is ott van
  }

  // ---- Hálózat: villanyoszlop a doboz fölött (az oszlop vonalai/pöttyei a Hálózat állapotszínét követik) ----
  {
    static const lv_point_precise_t *const PARMS[12] = {EN_PYLON_LEG_L, EN_PYLON_LEG_R, EN_PYLON_RUNG1, EN_PYLON_RUNG2,
                                                          EN_PYLON_X1A, EN_PYLON_X1B, EN_PYLON_X2A, EN_PYLON_X2B,
                                                          EN_PYLON_ARM_L, EN_PYLON_ARM_R, EN_PYLON_DROP_L, EN_PYLON_DROP_R};
    for (int k = 0; k < 12; k++) en_grid_lines[k] = en_pylon_line(zl, PARMS[k], k < 2 ? S(3) : S(2));
    en_grid_dots[0] = make_shape(zl, EN_GRID_CX - EN_PY_ARM_HALF - EN_GRID_ARM_OUT - S(3), EN_PY_ARM_Y - S(3), S(6), S(6), LV_RADIUS_CIRCLE, COL_WHITE_DIM);
    en_grid_dots[1] = make_shape(zl, EN_GRID_CX + EN_PY_ARM_HALF + EN_GRID_ARM_OUT - S(3), EN_PY_ARM_Y - S(3), S(6), S(6), LV_RADIUS_CIRCLE, COL_WHITE_DIM);
  }
  {   // a Hálózat doboza (teljes szegéllyel, nem olvad egybe az oszloppal)
    const int bx = EN_GRID_CX - EN_GRID_BOX_W / 2, by = EN_GRID_BOTTOM - EN_GRID_BOX_H;
    en_grid_box = make_shape(zl, bx, by, EN_GRID_BOX_W, EN_GRID_BOX_H, S(10), EN_COL_NODE);
    lv_obj_set_style_border_width(en_grid_box, S(4), 0);
    lv_obj_set_style_border_color(en_grid_box, lv_color_hex(COL_WHITE_DIM), 0);
    const int iw = EN_GRID_BOX_W - S(8);
    lv_obj_t *gt = en_one_line(dlabel_c(en_grid_box, tr("Hálózat", "Grid"), sm, COL_GREY, 0, S(4), iw), iw, S(16));
    en_grid_val = en_one_line(dlabel_c(en_grid_box, "--", hu, COL_TEXT, 0, S(20), iw), iw, S(30));
    en_grid_sub = en_one_line(dlabel_c(en_grid_box, "", sm, COL_GREY, 0, S(52), iw), iw, S(16));
    lv_obj_set_style_pad_hor(gt, S(6), 0); lv_obj_set_style_pad_hor(en_grid_sub, S(6), 0);
  }

  // ---- Napelem + Ház + Bojler kártya ----
  // A tető, a nap (sugarakkal) és a napelem-rész oldala csak akkor sárga, ha van napelem-adat (en_page_update); addig szürke.
  make_shape(zl, EN_HC_X, EN_HC_TOP, EN_HC_W, EN_HC_BOTTOM - EN_HC_TOP, S(10), EN_COL_NODE);
  {
    lv_obj_t *roof = lv_line_create(zl);   // nyitott vonal (nincs vízszintes alapvonala): a tető és a napelem egy térben van
    lv_line_set_points(roof, EN_ROOF_PTS, 3);
    lv_obj_set_style_line_width(roof, S(6), 0);
    lv_obj_set_style_line_rounded(roof, true, 0);
    en_pv_lines[0] = roof;
  }
  for (int k = 0; k < 8; k++) {   // nap: teljes kör alakú sugárzás a tető jobb oldalán
    const float ang = k * 3.14159265f / 4;
    const float cs = cosf(ang), sn = sinf(ang);
    en_sun_ray_pts[k][0] = {(lv_coord_t)lroundf(EN_SUN_CX + cs * (EN_SUN_D / 2 + EN_SUN_GAP)), (lv_coord_t)lroundf(EN_SUN_CY + sn * (EN_SUN_D / 2 + EN_SUN_GAP))};
    en_sun_ray_pts[k][1] = {(lv_coord_t)lroundf(EN_SUN_CX + cs * (EN_SUN_D / 2 + EN_SUN_GAP + EN_SUN_RAY)), (lv_coord_t)lroundf(EN_SUN_CY + sn * (EN_SUN_D / 2 + EN_SUN_GAP + EN_SUN_RAY))};
    lv_obj_t *ray = lv_line_create(zl);
    lv_line_set_points(ray, en_sun_ray_pts[k], 2);
    lv_obj_set_style_line_width(ray, S(3), 0);
    lv_obj_set_style_line_rounded(ray, true, 0);
    en_pv_lines[1 + k] = ray;
  }
  en_pv_sun = make_shape(zl, EN_SUN_CX - EN_SUN_D / 2, EN_SUN_CY - EN_SUN_D / 2, EN_SUN_D, EN_SUN_D, LV_RADIUS_CIRCLE, EN_COL_NODE);
  lv_obj_set_style_border_width(en_pv_sun, S(3), 0);

  {   // a napelem-rész oldala (a tető alatt, a válaszvonalig)
    static const lv_point_precise_t EN_PV_SIDE_L[2] = {{EN_HC_X, EN_HC_TOP}, {EN_HC_X, EN_HC_DIV_Y}};
    static const lv_point_precise_t EN_PV_SIDE_R[2] = {{EN_HC_X + EN_HC_W, EN_HC_TOP}, {EN_HC_X + EN_HC_W, EN_HC_DIV_Y}};
    en_pv_lines[9] = en_pylon_line(zl, EN_PV_SIDE_L, S(5));
    en_pv_lines[10] = en_pylon_line(zl, EN_PV_SIDE_R, S(5));
  }
  lv_obj_t *pv_title = en_one_line(dlabel_c(zl, tr("Napelem", "Solar"), sm, COL_GREY, EN_HC_X, EN_HC_TOP - SY(16), EN_HC_W), EN_HC_W, S(18));   // a tetőbe lóg bele
  en_pv_val = en_one_line(dlabel_c(zl, "--", hu, COL_YELLOW, EN_HC_X, EN_HC_TOP + SY(2), EN_HC_W), EN_HC_W, S(32));
  (void)pv_title;

  // a válaszvonal + a Ház alsó/oldalsó kerete: mind a ház energiaszínét követi (en_page_update)
  static const lv_point_precise_t EN_HB_L[2] = {{EN_HC_X, EN_HC_DIV_Y}, {EN_HC_X, EN_HC_BOTTOM}};
  static const lv_point_precise_t EN_HB_R[2] = {{EN_HC_X + EN_HC_W, EN_HC_DIV_Y}, {EN_HC_X + EN_HC_W, EN_HC_BOTTOM}};
  static const lv_point_precise_t EN_HB_BOT[2] = {{EN_HC_X, EN_HC_BOTTOM}, {EN_HC_X + EN_HC_W, EN_HC_BOTTOM}};
  static const lv_point_precise_t EN_HB_DIV[2] = {{EN_HC_X, EN_HC_DIV_Y}, {EN_HC_X + EN_HC_W, EN_HC_DIV_Y}};
  en_house_border[0] = en_pylon_line(zl, EN_HB_L, S(5));
  en_house_border[1] = en_pylon_line(zl, EN_HB_R, S(5));
  en_house_border[2] = en_pylon_line(zl, EN_HB_BOT, S(5));
  en_house_border[3] = en_pylon_line(zl, EN_HB_DIV, S(4));

  lv_obj_t *house_title = en_one_line(dlabel(zl, tr("Ház", "House"), sm, COL_GREY, EN_HOUSE_TXT_X, EN_HC_DIV_Y + SY(10)), EN_HOUSE_TXT_W, S(20));
  en_house_val = en_one_line(dlabel(zl, "--", hu, COL_TEXT, EN_HOUSE_TXT_X, EN_HC_DIV_Y + SY(30)), EN_HOUSE_TXT_W, S(36));
  en_house_sub = en_one_line(dlabel(zl, "", sm, COL_GREY, EN_HOUSE_TXT_X, EN_HC_DIV_Y + SY(66)), EN_HOUSE_TXT_W, S(20));
  (void)house_title;

  {   // Bojler: kis bordás téglalap a kártyán belül, jobbra lent (nincs hozzá vonal/vezeték - benne van a házban)
    en_boil_box = make_shape(zl, EN_BOIL_X, EN_BOIL_Y, EN_BOIL_W, EN_BOIL_H, S(8), EN_COL_NODE);
    lv_obj_set_style_border_width(en_boil_box, S(3), 0);
    lv_obj_set_style_border_color(en_boil_box, lv_color_hex(EN_COL_GREEN), 0);
    const int iw = EN_BOIL_W - S(8), tw = iw - 2 * EN_BOIL_BADGE;   // a cím mindkét oldalon kihagyja a jel helyét (középre zárt)
    lv_obj_t *bt = en_one_line(dlabel_c(en_boil_box, tr("Bojler", "Water heater"), sm, COL_GREY, EN_BOIL_BADGE, S(4), tw), tw, S(16));
    en_boil_val  = en_one_line(dlabel_c(en_boil_box, "--", hu, COL_TEXT, 0, S(21), iw), iw, S(30));   // hőfok / cél-hőfok
    en_boil_mode = en_one_line(dlabel_c(en_boil_box, "", sm, COL_GREY, 0, S(53), iw), iw, S(16));     // üzemmód (vagy "kikapcsolva")
    en_boil_sub  = en_one_line(dlabel_c(en_boil_box, "", sm, COL_GREY, 0, S(70), iw), iw, S(16));     // kivehető melegvíz
    en_boil_w    = en_one_line(dlabel_c(en_boil_box, "", hu, COL_TEXT, 0, S(88), iw), iw, S(30));     // a felvett teljesítmény: a hőfokkal egyező méretben
    for (lv_obj_t *l : {en_boil_mode, en_boil_sub, en_boil_w}) lv_obj_set_style_pad_hor(l, S(4), 0);
    en_boil_badge = make_shape(en_boil_box, iw - EN_BOIL_BADGE - S(2), S(2), EN_BOIL_BADGE, EN_BOIL_BADGE, LV_RADIUS_CIRCLE, EN_COL_NODE);
    lv_obj_set_style_border_width(en_boil_badge, S(2), 0);
    en_boil_badge_l = lv_label_create(en_boil_badge);
    lv_label_set_text(en_boil_badge_l, "");
    lv_obj_center(en_boil_badge_l);
    lv_obj_set_hidden(en_boil_badge, true);
    (void)bt;
  }

  // ---- bal alul: a mai termelés (vászon; csak új adatnál rajzoljuk újra) ----
  lv_obj_t *zc = make_zone(page_en, EN_ZL_X, EN_ZC_Y, EN_ZL_W, EN_ZC_H, COL_BG_TL);
  lv_obj_t *ch_title = dlabel(zc, tr("Ma", "Today"), hu, COL_YELLOW, SX(16), SY(6));
  lv_obj_update_layout(ch_title);   // a csúcs felirata a cím után (az angol "Today" hosszabb, mint a "Ma")
  const int peak_x = SX(16) + lv_obj_get_width(ch_title) + SX(12), peak_w = EN_ZL_W - SX(16) - 4 * SX(76) - peak_x;
  en_ch_peak = dlabel(zc, "", sm, COL_GREY, peak_x, SY(16));
  lv_obj_set_size(en_ch_peak, peak_w, S(20));
  lv_label_set_long_mode(en_ch_peak, LV_LABEL_LONG_DOT);
  {   // jelmagyarázat jobbra: termelés, saját fogyasztás, hálózati vétel, bojler
    static const char *const LEG[2][4] = {{"termelés", "saját", "vétel", "bojler"}, {"solar", "own use", "import", "heater"}};
    static const uint32_t LC[4] = {EN_COL_PV, EN_COL_SELF, EN_COL_IMP, EN_COL_BOIL};
    for (int k = 0; k < 4; k++) en_one_line(dlabel_r(zc, LEG[g_lang][k], sm, LC[k], EN_ZL_W - SX(16) - (3 - k) * SX(76), SY(16), SX(70)), SX(70), S(20));
  }
  const size_t bytes = (size_t)EN_CH_W * EN_CH_H * 2 + 256;
  void *buf = heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (buf) {
    en_ch_cv = lv_canvas_create(zc);
    lv_canvas_set_buffer(en_ch_cv, buf, EN_CH_W, EN_CH_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(en_ch_cv, EN_CH_X, EN_CH_Y);
  }
  for (int k = 0; k < EN_CH_YLAB_MAX; k++) { en_ch_ylab[k] = dlabel_r(zc, "", sm, COL_GREY, EN_CH_X - SX(8), EN_CH_Y - S(9), SX(56)); lv_obj_set_hidden(en_ch_ylab[k], true); }
  for (int k = 0; k <= 8; k++) {   // 00, 03, 06, ..., 24 óra (3 óránként)
    char hb[4];
    snprintf(hb, sizeof(hb), "%02d", 3 * k);
    lv_obj_t *l = dlabel_c(zc, "", sm, COL_GREY, EN_CH_X + k * (EN_CH_W - 1) / 8 - SX(13), EN_CH_Y + EN_CH_H + SY(6), SX(26));
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
  dlabel(zr, tr("Bojler", "Water heater"), hu, EN_COL_BOIL, x0, y); y += EN_ROW;
  en_r_boil = en_one_line(dlabel(zr, "", hu, COL_TEXT, x0, y), lw, EN_ROW); y += EN_ROW;
  // alul: az adatok kora vagy a hiba (két sor)
  en_r_status = dlabel(zr, "", sm, COL_GREY, x0, EN_Z_H - SY(52));
  lv_obj_set_size(en_r_status, lw, SY(44));
  lv_label_set_long_mode(en_r_status, LV_LABEL_LONG_DOT);
}

// egy bordás doboz (Hálózat vagy Bojler) értéke és kerete; sub==NULL: nincs alszöveg-mező (pl. a Bojlernek)
static void en_box_set(lv_obj_t *ring, lv_obj_t *val, lv_obj_t *sub, const char *v, const char *s, uint32_t color, bool on) {
  set_label_if_changed(val, v);
  if (sub) set_label_if_changed(sub, s);
  lv_obj_set_style_border_color(ring, lv_color_hex(on ? color : COL_WHITE_DIM), 0);
  lv_obj_set_style_text_color(val, lv_color_hex(on ? color : COL_GREY), 0);
}

// color: 0 esetén COL_WHITE_DIM; rev: a pöttyök pts[1] -> pts[0] irányba mennek (pl. visszatáplálás)
static void en_flow_set(int i, float w, bool known, uint32_t color, bool rev) {
  EnFlow &f = en_flows[i];
  const bool act = known && w >= EN_FLOW_MIN_W;
  f.rev = rev;
  lv_obj_set_style_line_color(f.line, lv_color_hex(act ? color : COL_WHITE_DIM), 0);
  const bool run = act && en_page_visible;
  if (run != f.active) { f.active = run; en_flow_anim(f, run); }
  if (run) for (lv_obj_t *d : f.dot) lv_obj_set_style_bg_color(d, lv_color_hex(color), 0);
}

// "12:34" a Growatt "2026-10-21 12:34:56" idejéből
static const char *en_hhmm(const char *upd) {
  const char *sp = strchr(upd, ' ');
  static char b[6];
  if (!sp || strlen(sp + 1) < 5) return "?";
  memcpy(b, sp + 1, 5); b[5] = 0;
  return b;
}

// egy oszlop értéke a szeletekből adott vászonszélességre vetítve (a szomszédos szeletek közt egyenesen; NAN: nincs adat)
static float en_ch_pv(int x, int w) {
  const float pos = EN_CH_FROM + (float)x * EN_CH_N / w;
  const int s0 = (int)pos, s1 = s0 + 1 < EN_PV_N ? s0 + 1 : s0;
  if (en_pv.w[s0] == EN_PV_NONE) return NAN;
  const float v0 = en_pv.w[s0], v1 = en_pv.w[s1] == EN_PV_NONE ? v0 : en_pv.w[s1];
  return v0 + (v1 - v0) * (pos - s0);
}
static float en_ch_net(int x, int w) {
  const float pos = EN_CH_FROM + (float)x * EN_CH_N / w;
  const int s0 = (int)pos, s1 = s0 + 1 < EN_PV_N ? s0 + 1 : s0;
  if (en_pv.net[s0] == EN_NET_NONE) return NAN;
  const float v0 = en_pv.net[s0], v1 = en_pv.net[s1] == EN_NET_NONE ? v0 : en_pv.net[s1];
  return v0 + (v1 - v0) * (pos - s0);
}
static float en_ch_boil(int x, int w) {
  const float pos = EN_CH_FROM + (float)x * EN_CH_N / w;
  const int s0 = (int)pos, s1 = s0 + 1 < EN_PV_N ? s0 + 1 : s0;
  if (en_pv.boil[s0] == EN_NET_NONE) return NAN;
  const float v0 = en_pv.boil[s0], v1 = en_pv.boil[s1] == EN_NET_NONE ? v0 : en_pv.boil[s1];
  return v0 + (v1 - v0) * (pos - s0);
}
// termelés (p), saját fogyasztás (self) és vétel (imp), W: a hálózati előjeles érték és a termelés alapján. Hálózati adat
// nélkül (pl. nem elérhető a P1) a saját fogyasztás nem tudható: csak a termelés látszik (zöld), kék és lila nélkül.
static void en_ch_split(float pv, float net, float &p, float &self, float &imp) {
  p = isnan(pv) ? 0 : pv;
  if (isnan(net)) { self = 0; imp = 0; return; }
  imp = net > 0 ? net : 0;
  const float ex = net < 0 ? -net : 0;
  self = p > ex ? p - ex : 0;
}

// A diagram skálája: a mai termeléshez (vagy a beállított kWp-hez) igazodó tető, és a napi csúcs. today=false esetén
// nincs mai adat, a tető az eddigi (vagy alapértelmezett) értéken marad.
static void en_chart_scale(bool today, int *out_top, int *out_peak, int *out_peak_s) {
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
  *out_top = top; *out_peak = peak; *out_peak_s = peak_s;
}

// A diagram magasságában hol van egy adott W-érték (h-1: 0 W, 0: a "top" tetején)
static int en_ch_y_of(float w_val, int top, int h) { return h - 1 - (int)lroundf((h - 1) * w_val / top); }

// A mai nap kirajzolása egy vászonba (kis- vagy teljes képernyős): háttér, rácsvonalak (0, 2 kW-onként a "top"-ig;
// 3 óránként), oszloponként a termelés/saját/vétel (lásd en_ch_split), rajtuk a bojler teljesítményének vékony piros nyoma.
static void en_chart_paint(lv_obj_t *cv, int w, int h, int top, bool today) {
  const lv_color_t bg = lv_color_hex(COL_BG_TL), grid = lv_color_hex(EN_CH_GRID);
  const lv_color_t c_edge = lv_color_hex(EN_COL_PV), c_pv = lv_color_hex(mix_col(COL_BG_TL, EN_COL_PV, 55));
  const lv_color_t c_self = lv_color_hex(EN_COL_SELF), c_imp = lv_color_hex(EN_COL_IMP), c_boil = lv_color_hex(EN_COL_BOIL);
  lv_canvas_fill_bg(cv, bg, LV_OPA_COVER);
  for (int x = 0; x < w; x++) lv_canvas_set_px(cv, x, h - 1, grid, LV_OPA_COVER);   // 0 W
  for (int s = EN_CH_YSTEP; s <= top; s += EN_CH_YSTEP) {
    const int y = en_ch_y_of((float)s, top, h);
    for (int x = 0; x < w; x++) lv_canvas_set_px(cv, x, y, grid, LV_OPA_COVER);
  }
  for (int k = 0; k <= 8; k++) {   // 3 óránként, az időtengely feliratokkal egyezően
    const int x = k * (w - 1) / 8;
    for (int y = 0; y < h; y++) lv_canvas_set_px(cv, x, y, grid, LV_OPA_COVER);
  }
  if (today) {
    const int lw = S(2);          // a termelés felső szélének vastagsága
    const int bw = max(S(3), h / 60);   // a bojler-nyom vastagsága
    const float k = (float)(h - 1) / top;
    for (int x = 0; x < w; x++) {
      const float pv = en_ch_pv(x, w), net = en_ch_net(x, w), boil = en_ch_boil(x, w);
      if (!isnan(pv) || !isnan(net)) {
        float p, self, imp;
        en_ch_split(pv, net, p, self, imp);
        const int hp = min((int)(p * k), h - 1), hs = min((int)(self * k), h - 1), hh = min((int)((self + imp) * k), h - 1);   // a tetején levágva
        for (int y = 0; y <= hp; y++) lv_canvas_set_px(cv, x, h - 1 - y, c_pv, LV_OPA_COVER);
        if (self > 0) for (int y = 0; y <= hs; y++) lv_canvas_set_px(cv, x, h - 1 - y, c_self, LV_OPA_COVER);   // 0-nál ne maradjon kék sor
        for (int y = hs + 1; y <= hh; y++) lv_canvas_set_px(cv, x, h - 1 - y, c_imp, LV_OPA_COVER);
        if (hp > 0) for (int y = hp - lw + 1; y <= hp; y++) if (y >= 0) lv_canvas_set_px(cv, x, h - 1 - y, c_edge, LV_OPA_COVER);
      }
      if (!isnan(boil)) {   // a bojler teljesítménye: vékony piros nyom (nem a 0-tól töltve, csak a vonal maga)
        const int hb = min((int)(boil * k), h - 1);
        for (int y = max(0, hb - bw + 1); y <= hb; y++) lv_canvas_set_px(cv, x, h - 1 - y, c_boil, LV_OPA_COVER);
      }
    }
  }
  lv_obj_invalidate(cv);
}

// A mai nap kirajzolása (csak ha új adat jött, vagy változott a skála / a nap); nyitott teljes képernyős nézetnél azt is frissíti
static void en_chart_update() {
  if (!en_ch_cv) return;
  struct tm t;
  const bool today = get_local_tm(t) && en_pv.key == (t.tm_year + 1900) * 400 + t.tm_yday;
  int top, peak, peak_s;
  en_chart_scale(today, &top, &peak, &peak_s);
  const uint32_t ver = en_pv_ver * 31u + (uint32_t)top + (today ? 1u : 0u);
  if (ver == en_ch_ver) return;
  en_ch_ver = ver;

  char b[48];
  for (int k = 0; k < EN_CH_YLAB_MAX; k++) {
    const int wv = k * EN_CH_YSTEP;
    const bool show = wv <= top;
    if (show) {
      if (wv == 0) strlcpy(b, "0", sizeof(b)); else snprintf(b, sizeof(b), "%d kW", wv / 1000);
      set_label_if_changed(en_ch_ylab[k], b);
      lv_obj_set_pos(en_ch_ylab[k], EN_CH_X - SX(8) - SX(56), EN_CH_Y + en_ch_y_of((float)wv, top, EN_CH_H) - S(9));
    }
    lv_obj_set_hidden(en_ch_ylab[k], !show);
    if (en_fsch_ylab[k]) {
      if (show) {
        set_label_if_changed(en_fsch_ylab[k], b);
        lv_obj_set_pos(en_fsch_ylab[k], EN_FSCH_X - SX(10) - SX(70), EN_FSCH_Y + en_ch_y_of((float)wv, top, EN_FSCH_H) - S(14));
      }
      lv_obj_set_hidden(en_fsch_ylab[k], !show);
    }
  }
  if (peak > 0) snprintf(b, sizeof(b), tr("csúcs %.1f kW (%02d:%02d)", "peak %.1f kW (%02d:%02d)"), peak / 1000.0f, peak_s / 12, peak_s % 12 * 5);
  else b[0] = 0;
  set_label_if_changed(en_ch_peak, b);
  if (en_fsch_peak) set_label_if_changed(en_fsch_peak, b);

  en_chart_paint(en_ch_cv, EN_CH_W, EN_CH_H, top, today);
  if (en_fs_open && en_fsch_cv) en_chart_paint(en_fsch_cv, EN_FSCH_W, EN_FSCH_H, top, today);
}

// Teljes képernyős diagram létrehozása (első megnyitáskor): ugyanaz, mint a kis diagram, csak nagyobb vásznon,
// jelmagyarázattal és X bezáró gombbal a jobb felső sarokban.
static void en_chart_fs_create() {
  en_fs_page = make_group(lv_screen_active(), 0, 0, LCD_H_RES, LCD_V_RES);
  lv_obj_t *card = lv_obj_create(en_fs_page);
  lv_obj_add_style(card, &style_card, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_set_size(card, LCD_H_RES, LCD_V_RES);
  lv_obj_set_pos(card, 0, 0);
  lv_obj_set_style_radius(card, 0, 0);
  lv_obj_set_scrollable(card, false);

  dlabel(card, tr("Energia ma", "Energy today"), &font_hu_28, COL_YELLOW, SX(20), SY(14));
  en_fsch_peak = dlabel(card, "", &font_hu_28, COL_GREY, SX(20), SY(56));
  lv_obj_set_size(en_fsch_peak, SX(340), S(36));
  lv_label_set_long_mode(en_fsch_peak, LV_LABEL_LONG_DOT);

  static const char *const LEG[2][4] = {{"termelés", "saját", "vétel", "bojler"}, {"solar", "own use", "import", "heater"}};
  static const uint32_t LC[4] = {EN_COL_PV, EN_COL_SELF, EN_COL_IMP, EN_COL_BOIL};
  for (int k = 0; k < 4; k++) {   // saját sor a cím alatt, bal oldalt (a jobb felső X-et így biztosan nem éri el)
    make_shape(card, SX(20) + k * SX(160), SY(92), S(30), S(8), S(4), LC[k]);
    dlabel(card, LEG[g_lang][k], &font_hu_28, LC[k], SX(20) + k * SX(160) + SX(38), SY(84));
  }

  lv_obj_t *xb = make_shape(card, LCD_H_RES - EN_FSCH_XBTN - SX(12), SY(10), EN_FSCH_XBTN, EN_FSCH_XBTN - S(24), S(12), 0x3A3A3A);
  lv_obj_t *xl = lv_label_create(xb);
  lv_label_set_text(xl, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_font(xl, &FONT_SYM, 0);
  lv_obj_set_style_text_color(xl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_center(xl);

  const size_t bytes = (size_t)EN_FSCH_W * EN_FSCH_H * 2 + 256;
  void *buf = heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (buf) {
    en_fsch_cv = lv_canvas_create(card);
    lv_canvas_set_buffer(en_fsch_cv, buf, EN_FSCH_W, EN_FSCH_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(en_fsch_cv, EN_FSCH_X, EN_FSCH_Y);
  }
  for (int k = 0; k < EN_CH_YLAB_MAX; k++) { en_fsch_ylab[k] = dlabel_r(card, "", &font_hu_28, COL_GREY, EN_FSCH_X - SX(10), EN_FSCH_Y - S(14), SX(70)); lv_obj_set_hidden(en_fsch_ylab[k], true); }
  for (int k = 0; k <= 8; k++) {
    char hb[4];
    snprintf(hb, sizeof(hb), "%02d", 3 * k);
    lv_obj_t *l = dlabel_c(card, "", &font_hu_28, COL_GREY, EN_FSCH_X + k * (EN_FSCH_W - 1) / 8 - SX(24), EN_FSCH_Y + EN_FSCH_H + SY(10), SX(48));
    lv_label_set_text(l, hb);
  }
  lv_obj_set_hidden(en_fs_page, true);
}

// A kis diagramra koppintva nyílik; az X zárja (lásd en_chart_fs_close_hit, idojaras_15_erintes.ino)
static void en_chart_fs_open() {
  if (!en_fs_page) en_chart_fs_create();
  if (!en_fs_page) return;
  en_fs_open = true;
  en_ch_ver = 0;   // a következő en_chart_update biztosan újrarajzol (a nagy vásznat is)
  en_chart_update();
  lv_obj_set_hidden(en_fs_page, false);
}

static void en_chart_fs_close() {
  en_fs_open = false;
  if (en_fs_page) lv_obj_set_hidden(en_fs_page, true);
}

// true, ha (x,y) a kis diagram vásznán belül van (idojaras_15_erintes.ino hívja)
static bool en_chart_hit(int x, int y) {
  return en_ch_cv && x >= EN_ZL_X + EN_CH_X && x < EN_ZL_X + EN_CH_X + EN_CH_W && y >= EN_ZC_Y + EN_CH_Y && y < EN_ZC_Y + EN_CH_Y + EN_CH_H;
}

// true, ha (x,y) a Bojler dobozán van (idojaras_15_erintes.ino hívja: a Beállítások Bojler füle nyílik)
static bool en_boil_hit(int x, int y) {
  return en_boil_box && x >= EN_ZL_X + EN_BOIL_X && x < EN_ZL_X + EN_BOIL_X + EN_BOIL_W && y >= EN_Z_Y + EN_BOIL_Y && y < EN_Z_Y + EN_BOIL_Y + EN_BOIL_H;
}

// true, ha (x,y) a teljes képernyős diagram bezáró sarkában van (idojaras_15_erintes.ino hívja)
static bool en_chart_fs_close_hit(int x, int y) {
  return x >= LCD_H_RES - EN_FSCH_CLOSE_ZONE && y <= EN_FSCH_CLOSE_ZONE;
}

// Az oldal frissítése (a látható oldalon másodpercenként)
static void en_page_update() {
  if (!page_en) return;
  const EnData d = en_snapshot();
  const bool p1 = en_fresh(d.p1_ms, EN_LOCAL_STALE_MS), sk = en_fresh(d.sk_ms, EN_LOCAL_STALE_MS), gw = en_fresh(d.gw_ms, EN_GW_STALE_MS);
  const float imp = p1 && d.grid_w > 0 ? d.grid_w : 0, exp = p1 && d.grid_w < 0 ? -d.grid_w : 0;
  const float house = en_house_w(d);
  char v[24], s[40];

  // Napelem (szöveg a tetőben); a tető, a nap és a napelem-rész oldala csak akkor sárga, ha van napelem-adat
  if (gw) snprintf(v, sizeof(v), "%.0f W", d.pv_w); else strlcpy(v, "--", sizeof(v));
  set_label_if_changed(en_pv_val, v);
  const uint32_t pv_col = gw ? COL_YELLOW : COL_WHITE_DIM;
  lv_obj_set_style_text_color(en_pv_val, lv_color_hex(gw ? COL_YELLOW : COL_GREY), 0);
  for (lv_obj_t *l : en_pv_lines) if (l) lv_obj_set_style_line_color(l, lv_color_hex(pv_col), 0);
  if (en_pv_sun) lv_obj_set_style_border_color(en_pv_sun, lv_color_hex(pv_col), 0);

  // Hálózat: a doboz és az oszlop is az energiahelyzet színét követi (lila: vétel, zöld: visszatáplálás)
  if (p1) snprintf(v, sizeof(v), "%.0f W", fabsf(d.grid_w)); else strlcpy(v, "--", sizeof(v));
  const uint32_t grid_col = exp > 0 ? EN_COL_GREEN : EN_COL_IMP;
  en_box_set(en_grid_box, en_grid_val, en_grid_sub, v, !p1 ? "" : exp > 0 ? tr("leadás", "export") : tr("vétel", "import"), grid_col, p1);
  const uint32_t pylon_col = p1 ? grid_col : COL_WHITE_DIM;
  for (lv_obj_t *l : en_grid_lines) if (l) lv_obj_set_style_line_color(l, lv_color_hex(pylon_col), 0);
  for (lv_obj_t *d2 : en_grid_dots) if (d2) lv_obj_set_style_bg_color(d2, lv_color_hex(pylon_col), 0);

  // a ház színe: lila, ha csak a hálózatból táplálódik (nincs érdemi napelem); kék, ha mindkét forrásból (hálózat is,
  // napelem is); zöld, ha a napelem önmagában ellátja (nincs vétel). A válaszvonal és a Ház alsó kerete ezt követi
  // (a tető és a napelem-rész felette marad sárga).
  const bool solar_active = gw && d.pv_w >= EN_FLOW_MIN_W;
  const uint32_t house_col = !p1 || d.grid_w <= 0 ? EN_COL_GREEN : solar_active ? COL_BLUE : EN_COL_IMP;
  for (lv_obj_t *l : en_house_border) if (l) lv_obj_set_style_line_color(l, lv_color_hex(!isnan(house) ? house_col : COL_WHITE_DIM), 0);
  if (!isnan(house)) snprintf(v, sizeof(v), "%.0f W", house); else strlcpy(v, "--", sizeof(v));
  set_label_if_changed(en_house_val, v);
  lv_obj_set_style_text_color(en_house_val, lv_color_hex(!isnan(house) ? house_col : COL_GREY), 0);
  set_label_if_changed(en_house_sub, isnan(house) ? "" : tr("becsült", "estimated"));

  // Bojler: zöld, ha tartja (készenlét), piros, ha fűt. A hőfok (Tuya) ha van, különben csak a wattos fogyasztás
  // (a dugalj méréséből) dönti el az állapotot: fűt, ha a wattos fogyasztás > 3 W.
  const bool bl = en_fresh(d.bl_ms, EN_BL_STALE_MS);
  const bool heating = bl ? strcmp(d.boil_state, "heating") == 0 : sk && d.boil_w > EN_BOIL_HEAT_W;
  char sub[24] = "", wb[16] = "";
  const char *mode = "";
  if (bl) {
    if (!isnan(d.boil_set)) snprintf(v, sizeof(v), "%.0f/%.0f°C", d.boil_temp, d.boil_set);   // hőfok / cél-hőfok
    else snprintf(v, sizeof(v), "%.0f°C", d.boil_temp);
    const int mi = en_bl_mode_idx(d.boil_mode);
    mode = d.boil_sw == 0 ? tr("kikapcsolva", "off") : mi >= 0 ? TR2(EN_BL_MODE_T)[mi] : d.boil_mode;
    if (!isnan(d.boil_water_pct)) snprintf(sub, sizeof(sub), tr("%.0f%% melegvíz", "%.0f%% hot water"), d.boil_water_pct);
    if (sk) snprintf(wb, sizeof(wb), "%.0f W", d.boil_w);
  } else if (sk) snprintf(v, sizeof(v), "%.0f W", d.boil_w);
  else strlcpy(v, "--", sizeof(v));
  const uint32_t boil_col = heating ? EN_COL_BOIL : EN_COL_GREEN;
  en_box_set(en_boil_box, en_boil_val, en_boil_sub, v, sub, boil_col, bl || sk);
  if (en_boil_w) {
    set_label_if_changed(en_boil_w, wb);
    lv_obj_set_style_text_color(en_boil_w, lv_color_hex(wb[0] ? boil_col : COL_GREY), 0);
  }
  if (en_boil_mode) {   // az üzemmód helyén a hibakód (pirosan), ha a bojler hibát jelez
    char fe[40], ft[48];
    en_bl_fault_text(bl ? d.boil_fault : 0, fe, sizeof(fe));
    if (fe[0]) snprintf(ft, sizeof(ft), tr("Hiba: %s", "Error: %s"), fe);
    set_label_if_changed(en_boil_mode, fe[0] ? ft : mode);
    lv_obj_set_style_text_color(en_boil_mode, lv_color_hex(fe[0] ? COL_WARN : COL_GREY), 0);
  }
  if (en_boil_badge) {   // kéz: kézi vezérlés, "A": napelemes automatika, "?": a vezérlő kijelző nem elérhető (csak ha a bojler Tuyán elérhető)
    const int au = en_bl_eff_auto(d);
    lv_obj_set_hidden(en_boil_badge, !bl);
    lv_obj_set_style_border_color(en_boil_badge, lv_color_hex(au > 0 ? COL_YELLOW : COL_GREY), 0);
    lv_obj_set_style_text_color(en_boil_badge_l, lv_color_hex(au > 0 ? COL_YELLOW : COL_GREY), 0);
    lv_obj_set_style_text_font(en_boil_badge_l, au == 0 ? &font_sym_hand : &font_hu_14, 0);
    set_label_if_changed(en_boil_badge_l, au > 0 ? "A" : au == 0 ? EN_SYM_HAND : "?");
  }

  // áramlás: a hálózat és a ház közt egyetlen vonal viszi a vételt (lila, hálózat -> ház) vagy a visszatáplálást
  // (zöld, ház -> hálózat, megfordított pöttyök); a napelem mindig a házba folyik, a bojler a házban van - ezekhez
  // nincs rajzolt vonal.
  if (p1 && exp > 0) en_flow_set(EN_F_GRID_HOUSE, exp, true, EN_COL_GREEN, true);
  else en_flow_set(EN_F_GRID_HOUSE, imp, p1, EN_COL_IMP, false);

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
  const char *who = err == d.p1_err ? tr("Hálózat", "Grid") : err == d.sk_err ? tr("Bojler", "Water heater") : tr("Napelem", "Solar");
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
