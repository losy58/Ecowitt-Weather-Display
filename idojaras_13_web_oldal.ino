// Webes kezelőoldal II.: a főoldal és a firmware-feltöltés
// (Az idojaras.ino része: az Arduino IDE és a PlatformIO a fájlokat ábécésorrendben, egy programmá fűzi össze.)

static unsigned lvgl_used_pct() {
  lv_mem_monitor_t mon;
  lv_mem_monitor(&mon);
  return mon.used_pct;
}

// A kérés a beállító hálózatról jön-e (a telefon a kijelző saját WiFi-jén): ott jelszó nélkül a beállító oldal látszik
static bool portal_client() {
  return wifiPortalActive() && cur_srv->client().localIP() == WiFi.softAPIP();
}

#define TL(hu, en_) (en ? (en_) : (hu))   // a WiFi-oldalak szövege az oldal nyelvén (a beállító oldalon választható)

// Új hálózat megadása (listából): a kezelőoldalon és a beállító oldalon is. A beállító oldalon a lista magától betöltődik,
// és a hely (napkelte, előrejelzés) is megadható.
static String wifi_form_html(bool en, bool portal) {
  String h = "<form method='POST' action='/wifiset' id='wf'>";
  if (portal) { h += "<input type='hidden' name='l' value='"; h += en ? "en" : "hu"; h += "'>"; }
  h += "<button type='button' id='wsb' style='background:#444;color:#eee'>";
  h += TL("Hálózatok keresése", "Search for networks");
  h += "</button><select id='wsl' style='font-size:18px;width:100%;margin:6px 0'><option value=''>";
  h += TL("(előbb keresd meg a hálózatokat)", "(search for networks first)");
  h += "</option></select><label>";
  h += TL("Hálózat neve", "Network name");
  h += "</label><input name='ssid' id='wss' maxlength='32' required><label>";
  h += TL("Jelszó", "Password");
  h += "</label><input name='pass' id='wsp' type='password' maxlength='64' autocomplete='new-password'>"
       "<label style='display:block'><input type='checkbox' id='wsv' style='width:auto;margin:0 8px 0 0'>";
  h += TL("Jelszó mutatása", "Show password");
  h += "</label>";
  if (portal) {
    char la[16] = "", lo[16] = "";
    if (cfg.has_loc) { snprintf(la, sizeof(la), "%.4f", cfg.lat); snprintf(lo, sizeof(lo), "%.4f", cfg.lon); }
    h += "<details><summary>";
    h += TL("Hely (nem kötelező; a napkeltéhez és az előrejelzéshez)", "Location (optional; for sunrise and the forecast)");
    h += "</summary><label>";
    h += TL("Földrajzi szélesség (pl. 47.4979)", "Latitude (e.g. 51.5074)");
    h += "</label><input name='lat' inputmode='decimal' maxlength='15' value='"; h += la; h += "'><label>";
    h += TL("Földrajzi hosszúság (pl. 19.0402)", "Longitude (e.g. -0.1278)");
    h += "</label><input name='lon' inputmode='decimal' maxlength='15' value='"; h += lo; h += "'></details>";
  }
  h += "<button type='submit'>";
  h += portal ? TL("Csatlakozás", "Connect") : TL("Csatlakozás az új hálózathoz", "Connect to the new network");
  h += "</button></form>"
       "<script>(function(){var l=document.getElementById('wsl'),s=document.getElementById('wss'),p=document.getElementById('wsp'),"
       "b=document.getElementById('wsb');"
       "document.getElementById('wsv').onchange=function(){p.type=this.checked?'text':'password';};"
       "l.onchange=function(){if(l.value)s.value=l.value;};";
  h += TL("var T=['Keresés...',', jelszavas',', nyílt','Hálózatok keresése újra','Nem sikerült, újra','Nem találtam hálózatot, újra'];",
          "var T=['Searching...',', secured',', open','Search again','Failed, try again','No network found, try again'];");
  h += "function scan(){b.textContent=T[0];"
       "fetch('/wifiscan',{cache:'no-store'}).then(function(x){return x.json();}).then(function(a){l.innerHTML='';"
       "a.forEach(function(n){var o=document.createElement('option');o.value=n.s;"
       "o.textContent=n.s+' ('+n.r+' dBm'+(n.l?T[1]:T[2])+')';l.appendChild(o);});"
       "if(a.length&&!s.value){s.value=a[0].s;}b.textContent=a.length?T[3]:T[5];}).catch(function(){b.textContent=T[4];});}"
       "b.onclick=scan;";
  if (portal) h += "scan();";   // a beállító oldalon azonnal
  h += "})();</script>";   // megerősítő ablak nincs: egyes böngészők nem mutatják (és akkor nem küldenék el); sikertelenül úgyis visszaáll
  return h;
}

