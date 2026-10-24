// 04_spi_loopback.ino - send bytes over SPI and check what comes back.
// Connect a jumper wire from the board's MOSI pin to its MISO pin to see the
// bytes echoed; without the wire every byte reads back as 0xFF or 0x00.
#include <SPI.h>

void setup()
{
    SPI.begin();
}

void loop()
{
    uint8_t sent[4] = {0x12, 0x34, 0x56, 0x78};
    uint8_t buf[4];
    memcpy(buf, sent, sizeof(buf));

    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    SPI.transfer(buf, sizeof(buf));
    SPI.endTransaction();

    bool match = memcmp(sent, buf, sizeof(buf)) == 0;
    Serial.printf("sent 12 34 56 78, received %02X %02X %02X %02X  %s\n",
                  buf[0], buf[1], buf[2], buf[3], match ? "(loopback OK)" : "(no loopback wire?)");
    delay(1000);
}
