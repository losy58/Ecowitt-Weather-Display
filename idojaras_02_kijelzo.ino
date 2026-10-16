// Napló (körpuffer), a 7B kijelző és az IO-bővítő indítása, képfolyam-figyelő, panel-újraépítés, LVGL-kijelző, a részletes oldal feliratai
// (Az idojaras.ino része: az Arduino IDE és a PlatformIO a fájlokat ábécésorrendben, egy programmá fűzi össze.)

// Waveshare 7B: IO-bovito (CH32V003, I2C 0x24). A bitkiosztas a korabbi,
// mukodo kodbol valo - a sematikaval erdemes egyszer ellenorizni.
// ---------------------------------------------------------------------------
// Napló: minden soros kiírás és az ESP-IDF rendszerüzenetek egy körpufferbe is kerülnek (a "Napló" oldalon látszanak)
// ---------------------------------------------------------------------------
constexpr int LOG_LINES = 30;     // ennyi sor látszik a Napló oldalon
constexpr int LOG_LEN   = 110;    // egy sor max. hossza (a többi levágódik)
static char log_lines[LOG_LINES][LOG_LEN + 1];
static int  log_head = 0, log_col = 0;           // log_head: az éppen írt sor
static volatile uint32_t log_version = 0;
static portMUX_TYPE log_mux = portMUX_INITIALIZER_UNLOCKED;

// Időbélyeg a sorok elejére: "HH:MM:SS " ha az óra már szinkronban van (NTP), addig a bekapcsolás óta eltelt idő ("+123s ")
static void log_stamp(char *out) {   // out: legalább 12 bájt
  time_t now = time(nullptr);
  if (now > 1700000000) {
    static time_t last = 0;
    static char cache[12];
    if (now != last) {
      struct tm t;
      localtime_r(&now, &t);
      snprintf(cache, sizeof(cache), "%02d:%02d:%02d ", t.tm_hour, t.tm_min, t.tm_sec);
      last = now;
    }
    strcpy(out, cache);
  } else {
    snprintf(out, 12, "+%lus ", (unsigned long)(millis() / 1000UL));
  }
}

class LogPrint : public Print {
public:
  size_t write(uint8_t c) override { write(&c, 1); return 1; }
  size_t write(const uint8_t *buf, size_t n) override {
    Serial.write(buf, n);
    char ts[12];
    log_stamp(ts);
    portENTER_CRITICAL(&log_mux);
    for (size_t i = 0; i < n; i++) {
      char ch = (char)buf[i];
      if (ch == '\r') continue;
      if (ch == '\n') {
        log_lines[log_head][log_col] = 0;
        log_head = (log_head + 1) % LOG_LINES;
        log_col = 0;
        log_lines[log_head][0] = 0;
      } else if (log_col < LOG_LEN) {
        if (log_col == 0) {                                  // a sor első karaktere előtt az időbélyeg
          for (const char *q = ts; *q && log_col < LOG_LEN; q++) log_lines[log_head][log_col++] = *q;
        }
        log_lines[log_head][log_col++] = ((unsigned char)ch < 32) ? ' ' : ch;
        log_lines[log_head][log_col] = 0;
      }
    }
    log_version = log_version + 1;
    portEXIT_CRITICAL(&log_mux);
    return n;
  }
};
static LogPrint LogOut;

static int log_vprintf_hook(const char *fmt, va_list ap) {
  char tmp[200];
  int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
  if (n > 0) LogOut.write((const uint8_t *)tmp, (size_t)(n < (int)sizeof(tmp) ? n : (int)sizeof(tmp) - 1));
  return n;
}

static void p4_log(const char *msg) { LogOut.println(msg); }   // a P4-es kijelzőkezelő üzenetei a Naplóba

static uint8_t g_io_state = 0xFF;   // a 0x24-es IO-bovito kimeneti allapota (a panel-reset ehhez nyul)

