/*
 * bsu_format.h - Baseline update packages (.bsu) and the staging record.
 *
 * Shared by Baseline (baseline/src/bl_update.cpp), the 4 MB boards'
 * bootloader hook (idf/bootloader_components/bsu_stage) and flick
 * (tools/flick.py, make_bsu), so keep the three in step.
 *
 * A .bsu file is a bsu_header_t followed by the Baseline app image (the IDF
 * app .bin, as flashed at the app partition) compressed with raw deflate
 * (zlib wbits -15). Every board takes the same file:
 *   - boards with two Baseline slots (8/16 MB flash) inflate it into the free
 *     slot while running, then switch slots (IDF rolls back a Baseline that
 *     does not confirm itself);
 *   - 4 MB boards store it, as is, over the sketch slots and the file system
 *     ("staging") and restart; the bootloader inflates it over the Baseline
 *     partition (bsu_stage_t below), then the new Baseline wipes the staging
 *     area. Each update erases the sketch and the files on these boards.
 */
#pragma once

#include <stdint.h>

#define BSU_MAGIC           0x31555342u     /* "BSU1" */
#define BSU_HEADER_VERSION  1

typedef struct __attribute__((packed)) {
    uint32_t magic;             /* BSU_MAGIC */
    uint8_t  header_version;    /* BSU_HEADER_VERSION */
    uint8_t  reserved0;
    uint16_t chip_id;           /* esp_chip_id_t of the chip it is built for */
    uint32_t raw_size;          /* app image bytes */
    uint32_t comp_size;         /* deflate bytes after this header */
    uint8_t  raw_sha256[32];    /* SHA-256 of the app image */
    uint8_t  comp_sha256[32];   /* SHA-256 of the deflate bytes (checked before staging is committed) */
    char     target[32];        /* BL_TARGET_NAME, NUL-padded */
    char     version[16];       /* BASELINE_VERSION, NUL-padded */
} bsu_header_t;

_Static_assert(sizeof(bsu_header_t) == 128, "bsu_header_t is 128 bytes");

/* ---- Staging (4 MB boards) ------------------------------------------------
 * The staging area starts at the sketch_a partition and runs to the start of
 * the coredump partition (sketch_a, sketch_b and userfs are contiguous). Its
 * first 4 KB sector holds a bsu_stage_t; the package follows at
 * BSU_STAGE_PKG_OFFSET. Baseline writes the record last, after checking the
 * package. The bootloader inflates the package while state is
 * BSU_STAGE_READY, clearing one bit of attempts per try (so a power cut
 * mid-copy simply starts the copy again), and erases the record when the
 * copied image's SHA-256 matches. */
#define BSU_STAGE_MAGIC         0x47545342u     /* "BSTG" */
#define BSU_STAGE_READY         0x59444552u     /* "REDY" */
#define BSU_STAGE_PKG_OFFSET    0x1000u
#define BSU_STAGE_ATTEMPTS      3

typedef struct __attribute__((packed)) {
    uint32_t magic;             /* BSU_STAGE_MAGIC */
    uint32_t state;             /* BSU_STAGE_READY once the package is complete and checked */
    uint32_t attempts;          /* 0xFFFFFFFF; the bootloader clears one bit per copy attempt */
    uint32_t pkg_size;          /* bsu_header_t + deflate bytes, at BSU_STAGE_PKG_OFFSET */
    uint32_t dest_addr;         /* flash address of the Baseline app partition */
    uint32_t dest_size;         /* its size */
} bsu_stage_t;

/* Labels in the partition table. */
#define BSU_LABEL_STAGE_START   "sketch_a"      /* staging starts here */
#define BSU_LABEL_STAGE_END     "coredump"      /* and ends where this starts */
