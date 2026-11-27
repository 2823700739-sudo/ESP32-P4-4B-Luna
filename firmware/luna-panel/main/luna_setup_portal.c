// Local phone setup only; no cloud relay or Internet forwarding.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_random.h"
#include "luna_setup_socket.h"
#include "luna_ble_wifi.h"
#include "luna_setup_portal.h"
#include "luna_setup_request.h"

extern const char setup_html_start[] asm("_binary_setup_html_start");
extern const char setup_html_end[] asm("_binary_setup_html_end");
static httpd_handle_t server;
static char token[33];

static bool header_is(httpd_req_t *req,const char *name,const char *value)
{
    char buffer[96];
    return httpd_req_get_hdr_value_len(req,name)<sizeof(buffer) &&
           httpd_req_get_hdr_value_str(req,name,buffer,sizeof(buffer))==ESP_OK && !strcmp(buffer,value);
}
static bool allowed(httpd_req_t *req,bool protected)
{
    char host[96]={0},origin[96]={0},provided[96]={0};
    bool has_origin=httpd_req_get_hdr_value_len(req,"Origin")>0;
    bool valid_host=httpd_req_get_hdr_value_str(req,"Host",host,sizeof(host))==ESP_OK;
    bool valid_origin=!has_origin || httpd_req_get_hdr_value_str(req,"Origin",origin,sizeof(origin))==ESP_OK;
    (void)httpd_req_get_hdr_value_str(req,"X-Luna-Token",provided,sizeof(provided));
    if (!valid_host || !valid_origin || !luna_setup_request_allowed(host,has_origin?origin:NULL,provided,protected?token:NULL)) {
        httpd_resp_set_hdr(req,"Connection","close");
        httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Open http://192.168.4.1 on the Luna setup network.");
        return false;
    }
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
    httpd_resp_set_hdr(req,"Referrer-Policy","no-referrer");
    return true;
}
static esp_err_t reply(httpd_req_t *req,cJSON *value)
{
    char *json=value?cJSON_PrintUnformatted(value):NULL;cJSON_Delete(value);
    if (!json) return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Memory unavailable.");
    httpd_resp_set_type(req,"application/json; charset=utf-8");
    esp_err_t rc=httpd_resp_sendstr(req,json);free(json);return rc;
}
static esp_err_t setup_error(httpd_req_t *req,bool temporary,const char *message)
{
    httpd_resp_set_status(req,temporary?"503 Service Unavailable":"400 Bad Request");
    httpd_resp_set_type(req,"text/plain; charset=utf-8");
    httpd_resp_set_hdr(req,"Connection","close");return httpd_resp_sendstr(req,message);
}
static void dispose(cJSON *root) { luna_setup_request_dispose(root); }
static cJSON *body(httpd_req_t *req)
{
    char data[768]={0};cJSON *root=NULL;
    if (!header_is(req,"Content-Type","application/json") || req->content_len<2 || req->content_len>=sizeof(data)) goto invalid;
    unsigned read=0;
    while (read<req->content_len) {
        int count=httpd_req_recv(req,data+read,req->content_len-read);
        if (count<=0) goto invalid;
        read+=count;
    }
    root=luna_setup_request_parse(data,read);
    if (!root) goto invalid;
    luna_weather_config_clear(data,sizeof(data));return root;
invalid:
    dispose(root);luna_weather_config_clear(data,sizeof(data));
    httpd_resp_set_hdr(req,"Connection","close");
    httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid or oversized JSON request.");return NULL;
}
static void random_id(char out[33])
{
    uint8_t bytes[16];esp_fill_random(bytes,sizeof(bytes));
    for (unsigned i=0;i<16;i++) snprintf(out+2*i,3,"%02x",bytes[i]);
    luna_weather_config_clear(bytes,sizeof(bytes));
}
static esp_err_t page(httpd_req_t *req)
{
    if (!allowed(req,false)) return ESP_OK;
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_set_hdr(req,"Content-Security-Policy","default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'; form-action 'none'; frame-ancestors 'none'; base-uri 'none'");
    return httpd_resp_send(req,setup_html_start,setup_html_end-setup_html_start-1);
}
static esp_err_t session(httpd_req_t *req)
{
    if (!allowed(req,false)) return ESP_OK;
    cJSON *json=cJSON_CreateObject();cJSON_AddStringToObject(json,"token",token);return reply(req,json);
}
static esp_err_t status(httpd_req_t *req)
{
    if (!allowed(req,true)) return ESP_OK;
    luna_ble_wifi_setup_status_t saved={0};luna_setup_ap_state_t ap={0};
    if (!luna_ble_wifi_setup_status(&saved) || !luna_setup_ap_snapshot(&ap))
        return setup_error(req,true,"Busy; retry status.");
    luna_ble_wifi_status_t wifi=luna_ble_wifi_status();
    cJSON *json=cJSON_CreateObject();
    cJSON_AddStringToObject(json,"setup_id",saved.setup_id);
    cJSON_AddBoolToObject(json,"saved",saved.saved);cJSON_AddNumberToObject(json,"state",saved.state);
    cJSON_AddNumberToObject(json,"error",saved.error);cJSON_AddNumberToObject(json,"wifi_error",ap.error);
    cJSON_AddStringToObject(json,"trial_id",ap.trial_id);
    cJSON_AddNumberToObject(json,"trial_state",ap.trial_state);
    cJSON_AddNumberToObject(json,"trial_error",ap.trial_error);
    cJSON_AddBoolToObject(json,"online",wifi.online);cJSON_AddBoolToObject(json,"clock_ready",time(NULL)>=1700000000);
    cJSON_AddBoolToObject(json,"weather_fresh",wifi.online && wifi.available && !wifi.cached);
    cJSON_AddNumberToObject(json,"remaining_seconds",ap.remaining_seconds);
    luna_weather_config_clear(&ap,sizeof(ap));return reply(req,json);
}
static esp_err_t network(httpd_req_t *req)
{
    if (!allowed(req,true)) return ESP_OK;
    cJSON *json=body(req);if (!json) return ESP_OK;
    /* Complete validation only: trial coordinates/name are NEVER saved or used for weather. */
    luna_weather_config_t config={0};
    /* Client chooses the id before sending, so a lost response can be reconciled. */
    cJSON_AddStringToObject(json,"location","Network trial");
    cJSON_AddNumberToObject(json,"latitude",0);cJSON_AddNumberToObject(json,"longitude",0);
    bool valid=cJSON_GetArraySize(json)==6 && luna_weather_config_parse(json,&config);
    bool accepted=valid && luna_ble_wifi_try_network(&config);
    dispose(json);luna_weather_config_clear(&config,sizeof(config));
    if (!accepted) return setup_error(req,valid,
                                            valid?"Setup busy; retry status first.":"Check Wi-Fi name/password.");
    httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,"{\"accepted\":true}");
}
static esp_err_t scan_networks(httpd_req_t *req)
{
    if (!allowed(req,true)) return ESP_OK;
    if (req->method==HTTP_POST) {
        cJSON *json=body(req);if (!json) return ESP_OK;
        bool valid=cJSON_GetArraySize(json)==0;dispose(json);
        if (!valid || !luna_setup_scan_request())
            return setup_error(req,valid,"Scan busy. Wait a few seconds and retry.");
        httpd_resp_set_type(req,"application/json");
        return httpd_resp_sendstr(req,"{\"accepted\":true}");
    }
    luna_setup_scan_t snapshot={0};
    if (!luna_setup_scan_snapshot(&snapshot)) return setup_error(req,true,"Scan busy.");
    cJSON *json=cJSON_CreateObject(),*networks=cJSON_AddArrayToObject(json,"networks");
    cJSON_AddNumberToObject(json,"state",snapshot.state);cJSON_AddNumberToObject(json,"error",snapshot.error);
    for (unsigned i=0;i<snapshot.count;i++) {
        cJSON *item=cJSON_CreateObject();
        cJSON_AddStringToObject(item,"ssid",snapshot.networks[i].ssid);
        cJSON_AddNumberToObject(item,"rssi",snapshot.networks[i].rssi);
        cJSON_AddBoolToObject(item,"secured",snapshot.networks[i].secured);
        cJSON_AddItemToArray(networks,item);
    }
    return reply(req,json);
}
static esp_err_t save(httpd_req_t *req)
{
    if (!allowed(req,true)) return ESP_OK;
    cJSON *json=body(req);if (!json) return ESP_OK;
    luna_weather_config_t config={0};
    bool valid=cJSON_GetArraySize(json)==6 && luna_weather_config_parse(json,&config);
    bool accepted=valid && luna_ble_wifi_configure(&config);
    dispose(json);luna_weather_config_clear(&config,sizeof(config));
    if (!accepted) return setup_error(req,valid,
                                            valid?"Setup busy; read status before retrying.":"Check network and city/coordinates.");
    httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,"{\"accepted\":true}");
}
typedef struct { char data[8192];size_t length;bool overflow; } city_response_t;
static esp_err_t city_data(esp_http_client_event_t *event)
{
    city_response_t *r=event->user_data;
    if (event->event_id==HTTP_EVENT_ON_DATA && event->data_len>0) {
        size_t count=event->data_len;
        if (count>sizeof(r->data)-r->length-1) {r->overflow=true;return ESP_FAIL;}
        memcpy(r->data+r->length,event->data,count);r->length+=count;r->data[r->length]=0;
    }
    return ESP_OK;
}
static esp_err_t cities(httpd_req_t *req)
{
    if (!allowed(req,true)) return ESP_OK;
    cJSON *query=body(req);if (!query) return ESP_OK;
    const cJSON *name=cJSON_GetObjectItemCaseSensitive(query,"query");
    char url[400]="https://geocoding-api.open-meteo.com/v1/search?count=5&language=zh&format=json&name=";
    size_t at=strlen(url);
    if (!cJSON_IsString(name) || strlen(name->valuestring)<2 || strlen(name->valuestring)>96) {
        dispose(query);return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Enter a city name.");
    }
    for (const unsigned char *p=(const unsigned char*)name->valuestring;*p;p++) {
        snprintf(url+at,sizeof(url)-at,"%%%02X",*p);at+=3;
    }
    dispose(query);
    if (!luna_ble_wifi_status().online || time(NULL)<1700000000)
        return setup_error(req,true,"Wait for Wi-Fi and network time, or enter coordinates manually.");
    city_response_t *response=calloc(1,sizeof(*response));
    if (!response) return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Memory unavailable.");
    esp_http_client_config_t config={.url=url,.crt_bundle_attach=esp_crt_bundle_attach,
        .timeout_ms=6000,.event_handler=city_data,.user_data=response,.disable_auto_redirect=true};
    esp_http_client_handle_t client=esp_http_client_init(&config);
    esp_err_t rc=client?esp_http_client_perform(client):ESP_FAIL;
    int code=client?esp_http_client_get_status_code(client):0;
    if (client) esp_http_client_cleanup(client);
    cJSON *source=rc==ESP_OK && code==200 && !response->overflow?cJSON_Parse(response->data):NULL;
    free(response);
    if (!source) return setup_error(req,true,"City search unavailable. Try pinyin, or enter coordinates manually.");
    cJSON *json=cJSON_CreateObject(),*results=cJSON_AddArrayToObject(json,"results");
    const cJSON *array=cJSON_GetObjectItemCaseSensitive(source,"results");
    for (int i=0;i<cJSON_GetArraySize(array) && i<5;i++) {
        const cJSON *city=cJSON_GetArrayItem(array,i);
        const cJSON *label=cJSON_GetObjectItemCaseSensitive(city,"name");
        const cJSON *lat=cJSON_GetObjectItemCaseSensitive(city,"latitude");
        const cJSON *lon=cJSON_GetObjectItemCaseSensitive(city,"longitude");
        if (!cJSON_IsString(label) || strlen(label->valuestring)>47 || !cJSON_IsNumber(lat) || !cJSON_IsNumber(lon)) continue;
        luna_weather_config_t valid={.version=1,.ssid="x",.setup_id="00000000000000000000000000000000",
            .latitude=lat->valuedouble,.longitude=lon->valuedouble};
        strlcpy(valid.location,label->valuestring,sizeof(valid.location));
        if (!luna_weather_config_valid(&valid)) continue;
        cJSON *item=cJSON_CreateObject();cJSON_AddStringToObject(item,"name",valid.location);
        cJSON_AddNumberToObject(item,"latitude",valid.latitude);cJSON_AddNumberToObject(item,"longitude",valid.longitude);
        const char *keys[]={"admin1","country"};
        for (unsigned k=0;k<2;k++) {
            const cJSON *value=cJSON_GetObjectItemCaseSensitive(city,keys[k]);
            cJSON_AddStringToObject(item,keys[k],cJSON_IsString(value) && strlen(value->valuestring)<128?value->valuestring:"");
        }
        cJSON_AddItemToArray(results,item);
    }
    cJSON_Delete(source);return reply(req,json);
}
static esp_err_t close_setup(httpd_req_t *req)
{
    if (!allowed(req,true)) return ESP_OK;
    luna_ble_wifi_setup_status_t state={0};
    if (!luna_ble_wifi_setup_status(&state) || state.state==1) return setup_error(req,true,"Wait for the current operation to finish.");
    luna_setup_ap_request(false);httpd_resp_set_type(req,"application/json");
    return httpd_resp_sendstr(req,"{\"closing\":true}");
}
esp_err_t luna_setup_portal_start(void)
{
    if (server) return ESP_ERR_INVALID_STATE;
    random_id(token);
    httpd_config_t config=HTTPD_DEFAULT_CONFIG();
    config.stack_size=8192;config.max_open_sockets=2;config.lru_purge_enable=true;
    config.max_uri_handlers=10;
    config.recv_wait_timeout=5;config.send_wait_timeout=5;config.open_fn=luna_setup_ap_socket;
    esp_err_t rc=httpd_start(&server,&config);
    const httpd_uri_t routes[]={
        {.uri="/",.method=HTTP_GET,.handler=page},
        {.uri="/api/session",.method=HTTP_GET,.handler=session},
        {.uri="/api/status",.method=HTTP_GET,.handler=status},
        {.uri="/api/network",.method=HTTP_POST,.handler=network},
        {.uri="/api/scan",.method=HTTP_POST,.handler=scan_networks},
        {.uri="/api/scan",.method=HTTP_GET,.handler=scan_networks},
        {.uri="/api/cities",.method=HTTP_POST,.handler=cities},
        {.uri="/api/save",.method=HTTP_POST,.handler=save},
        {.uri="/api/close",.method=HTTP_POST,.handler=close_setup}
    };
    for (unsigned i=0;rc==ESP_OK && i<sizeof(routes)/sizeof(routes[0]);i++) rc=httpd_register_uri_handler(server,&routes[i]);
    if (rc!=ESP_OK) luna_setup_portal_stop();
    return rc;
}
void luna_setup_portal_stop(void)
{
    if (server) {httpd_stop(server);server=NULL;}
    luna_weather_config_clear(token,sizeof(token));
}
