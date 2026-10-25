#include "wifi_manager.h"
#include "kijelzo.h"   // P4: a kamera megállítása flash-írás idejére (p4_cam_pause)

#include <WiFi.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <DNSServer.h>

// Saját beállító hálózat ("captive portal"): a DNS minden nevet a 192.168.4.1-re irányít, így a telefon magától
// felkínálja a beállító oldalt (a webszerver útvonalai az idojaras_13-ban). Jelszó nélküli, mint a régi WiFiManageré.
// A P4-é más nevű, hogy a két kijelző beállító hálózata ne ütközzön.
#if KIJELZO_P4
static const char *AP_NAME = "Weather-Setup-P4";
#else
static const char *AP_NAME = "Weather-Setup";
#endif
static const IPAddress AP_IP(192, 168, 4, 1);

constexpr uint32_t TRY_MS = 15000;          // egy csatlakozási próba ideje (induláskor)
constexpr uint32_t RETRY_MS = 120000;       // beállító módban ilyen gyakran próbálja újra a mentett hálózatot ...
constexpr uint32_t SCAN_HOLD_MS = 180000;   // ... de hálózatkeresés (a beállító oldal) után ennyi ideig nem: közben a keresés nem menne
constexpr uint32_t AP_LINGER_MS = 60000;    // a kapcsolat létrejötte után ennyi ideig marad nyitva a beállító hálózat (a telefon lássa)

static WifiPumpCb uiPump = nullptr;
static WifiStatusCb uiStatus = nullptr;

// a WiFi-üzenetek nyelve (az idojaras.ino nyelvbeállítása: wifiSetEnglish)
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
    if (uiStatus)
        uiStatus(msg);   // a Napló a soros portra is kiírja
    else
        Serial.println(msg);
}

static void pump()
{
    if (uiPump)
        uiPump();
}

// Az utolsó leválás oka (WiFi hibakód, pl. 15 = 4-utas kézfogás időtúllépés, 201 = nincs ilyen hálózat, 202 = hitelesítési hiba)
static volatile uint8_t lastDisconnectReason = 0;

// Adóteljesítmények. Fél méterre a routertől a teljes (19,5 dBm) teljesítménnyel gyakran nem jön létre a kapcsolat
// (a router még el sem utasítja: ok 0), a csökkentettel igen. Ezért a legutóbb bevált fokozattal kezdünk (NVS "cfg"/"txp",
// alapból a 11 dBm), és sikeres kapcsolódás után megjegyezzük, melyik vált be.
static const wifi_power_t TX_POWER[] = {WIFI_POWER_11dBm, WIFI_POWER_8_5dBm, WIFI_POWER_19_5dBm};
static const char *TX_NAME[] = {"11 dBm", "8,5 dBm", "19,5 dBm"};
static const int TX_COUNT = sizeof(TX_POWER) / sizeof(TX_POWER[0]);

// A mentett fokozatot csak egyszer olvassuk ki (induláskor, a kamera indulása előtt), utána a memóriából adjuk. A P4-en a
// flash-OLVASÁS is kikapcsolja a cache-t, és ha ekkor ér véget egy kamerakép, összeomlás (ipc0, ok 25): a wifiTune()
// 20 mp-enkénti NVS-olvasása 1-3 naponta ezt okozta (2026.10.21., 10.24., 10.25., a coredumpok szerint).
static int txCached = -1;

static int txSaved()
{
    if (txCached < 0)
    {
        Preferences p;
        p.begin("cfg", true);
        int i = p.getInt("txp", 0);
        p.end();
        txCached = (i >= 0 && i < TX_COUNT) ? i : 0;
    }
    return txCached;
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
    txCached = i;
#if KIJELZO_P4
    p4_cam_pause(false);
#endif
}

// Van-e elmentett WiFi-hálózat. (A WiFi.SSID() az arduino-esp32 3.x-ben a csatlakoztatott hálózatot adja, csatlakozás
// előtt üreset, ezért a mentett beállítást közvetlenül az ESP-IDF-ből olvassuk. A P4-en ezt a WiFi-csip (ESP32-C6)
// tárolja, a saját flashében: a P4 teljes újratelepítése nem törli.)
bool wifiConfigured()
{
    if (!(WiFi.getMode() & WIFI_MODE_STA))
        WiFi.mode(WIFI_STA);
    wifi_config_t conf = {};
    if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK)
        return false;
    return conf.sta.ssid[0] != 0;
}

