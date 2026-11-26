#include <string.h>
#include "luna_setup_request.h"
#include "luna_weather_config.h"

bool luna_setup_request_allowed(const char *host,const char *origin,const char *provided,const char *expected)
{
    if (!host || (strcmp(host,"192.168.4.1") && strcmp(host,"192.168.4.1:80"))) return false;
    if (origin && strcmp(origin,"http://192.168.4.1") && strcmp(origin,"http://192.168.4.1:80")) return false;
    return !expected || (expected[0] && provided && !strcmp(provided,expected));
}
static void erase(cJSON *root)
{
    if (!root) return;
    for (cJSON *p=root->child;p;p=p->next) erase(p);
    if (cJSON_IsString(root) && root->valuestring)
        luna_weather_config_clear(root->valuestring,strlen(root->valuestring));
}
void luna_setup_request_dispose(cJSON *root) {erase(root);cJSON_Delete(root);}

cJSON *luna_setup_request_parse(const char *data,size_t length)
{
    char copy[768]={0};cJSON *root=NULL;
    if (!data || length<2 || length>=sizeof(copy) || memchr(data,0,length)) return NULL;
    memcpy(copy,data,length);
    if (strstr(copy,"\\u0000")) goto invalid;
    bool quoted=false,escaped=false;unsigned depth=0;
    for (size_t i=0;i<length;i++) {
        char ch=copy[i];
        if (quoted) {
            if (escaped) escaped=false;
            else if (ch=='\\') escaped=true;
            else if (ch=='"') quoted=false;
        } else if (ch=='"') quoted=true;
        else if (ch=='[' || ch==']') goto invalid;
        else if (ch=='{') {if (++depth>1) goto invalid;}
        else if (ch=='}') {if (!depth) goto invalid;depth--;}
    }
    if (quoted || depth) goto invalid;
    root=cJSON_ParseWithLengthOpts(copy,length+1,NULL,true);
    if (!cJSON_IsObject(root) || cJSON_GetArraySize(root)>8) goto invalid;
    for (cJSON *a=root->child;a;a=a->next) {
        if (!a->string || (!cJSON_IsString(a) && !cJSON_IsNumber(a))) goto invalid;
        for (cJSON *b=a->next;b;b=b->next) if (b->string && !strcmp(a->string,b->string)) goto invalid;
    }
    luna_weather_config_clear(copy,sizeof(copy));return root;
invalid:
    luna_setup_request_dispose(root);luna_weather_config_clear(copy,sizeof(copy));return NULL;
}
