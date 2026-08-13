/*
 * ============================================================================
 *  Lightning Early-Warning System
 *  CAPTIVE-PORTAL WiFi  +  FIREBASE LOGGING  +  BENGALI (UCS2) SMS
 *  Final-Year CSE Project  |  Author: Ahoshan
 * ----------------------------------------------------------------------------
 *  IMPORTANT: SAVE THIS FILE AS UTF-8.
 *  Arduino IDE 2.x does this by default. If the Bengali text below looks like
 *  garbage in your editor, the file encoding is wrong and the SMS will be
 *  wrong too.
 * ----------------------------------------------------------------------------
 *  WiFi PROVISIONING (no hardcoded credentials)
 *    - Boot reads saved WiFi from ESP32 flash (NVS).
 *    - If none / unreachable -> opens AP "Lightning-EWS-Setup" (pw lightning123)
 *    - Connect a phone, captive portal opens (or go to http://192.168.4.1)
 *    - Pick network, enter password, Save -> node reboots, connects, AP closes.
 *    - Hold the silence button (GPIO23) 3 s to wipe WiFi and re-open setup.
 *
 *  BENGALI SMS
 *    The SIM800L cannot send Bengali in the GSM 7-bit alphabet. Messages are
 *    converted UTF-8 -> UTF-16BE and sent as hex with the module in UCS2 mode
 *    (AT+CSCS="UCS2", AT+CSMP dcs=8). The phone number is hex-encoded too,
 *    which is the step most guides miss.
 *    NOTE: a UCS2 SMS holds only 70 characters, not 160.
 *
 *  LIBRARIES: only Mobizt's "Firebase Arduino Client Library for ESP8266 and
 *  ESP32" is external. WebServer / DNSServer / Preferences ship with the core.
 * ============================================================================
 */

#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <time.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"

// ------------------- CHOOSE YOUR SOURCE (change this one line) -------------
#define SRC_EMULATOR       2
#define SRC_FLASH_THUNDER  3
#define SRC_AS3935         1

#define SENSOR_SOURCE   SRC_EMULATOR
// --------------------------------------------------------------------------

// ------------------------------ USER CONFIG --------------------------------
const char* RECIPIENTS[] = { "+8801540392159", "+8801581413109" };
const uint8_t NUM_RECIPIENTS = sizeof(RECIPIENTS) / sizeof(RECIPIENTS[0]);

// Node name appears in every SMS. Keep it SHORT - UCS2 SMS = 70 chars total.
const char*   NODE_NAME  = "মাঠ নোড ১";
const bool    SMS_ENABLED = true;

const uint8_t KM_SHELTER = 10;              // <= 10 km -> TAKE SHELTER (L3)
const uint8_t KM_WARNING = 25;              // <= 25 km -> WARNING      (L2)
                                            //  > 25 km -> WATCH        (L1)
const unsigned long ALL_CLEAR_MS = 30UL * 60UL * 1000UL;   // 30 min

// ---------------------------- [PORTAL] CONFIG ------------------------------
#define AP_SSID         "Lightning-EWS-Setup"
#define AP_PASSWORD     "11112222"      // >= 8 chars
#define WIFI_CONNECT_TIMEOUT_MS 20000
#define NVS_NAMESPACE   "ewscfg"

// ---------------------------- [CLOUD] CONFIG -------------------------------
#define FB_API_KEY      "AIzaSyDDEDKHAd1B-U_BI1034STMPej4aTxlWio"
#define FB_DATABASE_URL "https://lightning-ews-default-rtdb.asia-southeast1.firebasedatabase.app/"

#define DEVICE_ID       "node1"
#define DEVICE_LOCATION "Field site, Dhaka"
#define DEVICE_LAT      23.8103
#define DEVICE_LNG      90.4125
#define FIRMWARE_VER    "1.3-bn"

const bool CLOUD_ENABLED = true;
const unsigned long HEARTBEAT_MS = 30000;
const long  GMT_OFFSET_SEC = 0;             // store UTC; dashboard converts
const int   DST_OFFSET_SEC = 0;

// ------------------------------ PIN MAP ------------------------------------
#define PIN_SIM_RX     16
#define PIN_SIM_TX     17
#define PIN_SIREN      21
#define PIN_STROBE     26
#define PIN_STATUS_LED  2
#define PIN_SILENCE    23    // short press = mute siren; hold 3 s = forget WiFi