bool wifiSavedSsid(char *buf, size_t n)
{
    wifi_config_t conf = {};
    buf[0] = 0;
    if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK || !conf.sta.ssid[0])
        return false;
    char s[33] = {};
    memcpy(s, conf.sta.ssid, 32);
    strlcpy(buf, s, n);
    return true;
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

static void regEvents()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) { lastDisconnectReason = info.wifi_sta_disconnected.reason; },
                 ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
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

// A P4-en a WiFi-beállítást a WiFi-csip (ESP32-C6) tárolja; ha ott a tárolás nem a flash, a törlés (és egy új hálózat)
// csak a csip memóriájában változik, és a következő induláskor a régi jön vissza (2026.10.19.: a "WiFi elfelejtése" után
// tápciklusra újra csatlakozott). Ezért a tárolás kifejezetten flash (az ESP-Hosted ezt is továbbítja a csipnek).
static void storageFlash()
{
    const esp_err_t e = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (e != ESP_OK)
    {
        char m[80];
        snprintf(m, sizeof(m), "WiFi: esp_wifi_set_storage hiba: 0x%x", (unsigned)e);
        setStatus(m);
    }
}

// ---------------------------------------------------------------------------
// Állapotgép: próbák -> (kapcsolat | beállító hálózat). A loop()-ból léptetve, semmi nem vár.
// ---------------------------------------------------------------------------
enum { WS_IDLE, WS_TRY, WS_PORTAL, WS_OK };
static int wState = WS_IDLE, wAttempt = 0, wFirstTx = 0;
static uint32_t wT0 = 0, wRetryAt = 0, wHoldUntil = 0, wApOffAt = 0;
static bool apOn = false, connEvent = false, portalEvent = false, retryForce = false;
static DNSServer dns;

static bool due(uint32_t t) { return (int32_t)(millis() - t) >= 0; }

static void apStart()
{
    if (apOn)
        return;
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(AP_NAME);
    dns.start(53, "*", AP_IP);
    apOn = true;
    char msg[96];
    snprintf(msg, sizeof(msg), T("WiFi: beállító hálózat: %s, http://%s/", "WiFi: setup network: %s, http://%s/"), AP_NAME,
             AP_IP.toString().c_str());
    setStatus(msg);
}

static void apStop()
{
    if (!apOn)
        return;
    dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    apOn = false;
    wApOffAt = 0;
    setStatus(T("WiFi: a beállító hálózat bezárult", "WiFi: the setup network is closed"));
}

static void tryAttempt()
{
    const int pi = (wFirstTx + wAttempt) % TX_COUNT;
    char msg[96];
    snprintf(msg, sizeof(msg), T("WiFi: kapcsolódás (%d/%d, %s)...", "WiFi: connecting (%d/%d, %s)..."), wAttempt + 1, TX_COUNT, TX_NAME[pi]);
    setStatus(msg);
    WiFi.setTxPower(TX_POWER[pi]);
    lastDisconnectReason = 0;
    beginSaved();   // a mentett hálózat, a legerősebb AP-n
    wT0 = millis();
}

static void onConnected()
{
    wState = WS_OK;
    WiFi.setAutoReconnect(true);
    connEvent = true;
    retryForce = false;
    char msg[120];
    snprintf(msg, sizeof(msg), T("WiFi: csatlakozva: %s, IP: %s", "WiFi: connected: %s, IP: %s"), WiFi.SSID().c_str(),
             WiFi.localIP().toString().c_str());
    setStatus(msg);
    if (apOn)
        wApOffAt = millis() + AP_LINGER_MS;
}

static void portalBegin()
{
    wState = WS_PORTAL;
    WiFi.setAutoReconnect(false);   // a folytonos újracsatlakozás alatt a hálózatkeresés nem menne; mi próbálunk, ritkábban
    apStart();
    wApOffAt = 0;
    wRetryAt = millis() + RETRY_MS;
    portalEvent = true;
}

