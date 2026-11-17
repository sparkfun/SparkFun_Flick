/*
 * bl_board.h - PJRC Teensy 4.1 with USB type Serial + Keyboard + Mouse + Joystick: the
 * board can type, move the mouse and act as a gamepad (USBHIDKeyboard, ...).
 * Everything else is teensy41's; USB audio and MIDI are what this one gives up.
 */
#pragma once
#include "../../teensy41/sdk/bl_board.h"
#undef BL_TARGET_NAME
#undef BL_BOARD_NAME
#undef BL_HAS_USB_AUDIO
#define BL_TARGET_NAME          "teensy41_hid"
#define BL_BOARD_NAME           "PJRC Teensy 4.1 (USB keyboard, mouse, joystick)"
#define BL_HAS_USB_DEVICE       1           /* USBHIDKeyboard, USBHIDMouse, USBHIDGamepad, USBHIDConsumerControl */
