// Webes kezelőoldal I.: frissítő képernyő, jelszó, beállítások, nyers adatok (/raw)
// (Az idojaras.ino része: az Arduino IDE és a PlatformIO a fájlokat ábécésorrendben, egy programmá fűzi össze.)

// ---------------------------------------------------------------------------
// Firmware-frissítés a böngészőből: http://idojaras.local/ vagy http://<a lap IP-je>/  (.bin feltöltése)
// ---------------------------------------------------------------------------
static const char *FW_NAME = "Ecowitt Weather Display v1.4.1" FW_BOARD;
static const char *MDNS_NAME = MDNS_HOST;   // a név a hálózaton (kijelzo.h): idojaras.local, ill. idojaras-p4.local

static lv_obj_t *ota_ov = NULL, *ota_l1 = NULL, *ota_l2 = NULL, *ota_fill = NULL;
static bool ota_ok = false;
static String ota_err;
static size_t ota_total = 0, ota_done = 0;
constexpr int OTA_BAR_W = SX(700);

static void create_ota_overlay() {
  ota_ov = make_shape(lv_screen_active(), 0, 0, LCD_H_RES, LCD_V_RES, 0, 0x000000);
  ota_l1 = dlabel_c(ota_ov, "", &font_hu_28, 0xFFFFFF, 0, SY(220), LCD_H_RES);
  ota_l2 = dlabel_c(ota_ov, "", &font_hu_28, COL_GREY, 0, SY(270), LCD_H_RES);
  make_shape(ota_ov, (LCD_H_RES - OTA_BAR_W) / 2, SY(330), OTA_BAR_W, S(16), S(8), 0x333333);
  ota_fill = make_shape(ota_ov, (LCD_H_RES - OTA_BAR_W) / 2, SY(330), S(4), S(16), S(8), 0x4FC3F7);
  lv_obj_set_hidden(ota_ov, true);
}

static void ota_screen(const char *l1, const char *l2, int pct) {
  if (!ota_ov) return;
  wake_until_ms = millis() + 120000UL;   // éjszakai fényerőn is látsszon
  lv_obj_set_hidden(ota_ov, false);
  lv_label_set_text(ota_l1, l1);
  lv_label_set_text(ota_l2, l2);
  lv_obj_set_width(ota_fill, S(4) + (OTA_BAR_W - S(4)) * (pct < 0 ? 0 : (pct > 100 ? 100 : pct)) / 100);
  lv_timer_handler();
}

static void ota_screen_hide() {
  if (ota_ov) lv_obj_set_hidden(ota_ov, true);
}

static WebServer *cur_srv = &my_server;

// Jelszókérés: a böngésző bejelentkező ablakot nyit; ha azt nem tudja (vagy megszakítják), ez a szöveg látszik
static void request_auth() {
  cur_srv->requestAuthentication(BASIC_AUTH, "Idojaras-kijelzo",
    "<html><head><meta charset='utf-8'></head><body style='font-family:sans-serif;background:#121212;color:#eee;padding:30px'>"
    "<h2>Jelszó szükséges / Password required</h2><p>Ez az oldal jelszóval védett (felhasználónév: <b>admin</b>). / "
    "This page is password protected (user name: <b>admin</b>).</p>"
    "<p>Ha nem jelent meg a bejelentkező ablak, nyisd meg az oldalt egy rendes böngészőben (Safari, Chrome). / "
    "If no login window appeared, open the page in a regular browser (Safari, Chrome).</p></body></html>");
}

static bool ota_auth_ok() {
  return !cfg.pw[0] || cur_srv->authenticate("admin", cfg.pw);   // a jelszó a beállításoknál adható meg
}

// Időzónák a beállító oldal listájához (POSIX TZ formátum; a nyári időszámítás benne van)
struct TzDef { const char *name; const char *name_en; const char *posix; const char *iana; };
static const TzDef TZ_LIST[] = {
  {"Közép-Európa (Budapest, Bécs, Berlin)", "Central Europe (Budapest, Vienna, Berlin)", "CET-1CEST,M3.5.0,M10.5.0/3",   "Europe/Budapest"},
  {"Kelet-Európa (Bukarest, Helsinki)",      "Eastern Europe (Bucharest, Helsinki)",      "EET-2EEST,M3.5.0/3,M10.5.0/4", "Europe/Bucharest"},
  {"Nyugat-Európa (London, Lisszabon)",      "Western Europe (London, Lisbon)",           "GMT0BST,M3.5.0/1,M10.5.0",     "Europe/London"},
  {"USA keleti (New York)",                  "US Eastern (New York)",                     "EST5EDT,M3.2.0,M11.1.0",       "America/New_York"},
  {"USA középső (Chicago)",                  "US Central (Chicago)",                      "CST6CDT,M3.2.0,M11.1.0",       "America/Chicago"},
  {"USA hegyvidéki (Denver)",                "US Mountain (Denver)",                      "MST7MDT,M3.2.0,M11.1.0",       "America/Denver"},
  {"USA csendes-óceáni (Los Angeles)",       "US Pacific (Los Angeles)",                  "PST8PDT,M3.2.0,M11.1.0",       "America/Los_Angeles"},
  {"Kelet-Ausztrália (Sydney)",              "Eastern Australia (Sydney)",                "AEST-10AEDT,M10.1.0,M4.1.0/3", "Australia/Sydney"},
  {"UTC",                                    "UTC",                                       "UTC0",                         "UTC"},
};

