// Diagnosztika: indulások oka, mentett összeomlás, watchdog
// (Az idojaras.ino része: az Arduino IDE és a PlatformIO a fájlokat ábécésorrendben, egy programmá fűzi össze.)

// ---------------------------------------------------------------------------
// Diagnosztika: az indulások oka (utolsó BOOT_HIST, NVS), a program saját újraindításainak oka (RTC-memória),
// a flashre mentett összeomlás összefoglalója (coredump) és a fő program watchdogja
// ---------------------------------------------------------------------------
constexpr int BOOT_HIST = 6;
constexpr uint32_t RST_MAGIC = 0x52535431;            // "RST1"
constexpr uint32_t LOOP_WDT_MS = 60000;               // ha a loop() ennyi ideig nem fut le, újraindulás (és coredump)
struct BootRec { uint32_t t; uint8_t reason, cause; uint16_t pad; };
RTC_NOINIT_ATTR static uint32_t rst_magic, rst_cause; // újraindítás előtt beírjuk, a következő induláskor kiolvassuk
static BootRec boots[BOOT_HIST] = {};
static bool boot_time_saved = false;
static String crash_html;                             // a mentett összeomlás leírása (üres: nincs)
static char crash_short[64] = "";                     // röviden (az értesítéshez)

// a program által kezdeményezett újraindítás oka (ESP.restart() előtt hívandó; a wifi_manager.cpp is használja)
void diag_mark_restart(int cause) { rst_magic = RST_MAGIC; rst_cause = (uint32_t)cause; }

static const char *reset_text(uint8_t r, uint8_t cause) {
  if (r == ESP_RST_SW) {
    switch (cause) {
      case RC_OTA:    return tr("újraindítás frissítés után", "restart after update");
      case RC_PORTAL: return tr("újraindítás: a WiFi-portál időtúllépése", "restart: WiFi portal timeout");
      case RC_PANEL:  return tr("újraindítás: a kijelző-panel nem indult", "restart: display panel failed to start");
      case RC_WIFISET: return tr("újraindítás a WiFi beállításához", "restart to set up WiFi");
      case RC_MANUAL: return tr("kézi újraindítás", "manual restart");
      default:        return tr("szoftveres újraindítás", "software restart");
    }
  }
  switch (r) {
    case ESP_RST_POWERON:    return tr("bekapcsolás / áramszünet", "power on / power failure");
    case ESP_RST_EXT:        return tr("külső reset", "external reset");
    case ESP_RST_PANIC:      return tr("ÖSSZEOMLÁS (programhiba)", "CRASH (program error)");
    case ESP_RST_INT_WDT:    return tr("WATCHDOG (megszakítás)", "WATCHDOG (interrupt)");
    case ESP_RST_TASK_WDT:   return tr("WATCHDOG: a program elakadt", "WATCHDOG: the program hung");
    case ESP_RST_WDT:        return "WATCHDOG";
    case ESP_RST_DEEPSLEEP:  return tr("ébredés mélyalvásból", "wake from deep sleep");
    case ESP_RST_BROWNOUT:   return tr("FESZÜLTSÉGESÉS (gyenge táp)", "BROWNOUT (weak power supply)");
    case ESP_RST_USB:        return tr("reset az USB-portról", "reset from the USB port");
    case ESP_RST_JTAG:       return "reset (JTAG)";
    case ESP_RST_PWR_GLITCH: return tr("tápfeszültség-zavar", "power glitch");
    case ESP_RST_CPU_LOCKUP: return tr("processzor-lefagyás", "CPU lockup");
    default:                 return tr("ismeretlen ok", "unknown reason");
  }
}

