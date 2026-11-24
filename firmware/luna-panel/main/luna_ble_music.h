#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"
#include "esp_err.h"

typedef struct {
    bool online, available, controllable, playing, volume_available, muted;
    bool pending, result_unknown, last_ok, has_result;
    unsigned volume;
    int64_t updated_us;
    char title[256], artist[128];
} luna_music_state_t;

esp_err_t luna_music_init(void);
void luna_music_set_boot_id(const char *id);
void luna_music_reset_session(void);
bool luna_music_get_state(luna_music_state_t *state);
bool luna_music_enqueue(const char *action, int value);
int luna_music_message(const char *type, const cJSON *root, const char *session,
                       char *reply, size_t capacity);
