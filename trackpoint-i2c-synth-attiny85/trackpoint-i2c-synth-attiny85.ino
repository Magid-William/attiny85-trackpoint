/*
  Exp44 — ATtiny85 synthetic rectangle (no trackpoint).

  Generates a smooth rectangular cursor path so the I2C slave -> nice!nano ->
  ZMK -> pointing path can be validated in isolation. The PS/2 trackpoint is
  not touched; there is no PS2Trackpoint here at all.

  Path (clockwise, y positive = down per PS/2 convention):
      R (+1,0) -> U (0,-1) -> L (-1,0) -> D (0,+1)

  Accumulates one delta step every STEP_INTERVAL_MS (10 ms) and serves the
  burst register destructively (PMW3610 semantics) so the 10ms-poll driver
  sees a continuous stream of small deltas instead of stale repeats.

  I2C slave 0x42:
      any read -> [acc_x, acc_y] served destructively (register byte ignored —
      the nRF TWIM combined transaction does not reliably deliver it to the
      ATtiny85 USI slave, so serving unconditionally sidesteps that).
*/

#define SLEEP_ENABLED 0

#include <Wire.h>
#include <avr/power.h>

#define I2C_ADDR         0x42
#define BURST_ADDR       0x12
#define DEBUG_ADDR       0x03

#define STEP_INTERVAL_MS 10
#define LEG_LEN          50

/*
  ATtiny85 (tinyX5 core) pin map:
    0 (PB0) = SDA  (fixed by USI TWI)   physical pin 5
    2 (PB2) = SCL  (fixed by USI TWI)   physical pin 7
    1 (PB1) = MOT                       physical pin 6
*/
#define MOT_PIN          1

static const int8_t LEG_DX[4] = {  1,  0, -1,  0 };
static const int8_t LEG_DY[4] = {  0, -1,  0,  1 };

static volatile uint8_t cur_addr;
static volatile int8_t  acc_x = 0;
static volatile int8_t  acc_y = 0;

static uint8_t  leg   = 0;
static uint16_t step  = 0;

void requestEvent() {
    (void)cur_addr;
    uint8_t buf[2] = { (uint8_t)acc_x, (uint8_t)acc_y };
    acc_x = 0;
    acc_y = 0;
    Wire.write(buf, 2);
}

void receiveEvent(int len) {
    if (len > 0) {
        cur_addr = Wire.read();
    }
}

void setup() {
    clock_prescale_set(clock_div_1); /* 8 MHz (fuse CKDIV8 stays set; software override only) */
    pinMode(MOT_PIN, OUTPUT);
    digitalWrite(MOT_PIN, HIGH);

    Wire.begin(I2C_ADDR);
    Wire.onRequest(requestEvent);
    Wire.onReceive(receiveEvent);
}

void loop() {
    static unsigned long last_ms = 0;
    unsigned long now = millis();
    if (now - last_ms >= STEP_INTERVAL_MS) {
        last_ms = now;
        acc_x += LEG_DX[leg];
        acc_y += LEG_DY[leg];
        if (++step >= LEG_LEN) {
            step = 0;
            if (++leg >= 4) leg = 0;
        }
    }
}
