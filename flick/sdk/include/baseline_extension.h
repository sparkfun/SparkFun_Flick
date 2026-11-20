/*
 * baseline_extension.h - Baseline extensions (docs/EXTENSIONS.md).
 *
 * An extension is a folder of extra Baseline source that a project keeps in
 * its own repository and builds into its own Baseline:
 *
 *     python flick.py baseline build -t <target> --ext path/to/<name>
 *
 * The extension registers what sketches may use, usually a const struct of
 * function pointers, under a name:
 *
 *     static const my_api_t api = { my_begin, my_read };
 *     BL_EXTENSION("my_project.thing", &api);
 *
 * and a sketch finds it with bl_ext_find("my_project.thing"), which returns
 * NULL on a Baseline built without the extension. The struct's layout is the
 * project's own interface: keep it in a header both sides include, and only
 * add to its end.
 */
#ifndef BASELINE_EXTENSION_H
#define BASELINE_EXTENSION_H

typedef struct bl_ext_entry {
    const char *name;
    void *ptr;
    struct bl_ext_entry *next;
} bl_ext_entry_t;

#ifdef __cplusplus
extern "C" {
#endif
/* Baseline side: add an entry (BL_EXTENSION does this before Baseline starts). */
void bl_ext_register(bl_ext_entry_t *entry);
#ifdef __cplusplus
}
#endif

#define BL_EXT_CAT2_(a, b) a##b
#define BL_EXT_CAT_(a, b) BL_EXT_CAT2_(a, b)
/* Register ptr under name (a string literal). At file scope in extension source. */
#define BL_EXTENSION(name, ptr)                                                              \
    static bl_ext_entry_t BL_EXT_CAT_(bl_ext_entry_, __LINE__) = {name, (void *)(ptr), 0};  \
    __attribute__((constructor)) static void BL_EXT_CAT_(bl_ext_register_, __LINE__)(void) \
    {                                                                                        \
        bl_ext_register(&BL_EXT_CAT_(bl_ext_entry_, __LINE__));                              \
    }

#endif
