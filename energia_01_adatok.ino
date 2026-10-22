// Energia: a ház energiaadatai (HomeWizard P1 mérő, HomeWizard Energy Socket, Growatt napelem-felhő), háttérfeladatból
// - hálózat (P1) és boyler (dugalj): helyi HTTP, Local API v1 (a HomeWizard appban kell bekapcsolni), 2 mp-enként
// - napelem: a Growatt OpenAPI (HTTPS, token a fejlécben), 5 percenként (a felhő ennél sűrűbben úgysem frissül)
// - a ház fogyasztása = napelem + hálózat (becslés, mert a napelem adata akár 5 perces)
// A beállítások (be/ki, címek, token) a webes kezelőoldalon, NVS "cfg" (en_on, en_p1, en_sk, en_gt, en_gs); alapból ki.
// A token és a címek nem kerülhetnek a kódba (a tároló nyilvános).
// (Az idojaras.ino része: az Arduino IDE és a PlatformIO a fájlokat ábécésorrendben, egy programmá fűzi össze. Ez a fájl
// közvetlenül az idojaras.ino után jön: a későbbi fájlok static függvényeit csak az alábbi előre deklarálással használja.)

static void logout_printf(const char *fmt, ...);     // idojaras_02 (a Naplóba)
static bool get_local_tm(struct tm &t);              // idojaras_03
static bool fw_host_valid(const char *h);            // idojaras_06b (gépnév vagy IP, esetleg :port)
static String html_attr(const char *s);              // idojaras_11

constexpr uint32_t EN_LOCAL_MS = 2000;               // P1 és dugalj lekérése ilyen gyakran
constexpr uint32_t EN_LOCAL_ERR_MS = 10000;          // ha egyik sem válaszol: ennyi idő múlva újra
constexpr uint32_t EN_GW_MS = 5UL * 60UL * 1000UL;   // Growatt
constexpr uint32_t EN_GW_RETRY_MS = 60UL * 1000UL;   // Growatt hiba után
constexpr uint32_t EN_GW_RATE_MS = 10UL * 60UL * 1000UL;   // Growatt "túl sűrű lekérdezés" (10012) után
constexpr int EN_GW_ERR_RATE = 10012;                // error_frequently_access (több kijelző ugyanazzal a tokennel)
constexpr uint32_t EN_LOCAL_STALE_MS = 15000;        // ennél régebbi P1-/dugalj-adat már nem érvényes
constexpr uint32_t EN_GW_STALE_MS = 20UL * 60UL * 1000UL;
constexpr int EN_CONNECT_MS = 1500, EN_TIMEOUT_MS = 2500;      // helyi eszközök
constexpr int EN_GW_TIMEOUT_MS = 10000;
constexpr float EN_OVERVOLT_V = 253.0f;              // túlfeszültség: 230 V + 10 %
#define EN_GW_DEFAULT "openapi.growatt.com"          // európai szerver

// beállítások (a főszál írja mentéskor, a háttérfeladat en_mux alatt másolja) és a mért adatok (EnCfg, EnData: idojaras.ino)
static EnCfg en_cfg = {false, "", "", "", "", 0.0f, ""};
static EnData en_data = {};
static portMUX_TYPE en_mux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t en_task_h = NULL;
static volatile bool en_gw_force = false;   // új beállítás: a Growattot azonnal kérdezzük

// Mai fogyasztás: a mérőállások a nap elején (a főszál kezeli, NVS "cfg"/"en_day", naponta egy írás). key: év*400 + az év
// napja; since: ettől a perctől számol (0: éjféltől; ha a kijelző napközben indul először, az indulás ideje); NAN: még nincs.
struct EnDay { int32_t key, since; float imp, exp, boil; };
static EnDay en_day = {-1, 0, NAN, NAN, NAN};

// A mai nap 5 perces szeletekben (a napi diagramhoz): w = a napelem termelése (W, a Growatt-lekérésekből, a felhő időbélyege
// szerint), net = a hálózati teljesítmény átlaga (W, + vétel, - visszatáplálás, a P1-ből). FFat "/energia.bin" (a többi
// előzményfájltól külön), szeletenként mentve. A mai nap kulcsa (key) szerint törlődik.
constexpr int EN_PV_N = 288;
constexpr uint16_t EN_PV_NONE = 0xFFFF;
constexpr int16_t EN_NET_NONE = INT16_MIN;
constexpr uint32_t EN_PV_MAGIC = 0x32564E45;   // "ENV2" (az ENV1 csak a termelést tárolta; azt a betöltés eldobja)
#define EN_PV_FILE "/energia.bin"
struct EnPvDay { uint32_t magic; int32_t key; uint16_t w[EN_PV_N]; int16_t net[EN_PV_N]; };
static EnPvDay en_pv = {};
static int en_pv_last = -1;         // az utolsó rögzített termelés-szelet (a kimaradt szeletek kitöltéséhez)
static uint32_t en_pv_ver = 1;      // változáskor nő (a diagram ebből tudja, hogy újra kell rajzolni)

static void en_pv_clear(int32_t key) {
  en_pv.magic = EN_PV_MAGIC;
  en_pv.key = key;
  for (uint16_t &w : en_pv.w) w = EN_PV_NONE;
  for (int16_t &n : en_pv.net) n = EN_NET_NONE;
  en_pv_last = -1;
  en_pv_ver++;
}

