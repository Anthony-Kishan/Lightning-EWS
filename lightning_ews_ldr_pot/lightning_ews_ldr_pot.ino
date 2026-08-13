/*
 * ============================================================================
 *  Lightning Early-Warning System
 *  LDR FLASH DETECTION + B5K POT DISTANCE  |  CAPTIVE-PORTAL WiFi
 *  FIREBASE LOGGING  |  BENGALI (UCS2) SMS
 *  Final-Year CSE Project  |  Author: Imran
 * ----------------------------------------------------------------------------
 *  IMPORTANT: SAVE THIS FILE AS UTF-8, or the Bengali SMS will be garbled.
 * ----------------------------------------------------------------------------
 *  TWO SENSOR SOURCES (pick one line below)
 *
 *    SRC_EMULATOR   Button (GPIO4) fires a strike. B5K pot (GPIO34) sets the
 *                   distance 1-40 km. You can also type a number in Serial.
 *                   Use this to demo the whole system with no light needed.
 *
 *    SRC_LDR_FLASH  LDR (GPIO35) detects a bright flash. The B5K pot (GPIO34)
 *                   sets the reported distance. So the LDR answers "did
 *                   lightning happen?" and the pot answers "how far?".
 *                   Test it with a camera flash or a torch.
 *
 *  Both sources share the SAME pipeline: threat grading, siren, strobe,
 *  Bengali SMS, Firebase logging, 30-min all-clear.
 * ----------------------------------------------------------------------------
 *  ADC NOTE: GPIO34 (pot) and GPIO35 (LDR) are both ADC1. This matters -
 *  ADC2 pins stop reading once WiFi is on, so do not move these to ADC2.
 *
 *  LDR WIRING (voltage divider):
 *      3V3 ---- LDR ----+---- GPIO35
 *      
 *                       |
 *                      10k
 *                       |
 *                      GND
 *      More light -> higher voltage on GPIO35. If yours reads backwards,
 *      swap the LDR and the 10k resistor.
 *
 *  B5K POT WIRING:
 *      one outer leg -> 3V3, other outer leg -> GND, wiper -> GPIO34
 *
 *  LIBRARIES: only Mobizt's "Firebase Arduino Client Library for ESP8266 and
 *  ESP32" is external. WebServer / DNSServer / Preferences ship with the core.
 * ============================================================================
 */

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <time.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// ------------------- CHOOSE YOUR SOURCE (change this one line) -------------
#define SRC_EMULATOR    1
#define SRC_LDR_FLASH   2

#define SENSOR_SOURCE   SRC_LDR_FLASH
// --------------------------------------------------------------------------

// ------------------------------ USER CONFIG --------------------------------
const char* RECIPIENTS[] = { "+8801798173527", "+8801581413109" };//8801540392159
const uint8_t NUM_RECIPIENTS = sizeof(RECIPIENTS) / sizeof(RECIPIENTS[0]);

// Node name appears in every SMS. Keep it SHORT - UCS2 SMS = 70 chars total.
const char*   NODE_NAME  = "মাঠ নোড ১";
const bool    SMS_ENABLED = true;

const uint8_t KM_SHELTER = 10;              // <= 10 km -> TAKE SHELTER (L3)
const uint8_t KM_WARNING = 25;              // <= 25 km -> WARNING      (L2)
                                            //  > 25 km -> WATCH        (L1)
const unsigned long ALL_CLEAR_MS = 30UL * 60UL * 1000UL;   // 30 min

// ------------------------- LDR FLASH TUNING --------------------------------
// A flash is detected when the LDR reading jumps this far above its slow
// rolling baseline. Raise it if daylight/room lights cause false triggers;
// lower it if a real flash is missed. Watch the Serial plot to tune.
const int  FLASH_DELTA       = 400;       // ADC counts above baseline (0-4095)
const unsigned long FLASH_COOLDOWN_MS = 2000;  // ignore re-triggers for 2 s
const float BASELINE_ALPHA   = 0.005f;    // baseline drift speed (smaller=slower)

// ---------------------------- [PORTAL] CONFIG ------------------------------
#define AP_SSID         "Lightning-EWS-Setup"
#define AP_PASSWORD     "11112222"          // >= 8 chars
#define WIFI_CONNECT_TIMEOUT_MS 20000
#define NVS_NAMESPACE   "ewscfg"

// ---------------------------- [CLOUD] CONFIG -------------------------------
#define FB_API_KEY      "AIzaSyDDEDKHAd1B-U_BI1034STMPej4aTxlWio"
#define FB_DATABASE_URL "https://lightning-ews-default-rtdb.asia-southeast1.firebasedatabase.app/"

