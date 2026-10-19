// Érintés (GT911): koppintás, ujjhúzás, hosszú nyomás
// (Az idojaras.ino része: az Arduino IDE és a PlatformIO a fájlokat ábécésorrendben, egy programmá fűzi össze.)

// ---------------------------------------------------------------------------
// Érintés (GT911, I2C 0x5D vagy 0x14) - egyszerű lekérdezés, az LVGL-től függetlenül
// ---------------------------------------------------------------------------
constexpr uint32_t TOUCH_POLL_MS     = 30;    // ilyen gyakran kérdezzük le az érintővezérlőt
constexpr int      TOUCH_TAP_MAX     = S(60);   // ennél kisebb elmozdulás (px) = koppintás
constexpr int      TOUCH_SWIPE_MIN   = S(80);   // ennél nagyobb vízszintes elmozdulás (px) = ujjhúzás
constexpr uint32_t TOUCH_COOLDOWN_MS = 250;   // két oldalváltás között legalább ennyi idő

enum { GESTURE_NONE, GESTURE_TAP, GESTURE_SWIPE_LEFT, GESTURE_SWIPE_RIGHT, GESTURE_LONG };

static uint8_t touch_addr = 0;   // 0 = nem találtunk érintővezérlőt

static bool gt911_read(uint16_t reg, uint8_t *buf, size_t len) {
  Wire.beginTransmission(touch_addr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((uint16_t)touch_addr, (size_t)len, true) != len) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

static void gt911_write8(uint16_t reg, uint8_t value) {
  Wire.beginTransmission(touch_addr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  Wire.write(value);
  Wire.endTransmission();
}

static void touch_init() {
#if KIJELZO_P4
  touch_addr = p4_touch_init(p4_log) ? 0x40 : 0;   // GSL3680 (a cím csak jelzés: van érintővezérlő)
  return;
#endif
  const uint8_t candidates[2] = {0x5D, 0x14};   // a cím attól függ, milyen állapotban volt az INT láb a resetkor
  touch_addr = 0;
  for (int i = 0; i < 2; i++) {
    Wire.beginTransmission(candidates[i]);
    if (Wire.endTransmission() == 0) { touch_addr = candidates[i]; break; }
  }

  if (!touch_addr) {
    LogOut.println("Érintővezérlő nem található (GT911, 0x5D / 0x14)");
    return;
  }

  uint8_t id[4] = {0, 0, 0, 0};
  gt911_read(0x8140, id, 4);   // termékazonosító, GT911 esetén "911"
  LogOut.printf("Érintővezérlő: 0x%02X, azonosító: %c%c%c\n", touch_addr, id[0], id[1], id[2]);
}

static int last_gx = 0, last_gy = 0;   // az utolsó kézmozdulat vége (a koppintás helye)

static int touch_classify(int sx, int sy, int lx, int ly, uint32_t held_ms) {
  last_gx = lx; last_gy = ly;
  int dx = lx - sx, dy = ly - sy;
  int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;

  if (adx < TOUCH_TAP_MAX && ady < TOUCH_TAP_MAX && held_ms < 2000) return GESTURE_TAP;
  if (adx < TOUCH_TAP_MAX && ady < TOUCH_TAP_MAX && held_ms >= 2000) return GESTURE_LONG;
  if (adx >= TOUCH_SWIPE_MIN && adx * 2 > ady * 3)                  return dx < 0 ? GESTURE_SWIPE_LEFT : GESTURE_SWIPE_RIGHT;
  return GESTURE_NONE;
}

// Egy kézmozdulatot az ujj felemelésekor értékel ki
static int touch_poll() {
  static uint32_t last_poll = 0, last_seen = 0, down_ms = 0;
  static bool touching = false;
  static int sx = 0, sy = 0, lx = 0, ly = 0;

  if (!touch_addr) return GESTURE_NONE;
  if (millis() - last_poll < TOUCH_POLL_MS) return GESTURE_NONE;
  last_poll = millis();

#if KIJELZO_P4
  {
    int x, y;
    if (p4_touch_read(&x, &y)) {
      if (!touching) { touching = true; sx = x; sy = y; down_ms = millis(); touch_down_event = true; }
      lx = x; ly = y; last_seen = millis();
    } else if (touching) {                     // az ujj felemelkedett (vagy a fekete keretre csúszott)
      touching = false;
      return touch_classify(sx, sy, lx, ly, millis() - down_ms);
    }
    return GESTURE_NONE;
  }
#endif

  uint8_t status = 0;
  if (!gt911_read(0x814E, &status, 1)) return GESTURE_NONE;

  if (status & 0x80) {                         // új adat áll rendelkezésre
    int points = status & 0x0F;
    if (points > 0) {
      uint8_t p[8];
      if (gt911_read(0x814F, p, 8)) {
        int x = p[1] | (p[2] << 8);
        int y = p[3] | (p[4] << 8);
        if (!touching) { touching = true; sx = x; sy = y; down_ms = millis(); touch_down_event = true; }
        lx = x; ly = y; last_seen = millis();
      }
    }
    gt911_write8(0x814E, 0);                   // nélküle a vezérlő nem ad újabb adatot

    if (points == 0 && touching) {             // az ujj felemelkedett
      touching = false;
      return touch_classify(sx, sy, lx, ly, millis() - down_ms);
    }
  } else if (touching && millis() - last_seen > 300) {   // biztonsági lezárás, ha a "felemelés" jelzés elveszne
    touching = false;
    return touch_classify(sx, sy, lx, ly, millis() - down_ms);
  }
  return GESTURE_NONE;
}

// Az első érintés (ha a kijelző éjszakai fényerőn van) csak felébreszt; utána a koppintás és az ujjhúzás lapoz.
// Koppintás és balra húzás: következő oldal, jobbra húzás: előző oldal.
static void touch_task() {
  static uint32_t last_action = 0;
  static bool ignore_gesture = false;

  int g = touch_poll();

  if (touch_down_event) {
    touch_down_event = false;
    ignore_gesture = (is_night() || cam_asleep()) && !is_awake();   // alvó kijelzőnél az első érintés nem lapoz
    wake_until_ms = millis() + (uint32_t)WAKE_SECONDS * 1000UL;
    page_since = millis();                          // az automatikus visszaugrás időzítője is újraindul
  }

  if (g == GESTURE_NONE) return;
  if (g == GESTURE_TAP && tap_dot) {   // visszajelzés a koppintás helyén
    lv_obj_set_pos(tap_dot, last_gx - S(28), last_gy - S(28));
    lv_obj_set_hidden(tap_dot, false);
    lv_obj_move_foreground(tap_dot);
    tap_dot_until = millis() + 180;
  }
  if (g == GESTURE_LONG) { LogOut.println("Érintés: hosszú nyomás - panel-reset"); panel_hw_reset(); return; }
  wake_until_ms = millis() + (uint32_t)WAKE_SECONDS * 1000UL;   // az ujj felemelésétől számít a 15 mp
  page_since = millis();

  if (ignore_gesture) { ignore_gesture = false; return; }
  if (millis() - last_action < TOUCH_COOLDOWN_MS) return;
  last_action = millis();

  // nyitott "Nincs WiFi" panel (a legfelső rétegen): csak a saját gombjai működnek
  if (wp_is_open()) {
    if (g == GESTURE_TAP) wp_tap(last_gx, last_gy);
    return;
  }
  // nyitott beállítás-képernyő: csak a saját gombjai működnek
  if (settings_is_open()) {
    if (g == GESTURE_TAP) settings_tap(last_gx, last_gy);
    return;
  }
  // a fogaskerékre (jobb felső sarok, minden oldalon) koppintva a Beállítások képernyő nyílik
  // (nem, ha a teljes képernyős grafikon vagy a fényerő panel van nyitva: ott a sarokban más van)
  if (g == GESTURE_TAP && fs_idx < 0 && !bl_panel_open() && last_gx >= LCD_H_RES - SX(90) && last_gy < SY(56)) {
    settings_open();
    return;
  }
  // nyitott fényerő panel: csak a panel gombjai működnek (mellé koppintva bezárul)
  if (bl_panel_open()) {
    if (g == GESTURE_TAP) bl_panel_tap(last_gx, last_gy);
    return;
  }
  // teljes képernyős grafikon: csak a jobb felső sarokban lévő X zárja be, a többi érintés nem csinál semmit
  if (fs_idx >= 0) {
    if (g == GESTURE_TAP && last_gx >= LCD_H_RES - FS_CLOSE_ZONE && last_gy <= FS_CLOSE_ZONE) fs_close();
    return;
  }
  // az előzményoldalon egy grafikonra koppintva az teljes képernyőn nyílik meg
  if (g == GESTURE_TAP && (current_page == PAGE_HIST || current_page == PAGE_HIST2) &&
      last_gx >= CH_TOG_X - SX(10) && last_gx < CH_TOG_X + CH_TOG_W + SX(10) && last_gy < CH_TOG_Y + CH_TOG_H + SY(10)) {
    int seg = (last_gx - CH_TOG_X) / CH_TOG_SEG;   // Nap | Hét | Hónap
    ch_set_mode(seg < 0 ? 0 : seg > 2 ? 2 : seg);
    hist_update_charts();
    return;
  }
  // beállító módban a felső sor "Nincs WiFi - beállítás" feliratára koppintva újra megnyílik a WiFi-panel
  if (g == GESTURE_TAP && wifiPortalNoConn() && last_gy < SY(56) && lbl_warn &&
      last_gx >= lv_obj_get_x(lbl_warn) && last_gx < lv_obj_get_x(lbl_warn) + lv_obj_get_width(lbl_warn)) {
    wp_open();
    return;
  }
  if (g == GESTURE_TAP && (current_page == PAGE_HIST || current_page == PAGE_HIST2)) {
    int hit = hist_card_at(last_gx, last_gy);
    if (hit >= 0) { fs_open(hit); return; }
  }

  // a főoldalon az órára koppintva a fényerő panel nyílik
  if (g == GESTURE_TAP && current_page == PAGE_DASH &&
      last_gx >= ZK_X && last_gx < ZK_X + ZK_W && last_gy >= ZB_Y && last_gy < ZB_Y + ZB_H) {
    bl_panel_show();
    return;
  }

  // a főoldalon a "Bent" zónára koppintva a következő helyiség jelenik meg
  if (g == GESTURE_TAP && current_page == PAGE_DASH &&
      last_gx >= ZT_X1 && last_gx < ZT_X1 + ZTB_W && last_gy >= ZT_Y && last_gy < ZT_Y + ZT_H) {
    for (int k = 0; k < ROOM_COUNT; k++) {   // a következő használt hely
      sel_room = (sel_room + 1) % ROOM_COUNT;
      if (rooms[sel_room].match[0]) break;
    }
    LogOut.printf("Érintés: Bent zóna -> %s\n", rooms[sel_room].name);
    update_dashboard();
    return;
  }

  int dir = 1;   // koppintás: előre
  if (g == GESTURE_SWIPE_LEFT)  dir = TOUCH_SWIPE_INVERT ? -1 : 1;
  if (g == GESTURE_SWIPE_RIGHT) dir = TOUCH_SWIPE_INVERT ? 1 : -1;

  LogOut.printf("Érintés: %s\n", g == GESTURE_TAP ? "koppintás" : (g == GESTURE_SWIPE_LEFT ? "balra húzás" : "jobbra húzás"));
  show_page((current_page + dir + PAGE_COUNT) % PAGE_COUNT);
}
