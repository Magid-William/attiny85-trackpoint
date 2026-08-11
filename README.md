# ATtiny85 dev guide — flash + serial debug via Arduino Leonardo

All ATtiny85 work lives in this folder. Two Leonardo sketches make up the
toolchain — the Leonardo is **reflashed between roles** (~7s each way):

| Role | Sketch | Leonardo runs |
|---|---|---|
| Programmer | `arduino-isp/` (env `leonardo`) | flash the ATtiny85 over ISP |
| Serial bridge | `leonardo-serial-reader/` | bridge ATtiny85 9600 soft-serial → USB CDC |

The Leonardo can't be both at once: reflash `arduino-isp` before flashing the
ATtiny85, reflash `leonardo-serial-reader` before reading its serial.

## Wiring — Leonardo 5V ISP → ATtiny85

| ATtiny85 Pin | Function | Leonardo |
|---|---|---|
| 1 | RESET (PB5) | Digital Pin 10 |
| 4 | GND | ICSP Pin 6 (GND) |
| 5 | PB0 / MOSI | ICSP Pin 4 (MOSI) |
| 6 | PB1 / MISO | ICSP Pin 1 (MISO) |
| 7 | PB2 / SCK | ICSP Pin 3 (SCK) |
| 8 | VCC | ICSP Pin 2 (5V) |

All 5V direct — no level shifting needed (the ATtiny85 runs at factory fuses,
1 MHz). ATtiny85 pin 5 doubles as the soft-serial TX used for debug.

### Leonardo ICSP header (top view, dot = pin 1 on the top right)

| ICSP | ICSP | ICSP ● |
| ---- | ---- | ------ |
| RST  | SCK  | MISO   |
| GND  | MOSI | 5V     |

ICSP pin 5 (RESET) is **unused** here — the ATtiny85's RESET comes from
Leonardo **D10**.

## Flash the ATtiny85 (via WSL)

```powershell
# 1. Flash the programmer role to the Leonardo (native USB, avr109)
pio run -e leonardo -t upload            # in attiny85-trackpoint/arduino-isp/

# 2. Attach the Leonardo to WSL (busid 3-1 = Leonardo COM9)
usbipd attach --wsl --busid 3-1
```

```bash
# 3. In Alpine: load driver, confirm the CDC port
modprobe cdc_acm
ls /dev/ttyACM0

# 4. Probe the ATtiny85 (expect signature 0x1e930b = t85)
avrdude -p attiny85 -c avrisp -P /dev/ttyACM0 -b 19200

# 5. Flash a build (write + verify)
avrdude -p attiny85 -c avrisp -P /dev/ttyACM0 -b 19200 \
  -U flash:w:".pio/build/program_via_ArduinoISP/firmware.hex":i
```

## Read the ATtiny85 serial through the Leonardo

No CH340G, no WSL, no rewiring (the ATtiny85's TX is already on ICSP MOSI = D16).

```powershell
# 1. Flash the bridge role to the Leonardo
pio run -t upload                        # in attiny85-trackpoint/leonardo-serial-reader/

# 2. Open the Leonardo's COM port (e.g. COM9) at any baud (USB CDC ignores it)
#    Expect a millis() heartbeat every ~500ms
```

## Build a sketch

Every PlatformIO project builds from this folder with `pio run`:

| Project | What it is |
|---|---|
| `attiny85-serial-test/` | bare `Serial.println(millis())` @9600 (Exp52 golden) |
| `attiny85-blink/` | blink PB0 |
| `attiny85-trackpoint-i2c-slave/` | I2C slave emulating PMW3610 (src = `trackpoint-i2c-slave-attiny85/`) |
| `attiny85-trackpoint-synth/` | synth variant |
| `attiny85-trackpoint-oled/` | OLED diagnostics variant |
| `attiny85-trackpoint-serial/` | PS/2 X/Y → 9600 soft-serial TX on PB0 (src = `trackpoint-serial-attiny85/`), read via Leonardo bridge |

`libraries/` is a self-contained copy (PS2Trackpoint, Wire, Tiny4kOLED,
LowPower) used via `lib_extra_dirs = ../libraries`.

## Gotchas

- **Signature `0xffffff`** = no target responding — almost always the RESET
  jumper (ATtiny85 pin 1 → Leonardo D10) is missing. Check it first.
- **Leonardo busid is `3-1`**, not `1-3`. After any physical re-plug the
  device drops out of WSL — re-run `usbipd attach --wsl --busid 3-1` and
  `modprobe cdc_acm`.
- First probe after attach may sync-fail (`resp=0x15`) — drain the port
  (`timeout 2 cat /dev/ttyACM0`) and retry.
- Reflashing the Leonardo back to programmer role: `pio run -e leonardo -t upload`
  in `arduino-isp/` (~7s).
