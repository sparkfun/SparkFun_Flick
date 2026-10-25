// teensy_audio.ino - the Teensy Audio library through Baseline (Teensy 4.x, ABI 1.8).
// No audio hardware needed: a 1 kHz sine goes into peak, RMS and FFT analyzers
// (the I2S output keeps time even with nothing on its pins), and to USB audio.
// The declarations are written the way the Audio System Design Tool exports them.
#ifndef BL_TARGET_TEENSY
#error "This board has no Teensy Audio library (Teensy 4.x only)"
#endif
#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>

// GUItool: begin automatically generated code
AudioSynthWaveformSine   sine1;
AudioSynthWaveform       wave1;
AudioMixer4              mixer1;
AudioEffectEnvelope      env1;
AudioAnalyzePeak         peak1;
AudioAnalyzeRMS          rms1;
AudioAnalyzeFFT256       fft1;
AudioAnalyzePeak         peak2;
AudioOutputI2S           i2s1;
AudioOutputUSB           usb1;
AudioConnection          patchCord1(sine1, 0, mixer1, 0);
AudioConnection          patchCord2(wave1, 0, mixer1, 1);
AudioConnection          patchCord3(mixer1, peak1);
AudioConnection          patchCord4(mixer1, rms1);
AudioConnection          patchCord5(mixer1, fft1);
AudioConnection          patchCord6(mixer1, 0, i2s1, 0);
AudioConnection          patchCord7(mixer1, 0, usb1, 0);
AudioConnection          patchCord8(mixer1, env1);
AudioConnection          patchCord9(env1, peak2);
AudioControlSGTL5000     sgtl5000_1;
// GUItool: end automatically generated code

static int passed, failed;
#define CHECK(name, cond) do { bool ok_ = (cond); Serial.printf("%s  %s\n", ok_ ? "PASS" : "FAIL", name); ok_ ? passed++ : failed++; } while (0)

static float wait_read(AudioAnalyzePeak &p)
{
    for (int i = 0; i < 200 && !p.available(); i++) delay(1);
    return p.read();
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("\n--- teensy_audio ---");
    CHECK("objects created (handles >= 0)", sine1.bl_handle() >= 0 && i2s1.bl_handle() >= 0 && sgtl5000_1.bl_handle() >= 0);

    AudioMemory(20);
    bool shield = sgtl5000_1.enable();
    Serial.printf("  audio shield (SGTL5000): %s\n", shield ? "found" : "not found");
    sgtl5000_1.volume(0.5);

    mixer1.gain(0, 1.0);
    mixer1.gain(1, 0.0);
    sine1.frequency(1000);
    sine1.amplitude(0.5);
    delay(300);

    float pk = wait_read(peak1);
    Serial.printf("  peak %.3f\n", pk);
    CHECK("sine amplitude 0.5 -> peak 0.48-0.52", pk > 0.48f && pk < 0.52f);

    rms1.read();                    // drop the average that still holds the silence before the sine
    delay(50);
    for (int i = 0; i < 200 && !rms1.available(); i++) delay(1);
    float rms = rms1.read();
    Serial.printf("  rms %.3f\n", rms);
    CHECK("rms of a 0.5 sine 0.33-0.37", rms > 0.33f && rms < 0.37f);

    for (int i = 0; i < 500 && !fft1.available(); i++) delay(1);
    int best = 0;
    float bestv = 0;
    for (int b = 1; b < 128; b++) {
        float v = fft1.read(b);
        if (v > bestv) { bestv = v; best = b; }
    }
    Serial.printf("  strongest FFT bin %d (%.0f Hz)\n", best, best * AUDIO_SAMPLE_RATE_EXACT / 256);
    CHECK("FFT peak at 1 kHz (bin 5 or 6)", best == 5 || best == 6);

    // Swap to a square wave on channel 1 at run time.
    AudioNoInterrupts();
    mixer1.gain(0, 0.0);
    mixer1.gain(1, 1.0);
    wave1.begin(0.25, 440, WAVEFORM_SQUARE);
    AudioInterrupts();
    delay(100);
    peak1.read();
    delay(100);
    pk = wait_read(peak1);
    Serial.printf("  square peak %.3f\n", pk);
    CHECK("square wave 0.25 through mixer channel 1", pk > 0.23f && pk < 0.27f);

    // Envelope: an idle envelope sends no audio at all; after noteOn() it passes
    // the 0.25 square at its default sustain level, 0.5.
    delay(50);
    peak2.read();
    delay(50);
    bool idle_silent = !peak2.available();
    env1.attack(5);
    env1.noteOn();
    delay(100);
    peak2.read();
    delay(50);
    float after = wait_read(peak2);
    Serial.printf("  envelope idle: %s, after noteOn %.3f\n", idle_silent ? "no audio" : "audio", after);
    CHECK("envelope opens on noteOn() (0.25 x sustain 0.5)", idle_silent && after > 0.11f && after < 0.14f);

    float cpu = AudioProcessorUsageMax();
    int mem = AudioMemoryUsageMax();
    Serial.printf("  audio CPU max %.2f%%, blocks max %d of 20\n", cpu, mem);
    CHECK("AudioProcessorUsageMax() and AudioMemoryUsageMax() report", cpu > 0 && mem > 0 && mem <= 20);
    CHECK("per-object processorUsageMax()", fft1.processorUsageMax() > 0);
}

void loop()
{
    Serial.printf("RESULT: %d passed, %d failed, 0 skipped\n", passed, failed);
    delay(2000);
}
