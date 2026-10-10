/*
 * Microcontroller-based Voltage Protector - ESP32 (Arduino core)
 * Measures V and I, opens a relay on over-voltage / over-current, reports to a PC over USB serial,
 * and allows settings changes from the device (3 buttons + 16x2 LCD) or from a PC (serial commands).
 *
 * SAFETY: built/tested with LOW-VOLTAGE STAND-INS ONLY (potentiometers emulate sensor outputs).
 * Do not connect this prototype to mains.
 *
 * Wiring (see schematics/):
 *   GPIO34 <- voltage sensor output (pot in simulation; ZMPT101B in the real product)
 *   GPIO35 <- current sensor output (pot in simulation; ACS712 in the real product)
 *   GPIO26 -> relay IN (active-LOW module: LOW = coil energised = load connected)
 *   GPIO27 <- SELECT button, GPIO25 <- UP button, GPIO33 <- RESET button (to GND, internal pull-ups)
 *   GPIO21/22 = I2C SDA/SCL -> LCD1602 (0x27)
 */
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>
#include <strings.h>
#include "protector_core.h"

#define SIM_DC_INPUT 1        // 1 = sensor pins carry a DC level representing the RMS value (pots)
                              // 0 = real AC sensors: sample ~1 mains cycle, compute RMS about the mid-point
#define PIN_V      34
#define PIN_I      35
#define PIN_RELAY  26
#define PIN_SEL    27
#define PIN_UP     25
#define PIN_RST    33
#define RELAY_ACTIVE_LOW 1

// Scaling: ADC full-scale (3.3 V, 12-bit) -> engineering units. Calibrate on the real sensors.
static const float V_FULL_SCALE = 300.0f;  // 0..300 V RMS
static const float I_FULL_SCALE = 20.0f;   // 0..20 A RMS

LiquidCrystal_I2C lcd(0x27, 16, 2);
Preferences prefs;
Protector prot;
float vRms = 0, iRms = 0;
bool streaming = true;
uint8_t editIdx = 0;                       // 0=OV 1=OC 2=DELAY 3=AUTO

static float readRms(int pin, float fullScale) {
#if SIM_DC_INPUT
  uint32_t acc = 0;
  for (int k = 0; k < 8; k++) acc += analogRead(pin);
  return (acc / 8.0f) / 4095.0f * fullScale;
#else
  const int N = 200;                       // ~1 cycle @50 Hz at ~100 us per sample
  static uint16_t buf[200];
  double mean = 0, sq = 0;
  for (int k = 0; k < N; k++) { buf[k] = analogRead(pin); mean += buf[k]; delayMicroseconds(100); }
  mean /= N;
  for (int k = 0; k < N; k++) { double d = buf[k] - mean; sq += d * d; }
  float counts = sqrt(sq / N);
  return counts / (2048.0f / 1.41421356f) * fullScale;  // full-swing sine = fullScale RMS (calibrate per sensor)
#endif
}

static void setRelay(bool on) {
  digitalWrite(PIN_RELAY, (on ^ RELAY_ACTIVE_LOW) ? HIGH : LOW);
}

static void saveSettings() {
  prefs.putFloat("ov", prot.s.ovV);   prefs.putFloat("oc", prot.s.ocA);
  prefs.putUShort("dly", prot.s.tripDelayMs);
  prefs.putBool("auto", prot.s.autoRecover); prefs.putUShort("rec", prot.s.recoverMs);
}
static void loadSettings() {
  prot.s.ovV = prefs.getFloat("ov", 250); prot.s.ocA = prefs.getFloat("oc", 10);
  prot.s.tripDelayMs = prefs.getUShort("dly", 20);
  prot.s.autoRecover = prefs.getBool("auto", false); prot.s.recoverMs = prefs.getUShort("rec", 5000);
}