#define PIN_STRIKE_BTN  4
#define PIN_POT         34
#define PIN_LIGHT       35
#define PIN_SOUND       32

#define PIN_AS3935_IRQ  4
#define PIN_SDA        21
#define PIN_SCL        22

#if (SENSOR_SOURCE == SRC_AS3935) && (PIN_SIREN == PIN_SDA)
#error "PIN_SIREN (21) collides with PIN_SDA for the AS3935. Set PIN_SIREN to 25."
#endif

HardwareSerial sim800(2);

// ------------------------------ STATE --------------------------------------
uint8_t  currentLevel = 0;
uint8_t  nearestKm    = 255;
bool     stormActive  = false;
bool     smsSent[4]   = {false,false,false,false};
unsigned long lastEventMs = 0;
unsigned long lastToggleMs = 0;
bool strobeState = false, silenced = false;

Preferences prefs;
WebServer   portal(80);
DNSServer   dns;
bool     portalActive   = false;
String   savedSSID      = "";
String   savedPASS      = "";
unsigned long silenceHeldSince = 0;

FirebaseData   fbdo;
FirebaseAuth   fbauth;
FirebaseConfig fbconfig;
bool     firebaseReady   = false;
bool     timeSynced      = false;
unsigned long lastBeatMs = 0;
unsigned long lastWifiTryMs = 0;

// forward decls
void pushStatus();
void broadcastSMS(const String &text);

// ===========================================================================
//  BENGALI / UNICODE SMS SUPPORT
//  Converts a UTF-8 String to UTF-16BE hex for the SIM800L's UCS2 mode.
// ===========================================================================
String utf8ToUcs2Hex(const String &utf8) {
  String hex = "";
  const char* s = utf8.c_str();
  size_t i = 0, n = utf8.length();

  while (i < n) {
    uint32_t cp = 0;
    uint8_t c = (uint8_t)s[i];

    if (c < 0x80)               { cp = c;                          i += 1; }
    else if ((c & 0xE0) == 0xC0){ cp =  c & 0x1F;                  i += 1;
                                  cp = (cp<<6) | (s[i] & 0x3F);    i += 1; }
    else if ((c & 0xF0) == 0xE0){ cp =  c & 0x0F;                  i += 1;  // Bengali
                                  cp = (cp<<6) | (s[i] & 0x3F);    i += 1;
                                  cp = (cp<<6) | (s[i] & 0x3F);    i += 1; }
    else if ((c & 0xF8) == 0xF0){ cp =  c & 0x07;                  i += 1;
                                  cp = (cp<<6) | (s[i] & 0x3F);    i += 1;
                                  cp = (cp<<6) | (s[i] & 0x3F);    i += 1;
                                  cp = (cp<<6) | (s[i] & 0x3F);    i += 1; }
    else { i++; continue; }                                        // bad byte

    char buf[5];
    if (cp <= 0xFFFF) {                       // Basic Multilingual Plane
      sprintf(buf, "%04X", (uint16_t)cp);                       hex += buf;
    } else {                                   // surrogate pair
      cp -= 0x10000;
      sprintf(buf, "%04X", (uint16_t)(0xD800 + (cp >> 10)));    hex += buf;
      sprintf(buf, "%04X", (uint16_t)(0xDC00 + (cp & 0x3FF)));  hex += buf;
    }
  }
  return hex;
}

// UTF-16 unit count. A single UCS2 SMS carries only 70 units.
uint16_t ucs2Units(const String &hex) { return hex.length() / 4; }

// ===========================================================================
//  [PORTAL] Credential storage in NVS flash
// ===========================================================================
void credsLoad() {
  prefs.begin(NVS_NAMESPACE, true);
  savedSSID = prefs.getString("ssid", "");
  savedPASS = prefs.getString("pass", "");
  prefs.end();
  if (savedSSID.length()) { Serial.print(F("[NVS] Saved SSID: ")); Serial.println(savedSSID); }
  else                      Serial.println(F("[NVS] No saved credentials."));
}

void credsSave(const String &ssid, const String &pass) {
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
  Serial.println(F("[NVS] Credentials saved."));
}

void credsClear() {
  prefs.begin(NVS_NAMESPACE, false);
  prefs.remove("ssid");
  prefs.remove("pass");
  prefs.end();
  Serial.println(F("[NVS] Credentials cleared."));
}

