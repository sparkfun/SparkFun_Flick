/* API table: USB device mode - keyboard, mouse, gamepad, media keys, MIDI (ABI 1.6).
 *
 * ESP32-S3 only. Normally the S3's USB port is the built-in USB-Serial-JTAG
 * (console, uploads, crash output). When a sketch asks for USB devices,
 * Baseline saves the set in NVS and reboots; at boot (bl_usb_boot) it then
 * creates those TinyUSB devices plus a TinyUSB CDC serial port, starts USB,
 * and moves the console to that CDC port, so uploads keep working.
 *
 * Safe mode always boots without USB devices, and installing a new sketch
 * forgets the set (the new sketch asks again if it needs them). Note: in USB
 * device mode, panic messages are not visible over USB; the coredump in flash
 * still records them. */
#include <Arduino.h>
#include "sdkconfig.h"
#include "soc/soc_caps.h"
#include "nvs.h"
#include "gen/bl_impl.h"
#include "bl_console.h"

#define NVS_NS  "baseline"
#define NVS_KEY "usb"
#define ALL_DEVICES (BL_USB_KEYBOARD | BL_USB_MOUSE | BL_USB_GAMEPAD | BL_USB_CONSUMER | BL_USB_MIDI)

static uint32_t load_mask(void)
{
    nvs_handle_t h;
    uint32_t mask = 0;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u32(h, NVS_KEY, &mask);
        nvs_close(h);
    }
    return mask & ALL_DEVICES;
}

static void save_mask(uint32_t mask)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, NVS_KEY, mask);
        nvs_commit(h);
        nvs_close(h);
    }
}

extern "C" void bl_usb_forget(void)
{
    if (load_mask()) {
        save_mask(0);
    }
}

#if SOC_USB_OTG_SUPPORTED && CONFIG_TINYUSB_ENABLED
#include "USB.h"
#include "USBCDC.h"
#include "USBHIDKeyboard.h"
#include "USBHIDMouse.h"
#include "USBHIDGamepad.h"
#include "USBHIDConsumerControl.h"
#include "USBMIDI.h"
#include "tusb.h"

static uint32_t s_active;
static USBCDC *s_cdc;
static USBHIDKeyboard *s_kbd;
static USBHIDMouse *s_mouse;
static USBHIDGamepad *s_pad;
static USBHIDConsumerControl *s_media;
static USBMIDI *s_midi;

#if SOC_USB_SERIAL_JTAG_SUPPORTED
#include "soc/rtc_cntl_reg.h"
/* Give the USB pins back to USB-Serial-JTAG. TinyUSB routes them to the OTG
 * controller through an RTC register, which esp_restart() (and a crash) does
 * not reset; without this, a board that leaves USB device mode by rebooting
 * shows up on USB as nothing at all until it is power-cycled. */
static void usb_pins_to_serial_jtag(void)
{
    const uint32_t otg_bits = RTC_CNTL_SW_HW_USB_PHY_SEL | RTC_CNTL_SW_USB_PHY_SEL | RTC_CNTL_USB_PAD_ENABLE;
    if (REG_READ(RTC_CNTL_USB_CONF_REG) & otg_bits) {
        CLEAR_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG, otg_bits);
        BLOG_I("USB pins handed back to the USB serial port");
    }
}
#else
static void usb_pins_to_serial_jtag(void) {}
#endif

extern "C" void bl_usb_boot(int safe_mode)
{
    uint32_t mask = load_mask();
    if (!mask) {
        usb_pins_to_serial_jtag();
        return;
    }
    if (safe_mode) {
        usb_pins_to_serial_jtag();
        BLOG_W("safe mode: USB devices off (plain USB serial port)");
        return;
    }
    // Every device must exist before USB.begin(): it builds one composite descriptor.
    if (mask & BL_USB_KEYBOARD) { s_kbd = new USBHIDKeyboard(); s_kbd->begin(); }
    if (mask & BL_USB_MOUSE)    { s_mouse = new USBHIDMouse(); s_mouse->begin(); }
    if (mask & BL_USB_GAMEPAD)  { s_pad = new USBHIDGamepad(); s_pad->begin(); }
    if (mask & BL_USB_CONSUMER) { s_media = new USBHIDConsumerControl(); s_media->begin(); }
    if (mask & BL_USB_MIDI)     { s_midi = new USBMIDI(); s_midi->begin(); }
    s_cdc = new USBCDC(0);
    s_cdc->setRxBufferSize(8192);   // upload lines arrive in ~1 KB bursts
    s_cdc->begin(115200);
    USB.begin();                    // takes the USB port over from USB-Serial-JTAG
    bl_console_use(s_cdc);
    s_active = mask;
    BLOG_I("USB device mode: devices 0x%02x + serial", (unsigned)mask);
}

