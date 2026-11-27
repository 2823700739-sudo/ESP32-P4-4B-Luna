#pragma once
#include <stdbool.h>
#include <stdint.h>
#define LUNA_FOCUS_MAX_MINUTES 999U

typedef enum { LUNA_FOCUS_IDLE, LUNA_FOCUS_RUNNING, LUNA_FOCUS_PAUSED, LUNA_FOCUS_DONE } luna_focus_state_t;
typedef struct {
    luna_focus_state_t state;
    unsigned duration_seconds;
    int64_t remaining_us, deadline_us;
    bool notice;
} luna_focus_t;

/* Session-only state. All times are monotonic microseconds, never wall time. */
void luna_focus_init(luna_focus_t *focus);
bool luna_focus_select(luna_focus_t *focus, unsigned minutes);
bool luna_focus_start(luna_focus_t *focus, int64_t now);
bool luna_focus_pause(luna_focus_t *focus, int64_t now);
void luna_focus_end(luna_focus_t *focus);
bool luna_focus_tick(luna_focus_t *focus, int64_t now);
unsigned luna_focus_remaining(const luna_focus_t *focus, int64_t now);
void luna_focus_ack(luna_focus_t *focus);