// WiFi a kezelőoldalon: a jelenlegi kapcsolat, új hálózat megadása, beállító hálózat, a mentett hálózat elfelejtése
static String wifi_html() {
  const bool en = g_lang;
  char b[300];
  String h = "<h3>WiFi</h3><p>";
  if (WiFi.status() == WL_CONNECTED)
    snprintf(b, sizeof(b), TL("Hálózat: <b>%s</b>, jelerősség: %d dBm, adóteljesítmény: %s, IP: %s",
                              "Network: <b>%s</b>, signal: %d dBm, TX power: %s, IP: %s"),
             html_attr(WiFi.SSID().c_str()).c_str(), (int)WiFi.RSSI(), wifiTxName(), WiFi.localIP().toString().c_str());
  else
    snprintf(b, sizeof(b), "%s", TL("Nincs kapcsolat.", "Not connected."));
  h += b;
  h += "</p>";
  h += wifi_form_html(en, false);
  h += "<p style='color:#aaa;margin:0'>";
  h += TL("A kijelző kipróbálja az új hálózatot; ha 25 mp alatt nem sikerül, visszaáll a mostanira. "
          "Új hálózaton más IP-címet kaphat: ezt a kijelző felső sorában látod (a konzoloknál is át kell írni).",
          "The display tries the new network; if it does not succeed within 25 s, it goes back to the current one. "
          "On a new network it may get a different IP address: you see it in the top line of the display (it must also be "
          "changed in the consoles).");
  h += "</p><form method='POST' action='/wifiportal'><button type='submit' style='background:#444;color:#eee'>";
  snprintf(b, sizeof(b), TL("Beállító hálózat (%s) indítása 5 percre", "Open the setup network (%s) for 5 minutes"), wifiApName());
  h += b;
  h += "</button></form><form method='POST' action='/wififorget' onsubmit=\"return confirm('";
  h += TL("A mentett hálózat törlődik, és a kijelző a beállító hálózatot indítja; ezen a hálózaton nem lesz elérhető. Mehet?",
          "The saved network is deleted and the display opens the setup network; it will not be reachable on this network. Continue?");
  h += "')\"><button type='submit' style='background:#6a2a1f;color:#eee'>";
  h += TL("WiFi elfelejtése", "Forget WiFi");
  h += "</button></form>";
  return h;
}

// A beállító oldal (a beállító hálózatról a "/" és minden ismeretlen cím ide vezet). Nyelve választható (?l=hu / ?l=en).
static void handle_portal_page() {
  const String la = cur_srv->arg("l");
  const bool en = la == "en" ? true : la == "hu" ? false : (bool)g_lang;
  char b[300], ss[40];
  String h = "<!doctype html><html lang='";
  h += en ? "en" : "hu";
  h += "'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
       "<title>Ecowitt Weather Display - WiFi</title><style>body{font-family:sans-serif;background:#121212;color:#eee;max-width:480px;"
       "margin:20px auto;padding:0 16px}a{color:#ff8a3d}summary{margin:10px 0;cursor:pointer}"
       "input,button{font-size:18px;margin:10px 0;width:100%;box-sizing:border-box}button{padding:12px;background:#ff8a3d;border:0;"
       "border-radius:8px;color:#000;cursor:pointer}button:disabled{opacity:.6}</style></head><body>"
       "<p style='text-align:right;margin:0'><a href='/?l=hu'>Magyar</a> | <a href='/?l=en'>English</a></p>"
       "<h2>Ecowitt Weather Display</h2><h3>";
  h += TL("WiFi beállítása", "WiFi setup");
  h += "</h3><p>";
  if (WiFi.status() == WL_CONNECTED)
    snprintf(b, sizeof(b), TL("Most: <b>%s</b> (IP: %s). Ha nem adsz meg újat, ez marad.", "Now: <b>%s</b> (IP: %s). If you set no new one, this one stays."),
             html_attr(WiFi.SSID().c_str()).c_str(), WiFi.localIP().toString().c_str());
  else if (wifiSavedSsid(ss, sizeof(ss)))
    snprintf(b, sizeof(b), TL("Mentett hálózat: <b>%s</b> - most nem érhető el.", "Saved network: <b>%s</b> - not available now."), html_attr(ss).c_str());
  else
    snprintf(b, sizeof(b), "%s", TL("Még nincs beállított WiFi-hálózat.", "No WiFi network is set yet."));
  h += b;
  h += "</p><p>";
  h += TL("Válaszd ki a hálózatot a listából (a keresés pár másodperc), add meg a jelszavát, és nyomd meg a Csatlakozás gombot.",
          "Choose your network from the list (searching takes a few seconds), enter its password and press Connect.");
  h += "</p>";
  h += wifi_form_html(en, true);
  h += "<p style='color:#aaa'>";
  h += TL("Csatlakozás után a kezelőoldal (beállítások, frissítés) a saját hálózatodon: http://" MDNS_HOST ".local/",
          "After connecting, the web page (settings, updates) is on your own network: http://" MDNS_HOST ".local/");
  h += "</p></body></html>";
  cur_srv->send(200, "text/html; charset=utf-8", h);
}

