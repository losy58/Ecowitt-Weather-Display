// Rajzoló segédfüggvények: kártya, alakzatok, feliratok, nap, hold, szél-iránytű
// (Az idojaras.ino része: az Arduino IDE és a PlatformIO a fájlokat ábécésorrendben, egy programmá fűzi össze.)

// Egy kártya: col 0..2, row 0..3, a sorok fentről lefelé kerülnek bele
static void make_card(int col, int row, uint32_t color, std::initializer_list<LineDef> lines) {
  lv_obj_t *card = lv_obj_create(page_details);
  lv_obj_add_style(card, &style_card, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(color), 0);
  lv_obj_set_size(card, CARD_W, CARD_H);
  lv_obj_set_pos(card, CARD_X0 + col * (CARD_W + CARD_GAP), CARD_Y0 + row * (CARD_H + CARD_GAP));
  lv_obj_set_scrollable(card, false);

  int y = 0;
  for (const LineDef &l : lines) {
    lv_obj_t *obj = lv_label_create(card);
    lv_label_set_text(obj, l.text);
    lv_obj_set_pos(obj, 0, y);
    lv_obj_set_width(obj, CARD_W - SX(20));                   // a kártya belseje (a szegéllyel és a margóval együtt)
    lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);        // ami nem fér ki, "..."-tal végződik, nem lóg ki
    lbl[l.idx] = obj;
    y += LINE_H;
  }
}

// ---------------------------------------------------------------------------
// Rajzoló segédfüggvények
// ---------------------------------------------------------------------------
static lv_obj_t *make_group(lv_obj_t *parent, int x, int y, int w, int h) {
  lv_obj_t *g = lv_obj_create(parent);
  lv_obj_set_size(g, w, h);
  lv_obj_set_pos(g, x, y);
  lv_obj_set_style_bg_opa(g, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(g, 0, 0);
  lv_obj_set_style_pad_all(g, 0, 0);
  lv_obj_set_style_radius(g, 0, 0);
  lv_obj_set_scrollable(g, false);
  return g;
}

static lv_obj_t *make_shape(lv_obj_t *parent, int x, int y, int w, int h, int radius, uint32_t color) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_set_size(o, w, h);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_set_scrollable(o, false);
  return o;
}

static lv_obj_t *make_zone(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg) {
  return make_shape(parent, x, y, w, h, S(10), bg);
}

static lv_obj_t *make_ring(lv_obj_t *parent, int x, int y, int d, uint32_t color, int width) {
  lv_obj_t *o = make_shape(parent, x, y, d, d, LV_RADIUS_CIRCLE, color);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(o, width, 0);
  lv_obj_set_style_border_color(o, lv_color_hex(color), 0);
  lv_obj_set_style_border_opa(o, LV_OPA_50, 0);
  return o;
}

