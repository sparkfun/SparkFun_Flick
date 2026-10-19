// hello_world.ino - Serial output over the USB port, Arduino style.

int count = 0;

void setup()
{
    Serial.begin(115200);
    Serial.println("Hello, world!");
    Serial.printf("%s at %lu MHz, %lu bytes of heap free\n",
                  ESP.getChipModel(), (unsigned long)ESP.getCpuFreqMHz(), (unsigned long)ESP.getFreeHeap());
}

void loop()
{
    Serial.printf("Hello #%d at %lu ms\n", count++, (unsigned long)millis());
    delay(1000);
}