// ===========================================================================
//  [PORTAL] Setup page served from the AP at 192.168.4.1
// ===========================================================================
String portalHtml(const String &msg = "") {
  String h;
  h += F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<title>Lightning EWS Setup</title><style>"
         "*{box-sizing:border-box}"
         "body{margin:0;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;"
         "background:#0f3d3e;color:#123;display:flex;justify-content:center;padding:18px}"
         ".card{background:#fff;border-radius:14px;max-width:430px;width:100%;padding:22px;"
         "box-shadow:0 8px 28px rgba(0,0,0,.25)}"
         "h1{margin:0 0 4px;font-size:20px;color:#0f3d3e}"
         "p.sub{margin:0 0 18px;font-size:13px;color:#6b7b7b}"
         "label{display:block;font-size:13px;font-weight:600;margin:12px 0 5px;color:#0f3d3e}"
         "select,input{width:100%;padding:11px;border:1px solid #c7d6d6;border-radius:8px;font-size:15px}"
         "button{width:100%;margin-top:18px;padding:13px;border:0;border-radius:8px;"
         "background:#0f3d3e;color:#fff;font-size:16px;font-weight:600}"
         "button.alt{background:#fff;color:#c62828;border:1px solid #c62828;margin-top:9px}"
         ".msg{padding:10px;border-radius:8px;background:#eaf5ee;color:#1f6b40;"
         "font-size:13.5px;margin-bottom:14px}"
         "</style></head><body><div class='card'>"
         "<h1>Lightning EWS</h1><p class='sub'>Connect this node to WiFi</p>");
  if (msg.length()) h += "<div class='msg'>" + msg + "</div>";

  h += F("<form method='POST' action='/save'>"
         "<label>Network</label><select name='ssid'>");
  int n = WiFi.scanNetworks();
  if (n <= 0) h += F("<option value=''>No networks found - reload to rescan</option>");
  else {
    for (int i = 0; i < n && i < 20; i++) {
      h += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) +
           "  (" + String(WiFi.RSSI(i)) + " dBm)</option>";
    }
  }
  WiFi.scanDelete();
  h += F("</select>"
         "<label>Password</label>"
         "<input type='password' name='pass' placeholder='WiFi password'>"
         "<button type='submit'>Save &amp; Connect</button></form>"
         "<form method='POST' action='/forget'>"
         "<button class='alt' type='submit'>Forget saved WiFi</button></form>"
         "</div></body></html>");
  return h;
}

void handleRoot() { portal.send(200, "text/html", portalHtml()); }

void handleSave() {
  String ssid = portal.arg("ssid");
  String pass = portal.arg("pass");
  if (ssid.length() == 0) {
    portal.send(200, "text/html", portalHtml("Please choose a network."));
    return;
  }
  credsSave(ssid, pass);
  portal.send(200, "text/html",
    F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<style>body{font-family:system-ui,sans-serif;background:#0f3d3e;color:#fff;"
      "text-align:center;padding:60px 22px}h2{margin:0 0 10px}p{color:#bcd}"
      "</style></head><body><h2>Saved</h2>"
      "<p>The node is restarting and will connect to your network.<br>"
      "This setup network will disappear.</p></body></html>"));
  delay(1200);
  ESP.restart();
}

void handleForget() {
  credsClear();
  portal.send(200, "text/html", portalHtml("Saved WiFi cleared."));
}

void handleNotFound() {
  portal.sendHeader("Location", "http://192.168.4.1/", true);
  portal.send(302, "text/plain", "");
}

void startPortal() {
  portalActive = true;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  delay(300);
  IPAddress ip = WiFi.softAPIP();
  dns.start(53, "*", ip);

  portal.on("/", handleRoot);
  portal.on("/save",   HTTP_POST, handleSave);
  portal.on("/forget", HTTP_POST, handleForget);
  portal.on("/generate_204",       handleNotFound);
  portal.on("/fwlink",             handleNotFound);
  portal.on("/hotspot-detect.html",handleNotFound);
  portal.onNotFound(handleNotFound);
  portal.begin();

  Serial.println(F("\n================ SETUP MODE ================"));
  Serial.print(F("  Connect to WiFi : ")); Serial.println(AP_SSID);
  Serial.print(F("  Password        : ")); Serial.println(AP_PASSWORD);
  Serial.print(F("  Then open       : http://")); Serial.println(ip);
  Serial.println(F("==========================================="));
}

