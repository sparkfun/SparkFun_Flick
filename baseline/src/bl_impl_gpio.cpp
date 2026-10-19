/* API table: GPIO, implemented on Arduino's GPIO layer. */
#include <Arduino.h>
#include "gen/bl_impl.h"
#include "bl_pins.h"

static_assert(BL_INPUT == INPUT && BL_OUTPUT == OUTPUT && BL_INPUT_PULLUP == INPUT_PULLUP &&
              BL_INPUT_PULLDOWN == INPUT_PULLDOWN && BL_OUTPUT_OPEN_DRAIN == OUTPUT_OPEN_DRAIN,
              "BL_* pin modes must equal Arduino's");

extern "C" int bl_impl_gpio_mode(int pin, int mode)
{
    if (!bl_pin_usable(pin)) {
        return BL_ERR_ARG;
    }
    switch (mode) {
    case BL_INPUT:
    case BL_INPUT_PULLUP:
    case BL_INPUT_PULLDOWN:
        break;
    case BL_OUTPUT:
    case BL_OUTPUT_OPEN_DRAIN:
        if (!GPIO_IS_VALID_OUTPUT_GPIO(pin)) {
            return BL_ERR_ARG;
        }
        break;
    default:
        return BL_ERR_ARG;
    }
    pinMode(pin, mode);
    return BL_OK;
}

extern "C" int bl_impl_gpio_write(int pin, int level)
{
    if (!bl_pin_usable(pin)) {
        return BL_ERR_ARG;
    }
    digitalWrite(pin, level ? HIGH : LOW);
    return BL_OK;
}

extern "C" int bl_impl_gpio_read(int pin)
{
    if (!bl_pin_usable(pin)) {
        return BL_ERR_ARG;
    }
    return digitalRead(pin) ? BL_HIGH : BL_LOW;
}

/* ---- Interrupts ----
 * The real ISR lives here, in IRAM, and only queues the pin number. The SDK's
 * callback pump calls the sketch's handler on the sketch task, so sketch code
 * never runs in interrupt context (where flash code may be unreachable). */

#define IRQ_QUEUE_LEN 64

static_assert(BL_RISING == RISING && BL_FALLING == FALLING && BL_CHANGE == CHANGE &&
              BL_ONLOW == ONLOW && BL_ONHIGH == ONHIGH, "BL_* interrupt modes must equal Arduino's");

static QueueHandle_t s_irq_queue;

static void IRAM_ATTR pin_isr(void *arg)
{
    uint8_t pin = (uint8_t)(uintptr_t)arg;
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_irq_queue, &pin, &woken);   // drops events if the sketch falls behind
    if (woken) {
        portYIELD_FROM_ISR();
    }
}

extern "C" int bl_impl_gpio_attach_interrupt(int pin, int mode)
{
    if (!bl_pin_usable(pin) || mode < BL_RISING || mode > BL_ONHIGH) {
        return BL_ERR_ARG;
    }
    if (!s_irq_queue) {
        s_irq_queue = xQueueCreate(IRQ_QUEUE_LEN, sizeof(uint8_t));
        if (!s_irq_queue) {
            return BL_ERR_NO_MEM;
        }
    }
    attachInterruptArg(pin, pin_isr, (void *)(uintptr_t)pin, mode);
    return BL_OK;
}

extern "C" int bl_impl_gpio_detach_interrupt(int pin)
{
    if (!bl_pin_usable(pin)) {
        return BL_ERR_ARG;
    }
    detachInterrupt(pin);
    return BL_OK;
}

extern "C" int bl_impl_gpio_interrupt_take(void)
{
    uint8_t pin;
    return (s_irq_queue && xQueueReceive(s_irq_queue, &pin, 0) == pdTRUE) ? pin : -1;
}
