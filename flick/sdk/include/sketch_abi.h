/*
 * sketch_abi.h - The binary contract between Baseline and a Sketch.
 *
 * This file is shared by:
 *   - Baseline firmware (C)
 *   - the Sketch SDK (C)
 *   - the Sketch linker script (passed through the C preprocessor with
 *     BL_LINKER_SCRIPT defined, so only the #defines are visible there)
 *
 * The target's bl_target.h (targets/<target>/sdk/, on the include path)
 * supplies the chip ID and the fixed addresses:
 *   BL_SKETCH_IBUS_BASE   code (.text) is executed here
 *   BL_SKETCH_DBUS_BASE   header, .rodata and .data initializers are read here
 *   BL_SKETCH_MAX_SIZE    largest image the windows can hold
 *   BL_SKETCH_RAM_BASE/SIZE   .data + .bss, reserved out of Baseline's heap
 * Byte offset N in the sketch slot appears at IBUS_BASE+N and DBUS_BASE+N.
 * Those addresses are part of the target's ABI: changing them requires a new
 * BL_ABI_MAJOR and re-building every sketch for that target.
 */
#ifndef SKETCH_ABI_H
#define SKETCH_ABI_H

#include "bl_target.h"

#define BL_SKETCH_HEADER_SIZE       0x100       /* header occupies offset 0..0xFF */

/* ---- Header ------------------------------------------------------------ */
#define BL_SKETCH_MAGIC             0x4B534C42  /* "BLSK" little-endian */
#define BL_SKETCH_HEADER_VERSION    1
#define BL_SKETCH_DEFAULT_STACK     8192
#define BL_SKETCH_MIN_STACK         3072

/* Byte offsets of fields that flick.py patches after linking. Kept in sync with
 * the struct below by static asserts in Baseline. */
#define BL_HDR_OFF_IMAGE_SIZE       12
#define BL_HDR_OFF_BUILD_ID         96
#define BL_HDR_OFF_SHA256           (BL_SKETCH_HEADER_SIZE - 32)

#ifndef BL_LINKER_SCRIPT

#include <stdint.h>

struct bl_api;

/* Filled in by the sketch's sketch_entry(). */
typedef struct {
    void (*setup)(void);
    void (*loop)(void);
    void (*teardown)(void);     /* optional, may be NULL */
} bl_sketch_callbacks_t;

typedef void (*bl_sketch_entry_t)(const struct bl_api *api, bl_sketch_callbacks_t *out);

/* Lives at offset 0 of the sketch image. All addresses are absolute
 * (the sketch is linked at the fixed windows above). */
typedef struct {
    uint32_t magic;             /*  0 BL_SKETCH_MAGIC */
    uint16_t header_version;    /*  4 */
    uint16_t chip_id;           /*  6 BL_TARGET_CHIP_ID */
    uint16_t abi_major;         /*  8 must equal Baseline's */
    uint16_t abi_minor;         /* 10 must be <= Baseline's */
    uint32_t image_size;        /* 12 total bytes incl. header (patched by flick) */
    uint32_t flags;             /* 16 reserved, 0 */
    uint32_t entry;             /* 20 IBUS address of sketch_entry */
    uint32_t data_load;         /* 24 DBUS address of .data initial values */
    uint32_t data_start;        /* 28 RAM address */
    uint32_t data_end;          /* 32 */
    uint32_t bss_start;         /* 36 */
    uint32_t bss_end;           /* 40 */
    uint32_t init_array_start;  /* 44 DBUS address */
    uint32_t init_array_end;    /* 48 */
    uint32_t stack_size;        /* 52 sketch task stack, bytes */
    char     name[32];          /* 56 */
    uint8_t  reserved0[8];      /* 88 */
    char     build_id[16];      /* 96 patched by flick */
    uint8_t  reserved1[112];    /* 112 */
    uint8_t  sha256[32];        /* 224 SHA-256 of the whole image with this field zeroed */
} bl_sketch_header_t;

#endif /* BL_LINKER_SCRIPT */
#endif /* SKETCH_ABI_H */
