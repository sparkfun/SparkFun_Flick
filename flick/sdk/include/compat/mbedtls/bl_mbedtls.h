/*
 * bl_mbedtls.h - the part of mbedTLS a sketch can use: checking that a
 * certificate or a private key parses, with Baseline's own mbedTLS (ABI 1.10
 * idf_tls_check). TLS connections go through WiFiClientSecure / HTTPClient.
 *
 * The contexts are placeholders: after a successful parse the certificate's or
 * key's fields can't be read. mbedtls_ctr_drbg_random fills with hardware
 * random numbers (no seeding needed). <mbedtls/ssl.h>, <mbedtls/x509_crt.h>,
 * <mbedtls/pk.h>, <mbedtls/ctr_drbg.h> and <mbedtls/entropy.h> lead here.
 */
#pragma once
#include "baseline.h"
#if !defined(BL_PLATFORM_ESP32)
#error "This board has no mbedTLS (it is an ESP32 API)."
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define MBEDTLS_ERR_X509_BAD_INPUT_DATA      -0x2800
#define MBEDTLS_ERR_PK_BAD_INPUT_DATA        -0x3E80
#define MBEDTLS_ERR_PK_FEATURE_UNAVAILABLE   -0x3980

typedef struct { int parsed; } mbedtls_x509_crt;
typedef struct { int parsed; } mbedtls_pk_context;
typedef struct { int unused; } mbedtls_ctr_drbg_context;
typedef struct { int unused; } mbedtls_entropy_context;

static inline void mbedtls_x509_crt_init(mbedtls_x509_crt *crt) { crt->parsed = 0; }
static inline void mbedtls_x509_crt_free(mbedtls_x509_crt *crt) { crt->parsed = 0; }
/* buflen counts the terminating NUL of a PEM certificate, as in mbedTLS. */
static inline int mbedtls_x509_crt_parse(mbedtls_x509_crt *crt, const unsigned char *buf, size_t buflen)
{
    int r = buf && buflen ? bl_idf_tls_check(BL_TLS_CERT, buf, buflen) : MBEDTLS_ERR_X509_BAD_INPUT_DATA;
    crt->parsed = r == 0;
    return r;
}

static inline void mbedtls_pk_init(mbedtls_pk_context *ctx) { ctx->parsed = 0; }
static inline void mbedtls_pk_free(mbedtls_pk_context *ctx) { ctx->parsed = 0; }
/* Keys with a password are not supported (MBEDTLS_ERR_PK_FEATURE_UNAVAILABLE). */
static inline int mbedtls_pk_parse_key(mbedtls_pk_context *ctx, const unsigned char *key, size_t keylen,
                                       const unsigned char *pwd, size_t pwdlen,
                                       int (*f_rng)(void *, unsigned char *, size_t), void *p_rng)
{
    if (pwd && pwdlen) {
        return MBEDTLS_ERR_PK_FEATURE_UNAVAILABLE;
    }
    int r = key && keylen ? bl_idf_tls_check(BL_TLS_KEY, key, keylen) : MBEDTLS_ERR_PK_BAD_INPUT_DATA;
    ctx->parsed = r == 0;
    return r;
}

static inline void mbedtls_entropy_init(mbedtls_entropy_context *ctx) {}
static inline void mbedtls_entropy_free(mbedtls_entropy_context *ctx) {}
static inline int mbedtls_entropy_func(void *data, unsigned char *output, size_t len)
{
    for (size_t i = 0; i < len; i += 4) {
        uint32_t r = bl_random();
        for (size_t j = 0; j < 4 && i + j < len; j++) {
            output[i + j] = (unsigned char)(r >> (8 * j));
        }
    }
    return 0;
}

static inline void mbedtls_ctr_drbg_init(mbedtls_ctr_drbg_context *ctx) {}
static inline void mbedtls_ctr_drbg_free(mbedtls_ctr_drbg_context *ctx) {}
static inline int mbedtls_ctr_drbg_seed(mbedtls_ctr_drbg_context *ctx, int (*f_entropy)(void *, unsigned char *, size_t),
                                        void *p_entropy, const unsigned char *custom, size_t len) { return 0; }
static inline int mbedtls_ctr_drbg_random(void *p_rng, unsigned char *output, size_t output_len)
{
    return mbedtls_entropy_func(p_rng, output, output_len);
}

#ifdef __cplusplus
}
#endif