static void en_pv_load() {
  en_pv_clear(-1);
  if (!ffat_ok) return;
  File f = FFat.open(EN_PV_FILE, FILE_READ);
  if (!f) return;
  EnPvDay t;
  if (f.read((uint8_t *)&t, sizeof(t)) == sizeof(t) && t.magic == EN_PV_MAGIC) {
    en_pv = t;
    for (int s = EN_PV_N - 1; s >= 0; s--) if (en_pv.w[s] != EN_PV_NONE) { en_pv_last = s; break; }
  }
  f.close();
}

static void en_pv_save() {
  if (!ffat_ok || g_demo) return;
  CamPause cam_pause_;   // P4: a kamera áll a flash-írás alatt
  File f = FFat.open(EN_PV_FILE, FILE_WRITE);
  if (!f) return;
  f.write((const uint8_t *)&en_pv, sizeof(en_pv));
  f.close();
}

static void en_pv_day(int32_t key) { if (en_pv.key != key) en_pv_clear(key); }

// Új Growatt-adat: a felhő idejének szeletébe (csak ha az mai; éjszaka a tegnapi utolsó adat jön vissza). Ha 3 szeletnél
// kevesebb maradt ki, az utolsó rögzített óta egyenesen kitöltjük (a felhő és a lekérés üteme elcsúszhat).
static void en_pv_record(const EnData &d) {
  struct tm t;
  int y, mo, da, h, mi;
  if (!get_local_tm(t) || sscanf(d.gw_upd, "%d-%d-%d %d:%d", &y, &mo, &da, &h, &mi) != 5) return;
  if (y != t.tm_year + 1900 || mo != t.tm_mon + 1 || da != t.tm_mday || h < 0 || h > 23 || mi < 0 || mi > 59) return;
  en_pv_day(y * 400 + t.tm_yday);
  const int s = (h * 60 + mi) / 5;
  const uint16_t w = (uint16_t)constrain((int)lroundf(d.pv_w), 0, 65000);
  if (en_pv_last >= 0 && s - en_pv_last > 1 && s - en_pv_last <= 3 && en_pv.w[en_pv_last] != EN_PV_NONE) {
    const int a = en_pv_last, wa = en_pv.w[a];
    for (int k = a + 1; k < s; k++) en_pv.w[k] = (uint16_t)(wa + (w - wa) * (k - a) / (s - a));
  }
  en_pv.w[s] = w;
  if (s > en_pv_last) en_pv_last = s;
  en_pv_ver++;
  en_pv_save();
}

// A hálózati teljesítmény 5 perces átlaga a P1 mintáiból: a szelet végén mentve, közben 30 mp-enként az aktuális (részleges)
// átlag kerül a diagramra (mentés nélkül).
static void en_net_task(const EnData &d) {
  struct tm t;
  if (!get_local_tm(t)) return;
  const int32_t key = (t.tm_year + 1900) * 400 + t.tm_yday;
  const int s = (t.tm_hour * 60 + t.tm_min) / 5;
  static int32_t acc_key = -1;
  static int acc_slice = -1, n = 0;
  static double sum = 0;
  static uint32_t last_p1 = 0, last_live = 0;
  if (key != acc_key || s != acc_slice) {   // új szelet: az előző végleges értéke (ha ugyanarra a napra esik)
    if (n > 0 && acc_key == en_pv.key && acc_slice >= 0) {
      en_pv.net[acc_slice] = (int16_t)constrain((int)lroundf(sum / n), -32000, 32000);
      en_pv_ver++;
      en_pv_save();
    }
    acc_key = key; acc_slice = s; sum = 0; n = 0;
  }
  if (en_fresh(d.p1_ms, EN_LOCAL_STALE_MS) && d.p1_ms != last_p1) { last_p1 = d.p1_ms; sum += d.grid_w; n++; }
  if (n > 0 && millis() - last_live >= 30000) {
    last_live = millis();
    en_pv_day(key);
    en_pv.net[s] = (int16_t)constrain((int)lroundf(sum / n), -32000, 32000);
    en_pv_ver++;
  }
}

// a mai érték: a mostani mérőállás és a nap eleji különbsége (NAN: nincs adat)
static float en_today(float now, float base) {
  if (isnan(now) || isnan(base)) return NAN;
  return now > base ? now - base : 0;
}

static EnData en_snapshot() {
  EnData d;
  portENTER_CRITICAL(&en_mux);
  d = en_data;
  portEXIT_CRITICAL(&en_mux);
  return d;
}

static bool en_fresh(uint32_t ms, uint32_t limit) { return ms && millis() - ms < limit; }

// A ház becsült fogyasztása (W); NAN, ha a hálózat vagy a napelem adata hiányzik
static float en_house_w(const EnData &d) {
  if (!en_fresh(d.p1_ms, EN_LOCAL_STALE_MS) || !en_fresh(d.gw_ms, EN_GW_STALE_MS)) return NAN;
  float w = d.pv_w + d.grid_w;
  return w < 0 ? 0 : w;
}

// ---- JSON: csak a szükséges mezők, könyvtár nélkül (a válaszok laposak, a Growatt a számokat gyakran szövegként adja) ----

// a "kulcs": utáni érték eleje (NULL: nincs ilyen kulcs)
static const char *en_json_val(const char *js, const char *key) {
  char k[48];
  snprintf(k, sizeof(k), "\"%s\"", key);
  const char *p = strstr(js, k);
  if (!p) return NULL;
  p += strlen(k);
  while (*p == ' ') p++;
  if (*p != ':') return NULL;
  p++;
  while (*p == ' ') p++;
  return p;
}