// Beállítások mentése: hely (üresen hagyva törlődik) és időzóna
static void handle_settings_save() {
  if (!ota_auth_ok()) { request_auth(); return; }
  String la = cur_srv->arg("lat"), lo = cur_srv->arg("lon"), tz = cur_srv->arg("tz");
  la.trim(); lo.trim(); tz.trim();
  la.replace(',', '.'); lo.replace(',', '.');
  String err;
  bool clear_loc = la.isEmpty() && lo.isEmpty();
  double lat = la.toDouble(), lon = lo.toDouble();
  if (!clear_loc && (la.isEmpty() || lo.isEmpty() || lat < -90 || lat > 90 || lon < -180 || lon > 180 || (lat == 0 && lon == 0)))
    err = tr("Hibás koordináták (szélesség: -90..90, hosszúság: -180..180).", "Invalid coordinates (latitude: -90..90, longitude: -180..180).");
  if (tz.isEmpty() || tz.length() >= sizeof(cfg.tz)) err = tr("Hibás időzóna.", "Invalid time zone.");
  const bool chpw = cur_srv->hasArg("chpw");
  String npw = cur_srv->arg("newpw");
  if (chpw && npw.length() >= sizeof(cfg.pw)) err = tr("A jelszó legfeljebb 32 karakter lehet.", "The password can be at most 32 characters.");
  if (chpw && npw != cur_srv->arg("newpw2")) err = tr("A két jelszó nem egyezik, nem mentettem.", "The two passwords do not match; nothing saved.");
  // éjszakai mód
  int nh1, nm1, nh2, nm2;
  const bool has_night = cur_srv->hasArg("n_st");
  // a hiányzó mező a régi értéket tartja meg
  const int n_pct = cur_srv->hasArg("n_pct") ? cur_srv->arg("n_pct").toInt() : cfg.n_pct;
  const int d_pct = cur_srv->hasArg("d_pct") ? cur_srv->arg("d_pct").toInt() : cfg.d_pct;
  if (has_night) {
    if (sscanf(cur_srv->arg("n_st").c_str(), "%d:%d", &nh1, &nm1) != 2 || sscanf(cur_srv->arg("n_en").c_str(), "%d:%d", &nh2, &nm2) != 2 ||
        nh1 < 0 || nh1 > 23 || nm1 < 0 || nm1 > 59 || nh2 < 0 || nh2 > 23 || nm2 < 0 || nm2 > 59)
      err = tr("Hibás éjszakai időpont (óó:pp).", "Invalid night time (hh:mm).");
    if (n_pct < 1 || n_pct > 100 || d_pct < 10 || d_pct > 100)
      err = tr("Hibás fényerő (éjjel 1-100 %, nappal 10-100 %).", "Invalid brightness (night 1-100 %, day 10-100 %).");
  }
  if (err.length()) {
    cur_srv->send(400, "text/html; charset=utf-8", String("<html><head><meta charset='utf-8'></head><body style='font-family:sans-serif;background:#121212;color:#eee;padding:30px'>"
                  "<h2>") + tr("Nem mentettem", "Not saved") + "</h2><p>" + err + "</p><p><a style='color:#ff8a3d' href='/'>" +
                  tr("Vissza", "Back") + "</a></p></body></html>");
    return;
  }
  if (clear_loc || !cfg.has_loc || lat != cfg.lat || lon != cfg.lon) { fc.valid = false; fc_force = true; }   // új hely: új előrejelzés
  CamPause cam_pause_;   // P4: a kamera áll a mentés alatt
  Preferences p;
  p.begin("cfg", false);
  if (clear_loc) { p.remove("lat"); p.remove("lon"); }
  else { p.putDouble("lat", lat); p.putDouble("lon", lon); }
  p.putString("tz", tz);
  if (chpw) p.putString("pw", npw);   // üres jelszó = nincs védelem
  p.end();
  if (has_night) {
    cfg.n_on = cur_srv->hasArg("n_on");
    cfg.n_st = nh1 * 60 + nm1; cfg.n_en = nh2 * 60 + nm2;
    cfg.n_pct = n_pct; cfg.d_pct = d_pct;
    if (cur_srv->hasArg("n_mode")) cfg.n_mode = cur_srv->arg("n_mode").toInt() ? 1 : 0;
    if (cur_srv->hasArg("n_so")) cfg.n_so = constrain((int)cur_srv->arg("n_so").toInt(), -120, 180);
    if (cur_srv->hasArg("n_ro")) cfg.n_ro = constrain((int)cur_srv->arg("n_ro").toInt(), -120, 180);
    cfg_save_night();
  }
  // értesítések
  if (cur_srv->hasArg("nt_t")) {
    String t = cur_srv->arg("nt_t"); t.trim();
    int mask = 0;
    for (int i = 0; i < 6; i++) { char k[8]; snprintf(k, sizeof(k), "nt_b%d", i); if (cur_srv->hasArg(k)) mask |= 1 << i; }
    if (t.isEmpty() || nt_topic_valid(t.c_str())) {
      strlcpy(cfg.nt_topic, t.c_str(), sizeof(cfg.nt_topic));
      cfg.nt_mask = mask;
      CamPause cam_pause_;   // P4: a kamera áll a mentés alatt
      Preferences np;
      np.begin("cfg", false);
      np.putString("nt_t", cfg.nt_topic);
      np.putInt("nt_m", cfg.nt_mask);
      np.end();
    }
  }
  // hangjelzés (P4; a jelölőnégyzet kikapcsolva nem küld semmit, ezért a rejtett snd_f jelzi, hogy a mező az űrlapon volt)
  if (cur_srv->hasArg("snd_f")) {
    cfg.snd_on = cur_srv->hasArg("snd");
    if (cur_srv->hasArg("sndv")) cfg.snd_vol = constrain((int)cur_srv->arg("sndv").toInt(), 0, 2);
    snd_save();
  }
  // automatikus fényerő a kamerával (P4; mint a hangjelzésnél, a rejtett cam_f jelzi, hogy a mező az űrlapon volt)
  if (cur_srv->hasArg("cam_f")) {
    cfg.cam_on = cur_srv->hasArg("cam");
    CamPause cam_pause_;   // P4: a kamera áll a mentés alatt
    Preferences cp;
    cp.begin("cfg", false);
    cp.putBool("cam", cfg.cam_on);
    cp.end();
  }
  en_settings_save(*cur_srv);   // energia (energia_01)
  // továbbküldés másik kijelzőre (a http:// előtag és a záró / elhagyható)
  if (cur_srv->hasArg("fw_h")) {
    String h = cur_srv->arg("fw_h"); h.trim();
    if (h.startsWith("http://")) h.remove(0, 7);
    while (h.endsWith("/")) h.remove(h.length() - 1);
    if (h.isEmpty() || fw_host_valid(h.c_str())) {
      cfg.fw_on = cur_srv->hasArg("fw_on");
      strlcpy(cfg.fw_host, h.c_str(), sizeof(cfg.fw_host));
      CamPause cam_pause_;   // P4: a kamera áll a mentés alatt
      Preferences fp;
      fp.begin("cfg", false);
      fp.putBool("fw_on", cfg.fw_on);
      fp.putString("fw_h", cfg.fw_host);
      fp.end();
    }
  }
  // helyiségek
  if (cur_srv->hasArg("r0n")) {
    bool changed = false;
    for (int i = 0; i < ROOM_COUNT; i++) {
      char k[8];
      snprintf(k, sizeof(k), "r%dn", i); String nm = cur_srv->arg(k); nm.trim();
      snprintf(k, sizeof(k), "r%dm", i); String mt = cur_srv->arg(k); mt.trim();
      snprintf(k, sizeof(k), "r%dv", i); float lv = cur_srv->arg(k).toFloat();
      if (nm.isEmpty()) nm = g_lang ? String("Place ") + (i + 1) : String(i + 1) + ". hely";
      if (lv < 0 || lv > 10) lv = 0;
      if (nm != rooms[i].name || mt != rooms[i].match || fabsf(lv - rooms[i].low_v) > 0.001f) changed = true;
      strlcpy(rooms[i].name, nm.c_str(), sizeof(rooms[i].name));
      strlcpy(rooms[i].match, mt.c_str(), sizeof(rooms[i].match));
      rooms[i].low_v = lv;
    }
    if (changed) {
      CamPause cam_pause_;   // P4: a kamera áll a mentés alatt
      Preferences rp;
      rp.begin("cfg", false);
      for (int i = 0; i < ROOM_COUNT; i++) {
        char k[8];
        snprintf(k, sizeof(k), "r%dn", i); rp.putString(k, rooms[i].name);
        snprintf(k, sizeof(k), "r%dm", i); rp.putString(k, rooms[i].match);
        snprintf(k, sizeof(k), "r%dv", i); rp.putFloat(k, rooms[i].low_v);
      }
      rp.end();
      for (Sender &x : senders) {            // a küldők újra besorolódnak az új azonosítók szerint
        x.count = 0; x.room = -1; x.st[0] = x.model[0] = 0; x.raw = "";
      }
      for (Room &r : rooms) r.last_ms = 0;
      LogOut.println("Helyiségek mentve");
    }
  }
  cfg_load();
  cfg_apply_tz();
  sun_last_key = -1;
  LogOut.printf("Beállítások mentve: hely %s, TZ %s%s\n", cfg.has_loc ? "megadva" : "nincs", cfg.tz,
                chpw ? (cfg.pw[0] ? ", jelszó beállítva" : ", jelszó törölve") : "");
  // nyelvváltás: mentés, majd újraindítás (a felület indításkor épül fel)
  if (cur_srv->hasArg("lang") && (cur_srv->arg("lang").toInt() ? 1 : 0) != g_lang) {
    const int nl = cur_srv->arg("lang").toInt() ? 1 : 0;
    lang_save(nl);
    cur_srv->send(200, "text/html; charset=utf-8", String("<html><head><meta charset='utf-8'><meta http-equiv='refresh' content='25;url=/'></head>"
                  "<body style='font-family:sans-serif;background:#121212;color:#eee;padding:30px'><h2>") +
                  (nl ? "Language saved" : "Nyelv mentve") + "</h2><p>" +
                  (nl ? "The display restarts; this page reloads in 25 seconds." : "A kijelző újraindul; az oldal 25 mp múlva újratöltődik.") +
                  "</p></body></html>");
    delay(500);
    hist_save();
    diag_mark_restart(RC_MANUAL);
    ESP.restart();
  }
  cur_srv->sendHeader("Location", "/");
  cur_srv->send(303, "text/plain", "");
}

