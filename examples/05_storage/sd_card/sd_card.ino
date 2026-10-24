// sd_card.ino - mount the microSD card, write a file, read it back, list files.
// SD.begin() uses the board's own slot (Thing Plus C: SPI, CS 5; S3 Thing Plus: SDIO).
#include <SD.h>

void listDir(fs::FS &fs, const char *dirname)
{
    File root = fs.open(dirname);
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
        Serial.printf("  %s %-20s %u bytes\n", f.isDirectory() ? "DIR " : "FILE", f.name(), (unsigned)f.size());
    }
}

void setup()
{
    Serial.begin(115200);
    if (!SD.begin()) {
        Serial.println("No SD card (or it isn't FAT formatted)");
        return;
    }
    const char *types[] = {"none", "MMC", "SD", "SDHC", "unknown"};
    Serial.printf("Card: %s, %llu MB, %llu MB used\n", types[SD.cardType()],
                  SD.cardSize() / (1024 * 1024), SD.usedBytes() / (1024 * 1024));

    File f = SD.open("/baseline.txt", FILE_APPEND);
    f.printf("hello at %lu ms\n", (unsigned long)millis());
    f.close();

    f = SD.open("/baseline.txt");
    Serial.println("/baseline.txt:");
    while (f.available()) {
        Serial.write(f.read());
    }
    f.close();

    Serial.println("Files:");
    listDir(SD, "/");
}

void loop()
{
    delay(1000);
}
