// Energia: a ház energiaadatai (HomeWizard P1 mérő, HomeWizard Energy Socket, Growatt napelem-felhő), háttérfeladatból
// - hálózat (P1) és bojler (dugalj): helyi HTTP, Local API v1 (a HomeWizard appban kell bekapcsolni), 2 mp-enként
// - napelem: a Growatt OpenAPI (HTTPS, token a fejlécben), 5 percenként (a felhő ennél sűrűbben úgysem frissül)
// - a ház fogyasztása = napelem + hálózat (becslés, mert a napelem adata akár 5 perces)
// A beállítások (be/ki, címek, token) a webes kezelőoldalon, NVS "cfg" (en_on, en_p1, en_sk, en_gt, en_gs, en_gsl); alapból ki.
// A token és a címek nem kerülhetnek a kódba (a tároló nyilvános).
// (Az idojaras.ino része: az Arduino IDE és a PlatformIO a fájlokat ábécésorrendben, egy programmá fűzi össze. Ez a fájl
// közvetlenül az idojaras.ino után jön: a későbbi fájlok static függvényeit csak az alábbi előre deklarálással használja.)

static void logout_printf(const char *fmt, ...);     // idojaras_02 (a Naplóba)
static bool get_local_tm(struct tm &t);              // idojaras_03
static bool fw_host_valid(const char *h);            // idojaras_06b (gépnév vagy IP, esetleg :port)
static String html_attr(const char *s);              // idojaras_11
static void nt_push(const char *title, const char *msg, uint8_t prio, const char *tags);   // idojaras_17c
static bool is_night();                              // idojaras_03

constexpr uint32_t EN_LOCAL_MS = 2000;               // P1 és dugalj lekérése ilyen gyakran
constexpr uint32_t EN_LOCAL_ERR_MS = 10000;          // ha egyik sem válaszol: ennyi idő múlva újra
constexpr uint32_t EN_GW_MS = 5UL * 60UL * 1000UL;   // Growatt
constexpr uint32_t EN_GW_RETRY_MS = 60UL * 1000UL;   // Growatt hiba után
constexpr uint32_t EN_GW_RATE_MS = 10UL * 60UL * 1000UL;   // Growatt "túl sűrű lekérdezés" (10012) után
constexpr int EN_GW_ERR_RATE = 10012;                // error_frequently_access (több kijelző ugyanazzal a tokennel)
// Körforgás (kiesés elleni védelem, a felhasználó kérése): több kijelzőn ugyanaz a token, és felváltva kérdeznek: a kör
// EN_GW_RING_S, a kijelzők a slotjuk szerint (0, 1, 2) EN_GW_RING_S / 3 eltolással, a pontos (NTP-) idő szerint. Így a tokenre
// 5 perc 10 mp-enként jut egy kérés (az 5 perc a Growatt határa; a 10 mp ráhagyás), és ha mindegyik továbbküld a többinek
// (en_fw), mindenki ilyen gyakran kap friss adatot. Ha egy kijelző kiesik, a többi tovább kérdez (akkor ott 10 perces a rés).
constexpr uint32_t EN_GW_RING_S = 930, EN_GW_SLOT_WIN_S = 120;   // a kör hossza; a slot kezdete után ennyi ideig még indulhat
constexpr int EN_GW_SLOTS = 3;
constexpr uint32_t EN_BL_MS = 30000, EN_BL_RETRY_MS = 60000;   // bojler Tuya-hőfok: 30 mp-enként, hiba után 1 perc
constexpr uint32_t EN_BL_STALE_MS = 90000;
constexpr uint32_t EN_BL_AFTER_CMD_MS = 1500;          // írás után ennyivel kérdezzük le újra
// bojler napelemes automatika: a bojler mindig be van kapcsolva (nem Eco módban, mert az 55 °C-ra korlátoz), és a cél-hőfoka az
// alap (bl_t0); ha a napelem-többlet (visszatáplálás + a bojler saját fogyasztása) legalább bl_on_w, EN_BA_ON_MS óta, a napelemes
// (bl_t1); ha EN_BA_HYST_W-tal kevesebb, EN_BA_OFF_MS óta, újra az alap (hiszterézis: a felhők ne kapcsolgassák)
constexpr int EN_BA_DEF_W = 2000, EN_BA_HYST_W = 500, EN_BA_MIN_W = 250, EN_BA_MAX_W = 8000;
constexpr int EN_BA_DEF_T0 = 50, EN_BA_DEF_T1 = 75, EN_BA_T_MIN = 30, EN_BA_T_MAX = 75;
#define EN_BA_MODE "manmenu"   // az automatika üzemmódja (Smart: a cél-hőfokot tartja; az Azonnali csak egyszer fűt fel)
constexpr uint32_t EN_BA_ON_MS = 2UL * 60UL * 1000UL, EN_BA_OFF_MS = 5UL * 60UL * 1000UL, EN_BA_RETRY_MS = 60000;
// reggeli Eco-ablak (a felhasználó kérése): minden nap 5:00-6:00 Eco módra vált (az 55 °C-os korlátig felfűt, még az
// olcsóbb tarifában), 6:00-kor vissza Smart módra, az alap cél-hőfokra; a napelemes cél-hőfok elsőbbséget kap
constexpr int EN_BA_MORN_H0 = 5, EN_BA_MORN_H1 = 6;
// napi Anti-bakteriális ciklus (a felhasználó kérése): ha legalább ennyi a napelem-többlet, ennyi ideig folyamatosan,
// naponta egyszer elindítja; utána a bojler saját logikája 80 °C-ra fűt, 30 perc hőkonzerválás után magától visszaáll
constexpr int EN_BA_HW_W = 3000;
constexpr uint32_t EN_BA_HW_ON_MS = 5UL * 60UL * 1000UL;
// a ciklus megszakítása, ha közben tartósan a hálózatból fűt (a felhasználó kérése: ha a napelem elapad, ne a
// hálózatból fűtsön 80 °C-ra): ennyi vétel (W) ennyi ideig folyamatosan -> vissza Smart módra
constexpr float EN_BA_HW_ABORT_W = 500.0f;
constexpr uint32_t EN_BA_HW_ABORT_MS = 10UL * 60UL * 1000UL;
// Bojler-vezérlő (főnök): egyetlen kijelző futtatja az automatikát (bl_boss üres); a többin (távirányító, bl_boss = a vezérlő címe)
// a Bojler fül parancsai a vezérlő /bojler címére mennek, és onnan kérdezik le 10 mp-enként az automatika állapotát. Ha a vezérlő
// nem elérhető, a Be/Ki, az üzemmód és a cél-hőfok közvetlenül a bojlerhez megy; az automatika ilyenkor nem állítható.
constexpr uint32_t EN_RB_MS = 10000, EN_RB_STALE_MS = 35000;
constexpr int EN_BL_CONNECT_MS = 3500;   // a Tuya-eszköz lassabban fogadja a kapcsolódást, mint a helyi HTTP-eszközök
constexpr uint32_t EN_LOCAL_STALE_MS = 15000;        // ennél régebbi P1-/dugalj-adat már nem érvényes
constexpr uint32_t EN_GW_STALE_MS = 20UL * 60UL * 1000UL;
constexpr int EN_CONNECT_MS = 1500, EN_TIMEOUT_MS = 2500;      // helyi eszközök
constexpr int EN_GW_TIMEOUT_MS = 10000;
constexpr float EN_OVERVOLT_V = 253.0f;              // túlfeszültség: 230 V + 10 %
#define EN_GW_DEFAULT "openapi.growatt.com"          // európai szerver

// beállítások (a főszál írja mentéskor, a háttérfeladat en_mux alatt másolja) és a mért adatok (EnCfg, EnData: idojaras.ino)
static EnCfg en_cfg = {};
static EnData en_data = {};
static portMUX_TYPE en_mux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t en_task_h = NULL;
static volatile bool en_gw_force = false;   // új beállítás: a Growattot azonnal kérdezzük

// Mai fogyasztás: a mérőállások a nap elején (a főszál kezeli, NVS "cfg"/"en_day", naponta egy írás). key: év*400 + az év
// napja; since: ettől a perctől számol (0: éjféltől; ha a kijelző napközben indul először, az indulás ideje); NAN: még nincs.
struct EnDay { int32_t key, since; float imp, exp, boil; };
static EnDay en_day = {-1, 0, NAN, NAN, NAN};

