/*
 * baseline_usb.h - USB device mode (ESP32-S3): keyboard, mouse, gamepad,
 * media keys and MIDI, with Arduino-ESP32's class names. <USB.h>,
 * <USBHIDKeyboard.h>, <USBHIDMouse.h>, <USBHIDGamepad.h>,
 * <USBHIDConsumerControl.h> and <USBMIDI.h> lead here.
 *
 * As in Arduino: begin() each device, then USB.begin(). The first time a
 * sketch asks for a set of devices, Baseline reboots into USB device mode (the
 * sketch starts again and USB.begin() then returns true). The board keeps its
 * USB serial port, so Serial and `bsk upload` work as before; the COM port
 * number may change once. Safe mode, `bsk baseline flash`, and installing a
 * different sketch switch USB device mode off again.
 *
 * On the classic ESP32 (no USB device hardware) USB.begin() returns false.
 */
#ifndef BASELINE_USB_H
#define BASELINE_USB_H

#ifdef __cplusplus

/* Devices registered with begin() before USB.begin(). */
void bl_usb_want(uint32_t device);

class ESPUSB {
public:
    bool begin();
    void end() { bl_usb_end(); }
    operator bool() { return bl_usb_mounted() == 1; }
    /* Descriptor settings are fixed by Baseline; accepted for compatibility. */
    bool VID(uint16_t) { return true; }
    bool PID(uint16_t) { return true; }
    bool productName(const char *) { return true; }
    bool manufacturerName(const char *) { return true; }
    bool serialNumber(const char *) { return true; }
    bool usbVersion(uint16_t) { return true; }
    bool usbPower(uint16_t) { return true; }
};
extern ESPUSB USB;

/* ---- keyboard ---- */
#define KEY_LEFT_CTRL    0x80
#define KEY_LEFT_SHIFT   0x81
#define KEY_LEFT_ALT     0x82
#define KEY_LEFT_GUI     0x83
#define KEY_RIGHT_CTRL   0x84
#define KEY_RIGHT_SHIFT  0x85
#define KEY_RIGHT_ALT    0x86
#define KEY_RIGHT_GUI    0x87
#define KEY_UP_ARROW     0xDA
#define KEY_DOWN_ARROW   0xD9
#define KEY_LEFT_ARROW   0xD8
#define KEY_RIGHT_ARROW  0xD7
#define KEY_MENU         0xED
#define KEY_SPACE        0x20
#define KEY_BACKSPACE    0xB2
#define KEY_TAB          0xB3
#define KEY_RETURN       0xB0
#define KEY_ESC          0xB1
#define KEY_INSERT       0xD1
#define KEY_DELETE       0xD4
#define KEY_PAGE_UP      0xD3
#define KEY_PAGE_DOWN    0xD6
#define KEY_HOME         0xD2
#define KEY_END          0xD5
#define KEY_NUM_LOCK     0xDB
#define KEY_CAPS_LOCK    0xC1
#define KEY_F1           0xC2
#define KEY_F2           0xC3
#define KEY_F3           0xC4
#define KEY_F4           0xC5
#define KEY_F5           0xC6
#define KEY_F6           0xC7
#define KEY_F7           0xC8
#define KEY_F8           0xC9
#define KEY_F9           0xCA
#define KEY_F10          0xCB
#define KEY_F11          0xCC
#define KEY_F12          0xCD
#define KEY_F13          0xF0
#define KEY_F14          0xF1
#define KEY_F15          0xF2
#define KEY_F16          0xF3
#define KEY_F17          0xF4
#define KEY_F18          0xF5
#define KEY_F19          0xF6
#define KEY_F20          0xF7
#define KEY_F21          0xF8
#define KEY_F22          0xF9
#define KEY_F23          0xFA
#define KEY_F24          0xFB
#define KEY_PRINT_SCREEN 0xCE
#define KEY_SCROLL_LOCK  0xCF
#define KEY_PAUSE        0xD0
#define KEY_KP_SLASH     0xDC
#define KEY_KP_ASTERISK  0xDD
#define KEY_KP_MINUS     0xDE
#define KEY_KP_PLUS      0xDF
#define KEY_KP_ENTER     0xE0
#define KEY_KP_1         0xE1
#define KEY_KP_2         0xE2
#define KEY_KP_3         0xE3
#define KEY_KP_4         0xE4
#define KEY_KP_5         0xE5
#define KEY_KP_6         0xE6
#define KEY_KP_7         0xE7
#define KEY_KP_8         0xE8
#define KEY_KP_9         0xE9
#define KEY_KP_0         0xEA
#define KEY_KP_DOT       0xEB

