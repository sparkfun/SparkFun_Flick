// 05_can_loopback.ino - CAN bus with FlexCAN_T4, in the controller's own loopback.
// Every frame sent comes straight back, so it runs with nothing attached. On a real
// bus, add a CAN transceiver (e.g. SN65HVD230) on the CAN1 pins (Teensy 4.x: 22 TX,
// 23 RX; MicroMod: the carrier's CAN pins) and remove enableLoopBack().
// FlexCAN_T4 comes with Teensyduino and compiles into the sketch as it is.
#if !defined(BL_PLATFORM_TEENSY)
#error "This board has no FlexCAN controller (CAN with FlexCAN_T4 is for Teensy 4.x)."
#endif
#include <FlexCAN_T4.h>

FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> can1;
uint32_t sent = 0, received = 0;

void setup()
{
    Serial.begin(115200);
    can1.begin();
    can1.setBaudRate(500000);
    can1.enableLoopBack();
    Serial.println("CAN1 at 500 kbit/s, loopback");
}

void loop()
{
    CAN_message_t tx;
    tx.id = 0x123;
    tx.len = 8;
    for (int i = 0; i < 8; i++) {
        tx.buf[i] = (uint8_t)(sent + i);
    }
    if (can1.write(tx)) {
        sent++;
    }
    CAN_message_t rx;
    while (can1.read(rx)) {
        received++;
        if (received % 100 == 1) {
            Serial.printf("got id 0x%03lX, %u bytes: %02X %02X %02X ...  (%lu sent, %lu received)\n",
                          (unsigned long)rx.id, rx.len, rx.buf[0], rx.buf[1], rx.buf[2],
                          (unsigned long)sent, (unsigned long)received);
        }
    }
    delay(10);
}
