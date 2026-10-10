// Full C++ system simulation of the Voltage Protector (no hardware needed).
// Models: 50 Hz AC sensors -> 12-bit ADC at 10 kHz -> 20 ms sliding RMS -> Protector (protector_core.h)
//         -> relay (10 ms mechanical delay) -> LCD text, buttons, serial commands, CSV log.
// Build: g++ -std=c++11 -O2 -I../../src/voltage_protector sim_main.cpp -o sim
// Run  : ./sim                    (built-in demo scenario)
//        ./sim script.txt         (your own script)
//        ./sim -                  (interactive, commands from stdin)
// Script commands (one per line):  v <Vrms> | i <Arms> | run <ms> | cmd <serial command> | btn reset | lcd
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <string>
#include <sstream>
#include <fstream>
#include <iostream>
#include "protector_core.h"

static const float V_FS = 300.0f, I_FS = 20.0f;      // same scaling as firmware: full-swing sine = FS RMS
static const int   FS_HZ = 10000, WIN = 200;          // 10 kHz sampling, 200 samples = one 50 Hz cycle
static const int   RELAY_MS = 10;                     // contact opening time of a typical relay

struct Window {                                        // sliding RMS over the last WIN samples
  uint16_t buf[WIN]; int idx = 0; Window() { for (int k = 0; k < WIN; k++) buf[k] = 2048; }
  void push(uint16_t s) { buf[idx] = s; idx = (idx + 1) % WIN; }
  float rms(float fullScale) const {
    double m = 0, q = 0; for (int k = 0; k < WIN; k++) m += buf[k]; m /= WIN;
    for (int k = 0; k < WIN; k++) { double d = buf[k] - m; q += d * d; }
    return (float)(sqrt(q / WIN) / (2048.0 / 1.41421356) * fullScale);
  }
};

static uint16_t adc(double rms, double fs, double phase) {   // sine -> 12-bit ADC counts with clipping + 1 LSB noise
  double c = 2048 + rms / fs * 2048.0 * sin(phase) + (rand() % 3 - 1);
  return (uint16_t)fmin(4095, fmax(0, c));
}

static Protector prot;
static Window wv, wi;
static float vTrue = 230, iTrue = 5, vMeas = 0, iMeas = 0;
static uint32_t tMs = 0;
static bool relayContacts = true;                 // physical contact state (lags the decision by RELAY_MS)
static int relayDelayLeft = 0;
static bool lastCmd = true;                       // last relay command sent to the coil
static FILE* csv;

static void lcd() {
  char l1[40], l2[40];
  snprintf(l1, sizeof l1, "V%5.1f I%4.1f %s", vMeas, iMeas, prot.relayOn ? "ON " : "OFF");
  if (prot.state == PS_TRIPPED) snprintf(l2, sizeof l2, "TRIP %s%s", (prot.fault & FAULT_OV) ? "OV " : "", (prot.fault & FAULT_OC) ? "OC" : "");
  else snprintf(l2, sizeof l2, "NORMAL");
  printf("   +----------------+\n   |%-16.16s|\n   |%-16.16s|\n   +----------------+\n", l1, l2);
}

static void step() {                               // advance 1 ms
  for (int k = 0; k < FS_HZ / 1000; k++) {         // 10 ADC samples per ms
    double ph = 2 * M_PI * 50.0 * (tMs * 1e-3 + k * 1e-4);
    double vi = relayContacts ? vTrue : vTrue;     // voltage sensor taps the grid (upstream of relay)
    double ii = relayContacts ? iTrue : 0.0;       // load current is zero once contacts are open
    wv.push(adc(vi, V_FS, ph)); wi.push(adc(ii, I_FS, ph));
  }
  if (tMs % 5 == 0) {                              // control loop every 5 ms
    vMeas = wv.rms(V_FS); iMeas = wi.rms(I_FS);
    prot.update(vMeas, iMeas, tMs);               // exactly what the firmware does (current reads ~0 once open)
    if (prot.relayOn != lastCmd) {
      lastCmd = prot.relayOn; relayDelayLeft = RELAY_MS;
      printf("[%7.3f s] EVENT %s fault=%s V=%.1f I=%.2f decision-latency=%lu ms (+%d ms relay)\n", tMs / 1000.0,
             prot.relayOn ? "RECLOSE" : "TRIP", (prot.fault & FAULT_OV) ? "OV" : (prot.fault & FAULT_OC) ? "OC" : "-",
             vMeas, iMeas, (unsigned long)prot.lastTripLatencyMs, RELAY_MS);
    }
  }
  if (prot.relayOn != lastCmd) { lastCmd = prot.relayOn; relayDelayLeft = RELAY_MS; }   // e.g. RESET button pressed
  if (relayDelayLeft > 0 && --relayDelayLeft == 0) {
    relayContacts = prot.relayOn;
    printf("[%7.3f s] relay contacts %s\n", (tMs + 1) / 1000.0, relayContacts ? "CLOSED" : "OPEN");
  }
  if (tMs % 10 == 0) fprintf(csv, "%u,%.1f,%.2f,%.1f,%.2f,%d,%d\n", tMs, vTrue, iTrue, vMeas, iMeas, relayContacts, prot.fault);
  tMs++;
}