// A mai nap 5 perces szeletekben (a napi diagramhoz): w = a napelem termelése (W, a Growatt-lekérésekből, a felhő időbélyege
// szerint), net = a hálózati teljesítmény átlaga (W, + vétel, - visszatáplálás, a P1-ből), boil = a bojler teljesítményének
// átlaga (W, a dugalj mintáiból). FFat "/energia.bin" (a többi előzményfájltól külön), szeletenként mentve. A mai nap
// kulcsa (key) szerint törlődik.
constexpr int EN_PV_N = 288;
constexpr uint16_t EN_PV_NONE = 0xFFFF;
constexpr int16_t EN_NET_NONE = INT16_MIN;
constexpr uint32_t EN_PV_MAGIC = 0x33564E45;   // "ENV3" (az ENV2 a bojlert még nem tárolta; azt a betöltés eldobja)
#define EN_PV_FILE "/energia.bin"
struct EnPvDay { uint32_t magic; int32_t key; uint16_t w[EN_PV_N]; int16_t net[EN_PV_N], boil[EN_PV_N]; };
static EnPvDay en_pv = {};
static int en_pv_last = -1;         // az utolsó rögzített termelés-szelet (a kimaradt szeletek kitöltéséhez)
static uint32_t en_pv_ver = 1;      // változáskor nő (a diagram ebből tudja, hogy újra kell rajzolni)

static void en_pv_clear(int32_t key) {
  en_pv.magic = EN_PV_MAGIC;
  en_pv.key = key;
  for (uint16_t &w : en_pv.w) w = EN_PV_NONE;
  for (int16_t &n : en_pv.net) n = EN_NET_NONE;
  for (int16_t &b : en_pv.boil) b = EN_NET_NONE;
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

// A hálózati teljesítmény és a bojler teljesítményének 5 perces átlaga a mintáikból: a szelet végén mentve, közben
// 30 mp-enként az aktuális (részleges) átlag kerül a diagramra (mentés nélkül).
static void en_net_task(const EnData &d) {
  struct tm t;
  if (!get_local_tm(t)) return;
  const int32_t key = (t.tm_year + 1900) * 400 + t.tm_yday;
  const int s = (t.tm_hour * 60 + t.tm_min) / 5;
  static int32_t acc_key = -1;
  static int acc_slice = -1, n = 0, bn = 0;
  static double sum = 0, bsum = 0;
  static uint32_t last_p1 = 0, last_sk = 0, last_live = 0;
  if (key != acc_key || s != acc_slice) {   // új szelet: az előző végleges értéke (ha ugyanarra a napra esik)
    if ((n > 0 || bn > 0) && acc_key == en_pv.key && acc_slice >= 0) {
      if (n > 0) en_pv.net[acc_slice] = (int16_t)constrain((int)lroundf(sum / n), -32000, 32000);
      if (bn > 0) en_pv.boil[acc_slice] = (int16_t)constrain((int)lroundf(bsum / bn), 0, 32000);
      en_pv_ver++;
      en_pv_save();
    }
    acc_key = key; acc_slice = s; sum = 0; n = 0; bsum = 0; bn = 0;
  }
  if (en_fresh(d.p1_ms, EN_LOCAL_STALE_MS) && d.p1_ms != last_p1) { last_p1 = d.p1_ms; sum += d.grid_w; n++; }
  if (en_fresh(d.sk_ms, EN_LOCAL_STALE_MS) && d.sk_ms != last_sk) { last_sk = d.sk_ms; bsum += d.boil_w; bn++; }
  if ((n > 0 || bn > 0) && millis() - last_live >= 30000) {
    last_live = millis();
    en_pv_day(key);
    if (n > 0) en_pv.net[s] = (int16_t)constrain((int)lroundf(sum / n), -32000, 32000);   // n == 0: még csak a dugaljtól jött adat (újraindulás után)
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

// A ház saját (a bojler nélküli) becsült fogyasztása (W): napelem + hálózat, mínusz a bojler (azt külön mutatjuk);
// NAN, ha a hálózat vagy a napelem adata hiányzik. A bojlert csak akkor vonjuk le, ha annak adata is friss.
static float en_house_w(const EnData &d) {
  if (!en_fresh(d.p1_ms, EN_LOCAL_STALE_MS) || !en_fresh(d.gw_ms, EN_GW_STALE_MS)) return NAN;
  float w = d.pv_w + d.grid_w;
  if (en_fresh(d.sk_ms, EN_LOCAL_STALE_MS)) w -= d.boil_w;
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

// ---- Bojler: Tuya helyi protokoll (3.3), titkosítva ----
// A konzol a WiFi-modul (Tuya) saját, helyi TCP-portján (6668) válaszol, AES-128-ECB-vel titkosítva a helyi kulccsal
// (local_key, a Tuya fiókból, NVS-ben, nem a kódban). Keret: 0x000055AA + seq(4) + cmd(4) + hossz(4) + titkosított
// payload + CRC32(4, a fejléc+payload fölött) + 0x0000AA55. A payload JSON {"gwId","devId","uid","t","dps":{}},
// PKCS7-gyel 16 bájtra igazítva. A válasz fejléce után egy 4 bájtos visszatérési kód jön (0: siker), utána a
// titkosított JSON. DP 1: be/ki, 2: üzemmód, 9: cél-hőfok (°C), 10: hőfok (°C), 13: állapot (standby/heating/warm),
// 14: kivehető melegvíz (%). Írás (CONTROL, cmd 0x07): a titkosított payload elé egy nem titkosított "3.3"+12 nulla bájt kell.
constexpr int EN_BL_PORT = 6668;
constexpr uint32_t EN_BL_CMD_CONTROL = 0x07, EN_BL_CMD_QUERY = 0x0A;
constexpr int EN_BL_VER_HDR = 15;   // "3.3" + 12 nulla bájt: a CONTROL titkosított payloadja előtt kell (a DP_QUERY-nél nincs)

// a bojler üzemmódjai (DP 2): a gyártói név és a kijelzőn mutatott név
constexpr int EN_BL_MODES = 4;
static const char *const EN_BL_MODE_ID[EN_BL_MODES] = {"jiheat", "eco", "manmenu", "highwarm"};
static const char *const EN_BL_MODE_T[2][EN_BL_MODES] = {{"Azonnali", "Eco", "Smart", "Anti-bakt."}, {"Instant", "Eco", "Smart", "Anti-bact."}};
static int en_bl_mode_idx(const char *m) {
  for (int i = 0; i < EN_BL_MODES; i++) if (strcmp(m, EN_BL_MODE_ID[i]) == 0) return i;
  return -1;
}

static uint32_t en_crc32(const uint8_t *data, size_t len) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320 & (-(int32_t)(crc & 1)));
  }
  return ~crc;
}

// AES-128-ECB, PKCS7-igazítással; enc: titkosítás (in_len-ből a kipufferelt, 16-ra kerekített méretet adja vissza),
// dec: visszafejtés (a padding levágva); 0: hiba (kicsi a puffer)
static int en_aes_ecb(bool enc, const uint8_t *key, const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap) {
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  int ret = 0;
  if (enc) {
    const size_t pad_len = ((in_len / 16) + 1) * 16;
    if (pad_len > out_cap) { mbedtls_aes_free(&ctx); return 0; }
    memcpy(out, in, in_len);
    const uint8_t pad = (uint8_t)(pad_len - in_len);
    for (size_t i = in_len; i < pad_len; i++) out[i] = pad;
    mbedtls_aes_setkey_enc(&ctx, key, 128);
    for (size_t i = 0; i < pad_len; i += 16) mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, out + i, out + i);
    ret = (int)pad_len;
  } else {
    if (in_len % 16 != 0 || in_len == 0 || in_len > out_cap) { mbedtls_aes_free(&ctx); return 0; }
    mbedtls_aes_setkey_dec(&ctx, key, 128);
    for (size_t i = 0; i < in_len; i += 16) mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_DECRYPT, in + i, out + i);
    const uint8_t pad = out[in_len - 1];
    ret = (pad > 0 && pad <= 16 && (size_t)pad <= in_len) ? (int)(in_len - pad) : (int)in_len;
  }
  mbedtls_aes_free(&ctx);
  return ret;
}

