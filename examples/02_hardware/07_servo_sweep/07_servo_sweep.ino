// 07_servo_sweep.ino - sweep a hobby servo back and forth.
// Servo signal wire to SERVO_PIN, power from a separate 5 V supply, grounds together.
#include <ESP32Servo.h>

const int SERVO_PIN = 14;
Servo servo;

void setup()
{
    Serial.begin(115200);
    servo.setPeriodHertz(50);
    servo.attach(SERVO_PIN, 500, 2400);
}

void loop()
{
    for (int angle = 0; angle <= 180; angle += 2) {
        servo.write(angle);
        delay(15);
    }
    for (int angle = 180; angle >= 0; angle -= 2) {
        servo.write(angle);
        delay(15);
    }
}