// szám vagy "szám"
static bool en_json_num(const char *js, const char *key, double &out) {
  const char *p = en_json_val(js, key);
  if (!p) return false;
  if (*p == '"') p++;
  char *end;
  double v = strtod(p, &end);
  if (end == p) return false;
  out = v;
  return true;
}

static float en_json_f(const char *js, const char *key, float def = NAN) {
  double v;
  return en_json_num(js, key, v) ? (float)v : def;
}

static bool en_json_str(const char *js, const char *key, char *out, size_t n) {
  const char *p = en_json_val(js, key);
  if (!p || *p != '"' || !n) return false;
  p++;
  size_t i = 0;
  while (*p && *p != '"' && i + 1 < n) out[i++] = *p++;
  out[i] = 0;
  return true;
}

// ---- lekérések (a háttérfeladatban) ----

static void en_http_err(int code, char *err, size_t n) {
  if (code < 0) strlcpy(err, HTTPClient::errorToString(code).c_str(), n);
  else snprintf(err, n, "HTTP %d", code);
}

// helyi eszköz (HomeWizard): http://<cím>/api/v1/data
static bool en_get_local(const char *host, String &body, char *err, size_t n) {
  char url[96];
  snprintf(url, sizeof(url), "http://%s/api/v1/data", host);
  WiFiClient cli;
  HTTPClient http;
  http.setConnectTimeout(EN_CONNECT_MS);
  http.setTimeout(EN_TIMEOUT_MS);
  http.setReuse(false);
  if (!http.begin(cli, url)) { strlcpy(err, tr("hibás cím", "invalid address"), n); return false; }
  int code = http.GET();
  if (code == 200) body = http.getString();
  else en_http_err(code, err, n);
  http.end();
  return code == 200;
}

// Growatt OpenAPI (a hívó tartja a TlsLock-ot); a válasz error_code mezője 0 kell legyen
static int en_gw_code = 0;   // az utolsó Growatt-válasz error_code-ja (csak a háttérfeladat használja)
static bool en_get_growatt(const EnCfg &c, const char *path, String &body, char *err, size_t n) {
  en_gw_code = 0;
  char url[160];
  snprintf(url, sizeof(url), "https://%s%s", c.gs[0] ? c.gs : EN_GW_DEFAULT, path);
  WiFiClientSecure cli;
  cli.setInsecure();   // a tanúsítványt nem ellenőrizzük, a forgalom titkosított
  HTTPClient http;
  http.setTimeout(EN_GW_TIMEOUT_MS);
  if (!http.begin(cli, url)) { strlcpy(err, tr("kapcsolódási hiba", "connection error"), n); return false; }
  http.addHeader("token", c.gt);
  int code = http.GET();
  if (code == 200) body = http.getString();
  else en_http_err(code, err, n);
  http.end();
  if (code != 200) return false;
  double ec;
  if (!en_json_num(body.c_str(), "error_code", ec)) { strlcpy(err, tr("hibás válasz", "invalid response"), n); return false; }
  if (ec != 0) {
    en_gw_code = (int)ec;
    char msg[48] = "";
    en_json_str(body.c_str(), "error_msg", msg, sizeof(msg));
    snprintf(err, n, tr("Growatt hiba %d: %s", "Growatt error %d: %s"), (int)ec, msg);
    return false;
  }
  return true;
}

static bool en_fetch_p1(const char *host) {
  String b;
  char err[48] = "";
  bool ok = en_get_local(host, b, err, sizeof(err));
  const char *js = b.c_str();
  float w = NAN, u[3], i[3], imp = NAN, exp = NAN;
  int tariff = 0;
  if (ok) {   // a feldolgozás a kritikus szakaszon kívül
    w = en_json_f(js, "active_power_w");
    u[0] = en_json_f(js, "active_voltage_l1_v"); u[1] = en_json_f(js, "active_voltage_l2_v"); u[2] = en_json_f(js, "active_voltage_l3_v");
    i[0] = en_json_f(js, "active_current_l1_a"); i[1] = en_json_f(js, "active_current_l2_a"); i[2] = en_json_f(js, "active_current_l3_a");
    imp = en_json_f(js, "total_power_import_kwh");
    exp = en_json_f(js, "total_power_export_kwh");
    tariff = (int)en_json_f(js, "active_tariff", 0);
    if (isnan(w)) { ok = false; strlcpy(err, tr("hiányzó adat", "missing data"), sizeof(err)); }
  }
  portENTER_CRITICAL(&en_mux);
  if (ok) {
    EnData &d = en_data;
    d.grid_w = w;
    memcpy(d.u, u, sizeof(u));
    memcpy(d.i, i, sizeof(i));
    d.imp_kwh = imp; d.exp_kwh = exp; d.tariff = tariff;
    d.p1_ms = millis();
  }
  strlcpy(en_data.p1_err, err, sizeof(en_data.p1_err));
  portEXIT_CRITICAL(&en_mux);
  return ok;
}

static bool en_fetch_socket(const char *host) {
  String b;
  char err[48] = "";
  bool ok = en_get_local(host, b, err, sizeof(err));
  const char *js = b.c_str();
  const float w = en_json_f(js, "active_power_w"), kwh = en_json_f(js, "total_power_import_kwh");
  if (ok && isnan(w)) { ok = false; strlcpy(err, tr("hiányzó adat", "missing data"), sizeof(err)); }
  portENTER_CRITICAL(&en_mux);
  if (ok) {
    en_data.boil_w = w;
    en_data.boil_kwh = kwh;
    en_data.sk_ms = millis();
  }
  strlcpy(en_data.sk_err, err, sizeof(en_data.sk_err));
  portEXIT_CRITICAL(&en_mux);
  return ok;
}

