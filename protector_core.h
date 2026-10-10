// protector_core.h - hardware-independent protection logic + command parser.
// Used by the ESP32 firmware AND by the host-side test (project_files/host_test),
// so the code that is tested is the code that runs on the device.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

enum PState : uint8_t { PS_NORMAL = 0, PS_TRIPPED = 1 };
enum : uint8_t { FAULT_NONE = 0, FAULT_OV = 1, FAULT_OC = 2 };

struct Settings {
  float    ovV          = 250.0f;  // over-voltage trip level (V RMS)
  float    ocA          = 10.0f;   // over-current trip level (A RMS)
  uint16_t tripDelayMs  = 20;      // fault must persist this long (glitch rejection)
  bool     autoRecover  = false;   // re-close automatically after fault clears
  uint16_t recoverMs    = 5000;    // how long values must stay safe before re-close
};

static const float SEVERE_FACTOR_V = 1.15f; // >115% of OV limit  -> trip with no delay
static const float SEVERE_FACTOR_I = 1.50f; // >150% of OC limit  -> trip with no delay
static const float HYSTERESIS      = 0.03f; // 3% below limit counts as "safe"

class Protector {
 public:
  Settings s;
  PState   state    = PS_NORMAL;
  uint8_t  fault    = FAULT_NONE;
  bool     relayOn  = true;       // true = appliance connected
  uint32_t tripCount = 0;
  uint32_t lastTripAtMs = 0;
  uint32_t lastTripLatencyMs = 0; // time from first over-limit sample to relay open

  void update(float v, float i, uint32_t now) {
    bool ov = v > s.ovV;
    bool oc = i > s.ocA;
    if (state == PS_NORMAL) {
      if (ov || oc) {
        if (!pending_) { pending_ = true; pendingSince_ = now; }
        bool severe = (v > s.ovV * SEVERE_FACTOR_V) || (i > s.ocA * SEVERE_FACTOR_I);
        if (severe || (now - pendingSince_) >= s.tripDelayMs)
          trip_((ov ? FAULT_OV : 0) | (oc ? FAULT_OC : 0), now);
      } else {
        pending_ = false;
      }
    } else {
      bool safe = isSafe_(v, i);
      if (safe) { if (!clearing_) { clearing_ = true; clearSince_ = now; } }
      else clearing_ = false;
      if (s.autoRecover && safe && (now - clearSince_) >= s.recoverMs) close_();
    }
  }

  // Manual reset (button or "RESET" command). Refused while the fault persists.
  bool requestReset(float v, float i) {
    if (state == PS_NORMAL) return true;
    if (!isSafe_(v, i)) return false;
    close_();
    return true;
  }

  // Parse one command line. Writes reply to out. Returns true if settings changed.
  bool handleCommand(const char* line, char* out, size_t n, float v, float i) {
    char cmd[12] = {0}, key[12] = {0};
    float val = 0;
    int k = sscanf(line, "%11s %11s %f", cmd, key, &val);
    upper_(cmd); upper_(key);
    if (k >= 1 && !strcmp(cmd, "GET")) {
      snprintf(out, n, "OK OV=%.1f OC=%.2f DELAY=%u AUTO=%d RECOVER=%u STATE=%s RELAY=%s TRIPS=%lu",
               s.ovV, s.ocA, s.tripDelayMs, s.autoRecover, s.recoverMs,
               state == PS_NORMAL ? "NORMAL" : "TRIPPED", relayOn ? "ON" : "OFF", (unsigned long)tripCount);
      return false;
    }
    if (k >= 1 && !strcmp(cmd, "RESET")) {
      snprintf(out, n, requestReset(v, i) ? "OK RESET" : "ERR FAULT_STILL_PRESENT");
      return false;
    }
    if (k == 3 && !strcmp(cmd, "SET")) {
      if      (!strcmp(key, "OV")      && val >= 150 && val <= 300)    s.ovV = val;
      else if (!strcmp(key, "OC")      && val >= 0.5f && val <= 30)    s.ocA = val;
      else if (!strcmp(key, "DELAY")   && val >= 0 && val <= 1000)    s.tripDelayMs = (uint16_t)val;
      else if (!strcmp(key, "AUTO")    && (val == 0 || val == 1))      s.autoRecover = (val == 1);
      else if (!strcmp(key, "RECOVER") && val >= 500 && val <= 60000)  s.recoverMs = (uint16_t)val;
      else { snprintf(out, n, "ERR BAD_KEY_OR_RANGE"); return false; }
      snprintf(out, n, "OK SET %s=%g", key, val);
      return true;
    }
    snprintf(out, n, "ERR UNKNOWN (GET | SET OV|OC|DELAY|AUTO|RECOVER <n> | RESET | STREAM 0/1)");
    return false;
  }

 private:
  bool pending_ = false, clearing_ = false;
  uint32_t pendingSince_ = 0, clearSince_ = 0;
  bool isSafe_(float v, float i) const {
    return v < s.ovV * (1 - HYSTERESIS) && i < s.ocA * (1 - HYSTERESIS);
  }
  void trip_(uint8_t f, uint32_t now) {
    state = PS_TRIPPED; fault = f; relayOn = false;
    tripCount++; lastTripAtMs = now; lastTripLatencyMs = now - pendingSince_;
    pending_ = false; clearing_ = false;
  }
  void close_() { state = PS_NORMAL; fault = FAULT_NONE; relayOn = true; pending_ = false; clearing_ = false; }
  static void upper_(char* p) { for (; *p; ++p) if (*p >= 'a' && *p <= 'z') *p -= 32; }
};
