#include "wifi_manager.h"
#include "kijelzo.h"   // P4: a kamera megállítása flash-írás idejére (p4_cam_pause)

#include <WiFi.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <WiFiManager.h>   // tzapu/WiFiManager (Library Manager: "WiFiManager" by tzapu), ugyanaz a kinézet, mint az IkeDryBox-ban

// A WiFiManager "captive portal": az iPhone/Android magától felkínálja a beállító oldalt, amint a telefon a Weather-Setup
// hálózatra csatlakozik (a DNS minden kérést a lapra irányít, az oldal a 192.168.4.1 címen van).
static const char *AP_NAME = "Weather-Setup";

// Ha már van mentett WiFi, de nem sikerül csatlakozni (pl. áramszünet után a router még bootol), a portál
// ennyi idő után újraindít és újra próbálkozik.
static const uint32_t PORTAL_TIMEOUT_MS = 5UL * 60UL * 1000UL;

static WifiPumpCb uiPump = nullptr;
static WifiStatusCb uiStatus = nullptr;

// a kijelzőre írt üzenetek nyelve (az idojaras.ino nyelvbeállítása: wifiSetEnglish)
static bool langEn = false;
void wifiSetEnglish(bool en) { langEn = en; }
static inline const char *T(const char *hu, const char *en) { return langEn ? en : hu; }

void wifiSetCallbacks(WifiPumpCb pump, WifiStatusCb status)
{
    uiPump = pump;
    uiStatus = status;
}

static void setStatus(const char *msg)
{
    Serial.println(msg);
    if (uiStatus)
        uiStatus(msg);
}

static void pump()
{
    if (uiPump)
        uiPump();
}

static void onPortalStarted(WiFiManager *)
{
    String ip = WiFi.softAPIP().toString();

    Serial.println();
    Serial.println("================================");
    Serial.println("WiFi konfigurációs mód");
    Serial.printf("SSID: %s\n", AP_NAME);
    Serial.printf("IP:   %s\n", ip.c_str());
    Serial.println("================================");

    char msg[96];
    snprintf(msg, sizeof(msg), T("WiFi beállítás: %s | %s", "WiFi setup: %s | %s"), AP_NAME, ip.c_str());
    setStatus(msg);
}

static void beginSaved();
static volatile bool portalSavePressed = false;   // a portálon megnyomták a Mentés gombot

bool wifiPortalOnce(uint32_t timeoutMs)
{
    WiFiManager wm;
    portalSavePressed = false;
    wm.setPreSaveConfigCallback([]() { portalSavePressed = true; });
    wm.setDebugOutput(false);
    wm.setConfigPortalBlocking(false);
    wm.setCaptivePortalEnable(true);
    wm.setAPCallback(onPortalStarted);
    wm.setTitle("Weather Station");
    std::vector<const char *> menu = {"wifi", "info", "exit"};
    wm.setMenu(menu);
    wm.startConfigPortal(AP_NAME);
    bool ok = false;
    const uint32_t start = millis();
    uint32_t retryAt = 0;
    while (millis() - start < timeoutMs)
    {
        if (wm.process() || WiFi.status() == WL_CONNECTED)   // új (vagy a régi) hálózatra csatlakozott
        {
            ok = true;
            break;
        }
        // Mentés új hálózat nélkül (vagy sikertelen csatlakozás után): pár mp múlva vissza a mentett hálózatra
        if (portalSavePressed)
        {
            portalSavePressed = false;
            retryAt = millis() + 8000UL;
        }
        if (retryAt && (int32_t)(millis() - retryAt) >= 0)
        {
            retryAt = 0;
            setStatus(T("WiFi: csatlakozás a mentett hálózathoz...", "WiFi: connecting to the saved network..."));
            beginSaved();
        }
        pump();
        delay(10);
    }
    wm.stopConfigPortal();
    WiFi.mode(WIFI_STA);
    setStatus(ok ? T("WiFi: új hálózat beállítva", "WiFi: new network set") : T("WiFi: a portál bezárult, a mentett hálózat marad", "WiFi: portal closed, keeping the saved network"));
    return ok;
}

// Van-e elmentett WiFi-hálózat. (A WiFi.SSID() az arduino-esp32 3.x-ben a csatlakoztatott hálózatot adja, csatlakozás
// előtt üreset, ezért a mentett beállítást közvetlenül az ESP-IDF-ből olvassuk.)
bool wifiConfigured()
{
    WiFi.mode(WIFI_STA);
    wifi_config_t conf = {};
    if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK)
        return false;
    return conf.sta.ssid[0] != 0;
}

// Az utolsó leválás oka (WiFi hibakód, pl. 15 = 4-utas kézfogás időtúllépés, 201 = nincs ilyen hálózat, 202 = hitelesítési hiba)
static volatile uint8_t lastDisconnectReason = 0;

