// Képernyőkép: GET /screenshot[?oldal=energia][&szoba=haloszoba] a kezelőoldal jelszavával (ha van); 24 bites BMP az aktív LVGL képernyőről.
// Az oldal= (név vagy sorszám) előbb átvált a kért oldalra, és az marad megnyitva. A kép egy saját PSRAM-pufferbe készül
// (az LVGL memóriájából nem foglal nagyot), a főszálban, ezért a kijelző 1-2 másodpercre megáll.
// A szoba= (a helyiség sorszáma 1..3, neve, ékezet nélkül is, vagy konzol-azonosítója) a főoldal "Bent" kártyáját állítja.
// (Az idojaras.ino része: az Arduino IDE és a PlatformIO a fájlokat ábécésorrendben, egy programmá fűzi össze.)

// az oldal neve (magyarul vagy angolul) vagy sorszáma -> PAGE_*; -1: ismeretlen
static int screenshot_page(String a) {
  a.trim();
  a.toLowerCase();
  static const struct { const char *name; int page; } NAMES[] = {
    {"fooldal", PAGE_DASH}, {"dash", PAGE_DASH}, {"main", PAGE_DASH},
    {"energia", PAGE_ENERGY}, {"energy", PAGE_ENERGY},
    {"elorejelzes", PAGE_FC}, {"forecast", PAGE_FC}, {"fc", PAGE_FC},
    {"kint", PAGE_HIST}, {"outdoor", PAGE_HIST}, {"hist", PAGE_HIST},
    {"bent", PAGE_HIST2}, {"indoor", PAGE_HIST2}, {"hist2", PAGE_HIST2},
    {"reszletes", PAGE_DETAILS}, {"details", PAGE_DETAILS},
    {"naplo", PAGE_LOG}, {"log", PAGE_LOG},
  };
  for (const auto &n : NAMES) if (a == n.name) return n.page;
  if (a.length() == 1 && isdigit((unsigned char)a[0]) && a[0] - '0' < PAGE_COUNT) return a[0] - '0';
  return -1;
}

// összehasonlításhoz: kisbetű, ékezet nélkül, csak betű és szám ("Hálószoba" -> "haloszoba")
static String screenshot_fold(const char *t) {
  String o;
  for (const uint8_t *p = (const uint8_t *)t; *p; p++) {
    char c = 0;
    if (*p == 0xC3 && p[1]) {
      switch (*++p | 0x20) {   // a nagybetű 0x20-szal kisebb (Á 0x81, á 0xA1)
        case 0xA1: c = 'a'; break;
        case 0xA9: c = 'e'; break;
        case 0xAD: c = 'i'; break;
        case 0xB3: case 0xB6: c = 'o'; break;
        case 0xBA: case 0xBC: c = 'u'; break;
      }
    } else if (*p == 0xC5 && p[1]) {
      ++p;
      if (*p == 0x90 || *p == 0x91) c = 'o';        // Ő ő
      else if (*p == 0xB0 || *p == 0xB1) c = 'u';   // Ű ű
    } else if (isalnum(*p)) {
      c = tolower(*p);
    }
    if (c) o += c;
  }
  return o;
}

// a helyiség sorszáma (1..3), neve (ékezet nélkül is) vagy konzol-azonosítója -> rooms[] index; -1: nincs ilyen
static int screenshot_room(const String &a) {
  const String f = screenshot_fold(a.c_str());
  if (f.length() == 1 && f[0] >= '1' && f[0] < '1' + ROOM_COUNT) {
    const int i = f[0] - '1';
    return rooms[i].match[0] ? i : -1;
  }
  if (!f.length()) return -1;
  for (int i = 0; i < ROOM_COUNT; i++)
    if (rooms[i].match[0] && (f == screenshot_fold(rooms[i].name) || f == screenshot_fold(rooms[i].match))) return i;
  return -1;
}

