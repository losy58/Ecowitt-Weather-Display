// A felület létrehozása, setup() és loop()
// (Az idojaras.ino része: az Arduino IDE és a PlatformIO a fájlokat ábécésorrendben, egy programmá fűzi össze.)

void create_ui() {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x121212), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  lv_style_init(&style_card);
  lv_style_set_bg_color(&style_card, lv_color_hex(0x1E1E1E));
  lv_style_set_border_width(&style_card, S(2));
  lv_style_set_radius(&style_card, S(10));
  lv_style_set_text_color(&style_card, lv_color_hex(0xE0E0E0));
  lv_style_set_text_font(&style_card, &font_hu_28);
  lv_style_set_pad_all(&style_card, S(8));

  // három oldal: főoldal, grafikonok (24 óra) és részletes (kártyás) oldal
  page_details = make_group(screen, 0, 0, LCD_H_RES, LCD_V_RES);
  page_hist    = make_group(screen, 0, 0, LCD_H_RES, LCD_V_RES);
  page_hist2   = make_group(screen, 0, 0, LCD_H_RES, LCD_V_RES);
  page_fc      = make_group(screen, 0, 0, LCD_H_RES, LCD_V_RES);
  page_dash    = make_group(screen, 0, 0, LCD_H_RES, LCD_V_RES);
  page_log     = make_group(screen, 0, 0, LCD_H_RES, LCD_V_RES);
  lbl_log = lv_label_create(page_log);
  lv_label_set_long_mode(lbl_log, LV_LABEL_LONG_CLIP);
  lv_obj_set_size(lbl_log, LCD_H_RES - SX(40), LCD_V_RES - SY(60));
  lv_obj_set_pos(lbl_log, SX(20), SY(50));
  lv_obj_set_style_text_color(lbl_log, lv_color_hex(0xB8D8B8), 0);
  lv_obj_set_style_text_font(lbl_log, &font_hu_14, 0);
  lv_obj_set_style_text_line_space(lbl_log, S(2), 0);
  lv_label_set_text_static(lbl_log, "");

  // felső sáv (mindkét oldalon látszik): óra, állomás + IP, az utolsó adat kora
  lbl_clock = lv_label_create(screen);
  lv_label_set_text(lbl_clock, "--:--:--");
  lv_obj_set_style_text_color(lbl_clock, lv_color_hex(0xB0B0B0), 0);
  lv_obj_set_style_text_font(lbl_clock, &font_hu_28, 0);
  lv_obj_align(lbl_clock, LV_ALIGN_TOP_LEFT, SX(20), SY(8));

  lbl_info = lv_label_create(screen);
  lv_label_set_text(lbl_info, "");
  lv_obj_set_style_text_color(lbl_info, lv_color_hex(0xB0B0B0), 0);
  lv_obj_set_style_text_font(lbl_info, &font_hu_28, 0);
  lv_obj_set_style_text_color(lbl_info, lv_color_hex(COL_YELLOW), 0);   // az oldal neve
  lv_obj_align(lbl_info, LV_ALIGN_TOP_LEFT, SX(20), SY(8));

  lbl_warn = lv_label_create(screen);
  lv_label_set_text(lbl_warn, "");
  lv_obj_set_style_text_color(lbl_warn, lv_color_hex(COL_WARN), 0);
  lv_obj_set_style_text_font(lbl_warn, &font_hu_28, 0);
  lv_obj_set_align(lbl_warn, LV_ALIGN_TOP_LEFT);           // helyét és szélességét az update_status_bar() állítja
  lv_obj_set_size(lbl_warn, SX(400), SY(34));                     // egy sorban; ami nem fér ki, "..."
  lv_label_set_long_mode(lbl_warn, LV_LABEL_LONG_DOT);

  lbl_title = lv_label_create(screen);
  lv_label_set_text(lbl_title, tr("Ecowitt | Csatlakozás...", "Ecowitt | Connecting..."));
  lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xB0B0B0), 0);
  lv_obj_set_style_text_font(lbl_title, &font_hu_28, 0);
  lv_obj_align(lbl_title, LV_ALIGN_TOP_RIGHT, -SX(20), SY(8));

  // részletes oldal: a 3x4-es kártyák
  const uint32_t ORANGE = 0xFF8A3D, LBLUE = 0x4FC3F7, BLUE = 0x3D7BFF, YELLOW = 0xFFD54F;
  const uint32_t PURPLE = 0xB388FF, GREEN = 0x66BB6A, RED = 0xEF5350, GRAY = 0x9E9E9E;

  // 1. sor: kinti levegő, szél
  make_card(0, 0, ORANGE, { {L_TEMP, tr("Kint: -- °C", "Outdoor: -- °C")}, {L_HUM, tr("Páratartalom: -- %", "Humidity: -- %")}, {L_VPD, "VPD: -- kPa"} });
  make_card(1, 0, LBLUE,  { {L_WIND, tr("Szél: -- km/h", "Wind: -- km/h")}, {L_DIR, tr("Irány: --", "Direction: --")}, {L_DIRAVG, tr("Átlagirány: --", "Avg direction: --")} });
  make_card(2, 0, LBLUE,  { {L_GUST, tr("Lökés: -- km/h", "Gust: -- km/h")}, {L_MAXGUST, tr("Napi max: -- km/h", "Daily max: -- km/h")} });

  // 2. sor: eső
  make_card(0, 1, BLUE,   { {L_RAINING, tr("Esik: --", "Raining: --")}, {L_RATE, tr("Intenzitás: -- mm/h", "Rate: -- mm/h")}, {L_EVENT, tr("Esemény: -- mm", "Event: -- mm")} });
  make_card(1, 1, BLUE,   { {L_HOUR, tr("Órás eső: -- mm", "Hourly rain: -- mm")}, {L_DAY, tr("Napi eső: -- mm", "Daily rain: -- mm")}, {L_24H, tr("24 órás eső: -- mm", "24h rain: -- mm")} });
  make_card(2, 1, BLUE,   { {L_WEEK, tr("Heti eső: -- mm", "Weekly rain: -- mm")}, {L_MONTH, tr("Havi eső: -- mm", "Monthly rain: -- mm")}, {L_YEAR, tr("Éves eső: -- mm", "Yearly rain: -- mm")} });

  // 3. sor: nap, légnyomás, bent
  make_card(0, 2, YELLOW, { {L_SOLAR, tr("Sugárzás: -- W/m²", "Solar: -- W/m²")}, {L_UV, "UV index: --"} });
  make_card(1, 2, PURPLE, { {L_BARO, tr("Nyomás: -- hPa", "Pressure: -- hPa")}, {L_BAROABS, "Abs.: -- hPa"} });
  make_card(2, 2, GREEN,  { {L_TIN, "--"}, {L_HIN, "--"}, {L_TIN3, "--"} });

  // 4. sor: CO2, akkuk, rendszer
  make_card(0, 3, GREEN,  { {L_CO2, "CO2 (ppm)"}, {L_CO2_24H, ""}, {L_CO2_3, ""} });
  make_card(1, 3, RED,    { {L_BATT_WS, tr("WS90 akku: -- V", "WS90 battery: -- V")}, {L_BATT_CAP, ""}, {L_BATT_CON, ""} });
  make_card(2, 3, GRAY,   { {L_SYS1, "-- | --s | fw --"}, {L_SYS2, tr("Futásidő: --", "Uptime: --")}, {L_SYS3, tr("Kondenzátor: -- V", "Capacitor: -- V")} });

  create_history_page();
  create_forecast_page();
  create_dashboard();
  // a fogaskerék a felugró rétegek (teljes képernyős grafikon, fényerő panel, Beállítások) alatt legyen
  lbl_gear = lv_label_create(screen);
  lv_label_set_text(lbl_gear, LV_SYMBOL_SETTINGS);
  lv_obj_set_style_text_font(lbl_gear, &FONT_SYM, 0);
  lv_obj_set_style_text_color(lbl_gear, lv_color_hex(0xB0B0B0), 0);
  lv_obj_align(lbl_gear, LV_ALIGN_TOP_RIGHT, -SX(24), SY(8));
  create_fullscreen_chart();
  create_bl_panel();           // utoljára, hogy minden fölött legyen (a Beállítások képernyő az első megnyitáskor jön létre)
  tap_dot = make_shape(lv_layer_top(), 0, 0, S(56), S(56), LV_RADIUS_CIRCLE, 0xFFFFFF);
  lv_obj_set_style_bg_opa(tap_dot, LV_OPA_40, 0);
  lv_obj_set_hidden(tap_dot, true);
  create_ota_overlay();
  show_page(PAGE_DASH);
}