// Adóteljesítmények. Fél méterre a routertől a teljes (19,5 dBm) teljesítménnyel gyakran nem jön létre a kapcsolat
// (a router még el sem utasítja: ok 0), a csökkentettel igen. Ezért a legutóbb bevált fokozattal kezdünk (NVS "cfg"/"txp",
// alapból a 11 dBm), és sikeres kapcsolódás után megjegyezzük, melyik vált be.
static const wifi_power_t TX_POWER[] = {WIFI_POWER_11dBm, WIFI_POWER_8_5dBm, WIFI_POWER_19_5dBm};
static const char *TX_NAME[] = {"11 dBm", "8,5 dBm", "19,5 dBm"};
static const int TX_COUNT = sizeof(TX_POWER) / sizeof(TX_POWER[0]);

static int txSaved()
{
    Preferences p;
    p.begin("cfg", true);
    int i = p.getInt("txp", 0);
    p.end();
    return (i >= 0 && i < TX_COUNT) ? i : 0;
}

static void txSave(int i)
{
    if (i == txSaved())
        return;
#if KIJELZO_P4
    p4_cam_pause(true);   // a kamera képfolyama áll a mentés alatt (lásd CamPause az idojaras.ino-ban)
#endif
    Preferences p;
    p.begin("cfg", false);
    p.putInt("txp", i);
    p.end();
#if KIJELZO_P4
    p4_cam_pause(false);
#endif
}

// Több hozzáférési pont ugyanazzal a hálózatnévvel (mesh): az ESP32 alapból az elsőként talált AP-ra csatlakozik, ami
// lehet egy távoli, gyenge jelű is. Így minden csatornát végignéz, és a legerősebb jelűt választja.
static void bestApScan()
{
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
}

// Csatlakozás a mentett hálózathoz a legerősebb AP-n. A paraméter nélküli WiFi.begin() a mentett beállítással indul, és
// figyelmen kívül hagyja a keresési/rendezési módot, ezért a nevet és a jelszót kiolvassuk, és azokkal hívjuk.
static void beginSaved()
{
    bestApScan();
    wifi_config_t conf = {};
    if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK && conf.sta.ssid[0])
    {
        char ssid[33] = {}, pass[65] = {};
        memcpy(ssid, conf.sta.ssid, 32);
        memcpy(pass, conf.sta.password, 64);
        WiFi.begin(ssid, pass[0] ? pass : nullptr);
    }
    else
    {
        WiFi.begin();
    }
}

bool wifiBeginNoWait()
{
    if (!wifiConfigured())
        return false;
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);
    beginSaved();
    return true;
}

// Kapcsolat közben az adóteljesítmény igazítása a jelerősséghez: gyenge jelnél (messze a routertől) a 11 dBm kevés,
// az átvitel akadozik, ezért feljebb kapcsolunk, és megjegyezzük (a következő induláskor ezzel kezd). A loop()-ból hívva.
void wifiTune()
{
    static uint32_t last = 0, lastRoam = 0;
    static int weak = 0;
    if (WiFi.status() != WL_CONNECTED || millis() - last < 20000UL)
        return;
    last = millis();
    const int rssi = WiFi.RSSI();
    const int cur = txSaved();
    // tartósan gyenge jel (6 mérés, 2 perc -80 dBm alatt): lehet, hogy egy távoli AP-n ragadt; újracsatlakozás a legerősebbre
    // (legfeljebb 10 percenként). A -65..-75 dBm még jó kapcsolat, ott nem bontjuk feleslegesen.
    weak = rssi < -80 ? weak + 1 : 0;
    if (weak >= 6 && (lastRoam == 0 || millis() - lastRoam > 600000UL))
    {
        lastRoam = millis();
        weak = 0;
        char m[80];
        snprintf(m, sizeof(m), T("WiFi: gyenge jel (%d dBm), újracsatlakozás a legerősebb AP-ra", "WiFi: weak signal (%d dBm), reconnecting to the strongest AP"), rssi);
        Serial.println(m);
        if (uiStatus) uiStatus(m);
        WiFi.disconnect(false, false);
        beginSaved();
        return;
    }
    if (rssi < -75 && cur != 2)   // gyenge jel: teljes teljesítmény (19,5 dBm)
    {
        WiFi.setTxPower(TX_POWER[2]);
        txSave(2);
        char msg[80];
        snprintf(msg, sizeof(msg), T("WiFi: gyenge jel (%d dBm), adóteljesítmény: %s", "WiFi: weak signal (%d dBm), TX power: %s"), rssi, TX_NAME[2]);
        Serial.println(msg);
        if (uiStatus) uiStatus(msg);
    }
}