// Nap sugarakkal: lemez + n db sugár, pixelenként számolva (3x3 átlagolás). clip_y >= 0: az alatta lévő rész levágva (horizont).
static lv_obj_t *make_sun(lv_obj_t *parent, int x, int y, int w, int h, float cx, float cy,
                          float R, float rin, float rout, float hw, int nrays, float clip_y) {
  size_t bytes = (size_t)w * h * 4 + 256;
  void *buf = heap_caps_aligned_alloc(64, bytes, bytes > 16384 ? (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (!buf) return NULL;
  lv_obj_t *cv = lv_canvas_create(parent);
  lv_canvas_set_buffer(cv, buf, w, h, LV_COLOR_FORMAT_ARGB8888);
  lv_canvas_fill_bg(cv, lv_color_black(), LV_OPA_TRANSP);
  lv_obj_set_pos(cv, x, y);

  const int dr = 0xFF, dg = 0xCB, db = 0x3D;   // a lemez színe
  const int rr = 0xF2, rg = 0xA8, rb = 0x1E;   // a sugarak színe
  const float step = 6.2831853f / nrays;

  for (int py = 0; py < h; py++) {
    for (int px = 0; px < w; px++) {
      int nd = 0, nr = 0;
      for (int sy = 0; sy < 3; sy++) {
        for (int sx = 0; sx < 3; sx++) {
          float fx = px + (sx + 0.5f) / 3.0f, fy = py + (sy + 0.5f) / 3.0f;
          if (clip_y >= 0 && fy > clip_y) continue;
          float dx = fx - cx, dy = fy - cy;
          if (dx * dx + dy * dy <= R * R) { nd++; continue; }
          for (int k = 0; k < nrays; k++) {
            float a = k * step;
            float u = dx * cosf(a) + dy * sinf(a);
            if (u < rin || u > rout) continue;
            float v = -dx * sinf(a) + dy * cosf(a);
            float half = hw * (1.0f - 0.35f * (u - rin) / (rout - rin));   // a sugár a vége felé kicsit vékonyodik
            if (v >= -half && v <= half) { nr++; break; }
          }
        }
      }
      int n = nd + nr;
      if (!n) continue;
      lv_color_t c = lv_color_make((dr * nd + rr * nr) / n, (dg * nd + rg * nr) / n, (db * nd + rb * nr) / n);
      lv_canvas_set_px(cv, px, py, c, (lv_opa_t)(n * 255 / 9));
    }
  }
  return cv;
}

// Felhő: lekerekített alap + két kör, w a teljes szélesség (a magasság kb. 0,54 * w)
static void make_cloud(lv_obj_t *parent, int x, int y, int w, uint32_t color) {
  float s = w / 130.0f;
  make_shape(parent, x, y + (int)(28 * s), w, (int)(42 * s), (int)(21 * s), color);
  make_shape(parent, x + (int)(34 * s), y, (int)(64 * s), (int)(64 * s), LV_RADIUS_CIRCLE, color);
  make_shape(parent, x + (int)(78 * s), y + (int)(12 * s), (int)(46 * s), (int)(46 * s), LV_RADIUS_CIRCLE, color);
}

static lv_obj_t *dlabel(lv_obj_t *parent, const char *txt, const lv_font_t *font, uint32_t color, int x, int y) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_pos(l, x, y);
  return l;
}

// Jobbra igazított felirat: a szöveg jobb széle a right_x pixelen áll (w legyen bőven elég széles)
static lv_obj_t *dlabel_r(lv_obj_t *parent, const char *txt, const lv_font_t *font, uint32_t color, int right_x, int y, int w) {
  lv_obj_t *l = dlabel(parent, txt, font, color, right_x - w, y);
  lv_obj_set_width(l, w);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
  return l;
}

static lv_obj_t *dlabel_c(lv_obj_t *parent, const char *txt, const lv_font_t *font, uint32_t color, int x, int y, int w) {
  lv_obj_t *l = dlabel(parent, txt, font, color, x, y);
  lv_obj_set_width(l, w);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  return l;
}

// Hétszegmenses érték "szellem" szegmensekkel: a halvány 8-asok mögött világít az aktuális érték
static lv_obj_t *seg_left(lv_obj_t *parent, const char *ghost, const char *txt, const lv_font_t *font, uint32_t col, uint32_t dim, int x, int y) {
  dlabel(parent, ghost, font, dim, x, y);
  return dlabel(parent, txt, font, col, x, y);
}

static lv_obj_t *seg_right(lv_obj_t *parent, const char *ghost, const char *txt, const lv_font_t *font, uint32_t col, uint32_t dim, int right_x, int y, int w) {
  dlabel_r(parent, ghost, font, dim, right_x, y, w);
  return dlabel_r(parent, txt, font, col, right_x, y, w);
}

// ---------------------------------------------------------------------------
// Napkelte / napnyugta (NOAA-képlet, +-1-2 perc pontosság). A helyi időeltolást a rendszeróra adja, így a nyári/téli idő automatikus.
// ---------------------------------------------------------------------------
static bool sun_event(int yday1, bool rise, int utc_off_min, int *out_min) {
  const double PI_ = 3.14159265358979;
  double g = 2.0 * PI_ / 365.0 * (yday1 - 1);
  double eq = 229.18 * (0.000075 + 0.001868 * cos(g) - 0.032077 * sin(g) - 0.014615 * cos(2 * g) - 0.040849 * sin(2 * g));
  double dec = 0.006918 - 0.399912 * cos(g) + 0.070257 * sin(g) - 0.006758 * cos(2 * g) + 0.000907 * sin(2 * g)
             - 0.002697 * cos(3 * g) + 0.00148 * sin(3 * g);
  if (!cfg.has_loc) return false;
  double la = cfg.lat * PI_ / 180.0;
  double c = cos(90.833 * PI_ / 180.0) / (cos(la) * cos(dec)) - tan(la) * tan(dec);
  if (c > 1.0 || c < -1.0) return false;   // sarki nap / sarki éjszaka
  double ha = acos(c) * 180.0 / PI_;
  double m = 720.0 - 4.0 * (cfg.lon + (rise ? ha : -ha)) - eq + utc_off_min;
  int mi = (int)lround(m) % 1440;
  if (mi < 0) mi += 1440;
  *out_min = mi;
  return true;
}

static int sun_last_key = -1;   // -1: a következő hívás biztosan újraszámol (pl. a hely megváltozása után)

static void update_sun_labels(const struct tm &t) {
  int key = t.tm_yday * 24 + t.tm_hour;
  if (key == sun_last_key) return;
  sun_last_key = key;

  time_t now = time(nullptr);
  int local_min = t.tm_hour * 60 + t.tm_min;
  int utc_min = (int)((now % 86400L) / 60L);
  int off = local_min - utc_min;
  while (off > 720) off -= 1440;
  while (off < -720) off += 1440;

  int rise, set;
  char b[24];
  if (sun_event(t.tm_yday + 1, true, off, &rise) && sun_event(t.tm_yday + 1, false, off, &set)) {
    snprintf(b, sizeof(b), "%02d:%02d", rise / 60, rise % 60);  set_label_if_changed(d_sunrise, b);
    snprintf(b, sizeof(b), "%02d:%02d", set / 60, set % 60);    set_label_if_changed(d_sunset, b);
  } else {
    set_label_if_changed(d_sunrise, "--:--");
    set_label_if_changed(d_sunset, "--:--");
  }
}

// ---------------------------------------------------------------------------
// Holdfázis: a pixeleket számoljuk (3x3 átlagolással), egyszer óránként frissül
// ---------------------------------------------------------------------------
static void moon_init(lv_obj_t *parent, int x, int y) {
  void *buf = heap_caps_aligned_alloc(64, MOON_D * MOON_D * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!buf) return;
  moon_canvas = lv_canvas_create(parent);
  lv_canvas_set_buffer(moon_canvas, buf, MOON_D, MOON_D, LV_COLOR_FORMAT_RGB565);
  lv_obj_set_pos(moon_canvas, x, y);
}

static float moon_phase(time_t t) {
  double jd = (double)t / 86400.0 + 2440587.5;             // Julian-dátum
  double ph = (jd - 2451550.26) / 29.530588861;            // 2000-01-06 újhold óta eltelt szinódikus hónapok
  ph -= floor(ph);
  return (float)ph;                                         // 0 = újhold, 0,5 = telihold
}

static void moon_draw(float f) {
  if (!moon_canvas) return;

  const int bg_r = (COL_BG_TR >> 16) & 0xFF, bg_g = (COL_BG_TR >> 8) & 0xFF, bg_b = COL_BG_TR & 0xFF;
  const int da_r = 0x3A, da_g = 0x3A, da_b = 0x3A;          // a hold sötét oldala
  const int li_r = 0xF2, li_g = 0xF2, li_b = 0xF2;          // a megvilágított rész (fehér)
  const float R = MOON_D / 2.0f;
  const float k = cosf(2.0f * 3.14159265f * f);
  const bool waxing = f < 0.5f;

  for (int y = 0; y < MOON_D; y++) {
    for (int x = 0; x < MOON_D; x++) {
      int cov = 0, lit = 0;
      for (int sy = 0; sy < 3; sy++) {
        for (int sx = 0; sx < 3; sx++) {
          float nx = (x + (sx + 0.5f) / 3.0f - R) / R;
          float ny = (y + (sy + 0.5f) / 3.0f - R) / R;
          if (nx * nx + ny * ny > 1.0f) continue;
          cov++;
          float thr = k * sqrtf(1.0f - ny * ny);
          if (waxing ? (nx > thr) : (nx < -thr)) lit++;
        }
      }
      int r = (bg_r * (9 - cov) + da_r * (cov - lit) + li_r * lit) / 9;
      int g = (bg_g * (9 - cov) + da_g * (cov - lit) + li_g * lit) / 9;
      int b = (bg_b * (9 - cov) + da_b * (cov - lit) + li_b * lit) / 9;
      lv_canvas_set_px(moon_canvas, x, y, lv_color_make(r, g, b), LV_OPA_COVER);
    }
  }
  lv_obj_invalidate(moon_canvas);
}

// ---------------------------------------------------------------------------
// Szél-iránytű: gyűrű, négy égtáj, a közepén a sebesség, a gyűrűn a mutató (vászonra rajzolva, csak irányváltozáskor)
// ---------------------------------------------------------------------------
constexpr bool WIND_ARROW_FROM = true;   // true: a mutató azt az irányt jelöli, ahonnan a szél fúj (mint a "DNY" felirat); false: ahová fúj
constexpr int  WC_R = S(96);             // az iránytű sugara (pixel)
constexpr int  WC_HUB = S(58);           // a középső tárcsa sugara (a mutató ez alatt nem látszik)

// Az iránytű skálája: vonal minden 5 foknál, hosszabb 30 foknál és még hosszabb a 4 égtájnál (az "É" piros). Egyszer rajzoljuk, vászonra.
static void wind_scale_create(lv_obj_t *parent, int cx, int cy) {
  const int D = WC_R * 2;
  size_t bytes = (size_t)D * D * 4 + 256;
  void *buf = heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) return;
  lv_obj_t *cv = lv_canvas_create(parent);
  lv_canvas_set_buffer(cv, buf, D, D, LV_COLOR_FORMAT_ARGB8888);
  lv_canvas_fill_bg(cv, lv_color_black(), LV_OPA_TRANSP);
  lv_obj_set_pos(cv, cx - WC_R, cy - WC_R);

  const float RAD2DEG = 57.2957795f, DEG2RAD = 0.0174532925f;
  const float rout = WC_R - 2.0f;                      // a gyűrű pereme alatt kezdődnek a vonalak
  for (int py = 0; py < D; py++) {
    for (int px = 0; px < D; px++) {
      float qx = px + 0.5f - WC_R, qy = py + 0.5f - WC_R;
      float rc = sqrtf(qx * qx + qy * qy);
      if (rc < rout - S(17.0f) || rc > rout + 1.5f) continue;   // gyors kizárás: csak a skála sávja
      int hits = 0, cr = 0, cg = 0, cb = 0;
      for (int sy = 0; sy < 3; sy++) {
        for (int sx = 0; sx < 3; sx++) {
          float fx = px + (sx + 0.5f) / 3.0f - WC_R, fy = py + (sy + 0.5f) / 3.0f - WC_R;
          float r = sqrtf(fx * fx + fy * fy);
          float deg = atan2f(fx, -fy) * RAD2DEG;         // 0 = fent, óramutató járása szerint
          if (deg < 0) deg += 360.0f;
          int kk = (int)lroundf(deg / 5.0f);
          float lateral = r * fabsf(deg - kk * 5.0f) * DEG2RAD;
          int k = kk % 72;
          bool card = (k % 18 == 0), major = (k % 6 == 0);
          float len = S(card ? 15.0f : (major ? 11.0f : 6.0f));
          float hw  = S(card ? 1.5f : (major ? 1.1f : 0.7f));
          if (r <= rout && r >= rout - len && lateral <= hw) {
            hits++;
            uint32_t c = (k == 0) ? 0xEF5350 : (card ? 0xE0E0E0 : (major ? 0xB0B0B0 : 0x6A6A6A));
            cr += (c >> 16) & 0xFF; cg += (c >> 8) & 0xFF; cb += c & 0xFF;
          }
        }
      }
      if (hits) lv_canvas_set_px(cv, px, py, lv_color_make(cr / hits, cg / hits, cb / hits), (lv_opa_t)(hits * 255 / 9));
    }
  }
}

