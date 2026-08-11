/*
 * ============================================================================
 *  IoT-Based Lightning Early-Warning System for Field Workers
 *  Final-Year CSE Project  |  Author: Imran
 * ----------------------------------------------------------------------------
 *  Board   : ESP32 DevKit V1 (38-pin)
 *  Sensor  : AS3935 Franklin Lightning Sensor (I2C mode)
 *  Comms   : SIM800L GSM module (SMS alerts over UART2)
 *  Outputs : High-dB siren + LED strobe beacon (via transistor/MOSFET drivers)
 *  Power   : Solar -> TP4056 -> 18650; boost to 5V for ESP32; VBAT for SIM800L
 * ----------------------------------------------------------------------------
 *  WHAT IT DOES
 *    - Continuously listens for lightning with the AS3935.
 *    - Estimates distance to the storm and grades the threat:
 *          WATCH (>25 km) -> WARNING (10-25 km) -> TAKE SHELTER (<=10 km)
 *    - Drives a local siren + strobe and sends SMS warnings to registered
 *      phones. Sends an ALL-CLEAR when the storm has moved off.
 *    - Rejects man-made disturbers and logs everything to Serial.
 *
 *  LIBRARIES TO INSTALL (Arduino IDE -> Library Manager)
 *    - "SparkFun AS3935 Lightning Detector" by SparkFun
 *    - Wire (built in)
 *
 *  NOTE: SIM800L RX is ~2.8V logic. Feed ESP32 TX (GPIO17) through the
 *        resistor divider shown in the wiring diagram (R1 = 1k, R2 = 5.6k)
 *        or a level shifter. Add a 1000uF capacitor across SIM800L VCC-GND.
 * ============================================================================
 */

#include <Wire.h>
#include "SparkFun_AS3935.h"

// ------------------------------ USER CONFIG --------------------------------

// Phone numbers that receive SMS alerts (international format, e.g. +8801XXXXXXXXX)
const char* RECIPIENTS[] = {
  "+8801XXXXXXXXX",
  "+8801YYYYYYYYY"
};
const uint8_t NUM_RECIPIENTS = sizeof(RECIPIENTS) / sizeof(RECIPIENTS[0]);

// A short name/location shown in the SMS so the receiver knows which node fired
const char* NODE_NAME = "Field Node 1";

// Distance thresholds (km) for threat grading
const uint8_t KM_SHELTER = 10;   // <= this  -> TAKE SHELTER (level 3)
const uint8_t KM_WARNING = 25;   // <= this  -> WARNING      (level 2)
                                 // > KM_WARNING -> WATCH     (level 1)

// Storm is considered over if no lightning for this long -> send ALL CLEAR
const unsigned long ALL_CLEAR_MS = 30UL * 60UL * 1000UL;   // 30 minutes

// ------------------------------ PIN MAP ------------------------------------

// I2C (AS3935)
#define PIN_SDA        21
#define PIN_SCL        22
#define PIN_AS3935_IRQ  4    // AS3935 IRQ -> ESP32 GPIO4

// SIM800L on UART2
#define PIN_SIM_RX     16    // ESP32 RX2  <- SIM800L TXD
#define PIN_SIM_TX     17    // ESP32 TX2  -> SIM800L RXD (through divider)

// Alarm outputs (each drives a transistor/MOSFET, see wiring diagram)
#define PIN_SIREN      25
#define PIN_STROBE     26
#define PIN_STATUS_LED  2    // onboard LED

// Optional local silence button (mutes siren, keeps strobe). To GND, uses pullup.
#define PIN_SILENCE    27

// ---------------------- AS3935 register constants --------------------------
// Defined here so the sketch compiles regardless of library version.
#define AS3935_INDOOR   0x12
#define AS3935_OUTDOOR  0x0E
#define INT_NOISE       0x01
#define INT_DISTURBER   0x04
#define INT_LIGHTNING   0x08
#define AS3935_ADDR     0x03   // default I2C address

SparkFun_AS3935 lightning(AS3935_ADDR);
HardwareSerial sim800(2);      // UART2 for SIM800L

// ------------------------------ STATE --------------------------------------
volatile bool     irqFlag      = false;   // set by ISR on AS3935 interrupt
volatile uint32_t irqAtMs      = 0;

uint8_t  currentLevel = 0;                 // 0 none, 1 watch, 2 warning, 3 shelter
uint8_t  nearestKm    = 255;               // nearest strike this episode
bool     stormActive  = false;
bool     smsSent[4]   = {false,false,false,false};   // per-level SMS latch
unsigned long lastEventMs = 0;