static void handleButtons(uint32_t now) {
  static bool lastSel = true, lastUp = true, lastRst = true;
  static uint32_t lastEdge = 0;
  bool sel = digitalRead(PIN_SEL), up = digitalRead(PIN_UP), rst = digitalRead(PIN_RST);
  if (now - lastEdge > 200) {
    if (!sel && lastSel) { editIdx = (editIdx + 1) % 4; lastEdge = now; }
    if (!up && lastUp) {
      switch (editIdx) {
        case 0: prot.s.ovV = (prot.s.ovV >= 290) ? 200 : prot.s.ovV + 10; break;
        case 1: prot.s.ocA = (prot.s.ocA >= 20) ? 2 : prot.s.ocA + 1; break;
        case 2: prot.s.tripDelayMs = (prot.s.tripDelayMs >= 200) ? 0 : prot.s.tripDelayMs + 20; break;
        case 3: prot.s.autoRecover = !prot.s.autoRecover; break;
      }
      saveSettings(); lastEdge = now;
    }
    if (!rst && lastRst) { prot.requestReset(vRms, iRms); lastEdge = now; }
  }
  lastSel = sel; lastUp = up; lastRst = rst;
}

static void drawLcd() {
  char l1[24], l2[24];
  snprintf(l1, sizeof(l1), "V%5.1f I%4.1f %s", vRms, iRms, prot.relayOn ? "ON " : "OFF");
  if (prot.state == PS_TRIPPED)
    snprintf(l2, sizeof(l2), "TRIP %s%s      ", (prot.fault & FAULT_OV) ? "OV " : "", (prot.fault & FAULT_OC) ? "OC" : "");
  else {
    const char* names[] = {"OV", "OC", "DLY", "AUT"};
    float val = editIdx == 0 ? prot.s.ovV : editIdx == 1 ? prot.s.ocA : editIdx == 2 ? prot.s.tripDelayMs : prot.s.autoRecover;
    snprintf(l2, sizeof(l2), "SET %s=%-6.1f   ", names[editIdx], val);
  }
  l1[16] = 0; l2[16] = 0;
  lcd.setCursor(0, 0); lcd.print(l1); lcd.setCursor(0, 1); lcd.print(l2);
}

static void handleSerial(float v, float i) {
  static char line[64]; static uint8_t n = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') { if (n < sizeof(line) - 1) line[n++] = c; continue; }
    line[n] = 0; n = 0;
    char out[160];
    if (!strncasecmp(line, "STREAM", 6)) {
      streaming = (atoi(line + 6) != 0);
      Serial.println(streaming ? "OK STREAM 1" : "OK STREAM 0");
      continue;
    }
    if (prot.handleCommand(line, out, sizeof(out), v, i)) saveSettings();
    Serial.println(out);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_RELAY, OUTPUT); setRelay(true);
  pinMode(PIN_SEL, INPUT_PULLUP); pinMode(PIN_UP, INPUT_PULLUP); pinMode(PIN_RST, INPUT_PULLUP);
  analogReadResolution(12);
  prefs.begin("vprot", false); loadSettings();
  Wire.begin(21, 22); lcd.init(); lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("Voltage Protector");
  delay(800);
  Serial.println("# Voltage Protector ready. Commands: GET | SET OV|OC|DELAY|AUTO|RECOVER <n> | RESET | STREAM 0/1");
}

void loop() {
  static uint32_t lastTel = 0, lastLcd = 0;
  uint32_t now = millis();
  vRms = readRms(PIN_V, V_FULL_SCALE);
  iRms = readRms(PIN_I, I_FULL_SCALE);
  bool wasOn = prot.relayOn;
  prot.update(vRms, iRms, now);
  if (prot.relayOn != wasOn) {
    setRelay(prot.relayOn);                // relay switches right after the decision
    Serial.printf("EVENT %s t=%lu fault=%s latency=%lums V=%.1f I=%.2f\n",
                  prot.relayOn ? "RECLOSE" : "TRIP", now,
                  (prot.fault & FAULT_OV) ? ((prot.fault & FAULT_OC) ? "OV+OC" : "OV") : ((prot.fault & FAULT_OC) ? "OC" : "-"),
                  (unsigned long)prot.lastTripLatencyMs, vRms, iRms);
  }
  handleButtons(now);
  handleSerial(vRms, iRms);
  if (now - lastLcd > 200) { lastLcd = now; drawLcd(); }
  if (streaming && now - lastTel > 500) {
    lastTel = now;
    Serial.printf("DATA,%lu,%.1f,%.2f,%d,%d\n", now, vRms, iRms, prot.relayOn, prot.fault);
  }
}