void portalBlink() {
  static unsigned long t = 0;
  static bool on = false;
  if (millis() - t > 300) { t = millis(); on = !on; digitalWrite(PIN_STATUS_LED, on); }
}

// ===========================================================================
//  WiFi connect / reconnect
// ===========================================================================
bool wifiTryConnect() {
  if (savedSSID.length() == 0) return false;
  Serial.print(F("[WiFi] Connecting to ")); Serial.print(savedSSID);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(savedSSID.c_str(), savedPASS.c_str());
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_CONNECT_TIMEOUT_MS) {
    delay(400); Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("[WiFi] OK  IP: ")); Serial.println(WiFi.localIP());
    return true;
  }
  Serial.println(F("[WiFi] Failed."));
  return false;
}

// Background reconnect. Never reopens the AP: alarms must keep running.
void wifiTick() {
  if (portalActive) return;
  if (!CLOUD_ENABLED || savedSSID.length() == 0) return;
  if (WiFi.status() == WL_CONNECTED) return;
  if (millis() - lastWifiTryMs < 15000) return;
  lastWifiTryMs = millis();
  Serial.println(F("[WiFi] Reconnecting..."));
  WiFi.disconnect();
  WiFi.begin(savedSSID.c_str(), savedPASS.c_str());
}

void checkForgetButton() {
  if (digitalRead(PIN_SILENCE) == LOW) {
    if (silenceHeldSince == 0) silenceHeldSince = millis();
    else if (millis() - silenceHeldSince > 3000) {
      Serial.println(F("[PORTAL] Forget-WiFi held. Clearing and restarting..."));
      credsClear();
      delay(300);
      ESP.restart();
    }
  } else silenceHeldSince = 0;
}

// ===========================================================================
//  SENSOR SOURCE 1 — EMULATOR
// ===========================================================================
#if SENSOR_SOURCE == SRC_EMULATOR
bool lastBtn = HIGH;
unsigned long lastBtnMs = 0;

void sensorSetup() {
  pinMode(PIN_STRIKE_BTN, INPUT_PULLUP);
  analogReadResolution(12);
  Serial.println(F("[SRC] EMULATOR: button = strike, pot = distance (1-40 km)."));
}

uint8_t readPotKm() {
  return constrain(map(analogRead(PIN_POT), 0, 4095, 1, 40), 1, 40);
}

bool sensorPoll(uint8_t &distanceKm) {
  bool b = digitalRead(PIN_STRIKE_BTN);
  if (lastBtn == HIGH && b == LOW && millis() - lastBtnMs > 250) {
    lastBtnMs = millis(); lastBtn = b;
    distanceKm = readPotKm();
    Serial.print(F("[SRC] Button strike @ ")); Serial.print(distanceKm); Serial.println(F(" km"));
    return true;
  }
  lastBtn = b;
  if (Serial.available()) {
    int km = Serial.parseInt();
    if (km >= 1 && km <= 40) {
      distanceKm = (uint8_t)km;
      Serial.print(F("[SRC] Serial strike @ ")); Serial.print(distanceKm); Serial.println(F(" km"));
      return true;
    }
  }
  return false;
}
#endif

// ===========================================================================
//  SENSOR SOURCE 2 — FLASH + THUNDER
// ===========================================================================
#if SENSOR_SOURCE == SRC_FLASH_THUNDER
float lightBase = 0, soundBase = 0;
bool  waitingThunder = false;
unsigned long tFlash = 0;
const int FLASH_DELTA = 350, SOUND_DELTA = 300;
const unsigned long THUNDER_TIMEOUT = 45000;
const uint8_t FLASH_ONLY_KM = 15;

void sensorSetup() {
  analogReadResolution(12);
  lightBase = analogRead(PIN_LIGHT);
  soundBase = analogRead(PIN_SOUND);
  Serial.println(F("[SRC] FLASH+THUNDER ready."));
}

bool sensorPoll(uint8_t &distanceKm) {
  int light = analogRead(PIN_LIGHT), sound = analogRead(PIN_SOUND);
  lightBase = lightBase * 0.995f + light * 0.005f;
  soundBase = soundBase * 0.990f + sound * 0.010f;
  if (!waitingThunder) {
    if (light - lightBase > FLASH_DELTA) {
      waitingThunder = true; tFlash = millis();
      Serial.println(F("[SRC] Flash detected, timing thunder..."));
    }
    return false;
  }
  unsigned long gap = millis() - tFlash;
  if (sound - soundBase > SOUND_DELTA) {
    waitingThunder = false;
    distanceKm = (uint8_t)constrain((int)(gap * 0.000343f + 0.5f), 1, 40);
    Serial.print(F("[SRC] Thunder -> ")); Serial.print(distanceKm); Serial.println(F(" km"));
    return true;
  }
  if (gap > THUNDER_TIMEOUT) {
    waitingThunder = false; distanceKm = FLASH_ONLY_KM;
    Serial.println(F("[SRC] Flash, no thunder -> distant storm."));
    return true;
  }
  return false;
}
#endif