// Ismeretlen cím: a beállító hálózaton a beállító oldalra irányít (így nyitja meg a telefon magától: "captive portal")
static void handle_not_found() {
  if (portal_client()) {
    cur_srv->sendHeader("Location", String("http://") + wifiApAddr() + "/", true);
    cur_srv->send(302, "text/plain", "");
    return;
  }
  cur_srv->send(404, "text/plain", "Not found");
}

static void handle_wifi_scan() {
  if (!portal_client() && !ota_auth_ok()) { request_auth(); return; }
  cur_srv->send(200, "application/json", wifiScanJson());
}

// az átállás a válasz elküldése után, a loop()-ban fut (wifi_switch_task)
static char wifi_new_ssid[33] = "", wifi_new_pass[65] = "";
static bool wifi_switch_pending = false, wifi_forget_pending = false;
static int wifi_switch_state = 0;   // 0: nincs, 1: folyamatban, 2: sikerült, 3: nem sikerült (visszaállt a régire)

// az átállás állapota JSON-ban (az "Átállás folyamatban" oldal ezt kérdezi le)
static void handle_wifi_status() {
  char b[160];
  snprintf(b, sizeof(b), "{\"state\":%d,\"ssid\":\"%s\",\"ip\":\"%s\"}", wifi_switch_state,
           WiFi.status() == WL_CONNECTED ? WiFi.SSID().c_str() : "", WiFi.localIP().toString().c_str());
  cur_srv->send(200, "application/json", b);
}

