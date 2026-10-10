// Host-side test of src/voltage_protector/protector_core.h (same code the ESP32 runs).
// Build: g++ -std=c++11 -I../../src/voltage_protector test_protector.cpp -o test_protector
// Run:   ./test_protector > ../../test_evidence/host_test_log.txt
#include <cstdio>
#include "protector_core.h"

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { checks++; if (cond) printf("  PASS  %s\n", msg); else { failures++; printf("  FAIL  %s\n", msg); } } while (0)

// Step the protector at 1 ms per tick holding (v,i) constant for `ms` milliseconds.
static uint32_t T = 0;
static void run(Protector& p, float v, float i, int ms) { for (int k = 0; k < ms; k++) p.update(v, i, T++); }

int main() {
  char out[200];

  printf("[T1] Normal operation: 230 V / 5 A for 1 s -> relay stays ON\n");
  { Protector p; run(p, 230, 5, 1000);
    CHECK(p.relayOn && p.state == PS_NORMAL, "relay ON, no trip"); }

  printf("[T2] Sustained surge 265 V (limit 250 V, delay 20 ms) -> trips after the delay\n");
  { Protector p; run(p, 230, 5, 100); uint32_t t0 = T; run(p, 265, 5, 100);
    CHECK(!p.relayOn && (p.fault & FAULT_OV), "relay OFF, fault=OV");
    printf("        measured trip latency = %lu ms (set delay 20 ms)\n", (unsigned long)p.lastTripLatencyMs);
    CHECK(p.lastTripLatencyMs >= 20 && p.lastTripLatencyMs <= 21, "latency equals configured delay (+/-1 ms)");
    (void)t0; }

  printf("[T3] 10 ms glitch at 265 V (shorter than 20 ms delay) -> ignored\n");
  { Protector p; run(p, 230, 5, 100); run(p, 265, 5, 10); run(p, 230, 5, 100);
    CHECK(p.relayOn && p.tripCount == 0, "no nuisance trip"); }

  printf("[T4] Severe surge 300 V (>115%% of limit) -> immediate trip (0 ms)\n");
  { Protector p; run(p, 230, 5, 50); run(p, 300, 5, 1);
    CHECK(!p.relayOn && p.lastTripLatencyMs == 0, "tripped on first sample"); }

  printf("[T5] Over-current 12 A (limit 10 A) -> trips with fault=OC\n");
  { Protector p; run(p, 230, 5, 50); run(p, 230, 12, 100);
    CHECK(!p.relayOn && p.fault == FAULT_OC, "relay OFF, fault=OC"); }

  printf("[T6] Short-circuit 18 A (>150%% of limit) -> immediate trip\n");
  { Protector p; run(p, 230, 5, 50); run(p, 230, 18, 1);
    CHECK(!p.relayOn && p.lastTripLatencyMs == 0, "tripped on first sample"); }

  printf("[T7] Latched: relay stays OFF after the fault clears, reset refused while fault persists\n");
  { Protector p; run(p, 265, 5, 100);
    run(p, 230, 5, 1000);
    CHECK(!p.relayOn, "still OFF after voltage returns to normal (latched)");
    CHECK(!p.requestReset(265, 5), "RESET refused while voltage still high");
    CHECK(p.requestReset(230, 5) && p.relayOn, "RESET accepted once safe -> relay ON"); }

  printf("[T8] Hysteresis: 248 V is below the 250 V limit but not 'safe' (<242.5 V) -> reset refused\n");
  { Protector p; run(p, 265, 5, 100);
    CHECK(!p.requestReset(248, 5), "reset refused at 248 V");
    CHECK(p.requestReset(240, 5), "reset accepted at 240 V"); }

  printf("[T9] Auto-recover enabled (3 s): re-closes only after values stay safe for 3 s\n");
  { Protector p; p.s.autoRecover = true; p.s.recoverMs = 3000; run(p, 265, 5, 100);
    run(p, 230, 5, 2900); CHECK(!p.relayOn, "still OFF at 2.9 s");
    run(p, 230, 5, 200);  CHECK(p.relayOn, "re-closed after 3 s");
    Protector q; q.s.autoRecover = true; q.s.recoverMs = 3000; run(q, 265, 5, 100);
    run(q, 230, 5, 2000); run(q, 265, 5, 10); run(q, 230, 5, 2000);
    CHECK(!q.relayOn, "recover timer restarts when the fault comes back"); }

  printf("[T10] PC settings over serial: GET / SET / range check / unknown command\n");
  { Protector p;
    bool ch = p.handleCommand("SET OV 240", out, sizeof out, 230, 5); printf("        > SET OV 240 -> %s\n", out);
    CHECK(ch && p.s.ovV == 240, "SET OV 240 applied");
    ch = p.handleCommand("set oc 8.5", out, sizeof out, 230, 5); printf("        > set oc 8.5 -> %s\n", out);
    CHECK(ch && p.s.ocA == 8.5f, "lower-case command accepted, OC=8.5");
    ch = p.handleCommand("SET OV 999", out, sizeof out, 230, 5); printf("        > SET OV 999 -> %s\n", out);
    CHECK(!ch && p.s.ovV == 240, "out-of-range value rejected, setting unchanged");
    p.handleCommand("GET", out, sizeof out, 230, 5); printf("        > GET -> %s\n", out);
    CHECK(strstr(out, "OV=240.0") && strstr(out, "OC=8.50"), "GET reports new settings");
    p.handleCommand("FOO", out, sizeof out, 230, 5); printf("        > FOO -> %s\n", out);
    CHECK(strncmp(out, "ERR", 3) == 0, "unknown command -> ERR");
    run(p, 245, 5, 100);
    CHECK(!p.relayOn, "new 240 V limit is actually used (245 V trips)"); }

  printf("\nSUMMARY: %d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
