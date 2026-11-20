/*
 * baseline_idf.h - ESP-IDF names for sketches on the ESP32 family (ABI 1.10
 * idf_* entries): error codes, heap_caps_*, esp_read_mac, the task watchdog,
 * the partition table and RTC_NOINIT_ATTR. Included by baseline.h, as
 * Arduino-ESP32's Arduino.h makes them visible; <esp_mac.h>, <esp_heap_caps.h>,
 * <esp_partition.h>, <esp_task_wdt.h> and <esp_err.h> include it too.
 *
 * Differences from ESP-IDF:
 *   - esp_partition_* only list the table (no read/write/erase/mmap); the
 *     esp_partition_t pointers stay valid, as in ESP-IDF.
 *   - The sketch's own task is watched by Baseline: esp_task_wdt_add/delete on
 *     it leave it as it is and return ESP_OK, esp_task_wdt_reset feeds it.
 *     disableLoopWDT()/enableLoopWDT() take it off and put it back.
 *   - RTC_NOINIT_ATTR variables keep their value across a restart (not a
 *     power cycle, not deep sleep) in the sketch's RAM, not in RTC memory.
 *     Only the same sketch reads them back: check a magic value first.
 */
#ifndef BASELINE_IDF_H
#define BASELINE_IDF_H

#if defined(BL_PLATFORM_ESP32)

#ifdef __cplusplus
extern "C" {
#endif

/* ---- esp_err.h ---- */
#ifndef ESP_OK
typedef int esp_err_t;
#define ESP_OK                  0
#endif
#ifndef ESP_FAIL
#define ESP_FAIL                (-1)
#endif
#ifndef ESP_ERR_NO_MEM
#define ESP_ERR_NO_MEM          0x101
#endif
#ifndef ESP_ERR_INVALID_ARG
#define ESP_ERR_INVALID_ARG     0x102
#endif
#ifndef ESP_ERR_INVALID_STATE
#define ESP_ERR_INVALID_STATE   0x103
#endif
#ifndef ESP_ERR_INVALID_SIZE
#define ESP_ERR_INVALID_SIZE    0x104
#endif
#ifndef ESP_ERR_NOT_FOUND
#define ESP_ERR_NOT_FOUND       0x105
#endif
#ifndef ESP_ERR_NOT_SUPPORTED
#define ESP_ERR_NOT_SUPPORTED   0x106
#endif
#ifndef ESP_ERR_TIMEOUT
#define ESP_ERR_TIMEOUT         0x107
#endif
#ifndef ESP_ERR_INVALID_RESPONSE
#define ESP_ERR_INVALID_RESPONSE 0x108
#endif
#ifndef ESP_ERR_INVALID_CRC
#define ESP_ERR_INVALID_CRC     0x109
#endif
#ifndef ESP_ERR_INVALID_VERSION
#define ESP_ERR_INVALID_VERSION 0x10A
#endif
#ifndef ESP_ERR_INVALID_MAC
#define ESP_ERR_INVALID_MAC     0x10B
#endif
#ifndef ESP_ERR_NOT_FINISHED
#define ESP_ERR_NOT_FINISHED    0x10C
#endif
#ifndef ESP_ERR_NOT_ALLOWED
#define ESP_ERR_NOT_ALLOWED     0x10D
#endif
/* esp_err_to_name() comes from baseline_api.h. */
#define ESP_ERROR_CHECK(x) do { esp_err_t err_rc_ = (x); \
    if (err_rc_ != ESP_OK) { bl_printf("ESP_ERROR_CHECK failed: %s at %s:%d\n", esp_err_to_name(err_rc_), __FILE__, __LINE__); bl_restart(); } } while (0)
#define ESP_ERROR_CHECK_WITHOUT_ABORT(x) (x)

/* ---- esp_heap_caps.h (heap_caps_malloc/calloc/realloc and
 *      heap_caps_malloc_extmem_enable come from baseline_api.h) ---- */
#define MALLOC_CAP_EXEC         (1u << 0)
#define MALLOC_CAP_32BIT        (1u << 1)
#define MALLOC_CAP_8BIT         (1u << 2)
#define MALLOC_CAP_DMA          (1u << 3)
#define MALLOC_CAP_SPIRAM       (1u << 10)
#define MALLOC_CAP_INTERNAL     (1u << 11)
#define MALLOC_CAP_DEFAULT      (1u << 12)
#define MALLOC_CAP_IRAM_8BIT    (1u << 13)
#define MALLOC_CAP_RETENTION    (1u << 14)
#define MALLOC_CAP_RTCRAM       (1u << 15)
#define MALLOC_CAP_INVALID      (1u << 31)

static inline void heap_caps_free(void *ptr) { bl_free(ptr); }
static inline size_t heap_caps_get_free_size(uint32_t caps) { return bl_idf_heap_info(caps, BL_HEAP_FREE); }
static inline size_t heap_caps_get_largest_free_block(uint32_t caps) { return bl_idf_heap_info(caps, BL_HEAP_LARGEST); }
static inline size_t heap_caps_get_minimum_free_size(uint32_t caps) { return bl_idf_heap_info(caps, BL_HEAP_MIN_FREE); }
static inline size_t heap_caps_get_total_size(uint32_t caps) { return bl_idf_heap_info(caps, BL_HEAP_TOTAL); }
static inline bool heap_caps_check_integrity(uint32_t caps, bool print_errors) { return bl_idf_heap_check(caps, print_errors) == 1; }
static inline bool heap_caps_check_integrity_all(bool print_errors) { return bl_idf_heap_check(MALLOC_CAP_INVALID, print_errors) == 1; }
static inline size_t esp_get_free_heap_size(void) { return heap_caps_get_free_size(MALLOC_CAP_DEFAULT); }
static inline size_t esp_get_free_internal_heap_size(void) { return heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL); }
static inline size_t esp_get_minimum_free_heap_size(void) { return heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT); }