// HTML-attribútumba írható szöveg (idézőjel, kacsacsőr, & kódolva)
static String html_attr(const char *s) {
  String o;
  for (; *s; s++) {
    switch (*s) {
      case '\'': o += "&#39;"; break;
      case '"':  o += "&quot;"; break;
      case '<':  o += "&lt;"; break;
      case '>':  o += "&gt;"; break;
      case '&':  o += "&amp;"; break;
      default:   o += *s;
    }
  }
  return o;
}

// Konzolok és helyiségek: név, a konzol azonosítója (a "model"/"stationtype" része), akkuriasztás; a küldő konzolok listája
static String rooms_html() {
  String h = tr("<h3>Konzolok és helyiségek</h3>"
                "<p style='color:#aaa;margin:0'>A konzol azonosítója egy részlet a konzol által küldött <i>model</i> vagy "
                "<i>stationtype</i> mezőből (pl. WS3910). Üresen hagyva a hely nincs használatban. Az 1. hely konzoljától jön "
                "a kinti (WS90) adat is. Akkuriasztás: e feszültség alatt figyelmeztet (0 = nincs).</p>"
                "<p style='color:#aaa'>Most küldő konzolok: ",
                "<h3>Consoles and rooms</h3>"
                "<p style='color:#aaa;margin:0'>The console identifier is a part of the <i>model</i> or <i>stationtype</i> field "
                "sent by the console (e.g. WS3910). Left empty, the place is not used. The outdoor (WS90) data also comes from the "
                "console of place 1. Battery alarm: warns below this voltage (0 = off).</p>"
                "<p style='color:#aaa'>Consoles sending now: ");
  bool any = false;
  for (const Sender &x : senders) {
    if (!x.count) continue;
    if (any) h += ", ";
    h += html_attr(x.model[0] ? x.model : x.st);
    h += x.room >= 0 ? String(" (") + html_attr(rooms[x.room].name) + ")" : String(tr(" (nincs hozzárendelve)", " (not assigned)"));
    any = true;
  }
  if (!any) h += tr("egyik sem", "none");
  h += "</p>";
  for (int i = 0; i < ROOM_COUNT; i++) {
    char b[400];
    snprintf(b, sizeof(b), tr("<h4 style='margin:14px 0 0'>%d. hely%s</h4>"
                              "<label>Név</label><input name='r%dn' maxlength='15' value='%s'>"
                              "<label>Konzol azonosítója</label><input name='r%dm' maxlength='20' value='%s'>"
                              "<label>Akkuriasztás (V)</label><input name='r%dv' type='number' step='0.1' min='0' max='10' value='%.1f'>",
                              "<h4 style='margin:14px 0 0'>Place %d%s</h4>"
                              "<label>Name</label><input name='r%dn' maxlength='15' value='%s'>"
                              "<label>Console identifier</label><input name='r%dm' maxlength='20' value='%s'>"
                              "<label>Battery alarm (V)</label><input name='r%dv' type='number' step='0.1' min='0' max='10' value='%.1f'>"),
             i + 1, i == 0 ? tr(" (innen jön a kinti adat is)", " (outdoor data also comes from here)") : "", i, html_attr(rooms[i].name).c_str(), i,
             html_attr(rooms[i].match).c_str(), i, rooms[i].low_v);
    h += b;
  }
  return h;
}