static void wind_needle_set(float deg) {
  if (!wind_cv) return;
  int a = ((int)lroundf(deg / 2.0f)) * 2;
  a = ((a % 360) + 360) % 360;
  if (a == wind_drawn) return;
  wind_drawn = a;

  // Csak egy nyílfej a skálázott gyűrűn, azon az égtájon, ahonnan a szél fúj; a hegye a középpont felé mutat
  // (déli szélnél a "D" betűnél áll, felfelé, befelé mutatva).
  float th = a * 3.14159265f / 180.0f;
  const float dx = sinf(th), dy = -cosf(th);
  const int D = WC_R * 2;
  const float U_TIP = WC_R - S(36.0f), U_BASE = WC_R - S(3.0f), HALF_W = S(11.0f);   // a hegy és az alap távolsága a középponttól, az alap fél szélessége

  lv_canvas_fill_bg(wind_cv, lv_color_black(), LV_OPA_TRANSP);
  for (int py = 0; py < D; py++) {
    for (int px = 0; px < D; px++) {
      int nb = 0, no = 0;
      for (int sy = 0; sy < 2; sy++) {
        for (int sx = 0; sx < 2; sx++) {
          float fx = px + (sx + 0.5f) / 2.0f - WC_R, fy = py + (sy + 0.5f) / 2.0f - WC_R;
          float u = fx * dx + fy * dy;
          float v = fabsf(-fx * dy + fy * dx);
          float w = HALF_W * (u - U_TIP) / (U_BASE - U_TIP);               // a háromszög szélessége az adott távolságon
          if (u >= U_TIP && u <= U_BASE && v <= w) nb++;                                    // a nyílfej
          else if (u >= U_TIP - 2.0f && u <= U_BASE + 2.0f && v <= w + 2.2f) no++;          // sötét körvonal, hogy a skála ne zavarja
        }
      }
      int n = nb + no;
      if (!n) continue;
      int r = (0x4F * nb + 0x10 * no) / n, g = (0xC3 * nb + 0x10 * no) / n, bl = (0xF7 * nb + 0x10 * no) / n;
      lv_canvas_set_px(wind_cv, px, py, lv_color_make(r, g, bl), (lv_opa_t)(n * 255 / 4));
    }
  }
  lv_obj_invalidate(wind_cv);
}

// Napkelte / napnyugta ikon: sugaras félnap a horizont fölött, mellette fel / le mutató nyíl
static const lv_point_precise_t CHEV_UP[3]   = {{0, S(9)}, {S(7), S(2)}, {S(14), S(9)}};
static const lv_point_precise_t CHEV_DOWN[3] = {{0, S(2)}, {S(7), S(9)}, {S(14), S(2)}};

static void make_sun_icon(lv_obj_t *parent, int x, int y, bool up) {
  lv_obj_t *g = make_group(parent, x, y, S(84), S(38));
  make_sun(g, 0, 0, S(60), S(33), S(30), S(29), S(11), S(16), S(28), S(3.0f), 8, S(31.0f));
  make_shape(g, S(1), S(31), S(58), S(3), 1, 0xB8A850);                    // horizont
  lv_obj_t *ln = lv_line_create(g);
  lv_line_set_points(ln, up ? CHEV_UP : CHEV_DOWN, 3);
  lv_obj_set_style_line_width(ln, S(3), 0);
  lv_obj_set_style_line_color(ln, lv_color_hex(0xF2B632), 0);
  lv_obj_set_style_line_rounded(ln, true, 0);
  lv_obj_set_pos(ln, S(66), S(10));
}