static void handle_wifi_set() {
  const bool portal = portal_client();
  if (!portal && !ota_auth_ok()) { request_auth(); return; }
  const String lang = cur_srv->arg("l");
  const bool en = lang.length() ? lang == "en" : (bool)g_lang;
  const String back = portal ? String("/?l=") + (en ? "en" : "hu") : String("/");
  String ss = cur_srv->arg("ssid"), pw = cur_srv->arg("pass");
  ss.trim();
  if (ss.isEmpty() || ss.length() > 32 || pw.length() > 64 || (pw.length() > 0 && pw.length() < 8)) {
    cur_srv->send(400, "text/html; charset=utf-8", String("<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
                  "</head><body style='font-family:sans-serif;background:#121212;color:#eee;padding:30px'><h2>") +
                  TL("Nem állítottam át</h2><p>Hibás hálózatnév vagy jelszó (a jelszó legalább 8 karakter, vagy üres a nyílt hálózatnál).",
                     "Not changed</h2><p>Invalid network name or password (the password is at least 8 characters, or empty for an open network).") +
                  "</p><p><a style='color:#ff8a3d' href='" + back + "'>" + TL("Vissza", "Back") + "</a></p></body></html>");
    return;
  }
  // a beállító oldalon megadott hely (csak ha mindkét mező érvényes)
  String la = cur_srv->arg("lat"), lo = cur_srv->arg("lon");
  la.trim(); lo.trim(); la.replace(',', '.'); lo.replace(',', '.');
  const double lat = la.toDouble(), lon = lo.toDouble();
  if (la.length() && lo.length() && lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180 && !(lat == 0 && lon == 0) &&
      (!cfg.has_loc || lat != cfg.lat || lon != cfg.lon)) {
    {
      CamPause cam_pause_;   // P4: a kamera áll a mentés alatt
      Preferences p;
      p.begin("cfg", false);
      p.putDouble("lat", lat);
      p.putDouble("lon", lon);
      p.end();
    }
    cfg_load();
    fc.valid = false; fc_force = true;   // új hely: új előrejelzés
    sun_last_key = -1;
    LogOut.println("Hely mentve (a beállító oldalon)");
  }
  strlcpy(wifi_new_ssid, ss.c_str(), sizeof(wifi_new_ssid));
  strlcpy(wifi_new_pass, pw.c_str(), sizeof(wifi_new_pass));
  wifi_switch_pending = true;
  wifi_switch_state = 1;
  cur_srv->sendHeader("Connection", "close");
  // az oldal 2 mp-enként lekérdezi az állapotot (az átállás alatt a kijelző nem válaszol), és kiírja az eredményt
  char ap[200];
  snprintf(ap, sizeof(ap), TL(" A beállító hálózat (%s) 1 perc múlva bezárul. Kezelőoldal: http://" MDNS_HOST ".local/",
                              " The setup network (%s) closes in 1 minute. Web page: http://" MDNS_HOST ".local/"), wifiApName());
  String pg = String("<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'></head>"
                "<body style='font-family:sans-serif;background:#121212;color:#eee;padding:30px'><h2 id='h'>") + TL("Átállás folyamatban...", "Switching...") +
              "</h2><p>" + TL("Csatlakozás ide: ", "Connecting to: ") + "<b>" + html_attr(wifi_new_ssid) + "</b></p><p id='m'>" +
              TL("Várakozás a kijelzőre...", "Waiting for the display...") + "</p><p><a style='color:#ff8a3d' href='" + back + "'>" +
              (portal ? TL("Vissza a beállító oldalra", "Back to the setup page") : TL("Vissza a kezelőoldalra", "Back to the web page")) + "</a></p><script>" +
              TL("var T=['Sikerült','A kijelző átállt ide: ',', IP-cím: ','Nem sikerült','Nem tudott csatlakozni (rossz jelszó, vagy nem "
                 "elérhető a hálózat), ezért visszaállt a régire: ','Folyamatban... (',' mp)','A kijelző nem érhető el',"
                 "'Ezen a hálózaton nem válaszol: valószínűleg átállt az új hálózatra. Az új IP-cím a kijelző felső sorában látszik.',"
                 "'A kijelző most nem válaszol (csatlakozik)... (','Nem tudott csatlakozni (rossz jelszó, vagy nem elérhető a hálózat). "
                 "Próbáld újra.'];",
                 "var T=['Done','The display switched to: ',', IP address: ','Failed','It could not connect (wrong password, or the "
                 "network is not available), so it went back to the old one: ','In progress... (',' s)','The display is not reachable',"
                 "'It does not answer on this network: it has probably switched to the new network. The new IP address is shown in "
                 "the top line of the display.','The display does not answer right now (connecting)... (','It could not connect "
                 "(wrong password, or the network is not available). Try again.'];") +
              "var P=" + (portal ? "1" : "0") + ",A='" + ap + "';"
              "var t0=Date.now(),h=document.getElementById('h'),m=document.getElementById('m');"
              "function poll(){var s=Math.round((Date.now()-t0)/1000);"
              "fetch('/wifistatus',{cache:'no-store'}).then(function(x){return x.json();}).then(function(j){"
              "if(j.state==2){h.textContent=T[0];m.textContent=T[1]+j.ssid+T[2]+j.ip+(P?A:'');return;}"
              "if(j.state==3){h.textContent=T[3];m.textContent=j.ssid?T[4]+j.ssid+' ('+j.ip+').':T[10];return;}"
              "m.textContent=T[5]+s+T[6];setTimeout(poll,2000);"
              "}).catch(function(){if(s>90){h.textContent=T[7];m.textContent=T[8];return;}"
              "m.textContent=T[9]+s+T[6];setTimeout(poll,2000);});}"
              "setTimeout(poll,3000);</script></body></html>";
  cur_srv->send(200, "text/html; charset=utf-8", pg);
}