// Non-blocking alarm timing
unsigned long lastToggleMs = 0;
bool strobeState = false;
bool sirenState  = false;
bool silenced    = false;

// ------------------------------ ISR ----------------------------------------
void IRAM_ATTR onLightningIRQ() {
  irqFlag = true;
  irqAtMs = millis();
}

// ============================================================================
//  SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n[Lightning EWS] Booting..."));

  pinMode(PIN_SIREN,   OUTPUT);
  pinMode(PIN_STROBE,  OUTPUT);
  pinMode(PIN_STATUS_LED, OUTPUT);
  pinMode(PIN_SILENCE, INPUT_PULLUP);
  digitalWrite(PIN_SIREN,  LOW);
  digitalWrite(PIN_STROBE, LOW);

  // ---- AS3935 over I2C ----
  Wire.begin(PIN_SDA, PIN_SCL);
  if (!lightning.begin(Wire)) {
    Serial.println(F("[AS3935] NOT found. Check wiring/I2C address. Halting."));
    fatalBlink();
  }
  Serial.println(F("[AS3935] Ready."));

  // Outdoor gain is essential for a field device
  lightning.setIndoorOutdoor(AS3935_OUTDOOR);

  // Disturber rejection / noise tuning (raise these if false alarms appear)
  lightning.setNoiseLevel(2);       // 1..7  higher = less sensitive to noise
  lightning.watchdogThreshold(2);   // 1..10 higher = more robust vs disturbers
  lightning.spikeRejection(2);      // 1..11 higher = stricter event validation
  lightning.lightningThreshold(1);  // strikes needed before an interrupt (1,5,9,16)
  lightning.maskDisturber(false);   // keep reporting disturbers so we can log them

  // AS3935 IRQ interrupt
  pinMode(PIN_AS3935_IRQ, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_AS3935_IRQ), onLightningIRQ, RISING);

  // ---- SIM800L ----
  sim800.begin(9600, SERIAL_8N1, PIN_SIM_RX, PIN_SIM_TX);
  delay(3000);                      // let the module register on the network
  simInit();

  Serial.println(F("[Lightning EWS] System armed. Listening..."));
  statusOK();
}

// ============================================================================
//  MAIN LOOP
// ============================================================================
void loop() {
  // Handle a pending sensor interrupt (wait ~2ms after IRQ before reading)
  if (irqFlag && (millis() - irqAtMs) > 2) {
    irqFlag = false;
    handleSensorEvent();
  }

  // Local silence button (mutes siren only)
  if (digitalRead(PIN_SILENCE) == LOW) {
    silenced = true;
  }

  // Escalating/holding alarm patterns
  updateAlarms();

  // All-clear check
  if (stormActive && (millis() - lastEventMs) > ALL_CLEAR_MS) {
    sendAllClear();
    resetEpisode();
  }
}

// ---------------------------------------------------------------------------
//  Read and classify a sensor event
// ---------------------------------------------------------------------------
void handleSensorEvent() {
  uint8_t intVal = lightning.readInterruptReg();

  if (intVal == INT_NOISE) {
    Serial.println(F("[AS3935] Noise level too high (ignored)."));
    return;
  }
  if (intVal == INT_DISTURBER) {
    Serial.println(F("[AS3935] Disturber detected (rejected)."));
    return;
  }
  if (intVal != INT_LIGHTNING) return;

  uint8_t distance = lightning.distanceToStorm();   // km, 1..40; 63 = out of range
  long    energy   = lightning.lightningEnergy();

  Serial.print(F("[AS3935] LIGHTNING  distance="));
  Serial.print(distance);
  Serial.print(F(" km  energy="));
  Serial.println(energy);

  if (distance == 63 || distance == 0) {
    // Out of range / non-metric event; note it but do not alarm
    Serial.println(F("[AS3935] Strike out of range, no alert."));
    return;
  }

  lastEventMs = millis();
  stormActive = true;
  if (distance < nearestKm) nearestKm = distance;

  uint8_t level = gradeThreat(distance);
  if (level > currentLevel) {         // escalation only
    currentLevel = level;
    silenced = false;                 // a closer strike re-arms the siren
    announce(level, distance);
  }
}

uint8_t gradeThreat(uint8_t km) {
  if (km <= KM_SHELTER) return 3;
  if (km <= KM_WARNING) return 2;
  return 1;
}

