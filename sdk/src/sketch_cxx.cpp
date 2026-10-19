/*
 * sketch_cxx.cpp - minimal C++ runtime for .ino/.cpp sketches.
 * Linked automatically when a sketch has C++ sources. No exceptions or RTTI.
 */
#include <stddef.h>
#include "baseline.h"

void *operator new(size_t n) { return bl_malloc(n); }
void *operator new[](size_t n) { return bl_malloc(n); }
void operator delete(void *p) noexcept { bl_free(p); }
void operator delete[](void *p) noexcept { bl_free(p); }
void operator delete(void *p, size_t) noexcept { bl_free(p); }
void operator delete[](void *p, size_t) noexcept { bl_free(p); }

extern "C" {

void *__dso_handle = 0;

/* Global destructors: a sketch never exits, so there is nothing to register. */
int __cxa_atexit(void (*)(void *), void *, void *)
{
    return 0;
}

void __cxa_pure_virtual(void)
{
    bl_log(BL_LOG_ERROR, "sketch", "pure virtual function called");
    bl_restart();
}

}
