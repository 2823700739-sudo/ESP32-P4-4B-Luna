// Actual LVGL 9.3 + actual firmware UI. Simulated input/data, never PC actions.
#include <assert.h>
#include <stdlib.h>
#include <windows.h>
#include "../../firmware/luna-panel/main/luna_font_decode.h"
#include "src/font/lv_font_fmt_txt.h"
#include "src/misc/lv_timer_private.h"
#undef TEXT
#include "../../firmware/luna-panel/main/luna_preview_ui.c"
static int64_t test_us=1000000;
static unsigned action_count;
static unsigned pair_answers,bond_answers,displayed_pair_generation;
static bool pair_accepted;
static void test_pair_answer(bool accept,unsigned generation){pair_answers++;pair_accepted=accept;displayed_pair_generation=generation;}
static bool bond_accepted;
static void test_bond_answer(unsigned generation,bool accept){bond_answers++;displayed_pair_generation=generation;bond_accepted=accept;}
static bool test_power_idle;
static unsigned dashboard_reads,music_reads,weather_reads;
static luna_setup_ap_state_t test_ap;
bool luna_setup_ap_snapshot(luna_setup_ap_state_t *out){*out=test_ap;return true;}
void luna_setup_ap_request(bool open){
    memset(&test_ap,0,sizeof(test_ap));test_ap.active=open;
    if(open){strcpy(test_ap.ssid,"Luna-Setup-EXAMPLE");test_ap.remaining_seconds=600;}
}
void luna_weather_config_clear(void *data,unsigned length){memset(data,0,length);}
esp_err_t luna_power_set_idle(bool value){test_power_idle=value;return ESP_OK;}
static lv_indev_state_t input_state=LV_INDEV_STATE_RELEASED;
static lv_point_t input_point;
static luna_music_state_t test_music={.online=true,.available=true,.controllable=true,.volume_available=true,
    .volume=42,.title="在月光下漫步",.artist="Luna Studio"};
static luna_dashboard_state_t test_dashboard={.online=true,.computer_fresh=true,.quota_fresh=true,
    .cpu=37,.gpu=24,.cpu_temp=-1,.gpu_temp=-1,.ram_used_gb=11.1,.ram_total_gb=15.8,
    .vram_used_gb=1.3,.vram_total_gb=8,.primary_remaining=44,.weekly_remaining=60,
    .received_us=1000000,.quota_sampled_ms=1790951000000LL,.gpu_name="AMD Radeon RX 6600",
    .project_name="ESP32-P4-4B-Luna"};