// ===========================================================================
//  SENSOR SOURCE 3 — AS3935
// ===========================================================================
#if SENSOR_SOURCE == SRC_AS3935
#include "SparkFun_AS3935.h"
#define AS3935_OUTDOOR 0x0E
#define INT_LIGHTNING 0x08
SparkFun_AS3935 lightning(0x03);
volatile bool irqFlag = false; volatile uint32_t irqAtMs = 0;
void IRAM_ATTR onIRQ(){ irqFlag = true; irqAtMs = millis(); }

void sensorSetup() {
  Wire.begin(PIN_SDA, PIN_SCL);
  if (!lightning.begin(Wire)) Serial.println(F("[AS3935] not found!"));
  lightning.setIndoorOutdoor(AS3935_OUTDOOR);
  lightning.setNoiseLevel(2);
  lightning.watchdogThreshold(2);
  lightning.spikeRejection(2);
  lightning.lightningThreshold(1);
  pinMode(PIN_AS3935_IRQ, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_AS3935_IRQ), onIRQ, RISING);
  Serial.println(F("[SRC] AS3935 ready."));
}

bool sensorPoll(uint8_t &distanceKm) {
  if (!irqFlag || (millis() - irqAtMs) <= 2) return false;
  irqFlag = false;
  if (lightning.readInterruptReg() != INT_LIGHTNING) return false;
  uint8_t d = lightning.distanceToStorm();
  if (d == 63 || d == 0) return false;
  distanceKm = d;
  return true;
}
#endif

// ===========================================================================
//  [CLOUD] time + Firebase
// ===========================================================================
void timeSync() {
  if (!CLOUD_ENABLED || WiFi.status() != WL_CONNECTED) return;
  configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, "pool.ntp.org", "time.nist.gov");
  struct tm ti;
  if (getLocalTime(&ti, 8000)) { timeSynced = true; Serial.println(F("[NTP] Synced (UTC).")); }
  else Serial.println(F("[NTP] Sync failed; will retry."));
}

uint64_t epochMs() {
  if (!timeSynced) return 0;
  time_t now; time(&now);
  return (uint64_t)now * 1000ULL;
}

void firebaseInit() {
  if (!CLOUD_ENABLED || WiFi.status() != WL_CONNECTED) return;
  fbconfig.api_key      = FB_API_KEY;
  fbconfig.database_url = FB_DATABASE_URL;
  fbconfig.token_status_callback = tokenStatusCallback;

  if (Firebase.signUp(&fbconfig, &fbauth, "", ""))
    Serial.println(F("[FB] Anonymous auth OK."));
  else {
    Serial.print(F("[FB] Auth error: "));
    Serial.println(fbconfig.signer.signupError.message.c_str());
  }
  Firebase.begin(&fbconfig, &fbauth);
  Firebase.reconnectWiFi(true);
  fbdo.setBSSLBufferSize(2048, 1024);
  firebaseReady = true;

  FirebaseJson meta;
  meta.set("name",     NODE_NAME);
  meta.set("location", DEVICE_LOCATION);
  meta.set("lat",      DEVICE_LAT);
  meta.set("lng",      DEVICE_LNG);
  meta.set("firmware", FIRMWARE_VER);
  String path = String("/devices/") + DEVICE_ID + "/meta";
  if (Firebase.RTDB.setJSON(&fbdo, path.c_str(), &meta))
    Serial.println(F("[FB] Meta published."));
  else Serial.println("[FB] Meta failed: " + fbdo.errorReason());
}

