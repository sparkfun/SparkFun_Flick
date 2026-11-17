/* API table: the USB host port (ABI 1.9), with Teensyduino's USBHost_t36.
 *
 * Baseline owns the drivers: the host controller needs its interrupt and
 * DMA-aligned buffers, and the library is too big to send with every sketch.
 * Two of each kind, behind two hubs. Sketches use USBHost_t36's class names
 * (sdk/include/baseline_usbhost.h), which call these.
 *
 * Keyboard callbacks run in the USB interrupt; they only queue events, which
 * the SDK's pump hands to the sketch's own callbacks on its own task.
 */
#include "bl_teensy.h"
#include <USBHost_t36.h>

/* The drivers are made when a sketch first starts the port, in a pool in OCRAM:
 * they take about 45 KB, which the fast RAM (DTCM) can't spare. They hold the
 * transfer descriptors the USB controller reads by DMA, and USBHost_t36 only
 * keeps data buffers cache-coherent, so the pool is uncached (MPU region 14).
 * USBHost keeps a list of drivers; they only need to exist before begin(). */
#include <new>
#define POOL_SIZE   65536       /* a power of two: one MPU region */
static uint8_t s_pool[POOL_SIZE] __attribute__((section(".dmabuffers"), aligned(POOL_SIZE)));
static size_t s_pool_used;

static void pool_uncached(void)
{
    arm_dcache_flush_delete(s_pool, POOL_SIZE);
    SCB_MPU_RBAR = (uint32_t)s_pool | SCB_MPU_RBAR_REGION(14) | SCB_MPU_RBAR_VALID;
    SCB_MPU_RASR = SCB_MPU_RASR_TEX(1) | SCB_MPU_RASR_AP(3) | SCB_MPU_RASR_XN |
                   SCB_MPU_RASR_SIZE(15) | SCB_MPU_RASR_ENABLE;        /* normal, not cached, RW, 64 KB */
    asm volatile("dsb\n isb" ::: "memory");
    memset(s_pool, 0, POOL_SIZE);
}

template <typename T, typename... A> static T *make(A &...args)
{
    size_t n = (sizeof(T) + 31) & ~(size_t)31;      /* USBHost_t36 wants 32-byte alignment */
    if (s_pool_used + n > POOL_SIZE) {
        return NULL;
    }
    void *at = s_pool + s_pool_used;
    s_pool_used += n;
    return new (at) T(args...);
}

static USBHost *s_host_p;
#define s_host (*s_host_p)
static KeyboardController *s_kb[2];
static MouseController *s_mouse[2];
static JoystickController *s_joy[2];
static MIDIDevice_BigBuffer *s_midi[2];
static USBSerial_BigBuffer *s_ser[2];
static USBDrive *s_drive[2];
static USBFilesystem *s_fs[2];
static USBHub *s_hub[2];

static void make_drivers(void)
{
    pool_uncached();
    s_host_p = make<USBHost>();
    for (int i = 0; i < 2; i++) {
        s_hub[i] = make<USBHub>(s_host);
    }
    for (int i = 0; i < 4; i++) {
        make<USBHIDParser>(s_host);     /* mice, joysticks, keyboards' extras: kept by USBHost */
    }
    for (int i = 0; i < 2; i++) {
        s_kb[i] = make<KeyboardController>(s_host);
        s_mouse[i] = make<MouseController>(s_host);
        s_joy[i] = make<JoystickController>(s_host);
        s_midi[i] = make<MIDIDevice_BigBuffer>(s_host);
        s_ser[i] = make<USBSerial_BigBuffer>(s_host);
        s_drive[i] = make<USBDrive>(s_host);
        s_fs[i] = make<USBFilesystem>(s_host);
    }
}

static bool s_started;

/* ---------------- keyboard events: a small ring per keyboard ---------------- */

#define KEYQ 32         /* a power of two */
static bl_usbh_key_t s_keyq[2][KEYQ];
static volatile uint8_t s_key_head[2], s_key_tail[2];