void init_io_expander()
{
#if KIJELZO_P4
    LogOut.println("Kijelző indítása (JC8012P4A1C)");   // nincs IO-bővítő: a kijelzőt a p4_display_init() indítja
    return;
#endif
    LogOut.println("Kijelző indítása (7B)");

    Wire.begin(8, 9);

    // I/O irányok
    Wire.beginTransmission(0x24);
    Wire.write(0x02);
    Wire.write(0xFF);
    Wire.endTransmission();

    delay(20);

    const uint8_t TOUCH_RESET = (1 << 1);
    const uint8_t BACKLIGHT  = (1 << 2);
    const uint8_t LCD_RESET  = (1 << 3);
    const uint8_t LCD_VDD    = (1 << 6);

    uint8_t state =
        0xFF & ~(TOUCH_RESET | BACKLIGHT | LCD_RESET);

    state |= LCD_VDD;

    Wire.beginTransmission(0x24);
    Wire.write(0x03);
    Wire.write(state);
    Wire.endTransmission();

    delay(20);

    state |= LCD_RESET;

    Wire.beginTransmission(0x24);
    Wire.write(0x03);
    Wire.write(state);
    Wire.endTransmission();

    delay(100);

    state |= TOUCH_RESET;

    Wire.beginTransmission(0x24);
    Wire.write(0x03);
    Wire.write(state);
    Wire.endTransmission();

    delay(200);

    state |= BACKLIGHT;

    Wire.beginTransmission(0x24);
    Wire.write(0x03);
    Wire.write(state);
    Wire.endTransmission();
    g_io_state = state;

    LogOut.println("Kijelző-vezérlő kész (7B)");
}

static esp_lcd_panel_handle_t g_panel = NULL;

// Képfolyam-figyelő (biztonsági háló): a kijelző minden képkockájának végén (vsync) megmérjük az eltelt időt. Névleges érték:
// (1384 x 661 képpont) / pclk = ~51 ms 18 MHz-nél. Ha ettől jelentősen eltér, a képfolyam megakadt vagy elcsúszott.
// A számlálók a frissítő oldalon látszanak; normál működésben mindkettő 0.
constexpr bool LCD_REBUILD_ON_GLITCH = true;   // zavar esetén automatikus panel-újraépítés (legfeljebb percenként egyszer)
static volatile int64_t  g_vs_last = 0;
static volatile uint32_t g_vs_glitch = 0, g_vs_max_us = 0, g_vs_min_us = 0xFFFFFFFF;
static uint32_t g_rebuild_count = 0;              // panel-újraépítések száma az indulás óta (a frissítő oldalon látszik)
static volatile uint32_t g_quiet_until_us = 0;     // (mikroszekundum) a panel újraépítése utáni beállás alatt nem számítunk zavarnak

static bool IRAM_ATTR on_vsync_cb(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *, void *) {
  int64_t now = esp_timer_get_time();
  if (g_vs_last) {
    uint32_t d = (uint32_t)(now - g_vs_last);
    uint32_t nominal = (uint32_t)(1384ULL * 661ULL * 1000000ULL / ((uint64_t)LCD_PCLK_MHZ * 1000000ULL));
    if (d > nominal * 13 / 10 || d < nominal * 7 / 10) {
      if ((int32_t)(g_quiet_until_us - (uint32_t)now) <= 0) {
        g_vs_glitch = g_vs_glitch + 1;
        if (d > g_vs_max_us) g_vs_max_us = d;
        if (d < g_vs_min_us) g_vs_min_us = d;
      }
    }
  }
  g_vs_last = now;
  return false;
}

static void register_vsync_monitor(esp_lcd_panel_handle_t h) {
  esp_lcd_rgb_panel_event_callbacks_t cbs = {};
  cbs.on_vsync = on_vsync_cb;
  esp_lcd_rgb_panel_register_event_callbacks(h, &cbs, NULL);
  g_vs_last = 0;
}