static void wifi_switch_task() {
  if (wifi_forget_pending) {
    wifi_forget_pending = false;
    delay(500);   // a válasz kimenjen
    LogOut.println("WiFi elfelejtése (a kezelőoldalon)");
    wifiForget();
    return;
  }
  if (!wifi_switch_pending) return;
  wifi_switch_pending = false;
  delay(500);   // a válasz kimenjen
  LogOut.printf("WiFi átállás: %s\n", wifi_new_ssid);
  const bool ok = wifiSwitchTo(wifi_new_ssid, wifi_new_pass, 25000UL);
  wifi_switch_state = ok ? 2 : 3;
  memset(wifi_new_pass, 0, sizeof(wifi_new_pass));
  LogOut.println(ok ? "WiFi átállás sikerült" : "WiFi átállás nem sikerült, vissza a régire");
  if (ok) { wifiTakeConnected(); wifi_connected(); }   // pontos idő, mDNS az új hálózaton, a WiFi-panelen a siker (egyszer)
}

static void handle_wifi_portal() {
  if (!ota_auth_ok()) { request_auth(); return; }
  const bool en = g_lang;
  wifiPortalStart(5UL * 60UL * 1000UL);
  wp_open();
  char b[400];
  snprintf(b, sizeof(b), TL("Beállító hálózat</h2><p>5 percre elindult a \"%s\" hálózat (a mostani kapcsolat megmarad). Csatlakozz rá "
                            "telefonnal: a telefon magától megnyitja a beállító oldalt (ha nem: http://%s/).",
                            "Setup network</h2><p>The \"%s\" network is open for 5 minutes (the current connection stays). Connect to it "
                            "with a phone: it opens the setup page by itself (if not: http://%s/)."), wifiApName(), wifiApAddr());
  cur_srv->send(200, "text/html; charset=utf-8", String("<html><head><meta charset='utf-8'></head><body style='font-family:sans-serif;"
                "background:#121212;color:#eee;padding:30px'><h2>") + b + "</p><p><a style='color:#ff8a3d' href='/'>" +
                TL("Vissza a kezelőoldalra", "Back to the web page") + "</a></p></body></html>");
}

static void handle_wifi_forget() {
  if (!ota_auth_ok()) { request_auth(); return; }
  const bool en = g_lang;
  char b[400];
  snprintf(b, sizeof(b), TL("WiFi elfelejtése</h2><p>A mentett hálózat törlődik, és a kijelző elindítja a \"%s\" beállító hálózatot. "
                            "Ezen a hálózaton a kijelző már nem érhető el.",
                            "Forget WiFi</h2><p>The saved network is deleted and the display opens the \"%s\" setup network. "
                            "The display is no longer reachable on this network."), wifiApName());
  cur_srv->sendHeader("Connection", "close");
  cur_srv->send(200, "text/html; charset=utf-8", String("<html><head><meta charset='utf-8'></head><body style='font-family:sans-serif;"
                "background:#121212;color:#eee;padding:30px'><h2>") + b + "</p></body></html>");
  wifi_forget_pending = true;
}
#undef TL

static void handle_update_page() {
  if (portal_client()) { handle_portal_page(); return; }   // a beállító hálózatról: jelszó nélkül csak a WiFi beállítása
  if (!ota_auth_ok()) { request_auth(); return; }
  String h = "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
             "<title>Ecowitt Weather Display</title><style>body{font-family:sans-serif;background:#121212;color:#eee;max-width:480px;margin:30px auto;padding:0 16px}"
             "input,button{font-size:18px;margin:14px 0;width:100%}button{padding:12px;background:#ff8a3d;border:0;border-radius:8px;color:#000;cursor:pointer;transition:filter .1s,transform .1s}"
             "button:active{filter:brightness(1.4);transform:scale(.97)}button:disabled{opacity:.6}</style></head><body>"
             "<h2>Ecowitt Weather Display</h2><p>";
  h += tr("Futó program: ", "Running firmware: ");
  h += FW_NAME;
  uint32_t up = millis() / 1000;
  char st[320];
  snprintf(st, sizeof(st), tr("</p><p>Üzemidő: %u nap %u óra %u perc<br>Képfolyam-zavar (képkocka): %u<br>Panel-újraépítés: %u<br>"
                              "Szabad belső RAM: %u KB (legkevesebb: %u KB)<br>LVGL memória: %u%% foglalt<br>WiFi: %d dBm, adóteljesítmény %s</p>",
                              "</p><p>Uptime: %u d %u h %u min<br>Video stream glitches (frames): %u<br>Panel rebuilds: %u<br>"
                              "Free internal RAM: %u KB (minimum: %u KB)<br>LVGL memory: %u%% used<br>WiFi: %d dBm, TX power %s</p>"),
           (unsigned)(up / 86400), (unsigned)(up / 3600 % 24), (unsigned)(up / 60 % 60), (unsigned)g_vs_glitch, (unsigned)g_rebuild_count,
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024), (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024),
           (unsigned)lvgl_used_pct(), (int)WiFi.RSSI(), wifiTxName());
  h += st;
