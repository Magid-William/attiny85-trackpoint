/*
  Exp54 — ATtiny85 PS/2 trackpoint serial reader.

  Reads 3-byte PS/2 motion packets from the trackpoint and prints X/Y on the
  tiny-core soft-serial TX (PB0, pin 5) @9600 — read through the Leonardo
  serial bridge. No I2C slave, no buttons (X/Y only).

  This IC streams on power-up (no reset/enableStreaming needed — Exp45).

  ATtiny85 (tinyX5 core) pin map:
    0 (PB0) = soft-serial TX                 physical pin 5
    3 (PB3) = PS2 CLK                        physical pin 2
    4 (PB4) = PS2 DAT                        physical pin 3
*/

#include <PS2Trackpoint.h>
#include <avr/power.h>

#define PS2_CLK 3
#define PS2_DAT 4

PS2Trackpoint ps2(PS2_CLK, PS2_DAT);

void setup() {
    clock_prescale_set(clock_div_1); /* 8 MHz (fuse CKDIV8 stays set; software override only) */

    Serial.begin(9600);
    Serial.println("--- Exp54: ATtiny85 TrackPoint Serial Reader ---");

    ps2.begin();
    ps2.setReadTimeout(2000);
}

void loop() {
    int8_t x, y;
    uint8_t buttons;

    if (ps2.readPacket(x, y, buttons)) {
        Serial.print("X:");
        Serial.print(x);
        Serial.print(" Y:");
        Serial.println(y);
    }
}