static void diag_init() {
  BootRec cur = {};
  cur.reason = (uint8_t)esp_reset_reason();
  cur.cause = (rst_magic == RST_MAGIC) ? (uint8_t)rst_cause : RC_NONE;
  rst_magic = 0;

  CamPause cam_pause_;   // P4: a kamera áll a mentés alatt
  Preferences p;
  p.begin("diag", false);
  p.getBytes("boots", boots, sizeof(boots));
  memmove(&boots[1], &boots[0], sizeof(BootRec) * (BOOT_HIST - 1));
  boots[0] = cur;                       // az időpontot a pontos idő megérkezése után írjuk be
  p.putBytes("boots", boots, sizeof(boots));
  p.end();
  LogOut.printf("Indulás oka: %s\n", reset_text(cur.reason, cur.cause));

  // a flashre mentett összeomlás (ha van): feladat, a hiba helye és a hívási lánc címei (a .elf-fel visszafejthető)
  if (esp_core_dump_image_check() == ESP_OK) {
    esp_core_dump_summary_t *sm = (esp_core_dump_summary_t *)heap_caps_malloc(sizeof(esp_core_dump_summary_t), MALLOC_CAP_SPIRAM);
    if (sm && esp_core_dump_get_summary(sm) == ESP_OK) {
      char b[96];
#if CONFIG_IDF_TARGET_ARCH_XTENSA
      snprintf(b, sizeof(b), tr("Feladat: %s, hiba helye: 0x%08lx, ok: %lu, cím: 0x%08lx<br>", "Task: %s, at: 0x%08lx, cause: %lu, address: 0x%08lx<br>"),
               sm->exc_task, (unsigned long)sm->exc_pc, (unsigned long)sm->ex_info.exc_cause, (unsigned long)sm->ex_info.exc_vaddr);
      crash_html = b;
      crash_html += tr("Hívási lánc:", "Backtrace:");
      for (uint32_t i = 0; i < sm->exc_bt_info.depth && i < 16; i++) {
        snprintf(b, sizeof(b), " 0x%08lx", (unsigned long)sm->exc_bt_info.bt[i]);
        crash_html += b;
      }
      if (sm->exc_bt_info.corrupted) crash_html += tr(" (sérült)", " (corrupted)");
#else
      // RISC-V (ESP32-P4): nincs kész hívási lánc, csak a hívó címe (ra) és a hiba oka (mcause, mtval)
      snprintf(b, sizeof(b), tr("Feladat: %s, hiba helye: 0x%08lx, ok: %lu, cím: 0x%08lx<br>", "Task: %s, at: 0x%08lx, cause: %lu, address: 0x%08lx<br>"),
               sm->exc_task, (unsigned long)sm->exc_pc, (unsigned long)sm->ex_info.mcause, (unsigned long)sm->ex_info.mtval);
      crash_html = b;
      snprintf(b, sizeof(b), tr("Hívó (ra): 0x%08lx", "Caller (ra): 0x%08lx"), (unsigned long)sm->ex_info.ra);
      crash_html += b;
#endif
      snprintf(crash_short, sizeof(crash_short), "%s, 0x%08lx", sm->exc_task, (unsigned long)sm->exc_pc);
      snprintf(b, sizeof(b), tr("<br>Program-azonosító (ELF SHA256): %.16s", "<br>Firmware ID (ELF SHA256): %.16s"), (const char *)sm->app_elf_sha256);
      crash_html += b;
      LogOut.printf("Mentett összeomlás: %s, 0x%08lx\n", sm->exc_task, (unsigned long)sm->exc_pc);
    } else {
      crash_html = tr("Van mentett összeomlás, de az összefoglaló nem olvasható.", "There is a saved crash, but its summary cannot be read.");
    }
    free(sm);
  }
}

// a pontos idő megérkezése után az indulás időpontja (egyszer)
static void diag_task() {
  if (boot_time_saved) return;
  time_t now = time(nullptr);
  if (now < 1600000000) return;
  boot_time_saved = true;
  boots[0].t = (uint32_t)(now - millis() / 1000);
  CamPause cam_pause_;   // P4: a kamera áll a mentés alatt
  Preferences p;
  p.begin("diag", false);
  p.putBytes("boots", boots, sizeof(boots));
  p.end();
}

// a fő program (loop) watchdogja: setup() végén kapcsoljuk be, a loop() minden körben "eteti"
static bool loop_wdt_on = false;   // előtte az esp_task_wdt_reset() hibát naplózna (a P4-en minden hívásnál)
static void loop_wdt_start() {
  loop_wdt_on = true;
  esp_task_wdt_config_t wc = {};
  wc.timeout_ms = LOOP_WDT_MS;
  wc.idle_core_mask = 1 << 0;   // a 0. mag üresjárati feladata továbbra is figyelve (ESP-IDF alapbeállítás)
  wc.trigger_panic = true;      // elakadáskor összeomlás-mentés és újraindulás
  esp_task_wdt_reconfigure(&wc);
  esp_task_wdt_add(NULL);
}

static String diag_html() {
  String h = tr("<h3>Diagnosztika</h3><p>Utolsó indulások (legutóbbi elöl):<br>", "<h3>Diagnostics</h3><p>Last starts (most recent first):<br>");
  for (int i = 0; i < BOOT_HIST; i++) {
    const BootRec &r = boots[i];
    if (i > 0 && !r.t && !r.reason) break;
    char b[120];
    if (r.t > 1600000000) {
      time_t t = r.t;
      struct tm lt;
      localtime_r(&t, &lt);
      snprintf(b, sizeof(b), "%04d.%02d.%02d %02d:%02d - %s<br>", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min,
               reset_text(r.reason, r.cause));
    } else {
      snprintf(b, sizeof(b), tr("(időpont nélkül) - %s<br>", "(no time) - %s<br>"), reset_text(r.reason, r.cause));
    }
    h += b;
  }
  h += "</p>";
  if (crash_html.length()) {
    h += tr("<p style='color:#ff8a80'><b>Mentett összeomlás</b><br>", "<p style='color:#ff8a80'><b>Saved crash</b><br>");
    h += "<span style='font-family:monospace;font-size:13px'>";
    h += crash_html;
    h += "</span></p><form method='POST' action='/clearcrash'><button type='submit' style='background:#555;color:#eee'>";
    h += tr("Összeomlás-adat törlése", "Clear crash data");
    h += "</button></form>";
  } else {
    h += tr("<p>Mentett összeomlás: nincs</p>", "<p>Saved crash: none</p>");
  }
  return h;
}

static void handle_clear_crash() {
  if (!ota_auth_ok()) { request_auth(); return; }
  {
    CamPause cam_pause_;   // P4: a kamera áll a flash-törlés alatt
    esp_core_dump_image_erase();
  }
  crash_html = "";
  LogOut.println("Összeomlás-adat törölve");
  cur_srv->sendHeader("Location", "/");
  cur_srv->send(303, "text/plain", "");
}
