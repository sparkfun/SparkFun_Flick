/*
 * sketch_abi.h - The binary contract between Baseline and a Sketch (ESP32-S3).
 *
 * This file is shared by:
 *   - Baseline firmware (C)
 *   - the Sketch SDK (C)
 *   - the Sketch linker script (passed through the C preprocessor with
 *     BL_LINKER_SCRIPT defined, so only the #defines are visible there)
 *
 * Every address below is part of the ABI. Changing any of them requires a
 * new BL_ABI_MAJOR and re-building every sketch.
 */
#ifndef SKETCH_ABI_H
#define SKETCH_ABI_H

/* ---- Target ------------------------------------------------------------ */
#define BL_CHIP_ESP32S3             9       /* == esp_chip_id_t ESP_CHIP_ID_ESP32S3 */

/* ---- Flash window ------------------------------------------------------
 * The active sketch slot is mapped by the flash MMU at a fixed virtual
 * address, 24 MB into the 32 MB external-memory window. On the ESP32-S3 the
 * instruction bus (0x42xxxxxx) and data bus (0x3Cxxxxxx) share one MMU table,
 * so a single mapping makes the sketch visible at both addresses:
 *   code   (.text)            -> executed through BL_SKETCH_IBUS_BASE
 *   header / .rodata / .data  -> read through BL_SKETCH_DBUS_BASE
 * Byte offset N in the sketch partition appears at IBUS_BASE+N and DBUS_BASE+N.
 */
#define BL_SKETCH_IBUS_BASE         0x43800000
#define BL_SKETCH_DBUS_BASE         0x3D800000
#define BL_SKETCH_MAX_SIZE          0x80000     /* 512 KB == one sketch slot */
#define BL_SKETCH_HEADER_SIZE       0x100       /* header occupies offset 0..0xFF */

/* ---- RAM window --------------------------------------------------------
 * Sketch .data + .bss live in internal DRAM at a fixed address. Baseline
 * removes this range from the heap (SOC_RESERVE_MEMORY_REGION). It sits above
 * the largest possible Baseline static data area on most builds and below
 * the ROM startup stack (0x3FCE9710). Larger buffers: use bl_malloc().
 */
#define BL_SKETCH_RAM_BASE          0x3FCD9000
#define BL_SKETCH_RAM_SIZE          0x10000     /* 64 KB */

/* ---- Header ------------------------------------------------------------ */
#define BL_SKETCH_MAGIC             0x4B534C42  /* "BLSK" little-endian */
#define BL_SKETCH_HEADER_VERSION    1
#define BL_SKETCH_DEFAULT_STACK     8192
#define BL_SKETCH_MIN_STACK         3072

/* Byte offsets of fields that bsk.py patches after linking. Kept in sync with
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
    uint16_t chip_id;           /*  6 BL_CHIP_* */
    uint16_t abi_major;         /*  8 must equal Baseline's */
    uint16_t abi_minor;         /* 10 must be <= Baseline's */
    uint32_t image_size;        /* 12 total bytes incl. header (patched by bsk) */
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
    char     build_id[16];      /* 96 patched by bsk */
    uint8_t  reserved1[112];    /* 112 */
    uint8_t  sha256[32];        /* 224 SHA-256 of the whole image with this field zeroed */
} bl_sketch_header_t;

#endif /* BL_LINKER_SCRIPT */
#endif /* SKETCH_ABI_H */
