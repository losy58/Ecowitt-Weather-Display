// Open-Meteo előrejelzés: lekérés háttérfeladatban, feldolgozás, eső-/fagyfigyelmeztetés
// (Az idojaras.ino része: az Arduino IDE és a PlatformIO a fájlokat ábécésorrendben, egy programmá fűzi össze.)

// ---- előrejelzés a nyomás 3 órás változásából (egyszerű szabályrendszer, kb. 12-24 órára) ----
// ---------------------------------------------------------------------------
// Időjárás-előrejelzés az Open-Meteo-ból (https://open-meteo.com, ingyenes, kulcs nélkül, CC BY 4.0):
// 3 nap, 30 percenként, háttérfeladatban (a lekérés alatt a kijelző nem akad meg). A beállított hely kell hozzá.
// ---------------------------------------------------------------------------
constexpr int FC_DAYS = 4;   // ennyi napot kérünk le (éjfél után az első nap már tegnapi lehet; 3-at mutatunk)
constexpr uint32_t FC_REFRESH_MS = 30UL * 60UL * 1000UL, FC_RETRY_MS = 5UL * 60UL * 1000UL;
constexpr uint32_t FC_QUICK_RETRY_MS = 60UL * 1000UL;   // hiba után az első 3 újrapróbálás ennyi idő múlva (pl. átmeneti HTTP 503)
struct FcDay { int y, m, d, code; float tmax, tmin, pprob, psum, wind; };
constexpr int FC_HOURS = 24;   // órás előrejelzés a jelenlegi órától
struct FcHour { int y, m, d, h, code; float t, pp, mm; };
struct Forecast { bool valid; int n, nh; FcDay day[FC_DAYS]; FcHour hour[FC_HOURS]; time_t fetched; };
static Forecast fc = {};                  // a kijelző ezt használja (csak a főszál írja)
static Forecast fc_in = {};               // a háttérfeladat ide tölt
static char fc_err[48] = "";              // az utolsó lekérés hibája (üres: sikeres)
static volatile bool fc_ready = false, fc_busy = false, fc_force = false;
static uint32_t fc_next_ms = 5000;        // a következő lekérés ideje (millis)
static int fc_fails = 0;                  // egymás utáni sikertelen lekérések
static double fc_lat = 0, fc_lon = 0;
static portMUX_TYPE fc_mux = portMUX_INITIALIZER_UNLOCKED;
static lv_obj_t *page_fc = NULL;

// időjárás-típusok (WMO-kódokból) az ikonokhoz
enum { WX_SUN, WX_PARTLY, WX_CLOUDY, WX_FOG, WX_RAIN, WX_SNOW, WX_STORM, WX_COUNT };

static int wx_type(int c) {
  if (c <= 1) return WX_SUN;
  if (c == 2) return WX_PARTLY;
  if (c == 3) return WX_CLOUDY;
  if (c == 45 || c == 48) return WX_FOG;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return WX_SNOW;
  if (c >= 95) return WX_STORM;
  return WX_RAIN;   // szitálás, eső, ónos eső, zápor
}

static const char *wx_text(int c) {
  switch (c) {
    case 0: return tr("Derült", "Clear");            case 1: return tr("Többnyire derült", "Mainly clear");
    case 2: return tr("Részben felhős", "Partly cloudy");    case 3: return tr("Borult", "Overcast");
    case 45: case 48: return tr("Köd", "Fog");
    case 51: case 53: case 55: return tr("Szitálás", "Drizzle");
    case 56: case 57: return tr("Ónos szitálás", "Freezing drizzle");
    case 61: return tr("Gyenge eső", "Light rain");       case 63: return tr("Eső", "Rain");          case 65: return tr("Erős eső", "Heavy rain");
    case 66: case 67: return tr("Ónos eső", "Freezing rain");
    case 71: return tr("Gyenge havazás", "Light snow");   case 73: return tr("Havazás", "Snow");      case 75: return tr("Erős havazás", "Heavy snow");
    case 77: return tr("Hószemcse", "Snow grains");
    case 80: return tr("Gyenge zápor", "Light showers");     case 81: return tr("Zápor", "Showers");        case 82: return tr("Heves zápor", "Violent showers");
    case 85: case 86: return tr("Hózápor", "Snow showers");
    case 95: return tr("Zivatar", "Thunderstorm");
    case 96: case 99: return tr("Zivatar jégesővel", "Thunderstorm with hail");
    default: return "";
  }
}

// A főoldal öt ikonja közül a legközelebbi
static int wx_dash_icon(int t) {
  switch (t) {
    case WX_SUN:    return ICON_SUN;
    case WX_PARTLY: return ICON_PARTLY;
    case WX_RAIN: case WX_STORM: return ICON_RAIN;
    default:        return ICON_CLOUDY;
  }
}

