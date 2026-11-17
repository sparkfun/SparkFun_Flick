/*
 * bl_board.h - SparkFun MicroMod Teensy with USB type Serial + Keyboard + Mouse + Joystick: the
 * board can type, move the mouse and act as a gamepad (USBHIDKeyboard, ...).
 * Everything else is teensy_micromod's; USB audio and MIDI are what this one gives up.
 */
#pragma once
#include "../../teensy_micromod/sdk/bl_board.h"
#undef BL_TARGET_NAME
#undef BL_BOARD_NAME
#undef BL_HAS_USB_AUDIO
#define BL_TARGET_NAME          "teensy_micromod_hid"
#define BL_BOARD_NAME           "SparkFun MicroMod Teensy (USB keyboard, mouse, joystick)"
#define BL_HAS_USB_DEVICE       1           /* USBHIDKeyboard, USBHIDMouse, USBHIDGamepad, USBHIDConsumerControl */