// Egy kérés 5 percenként: az erőmű adatai (pillanatnyi teljesítmény kW-ban, napi / havi / éves termelés). Az erőmű
// azonosítóját csak egyszer, az erőműlistából kérdezzük le. (A Growatt a sűrű lekérdezést 10012-es hibával utasítja el:
// több kijelző ugyanazzal a tokennel, újraindítások.)
static long en_gw_pid = 0;
static bool en_fetch_growatt(const EnCfg &c) {
  char err[80] = "";
  bool ok = false;
  float pv = NAN, day = NAN, month = NAN, year = NAN;
  char upd[24] = "";
  {
    TlsLock tls_lock_;   // egyszerre csak egy HTTPS-kapcsolat
    String b;
    double v;
    if (!en_gw_pid && en_get_growatt(c, "/v1/plant/list", b, err, sizeof(err))) {
      const char *pl = strstr(b.c_str(), "\"plants\"");   // az első erőmű
      if (pl && en_json_num(pl, "plant_id", v)) en_gw_pid = (long)v;
      else strlcpy(err, tr("nincs erőmű a fiókban", "no plant in the account"), sizeof(err));
    }
    if (en_gw_pid) {
      char path[64];
      snprintf(path, sizeof(path), "/v1/plant/data?plant_id=%ld", en_gw_pid);
      b = "";
      if (en_get_growatt(c, path, b, err, sizeof(err))) {
        const char *js = b.c_str();
        pv = en_json_f(js, "current_power") * 1000.0f;   // itt kW-ban
        day = en_json_f(js, "today_energy");
        month = en_json_f(js, "monthly_energy");
        year = en_json_f(js, "yearly_energy");
        en_json_str(js, "last_update_time", upd, sizeof(upd));
        ok = !isnan(pv);
        if (!ok) strlcpy(err, tr("hiányzó adat", "missing data"), sizeof(err));
      } else if (en_gw_code && en_gw_code != EN_GW_ERR_RATE) en_gw_pid = 0;   // pl. megváltozott az erőmű: újra a listából
    }
  }
  portENTER_CRITICAL(&en_mux);
  if (ok) {
    EnData &d = en_data;
    d.pv_w = pv < 0 ? 0 : pv;
    d.plant_id = en_gw_pid;
    d.pv_day = day; d.pv_month = month; d.pv_year = year;
    strlcpy(d.gw_upd, upd, sizeof(d.gw_upd));
    d.gw_remote = false;
    d.gw_ms = millis();
  }
  strlcpy(en_data.gw_err, err, sizeof(en_data.gw_err));
  portEXIT_CRITICAL(&en_mux);
  return ok;
}

// A napelem-adat továbbküldése másik kijelzőkre (a háttérfeladatban, sikeres Growatt-lekérés után): HTTP POST a cél /energia
// címére. Egyetlen kijelzőn van Growatt-token (a Growatt a sűrű lekérdezést 10012-es hibával utasítja el), a többi tőle kapja.
static char en_fw_st[110] = "";
static void en_fw_send(const EnCfg &c) {
  if (!c.fw[0]) return;
  EnData d = en_snapshot();
  if (!d.gw_ms || !d.gw_upd[0]) return;
  char body[200];
  int n = snprintf(body, sizeof(body), "w=%.0f&upd=", d.pv_w);
  for (const char *q = d.gw_upd; *q && n < (int)sizeof(body) - 80; q++) body[n++] = *q == ' ' ? 'T' : *q;
  if (!isnan(d.pv_day)) n += snprintf(body + n, sizeof(body) - n, "&day=%.2f&month=%.1f&year=%.1f", d.pv_day, d.pv_month, d.pv_year);
  char list[sizeof(c.fw)], st[sizeof(en_fw_st)] = "";
  strlcpy(list, c.fw, sizeof(list));
  for (char *tok = strtok(list, ", "); tok; tok = strtok(NULL, ", ")) {
    if (!fw_host_valid(tok)) continue;
    char url[96];
    snprintf(url, sizeof(url), "http://%s/energia", tok);
    WiFiClient cli;
    HTTPClient http;
    http.setConnectTimeout(EN_CONNECT_MS);
    http.setTimeout(EN_TIMEOUT_MS);
    http.setReuse(false);
    int code = -1;
    if (http.begin(cli, url)) {
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      code = http.POST((uint8_t *)body, n);
      http.end();
    }
    char one[48];
    snprintf(one, sizeof(one), code == 200 ? "%s: OK" : "%s: %s", tok, code == 200 ? "" : (code < 0 ? HTTPClient::errorToString(code).c_str() : String(code).c_str()));
    if (st[0]) strlcat(st, "; ", sizeof(st));
    strlcat(st, one, sizeof(st));
  }
  struct tm t;
  char ts[8] = "";
  if (get_local_tm(t)) snprintf(ts, sizeof(ts), "%02d:%02d ", t.tm_hour, t.tm_min);
  portENTER_CRITICAL(&en_mux);
  snprintf(en_fw_st, sizeof(en_fw_st), "%s%s", ts, st);
  portEXIT_CRITICAL(&en_mux);
}