static volatile uint32_t g_redraw_at_ms = 0;   // az újraépítés után a teljes újrarajzolás ekkor indul (előtte a képfolyam megnyugszik)
static esp_lcd_rgb_panel_config_t g_panel_cfg;   // az init_display ide menti a panel beallitasait az ujralétrehozashoz

// Teljes panel-újraépítés: panel törlése -> kijelző-reset (IO-bővítő) -> új panel. (Az esp_lcd_rgb_panel_restart()
// az elcsúszott képet nem javítja.) A képfolyam-figyelő hívja zavar esetén, illetve kézzel: 2 mp hosszú érintés.
static void panel_hw_reset() {
#if KIJELZO_P4
  lv_obj_invalidate(lv_screen_active());   // a P4-es kijelzőnek saját képpuffere van: elég a teljes újrarajzolás
  return;
#endif
  if (!g_panel) return;
  lv_display_t *disp = lv_display_get_default();
  const uint8_t LCD_RESET = (1 << 3);

  esp_lcd_panel_del(g_panel);          // leallitja a kepfolyamot, felszabaditja a keppuffert
  g_panel = NULL;

  Wire.beginTransmission(0x24); Wire.write(0x03); Wire.write(g_io_state & ~LCD_RESET); Wire.endTransmission();
  delay(20);
  Wire.beginTransmission(0x24); Wire.write(0x03); Wire.write(g_io_state); Wire.endTransmission();
  delay(120);

  esp_lcd_panel_handle_t h = NULL;
  if (esp_lcd_new_rgb_panel(&g_panel_cfg, &h) != ESP_OK || esp_lcd_panel_reset(h) != ESP_OK || esp_lcd_panel_init(h) != ESP_OK) {
    LogOut.println("Panel-újraépítés sikertelen - újraindítás");
    delay(200);
    diag_mark_restart(RC_PANEL);
    ESP.restart();
  }
  g_panel = h;
  g_rebuild_count++;
  register_vsync_monitor(h);
  g_quiet_until_us = (uint32_t)esp_timer_get_time() + 1500000UL;
  if (disp) lv_display_set_user_data(disp, h);
  g_redraw_at_ms = millis() + 300;         // a teljes kepet ujra kell rajzolni az uj keppufferbe - de csak kicsit kesobb
  LogOut.println("Panel újraépítve");
}

// A képfolyam-zavarok naplózása (időbélyeggel) és szükség esetén automatikus újraépítés
static void glitch_task() {
  if (g_redraw_at_ms && (int32_t)(millis() - g_redraw_at_ms) >= 0) { g_redraw_at_ms = 0; lv_obj_invalidate(lv_screen_active()); }
  static uint32_t last_check = 0, last_seen = 0, last_rebuild = 0;
  if (millis() - last_check < 500) return;
  last_check = millis();
  uint32_t n = g_vs_glitch;
  if (n == last_seen) return;
  LogOut.printf("Képfolyam-zavar: %u db, képkockaidő %u..%u ms (normál ~%d)\n", (unsigned)(n - last_seen),
                (unsigned)(g_vs_min_us == 0xFFFFFFFF ? 0 : g_vs_min_us / 1000), (unsigned)(g_vs_max_us / 1000), (int)(1384 * 661 / LCD_PCLK_MHZ / 1000));
  last_seen = n; g_vs_max_us = 0; g_vs_min_us = 0xFFFFFFFF;
  if (LCD_REBUILD_ON_GLITCH && (last_rebuild == 0 || millis() - last_rebuild > 60000UL)) {
    last_rebuild = millis();
    panel_hw_reset();
    last_seen = g_vs_glitch;
  }
}

