# Microcontroller-based Voltage Protector (ESP32)

Protects an appliance from over-voltage surges and over-current by opening a relay, reports to a PC over USB serial,
and can be configured from the device (LCD + 3 buttons) or from a computer (serial CLI).
Architecture follows the ESP32 + ZMPT101B + ACS712 + relay design in the course project deck; the firmware and tests here are original.

> **Safety:** low-voltage stand-ins only (potentiometers emulate the sensors). Nothing here is connected to mains.

## Layout
| Path | Content |
|---|---|
| `src/voltage_protector/` | ESP32 firmware (`.ino`) + `protector_core.h` (hardware-independent protection logic and command parser) |
| `src/pc_tool/vprot_cli.py` | PC tool: `monitor`, `get`, `set`, `reset` over serial |
| `design_document.pdf` | one-page report |
| `schematics/` | `block_diagram.png` (real product), `wiring_table.md` (simulation wiring) |
| `test_evidence/host_test_log.txt` | output of the host-side test run (21 checks) |
| `project_files/wokwi/` | Wokwi simulation (`diagram.json`, `libraries.txt`) |
| `project_files/host_test/` | C++ unit test that exercises `protector_core.h` |
| `project_files/cpp_simulation/sim_main.cpp` | full C++ system simulation (AC sensors, ADC, RMS, relay delay, buttons, serial) |

## How to run
**Host test (no hardware needed)**
```
cd project_files/host_test
g++ -std=c++11 -I../../src/voltage_protector test_protector.cpp -o test_protector && ./test_protector
```
**Simulation:** create a new ESP32 project on wokwi.com, paste `project_files/wokwi/sketch.ino` and `protector_core.h`
(as a second file), `diagram.json` and `libraries.txt`. Turn the V pot above 250/300 of full scale (about 0.83 x full travel) to trip OV; I pot above 0.5 x travel (10 A of 20 A) to trip OC.
**Real board:** Arduino IDE, board "ESP32 Dev Module", library `LiquidCrystal I2C`. Open `voltage_protector.ino`.

## Serial commands (115200 baud) / PC tool
`GET` | `SET OV|OC|DELAY|AUTO|RECOVER <n>` | `RESET` | `STREAM 0/1`  - e.g. `python src/pc_tool/vprot_cli.py COM5 set OV 245`

## Local settings
SELECT cycles OV -> OC -> DELAY -> AUTO, UP changes the value (saved to flash), RESET re-closes the relay once the fault has cleared.

## Honest status
Done and tested: protection logic (OV, OC, glitch rejection, immediate trip on severe faults, latch, hysteresis, auto-recover),
command parser with range checks - 21/21 host checks pass (`test_evidence/`).
**Not done / not verified:** the `.ino` has not been compiled or run on an ESP32 and the Wokwi diagram is unsimulated;
real AC RMS path (`SIM_DC_INPUT 0`) and sensor calibration are untested; no real relay-timing capture, so the 20 ms figure is the *configured* delay, not a measured relay opening time (add mechanical relay time, roughly 10 ms); no mains testing, by design.

## C++ system simulation
```
cd project_files/cpp_simulation
g++ -std=c++11 -O2 -I../../src/voltage_protector sim_main.cpp -o sim
./sim            # demo scenario; also writes sim_log.csv      ./sim - = interactive (v 265 / i 12 / run 300 / btn reset / cmd GET)
```
Results are in `test_evidence/cpp_simulation_log.txt`, `.csv` and `cpp_simulation_plot.png`. Observed: a 265 V step trips about 35 ms after onset
(20 ms RMS window fill + 20 ms confirm delay, partly overlapping) plus ~10 ms modelled relay time; a short circuit trips about 15 ms after onset.
Known limit shown by the simulation: after an over-current trip the load current reads ~0, so RESET cannot tell whether the overload is gone - the device re-closes and trips again if it persists.
