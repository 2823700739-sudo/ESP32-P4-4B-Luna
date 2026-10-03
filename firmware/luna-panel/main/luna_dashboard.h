#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "cJSON.h"
typedef struct {
    bool online,computer_fresh,quota_fresh;
    double cpu,gpu,cpu_temp,gpu_temp,ram_used_gb,ram_total_gb,vram_used_gb,vram_total_gb;
    int primary_remaining,weekly_remaining;
    int64_t received_us,quota_sampled_ms;
    unsigned computer_age_ms;
    char gpu_name[128],project_name[192];
} luna_dashboard_state_t;
esp_err_t luna_dashboard_init(void);
void luna_dashboard_disconnect(void);
bool luna_dashboard_snapshot(luna_dashboard_state_t *out);
int luna_dashboard_message(const cJSON *root,const char *session,const char *boot,char *reply,size_t cap);
