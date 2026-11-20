/*
 * sketch_idf.cpp - esp_partition_find/get/next on Baseline's idf_partition
 * entry (baseline_idf.h). The table is read once, into the heap (not the
 * sketch's small RAM window); the esp_partition_t pointers handed out stay
 * valid, as in ESP-IDF.
 */
#include "baseline.h"

#if defined(BL_PLATFORM_ESP32)

#define MAX_PARTITIONS 24

struct esp_partition_iterator_opaque_ {
    esp_partition_type_t type;
    esp_partition_subtype_t subtype;
    const char *label;
    int index;                  /* the entry it points at */
};

static esp_partition_t *s_table;
static int s_count = -1;

static void load_table(void)
{
    if (s_count >= 0) {
        return;
    }
    s_table = (esp_partition_t *)calloc(MAX_PARTITIONS, sizeof(esp_partition_t));
    s_count = 0;
    if (!s_table) {
        return;
    }
    bl_partition_t p;
    while (s_count < MAX_PARTITIONS && bl_idf_partition(s_count, &p) == BL_OK) {
        esp_partition_t *e = &s_table[s_count++];
        e->flash_chip = NULL;
        e->type = (esp_partition_type_t)p.type;
        e->subtype = (esp_partition_subtype_t)p.subtype;
        e->address = p.address;
        e->size = p.size;
        e->erase_size = 4096;
        memcpy(e->label, p.label, sizeof(e->label));
        e->label[sizeof(e->label) - 1] = '\0';
        e->encrypted = p.encrypted != 0;
        e->readonly = false;
    }
}

static bool matches(const esp_partition_iterator_opaque_ *it, const esp_partition_t *p)
{
    return (it->type == ESP_PARTITION_TYPE_ANY || p->type == it->type) &&
           (it->subtype == ESP_PARTITION_SUBTYPE_ANY || p->subtype == it->subtype) &&
           (!it->label || strncmp(p->label, it->label, sizeof(p->label) - 1) == 0);
}

/* Move to the first match at or after index; release the iterator if there is none. */
static esp_partition_iterator_t seek(esp_partition_iterator_t it, int index)
{
    for (; index < s_count; index++) {
        if (matches(it, &s_table[index])) {
            it->index = index;
            return it;
        }
    }
    free(it);
    return NULL;
}

extern "C" esp_partition_iterator_t esp_partition_find(esp_partition_type_t type, esp_partition_subtype_t subtype,
                                                       const char *label)
{
    load_table();
    esp_partition_iterator_t it = (esp_partition_iterator_t)malloc(sizeof(*it));
    if (!it) {
        return NULL;
    }
    it->type = type;
    it->subtype = subtype;
    it->label = label;
    it->index = 0;
    return seek(it, 0);
}

extern "C" const esp_partition_t *esp_partition_find_first(esp_partition_type_t type, esp_partition_subtype_t subtype,
                                                           const char *label)
{
    esp_partition_iterator_t it = esp_partition_find(type, subtype, label);
    if (!it) {
        return NULL;
    }
    const esp_partition_t *p = &s_table[it->index];
    free(it);
    return p;
}

extern "C" const esp_partition_t *esp_partition_get(esp_partition_iterator_t it)
{
    return it ? &s_table[it->index] : NULL;
}

extern "C" esp_partition_iterator_t esp_partition_next(esp_partition_iterator_t it)
{
    return it ? seek(it, it->index + 1) : NULL;
}

extern "C" void esp_partition_iterator_release(esp_partition_iterator_t it)
{
    free(it);
}

#endif /* BL_PLATFORM_ESP32 */