// Értesítések (ntfy.sh): téma és az egyes értesítések
static String ntfy_html() {
  static const char *NAMES[2][6] = {{"Fagy várható", "Eső várható", "Szellőztess (magas CO2)", "Lemerülő / hiányzó elem; P4: áramszünet (akkuról megy)",
                                     "Elakadt adat (15 perce nem küld egy konzol)", "Újraindulás összeomlás vagy watchdog miatt"},
                                    {"Frost expected", "Rain expected", "Ventilate (high CO2)", "Low / missing battery; P4: power failure (on battery)",
                                     "Stale data (a console has not sent for 15 minutes)", "Restart because of a crash or watchdog"}};
  String h = tr("<h3>Értesítések a telefonra (ntfy)</h3>"
                "<p style='color:#aaa;margin:0'>Telepítsd a telefonra az ingyenes <b>ntfy</b> appot, és iratkozz fel ugyanarra a "
                "témára, amit itt megadsz. A téma gyakorlatilag jelszó: legyen egyedi és nehezen kitalálható "
                "(pl. idojaras-pelda-q8m2v; csak betű, szám, - és _). Üresen hagyva nincs értesítés. Az éjszakai mód idején "
                "csak a fagy és az összeomlás megy ki.</p><label>Téma</label>",
                "<h3>Phone notifications (ntfy)</h3>"
                "<p style='color:#aaa;margin:0'>Install the free <b>ntfy</b> app on your phone and subscribe to the same topic "
                "that you enter here. The topic is practically a password: make it unique and hard to guess "
                "(e.g. weather-example-q8m2v; only letters, digits, - and _). Left empty, there are no notifications. During "
                "night mode only frost and crash notifications are sent.</p><label>Topic</label>");
  h += "<input name='nt_t' maxlength='64' pattern='[A-Za-z0-9_-]*' value='";
  h += html_attr(cfg.nt_topic);
  h += "'>";
  for (int i = 0; i < 6; i++) {
    char b[256];
    snprintf(b, sizeof(b), "<label style='display:block'><input type='checkbox' name='nt_b%d'%s style='width:auto;margin:0 8px 0 0'>%s</label>",
             i, (cfg.nt_mask & (1 << i)) ? " checked" : "", NAMES[g_lang][i]);
    h += b;
  }
  h += tr("<p style='color:#aaa;margin:0'>Utolsó küldés: ", "<p style='color:#aaa;margin:0'>Last notification: ");
  h += html_attr(nt_status_text());
  h += "</p>";
  return h;
}