void logEvent(uint8_t level, uint8_t distanceKm, const char* type) {
  if (!CLOUD_ENABLED || !firebaseReady || WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[FB] Offline, event not logged."));
    return;
  }
  FirebaseJson ev;
  ev.set("ts",         (double)epochMs());
  ev.set("type",       type);
  ev.set("level",      (int)level);
  ev.set("distanceKm", (int)distanceKm);
  String path = String("/devices/") + DEVICE_ID + "/events";
  if (Firebase.RTDB.pushJSON(&fbdo, path.c_str(), &ev)) {
    Serial.print(F("[FB] Event logged: ")); Serial.println(type);
  } else Serial.println("[FB] Event failed: " + fbdo.errorReason());
  pushStatus();
}

void pushStatus() {
  if (!CLOUD_ENABLED || !firebaseReady || WiFi.status() != WL_CONNECTED) return;
  FirebaseJson st;
  st.set("online",       true);
  st.set("lastSeen",     (double)epochMs());
  st.set("rssi",         (int)WiFi.RSSI());
  st.set("currentLevel", (int)currentLevel);
  st.set("stormActive",  stormActive);
  String path = String("/devices/") + DEVICE_ID + "/status";
  if (!Firebase.RTDB.setJSON(&fbdo, path.c_str(), &st))
    Serial.println("[FB] Status failed: " + fbdo.errorReason());
}

void heartbeatTick() {
  if (portalActive) return;
  if (millis() - lastBeatMs < HEARTBEAT_MS) return;
  lastBeatMs = millis();
  if (!timeSynced) timeSync();
  if (!firebaseReady && WiFi.status() == WL_CONNECTED) firebaseInit();
  pushStatus();
}

// ===========================================================================
//  SHARED PIPELINE  (Bengali alert text)
// ===========================================================================
uint8_t gradeThreat(uint8_t km) {
  if (km <= KM_SHELTER) return 3;
  if (km <= KM_WARNING) return 2;
  return 1;
}

// Threat label, Bengali
const char* levelLabel(uint8_t l) {
  switch (l) {
    case 3: return "নিরাপদ আশ্রয় নিন";
    case 2: return "সতর্কতা";
    case 1: return "আশে পাশে দেখুন";
    default: return "বিপদমুক্ত";
  }
}

// Action line per level, Bengali
const char* levelAdvice(uint8_t l) {
  switch (l) {
    case 3: return "এখনই নিরাপদ আশ্রয়ে যান।";
    case 2: return "বজ্রঝড় এগিয়ে আসছে, আশ্রয় নিন।";
    case 1: return "সতর্ক থাকুন।";
    default: return "কাজ শুরু করতে পারেন।";
  }
}

// English label for the Serial log only (keeps debugging readable)
const char* levelLabelEn(uint8_t l) {
  switch (l) { case 3: return "TAKE SHELTER"; case 2: return "WARNING";
               case 1: return "WATCH"; default: return "CLEAR"; }
}

void handleEvent(uint8_t distanceKm) {
  lastEventMs = millis();
  stormActive = true;
  if (distanceKm < nearestKm) nearestKm = distanceKm;

  uint8_t level = gradeThreat(distanceKm);

  // Log every strike so the dashboard charts show the whole storm.
  logEvent(level, distanceKm, "strike");

  if (level > currentLevel) {                 // escalation only
    currentLevel = level;
    silenced = false;                         // closer strike re-arms the siren
    Serial.print(F("[ALERT] -> ")); Serial.println(levelLabelEn(level));

    if (!smsSent[level]) {
      String m = String(NODE_NAME) + ": " + levelLabel(level) +
                 "। বজ্রপাত প্রায় " + String(distanceKm) + " কিমি দূরে। " +
                 levelAdvice(level);
      broadcastSMS(m);
      smsSent[level] = true;
    }
  }
}

void updateAlarms() {
  if (currentLevel == 0) { digitalWrite(PIN_SIREN, LOW); digitalWrite(PIN_STROBE, LOW); return; }
  unsigned long now = millis();
  unsigned long period = (currentLevel == 3) ? 120 : (currentLevel == 2) ? 250 : 1000;
  if (now - lastToggleMs >= period) {
    lastToggleMs = now;
    strobeState = !strobeState;
    digitalWrite(PIN_STROBE, strobeState);
    if (silenced)                digitalWrite(PIN_SIREN, LOW);
    else if (currentLevel == 3)  digitalWrite(PIN_SIREN, HIGH);        // solid
    else if (currentLevel == 2)  digitalWrite(PIN_SIREN, strobeState); // beep
    else                         digitalWrite(PIN_SIREN, LOW);         // strobe only
  }
}