// Egy Tuya-kérés (a háttérfeladatban): kapcsolódás a 6668-as porton, a keret elküldése, a válasz első keretének beolvasása.
// cmd: EN_BL_CMD_QUERY (dps: NULL) vagy EN_BL_CMD_CONTROL (dps: az írandó DP-k a JSON-objektum belsejeként, pl. "\"1\":true").
// out: a visszafejtett JSON; üres, ha a válaszban nincs adat (pl. a CONTROL nyugtája).
static bool en_tuya_xchg(const EnCfg &c, uint32_t cmd, const char *dps, char *out, size_t out_n, char *err, size_t err_n) {
  out[0] = 0;
  WiFiClient cli;
  cli.setTimeout(EN_TIMEOUT_MS);
  if (!cli.connect(c.bl, EN_BL_PORT, EN_BL_CONNECT_MS)) { strlcpy(err, tr("nem elérhető", "unreachable"), err_n); return false; }
  char js[192];
  const unsigned long t = (unsigned long)time(NULL);
  if (cmd == EN_BL_CMD_QUERY) snprintf(js, sizeof(js), "{\"gwId\":\"%s\",\"devId\":\"%s\",\"uid\":\"\",\"t\":\"%lu\",\"dps\":{}}", c.bl_id, c.bl_id, t);
  else snprintf(js, sizeof(js), "{\"devId\":\"%s\",\"uid\":\"\",\"t\":\"%lu\",\"dps\":{%s}}", c.bl_id, t, dps);
  const int hdr = cmd == EN_BL_CMD_CONTROL ? EN_BL_VER_HDR : 0;
  uint8_t pkt[16 + EN_BL_VER_HDR + 208 + 8];
  const int enc_len = en_aes_ecb(true, (const uint8_t *)c.bl_key, (const uint8_t *)js, strlen(js), pkt + 16 + hdr, 208);
  if (enc_len <= 0) { cli.stop(); strlcpy(err, tr("belső hiba", "internal error"), err_n); return false; }
  if (hdr) { memset(pkt + 16, 0, hdr); memcpy(pkt + 16, "3.3", 3); }
  const uint32_t length = hdr + enc_len + 8;
  pkt[0] = 0x00; pkt[1] = 0x00; pkt[2] = 0x55; pkt[3] = 0xAA;
  memset(pkt + 4, 0, 4);   // seq
  pkt[8] = pkt[9] = pkt[10] = 0; pkt[11] = (uint8_t)cmd;
  pkt[12] = (uint8_t)(length >> 24); pkt[13] = (uint8_t)(length >> 16); pkt[14] = (uint8_t)(length >> 8); pkt[15] = (uint8_t)length;
  const uint32_t crc = en_crc32(pkt, 16 + hdr + enc_len);
  uint8_t *s = pkt + 16 + hdr + enc_len;
  s[0] = (uint8_t)(crc >> 24); s[1] = (uint8_t)(crc >> 16); s[2] = (uint8_t)(crc >> 8); s[3] = (uint8_t)crc;
  s[4] = 0x00; s[5] = 0x00; s[6] = 0xAA; s[7] = 0x55;
  cli.write(pkt, 16 + hdr + enc_len + 8);
  cli.flush();
  // a válasz: előbb a fejléc (benne a hossz), utána a keret többi része
  uint8_t resp[320];
  size_t got = 0, need = 16;
  const uint32_t t0 = millis();
  while (got < need && millis() - t0 < (uint32_t)EN_TIMEOUT_MS) {
    if (!cli.available()) { delay(5); continue; }
    const int n = cli.read(resp + got, need - got);
    if (n > 0) got += n;
    if (got == 16 && need == 16) {
      const uint32_t rlen = ((uint32_t)resp[12] << 24) | ((uint32_t)resp[13] << 16) | ((uint32_t)resp[14] << 8) | resp[15];
      need = 16 + (rlen < sizeof(resp) - 16 ? rlen : sizeof(resp) - 16);
    }
  }
  cli.stop();
  if (got < 16 || resp[0] != 0x00 || resp[1] != 0x00 || resp[2] != 0x55 || resp[3] != 0xAA) {
    strlcpy(err, tr("hibás válasz", "invalid response"), err_n);
    return false;
  }
  const uint32_t rlen = ((uint32_t)resp[12] << 24) | ((uint32_t)resp[13] << 16) | ((uint32_t)resp[14] << 8) | resp[15];
  if (16 + rlen > got || rlen < 12) { strlcpy(err, tr("csonka válasz", "truncated response"), err_n); return false; }
  const uint8_t *body = resp + 16;   // 4 bájt visszatérési kód + (titkosított) JSON + CRC + vég
  const uint32_t rc = ((uint32_t)body[0] << 24) | ((uint32_t)body[1] << 16) | ((uint32_t)body[2] << 8) | body[3];
  if (rc != 0) { snprintf(err, err_n, tr("a bojler elutasította (%u)", "rejected by the heater (%u)"), (unsigned)rc); return false; }
  const uint8_t *enc2 = body + 4;
  size_t enc2_len = rlen - 12;
  if (enc2_len >= (size_t)EN_BL_VER_HDR && memcmp(enc2, "3.3", 3) == 0) { enc2 += EN_BL_VER_HDR; enc2_len -= EN_BL_VER_HDR; }
  if (enc2_len == 0) return true;
  const int dec_len = en_aes_ecb(false, (const uint8_t *)c.bl_key, enc2, enc2_len, (uint8_t *)out, out_n - 1);
  if (dec_len <= 0) { out[0] = 0; strlcpy(err, tr("visszafejtési hiba (rossz kulcs?)", "decryption error (wrong key?)"), err_n); return false; }
  out[dec_len] = 0;
  return true;
}

// DP 1 (be/ki) a JSON-ból: 1 / 0, -1: nincs benne
static int8_t en_bl_sw(const char *js) {
  const char *p = en_json_val(js, "1");
  return !p ? -1 : *p == 't' ? 1 : *p == 'f' ? 0 : -1;
}

// A bojler lekérdezése: hőfok, cél-hőfok, be/ki, állapot, üzemmód, kivehető melegvíz
static bool en_fetch_boiler(const EnCfg &c) {
  char err[48] = "", js[256];
  float temp = NAN, set = NAN, water_pct = NAN;
  char state[16] = "", mode[16] = "";
  int8_t sw = -1;
  int fault = -1;
  bool ok = en_tuya_xchg(c, EN_BL_CMD_QUERY, NULL, js, sizeof(js), err, sizeof(err));
  if (ok) {
    temp = en_json_f(js, "10");
    set = en_json_f(js, "9");
    sw = en_bl_sw(js);
    en_json_str(js, "13", state, sizeof(state));
    en_json_str(js, "2", mode, sizeof(mode));
    water_pct = en_json_f(js, "14");
    double f;
    if (en_json_num(js, "20", f)) fault = (int)f;
    ok = !isnan(temp);
    if (!ok) strlcpy(err, tr("hiányzó adat", "missing data"), sizeof(err));
  }
  portENTER_CRITICAL(&en_mux);
  if (ok) {
    en_data.boil_temp = temp;
    en_data.boil_set = set;
    en_data.boil_sw = sw;
    en_data.boil_fault = fault;
    en_data.boil_water_pct = water_pct;
    strlcpy(en_data.boil_state, state, sizeof(en_data.boil_state));
    strlcpy(en_data.boil_mode, mode, sizeof(en_data.boil_mode));
    en_data.bl_ms = millis();
  }
  strlcpy(en_data.bl_err, err, sizeof(en_data.bl_err));
  portEXIT_CRITICAL(&en_mux);
  return ok;
}

// Írás a bojlerbe (vezérlés): a főszál sorba teszi (en_boiler_cmd), a háttérfeladat küldi (en_boiler_control), az eredményt a
// főszál írja a Naplóba (en_loop). Sikeres írás után az új értéket rögtön átvesszük (a következő lekérdezés megerősíti).
constexpr int EN_BL_Q = 4, EN_BL_CMD_LEN = 40;
static char en_bl_q[EN_BL_Q][EN_BL_CMD_LEN];
static int en_bl_qn = 0;
static char en_bl_res[96] = "";   // az utolsó írás eredménye (a Naplóba)
static uint32_t en_bl_res_n = 0;

static void en_boiler_cmd(const char *dps) {
  bool full;
  portENTER_CRITICAL(&en_mux);
  full = en_bl_qn >= EN_BL_Q;
  if (!full) strlcpy(en_bl_q[en_bl_qn++], dps, EN_BL_CMD_LEN);
  portEXIT_CRITICAL(&en_mux);
  if (full) logout_printf("Bojler: az írás elmaradt (teli a sor): %s\n", dps);
}

static void en_boiler_control(const EnCfg &c, const char *dps) {
  char err[48] = "", js[256], tmp[EN_BL_CMD_LEN + 2];
  const bool ok = en_tuya_xchg(c, EN_BL_CMD_CONTROL, dps, js, sizeof(js), err, sizeof(err));
  snprintf(tmp, sizeof(tmp), "{%s}", dps);
  portENTER_CRITICAL(&en_mux);
  if (ok) {
    const int8_t sw = en_bl_sw(tmp);
    if (sw >= 0) en_data.boil_sw = sw;
    const float set = en_json_f(tmp, "9");
    if (!isnan(set)) en_data.boil_set = set;
    en_json_str(tmp, "2", en_data.boil_mode, sizeof(en_data.boil_mode));
  }
  snprintf(en_bl_res, sizeof(en_bl_res), "%s: %s", dps, ok ? "OK" : err);
  en_bl_res_n++;
  portEXIT_CRITICAL(&en_mux);
}

