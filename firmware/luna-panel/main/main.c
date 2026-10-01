// SPDX-License-Identifier: CC0-1.0

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lvgl.h"
#include "luna_agent_client.h"
#include "luna_usb.h"
#include "luna_weather.h"
#include "nvs_flash.h"

#define UI_QUEUE_DEPTH 16
#define UI_EVENT_TIMER_MS 50
#define UI_DETAIL_LENGTH 128
#define AUDIO_SAMPLE_RATE 16000
#define AUDIO_CHANNEL_COUNT 2
#define AUDIO_BITS_PER_SAMPLE 16
#define TONE_FRAME_COUNT 480
#define TONE_REPEAT_COUNT 20
#define MIC_BUFFER_SAMPLES 960
#define MIC_METER_FLOOR_DBFS (-52.0f)
#define MIC_METER_CEILING_DBFS (-8.0f)
#define MIC_METER_ATTACK 0.65f
#define MIC_METER_RELEASE 0.12f
#define HOME_SCREEN_WIDTH 720
#define HOME_CARD_WIDTH 590
#define HOME_CARD_HEIGHT 450
#define HOME_CARD_PADDING 22
#define HOME_CARD_CONTENT_WIDTH (HOME_CARD_WIDTH - HOME_CARD_PADDING * 2)
#define HOME_CARD_CENTER_X ((HOME_SCREEN_WIDTH - HOME_CARD_WIDTH) / 2)
#define HOME_CARD_GAP 25
#define HOME_CARD_STEP (HOME_CARD_WIDTH + HOME_CARD_GAP)
#define HOME_CARD_TOP 95
#define HOME_CARD_COUNT 4
#define HOME_CARD_SIDE_VEIL_OPA 190
#define HOME_CARD_SIDE_DROP 8
#define HOME_CARD_FOCUS_STEPS 12
#define MUSIC_DISC_SIZE 260
#define MUSIC_COVER_SIZE 172
#define MUSIC_BUTTON_SIZE 64
#define LVGL_TASK_STACK_SIZE (16 * 1024)
#define MEDIA_ACTION_COOLDOWN_US 450000
#define MEDIA_FEEDBACK_US 450000
#define ACTION_CONFIRM_US 4000000
#define WIFI_RECOVERY_INTERVAL_US (30LL * 1000000LL)

typedef enum {
    LUNA_COMPONENT_WIFI,
    LUNA_COMPONENT_USB,
    LUNA_COMPONENT_STORAGE,
    LUNA_COMPONENT_AUDIO,
    LUNA_COMPONENT_MIC,
    LUNA_COMPONENT_AGENT,
    LUNA_COMPONENT_PC_LINK,
    LUNA_COMPONENT_MUSIC,
    LUNA_COMPONENT_VOLUME,
    LUNA_COMPONENT_CODEX,
    LUNA_COMPONENT_PROJECT,
    LUNA_COMPONENT_WEATHER,
    LUNA_COMPONENT_ACTION_RESULT,
    LUNA_COMPONENT_COUNT,
} luna_component_t;

typedef enum {
    LUNA_STATUS_PENDING,
    LUNA_STATUS_READY,
    LUNA_STATUS_BUSY,
    LUNA_STATUS_WARNING,
    LUNA_STATUS_FAILED,
} luna_status_t;

typedef struct {
    luna_component_t component;
    luna_status_t status;
    int value;
    int auxiliary;
    char detail[UI_DETAIL_LENGTH];
    char secondary[UI_DETAIL_LENGTH];
} luna_ui_event_t;

typedef struct {
    uint8_t *data;
    size_t length;
} luna_cover_event_t;

typedef enum {
    LUNA_ACTION_PREVIOUS,
    LUNA_ACTION_PLAY,
    LUNA_ACTION_PAUSE,
    LUNA_ACTION_NEXT,
    LUNA_ACTION_VOLUME_SET,
    LUNA_ACTION_MUTE,
} luna_action_t;

typedef struct {
    luna_action_t action;
    int value;
} luna_action_request_t;

typedef enum {
    LUNA_CARD_CODEX,
    LUNA_CARD_MUSIC,
    LUNA_CARD_WEATHER,
    LUNA_CARD_CLOCK,
} luna_card_t;

static const char *TAG = "luna";
static QueueHandle_t s_ui_queue;
static QueueHandle_t s_mic_ui_queue;
static QueueHandle_t s_cover_ui_queue;
static QueueHandle_t s_action_queue;
static lv_obj_t *s_home_screen;
static lv_obj_t *s_diagnostic_screen;
static lv_obj_t *s_wifi_label;
static lv_obj_t *s_usb_label;
static lv_obj_t *s_storage_label;
static lv_obj_t *s_audio_label;
static lv_obj_t *s_pc_link_label;
static lv_obj_t *s_runtime_label;
static lv_obj_t *s_usb_stats_label;
static lv_obj_t *s_usb_errors_label;
static lv_obj_t *s_detail_label;
static lv_obj_t *s_touch_label;
static lv_obj_t *s_speaker_button;
static lv_obj_t *s_mic_bar;
static lv_obj_t *s_mic_value_label;
static lv_obj_t *s_home_wifi_label;
static lv_obj_t *s_agent_label;
static lv_obj_t *s_card_layer;
static lv_obj_t *s_card_objects[HOME_CARD_COUNT];
static lv_obj_t *s_card_focus_veils[HOME_CARD_COUNT];
static lv_obj_t *s_card_indicator_label;
static lv_obj_t *s_music_title_label;
static lv_obj_t *s_music_artist_label;
static lv_obj_t *s_music_cover_frame;
static lv_obj_t *s_music_cover_image;
static lv_obj_t *s_music_cover_placeholder;
static lv_obj_t *s_music_previous_button;
static lv_obj_t *s_music_toggle_button;
static lv_obj_t *s_music_next_button;
static lv_obj_t *s_music_toggle_label;
static lv_obj_t *s_music_volume_popup;
static lv_obj_t *s_music_mute_label;
static lv_obj_t *s_music_volume_slider;
static lv_obj_t *s_media_feedback_button;
static lv_obj_t *s_codex_value_label;
static lv_obj_t *s_codex_detail_label;
static lv_obj_t *s_codex_reset_label;
static lv_obj_t *s_project_name_label;
static lv_obj_t *s_project_path_label;
static lv_obj_t *s_weather_value_label;
static lv_obj_t *s_weather_detail_label;
static lv_obj_t *s_weather_metrics_label;
static lv_obj_t *s_clock_time_label;
static lv_obj_t *s_clock_seconds_label;
static lv_obj_t *s_clock_date_label;
static lv_obj_t *s_clock_status_label;
static unsigned s_touch_count;
static int s_wifi_retry_count;
static esp_timer_handle_t s_wifi_recovery_timer;
static atomic_bool s_wifi_has_ip;
static bool s_agent_task_started;
static esp_codec_dev_handle_t s_speaker_dev;
static esp_codec_dev_handle_t s_microphone_dev;
static volatile bool s_audio_ready;
static volatile bool s_audio_busy;
static uint8_t *s_music_cover_data;
static lv_image_dsc_t s_music_cover_dsc;
static char s_loaded_cover_id[LUNA_AGENT_TEXT_SHORT];
static luna_ui_event_t s_latest_ui_state[LUNA_COMPONENT_COUNT];
static bool s_latest_ui_state_valid[LUNA_COMPONENT_COUNT];
static uint8_t s_current_card_index;
static uint8_t s_carousel_target_index;
static bool s_carousel_animating;
static int64_t s_last_media_action_us;
static int64_t s_media_feedback_until_us;
static int64_t s_playback_pending_until_us;
static int64_t s_volume_pending_until_us;
static bool s_music_playing;
static bool s_music_authoritative_playing;
static bool s_playback_pending;
static bool s_media_action_inflight;
static bool s_playback_requested_playing;
static bool s_volume_set_pending;
static int s_volume_requested_percent;
static volatile bool s_clock_ntp_synced;
static luna_status_t s_pc_status = LUNA_STATUS_PENDING;
static char s_pc_link_name[24] = "waiting";

static void rebuild_card_window(void);

static void ui_post(luna_component_t component, luna_status_t status, const char *detail,
                     int value)
{
    if (s_ui_queue == NULL || s_mic_ui_queue == NULL) {
        return;
    }

    luna_ui_event_t event = {
        .component = component,
        .status = status,
        .value = value,
    };
    if (detail != NULL) {
        strlcpy(event.detail, detail, sizeof(event.detail));
    }
    if (component == LUNA_COMPONENT_MIC) {
        xQueueOverwrite(s_mic_ui_queue, &event);
        return;
    }
    if (xQueueSend(s_ui_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "UI event queue is full");
    }
}

static void ui_post_extended(luna_component_t component, luna_status_t status,
                             const char *detail, const char *secondary, int value)
{
    if (s_ui_queue == NULL) {
        return;
    }

    luna_ui_event_t event = {
        .component = component,
        .status = status,
        .value = value,
    };
    if (detail != NULL) {
        strlcpy(event.detail, detail, sizeof(event.detail));
    }
    if (secondary != NULL) {
        strlcpy(event.secondary, secondary, sizeof(event.secondary));
    }
    if (xQueueSend(s_ui_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "UI event queue is full");
    }
}

static void ui_post_volume(const luna_volume_state_t *volume)
{
    if (s_ui_queue == NULL || volume == NULL) {
        return;
    }
    const luna_ui_event_t event = {
        .component = LUNA_COMPONENT_VOLUME,
        .status = volume->available ? LUNA_STATUS_READY : LUNA_STATUS_WARNING,
        .value = volume->percent,
        .auxiliary = volume->muted ? 1 : 0,
    };
    if (xQueueSend(s_ui_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "UI event queue is full");
    }
}

static lv_color_t status_color(luna_status_t status)
{
    switch (status) {
    case LUNA_STATUS_READY:
        return lv_color_hex(0x4ADE80);
    case LUNA_STATUS_BUSY:
        return lv_color_hex(0x38BDF8);
    case LUNA_STATUS_WARNING:
        return lv_color_hex(0xFBBF24);
    case LUNA_STATUS_FAILED:
        return lv_color_hex(0xFB7185);
    case LUNA_STATUS_PENDING:
    default:
        return lv_color_hex(0x94A3B8);
    }
}

static const char *status_text(luna_status_t status)
{
    switch (status) {
    case LUNA_STATUS_READY:
        return "ready";
    case LUNA_STATUS_BUSY:
        return "testing";
    case LUNA_STATUS_WARNING:
        return "check";
    case LUNA_STATUS_FAILED:
        return "failed";
    case LUNA_STATUS_PENDING:
    default:
        return "starting";
    }
}

static void set_component_label(lv_obj_t *label, const char *name, luna_status_t status)
{
    lv_label_set_text_fmt(label, "%s: %s", name, status_text(status));
    lv_obj_set_style_text_color(label, status_color(status), LV_PART_MAIN);
}

static void update_pc_status_labels(void)
{
    lv_label_set_text_fmt(s_agent_label, "PC %s (%s)", status_text(s_pc_status), s_pc_link_name);
    lv_obj_set_style_text_color(s_agent_label, status_color(s_pc_status), LV_PART_MAIN);
    lv_label_set_text_fmt(s_pc_link_label, "PC link: %s", s_pc_link_name);
    lv_obj_set_style_text_color(s_pc_link_label, status_color(s_pc_status), LV_PART_MAIN);
}

static void set_media_buttons_disabled(bool disabled)
{
    lv_obj_t *buttons[] = {
        s_music_previous_button,
        s_music_toggle_button,
        s_music_next_button,
    };
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
        if (buttons[i] == NULL) {
            continue;
        }
        if (disabled) {
            lv_obj_add_state(buttons[i], LV_STATE_DISABLED);
        } else {
            lv_obj_remove_state(buttons[i], LV_STATE_DISABLED);
        }
    }
}