// "kulcs":[v1,v2,...] számtömb beolvasása a from pozíciótól (null -> NAN)
static int json_floats(const String &js, int from, const char *key, float *out, int maxn) {
  String k = String("\"") + key + "\":[";
  int p = js.indexOf(k, from);
  if (p < 0) return 0;
  const char *s = js.c_str() + p + k.length();
  int n = 0;
  while (n < maxn && *s && *s != ']') {
    char *end;
    float v = strtof(s, &end);
    if (end == s) {
      if (strncmp(s, "null", 4) != 0) break;
      v = NAN; end = (char *)s + 4;
    }
    out[n++] = v;
    s = end;
    while (*s == ',' || *s == ' ') s++;
  }
  return n;
}

static void fc_fetch_once() {
  char url[300];
  snprintf(url, sizeof(url), "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,precipitation_sum,wind_speed_10m_max"
           "&hourly=temperature_2m,precipitation_probability,precipitation,weather_code&forecast_hours=%d"
           "&timezone=auto&forecast_days=%d", fc_lat, fc_lon, FC_HOURS, FC_DAYS);
  Forecast f = {};
  char err[48] = "";
  {
    WiFiClientSecure cli;
    cli.setInsecure();   // a tanúsítványt nem ellenőrizzük (időjárás-adat), a forgalom így is titkosított
    HTTPClient http;
    http.setTimeout(10000);
    if (!http.begin(cli, url)) {
      strlcpy(err, tr("kapcsolódási hiba", "connection error"), sizeof(err));
    } else {
      int code = http.GET();
      if (code != 200) {
        snprintf(err, sizeof(err), "HTTP %d", code);
      } else {
        String js = http.getString();
        int dp = js.indexOf("\"daily\":{");
        int tp = dp < 0 ? -1 : js.indexOf("\"time\":[", dp);
        if (tp < 0) {
          strlcpy(err, tr("hibás válasz", "invalid response"), sizeof(err));
        } else {
          // dátumok: "time":["2026-10-10","2026-10-11",...]
          const char *s = js.c_str() + tp + 8;
          while (f.n < FC_DAYS && sscanf(s, "\"%d-%d-%d\"", &f.day[f.n].y, &f.day[f.n].m, &f.day[f.n].d) == 3) {
            f.n++;
            s = strchr(s + 1, '"'); if (!s) break;      // a dátum záró idézőjele
            s = strchr(s + 1, '"'); if (!s) break;      // a következő dátum nyitó idézőjele
          }
          float v[FC_DAYS];
          int ok = f.n > 0;
          #define FC_FIELD(key, member) \
            if (ok && json_floats(js, dp, key, v, FC_DAYS) >= f.n) { for (int i = 0; i < f.n; i++) f.day[i].member = v[i]; } else ok = 0;
          FC_FIELD("temperature_2m_max", tmax)
          FC_FIELD("temperature_2m_min", tmin)
          FC_FIELD("precipitation_probability_max", pprob)
          FC_FIELD("precipitation_sum", psum)
          FC_FIELD("wind_speed_10m_max", wind)
          #undef FC_FIELD
          if (ok && json_floats(js, dp, "weather_code", v, FC_DAYS) >= f.n) {
            for (int i = 0; i < f.n; i++) f.day[i].code = isnan(v[i]) ? 3 : (int)v[i];
          } else ok = 0;
          f.valid = ok;
          // órás adatok: "hourly":{"time":["2026-10-11T06:00",...], ...} (nem kötelező: hiánya nem hiba)
          int hp = js.indexOf("\"hourly\":{");
          int htp = hp < 0 ? -1 : js.indexOf("\"time\":[", hp);
          if (ok && htp >= 0) {
            const char *q = js.c_str() + htp + 8;
            int mi;
            while (f.nh < FC_HOURS &&
                   sscanf(q, "\"%d-%d-%dT%d:%d\"", &f.hour[f.nh].y, &f.hour[f.nh].m, &f.hour[f.nh].d, &f.hour[f.nh].h, &mi) == 5) {
              f.nh++;
              q = strchr(q + 1, '"'); if (!q) break;
              q = strchr(q + 1, '"'); if (!q) break;
            }
            float hv[FC_HOURS];
            int hok = f.nh > 0;
            #define FH_FIELD(key, member) \
              if (hok && json_floats(js, hp, key, hv, FC_HOURS) >= f.nh) { for (int i = 0; i < f.nh; i++) f.hour[i].member = hv[i]; } else hok = 0;
            FH_FIELD("temperature_2m", t)
            FH_FIELD("precipitation_probability", pp)
            FH_FIELD("precipitation", mm)
            #undef FH_FIELD
            if (hok && json_floats(js, hp, "weather_code", hv, FC_HOURS) >= f.nh) {
              for (int i = 0; i < f.nh; i++) f.hour[i].code = isnan(hv[i]) ? 3 : (int)hv[i];
            } else hok = 0;
            if (!hok) f.nh = 0;
          }
          f.fetched = time(nullptr);
          if (!ok) strlcpy(err, tr("hiányos válasz", "incomplete response"), sizeof(err));
        }
      }
      http.end();
    }
  }
  portENTER_CRITICAL(&fc_mux);
  if (f.valid) fc_in = f;
  strlcpy(fc_err, err, sizeof(fc_err));
  fc_ready = true;
  fc_busy = false;
  portEXIT_CRITICAL(&fc_mux);
}

