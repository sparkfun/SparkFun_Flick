// rtos_test.ino - FreeRTOS tasks, mutexes, semaphores and queues in a sketch
// (ESP32 family). One PASS/FAIL line per check, then a RESULT line.
//
//   - tasks on each core (or the only one) share a counter under a mutex
//   - a recursive mutex, a counting semaphore, a queue between two tasks
//   - vTaskDelay timing, task names, stack high-water marks, priorities
//   - xTaskCreateWithCaps: a task stack in PSRAM, given back on return and delete
//   - the sketch's own task waits 6 s on a semaphore (longer than the task
//     watchdog's 5 s): no watchdog reset
//   - Ticker callbacks run only on the sketch's own task while another task
//     calls delay()
//   - last, setup() ends with vTaskDelete(NULL): loop() never runs, a task keeps
//     printing, and `python flick.py info` says "running: loop() ended by vTaskDelete"
#if !defined(BL_HAS_RTOS)
#error "This board has no FreeRTOS."
#endif
#include <Ticker.h>
#include <esp_heap_caps.h>

static int passed, failed;
#define CHECK(name, cond) do { bool ok_ = (cond); Serial.printf("%s  %s\n", ok_ ? "PASS" : "FAIL", name); ok_ ? passed++ : failed++; } while (0)

static const int ROUNDS = 20000;
static SemaphoreHandle_t counterLock, done;
static volatile uint32_t counter;
static volatile int workerCore[2] = {-1, -1};

static void worker(void *arg)
{
    int id = (int)(intptr_t)arg;
    workerCore[id] = xPortGetCoreID();
    for (int i = 0; i < ROUNDS; i++) {
        xSemaphoreTake(counterLock, portMAX_DELAY);
        counter = counter + 1;      // read-modify-write: loses counts without the lock
        xSemaphoreGive(counterLock);
        if ((i & 1023) == 0) {
            vTaskDelay(1);
        }
    }
    xSemaphoreGive(done);
    vTaskDelete(NULL);
}

static QueueHandle_t queue;
static void producer(void *arg)
{
    for (int i = 1; i <= 100; i++) {
        xQueueSend(queue, &i, portMAX_DELAY);
    }
    // returning ends the task
}

static SemaphoreHandle_t lateGive;
static void giveLater(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(6000));
    xSemaphoreGive(lateGive);
}

static Ticker ticker;
static TaskHandle_t sketchTask;
static volatile int tickerCalls, tickerWrongTask;
static void onTick()
{
    tickerCalls++;
    if (xTaskGetCurrentTaskHandle() != sketchTask) {
        tickerWrongTask++;
    }
}
static volatile bool delayerDone;
static void delayer(void *arg)
{
    for (int i = 0; i < 100; i++) {
        delay(5);
    }
    delayerDone = true;
    vTaskDelete(NULL);
}

