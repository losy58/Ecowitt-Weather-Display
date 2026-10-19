// "Nincs WiFi" panel: ha nincs kapcsolat (vagy a Beállításokból kérik), megmutatja, hogyan lehet a WiFi-t beállítani:
// a beállító hálózat neve és címe, két QR-kód (csatlakozás a hálózatra, a beállító oldal), gombok: Bemutató mód,
// Újrapróbálás, Bezárás. Kapcsolat nélkül induláskor magától nyílik (wifiTakePortalOpened); bezárva a felső sorban a
// "Nincs WiFi - beállítás" feliratra koppintva újra megnyílik. Az első megnyitáskor jön létre, bezáráskor törlődik
// (az LVGL memóriája véges).
// (Az idojaras.ino része: az Arduino IDE és a PlatformIO a fájlokat ábécésorrendben, egy programmá fűzi össze.)

constexpr int WP_X = SX(72), WP_Y = SY(30), WP_W = SX(880), WP_H = SY(540);   // a panel helye és mérete
constexpr int WP_QR = S(190), WP_QR_Y = SY(110), WP_QR2_X = SX(24) + S(190) + SX(24);   // QR-kódok (a panelen belül)
constexpr int WP_TX_X = SX(470), WP_TX_W = SX(386);   // a lépések szövege (jobb oldal)
constexpr int WP_BTN_Y = WP_H - SY(84), WP_BTN_W = SX(250), WP_BTN_X0 = SX(24), WP_BTN_DX = SX(291);   // gombok
constexpr uint32_t WP_ARM_MS = 5000;   // a Bemutató gomb második koppintására ennyi idő van

static lv_obj_t *wp_ov = NULL, *wp_title = NULL, *wp_state = NULL, *wp_demo_l = NULL, *wp_retry = NULL;
static uint32_t wp_close_at = 0, wp_demo_arm = 0, wp_info_ms = 0;

static bool wp_is_open() { return wp_ov != NULL; }

static void wp_close() {
  if (!wp_ov) return;
  lv_obj_delete(wp_ov);
  wp_ov = wp_title = wp_state = wp_demo_l = wp_retry = NULL;
  wp_close_at = wp_demo_arm = 0;
}

static lv_obj_t *wp_qr(lv_obj_t *parent, int x, const char *data) {
  lv_obj_t *q = lv_qrcode_create(parent);
  lv_qrcode_set_size(q, WP_QR);
  lv_qrcode_set_dark_color(q, lv_color_hex(0x000000));
  lv_qrcode_set_light_color(q, lv_color_hex(0xFFFFFF));
  lv_qrcode_set_quiet_zone(q, true);   // fehér keret: a telefon a sötét háttéren is felismeri
  lv_qrcode_update(q, data, strlen(data));
  lv_obj_set_pos(q, x, WP_QR_Y);
  return q;
}