// A WiFi-kezelő ezeket hívja: a (rövid, blokkoló) hálózatváltás alatt a kijelző frissüljön, az üzenetek a Naplóba
// kerüljenek (a WiFi állapota a felső sorban látszik: wifiStatusLine, update_status_bar)
static void ui_pump() {
  lv_timer_handler();
  if (loop_wdt_on) esp_task_wdt_reset();
}

static void ui_status(const char *msg) {
  LogOut.println(msg);   // a WiFi-kapcsolódás lépései a Naplóban is látszanak
}

void app_setup() {

  Serial.begin(115200);
#if KIJELZO_P4
  // a P4-en a napló a natív USB-porton megy: újraindulás után a gépnek kell egy kis idő, hogy újra rákapcsolódjon,
  // különben az indulás eleje (kijelző, érintés) elveszne
  for (uint32_t t0 = millis(); !Serial && millis() - t0 < 2000; ) delay(10);
#endif
  esp_log_set_vprintf(log_vprintf_hook);
  if (!hist_alloc()) LogOut.println("Előzmény: nincs elég memória");
  lang_load();   // a felület nyelve (a kijelző felépítése és a diagnosztika szövegei előtt)
  diag_init();
  init_io_expander();

  delay(500);

  init_display();

  create_ui();
  lv_refr_now(NULL);
  LogOut.println("Kijelző OK");
  {
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    LogOut.printf("LVGL memória: %u KB összesen, %u KB szabad (%u%% foglalt)\n",
                  (unsigned)(mon.total_size / 1024), (unsigned)(mon.free_size / 1024), (unsigned)mon.used_pct);
  }

  touch_init();

  ffat_ok = FFat.begin(true);   // az "ffat" partíció; ha még nincs formázva, formáz
  LogOut.println(ffat_ok ? "FFat OK" : "FFat HIBA - az elozmenyek nem maradnak meg ujrainditas utan");

  cfg_load();
  en_cfg_load();   // energia (energia_01)
  cfg_apply_tz();
  p4_hw_setup();   // P4: az idő a lap órájából (WiFi nélkül is), az akku mérése
  wifiSetCallbacks(ui_pump, ui_status);
  wifiSetEnglish(g_lang);

  // A WiFi nem tart fel: a kijelző azonnal működik (érintés, Beállítások), a kapcsolódás a háttérben megy (wifiLoop).
  // Ha nincs mentett hálózat vagy nem sikerül csatlakozni, elindul a beállító hálózat, és a kijelzőn a "Nincs WiFi"
  // panel mutatja, hogyan lehet beállítani (idojaras_17f). Bemutató módban nincs beállító hálózat.
  if (g_demo) demo_start();
  else        wifiStart();

  my_server.on("/", HTTP_GET, handle_update_page);      // a gyökér is a frissítő oldal
  my_server.on("/update", HTTP_GET, handle_update_page);
  my_server.on("/update", HTTP_POST, handle_update_done, handle_update_upload);
  my_server.on("/settings", HTTP_POST, handle_settings_save);
  my_server.on("/raw", HTTP_GET, handle_raw_page);
  if (MDNS.begin(MDNS_NAME)) MDNS.addService("http", "tcp", 80);
  my_server.on("/screenshot", HTTP_GET, handle_screenshot);   // képernyőkép BMP-ben (idojaras_17g)
  my_server.on("/energia", HTTP_POST, handle_energia_post);   // a másik kijelzőtől érkező napelem-adat (energia_01)
  my_server.on("/ecowitt", HTTP_POST, handle_ecowitt_post);
  my_server.on("/ecowitt", HTTP_GET, handle_ecowitt_post);
  my_server.on("/clearcrash", HTTP_POST, handle_clear_crash);
  my_server.on("/coredump", HTTP_GET, handle_coredump);
  my_server.on("/ntfytest", HTTP_POST, handle_ntfy_test);
#if KIJELZO_P4
  my_server.on("/beeptest", HTTP_POST, handle_beep_test);
#endif
  my_server.on("/wifiscan", HTTP_GET, handle_wifi_scan);
  my_server.on("/wifiset", HTTP_POST, handle_wifi_set);
  my_server.on("/wifistatus", HTTP_GET, handle_wifi_status);
  my_server.on("/wifiportal", HTTP_POST, handle_wifi_portal);
  my_server.on("/wififorget", HTTP_POST, handle_wifi_forget);
  my_server.onNotFound(handle_not_found);   // a beállító hálózaton: a telefonok ellenőrző címei a beállító oldalra
  my_server.begin();
#if KIJELZO_P4
  if (!g_demo) p4_cam_init(p4_log);   // kamera (fény, jelenlét): a WiFi és a beállító portál (mentésekkel) után
#endif
  loop_wdt_start();   // innentől a loop() elakadása újraindítást (és összeomlás-mentést) okoz
}