// Tasks with their stack in PSRAM (xTaskCreateWithCaps).
static SemaphoreHandle_t capsReady, capsGo;
static volatile bool capsRan;
static void capsWorker(void *arg)
{
    volatile char big[2048];        // something on the stack
    for (int i = 0; i < (int)sizeof(big); i++) {
        big[i] = (char)i;
    }
    capsRan = big[100] == 100;
    xSemaphoreGive(capsReady);
    xSemaphoreTake(capsGo, portMAX_DELAY);
    // returning ends the task
}
static void capsIdle(void *arg)
{
    for (;;) {
        vTaskDelay(100);
    }
}
static long psramFree() { return (long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }
static long internalFree() { return (long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); }

static void survivor(void *arg)
{
    for (int i = 1; i <= 10; i++) {
        vTaskDelay(1000);
        Serial.printf("survivor task: %d s after setup() deleted the sketch task\n", i);
    }
    Serial.println("PASS  sketch task deleted, other task still runs (no watchdog reset)");
    Serial.println("--- rtos_test done");
    vTaskDelete(NULL);
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("--- rtos_test");
    sketchTask = xTaskGetCurrentTaskHandle();
    int cores = bl_task_info(BL_TASK_INFO_CORES);

    // Two workers fighting over one mutex.
    counterLock = xSemaphoreCreateMutex();
    done = xSemaphoreCreateCounting(2, 0);
    CHECK("create mutex and counting semaphore", counterLock && done);
    UBaseType_t before = uxTaskGetNumberOfTasks();
    TaskHandle_t w0 = NULL, w1 = NULL;
    bool made = xTaskCreatePinnedToCore(worker, "worker0", 4096, (void *)0, 1, &w0, 0) == pdPASS &&
                xTaskCreatePinnedToCore(worker, "worker1", 4096, (void *)1, 1, &w1, cores > 1 ? 1 : 0) == pdPASS;
    CHECK("create two tasks", made && w0 && w1);
    CHECK("task count went up by 2", uxTaskGetNumberOfTasks() == before + 2);
    CHECK("task name", w0 && strcmp(pcTaskGetName(w0), "worker0") == 0);
    bool both = xSemaphoreTake(done, pdMS_TO_TICKS(4000)) && xSemaphoreTake(done, pdMS_TO_TICKS(4000));
    CHECK("both tasks finished", both);
    Serial.printf("      counter %lu (expected %d), workers on cores %d and %d\n",
                  (unsigned long)counter, 2 * ROUNDS, workerCore[0], workerCore[1]);
    CHECK("mutex kept the counter exact", counter == 2 * ROUNDS);
    CHECK("tasks pinned to the asked cores", workerCore[0] == 0 && workerCore[1] == (cores > 1 ? 1 : 0));

    // Recursive mutex.
    SemaphoreHandle_t rec = xSemaphoreCreateRecursiveMutex();
    bool r = xSemaphoreTakeRecursive(rec, 0) && xSemaphoreTakeRecursive(rec, 0);
    r = r && xSemaphoreGiveRecursive(rec) && xSemaphoreGiveRecursive(rec) && !xSemaphoreGiveRecursive(rec);
    CHECK("recursive mutex", r);
    vSemaphoreDelete(rec);

    // A mutex can't be given back by someone who doesn't hold it, or twice.
    SemaphoreHandle_t m = xSemaphoreCreateMutex();
    CHECK("mutex: take, count, give", xSemaphoreTake(m, 0) && uxSemaphoreGetCount(m) == 0 &&
                                      !xSemaphoreTake(m, 10) && xSemaphoreGive(m) && uxSemaphoreGetCount(m) == 1);
    vSemaphoreDelete(m);

    // Queue: a producer task, the sketch task receives in order.
    queue = xQueueCreate(8, sizeof(int));
    TaskHandle_t p = NULL;
    xTaskCreate(producer, "producer", 3072, NULL, 2, &p);
    bool inOrder = true;
    int got = 0;
    for (int i = 1; i <= 100; i++) {
        int v = 0;
        if (!xQueueReceive(queue, &v, pdMS_TO_TICKS(1000)) || v != i) {
            inOrder = false;
            break;
        }
        got++;
    }
    CHECK("queue: 100 items in order from another task", inOrder && got == 100);
    int v = 0;
    CHECK("queue: empty receive times out", !xQueueReceive(queue, &v, 20) && uxQueueMessagesWaiting(queue) == 0);
    int one = 7, two = 8;
    CHECK("queue: send to front, peek", xQueueSend(queue, &one, 0) && xQueueSendToFront(queue, &two, 0) &&
                                        xQueuePeek(queue, &v, 0) && v == 8 && uxQueueMessagesWaiting(queue) == 2);
    CHECK("queue: spaces and reset", uxQueueSpacesAvailable(queue) == 6 && xQueueReset(queue) &&
                                     uxQueueMessagesWaiting(queue) == 0);
    vQueueDelete(queue);

    // Timing, stack, priority.
    uint32_t t0 = millis();
    vTaskDelay(pdMS_TO_TICKS(100));
    uint32_t dt = millis() - t0;
    CHECK("vTaskDelay(100) takes 100 ms", dt >= 99 && dt <= 110);
    TickType_t last = xTaskGetTickCount();
    for (int i = 0; i < 5; i++) {
        vTaskDelayUntil(&last, 20);
    }
    CHECK("vTaskDelayUntil keeps the period", xTaskGetTickCount() - last <= 2);
    UBaseType_t freeStack = uxTaskGetStackHighWaterMark(NULL);
    Serial.printf("      sketch task: %s, least free stack %u bytes, priority %u, core %d of %d\n",
                  pcTaskGetName(NULL), (unsigned)freeStack, (unsigned)uxTaskPriorityGet(NULL), (int)xPortGetCoreID(), cores);
    CHECK("stack high-water mark", freeStack > 256 && freeStack < 65536);
    vTaskPrioritySet(NULL, 50);
    CHECK("priority capped at 10", uxTaskPriorityGet(NULL) == BL_TASK_MAX_PRIORITY);
    vTaskPrioritySet(NULL, 1);

    // A 16 KB stack in PSRAM: the task runs, and its memory comes back both
    // when it returns and when it is deleted. Without PSRAM it is in internal RAM.
    const UBaseType_t PSRAM_CAPS = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    long psram0 = psramFree(), internal0 = internalFree();
    capsReady = xSemaphoreCreateBinary();
    capsGo = xSemaphoreCreateBinary();
    TaskHandle_t capsTask = NULL;
    bool capsMade = xTaskCreateWithCaps(capsWorker, "capsWorker", 16384, NULL, 1, &capsTask, PSRAM_CAPS) == pdPASS;
    bool capsOk = capsMade && xSemaphoreTake(capsReady, pdMS_TO_TICKS(2000)) && capsRan;
    long psram1 = psramFree(), internal1 = internalFree();
    CHECK("task with a PSRAM stack runs", capsOk);
    Serial.printf("      PSRAM %ld bytes less, internal RAM %ld bytes less\n", psram0 - psram1, internal0 - internal1);
    if (psram0 > 0) {
        CHECK("its 16 KB stack is in PSRAM, not internal RAM", psram0 - psram1 >= 16384 && internal0 - internal1 < 4096);
    }
    xSemaphoreGive(capsGo);
    delay(300);
    CHECK("returning gives the PSRAM stack back", psramFree() >= psram0 - 1024);
    TaskHandle_t idleTask = NULL;
    bool idleMade = xTaskCreateWithCaps(capsIdle, "capsIdle", 16384, NULL, 1, &idleTask, PSRAM_CAPS) == pdPASS;
    delay(50);
    if (idleMade) {
        vTaskDeleteWithCaps(idleTask);
    }
    delay(300);
    CHECK("vTaskDeleteWithCaps gives it back", idleMade && psramFree() >= psram0 - 1024);
    vSemaphoreDelete(capsReady);
    vSemaphoreDelete(capsGo);

    // Callbacks stay on the sketch task while another task calls delay().
    ticker.attach_ms(10, onTick);
    xTaskCreate(delayer, "delayer", 3072, NULL, 1, NULL);
    while (!delayerDone) {
        delay(10);
    }
    ticker.detach();
    Serial.printf("      ticker: %d calls, %d on another task\n", tickerCalls, tickerWrongTask);
    CHECK("callbacks only on the sketch task", tickerCalls > 20 && tickerWrongTask == 0);

    // A wait longer than the task watchdog on the sketch task.
    lateGive = xSemaphoreCreateBinary();
    xTaskCreate(giveLater, "giveLater", 3072, NULL, 1, NULL);
    t0 = millis();
    bool late = xSemaphoreTake(lateGive, portMAX_DELAY);
    CHECK("6 s wait on the sketch task (no watchdog)", late && millis() - t0 >= 5900);

    Serial.printf("RESULT %d passed, %d failed\n", passed, failed);
    Serial.println("now deleting the sketch task; a task keeps printing for 10 s");
    xTaskCreate(survivor, "survivor", 4096, NULL, 1, NULL);
    vTaskDelete(NULL);
}

void loop()
{
    Serial.println("FAIL  loop() ran after vTaskDelete(NULL)");
    delay(1000);
}