#define DEVICE_ID       "node1"
#define DEVICE_LOCATION "Field site, Dhaka"
#define DEVICE_LAT      23.8103
#define DEVICE_LNG      90.4125
#define FIRMWARE_VER    "1.4-ldr"

const bool CLOUD_ENABLED = true;
const unsigned long HEARTBEAT_MS = 30000;
const long  GMT_OFFSET_SEC = 0;             // store UTC; dashboard converts
const int   DST_OFFSET_SEC = 0;

// ------------------------------ PIN MAP ------------------------------------
#define PIN_SIM_RX     16    // ESP32 RX2 <- SIM800L TXD
#define PIN_SIM_TX     17    // ESP32 TX2 -> SIM800L RXD (via divider)
#define PIN_SIREN      21
#define PIN_STROBE     26
#define PIN_STATUS_LED  2
#define PIN_SILENCE    23    // short press = mute siren; hold 3 s = forget WiFi

#define PIN_STRIKE_BTN  4    // emulator strike button (to GND)
#define PIN_POT         34   // B5K pot wiper -> distance 1-40 km   (ADC1)
#define PIN_LDR         35   // LDR divider    -> flash detection    (ADC1)

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
String   scanCache      = "";      // pre-rendered <option> list
int      scanCount      = -1;      // -1 = never scanned



#define FB_DB_SECRET "B96EGBeRsrcaxMdkIVEFWEjv1eHILBGPpbTNWPeW"
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
//  SHARED INPUT HELPER — the B5K pot sets distance in BOTH modes
// ===========================================================================
uint8_t readPotKm() {
  int raw = analogRead(PIN_POT);                    // 0..4095
  return (uint8_t)constrain(map(raw, 0, 4095, 1, 40), 1, 40);
}

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


// ---------------------------------------------------------------------------
//  Scan into a cache. Doing this OUTSIDE the page render is the fix: scanning
//  during an HTTP response makes the AP stall and the browser time out.
//  async=false, show_hidden=true, and an explicit STA disconnect first give
//  the most reliable result while softAP is running.
// ---------------------------------------------------------------------------
void scanNetworksCached() {
  Serial.println(F("[SCAN] Scanning..."));
  WiFi.scanDelete();
  WiFi.disconnect(false, false);      // drop any half-open STA attempt
  delay(100);

  scanCount = WiFi.scanNetworks(false, true);   // blocking, include hidden
  scanCache = "";

  if (scanCount <= 0) {
    Serial.println(F("[SCAN] No networks found."));
  } else {
    Serial.print(F("[SCAN] Found ")); Serial.println(scanCount);
    for (int i = 0; i < scanCount && i < 25; i++) {
      String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) continue;                 // skip hidden/blank
      scanCache += "<option value='" + ssid + "'>" + ssid +
                   "  (" + String(WiFi.RSSI(i)) + " dBm)</option>";
    }
  }
  WiFi.scanDelete();
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
         "button.alt{background:#fff;color:#0f3d3e;border:1px solid #0f3d3e;margin-top:9px}"
         "button.danger{background:#fff;color:#c62828;border:1px solid #c62828;margin-top:9px}"
         ".msg{padding:10px;border-radius:8px;background:#eaf5ee;color:#1f6b40;"
         "font-size:13.5px;margin-bottom:14px}"
         ".warn{background:#fff4e5;color:#8a5300}"
         ".or{text-align:center;font-size:12px;color:#8a9a9a;margin:12px 0 0}"
         ".hint{font-size:11.5px;color:#8a9a9a;margin:5px 0 0}"
         "</style></head><body><div class='card'>"
         "<h1>Lightning EWS</h1><p class='sub'>Connect this node to WiFi</p>");

  if (msg.length()) h += "<div class='msg'>" + msg + "</div>";

  if (scanCount == 0)
    h += F("<div class='msg warn'>No networks detected. Type the network name "
           "manually below, or tap Rescan.</div>");

  h += F("<form method='POST' action='/save'>"
         "<label>Nearby networks</label><select name='ssid' id='pick'>"
         "<option value=''>-- select a network --</option>");
  h += scanCache;                       // cached, no scan during render
  h += F("</select>"

         "<p class='or'>or</p>"

         "<label>Enter network name manually</label>"
         "<input type='text' name='ssid_manual' id='man' "
         "placeholder='Type exact SSID (case sensitive)' autocapitalize='off' "
         "autocorrect='off' spellcheck='false'>"
         "<p class='hint'>Use this for hidden networks or if the scan is empty. "
         "This overrides the dropdown.</p>"

         "<label>Password</label>"
         "<input type='password' name='pass' placeholder='WiFi password' "
         "autocapitalize='off' autocorrect='off' spellcheck='false'>"
         "<p class='hint'>Leave empty for an open network.</p>"

         "<button type='submit'>Save &amp; Connect</button></form>"

         "<form method='POST' action='/rescan'>"
         "<button class='alt' type='submit'>Rescan networks</button></form>"

         "<form method='POST' action='/forget'>"
         "<button class='danger' type='submit'>Forget saved WiFi</button></form>"

         "<script>"
         "var m=document.getElementById('man'),p=document.getElementById('pick');"
         "m.addEventListener('input',function(){if(m.value)p.selectedIndex=0;});"
         "p.addEventListener('change',function(){if(p.value)m.value='';});"
         "</script>"
         "</div></body></html>");
  return h;
}


