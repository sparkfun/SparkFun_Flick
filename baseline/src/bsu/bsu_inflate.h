/*
 * bsu_inflate.h - raw deflate decoder for Baseline update packages.
 *
 * Writes its output a block at a time and reads back-references (deflate
 * reaches up to 32 KB back) from output it has already written, so it needs
 * only ~6 KB of RAM: small enough for the second-stage bootloader. Used by
 * Baseline and by the 4 MB boards' bootloader hook; plain C, no allocation.
 */
#pragma once

#include <stdint.h>

#define BSU_BLOCK   4096        /* output is handed to write_out in blocks of this size (one flash sector) */

typedef struct {
    /* Up to n compressed bytes into buf. Returns the count (> 0), or <= 0 for
     * end of input or an error. */
    int (*read_in)(void *ctx, uint8_t *buf, uint32_t n);
    /* A block of output at output offset off (a multiple of BSU_BLOCK); len is
     * BSU_BLOCK except for the last block. Returns 0, or < 0 to stop. */
    int (*write_out)(void *ctx, uint32_t off, const uint8_t *buf, uint32_t len);
    /* Output already passed to write_out: len bytes at output offset off.
     * Returns 0, or < 0 on error. */
    int (*read_back)(void *ctx, uint32_t off, uint8_t *buf, uint32_t len);
    void *ctx;
} bsu_inflate_io_t;

typedef struct {
    uint16_t counts[16];
    uint16_t symbols[288];
} bsu_tree_t;

/* Working memory, supplied by the caller (static in the bootloader). */
typedef struct {
    const bsu_inflate_io_t *io;
    uint8_t out[BSU_BLOCK];     /* the block being filled */
    uint32_t out_base;          /* output offset of out[0] */
    uint32_t out_fill;
    uint32_t out_limit;         /* expected total output */
    uint8_t in[256];
    uint32_t in_pos, in_len;
    uint32_t tag;               /* bit buffer */
    int bits;
    int err;
    bsu_tree_t lt, dt;          /* literal/length and distance trees */
    uint8_t copy[258];          /* back-reference bytes read from flash */
} bsu_inflate_t;

#define BSU_OK              0
#define BSU_ERR_INPUT      -1   /* compressed data ended early or read_in failed */
#define BSU_ERR_DATA       -2   /* not valid deflate data */
#define BSU_ERR_OUTPUT     -3   /* write_out or read_back failed */
#define BSU_ERR_SIZE       -4   /* output larger or smaller than expected */

#ifdef __cplusplus
extern "C" {
#endif

/* Inflate a raw deflate stream that should produce exactly raw_size bytes. */
int bsu_inflate(bsu_inflate_t *s, const bsu_inflate_io_t *io, uint32_t raw_size);

#ifdef __cplusplus
}
#endif
