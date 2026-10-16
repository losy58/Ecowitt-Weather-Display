// A konzolok (Ecowitt "customized upload") fogadása és szétválogatása helyiségenként
// (Az idojaras.ino része: az Arduino IDE és a PlatformIO a fájlokat ábécésorrendben, egy programmá fűzi össze.)

// Melyik helyiség konzolja küldte? (-1: ismeretlen)
static int room_of(const String &model, const String &st) {
  for (int i = 0; i < ROOM_COUNT; i++) {
    if (!rooms[i].match[0]) continue;   // üres azonosító: a hely nincs használatban
    if (model.indexOf(rooms[i].match) >= 0 || st.indexOf(rooms[i].match) >= 0) return i;
  }
  return -1;
}

// A küldő nyilvántartása (új küldőnél naplóbejegyzés); visszaadja a helyiséget
static int track_sender(const String &model, const String &st) {
  uint32_t ip = (uint32_t)my_server.client().remoteIP();
  int room = room_of(model, st);
  int slot = -1, used = 0;
  for (int i = 0; i < SENDER_MAX; i++) {
    if (senders[i].count) used++;
    if (senders[i].count && senders[i].ip == ip && st.equals(senders[i].st)) slot = i;
  }
  if (slot < 0) {
    slot = 0;   // ha betelt, a legrégebben hallott helyére
    for (int i = 0; i < SENDER_MAX; i++) {
      if (!senders[i].count) { slot = i; break; }
      if (senders[i].last_ms < senders[slot].last_ms) slot = i;
    }
    Sender &n = senders[slot];
    n = Sender{};
    n.ip = ip;
    strlcpy(n.st, st.c_str(), sizeof(n.st));
    strlcpy(n.model, model.c_str(), sizeof(n.model));
    // ismeretlen típus: ha ez az egyetlen küldő, a Nappali konzoljának tekintjük (a régi, egykonzolos működés)
    if (room < 0 && used == 0) room = 0;
    n.room = room;
    LogOut.printf("Új konzol: %s | model=%s | stationtype=%s -> %s\n", IPAddress(ip).toString().c_str(),
                  model.c_str(), st.c_str(), room >= 0 ? rooms[room].name : tr("ISMERETLEN", "UNKNOWN"));
  }
  Sender &c = senders[slot];
  c.last_ms = millis();
  c.count++;
  c.raw = "";
  for (int i = 0; i < my_server.args(); i++) {
    if (my_server.argName(i) == "PASSKEY") continue;   // az eszköz azonosítója, nem kell kiírni
    c.raw += my_server.argName(i); c.raw += '='; c.raw += my_server.arg(i); c.raw += '\n';
  }
  return c.room;
}

void handle_ecowitt_post() {
  if (g_demo) { my_server.send(200, "text/plain", "OK"); return; }   // bemutató módban a valódi adatot nem használjuk
  String model = my_server.hasArg("model") ? my_server.arg("model") : String();
  String st    = my_server.hasArg("stationtype") ? my_server.arg("stationtype") : String();
  int r = track_sender(model, st);

  // benti adatok: a küldő helyiségéhez
  if (r >= 0) {
    Room &rm = rooms[r];
    if (my_server.hasArg("tempinf"))      rm.t = (my_server.arg("tempinf").toFloat() - 32.0f) * F2C;
    if (my_server.hasArg("humidityin"))   rm.h = my_server.arg("humidityin").toFloat();
    rm.has_co2 = my_server.hasArg("co2in");
    if (rm.has_co2)                       rm.co2 = my_server.arg("co2in").toFloat();
    if (my_server.hasArg("co2in_24h"))    rm.co2_24h = my_server.arg("co2in_24h").toFloat();
    rm.has_batt = my_server.hasArg("console_batt");
    if (rm.has_batt)                      rm.batt = my_server.arg("console_batt").toFloat();
    rm.last_ms = millis();
    if (r == 0) {   // a Nappali értékei a régi mezőkben is (előzmény-grafikon, részletes oldal)
      weather.temp_in_c = rm.t; weather.humidity_in = rm.h; weather.co2 = rm.co2; weather.co2_24h = rm.co2_24h;
      weather.console_batt_v = rm.batt;
    }
  }

  // kinti adatok: a Nappali konzoljától, vagy ha az OUTDOOR_FAILOVER_S óta hallgat, bármelyiktől
  bool outdoor = (r == 0) || !rooms[0].last_ms || (millis() - rooms[0].last_ms) > (uint32_t)OUTDOOR_FAILOVER_S * 1000UL;
  if (outdoor) {
    for (const FloatField &f : FLOAT_FIELDS) {
      if (my_server.hasArg(f.key)) {
        *f.dst = my_server.arg(f.key).toFloat() * f.mul + f.add;
      }
    }
    strlcpy(weather.stationtype, st.c_str(), sizeof(weather.stationtype));
    if (my_server.hasArg("freq"))
      strlcpy(weather.freq, my_server.arg("freq").c_str(), sizeof(weather.freq));
    if (my_server.hasArg("dateutc"))
      strlcpy(weather.dateutc, my_server.arg("dateutc").c_str(), sizeof(weather.dateutc));

    weather.has_data = true;
    weather.last_rx_ms = millis();
    hist_record();
  }
  update_day_stats();
  fw_push();   // továbbküldés a másik kijelzőre (ha be van állítva; háttérben)

  my_server.send(200, "text/plain", "OK");
  update_ui_labels();
  update_status_bar();
}
