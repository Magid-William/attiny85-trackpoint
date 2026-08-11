#include <Arduino.h>
#include <SoftwareSerial.h>

SoftwareSerial targetSerial(16, 13);

void setup() {
    Serial.begin(115200);
    targetSerial.begin(9600);
}

void loop() {
    if (targetSerial.available()) {
        Serial.write(targetSerial.read());
    }
}