// ---------------------------------------------------------------------------
//  Announce a threat level (SMS, rate-limited to once per level per episode)
// ---------------------------------------------------------------------------
void announce(uint8_t level, uint8_t km) {
  const char* label = levelLabel(level);
  Serial.print(F("[ALERT] Level -> "));
  Serial.println(label);

  if (!smsSent[level]) {
    String msg = String(NODE_NAME) + ": " + label +
                 ". Lightning ~" + String(km) + " km away. ";
    if (level == 3)      msg += "TAKE SHELTER NOW.";
    else if (level == 2) msg += "Seek shelter, storm approaching.";
    else                 msg += "Stay alert.";
    broadcastSMS(msg);
    smsSent[level] = true;
  }
}

const char* levelLabel(uint8_t level) {
  switch (level) {
    case 3: return "TAKE SHELTER";
    case 2: return "WARNING";
    case 1: return "WATCH";
    default: return "CLEAR";
  }
}

// ---------------------------------------------------------------------------
//  Non-blocking siren + strobe patterns based on currentLevel
// ---------------------------------------------------------------------------
void updateAlarms() {
  unsigned long now = millis();

  if (currentLevel == 0) {
    digitalWrite(PIN_SIREN,  LOW);
    digitalWrite(PIN_STROBE, LOW);
    return;
  }

  // Strobe: faster blink for higher threat
  unsigned long strobePeriod = (currentLevel == 3) ? 120 :
                               (currentLevel == 2) ? 250 : 1000;
  if (now - lastToggleMs >= strobePeriod) {
    lastToggleMs = now;
    strobeState = !strobeState;
    digitalWrite(PIN_STROBE, strobeState);

    // Siren behaviour
    if (silenced) {
      digitalWrite(PIN_SIREN, LOW);
    } else if (currentLevel == 3) {
      digitalWrite(PIN_SIREN, HIGH);            // solid siren
    } else if (currentLevel == 2) {
      digitalWrite(PIN_SIREN, strobeState);     // beep in step with strobe
    } else {
      digitalWrite(PIN_SIREN, LOW);             // watch = strobe only
    }
  }
}

// ---------------------------------------------------------------------------
//  End-of-storm handling
// ---------------------------------------------------------------------------
void sendAllClear() {
  Serial.println(F("[ALERT] Storm passed -> ALL CLEAR."));
  String msg = String(NODE_NAME) + ": ALL CLEAR. No lightning for 30 min. Safe to resume.";
  broadcastSMS(msg);
}

void resetEpisode() {
  currentLevel = 0;
  nearestKm    = 255;
  stormActive  = false;
  silenced     = false;
  for (uint8_t i = 0; i < 4; i++) smsSent[i] = false;
  digitalWrite(PIN_SIREN,  LOW);
  digitalWrite(PIN_STROBE, LOW);
  Serial.println(F("[Lightning EWS] Re-armed. Listening..."));
}

// ============================================================================
//  SIM800L helpers
// ============================================================================
void simInit() {
  simCmd("AT", 1000);
  simCmd("ATE0", 1000);          // echo off
  simCmd("AT+CMGF=1", 1000);     // SMS text mode
  simCmd("AT+CSCS=\"GSM\"", 1000);
  Serial.println(F("[SIM800L] Initialised."));
}

void broadcastSMS(const String &text) {
  for (uint8_t i = 0; i < NUM_RECIPIENTS; i++) {
    sendSMS(RECIPIENTS[i], text);
    delay(1000);
  }
}

void sendSMS(const char* number, const String &text) {
  Serial.print(F("[SIM800L] SMS -> "));
  Serial.println(number);

  sim800.print("AT+CMGS=\"");
  sim800.print(number);
  sim800.println("\"");
  delay(500);

  sim800.print(text);
  delay(200);
  sim800.write(26);              // Ctrl+Z sends the message
  delay(4000);                   // give the module time to transmit
}

// Send a basic AT command and echo the reply to Serial
void simCmd(const char* cmd, unsigned long waitMs) {
  sim800.println(cmd);
  unsigned long t = millis();
  while (millis() - t < waitMs) {
    while (sim800.available()) Serial.write(sim800.read());
  }
}

// ============================================================================
//  Status helpers
// ============================================================================
void statusOK() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_STATUS_LED, HIGH); delay(120);
    digitalWrite(PIN_STATUS_LED, LOW);  delay(120);
  }
}

void fatalBlink() {
  while (true) {
    digitalWrite(PIN_STATUS_LED, HIGH); delay(150);
    digitalWrite(PIN_STATUS_LED, LOW);  delay(150);
  }
}
