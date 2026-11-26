#include "luna_weather_config.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

void luna_weather_config_clear(void *data, unsigned length)
{
    volatile unsigned char *p = data;
    while (length--) *p++ = 0;
}

static bool text(const char *s, size_t cap, bool empty)
{
    const char *end = memchr(s, 0, cap);
    if (!end || (!empty && end == s)) return false;
    /* Validate UTF-8, rejecting controls, overlong encodings and surrogates. */
    for (const unsigned char *p = (const unsigned char *)s; *p;) {
        unsigned c = *p++, n, value, minimum;
        if (c < 0x80) { if (c < 32 || c == 127) return false; continue; }
        if (c >= 0xc2 && c <= 0xdf) { n=1; value=c&31; minimum=0x80; }
        else if (c >= 0xe0 && c <= 0xef) { n=2; value=c&15; minimum=0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { n=3; value=c&7; minimum=0x10000; }
        else return false;
        while (n--) { if ((*p & 0xc0) != 0x80) return false; value=(value<<6)|(*p++&63); }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}

bool luna_weather_config_valid(const luna_weather_config_t *c)
{
    if (!c || c->version != 1 || !memchr(c->setup_id,0,sizeof(c->setup_id)) ||
        strlen(c->setup_id) != 32 || !text(c->ssid,sizeof(c->ssid),false) ||
        !text(c->password,sizeof(c->password),true) || !text(c->location,sizeof(c->location),false) ||
        !isfinite(c->latitude) || c->latitude < -90 || c->latitude > 90 ||
        !isfinite(c->longitude) || c->longitude < -180 || c->longitude > 180) return false;
    for (unsigned i=0;i<32;i++) if (!((c->setup_id[i]>='0' && c->setup_id[i]<='9') ||
                                                   (c->setup_id[i]>='a' && c->setup_id[i]<='f'))) return false;
    size_t length=strlen(c->password);
    if (length && length < 8) return false;
    for (size_t i=0;i<length;i++) {
        unsigned char ch=(unsigned char)c->password[i];
        if (ch < 32 || ch > 126) return false;
        if (length==64 && !((ch>='0'&&ch<='9')||(ch>='a'&&ch<='f')||(ch>='A'&&ch<='F'))) return false;
    }
    return true;
}

bool luna_weather_config_parse(const cJSON *root, luna_weather_config_t *out)
{
    if (!cJSON_IsObject(root) || !out) return false;
    luna_weather_config_t c={.version=1};
    const char *keys[]={"setup_id","ssid","password","location"};
    char *dest[]={c.setup_id,c.ssid,c.password,c.location};
    size_t caps[]={sizeof(c.setup_id),sizeof(c.ssid),sizeof(c.password),sizeof(c.location)};
    for (unsigned i=0;i<4;i++) {
        const cJSON *v=cJSON_GetObjectItemCaseSensitive(root,keys[i]);
        if (!cJSON_IsString(v) || strlen(v->valuestring)>=caps[i]) goto invalid;
        memcpy(dest[i],v->valuestring,strlen(v->valuestring)+1);
    }
    const cJSON *lat=cJSON_GetObjectItemCaseSensitive(root,"latitude");
    const cJSON *lon=cJSON_GetObjectItemCaseSensitive(root,"longitude");
    if (!cJSON_IsNumber(lat) || !cJSON_IsNumber(lon)) goto invalid;
    c.latitude=lat->valuedouble; c.longitude=lon->valuedouble;
    if (!luna_weather_config_valid(&c)) goto invalid;
    *out=c;
    luna_weather_config_clear(&c,sizeof(c));
    return true;
invalid:
    luna_weather_config_clear(&c,sizeof(c));
    return false;
}