void resetEpisode() {
  currentLevel = 0; nearestKm = 255; stormActive = false; silenced = false;
  for (uint8_t i = 0; i < 4; i++) smsSent[i] = false;
  digitalWrite(PIN_SIREN, LOW); digitalWrite(PIN_STROBE, LOW);
  Serial.println(F("[EWS] Re-armed. Listening..."));
}

// ===========================================================================
//  SIM800L  (UCS2 / Bengali)
// ===========================================================================
void simInit() {
  if (!SMS_ENABLED) return;
  sim800.begin(9600, SERIAL_8N1, PIN_SIM_RX, PIN_SIM_TX);
  delay(3000);
  sim800.println("AT");                 delay(500);
  sim800.println("ATE0");               delay(500);
  sim800.println("AT+CMGF=1");          delay(500);   // text mode
  sim800.println("AT+CSCS=\"UCS2\"");   delay(500);   // Unicode charset
  sim800.println("AT+CSMP=17,167,0,8"); delay(500);   // dcs = 8 -> UCS2
  Serial.println(F("[SIM800L] Initialised (UCS2 / Bengali)."));
}

void sendSMS(const char* number, const String &text) {
  if (!SMS_ENABLED) { Serial.print(F("[SMS off] ")); Serial.println(text); return; }

  String bodyHex = utf8ToUcs2Hex(text);
  String numHex  = utf8ToUcs2Hex(String(number));   // number must be UCS2 too

  if (ucs2Units(bodyHex) > 70)
    Serial.println(F("[SMS] WARNING: over 70 units, may split or truncate."));

  Serial.print(F("[SIM800L] SMS -> ")); Serial.println(number);

  // Re-assert the mode each send in case the module reset
  sim800.println("AT+CMGF=1");          delay(300);
  sim800.println("AT+CSCS=\"UCS2\"");   delay(300);
  sim800.println("AT+CSMP=17,167,0,8"); delay(300);

  sim800.print("AT+CMGS=\""); sim800.print(numHex); sim800.println("\"");
  delay(600);
  sim800.print(bodyHex);
  delay(200);
  sim800.write(26);                      // Ctrl+Z
  delay(6000);                           // Unicode sends take longer
}

void broadcastSMS(const String &text) {
  for (uint8_t i = 0; i < NUM_RECIPIENTS; i++) { sendSMS(RECIPIENTS[i], text); delay(800); }
}

// ================================ SETUP / LOOP =============================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n[EWS] Booting (portal + cloud + Bengali SMS)..."));

  pinMode(PIN_SIREN, OUTPUT);  pinMode(PIN_STROBE, OUTPUT);
  pinMode(PIN_STATUS_LED, OUTPUT); pinMode(PIN_SILENCE, INPUT_PULLUP);
  digitalWrite(PIN_SIREN, LOW); digitalWrite(PIN_STROBE, LOW);

  sensorSetup();

  credsLoad();
  if (wifiTryConnect()) {
    timeSync();
    firebaseInit();
    pushStatus();
  } else {
    startPortal();               // no creds, or saved network unreachable
  }

  simInit();

  for (int i = 0; i < 3; i++){ digitalWrite(PIN_STATUS_LED,HIGH); delay(120);
                               digitalWrite(PIN_STATUS_LED,LOW);  delay(120); }
  Serial.println(F("[EWS] Armed."));
}

void loop() {
  // ---- Setup mode: serve the portal, but KEEP DETECTING ----
  if (portalActive) {
    dns.processNextRequest();
    portal.handleClient();
    portalBlink();

    uint8_t d;
    if (sensorPoll(d)) handleEvent(d);     // alarms work during setup too
    updateAlarms();
    return;
  }

  // ---- Normal operation ----
  uint8_t distanceKm;
  if (sensorPoll(distanceKm)) handleEvent(distanceKm);

  if (digitalRead(PIN_SILENCE) == LOW) silenced = true;
  checkForgetButton();                     // hold 3 s = wipe WiFi + restart

  updateAlarms();
  wifiTick();
  heartbeatTick();

  if (stormActive && (millis() - lastEventMs) > ALL_CLEAR_MS) {
    Serial.println(F("[ALERT] Storm passed -> ALL CLEAR."));
    broadcastSMS(String(NODE_NAME) + ": বিপদমুক্ত। আর বজ্রপাত নেই, কাজ শুরু করতে পারেন।");
    logEvent(0, nearestKm, "all_clear");
    resetEpisode();
    pushStatus();
  }
}
