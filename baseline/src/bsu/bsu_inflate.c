/*
 * bsu_inflate.c - raw deflate (RFC 1951) decoder with its window in flash.
 * Canonical-Huffman decoding in the style of tinf (Joergen Ibsen); see
 * bsu_inflate.h for the interface.
 */
#include <string.h>
#include "bsu_inflate.h"

static const uint16_t len_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t len_bits[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t dist_bits[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
static const uint8_t clen_order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

/* ---- input ---- */

static int next_byte(bsu_inflate_t *s)
{
    if (s->in_pos == s->in_len) {
        int n = s->io->read_in(s->io->ctx, s->in, sizeof(s->in));
        if (n <= 0) {
            if (!s->err) {
                s->err = BSU_ERR_INPUT;
            }
            return 0;
        }
        s->in_len = (uint32_t)n;
        s->in_pos = 0;
    }
    return s->in[s->in_pos++];
}

static int get_bit(bsu_inflate_t *s)
{
    if (!s->bits--) {
        s->tag = (uint32_t)next_byte(s);
        s->bits = 7;
    }
    int bit = s->tag & 1;
    s->tag >>= 1;
    return bit;
}

static uint32_t get_bits(bsu_inflate_t *s, int n)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++) {
        v |= (uint32_t)get_bit(s) << i;
    }
    return v;
}

/* ---- output ---- */

static void put_byte(bsu_inflate_t *s, uint8_t b)
{
    if (s->err) {
        return;
    }
    if (s->out_base + s->out_fill >= s->out_limit) {
        s->err = BSU_ERR_SIZE;
        return;
    }
    s->out[s->out_fill++] = b;
    if (s->out_fill == BSU_BLOCK) {
        if (s->io->write_out(s->io->ctx, s->out_base, s->out, BSU_BLOCK) < 0) {
            s->err = BSU_ERR_OUTPUT;
            return;
        }
        s->out_base += BSU_BLOCK;
        s->out_fill = 0;
    }
}

/* Copy len bytes from dist bytes back. Bytes still in the block buffer come
 * from there, older ones from flash. */
static void copy_back(bsu_inflate_t *s, uint32_t dist, uint32_t len)
{
    while (len && !s->err) {
        uint32_t pos = s->out_base + s->out_fill;
        uint32_t src = pos - dist;
        if (src >= s->out_base) {
            put_byte(s, s->out[src - s->out_base]);
            len--;
            continue;
        }
        uint32_t n = s->out_base - src;
        if (n > len) {
            n = len;
        }
        if (n > sizeof(s->copy)) {
            n = sizeof(s->copy);
        }
        if (s->io->read_back(s->io->ctx, src, s->copy, n) < 0) {
            s->err = BSU_ERR_OUTPUT;
            return;
        }
        for (uint32_t i = 0; i < n; i++) {
            put_byte(s, s->copy[i]);
        }
        len -= n;
    }
}

/* ---- Huffman trees ---- */

static int build_tree(bsu_tree_t *t, const uint8_t *lengths, int num)
{
    uint16_t offs[16];
    memset(t->counts, 0, sizeof(t->counts));
    for (int i = 0; i < num; i++) {
        t->counts[lengths[i]]++;
    }
    t->counts[0] = 0;
    int left = 1;       // reject over-subscribed code sets
    for (int i = 1; i < 16; i++) {
        left <<= 1;
        left -= t->counts[i];
        if (left < 0) {
            return BSU_ERR_DATA;
        }
    }
    uint16_t sum = 0;
    for (int i = 0; i < 16; i++) {
        offs[i] = sum;
        sum += t->counts[i];
    }
    for (int i = 0; i < num; i++) {
        if (lengths[i]) {
            t->symbols[offs[lengths[i]]++] = (uint16_t)i;
        }
    }
    return BSU_OK;
}

static int decode_symbol(bsu_inflate_t *s, const bsu_tree_t *t)
{
    int sum = 0, cur = 0, len = 0;
    do {
        cur = 2 * cur + get_bit(s);
        if (++len == 16) {
            s->err = BSU_ERR_DATA;
            return 0;
        }
        sum += t->counts[len];
        cur -= t->counts[len];
    } while (cur >= 0);
    return t->symbols[sum + cur];
}

static void fixed_trees(bsu_inflate_t *s)
{
    uint8_t lengths[288];
    memset(lengths, 8, 144);
    memset(lengths + 144, 9, 112);
    memset(lengths + 256, 7, 24);
    memset(lengths + 280, 8, 8);
    build_tree(&s->lt, lengths, 288);
    memset(lengths, 5, 30);
    build_tree(&s->dt, lengths, 30);
}

