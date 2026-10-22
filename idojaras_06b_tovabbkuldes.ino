// A konzoloktól kapott adatok továbbküldése egy másik kijelzőre (pl. a 7B-ről a P4-re): a konzolok általában csak egy
// címre küldenek. A beérkezett mezők urlencoded alakban egy kis sorba kerülnek, és egy háttérfeladat küldi el őket
// HTTP POST-tal a cél /ecowitt címére; a főciklus nem vár rá. Ha a cél nem érhető el, a sor betelik, és az újak elmaradnak.
// A továbbküldött kérés címében ?fwd=N van (N: hányadik továbbküldés): a fogadó is továbbküldheti (pl. 7B -> P4 -> egy
// harmadik kijelző), de legfeljebb FW_MAX_HOPS lépésig, így két egymásra mutató kijelző sem küldi körbe végtelenül.
// (Az idojaras.ino része: az Arduino IDE és a PlatformIO a fájlokat ábécésorrendben, egy programmá fűzi össze.)

constexpr int FW_QUEUE = 4;
constexpr int FW_MAX_HOPS = 3;        // egy konzol-küldés legfeljebb ennyi továbbküldésen megy át
constexpr int FW_CONNECT_MS = 2000;   // kapcsolódás legfeljebb ennyi ideig
constexpr int FW_TIMEOUT_MS = 3000;   // válaszra legfeljebb ennyi ideig várunk
static QueueHandle_t fw_q = NULL;     // FwMsg mutatók (a puffer PSRAM-ban; a feladat szabadítja fel)
static TaskHandle_t fw_task_h = NULL;
static portMUX_TYPE fw_mux = portMUX_INITIALIZER_UNLOCKED;
static char fw_status[128] = "";   // az utolsó továbbküldés eredménye (a kezelőoldalon; üres: még nem küldött)
static uint32_t fw_ok = 0, fw_err = 0, fw_drop = 0;

// A cél ellenőrzése: gépnév vagy IP, esetleg :port (http:// előtag és záró / nélkül)
static bool fw_host_valid(const char *h) {
  if (!h[0] || strlen(h) > 64) return false;
  for (; *h; h++) if (!isalnum((unsigned char)*h) && *h != '.' && *h != '-' && *h != ':') return false;
  return true;
}

// urlencoded írás a pufferbe; false, ha nem fér el
static bool fw_append_enc(FwMsg &m, const String &s) {
  static const char HEX_DIGITS[] = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) {
    const uint8_t c = (uint8_t)s[i];
    if (m.len + 4 > sizeof(m.body)) return false;
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') m.body[m.len++] = (char)c;
    else if (c == ' ') m.body[m.len++] = '+';
    else { m.body[m.len++] = '%'; m.body[m.len++] = HEX_DIGITS[c >> 4]; m.body[m.len++] = HEX_DIGITS[c & 15]; }
  }
  m.body[m.len] = 0;
  return true;
}

static void fw_set_status(bool ok, const char *what) {
  struct tm t;
  char ts[8] = "";
  if (get_local_tm(t)) snprintf(ts, sizeof(ts), "%02d:%02d", t.tm_hour, t.tm_min);
  portENTER_CRITICAL(&fw_mux);
  if (ok) fw_ok++; else fw_err++;
  snprintf(fw_status, sizeof(fw_status), "%s %s", ts, what);
  portEXIT_CRITICAL(&fw_mux);
}

static void fw_send_one(const FwMsg &m) {
  WiFiClient cli;
  HTTPClient http;
  http.setConnectTimeout(FW_CONNECT_MS);
  http.setTimeout(FW_TIMEOUT_MS);
  http.setReuse(false);
  int code = -1;
  if (http.begin(cli, m.url)) {
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    code = http.POST((uint8_t *)m.body, m.len);
    http.end();
  }
  char b[96];
  if (code == 200) snprintf(b, sizeof(b), tr("elküldve (%u bájt)", "sent (%u bytes)"), (unsigned)m.len);
  else if (code > 0) snprintf(b, sizeof(b), tr("HIBA: HTTP %d", "ERROR: HTTP %d"), code);
  else snprintf(b, sizeof(b), tr("HIBA: %s", "ERROR: %s"), HTTPClient::errorToString(code).c_str());
  fw_set_status(code == 200, b);
}

