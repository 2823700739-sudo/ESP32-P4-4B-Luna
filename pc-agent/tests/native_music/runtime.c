// Deterministic single-thread runtime only, not a FreeRTOS scheduling test.
#include <stdint.h>
#include <string.h>
#include "runtime.h"
static int64_t now;
void luna_test_clock(int64_t value) { now = value; }
int64_t esp_timer_get_time(void) { return now; }
void *xSemaphoreCreateMutex(void) { return &now; }
int xSemaphoreTake(void *mutex, unsigned timeout) { (void)mutex; (void)timeout; return 1; }
void xSemaphoreGive(void *mutex) { (void)mutex; }
size_t luna_test_strlcpy(char *out, const char *text, size_t cap)
{
    size_t n = strlen(text);
    if (cap) { size_t copy = n < cap - 1 ? n : cap - 1; memcpy(out, text, copy); out[copy] = 0; }
    return n;
}