/* ---- esp32-hal-psram.h (psramFound is in baseline.h; Baseline starts PSRAM) ---- */
static inline bool psramInit(void) { return psramFound(); }

/* ---- esp_mac.h (esp_read_mac comes from baseline_api.h) ---- */
typedef enum {
    ESP_MAC_WIFI_STA,
    ESP_MAC_WIFI_SOFTAP,
    ESP_MAC_BT,
    ESP_MAC_ETH,
    ESP_MAC_IEEE802154,
    ESP_MAC_BASE,
    ESP_MAC_EFUSE_FACTORY,
    ESP_MAC_EFUSE_CUSTOM,
    ESP_MAC_EFUSE_EXT,
} esp_mac_type_t;

/* ---- esp_task_wdt.h ---- */
static inline esp_err_t esp_task_wdt_add(void *task) { return bl_idf_task_wdt(BL_TWDT_ADD, task); }
static inline esp_err_t esp_task_wdt_delete(void *task) { return bl_idf_task_wdt(BL_TWDT_DELETE, task); }
static inline esp_err_t esp_task_wdt_status(void *task) { return bl_idf_task_wdt(BL_TWDT_STATUS, task); }
static inline esp_err_t esp_task_wdt_reset(void) { return bl_idf_task_wdt(BL_TWDT_RESET, NULL); }

/* Arduino-ESP32's loop() watchdog calls. Flick watches loop() from the start
 * (a new sketch that hangs rolls back); a sketch whose loop() can block for
 * more than 5 s calls disableLoopWDT() in setup(). */
static inline void disableLoopWDT(void) { bl_idf_task_wdt(BL_TWDT_LOOP_OFF, NULL); }
static inline void enableLoopWDT(void) { bl_idf_task_wdt(BL_TWDT_LOOP_ON, NULL); }
static inline void feedLoopWDT(void) { bl_idf_task_wdt(BL_TWDT_RESET, NULL); }

/* ---- esp_partition.h (sdk/src/sketch_idf.cpp) ---- */
typedef enum {
    ESP_PARTITION_TYPE_APP = 0x00,
    ESP_PARTITION_TYPE_DATA = 0x01,
    ESP_PARTITION_TYPE_BOOTLOADER = 0x02,
    ESP_PARTITION_TYPE_PARTITION_TABLE = 0x03,
    ESP_PARTITION_TYPE_ANY = 0xff,
} esp_partition_type_t;

typedef enum {
    ESP_PARTITION_SUBTYPE_APP_FACTORY = 0x00,
    ESP_PARTITION_SUBTYPE_APP_OTA_MIN = 0x10,
    ESP_PARTITION_SUBTYPE_APP_OTA_0 = 0x10,
    ESP_PARTITION_SUBTYPE_APP_OTA_1 = 0x11,
    ESP_PARTITION_SUBTYPE_APP_OTA_MAX = 0x20,
    ESP_PARTITION_SUBTYPE_APP_TEST = 0x20,
    ESP_PARTITION_SUBTYPE_DATA_OTA = 0x00,
    ESP_PARTITION_SUBTYPE_DATA_PHY = 0x01,
    ESP_PARTITION_SUBTYPE_DATA_NVS = 0x02,
    ESP_PARTITION_SUBTYPE_DATA_COREDUMP = 0x03,
    ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS = 0x04,
    ESP_PARTITION_SUBTYPE_DATA_EFUSE_EM = 0x05,
    ESP_PARTITION_SUBTYPE_DATA_UNDEFINED = 0x06,
    ESP_PARTITION_SUBTYPE_DATA_ESPHTTPD = 0x80,
    ESP_PARTITION_SUBTYPE_DATA_FAT = 0x81,
    ESP_PARTITION_SUBTYPE_DATA_SPIFFS = 0x82,
    ESP_PARTITION_SUBTYPE_DATA_LITTLEFS = 0x83,
    ESP_PARTITION_SUBTYPE_ANY = 0xff,
} esp_partition_subtype_t;

typedef struct {
    const void *flash_chip;             /* always NULL */
    esp_partition_type_t type;
    esp_partition_subtype_t subtype;
    uint32_t address;
    uint32_t size;
    uint32_t erase_size;
    char label[17];
    bool encrypted;
    bool readonly;
} esp_partition_t;

typedef struct esp_partition_iterator_opaque_ *esp_partition_iterator_t;

esp_partition_iterator_t esp_partition_find(esp_partition_type_t type, esp_partition_subtype_t subtype, const char *label);
const esp_partition_t *esp_partition_find_first(esp_partition_type_t type, esp_partition_subtype_t subtype, const char *label);
const esp_partition_t *esp_partition_get(esp_partition_iterator_t iterator);
esp_partition_iterator_t esp_partition_next(esp_partition_iterator_t iterator);
void esp_partition_iterator_release(esp_partition_iterator_t iterator);

#ifdef __cplusplus
}
#endif

/* ---- esp_attr.h ---- */
#ifndef RTC_NOINIT_ATTR
#define RTC_NOINIT_ATTR __attribute__((section(".noinit")))     /* sdk/ld/sketch_esp.ld.in */
#endif
#ifndef NOINIT_ATTR
#define NOINIT_ATTR RTC_NOINIT_ATTR
#endif

#endif /* BL_PLATFORM_ESP32 */

#endif /* BASELINE_IDF_H */
