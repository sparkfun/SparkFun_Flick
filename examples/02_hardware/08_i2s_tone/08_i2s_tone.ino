// 08_i2s_tone.ino - play a 440 Hz tone through an I2S amplifier (e.g. MAX98357A).
// Wire the amplifier's BCLK, LRC (word select) and DIN to the pins below.
#include <ESP_I2S.h>

const int I2S_BCLK = 14;    // these three work on both Thing Plus boards
const int I2S_LRC = 15;
const int I2S_DOUT = 16;
const int SAMPLE_RATE = 16000;

I2SClass i2s;
int16_t samples[160];       // 10 ms of a 440 Hz sine, mono

void setup()
{
    Serial.begin(115200);
    for (int i = 0; i < 160; i++) {
        // sin16() takes a 16-bit angle; 440 Hz at 16 kHz is 440/16000 turns per sample.
        samples[i] = sin16((uint16_t)(i * 65536UL * 440 / SAMPLE_RATE)) / 4;
    }
    i2s.setPins(I2S_BCLK, I2S_LRC, I2S_DOUT);
    if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
        Serial.println("I2S failed to start");
        return;
    }
    Serial.println("Playing 440 Hz");
}

void loop()
{
    i2s.write((const uint8_t *)samples, sizeof(samples));
}