// A jelenlegi adóteljesítmény neve (a kezelőoldalon)
const char *wifiTxName() { return TX_NAME[txSaved()]; }

// Csatlakozás a mentett hálózathoz: mindhárom adóteljesítménnyel egy-egy próba, a legutóbb bevált fokozattal kezdve
static bool connectSaved(uint32_t timeoutMs)
{
    const int first = txSaved();
    char msg[96];
    for (int a = 0; a < TX_COUNT; a++)
    {
        const int pi = (first + a) % TX_COUNT;
        snprintf(msg, sizeof(msg), T("WiFi: kapcsolódás (%d/%d, %s)...", "WiFi: connecting (%d/%d, %s)..."), a + 1, TX_COUNT, TX_NAME[pi]);
        setStatus(msg);
        WiFi.mode(WIFI_STA);
        WiFi.setTxPower(TX_POWER[pi]);
        lastDisconnectReason = 0;
        beginSaved();   // a mentett hálózat, a legerősebb AP-n
        uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs)
        {
            pump();
            delay(10);
        }
        if (WiFi.status() == WL_CONNECTED)
        {
            if (a > 0)
            {
                snprintf(msg, sizeof(msg), T("WiFi: a(z) %d. próba sikerült (%s)", "WiFi: attempt %d succeeded (%s)"), a + 1, TX_NAME[pi]);
                setStatus(msg);
            }
            txSave(pi);
            return true;
        }
        snprintf(msg, sizeof(msg), T("WiFi: a(z) %d. próba nem sikerült (ok: %u)", "WiFi: attempt %d failed (reason: %u)"), a + 1, (unsigned)lastDisconnectReason);
        setStatus(msg);
        WiFi.disconnect(false, false);
        delay(300);
    }
    WiFi.setTxPower(TX_POWER[first]);
    return false;
}

bool wifiConnectOrSetup(uint32_t timeoutMs)
{
    bool hadSaved = wifiConfigured();

    static bool evRegistered = false;
    if (!evRegistered)
    {
        evRegistered = true;
        WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) { lastDisconnectReason = info.wifi_sta_disconnected.reason; },
                     ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    }
    WiFi.setAutoReconnect(true);

    WiFi.setSleep(false);   // energiatakarékos mód ki: a kijelző hálózatról megy, így gyorsabban válaszol (OTA, kezelőoldal)
    if (hadSaved && connectSaved(timeoutMs))
    {
        Serial.println();
        Serial.println("WiFi csatlakozott.");
        Serial.println(WiFi.localIP());
        return true;
    }

    WiFiManager wm;
    wm.setDebugOutput(false);
    wm.setConfigPortalBlocking(false);              // a kijelző a portál alatt is frissül (a ciklust mi futtatjuk)
    wm.setConnectTimeout(timeoutMs / 1000);         // a mentett hálózatra ennyi másodpercig próbálkozik
    wm.setCaptivePortalEnable(true);
    wm.setAPCallback(onPortalStarted);
    wm.setTitle("Weather Station");

    std::vector<const char *> menu = {"wifi", "info", "exit", "sep", "update"};
    wm.setMenu(menu);

    // A hely (napkelte/napnyugta) a WiFi-adatokkal együtt megadható; üresen hagyva később a frissítő oldalon is beállítható.
    // A program a "cfg" NVS-névtérből olvassa (lat, lon).
    char latBuf[16] = "", lonBuf[16] = "";
    {
        Preferences p;
        p.begin("cfg", true);
        if (p.isKey("lat") && p.isKey("lon"))
        {
            snprintf(latBuf, sizeof(latBuf), "%.4f", p.getDouble("lat", 0.0));
            snprintf(lonBuf, sizeof(lonBuf), "%.4f", p.getDouble("lon", 0.0));
        }
        p.end();
    }
    WiFiManagerParameter pLat("lat", T("Földrajzi szélesség (pl. 47.4979, nem kötelező)", "Latitude (e.g. 51.5074, optional)"), latBuf, 15);
    WiFiManagerParameter pLon("lon", T("Földrajzi hosszúság (pl. 19.0402, nem kötelező)", "Longitude (e.g. -0.1278, optional)"), lonBuf, 15);
    wm.addParameter(&pLat);
    wm.addParameter(&pLon);

    setStatus(T("WiFi: kapcsolódás...", "WiFi: connecting..."));
    WiFi.setAutoReconnect(true);

    // Mentett hálózat esetén csatlakozik; ha nem sikerül (vagy nincs mentett), elindul a portál és false-t ad vissza.
    bool ok = wm.autoConnect(AP_NAME);

    if (!ok)
    {
        if (hadSaved)
            setStatus(T("WiFi hiba - beállító mód", "WiFi error - setup mode"));

        uint32_t start = millis(), lastTry = millis();

        while (WiFi.status() != WL_CONNECTED)
        {
            wm.process();
            pump();
            delay(10);

            // a portál mellett a mentett hálózattal is tovább próbálkozunk; ha sikerül, a portál bezárul
            if (hadSaved && millis() - lastTry > 30000UL)
            {
                lastTry = millis();
                WiFi.setTxPower(TX_POWER[txSaved()]);   // a bevált (alapból csökkentett) adóteljesítménnyel
                beginSaved();
            }

            if (hadSaved && millis() - start > PORTAL_TIMEOUT_MS)
            {
                Serial.println("Portál-időtúllépés, újraindítás.");
                diag_mark_restart(2);   // RC_PORTAL (idojaras.ino): a következő induláskor a diagnosztikában látszik
                ESP.restart();
            }
        }

        // a portálon megadott hely mentése (csak ha mindkét mező érvényes)
        String la = pLat.getValue(), lo = pLon.getValue();
        la.trim(); lo.trim(); la.replace(',', '.'); lo.replace(',', '.');
        double lat = la.toDouble(), lon = lo.toDouble();
        if (la.length() && lo.length() && lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180 && !(lat == 0 && lon == 0))
        {
            Preferences p;
            p.begin("cfg", false);
            p.putDouble("lat", lat);
            p.putDouble("lon", lon);
            p.end();
        }

        // a beállítás után a portált leállítjuk, a 80-as port így szabad lesz a webszervernek
        wm.stopConfigPortal();
        WiFi.mode(WIFI_STA);
    }

    Serial.println();
    Serial.println("WiFi csatlakozott.");
    Serial.println(WiFi.localIP());

    return true;
}

