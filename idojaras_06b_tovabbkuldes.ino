// A konzoloktól kapott adatok továbbküldése másik kijelző(k)re: egy konzol csak egy címre küld, ezért ha minden konzol
// másik kijelzőre küld (csillag-elrendezés), mindegyik kijelzőnek a másik kettőnek kell továbbküldenie a saját
// konzoljától KÖZVETLENÜL kapott adatot (cfg.fw_host: vesszővel elválasztott lista). A már továbbküldött (?fwd=1-es)
// adatot a fogadó NEM küldi tovább megint - különben a 3 kijelző körbe-körbe verné egymásnak a saját adatát. A
// beérkezett mezők urlencoded alakban egy kis sorba kerülnek, és egy háttérfeladat küldi el őket HTTP POST-tal a
// cél(ok) /ecowitt címére; a főciklus nem vár rá. Ha egy cél nem érhető el, a sor betelik, és az újak elmaradnak.
// (Az idojaras.ino része: az Arduino IDE és a PlatformIO a fájlokat ábécésorrendben, egy programmá fűzi össze.)

constexpr int FW_QUEUE = 4;
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

// Az egyedi mDNS-név ellenőrzése: csak betű, szám, kötőjel (ez kerül a ".local" elé, pont/kettőspont nélkül)
static bool mdns_name_valid(const char *h) {
  if (!h[0] || strlen(h) > 19) return false;
  for (; *h; h++) if (!isalnum((unsigned char)*h) && *h != '-') return false;
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

// A handle_ecowitt_post()-ból: a most fogadott küldés sorba tétele (nem blokkol). A cfg.fw_host vesszővel elválasztott
// lista is lehet (pl. ha minden konzol másik kijelzőre küld, és mindegyik a másik kettőnek adja tovább a saját adatát):
// a törzset egyszer építjük fel (PSRAM-ban), utána minden célra külön üzenet kerül a sorba.
static void fw_push() {
  if (!cfg.fw_on || !cfg.fw_host[0]) return;   // kikapcsolva vagy nincs cél
  // csak a saját konzoltól közvetlenül kapott adatot küldjük tovább (fwd nélkül); a már továbbküldöttet nem küldjük
  // tovább megint (különben csillag-elrendezésnél a 3 kijelző körbe-körbe verné a saját adatát egymásnak)
  if (my_server.hasArg("fwd")) return;
  const int hops = 0;
  if (!fw_q) {
    fw_q = xQueueCreate(FW_QUEUE, sizeof(FwMsg *));
    if (!fw_q) return;
  }
  if (!fw_task_h &&
      xTaskCreatePinnedToCoreWithCaps(fw_worker, "tovabbkuld", 6144, NULL, 1, &fw_task_h, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    fw_task_h = NULL;
    return;
  }
  FwMsg *tmpl = (FwMsg *)heap_caps_malloc(sizeof(FwMsg), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!tmpl) { fw_drop_one("nincs memória"); return; }
  tmpl->len = 0;
  tmpl->body[0] = 0;
  bool fits = true;
  for (int i = 0; i < my_server.args() && fits; i++) {
    const String name = my_server.argName(i);
    if (name == "plain" || name == "fwd") continue;   // a WebServer a nem urlencoded törzset és a cím paramétereit is argumentumként adja
    if (tmpl->len) tmpl->body[tmpl->len++] = '&';
    fits = fw_append_enc(*tmpl, name) && tmpl->len + 1 < sizeof(tmpl->body);
    if (fits) { tmpl->body[tmpl->len++] = '='; fits = fw_append_enc(*tmpl, my_server.arg(i)); }
  }
  if (!fits) { heap_caps_free(tmpl); fw_drop_one("a küldés túl hosszú"); return; }
  char list[sizeof(cfg.fw_host)];
  strlcpy(list, cfg.fw_host, sizeof(list));
  for (char *tok = strtok(list, ", "); tok; tok = strtok(NULL, ", ")) {
    if (!fw_host_valid(tok)) continue;
    if (uxQueueSpacesAvailable(fw_q) == 0) { fw_drop_one("a sor tele (a cél nem válaszol?), a küldés elmaradt"); continue; }
    FwMsg *m = (FwMsg *)heap_caps_malloc(sizeof(FwMsg), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!m) { fw_drop_one("nincs memória"); continue; }
    snprintf(m->url, sizeof(m->url), "http://%s/ecowitt?fwd=%d", tok, hops + 1);
    m->len = tmpl->len;
    memcpy(m->body, tmpl->body, tmpl->len + 1);
    if (xQueueSend(fw_q, &m, 0) != pdTRUE) { heap_caps_free(m); fw_drop_one("a sor tele, a küldés elmaradt"); }
  }
  heap_caps_free(tmpl);
}

static const char *fw_status_text() {
  static char b[192];
  portENTER_CRITICAL(&fw_mux);
  snprintf(b, sizeof(b), tr("%s (sikeres: %u, hibás: %u, elmaradt: %u)", "%s (successful: %u, failed: %u, dropped: %u)"),
           fw_status[0] ? fw_status : tr("még nem küldött", "nothing sent yet"), (unsigned)fw_ok, (unsigned)fw_err, (unsigned)fw_drop);
  portEXIT_CRITICAL(&fw_mux);
  return b;
}
