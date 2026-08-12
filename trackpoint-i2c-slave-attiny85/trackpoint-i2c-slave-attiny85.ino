#define SLEEP_ENABLED 0
#define SERIAL_LOG 0

#include <Wire.h>
#include <PS2Trackpoint.h>
#include <PowerCurve.h>
#include <avr/power.h>

#define I2C_ADDR     0x42
#define BURST_ADDR   0x12
#define DEBUG_ADDR   0x03
#define SPEED_REG    PowerCurve::REG_SENS

/*
  ATtiny85 (tinyX5 core) pin map:
    0 (PB0) = SDA  (fixed by USI TWI)   physical pin 5
    2 (PB2) = SCL  (fixed by USI TWI)   physical pin 7
    1 (PB1) = MOT                       physical pin 6
    3 (PB3) = PS2 CLK                   physical pin 2
    4 (PB4) = PS2 DAT                   physical pin 3
*/
#define MOT_PIN      1
#define PS2_CLK      3
#define PS2_DAT      4

PS2Trackpoint ps2(PS2_CLK, PS2_DAT);
PowerCurve curve;

static volatile uint8_t cur_addr;
static volatile int16_t acc_x = 0; /* Exp63: accumulated delta since last poll */
static volatile int16_t acc_y = 0;
static unsigned long last_good_ms = 0;
static volatile unsigned long last_busy_ms = 0; /* Exp62: USI idle self-heal */

void requestEvent() {
    /* Exp62 fix: serve burst unconditionally on ANY read. The ATtiny85's
     * bit-banged USI-TWI slave does not reliably capture the register byte of
     * the nRF TWIM combined transaction (see the synth sketch), so gating on
     * cur_addr==BURST_ADDR dropped real motion to {0,0} → ZMK zero_count stale
     * clears → choppy cursor. Driven only ever reads 0x12, so unconditional is
     * safe; keep the 0x03 debug branch first for the shell live reader.
     *
     * Exp63 fix: ACCUMULATE instead of overwrite. Poll rate (10ms) and PS/2
     * packet period (~8.7ms) are rate-mismatched — overwriting dropped a
     * packet whenever two landed in one poll window (the first got clobbered
     * before it was read). Serve the accumulated sum, clamped to int8, and
     * subtract what was served so no remainder is lost (fast-flick > 127). */
    if (cur_addr == DEBUG_ADDR && !(acc_x || acc_y)) {
        uint8_t buf[5] = { ps2.last_status, ps2.last_xraw, ps2.last_yraw,
                           (uint8_t)ps2.read_timeouts, (uint8_t)(ps2.read_timeouts >> 8) };
        Wire.write(buf, 5);
    } else {
        int8_t sx = (acc_x > 127) ? 127 : (acc_x < -128) ? -128 : (int8_t)acc_x;
        int8_t sy = (acc_y > 127) ? 127 : (acc_y < -128) ? -128 : (int8_t)acc_y;
        uint8_t buf[2] = { (uint8_t)sx, (uint8_t)sy };
        acc_x -= sx;
        acc_y -= sy;
        Wire.write(buf, 2);
    }
    last_busy_ms = millis();
}

void receiveEvent(int len) {
    if (len <= 0) {
        return;
    }
    last_busy_ms = millis();
    cur_addr = Wire.read();
    uint8_t b1 = 0, b2 = 0;
    if (len > 1) b1 = Wire.read();
    if (len > 2) b2 = Wire.read();

    /* Exp60 port: ZMK driver writes speed/curve params at init + every
     * link-restore. Store them and let loop() rebuild the LUT. */
    if (cur_addr == SPEED_REG) {
        curve.setSens(b1);
    } else if (cur_addr == PowerCurve::REG_RATE
            || cur_addr == PowerCurve::REG_EXP
            || cur_addr == PowerCurve::REG_START) {
        curve.setParam(cur_addr, (uint16_t)b1 | ((uint16_t)b2 << 8));
    }
}

void setup() {
    clock_prescale_set(clock_div_1); /* 8 MHz (fuse CKDIV8 stays set; software override only) */
    pinMode(MOT_PIN, OUTPUT);
    digitalWrite(MOT_PIN, HIGH);

    Wire.begin(I2C_ADDR);
    Wire.onRequest(requestEvent);
    Wire.onReceive(receiveEvent);

    ps2.begin();
    ps2.setReadTimeout(10000); /* Exp43: bound PS/2 idle wait; Exp58: 2000 (~1.5ms) timed out between packets — 10000 (~7.5ms) covers the inter-packet gap */
    curve.begin();

    last_good_ms = millis();
}

void loop() {
    int8_t x, y;
    uint8_t buttons;

    /* Exp62: USI idle self-heal. When the NiceNano enters deep sleep
     * (CONFIG_ZMK_SLEEP) the TWIM shuts down mid USI byte-count, leaving this
     * slave armed for clock edges that never arrive → on resume the master's
     * polls get no ACK and the link stays dead until a NiceNano reset. Re-arm
     * the USI slave when the bus has been silent >1s (safe: no live
     * transaction is in flight). Wire.begin() clears the ISR callbacks, so
     * re-register them. */
    if (millis() - last_busy_ms > 1000) {
        Wire.end();
        Wire.begin(I2C_ADDR);
        Wire.onRequest(requestEvent);
        Wire.onReceive(receiveEvent);
        last_busy_ms = millis();
    }

    /* readPacket syncs itself to packet gaps — no fixed interval needed */
    if (ps2.readPacket(x, y, buttons)) {
        int8_t cx, cy;
        curve.apply(x, y, cx, cy);
        acc_x += cx;
        acc_y += cy;
        last_good_ms = millis();
    }

    curve.update(); /* rebuild LUT if a param landed since the last loop (no-op otherwise) */

    /* stop feeding the driver stale motion after 100ms without a packet */
    if ((acc_x || acc_y) && (millis() - last_good_ms > 100)) {
        acc_x = 0;
        acc_y = 0;
    }
}