#if KIJELZO_P4
  h += tr("<p>Akku: ", "<p>Battery: ");
  h += batt_text();
  h += tr("</p><p>Kamera: ", "</p><p>Camera: ");
  h += cam_text();
  h += "</p>";
#endif
  h += tr("<p>Energia: ", "<p>Energy: ");
  h += en_text();
  h += "</p>";
  if (g_demo) h += tr("<p style='color:#ffd54f'><b>Bemutató mód:</b> a kijelző kitalált adatokat mutat, a konzolok adatait nem használja, "
                      "és nem ment előzményt. Kikapcsolás a kijelzőn: Beállítások / Rendszer / Bemutató kikapcsolása.</p>",
                      "<p style='color:#ffd54f'><b>Demo mode:</b> the display shows made-up data, does not use the consoles' data and "
                      "does not save history. To turn it off on the display: Settings / System / Turn off demo.</p>");
  h += tr("<p><b>Konzolok</b><br>", "<p><b>Consoles</b><br>");
  for (const Sender &x : senders) {
    if (!x.count) continue;
    char sl[260];
    const Room *xr = x.room >= 0 ? &rooms[x.room] : NULL;
    char co2s[24] = "", batts[24] = "";   // csak amit a konzol ténylegesen küld (a GW3000A-nak pl. nincs CO2-je és akkuja)
    if (xr && xr->has_co2)  snprintf(co2s, sizeof(co2s), ", CO2 %d ppm", (int)xr->co2);
    if (xr && xr->has_batt) snprintf(batts, sizeof(batts), tr(", akku %.2f V", ", battery %.2f V"), xr->batt);
    snprintf(sl, sizeof(sl), tr("%s &ndash; %s | model=%s | stationtype=%s | %u db, %u mp-e | %.1f &deg;C, %.0f %%%s%s<br>",
                                "%s &ndash; %s | model=%s | stationtype=%s | %u uploads, %u s ago | %.1f &deg;C, %.0f %%%s%s<br>"),
             xr ? xr->name : tr("ISMERETLEN", "UNKNOWN"), IPAddress(x.ip).toString().c_str(), x.model, x.st,
             (unsigned)x.count, (unsigned)((millis() - x.last_ms) / 1000),
             xr ? xr->t : 0.0f, xr ? xr->h : 0.0f, co2s, batts);
    h += sl;
  }
  h += "</p>";
  h += diag_html();
  h += wifi_html();
  h += settings_html();
  h += tr("<h3>Firmware frissítés</h3><p>Válaszd ki a <b>.bin</b> fájlt (a kiadás <i>…-ota.bin</i> fájlja, vagy PlatformIO-val "
          "fordítva: <i>.pio/build/&lt;környezet&gt;/firmware.bin</i>).</p>",
          "<h3>Firmware update</h3><p>Choose the <b>.bin</b> file (the release's <i>…-ota.bin</i> file, or built with PlatformIO: "
          "<i>.pio/build/&lt;environment&gt;/firmware.bin</i>).</p>");
  h += "<form id='f' method='POST' enctype='multipart/form-data'><input type='file' name='firmware' accept='.bin' required>"
       "<button type='submit'>";
  h += tr("Feltöltés", "Upload");
  h += "</button></form>"
       "<script>document.getElementById('f').onsubmit=function(){this.action='/update?size='+this.firmware.files[0].size;};</script>"
       // visszajelzés: elküldéskor a gomb felirata "Folyamatban...", és elhalványul (ha az űrlapot nem állította meg egy ellenőrzés)
       "<script>document.querySelectorAll('form').forEach(function(f){f.addEventListener('submit',function(e){"
       "setTimeout(function(){if(e.defaultPrevented)return;var b=f.querySelector('button[type=submit]');"
       "if(b){b.textContent='";
  h += tr("Folyamatban...", "In progress...");
  h += "';b.disabled=true;}},0);});});</script>"
       "</body></html>";
  cur_srv->send(200, "text/html; charset=utf-8", h);
}