void init_display() {

#if KIJELZO_P4
  // JC8012P4A1C: a panelt és a háttérvilágítást a p4/kijelzo_p4.c indítja; a rajzolás a disp_flush()-ból oda megy
  if (!p4_display_init(LCD_H_RES, LCD_V_RES, p4_log)) LogOut.println("Kijelző HIBA - a webes kezelőoldal ettől még működik");
  esp_lcd_panel_handle_t panel_handle = NULL;
#else
  esp_lcd_rgb_panel_config_t panel_config = {};
  panel_config.clk_src = LCD_CLK_SRC_DEFAULT;
  panel_config.data_width = 16;
  panel_config.dma_burst_size = 64;   // (a régi psram_trans_align utódja)
  panel_config.disp_gpio_num = PIN_NUM_DISP_EN;
  panel_config.pclk_gpio_num = PIN_NUM_PCLK;
  panel_config.vsync_gpio_num = PIN_NUM_VSYNC;
  panel_config.hsync_gpio_num = PIN_NUM_HSYNC;
  panel_config.de_gpio_num = PIN_NUM_DE;

  int data_gpios[] = {
    PIN_NUM_DATA0, PIN_NUM_DATA1, PIN_NUM_DATA2, PIN_NUM_DATA3, PIN_NUM_DATA4,
    PIN_NUM_DATA5, PIN_NUM_DATA6, PIN_NUM_DATA7, PIN_NUM_DATA8, PIN_NUM_DATA9,
    PIN_NUM_DATA10, PIN_NUM_DATA11, PIN_NUM_DATA12, PIN_NUM_DATA13, PIN_NUM_DATA14, PIN_NUM_DATA15
  };
  for(int i = 0; i < 16; i++) {
    panel_config.data_gpio_nums[i] = data_gpios[i];
  }

  panel_config.timings.pclk_hz = LCD_PCLK_MHZ * 1000 * 1000;
  panel_config.timings.h_res = LCD_H_RES;
  panel_config.timings.v_res = LCD_V_RES;
  panel_config.timings.hsync_pulse_width = 162;
  panel_config.timings.hsync_back_porch = 150;
  panel_config.timings.hsync_front_porch = 48;
  panel_config.timings.vsync_pulse_width = 45;
  panel_config.timings.vsync_back_porch = 13;
  panel_config.timings.vsync_front_porch = 3;
  panel_config.timings.flags.pclk_active_neg = true;

  panel_config.flags.fb_in_psram = true;
  panel_config.bounce_buffer_size_px = LCD_H_RES * 20; 

  g_panel_cfg = panel_config;
  esp_lcd_panel_handle_t panel_handle = NULL;
  ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
#endif

  lv_init();
  lv_tick_set_cb(my_tick_cb);

  lv_display_t * disp = lv_display_create(LCD_H_RES, LCD_V_RES);

  // A panel RGB565 (2 bajt/pixel). LVGL 9-ben a sizeof(lv_color_t) 3 bajt lenne,
  // ezert a formatumot kifejezetten beallitjuk es 2 bajttal szamolunk.
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

  // Az LVGL rajzpufferei lehetőleg a belső RAM-ban legyenek: a PSRAM-ot a kijelző folyamatosan olvassa,
  // és ha a rajzolás is oda ír/olvas, a kép elcsúszhat. Ha nem fér el, kisebb pufferrel próbáljuk, végül PSRAM.
  size_t buf_size = 0;
  void *buf1 = NULL, *buf2 = NULL;
#if KIJELZO_P4
  // A P4-en a kijelzőnek saját képpuffere van (nem csúszik el a kép), és bőven van PSRAM: nagy rajzpuffer, hogy egy kép
  // kevés csíkban készüljön (8 sorral a grafikonos oldalak 75 csíkban, 400-500 ms alatt rajzolódtak ki)
  buf_size = (size_t)LCD_H_RES * P4_LVGL_LINES * 2;
  buf1 = heap_caps_aligned_alloc(64, buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  buf2 = heap_caps_aligned_alloc(64, buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  const bool p4_big = buf1 && buf2;
  if (p4_big) LogOut.printf("LVGL puffer: PSRAM, %d sor\n", P4_LVGL_LINES);
#else
  const bool p4_big = false;
#endif
  const int try_lines[] = {8, 4};
  for (int i = 0; i < 2 && !(buf1 && buf2); i++) {
    heap_caps_free(buf1); heap_caps_free(buf2);
    buf_size = (size_t)LCD_H_RES * try_lines[i] * 2;
    buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    buf2 = heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    // A WiFi-nek (és az LVGL-nek) sok belső RAM kell: ha túl kevés marad, kisebb puffer / PSRAM.
    if (buf1 && buf2 && heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < INTERNAL_RAM_RESERVE) {
      heap_caps_free(buf1); heap_caps_free(buf2); buf1 = buf2 = NULL;
    }
  }
  if (!buf1 || !buf2) {
    heap_caps_free(buf1); heap_caps_free(buf2);
    buf_size = (size_t)LCD_H_RES * 80 * 2;
    buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    buf2 = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    LogOut.println("LVGL puffer: PSRAM (nem fért el a belső RAM-ban)");
  } else if (!p4_big) {
    LogOut.printf("LVGL puffer: belső RAM, %u sor\n", (unsigned)(buf_size / (LCD_H_RES * 2)));
  }
  LogOut.printf("Szabad belső RAM: %u KB\n", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));

  lv_display_set_buffers(disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, disp_flush);
  lv_display_set_user_data(disp, panel_handle);
#if !KIJELZO_P4
  g_panel = panel_handle;
  register_vsync_monitor(panel_handle);
#endif
}

static void set_text(int idx, const char *fmt, ...) {
  if (!lbl[idx]) return;
  char buf[96];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  lv_label_set_text(lbl[idx], buf);
}

static const char *compass(float deg) {
  static const char *names[] = {tr("É", "N"), tr("ÉK", "NE"), tr("K", "E"), tr("DK", "SE"), tr("D", "S"), tr("DNY", "SW"), tr("NY", "W"), tr("ÉNY", "NW")};
  int i = (int)((deg + 22.5f) / 45.0f) % 8;
  if (i < 0) i += 8;
  return names[i];
}

void update_ui_labels() {
  const WeatherData &w = weather;

  // Kint
  set_text(L_TEMP, tr("Kint: %.1f °C", "Outdoor: %.1f °C"), w.temp_c);
  set_text(L_HUM,  tr("Páratartalom: %.0f %%", "Humidity: %.0f %%"), w.humidity);
  set_text(L_VPD,  "VPD: %.2f kPa", w.vpd_kpa);

  // Szél
  set_text(L_WIND,    tr("Szél: %.1f km/h", "Wind: %.1f km/h"), w.wind_kmh);
  set_text(L_DIR,     tr("Irány: %.0f° %s", "Direction: %.0f° %s"), w.wind_dir, compass(w.wind_dir));
  set_text(L_DIRAVG,  tr("Átlagirány: %.0f° %s", "Avg direction: %.0f° %s"), w.wind_dir_avg10m, compass(w.wind_dir_avg10m));
  set_text(L_GUST,    tr("Lökés: %.1f km/h", "Gust: %.1f km/h"), w.gust_kmh);
  set_text(L_MAXGUST, tr("Napi max: %.1f km/h", "Daily max: %.1f km/h"), w.max_gust_kmh);

  // Eső
  set_text(L_RAINING, tr("Esik: %s", "Raining: %s"), w.raining > 0.5f ? tr("igen", "yes") : tr("nem", "no"));
  set_text(L_RATE,    tr("Intenzitás: %.1f mm/h", "Rate: %.1f mm/h"), w.rain_rate_mm);
  set_text(L_EVENT,   tr("Esemény: %.1f mm", "Event: %.1f mm"), w.rain_event_mm);
  set_text(L_HOUR,    tr("Órás eső: %.1f mm", "Hourly rain: %.1f mm"), w.rain_hour_mm);
  set_text(L_DAY,     tr("Napi eső: %.1f mm", "Daily rain: %.1f mm"), w.rain_day_mm);
  set_text(L_24H,     tr("24 órás eső: %.1f mm", "24h rain: %.1f mm"), w.rain_24h_mm);
  set_text(L_WEEK,    tr("Heti eső: %.1f mm", "Weekly rain: %.1f mm"), w.rain_week_mm);
  set_text(L_MONTH,   tr("Havi eső: %.1f mm", "Monthly rain: %.1f mm"), w.rain_month_mm);
  set_text(L_YEAR,    tr("Éves eső: %.1f mm", "Yearly rain: %.1f mm"), w.rain_year_mm);

  // Nap, légnyomás
  set_text(L_SOLAR,   tr("Sugárzás: %.0f W/m²", "Solar: %.0f W/m²"), w.solar_wm2);
  set_text(L_UV,      "UV index: %.0f", w.uv_index);
  set_text(L_BARO,    tr("Nyomás: %.1f hPa", "Pressure: %.1f hPa"), w.baro_rel_hpa);
  set_text(L_BAROABS, "Abs.: %.1f hPa", w.baro_abs_hpa);

  // Bent: helyiségenként (elavult vagy hiányzó adatnál "--"; a nem használt hely sora üres)
  const int room_line[ROOM_COUNT] = {L_TIN, L_HIN, L_TIN3};
  for (int i = 0; i < ROOM_COUNT; i++) {
    if (!rooms[i].match[0])  set_text(room_line[i], "");
    else if (room_fresh(i))  set_text(room_line[i], "%s: %.1f° %.0f%%", rooms[i].name, rooms[i].t, rooms[i].h);
    else                     set_text(room_line[i], "%s: --", rooms[i].name);
  }
  // CO2: azok a helyek, amelyek konzolja küld CO2-t; legfeljebb kettőnél fejléccel, háromnál anélkül
  int co2r[ROOM_COUNT], nco2 = 0;
  for (int i = 0; i < ROOM_COUNT; i++) if (rooms[i].match[0] && rooms[i].has_co2) co2r[nco2++] = i;
  const int co2_lines[3] = {L_CO2, L_CO2_24H, L_CO2_3};
  int ln = 0;
  if (nco2 < 3) set_text(co2_lines[ln++], "CO2 (ppm)");
  if (nco2 == 0) set_text(co2_lines[ln++], tr("nincs CO2-mérés", "no CO2 sensor"));
  for (int k = 0; k < nco2 && ln < 3; k++) {
    const Room &r = rooms[co2r[k]];
    if (room_fresh(co2r[k])) set_text(co2_lines[ln++], nco2 < 3 ? "%s: %.0f" : "%s: %.0f ppm", r.name, r.co2);
    else                     set_text(co2_lines[ln++], "%s: --", r.name);
  }
  while (ln < 3) set_text(co2_lines[ln++], "");

  // Akkuk: a WS90 és (legfeljebb két) akkus konzol
  set_text(L_BATT_WS,  tr("WS90 akku: %.2f V", "WS90 battery: %.2f V"), w.wh90_batt_v);
  const int batt_lines[2] = {L_BATT_CAP, L_BATT_CON};
  int bn = 0;
  for (int i = 0; i < ROOM_COUNT && bn < 2; i++)
    if (rooms[i].match[0] && rooms[i].has_batt) set_text(batt_lines[bn++], tr("%s akku: %.2f V", "%s: %.2f V"), rooms[i].name, rooms[i].batt);
  while (bn < 2) set_text(batt_lines[bn++], "");

  // Rendszer
  uint32_t s = (uint32_t)w.runtime_s;
  set_text(L_SYS1, "%s | %.0fs | fw %.0f", w.freq, w.interval_s, w.ws90_ver);
  set_text(L_SYS2, tr("Futásidő: %u nap %u ó", "Uptime: %u d %u h"), (unsigned)(s / 86400UL), (unsigned)((s % 86400UL) / 3600UL));
  set_text(L_SYS3, tr("Kondenzátor: %.1f V", "Capacitor: %.1f V"), w.ws90_cap_v);

  update_dashboard();
}
