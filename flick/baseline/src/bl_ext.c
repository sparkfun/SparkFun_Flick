/*
 * bl_ext.c - the registry of Baseline extensions (ext_find, ABI 1.10;
 * sdk/include/baseline_extension.h, docs/EXTENSIONS.md). Shared by every
 * builder: flick stages it for Teensy and RP2 too.
 */
#include <stddef.h>
#include <string.h>
#include "baseline_extension.h"

static bl_ext_entry_t *s_entries;

/* Runs from static constructors, before Baseline starts: nothing else runs yet. */
void bl_ext_register(bl_ext_entry_t *entry)
{
    entry->next = s_entries;
    s_entries = entry;
}

void *bl_impl_ext_find(const char *name)
{
    if (!name) {
        return NULL;
    }
    for (bl_ext_entry_t *e = s_entries; e; e = e->next) {
        if (strcmp(e->name, name) == 0) {
            return e->ptr;
        }
    }
    return NULL;
}
