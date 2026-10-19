#pragma once

#include <Arduino.h>

// Hivogatott fuggvenyek, hogy a kijelzo a (rovid, blokkolo) WiFi-atallas alatt is frissuljon.
typedef void (*WifiPumpCb)(void);                // pl. lv_timer_handler()
typedef void (*WifiStatusCb)(const char *msg);   // allapotszoveg (Naplo)

void wifiSetCallbacks(WifiPumpCb pump, WifiStatusCb status);
void wifiSetEnglish(bool en);   // a WiFi-üzenetek nyelve

// Indulás (nem vár): a mentett hálózathoz három próba (háttérben, a loop()-ból léptetve); ha nem sikerül, vagy nincs
// mentett hálózat, elindul a beállító hálózat (wifiApName(), 192.168.4.1), és közben 2 percenként újrapróbálja a mentettet.
void wifiStart();
void wifiLoop();               // a loop()-ból
bool wifiManaged();            // volt már kapcsolat (a loop() újracsatlakozhat; a próbák és a portál alatt nem)
bool wifiTakeConnected();      // egyszer igaz, amikor a kapcsolat létrejött (indulás vagy portál után): NTP, mDNS
bool wifiTakePortalOpened();   // egyszer igaz, amikor kapcsolat nélkül elindult a beállító hálózat (a kijelző panelt nyit)
bool wifiPortalActive();       // nyitva van-e a beállító hálózat
bool wifiPortalNoConn();       // a beállító hálózat azért van nyitva, mert nincs kapcsolat
void wifiPortalStart(uint32_t ms);   // a beállító hálózat kérésre (a kapcsolat megmarad), ms ideig
void wifiRetryNow();           // azonnali újrapróbálás a mentett hálózattal
void wifiForget();             // a mentett hálózat törlése (a P4-en a WiFi-csipből is), utána beállító hálózat
const char *wifiApName();      // a beállító hálózat neve
const char *wifiApAddr();      // a beállító oldal címe: "192.168.4.1"
bool wifiSavedSsid(char *buf, size_t n);   // a mentett hálózat neve (false: nincs)
void wifiStatusLine(char *buf, size_t n);  // a felső sorba: próba, beállító mód, nincs kapcsolat (kapcsolatnál üres)

bool wifiConfigured();
// Csatlakozás a mentett hálózathoz várakozás és beállító portál nélkül (bemutató mód; mentett hálózat nélkül nem csinál semmit)
bool wifiBeginNoWait();
void wifiTune();              // a loop()-ból: gyenge jelnél feljebb kapcsolja az adóteljesítményt
const char *wifiTxName();     // a beállított adóteljesítmény (pl. "11 dBm")
String wifiScanJson();        // a környező hálózatok JSON-ban: [{"s":név,"r":dBm,"l":jelszavas}]
// Átállás új hálózatra; ha nem sikerül, vissza a régire. true: az új hálózat él.
bool wifiSwitchTo(const char *ssid, const char *pass, uint32_t timeoutMs);
