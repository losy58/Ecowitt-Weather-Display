// Webes kezelőoldal II.: a főoldal és a firmware-feltöltés
// (Az idojaras.ino része: az Arduino IDE és a PlatformIO a fájlokat ábécésorrendben, egy programmá fűzi össze.)

static unsigned lvgl_used_pct() {
  lv_mem_monitor_t mon;
  lv_mem_monitor(&mon);
  return mon.used_pct;
}

// WiFi: a jelenlegi kapcsolat, új hálózat megadása (listából), beállító portál
static String wifi_html() {
  char b[300];
  String h = "<h3>WiFi</h3><p>";
  if (WiFi.status() == WL_CONNECTED)
    snprintf(b, sizeof(b), tr("Hálózat: <b>%s</b>, jelerősség: %d dBm, adóteljesítmény: %s, IP: %s",
                              "Network: <b>%s</b>, signal: %d dBm, TX power: %s, IP: %s"),
             html_attr(WiFi.SSID().c_str()).c_str(), (int)WiFi.RSSI(), wifiTxName(), WiFi.localIP().toString().c_str());
  else
    snprintf(b, sizeof(b), "%s", tr("Nincs kapcsolat.", "Not connected."));
  h += b;
  h += "</p><form method='POST' action='/wifiset' id='wf'><button type='button' id='wsb' style='background:#444;color:#eee'>";
  h += tr("Hálózatok keresése", "Search for networks");
  h += "</button><select id='wsl' style='font-size:18px;width:100%;margin:6px 0'><option value=''>";
  h += tr("(előbb keresd meg a hálózatokat)", "(search for networks first)");
  h += "</option></select><label>";
  h += tr("Hálózat neve", "Network name");
  h += "</label><input name='ssid' id='wss' maxlength='32' required><label>";
  h += tr("Jelszó", "Password");
  h += "</label><input name='pass' id='wsp' type='password' maxlength='64' autocomplete='new-password'>"
       "<label style='display:block'><input type='checkbox' id='wsv' style='width:auto;margin:0 8px 0 0'>";
  h += tr("Jelszó mutatása", "Show password");
  h += "</label><button type='submit'>";
  h += tr("Csatlakozás az új hálózathoz", "Connect to the new network");
  h += "</button></form><p style='color:#aaa;margin:0'>";
  h += tr("A kijelző kipróbálja az új hálózatot; ha 25 mp alatt nem sikerül, visszaáll a mostanira. "
          "Új hálózaton más IP-címet kaphat: ezt a kijelző felső sorában látod (a konzoloknál is át kell írni).",
          "The display tries the new network; if it does not succeed within 25 s, it goes back to the current one. "
          "On a new network it may get a different IP address: you see it in the top line of the display (it must also be "
          "changed in the consoles).");
  h += "</p><form method='POST' action='/wifiportal'><button type='submit' style='background:#444;color:#eee'>";
  h += tr("Beállító portál indítása (újraindul, 5 perc)", "Start the setup portal (restarts, 5 minutes)");
  h += "</button></form>"
       "<script>(function(){var l=document.getElementById('wsl'),s=document.getElementById('wss'),p=document.getElementById('wsp');"
       "document.getElementById('wsv').onchange=function(){p.type=this.checked?'text':'password';};"
       "l.onchange=function(){if(l.value)s.value=l.value;};";
  h += tr("var T=['Keresés...',', jelszavas',', nyílt','Hálózatok keresése','Nem sikerült, újra'];",
          "var T=['Searching...',', secured',', open','Search for networks','Failed, try again'];");
  h += "document.getElementById('wsb').onclick=function(){var b=this;b.textContent=T[0];"
       "fetch('/wifiscan').then(function(x){return x.json();}).then(function(a){l.innerHTML='';"
       "a.forEach(function(n){var o=document.createElement('option');o.value=n.s;"
       "o.textContent=n.s+' ('+n.r+' dBm'+(n.l?T[1]:T[2])+')';l.appendChild(o);});"
       "if(a.length){s.value=a[0].s;}b.textContent=T[3];}).catch(function(){b.textContent=T[4];});};"
       "})();</script>";   // megerősítő ablak nincs: egyes böngészők nem mutatják (és akkor nem küldenék el); sikertelenül úgyis visszaáll
  return h;
}

static void handle_wifi_scan() {
  if (!ota_auth_ok()) { request_auth(); return; }
  cur_srv->send(200, "application/json", wifiScanJson());
}

// az átállás a válasz elküldése után, a loop()-ban fut (wifi_switch_task)
static char wifi_new_ssid[33] = "", wifi_new_pass[65] = "";
static bool wifi_switch_pending = false;
static int wifi_switch_state = 0;   // 0: nincs, 1: folyamatban, 2: sikerült, 3: nem sikerült (visszaállt a régire)

// az átállás állapota JSON-ban (az "Átállás folyamatban" oldal ezt kérdezi le)
static void handle_wifi_status() {
  char b[160];
  snprintf(b, sizeof(b), "{\"state\":%d,\"ssid\":\"%s\",\"ip\":\"%s\"}", wifi_switch_state,
           WiFi.status() == WL_CONNECTED ? WiFi.SSID().c_str() : "", WiFi.localIP().toString().c_str());
  cur_srv->send(200, "application/json", b);
}