// "kulcs=érték" a vezérlő válaszából (pl. "auto=1&on_w=2000"); false: nincs benne
static bool en_form_int(const char *b, const char *key, int &out) {
  const size_t n = strlen(key);
  for (const char *p = b; (p = strstr(p, key)) != NULL; p += n)
    if ((p == b || p[-1] == '&') && p[n] == '=') { out = atoi(p + n + 1); return true; }
  return false;
}

// HTTP-kérés a vezérlő kijelző /bojler címére (body == NULL: GET, különben POST); a válasz szövege a resp-be
static bool en_boss_http(const EnCfg &c, const char *body, char *resp, size_t resp_n, char *err, size_t err_n) {
  char url[96];
  snprintf(url, sizeof(url), "http://%s/bojler", c.bl_boss);
  WiFiClient cli;
  HTTPClient http;
  http.setConnectTimeout(EN_CONNECT_MS);
  http.setTimeout(EN_TIMEOUT_MS);
  http.setReuse(false);
  resp[0] = 0;
  if (!http.begin(cli, url)) { strlcpy(err, tr("hibás cím", "invalid address"), err_n); return false; }
  int code;
  if (body) {
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    code = http.POST((uint8_t *)body, strlen(body));
  } else code = http.GET();
  if (code == 200) strlcpy(resp, http.getString().c_str(), resp_n);
  else en_http_err(code, err, err_n);
  http.end();
  return code == 200;
}

// a vezérlő automatikájának lekérése (távirányító kijelzőn, a háttérfeladatban)
static bool en_boss_get(const EnCfg &c) {
  char resp[96], err[48] = "";
  int au = 0, w = 0, t0 = EN_BA_DEF_T0, t1 = EN_BA_DEF_T1;
  bool ok = en_boss_http(c, NULL, resp, sizeof(resp), err, sizeof(err));
  if (ok && !(en_form_int(resp, "auto", au) && en_form_int(resp, "on_w", w))) { ok = false; strlcpy(err, tr("hibás válasz", "invalid response"), sizeof(err)); }
  if (ok) { en_form_int(resp, "t0", t0); en_form_int(resp, "t1", t1); }
  portENTER_CRITICAL(&en_mux);
  if (ok) { en_data.rb_auto = au != 0; en_data.rb_on_w = w; en_data.rb_t0 = t0; en_data.rb_t1 = t1; en_data.rb_ms = millis(); }
  strlcpy(en_data.rb_err, err, sizeof(en_data.rb_err));
  portEXIT_CRITICAL(&en_mux);
  return ok;
}