static void key_push(int i, uint8_t type, uint16_t key)
{
    uint8_t h = s_key_head[i];
    if ((uint8_t)(h - s_key_tail[i]) >= KEYQ) {
        return;         /* the sketch isn't reading: drop */
    }
    bl_usbh_key_t *e = &s_keyq[i][h % KEYQ];
    e->type = type;
    e->oem = s_kb[i]->getOemKey();
    e->modifiers = s_kb[i]->getModifiers();
    e->reserved = 0;
    e->key = key;
    s_key_head[i] = h + 1;
}

static void press0(int k) { key_push(0, BL_USBH_KEY_PRESS, (uint16_t)k); }
static void release0(int k) { key_push(0, BL_USBH_KEY_RELEASE, (uint16_t)k); }
static void raw_press0(uint8_t k) { key_push(0, BL_USBH_KEY_RAW_PRESS, k); }
static void raw_release0(uint8_t k) { key_push(0, BL_USBH_KEY_RAW_RELEASE, k); }
static void press1(int k) { key_push(1, BL_USBH_KEY_PRESS, (uint16_t)k); }
static void release1(int k) { key_push(1, BL_USBH_KEY_RELEASE, (uint16_t)k); }
static void raw_press1(uint8_t k) { key_push(1, BL_USBH_KEY_RAW_PRESS, k); }
static void raw_release1(uint8_t k) { key_push(1, BL_USBH_KEY_RAW_RELEASE, k); }

/* From bl_poll(): enumeration, hubs and drives run in Task(). */
extern "C" void bl_usbh_poll(void)
{
    if (s_started) {
        s_host.Task();
    }
}

/* Also false before begin(): the drivers don't exist yet. */
static bool ok_index(int i)
{
    return s_started && (i == 0 || i == 1);
}

extern "C" int bl_impl_usbh_begin(void)
{
    if (!s_started) {
        make_drivers();
        s_kb[0]->attachPress(press0);
        s_kb[0]->attachRelease(release0);
        s_kb[0]->attachRawPress(raw_press0);
        s_kb[0]->attachRawRelease(raw_release0);
        s_kb[1]->attachPress(press1);
        s_kb[1]->attachRelease(release1);
        s_kb[1]->attachRawPress(raw_press1);
        s_kb[1]->attachRawRelease(raw_release1);
        s_host.begin();
        s_started = true;
    }
    return BL_OK;
}

static void copy_str(char *dst, const uint8_t *src)
{
    strlcpy(dst, src ? (const char *)src : "", 32);
}

template <typename D> static void fill_info(D *d, bl_usbh_info_t *info)
{
    info->connected = (bool)*d;
    if (info->connected) {
        info->vid = d->idVendor();
        info->pid = d->idProduct();
        copy_str(info->manufacturer, d->manufacturer());
        copy_str(info->product, d->product());
        copy_str(info->serial, d->serialNumber());
    }
}

extern "C" int bl_impl_usbh_info(int kind, int index, bl_usbh_info_t *info)
{
    if (!info || !ok_index(index)) {
        return BL_ERR_ARG;
    }
    memset(info, 0, sizeof(*info));
    switch (kind) {
    // The HID drivers inherit these from both their USB and their Bluetooth base:
    // name the base. A drive's IDs are on its file system object.
    case BL_USBH_KEYBOARD: fill_info(s_kb[index], info); break;
    case BL_USBH_MOUSE:    fill_info(static_cast<USBHIDInput *>(s_mouse[index]), info); break;
    case BL_USBH_JOYSTICK: fill_info(s_joy[index], info); break;
    case BL_USBH_MIDI:     fill_info(static_cast<USBDriver *>(s_midi[index]), info); break;
    case BL_USBH_SERIAL:   fill_info(static_cast<USBDriver *>(s_ser[index]), info); break;
    case BL_USBH_DRIVE:    fill_info(s_fs[index], info); break;
    case BL_USBH_HUB:      fill_info(static_cast<USBDriver *>(s_hub[index]), info); break;
    default:               return BL_ERR_ARG;
    }
    return BL_OK;
}