// A másik kijelzőtől érkező napelem-adat fogadása (POST /energia): úgy kezeljük, mintha mi kérdeztük volna le a Growattot
static void handle_energia_post() {
  WebServer &s = my_server;
  if (!en_cfg.on || g_demo) { s.send(200, "text/plain", "off"); return; }
  const double w = s.arg("w").toDouble();
  String upd = s.arg("upd");
  upd.replace('T', ' ');
  int y, mo, da, h, mi;
  if (!s.hasArg("w") || !(w >= 0 && w <= 100000) || upd.length() >= sizeof(en_data.gw_upd) || sscanf(upd.c_str(), "%d-%d-%d %d:%d", &y, &mo, &da, &h, &mi) != 5) {
    s.send(400, "text/plain", "bad request");
    return;
  }
  const float day = s.hasArg("day") ? s.arg("day").toFloat() : NAN;
  const float month = s.hasArg("month") ? s.arg("month").toFloat() : NAN;
  const float year = s.hasArg("year") ? s.arg("year").toFloat() : NAN;
  portENTER_CRITICAL(&en_mux);
  EnData &d = en_data;
  d.pv_w = (float)w;
  if (!isnan(day)) { d.pv_day = day; d.pv_month = month; d.pv_year = year; }
  strlcpy(d.gw_upd, upd.c_str(), sizeof(d.gw_upd));
  d.gw_remote = true;
  d.gw_err[0] = 0;
  d.gw_ms = millis();
  portEXIT_CRITICAL(&en_mux);
  s.send(200, "text/plain", "OK");
}

