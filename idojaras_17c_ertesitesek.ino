// Telefonos értesítések az ntfy.sh-n keresztül (ingyenes, regisztráció nélkül): a telefonon az ntfy appban a beállított
// témára kell feliratkozni. Az események (fagy, eső, szellőztetés, akku, elakadt adat, összeomlás) egyszer szólnak, és csak
// megszűnés után újra; az éjszakai mód idején csak a fontosak (fagy, összeomlás) mennek ki.
// (Az idojaras.ino része: az Arduino IDE és a PlatformIO a fájlokat ábécésorrendben, egy programmá fűzi össze.)

constexpr int NT_QUEUE = 8;
constexpr uint32_t NT_CHECK_MS = 30000;            // ilyen gyakran nézzük az eseményeket
constexpr uint32_t NT_GRACE_MS = 10UL * 60000UL;   // indulás után ennyi ideig nincs akku-/adat-értesítés (az adatok beérkezése)
constexpr uint32_t NT_STALE_MS = 15UL * 60000UL;   // ennyi ideje nem küld egy konzol: elakadt adat
static NtMsg nt_q[NT_QUEUE];
static int nt_head = 0, nt_tail = 0;               // gyűrűs sor (head: következő küldendő, tail: következő szabad hely)
static portMUX_TYPE nt_mux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t nt_task_h = NULL;
static char nt_status[96] = "";                    // az utolsó küldés eredménye (a kezelőoldalon; üres: még nem küldött)

// JSON-szövegbe írás (idézőjel, fordított perjel, sortörés kódolva)
static void json_str(String &o, const char *s) {
  o += '"';
  for (; *s; s++) {
    if (*s == '"' || *s == '\\') { o += '\\'; o += *s; }
    else if (*s == '\n') o += "\\n";
    else if ((uint8_t)*s >= 0x20) o += *s;
  }
  o += '"';
}

static bool nt_topic_valid(const char *t) {
  if (!t[0] || strlen(t) > 64) return false;
  for (; *t; t++) if (!isalnum((unsigned char)*t) && *t != '-' && *t != '_') return false;
  return true;
}

static void nt_send_one(const NtMsg &m) {
  String body = "{\"topic\":";
  json_str(body, cfg.nt_topic);
  body += ",\"title\":";   json_str(body, m.title);
  body += ",\"message\":"; json_str(body, m.msg);
  body += ",\"priority\":"; body += (int)m.prio;
  if (m.tags[0]) { body += ",\"tags\":["; json_str(body, m.tags); body += "]"; }
  body += "}";
  WiFiClientSecure cli;
  cli.setInsecure();   // a tanúsítványt nem ellenőrizzük, a forgalom titkosított
  HTTPClient http;
  http.setTimeout(10000);
  int code = -1;
  if (http.begin(cli, "https://ntfy.sh/")) {
    http.addHeader("Content-Type", "application/json");
    code = http.POST(body);
    http.end();
  }
  struct tm t;
  char ts[8] = "";
  if (get_local_tm(t)) snprintf(ts, sizeof(ts), "%02d:%02d", t.tm_hour, t.tm_min);
  portENTER_CRITICAL(&nt_mux);
  snprintf(nt_status, sizeof(nt_status), "%s %s: %s", ts, code == 200 ? tr("elküldve", "sent") : tr("HIBA", "ERROR"), m.title);
  portEXIT_CRITICAL(&nt_mux);
}

// a háttérfeladat: értesítésre kiküldi a sorban álló üzeneteket (a TLS-kapcsolat ne akassza meg a kijelzőt)
static void nt_worker(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    for (;;) {
      NtMsg m;
      bool have = false;
      portENTER_CRITICAL(&nt_mux);
      if (nt_head != nt_tail) { m = nt_q[nt_head]; nt_head = (nt_head + 1) % NT_QUEUE; have = true; }
      portEXIT_CRITICAL(&nt_mux);
      if (!have) break;
      if (WiFi.status() == WL_CONNECTED) nt_send_one(m);
    }
  }
}