// Továbbküldés másik kijelzőre: cél és az utolsó küldés eredménye
static String forward_html() {
  String h = tr("<h3>Továbbküldés másik kijelzőre</h3>"
                "<p style='color:#aaa;margin:0'>A konzoloktól kapott adatokat ez a kijelző változatlanul továbbküldi a megadott "
                "kijelző <i>/ecowitt</i> címére (pl. idojaras-p4.local vagy 192.168.1.50). "
                "Ha a cél nem érhető el, ennek a kijelzőnek a működése nem változik. Telefonos értesítést elég az egyiken beállítani.</p>",
                "<h3>Forwarding to another display</h3>"
                "<p style='color:#aaa;margin:0'>This display forwards the data received from the consoles unchanged to the "
                "<i>/ecowitt</i> address of the given display (e.g. idojaras-p4.local or 192.168.1.50). "
                "If the target is not reachable, this display keeps working normally. Phone notifications need to be set up on one "
                "display only.</p>");
  h += "<label style='display:block'><input type='checkbox' name='fw_on'";
  if (cfg.fw_on) h += " checked";
  h += " style='width:auto;margin:0 8px 0 0'>";
  h += tr("Továbbküldés bekapcsolva</label><label>Cél (gépnév vagy IP)</label>", "Forwarding on</label><label>Target (host name or IP)</label>");
  h += "<input name='fw_h' maxlength='64' pattern='[A-Za-z0-9.:/-]*' value='";
  h += html_attr(cfg.fw_host);
  h += tr("'><p style='color:#aaa;margin:0'>Utolsó továbbküldés: ", "'><p style='color:#aaa;margin:0'>Last forward: ");
  h += (cfg.fw_on && cfg.fw_host[0]) ? html_attr(fw_status_text()) : String(cfg.fw_on ? tr("nincs megadva cél", "no target set") : tr("kikapcsolva", "off"));
  h += "</p>";
  return h;
}

