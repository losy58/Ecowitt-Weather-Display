// A főoldal felépítése
// (Az idojaras.ino része: az Arduino IDE és a PlatformIO a fájlokat ábécésorrendben, egy programmá fűzi össze.)

// ---------------------------------------------------------------------------
// A főoldal felépítése
// ---------------------------------------------------------------------------
static void create_dashboard() {
  const lv_font_t *hu = &font_hu_28;

  // ---- felül balra: KINT (a hőmérséklet jobb széle: OT_RX)  (narancs): nyíl, hőmérséklet, mellette min/max/hőérzet/harmatpont/UV, alatta a páratartalom ----
  lv_obj_t *bl = make_zone(page_dash, ZT_X0, ZT_Y, ZTK_W, ZT_H, COL_BG_BL);
  dlabel(bl, tr("Kint", "Outdoor"), hu, COL_ORANGE, SX(16), SY(8));
  d_kalert = dlabel(bl, "", hu, COL_WARN, SX(90), SY(8));   // pl. "Eső várható 13:00-tól   Fagy várható 3:00"
  lv_obj_set_size(d_kalert, ZTK_W - SX(90 + 12), SY(34));   // egy sor; ami nem fér ki, "..."
  lv_label_set_long_mode(d_kalert, LV_LABEL_LONG_DOT);
  arrow_cv[ARR_OTEMP] = arrow_create(bl, SX(8), ZT_H / 2 - ARW / 2);
  d_otemp_g = dlabel_r(bl, "88.8", &font_seg_96, GHOST_BL, OT_RX, SY(38), SX(320));
  d_otemp = dlabel_r(bl, "--.-", &font_seg_96, COL_ORANGE, OT_RX, SY(38), SX(320));
  d_ominus = make_shape(bl, SX(35), SY(38) + S(43), S(24), S(10), S(2), COL_ORANGE);   // a mínuszjel rajzolva, hogy kevesebb helyet foglaljon
  lv_obj_set_hidden(d_ominus, true);
  dlabel(bl, "°C", hu, COL_ORANGE, OT_RX + S(5), SY(38) + S(4));
  d_ohum = seg_right(bl, "888", "--", &font_seg_72, COL_ORANGE, GHOST_BL, OT_RX, SY(156), SX(240));
  dlabel(bl, "%", hu, COL_ORANGE, OT_RX + S(5), SY(156) + S(40));
  {
    const char *nm[5] = {"Min", "Max", tr("Hőérzet", "Feels like"), tr("Harmat", "Dew point"), "UV"};
    lv_obj_t **vv[5] = {&d_tmin, &d_tmax, &d_feels, &d_dew, &d_uv};
    for (int i = 0; i < 5; i++) {
      dlabel(bl, nm[i], hu, COL_ORANGE_SOFT, ZTK_W - SX(180), SY(46 + i * 36));
      *vv[i] = dlabel_r(bl, "--", hu, COL_TEXT, ZTK_W - SX(8), SY(46 + i * 36), SX(140));
    }
  }

  // ---- felül jobbra: BENT (kék): hőmérséklet, mellette a páratartalom, alatta CO2 és napi max, komfortskála ----
  lv_obj_t *br = make_zone(page_dash, ZT_X1, ZT_Y, ZTB_W, ZT_H, COL_BG_BR);
  d_room = dlabel(br, tr("Bent: Nappali", "Indoor"), hu, COL_BLUE, SX(16), SY(8));   // koppintásra a következő helyiség
  d_itemp = seg_right(br, "88.8", "--.-", &font_seg_96, COL_BLUE, GHOST_BR, SX(252), SY(40), SX(260));
  dlabel(br, "°C", hu, COL_BLUE, SX(252) + S(6), SY(40) + S(6));
  d_ihum = seg_right(br, "88", "--", &font_seg_72, COL_BLUE, GHOST_BR, SX(404), SY(63), SX(200));
  dlabel(br, "%", hu, COL_BLUE, SX(404) + S(5), SY(63) + S(40));
  d_co2    = dlabel(br, "CO2: -- ppm", hu, COL_BLUE_SOFT, SX(12), SY(150));
  d_co2max = dlabel_r(br, "Max: -- ppm", hu, COL_BLUE_SOFT, ZTB_W - SX(12), SY(150), SX(210));
  const char *cz[3] = {tr("Száraz", "Dry"), tr("Komfort", "Comfort"), tr("Nedves", "Humid")};
  for (int i = 0; i < 3; i++) {
    d_comfort[i] = make_shape(br, SX(14 + i * 138), SY(196), SX(130), S(10), S(5), COL_BLUE_DIM);
    dlabel_c(br, cz[i], hu, COL_BLUE_SOFT, SX(14 + i * 138), SY(196) + S(10), SX(130));
  }

  // ---- lent balra: előrejelzés (ikon, nyomás + trend, szöveg), eső, UV ----
  lv_obj_t *tl = make_zone(page_dash, ZF_X, ZB_Y, ZF_W, ZB_H, COL_BG_TL);
  dlabel(tl, tr("Előrejelzés", "Forecast"), hu, COL_GREY, SX(16), SY(8));

  for (int i = 0; i < ICON_COUNT; i++) {
    d_icons[i] = make_group(tl, SX(16), SY(34), S(230), S(112));   // az ikonok egyenletesen nagyítva (S)
    lv_obj_set_hidden(d_icons[i], true);
  }
  // napos (sugaras nap)
  make_sun(d_icons[ICON_SUN], S(39), 0, S(112), S(112), S(56), S(56), S(25), S(34), S(53), S(4.5f), 12, -1);
  // változóan felhős
  make_sun(d_icons[ICON_PARTLY], S(4), S(4), S(88), S(88), S(44), S(44), S(18), S(25), S(41), S(3.5f), 12, -1);
  make_cloud(d_icons[ICON_PARTLY], S(84), S(40), S(130), 0xA9C3D6);
  // borult
  make_cloud(d_icons[ICON_CLOUDY], S(10), S(10), S(130), 0xA9C3D6);
  make_cloud(d_icons[ICON_CLOUDY], S(110), S(46), S(100), 0x8FB0C8);
  // esik
  make_cloud(d_icons[ICON_RAIN], S(50), S(4), S(130), 0x7F98AD);
  make_shape(d_icons[ICON_RAIN], S(74), S(84), S(6), S(20), S(3), 0x4FC3F7);
  make_shape(d_icons[ICON_RAIN], S(108), S(90), S(6), S(20), S(3), 0x4FC3F7);
  make_shape(d_icons[ICON_RAIN], S(142), S(84), S(6), S(20), S(3), 0x4FC3F7);
  // éjszaka: holdsarló és csillagok
  make_shape(d_icons[ICON_NIGHT], S(70), S(16), S(72), S(72), LV_RADIUS_CIRCLE, 0xE8E2B0);
  make_shape(d_icons[ICON_NIGHT], S(96), S(8), S(62), S(62), LV_RADIUS_CIRCLE, COL_BG_TL);
  make_shape(d_icons[ICON_NIGHT], S(24), S(24), S(5), S(5), LV_RADIUS_CIRCLE, 0xFFFFFF);
  make_shape(d_icons[ICON_NIGHT], S(196), S(18), S(4), S(4), LV_RADIUS_CIRCLE, 0xFFFFFF);
  make_shape(d_icons[ICON_NIGHT], S(180), S(80), S(5), S(5), LV_RADIUS_CIRCLE, 0xFFFFFF);
  make_shape(d_icons[ICON_NIGHT], S(36), S(78), S(4), S(4), LV_RADIUS_CIRCLE, 0xFFFFFF);
  make_shape(d_icons[ICON_NIGHT], S(150), S(100), S(4), S(4), LV_RADIUS_CIRCLE, 0xFFFFFF);

  arrow_cv[ARR_PRESS] = arrow_create(tl, SX(262), SY(208));
  d_fc1  = dlabel(tl, "", hu, COL_TEXT, SX(16), SY(146));
  d_rain = dlabel(tl, tr("Eső ma: -- mm", "Rain today: -- mm"), hu, COL_TEXT, SX(16), SY(177));
  d_press = seg_right(tl, "8888", "----", &font_seg_72, 0xE8E8E8, GHOST_TL, SX(251), SY(206), SX(260));
  dlabel(tl, "hPa", hu, COL_GREY, SX(251) + S(5), SY(206) + S(42));

  // ---- lent középen: szél-iránytű ----
  lv_obj_t *wz = make_zone(page_dash, ZC_X, ZB_Y, ZC_W, ZB_H, COL_BG_TL);
  const int cx = ZC_W / 2, cy = SY(36) + WC_R;   // az iránytű a zóna tetejéhez simul: fölötte csak az "É" betű

  lv_obj_t *ring = make_shape(wz, cx - WC_R, cy - WC_R, WC_R * 2, WC_R * 2, LV_RADIUS_CIRCLE, COL_BG_TL);
  lv_obj_set_style_border_width(ring, S(2), 0);
  lv_obj_set_style_border_color(ring, lv_color_hex(0x4A4A4A), 0);
  wind_scale_create(wz, cx, cy);

  // a mutató vászna (PSRAM-ban), a tárcsa és a szám fölötte
  {
    size_t bytes = (size_t)WC_R * 2 * WC_R * 2 * 4 + 256;
    void *buf = heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf) {
      wind_cv = lv_canvas_create(wz);
      lv_canvas_set_buffer(wind_cv, buf, WC_R * 2, WC_R * 2, LV_COLOR_FORMAT_ARGB8888);
      lv_canvas_fill_bg(wind_cv, lv_color_black(), LV_OPA_TRANSP);
      lv_obj_set_pos(wind_cv, cx - WC_R, cy - WC_R);
    }
  }
  make_shape(wz, cx - WC_HUB, cy - WC_HUB, WC_HUB * 2, WC_HUB * 2, LV_RADIUS_CIRCLE, COL_BG_TL);
  d_wnum = dlabel_c(wz, "--", &font_seg_40, 0xE8E8E8, cx - WC_HUB, cy - S(34), WC_HUB * 2);
  dlabel_c(wz, "km/h", hu, COL_GREY, cx - WC_HUB, cy + S(8), WC_HUB * 2);

  dlabel_c(wz, tr("É", "N"),  hu, 0xEF5350, cx - S(30), cy - WC_R - S(34), S(60));
  dlabel_c(wz, tr("D", "S"),  hu, COL_GREY, cx - S(30), cy + WC_R + S(2), S(60));
  dlabel(wz,   tr("K", "E"),  hu, COL_GREY, cx + WC_R + S(8), cy - S(17));
  dlabel_r(wz, tr("NY", "W"), hu, COL_GREY, cx - WC_R - S(8), cy - S(17), S(60));

  // az iránytű alatt: balra az irány betűjele, jobbra a lökés (középen a "D" betű áll)
  d_wdir  = dlabel(wz, "", hu, 0x4FC3F7, SX(8), cy + WC_R + S(4));
  d_wgust = dlabel_r(wz, tr("Lökés: --", "Gust: --"), hu, COL_TEXT, ZC_W - SX(4), cy + WC_R + S(4), SX(130));

  // ---- lent jobbra: napkelte / napnyugta, óra, dátum, hold ----
  lv_obj_t *tr = make_zone(page_dash, ZK_X, ZB_Y, ZK_W, ZB_H, COL_BG_TR);
  // fent középen a hold, alatta az óra és a dátum, legalul középen a napkelte / napnyugta
  moon_init(tr, (ZK_W - MOON_D) / 2, SY(8));
  d_clock = seg_left(tr, "88:88", "--:--", &font_seg_72, COL_YELLOW, GHOST_TR, SX(16), SY(76));
  d_secs  = seg_left(tr, ":88", ":--", &font_seg_40, COL_YELLOW, GHOST_TR, SX(16) + S(255), SY(76) + S(33));   // az óra mellett, az alja egy vonalban
  d_date  = seg_left(tr, "88.88.", "--.--.", &font_seg_40, COL_YELLOW, GHOST_TR, SX(16), SY(166));
  d_week  = dlabel(tr, "", hu, COL_YELLOW, SX(16) + S(136), SY(166) + S(6));
  d_year  = dlabel_r(tr, "", hu, 0xB8A850, ZK_W - SX(10), SY(166) + S(6), S(80));

  make_sun_icon(tr, SX(12), SY(234), true);
  d_sunrise = dlabel(tr, "--:--", hu, COL_YELLOW, SX(12) + S(88), SY(234) + S(4));
  make_sun_icon(tr, SX(180), SY(234), false);
  d_sunset = dlabel(tr, "--:--", hu, COL_YELLOW, SX(180) + S(88), SY(234) + S(4));

  moon_draw(0.5f);   // amíg nincs pontos idő: telihold
}
