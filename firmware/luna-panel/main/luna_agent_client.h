#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define LUNA_AGENT_TEXT_SHORT 48
#define LUNA_AGENT_TEXT_LONG 96
#define LUNA_AGENT_TEXT_WEATHER 128

typedef struct {
    bool online;
    char name[LUNA_AGENT_TEXT_SHORT];
} luna_pc_state_t;

typedef struct {
    bool controllable;
    bool metadata_available;
    bool playing;
    bool cover_available;
    char title[LUNA_AGENT_TEXT_LONG];
    char artist[LUNA_AGENT_TEXT_SHORT];
    char cover_id[LUNA_AGENT_TEXT_SHORT];
    char cover_content_type[LUNA_AGENT_TEXT_SHORT];
} luna_music_state_t;

typedef struct {
    bool available;
    bool muted;
    int percent;
} luna_volume_state_t;

typedef struct {
    bool available;
    int remaining_percent;
    int weekly_remaining_percent;
    char reset_at[LUNA_AGENT_TEXT_SHORT];
    char weekly_reset_at[LUNA_AGENT_TEXT_SHORT];
    char summary[LUNA_AGENT_TEXT_LONG];
    char reset_text[LUNA_AGENT_TEXT_LONG];
} luna_codex_state_t;

typedef struct {
    char name[LUNA_AGENT_TEXT_LONG];
    char path[LUNA_AGENT_TEXT_LONG];
} luna_project_state_t;

typedef struct {
    bool configured;
    bool available;
    bool stale;
    bool coordinates_valid;
    double latitude;
    double longitude;
    int temperature_c;
    char location[LUNA_AGENT_TEXT_SHORT];
    char condition[LUNA_AGENT_TEXT_SHORT];
    char observed_at[LUNA_AGENT_TEXT_SHORT];
    char summary[LUNA_AGENT_TEXT_WEATHER];
    char details[LUNA_AGENT_TEXT_WEATHER];
} luna_weather_state_t;

typedef enum {
    LUNA_AGENT_TRANSPORT_NONE,
    LUNA_AGENT_TRANSPORT_USB,
    LUNA_AGENT_TRANSPORT_HTTP,
} luna_agent_transport_t;

typedef struct {
    uint32_t sequence;
    char generated_at[LUNA_AGENT_TEXT_SHORT];
    luna_agent_transport_t transport;
    luna_pc_state_t pc;
    luna_music_state_t music;
    luna_volume_state_t volume;
    luna_codex_state_t codex;
    luna_project_state_t project;
    luna_weather_state_t weather;
} luna_agent_state_t;

typedef struct {
    bool has_music;
    bool has_volume;
    luna_music_state_t music;
    luna_volume_state_t volume;
} luna_agent_action_result_t;

bool luna_agent_is_configured(void);
void luna_agent_set_http_ready(bool ready);
esp_err_t luna_agent_client_init(void);
esp_err_t luna_agent_fetch_state(luna_agent_state_t *state);
esp_err_t luna_agent_fetch_cover(const char *cover_id, uint8_t **data, size_t *length);
esp_err_t luna_agent_send_action(const char *action, uint64_t request_id, int value,
                                 luna_agent_action_result_t *result);
