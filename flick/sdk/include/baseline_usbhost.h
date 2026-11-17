/*
 * baseline_usbhost.h - the USB host port with USBHost_t36's names (Teensy 4.x).
 *
 *   #include <USBHost_t36.h>
 *   USBHost myusb;
 *   USBHub hub1(myusb);
 *   KeyboardController keyboard1(myusb);
 *   void setup() { myusb.begin(); keyboard1.attachPress(onPress); }
 *   void loop()  { myusb.Task(); }
 *
 * The drivers live in Baseline (two of each kind, behind two hubs); these
 * classes are thin handles. Each object takes the next driver of its kind in
 * the order the sketch declares them. Keyboard callbacks run on the sketch's
 * task (from delay(), yield() and after loop()), not in the USB interrupt.
 * Bluetooth through a USB dongle is not supported.
 */
#ifndef BASELINE_USBHOST_H
#define BASELINE_USBHOST_H

#ifdef __cplusplus

#define USBHOST_SERIAL_7E1 0x047
#define USBHOST_SERIAL_7O1 0x027
#define USBHOST_SERIAL_8N1 0x08
#define USBHOST_SERIAL_8N2 0x108
#define USBHOST_SERIAL_8E1 0x048
#define USBHOST_SERIAL_8O1 0x028

class USBHost {
public:
    constexpr USBHost() {}
    static void begin() { bl_usbh_begin(); }
    static void Task() { yield(); }     /* Baseline runs the real Task(); this delivers queued events */
};

/* The next driver of a kind: the order of declaration in the sketch. */
int bl_usbh_claim(int kind);

class USBHostDevice {
public:
    USBHostDevice(int kind) : kind_(kind), idx_(bl_usbh_claim(kind)) {}
    operator bool() { return info().connected; }
    uint16_t idVendor() { return info().vid; }
    uint16_t idProduct() { return info().pid; }
    const uint8_t *manufacturer() { refresh(); return (const uint8_t *)info_.manufacturer; }
    const uint8_t *product() { refresh(); return (const uint8_t *)info_.product; }
    const uint8_t *serialNumber() { refresh(); return (const uint8_t *)info_.serial; }

protected:
    const bl_usbh_info_t &info() { refresh(); return info_; }
    void refresh() { bl_usbh_info(kind_, idx_, &info_); }
    int kind_;
    int idx_;
    bl_usbh_info_t info_ = {};
};

class USBHub : public USBHostDevice {
public:
    USBHub(USBHost &) : USBHostDevice(BL_USBH_HUB) {}
};

class USBHIDParser {            /* Baseline has its own parsers: nothing to do */
public:
    constexpr USBHIDParser(USBHost &) {}
};

class KeyboardController : public USBHostDevice {
public:
    KeyboardController(USBHost &);
    uint16_t getKey() { return key_; }
    uint8_t getModifiers() { return modifiers_; }
    uint8_t getOemKey() { return oem_; }
    void attachPress(void (*f)(int unicode)) { press_ = f; }
    void attachRelease(void (*f)(int unicode)) { release_ = f; }
    void attachRawPress(void (*f)(uint8_t keycode)) { raw_press_ = f; }
    void attachRawRelease(void (*f)(uint8_t keycode)) { raw_release_ = f; }
    void LEDS(uint8_t leds) { bl_usbh_keyboard_leds(idx_, leds); }
    uint8_t LEDS() { return (uint8_t)bl_usbh_keyboard_leds(idx_, -1); }
    bool numLock() { return LEDS() & 1; }
    bool capsLock() { return LEDS() & 2; }
    bool scrollLock() { return LEDS() & 4; }
    void numLock(bool on) { LEDS((LEDS() & ~1) | (on ? 1 : 0)); }
    void capsLock(bool on) { LEDS((LEDS() & ~2) | (on ? 2 : 0)); }
    void scrollLock(bool on) { LEDS((LEDS() & ~4) | (on ? 4 : 0)); }
    void deliver();             /* SDK-internal: the pump */

private:
    uint16_t key_ = 0;
    uint8_t modifiers_ = 0, oem_ = 0;
    void (*press_)(int) = nullptr;
    void (*release_)(int) = nullptr;
    void (*raw_press_)(uint8_t) = nullptr;
    void (*raw_release_)(uint8_t) = nullptr;
};

class MouseController : public USBHostDevice {
public:
    MouseController(USBHost &) : USBHostDevice(BL_USBH_MOUSE) {}
    bool available() { if (!have_) have_ = bl_usbh_mouse_take(idx_, &m_) == 1; return have_; }
    void mouseDataClear() { have_ = false; }
    uint8_t getButtons() { return m_.buttons; }
    int getMouseX() { return m_.x; }
    int getMouseY() { return m_.y; }
    int getWheel() { return m_.wheel; }
    int getWheelH() { return m_.wheel_h; }

private:
    bl_usbh_mouse_t m_ = {};
    bool have_ = false;
};