// egy parancs a vezérlőnek: "@auto=1&on_w=2000" (az automatika) vagy DP-írás ("\"1\":true" -> cmd=...); a DP-írás, ha a
// vezérlő nem elérhető, közvetlenül a bojlerhez megy
static void en_boss_cmd(const EnCfg &c, const char *cmd) {
  char body[3 * EN_BL_CMD_LEN + 8], resp[96], err[48] = "";   // (az automatika parancsa @-cal: EN_BL_CMD_LEN-be fér)
  if (cmd[0] == '@') strlcpy(body, cmd + 1, sizeof(body));
  else {
    int n = snprintf(body, sizeof(body), "cmd=");
    for (const char *q = cmd; *q && n < (int)sizeof(body) - 4; q++) {
      if (isalnum((unsigned char)*q)) body[n++] = *q;
      else n += snprintf(body + n, sizeof(body) - n, "%%%02X", (unsigned char)*q);
    }
    body[n] = 0;
  }
  const bool ok = en_boss_http(c, body, resp, sizeof(resp), err, sizeof(err));
  if (!ok && cmd[0] != '@' && c.bl[0] && c.bl_id[0] && c.bl_key[0]) { en_boiler_control(c, cmd); return; }   // tartalék: közvetlenül
  int au, w, t0, t1;
  portENTER_CRITICAL(&en_mux);
  if (ok && en_form_int(resp, "auto", au) && en_form_int(resp, "on_w", w)) {
    en_data.rb_auto = au != 0; en_data.rb_on_w = w; en_data.rb_ms = millis();
    if (en_form_int(resp, "t0", t0) && en_form_int(resp, "t1", t1)) { en_data.rb_t0 = t0; en_data.rb_t1 = t1; }
  }
  if (!ok) strlcpy(en_data.rb_err, err, sizeof(en_data.rb_err));
  snprintf(en_bl_res, sizeof(en_bl_res), "%s -> %s: %s", cmd, c.bl_boss, ok ? "OK" : err);
  en_bl_res_n++;
  portEXIT_CRITICAL(&en_mux);
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
  uint32_t next_local = 0, next_bl = 0, next_rb = 0, gw_cycle = UINT32_MAX;
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
    const bool bl_ok = c.bl[0] && c.bl_id[0] && c.bl_key[0];
    if (bl_ok || c.bl_boss[0]) {
      char cmd[EN_BL_CMD_LEN];
      bool have = false;
      portENTER_CRITICAL(&en_mux);
      if (en_bl_qn) {
        strlcpy(cmd, en_bl_q[0], sizeof(cmd));
        memmove(en_bl_q[0], en_bl_q[1], sizeof(en_bl_q[0]) * (EN_BL_Q - 1));
        en_bl_qn--;
        have = true;
      }
      portEXIT_CRITICAL(&en_mux);
      if (have) {
        if (c.bl_boss[0]) en_boss_cmd(c, cmd);   // távirányító: a vezérlőn keresztül
        else if (cmd[0] != '@') en_boiler_control(c, cmd);
        next_bl = millis() + EN_BL_AFTER_CMD_MS;   // utána hamar lekérdezzük (megerősítés)
      } else if (c.bl_boss[0] && (int32_t)(millis() - next_rb) >= 0) {
        en_boss_get(c);
        next_rb = millis() + EN_RB_MS;
      } else if (bl_ok && (int32_t)(millis() - next_bl) >= 0) {
        const uint32_t t0 = millis();
        const bool ok = en_fetch_boiler(c);
        next_bl = t0 + (ok ? EN_BL_MS : EN_BL_RETRY_MS);
      }
    }
    if (c.gt[0] && c.gw_slot >= 0) {   // körforgás: csak a saját slotunkban, körönként egyszer
      const time_t now = time(NULL);
      if (now > 1700000000) {   // csak pontos idővel (a slot az óra szerint)
        const uint32_t t = (uint32_t)now - (uint32_t)c.gw_slot * (EN_GW_RING_S / EN_GW_SLOTS);
        const uint32_t cycle = t / EN_GW_RING_S;
        if (t % EN_GW_RING_S < EN_GW_SLOT_WIN_S && cycle != gw_cycle) {
          gw_cycle = cycle;
          if (en_fetch_growatt(c)) en_fw_send(c);
        }
      }
    } else if (c.gt[0] && (force || (int32_t)(millis() - next_gw) >= 0)) {
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
  c.gw_slot = p.isKey("en_gsl") ? (int8_t)constrain(p.getChar("en_gsl", -1), -1, EN_GW_SLOTS - 1) : -1;
  c.kwp = pref_flt(p, "en_kwp", 0.0f);
  strlcpy(c.fw, pref_str(p, "en_fw", "").c_str(), sizeof(c.fw));
  strlcpy(c.bl, pref_str(p, "en_bl", "").c_str(), sizeof(c.bl));
  strlcpy(c.bl_id, pref_str(p, "en_blid", "").c_str(), sizeof(c.bl_id));
  strlcpy(c.bl_key, pref_str(p, "en_blk", "").c_str(), sizeof(c.bl_key));
  c.bl_auto = p.isKey("en_ba") && p.getBool("en_ba", false);
  c.bl_on_w = p.isKey("en_bth") ? constrain(p.getInt("en_bth", EN_BA_DEF_W), EN_BA_MIN_W, EN_BA_MAX_W) : EN_BA_DEF_W;
  strlcpy(c.bl_boss, pref_str(p, "en_bbo", "").c_str(), sizeof(c.bl_boss));
  c.bl_t0 = p.isKey("en_bt0") ? constrain(p.getInt("en_bt0", EN_BA_DEF_T0), EN_BA_T_MIN, EN_BA_T_MAX) : EN_BA_DEF_T0;
  c.bl_t1 = p.isKey("en_bt1") ? constrain(p.getInt("en_bt1", EN_BA_DEF_T1), EN_BA_T_MIN, EN_BA_T_MAX) : EN_BA_DEF_T1;
  EnDay day;
  if (p.isKey("en_day") && p.getBytesLength("en_day") == sizeof(day) && p.getBytes("en_day", &day, sizeof(day)) == sizeof(day)) en_day = day;
  p.end();
  portENTER_CRITICAL(&en_mux);
  en_cfg = c;
  en_data.pv_day = en_data.pv_month = en_data.pv_year = NAN;   // amíg nincs napelem-adat, "--" (nem 0)
  en_data.boil_set = NAN;
  en_data.boil_sw = -1;
  en_data.boil_fault = -1;
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

// A bojler automatikájának beállítása (a kijelzőn): azonnal érvényes, a mentés külön (en_bl_auto_save)
static void en_bl_auto_set(bool on, int w, int t0, int t1) {
  portENTER_CRITICAL(&en_mux);
  en_cfg.bl_auto = on;
  en_cfg.bl_on_w = constrain(w, EN_BA_MIN_W, EN_BA_MAX_W);
  en_cfg.bl_t0 = constrain(t0, EN_BA_T_MIN, EN_BA_T_MAX);
  en_cfg.bl_t1 = constrain(t1, en_cfg.bl_t0, EN_BA_T_MAX);   // a napelemes nem lehet kisebb az alapnál
  portEXIT_CRITICAL(&en_mux);
}
static void en_bl_auto_save() {
  CamPause cam_pause_;   // P4: a kamera áll a mentés alatt
  Preferences p;
  p.begin("cfg", false);
  p.putBool("en_ba", en_cfg.bl_auto);
  p.putInt("en_bth", en_cfg.bl_on_w);
  p.putInt("en_bt0", en_cfg.bl_t0);
  p.putInt("en_bt1", en_cfg.bl_t1);
  p.end();
}

// Az automatika hatályos állapota a kijelzőn: a vezérlőn a sajátja, a távirányítón a vezérlőtől kapott (-1: nem tudjuk,
// a vezérlő nem elérhető)
static bool en_bl_remote() { return en_cfg.bl_boss[0] != 0; }
static int en_bl_eff_auto(const EnData &d) {
  if (!en_bl_remote()) return en_cfg.bl_auto;
  return en_fresh(d.rb_ms, EN_RB_STALE_MS) ? d.rb_auto : -1;
}
static int en_bl_eff_on_w(const EnData &d) { return en_bl_remote() ? d.rb_on_w : en_cfg.bl_on_w; }
static int en_bl_eff_t0(const EnData &d) { return en_bl_remote() ? d.rb_t0 : en_cfg.bl_t0; }
static int en_bl_eff_t1(const EnData &d) { return en_bl_remote() ? d.rb_t1 : en_cfg.bl_t1; }

// Az automatika váltása a kijelző Bojler füléről: a vezérlőn helyben (true: menteni kell), a távirányítón parancs a vezérlőnek
static bool en_bl_auto_change(bool on, int w, int t0, int t1) {
  w = constrain(w, EN_BA_MIN_W, EN_BA_MAX_W);
  t0 = constrain(t0, EN_BA_T_MIN, EN_BA_T_MAX);
  t1 = constrain(t1, t0, EN_BA_T_MAX);
  if (!en_bl_remote()) { en_bl_auto_set(on, w, t0, t1); return true; }
  char cmd[EN_BL_CMD_LEN];
  snprintf(cmd, sizeof(cmd), "@auto=%d&on_w=%d&t0=%d&t1=%d", on ? 1 : 0, w, t0, t1);
  en_boiler_cmd(cmd);
  portENTER_CRITICAL(&en_mux);
  en_data.rb_auto = on; en_data.rb_on_w = w; en_data.rb_t0 = t0; en_data.rb_t1 = t1;   // azonnal látsszon (a válasz megerősíti)
  portEXIT_CRITICAL(&en_mux);
  return false;
}

// A vezérlő kijelző végpontja: GET /bojler -> "auto=1&on_w=2000&t0=50&t1=75"; POST /bojler cmd=<DP-írás> és/vagy auto=0|1,
// on_w=<W>, t0=, t1=<°C>
// (a távirányító kijelzőktől). Csak a vezérlőn (bl_boss üres) fogad parancsot.
static void handle_bojler() {
  WebServer &s = my_server;
  if (!en_cfg.on || g_demo) { s.send(409, "text/plain; charset=utf-8", "Az Energia ki van kapcsolva."); return; }
  if (s.method() == HTTP_POST) {
    if (en_cfg.bl_boss[0]) { s.send(409, "text/plain; charset=utf-8", "Nem ez a bojler vezérlője."); return; }
    const String from = s.client().remoteIP().toString();
    if (s.hasArg("cmd")) {
      const String c = s.arg("cmd");
      bool ok = c.length() > 0 && c.length() < EN_BL_CMD_LEN;
      for (size_t i = 0; ok && i < c.length(); i++) if (!isalnum((unsigned char)c[i]) && !strchr("\":,", c[i])) ok = false;
      if (!ok) { s.send(400, "text/plain", "bad cmd"); return; }
      en_boiler_cmd(c.c_str());
      logout_printf("Bojler: írás %s (a %s kijelzőtől)\n", c.c_str(), from.c_str());
    }
    if (s.hasArg("auto") || s.hasArg("on_w") || s.hasArg("t0") || s.hasArg("t1")) {
      const bool au = s.hasArg("auto") ? s.arg("auto").toInt() != 0 : en_cfg.bl_auto;
      const int w = s.hasArg("on_w") ? (int)s.arg("on_w").toInt() : en_cfg.bl_on_w;
      const int t0 = s.hasArg("t0") ? (int)s.arg("t0").toInt() : en_cfg.bl_t0, t1 = s.hasArg("t1") ? (int)s.arg("t1").toInt() : en_cfg.bl_t1;
      en_bl_auto_set(au, w, t0, t1);
      en_bl_auto_save();
      logout_printf("Bojler automatika: %s, %d W, %d/%d °C (a %s kijelzőtől)\n", au ? "be" : "ki", en_cfg.bl_on_w, en_cfg.bl_t0, en_cfg.bl_t1, from.c_str());
    }
  }
  char b[64];
  snprintf(b, sizeof(b), "auto=%d&on_w=%d&t0=%d&t1=%d", en_cfg.bl_auto ? 1 : 0, en_cfg.bl_on_w, en_cfg.bl_t0, en_cfg.bl_t1);
  s.send(200, "text/plain", b);
}

// A bojler napelemes automatikája (másodpercenként; csak a vezérlőn): a bojlert bekapcsolva és nem Eco módban tartja (Eco-ban a
// bojler 55 °C-ra korlátoz), a cél-hőfoka az alap (bl_t0), napelem-többletnél a napelemes (bl_t1), hiszterézissel (EN_BA_*).
// A többlet = visszatáplálás + a bojler saját fogyasztása (amit fűtés közben elvesz a visszatáplálásból). Friss hálózati és
// bojler-adat nélkül nem ír. Egy körben legfeljebb egy írás, utána EN_BA_RETRY_MS-ig vár (amíg a lekérdezés megerősíti).
static float en_ba_avail = NAN;   // a legutóbbi többlet (W; NAN: nem számolható), a Beállításokban látszik
static int8_t en_ba_solar = -1;   // napelemes cél-hőfok van-e érvényben (-1: még nem tudjuk; az első adatból: cél >= bl_t1)
static void en_boiler_auto(const EnData &d) {
  static uint32_t since = 0, last_cmd = 0, hw_since = 0, hw_abort_since = 0;
  static int32_t hw_day = -1;   // melyik napon volt már az Anti-bakteriális ciklus (év*400 + az év napja)
  const bool bl = en_fresh(d.bl_ms, EN_BL_STALE_MS), p1 = en_fresh(d.p1_ms, EN_LOCAL_STALE_MS);
  en_ba_avail = p1 ? -d.grid_w + (en_fresh(d.sk_ms, EN_LOCAL_STALE_MS) ? d.boil_w : 0.0f) : NAN;
  if (!en_cfg.bl_auto || en_cfg.bl_boss[0]) { since = 0; hw_since = 0; hw_abort_since = 0; en_ba_solar = -1; return; }   // csak a vezérlő kapcsol
  if (!bl || !p1 || d.boil_sw < 0 || isnan(d.boil_set)) { since = 0; hw_since = 0; hw_abort_since = 0; return; }
  if (en_ba_solar < 0) en_ba_solar = d.boil_set >= en_cfg.bl_t1 - 0.5f;
  // hiszterézis: napelemes cél-hőfokra váltás / vissza
  const bool want = en_ba_solar ? en_ba_avail >= en_cfg.bl_on_w - EN_BA_HYST_W : en_ba_avail >= en_cfg.bl_on_w;
  if (want == (bool)en_ba_solar) since = 0;
  else {
    if (!since) since = millis();
    if (millis() - since >= (en_ba_solar ? EN_BA_OFF_MS : EN_BA_ON_MS)) {
      en_ba_solar = want;
      since = 0;
      logout_printf("Bojler: napelemes automatika: %s (többlet %.0f W)\n", want ? "napelemes cél-hőfok" : "alap cél-hőfok", en_ba_avail);
      if ((cfg.nt_mask & NT_BOIL) && !is_night()) {   // halk értesítés (a csendes időszakban nem)
        char title[64], msg[120];
        snprintf(title, sizeof(title), want ? tr("Bojler: napenergiából %d °C-ig", "Water heater: up to %d °C from solar")
                                            : tr("Bojler: vissza %d °C-ra", "Water heater: back to %d °C"), want ? en_cfg.bl_t1 : en_cfg.bl_t0);
        snprintf(msg, sizeof(msg), tr("Napelem-többlet: %.0f W (határ: %d W).", "Solar surplus: %.0f W (limit: %d W)."),
                 en_ba_avail, want ? en_cfg.bl_on_w : en_cfg.bl_on_w - EN_BA_HYST_W);
        nt_push(title, msg, 2, want ? "sunny" : "zzz");
      }
    }
  }
  // a bojler állapotának igazítása: be, Smart mód (az Anti-bakteriális ciklust nem szakítjuk meg), a megfelelő cél-hőfok
  // (egyszerre egy írás); reggel 5-6 között Eco mód (napelemes cél-hőfok esetén nem), az Eco a cél-hőfokot nem írja át
  if (last_cmd && millis() - last_cmd < EN_BA_RETRY_MS) return;   // az előző írás még nem látszik
  // az Anti-bakteriális ciklus megszakítása, ha közben tartósan a hálózatból fűt (lásd a konstansoknál): a ciklust a
  // kijelző nem figyeli, de ha a napelem elapad, inkább visszaváltunk Smart módra, mint hogy a hálózatból fűtsön 80 °C-ra
  if (strcmp(d.boil_mode, "highwarm") == 0 && d.grid_w > EN_BA_HW_ABORT_W) {
    if (!hw_abort_since) hw_abort_since = millis();
    else if (millis() - hw_abort_since >= EN_BA_HW_ABORT_MS) {
      hw_abort_since = 0;
      last_cmd = millis();
      en_boiler_cmd("\"2\":\"" EN_BA_MODE "\"");
      logout_printf("Bojler: napi Anti-bakteriális ciklus megszakítva (hálózati vétel tartósan %.0f W)\n", d.grid_w);
      if ((cfg.nt_mask & NT_BOIL) && !is_night()) {
        char msg[96];
        snprintf(msg, sizeof(msg), tr("Megszakítva: tartósan a hálózatból fűtött (vétel: %.0f W).", "Interrupted: heating from the grid for too long (import: %.0f W)."), d.grid_w);
        nt_push(tr("Bojler: Anti-bakteriális ciklus", "Water heater: anti-bacterial cycle"), msg, 2, "warning");
      }
      return;
    }
  } else hw_abort_since = 0;
  // napi Anti-bakteriális ciklus indítása (lásd a konstansoknál); a bojler már highwarm módban van: nincs mit indítani
  struct tm tnow;
  const bool have_tm = get_local_tm(tnow);
  const int32_t today = have_tm ? (tnow.tm_year + 1900) * 400 + tnow.tm_yday : -1;
  if (today >= 0 && hw_day != today && d.boil_sw != 0 && strcmp(d.boil_mode, "highwarm") != 0) {
    if (en_ba_avail >= EN_BA_HW_W) {
      if (!hw_since) hw_since = millis();
      if (millis() - hw_since >= EN_BA_HW_ON_MS) {
        hw_day = today;
        hw_since = 0;
        last_cmd = millis();
        en_boiler_cmd("\"2\":\"highwarm\"");
        logout_printf("Bojler: napi Anti-bakteriális ciklus indítva (többlet %.0f W)\n", en_ba_avail);
        if ((cfg.nt_mask & NT_BOIL) && !is_night()) {
          char msg[96];
          snprintf(msg, sizeof(msg), tr("Napi Anti-bakteriális ciklus indult (napelem-többlet: %.0f W).", "Daily anti-bacterial cycle started (solar surplus: %.0f W)."), en_ba_avail);
          nt_push(tr("Bojler: Anti-bakteriális ciklus", "Water heater: anti-bacterial cycle"), msg, 2, "droplet");
        }
        return;
      }
    } else hw_since = 0;
  }
  bool morning = false;
  if (!en_ba_solar) { struct tm t; morning = get_local_tm(t) && t.tm_hour >= EN_BA_MORN_H0 && t.tm_hour < EN_BA_MORN_H1; }
  const char *want_mode = morning ? "eco" : EN_BA_MODE;
  const int target = en_ba_solar ? en_cfg.bl_t1 : en_cfg.bl_t0;
  char cmd[EN_BL_CMD_LEN] = "";
  if (d.boil_sw == 0) strlcpy(cmd, "\"1\":true", sizeof(cmd));
  else if (d.boil_mode[0] && strcmp(d.boil_mode, want_mode) != 0 && strcmp(d.boil_mode, "highwarm") != 0)
    snprintf(cmd, sizeof(cmd), "\"2\":\"%s\"", want_mode);
  else if (!morning && fabsf(d.boil_set - target) > 0.5f) snprintf(cmd, sizeof(cmd), "\"9\":%d", target);
  if (!cmd[0]) return;
  last_cmd = millis();
  en_boiler_cmd(cmd);
  logout_printf("Bojler: automatika: írás %s\n", cmd);
}

// a bojler hibakódja szövegesen ("E2", "E2 E4"; a bitmap 1. bitje E2); üres: nincs hiba
static void en_bl_fault_text(int f, char *out, size_t n) {
  out[0] = 0;
  for (int k = 0; f > 0 && k < 16; k++, f >>= 1)
    if (f & 1) { char e[6]; snprintf(e, sizeof(e), "%sE%d", out[0] ? " " : "", k + 2); strlcat(out, e, n); }
}

// A bojler hibakódjának figyelése (másodpercenként): változáskor a Naplóba, és a vezérlő (vagy az egyetlen kijelző)
// értesítést küld a megjelenéséről és a megszűnéséről. Az első lekérdezés hibáját is jelzi (ha induláskor már hibás).
static void en_boiler_fault_task(const EnData &d) {
  static int last = 0;
  if (!en_fresh(d.bl_ms, EN_BL_STALE_MS) || d.boil_fault < 0 || d.boil_fault == last) return;
  char now[40], was[40], msg[120];
  en_bl_fault_text(d.boil_fault, now, sizeof(now));
  en_bl_fault_text(last, was, sizeof(was));
  last = d.boil_fault;
  logout_printf(now[0] ? "Bojler: hibakód %s\n" : "Bojler: a hiba megszűnt (%s)\n", now[0] ? now : was);
  if (en_bl_remote() || !(cfg.nt_mask & NT_BOIL)) return;
  if (now[0]) {
    snprintf(msg, sizeof(msg), tr("A bojler hibát jelez: %s (%.0f °C).", "The water heater reports an error: %s (%.0f °C)."), now, d.boil_temp);
    nt_push(tr("Bojler hiba", "Water heater error"), msg, 4, "warning");
  } else {
    snprintf(msg, sizeof(msg), tr("A bojler hibája (%s) megszűnt.", "The water heater error (%s) has cleared."), was);
    nt_push(tr("Bojler: a hiba megszűnt", "Water heater: error cleared"), msg, 2, "white_check_mark");
  }
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
  en_boiler_auto(d);
  en_boiler_fault_task(d);
  static uint32_t res_n = 0;
  if (en_bl_res_n != res_n) {   // a háttérfeladat írásainak eredménye
    char r[sizeof(en_bl_res)];
    portENTER_CRITICAL(&en_mux);
    res_n = en_bl_res_n;
    strlcpy(r, en_bl_res, sizeof(r));
    portEXIT_CRITICAL(&en_mux);
    logout_printf("Bojler: írás %s\n", r);
  }
  static uint32_t last_gw = 0;
  if (d.gw_ms && d.gw_ms != last_gw) { last_gw = d.gw_ms; en_pv_record(d); }
  const char *errs[3] = {d.p1_err, d.sk_err, d.gw_err};
  const uint32_t ms[3] = {d.p1_ms, d.sk_ms, d.gw_ms};
  static const char *NAME[3] = {"hálózat (P1)", "bojler (dugalj)", "napelem (Growatt)"};
  for (int k = 0; k < 3; k++) {
    if (strcmp(errs[k], last_err[k]) != 0) {
      if (errs[k][0]) logout_printf("Energia: %s hiba: %s\n", NAME[k], errs[k]);
      else if (seen[k]) logout_printf("Energia: %s újra rendben\n", NAME[k]);
      strlcpy(last_err[k], errs[k], sizeof(last_err[k]));
    }
    if (ms[k] && !seen[k]) {
      seen[k] = true;
      if (k == 0) logout_printf("Energia: hálózat %.0f W\n", d.grid_w);
      if (k == 1) logout_printf("Energia: bojler %.0f W\n", d.boil_w);
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
  if (!en_cfg.on && !g_demo) return tr("kikapcsolva", "off");
  const EnData d = en_snapshot();
  String h = g_demo ? tr("(kitalált adat, bemutató mód)", "(made-up data, demo mode)") : "";
  char b[200], age[16];
  // hálózat
  h += tr("<br>Hálózat: ", "<br>Grid: ");
  if (!en_cfg.p1[0] && !g_demo) h += tr("nincs megadva", "not set");
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
  // bojler
  h += tr("<br>Bojler: ", "<br>Boiler: ");
  if (!en_cfg.sk[0] && !g_demo) h += tr("nincs megadva", "not set");
  else if (d.sk_ms) {
    en_age(d.sk_ms, age, sizeof(age));
    snprintf(b, sizeof(b), tr("%.0f W (%s), összesen %.1f kWh", "%.0f W (%s ago), total %.1f kWh"), d.boil_w, age, d.boil_kwh);
    h += b;
    const float tb = en_today(d.boil_kwh, en_day.boil);
    if (!isnan(tb)) { snprintf(b, sizeof(b), tr(", ma %.2f kWh", ", today %.2f kWh"), tb); h += b; }
  } else h += tr("még nincs adat", "no data yet");
  if (en_cfg.sk[0] && d.sk_err[0]) { h += " <span style='color:#ff8a80'>"; h += html_attr(d.sk_err); h += "</span>"; }
  if (en_cfg.bl[0] || g_demo) {
    h += tr("<br>Bojler hőfoka: ", "<br>Water heater temperature: ");
    if (d.bl_ms) {
      en_age(d.bl_ms, age, sizeof(age));
      snprintf(b, sizeof(b), tr("%.0f °C, %s (%s)", "%.0f °C, %s (%s ago)"), d.boil_temp, d.boil_state[0] ? d.boil_state : "?", age);
      h += b;
      if (!isnan(d.boil_set)) { snprintf(b, sizeof(b), tr(", cél-hőfok: %.0f °C", ", target: %.0f °C"), d.boil_set); h += b; }
      if (d.boil_sw >= 0) h += d.boil_sw ? tr(", bekapcsolva", ", on") : tr(", kikapcsolva", ", off");
      char fe[40];
      en_bl_fault_text(d.boil_fault, fe, sizeof(fe));
      if (fe[0]) { h += tr(" <b style='color:#ff8a80'>hibakód: ", " <b style='color:#ff8a80'>error code: "); h += fe; h += "</b>"; }
      if (!isnan(d.boil_water_pct)) { snprintf(b, sizeof(b), tr(", kivehető melegvíz: %.0f %%", ", hot water available: %.0f%%"), d.boil_water_pct); h += b; }
      if (d.boil_mode[0]) { snprintf(b, sizeof(b), tr(", üzemmód: %s", ", mode: %s"), d.boil_mode); h += b; }
    } else h += tr("még nincs adat", "no data yet");
    if (d.bl_err[0]) { h += " <span style='color:#ff8a80'>"; h += html_attr(d.bl_err); h += "</span>"; }
    if (en_bl_remote()) {
      const int au = en_bl_eff_auto(d);
      if (au < 0) snprintf(b, sizeof(b), tr("<br>Bojler automatika: a vezérlő (%s) nem elérhető", "<br>Water heater automation: the controller (%s) is unreachable"), en_cfg.bl_boss);
      else snprintf(b, sizeof(b), tr("<br>Bojler automatika: %s, %d W, %d/%d °C (a vezérlő: %s)", "<br>Water heater automation: %s, %d W, %d/%d °C (controller: %s)"),
                    au ? tr("be", "on") : tr("ki", "off"), d.rb_on_w, d.rb_t0, d.rb_t1, en_cfg.bl_boss);
      h += b;
    } else if (en_cfg.bl_auto) {
      snprintf(b, sizeof(b), tr("<br>Bojler automatika: be (napelemes cél-hőfok %d W többlet felett, alap %d W alatt)",
                                "<br>Water heater automation: on (solar target above %d W surplus, base below %d W)"),
               en_cfg.bl_on_w, en_cfg.bl_on_w - EN_BA_HYST_W);
      h += b;
      snprintf(b, sizeof(b), tr("; cél-hőfok: alap %d °C, napelemes %d °C (most: %s)", "; target: base %d °C, solar %d °C (now: %s)"),
               en_cfg.bl_t0, en_cfg.bl_t1, en_ba_solar > 0 ? tr("napelemes", "solar") : tr("alap", "base"));
      h += b;
      if (!isnan(en_ba_avail)) { snprintf(b, sizeof(b), tr("; többlet most: %.0f W", "; surplus now: %.0f W"), en_ba_avail); h += b; }
    } else h += tr("<br>Bojler automatika: ki (kézi vezérlés)", "<br>Water heater automation: off (manual control)");
  }
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
  if (en_cfg.gt[0] && en_cfg.gw_slot >= 0) {
    snprintf(b, sizeof(b), tr(" (lekérés körben, %d. kijelzőként)", " (requests in turn, as display %d)"), en_cfg.gw_slot + 1);
    h += b;
  }
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
    snprintf(b, sizeof(b), tr("<br>Ház: kb. %.0f W (napelem + hálózat - bojler)", "<br>House: approx. %.0f W (solar + grid - water heater)"), house);
    h += b;
  }
  return h;
}

// A webes Beállítások "Energia" része
static String en_settings_html() {
  String h = tr("<h3>Energia (HomeWizard, Growatt)</h3>"
                "<p style='color:#aaa;margin:0'>A ház energiaadatai: a HomeWizard P1 mérő (hálózat) és egy HomeWizard Energy "
                "Socket (pl. a bojler) a helyi hálózaton, a napelem a Growatt felhőből. A HomeWizard appban az eszközönként a "
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
  h += tr("'><label>Energy Socket címe (bojler)</label>", "'><label>Energy Socket address (water heater)</label>");
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
  h += tr("'><label>Growatt-lekérés (ha több kijelzőn is van token)</label>", "'><label>Growatt requests (if several displays have the token)</label>");
  h += "<select name='en_gsl'>";
  {
    static const char *const SL[2][4] = {{"Egyedül: 5 percenként", "Körben, 1. kijelzőként (0:00)", "Körben, 2. kijelzőként (5:10)", "Körben, 3. kijelzőként (10:20)"},
                                         {"Alone: every 5 minutes", "In turn, as display 1 (0:00)", "In turn, as display 2 (5:10)", "In turn, as display 3 (10:20)"}};
    for (int k = -1; k < EN_GW_SLOTS; k++) {
      h += "<option value='" + String(k) + "'";
      if (k == en_cfg.gw_slot) h += " selected";
      h += ">"; h += TR2(SL)[k + 1]; h += "</option>";
    }
  }
  h += tr("</select><p style='color:#aaa;margin:0'>Körben: a kijelzők felváltva kérdeznek (mindegyik 15,5 percenként, egymáshoz képest 5 perc "
          "10 mp eltolással), és a továbbküldéssel egymásnak adják az adatot; ha egy kiesik, a többi pótolja. Mindegyiken ugyanaz a token, "
          "más-más sorszám, és a továbbküldés címei a másik kijelzők.</p><label>",
          "</select><p style='color:#aaa;margin:0'>In turn: the displays take turns (each every 15.5 minutes, 5 min 10 s apart) and pass the "
          "data to each other by forwarding; if one drops out, the others cover. Same token on each, a different number, and the forwarding "
          "addresses are the other displays.</p><label>");
  h += tr("A napelemek teljesítménye, kWp (a napi diagram teteje; üresen: a mai termeléshez igazodik)</label>",
          "Solar panel power, kWp (top of the daily chart; empty: follows today's production)</label>");
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
  h += tr("<h3>Bojler hőfoka (Tuya)</h3><p style='color:#aaa;margin:0'>A bojler saját (Tuya) WiFi-hálózata helyben, titkosítva adja "
          "a víz hőfokát és az állapotát. Üresen hagyott cím: nincs lekérdezve.</p><label>Bojler Tuya-hálózatának címe (IP)</label>",
          "<h3>Water heater temperature (Tuya)</h3><p style='color:#aaa;margin:0'>The water heater's own (Tuya) WiFi network gives "
          "its water temperature and state locally, encrypted. Left empty: not queried.</p><label>Water heater Tuya network address (IP)</label>");
  h += "<input name='en_bl' maxlength='64' pattern='[A-Za-z0-9.:-]*' value='"; h += html_attr(en_cfg.bl);
  h += tr("'><label>Eszköz-ID (devId)</label>", "'><label>Device ID (devId)</label>");
  h += "<input name='en_blid' maxlength='31' pattern='[A-Za-z0-9]*' value='"; h += html_attr(en_cfg.bl_id);
  h += tr("'><label>Helyi kulcs (local_key; üresen hagyva a mentett marad)</label>", "'><label>Local key (left empty, the saved one stays)</label>");
  h += "<input type='password' name='en_blk' maxlength='19' autocomplete='off' placeholder='";
  if (en_cfg.bl_key[0]) {
    const size_t n = strlen(en_cfg.bl_key);
    h += tr("mentve, vége: ", "saved, ends with: ");
    h += html_attr(en_cfg.bl_key + (n > 4 ? n - 4 : 0));
  } else h += tr("nincs megadva", "not set");
  h += "'><label style='display:block'><input type='checkbox' name='en_blkdel' style='width:auto;margin:0 8px 0 0'>";
  h += tr("Mentett kulcs törlése</label>", "Delete the saved key</label>");
  h += tr("<label>A bojler vezérlője (másik kijelző címe; üresen: ez a kijelző vezérel)</label>",
          "<label>Water heater controller (another display's address; empty: this display controls it)</label>");
  h += "<input name='en_bbo' maxlength='64' pattern='[A-Za-z0-9.:-]*' value='"; h += html_attr(en_cfg.bl_boss);
  h += "'>";
  if (en_cfg.bl_boss[0]) {   // távirányító: az automatika a vezérlőn állítható (a kijelzőn innen is, a Bojler fülön)
    h += tr("<p style='color:#aaa;margin:0'>Az automatikát és a bojler írását a vezérlő kijelző végzi; a kijelző Bojler fülén "
            "innen is állítható.</p>",
            "<p style='color:#aaa;margin:0'>The automation and the writes to the heater are done by the controller display; "
            "they can be changed from this display's Water heater tab too.</p>");
    return h;
  }
  h += "<input type='hidden' name='en_baf' value='1'><label style='display:block'><input type='checkbox' name='en_ba'";
  if (en_cfg.bl_auto) h += " checked";
  h += " style='width:auto;margin:0 8px 0 0'>";
  h += tr("Napelemes automatika: a bojler mindig be van kapcsolva, az alap cél-hőfokot tartja; ha van elég napelem-többlet, a "
          "napelemes cél-hőfokig fűt</label><label>Napelemes cél-hőfok ennyi többletnél (W; vissza az alapra " ,
          "Solar automation: the water heater is always on and keeps the base target; when there is enough solar surplus it "
          "heats up to the solar target</label><label>Solar target at this surplus (W; back to base ");
  h += String(EN_BA_HYST_W);
  h += tr(" W-tal kevesebbnél)</label>", " W below it)</label>");
  h += "<input name='en_bth' type='number' step='50' min='" + String(EN_BA_MIN_W) + "' max='" + String(EN_BA_MAX_W) + "' value='" + String(en_cfg.bl_on_w);
  h += tr("'><label>Alap cél-hőfok (°C)</label>", "'><label>Base target (°C)</label>");
  h += "<input name='en_bt0' type='number' min='" + String(EN_BA_T_MIN) + "' max='" + String(EN_BA_T_MAX) + "' value='" + String(en_cfg.bl_t0);
  h += tr("'><label>Napelemes cél-hőfok (°C)</label>", "'><label>Solar target (°C)</label>");
  h += "<input name='en_bt1' type='number' min='" + String(EN_BA_T_MIN) + "' max='" + String(EN_BA_T_MAX) + "' value='" + String(en_cfg.bl_t1);
  h += tr("'><p style='color:#aaa;margin:0'>Az automatika a bojlert Smart módba teszi: az a cél-hőfokot tartja (kb. 8 °C hűlésnél újra fűt); az Azonnali mód csak egyszer fűt fel, az Eco 55 °C-ra korlátoz. A kijelzőn: "
          "Beállítások / Bojler; a Be/Ki gomb vagy egy üzemmód választása kézi vezérlésre vált.</p>",
          "'><p style='color:#aaa;margin:0'>The automation puts the heater in Smart mode, which keeps the target (reheats after about 8 °C of cooling); Instant mode heats up only once, Eco limits it to 55 °C. On the display: "
          "Settings / Water heater; the On/Off button or choosing a mode switches to manual control.</p>");
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
  if (s.hasArg("en_gsl")) c.gw_slot = (int8_t)constrain((int)s.arg("en_gsl").toInt(), -1, EN_GW_SLOTS - 1);
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
  en_host_arg(s, "en_bl", c.bl, sizeof(c.bl));
  if (s.hasArg("en_blid")) {
    String bi = s.arg("en_blid");
    bi.trim();
    bool ok = true;
    for (size_t i = 0; i < bi.length(); i++) if (!isalnum((unsigned char)bi[i])) ok = false;
    if (ok && bi.length() < sizeof(c.bl_id)) strlcpy(c.bl_id, bi.c_str(), sizeof(c.bl_id));
  }
  String bk = s.arg("en_blk");
  bk.trim();
  if (s.hasArg("en_blkdel")) c.bl_key[0] = 0;
  else if (!bk.isEmpty() && bk.length() < sizeof(c.bl_key)) {
    bool ok = true;
    for (size_t i = 0; i < bk.length(); i++) if ((unsigned char)bk[i] < 0x20 || (unsigned char)bk[i] > 0x7E) ok = false;
    if (ok) strlcpy(c.bl_key, bk.c_str(), sizeof(c.bl_key));
  }
  if (s.hasArg("en_baf")) c.bl_auto = s.hasArg("en_ba");   // csak ha az automatika mezői az űrlapon voltak
  en_host_arg(s, "en_bbo", c.bl_boss, sizeof(c.bl_boss));
  if (s.hasArg("en_bth")) {
    const long w = s.arg("en_bth").toInt();
    if (w >= EN_BA_MIN_W && w <= EN_BA_MAX_W) c.bl_on_w = (int)w;
  }
  if (s.hasArg("en_bt0")) c.bl_t0 = constrain((int)s.arg("en_bt0").toInt(), EN_BA_T_MIN, EN_BA_T_MAX);
  if (s.hasArg("en_bt1")) c.bl_t1 = constrain((int)s.arg("en_bt1").toInt(), c.bl_t0, EN_BA_T_MAX);
  {
    CamPause cam_pause_;   // P4: a kamera áll a mentés alatt
    Preferences p;
    p.begin("cfg", false);
    p.putBool("en_on", c.on);
    p.putBool("en_ba", c.bl_auto);
    p.putInt("en_bth", c.bl_on_w);
    p.putInt("en_bt0", c.bl_t0);
    p.putInt("en_bt1", c.bl_t1);
    p.putString("en_bbo", c.bl_boss);
    p.putString("en_p1", c.p1);
    p.putString("en_sk", c.sk);
    p.putString("en_gt", c.gt);
    p.putString("en_gs", c.gs);
    p.putChar("en_gsl", c.gw_slot);
    p.putFloat("en_kwp", c.kwp);
    p.putString("en_fw", c.fw);
    p.putString("en_bl", c.bl);
    p.putString("en_blid", c.bl_id);
    p.putString("en_blk", c.bl_key);
    p.end();
  }
  const bool gw_changed = strcmp(c.gt, en_cfg.gt) != 0 || strcmp(c.gs, en_cfg.gs) != 0 || (c.on && !en_cfg.on);
  const bool boss_changed = strcmp(c.bl_boss, en_cfg.bl_boss) != 0;
  portENTER_CRITICAL(&en_mux);
  en_cfg = c;
  if (gw_changed) en_gw_force = true;
  if (!c.gt[0] && !en_data.gw_remote) { en_data.gw_ms = 0; en_data.gw_err[0] = 0; }   // a régi adat ne látsszon (a másik kijelzőtől kapott marad)
  if (!c.gt[0]) en_data.gw_err[0] = 0;
  if (!c.p1[0]) { en_data.p1_ms = 0; en_data.p1_err[0] = 0; }
  if (!c.sk[0]) { en_data.sk_ms = 0; en_data.sk_err[0] = 0; }
  if (!c.bl[0] || !c.bl_id[0] || !c.bl_key[0]) { en_data.bl_ms = 0; en_data.bl_err[0] = 0; }
  if (boss_changed) { en_data.rb_ms = 0; en_data.rb_err[0] = 0; }
  portEXIT_CRITICAL(&en_mux);
  logout_printf("Energia: beállítások mentve (%s)\n", c.on ? "be" : "ki");
}