static void handle_wifi_set() {
  if (!ota_auth_ok()) { request_auth(); return; }
  String ss = cur_srv->arg("ssid"), pw = cur_srv->arg("pass");
  ss.trim();
  if (ss.isEmpty() || ss.length() > 32 || pw.length() > 64 || (pw.length() > 0 && pw.length() < 8)) {
    cur_srv->send(400, "text/html; charset=utf-8", String("<html><head><meta charset='utf-8'></head><body style='font-family:sans-serif;"
                  "background:#121212;color:#eee;padding:30px'><h2>") +
                  tr("Nem állítottam át</h2><p>Hibás hálózatnév vagy jelszó (a jelszó legalább 8 karakter, vagy üres a nyílt hálózatnál).",
                     "Not changed</h2><p>Invalid network name or password (the password is at least 8 characters, or empty for an open network).") +
                  "</p><p><a style='color:#ff8a3d' href='/'>" + tr("Vissza", "Back") + "</a></p></body></html>");
    return;
  }
  strlcpy(wifi_new_ssid, ss.c_str(), sizeof(wifi_new_ssid));
  strlcpy(wifi_new_pass, pw.c_str(), sizeof(wifi_new_pass));
  wifi_switch_pending = true;
  wifi_switch_state = 1;
  cur_srv->sendHeader("Connection", "close");
  // az oldal 2 mp-enként lekérdezi az állapotot (az átállás alatt a kijelző nem válaszol), és kiírja az eredményt
  String pg = String("<html><head><meta charset='utf-8'></head><body style='font-family:sans-serif;"
                "background:#121212;color:#eee;padding:30px'><h2 id='h'>") + tr("Átállás folyamatban...", "Switching...") +
              "</h2><p>" + tr("Csatlakozás ide: ", "Connecting to: ") + "<b>" + html_attr(wifi_new_ssid) + "</b></p><p id='m'>" +
              tr("Várakozás a kijelzőre...", "Waiting for the display...") + "</p><p><a style='color:#ff8a3d' href='/'>" +
              tr("Vissza a kezelőoldalra", "Back to the web page") + "</a></p><script>" +
              tr("var T=['Sikerült','A kijelző átállt ide: ',', IP-cím: ','Nem sikerült','Nem tudott csatlakozni (rossz jelszó, vagy nem "
                 "elérhető a hálózat), ezért visszaállt a régire: ','Folyamatban... (',' mp)','A kijelző nem érhető el',"
                 "'Ezen a hálózaton nem válaszol: valószínűleg átállt az új hálózatra. Az új IP-cím a kijelző felső sorában látszik.',"
                 "'A kijelző most nem válaszol (csatlakozik)... ('];",
                 "var T=['Done','The display switched to: ',', IP address: ','Failed','It could not connect (wrong password, or the "
                 "network is not available), so it went back to the old one: ','In progress... (',' s)','The display is not reachable',"
                 "'It does not answer on this network: it has probably switched to the new network. The new IP address is shown in "
                 "the top line of the display.','The display does not answer right now (connecting)... ('];") +
              "var t0=Date.now(),h=document.getElementById('h'),m=document.getElementById('m');"
              "function poll(){var s=Math.round((Date.now()-t0)/1000);"
              "fetch('/wifistatus',{cache:'no-store'}).then(function(x){return x.json();}).then(function(j){"
              "if(j.state==2){h.textContent=T[0];m.textContent=T[1]+j.ssid+T[2]+j.ip;return;}"
              "if(j.state==3){h.textContent=T[3];m.textContent=T[4]+j.ssid+' ('+j.ip+').';return;}"
              "m.textContent=T[5]+s+T[6];setTimeout(poll,2000);"
              "}).catch(function(){if(s>90){h.textContent=T[7];m.textContent=T[8];return;}"
              "m.textContent=T[9]+s+T[6];setTimeout(poll,2000);});}"
              "setTimeout(poll,3000);</script></body></html>";
  cur_srv->send(200, "text/html; charset=utf-8", pg);
}

static void wifi_switch_task() {
  if (!wifi_switch_pending) return;
  wifi_switch_pending = false;
  delay(500);   // a válasz kimenjen
  LogOut.printf("WiFi átállás: %s\n", wifi_new_ssid);
  const bool ok = wifiSwitchTo(wifi_new_ssid, wifi_new_pass, 25000UL);
  wifi_switch_state = ok ? 2 : 3;
  memset(wifi_new_pass, 0, sizeof(wifi_new_pass));
  LogOut.println(ok ? "WiFi átállás sikerült" : "WiFi átállás nem sikerült, vissza a régire");
  MDNS.end();   // az új IP-n is elérhető legyen idojaras.local néven
  if (MDNS.begin(MDNS_NAME)) MDNS.addService("http", "tcp", 80);
}

static void handle_wifi_portal() {
  if (!ota_auth_ok()) { request_auth(); return; }
  cur_srv->send(200, "text/html; charset=utf-8", String("<html><head><meta charset='utf-8'></head><body style='font-family:sans-serif;"
                "background:#121212;color:#eee;padding:30px'><h2>") +
                tr("Beállító portál</h2><p>A kijelző újraindul, és 5 percre elindítja a \"Weather-Setup\" hálózatot. Csatlakozz rá "
                   "telefonnal, és válaszd ki a hálózatot.",
                   "Setup portal</h2><p>The display restarts and opens the \"Weather-Setup\" network for 5 minutes. Connect to it with "
                   "a phone and choose your network.") + "</p></body></html>");
  delay(500);
  hist_save();
  diag_mark_restart(RC_WIFISET);
  ESP.restart();
}

static void handle_update_page() {
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
  h += "</p>";
#endif
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
    if (ota_err.length()) { Update.abort(); return; }
    if (Update.end(true)) ota_ok = true;
    else ota_err = Update.errorString();
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    ota_err = tr("megszakadt a feltöltés", "the upload was interrupted");
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