static int dynamic_trees(bsu_inflate_t *s)
{
    uint8_t lengths[288 + 32];
    int hlit = (int)get_bits(s, 5) + 257;
    int hdist = (int)get_bits(s, 5) + 1;
    int hclen = (int)get_bits(s, 4) + 4;
    if (hlit > 286 || hdist > 30) {
        return BSU_ERR_DATA;
    }
    memset(lengths, 0, 19);
    for (int i = 0; i < hclen; i++) {
        lengths[clen_order[i]] = (uint8_t)get_bits(s, 3);
    }
    if (build_tree(&s->lt, lengths, 19) != BSU_OK) {    // the code-length tree, briefly in lt
        return BSU_ERR_DATA;
    }
    int num = hlit + hdist;
    for (int i = 0; i < num;) {
        int sym = decode_symbol(s, &s->lt);
        if (s->err) {
            return s->err;
        }
        uint8_t fill = 0;
        int rep;
        if (sym < 16) {
            lengths[i++] = (uint8_t)sym;
            continue;
        } else if (sym == 16) {
            if (i == 0) {
                return BSU_ERR_DATA;
            }
            fill = lengths[i - 1];
            rep = 3 + (int)get_bits(s, 2);
        } else if (sym == 17) {
            rep = 3 + (int)get_bits(s, 3);
        } else {
            rep = 11 + (int)get_bits(s, 7);
        }
        if (i + rep > num) {
            return BSU_ERR_DATA;
        }
        memset(lengths + i, fill, (size_t)rep);
        i += rep;
    }
    if (lengths[256] == 0) {
        return BSU_ERR_DATA;    // no end-of-block code
    }
    if (build_tree(&s->lt, lengths, hlit) != BSU_OK || build_tree(&s->dt, lengths + hlit, hdist) != BSU_OK) {
        return BSU_ERR_DATA;
    }
    return BSU_OK;
}

/* ---- blocks ---- */

static int huffman_block(bsu_inflate_t *s)
{
    for (;;) {
        int sym = decode_symbol(s, &s->lt);
        if (s->err) {
            return s->err;
        }
        if (sym < 256) {
            put_byte(s, (uint8_t)sym);
            continue;
        }
        if (sym == 256) {
            return BSU_OK;
        }
        sym -= 257;
        if (sym >= 29) {
            return BSU_ERR_DATA;
        }
        uint32_t len = len_base[sym] + get_bits(s, len_bits[sym]);
        int dsym = decode_symbol(s, &s->dt);
        if (s->err) {
            return s->err;
        }
        if (dsym >= 30) {
            return BSU_ERR_DATA;
        }
        uint32_t dist = dist_base[dsym] + get_bits(s, dist_bits[dsym]);
        if (dist > s->out_base + s->out_fill) {
            return BSU_ERR_DATA;
        }
        copy_back(s, dist, len);
        if (s->err) {
            return s->err;
        }
    }
}

static int stored_block(bsu_inflate_t *s)
{
    s->bits = 0;    // skip to a byte boundary
    uint32_t len = (uint32_t)next_byte(s);
    len |= (uint32_t)next_byte(s) << 8;
    uint32_t nlen = (uint32_t)next_byte(s);
    nlen |= (uint32_t)next_byte(s) << 8;
    if (s->err) {
        return s->err;
    }
    if (len != (~nlen & 0xFFFF)) {
        return BSU_ERR_DATA;
    }
    while (len-- && !s->err) {
        put_byte(s, (uint8_t)next_byte(s));
    }
    return s->err;
}

int bsu_inflate(bsu_inflate_t *s, const bsu_inflate_io_t *io, uint32_t raw_size)
{
    memset(s, 0, sizeof(*s));
    s->io = io;
    s->out_limit = raw_size;
    int last;
    do {
        last = get_bit(s);
        int type = (int)get_bits(s, 2);
        int r;
        if (s->err) {
            return s->err;
        }
        if (type == 0) {
            r = stored_block(s);
        } else if (type == 1) {
            fixed_trees(s);
            r = huffman_block(s);
        } else if (type == 2) {
            r = dynamic_trees(s);
            if (r == BSU_OK) {
                r = huffman_block(s);
            }
        } else {
            r = BSU_ERR_DATA;
        }
        if (r != BSU_OK) {
            return r;
        }
    } while (!last);
    if (s->out_base + s->out_fill != raw_size) {
        return BSU_ERR_SIZE;
    }
    if (s->out_fill && io->write_out(io->ctx, s->out_base, s->out, s->out_fill) < 0) {
        return BSU_ERR_OUTPUT;
    }
    return BSU_OK;
}