extern "C" int bl_impl_usbh_key_take(int index, bl_usbh_key_t *ev)
{
    if (!ev || !ok_index(index) || s_key_tail[index] == s_key_head[index]) {
        return 0;
    }
    uint8_t t = s_key_tail[index];
    *ev = s_keyq[index][t % KEYQ];
    s_key_tail[index] = t + 1;
    return 1;
}

extern "C" int bl_impl_usbh_keyboard_leds(int index, int leds)
{
    if (!ok_index(index)) {
        return BL_ERR_ARG;
    }
    if (leds >= 0) {
        s_kb[index]->LEDS((uint8_t)leds);
    }
    return s_kb[index]->LEDS();
}

extern "C" int bl_impl_usbh_mouse_take(int index, bl_usbh_mouse_t *m)
{
    if (!m || !ok_index(index) || !s_mouse[index]->available()) {
        return 0;
    }
    MouseController *d = s_mouse[index];
    m->buttons = d->getButtons();
    m->x = (int16_t)d->getMouseX();
    m->y = (int16_t)d->getMouseY();
    m->wheel = (int8_t)d->getWheel();
    m->wheel_h = (int8_t)d->getWheelH();
    m->reserved = 0;
    d->mouseDataClear();
    return 1;
}

extern "C" int bl_impl_usbh_joystick_take(int index, bl_usbh_joystick_t *j)
{
    if (!j || !ok_index(index) || !s_joy[index]->available()) {
        return 0;
    }
    JoystickController *d = s_joy[index];
    memset(j, 0, sizeof(*j));
    j->buttons = d->getButtons();
    j->axis_mask = (uint32_t)(d->axisMask() & 0xFFFF);
    for (int a = 0; a < BL_USBH_JOY_AXES; a++) {
        j->axes[a] = d->getAxis(a);
    }
    j->type = (uint8_t)d->joystickType();
    d->joystickDataClear();
    return 1;
}

extern "C" int bl_impl_usbh_midi_read(int index, uint8_t *msg)
{
    if (!msg || !ok_index(index) || !s_midi[index]->read()) {
        return 0;
    }
    MIDIDevice_BigBuffer *d = s_midi[index];
    msg[0] = d->getType();
    msg[1] = d->getChannel();
    msg[2] = d->getData1();
    msg[3] = d->getData2();
    msg[4] = d->getCable();
    return 1;
}

extern "C" int bl_impl_usbh_midi_send(int index, uint8_t type, uint8_t data1, uint8_t data2, uint8_t channel,
                                      uint8_t cable)
{
    if (!ok_index(index)) {
        return BL_ERR_ARG;
    }
    s_midi[index]->send(type, data1, data2, channel, cable);
    return BL_OK;
}

extern "C" int bl_impl_usbh_serial_begin(int index, uint32_t baud, uint32_t format)
{
    if (!ok_index(index)) {
        return BL_ERR_ARG;
    }
    s_ser[index]->begin(baud, format ? format : USBHOST_SERIAL_8N1);
    return BL_OK;
}

extern "C" int bl_impl_usbh_serial_available(int index)
{
    if (!ok_index(index) || !*s_ser[index]) {
        return -1;
    }
    return s_ser[index]->available();
}

extern "C" int bl_impl_usbh_serial_read(int index, uint8_t *buf, size_t len)
{
    if (!buf || !ok_index(index)) {
        return 0;
    }
    size_t n = 0;
    while (n < len && s_ser[index]->available() > 0) {
        buf[n++] = (uint8_t)s_ser[index]->read();
    }
    return (int)n;
}

extern "C" int bl_impl_usbh_serial_write(int index, const uint8_t *buf, size_t len)
{
    if (!buf || !ok_index(index) || !*s_ser[index]) {
        return 0;
    }
    return (int)s_ser[index]->write(buf, len);
}

/* The first partition of a USB drive, for the fs calls (BL_FS_USB). */
extern "C" FS *bl_usbh_fs(void)
{
    if (!s_started) {
        return NULL;
    }
    if (*s_fs[0]) {
        return s_fs[0];
    }
    return *s_fs[1] ? s_fs[1] : NULL;
}