static void ui_apply_event(const luna_ui_event_t *event)
{
    if (event->component >= LUNA_COMPONENT_MUSIC &&
        event->component <= LUNA_COMPONENT_WEATHER) {
        s_latest_ui_state[event->component] = *event;
        s_latest_ui_state_valid[event->component] = true;
    }

    switch (event->component) {
    case LUNA_COMPONENT_WIFI:
        set_component_label(s_wifi_label, "Wi-Fi", event->status);
        lv_label_set_text(s_home_wifi_label,
                          event->status == LUNA_STATUS_READY ? "Wi-Fi online" : "Wi-Fi offline");
        lv_obj_set_style_text_color(s_home_wifi_label, status_color(event->status), LV_PART_MAIN);
        break;
    case LUNA_COMPONENT_USB:
        set_component_label(s_usb_label, "USB", event->status);
        break;
    case LUNA_COMPONENT_STORAGE:
        set_component_label(s_storage_label, "TF card", event->status);
        break;
    case LUNA_COMPONENT_AUDIO:
        set_component_label(s_audio_label, "Audio", event->status);
        if (event->status == LUNA_STATUS_READY) {
            lv_obj_remove_state(s_speaker_button, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(s_speaker_button, LV_STATE_DISABLED);
        }
        break;
    case LUNA_COMPONENT_MIC:
        lv_bar_set_value(s_mic_bar, event->value, LV_ANIM_OFF);
        lv_label_set_text_fmt(s_mic_value_label, "Mic %d%%", event->value);
        return;
    case LUNA_COMPONENT_AGENT:
        s_pc_status = event->status;
        update_pc_status_labels();
        return;
    case LUNA_COMPONENT_PC_LINK:
        strlcpy(s_pc_link_name, event->detail, sizeof(s_pc_link_name));
        update_pc_status_labels();
        return;
    case LUNA_COMPONENT_MUSIC:
        s_music_authoritative_playing = event->value == 1;
        if (s_playback_pending &&
            (event->status != LUNA_STATUS_READY ||
             s_music_authoritative_playing == s_playback_requested_playing ||
             esp_timer_get_time() >= s_playback_pending_until_us)) {
            s_playback_pending = false;
        }
        if (!s_playback_pending) {
            s_music_playing = s_music_authoritative_playing;
        }
        if (s_music_title_label != NULL) {
            lv_label_set_text(s_music_title_label,
                              event->detail[0] != '\0' ? event->detail : "No active media");
        }
        if (s_music_artist_label != NULL) {
            lv_label_set_text(s_music_artist_label, event->secondary);
        }
        if (s_music_toggle_label != NULL) {
            lv_label_set_text(s_music_toggle_label,
                              s_music_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
        }
        set_media_buttons_disabled(event->status != LUNA_STATUS_READY || s_media_action_inflight);
        return;
    case LUNA_COMPONENT_VOLUME:
        if (s_music_mute_label != NULL) {
            lv_label_set_text(s_music_mute_label,
                              event->auxiliary != 0 ? LV_SYMBOL_MUTE : LV_SYMBOL_VOLUME_MID);
        }
        if (s_volume_set_pending &&
            (abs(event->value - s_volume_requested_percent) <= 1 ||
             esp_timer_get_time() >= s_volume_pending_until_us)) {
            s_volume_set_pending = false;
        }
        if (s_music_volume_slider != NULL && !lv_slider_is_dragged(s_music_volume_slider) &&
            !s_volume_set_pending && event->status == LUNA_STATUS_READY) {
            lv_slider_set_value(s_music_volume_slider, event->value, LV_ANIM_OFF);
        }
        return;
    case LUNA_COMPONENT_ACTION_RESULT:
        if (event->value == LUNA_ACTION_PLAY || event->value == LUNA_ACTION_PAUSE ||
            event->value == LUNA_ACTION_PREVIOUS || event->value == LUNA_ACTION_NEXT) {
            s_media_action_inflight = false;
            set_media_buttons_disabled(
                !s_latest_ui_state_valid[LUNA_COMPONENT_MUSIC] ||
                s_latest_ui_state[LUNA_COMPONENT_MUSIC].status != LUNA_STATUS_READY);
        }
        if (event->status == LUNA_STATUS_FAILED) {
            if (event->value == LUNA_ACTION_PLAY || event->value == LUNA_ACTION_PAUSE) {
                s_playback_pending = false;
                s_music_playing = s_music_authoritative_playing;
                if (s_music_toggle_label != NULL) {
                    lv_label_set_text(s_music_toggle_label,
                                      s_music_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
                }
                set_media_buttons_disabled(
                    !s_latest_ui_state_valid[LUNA_COMPONENT_MUSIC] ||
                    s_latest_ui_state[LUNA_COMPONENT_MUSIC].status != LUNA_STATUS_READY);
            } else if (event->value == LUNA_ACTION_PREVIOUS ||
                       event->value == LUNA_ACTION_NEXT) {
                set_media_buttons_disabled(
                    !s_latest_ui_state_valid[LUNA_COMPONENT_MUSIC] ||
                    s_latest_ui_state[LUNA_COMPONENT_MUSIC].status != LUNA_STATUS_READY);
            } else if (event->value == LUNA_ACTION_VOLUME_SET) {
                s_volume_set_pending = false;
                if (s_latest_ui_state_valid[LUNA_COMPONENT_VOLUME]) {
                    ui_apply_event(&s_latest_ui_state[LUNA_COMPONENT_VOLUME]);
                }
            }
        }
        return;
    case LUNA_COMPONENT_CODEX:
        if (s_codex_value_label == NULL || s_codex_detail_label == NULL ||
            s_codex_reset_label == NULL) {
            return;
        }
        if (event->status == LUNA_STATUS_READY && event->value >= 0) {
            lv_label_set_text_fmt(s_codex_value_label, "%d%%", event->value);
            lv_label_set_text(s_codex_detail_label,
                              event->detail[0] != '\0' ? event->detail : "remaining");
            lv_label_set_text(s_codex_reset_label, event->secondary);
        } else {
            lv_label_set_text(s_codex_value_label, "--");
            lv_label_set_text(s_codex_detail_label, "Codex unavailable");
            lv_label_set_text(s_codex_reset_label, "Check Windows agent");
        }
        return;
    case LUNA_COMPONENT_PROJECT:
        if (s_project_name_label == NULL || s_project_path_label == NULL) {
            return;
        }
        lv_label_set_text(s_project_name_label,
                          event->detail[0] != '\0' ? event->detail : "No recent project");
        lv_label_set_text(s_project_path_label, event->secondary);
        return;
    case LUNA_COMPONENT_WEATHER:
        if (s_weather_value_label == NULL || s_weather_detail_label == NULL ||
            s_weather_metrics_label == NULL) {
            return;
        }
        if (event->value > -100) {
            lv_label_set_text_fmt(s_weather_value_label, "%d C", event->value);
        } else {
            lv_label_set_text(s_weather_value_label, "-- C");
        }
        lv_obj_set_style_text_color(
            s_weather_value_label,
            event->status == LUNA_STATUS_WARNING ? lv_color_hex(0xFBBF24)
                                                  : lv_color_hex(0xBFDBFE),
            LV_PART_MAIN);
        lv_label_set_text(s_weather_detail_label, event->detail);
        lv_label_set_text_fmt(s_weather_metrics_label, "%s%s",
                              event->status == LUNA_STATUS_WARNING && event->value > -100
                                  ? "CACHED\n"
                                  : "",
                              event->secondary);
        return;
    default:
        return;
    }

    if (event->detail[0] != '\0') {
        lv_label_set_text(s_detail_label, event->detail);
    }
}

static bool jpeg_dimensions(const uint8_t *data, size_t length, uint16_t *width,
                            uint16_t *height)
{
    if (data == NULL || length < 10 || data[0] != 0xFF || data[1] != 0xD8) {
        return false;
    }

    size_t offset = 2;
    while (offset + 4 <= length) {
        if (data[offset] != 0xFF) {
            ++offset;
            continue;
        }
        while (offset < length && data[offset] == 0xFF) {
            ++offset;
        }
        if (offset >= length) {
            break;
        }
        const uint8_t marker = data[offset++];
        if (marker == 0xD8 || marker == 0xD9 || (marker >= 0xD0 && marker <= 0xD7)) {
            continue;
        }
        if (offset + 2 > length) {
            break;
        }
        const size_t segment_length = ((size_t)data[offset] << 8) | data[offset + 1];
        if (segment_length < 2 || offset + segment_length > length) {
            break;
        }
        const bool is_sof =
            (marker >= 0xC0 && marker <= 0xC3) || (marker >= 0xC5 && marker <= 0xC7) ||
            (marker >= 0xC9 && marker <= 0xCB) || (marker >= 0xCD && marker <= 0xCF);
        if (is_sof && segment_length >= 7) {
            *height = ((uint16_t)data[offset + 3] << 8) | data[offset + 4];
            *width = ((uint16_t)data[offset + 5] << 8) | data[offset + 6];
            return *width > 0 && *height > 0;
        }
        offset += segment_length;
    }
    return false;
}

static void ui_refresh_cover_objects(void)
{
    if (s_music_cover_frame == NULL || s_music_cover_image == NULL ||
        s_music_cover_placeholder == NULL) {
        return;
    }
    if (s_music_cover_data == NULL) {
        lv_image_set_src(s_music_cover_image, NULL);
        lv_obj_add_flag(s_music_cover_image, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_music_cover_placeholder, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    const uint16_t width = s_music_cover_dsc.header.w;
    const uint16_t height = s_music_cover_dsc.header.h;
    lv_image_set_src(s_music_cover_image, &s_music_cover_dsc);
    const uint32_t scale_x = (MUSIC_COVER_SIZE * LV_SCALE_NONE) / width;
    const uint32_t scale_y = (MUSIC_COVER_SIZE * LV_SCALE_NONE) / height;
    const uint32_t scale = scale_x > scale_y ? scale_x : scale_y;
    lv_image_set_scale(s_music_cover_image, scale);
    lv_obj_center(s_music_cover_image);
    lv_obj_remove_flag(s_music_cover_image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_music_cover_placeholder, LV_OBJ_FLAG_HIDDEN);
}

static void ui_apply_cover(luna_cover_event_t *event)
{
    uint16_t width = 0;
    uint16_t height = 0;
    if (event->data == NULL || !jpeg_dimensions(event->data, event->length, &width, &height)) {
        free(event->data);
        if (s_music_cover_image != NULL) {
            lv_image_set_src(s_music_cover_image, NULL);
        }
        if (s_music_cover_data != NULL) {
            lv_image_cache_drop(&s_music_cover_dsc);
            free(s_music_cover_data);
            s_music_cover_data = NULL;
            memset(&s_music_cover_dsc, 0, sizeof(s_music_cover_dsc));
        }
        ui_refresh_cover_objects();
        return;
    }

    if (s_music_cover_data != NULL) {
        if (s_music_cover_image != NULL) {
            lv_image_set_src(s_music_cover_image, NULL);
        }
        lv_image_cache_drop(&s_music_cover_dsc);
        free(s_music_cover_data);
    }
    s_music_cover_data = event->data;
    s_music_cover_dsc = (lv_image_dsc_t) {
        .header = {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_RAW,
            .w = width,
            .h = height,
            .stride = (uint32_t)width * 3,
        },
        .data_size = event->length,
        .data = s_music_cover_data,
    };
    ui_refresh_cover_objects();
}

static void ui_event_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    luna_ui_event_t event;

    while (xQueueReceive(s_ui_queue, &event, 0) == pdTRUE) {
        ui_apply_event(&event);
    }
    if (xQueueReceive(s_mic_ui_queue, &event, 0) == pdTRUE) {
        if (lv_screen_active() == s_diagnostic_screen) {
            ui_apply_event(&event);
        }
    }
    if (!s_carousel_animating) {
        luna_cover_event_t cover_event;
        if (xQueueReceive(s_cover_ui_queue, &cover_event, 0) == pdTRUE) {
            ui_apply_cover(&cover_event);
        }
    }
    const int64_t now = esp_timer_get_time();
    if (s_media_feedback_button != NULL && now >= s_media_feedback_until_us) {
        lv_obj_set_style_bg_opa(s_media_feedback_button, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_media_feedback_button, lv_color_hex(0x303C44), LV_PART_MAIN);
        s_media_feedback_button = NULL;
    }
    if (s_playback_pending && now >= s_playback_pending_until_us) {
        s_playback_pending = false;
        s_music_playing = s_music_authoritative_playing;
        if (s_music_toggle_label != NULL) {
            lv_label_set_text(s_music_toggle_label,
                              s_music_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
        }
        set_media_buttons_disabled(
            s_media_action_inflight || !s_latest_ui_state_valid[LUNA_COMPONENT_MUSIC] ||
            s_latest_ui_state[LUNA_COMPONENT_MUSIC].status != LUNA_STATUS_READY);
    }
    if (s_volume_set_pending && now >= s_volume_pending_until_us) {
        s_volume_set_pending = false;
        if (s_music_volume_slider != NULL &&
            s_latest_ui_state_valid[LUNA_COMPONENT_VOLUME] &&
            s_latest_ui_state[LUNA_COMPONENT_VOLUME].status == LUNA_STATUS_READY) {
            lv_slider_set_value(s_music_volume_slider,
                                s_latest_ui_state[LUNA_COMPONENT_VOLUME].value, LV_ANIM_OFF);
        }
    }
}

static void screen_switch_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    lv_obj_t *target = lv_event_get_user_data(event);
    if (target != NULL) {
        lv_screen_load_anim(target, LV_SCR_LOAD_ANIM_FADE_IN, 220, 0, false);
    }
}

static void media_action_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || s_action_queue == NULL) {
        return;
    }
    luna_action_request_t request = {
        .action = (luna_action_t)(intptr_t)lv_event_get_user_data(event),
        .value = -1,
    };
    const bool playback_action = request.action != LUNA_ACTION_MUTE;
    const int64_t now = esp_timer_get_time();
    if (playback_action && now - s_last_media_action_us < MEDIA_ACTION_COOLDOWN_US) {
        return;
    }
    if (request.action == LUNA_ACTION_PLAY) {
        request.action = s_music_playing ? LUNA_ACTION_PAUSE : LUNA_ACTION_PLAY;
    }
    if (xQueueSend(s_action_queue, &request, 0) != pdTRUE) {
        ui_post(LUNA_COMPONENT_AGENT, LUNA_STATUS_WARNING, "Action queue full", 0);
    } else {
        if (playback_action) {
            s_last_media_action_us = now;
            s_media_action_inflight = true;
        }
        if (request.action == LUNA_ACTION_PLAY || request.action == LUNA_ACTION_PAUSE) {
            s_music_playing = request.action == LUNA_ACTION_PLAY;
            s_playback_requested_playing = s_music_playing;
            s_playback_pending = true;
            s_playback_pending_until_us = now + ACTION_CONFIRM_US;
            if (s_music_toggle_label != NULL) {
                lv_label_set_text(s_music_toggle_label,
                                  s_music_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
            }
            set_media_buttons_disabled(true);
        } else if (request.action == LUNA_ACTION_PREVIOUS ||
                   request.action == LUNA_ACTION_NEXT) {
            lv_obj_t *button = lv_event_get_target(event);
            if (s_media_feedback_button != NULL && s_media_feedback_button != button) {
                lv_obj_set_style_bg_opa(s_media_feedback_button, LV_OPA_TRANSP, LV_PART_MAIN);
                lv_obj_set_style_bg_color(s_media_feedback_button,
                                          lv_color_hex(0x303C44), LV_PART_MAIN);
            }
            s_media_feedback_button = button;
            s_media_feedback_until_us = now + MEDIA_FEEDBACK_US;
            lv_obj_set_style_bg_color(button, lv_color_hex(0x56A4BC), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(button, LV_OPA_70, LV_PART_MAIN);
            set_media_buttons_disabled(true);
        }
    }
}

static void volume_slider_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_RELEASED || s_action_queue == NULL) {
        return;
    }
    const int value = lv_slider_get_value(lv_event_get_target(event));
    const luna_action_request_t request = {
        .action = LUNA_ACTION_VOLUME_SET,
        .value = value,
    };
    if (xQueueSend(s_action_queue, &request, 0) == pdTRUE) {
        s_volume_set_pending = true;
        s_volume_requested_percent = value;
        s_volume_pending_until_us = esp_timer_get_time() + ACTION_CONFIRM_US;
    } else {
        ui_post(LUNA_COMPONENT_AGENT, LUNA_STATUS_WARNING, "Action queue full", 0);
    }
}

static void volume_toggle_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || s_music_volume_popup == NULL) {
        return;
    }
    if (lv_obj_has_flag(s_music_volume_popup, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_remove_flag(s_music_volume_popup, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_music_volume_popup, LV_OBJ_FLAG_HIDDEN);
    }
}

static lv_obj_t *create_card(lv_obj_t *screen, int x, int y, int width, int height,
                             uint32_t color)
{
    lv_obj_t *card = lv_obj_create(screen);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, width, height);
    lv_obj_set_style_radius(card, 28, LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(0x334155), LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, HOME_CARD_PADDING, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static lv_obj_t *create_card_title(lv_obj_t *card, const char *text)
{
    lv_obj_t *label = lv_label_create(card);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
    return label;
}

static void create_card_focus_veil(lv_obj_t *card, uint8_t index)
{
    lv_obj_t *veil = lv_obj_create(card);
    lv_obj_set_pos(veil, -HOME_CARD_PADDING, -HOME_CARD_PADDING);
    lv_obj_set_size(veil, HOME_CARD_WIDTH, HOME_CARD_HEIGHT);
    lv_obj_set_style_radius(veil, 28, LV_PART_MAIN);
    lv_obj_set_style_bg_color(veil, lv_color_hex(0x7082A0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(veil, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(veil, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(veil, 0, LV_PART_MAIN);
    lv_obj_clear_flag(veil, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    s_card_focus_veils[index] = veil;
}

static lv_obj_t *create_media_button(lv_obj_t *parent, int x, int y, int size,
                                     const char *symbol, lv_event_cb_t callback,
                                     luna_action_t action, bool filled, lv_obj_t **label_out)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, size, size);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x303C44), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, filled ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, (void *)(intptr_t)action);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF2F5F7), LV_PART_MAIN);
    lv_obj_center(label);
    if (label_out != NULL) {
        *label_out = label;
    }
    return button;
}

static void create_vinyl_ring(lv_obj_t *card, int size, uint32_t color, int border_width)
{
    lv_obj_t *ring = lv_obj_create(card);
    lv_obj_set_size(ring, size, size);
    lv_obj_align(ring, LV_ALIGN_TOP_MID, 0, 16 + (MUSIC_DISC_SIZE - size) / 2);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(ring, border_width, LV_PART_MAIN);
    lv_obj_set_style_border_color(ring, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_pad_all(ring, 0, LV_PART_MAIN);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

static uint8_t wrap_card_index(int index)
{
    while (index < 0) {
        index += HOME_CARD_COUNT;
    }
    return (uint8_t)(index % HOME_CARD_COUNT);
}

static void delete_card_object(uint8_t index)
{
    lv_obj_t *card = s_card_objects[index];
    if (card == NULL) {
        return;
    }

    switch (index) {
    case LUNA_CARD_CODEX:
        s_codex_value_label = NULL;
        s_codex_detail_label = NULL;
        s_codex_reset_label = NULL;
        s_project_name_label = NULL;
        s_project_path_label = NULL;
        break;
    case LUNA_CARD_MUSIC:
        lv_image_set_src(s_music_cover_image, NULL);
        if (s_music_cover_data != NULL) {
            lv_image_cache_drop(&s_music_cover_dsc);
        }
        s_music_title_label = NULL;
        s_music_artist_label = NULL;
        s_music_cover_frame = NULL;
        s_music_cover_image = NULL;
        s_music_cover_placeholder = NULL;
        s_music_previous_button = NULL;
        s_music_toggle_button = NULL;
        s_music_next_button = NULL;
        s_music_toggle_label = NULL;
        s_music_volume_popup = NULL;
        s_music_mute_label = NULL;
        s_music_volume_slider = NULL;
        s_media_feedback_button = NULL;
        break;
    case LUNA_CARD_WEATHER:
        s_weather_value_label = NULL;
        s_weather_detail_label = NULL;
        s_weather_metrics_label = NULL;
        break;
    case LUNA_CARD_CLOCK:
        s_clock_time_label = NULL;
        s_clock_seconds_label = NULL;
        s_clock_date_label = NULL;
        s_clock_status_label = NULL;
        break;
    default:
        break;
    }
    lv_obj_delete(card);
    s_card_objects[index] = NULL;
    s_card_focus_veils[index] = NULL;
}

static lv_obj_t *create_codex_card(int x)
{
    lv_obj_t *card = create_card(s_card_layer, x, 0, HOME_CARD_WIDTH, HOME_CARD_HEIGHT,
                                 0x111827);
    create_card_title(card, "CODEX WORKSPACE");

    lv_obj_t *quota_title = lv_label_create(card);
    lv_label_set_text(quota_title, "QUOTA");
    lv_obj_set_style_text_font(quota_title, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(quota_title, lv_color_hex(0x6EE7B7), LV_PART_MAIN);
    lv_obj_set_pos(quota_title, 0, 66);

    s_codex_value_label = lv_label_create(card);
    lv_label_set_text(s_codex_value_label, "--");
    lv_obj_set_style_text_font(s_codex_value_label, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_codex_value_label, lv_color_hex(0xA7F3D0), LV_PART_MAIN);
    lv_obj_set_pos(s_codex_value_label, 0, 104);

    s_codex_detail_label = lv_label_create(card);
    lv_label_set_text(s_codex_detail_label, "Waiting for PC");
    lv_obj_set_width(s_codex_detail_label, 145);
    lv_obj_set_style_text_font(s_codex_detail_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_codex_detail_label, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_set_pos(s_codex_detail_label, 0, 174);

    s_codex_reset_label = lv_label_create(card);
    lv_label_set_text(s_codex_reset_label, "");
    lv_obj_set_width(s_codex_reset_label, 145);
    lv_label_set_long_mode(s_codex_reset_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_codex_reset_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_codex_reset_label, lv_color_hex(0x64748B), LV_PART_MAIN);
    lv_obj_set_pos(s_codex_reset_label, 0, 238);

    lv_obj_t *divider = lv_obj_create(card);
    lv_obj_set_pos(divider, 160, 62);
    lv_obj_set_size(divider, 1, 260);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0x334155), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(divider, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(divider, 0, LV_PART_MAIN);

    lv_obj_t *project_title = lv_label_create(card);
    lv_label_set_text(project_title, "CURRENT PROJECT");
    lv_obj_set_style_text_font(project_title, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(project_title, lv_color_hex(0x7DD3FC), LV_PART_MAIN);
    lv_obj_set_pos(project_title, 188, 66);

    s_project_name_label = lv_label_create(card);
    lv_label_set_text(s_project_name_label, "Waiting for PC");
    lv_obj_set_width(s_project_name_label, HOME_CARD_CONTENT_WIDTH - 188);
    lv_label_set_long_mode(s_project_name_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_project_name_label, &lv_font_source_han_sans_sc_16_cjk,
                               LV_PART_MAIN);
    lv_obj_set_style_text_color(s_project_name_label, lv_color_hex(0xBAE6FD), LV_PART_MAIN);
    lv_obj_set_pos(s_project_name_label, 188, 110);

    s_project_path_label = lv_label_create(card);
    lv_label_set_text(s_project_path_label, "No data yet");
    lv_obj_set_width(s_project_path_label, HOME_CARD_CONTENT_WIDTH - 188);
    lv_label_set_long_mode(s_project_path_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_project_path_label, &lv_font_source_han_sans_sc_16_cjk,
                               LV_PART_MAIN);
    lv_obj_set_style_text_color(s_project_path_label, lv_color_hex(0x64748B), LV_PART_MAIN);
    lv_obj_set_pos(s_project_path_label, 188, 164);
    create_card_focus_veil(card, LUNA_CARD_CODEX);
    return card;
}

static lv_obj_t *create_music_card(int x)
{
    lv_obj_t *card = create_card(s_card_layer, x, 0, HOME_CARD_WIDTH, HOME_CARD_HEIGHT,
                                 0x1D292F);
    create_card_title(card, "MUSIC");

    lv_obj_t *disc = lv_obj_create(card);
    lv_obj_set_size(disc, MUSIC_DISC_SIZE, MUSIC_DISC_SIZE);
    lv_obj_align(disc, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(disc, lv_color_hex(0x090B0D), LV_PART_MAIN);
    lv_obj_set_style_border_color(disc, lv_color_hex(0x344047), LV_PART_MAIN);
    lv_obj_set_style_border_width(disc, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(disc, 0, LV_PART_MAIN);
    lv_obj_clear_flag(disc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    create_vinyl_ring(card, 244, 0x171B1E, 3);
    create_vinyl_ring(card, 230, 0x23282B, 1);
    create_vinyl_ring(card, 218, 0x15191B, 1);
    create_vinyl_ring(card, 206, 0x282D30, 1);
    create_vinyl_ring(card, 192, 0x1A1F22, 2);
    create_vinyl_ring(card, 182, 0x343A3D, 2);

    s_music_cover_frame = lv_obj_create(card);
    lv_obj_set_size(s_music_cover_frame, MUSIC_COVER_SIZE, MUSIC_COVER_SIZE);
    lv_obj_align(s_music_cover_frame, LV_ALIGN_TOP_MID, 0,
                 16 + (MUSIC_DISC_SIZE - MUSIC_COVER_SIZE) / 2);
    lv_obj_set_style_radius(s_music_cover_frame, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(s_music_cover_frame, true, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_music_cover_frame, lv_color_hex(0x26333A), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_music_cover_frame, lv_color_hex(0x07090A), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_music_cover_frame, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_music_cover_frame, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_music_cover_frame, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    s_music_cover_placeholder = lv_label_create(s_music_cover_frame);
    lv_label_set_text(s_music_cover_placeholder, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_align(s_music_cover_placeholder, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_music_cover_placeholder, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_music_cover_placeholder, lv_color_hex(0x8B9DA5), LV_PART_MAIN);
    lv_obj_center(s_music_cover_placeholder);

    s_music_cover_image = lv_image_create(s_music_cover_frame);
    lv_obj_add_flag(s_music_cover_image, LV_OBJ_FLAG_HIDDEN);

    static const lv_point_precise_t tonearm_points[] = {
        { 437, 24 }, { 407, 53 }, { 374, 83 }, { 353, 94 },
    };
    lv_obj_t *tonearm = lv_line_create(card);
    lv_line_set_points(tonearm, tonearm_points,
                       sizeof(tonearm_points) / sizeof(tonearm_points[0]));
    lv_obj_set_style_line_width(tonearm, 5, LV_PART_MAIN);
    lv_obj_set_style_line_color(tonearm, lv_color_hex(0xE7ECEE), LV_PART_MAIN);
    lv_obj_set_style_line_rounded(tonearm, true, LV_PART_MAIN);
    lv_obj_clear_flag(tonearm, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *pivot = lv_obj_create(card);
    lv_obj_set_pos(pivot, 427, 14);
    lv_obj_set_size(pivot, 20, 20);
    lv_obj_set_style_radius(pivot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(pivot, lv_color_hex(0xF4F7F8), LV_PART_MAIN);
    lv_obj_set_style_border_color(pivot, lv_color_hex(0x8A969B), LV_PART_MAIN);
    lv_obj_set_style_border_width(pivot, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_all(pivot, 0, LV_PART_MAIN);
    lv_obj_clear_flag(pivot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *stylus = lv_obj_create(card);
    lv_obj_set_pos(stylus, 336, 89);
    lv_obj_set_size(stylus, 22, 11);
    lv_obj_set_style_radius(stylus, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(stylus, lv_color_hex(0xF4F7F8), LV_PART_MAIN);
    lv_obj_set_style_border_width(stylus, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(stylus, 0, LV_PART_MAIN);
    lv_obj_clear_flag(stylus, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    s_music_title_label = lv_label_create(card);
    lv_label_set_text(s_music_title_label, "NetEase Cloud Music");
    lv_obj_set_width(s_music_title_label, HOME_CARD_CONTENT_WIDTH - 26);
    lv_label_set_long_mode(s_music_title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_music_title_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_music_title_label, &lv_font_source_han_sans_sc_16_cjk,
                               LV_PART_MAIN);
    lv_obj_set_style_text_color(s_music_title_label, lv_color_hex(0xF2F5F7), LV_PART_MAIN);
    lv_obj_align(s_music_title_label, LV_ALIGN_TOP_MID, 0, 279);

    s_music_artist_label = lv_label_create(card);
    lv_label_set_text(s_music_artist_label, "Waiting for PC");
    lv_obj_set_width(s_music_artist_label, HOME_CARD_CONTENT_WIDTH - 26);
    lv_label_set_long_mode(s_music_artist_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_music_artist_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_music_artist_label, &lv_font_source_han_sans_sc_16_cjk,
                               LV_PART_MAIN);
    lv_obj_set_style_text_color(s_music_artist_label, lv_color_hex(0x9AAAB1), LV_PART_MAIN);
    lv_obj_align(s_music_artist_label, LV_ALIGN_TOP_MID, 0, 309);

    s_music_previous_button = create_media_button(
        card, 137, 342, MUSIC_BUTTON_SIZE, LV_SYMBOL_PREV,
        media_action_event_cb, LUNA_ACTION_PREVIOUS, false, NULL);
    s_music_toggle_button = create_media_button(
        card, 241, 342, MUSIC_BUTTON_SIZE, LV_SYMBOL_PLAY,
        media_action_event_cb, LUNA_ACTION_PLAY, true, &s_music_toggle_label);
    s_music_next_button = create_media_button(
        card, 345, 342, MUSIC_BUTTON_SIZE, LV_SYMBOL_NEXT,
        media_action_event_cb, LUNA_ACTION_NEXT, false, NULL);
    create_media_button(card, 483, 350, 48, LV_SYMBOL_VOLUME_MID,
                        volume_toggle_event_cb, LUNA_ACTION_MUTE, false, NULL);

    s_music_volume_popup = lv_obj_create(card);
    lv_obj_set_pos(s_music_volume_popup, 470, 69);
    lv_obj_set_size(s_music_volume_popup, 70, 204);
    lv_obj_set_style_radius(s_music_volume_popup, 24, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_music_volume_popup, lv_color_hex(0x2B383F), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_music_volume_popup, lv_color_hex(0x52616A), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_music_volume_popup, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_music_volume_popup, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_music_volume_popup, LV_OBJ_FLAG_SCROLLABLE);
    create_media_button(s_music_volume_popup, 10, 7, 50, LV_SYMBOL_VOLUME_MID,
                        media_action_event_cb, LUNA_ACTION_MUTE, false, &s_music_mute_label);
    s_music_volume_slider = lv_slider_create(s_music_volume_popup);
    lv_obj_set_pos(s_music_volume_slider, 21, 67);
    lv_obj_set_size(s_music_volume_slider, 28, 122);
    lv_slider_set_orientation(s_music_volume_slider, LV_SLIDER_ORIENTATION_VERTICAL);
    lv_slider_set_range(s_music_volume_slider, 0, 100);
    lv_slider_set_value(s_music_volume_slider, 50, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_music_volume_slider, lv_color_hex(0x46545D), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_music_volume_slider, lv_color_hex(0x84CCE1), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_music_volume_slider, lv_color_hex(0xE7F7FA), LV_PART_KNOB);
    lv_obj_set_style_radius(s_music_volume_slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(s_music_volume_slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_music_volume_slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_add_event_cb(s_music_volume_slider, volume_slider_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_flag(s_music_volume_popup, LV_OBJ_FLAG_HIDDEN);
    set_media_buttons_disabled(true);
    ui_refresh_cover_objects();
    create_card_focus_veil(card, LUNA_CARD_MUSIC);
    return card;
}

static lv_obj_t *create_weather_card(int x)
{
    lv_obj_t *card = create_card(s_card_layer, x, 0, HOME_CARD_WIDTH, HOME_CARD_HEIGHT,
                                 0x172554);
    create_card_title(card, "WEATHER");
    s_weather_value_label = lv_label_create(card);
    lv_label_set_text(s_weather_value_label, "-- C");
    lv_obj_set_style_text_font(s_weather_value_label, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_weather_value_label, lv_color_hex(0xBFDBFE), LV_PART_MAIN);
    lv_obj_set_pos(s_weather_value_label, 0, 72);
    s_weather_detail_label = lv_label_create(card);
    lv_label_set_text(s_weather_detail_label, "Location not configured");
    lv_obj_set_width(s_weather_detail_label, HOME_CARD_CONTENT_WIDTH - 170);
    lv_label_set_long_mode(s_weather_detail_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_weather_detail_label, &lv_font_source_han_sans_sc_16_cjk,
                               LV_PART_MAIN);
    lv_obj_set_style_text_color(s_weather_detail_label, lv_color_hex(0x93C5FD), LV_PART_MAIN);
    lv_obj_set_pos(s_weather_detail_label, 170, 70);

    s_weather_metrics_label = lv_label_create(card);
    lv_label_set_text(s_weather_metrics_label, "Configure latitude and longitude on PC");
    lv_obj_set_width(s_weather_metrics_label, HOME_CARD_CONTENT_WIDTH);
    lv_label_set_long_mode(s_weather_metrics_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_weather_metrics_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_weather_metrics_label, lv_color_hex(0xCBD5E1), LV_PART_MAIN);
    lv_obj_set_pos(s_weather_metrics_label, 0, 198);

    lv_obj_t *source_label = lv_label_create(card);
    lv_label_set_text(source_label, "Weather data: Open-Meteo");
    lv_obj_set_style_text_font(source_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(source_label, lv_color_hex(0x64748B), LV_PART_MAIN);
    lv_obj_align(source_label, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    create_card_focus_veil(card, LUNA_CARD_WEATHER);
    return card;
}

static void clock_set_text_if_changed(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}

static void clock_refresh(void)
{
    if (s_clock_time_label == NULL) {
        return;
    }

    const time_t now = time(NULL);
    struct tm local_time;
    if (now < 1704067200 || localtime_r(&now, &local_time) == NULL) {
        clock_set_text_if_changed(s_clock_time_label, "--:--");
        clock_set_text_if_changed(s_clock_seconds_label, "--");
        clock_set_text_if_changed(s_clock_date_label, "等待网络校时");
        clock_set_text_if_changed(s_clock_status_label, "连接 Wi-Fi 后自动校时");
        return;
    }

    static const char *weekdays[] = {
        "星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六",
    };
    char text[64];
    snprintf(text, sizeof(text), "%02d:%02d", local_time.tm_hour, local_time.tm_min);
    clock_set_text_if_changed(s_clock_time_label, text);
    snprintf(text, sizeof(text), "%02d", local_time.tm_sec);
    clock_set_text_if_changed(s_clock_seconds_label, text);
    snprintf(text, sizeof(text), "%04d.%02d.%02d  %s", local_time.tm_year + 1900,
             local_time.tm_mon + 1, local_time.tm_mday, weekdays[local_time.tm_wday]);
    clock_set_text_if_changed(s_clock_date_label, text);
    clock_set_text_if_changed(s_clock_status_label,
                              s_clock_ntp_synced ? "本地时钟 · 电脑休眠仍可显示"
                                                  : "估计时间 · 等待网络校时");
}

static void clock_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    clock_refresh();
    if (s_runtime_label != NULL && lv_screen_active() == s_diagnostic_screen) {
        const uint64_t uptime_seconds = (uint64_t)(esp_timer_get_time() / 1000000LL);
        const size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        const size_t min_internal = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        lv_label_set_text_fmt(s_runtime_label,
                              "Uptime %lluh %02llum   Internal heap %u KB (min %u KB)",
                              (unsigned long long)(uptime_seconds / 3600),
                              (unsigned long long)((uptime_seconds / 60) % 60),
                              (unsigned)(free_internal / 1024),
                              (unsigned)(min_internal / 1024));
        luna_usb_stats_t usb_stats;
        luna_usb_get_stats(&usb_stats);
        lv_label_set_text_fmt(s_usb_stats_label,
                              "USB opens %lu  hello %lu  requests %lu ok / %lu fail",
                              (unsigned long)usb_stats.cdc_opens,
                              (unsigned long)usb_stats.handshakes,
                              (unsigned long)usb_stats.exchanges_ok,
                              (unsigned long)usb_stats.exchanges_failed);
        lv_label_set_text_fmt(s_usb_errors_label,
                              "Timeout %lu  protocol %lu  queue %lu  TX %lu",
                              (unsigned long)usb_stats.exchanges_timed_out,
                              (unsigned long)usb_stats.protocol_errors,
                              (unsigned long)usb_stats.queue_drops,
                              (unsigned long)usb_stats.tx_errors);
    }
}

static lv_obj_t *create_clock_card(int x)
{
    lv_obj_t *card = create_card(s_card_layer, x, 0, HOME_CARD_WIDTH, HOME_CARD_HEIGHT,
                                 0x102847);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x342C63), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(0x57729B), LV_PART_MAIN);

    lv_obj_t *halo = lv_obj_create(card);
    lv_obj_set_size(halo, 250, 250);
    lv_obj_set_pos(halo, 285, 34);
    lv_obj_set_style_radius(halo, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(halo, lv_color_hex(0xFBBF88), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(halo, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_border_width(halo, 0, LV_PART_MAIN);
    lv_obj_clear_flag(halo, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *moon = lv_obj_create(card);
    lv_obj_set_size(moon, 160, 160);
    lv_obj_set_pos(moon, 335, 78);
    lv_obj_set_style_radius(moon, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(moon, lv_color_hex(0xFDE7C0), LV_PART_MAIN);
    lv_obj_set_style_border_width(moon, 0, LV_PART_MAIN);
    lv_obj_clear_flag(moon, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = create_card_title(card, "LUNA CLOCK");
    lv_obj_set_style_text_color(title, lv_color_hex(0xC8D8F7), LV_PART_MAIN);

    s_clock_time_label = lv_label_create(card);
    lv_label_set_text(s_clock_time_label, "--:--");
    lv_obj_set_style_text_font(s_clock_time_label, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_clock_time_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_pos(s_clock_time_label, 0, 126);

    s_clock_seconds_label = lv_label_create(card);
    lv_label_set_text(s_clock_seconds_label, "--");
    lv_obj_set_style_text_font(s_clock_seconds_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_clock_seconds_label, lv_color_hex(0xE0B8F8), LV_PART_MAIN);
    lv_obj_set_pos(s_clock_seconds_label, 170, 150);

    s_clock_date_label = lv_label_create(card);
    lv_label_set_text(s_clock_date_label, "等待网络校时");
    lv_obj_set_style_text_font(s_clock_date_label, &lv_font_source_han_sans_sc_16_cjk,
                               LV_PART_MAIN);
    lv_obj_set_style_text_color(s_clock_date_label, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
    lv_obj_set_pos(s_clock_date_label, 4, 228);

    s_clock_status_label = lv_label_create(card);
    lv_label_set_text(s_clock_status_label, "连接 Wi-Fi 后自动校时");
    lv_obj_set_style_text_font(s_clock_status_label, &lv_font_source_han_sans_sc_16_cjk,
                               LV_PART_MAIN);
    lv_obj_set_style_text_color(s_clock_status_label, lv_color_hex(0xB6C7E7), LV_PART_MAIN);
    lv_obj_align(s_clock_status_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    clock_refresh();
    create_card_focus_veil(card, LUNA_CARD_CLOCK);
    return card;
}

static void restore_component_state(luna_component_t component)
{
    if (s_latest_ui_state_valid[component]) {
        ui_apply_event(&s_latest_ui_state[component]);
    }
}

static void update_card_focus(uint8_t index, int32_t x)
{
    lv_obj_t *card = s_card_objects[index];
    lv_obj_t *veil = s_card_focus_veils[index];
    if (card == NULL || veil == NULL) {
        return;
    }

    int32_t distance = x - HOME_CARD_CENTER_X;
    if (distance < 0) {
        distance = -distance;
    }
    if (distance > HOME_CARD_STEP) {
        distance = HOME_CARD_STEP;
    }
    const int32_t focus_step = (distance * HOME_CARD_FOCUS_STEPS + HOME_CARD_STEP / 2) /
                               HOME_CARD_STEP;
    const lv_opa_t veil_opa = (lv_opa_t)(focus_step * HOME_CARD_SIDE_VEIL_OPA /
                                         HOME_CARD_FOCUS_STEPS);
    if (lv_obj_get_style_bg_opa(veil, LV_PART_MAIN) != veil_opa) {
        lv_obj_set_style_bg_opa(veil, veil_opa, LV_PART_MAIN);
    }

    const int32_t drop = focus_step * HOME_CARD_SIDE_DROP / HOME_CARD_FOCUS_STEPS;
    if (lv_obj_get_y(card) != drop) {
        lv_obj_set_y(card, drop);
    }
    const lv_opa_t border_opa = (lv_opa_t)(80 +
                                            (HOME_CARD_FOCUS_STEPS - focus_step) * 175 /
                                                HOME_CARD_FOCUS_STEPS);
    if (lv_obj_get_style_border_opa(card, LV_PART_MAIN) != border_opa) {
        lv_obj_set_style_border_opa(card, border_opa, LV_PART_MAIN);
    }
}

static void position_card(uint8_t index, int32_t logical_x)
{
    int32_t draw_x = logical_x;
    if (draw_x + HOME_CARD_WIDTH <= 0) {
        draw_x = -HOME_CARD_WIDTH;
    } else if (draw_x >= HOME_SCREEN_WIDTH) {
        draw_x = HOME_SCREEN_WIDTH;
    }
    if (lv_obj_get_x(s_card_objects[index]) != draw_x) {
        lv_obj_set_x(s_card_objects[index], draw_x);
    }
    update_card_focus(index, logical_x);
}

static void rebuild_card_window(void)
{
    static const char *names[HOME_CARD_COUNT] = {
        "CODEX",
        "MUSIC",
        "WEATHER",
        "CLOCK",
    };

    bool visible[HOME_CARD_COUNT] = {false};
    for (int offset = -1; offset <= 1; ++offset) {
        visible[wrap_card_index((int)s_current_card_index + offset)] = true;
    }
    for (uint8_t index = 0; index < HOME_CARD_COUNT; ++index) {
        if (!visible[index]) {
            delete_card_object(index);
        }
    }

    for (int offset = -1; offset <= 1; ++offset) {
        const uint8_t index = wrap_card_index((int)s_current_card_index + offset);
        if (s_card_objects[index] == NULL) {
            switch (index) {
            case LUNA_CARD_CODEX:
                s_card_objects[index] = create_codex_card(HOME_CARD_CENTER_X);
                break;
            case LUNA_CARD_MUSIC:
                s_card_objects[index] = create_music_card(HOME_CARD_CENTER_X);
                break;
            case LUNA_CARD_WEATHER:
                s_card_objects[index] = create_weather_card(HOME_CARD_CENTER_X);
                break;
            case LUNA_CARD_CLOCK:
                s_card_objects[index] = create_clock_card(HOME_CARD_CENTER_X);
                break;
            default:
                break;
            }
            switch (index) {
            case LUNA_CARD_CODEX:
                restore_component_state(LUNA_COMPONENT_CODEX);
                restore_component_state(LUNA_COMPONENT_PROJECT);
                break;
            case LUNA_CARD_MUSIC:
                restore_component_state(LUNA_COMPONENT_MUSIC);
                restore_component_state(LUNA_COMPONENT_VOLUME);
                break;
            case LUNA_CARD_WEATHER:
                restore_component_state(LUNA_COMPONENT_WEATHER);
                break;
            default:
                break;
            }
        }
        const int32_t x = HOME_CARD_CENTER_X + offset * HOME_CARD_STEP;
        position_card(index, x);
    }
    lv_label_set_text_fmt(s_card_indicator_label, "<  %u / %u  %s  >",
                          (unsigned)s_current_card_index + 1, HOME_CARD_COUNT,
                          names[s_current_card_index]);
}

static void carousel_set_offset(void *layer, int32_t offset)
{
    (void)layer;
    for (int slot = -1; slot <= 1; ++slot) {
        const uint8_t index = wrap_card_index((int)s_current_card_index + slot);
        const int32_t x = HOME_CARD_CENTER_X + slot * HOME_CARD_STEP + offset;
        position_card(index, x);
    }
}

static int32_t carousel_jelly_path(const lv_anim_t *animation)
{
    const int32_t smooth = lv_anim_path_ease_out(animation);
    const int32_t overshoot = lv_anim_path_overshoot(animation);
    return smooth + (overshoot - smooth) / 2;
}

static void carousel_animation_completed(lv_anim_t *animation)
{
    (void)animation;
    s_current_card_index = s_carousel_target_index;
    rebuild_card_window();
    s_carousel_animating = false;
}

static void home_gesture_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_GESTURE || s_carousel_animating) {
        return;
    }
    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL) {
        return;
    }
    const lv_dir_t direction = lv_indev_get_gesture_dir(indev);
    int end_x = 0;
    if (direction == LV_DIR_LEFT) {
        s_carousel_target_index = wrap_card_index((int)s_current_card_index + 1);
        end_x = -HOME_CARD_STEP;
    } else if (direction == LV_DIR_RIGHT) {
        s_carousel_target_index = wrap_card_index((int)s_current_card_index - 1);
        end_x = HOME_CARD_STEP;
    } else {
        return;
    }

    s_carousel_animating = true;
    lv_indev_wait_release(indev);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, s_card_layer);
    lv_anim_set_exec_cb(&animation, carousel_set_offset);
    lv_anim_set_values(&animation, 0, end_x);
    lv_anim_set_duration(&animation, 340);
    lv_anim_set_path_cb(&animation, carousel_jelly_path);
    lv_anim_set_completed_cb(&animation, carousel_animation_completed);
    lv_anim_start(&animation);
}

static void create_home_screen(void)
{
    s_home_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_home_screen, lv_color_hex(0x07111F), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_home_screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(s_home_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_home_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_home_screen, home_gesture_event_cb, LV_EVENT_GESTURE, NULL);

    lv_obj_t *title = lv_label_create(s_home_screen);
    lv_label_set_text(title, "Luna");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0xF8FAFC), LV_PART_MAIN);
    lv_obj_set_pos(title, 30, 24);

    s_home_wifi_label = lv_label_create(s_home_screen);
    lv_label_set_text(s_home_wifi_label, "Wi-Fi starting");
    lv_obj_set_style_text_font(s_home_wifi_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_home_wifi_label, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_align(s_home_wifi_label, LV_ALIGN_TOP_RIGHT, -30, 22);

    s_agent_label = lv_label_create(s_home_screen);
    lv_label_set_text(s_agent_label, "PC starting");
    lv_obj_set_style_text_font(s_agent_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_agent_label, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_align(s_agent_label, LV_ALIGN_TOP_RIGHT, -30, 48);

    s_card_layer = lv_obj_create(s_home_screen);
    lv_obj_set_pos(s_card_layer, 0, HOME_CARD_TOP);
    lv_obj_set_size(s_card_layer, HOME_SCREEN_WIDTH, HOME_CARD_HEIGHT);
    lv_obj_set_style_bg_opa(s_card_layer, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_card_layer, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_card_layer, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_card_layer, LV_OBJ_FLAG_SCROLLABLE);

    s_card_indicator_label = lv_label_create(s_home_screen);
    lv_obj_set_style_text_font(s_card_indicator_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_card_indicator_label, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_align(s_card_indicator_label, LV_ALIGN_BOTTOM_MID, 0, -112);

    lv_obj_t *diagnostics = lv_button_create(s_home_screen);
    lv_obj_set_size(diagnostics, 190, 68);
    lv_obj_set_pos(diagnostics, 30, 620);
    lv_obj_set_style_radius(diagnostics, 20, LV_PART_MAIN);
    lv_obj_set_style_bg_color(diagnostics, lv_color_hex(0x1E293B), LV_PART_MAIN);
    lv_obj_add_event_cb(diagnostics, screen_switch_event_cb, LV_EVENT_CLICKED,
                        s_diagnostic_screen);
    lv_obj_t *diagnostics_label = lv_label_create(diagnostics);
    lv_label_set_text(diagnostics_label, "Diagnostics");
    lv_obj_set_style_text_font(diagnostics_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_center(diagnostics_label);

    lv_obj_t *hint = lv_label_create(s_home_screen);
    lv_label_set_text(hint, "Swipe left or right");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x64748B), LV_PART_MAIN);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_RIGHT, -30, -44);

    rebuild_card_window();
}

static void touch_panel_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    ++s_touch_count;
    if (!luna_usb_is_connected()) {
        lv_label_set_text_fmt(s_touch_label, "Touch %u / USB offline", s_touch_count);
        return;
    }

    const esp_err_t result = luna_usb_send_touch_test(s_touch_count);
    lv_label_set_text_fmt(s_touch_label, result == ESP_OK ? "Touch %u / USB sent"
                                                          : "Touch %u / USB error",
                          s_touch_count);
}

static void usb_event_handler(const luna_usb_event_t *event, void *context)
{
    (void)context;

    switch (event->type) {
    case LUNA_USB_EVENT_DRIVER_READY:
    case LUNA_USB_EVENT_ATTACHED:
        ui_post(LUNA_COMPONENT_USB, LUNA_STATUS_BUSY, event->detail, 0);
        break;
    case LUNA_USB_EVENT_CONNECTED:
    case LUNA_USB_EVENT_HANDSHAKE:
    case LUNA_USB_EVENT_PING:
        ui_post(LUNA_COMPONENT_USB, LUNA_STATUS_READY, event->detail, 0);
        break;
    case LUNA_USB_EVENT_DISCONNECTED:
        ui_post(LUNA_COMPONENT_USB, LUNA_STATUS_WARNING, event->detail, 0);
        break;
    case LUNA_USB_EVENT_ERROR:
        ui_post(LUNA_COMPONENT_USB, LUNA_STATUS_WARNING, event->detail, 0);
        break;
    }
}

static int16_t triangle_sample(unsigned phase)
{
    const unsigned quadrant = (phase >> 14) & 0x3U;
    const int32_t offset = (int32_t)(phase & 0x3FFFU);
    int32_t sample;

    switch (quadrant) {
    case 0:
        sample = offset;
        break;
    case 1:
        sample = 0x3FFF - offset;
        break;
    case 2:
        sample = -offset;
        break;
    default:
        sample = -0x3FFF + offset;
        break;
    }
    return (int16_t)(sample / 2);
}

static void speaker_test_task(void *arg)
{
    (void)arg;
    int16_t samples[TONE_FRAME_COUNT * AUDIO_CHANNEL_COUNT];
    unsigned phase = 0;
    const unsigned phase_step = (880U * 65536U) / AUDIO_SAMPLE_RATE;

    for (size_t frame = 0; frame < TONE_FRAME_COUNT; ++frame) {
        const int16_t sample = triangle_sample(phase);
        samples[frame * 2] = sample;
        samples[frame * 2 + 1] = sample;
        phase = (phase + phase_step) & 0xFFFFU;
    }

    ui_post(LUNA_COMPONENT_AUDIO, LUNA_STATUS_BUSY, "Playing an 880 Hz speaker test", 0);
    esp_err_t result = ESP_OK;
    for (int repeat = 0; repeat < TONE_REPEAT_COUNT && result == ESP_OK; ++repeat) {
        result = esp_codec_dev_write(s_speaker_dev, samples, sizeof(samples));
    }

    if (result == ESP_OK) {
        ui_post(LUNA_COMPONENT_AUDIO, LUNA_STATUS_READY, "Speaker test complete", 0);
    } else {
        ESP_LOGE(TAG, "Speaker test failed: %s", esp_err_to_name(result));
        ui_post(LUNA_COMPONENT_AUDIO, LUNA_STATUS_FAILED, esp_err_to_name(result), 0);
        s_audio_ready = false;
    }
    s_audio_busy = false;
    vTaskDelete(NULL);
}

static void speaker_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !s_audio_ready || s_audio_busy) {
        return;
    }

    s_audio_busy = true;
    if (xTaskCreate(speaker_test_task, "speaker_test", 4096, NULL, 5, NULL) != pdPASS) {
        s_audio_busy = false;
        ui_post(LUNA_COMPONENT_AUDIO, LUNA_STATUS_FAILED, "Unable to start speaker task", 0);
    }
}

static lv_obj_t *create_status_label(lv_obj_t *parent, int y, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 26, y);
    return label;
}

static lv_obj_t *create_action_button(lv_obj_t *screen, int x, const char *text,
                                      lv_event_cb_t callback)
{
    lv_obj_t *button = lv_button_create(screen);
    lv_obj_set_size(button, 290, 104);
    lv_obj_align(button, LV_ALIGN_BOTTOM_MID, x, -45);
    lv_obj_set_style_radius(button, 24, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x172554), LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_hex(0x3B82F6), LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 2, LV_PART_MAIN);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0xDBEAFE), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(label);
    return button;
}

static esp_err_t ui_start(void)
{
    bsp_display_cfg_t display_config = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_0,
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,
        .touch_flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    display_config.lv_adapter_cfg.task_stack_size = LVGL_TASK_STACK_SIZE;
    ESP_LOGI(TAG, "Starting LVGL with %u-byte task stack",
             (unsigned)display_config.lv_adapter_cfg.task_stack_size);
    ESP_RETURN_ON_FALSE(bsp_display_start_with_config(&display_config) != NULL, ESP_FAIL, TAG,
                        "Display start failed");
    ESP_RETURN_ON_ERROR(bsp_display_backlight_on(), TAG, "Backlight start failed");
    ESP_RETURN_ON_FALSE(bsp_display_lock(pdMS_TO_TICKS(2000)), ESP_ERR_TIMEOUT, TAG,
                        "Display lock failed");

    lv_obj_t *screen = lv_screen_active();
    s_diagnostic_screen = screen;
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0B1020), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Luna hardware check");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF8FAFC), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 36);

    lv_obj_t *subtitle = lv_label_create(screen);
    lv_label_set_text(subtitle, "ESP32-P4 rev 1.3 / IDF 6.0.2");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 78);

    lv_obj_t *status_card = lv_obj_create(screen);
    lv_obj_set_size(status_card, 640, 335);
    lv_obj_align(status_card, LV_ALIGN_TOP_MID, 0, 120);
    lv_obj_set_style_radius(status_card, 28, LV_PART_MAIN);
    lv_obj_set_style_bg_color(status_card, lv_color_hex(0x111827), LV_PART_MAIN);
    lv_obj_set_style_border_color(status_card, lv_color_hex(0x334155), LV_PART_MAIN);
    lv_obj_set_style_border_width(status_card, 2, LV_PART_MAIN);
    lv_obj_clear_flag(status_card, LV_OBJ_FLAG_SCROLLABLE);

    s_wifi_label = create_status_label(status_card, 20, "Wi-Fi: starting");
    s_usb_label = create_status_label(status_card, 60, "USB: starting");
    s_storage_label = create_status_label(status_card, 100, "TF card: starting");
    s_audio_label = create_status_label(status_card, 140, "Audio: starting");
    s_pc_link_label = create_status_label(status_card, 230, "PC link: waiting");

    s_mic_value_label = lv_label_create(status_card);
    lv_label_set_text(s_mic_value_label, "Mic 0%");
    lv_obj_set_style_text_color(s_mic_value_label, lv_color_hex(0xCBD5E1), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_mic_value_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(s_mic_value_label, LV_ALIGN_TOP_LEFT, 26, 193);

    s_mic_bar = lv_bar_create(status_card);
    lv_obj_set_size(s_mic_bar, 480, 18);
    lv_obj_align(s_mic_bar, LV_ALIGN_TOP_RIGHT, -26, 195);
    lv_bar_set_range(s_mic_bar, 0, 100);
    lv_bar_set_value(s_mic_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_mic_bar, lv_color_hex(0x1E293B), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_mic_bar, lv_color_hex(0x22C55E), LV_PART_INDICATOR);

    s_detail_label = lv_label_create(status_card);
    lv_label_set_text(s_detail_label, "Running non-destructive hardware tests");
    lv_obj_set_width(s_detail_label, 580);
    lv_obj_set_style_text_align(s_detail_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_detail_label, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_detail_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(s_detail_label, LV_ALIGN_BOTTOM_MID, 0, -28);

    s_runtime_label = lv_label_create(screen);
    lv_label_set_text(s_runtime_label, "Uptime --   Internal heap --");
    lv_obj_set_style_text_color(s_runtime_label, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_runtime_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(s_runtime_label, LV_ALIGN_TOP_MID, 0, 478);

    s_usb_stats_label = lv_label_create(screen);
    lv_label_set_text(s_usb_stats_label, "USB opens --  hello --  requests --");
    lv_obj_set_style_text_color(s_usb_stats_label, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_usb_stats_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(s_usb_stats_label, LV_ALIGN_TOP_MID, 0, 505);

    s_usb_errors_label = lv_label_create(screen);
    lv_label_set_text(s_usb_errors_label, "Timeout --  protocol --  queue --  TX --");
    lv_obj_set_style_text_color(s_usb_errors_label, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_usb_errors_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(s_usb_errors_label, LV_ALIGN_TOP_MID, 0, 532);

    lv_obj_t *touch_button = create_action_button(screen, -160, "Touch / USB test",
                                                   touch_panel_event_cb);
    s_touch_label = lv_obj_get_child(touch_button, 0);

    s_speaker_button = create_action_button(screen, 160, "Test speaker",
                                             speaker_button_event_cb);
    lv_obj_add_state(s_speaker_button, LV_STATE_DISABLED);

    create_home_screen();

    lv_obj_t *home_button = lv_button_create(s_diagnostic_screen);
    lv_obj_set_size(home_button, 96, 52);
    lv_obj_set_pos(home_button, 18, 22);
    lv_obj_set_style_radius(home_button, 16, LV_PART_MAIN);
    lv_obj_set_style_bg_color(home_button, lv_color_hex(0x1E293B), LV_PART_MAIN);
    lv_obj_add_event_cb(home_button, screen_switch_event_cb, LV_EVENT_CLICKED, s_home_screen);
    lv_obj_t *home_label = lv_label_create(home_button);
    lv_label_set_text(home_label, "Home");
    lv_obj_set_style_text_font(home_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_center(home_label);

    lv_timer_t *ui_event_timer = lv_timer_create(ui_event_timer_cb, UI_EVENT_TIMER_MS, NULL);
    if (ui_event_timer == NULL) {
        bsp_display_unlock();
        return ESP_ERR_NO_MEM;
    }
    if (lv_timer_create(clock_timer_cb, 1000, NULL) == NULL) {
        bsp_display_unlock();
        return ESP_ERR_NO_MEM;
    }

    lv_screen_load(s_home_screen);

    bsp_display_unlock();
    return ESP_OK;
}

static void sync_music_cover(const luna_music_state_t *music)
{
    if (!music->cover_available || music->cover_id[0] == '\0') {
        if (s_loaded_cover_id[0] != '\0') {
            const luna_cover_event_t clear_event = {0};
            if (xQueueSend(s_cover_ui_queue, &clear_event, 0) == pdTRUE) {
                s_loaded_cover_id[0] = '\0';
            }
        }
        return;
    }
    if (strcmp(s_loaded_cover_id, music->cover_id) == 0) {
        return;
    }

    uint8_t *data = NULL;
    size_t length = 0;
    const esp_err_t result = luna_agent_fetch_cover(music->cover_id, &data, &length);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Music cover fetch failed: %s", esp_err_to_name(result));
        return;
    }

    const luna_cover_event_t cover_event = {
        .data = data,
        .length = length,
    };
    if (xQueueSend(s_cover_ui_queue, &cover_event, 0) == pdTRUE) {
        strlcpy(s_loaded_cover_id, music->cover_id, sizeof(s_loaded_cover_id));
    } else {
        free(data);
    }
}

static void weather_update_handler(const luna_weather_state_t *weather, void *context)
{
    (void)context;
    ui_post_extended(LUNA_COMPONENT_WEATHER,
                     weather->available && !weather->stale
                         ? LUNA_STATUS_READY : LUNA_STATUS_WARNING,
                     weather->summary, weather->details, weather->temperature_c);
}

static void agent_sync_task(void *arg)
{
    (void)arg;
    while (true) {
        luna_agent_state_t state;
        const esp_err_t result = luna_agent_fetch_state(&state);
        if (result == ESP_OK) {
            luna_weather_accept_pc_time(state.generated_at);
            ui_post(LUNA_COMPONENT_AGENT, LUNA_STATUS_READY, state.pc.name, 0);
            ui_post(LUNA_COMPONENT_PC_LINK, LUNA_STATUS_READY,
                    state.transport == LUNA_AGENT_TRANSPORT_USB ? "USB" : "Wi-Fi HTTP", 0);
            ui_post_extended(LUNA_COMPONENT_MUSIC,
                             state.music.controllable ? LUNA_STATUS_READY : LUNA_STATUS_WARNING,
                             state.music.title, state.music.artist,
                             state.music.playing ? 1 : 0);
            ui_post_volume(&state.volume);
            ui_post_extended(LUNA_COMPONENT_CODEX,
                             state.codex.available ? LUNA_STATUS_READY : LUNA_STATUS_WARNING,
                             state.codex.summary, state.codex.reset_text,
                             state.codex.remaining_percent);
            ui_post_extended(LUNA_COMPONENT_PROJECT, LUNA_STATUS_READY, state.project.name,
                             state.project.path, 0);
            luna_weather_accept_pc_settings(&state.weather);
            sync_music_cover(&state.music);
        } else {
            ui_post(LUNA_COMPONENT_AGENT, LUNA_STATUS_WARNING, esp_err_to_name(result), 0);
            ui_post(LUNA_COMPONENT_PC_LINK, LUNA_STATUS_WARNING,
                    luna_usb_is_ready() ? "USB stalled" : "Wi-Fi unavailable", 0);
        }
        vTaskDelay(pdMS_TO_TICKS(CONFIG_LUNA_AGENT_POLL_SECONDS * 1000));
    }
}

static void agent_action_task(void *arg)
{
    (void)arg;
    luna_action_request_t request;
    while (true) {
        if (xQueueReceive(s_action_queue, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        const char *action_name = NULL;
        switch (request.action) {
        case LUNA_ACTION_PREVIOUS:
            action_name = "music.previous";
            break;
        case LUNA_ACTION_PLAY:
            action_name = "music.play";
            break;
        case LUNA_ACTION_PAUSE:
            action_name = "music.pause";
            break;
        case LUNA_ACTION_NEXT:
            action_name = "music.next";
            break;
        case LUNA_ACTION_VOLUME_SET:
            action_name = "music.volume_set";
            break;
        case LUNA_ACTION_MUTE:
            action_name = "music.mute";
            break;
        default:
            continue;
        }

        const uint64_t request_id = ((uint64_t)esp_random() << 32) |
                                    ((uint64_t)esp_timer_get_time() & UINT32_MAX);
        luna_agent_action_result_t action_result;
        const esp_err_t result = luna_agent_send_action(
            action_name, request_id, request.value, &action_result);
        ui_post(LUNA_COMPONENT_AGENT,
                result == ESP_OK ? LUNA_STATUS_READY : LUNA_STATUS_WARNING,
                result == ESP_OK ? "Command delivered" : esp_err_to_name(result), 0);
        if (result != ESP_OK) {
            ui_post(LUNA_COMPONENT_ACTION_RESULT, LUNA_STATUS_FAILED,
                    esp_err_to_name(result), request.action);
            continue;
        }
        ui_post(LUNA_COMPONENT_ACTION_RESULT, LUNA_STATUS_READY, NULL, request.action);
        if (action_result.has_volume) {
            ui_post_volume(&action_result.volume);
        }
        if (action_result.has_music) {
            ui_post_extended(LUNA_COMPONENT_MUSIC,
                             action_result.music.controllable ? LUNA_STATUS_READY
                                                              : LUNA_STATUS_WARNING,
                             action_result.music.title, action_result.music.artist,
                             action_result.music.playing ? 1 : 0);
            sync_music_cover(&action_result.music);
        } else if (request.action == LUNA_ACTION_PLAY || request.action == LUNA_ACTION_PAUSE ||
                   request.action == LUNA_ACTION_PREVIOUS || request.action == LUNA_ACTION_NEXT) {
            luna_agent_state_t state;
            if (luna_agent_fetch_state(&state) == ESP_OK) {
                ui_post_extended(LUNA_COMPONENT_MUSIC,
                                 state.music.controllable ? LUNA_STATUS_READY
                                                          : LUNA_STATUS_WARNING,
                                 state.music.title, state.music.artist,
                                 state.music.playing ? 1 : 0);
                ui_post_volume(&state.volume);
                sync_music_cover(&state.music);
            } else {
                ui_post(LUNA_COMPONENT_ACTION_RESULT, LUNA_STATUS_FAILED,
                        "Unable to confirm playback", request.action);
            }
        }
    }
}

static void start_agent_sync_task(void)
{
    if (s_agent_task_started) {
        return;
    }
    if (xTaskCreate(agent_sync_task, "agent_sync", 8192, NULL, 4, NULL) == pdPASS) {
        s_agent_task_started = true;
    } else {
        ui_post(LUNA_COMPONENT_AGENT, LUNA_STATUS_FAILED,
                "Unable to start PC sync task", 0);
    }
}

static void wifi_schedule_recovery(void)
{
    if (atomic_load(&s_wifi_has_ip) || s_wifi_recovery_timer == NULL ||
        esp_timer_is_active(s_wifi_recovery_timer)) {
        return;
    }
    const esp_err_t result = esp_timer_start_once(s_wifi_recovery_timer,
                                                   WIFI_RECOVERY_INTERVAL_US);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi recovery timer failed: %s", esp_err_to_name(result));
    }
}

static void wifi_recovery_timer_cb(void *arg)
{
    (void)arg;
    if (atomic_load(&s_wifi_has_ip)) {
        return;
    }
    ESP_LOGI(TAG, "Wi-Fi recovery attempt");
    const esp_err_t result = esp_wifi_connect();
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi recovery connect failed: %s", esp_err_to_name(result));
        wifi_schedule_recovery();
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                               void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ui_post(LUNA_COMPONENT_WIFI, LUNA_STATUS_BUSY, "Waiting for access point", 0);
        esp_wifi_connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        atomic_store(&s_wifi_has_ip, false);
        luna_agent_set_http_ready(false);
        luna_weather_set_network_ready(false);
        if (!luna_usb_is_ready()) {
            ui_post(LUNA_COMPONENT_AGENT, LUNA_STATUS_WARNING, "Waiting for Wi-Fi", 0);
        }
        if (s_wifi_retry_count < CONFIG_LUNA_WIFI_MAXIMUM_RETRY) {
            ++s_wifi_retry_count;
            char detail[UI_DETAIL_LENGTH];
            snprintf(detail, sizeof(detail), "Wi-Fi retry %d of %d", s_wifi_retry_count,
                     CONFIG_LUNA_WIFI_MAXIMUM_RETRY);
            ui_post(LUNA_COMPONENT_WIFI, LUNA_STATUS_BUSY, detail, 0);
            esp_wifi_connect();
        } else {
            ui_post(LUNA_COMPONENT_WIFI, LUNA_STATUS_WARNING,
                    "Wi-Fi offline; retrying every 30 seconds", 0);
            ESP_LOGW(TAG, "Wi-Fi immediate retries exhausted; recovery in 30 seconds");
            wifi_schedule_recovery();
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        atomic_store(&s_wifi_has_ip, true);
        const ip_event_got_ip_t *event = event_data;
        char detail[UI_DETAIL_LENGTH];
        snprintf(detail, sizeof(detail), "Wi-Fi IPv4: " IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_retry_count = 0;
        if (s_wifi_recovery_timer != NULL && esp_timer_is_active(s_wifi_recovery_timer)) {
            esp_timer_stop(s_wifi_recovery_timer);
        }
        luna_agent_set_http_ready(true);
        luna_weather_set_network_ready(true);
        ui_post(LUNA_COMPONENT_WIFI, LUNA_STATUS_READY, detail, 0);
        if (luna_agent_is_configured()) {
            start_agent_sync_task();
        }
    }
}

static void clock_ntp_synced_callback(struct timeval *time_value)
{
    (void)time_value;
    s_clock_ntp_synced = true;
    ESP_LOGI(TAG, "Clock synchronized by NTP");
}

static esp_err_t wifi_start(void)
{
    if (CONFIG_LUNA_WIFI_SSID[0] == '\0') {
        ui_post(LUNA_COMPONENT_WIFI, LUNA_STATUS_WARNING,
                "Configure the 2.4 GHz Wi-Fi SSID in menuconfig", 0);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Network interface init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Event loop init failed");
    ESP_RETURN_ON_FALSE(esp_netif_create_default_wifi_sta() != NULL, ESP_FAIL, TAG,
                        "Wi-Fi station interface init failed");

    const wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "C6 hosted Wi-Fi init failed");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL), TAG,
        "Wi-Fi event registration failed");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL), TAG,
        "IP event registration failed");

    const esp_timer_create_args_t recovery_timer_config = {
        .callback = wifi_recovery_timer_cb,
        .name = "wifi_recovery",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&recovery_timer_config, &s_wifi_recovery_timer), TAG,
                        "Wi-Fi recovery timer creation failed");

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, CONFIG_LUNA_WIFI_SSID,
            sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, CONFIG_LUNA_WIFI_PASSWORD,
            sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode =
        CONFIG_LUNA_WIFI_PASSWORD[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Station mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG,
                        "Station config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Wi-Fi start failed");
    esp_sntp_config_t time_config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_LUNA_NTP_SERVER);
    time_config.sync_cb = clock_ntp_synced_callback;
    const esp_err_t time_result = esp_netif_sntp_init(&time_config);
    if (time_result != ESP_OK) {
        ESP_LOGW(TAG, "Clock sync initialization failed: %s", esp_err_to_name(time_result));
    }
    return ESP_OK;
}

#ifdef CONFIG_LUNA_ENABLE_TF_TEST
static esp_err_t storage_test(void)
{
    ui_post(LUNA_COMPONENT_STORAGE, LUNA_STATUS_BUSY, "Mounting TF card", 0);
    ESP_RETURN_ON_ERROR(bsp_sdcard_mount(), TAG, "TF card mount failed");

    static const char marker[] = "luna-storage-check-v1\n";
    const char path[] = BSP_SD_MOUNT_POINT "/.luna_probe.tmp";
    FILE *file = fopen(path, "wb");
    ESP_RETURN_ON_FALSE(file != NULL, ESP_FAIL, TAG, "TF card write open failed");
    const size_t written = fwrite(marker, 1, sizeof(marker), file);
    fclose(file);
    ESP_RETURN_ON_FALSE(written == sizeof(marker), ESP_FAIL, TAG, "TF card write failed");

    char readback[sizeof(marker)] = {0};
    file = fopen(path, "rb");
    ESP_RETURN_ON_FALSE(file != NULL, ESP_FAIL, TAG, "TF card read open failed");
    const size_t read = fread(readback, 1, sizeof(readback), file);
    fclose(file);
    unlink(path);
    ESP_RETURN_ON_FALSE(read == sizeof(marker) && memcmp(readback, marker, sizeof(marker)) == 0,
                        ESP_FAIL, TAG, "TF card verification failed");

    const uint64_t capacity_mib =
        ((uint64_t)bsp_sdcard->csd.capacity * bsp_sdcard->csd.sector_size) / (1024U * 1024U);
    char detail[UI_DETAIL_LENGTH];
    snprintf(detail, sizeof(detail), "TF %s: %llu MiB, read/write OK", bsp_sdcard->cid.name,
             (unsigned long long)capacity_mib);
    ui_post(LUNA_COMPONENT_STORAGE, LUNA_STATUS_READY, detail, 0);
    return ESP_OK;
}
#endif

static void microphone_level_task(void *arg)
{
    (void)arg;
    float displayed_level = 0.0f;
    int16_t *samples = malloc(MIC_BUFFER_SAMPLES * sizeof(int16_t));
    if (samples == NULL) {
        ui_post(LUNA_COMPONENT_AUDIO, LUNA_STATUS_FAILED, "Microphone buffer allocation failed", 0);
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        const esp_err_t result =
            esp_codec_dev_read(s_microphone_dev, samples, MIC_BUFFER_SAMPLES * sizeof(int16_t));
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "Microphone read failed: %s", esp_err_to_name(result));
            ui_post(LUNA_COMPONENT_AUDIO, LUNA_STATUS_FAILED, "Microphone read failed", 0);
            break;
        }

        uint64_t sum_squares = 0;
        for (size_t i = 0; i < MIC_BUFFER_SAMPLES; ++i) {
            const int32_t sample = samples[i];
            sum_squares += (uint64_t)((int64_t)sample * sample);
        }

        const float rms = sqrtf((float)sum_squares / MIC_BUFFER_SAMPLES);
        float target_level = 0.0f;
        if (rms > 1.0f) {
            const float dbfs = 20.0f * log10f(rms / 32768.0f);
            target_level =
                (dbfs - MIC_METER_FLOOR_DBFS) * 100.0f /
                (MIC_METER_CEILING_DBFS - MIC_METER_FLOOR_DBFS);
        }
        if (target_level < 0.0f) {
            target_level = 0.0f;
        } else if (target_level > 100.0f) {
            target_level = 100.0f;
        }

        const float smoothing =
            target_level > displayed_level ? MIC_METER_ATTACK : MIC_METER_RELEASE;
        displayed_level += (target_level - displayed_level) * smoothing;
        const int level = (int)(displayed_level + 0.5f);
        ui_post(LUNA_COMPONENT_MIC, LUNA_STATUS_READY, NULL, level);
    }

    free(samples);
    vTaskDelete(NULL);
}

static esp_err_t audio_bus_init_16k(void)
{
    const i2s_std_gpio_config_t tx_gpio = {
        .mclk = BSP_I2S_MCLK,
        .bclk = BSP_I2S_SCLK,
        .ws = BSP_I2S_LCLK,
        .dout = BSP_I2S_DOUT,
        .din = BSP_I2S_DSIN,
        .invert_flags = {
            .mclk_inv = false,
            .bclk_inv = false,
            .ws_inv = false,
        },
    };
    const i2s_tdm_gpio_config_t rx_gpio = {
        .mclk = BSP_I2S_MCLK,
        .bclk = BSP_I2S_SCLK,
        .ws = BSP_I2S_LCLK,
        .dout = BSP_I2S_DOUT,
        .din = BSP_I2S_DSIN,
        .invert_flags = {
            .mclk_inv = false,
            .bclk_inv = false,
            .ws_inv = false,
        },
    };
    i2s_std_config_t tx_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = tx_gpio,
    };
    i2s_tdm_config_t rx_config = {
        .clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_TDM_PHILIP_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO,
            I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3),
        .gpio_cfg = rx_gpio,
    };
    tx_config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    tx_config.gpio_cfg.din = I2S_GPIO_UNUSED;
    rx_config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    rx_config.clk_cfg.bclk_div = 8;
    rx_config.slot_cfg.total_slot = BSP_AUDIO_TDM_SLOT_COUNT;
    rx_config.gpio_cfg.dout = I2S_GPIO_UNUSED;
    return bsp_audio_init_tx_std_rx_tdm(&tx_config, &rx_config);
}

static esp_err_t audio_start(void)
{
    ui_post(LUNA_COMPONENT_AUDIO, LUNA_STATUS_BUSY, "Initializing speaker and microphones", 0);
    ESP_RETURN_ON_ERROR(audio_bus_init_16k(), TAG, "Audio bus init failed");

    esp_codec_dev_sample_info_t playback_format = {
        .sample_rate = AUDIO_SAMPLE_RATE,
        .channel = AUDIO_CHANNEL_COUNT,
        .bits_per_sample = AUDIO_BITS_PER_SAMPLE,
    };
    s_speaker_dev = bsp_audio_codec_speaker_init();
    ESP_RETURN_ON_FALSE(s_speaker_dev != NULL, ESP_FAIL, TAG, "ES8311 init failed");
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(s_speaker_dev, &playback_format), TAG,
                        "ES8311 open failed");
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_out_vol(s_speaker_dev, CONFIG_LUNA_SPEAKER_VOLUME), TAG,
                        "Speaker volume failed");
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_out_mute(s_speaker_dev, false), TAG,
                        "Speaker unmute failed");

    esp_codec_dev_sample_info_t microphone_format = {
        .sample_rate = AUDIO_SAMPLE_RATE,
        .channel = BSP_AUDIO_TDM_SLOT_COUNT,
        .bits_per_sample = AUDIO_BITS_PER_SAMPLE,
        .channel_mask = BSP_AUDIO_TDM_SLOT_MASK_FL,
    };
    s_microphone_dev = bsp_audio_codec_microphone_init();
    ESP_RETURN_ON_FALSE(s_microphone_dev != NULL, ESP_FAIL, TAG, "ES7210 init failed");
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(s_microphone_dev, &microphone_format), TAG,
                        "ES7210 open failed");
    ESP_RETURN_ON_ERROR(
        esp_codec_dev_set_in_gain(s_microphone_dev, (float)CONFIG_LUNA_MIC_GAIN_DB), TAG,
        "Microphone gain failed");

    s_audio_ready = true;
    char detail[UI_DETAIL_LENGTH];
    snprintf(detail, sizeof(detail), "Speaker and microphones ready; mic gain %d dB",
             CONFIG_LUNA_MIC_GAIN_DB);
    ui_post(LUNA_COMPONENT_AUDIO, LUNA_STATUS_READY, detail, 0);
    ESP_RETURN_ON_FALSE(
        xTaskCreate(microphone_level_task, "mic_level", 4096, NULL, 5, NULL) == pdPASS,
        ESP_ERR_NO_MEM, TAG, "Microphone meter task creation failed");
    return ESP_OK;
}

static void hardware_test_task(void *arg)
{
    (void)arg;
    esp_err_t result;
#ifdef CONFIG_LUNA_ENABLE_TF_TEST
    result = storage_test();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "TF card test failed: %s", esp_err_to_name(result));
        ui_post(LUNA_COMPONENT_STORAGE, LUNA_STATUS_FAILED, esp_err_to_name(result), 0);
    }
#else
    ui_post(LUNA_COMPONENT_STORAGE, LUNA_STATUS_WARNING,
            "TF test disabled during Wi-Fi stability testing", 0);
#endif

    result = audio_start();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Audio test initialization failed: %s", esp_err_to_name(result));
        ui_post(LUNA_COMPONENT_AUDIO, LUNA_STATUS_FAILED, esp_err_to_name(result), 0);
    }
    vTaskDelete(NULL);
}

void app_main(void)
{
    setenv("TZ", CONFIG_LUNA_TIMEZONE, 1);
    tzset();

    s_ui_queue = xQueueCreate(UI_QUEUE_DEPTH, sizeof(luna_ui_event_t));
    ESP_ERROR_CHECK(s_ui_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    s_mic_ui_queue = xQueueCreate(1, sizeof(luna_ui_event_t));
    ESP_ERROR_CHECK(s_mic_ui_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    s_cover_ui_queue = xQueueCreate(1, sizeof(luna_cover_event_t));
    ESP_ERROR_CHECK(s_cover_ui_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    s_action_queue = xQueueCreate(4, sizeof(luna_action_request_t));
    ESP_ERROR_CHECK(s_action_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(luna_agent_client_init());
    ESP_ERROR_CHECK(ui_start());
    ESP_ERROR_CHECK(xTaskCreate(agent_action_task, "agent_actions", 10240, NULL, 4, NULL) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);

    esp_err_t error = luna_usb_start(usb_event_handler, NULL);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "USB startup failed: %s", esp_err_to_name(error));
        ui_post(LUNA_COMPONENT_USB, LUNA_STATUS_FAILED, esp_err_to_name(error), 0);
    }
    error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        error = nvs_flash_init();
    }
    ESP_ERROR_CHECK(error);
    ESP_ERROR_CHECK(luna_weather_start(weather_update_handler, NULL));
    start_agent_sync_task();

    ESP_ERROR_CHECK(xTaskCreate(hardware_test_task, "hardware_test", 6144, NULL, 5, NULL) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);

    error = wifi_start();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Wi-Fi startup failed: %s", esp_err_to_name(error));
        ui_post(LUNA_COMPONENT_WIFI, LUNA_STATUS_FAILED, esp_err_to_name(error), 0);
    }
}