// a háttérfeladat: a sorban álló küldéseket egyenként elküldi
static void fw_worker(void *) {
  for (;;) {
    FwMsg *m = NULL;
    if (xQueueReceive(fw_q, &m, portMAX_DELAY) != pdTRUE || !m) continue;
    if (WiFi.status() == WL_CONNECTED) fw_send_one(*m);
    else fw_set_status(false, tr("HIBA: nincs WiFi", "ERROR: no WiFi"));
    heap_caps_free(m);
  }
}

static void fw_drop_one(const char *why) {
  portENTER_CRITICAL(&fw_mux);
  fw_drop++;
  portEXIT_CRITICAL(&fw_mux);
  static uint32_t last_log = 0;   // a napló ne teljen meg, ha a cél sokáig nem érhető el
  if (!last_log || millis() - last_log > 600000UL) { last_log = millis(); LogOut.printf("Továbbküldés: %s\n", why); }
}

// A handle_ecowitt_post()-ból: a most fogadott küldés sorba tétele (nem blokkol)
static void fw_push() {
  if (!cfg.fw_on || !cfg.fw_host[0]) return;   // kikapcsolva vagy nincs cél
  const int hops = my_server.hasArg("fwd") ? (int)my_server.arg("fwd").toInt() : 0;   // a régi programok fwd=1-et küldenek
  if (hops < 0 || hops >= FW_MAX_HOPS) return;
  if (!fw_q) {
    fw_q = xQueueCreate(FW_QUEUE, sizeof(FwMsg *));
    if (!fw_q) return;
  }
  if (!fw_task_h &&
      xTaskCreatePinnedToCoreWithCaps(fw_worker, "tovabbkuld", 6144, NULL, 1, &fw_task_h, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    fw_task_h = NULL;
    return;
  }
  if (uxQueueSpacesAvailable(fw_q) == 0) { fw_drop_one("a sor tele (a cél nem válaszol?), a küldés elmaradt"); return; }
  FwMsg *m = (FwMsg *)heap_caps_malloc(sizeof(FwMsg), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!m) { fw_drop_one("nincs memória"); return; }
  snprintf(m->url, sizeof(m->url), "http://%s/ecowitt?fwd=%d", cfg.fw_host, hops + 1);
  m->len = 0;
  m->body[0] = 0;
  bool fits = true;
  for (int i = 0; i < my_server.args() && fits; i++) {
    const String name = my_server.argName(i);
    if (name == "plain" || name == "fwd") continue;   // a WebServer a nem urlencoded törzset és a cím paramétereit is argumentumként adja
    if (m->len) m->body[m->len++] = '&';
    fits = fw_append_enc(*m, name) && m->len + 1 < sizeof(m->body);
    if (fits) { m->body[m->len++] = '='; fits = fw_append_enc(*m, my_server.arg(i)); }
  }
  if (!fits) { heap_caps_free(m); fw_drop_one("a küldés túl hosszú"); return; }
  if (xQueueSend(fw_q, &m, 0) != pdTRUE) { heap_caps_free(m); fw_drop_one("a sor tele, a küldés elmaradt"); }
}

static const char *fw_status_text() {
  static char b[192];
  portENTER_CRITICAL(&fw_mux);
  snprintf(b, sizeof(b), tr("%s (sikeres: %u, hibás: %u, elmaradt: %u)", "%s (successful: %u, failed: %u, dropped: %u)"),
           fw_status[0] ? fw_status : tr("még nem küldött", "nothing sent yet"), (unsigned)fw_ok, (unsigned)fw_err, (unsigned)fw_drop);
  portEXIT_CRITICAL(&fw_mux);
  return b;
}