// Az előrejelzés háttérfeladata: egyszer indul (verem a PSRAM-ban), értesítésre kér le
static TaskHandle_t fc_task_h = NULL;
static void fc_worker(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    fc_fetch_once();
  }
}

// A loop()-ból: az elkészült lekérés átvétele és a következő indítása
static void fc_loop() {
  if (fc_ready) {
    Forecast f;
    char e[48];
    portENTER_CRITICAL(&fc_mux);
    f = fc_in;
    strlcpy(e, fc_err, sizeof(e));
    fc_ready = false;
    portEXIT_CRITICAL(&fc_mux);
    if (!e[0] && f.valid) {
      fc = f;
      fc_fails = 0;
      fc_next_ms = millis() + FC_REFRESH_MS;
      LogOut.printf("Előrejelzés frissítve (%d nap)\n", fc.n);
    } else {
      fc_fails++;
      fc_next_ms = millis() + (fc_fails <= 3 ? FC_QUICK_RETRY_MS : FC_RETRY_MS);
      LogOut.printf("Előrejelzés hiba: %s\n", e);
    }
    fc_update_ui();
    update_dashboard();
  }
  // pontos idő (NTP) nélkül nem kérünk le: a napok kiválasztásához és a frissítés idejéhez kell; bemutatón a sajátját mutatja
  if (g_demo || fc_busy || !cfg.has_loc || WiFi.status() != WL_CONNECTED || time(nullptr) < 1600000000) return;
  if (!fc_force && (int32_t)(millis() - fc_next_ms) < 0) return;
  fc_force = false;
  fc_lat = cfg.lat; fc_lon = cfg.lon;
  if (!fc_task_h &&
      xTaskCreatePinnedToCoreWithCaps(fc_worker, "fc", 12288, NULL, 1, &fc_task_h, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    fc_task_h = NULL;
    fc_next_ms = millis() + FC_RETRY_MS;
    LogOut.println("Előrejelzés: a háttérfeladat nem indult");
    return;
  }
  fc_busy = true;
  xTaskNotifyGive(fc_task_h);
}

static int fc_key(int y, int m, int d) { return y * 10000 + m * 100 + d; }

// Az első órás előrejelzés indexe, amely nem régebbi a jelenlegi óránál (-1: nincs ilyen / nincs adat)
static int fc_first_hour() {
  if (!fc.valid || fc.nh <= 0) return -1;
  struct tm t;
  if (!get_local_tm(t)) return -1;
  const long now_key = (long)fc_key(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday) * 100 + t.tm_hour;
  for (int i = 0; i < fc.nh; i++) {
    const FcHour &h = fc.hour[i];
    if ((long)fc_key(h.y, h.m, h.d) * 100 + h.h >= now_key) return i;
  }
  return -1;
}

// Eső a következő 6 órában (ha most nem esik): az órás előrejelzés indexe (-1: nincs)
static int fc_rain_hour() {
  const int h0 = fc_first_hour();
  if (h0 < 0 || weather.raining > 0.5f || weather.rain_rate_mm > 0.05f) return -1;
  for (int i = h0; i < fc.nh && i < h0 + 6; i++) {
    const FcHour &h = fc.hour[i];
    if ((!isnan(h.pp) && h.pp >= 60.0f) || (!isnan(h.mm) && h.mm >= 0.3f)) return i;
  }
  return -1;
}

// Fagy a következő 12 órában (ha most fagypont fölött van): az órás előrejelzés indexe (-1: nincs)
static int fc_frost_hour() {
  const int h0 = fc_first_hour();
  if (h0 < 0 || !weather.has_data || weather.temp_c <= 0.0f) return -1;
  for (int i = h0; i < fc.nh && i < h0 + 12; i++)
    if (!isnan(fc.hour[i].t) && fc.hour[i].t <= 0.0f) return i;
  return -1;
}

// Figyelmeztetés a következő órákra: eső (6 órán belül, ha most nem esik) és fagy (12 órán belül, ha most fagypont fölött van)
static void fc_alerts(char *out, size_t n) {
  out[0] = 0;
  const int ri = fc_rain_hour(), fi = fc_frost_hour();
  if (ri >= 0) snprintf(out, n, ri == fc_first_hour() ? tr("Eső várható", "Rain expected") : tr("Eső várható %d:00-tól", "Rain expected from %d:00"), fc.hour[ri].h);
  if (fi >= 0) {
    char b[40];
    snprintf(b, sizeof(b), tr("%sFagy várható %d:00", "%sFrost expected at %d:00"), out[0] ? "   " : "", fc.hour[fi].h);
    strlcat(out, b, n);
  }
}

// Az első előrejelzett nap indexe, amely nem régebbi a mainál (-1: nincs ilyen / nincs adat)
static int fc_first_today() {
  if (!fc.valid) return -1;
  struct tm t;
  if (!get_local_tm(t)) return 0;
  int today = fc_key(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  for (int i = 0; i < fc.n; i++) {
    if (fc_key(fc.day[i].y, fc.day[i].m, fc.day[i].d) >= today) return i;
  }
  return -1;
}
