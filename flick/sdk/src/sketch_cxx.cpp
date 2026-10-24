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

/* ---- The C++ standard library without exceptions ----
 * libstdc++'s containers (std::string, std::map, std::vector, ...) report
 * errors by calling these. Sketches have no exceptions, so each one logs the
 * error and stops the sketch with a crash; Baseline then rolls back or enters
 * safe mode as for any other crash. Defining them here also keeps libstdc++'s
 * exception runtime (and its locks) out of the sketch. */
static void __attribute__((noreturn)) cxx_fatal(const char *what, const char *msg)
{
    bl_log(BL_LOG_ERROR, "sketch", "C++ error: %s%s%s", what, msg ? ": " : "", msg ? msg : "");
    delay(50);              // let the message out
    __builtin_trap();
}

namespace std {
void __throw_bad_alloc() { cxx_fatal("out of memory", nullptr); }
void __throw_bad_array_new_length() { cxx_fatal("bad array new length", nullptr); }
void __throw_bad_cast() { cxx_fatal("bad cast", nullptr); }
void __throw_bad_typeid() { cxx_fatal("bad typeid", nullptr); }
void __throw_bad_function_call() { cxx_fatal("bad function call", nullptr); }
void __throw_logic_error(const char *s) { cxx_fatal("logic error", s); }
void __throw_domain_error(const char *s) { cxx_fatal("domain error", s); }
void __throw_invalid_argument(const char *s) { cxx_fatal("invalid argument", s); }
void __throw_length_error(const char *s) { cxx_fatal("length error", s); }
void __throw_out_of_range(const char *s) { cxx_fatal("out of range", s); }
void __throw_out_of_range_fmt(const char *s, ...) { cxx_fatal("out of range", s); }
void __throw_runtime_error(const char *s) { cxx_fatal("runtime error", s); }
void __throw_range_error(const char *s) { cxx_fatal("range error", s); }
void __throw_overflow_error(const char *s) { cxx_fatal("overflow error", s); }
void __throw_underflow_error(const char *s) { cxx_fatal("underflow error", s); }
void __throw_system_error(int e) { cxx_fatal("system error", nullptr); }
}  // namespace std

extern "C" void abort(void)
{
    cxx_fatal("abort()", nullptr);
}
