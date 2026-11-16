// 01_interval_timer.ino - Teensy's IntervalTimer: a function called every
// 100 microseconds from a hardware timer interrupt, while loop() reports.
// Teensy 4.x only. Interrupt handlers run in interrupt context: keep them short,
// and make the variables they share with loop() volatile.
#include <IntervalTimer.h>

IntervalTimer ticker;
volatile uint32_t ticks;

void tick()
{
    ticks++;
    if (ticks % 5000 == 0) {
        digitalToggleFast(LED_BUILTIN);     // blinks at 1 Hz
    }
}

elapsedMillis sinceReport;

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    ticker.begin(tick, 100);    // every 100 us
}

void loop()
{
    if (sinceReport >= 1000) {
        sinceReport = 0;
        noInterrupts();         // read the shared counter in one piece
        uint32_t n = ticks;
        interrupts();
        Serial.printf("%lu timer interrupts so far\n", (unsigned long)n);
    }
}
