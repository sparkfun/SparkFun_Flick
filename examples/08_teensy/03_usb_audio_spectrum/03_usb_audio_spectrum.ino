// 03_usb_audio_spectrum.ino - the Teensy Audio library's FFT: play music on the
// computer to the Teensy (Windows: choose "Teensy Audio" as the output device)
// and watch its spectrum as bars in the serial monitor. Teensy 4.x only.
#include <Audio.h>

AudioInputUSB            usbIn;
AudioAnalyzeFFT1024      fft;
AudioOutputI2S           i2s;           // keeps the audio clock running
AudioConnection          patchCord1(usbIn, 0, fft, 0);
AudioConnection          patchCord2(usbIn, 0, i2s, 0);

// Eight bands, roughly an octave each (bins are 43 Hz wide).
const int edges[] = {1, 3, 6, 12, 24, 48, 96, 192, 384};

void setup()
{
    AudioMemory(16);
}

void loop()
{
    if (!fft.available()) {
        return;
    }
    char line[8 * 11 + 1];
    char *p = line;
    for (int b = 0; b < 8; b++) {
        float level = fft.read(edges[b], edges[b + 1] - 1);
        int n = (int)(level * 40);
        if (n > 10) {
            n = 10;
        }
        for (int i = 0; i < 10; i++) {
            *p++ = i < n ? '#' : '.';
        }
        *p++ = ' ';
    }
    *p = '\0';
    Serial.println(line);
    delay(100);
}