static String settings_html() {
  String h = tr("<h3>Beállítások</h3><form method='POST' action='/settings'><label>Földrajzi szélesség (pl. 47.4979)</label>",
                "<h3>Settings</h3><form method='POST' action='/settings'><label>Latitude (e.g. 51.5074)</label>");
  h += "<input name='lat' id='lat' inputmode='decimal' value='";
  if (cfg.has_loc && !demo_loc) h += String(cfg.lat, 4);
  h += tr("'><label>Földrajzi hosszúság (pl. 19.0402)</label>", "'><label>Longitude (e.g. -0.1278)</label>");
  h += "<input name='lon' id='lon' inputmode='decimal' value='";
  if (cfg.has_loc && !demo_loc) h += String(cfg.lon, 4);
  h += "'><button type='button' id='ipb' style='background:#444;color:#eee'>";
  h += tr("Hely keresése IP alapján", "Find location by IP");
  h += "</button><p id='ipr' style='color:#aaa;margin:0'></p><label>";
  h += tr("Időzóna", "Time zone");
  h += "</label><select id='tzs' style='font-size:18px;width:100%;margin:14px 0'>";
  bool known = false;
  for (const TzDef &z : TZ_LIST) {
    bool sel = strcmp(z.posix, cfg.tz) == 0;
    known |= sel;
    h += "<option value='"; h += z.posix; h += "' data-iana='"; h += z.iana; h += "'"; h += sel ? " selected>" : ">"; h += tr(z.name, z.name_en); h += "</option>";
  }
  h += "<option value=''"; h += known ? "" : " selected"; h += ">"; h += tr("Egyéb (POSIX TZ szöveg)", "Other (POSIX TZ string)"); h += "</option></select>"
       "<input name='tz' id='tz' value='"; h += cfg.tz; h += "'>";
  h += rooms_html();
  h += "<h3>Nyelv / Language</h3><select name='lang' style='font-size:18px;width:100%;margin:14px 0'>";
  h += g_lang ? "<option value='0'>Magyar</option><option value='1' selected>English</option>"
              : "<option value='0' selected>Magyar</option><option value='1'>English</option>";
  h += "</select>";
  h += tr("<h3>Éjszakai mód</h3>", "<h3>Night mode</h3>");
  h += "<label style='display:block'><input type='checkbox' name='n_on'";
  if (cfg.n_on) h += " checked";
  char nb[500];
  snprintf(nb, sizeof(nb), tr(" style='width:auto;margin:0 8px 0 0'>Bekapcsolva</label>"
                              "<label>Időzítés</label><select name='n_mode' style='font-size:18px;width:100%%;margin:14px 0'>"
                              "<option value='0'%s>Óra szerint (a lenti időpontok)</option>"
                              "<option value='1'%s>Napnyugta szerint (hely kell hozzá)</option></select>",
                              " style='width:auto;margin:0 8px 0 0'>On</label>"
                              "<label>Schedule</label><select name='n_mode' style='font-size:18px;width:100%%;margin:14px 0'>"
                              "<option value='0'%s>By clock (the times below)</option>"
                              "<option value='1'%s>By sunset (needs the location)</option></select>"),
           cfg.n_mode ? "" : " selected", cfg.n_mode ? " selected" : "");
  h += nb;
  snprintf(nb, sizeof(nb), tr("<label>Óra szerint: kezdete</label><input type='time' name='n_st' value='%02d:%02d'>"
                              "<label>Óra szerint: vége</label><input type='time' name='n_en' value='%02d:%02d'>",
                              "<label>By clock: start</label><input type='time' name='n_st' value='%02d:%02d'>"
                              "<label>By clock: end</label><input type='time' name='n_en' value='%02d:%02d'>"),
           cfg.n_st / 60, cfg.n_st % 60, cfg.n_en / 60, cfg.n_en % 60);
  h += nb;
  snprintf(nb, sizeof(nb), tr("<label>Napnyugta szerint: kezdete a napnyugta után (perc, -120..180)</label>"
                              "<input type='number' name='n_so' min='-120' max='180' step='5' value='%d'>"
                              "<label>Napnyugta szerint: vége a napkelte után (perc, -120..180)</label>"
                              "<input type='number' name='n_ro' min='-120' max='180' step='5' value='%d'>",
                              "<label>By sunset: start, minutes after sunset (-120..180)</label>"
                              "<input type='number' name='n_so' min='-120' max='180' step='5' value='%d'>"
                              "<label>By sunset: end, minutes after sunrise (-120..180)</label>"
                              "<input type='number' name='n_ro' min='-120' max='180' step='5' value='%d'>"),
           cfg.n_so, cfg.n_ro);
  h += nb;
  snprintf(nb, sizeof(nb), tr("<label>Éjszakai fényerő (%%, 1-100)</label><input type='number' name='n_pct' min='1' max='100' value='%d'>"
                              "<label>Nappali fényerő (%%, 10-100)</label><input type='number' name='d_pct' min='10' max='100' value='%d'>",
                              "<label>Night brightness (%%, 1-100)</label><input type='number' name='n_pct' min='1' max='100' value='%d'>"
                              "<label>Day brightness (%%, 10-100)</label><input type='number' name='d_pct' min='10' max='100' value='%d'>"),
           cfg.n_pct, cfg.d_pct);
  h += nb;
  h += tr("<p style='color:#aaa;margin:0'>A kijelzőn is állítható (az időpontok 15 perces lépésekben): a főoldalon az órára koppintva.</p>",
          "<p style='color:#aaa;margin:0'>Also adjustable on the display (times in 15-minute steps): tap the clock on the main page.</p>");
#if KIJELZO_P4
  h += "<input type='hidden' name='cam_f' value='1'><label style='display:block'><input type='checkbox' name='cam'";
  if (cfg.cam_on) h += " checked";
  h += " style='width:auto;margin:0 8px 0 0'>";
  h += tr("Automatikus fényerő a kamerával</label><p style='color:#aaa;margin:0'>A kijelző fénye a szoba fényéhez igazodik, "
          "az éjszakai (sötétben) és a nappali fényerő (világosban) között; ha 10 percig senki sem mozog előtte, az éjszakai "
          "fényerőre sötétül, mozgásra vagy érintésre felébred. Bekapcsolva ez veszi át az éjszakai mód időzítésének szerepét. "
          "A kamera képe nem kerül ki a kijelzőből, nem mentődik.</p>",
          "Automatic brightness with the camera</label><p style='color:#aaa;margin:0'>The display follows the room's light, "
          "between the night (dark) and day (bright) brightness; if nobody moves in front of it for 10 minutes, it dims to "
          "the night brightness and wakes up on movement or touch. When on, it replaces the night mode schedule. "
          "The camera image never leaves the display and is not saved.</p>");
  {
    p4_cam_stat_t c;
    p4_cam_get(&c);
    if (!c.ok) h += tr("<p style='color:#ffd54f;margin:0'>A kamera most nem működik (lásd fent, a „Kamera:” sorban), ezért az "
                       "éjszakai mód érvényes.</p>",
                       "<p style='color:#ffd54f;margin:0'>The camera is not working now (see the \"Camera:\" line above), so the "
                       "night mode applies.</p>");
  }
#endif
  h += ntfy_html();
#if KIJELZO_P4
  h += tr("<h3>Hangjelzés</h3>", "<h3>Sound alert</h3>");
  h += "<input type='hidden' name='snd_f' value='1'><label style='display:block'><input type='checkbox' name='snd'";
  if (cfg.snd_on) h += " checked";
  h += " style='width:auto;margin:0 8px 0 0'>";
  h += tr("Hangjelzés, amikor megjelenik a \"Szellőztess\"</label><label>Hangerő (választáskor megszólal; mentés a lenti gombbal)</label>",
          "Sound alert when \"Ventilate\" appears</label><label>Volume (plays when chosen; save with the button below)</label>");
  h += "<select name='sndv' id='sndv' style='font-size:18px;width:100%;margin:14px 0'>";
  {
    static const char *VOL[2][3] = {{"Halk", "Közepes", "Hangos"}, {"Low", "Medium", "Loud"}};
    for (int i = 0; i < 3; i++) { h += "<option value='"; h += i; h += cfg.snd_vol == i ? "' selected>" : "'>"; h += VOL[g_lang][i]; h += "</option>"; }
  }
  h += "</select><button type='button' id='sndt' style='background:#444;color:#eee'>";
  h += tr("Meghallgatás", "Listen");
  h += "</button><script>(function(){var s=document.getElementById('sndv');function t(){fetch('/beeptest?v='+s.value,{method:'POST'});}"
       "s.onchange=t;document.getElementById('sndt').onclick=t;})();</script>";
  h += tr("<p style='color:#aaa;margin:0'>Az éjszakai mód időszakában (óra vagy napnyugta szerint, lásd fent) nem szól, akkor sem, "
          "ha a sötétítés ki van kapcsolva. A kijelzőn is állítható: Beállítások / Rendszer.</p>",
          "<p style='color:#aaa;margin:0'>Silent during the night mode period (by clock or by sunset, see above), even if dimming "
          "is off. Also adjustable on the display: Settings / System.</p>");
#endif
  h += en_settings_html();
  h += forward_html();
  h += tr("<h3>Jelszó</h3>", "<h3>Password</h3>");
  h += "<label style='display:block;margin-top:10px'><input type='checkbox' name='chpw' id='chpw' style='width:auto;margin:0 8px 0 0'>";
  h += tr("Jelszó módosítása</label>"
          "<input type='password' name='newpw' id='newpw' maxlength='32' autocomplete='new-password' disabled "
          "placeholder='új jelszó (üresen hagyva: nincs jelszó)'>"
          "<input type='password' name='newpw2' id='newpw2' maxlength='32' autocomplete='new-password' disabled "
          "placeholder='új jelszó még egyszer'><p style='color:#aaa;margin:0'>Jelszó most: ",
          "Change password</label>"
          "<input type='password' name='newpw' id='newpw' maxlength='32' autocomplete='new-password' disabled "
          "placeholder='new password (empty: no password)'>"
          "<input type='password' name='newpw2' id='newpw2' maxlength='32' autocomplete='new-password' disabled "
          "placeholder='new password again'><p style='color:#aaa;margin:0'>Password now: ");
  h += cfg.pw[0] ? tr("be van állítva (felhasználónév: admin)", "set (user name: admin)") : tr("nincs", "none");
  h += "</p><hr style='border:0;border-top:1px solid #444;margin:18px 0 4px'><button type='submit'>";
  h += tr("Beállítások mentése</button><p style='color:#aaa;margin:0'>Egyszerre menti a helyet, az időzónát, a helyiségeket, az "
          "éjszakai módot, az értesítéseket, a továbbküldést és (ha bepipáltad) a jelszót.</p></form>"
          "<form method='POST' action='/ntfytest'><button type='submit' style='background:#444;color:#eee'>Próbaüzenet küldése a "
          "telefonra</button></form><p style='color:#aaa;margin:0'>A próbaüzenet a már elmentett témára megy.</p>"
          "<p style='color:#aaa'>A hely a napkelte/napnyugtához és az előrejelzéshez kell. Az IP alapú keresés a böngésződből kérdezi le "
          "az ipwho.is-t (tartalék: ip-api.com); ez csak városnyi pontosságú (gyakran a szolgáltató központja), mentés előtt ellenőrizd.</p>",
          "Save settings</button><p style='color:#aaa;margin:0'>Saves the location, time zone, rooms, night mode, notifications, "
          "forwarding and (if ticked) the password at once.</p></form>"
          "<form method='POST' action='/ntfytest'><button type='submit' style='background:#444;color:#eee'>Send a test message to "
          "the phone</button></form><p style='color:#aaa;margin:0'>The test message goes to the topic already saved.</p>"
          "<p style='color:#aaa'>The location is needed for sunrise/sunset and the forecast. The IP lookup queries ipwho.is from your "
          "browser (fallback: ip-api.com); it is only accurate to a city (often the provider's location), check it before saving.</p>");
  h += "<script>";
  h += tr("var L=['A két jelszó nem egyezik.','Keresés...','Találat: ',' - mentés előtt ellenőrizd!',"
          "'Nem sikerült: egyik helykereső szolgáltatás sem érhető el.'];",
          "var L=['The two passwords do not match.','Searching...','Found: ',' - check it before saving!',"
          "'Failed: none of the location services is reachable.'];");
  h += "var s=document.getElementById('tzs'),t=document.getElementById('tz');"
       "var p1=document.getElementById('newpw'),p2=document.getElementById('newpw2');"
       "document.getElementById('chpw').onchange=function(){p1.disabled=p2.disabled=!this.checked;};"
       "p1.closest('form').onsubmit=function(){if(!p1.disabled&&p1.value!==p2.value){alert(L[0]);return false;}};"
       "s.onchange=function(){if(s.value)t.value=s.value;};"
       // IP alapú helykeresés a böngészőből: előbb ipwho.is, ha nem megy, ip-api.com (mindkettő legfeljebb 8 mp)
       "function get(u){var c=new AbortController(),tm=setTimeout(function(){c.abort();},8000);"
       "return fetch(u,{signal:c.signal}).then(function(x){clearTimeout(tm);return x.json();});}"
       "function ipwho(){return get('https://ipwho.is/?fields=success,latitude,longitude,city,timezone').then(function(j){"
       "if(!j.success)throw 0;return{lat:j.latitude,lon:j.longitude,city:j.city,tz:j.timezone&&j.timezone.id};});}"
       "function ipapi(){return get('http://ip-api.com/json/?fields=status,lat,lon,city,timezone').then(function(j){"
       "if(j.status!=='success')throw 0;return{lat:j.lat,lon:j.lon,city:j.city,tz:j.timezone};});}"
       "document.getElementById('ipb').onclick=function(){var r=document.getElementById('ipr');r.textContent=L[1];"
       "ipwho().catch(ipapi).then(function(j){"
       "document.getElementById('lat').value=j.lat.toFixed(4);document.getElementById('lon').value=j.lon.toFixed(4);"
       "r.textContent=L[2]+j.city+' ('+j.tz+')'+L[3];"
       "for(var i=0;i<s.options.length;i++){if(s.options[i].dataset.iana===j.tz){s.selectedIndex=i;t.value=s.value;}}"
       "}).catch(function(){r.textContent=L[4];});};</script>";
  return h;
}

// A konzolok utolsó küldésének nyers mezői (hibakereséshez)
static void handle_raw_page() {
  if (!ota_auth_ok()) { request_auth(); return; }
  String h = String("<!doctype html><html><head><meta charset='utf-8'><title>") + tr("Nyers adatok", "Raw data") + "</title></head>"
             "<body style='font-family:monospace;background:#121212;color:#eee;padding:16px'>";
  for (const Sender &x : senders) {
    if (!x.count) continue;
    h += "<h3>"; h += x.room >= 0 ? rooms[x.room].name : tr("ISMERETLEN", "UNKNOWN"); h += " &ndash; "; h += x.model; h += "</h3><pre>";
    h += x.raw; h += "</pre>";
  }
  h += "</body></html>";
  cur_srv->send(200, "text/html; charset=utf-8", h);
}
