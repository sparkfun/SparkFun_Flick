/* sketch_teensy.cpp - Teensy 4.x objects (baseline_teensy.h): the extra UARTs and Wire2. */
#include "baseline.h"

HardwareSerial Serial3(3);
HardwareSerial Serial4(4);
HardwareSerial Serial5(5);
HardwareSerial Serial6(6);
HardwareSerial Serial7(7);
HardwareSerial Serial8(8);      /* Teensy 4.1 only: begin() fails on the 4.0 */
TwoWire Wire2(2);