static void handle_update_upload() {
  HTTPUpload &up = cur_srv->upload();
  if (up.status == UPLOAD_FILE_START) {
    ota_ok = false;
    ota_err = "";
    ota_done = 0;
    if (!ota_auth_ok()) { ota_err = tr("hibás jelszó", "wrong password"); return; }
    ota_total = (size_t)cur_srv->arg("size").toInt();
    ota_screen(tr("Firmware frissítés", "Firmware update"), tr("Ne kapcsold ki a készüléket!", "Do not switch off the device!"), 0);
#if KIJELZO_P4
    p4_cam_pause(true);   // P4: a kamera képfolyama a frissítés alatt áll (siker után úgyis újraindul)
#endif
    if (!Update.begin(ota_total ? ota_total : UPDATE_SIZE_UNKNOWN)) ota_err = Update.errorString();
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (ota_err.length()) return;
    esp_task_wdt_reset();   // a feltöltés a loop()-on belül hosszan tarthat
    if (Update.write(up.buf, up.currentSize) != up.currentSize) { ota_err = Update.errorString(); return; }
    ota_done += up.currentSize;
    static uint32_t last_ui = 0;
    if (millis() - last_ui > 300) {
      last_ui = millis();
      char b[48];
      snprintf(b, sizeof(b), "%u KB%s", (unsigned)(ota_done / 1024), ota_total ? "" : tr(" feltöltve", " uploaded"));
      int pct = ota_total ? (int)(ota_done * 100 / ota_total) : 0;
      if (ota_total) snprintf(b, sizeof(b), "%d %%  (%u / %u KB)", pct, (unsigned)(ota_done / 1024), (unsigned)(ota_total / 1024));
      ota_screen(tr("Firmware frissítés", "Firmware update"), b, pct);
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (ota_err.length()) {
      Update.abort();
#if KIJELZO_P4
      p4_cam_pause(false);
#endif
      return;
    }
    if (Update.end(true)) ota_ok = true;
    else ota_err = Update.errorString();
#if KIJELZO_P4
    if (!ota_ok) p4_cam_pause(false);
#endif
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    ota_err = tr("megszakadt a feltöltés", "the upload was interrupted");
#if KIJELZO_P4
    p4_cam_pause(false);
#endif
  }
}

static void handle_update_done() {
  if (!ota_auth_ok()) { request_auth(); return; }
  cur_srv->sendHeader("Connection", "close");
  if (ota_ok) {
    cur_srv->send(200, "text/html; charset=utf-8",
                   String("<html><head><meta charset='utf-8'></head><body style='font-family:sans-serif;background:#121212;color:#eee;padding:30px'>"
                   "<h2>") + tr("Sikeres frissítés</h2><p>A készülék újraindul.", "Update successful</h2><p>The device restarts.") +
                   "</p></body></html>");
    ota_screen(tr("Kész, újraindítás...", "Done, restarting..."), "", 100);
    hist_save();   // az utolsó (legfeljebb 15 perc) előzmény ne vesszen el az újraindítással
    delay(1200);
    diag_mark_restart(RC_OTA);
    ESP.restart();
  } else {
    String m = "<html><head><meta charset='utf-8'></head><body style='font-family:sans-serif;background:#121212;color:#eee;padding:30px'>"
               "<h2>";
    m += tr("Sikertelen frissítés", "Update failed");
    m += "</h2><p>";
    m += ota_err.length() ? ota_err.c_str() : tr("ismeretlen hiba", "unknown error");
    m += "</p><p>";
    m += tr("A régi program fut tovább. <a style='color:#ff8a3d' href='/update'>Újra</a>",
            "The old firmware keeps running. <a style='color:#ff8a3d' href='/update'>Again</a>");
    m += "</p></body></html>";
    cur_srv->send(500, "text/html; charset=utf-8", m);
    ota_screen_hide();
  }
}