bool luna_dashboard_snapshot(luna_dashboard_state_t *s){dashboard_reads++;*s=test_dashboard;return true;}
static uint8_t pixels[720*720*3];
static uint8_t second_pixels[720*720*3];
static uint8_t *front_pixels=pixels;
static uint64_t flushed_pixels;
static unsigned flushes;
int64_t esp_timer_get_time(void){return test_us;}
struct tm *localtime_r(const time_t *value,struct tm *result){struct tm *t=localtime(value);if(!t)return NULL;*result=*t;return result;}
size_t strlcpy(char *d,const char *s,size_t n){size_t length=strlen(s);if(n){size_t k=length<n-1?length:n-1;memcpy(d,s,k);d[k]=0;}return length;}
bool luna_music_get_state(luna_music_state_t *s){music_reads++;*s=test_music;return true;}
bool luna_music_enqueue(const char *name,int value)
{
    action_count++;if(!strcmp(name,"music.play"))test_music.playing=true;
    if(!strcmp(name,"music.pause"))test_music.playing=false;
    if(!strcmp(name,"music.volume_set"))test_music.volume=value;
    if(!strcmp(name,"music.mute"))test_music.muted=!test_music.muted;
    return true;
}
luna_time_policy_t luna_time_snapshot(void){return (luna_time_policy_t){.valid=true,.source=LUNA_TIME_BLE};}
bool luna_weather_snapshot(luna_weather_state_t *w,luna_weather_metrics_t *m)
{
    weather_reads++;
    *w=(luna_weather_state_t){.configured=true,.available=true,.temperature_c=23};
    strlcpy(w->location,"上海",sizeof(w->location));strlcpy(w->condition,"多云",sizeof(w->condition));
    strlcpy(w->observed_at,"2026-10-02T10:24",sizeof(w->observed_at));
    *m=(luna_weather_metrics_t){.available=true,.apparent_c=22,.humidity_percent=64,.wind_decims_ms=21,.weather_code=2};return true;
}
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *buffer){if(lv_display_flush_is_last(d))front_pixels=buffer;flushed_pixels+=(uint64_t)(a->x2-a->x1+1)*(a->y2-a->y1+1);flushes++;lv_display_flush_ready(d);}
static void read_input(lv_indev_t *in,lv_indev_data_t *data){(void)in;data->state=input_state;data->point=input_point;}
static void advance(unsigned ms){for(unsigned i=0;i<ms;i+=10){test_us+=10000;lv_tick_inc(10);lv_timer_handler();}}
static void pointer(int x,int y,bool pressed){input_point=(lv_point_t){x,y};input_state=pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;advance(40);}
static void click(int x,int y){pointer(x,y,true);pointer(x,y,false);advance(250);}
static void click_object(lv_obj_t *object){lv_area_t a;lv_obj_get_coords(object,&a);click((a.x1+a.x2)/2,(a.y1+a.y2)/2);}
static void save(const char *name)
{
    char path[256];snprintf(path,sizeof(path),"%s.ppm",name);lv_refr_now(NULL);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n720 720\n255\n");
    for(unsigned i=0;i<720*720;i++){fputc(front_pixels[i*3+2],f);fputc(front_pixels[i*3+1],f);fputc(front_pixels[i*3],f);}fclose(f);
}
static void assert_visible_text_centered(lv_obj_t *container)
{
    // Measure rendered ink independently of font metrics/layout calculations.
    lv_obj_update_layout(root);lv_refr_now(NULL);
    lv_area_t parent,label;lv_obj_get_coords(container,&parent);
    lv_obj_get_coords(lv_obj_get_child(container,0),&label);
    int bg_index=((parent.y1+2)*720+(parent.x1+parent.x2)/2)*3;
    int left=720,right=-1,top=720,bottom=-1;
    for(int y=label.y1;y<=label.y2;y++)for(int x=label.x1;x<=label.x2;x++){
        if(x<0||x>=720||y<0||y>=720)continue;
        int pixel=(y*720+x)*3,difference=0;
        for(unsigned c=0;c<3;c++)difference+=abs((int)front_pixels[pixel+c]-front_pixels[bg_index+c]);
        if(difference<75)continue;
        if(x<left)left=x;if(x>right)right=x;
        if(y<top)top=y;if(y>bottom)bottom=y;
    }
    assert(right>=left&&bottom>=top);
    assert(abs(left+right-parent.x1-parent.x2)<=2);
    assert(abs(top+bottom-parent.y1-parent.y2)<=2);
}
static uint32_t computer_background_hash(void)
{
    uint32_t hash=2166136261U;
    // Clock digits previously occupied this area; computer CPU ring is stable.
    for(unsigned y=303;y<422;y++)for(unsigned x=98;x<420;x++)for(unsigned c=0;c<3;c++)
        hash=(hash^front_pixels[(y*720+x)*3+c])*16777619U;
    return hash;
}
static bool decode_raw;
static volatile LONG mismatches;
static uint32_t expected[4];
static const uint32_t race_letters[]={0x55b5,0x9f98,0x6d77,0x661f};
static uint32_t glyph_hash(unsigned letter)
{
    lv_font_glyph_dsc_t glyph;
    assert(lv_font_get_glyph_dsc(&luna_cjk_28,&glyph,race_letters[letter],0));
    lv_draw_buf_t *buffer=lv_draw_buf_create(glyph.box_w,glyph.box_h,LV_COLOR_FORMAT_A8,0);
    assert(buffer);glyph.resolved_font=&luna_cjk_28;
    (decode_raw?lv_font_get_bitmap_fmt_txt:luna_font_bitmap_get)(&glyph,buffer);
    uint32_t result=2166136261U;
    for(unsigned y=0;y<glyph.box_h;y++)for(unsigned x=0;x<glyph.box_w;x++)
        result=(result^buffer->data[y*buffer->header.stride+x])*16777619U;
    lv_draw_buf_destroy(buffer);return result;
}
static DWORD WINAPI font_worker(void *arg)
{
    unsigned worker=(unsigned)(uintptr_t)arg;
    for(unsigned i=0;i<3000;i++){
        unsigned letter=(i+worker)%4;
        if(glyph_hash(letter)!=expected[letter])InterlockedIncrement(&mismatches);
    }
    return 0;
}
int main(int argc,char **argv)
{
    bool double_buffer=argc>1&&!strcmp(argv[1],"--double-buffer");
    if(argc>1&&!double_buffer){
        lv_init();luna_font_decode_init();decode_raw=!strcmp(argv[1],"--unsafe-font-race");
        for(unsigned i=0;i<4;i++)expected[i]=glyph_hash(i);
        HANDLE workers[4];for(unsigned i=0;i<4;i++)workers[i]=CreateThread(NULL,0,font_worker,(void *)(uintptr_t)i,0,NULL);
        WaitForMultipleObjects(4,workers,TRUE,INFINITE);
        for(unsigned i=0;i<4;i++)CloseHandle(workers[i]);
        printf("%s: 12000 parallel glyph decodes; corrupted=%ld\n",decode_raw?"Unsafe LVGL baseline":"Protected Luna decoder",mismatches);
        return !decode_raw&&mismatches?1:0;
    }
    for(int64_t ms=1;ms<=192000;ms++){
        luna_pet_position_t a=luna_pet_motion(ms-1),b=luna_pet_motion(ms);
        assert(fabs(a.x-b.x)<.1&&fabs(a.y-b.y)<.1);
        assert(b.x>=8&&b.x<=660&&b.y>=85&&b.y<=596);
    }
    assert(luna_ui_next_card(0,-1)==5&&luna_ui_next_card(5,1)==0&&luna_ui_next_card(4,1)==5);
    puts("PASS pet rails/rest/resume continuity and six-card wrap");
    lv_init();lv_display_t *display=lv_display_create(720,720);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB888);
    const uint32_t chinese[]={0x6d77,0x9f98,0x55b5,0x661f,0x20087};
    for(unsigned i=0;i<sizeof(chinese)/sizeof(chinese[0]);i++) {
        lv_font_glyph_dsc_t glyph;
        assert(lv_font_get_glyph_dsc(&luna_cjk_16,&glyph,chinese[i],0));
        assert(lv_font_get_glyph_dsc(&luna_cjk_28,&glyph,chinese[i],0));
    }
    puts("PASS real fonts include Chinese titles/locations and supplementary Han");
    memset(second_pixels,0xa5,sizeof(second_pixels));
    lv_display_set_buffers(display,pixels,double_buffer?second_pixels:NULL,sizeof(pixels),LV_DISPLAY_RENDER_MODE_DIRECT);lv_display_set_flush_cb(display,flush);
    printf("Native DIRECT renderer: %s\n",double_buffer?"two buffers with LVGL sync":"single buffer");
    lv_indev_t *input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(input,read_input);
    luna_music_ui_start(lv_screen_active());advance(500);save("clock");
    assert(lv_obj_get_y(footer)==639);
    const uint32_t large_label_chars[]={0x6162,0x4e00,0x70b9,0x4e5f,0x5f88,0x597d,0x4f53,0x611f,0x6e7f,0x5ea6,0x98ce,0x901f};
    for(unsigned i=0;i<sizeof(large_label_chars)/sizeof(large_label_chars[0]);i++){
        lv_font_glyph_dsc_t glyph;assert(lv_font_get_glyph_dsc(&luna_cjk_20,&glyph,large_label_chars[i],0));
    }
    for(unsigned i=0;i<CARD_COUNT;i++){
        assert(lv_obj_get_width(nav[i])==64&&lv_obj_get_height(nav[i])==58);
        assert(lv_obj_get_style_text_font(nav_names[i],0)==&luna_cjk_16);
        lv_area_t button_bounds,text_bounds;lv_obj_get_coords(nav[i],&button_bounds);lv_obj_get_coords(nav_names[i],&text_bounds);
        assert(lv_obj_get_y(nav_names[i])==32&&lv_obj_get_height(nav_names[i])==24);
        assert(text_bounds.x1>=button_bounds.x1&&text_bounds.x2<=button_bounds.x2);
        assert(text_bounds.y1>=button_bounds.y1&&text_bounds.y2<=button_bounds.y2-2&&text_bounds.y2<720);
    }
    puts("PASS larger navigation and complete 20px Chinese detail glyphs");
    uint64_t old_pixels=flushed_pixels;unsigned old_flushes=flushes;advance(1000);
    uint64_t dirty=flushed_pixels-old_pixels;unsigned count=flushes-old_flushes;
    printf("Dirty-region test: %u flushes, %llu pixels over 1s (full frame %u)\n",count,(unsigned long long)dirty,720*720);
    assert(count>0&&dirty<(uint64_t)720*720*3);
    assert(!lv_obj_has_flag(root,LV_OBJ_FLAG_GESTURE_BUBBLE));assert(lv_obj_get_width(edges[0])==30);
    pointer(500,430,true);pointer(420,430,true);pointer(340,430,true);pointer(250,430,false);advance(300);
    assert(active==1);assert(action_count==0);save("music");puts("PASS real input horizontal swipe reaches root; no media action");
    assert(!idle);
    assert(lv_obj_get_width(play)==84&&lv_obj_get_height(previous)==56&&lv_obj_get_height(next)==56);
    assert(lv_obj_get_y(play)+42==326&&lv_obj_get_y(previous)+28==326&&lv_obj_get_y(next)+28==326);
    assert(tonearm_position==0&&tonearm_points[2].x>196);
    test_music.playing=true;advance(100);
    assert(tonearm_position>0&&tonearm_position<1);save("music-moving");
    advance(300);assert(tonearm_position==1&&tonearm_points[2].x<196);save("music-playing");
    double playing_angle=record_angle;test_music.controllable=false;advance(100);
    assert(tonearm_position==1&&record_angle!=playing_angle);
    test_music.online=false;advance(400);assert(tonearm_position==0);save("music-offline");
    test_music.online=true;test_music.available=false;advance(400);assert(tonearm_position==0);
    test_music.available=true;advance(100);assert(tonearm_position>0&&tonearm_position<1);
    test_music.playing=false;advance(50);assert(tonearm_position<1);advance(300);assert(tonearm_position==0);
    test_music.playing=true;advance(100);float hidden_tonearm=tonearm_position;
    choose_card(0,0);advance(500);assert(tonearm_position==hidden_tonearm);
    choose_card(1,0);advance(50);assert(tonearm_position==1);
    set_idle(true);test_music.playing=false;advance(500);assert(tonearm_position==1);
    set_idle(false);advance(50);assert(tonearm_position==0);
    test_music.controllable=true;advance(50);
    assert(action_count==0);save("music-paused");
    puts("PASS tonearm eases and reverses between playback and parking, ignores controllability, parks offline/empty, and freezes hidden/idle");
    click_object(play);click_object(play);assert(action_count==2&&!test_music.playing);puts("PASS two rapid play clicks alternate");
    click_object(volume_button);assert(!lv_obj_has_flag(volume_popup,LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_get_height(slider)==192&&lv_obj_get_width(volume_popup)==128);
    assert(lv_obj_get_child_count(volume_popup)==3);save("volume");
    click_object(mute);assert(test_music.muted);assert(lv_image_get_src(volume_icon)==&luna_icon_mute_24);
    assert(!lv_obj_has_flag(volume_popup,LV_OBJ_FLAG_HIDDEN));puts("PASS top-popup mute and matching right-hand volume icon");
    lv_area_t slider_area;lv_obj_get_coords(slider,&slider_area);int slider_x=(slider_area.x1+slider_area.x2)/2;
    unsigned before=active;pointer(slider_x,slider_area.y2-10,true);pointer(slider_x,slider_area.y1+80,true);pointer(slider_x,slider_area.y1+20,false);assert(active==before);
    assert(test_music.volume>50);puts("PASS vertical slider changes volume without navigating");
    click_object(play);assert(lv_obj_has_flag(volume_popup,LV_OBJ_FLAG_HIDDEN));assert(test_music.playing);
    puts("PASS outside tap closes popup AND performs playback action");
    for(unsigned i=2;i<5;i++){choose_card(i,1);advance(300);save(i==2?"weather":i==3?"codex":"computer");}
    assert(lv_obj_get_style_text_font(weather_city,0)==&luna_cjk_28);
    for(unsigned i=0;i<3;i++){
        assert(lv_obj_get_style_text_font(weather_labels[i],0)==&luna_cjk_20);
        assert(lv_obj_get_style_text_font(weather_readings[i],0)==&lv_font_montserrat_28);
        assert(lv_obj_get_style_text_align(weather_labels[i],0)==LV_TEXT_ALIGN_CENTER);
        assert(lv_obj_get_style_text_align(weather_readings[i],0)==LV_TEXT_ALIGN_CENTER);
        lv_area_t panel,label,value;lv_obj_get_coords(lv_obj_get_parent(weather_labels[i]),&panel);
        lv_obj_get_coords(weather_labels[i],&label);lv_obj_get_coords(weather_readings[i],&value);
        assert(label.y1>=panel.y1&&value.y2<=panel.y2);
    }
    for(unsigned i=0;i<lv_obj_get_child_count(cards[3]);i++){
        lv_obj_t *child=lv_obj_get_child(cards[3],i);
        if(lv_obj_check_type(child,&lv_label_class))assert(!strstr(lv_label_get_text(child),"工程来源"));
    }
    puts("PASS centered larger weather values and no Codex source footer");
    assert(!strcmp(lv_label_get_text(quota_values[0]),"44%"));
    assert(lv_obj_get_width(quota_bars[0])==238);assert(lv_arc_get_value(gauges[0])==37);
    assert(strstr(lv_label_get_text(memory_values[0]),"11.1 / 15.8"));
    for(unsigned i=0;i<2;i++){
        assert(lv_arc_get_bg_angle_start(gauges[i])==0&&lv_arc_get_bg_angle_end(gauges[i])==270);
        assert(lv_obj_get_y(gauge_values[i])==46);
        assert(lv_obj_get_style_text_font(gauge_units[i],0)==&lv_font_montserrat_20);
        assert(lv_obj_get_height(memory_bars[i])==12);
        assert(lv_obj_get_style_text_font(memory_values[i],0)==&luna_cjk_18);
        assert(lv_obj_get_style_text_font(memory_percent[i],0)==&lv_font_montserrat_24);
    }
    choose_card(4,0);test_dashboard.cpu=100;test_dashboard.gpu=-1;
    test_dashboard.vram_used_gb=-1;advance(600);lv_obj_update_layout(cards[4]);
    assert(!strcmp(lv_label_get_text(gauge_values[0]),"100"));
    assert(lv_obj_has_flag(gauge_units[1],LV_OBJ_FLAG_HIDDEN));
    assert(!strcmp(lv_label_get_text(memory_percent[1]),"--"));
    assert(lv_obj_has_flag(memory_bars[1],LV_OBJ_FLAG_HIDDEN));
    lv_area_t ring_area,number_area,unit_area;
    lv_obj_get_coords(lv_obj_get_parent(gauge_values[0]),&ring_area);
    lv_obj_get_coords(gauge_values[0],&number_area);lv_obj_get_coords(gauge_units[0],&unit_area);
    assert(number_area.x1>=ring_area.x1+12&&unit_area.x2<=ring_area.x2-12);
    test_dashboard.cpu=37;test_dashboard.gpu=24;test_dashboard.vram_used_gb=1.3;advance(600);
    save("computer");
    puts("PASS B open dials, lower readings, large memory blocks and unknown metrics");
    for(unsigned i=0;i<CARD_COUNT;i++)assert(lv_obj_get_style_bg_opa(cards[i],0)==255);
    lv_refr_now(NULL);uint32_t clean_computer=computer_background_hash();
    choose_card(0,0);lv_label_set_text(clock_hm,"88:88");lv_refr_now(NULL);
    choose_card(4,0);lv_refr_now(NULL);assert(computer_background_hash()==clean_computer);
    set_idle(true);update(NULL);lv_refr_now(NULL);set_idle(false);update(NULL);lv_refr_now(NULL);
    assert(computer_background_hash()==clean_computer);
    puts("PASS opaque cards and no clock numeral residue after switch or standby return");
    for(unsigned cycle=0;cycle<100;cycle++){
        choose_card(0,0);lv_label_set_text(clock_hm,cycle%2?"88:88":"11:11");lv_refr_now(NULL);
        choose_card(4,0);lv_refr_now(NULL);assert(computer_background_hash()==clean_computer);
        if(cycle%10==0){
            set_idle(true);update(NULL);lv_refr_now(NULL);
            set_idle(false);update(NULL);lv_refr_now(NULL);
            assert(computer_background_hash()==clean_computer);
        }
    }
    puts("PASS 100 clock/computer switch cycles and 10 standby returns retain clean pixels");
    unsigned hidden_dr=dashboard_reads,hidden_wr=weather_reads;
    choose_card(0,0);advance(1200);
    assert(dashboard_reads==hidden_dr&&weather_reads==hidden_wr);
    choose_card(1,0);advance(600);
    assert(dashboard_reads==hidden_dr&&weather_reads==hidden_wr);
    luna_music_state_t ack_only=test_music;ack_only.pending=!ack_only.pending;
    ack_only.updated_us++;ack_only.result_unknown=!ack_only.result_unknown;
    assert(music_display_equal(&test_music,&ack_only));
    ack_only.volume++;assert(!music_display_equal(&test_music,&ack_only));
    puts("PASS hidden-card snapshot/layout skipped and invisible ACK changes don't repaint music");
    test_dashboard.primary_remaining=-1;test_dashboard.quota_fresh=false;test_dashboard.computer_fresh=false;
    choose_card(3,0);
    advance(600);assert(!strcmp(lv_label_get_text(quota_values[0]),"--"));
    assert(!strcmp(lv_label_get_text(quota_values[1]),"60%"));assert(lv_obj_has_flag(quota_bars[0],LV_OBJ_FLAG_HIDDEN));
    choose_card(4,0);advance(40);
    assert(strstr(lv_label_get_text(computer_kicker),"离线"));
    // Isolate stable card content from intentional pet/star animations.
    lv_obj_add_flag(pet,LV_OBJ_FLAG_HIDDEN);
    for(unsigned i=0;i<6;i++)lv_obj_add_flag(twinkles[i],LV_OBJ_FLAG_HIDDEN);
    advance(600);lv_refr_now(NULL);
    uint64_t stable_pixels=flushed_pixels;advance(1200);
    assert(flushed_pixels==stable_pixels);
    lv_obj_remove_flag(pet,LV_OBJ_FLAG_HIDDEN);
    for(unsigned i=0;i<6;i++)lv_obj_remove_flag(twinkles[i],LV_OBJ_FLAG_HIDDEN);
    puts("PASS unchanged computer readings don't schedule redundant card redraws");
    puts("PASS dashboard values, independent missing quota, stale metrics and native 28px weather city");
    assert(lv_obj_get_height(weather_cloud)==105);assert(lv_obj_get_y(weather_cloud)>=31);puts("PASS flattened cloud fits complete weather visual");
    choose_card(1,0);advance(300);unsigned actions=action_count;
    last_touch=test_us;test_us+=IDLE_US-1;update(NULL);assert(!idle);
    test_us++;update(NULL);assert(idle&&active==1&&ui_timer->period==200&&test_power_idle);
    assert(lv_display_get_refr_timer(display)->period==50);
    assert(!strcmp(lv_label_get_text(idle_message),"慢慢来，你已经在向前了。"));
    unsigned dr=dashboard_reads,mr=music_reads,wr=weather_reads;
    int z_y=lv_obj_get_y(idle_zzz[0]);int z_opa=lv_obj_get_style_opa(idle_zzz[0],0);
    advance(400);assert(lv_obj_get_y(idle_zzz[0])!=z_y||lv_obj_get_style_opa(idle_zzz[0],0)!=z_opa);
    advance(1600);assert(dr==dashboard_reads&&mr==music_reads&&wr==weather_reads);
    puts("PASS standby warm sentence, 5Hz ZZZ motion and no hidden-card data layout");
    assert(lv_obj_has_flag(cards[1],LV_OBJ_FLAG_HIDDEN));save("idle");
    assert(lv_obj_get_parent(idle_pet)==idle_screen);
    assert(lv_image_get_src(idle_pet)==&luna_pet_6||lv_image_get_src(idle_pet)==&luna_pet_7);
    lv_area_t sleep_bounds;lv_obj_get_coords(idle_pet,&sleep_bounds);
    assert(sleep_bounds.x1==624&&sleep_bounds.y1==621&&sleep_bounds.x2<720&&sleep_bounds.y2<720);
    for(unsigned i=0;i<4;i++){
        lv_area_t number;lv_obj_get_coords(idle_numbers[i],&number);
        assert(lv_obj_get_width(idle_numbers[i])==40);
        const int xs[]={number.x1,number.x2},ys[]={number.y1,number.y2};
        for(unsigned x=0;x<2;x++)for(unsigned y=0;y<2;y++)
            assert(hypot(xs[x]-360,ys[y]-325)<181);
    }
    puts("PASS complete sleeping cat and clock numbers clear of inner tick radius");
    click(660,660);assert(!idle&&active==1&&action_count==actions);
    assert(!test_power_idle&&ui_timer->period==33&&lv_display_get_refr_timer(display)->period==15);
    click(360,443);assert(action_count==actions+1);puts("PASS 3-minute automatic clock and wake-only first touch");
    click(400,550);assert(!idle);click(60,610);assert(!idle);click(680,600);assert(!idle);
    click(120,50);assert(!idle);click(540,60);assert(!idle);
    click(400,120);assert(idle&&active==1);click(360,325);assert(!idle&&active==1);
    puts("PASS only top blank area enters manual clock; content/card/bottom/side taps don't");
    pet_response_until=0;pet_activity=0;update_pet(test_us,0);lv_obj_update_layout(root);
    int x=lv_obj_get_x(pet),y=lv_obj_get_y(pet);
    assert(lv_obj_get_width(pet)==72&&lv_obj_get_height(pet)==72);
    assert(lv_obj_get_x(pet_image)==0&&lv_obj_get_y(pet_image)==0);
    click(x+24,y+24);assert(pet_response_until>test_us);puts("PASS pet tap responds with bounded timer");
    assert(lv_obj_get_parent(pet_bubble)==root);
    for(int64_t t=0;t<78000;t+=1000){
        pet_activity=t*1000;pet_response_until=test_us+1600000;update_pet(test_us,0);
        lv_obj_update_layout(root);
        lv_area_t area,label_area;lv_obj_get_coords(pet_bubble,&area);
        lv_obj_get_coords(lv_obj_get_child(pet_bubble,0),&label_area);
        assert(area.x1>=8&&area.y1>=8&&area.x2<720&&area.y2<720);
        assert(label_area.x1>=area.x1&&label_area.x2<=area.x2);
        assert(label_area.y1>=area.y1&&label_area.y2<=area.y2);
    }
    puts("PASS pet greeting is a screen-clamped independent overlay with full text bounds");
    choose_card(2,1);advance(40);
    assert(lv_obj_get_style_opa(cards[2],0)==255);
    assert(lv_obj_get_style_transform_scale_x(cards[2],0)==256);
    assert(lv_obj_get_style_transform_scale_y(cards[2],0)==256);
    puts("PASS card transition uses translation only, no full-card alpha/scale layer");
    unsigned setup_actions=action_count;
    choose_card(2,0);advance(300);click(560,521);
    assert(test_ap.active&&!lv_obj_has_flag(setup_panel,LV_OBJ_FLAG_HIDDEN));
    assert(!strcmp(lv_label_get_text(setup_name),"Luna-Setup-EXAMPLE"));
    assert(strstr(lv_label_get_text(setup_access),"请保持连接"));
    const uint32_t setup_glyphs[]={0x624b,0x673a,0x70ed,0x5bc6,0x7801,0x6d4f,0x89c8,0x95ed,0x9690,0x85cf,0x5206,0x949f,0x4f59};
    for(unsigned i=0;i<sizeof(setup_glyphs)/sizeof(setup_glyphs[0]);i++){
        lv_font_glyph_dsc_t glyph;assert(lv_font_get_glyph_dsc(&luna_cjk_20,&glyph,setup_glyphs[i],0));
    }
    lv_obj_update_layout(root);
    lv_area_t panel_bounds,access_bounds;lv_obj_get_coords(setup_panel,&panel_bounds);lv_obj_get_coords(setup_access,&access_bounds);
    assert(access_bounds.x1>=panel_bounds.x1&&access_bounds.x2<=panel_bounds.x2);
    assert(lv_obj_get_width(setup_overlay)==720&&lv_obj_get_height(setup_overlay)==720);
    /* Real pointer dispatch: background navigation, arrows, swipes and standby
       must all remain blocked, including outside the smaller dialog. */
    for(unsigned i=0;i<CARD_COUNT;i++){
        lv_area_t a;lv_obj_get_coords(nav[i],&a);click((a.x1+a.x2)/2,(a.y1+a.y2)/2);
        assert(active==2&&setup_visible&&!idle);
    }
    click(20,360);click(700,360);click(10,10);
    pointer(600,650,true);pointer(400,650,true);pointer(170,650,true);pointer(170,650,false);
    assert(active==2&&setup_visible&&!idle&&action_count==setup_actions);
    last_touch=test_us-IDLE_US-100000;advance(100);
    assert(!idle&&setup_visible);
    puts("PASS full-screen setup modal blocks background navigation, arrows, swipe and manual/automatic standby");
    save("phone-setup");click(208,539);
    assert(test_ap.active&&lv_obj_has_flag(setup_panel,LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(setup_overlay,LV_OBJ_FLAG_HIDDEN));
    lv_area_t nav_bounds;lv_obj_get_coords(nav[0],&nav_bounds);
    click((nav_bounds.x1+nav_bounds.x2)/2,(nav_bounds.y1+nav_bounds.y2)/2);
    assert(active==0&&test_ap.active&&!idle);
    choose_card(2,0);advance(300);
    click(560,521);assert(!lv_obj_has_flag(setup_panel,LV_OBJ_FLAG_HIDDEN));
    click(487,539);assert(!test_ap.active&&lv_obj_has_flag(setup_panel,LV_OBJ_FLAG_HIDDEN));
    assert(!lv_label_get_text(setup_access)[0]&&action_count==setup_actions);
    puts("PASS phone setup physical button, Chinese open-network guidance bounds, hide/reopen/close, no media action");
    unsigned focus_actions=action_count;
    choose_card(5,0);advance(100);
    lv_font_glyph_dsc_t colon;assert(lv_font_get_glyph_dsc(&luna_digits_86,&colon,':',0));
    const uint32_t focus_glyphs[]={0x4e13,0x6ce8,0x5f00,0x59cb,0x6682,0x505c,0x7ed3,0x675f,0x4f11,0x606f,0x7ee7,0x7eed};
    for(unsigned i=0;i<sizeof(focus_glyphs)/sizeof(focus_glyphs[0]);i++){
        lv_font_glyph_dsc_t glyph;assert(lv_font_get_glyph_dsc(&luna_cjk_16,&glyph,focus_glyphs[i],0));
    }
    assert(!strcmp(lv_label_get_text(focus_time),"25:00"));
    click_object(focus_presets[1]);assert(!strcmp(lv_label_get_text(focus_time),"45:00"));
    click_object(focus_presets[2]);assert(!strcmp(lv_label_get_text(focus_time),"60:00"));
    click_object(focus_presets[0]);save("focus-ready");
    assert_visible_text_centered(focus_custom);assert_visible_text_centered(focus_start);assert_visible_text_centered(focus_end);
    click_object(focus_custom);assert(focus_editing&&lv_obj_check_type(focus_keyboard,&lv_keyboard_class));save("focus-keyboard");
    choose_card(1,0);click(360,668);click(617,668);click(32,100);
    pointer(550,670,true);pointer(440,670,true);pointer(250,670,false);advance(300);
    last_touch=test_us-IDLE_US-100000;update(NULL);set_idle(true);
    assert(active==5&&!idle&&focus_editing&&action_count==focus_actions);
    click(212,490);click(432,580);assert(focus_editing&&strlen(lv_label_get_text(focus_error))>0);
    click(212,354);click(360,490);click(432,580);
    assert(!focus_editing&&focus.state==LUNA_FOCUS_IDLE&&focus.duration_seconds==2400);
    assert_visible_text_centered(focus_custom);save("focus-custom40");
    click_object(focus_custom);click(508,354);click(360,354);click(210,580);
    assert(!focus_editing&&focus.duration_seconds==2400);
    click_object(focus_custom);assert(!strcmp(lv_textarea_get_text(focus_input),"40"));click(562,126);
    assert(luna_focus_select(&focus,999));update(NULL);advance(50);assert_visible_text_centered(focus_custom);save("focus-custom999");
    click_object(focus_presets[0]);
    test_music.online=false;click_object(focus_start);
    assert(focus.state==LUNA_FOCUS_RUNNING&&!lv_obj_has_state(focus_start,LV_STATE_DISABLED));
    assert(lv_obj_has_state(focus_presets[0],LV_STATE_DISABLED));save("focus-running");
    assert_visible_text_centered(focus_start);assert_visible_text_centered(focus_end);
    int64_t deadline=focus.deadline_us;
    choose_card(0,0);advance(100);test_us+=IDLE_US+10000000;update(NULL);set_idle(true);click(32,100);assert(!idle);
    assert(focus.deadline_us==deadline&&luna_focus_remaining(&focus,test_us)<1500);
    choose_card(5,0);advance(100);click_object(focus_start);
    assert(focus.state==LUNA_FOCUS_PAUSED);int64_t paused=focus.remaining_us;
    test_us+=300000000;update(NULL);assert(focus.remaining_us==paused&&idle);
    set_idle(false);advance(50);save("focus-paused");assert_visible_text_centered(focus_start);click_object(focus_start);
    assert(focus.state==LUNA_FOCUS_RUNNING&&focus.remaining_us==paused);
    luna_setup_ap_request(true);advance(100);assert(setup_visible);
    test_us=focus.deadline_us;update(NULL);
    assert(focus.state==LUNA_FOCUS_DONE&&focus.notice&&setup_visible&&!idle);
    assert(lv_obj_has_flag(pet_bubble,LV_OBJ_FLAG_HIDDEN));
    click(208,539);assert(!setup_visible&&focus.notice);
    assert(!strcmp(lv_label_get_text(lv_obj_get_child(pet_bubble,0)),"休息一下"));save("focus-complete");
    assert_visible_text_centered(pet_bubble);
    set_idle(true);update(NULL);
    assert(idle&&focus.notice&&!lv_obj_has_flag(pet_bubble,LV_OBJ_FLAG_HIDDEN));save("focus-complete-idle");
    luna_setup_ap_request(false);advance(50);set_idle(false);choose_card(5,0);advance(100);
    assert(!focus.notice&&focus.state==LUNA_FOCUS_DONE);
    assert_visible_text_centered(focus_start);
    click_object(focus_start);click_object(focus_end);
    assert(focus.state==LUNA_FOCUS_IDLE&&luna_focus_remaining(&focus,test_us)==1500);
    click(617,668);assert(active==0);click(103,668);assert(active==5);
    click_object(focus_start);set_idle(true);assert(!idle);test_us=focus.deadline_us;update(NULL);
    assert(!idle&&focus.state==LUNA_FOCUS_DONE&&focus.notice&&!lv_obj_has_flag(pet_bubble,LV_OBJ_FLAG_HIDDEN));
    set_idle(true);update(NULL);assert(idle);
    save("focus-complete-idle");set_idle(false);choose_card(5,0);advance(50);click_object(focus_end);
    pointer(500,430,true);pointer(420,430,true);pointer(340,430,true);pointer(250,430,false);advance(300);assert(active==0);
    pointer(250,430,true);pointer(340,430,true);pointer(420,430,true);pointer(500,430,false);advance(300);assert(active==5);
    assert(action_count==focus_actions);
    choose_card(5,0);advance(50);pet_response_until=test_us+1600000;update(NULL);advance(50);
    assert_visible_text_centered(pet_bubble);save("focus-greeting-centered");
    puts("PASS rendered-ink center within one pixel: greeting/rest bubbles, custom/default/999-minute labels, start/pause/resume/restart/end including disabled end");
    puts("PASS B focus, green K2 real LVGL keyboard, modal blocks background clicks/swipes/standby, custom commit/cancel, running blocks manual/auto idle across cards, paused permits idle, completion reminder; no media action");
    unsigned before_pair_actions=action_count;
    test_music.online=false;update(NULL);
    luna_connection_view_t view={.phase=LUNA_LINK_ACTIVE};
    luna_music_ui_connection(&view,test_pair_answer,test_bond_answer);update(NULL);
    assert(!strcmp(lv_label_get_text(connection),"蓝牙 · 已连接"));
    view.phase=LUNA_LINK_READY;luna_music_ui_connection(&view,test_pair_answer,test_bond_answer);
    assert(!strcmp(lv_label_get_text(connection),"蓝牙 · 已配对"));
    set_idle(true);assert(idle);
    view=(luna_connection_view_t){.phase=LUNA_LINK_SECURING,.pairing=true,.code=123456,.remaining_seconds=28,.generation=17};
    luna_music_ui_connection(&view,test_pair_answer,test_bond_answer);advance(50);
    assert(ble_modal&&!idle&&!strcmp(lv_label_get_text(ble_heading),"蓝牙配对"));
    assert(!strcmp(lv_label_get_text(ble_number),"123456"));save("ble-pairing");
    lv_area_t accept_bounds;lv_obj_get_coords(ble_accept,&accept_bounds);
    int accept_x=(accept_bounds.x1+accept_bounds.x2)/2,accept_y=(accept_bounds.y1+accept_bounds.y2)/2;
    pointer(accept_x,accept_y,true);
    view.code=987654;view.generation=18;luna_music_ui_connection(&view,test_pair_answer,test_bond_answer);
    pointer(accept_x,accept_y,false);advance(50);assert(pair_answers==0);
    view.code=123456;view.generation=17;luna_music_ui_connection(&view,test_pair_answer,test_bond_answer);
    unsigned prior_card=active;click(617,668);choose_card((active+1)%CARD_COUNT,1);set_idle(true);
    pointer(500,430,true);pointer(350,430,true);pointer(250,430,false);advance(300);
    test_us+=IDLE_US+1000000;update(NULL);assert(ble_modal&&!idle&&active==prior_card);
    click_object(ble_accept);click_object(ble_accept);
    assert(pair_answers==1&&pair_accepted&&displayed_pair_generation==17&&action_count==before_pair_actions);
    view.pairing=false;view.phase=LUNA_LINK_READY;luna_music_ui_connection(&view,test_pair_answer,test_bond_answer);
    assert(!ble_modal&&!idle&&last_touch==test_us);update(NULL);assert(!idle);
    choose_card(5,0);click_object(focus_custom);assert(focus_editing);
    view.pairing=true;view.phase=LUNA_LINK_SECURING;view.code=654321;view.generation=18;
    luna_music_ui_connection(&view,test_pair_answer,test_bond_answer);
    assert(ble_modal&&!focus_editing);click_object(ble_reject);
    assert(pair_answers==2&&!pair_accepted&&displayed_pair_generation==18);
    view.pairing=false;view.recovery=true;view.generation=19;
    luna_music_ui_connection(&view,test_pair_answer,test_bond_answer);advance(50);save("ble-recovery");
    assert(!strcmp(lv_label_get_text(ble_heading),"重新配对"));click_object(ble_recover);
    assert(bond_answers==1&&bond_accepted&&displayed_pair_generation==19);
    view.answer_pending=false;view.generation=20;luna_music_ui_connection(&view,test_pair_answer,test_bond_answer);
    click_object(ble_defer);assert(bond_answers==2&&!bond_accepted&&displayed_pair_generation==20);
    view.recovery_result=-1;luna_music_ui_connection(&view,test_pair_answer,test_bond_answer);advance(50);
    assert(strstr(lv_label_get_text(ble_note),"处理失败")&&!lv_obj_has_state(ble_recover,LV_STATE_DISABLED));
    save("ble-recovery-failed");
    view=(luna_connection_view_t){.phase=LUNA_LINK_WAITING};
    view.recovery_result=1;luna_music_ui_connection(&view,test_pair_answer,test_bond_answer);advance(50);
    assert(!lv_obj_has_flag(ble_feedback,LV_OBJ_FLAG_HIDDEN));save("ble-recovery-saved");
    view.recovery_result=0;
    luna_music_ui_connection(&view,test_pair_answer,test_bond_answer);update(NULL);advance(50);save("ble-waiting");
    assert(!ble_modal&&!idle&&action_count==before_pair_actions);
    choose_card(5,0);assert(luna_focus_select(&focus,1));assert(luna_focus_start(&focus,test_us));
    view.pairing=true;view.generation=30;view.code=333333;
    luna_music_ui_connection(&view,test_pair_answer,test_bond_answer);test_us=focus.deadline_us;update(NULL);
    assert(ble_modal&&focus.state==LUNA_FOCUS_DONE&&focus.notice&&lv_obj_has_flag(pet_bubble,LV_OBJ_FLAG_HIDDEN));
    view.pairing=false;luna_music_ui_connection(&view,test_pair_answer,test_bond_answer);update(NULL);
    assert(!ble_modal&&!idle&&focus.notice&&!lv_obj_has_flag(pet_bubble,LV_OBJ_FLAG_HIDDEN));
    puts("PASS Chinese BLE pairing/recovery modal, independent transport badge, real pointer answers with displayed generation, blocks background swipe/navigation/idle and resumes without media action");
    lv_obj_update_layout(root);puts("Native UI assertions passed; rendered six cards, volume and manual/automatic clock");return 0;
}
