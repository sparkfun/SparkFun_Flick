/*
 * ext_demo_api.h - the interface of the ext_demo Baseline extension, shared by
 * the extension and the sketches that use it (sketch.cfg: extensions = ../ext_demo).
 * Only add members at the end: sketches built for an older layout keep working.
 */
#ifndef EXT_DEMO_API_H
#define EXT_DEMO_API_H
#include <stdint.h>

#define EXT_DEMO_NAME "flick.ext_demo"

typedef struct {
    uint32_t version;                       /* 1 */
    int (*add)(int a, int b);
    const char *(*platform)(void);          /* text from code that runs inside Baseline */
    uint32_t (*internal_free)(void);        /* ESP32: heap_caps_get_free_size(MALLOC_CAP_INTERNAL); else 0 */
} ext_demo_api_t;

#endif
