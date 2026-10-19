// littlefs_files.ino - write, append, read and list files with LittleFS.
// Files live on the "userfs" partition and survive sketch uploads.
#include <FS.h>
#include <LittleFS.h>

void listDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
    Serial.printf("Listing %s\n", dirname);
    File root = fs.open(dirname);
    if (!root || !root.isDirectory()) {
        Serial.println("  not a directory");
        return;
    }
    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            Serial.printf("  DIR  %s\n", file.name());
            if (levels) {
                listDir(fs, file.path(), levels - 1);
            }
        } else {
            Serial.printf("  FILE %s  %u bytes\n", file.name(), (unsigned)file.size());
        }
        file = root.openNextFile();
    }
}

void setup()
{
    Serial.begin(115200);
    if (!LittleFS.begin(true)) {     // true: format the partition the first time
        Serial.println("LittleFS mount failed");
        return;
    }
    Serial.printf("LittleFS: %u of %u bytes used\n", (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());

    LittleFS.mkdir("/logs");
    File f = LittleFS.open("/logs/boot.txt", FILE_APPEND);
    f.printf("boot at %lu ms\n", (unsigned long)millis());
    f.close();

    f = LittleFS.open("/hello.txt", FILE_WRITE);
    f.println("Hello from Baseline Sketcher");
    f.close();

    f = LittleFS.open("/hello.txt");
    Serial.print("/hello.txt says: ");
    while (f.available()) {
        Serial.write(f.read());
    }
    f.close();

    listDir(LittleFS, "/", 1);
}

void loop()
{
    delay(1000);
}
