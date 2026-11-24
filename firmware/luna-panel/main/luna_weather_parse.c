#include <math.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "luna_weather_parse.h"

static bool number(const cJSON *o, const char *key, double min, double max, double *out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble < min || v->valuedouble > max) return false;
    *out = v->valuedouble; return true;
}
static bool daily_number(const cJSON *o, const char *key, double min, double max, double *out)
{
    const cJSON *a = cJSON_GetObjectItemCaseSensitive(o, key);
    const cJSON *v = cJSON_IsArray(a) ? cJSON_GetArrayItem(a, 0) : NULL;
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble < min || v->valuedouble > max) return false;
    *out = v->valuedouble; return true;
}
static bool unit(const cJSON *o, const char *key, const char *expected)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) && !strcmp(v->valuestring, expected);
}
static const char *condition_for_code(int code)
{
    switch (code) {
    case 0: return "晴朗";
    case 1: return "晴间多云";
    case 2: return "多云";
    case 3: return "阴";
    case 45: case 48: return "雾";
    case 51: case 53: case 55: case 56: case 57: return "毛毛雨";
    case 61: case 63: case 65: case 66: case 67: return "雨";
    case 71: case 73: case 75: case 77: return "雪";
    case 80: case 81: case 82: return "阵雨";
    case 85: case 86: return "阵雪";
    case 95: case 96: case 99: return "雷暴";
    default: return "天气变化";
    }
}
static bool observed_time(const char *s)
{
    if (!s || strlen(s) < 16 || strlen(s) >= 48) return false;
    for (unsigned i = 0; i < 16; i++) {
        if (i == 4 || i == 7) { if (s[i] != '-') return false; }
        else if (i == 10) { if (s[i] != 'T') return false; }
        else if (i == 13) { if (s[i] != ':') return false; }
        else if (s[i] < '0' || s[i] > '9') return false;
    }
    return true; // Display metadata only, never used to seed the system clock.
}

esp_err_t luna_weather_parse_forecast(const char *json, luna_weather_state_t *weather,
                                     luna_weather_metrics_t *metrics)
{
    if (!json || !weather || !metrics) return ESP_ERR_INVALID_RESPONSE;
    const char *end;
    cJSON *root = cJSON_ParseWithOpts(json, &end, true);
    if (!cJSON_IsObject(root)) { cJSON_Delete(root); return ESP_ERR_INVALID_RESPONSE; }
    const cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current");
    const cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    const cJSON *units = cJSON_GetObjectItemCaseSensitive(root, "current_units");
    const cJSON *daily_units = cJSON_GetObjectItemCaseSensitive(root, "daily_units");
    const cJSON *observed = cJSON_GetObjectItemCaseSensitive(current, "time");
    double temperature, apparent, humidity, wind, code, high, low, rain = 0;
    bool valid = cJSON_IsObject(current) && cJSON_IsObject(daily) && cJSON_IsString(observed) &&
        observed_time(observed->valuestring) &&
        unit(units, "temperature_2m", "°C") && unit(units, "apparent_temperature", "°C") &&
        unit(units, "relative_humidity_2m", "%") && unit(units, "wind_speed_10m", "m/s") &&
        unit(daily_units, "temperature_2m_max", "°C") && unit(daily_units, "temperature_2m_min", "°C") &&
        number(current, "temperature_2m", -100, 80, &temperature) &&
        number(current, "apparent_temperature", -120, 90, &apparent) &&
        number(current, "relative_humidity_2m", 0, 100, &humidity) &&
        number(current, "wind_speed_10m", 0, 140, &wind) &&
        number(current, "weather_code", 0, 99, &code) &&
        daily_number(daily, "temperature_2m_max", -100, 80, &high) &&
        daily_number(daily, "temperature_2m_min", -100, 80, &low);
    if (!valid || floor(code) != code || low > high) { cJSON_Delete(root); return ESP_ERR_INVALID_RESPONSE; }
    bool have_rain = unit(daily_units, "precipitation_probability_max", "%") &&
        daily_number(daily, "precipitation_probability_max", 0, 100, &rain);
    luna_weather_metrics_t m = {.available=true, .rain_available=have_rain,
        .apparent_c=(int)lround(apparent), .humidity_percent=(int)lround(humidity),
        .wind_decims_ms=(int)lround(wind * 10), .weather_code=(int)code,
        .high_c=(int)lround(high), .low_c=(int)lround(low), .rain_percent=(int)lround(rain)};
    luna_weather_state_t w = *weather;
    w.available=true; w.stale=false; w.temperature_c=(int)lround(temperature);
    strlcpy(w.condition, condition_for_code(m.weather_code), sizeof(w.condition));
    strlcpy(w.observed_at, observed->valuestring, sizeof(w.observed_at));
    snprintf(w.summary, sizeof(w.summary), "%s\n%s\nFeels %d C | Humidity %d%%",
             w.condition, w.location, m.apparent_c, m.humidity_percent);
    char rain_text[16] = "--";
    if (have_rain) snprintf(rain_text, sizeof(rain_text), "%d%%", m.rain_percent);
    snprintf(w.details, sizeof(w.details),
             "H %d / L %d C | Rain %s\nWind %d km/h\n%.4f %c | %.4f %c\n"
             "Updated %.5s %.5s\nOpen-Meteo / Luna Wi-Fi",
             m.high_c, m.low_c, rain_text, (int)lround(wind * 3.6),
             fabs(w.latitude), w.latitude >= 0 ? 'N' : 'S', fabs(w.longitude), w.longitude >= 0 ? 'E' : 'W',
             observed->valuestring + 5, observed->valuestring + 11);
    *weather=w; *metrics=m;
    cJSON_Delete(root); return ESP_OK;
}