// a cím és az állapot (másodpercenként)
static void wp_refresh() {
  if (!wp_ov) return;
  if (wp_close_at) return;   // a sikeres kapcsolódás üzenete marad a bezárásig
  char b[300], ss[40];
  const bool noconn = wifiPortalNoConn();
  set_label_if_changed(wp_title, noconn ? tr("Nincs WiFi-kapcsolat", "No WiFi connection") : tr("WiFi beállítása", "WiFi setup"));
  if (noconn) {
    if (wifiSavedSsid(ss, sizeof(ss)))
      snprintf(b, sizeof(b), tr("Mentett hálózat: %s - nem érhető el. 2 percenként újrapróbálom, amíg senki sincs a beállító hálózaton.",
                                "Saved network: %s - not available. I retry every 2 minutes while nobody is on the setup network."), ss);
    else
      snprintf(b, sizeof(b), "%s", tr("Nincs mentett WiFi-hálózat.", "No saved WiFi network."));
  } else if (WiFi.status() == WL_CONNECTED) {
    snprintf(b, sizeof(b), tr("Most: %s (IP: %s). A beállító hálózat 5 percig nyitva; ha nem adsz meg újat, ez marad.",
                              "Now: %s (IP: %s). The setup network stays open for 5 minutes; if you set no new one, this one stays."),
             WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
  } else {
    snprintf(b, sizeof(b), "%s", tr("Nincs WiFi-kapcsolat.", "No WiFi connection."));
  }
  set_label_if_changed(wp_state, b);
  lv_obj_set_hidden(wp_retry, !noconn);
}

static void wp_open() {
  if (wp_ov) { wp_refresh(); return; }
  if (settings_is_open()) { st_action = ST_A_NONE; settings_close(); }   // a panel a Beállítások helyett (és fölött) látszik
  const lv_font_t *hu = &font_hu_28, *sm = &font_hu_14;
  wp_ov = make_group(lv_layer_top(), 0, 0, LCD_H_RES, LCD_V_RES);
  lv_obj_set_style_bg_color(wp_ov, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(wp_ov, LV_OPA_60, 0);
  lv_obj_t *card = make_shape(wp_ov, WP_X, WP_Y, WP_W, WP_H, S(16), 0x1E1E1E);
  lv_obj_set_style_border_width(card, S(2), 0);
  lv_obj_set_style_border_color(card, lv_color_hex(COL_YELLOW), 0);
  wp_title = dlabel(card, "", hu, COL_YELLOW, SX(24), SY(18));
  wp_state = dlabel(card, "", sm, COL_GREY, SX(24), SY(62));
  lv_obj_set_width(wp_state, WP_W - SX(48));
  lv_label_set_long_mode(wp_state, LV_LABEL_LONG_WRAP);

  char b[420];
  snprintf(b, sizeof(b), "WIFI:T:nopass;S:%s;;", wifiApName());   // a telefon kamerája ebből csatlakozik
  wp_qr(card, SX(24), b);
  snprintf(b, sizeof(b), "http://%s/", wifiApAddr());
  wp_qr(card, WP_QR2_X, b);
  dlabel_c(card, tr("1. WiFi-hálózat", "1. WiFi network"), sm, COL_GREY, SX(24), WP_QR_Y + WP_QR + SY(6), WP_QR);
  dlabel_c(card, tr("2. Beállító oldal", "2. Setup page"), sm, COL_GREY, WP_QR2_X, WP_QR_Y + WP_QR + SY(6), WP_QR);
  snprintf(b, sizeof(b),
           tr("1. Csatlakozz telefonnal a(z) \"%s\" hálózathoz (nincs jelszava), vagy olvasd be a bal oldali QR-kódot.\n\n"
              "2. A telefon általában magától megnyitja a beállító oldalt. Ha nem, nyisd meg böngészőben: http://%s/ "
              "(jobb oldali QR-kód).\n\n3. Válaszd ki a hálózatot, add meg a jelszavát, és koppints a Csatlakozás gombra.",
              "1. Connect your phone to the \"%s\" network (no password), or scan the QR code on the left.\n\n"
              "2. The phone usually opens the setup page by itself. If not, open it in a browser: http://%s/ "
              "(QR code on the right).\n\n3. Choose your network, enter its password and tap Connect."),
           wifiApName(), wifiApAddr());
  lv_obj_t *steps = dlabel(card, b, sm, COL_TEXT, WP_TX_X, WP_QR_Y);
  lv_obj_set_width(steps, WP_TX_W);
  lv_label_set_long_mode(steps, LV_LABEL_LONG_WRAP);
  snprintf(b, sizeof(b), "%s   |   http://%s/", wifiApName(), wifiApAddr());
  dlabel_c(card, b, hu, 0xFFFFFF, SX(24), SY(360), WP_W - SX(48));

  bp_button(card, WP_BTN_X0, WP_BTN_Y, WP_BTN_W, tr("Bemutató mód", "Demo mode"), 0x4A3A6A, &wp_demo_l);
  wp_retry = bp_button(card, WP_BTN_X0 + WP_BTN_DX, WP_BTN_Y, WP_BTN_W, tr("Újrapróbálás", "Retry"), 0x1F4A6A, NULL);
  bp_button(card, WP_BTN_X0 + 2 * WP_BTN_DX, WP_BTN_Y, WP_BTN_W, tr("Bezárás", "Close"), 0x555555, NULL);
  wp_close_at = wp_demo_arm = 0;
  wp_info_ms = millis();
  wp_refresh();
  LogOut.println("WiFi-panel megnyitva");
}

// Koppintás a nyitott panelen (képernyő-koordináták)
static void wp_tap(int x, int y) {
  const int lx = x - WP_X, ly = y - WP_Y;
  if (in_rect(lx, ly, WP_BTN_X0, WP_BTN_Y, WP_BTN_W, BP_BTN_H)) {   // Bemutató mód: a második koppintásra
    if (!wp_demo_arm) {
      wp_demo_arm = millis();
      lv_label_set_text(wp_demo_l, tr("Biztos? Koppints újra", "Sure? Tap again"));
      return;
    }
    LogOut.println("Bemutató mód be (a WiFi-panelről): újraindítás");
    demo_save(true);
    hist_save();
    delay(300);
    diag_mark_restart(RC_MANUAL);
    ESP.restart();
  }
  if (wp_retry && !lv_obj_is_hidden(wp_retry) && in_rect(lx, ly, WP_BTN_X0 + WP_BTN_DX, WP_BTN_Y, WP_BTN_W, BP_BTN_H)) {
    wifiRetryNow();
    set_label_if_changed(wp_state, tr("Újrapróbálás a mentett hálózattal...", "Retrying the saved network..."));
    wp_info_ms = millis();   // az üzenet pár másodpercig látszik
    return;
  }
  if (in_rect(lx, ly, WP_BTN_X0 + 2 * WP_BTN_DX, WP_BTN_Y, WP_BTN_W, BP_BTN_H)) wp_close();
}

// A kapcsolat létrejött (indulás vagy beállítás után): pontos idő, mDNS, és a panelen a siker
static void wifi_connected() {
  LogOut.println("WiFi csatlakozva");
  configTzTime(cfg.tz, "pool.ntp.org", "time.google.com");
  MDNS.end();   // az új hálózaton is elérhető legyen a nevével
  if (MDNS.begin(MDNS_NAME)) MDNS.addService("http", "tcp", 80);
  update_status_bar();
  if (!wp_ov) return;
  char b[200];
  set_label_if_changed(wp_title, tr("WiFi: csatlakozva", "WiFi: connected"));
  snprintf(b, sizeof(b), tr("Hálózat: %s, IP-cím: %s (a konzolokban ezt kell megadni). Kezelőoldal: http://" MDNS_HOST ".local/",
                            "Network: %s, IP address: %s (enter this in the consoles). Web page: http://" MDNS_HOST ".local/"),
           WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
  set_label_if_changed(wp_state, b);
  lv_obj_set_style_text_color(wp_state, lv_color_hex(0x66BB6A), 0);
  wp_close_at = millis() + 15000;
}

static void wp_task() {
  if (!wp_ov) return;
  if (wp_close_at && (int32_t)(millis() - wp_close_at) >= 0) { wp_close(); return; }
  if (!wp_close_at && !wifiPortalActive()) { wp_close(); return; }   // a kérésre nyitott beállító hálózat bezárult
  if (wp_demo_arm && millis() - wp_demo_arm > WP_ARM_MS) {
    wp_demo_arm = 0;
    lv_label_set_text(wp_demo_l, tr("Bemutató mód", "Demo mode"));
  }
  if (millis() - wp_info_ms >= 3000) { wp_info_ms = millis() - 2000; wp_refresh(); }   // másodpercenként (az újrapróbálás üzenete 3 mp)
}
