// teensy_isr.ino - interrupt handlers in a sketch on Teensy 4.x (ABI 1.8):
// IntervalTimer, attachInterrupt, attachInterruptVector() from imxrt.h, a DMA
// channel, noInterrupts(), elapsedMillis. Prints PASS/FAIL lines and a RESULT.
#ifndef BL_TARGET_TEENSY
#error "This board has no interrupt handlers in the sketch (Teensy 4.x only)"
#endif
#include <imxrt.h>

static int passed, failed;
#define CHECK(name, cond) do { bool ok_ = (cond); Serial.printf("%s  %s\n", ok_ ? "PASS" : "FAIL", name); ok_ ? passed++ : failed++; } while (0)

volatile uint32_t timer_ticks;
volatile uint32_t pin_edges;
volatile uint32_t vector_calls;

void on_timer() { timer_ticks++; }
void on_pin() { pin_edges++; }
void on_vector() { vector_calls++; }

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("\n--- teensy_isr ---");

    // IntervalTimer every 100 us: about 10000 calls a second, in interrupt context.
    IntervalTimer t;
    CHECK("IntervalTimer begins", t.begin(on_timer, 100));
    elapsedMillis since;
    while (since < 1000) {
    }
    t.end();
    uint32_t ticks = timer_ticks;
    Serial.printf("  %lu timer interrupts in 1 s\n", (unsigned long)ticks);
    CHECK("IntervalTimer ran while loop() spun (9900-10100 in 1 s)", ticks >= 9900 && ticks <= 10100);

    // A pin interrupt, triggered by driving the same pin.
    pinMode(2, OUTPUT);
    digitalWrite(2, LOW);
    attachInterrupt(2, on_pin, CHANGE);
    for (int i = 0; i < 10; i++) {
        digitalWrite(2, i & 1 ? LOW : HIGH);
        delayMicroseconds(50);
    }
    detachInterrupt(2);
    Serial.printf("  %lu pin interrupts for 10 edges\n", (unsigned long)pin_edges);
    CHECK("attachInterrupt fires in interrupt context (10 edges)", pin_edges == 10);

    // A raw vector through imxrt.h: GPT2's IRQ, pended by software.
    attachInterruptVector(IRQ_GPT2, on_vector);
    NVIC_ENABLE_IRQ(IRQ_GPT2);
    NVIC_SET_PENDING(IRQ_GPT2);
    delayMicroseconds(10);
    NVIC_SET_PENDING(IRQ_GPT2);
    delayMicroseconds(10);
    NVIC_DISABLE_IRQ(IRQ_GPT2);
    CHECK("attachInterruptVector + NVIC from imxrt.h (2 calls)", vector_calls == 2);

    // noInterrupts() really masks: a pended IRQ waits until interrupts().
    vector_calls = 0;
    NVIC_ENABLE_IRQ(IRQ_GPT2);
    noInterrupts();
    NVIC_SET_PENDING(IRQ_GPT2);
    delayMicroseconds(10);
    uint32_t during = vector_calls;
    interrupts();
    delayMicroseconds(10);
    NVIC_DISABLE_IRQ(IRQ_GPT2);
    CHECK("noInterrupts() holds an interrupt until interrupts()", during == 0 && vector_calls == 1);

    int ch = bl_dma_channel_alloc();
    Serial.printf("  DMA channel %d\n", ch);
    CHECK("DMA channel reserved", ch >= 0 && ch < 32);
    bl_dma_channel_free(ch);

    CHECK("cycle counter runs (ARM_DWT_CYCCNT)", ARM_DWT_CYCCNT != ARM_DWT_CYCCNT);
}

void loop()
{
    Serial.printf("RESULT: %d passed, %d failed, 0 skipped\n", passed, failed);
    delay(2000);
}