// A háttérfeladat: egyszer indul (verem a PSRAM-ban), és amíg be van kapcsolva, a saját ütemében kérdez
static void en_worker(void *) {
  uint32_t next_local = 0;
  uint32_t next_gw = millis() + esp_random() % 30000;   // induláskor szétszórva (több kijelző ugyanazzal a tokennel)
  for (;;) {
    EnCfg c;
    portENTER_CRITICAL(&en_mux);
    c = en_cfg;
    const bool force = en_gw_force;
    en_gw_force = false;
    portEXIT_CRITICAL(&en_mux);
    if (force) en_gw_pid = 0;   // új token / szerver: az erőművet is újra keressük
    if (!c.on || WiFi.status() != WL_CONNECTED) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
    if ((int32_t)(millis() - next_local) >= 0) {
      const uint32_t t0 = millis();
      bool any = false;
      if (c.p1[0]) any |= en_fetch_p1(c.p1);
      if (c.sk[0]) any |= en_fetch_socket(c.sk);
      next_local = t0 + ((any || (!c.p1[0] && !c.sk[0])) ? EN_LOCAL_MS : EN_LOCAL_ERR_MS);
    }
    if (c.gt[0] && (force || (int32_t)(millis() - next_gw) >= 0)) {
      const bool ok = en_fetch_growatt(c);
      next_gw = millis() + (ok ? EN_GW_MS : en_gw_code == EN_GW_ERR_RATE ? EN_GW_RATE_MS : EN_GW_RETRY_MS);
      if (ok) en_fw_send(c);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ---- főszál ----

static void en_cfg_load() {
  Preferences p;
  p.begin("cfg", true);
  EnCfg c = {};
  c.on = p.isKey("en_on") && p.getBool("en_on", false);
  strlcpy(c.p1, pref_str(p, "en_p1", "").c_str(), sizeof(c.p1));
  strlcpy(c.sk, pref_str(p, "en_sk", "").c_str(), sizeof(c.sk));
  strlcpy(c.gt, pref_str(p, "en_gt", "").c_str(), sizeof(c.gt));
  strlcpy(c.gs, pref_str(p, "en_gs", "").c_str(), sizeof(c.gs));
  c.kwp = pref_flt(p, "en_kwp", 0.0f);
  strlcpy(c.fw, pref_str(p, "en_fw", "").c_str(), sizeof(c.fw));
  EnDay day;
  if (p.isKey("en_day") && p.getBytesLength("en_day") == sizeof(day) && p.getBytes("en_day", &day, sizeof(day)) == sizeof(day)) en_day = day;
  p.end();
  portENTER_CRITICAL(&en_mux);
  en_cfg = c;
  en_data.pv_day = en_data.pv_month = en_data.pv_year = NAN;   // amíg nincs napelem-adat, "--" (nem 0)
  portEXIT_CRITICAL(&en_mux);
  en_pv_load();
}

// A nap eleji mérőállások: új napon (vagy ha egy mérő még hiányzott) az első friss adatból; mérőcsere / nullázás után
// (az állás a kezdő alá esik) onnan újra. Változáskor mentés.
static void en_day_task(const EnData &d) {
  struct tm t;
  if (!get_local_tm(t)) return;
  const int32_t key = (t.tm_year + 1900) * 400 + t.tm_yday, now = t.tm_hour * 60 + t.tm_min;
  const bool p1 = en_fresh(d.p1_ms, EN_LOCAL_STALE_MS) && !isnan(d.imp_kwh) && !isnan(d.exp_kwh);
  const bool sk = en_fresh(d.sk_ms, EN_LOCAL_STALE_MS) && !isnan(d.boil_kwh);
  bool ch = false;
  if (key != en_day.key) { en_day = {key, now < 5 ? 0 : now, NAN, NAN, NAN}; ch = true; }
  if (p1 && (isnan(en_day.imp) || d.imp_kwh < en_day.imp - 0.01f || d.exp_kwh < en_day.exp - 0.01f)) {
    en_day.imp = d.imp_kwh; en_day.exp = d.exp_kwh; ch = true;
  }
  if (sk && (isnan(en_day.boil) || d.boil_kwh < en_day.boil - 0.01f)) { en_day.boil = d.boil_kwh; ch = true; }
  if (!ch || (isnan(en_day.imp) && isnan(en_day.boil))) return;   // még nincs mit menteni
  CamPause cam_pause_;   // P4: a kamera áll a mentés alatt
  Preferences p;
  p.begin("cfg", false);
  p.putBytes("en_day", &en_day, sizeof(en_day));
  p.end();
}

// A loop()-ból: a háttérfeladat indítása (bekapcsolva, nem bemutató módban) és a hibák változásának naplózása
static void en_loop() {
  if (!en_cfg.on || g_demo) return;
  if (!en_task_h &&
      xTaskCreatePinnedToCoreWithCaps(en_worker, "energia", 10240, NULL, 1, &en_task_h, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    en_task_h = NULL;
    static uint32_t last_fail = 0;
    if (!last_fail || millis() - last_fail > 60000) { last_fail = millis(); logout_printf("Energia: a háttérfeladat nem indult\n"); }
    return;
  }
  static uint32_t last_check = 0;
  if (millis() - last_check < 1000) return;
  last_check = millis();
  // csak a változást írjuk a Naplóba (2 mp-enként nem)
  static char last_err[3][80] = {};
  static bool seen[3] = {};
  const EnData d = en_snapshot();
  en_day_task(d);
  en_net_task(d);
  static uint32_t last_gw = 0;
  if (d.gw_ms && d.gw_ms != last_gw) { last_gw = d.gw_ms; en_pv_record(d); }
  const char *errs[3] = {d.p1_err, d.sk_err, d.gw_err};
  const uint32_t ms[3] = {d.p1_ms, d.sk_ms, d.gw_ms};
  static const char *NAME[3] = {"hálózat (P1)", "boyler (dugalj)", "napelem (Growatt)"};
  for (int k = 0; k < 3; k++) {
    if (strcmp(errs[k], last_err[k]) != 0) {
      if (errs[k][0]) logout_printf("Energia: %s hiba: %s\n", NAME[k], errs[k]);
      else if (seen[k]) logout_printf("Energia: %s újra rendben\n", NAME[k]);
      strlcpy(last_err[k], errs[k], sizeof(last_err[k]));
    }
    if (ms[k] && !seen[k]) {
      seen[k] = true;
      if (k == 0) logout_printf("Energia: hálózat %.0f W\n", d.grid_w);
      if (k == 1) logout_printf("Energia: boyler %.0f W\n", d.boil_w);
      if (k == 2) logout_printf("Energia: napelem %.0f W, ma %.1f kWh (erőmű %ld)\n", d.pv_w, d.pv_day, d.plant_id);
    }
  }
}

// mennyi ideje (a kezelőoldalra): "3 mp" / "4 perc"
static void en_age(uint32_t ms, char *out, size_t n) {
  uint32_t s = (millis() - ms) / 1000;
  if (s < 120) snprintf(out, n, tr("%u mp", "%u s"), (unsigned)s);
  else snprintf(out, n, tr("%u perc", "%u min"), (unsigned)(s / 60));
}

// A kezelőoldal "Energia:" sora (HTML)
static String en_text() {
  if (!en_cfg.on) return tr("kikapcsolva", "off");
  if (g_demo) return tr("bemutató módban nincs lekérés", "no requests in demo mode");
  const EnData d = en_snapshot();
  String h;
  char b[200], age[16];
  // hálózat
  h += tr("<br>Hálózat: ", "<br>Grid: ");
  if (!en_cfg.p1[0]) h += tr("nincs megadva", "not set");
  else if (d.p1_ms) {
    en_age(d.p1_ms, age, sizeof(age));
    snprintf(b, sizeof(b), d.grid_w >= 0 ? tr("vétel %.0f W", "import %.0f W") : tr("visszatáplálás %.0f W", "export %.0f W"), fabsf(d.grid_w));
    h += b;
    snprintf(b, sizeof(b), tr(" (%s); %.1f / %.1f / %.1f V, %.0f / %.0f / %.0f A, T%d",
                              " (%s ago); %.1f / %.1f / %.1f V, %.0f / %.0f / %.0f A, T%d"),
             age, d.u[0], d.u[1], d.u[2], d.i[0], d.i[1], d.i[2], d.tariff);
    h += b;
    const float ti = en_today(d.imp_kwh, en_day.imp), te = en_today(d.exp_kwh, en_day.exp);
    if (!isnan(ti)) { snprintf(b, sizeof(b), tr("; ma vett %.2f, adott %.2f kWh", "; today imported %.2f, exported %.2f kWh"), ti, te); h += b; }
    for (int k = 0; k < 3; k++) if (d.u[k] > EN_OVERVOLT_V) {
      snprintf(b, sizeof(b), tr(" <b style='color:#ffd54f'>túlfeszültség (L%d)</b>", " <b style='color:#ffd54f'>overvoltage (L%d)</b>"), k + 1);
      h += b;
    }
  } else h += tr("még nincs adat", "no data yet");
  if (en_cfg.p1[0] && d.p1_err[0]) { h += " <span style='color:#ff8a80'>"; h += html_attr(d.p1_err); h += "</span>"; }
  // boyler
  h += tr("<br>Boyler: ", "<br>Boiler: ");
  if (!en_cfg.sk[0]) h += tr("nincs megadva", "not set");
  else if (d.sk_ms) {
    en_age(d.sk_ms, age, sizeof(age));
    snprintf(b, sizeof(b), tr("%.0f W (%s), összesen %.1f kWh", "%.0f W (%s ago), total %.1f kWh"), d.boil_w, age, d.boil_kwh);
    h += b;
    const float tb = en_today(d.boil_kwh, en_day.boil);
    if (!isnan(tb)) { snprintf(b, sizeof(b), tr(", ma %.2f kWh", ", today %.2f kWh"), tb); h += b; }
  } else h += tr("még nincs adat", "no data yet");
  if (en_cfg.sk[0] && d.sk_err[0]) { h += " <span style='color:#ff8a80'>"; h += html_attr(d.sk_err); h += "</span>"; }
  // napelem
  h += tr("<br>Napelem: ", "<br>Solar: ");
  if (!d.gw_ms && !en_cfg.gt[0]) h += tr("nincs token (és másik kijelzőtől sem kaptunk adatot)", "no token (and nothing received from another display)");
  else if (d.gw_ms) {
    en_age(d.gw_ms, age, sizeof(age));
    snprintf(b, sizeof(b), tr("%.0f W (%s, felhő: %s); ma %.1f, a hónapban %.0f, az évben %.0f kWh",
                              "%.0f W (%s ago, cloud: %s); today %.1f, this month %.0f, this year %.0f kWh"),
             d.pv_w, age, d.gw_upd[0] ? d.gw_upd : "?", d.pv_day, d.pv_month, d.pv_year);
    h += b;
    if (d.gw_remote) h += tr(" (másik kijelzőtől)", " (from another display)");
  } else h += tr("még nincs adat", "no data yet");
  if (en_cfg.gt[0] && d.gw_err[0]) { h += " <span style='color:#ff8a80'>"; h += html_attr(d.gw_err); h += "</span>"; }
  if (en_day.since > 0 && en_day.key >= 0) {
    snprintf(b, sizeof(b), tr("<br>A mai értékek %02d:%02d óta (az első indulás ma).", "<br>Today's values since %02d:%02d (first start today)."),
             (int)(en_day.since / 60), (int)(en_day.since % 60));
    h += b;
  }
  if (en_cfg.fw[0]) {
    char st[sizeof(en_fw_st)];
    portENTER_CRITICAL(&en_mux);
    strlcpy(st, en_fw_st, sizeof(st));
    portEXIT_CRITICAL(&en_mux);
    h += tr("<br>Napelem-adat továbbküldése: ", "<br>Solar data forwarding: ");
    h += st[0] ? html_attr(st) : String(tr("még nem küldött", "nothing sent yet"));
  }
  // ház
  const float house = en_house_w(d);
  if (!isnan(house)) {
    snprintf(b, sizeof(b), tr("<br>Ház: kb. %.0f W (napelem + hálózat)", "<br>House: approx. %.0f W (solar + grid)"), house);
    h += b;
  }
  return h;
}

// A webes Beállítások "Energia" része
static String en_settings_html() {
  String h = tr("<h3>Energia (HomeWizard, Growatt)</h3>"
                "<p style='color:#aaa;margin:0'>A ház energiaadatai: a HomeWizard P1 mérő (hálózat) és egy HomeWizard Energy "
                "Socket (pl. a boyler) a helyi hálózaton, a napelem a Growatt felhőből. A HomeWizard appban az eszközönként a "
                "<i>Local API</i>-t be kell kapcsolni. Üresen hagyott mező: az az adat nincs.</p>",
                "<h3>Energy (HomeWizard, Growatt)</h3>"
                "<p style='color:#aaa;margin:0'>The house's energy data: the HomeWizard P1 meter (grid) and a HomeWizard Energy "
                "Socket (e.g. the water heater) on the local network, solar from the Growatt cloud. In the HomeWizard app the "
                "<i>Local API</i> has to be switched on for each device. A field left empty: that data is not used.</p>");
  h += "<input type='hidden' name='en_f' value='1'><label style='display:block'><input type='checkbox' name='en_on'";
  if (en_cfg.on) h += " checked";
  h += " style='width:auto;margin:0 8px 0 0'>";
  h += tr("Bekapcsolva</label><label>P1 mérő címe (IP vagy gépnév)</label>", "On</label><label>P1 meter address (IP or host name)</label>");
  h += "<input name='en_p1' maxlength='64' pattern='[A-Za-z0-9.:-]*' value='"; h += html_attr(en_cfg.p1);
  h += tr("'><label>Energy Socket címe (boyler)</label>", "'><label>Energy Socket address (water heater)</label>");
  h += "<input name='en_sk' maxlength='64' pattern='[A-Za-z0-9.:-]*' value='"; h += html_attr(en_cfg.sk);
  h += tr("'><label>Growatt API token (üresen hagyva a mentett marad)</label>", "'><label>Growatt API token (left empty, the saved one stays)</label>");
  h += "<input type='password' name='en_gt' maxlength='64' autocomplete='off' placeholder='";
  if (en_cfg.gt[0]) {
    const size_t n = strlen(en_cfg.gt);
    h += tr("mentve, vége: ", "saved, ends with: ");
    h += html_attr(en_cfg.gt + (n > 4 ? n - 4 : 0));
  } else h += tr("nincs megadva", "not set");
  h += "'><label style='display:block'><input type='checkbox' name='en_gtdel' style='width:auto;margin:0 8px 0 0'>";
  h += tr("Mentett token törlése</label><label>Growatt szerver (üresen: " EN_GW_DEFAULT ")</label>",
          "Delete the saved token</label><label>Growatt server (empty: " EN_GW_DEFAULT ")</label>");
  h += "<input name='en_gs' maxlength='64' pattern='[A-Za-z0-9.-]*' value='"; h += html_attr(en_cfg.gs);
  h += tr("'><label>A napelemek teljesítménye, kWp (a napi diagram teteje; üresen: a mai termeléshez igazodik)</label>",
          "'><label>Solar panel power, kWp (top of the daily chart; empty: follows today's production)</label>");
  h += "<input name='en_kwp' type='number' step='0.1' min='0' max='1000' value='";
  if (en_cfg.kwp > 0) h += String(en_cfg.kwp, 1);
  h += tr("'><label>Napelem-adat továbbküldése másik kijelzőre (címek vesszővel)</label>",
          "'><label>Forward the solar data to other displays (addresses, comma separated)</label>");
  h += "<input name='en_fw' maxlength='128' pattern='[A-Za-z0-9.:, -]*' value='"; h += html_attr(en_cfg.fw);
  h += tr("'><p style='color:#aaa;margin:0'>A Growatt a sűrű lekérdezést elutasítja, ezért a tokent elég egyetlen kijelzőn megadni: "
          "az ide írt kijelzők tőle kapják a napelem adatát (pl. idojaras-p4.local, 192.168.1.50). Azokon az Energia legyen bekapcsolva, "
          "a token üres.</p>",
          "'><p style='color:#aaa;margin:0'>Growatt rejects frequent requests, so enter the token on one display only: the "
          "displays listed here get the solar data from it (e.g. idojaras-p4.local, 192.168.1.50). Energy must be on there; "
          "leave their token empty.</p>");
  return h;
}

// gépnév vagy IP (a http:// előtag és a záró / elhagyható); false: hibás, nem mentjük
static bool en_host_arg(WebServer &s, const char *name, char *out, size_t n) {
  String v = s.arg(name);
  v.trim();
  if (v.startsWith("http://")) v.remove(0, 7);
  else if (v.startsWith("https://")) v.remove(0, 8);
  while (v.endsWith("/")) v.remove(v.length() - 1);
  if (!v.isEmpty() && !fw_host_valid(v.c_str())) return false;
  strlcpy(out, v.c_str(), n);
  return true;
}

// A webes Beállítások mentésekor (csak ha az Energia rész az űrlapon volt)
static void en_settings_save(WebServer &s) {
  if (!s.hasArg("en_f")) return;
  EnCfg c = en_cfg;
  c.on = s.hasArg("en_on");
  en_host_arg(s, "en_p1", c.p1, sizeof(c.p1));
  en_host_arg(s, "en_sk", c.sk, sizeof(c.sk));
  en_host_arg(s, "en_gs", c.gs, sizeof(c.gs));
  if (s.hasArg("en_kwp")) {
    String k = s.arg("en_kwp");
    k.trim(); k.replace(',', '.');
    const float v = k.toFloat();
    c.kwp = (v > 0 && v <= 1000) ? v : 0.0f;
  }
  if (s.hasArg("en_fw")) {   // címek vesszővel / szóközzel; hibás cím esetén a régi marad
    String v = s.arg("en_fw");
    v.trim();
    String out;
    bool ok = v.length() < sizeof(c.fw);
    char tmp[130];
    strlcpy(tmp, v.c_str(), sizeof(tmp));
    for (char *tok = strtok(tmp, ", "); ok && tok; tok = strtok(NULL, ", ")) {
      String h = tok;
      if (h.startsWith("http://")) h.remove(0, 7);
      while (h.endsWith("/")) h.remove(h.length() - 1);
      if (!fw_host_valid(h.c_str())) { ok = false; break; }
      if (out.length()) out += ", ";
      out += h;
    }
    if (ok && out.length() < sizeof(c.fw)) strlcpy(c.fw, out.c_str(), sizeof(c.fw));
  }
  String t = s.arg("en_gt");
  t.trim();
  if (s.hasArg("en_gtdel")) c.gt[0] = 0;
  else if (!t.isEmpty() && t.length() < sizeof(c.gt)) {
    bool ok = true;
    for (size_t i = 0; i < t.length(); i++) if (!isalnum((unsigned char)t[i]) && t[i] != '-' && t[i] != '_') ok = false;
    if (ok) strlcpy(c.gt, t.c_str(), sizeof(c.gt));
  }
  {
    CamPause cam_pause_;   // P4: a kamera áll a mentés alatt
    Preferences p;
    p.begin("cfg", false);
    p.putBool("en_on", c.on);
    p.putString("en_p1", c.p1);
    p.putString("en_sk", c.sk);
    p.putString("en_gt", c.gt);
    p.putString("en_gs", c.gs);
    p.putFloat("en_kwp", c.kwp);
    p.putString("en_fw", c.fw);
    p.end();
  }
  const bool gw_changed = strcmp(c.gt, en_cfg.gt) != 0 || strcmp(c.gs, en_cfg.gs) != 0 || (c.on && !en_cfg.on);
  portENTER_CRITICAL(&en_mux);
  en_cfg = c;
  if (gw_changed) en_gw_force = true;
  if (!c.gt[0] && !en_data.gw_remote) { en_data.gw_ms = 0; en_data.gw_err[0] = 0; }   // a régi adat ne látsszon (a másik kijelzőtől kapott marad)
  if (!c.gt[0]) en_data.gw_err[0] = 0;
  if (!c.p1[0]) { en_data.p1_ms = 0; en_data.p1_err[0] = 0; }
  if (!c.sk[0]) { en_data.sk_ms = 0; en_data.sk_err[0] = 0; }
  portEXIT_CRITICAL(&en_mux);
  logout_printf("Energia: beállítások mentve (%s)\n", c.on ? "be" : "ki");
}
