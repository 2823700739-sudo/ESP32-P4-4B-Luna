#pragma once
#include "lvgl.h"
typedef enum {
    LUNA_LINK_STARTING, LUNA_LINK_WAITING, LUNA_LINK_SECURING,
    LUNA_LINK_READY, LUNA_LINK_ACTIVE, LUNA_LINK_ERROR
} luna_link_phase_t;
typedef struct {
    luna_link_phase_t phase;
    bool pairing, recovery, answer_pending;
    unsigned code, remaining_seconds, generation;
    int recovery_result;
} luna_connection_view_t;
void luna_music_ui_start(lv_obj_t *screen);
/* UI-thread only. Transport state is independent of music snapshot freshness. */
void luna_music_ui_connection(const luna_connection_view_t *view,
                              void (*answer)(bool,unsigned), void (*recover)(unsigned,bool));
