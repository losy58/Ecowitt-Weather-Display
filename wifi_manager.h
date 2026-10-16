#pragma once

#include <Arduino.h>

// Hivogatott fuggvenyek, hogy a kijelzo a WiFi-varakozas / beallito portal alatt is frissuljon.
typedef void (*WifiPumpCb)(void);                // pl. lv_timer_handler()
typedef void (*WifiStatusCb)(const char *msg);   // allapotszoveg kiirasa a kijelzore

void wifiSetCallbacks(WifiPumpCb pump, WifiStatusCb status);
void wifiSetEnglish(bool en);   // a kijelzőre írt WiFi-üzenetek nyelve
bool wifiConnectOrSetup(uint32_t timeoutMs = 15000);
bool wifiConfigured();
// Csatlakozás a mentett hálózathoz várakozás és beállító portál nélkül (bemutató mód; mentett hálózat nélkül nem csinál semmit)
bool wifiBeginNoWait();
// A beállító portál egyszeri futtatása (a mentett hálózat megmarad, ha nem adnak meg újat); true, ha új hálózatra csatlakozott
bool wifiPortalOnce(uint32_t timeoutMs);
void wifiTune();              // a loop()-ból: gyenge jelnél feljebb kapcsolja az adóteljesítményt
const char *wifiTxName();     // a beállított adóteljesítmény (pl. "11 dBm")
String wifiScanJson();        // a környező hálózatok JSON-ban: [{"s":név,"r":dBm,"l":jelszavas}]
// Átállás új hálózatra; ha nem sikerül, vissza a régire. true: az új hálózat él.
bool wifiSwitchTo(const char *ssid, const char *pass, uint32_t timeoutMs);

// az idojaras.ino diagnosztikája: a program által kezdeményezett újraindítás oka (ESP.restart() előtt)
void diag_mark_restart(int cause);