static void nt_push(const char *title, const char *msg, uint8_t prio, const char *tags) {
  if (!nt_topic_valid(cfg.nt_topic)) return;
  portENTER_CRITICAL(&nt_mux);
  const int next = (nt_tail + 1) % NT_QUEUE;
  if (next != nt_head) {   // tele sornál az új üzenet elmarad
    NtMsg &m = nt_q[nt_tail];
    strlcpy(m.title, title, sizeof(m.title));
    strlcpy(m.msg, msg, sizeof(m.msg));
    strlcpy(m.tags, tags ? tags : "", sizeof(m.tags));
    m.prio = prio;
    nt_tail = next;
  }
  portEXIT_CRITICAL(&nt_mux);
  if (!nt_task_h &&
      xTaskCreatePinnedToCoreWithCaps(nt_worker, "ntfy", 10240, NULL, 1, &nt_task_h, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    nt_task_h = NULL;
    return;
  }
  xTaskNotifyGive(nt_task_h);
  LogOut.printf("Értesítés: %s\n", title);
}

static const char *nt_status_text() { return nt_status[0] ? nt_status : tr("még nem küldött", "nothing sent yet"); }

// Egy esemény állapota: szól a bekövetkezéskor (ha a maszk és a csendes időszak engedi), majd csak megszűnés után újra
static void nt_event(bool &sent, bool active, int bit, bool important, const char *title, const char *msg,
                     uint8_t prio, const char *tags) {
  if (!active) { sent = false; return; }
  if (sent || !(cfg.nt_mask & bit)) return;
  if (is_night() && !important) return;   // csendes időszak: reggel (ha még fennáll) megy ki
  nt_push(title, msg, prio, tags);
  sent = true;
}

// A loop()-ból: az események figyelése
static void nt_task() {
  static uint32_t last = 0;
  static bool s_frost = false, s_rain = false, s_co2[ROOM_COUNT] = {}, s_batt[ROOM_COUNT] = {}, s_ws90 = false,
              s_stale[ROOM_COUNT] = {}, s_crash = false;
  if (g_demo || !cfg.nt_topic[0] || WiFi.status() != WL_CONNECTED || time(nullptr) < 1600000000) return;   // bemutatón nincs értesítés
  if (last && millis() - last < NT_CHECK_MS) return;
  last = millis();
  char title[64], msg[200];

  // összeomlás / watchdog / gyenge táp miatti újraindulás (egyszer, induláskor)
  const uint8_t rr = boots[0].reason;
  const bool crashed = rr == ESP_RST_PANIC || rr == ESP_RST_TASK_WDT || rr == ESP_RST_INT_WDT || rr == ESP_RST_WDT || rr == ESP_RST_BROWNOUT;
  if (crashed && !s_crash) {
    snprintf(msg, sizeof(msg), tr("Ok: %s.%s%s", "Reason: %s.%s%s"), reset_text(rr, boots[0].cause),
             crash_short[0] ? tr(" Hiba helye: ", " Location: ") : "", crash_short);
    nt_event(s_crash, true, NT_CRASH, true, tr("A kijelző újraindult", "The display restarted"), msg, 4, "rotating_light");
  }

  // fagy és eső az órás előrejelzésből
  const int fi = fc_frost_hour(), ri = fc_rain_hour();
  if (fi >= 0) snprintf(msg, sizeof(msg), tr("A következő órákban %.0f °C körülire hűl (%d:00 körül).",
                                                "In the next hours it cools down to about %.0f °C (around %d:00)."), fc.hour[fi].t, fc.hour[fi].h);
  nt_event(s_frost, fi >= 0, NT_FROST, true, tr("Fagy várható", "Frost expected"), msg, 4, "snowflake");
  if (ri >= 0) snprintf(msg, sizeof(msg), tr("Eső várható kb. %d:00-tól (%.0f%% esély).", "Rain expected from about %d:00 (%.0f%% chance)."), fc.hour[ri].h, isnan(fc.hour[ri].pp) ? 0.0f : fc.hour[ri].pp);
  nt_event(s_rain, ri >= 0, NT_RAIN, false, tr("Eső várható", "Rain expected"), msg, 3, "umbrella");

  const bool grace = millis() < NT_GRACE_MS;
  for (int i = 0; i < ROOM_COUNT; i++) {
    const Room &r = rooms[i];
    if (!r.match[0]) continue;
    // szellőztetés (ugyanaz az állapot, mint a főoldal felső sorában)
    snprintf(title, sizeof(title), tr("Szellőztess: %s", "Ventilate: %s"), r.name);
    snprintf(msg, sizeof(msg), tr("A CO2 %.0f ppm (%s).", "CO2 is %.0f ppm (%s)."), r.co2, r.name);
    nt_event(s_co2[i], co2_vent[i], NT_CO2, false, title, msg, 3, "wind_face");
    // akku
    const bool low = !grace && room_fresh(i) && r.has_batt && r.low_v > 0 && r.batt < r.low_v;
    snprintf(title, sizeof(title), tr("Akku: %s", "Battery: %s"), r.name);
    if (r.batt < 0.5f) snprintf(msg, sizeof(msg), tr("A(z) %s konzoljában nincs elem (vagy teljesen lemerült).",
                                                   "The %s console has no battery (or it is completely flat)."), r.name);
    else               snprintf(msg, sizeof(msg), tr("A(z) %s konzoljának akkuja %.2f V (határ: %.1f V).",
                                                   "The battery of the %s console is %.2f V (limit: %.1f V)."), r.name, r.batt, r.low_v);
    nt_event(s_batt[i], low, NT_BATT, false, title, msg, 3, "battery");
    // elakadt adat (és ha újra jön, arról is szól)
    const bool stale = !grace && r.last_ms && millis() - r.last_ms > NT_STALE_MS;
    const bool was = s_stale[i];
    snprintf(title, sizeof(title), tr("Nem jön adat: %s", "No data: %s"), r.name);
    snprintf(msg, sizeof(msg), tr("A(z) %s konzolja %u perce nem küldött adatot.", "The %s console has not sent data for %u minutes."), r.name, (unsigned)((millis() - r.last_ms) / 60000UL));
    nt_event(s_stale[i], stale, NT_STALE, false, title, msg, 4, "warning");
    if (was && !stale && (cfg.nt_mask & NT_STALE)) {
      snprintf(title, sizeof(title), tr("Újra jön adat: %s", "Data again: %s"), r.name);
      nt_push(title, tr("A konzol újra küld adatot.", "The console sends data again."), 2, "white_check_mark");
    }
  }
  // a WS90 eleme
  const bool ws_low = !grace && weather.has_data && weather.wh90_batt_v > 0.1f && weather.wh90_batt_v < LOW_WS90_V;
  snprintf(msg, sizeof(msg), tr("A WS90 elemfeszültsége %.2f V.", "The WS90 battery voltage is %.2f V."), weather.wh90_batt_v);
  nt_event(s_ws90, ws_low, NT_BATT, false, tr("Akku: WS90", "Battery: WS90"), msg, 3, "battery");
}

// a kezelőoldal "Próbaüzenet" gombja
static void handle_ntfy_test() {
  if (!ota_auth_ok()) { request_auth(); return; }
  nt_push(tr("Próbaüzenet", "Test message"), tr("Ha ezt látod, a kijelző értesítései működnek.", "If you see this, the display's notifications work."), 3, "tada");
  cur_srv->sendHeader("Location", "/");
  cur_srv->send(303, "text/plain", "");
}