class USBHIDKeyboard : public Print {
public:
    void begin(const uint8_t *layout = 0) { bl_usb_want(BL_USB_KEYBOARD); }
    void end() {}
    using Print::write;
    size_t write(uint8_t k) { return (size_t)bl_usb_keyboard(BL_USB_WRITE, k); }
    size_t write(const uint8_t *buf, size_t size) override
    {
        size_t n = 0;
        while (n < size && write(buf[n])) {
            n++;
        }
        return n;
    }
    size_t press(uint8_t k) { return (size_t)bl_usb_keyboard(BL_USB_PRESS, k); }
    size_t release(uint8_t k) { return (size_t)bl_usb_keyboard(BL_USB_RELEASE, k); }
    void releaseAll() { bl_usb_keyboard(BL_USB_RELEASE_ALL, 0); }
};

/* ---- mouse ---- */
#define MOUSE_LEFT     0x01
#define MOUSE_RIGHT    0x02
#define MOUSE_MIDDLE   0x04
#define MOUSE_BACKWARD 0x08
#define MOUSE_FORWARD  0x10
#define MOUSE_ALL      0x1F

class USBHIDMouse {
public:
    void begin() { bl_usb_want(BL_USB_MOUSE); }
    void end() {}
    void move(int x, int y, int wheel = 0, int pan = 0) { bl_usb_mouse(BL_USB_MOVE, x, y, wheel, 0); }
    void click(uint8_t b = MOUSE_LEFT) { bl_usb_mouse(BL_USB_CLICK, 0, 0, 0, b); }
    void press(uint8_t b = MOUSE_LEFT) { buttons_ |= b; bl_usb_mouse(BL_USB_PRESS, 0, 0, 0, b); }
    void release(uint8_t b = MOUSE_LEFT) { buttons_ &= ~b; bl_usb_mouse(BL_USB_RELEASE, 0, 0, 0, b); }
    bool isPressed(uint8_t b = MOUSE_LEFT) { return (buttons_ & b) != 0; }

private:
    uint8_t buttons_ = 0;
};

/* ---- gamepad ---- */
#define BUTTON_A      0
#define BUTTON_B      1
#define BUTTON_C      2
#define BUTTON_X      3
#define BUTTON_Y      4
#define BUTTON_Z      5
#define BUTTON_TL     6
#define BUTTON_TR     7
#define BUTTON_TL2    8
#define BUTTON_TR2    9
#define BUTTON_SELECT 10
#define BUTTON_START  11
#define BUTTON_MODE   12
#define BUTTON_THUMBL 13
#define BUTTON_THUMBR 14
#define BUTTON_SOUTH BUTTON_A
#define BUTTON_EAST  BUTTON_B
#define BUTTON_NORTH BUTTON_X
#define BUTTON_WEST  BUTTON_Y
#define HAT_CENTER     0
#define HAT_UP         1
#define HAT_UP_RIGHT   2
#define HAT_RIGHT      3
#define HAT_DOWN_RIGHT 4
#define HAT_DOWN       5
#define HAT_DOWN_LEFT  6
#define HAT_LEFT       7
#define HAT_UP_LEFT    8

class USBHIDGamepad {
public:
    void begin() { bl_usb_want(BL_USB_GAMEPAD); }
    void end() {}
    bool leftStick(int8_t x, int8_t y) { x_ = x; y_ = y; return write(); }
    bool rightStick(int8_t z, int8_t rz) { z_ = z; rz_ = rz; return write(); }
    bool leftTrigger(int8_t rx) { rx_ = rx; return write(); }
    bool rightTrigger(int8_t ry) { ry_ = ry; return write(); }
    bool hat(uint8_t h) { hat_ = h; return write(); }
    bool pressButton(uint8_t b) { if (b < 32) buttons_ |= 1UL << b; return write(); }
    bool releaseButton(uint8_t b) { if (b < 32) buttons_ &= ~(1UL << b); return write(); }
    bool send(int8_t x, int8_t y, int8_t z, int8_t rz, int8_t rx, int8_t ry, uint8_t h, uint32_t buttons)
    {
        x_ = x; y_ = y; z_ = z; rz_ = rz; rx_ = rx; ry_ = ry; hat_ = h; buttons_ = buttons;
        return write();
    }
    bool write() { return bl_usb_gamepad(x_, y_, z_, rz_, rx_, ry_, hat_, buttons_) == 1; }

private:
    int8_t x_ = 0, y_ = 0, z_ = 0, rz_ = 0, rx_ = 0, ry_ = 0;
    uint8_t hat_ = 0;
    uint32_t buttons_ = 0;
};