static void handle_screenshot() {
  if (!ota_auth_ok()) { request_auth(); return; }
  WebServer &s = my_server;
  if (s.hasArg("szoba")) {   // a főoldal "Bent" kártyájának helyisége (mint a kártyára koppintva)
    const int r = screenshot_room(s.arg("szoba"));
    if (r < 0) {
      String m = "Ismeretlen helyiség. Lehet a sorszáma (1.." + String(ROOM_COUNT) + ") vagy a neve:";
      for (int i = 0; i < ROOM_COUNT; i++) if (rooms[i].match[0]) { m += ' '; m += rooms[i].name; }
      s.send(400, "text/plain; charset=utf-8", m);
      return;
    }
    sel_room = r;
    LogOut.printf("Képernyőkép: Bent zóna -> %s\n", rooms[sel_room].name);
    update_dashboard();
  }
  if (s.hasArg("oldal")) {
    const int p = screenshot_page(s.arg("oldal"));
    if (p < 0) {
      char m[140];   // a számok felső határa a PAGE_COUNT-ból (új oldalnál se maradjon le)
      snprintf(m, sizeof(m), "Ismeretlen oldal. Lehet: fooldal, energia, elorejelzes, kint, bent, reszletes, naplo (vagy 0..%d).", PAGE_COUNT - 1);
      s.send(400, "text/plain; charset=utf-8", m);
      return;
    }
    if (p == PAGE_ENERGY && !en_page_enabled()) { s.send(409, "text/plain; charset=utf-8", "Az Energia ki van kapcsolva."); return; }
    show_page(p);
  }
  lv_obj_t *scr = lv_screen_active();
  lv_obj_update_layout(scr);
  const int w = lv_obj_get_width(scr), h = lv_obj_get_height(scr);
  const lv_color_format_t cf = LV_COLOR_FORMAT_RGB565;
  const uint32_t stride = lv_draw_buf_width_to_stride(w, cf), size = stride * h;
  void *buf = heap_caps_aligned_alloc(64, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  const int rowb = (w * 3 + 3) & ~3;
  uint8_t *row = (uint8_t *)heap_caps_malloc(rowb, MALLOC_CAP_8BIT);
  if (!buf || !row) {
    heap_caps_free(buf); heap_caps_free(row);
    s.send(503, "text/plain; charset=utf-8", "Nincs elég memória a képernyőképhez.");
    return;
  }
  lv_draw_buf_t db;
  const uint32_t t0 = millis();
  if (lv_draw_buf_init(&db, w, h, cf, LV_STRIDE_AUTO, buf, size) != LV_RESULT_OK || lv_snapshot_take_to_draw_buf(scr, cf, &db) != LV_RESULT_OK) {
    heap_caps_free(buf); heap_caps_free(row);
    s.send(500, "text/plain; charset=utf-8", "A képernyőkép nem sikerült.");
    return;
  }
  LogOut.printf("Képernyőkép: %dx%d, %u ms\n", w, h, (unsigned)(millis() - t0));

  // 24 bites, alulról felfelé tárolt BMP; a sorok 4 bájtra kerekítve
  const uint32_t total = 54 + (uint32_t)rowb * h;
  uint8_t hd[54] = {'B', 'M'};
  auto put32 = [&](int at, uint32_t v) { for (int i = 0; i < 4; i++) hd[at + i] = (uint8_t)(v >> (8 * i)); };
  put32(2, total); put32(10, 54); put32(14, 40); put32(18, w); put32(22, h);
  hd[26] = 1; hd[28] = 24; put32(34, (uint32_t)rowb * h); put32(38, 2835); put32(42, 2835);
  s.setContentLength(total);
  s.sendHeader("Content-Disposition", "inline; filename=screenshot.bmp");
  s.send(200, "image/bmp", "");
  WiFiClient cli = s.client();
  cli.write(hd, sizeof(hd));
  for (int y = h - 1; y >= 0 && cli.connected(); y--) {
    const uint16_t *src = (const uint16_t *)((const uint8_t *)db.data + (size_t)y * stride);
    uint8_t *d = row;
    for (int x = 0; x < w; x++) {
      const uint16_t p = src[x];
      const uint8_t r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
      *d++ = (b * 255 + 15) / 31; *d++ = (g * 255 + 31) / 63; *d++ = (r * 255 + 15) / 31;
    }
    while ((d - row) < rowb) *d++ = 0;
    cli.write(row, rowb);
    if ((y & 31) == 0) esp_task_wdt_reset();
  }
  heap_caps_free(buf);
  heap_caps_free(row);
}