void wifiStart()
{
    regEvents();
    WiFi.setSleep(false);   // energiatakarékos mód ki: a kijelző hálózatról megy, így gyorsabban válaszol (OTA, kezelőoldal)
    const bool saved = wifiConfigured();   // (ez indítja a WiFi-t is)
    storageFlash();
    if (saved)
    {
        wState = WS_TRY;
        wAttempt = 0;
        wFirstTx = txSaved();
        WiFi.setAutoReconnect(true);
        tryAttempt();
    }
    else
    {
        setStatus(T("WiFi: nincs mentett hálózat", "WiFi: no saved network"));
        portalBegin();
    }
}

void wifiLoop()
{
    const bool conn = WiFi.status() == WL_CONNECTED;
    switch (wState)
    {
    case WS_TRY:
        if (conn)
        {
            txSave((wFirstTx + wAttempt) % TX_COUNT);
            onConnected();
        }
        else if (millis() - wT0 > TRY_MS)
        {
            char msg[96];
            snprintf(msg, sizeof(msg), T("WiFi: a(z) %d. próba nem sikerült (ok: %u)", "WiFi: attempt %d failed (reason: %u)"), wAttempt + 1,
                     (unsigned)lastDisconnectReason);
            setStatus(msg);
            WiFi.disconnect(false, false);
            if (++wAttempt < TX_COUNT)
            {
                tryAttempt();
            }
            else
            {
                WiFi.setTxPower(TX_POWER[wFirstTx]);
                setStatus(T("WiFi: nem sikerült csatlakozni - beállító mód", "WiFi: could not connect - setup mode"));
                portalBegin();
            }
        }
        break;
    case WS_PORTAL:
        if (conn)
        {
            onConnected();
            break;
        }
        // a mentett hálózat újrapróbálása: csak ha nincs senki a beállító hálózaton, és nem kerestek nemrég hálózatot
        if (due(wRetryAt) && (retryForce || (due(wHoldUntil) && WiFi.softAPgetStationNum() == 0)) && wifiConfigured())
        {
            retryForce = false;
            wRetryAt = millis() + RETRY_MS;
            setStatus(T("WiFi: újrapróbálás a mentett hálózattal...", "WiFi: retrying the saved network..."));
            WiFi.setTxPower(TX_POWER[txSaved()]);
            beginSaved();
        }
        break;
    default:
        break;
    }
    // a kérésre (vagy a kapcsolat létrejötte után még egy ideig) nyitott beállító hálózat bezárása
    if (apOn && wApOffAt && wState != WS_PORTAL && due(wApOffAt))
        apStop();
}

bool wifiManaged() { return wState == WS_OK; }
bool wifiPortalActive() { return apOn; }
bool wifiPortalNoConn() { return wState == WS_PORTAL; }
const char *wifiApName() { return AP_NAME; }
const char *wifiApAddr() { return "192.168.4.1"; }

bool wifiTakeConnected()
{
    const bool e = connEvent;
    connEvent = false;
    return e;
}

bool wifiTakePortalOpened()
{
    const bool e = portalEvent;
    portalEvent = false;
    return e;
}

void wifiPortalStart(uint32_t ms)
{
    regEvents();
    if (wState == WS_PORTAL || wState == WS_TRY)   // kapcsolat nélkül: a beállító mód (amíg nincs kapcsolat)
    {
        if (wState == WS_TRY)
            portalBegin();
        return;
    }
    apStart();
    wApOffAt = millis() + ms;
}

void wifiRetryNow()
{
    if (wState != WS_PORTAL)
        return;
    retryForce = true;
    wRetryAt = 0;
}

