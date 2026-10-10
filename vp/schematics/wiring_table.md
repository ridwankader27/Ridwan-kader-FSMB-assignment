# Wiring (simulation / low-voltage prototype)  -  matches `project_files/wokwi/diagram.json`

| ESP32 pin | Connected to | Function |
|---|---|---|
| GPIO34 (ADC1) | Potentiometer wiper "V" (0-3.3 V) | stand-in for ZMPT101B output; 0-3.3 V = 0-300 V RMS |
| GPIO35 (ADC1) | Potentiometer wiper "I" (0-3.3 V) | stand-in for ACS712 output; 0-3.3 V = 0-20 A RMS |
| GPIO26 | Relay module IN (active-LOW) | LOW = load connected |
| GPIO27 / 25 / 33 | SELECT / UP / RESET push buttons to GND | local settings (internal pull-ups) |
| GPIO21 / 22 | LCD1602 I2C backpack (0x27) SDA / SCL | local display |
| USB (UART0) | PC | serial commands + telemetry, 115200 baud |

Relay contacts switch a 3.3 V LED + 330 R "load" in simulation. **Nothing here touches mains.**

## Real product (not built - mains work needs certified isolation and enclosure)
Relay contacts in series with the live conductor, ZMPT101B on the supply tap, ACS712 / shunt in series with the load,
varistor + fuse upstream, isolated AC-DC supply for the electronics. See `block_diagram.png`.