extern "C" int bl_impl_usb_begin(uint32_t devices)
{
    if (!devices || (devices & ~ALL_DEVICES)) {
        return BL_ERR_ARG;
    }
    if ((s_active & devices) == devices) {
        return BL_OK;               // already running
    }
    save_mask(s_active | devices);
    BLOG_I("switching to USB device mode (devices 0x%02x): rebooting", (unsigned)(s_active | devices));
    bl_console_flush();
    delay(100);
    esp_restart();
    return BL_OK;
}

extern "C" void bl_impl_usb_end(void)
{
    if (!s_active && !load_mask()) {
        return;
    }
    save_mask(0);
    BLOG_I("leaving USB device mode: rebooting");
    bl_console_flush();
    delay(100);
    esp_restart();
}

extern "C" uint32_t bl_impl_usb_devices(void)
{
    return s_active;
}

extern "C" int bl_impl_usb_mounted(void)
{
    return s_active && tud_mounted() ? 1 : 0;
}

extern "C" int bl_impl_usb_keyboard(int op, uint32_t key)
{
    if (!s_kbd) {
        return 0;
    }
    switch (op) {
    case BL_USB_PRESS:       return s_kbd->press((uint8_t)key) ? 1 : 0;
    case BL_USB_RELEASE:     return s_kbd->release((uint8_t)key) ? 1 : 0;
    case BL_USB_RELEASE_ALL: s_kbd->releaseAll(); return 1;
    case BL_USB_WRITE:       return s_kbd->write((uint8_t)key) ? 1 : 0;
    default:                 return 0;
    }
}

static int8_t clamp8(int v)
{
    return (int8_t)(v < -127 ? -127 : v > 127 ? 127 : v);
}

extern "C" int bl_impl_usb_mouse(int op, int x, int y, int wheel, int buttons)
{
    if (!s_mouse) {
        return 0;
    }
    switch (op) {
    case BL_USB_MOVE:    s_mouse->move(clamp8(x), clamp8(y), clamp8(wheel)); return 1;
    case BL_USB_PRESS:   s_mouse->press((uint8_t)buttons); return 1;
    case BL_USB_RELEASE: s_mouse->release((uint8_t)buttons); return 1;
    case BL_USB_CLICK:   s_mouse->click((uint8_t)buttons); return 1;
    default:             return 0;
    }
}

extern "C" int bl_impl_usb_gamepad(int x, int y, int z, int rz, int rx, int ry, int hat, uint32_t buttons)
{
    return s_pad && s_pad->send(clamp8(x), clamp8(y), clamp8(z), clamp8(rz), clamp8(rx), clamp8(ry),
                                (uint8_t)hat, buttons) ? 1 : 0;
}

extern "C" int bl_impl_usb_consumer(int op, uint16_t usage)
{
    if (!s_media) {
        return 0;
    }
    if (op == BL_USB_PRESS) {
        return s_media->press(usage) ? 1 : 0;
    }
    return s_media->release() ? 1 : 0;
}

extern "C" int bl_impl_usb_midi_write(const uint8_t *packet)
{
    if (!s_midi || !packet) {
        return 0;
    }
    midiEventPacket_t p;
    memcpy(&p, packet, 4);
    return s_midi->writePacket(&p) ? 1 : 0;
}

extern "C" int bl_impl_usb_midi_read(uint8_t *packet)
{
    midiEventPacket_t p;
    if (!s_midi || !packet || !s_midi->readPacket(&p)) {
        return 0;
    }
    memcpy(packet, &p, 4);
    return 1;
}

#else   /* no USB OTG on this chip (classic ESP32) */

extern "C" void bl_usb_boot(int safe_mode) {}
extern "C" int bl_impl_usb_begin(uint32_t devices) { return BL_ERR_UNSUPPORTED; }
extern "C" void bl_impl_usb_end(void) {}
extern "C" uint32_t bl_impl_usb_devices(void) { return 0; }
extern "C" int bl_impl_usb_mounted(void) { return 0; }
extern "C" int bl_impl_usb_keyboard(int op, uint32_t key) { return 0; }
extern "C" int bl_impl_usb_mouse(int op, int x, int y, int wheel, int buttons) { return 0; }
extern "C" int bl_impl_usb_gamepad(int x, int y, int z, int rz, int rx, int ry, int hat, uint32_t buttons) { return 0; }
extern "C" int bl_impl_usb_consumer(int op, uint16_t usage) { return 0; }
extern "C" int bl_impl_usb_midi_write(const uint8_t *packet) { return 0; }
extern "C" int bl_impl_usb_midi_read(uint8_t *packet) { return 0; }

#endif