// A környező hálózatok listája JSON-ban (a kezelőoldal választólistájához): azonos nevűekből a legerősebb, erősség szerint
String wifiScanJson()
{
    const int n = WiFi.scanNetworks(false, false);
    String o = "[";
    int put = 0;
    for (int i = 0; i < n && put < 25; i++)
    {
        const String ssid = WiFi.SSID(i);
        if (!ssid.length())
            continue;
        bool dup = false;   // a lista erősség szerint rendezett: ha már szerepelt, az erősebb volt
        for (int j = 0; j < i && !dup; j++)
            dup = WiFi.SSID(j) == ssid;
        if (dup)
            continue;
        if (put++)
            o += ',';
        o += "{\"s\":\"";
        for (char c : ssid)
        {
            if (c == '"' || c == '\\') o += '\\';
            if ((uint8_t)c >= 0x20) o += c;
        }
        o += "\",\"r\":";
        o += WiFi.RSSI(i);
        o += ",\"l\":";
        o += WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? 0 : 1;
        o += '}';
    }
    WiFi.scanDelete();
    o += ']';
    return o;
}

// Átállás új hálózatra; ha timeoutMs alatt nem sikerül, vissza a régire (annak mentett adataival). true: az új él.
bool wifiSwitchTo(const char *ssid, const char *pass, uint32_t timeoutMs)
{
    wifi_config_t old = {};
    const bool hadOld = esp_wifi_get_config(WIFI_IF_STA, &old) == ESP_OK && old.sta.ssid[0];
    char msg[96];
    snprintf(msg, sizeof(msg), T("WiFi: csatlakozás ide: %s...", "WiFi: connecting to %s..."), ssid);
    setStatus(msg);
    bestApScan();
    WiFi.disconnect(false, false);
    delay(200);
    WiFi.begin(ssid, (pass && pass[0]) ? pass : nullptr);   // el is menti
    const uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs)
    {
        pump();
        delay(10);
    }
    if (WiFi.status() == WL_CONNECTED)
    {
        snprintf(msg, sizeof(msg), T("WiFi: új hálózat: %s, IP: %s", "WiFi: new network: %s, IP: %s"), ssid, WiFi.localIP().toString().c_str());
        setStatus(msg);
        return true;
    }
    snprintf(msg, sizeof(msg), T("WiFi: nem sikerült csatlakozni ide: %s (ok: %u), vissza a régire", "WiFi: could not connect to %s (reason: %u), back to the old one"), ssid, (unsigned)lastDisconnectReason);
    setStatus(msg);
    WiFi.disconnect(false, false);
    delay(200);
    if (hadOld)
    {
        char os[33] = {}, op[65] = {};
        memcpy(os, old.sta.ssid, 32);
        memcpy(op, old.sta.password, 64);
        WiFi.begin(os, op[0] ? op : nullptr);   // a régi adatok vissza (újra el is menti)
        const uint32_t t1 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t1 < 20000UL)
        {
            pump();
            delay(10);
        }
    }
    return false;
}