void wifiForget()
{
    regEvents();
    if (!(WiFi.getMode() & WIFI_MODE_STA))
        WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    storageFlash();
    WiFi.disconnect(false, true);   // a beállítás törlése (üres beállítás; a P4-en a WiFi-csipben)
    char m[96];
    if (wifiConfigured())   // nem törlődött: a WiFi teljes beállítása vissza az alapra
    {
        const esp_err_t e = esp_wifi_restore();
        snprintf(m, sizeof(m), "WiFi: az üres beállítás nem maradt meg, esp_wifi_restore: 0x%x", (unsigned)e);
        setStatus(m);
        WiFi.mode(WIFI_STA);
        storageFlash();
    }
    setStatus(wifiConfigured() ? T("WiFi: a mentett hálózatot NEM sikerült törölni", "WiFi: could NOT forget the saved network")
                               : T("WiFi: a mentett hálózat törölve", "WiFi: the saved network is forgotten"));
    portalBegin();
}

void wifiStatusLine(char *buf, size_t n)
{
    buf[0] = 0;
    if (wState == WS_TRY)
        snprintf(buf, n, T("WiFi: kapcsolódás (%d/%d)...", "WiFi: connecting (%d/%d)..."), wAttempt + 1, TX_COUNT);
    else if (wState == WS_PORTAL)
        snprintf(buf, n, T("Nincs WiFi - beállítás: %s, %s", "No WiFi - setup: %s, %s"), AP_NAME, wifiApAddr());
    else if (WiFi.status() != WL_CONNECTED)
        snprintf(buf, n, "%s", T("WiFi: nincs kapcsolat", "WiFi: not connected"));
    else if (apOn)
        snprintf(buf, n, T("Beállító hálózat nyitva: %s", "Setup network open: %s"), AP_NAME);
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
        setStatus(m);
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
        setStatus(msg);
    }
}

// A jelenlegi adóteljesítmény neve (a kezelőoldalon)
const char *wifiTxName() { return TX_NAME[txSaved()]; }

// A környező hálózatok listája JSON-ban (a kezelőoldal és a beállító oldal választólistájához): azonos nevűekből a
// legerősebb, erősség szerint. Kapcsolat nélkül előbb leállítja a folyamatban lévő csatlakozási próbát (közben a keresés
// nem menne), és egy ideig nem próbálkozik újra. Az eredmény 10 mp-ig megmarad (az oldal újratöltése ne keressen újra).
String wifiScanJson()
{
    static String cache;
    static uint32_t cacheMs = 0;
    if (cache.length() && millis() - cacheMs < 10000UL)
        return cache;
    if (WiFi.status() != WL_CONNECTED)
    {
        WiFi.disconnect(false, false);
        wHoldUntil = millis() + SCAN_HOLD_MS;
        delay(100);
    }
    int n = WiFi.scanNetworks(false, false);
    if (n < 0)   // egyszer még (pl. épp véget ért egy próba)
    {
        delay(300);
        n = WiFi.scanNetworks(false, false);
    }
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
    if (n > 0)
    {
        cache = o;
        cacheMs = millis();
    }
    return o;
}

// Átállás új hálózatra; ha timeoutMs alatt nem sikerül, vissza a régire (annak mentett adataival). true: az új él.
bool wifiSwitchTo(const char *ssid, const char *pass, uint32_t timeoutMs)
{
    wifi_config_t old = {};
    const bool hadOld = esp_wifi_get_config(WIFI_IF_STA, &old) == ESP_OK && old.sta.ssid[0];
    const bool wasConn = WiFi.status() == WL_CONNECTED;
    char msg[96];
    snprintf(msg, sizeof(msg), T("WiFi: csatlakozás ide: %s...", "WiFi: connecting to %s..."), ssid);
    setStatus(msg);
    bestApScan();
    storageFlash();
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
        if (wState != WS_OK)
            onConnected();   // beállító módból: a beállító hálózat még AP_LINGER_MS ideig nyitva marad
        else if (apOn)
            wApOffAt = millis() + AP_LINGER_MS;
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
        while (wasConn && WiFi.status() != WL_CONNECTED && millis() - t1 < 20000UL)   // csak ha az előbb még élt
        {
            pump();
            delay(10);
        }
        if (!wasConn)
            wHoldUntil = millis() + SCAN_HOLD_MS;   // beállító módban: a következő próba majd később
    }
    else
    {
        WiFi.disconnect(false, true);   // a sikertelen új hálózat ne maradjon mentve
    }
    return false;
}