/* ---- media keys (HID consumer page) ---- */
#define CONSUMER_CONTROL_VOLUME_INCREMENT 0x00E9
#define CONSUMER_CONTROL_VOLUME_DECREMENT 0x00EA
#define CONSUMER_CONTROL_MUTE             0x00E2
#define CONSUMER_CONTROL_PLAY_PAUSE       0x00CD
#define CONSUMER_CONTROL_SCAN_NEXT        0x00B5
#define CONSUMER_CONTROL_SCAN_PREVIOUS    0x00B6
#define CONSUMER_CONTROL_STOP             0x00B7
#define CONSUMER_CONTROL_BRIGHTNESS_UP    0x006F
#define CONSUMER_CONTROL_BRIGHTNESS_DOWN  0x0070
#define CONSUMER_CONTROL_POWER            0x0030
#define CONSUMER_CONTROL_SLEEP            0x0032
#define CONSUMER_CONTROL_CALCULATOR       0x0192
#define CONSUMER_CONTROL_EMAIL_READER     0x018A
#define CONSUMER_CONTROL_WWW_HOME         0x0223
#define CONSUMER_CONTROL_SEARCH           0x0221

class USBHIDConsumerControl {
public:
    void begin() { bl_usb_want(BL_USB_CONSUMER); }
    void end() {}
    size_t press(uint16_t k) { return (size_t)bl_usb_consumer(BL_USB_PRESS, k); }
    size_t release() { return (size_t)bl_usb_consumer(BL_USB_RELEASE, 0); }
};

/* ---- MIDI ---- */
typedef struct {
    uint8_t header;     /* cable number << 4 | code index number */
    uint8_t byte1;
    uint8_t byte2;
    uint8_t byte3;
} midiEventPacket_t;

class USBMIDI {
public:
    void begin() { bl_usb_want(BL_USB_MIDI); }
    void end() {}
    void noteOn(uint8_t note, uint8_t velocity = 0, uint8_t channel = 1) { msg(0x9, 0x90, channel, note, velocity); }
    void noteOff(uint8_t note, uint8_t velocity = 0, uint8_t channel = 1) { msg(0x8, 0x80, channel, note, velocity); }
    void programChange(uint8_t program, uint8_t channel = 1) { msg(0xC, 0xC0, channel, program, 0); }
    void controlChange(uint8_t control, uint8_t value = 0, uint8_t channel = 1) { msg(0xB, 0xB0, channel, control, value); }
    void polyPressure(uint8_t note, uint8_t pressure, uint8_t channel = 1) { msg(0xA, 0xA0, channel, note, pressure); }
    void channelPressure(uint8_t pressure, uint8_t channel = 1) { msg(0xD, 0xD0, channel, pressure, 0); }
    void pitchBend(int16_t value, uint8_t channel = 1) { pitchBend((uint16_t)(value + 8192), channel); }   // -8192..8191
    void pitchBend(uint16_t value, uint8_t channel = 1) { msg(0xE, 0xE0, channel, value & 0x7F, (value >> 7) & 0x7F); }   // 0..16383
    void pitchBend(double value, uint8_t channel = 1) { pitchBend((int16_t)(value * 8191.0), channel); }   // -1.0..1.0
    bool readPacket(midiEventPacket_t *packet) { return packet && bl_usb_midi_read((uint8_t *)packet) == 1; }
    bool writePacket(midiEventPacket_t *packet) { return packet && bl_usb_midi_write((const uint8_t *)packet) == 1; }

private:
    void msg(uint8_t cin, uint8_t status, uint8_t channel, uint8_t d1, uint8_t d2)
    {
        midiEventPacket_t p = { cin, (uint8_t)(status | ((channel - 1) & 0x0F)), (uint8_t)(d1 & 0x7F), (uint8_t)(d2 & 0x7F) };
        writePacket(&p);
    }
};

#endif /* __cplusplus */
#endif /* BASELINE_USB_H */