class JoystickController : public USBHostDevice {
public:
    typedef enum { UNKNOWN = 0, PS3, PS4, XBOXONE, XBOX360, PS3_MOTION, SpaceNav, SWITCH } joytype_t;
    JoystickController(USBHost &) : USBHostDevice(BL_USBH_JOYSTICK) {}
    bool available() { if (!have_) have_ = bl_usbh_joystick_take(idx_, &j_) == 1; return have_; }
    void joystickDataClear() { have_ = false; }
    uint32_t getButtons() { return j_.buttons; }
    int getAxis(uint32_t i) { return i < BL_USBH_JOY_AXES ? (int)j_.axes[i] : 0; }
    uint64_t axisMask() { return j_.axis_mask; }
    joytype_t joystickType() { return (joytype_t)j_.type; }

private:
    bl_usbh_joystick_t j_ = {};
    bool have_ = false;
};

class MIDIDevice : public USBHostDevice {
public:
    enum MidiType {
        InvalidType = 0x00, NoteOff = 0x80, NoteOn = 0x90, AfterTouchPoly = 0xA0, ControlChange = 0xB0,
        ProgramChange = 0xC0, AfterTouchChannel = 0xD0, PitchBend = 0xE0, SystemExclusive = 0xF0,
        TimeCodeQuarterFrame = 0xF1, SongPosition = 0xF2, SongSelect = 0xF3, TuneRequest = 0xF6,
        Clock = 0xF8, Start = 0xFA, Continue = 0xFB, Stop = 0xFC, ActiveSensing = 0xFE, SystemReset = 0xFF,
    };
    MIDIDevice(USBHost &) : USBHostDevice(BL_USBH_MIDI) {}
    bool read(uint8_t channel = 0)
    {
        while (bl_usbh_midi_read(idx_, msg_) == 1) {
            if (channel == 0 || msg_[1] == channel || msg_[0] >= 0xF0) {
                return true;
            }
        }
        return false;
    }
    uint8_t getType() { return msg_[0]; }
    uint8_t getChannel() { return msg_[1]; }
    uint8_t getData1() { return msg_[2]; }
    uint8_t getData2() { return msg_[3]; }
    uint8_t getCable() { return msg_[4]; }
    void send(uint8_t type, uint8_t data1, uint8_t data2, uint8_t channel, uint8_t cable = 0)
    {
        bl_usbh_midi_send(idx_, type, data1, data2, channel, cable);
    }
    void sendNoteOn(uint8_t note, uint8_t velocity, uint8_t channel, uint8_t cable = 0) { send(NoteOn, note, velocity, channel, cable); }
    void sendNoteOff(uint8_t note, uint8_t velocity, uint8_t channel, uint8_t cable = 0) { send(NoteOff, note, velocity, channel, cable); }
    void sendControlChange(uint8_t control, uint8_t value, uint8_t channel, uint8_t cable = 0) { send(ControlChange, control, value, channel, cable); }
    void sendProgramChange(uint8_t program, uint8_t channel, uint8_t cable = 0) { send(ProgramChange, program, 0, channel, cable); }
    void sendAfterTouch(uint8_t pressure, uint8_t channel, uint8_t cable = 0) { send(AfterTouchChannel, pressure, 0, channel, cable); }
    void sendPitchBend(int value, uint8_t channel, uint8_t cable = 0)
    {
        unsigned v = (unsigned)(value + 8192);
        send(PitchBend, v & 0x7F, (v >> 7) & 0x7F, channel, cable);
    }

private:
    uint8_t msg_[5] = {};
};
typedef MIDIDevice MIDIDevice_BigBuffer;

class USBSerial : public USBHostDevice, public Stream {
public:
    USBSerial(USBHost &) : USBHostDevice(BL_USBH_SERIAL) {}
    void begin(uint32_t baud, uint32_t format = USBHOST_SERIAL_8N1) { bl_usbh_serial_begin(idx_, baud, format); }
    void end() {}
    int available() override { int n = bl_usbh_serial_available(idx_); return n > 0 ? n + (peek_ >= 0) : (peek_ >= 0); }
    int read() override
    {
        if (peek_ >= 0) { int c = peek_; peek_ = -1; return c; }
        uint8_t c;
        return bl_usbh_serial_read(idx_, &c, 1) == 1 ? c : -1;
    }
    int peek() override
    {
        if (peek_ < 0) { uint8_t c; if (bl_usbh_serial_read(idx_, &c, 1) == 1) peek_ = c; }
        return peek_;
    }
    size_t write(uint8_t c) override { return (size_t)bl_usbh_serial_write(idx_, &c, 1); }
    size_t write(const uint8_t *buf, size_t len) override { return (size_t)bl_usbh_serial_write(idx_, buf, len); }
    using Print::write;
    void flush() override {}
    operator bool() { return USBHostDevice::operator bool(); }

private:
    int peek_ = -1;
};
typedef USBSerial USBSerial_BigBuffer;

class USBDrive : public USBHostDevice {
public:
    USBDrive(USBHost &) : USBHostDevice(BL_USBH_DRIVE) {}
    bool available() { return (bool)*this; }
};

/* The drive's first partition: the same calls as LittleFS and SD. */
class USBFilesystem : public FS {
public:
    USBFilesystem(USBHost &) : FS(BL_FS_USB) {}
    operator bool()
    {
        bl_usbh_info_t i;
        return bl_usbh_info(BL_USBH_DRIVE, 0, &i) == BL_OK && i.connected;
    }
    bool mediaPresent() { return (bool)*this; }
};

#endif /* __cplusplus */
#endif /* BASELINE_USBHOST_H */