void app_loop() {
  esp_task_wdt_reset();
  my_server.handleClient();
  lv_timer_handler();

  // Felso sor (ora, az utolso adat kora) frissitese; csak a valtozas rajzolodik ujra
  static uint32_t last_title_update = 0;
  if (millis() - last_title_update >= 200) {
    last_title_update = millis();
    update_status_bar();
  }

  // WiFi: indulási próbák, beállító hálózat; ha már volt kapcsolat, és menet közben elesik, 10 mp-enként újracsatlakozunk
  wifiLoop();
  if (wifiTakeConnected()) wifi_connected();
  if (wifiTakePortalOpened()) wp_open();
  static uint32_t last_wifi_check = 0;
  if (millis() - last_wifi_check > 10000) {
    last_wifi_check = millis();
    if (WiFi.status() != WL_CONNECTED && (g_demo || wifiManaged())) {
      WiFi.reconnect();
    }
  }

  glitch_task();
  diag_task();
  backlight_task();
  bl_panel_task();
  settings_task();
  wifiTune();
  nt_task();
  wifi_switch_task();
  wp_task();
  if (tap_dot_until && (int32_t)(millis() - tap_dot_until) >= 0) { tap_dot_until = 0; lv_obj_set_hidden(tap_dot, true); }
  touch_task();
  page_task();
  demo_task();
  p4_hw_task();
  hist_task();
  fc_loop();
  en_loop();

  delay(5);
}
