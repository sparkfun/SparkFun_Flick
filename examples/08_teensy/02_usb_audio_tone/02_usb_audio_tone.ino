// 02_usb_audio_tone.ino - the Teensy Audio library: a tone that sweeps up and
// down, sent to the computer over USB audio (and to an audio shield, if one is
// plugged in). Teensy 4.x only; no extra hardware needed.
// On Windows: Sound settings > Input > "Teensy Audio", then "Listen to this device".
// The declarations are the way PJRC's Audio System Design Tool writes them.
#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>

// GUItool: begin automatically generated code
AudioSynthWaveform       wave;
AudioEffectEnvelope      envelope;
AudioOutputUSB           usb;
AudioOutputI2S           i2s;
AudioConnection          patchCord1(wave, envelope);
AudioConnection          patchCord2(envelope, 0, usb, 0);
AudioConnection          patchCord3(envelope, 0, usb, 1);
AudioConnection          patchCord4(envelope, 0, i2s, 0);
AudioConnection          patchCord5(envelope, 0, i2s, 1);
AudioControlSGTL5000     shield;
// GUItool: end automatically generated code

const float notes[] = {261.63, 293.66, 329.63, 349.23, 392.00, 440.00, 493.88, 523.25};   // C major

void setup()
{
    AudioMemory(12);
    if (shield.enable()) {
        shield.volume(0.4);
        Serial.println("Audio shield found");
    }
    wave.begin(0.3, notes[0], WAVEFORM_BANDLIMIT_SAWTOOTH);
    envelope.attack(10);
    envelope.release(200);
}

void loop()
{
    for (float f : notes) {
        wave.frequency(f);
        envelope.noteOn();
        delay(250);
        envelope.noteOff();
        delay(100);
    }
    Serial.printf("audio CPU %.1f%%, blocks %d\n", AudioProcessorUsageMax(), AudioMemoryUsageMax());
}