static void command(const std::string& line) {
  std::istringstream ss(line); std::string c; ss >> c;
  if (c.empty() || c[0] == '#') return;
  if (c == "v") { ss >> vTrue; printf("[%7.3f s] grid voltage -> %.1f V\n", tMs / 1000.0, vTrue); }
  else if (c == "i") { ss >> iTrue; printf("[%7.3f s] load current -> %.2f A\n", tMs / 1000.0, iTrue); }
  else if (c == "run") { int ms; ss >> ms; for (int k = 0; k < ms; k++) step(); }
  else if (c == "lcd") lcd();
  else if (c == "btn") { std::string b; ss >> b; if (b == "reset") printf("[%7.3f s] RESET button -> %s\n", tMs / 1000.0, prot.requestReset(vMeas, iMeas) ? "accepted" : "refused (fault present)"); }
  else if (c == "cmd") {
    std::string rest; std::getline(ss, rest); char out[200];
    prot.handleCommand(rest.c_str() + (rest.size() ? 1 : 0), out, sizeof out, vMeas, iMeas);
    printf("[%7.3f s] PC> %s\n            MCU> %s\n", tMs / 1000.0, rest.c_str() + (rest.size() ? 1 : 0), out);
  } else printf("unknown script command: %s\n", line.c_str());
}

static const char* DEMO =
"# normal operation\nv 230\ni 5\nrun 500\nlcd\n"
"# 10 ms surge (glitch) - must be ignored\nv 265\nrun 10\nv 230\nrun 300\n"
"# sustained over-voltage surge\nv 265\nrun 300\nlcd\n"
"# reset refused while surge persists, then accepted when supply is normal\nbtn reset\nv 230\nrun 200\nbtn reset\nrun 200\n"
"# over-current overload; operator resets while overload persists -> trips again; then overload removed\ni 13\nrun 300\nlcd\nbtn reset\nrun 300\ni 5\nbtn reset\nrun 300\nlcd\n"
"# short circuit (instant trip)\ni 18\nrun 100\ni 5\nrun 100\nbtn reset\n"
"# change settings from the PC, then verify the new limit is used\ncmd SET OV 240\ncmd SET OC 8\ncmd GET\nv 245\nrun 200\nlcd\ncmd SET OV 999\ncmd HELLO\n"
"v 230\nrun 200\nbtn reset\nrun 100\ncmd SET AUTO 1\ncmd SET RECOVER 1000\nv 260\nrun 200\nv 230\nrun 1500\nlcd\ncmd GET\n";

int main(int argc, char** argv) {
  csv = fopen("sim_log.csv", "w");
  fprintf(csv, "t_ms,v_true,i_true,v_meas,i_meas,relay_closed,fault\n");
  std::string line;
  if (argc > 1 && std::string(argv[1]) == "-") { while (std::getline(std::cin, line)) command(line); }
  else if (argc > 1) { std::ifstream f(argv[1]); while (std::getline(f, line)) command(line); }
  else { std::istringstream d(DEMO); while (std::getline(d, line)) command(line); }
  fclose(csv);
  printf("\nSimulated %.2f s, %lu trip(s). CSV: sim_log.csv\n", tMs / 1000.0, (unsigned long)prot.tripCount);
  return 0;
}
