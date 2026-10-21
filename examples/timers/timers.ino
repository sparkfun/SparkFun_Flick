// timers.ino - Ticker, a hardware timer, and esp_timer side by side.
// All three callbacks run on the sketch task, so they can use Serial.
#include <Ticker.h>
#include <esp_timer.h>

Ticker blinker;
hw_timer_t *counter;
esp_timer_handle_t oneShot;
volatile uint32_t ticks;

void blink()
{
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
}

void onAlarm()
{
    ticks++;
}

void onceLater(void *)
{
    Serial.println("esp_timer: 3 seconds after setup()");
}

void setup()
{
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);
    blinker.attach_ms(250, blink);              // Ticker: blink every 250 ms

    counter = timerBegin(1000000);              // 1 MHz hardware-timer API
    timerAttachInterrupt(counter, onAlarm);
    timerAlarm(counter, 100000, true, 0);       // every 100 ms, forever

    esp_timer_create_args_t args = {};
    args.callback = onceLater;
    esp_timer_create(&args, &oneShot);
    esp_timer_start_once(oneShot, 3000000);
}

void loop()
{
    Serial.printf("hw timer alarms: %u (expect ~10 per second), counter %llu us\n",
                  (unsigned)ticks, timerReadMicros(counter));
    delay(1000);
}