void handleRoot() { portal.send(200, "text/html", portalHtml()); }

void handleSave() {
  String ssid   = portal.arg("ssid");
  String manual = portal.arg("ssid_manual");
  String pass   = portal.arg("pass");

  manual.trim();                        // stray spaces are a classic failure
  if (manual.length() > 0) ssid = manual;   // manual entry overrides the list
  ssid.trim();

  if (ssid.length() == 0) {
    portal.send(200, "text/html",
      portalHtml("Please pick a network or type its name."));
    return;
  }

  Serial.print(F("[PORTAL] Saving SSID: ")); Serial.println(ssid);
  credsSave(ssid, pass);
  portal.send(200, "text/html",
    F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<style>body{font-family:system-ui,sans-serif;background:#0f3d3e;color:#fff;"
      "text-align:center;padding:60px 22px}h2{margin:0 0 10px}p{color:#bcd}"
      "</style></head><body><h2>Saved</h2>"
      "<p>The node is restarting and will connect to your network.<br>"
      "This setup network will disappear.<br><br>"
      "If it cannot connect, the setup network reappears.</p></body></html>"));
  delay(1200);
  ESP.restart();
}

void handleRescan() {
  scanNetworksCached();
  portal.send(200, "text/html",
    portalHtml(scanCount > 0 ? "Scan complete." : "Still nothing found."));
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

    scanNetworksCached();

  portal.on("/", handleRoot);
  portal.on("/save",   HTTP_POST, handleSave);
  portal.on("/rescan", HTTP_POST, handleRescan); 
  portal.on("/forget", HTTP_POST, handleForget);
  portal.on("/generate_204",        handleNotFound);
  portal.on("/fwlink",              handleNotFound);
  portal.on("/hotspot-detect.html", handleNotFound);
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
//  SENSOR SOURCE 1 — EMULATOR (button fires, pot sets distance)
// ===========================================================================
#if SENSOR_SOURCE == SRC_EMULATOR
bool lastBtn = HIGH;
unsigned long lastBtnMs = 0;

void sensorSetup() {
  pinMode(PIN_STRIKE_BTN, INPUT_PULLUP);
  analogReadResolution(12);
  Serial.println(F("[SRC] EMULATOR: button = strike, B5K pot = distance (1-40 km)."));
  Serial.println(F("      You can also type a distance in Serial and press Enter."));
}

bool sensorPoll(uint8_t &distanceKm) {
  bool b = digitalRead(PIN_STRIKE_BTN);
  if (lastBtn == HIGH && b == LOW && millis() - lastBtnMs > 250) {   // debounce
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
//  SENSOR SOURCE 2 — LDR FLASH (LDR detects, pot sets distance)
//  The LDR answers "did lightning flash?"  The pot answers "how far?"
// ===========================================================================
#if SENSOR_SOURCE == SRC_LDR_FLASH
float         ldrBase      = 0;      // slow rolling baseline of ambient light
unsigned long lastFlashMs  = 0;      // cooldown so one flash = one event
bool          baseReady    = false;

void sensorSetup() {
  analogReadResolution(12);
  // Seed the baseline with an average so the first reading isn't noise
  long sum = 0;
  for (int i = 0; i < 32; i++) { sum += analogRead(PIN_LDR); delay(10); }
  ldrBase = sum / 32.0f;
  baseReady = true;

  Serial.println(F("[SRC] LDR FLASH mode."));
  Serial.print (F("      Ambient baseline: ")); Serial.println((int)ldrBase);
  Serial.print (F("      Trigger delta   : ")); Serial.println(FLASH_DELTA);
  Serial.println(F("      B5K pot sets the reported distance (1-40 km)."));
  Serial.println(F("      Test with a camera flash or a torch."));
}

bool sensorPoll(uint8_t &distanceKm) {
  int light = analogRead(PIN_LDR);

  // Slow-moving baseline: tracks sunrise/sunset and room lighting, but is far
  // too slow to follow a lightning flash, so a flash stands out as a spike.
  ldrBase = ldrBase * (1.0f - BASELINE_ALPHA) + light * BASELINE_ALPHA;

  // Cooldown: a real flash lingers for many loop passes, and lightning often
  // flickers. Without this one strike would fire dozens of events.
  if (millis() - lastFlashMs < FLASH_COOLDOWN_MS) return false;

  if (light - ldrBase > FLASH_DELTA) {
    lastFlashMs = millis();
    distanceKm  = readPotKm();                  // pot decides the distance
    Serial.print(F("[SRC] FLASH detected (raw ")); Serial.print(light);
    Serial.print(F(", base ")); Serial.print((int)ldrBase);
    Serial.print(F(") @ ")); Serial.print(distanceKm); Serial.println(F(" km"));
    return true;
  }
  return false;
}

// Prints raw LDR values so you can tune FLASH_DELTA on the Serial Plotter.
// Call this from loop() only while calibrating, then comment it out.
#endif

// ===========================================================================
//  LIVE SENSOR MONITOR — non-blocking, prints POT + LDR continuously
//  Set MONITOR_MS to 0 to disable. Uses millis(), never delay(), so alarms,
//  WiFi, and Firebase keep running normally while it prints.
// ===========================================================================
const unsigned long MONITOR_MS = 250;   // print interval; 0 = off

void sensorMonitor() {
  if (MONITOR_MS == 0) return;
  static unsigned long tLast = 0;
  if (millis() - tLast < MONITOR_MS) return;
  tLast = millis();

  int potRaw = analogRead(PIN_POT);
  int ldrRaw = analogRead(PIN_LDR);
  uint8_t km = (uint8_t)constrain(map(potRaw, 0, 4095, 1, 40), 1, 40);

  // Serial Plotter friendly: "LABEL:value" pairs separated by spaces/tabs
  Serial.print(F("POT:"));    Serial.print(potRaw);
  Serial.print(F("\tKM:"));   Serial.print(km);
  Serial.print(F("\tLDR:"));  Serial.print(ldrRaw);

#if SENSOR_SOURCE == SRC_LDR_FLASH
  Serial.print(F("\tBASE:"));  Serial.print((int)ldrBase);
  Serial.print(F("\tDELTA:")); Serial.print(ldrRaw - (int)ldrBase);
  Serial.print(F("\tTRIG:"));  Serial.print(FLASH_DELTA);
  // headroom: how close you are to firing (negative = would trigger)
  Serial.print(F("\tGAP:"));   Serial.print(FLASH_DELTA - (ldrRaw - (int)ldrBase));
#endif

  Serial.println();
}

// ===========================================================================
//  [CLOUD] time + Firebase
// ===========================================================================
bool sntpStarted = false;      // add near your other cloud state

// Start SNTP ONCE. Calling configTime() repeatedly is what triggers the
// "Required to lock TCPIP core functionality!" assert and reboot loop.
void timeSyncBegin() {
  if (!CLOUD_ENABLED || sntpStarted) return;
  if (WiFi.status() != WL_CONNECTED) return;
  configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC,
             "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  sntpStarted = true;
  Serial.println(F("[NTP] SNTP started, waiting for time..."));
}

// Non-blocking check. Safe to call as often as you like.
void timeSyncPoll() {
  if (timeSynced || !sntpStarted) return;
  time_t now = time(nullptr);
  if (now > 1700000000) {                 // sane epoch => we have real time
    timeSynced = true;
    Serial.print(F("[NTP] Synced (UTC): ")); Serial.println((uint32_t)now);
  }
}

uint64_t epochMs() {
  if (!timeSynced) return 0;
  time_t now; time(&now);
  return (uint64_t)now * 1000ULL;
}


// ===========================================================================
//  FIREBASE via plain REST  (stateless - no persistent TLS to time out)
//  Each call opens a connection, sends, closes. Slower per call, but it
//  cannot be broken by the blocking SMS delays, and uses far less RAM.
// ===========================================================================

// method: "PUT" to overwrite a path, "POST" to push a new child
bool fbSend(const String &path, const String &json, const char* method) {
  if (!CLOUD_ENABLED || WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[FB] Offline, skipped."));
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();               // skip cert validation (fine for this project)
  client.setTimeout(8000);

  String url = String(FB_DATABASE_URL);
  if (url.endsWith("/")) url.remove(url.length() - 1);
  url += path + ".json?auth=" + FB_DB_SECRET;

  HTTPClient http;
  http.setTimeout(8000);
  http.setReuse(false);
  if (!http.begin(client, url)) {
    Serial.println(F("[FB] begin() failed"));
    return false;
  }
  http.addHeader("Content-Type", "application/json");

  int code = http.sendRequest(method, (uint8_t*)json.c_str(), json.length());
  bool ok = (code == 200);
  if (!ok) { Serial.print(F("[FB] HTTP ")); Serial.println(code); }
  http.end();                          // close immediately, keep nothing open
  return ok;
}

void firebaseInit() {
  if (!CLOUD_ENABLED || WiFi.status() != WL_CONNECTED) return;
  String meta = "{\"name\":\"" + String(NODE_NAME) +
                "\",\"location\":\"" + String(DEVICE_LOCATION) +
                "\",\"lat\":" + String(DEVICE_LAT, 4) +
                ",\"lng\":" + String(DEVICE_LNG, 4) +
                ",\"firmware\":\"" + String(FIRMWARE_VER) + "\"}";
  if (fbSend("/devices/" DEVICE_ID "/meta", meta, "PUT"))
    Serial.println(F("[FB] Meta published."));
  firebaseReady = true;
}

void pushStatus() {
  String st = "{\"online\":true,\"lastSeen\":" + String((double)epochMs(), 0) +
              ",\"rssi\":" + String(WiFi.RSSI()) +
              ",\"currentLevel\":" + String(currentLevel) +
              ",\"stormActive\":" + String(stormActive ? "true" : "false") + "}";
  if (fbSend("/devices/" DEVICE_ID "/status", st, "PUT"))
    Serial.println(F("[FB] Status OK."));
}

void logEvent(uint8_t level, uint8_t distanceKm, const char* type) {
  String ev = "{\"ts\":" + String((double)epochMs(), 0) +
              ",\"type\":\"" + String(type) +
              "\",\"level\":" + String(level) +
              ",\"distanceKm\":" + String(distanceKm) + "}";
  if (fbSend("/devices/" DEVICE_ID "/events", ev, "POST"))
    { Serial.print(F("[FB] Event logged: ")); Serial.println(type); }
  // NOTE: no pushStatus() here - it happens in handleEvent, before the SMS
}

void heartbeatTick() {
  if (portalActive) return;
  if (millis() - lastBeatMs < HEARTBEAT_MS) return;
  lastBeatMs = millis();
  timeSyncBegin();     // no-op after the first call
  timeSyncPoll();
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
  if (level > currentLevel) { currentLevel = level; silenced = false; }

  // --- Cloud first: fast, and finished before SMS blocks the CPU ---
  logEvent(level, distanceKm, "strike");
  pushStatus();

  // --- Then the slow blocking SMS ---
  if (level == currentLevel && !smsSent[level]) {
    Serial.print(F("[ALERT] -> ")); Serial.println(levelLabelEn(level));
    String m = String(NODE_NAME) + ": " + levelLabel(level) +
               "। বজ্রপাত ~" + String(distanceKm) + " কিমি দূরে। " +
               levelAdvice(level);
    broadcastSMS(m);
    smsSent[level] = true;
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
  Serial.println(F("\n[EWS] Booting (LDR + pot / portal / cloud / Bengali SMS)..."));

  pinMode(PIN_SIREN, OUTPUT);  pinMode(PIN_STROBE, OUTPUT);
  pinMode(PIN_STATUS_LED, OUTPUT); pinMode(PIN_SILENCE, INPUT_PULLUP);
  digitalWrite(PIN_SIREN, LOW); digitalWrite(PIN_STROBE, LOW);

  sensorSetup();

  credsLoad();
  if (wifiTryConnect()) {
    timeSyncBegin();
    // give NTP a few seconds, but never block forever
    for (int i = 0; i < 40 && !timeSynced; i++) { delay(250); timeSyncPoll(); }
    if (!timeSynced) Serial.println(F("[NTP] Not synced yet; continuing."));
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
    sensorMonitor();

    uint8_t d;
    if (sensorPoll(d)) handleEvent(d);     // alarms work during setup too
    updateAlarms();
    return;
  }

  // ---- Normal operation ----

  uint8_t distanceKm;
  if (sensorPoll(distanceKm)) handleEvent(distanceKm);

  sensorMonitor();          // live POT + LDR readout

  // Uncomment while tuning FLASH_DELTA, then comment out again:
  // #if SENSOR_SOURCE == SRC_LDR_FLASH
  //   ldrDebugPrint();
  // #endif

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
