// Hardware port of docs/design/preview/{index.html,styles.css,model.mjs}.
// Five cards with read-only live BLE dashboard and independent Wi-Fi weather.
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_timer.h"
#include "luna_ble_music.h"
#include "luna_dashboard.h"
#include "luna_music_ui.h"
#include "luna_weather.h"
#include "luna_time.h"
#include "luna_ui_motion.h"
#include "luna_font_decode.h"
#include "luna_power.h"
#include "assets/luna_ui_assets.h"

#define NIGHT 0x101827
#define PANEL 0x1c293c
#define TEXT 0xf1f5fb
#define SUB 0xa9b7cc
#define MOON 0xb8a2ff
#define PEACH 0xffbc82
#define SKY 0x86ccfa
#define MINT 0x81debb
#define CARD_COUNT 5
static lv_obj_t *root,*cards[5],*nav[5],*nav_icons[5],*nav_names[5],*header,*footer,*edges[2];
static lv_obj_t *twinkles[6];
static lv_obj_t *connection,*connection_icon,*clock_hm,*clock_ss,*clock_date,*clock_greeting,*clock_source,*clock_bottom;
static lv_obj_t *title,*artist,*play,*previous,*next,*play_icon,*record_logo,*music_kicker;
static lv_obj_t *volume_button,*volume_icon,*volume_popup,*slider,*volume_percent,*mute,*mute_icon;
static lv_obj_t *weather_city,*weather_temp,*weather_condition,*weather_note,*weather_sun,*weather_cloud,*weather_unknown;
static lv_obj_t *weather_readings[3],*weather_labels[3],*weather_drops[3],*weather_visual;
static lv_obj_t *pet,*pet_image,*pet_bubble;
static lv_obj_t *quota_values[2],*quota_bars[2],*quota_kicker,*quota_note,*project_name,*project_label;
static lv_obj_t *computer_name,*computer_kicker,*computer_note,*gauges[2],*gauge_values[2],*temperatures[2],*memory_values[2],*memory_bars[2];
static lv_obj_t *idle_screen,*idle_hands[3],*idle_numbers[4],*idle_date,*idle_message,*idle_pet,*idle_zzz[3];
static lv_point_precise_t hand_points[3][2],tick_points[60][2];
static bool idle,blank_press,press_moved,popup_dismissed;
static lv_point_t press_point;
static int64_t last_touch,last_dashboard;
#define IDLE_US 180000000LL
#define MANUAL_IDLE_TOP_HEIGHT 135
#define ACTIVE_UI_MS 33
#define IDLE_UI_MS 200
#define ACTIVE_REFRESH_MS 15
#define IDLE_REFRESH_MS 50
static luna_music_state_t current;
static luna_music_state_t shown_music;
static bool music_initialized,connection_initialized,connection_online;
static unsigned active;
static bool slider_drag,pet_held,closed_volume_on_press;
static int64_t last_frame,last_weather,last_clock,pet_activity,pet_response_until;
static int64_t last_stars;
static double record_angle;
static const lv_image_dsc_t *pet_frames[]={&luna_pet_0,&luna_pet_1,&luna_pet_2,&luna_pet_3,
    &luna_pet_4,&luna_pet_5,&luna_pet_6,&luna_pet_7};
static const lv_image_dsc_t *pet_left_frames[]={&luna_pet_left_0,&luna_pet_left_1,&luna_pet_left_2,&luna_pet_left_3};
static lv_timer_t *ui_timer;
// LVGL dirty rectangles already handle partial redraw. Avoid invalidating
// unchanged labels/images, including the off-screen cards.
static void changed_text(lv_obj_t *o,const char *value)
{if(strcmp(lv_label_get_text(o),value))lv_label_set_text(o,value);}
static void changed_format(lv_obj_t *o,const char *format,...)
{char value[384];va_list args;va_start(args,format);vsnprintf(value,sizeof(value),format,args);va_end(args);changed_text(o,value);}
static void changed_image(lv_obj_t *o,const void *src)
{if(lv_image_get_src(o)!=src)lv_image_set_src(o,src);}
static void changed_opacity(lv_obj_t *o,lv_opa_t opacity)
{if(lv_obj_get_style_opa(o,0)!=opacity)lv_obj_set_style_opa(o,opacity,0);}
static bool music_display_equal(const luna_music_state_t *a,const luna_music_state_t *b)
{
    // Compare displayed fields, not padding, timestamps or invisible ACK state.
    return a->online==b->online&&a->available==b->available&&a->controllable==b->controllable&&
        a->playing==b->playing&&a->volume_available==b->volume_available&&a->muted==b->muted&&
        a->volume==b->volume&&!strcmp(a->title,b->title)&&!strcmp(a->artist,b->artist);
}
#define lv_label_set_text changed_text
#define lv_label_set_text_fmt changed_format
#define lv_image_set_src changed_image

static lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color,int radius)
{
    lv_obj_t *o=lv_obj_create(parent);lv_obj_remove_style_all(o);
    lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,255,0);
    lv_obj_set_style_radius(o,radius,0);lv_obj_set_style_pad_all(o,0,0);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);return o;
}
static lv_obj_t *text(lv_obj_t *p,const char *value,int x,int y,const lv_font_t *font,uint32_t color)
{
    lv_obj_t *o=lv_label_create(p);lv_label_set_text(o,value);lv_obj_set_pos(o,x,y);
    lv_obj_set_style_text_font(o,font,0);lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    lv_obj_set_style_text_line_space(o,0,0);return o;
}
static lv_obj_t *bounded(lv_obj_t *p,const char *value,int x,int y,int w,const lv_font_t *font,uint32_t color,bool right)
{
    lv_obj_t *o=text(p,value,x,y,font,color);lv_obj_set_width(o,w);lv_label_set_long_mode(o,LV_LABEL_LONG_MODE_DOTS);
    if(right)lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_RIGHT,0);
    return o;
}
static lv_obj_t *image(lv_obj_t *p,const lv_image_dsc_t *src,int x,int y,int size,uint32_t tint)
{
    lv_obj_t *o=lv_image_create(p);lv_image_set_src(o,src);lv_obj_set_pos(o,x,y);
    if(size && size!=src->header.w) {
        lv_image_set_pivot(o,0,0);lv_image_set_scale(o,256*size/src->header.w);
    }
    if(tint!=UINT32_MAX){lv_obj_set_style_image_recolor(o,lv_color_hex(tint),0);lv_obj_set_style_image_recolor_opa(o,255,0);}
    lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE);return o;
}
static lv_obj_t *button(lv_obj_t *p,int x,int y,int w,int h,uint32_t color,int opacity,
                       lv_event_cb_t cb,void *data)
{
    lv_obj_t *o=lv_button_create(p);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_set_style_radius(o,h/2,0);lv_obj_set_style_bg_color(o,lv_color_hex(color),0);
    lv_obj_set_style_bg_opa(o,opacity,0);lv_obj_set_style_opa(o,LV_OPA_40,LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(o,opacity?opacity:20,LV_STATE_PRESSED);lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);
    if(cb)lv_obj_add_event_cb(o,cb,LV_EVENT_CLICKED,data);
    return o;
}
static void disabled(lv_obj_t *o,bool off){lv_obj_set_state(o,LV_STATE_DISABLED,off);}
static void x_animation(void *o,int32_t x){lv_obj_set_x(o,x);}
static void card_arrived(lv_anim_t *a)
{
    (void)a;
    // Final opaque frame after motion; don't redraw the full screen every tick.
    lv_obj_invalidate(root);
}
static void choose_card(unsigned index,int direction)
{
    if(index>=CARD_COUNT )return;
    if(ui_timer&&index==active){lv_obj_add_flag(volume_popup,LV_OBJ_FLAG_HIDDEN);return;}
    for(unsigned i=0;i<CARD_COUNT;i++) {
        lv_anim_delete(cards[i],NULL);lv_obj_set_pos(cards[i],65,135);lv_obj_set_style_opa(cards[i],255,0);
        lv_obj_set_flag(cards[i],LV_OBJ_FLAG_HIDDEN,i!=index);
        lv_obj_set_style_bg_opa(nav[i],i==index?18:0,0);
        lv_obj_set_style_text_color(nav_names[i],lv_color_hex(i==index?MOON:0x71829c),0);
        lv_obj_set_style_image_recolor(nav_icons[i],lv_color_hex(i==index?MOON:0x71829c),0);
    }
    active=index;lv_obj_add_flag(volume_popup,LV_OBJ_FLAG_HIDDEN);
    // A hidden card is intentionally not laid out. Populate the chosen card
    // from collectors on the next UI tick, without waiting for a stale deadline.
    last_clock=last_weather=last_dashboard=0;
    music_initialized=false;
    if(ui_timer)lv_timer_ready(ui_timer);
    // Keep the approved 230 ms / 26 px arrival. Full-card opacity/scale
    // requires a large intermediate layer and resampling every animation tick.
    // Translation alone redraws directly without changing the final layout.
    if(direction) {
        lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,cards[index]);lv_anim_set_duration(&a,230);
        lv_anim_set_values(&a,65+(direction>0?26:-26),65);lv_anim_set_exec_cb(&a,x_animation);
        lv_anim_set_completed_cb(&a,card_arrived);
        lv_anim_set_path_cb(&a,lv_anim_path_ease_out);lv_anim_start(&a);
    }
    // Scene changes need one complete frame across partial/triple buffers.
    lv_obj_invalidate(root);
}
static void nav_click(lv_event_t *e){choose_card((unsigned)(uintptr_t)lv_event_get_user_data(e),1);}
static void arrow_click(lv_event_t *e)
{
    int delta=(int)(intptr_t)lv_event_get_user_data(e);choose_card(luna_ui_next_card(active,delta),delta);
}
static void media_click(lv_event_t *e)
{
    const char *command=lv_event_get_user_data(e);
    if(!command)command=current.playing?"music.pause":"music.play";
    luna_music_enqueue(command,0);
    // Two clicks in one timer interval still alternate explicit play and pause.
    if(luna_music_get_state(&current))lv_image_set_src(play_icon,current.playing?&luna_icon_pause_30:&luna_icon_play_30);
}
static void volume_toggle(lv_event_t *e)
{
    (void)e;if(closed_volume_on_press){closed_volume_on_press=false;return;}
    if(current.online&&current.volume_available)
        lv_obj_set_flag(volume_popup,LV_OBJ_FLAG_HIDDEN,!lv_obj_has_flag(volume_popup,LV_OBJ_FLAG_HIDDEN));
}
static bool within(lv_obj_t *o,lv_obj_t *ancestor)
{
    for(;o;o=lv_obj_get_parent(o))if(o==ancestor)return true;
    return false;
}
static void set_idle(bool value)
{
    // Restore the active workload profile before cards; clock/brightness stay fixed.
    if(!value)luna_power_set_idle(false);
    idle=value;blank_press=false;slider_drag=false;
    lv_obj_set_flag(idle_screen,LV_OBJ_FLAG_HIDDEN,!value);
    lv_obj_add_flag(volume_popup,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flag(header,LV_OBJ_FLAG_HIDDEN,value);lv_obj_set_flag(footer,LV_OBJ_FLAG_HIDDEN,value);
    for(unsigned i=0;i<CARD_COUNT;i++)lv_obj_set_flag(cards[i],LV_OBJ_FLAG_HIDDEN,value||i!=active);
    for(unsigned i=0;i<2;i++)lv_obj_set_flag(edges[i],LV_OBJ_FLAG_HIDDEN,value);
    for(unsigned i=0;i<6;i++)lv_obj_set_flag(twinkles[i],LV_OBJ_FLAG_HIDDEN,value);
    lv_obj_set_flag(pet,LV_OBJ_FLAG_HIDDEN,value);lv_obj_add_flag(pet_bubble,LV_OBJ_FLAG_HIDDEN);
    last_clock=0;last_frame=0;last_touch=esp_timer_get_time();
    if(!value){last_weather=0;last_dashboard=0;}
    if(ui_timer){lv_timer_set_period(ui_timer,value?IDLE_UI_MS:ACTIVE_UI_MS);lv_timer_ready(ui_timer);}
    lv_timer_t *refresh=lv_display_get_refr_timer(lv_obj_get_display(root));
    if(refresh){lv_timer_set_period(refresh,value?IDLE_REFRESH_MS:ACTIVE_REFRESH_MS);lv_timer_ready(refresh);}
    if(value)luna_power_set_idle(true);
    lv_obj_invalidate(root);
}
static bool interactive(lv_obj_t *target)
{
    for(lv_obj_t *o=target;o&&o!=root;o=lv_obj_get_parent(o))
        if(lv_obj_check_type(o,&lv_button_class)||lv_obj_check_type(o,&lv_slider_class)||
           o==edges[0]||o==edges[1]||o==volume_popup)return true;
    return false;
}
static bool content_at(lv_obj_t *o,const lv_point_t *p)
{
    if(lv_obj_has_flag(o,LV_OBJ_FLAG_HIDDEN))return false;
    if(lv_obj_check_type(o,&lv_label_class)||lv_obj_check_type(o,&lv_image_class)){
        bool decorative=lv_obj_check_type(o,&lv_image_class)&&lv_image_get_src(o)==&luna_backdrop;
        for(unsigned i=0;i<6;i++)if(o==twinkles[i])decorative=true;
        lv_area_t a;lv_obj_get_coords(o,&a);
        if(!decorative&&p->x>=a.x1&&p->x<=a.x2&&p->y>=a.y1&&p->y<=a.y2)return true;
    }
    for(unsigned i=0;i<lv_obj_get_child_count(o);i++)if(content_at(lv_obj_get_child(o,i),p))return true;
    return false;
}
static void screen_touch(lv_event_t *e)
{
    lv_event_code_t code=lv_event_get_code(e);
    if(code==LV_EVENT_PRESSED||code==LV_EVENT_PRESSING)last_touch=esp_timer_get_time();
    lv_indev_t *in=lv_indev_active();
    if(code==LV_EVENT_PRESSING&&in){
        lv_point_t p;lv_indev_get_point(in,&p);
        if(abs(p.x-press_point.x)>12||abs(p.y-press_point.y)>12)press_moved=true;
        return;
    }
    if(code==LV_EVENT_RELEASED){
        if(blank_press&&!press_moved&&!popup_dismissed)set_idle(true);
        blank_press=false;return;
    }
    if(code==LV_EVENT_PRESS_LOST){blank_press=false;return;}
    if(code!=LV_EVENT_PRESSED)return;
    if(idle){set_idle(false);if(in)lv_indev_wait_release(in);lv_event_stop_processing(e);return;}
    if(in)lv_indev_get_point(in,&press_point);
    press_moved=false;popup_dismissed=false;
    closed_volume_on_press=false;
    lv_obj_t *target=lv_event_get_target_obj(e);
    bool brand_area=press_point.x>=65&&press_point.x<=285&&press_point.y>=34&&press_point.y<=82;
    blank_press=press_point.y>=0&&press_point.y<MANUAL_IDLE_TOP_HEIGHT&&!brand_area&&
        !interactive(target)&&!within(target,lv_obj_get_parent(connection))&&!content_at(root,&press_point);
    if(!lv_obj_has_flag(volume_popup,LV_OBJ_FLAG_HIDDEN)&&!within(target,volume_popup)) {
        popup_dismissed=true;
        closed_volume_on_press=within(target,volume_button);lv_obj_add_flag(volume_popup,LV_OBJ_FLAG_HIDDEN);
    }
}
static void slider_event(lv_event_t *e)
{
    lv_event_code_t code=lv_event_get_code(e);
    if(code==LV_EVENT_PRESSED)slider_drag=true;
    if(code==LV_EVENT_VALUE_CHANGED)lv_label_set_text_fmt(volume_percent,"%d%%",(int)lv_slider_get_value(slider));
    if(code==LV_EVENT_RELEASED){slider_drag=false;luna_music_enqueue("music.volume_set",lv_slider_get_value(slider));}
    if(code==LV_EVENT_PRESS_LOST)slider_drag=false;
}
static void gesture(lv_event_t *e)
{
    (void)e;blank_press=false;press_moved=true;if(idle||slider_drag||pet_held)return;
    lv_indev_t *in=lv_indev_active();if(!in)return;
    lv_dir_t dir=lv_indev_get_gesture_dir(in);
    int delta=dir==LV_DIR_LEFT?1:dir==LV_DIR_RIGHT?-1:0;
    if(delta){choose_card(luna_ui_next_card(active,delta),delta);lv_indev_wait_release(in);}
}
static void pet_event(lv_event_t *e)
{
    lv_event_code_t code=lv_event_get_code(e);
    if(code==LV_EVENT_PRESSED)pet_held=true;
    if(code==LV_EVENT_RELEASED||code==LV_EVENT_PRESS_LOST)pet_held=false;
    if(code==LV_EVENT_CLICKED)pet_response_until=esp_timer_get_time()+1600000;
}
static void bubble(lv_obj_t *o)
{
    lv_obj_add_flag(o,LV_OBJ_FLAG_EVENT_BUBBLE);
    for(unsigned i=0;i<lv_obj_get_child_count(o);i++)bubble(lv_obj_get_child(o,i));
}
static void card_header(unsigned n,const char *name,const lv_image_dsc_t *glyph,const char *kicker,uint32_t accent)
{
    image(cards[n],glyph,32,32,22,accent);
    lv_obj_t *heading=text(cards[n],name,64,30,&lv_font_montserrat_20,TEXT);
    lv_obj_set_style_text_letter_space(heading,2,0);
    lv_obj_t *k=bounded(cards[n],kicker,332,33,226,&luna_cjk_13,SUB,true);
    if(n==0){lv_obj_set_style_text_font(k,&luna_cjk_20,0);lv_obj_set_y(k,30);}
    if(n==1)music_kicker=k;
    if(n==3){quota_kicker=k;lv_obj_set_style_text_font(k,&luna_cjk_16,0);}
    if(n==4){computer_kicker=k;lv_obj_set_style_text_font(k,&luna_cjk_16,0);}
    if(n==2){
        weather_city=k;lv_obj_set_style_text_font(k,&luna_cjk_28,0);lv_obj_set_y(k,27);
        // Native full Han size, no raster scaling. Long names remain bounded.
    }
}
static lv_obj_t *bottom(unsigned n,const char *left,const char *right)
{
    lv_obj_t *l=bounded(cards[n],left,32,408,350,&luna_cjk_13,SUB,false);
    lv_obj_t *r=right&&right[0]?bounded(cards[n],right,365,408,193,&luna_cjk_13,SUB,true):NULL;
    if(n==0)clock_bottom=r;
    return l;
}
static void build_clock(void)
{
    card_header(0,"TIME",&luna_icon_clock_24,"慢一点，也很好",MOON);
    lv_obj_t *orbit=box(cards[0],420,78,325,325,PANEL,LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(orbit,1,0);lv_obj_set_style_border_color(orbit,lv_color_hex(MOON),0);
    lv_obj_set_style_border_opa(orbit,21,0);lv_obj_remove_flag(orbit,LV_OBJ_FLAG_CLICKABLE);
    clock_greeting=text(cards[0],"此刻，是你的时间",32,102,&luna_cjk_18,SUB);
    clock_hm=text(cards[0],"--:--",32,168,&luna_digits_101,TEXT);
    lv_obj_set_style_text_letter_space(clock_hm,-6,0);
    clock_ss=text(cards[0],"--",290,215,&lv_font_montserrat_28,MOON);
    clock_date=text(cards[0],"等待有效时间",32,280,&luna_cjk_20,0xd3dced);
    lv_obj_t *moon=image(cards[0],&luna_icon_moon_135,415,128,135,MOON);lv_obj_set_style_opa(moon,51,0);
    lv_obj_t *source=box(cards[0],32,336,300,34,0x172235,20);
    box(source,12,15,5,5,MOON,LV_RADIUS_CIRCLE);
    clock_source=bounded(source,"等待蓝牙 / Wi-Fi NTP 校时",24,7,264,&luna_cjk_13,SUB,false);
    bottom(0,"LOCAL CLOCK","等待有效时间");
}
static void build_music(void)
{
    card_header(1,"MUSIC",&luna_icon_play_24,"等待电脑同步",PEACH);
    lv_obj_t *disc=box(cards[1],32,82,164,164,0x101521,LV_RADIUS_CIRCLE);lv_obj_remove_flag(disc,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_shadow_width(disc,20,0);lv_obj_set_style_shadow_offset_y(disc,8,0);
    lv_obj_set_style_shadow_color(disc,lv_color_hex(0),0);lv_obj_set_style_shadow_opa(disc,68,0);
    for(int n=0;n<10;n++){
        int inset=5+n*5;lv_obj_t *ring=box(disc,inset,inset,164-2*inset,164-2*inset,0x141b27,LV_RADIUS_CIRCLE);
        lv_obj_set_style_bg_opa(ring,0,0);lv_obj_set_style_border_width(ring,1,0);
        lv_obj_set_style_border_color(ring,lv_color_hex(0x283241),0);lv_obj_remove_flag(ring,LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_t *center=box(disc,49,49,66,66,PEACH,LV_RADIUS_CIRCLE);lv_obj_remove_flag(center,LV_OBJ_FLAG_CLICKABLE);
    record_logo=image(disc,&luna_icon_moon_30,67,67,30,0x533c37);lv_image_set_pivot(record_logo,15,15);
    lv_obj_t *dot=box(disc,79,79,6,6,0x201d2b,LV_RADIUS_CIRCLE);lv_obj_remove_flag(dot,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *caption=text(cards[1],"LUNA / FIXED PLAYER",228,118,&luna_cjk_10,PEACH);
    lv_obj_set_style_text_letter_space(caption,2,0);
    title=text(cards[1],"等待音乐",228,137,&luna_cjk_28,TEXT);
    lv_obj_set_width(title,330);lv_obj_set_height(title,LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(title,84,0);lv_label_set_long_mode(title,LV_LABEL_LONG_MODE_DOTS);
    artist=bounded(cards[1],"连接电脑，打开音乐播放器",228,192,330,&luna_cjk_16,SUB,false);
    previous=button(cards[1],142,284,48,48,0x253145,255,media_click,"music.previous");
    image(previous,&luna_icon_prev_24,12,12,24,TEXT);
    play=button(cards[1],226,272,72,72,PEACH,255,media_click,NULL);
    play_icon=image(play,&luna_icon_play_30,21,21,30,0x332632);
    next=button(cards[1],334,284,48,48,0x253145,255,media_click,"music.next");
    image(next,&luna_icon_next_24,12,12,24,TEXT);
    lv_obj_set_x(previous,175);lv_obj_set_x(play,259);lv_obj_set_x(next,367);
    volume_button=button(cards[1],510,378,48,48,0x253145,255,volume_toggle,NULL);
    volume_icon=image(volume_button,&luna_icon_volume_24,12,12,24,SUB);
}
static void build_weather(void)
{
    card_header(2,"WEATHER",&luna_icon_weather_24,"尚未设置地点",SKY);
    weather_visual=box(cards[2],40,82,202,175,PANEL,0);lv_obj_set_style_bg_opa(weather_visual,0,0);
    lv_obj_add_flag(weather_visual,LV_OBJ_FLAG_OVERFLOW_VISIBLE);lv_obj_remove_flag(weather_visual,LV_OBJ_FLAG_CLICKABLE);
    weather_sun=image(weather_visual,&luna_sun,30,-23,0,UINT32_MAX);
    weather_cloud=image(weather_visual,&luna_cloud,18,36,0,UINT32_MAX);
    weather_unknown=text(weather_visual,"?",72,15,&luna_digits_86,SUB);
    // '?' isn't a clock digit; use the complete large CJK font and scale this one glyph.
    lv_obj_set_style_text_font(weather_unknown,&luna_cjk_28,0);
    lv_obj_set_style_transform_scale_x(weather_unknown,768,0);lv_obj_set_style_transform_scale_y(weather_unknown,768,0);
    for(unsigned i=0;i<3;i++){
        weather_drops[i]=box(weather_visual,50+i*42,150,5,12,SKY,3);
        lv_obj_remove_flag(weather_drops[i],LV_OBJ_FLAG_CLICKABLE);lv_obj_add_flag(weather_drops[i],LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(weather_sun,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(weather_cloud,LV_OBJ_FLAG_HIDDEN);
    weather_temp=text(cards[2],"--",377,121,&luna_digits_86,TEXT);
    lv_obj_set_style_text_letter_space(weather_temp,-3,0);
    text(cards[2],"°C",510,122,&lv_font_montserrat_28,TEXT);
    weather_condition=bounded(cards[2],"等待天气",310,200,240,&luna_cjk_18,SUB,true);
    const char *names[]={"体感","湿度","风速"};
    for(unsigned i=0;i<3;i++){
        lv_obj_t *panel=box(cards[2],32+i*179,269,167,89,0x192437,18);
        weather_labels[i]=bounded(panel,names[i],8,10,151,&luna_cjk_20,SUB,false);
        lv_obj_set_style_text_align(weather_labels[i],LV_TEXT_ALIGN_CENTER,0);
        weather_readings[i]=bounded(panel,"--",8,41,151,&lv_font_montserrat_28,TEXT,false);
        lv_obj_set_style_text_align(weather_readings[i],LV_TEXT_ALIGN_CENTER,0);
    }
    weather_note=bottom(2,"尚无天气数据","OPEN-METEO");
}
static void track(lv_obj_t *p,int x,int y,int w,int h){box(p,x,y,w,h,0x111c2c,h/2);}
static void build_quota(void)
{
    card_header(3,"CODEX",&luna_icon_code_24,"等待同步",MOON);
    const char *labels[]={"5 小时剩余","周额度剩余"};
    for(unsigned i=0;i<2;i++){
        int y=84+i*99;lv_obj_t *label=text(cards[3],labels[i],24,y+5,&luna_cjk_20,TEXT);
        lv_obj_set_style_text_letter_space(label,2,0);
        quota_values[i]=bounded(cards[3],"--",450,y,108,&lv_font_montserrat_28,TEXT,true);
        track(cards[3],24,y+49,542,22);
        quota_bars[i]=box(cards[3],24,y+49,542,22,MOON,11);lv_obj_add_flag(quota_bars[i],LV_OBJ_FLAG_HIDDEN);
        if(i)lv_obj_set_style_bg_opa(quota_bars[i],184,0);
    }
    lv_obj_t *project=box(cards[3],32,308,526,73,0x182337,18);
    image(project,&luna_icon_code_24,17,22,27,MOON);
    project_label=bounded(project,"当前 VS Code 项目",58,11,448,&luna_cjk_16,SUB,false);
    project_name=bounded(project,"尚未识别工程",58,33,448,&luna_cjk_16,TEXT,false);
    quota_note=bottom(3,"额度更新 --",NULL);
    lv_obj_set_style_text_font(quota_note,&luna_cjk_16,0);
}
static void build_computer(void)
{
    card_header(4,"COMPUTER",&luna_icon_computer_24,"等待同步",MINT);
    computer_name=bounded(cards[4],"等待电脑状态",32,66,526,&luna_cjk_16,SUB,false);
    const char *names[]={"CPU","GPU"};
    for(unsigned i=0;i<2;i++){
        // Preview SVG: radius 62 + 4.5 stroke, 133 px outer diameter.
        int x=126+i*205;lv_obj_t *ring=box(cards[4],x,103,133,133,PANEL,LV_RADIUS_CIRCLE);
        lv_obj_set_style_bg_opa(ring,0,0);lv_obj_remove_flag(ring,LV_OBJ_FLAG_CLICKABLE);
        gauges[i]=lv_arc_create(ring);lv_obj_remove_style_all(gauges[i]);lv_obj_set_size(gauges[i],133,133);
        lv_arc_set_bg_angles(gauges[i],0,360);lv_arc_set_rotation(gauges[i],270);lv_arc_set_range(gauges[i],0,100);
        lv_obj_set_style_arc_width(gauges[i],9,LV_PART_MAIN);lv_obj_set_style_arc_color(gauges[i],lv_color_hex(0x111c2c),LV_PART_MAIN);
        lv_obj_set_style_arc_width(gauges[i],9,LV_PART_INDICATOR);lv_obj_set_style_arc_color(gauges[i],lv_color_hex(i?SKY:MINT),LV_PART_INDICATOR);
        lv_obj_remove_flag(gauges[i],LV_OBJ_FLAG_CLICKABLE);lv_arc_set_value(gauges[i],0);
        lv_obj_t *value=text(ring,"--",0,24,&lv_font_montserrat_40,TEXT);lv_obj_set_width(value,133);gauge_values[i]=value;
        lv_obj_set_style_text_align(value,LV_TEXT_ALIGN_CENTER,0);
        lv_obj_t *name=text(ring,names[i],0,77,&luna_cjk_13,SUB);lv_obj_set_width(name,133);lv_obj_set_style_text_align(name,LV_TEXT_ALIGN_CENTER,0);
        lv_obj_t *temperature=bounded(cards[4],"温度不可用",x,241,133,&luna_cjk_13,SUB,false);
        lv_obj_set_style_text_align(temperature,LV_TEXT_ALIGN_CENTER,0);
        temperatures[i]=temperature;
    }
    const char *memory[]={"内存","显存"};
    for(unsigned i=0;i<2;i++){
        int y=275+i*48;text(cards[4],memory[i],32,y,&luna_cjk_13,TEXT);
        memory_values[i]=bounded(cards[4],"不可用",218,y,340,&luna_cjk_13,SUB,true);track(cards[4],32,y+27,526,9);
        memory_bars[i]=box(cards[4],32,y+27,526,9,i?SKY:MINT,5);lv_obj_add_flag(memory_bars[i],LV_OBJ_FLAG_HIDDEN);
    }
    computer_note=bottom(4,"等待电脑状态","WINDOWS / WDDM");
    lv_obj_set_style_text_font(computer_note,&luna_cjk_16,0);
}
static void progress_value(lv_obj_t *bar,int width,int value,bool stale)
{
    lv_obj_set_flag(bar,LV_OBJ_FLAG_HIDDEN,value<=0);
    if(value>0)lv_obj_set_width(bar,(width*value+50)/100);
    changed_opacity(bar,stale?96:255);
}
static void update_dashboard(int64_t now)
{
    if(last_dashboard&&now-last_dashboard<500000)return;
    last_dashboard=now;luna_dashboard_state_t s;if(!luna_dashboard_snapshot(&s))return;
    if(active==3){
    int remaining[]={s.primary_remaining,s.weekly_remaining};
    for(unsigned i=0;i<2;i++){
        if(remaining[i]>=0)lv_label_set_text_fmt(quota_values[i],"%d%%",remaining[i]);else lv_label_set_text(quota_values[i],"--");
        progress_value(quota_bars[i],542,remaining[i],!s.quota_fresh);
    }
    bool has_quota=remaining[0]>=0||remaining[1]>=0;
    lv_label_set_text(quota_kicker,has_quota?(s.quota_fresh?"实时额度":"上次额度 · 已过期"):"额度暂不可用");
    if(has_quota&&s.quota_sampled_ms>0){
        time_t epoch=s.quota_sampled_ms/1000;struct tm tm;
        if(localtime_r(&epoch,&tm))lv_label_set_text_fmt(quota_note,"额度更新 %02d:%02d%s",tm.tm_hour,tm.tm_min,s.quota_fresh?"":" · 上次数据");
    }else lv_label_set_text(quota_note,"额度更新 --");
    lv_label_set_text(project_name,s.project_name[0]?s.project_name:"尚未识别工程");
    lv_label_set_text(project_label,s.project_name[0]?"最近活动 VS Code 项目":"当前 VS Code 项目");
    return;
    }
    lv_label_set_text(computer_name,s.gpu_name[0]?s.gpu_name:"等待电脑状态");
    lv_label_set_text(computer_kicker,s.computer_fresh?"实时同步":s.received_us?"上次数据 · 已离线":"等待同步");
    double usage[]={s.cpu,s.gpu},temp[]={s.cpu_temp,s.gpu_temp};
    for(unsigned i=0;i<2;i++){
        if(usage[i]>=0)lv_label_set_text_fmt(gauge_values[i],"%.0f%%",usage[i]);else lv_label_set_text(gauge_values[i],"--");
        int value=usage[i]>=0?(int)lround(usage[i]):0;
        if(lv_arc_get_value(gauges[i])!=value)lv_arc_set_value(gauges[i],value);
        changed_opacity(gauges[i],s.computer_fresh?255:96);
        if(temp[i]>=0)lv_label_set_text_fmt(temperatures[i],"%.0f °C",temp[i]);else lv_label_set_text(temperatures[i],"温度不可用");
    }
    double used[]={s.ram_used_gb,s.vram_used_gb},total[]={s.ram_total_gb,s.vram_total_gb};
    for(unsigned i=0;i<2;i++){
        int percent=used[i]>=0&&total[i]>0?(int)lround(used[i]*100/total[i]):-1;
        if(percent>=0)lv_label_set_text_fmt(memory_values[i],"%.1f / %.1f GB · %d%%",used[i],total[i],percent);
        else lv_label_set_text(memory_values[i],"不可用");
        progress_value(memory_bars[i],526,percent,!s.computer_fresh);
    }
    lv_label_set_text(computer_note,s.computer_fresh?"2 秒采样 · 最忙 GPU 引擎":s.received_us?"连接中断 · 保留上次读数":"等待电脑状态");
}
static lv_obj_t *clock_line(lv_obj_t *parent,lv_point_precise_t *points,unsigned width,uint32_t color)
{
    lv_obj_t *line=lv_line_create(parent);lv_line_set_points(line,points,2);
    lv_obj_set_style_line_width(line,width,0);lv_obj_set_style_line_color(line,lv_color_hex(color),0);
    lv_obj_set_style_line_rounded(line,true,0);return line;
}
static void build_idle(void)
{
    idle_screen=box(root,0,0,720,720,NIGHT,0);lv_obj_remove_flag(idle_screen,LV_OBJ_FLAG_GESTURE_BUBBLE);
    image(idle_screen,&luna_backdrop,0,0,0,UINT32_MAX);
    bounded(idle_screen,"LUNA / MOON CLOCK",160,44,400,&lv_font_montserrat_20,MOON,false);
    lv_obj_set_style_text_align(lv_obj_get_child(idle_screen,1),LV_TEXT_ALIGN_CENTER,0);
    lv_obj_t *face=box(idle_screen,140,105,440,440,PANEL,LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(face,1,0);lv_obj_set_style_border_color(face,lv_color_hex(0x344357),0);
    lv_obj_remove_flag(face,LV_OBJ_FLAG_CLICKABLE);
    for(unsigned i=0;i<60;i++){
        double angle=i*6*M_PI/180.0;int inner=i%5?194:183;
        tick_points[i][0]=(lv_point_precise_t){360+inner*sin(angle),325-inner*cos(angle)};
        tick_points[i][1]=(lv_point_precise_t){360+202*sin(angle),325-202*cos(angle)};
        clock_line(idle_screen,tick_points[i],i%5?2:4,i%5?0x596b85:MOON);
    }
    // Digit centers lie on a 152px radius, with a gap to the 183px tick tips.
    const char *hours[]={"12","3","6","9"};const int positions[][2]={{340,159},{492,309},{340,461},{188,309}};
    for(unsigned i=0;i<4;i++){
        lv_obj_t *label=bounded(idle_screen,hours[i],positions[i][0],positions[i][1],40,&lv_font_montserrat_28,TEXT,false);idle_numbers[i]=label;
        lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
    }
    for(unsigned i=0;i<3;i++){
        hand_points[i][0]=hand_points[i][1]=(lv_point_precise_t){360,325};
        idle_hands[i]=clock_line(idle_screen,hand_points[i],i==0?9:i==1?6:2,i==2?PEACH:i==1?TEXT:MOON);
    }
    box(idle_screen,354,319,12,12,MOON,LV_RADIUS_CIRCLE);
    idle_date=bounded(idle_screen,"等待有效时间",100,575,520,&luna_cjk_28,TEXT,false);
    lv_obj_set_style_text_align(idle_date,LV_TEXT_ALIGN_CENTER,0);
    idle_message=bounded(idle_screen,"慢慢来，你已经在向前了。",100,640,520,&luna_cjk_16,SUB,false);
    lv_obj_set_style_text_align(idle_message,LV_TEXT_ALIGN_CENTER,0);
    // Reuse full sleeping frames, clear of digits, hands, date and source text.
    idle_pet=image(idle_screen,&luna_pet_6,624,621,0,UINT32_MAX);
    const char *zs[]={"z","z","Z"};
    for(unsigned i=0;i<3;i++)idle_zzz[i]=text(idle_screen,zs[i],642+i*16,610-i*6,&luna_cjk_16,MOON);
    lv_obj_add_flag(idle_screen,LV_OBJ_FLAG_HIDDEN);
}
static void update_weather(int64_t now)
{
    if(last_weather&&now-last_weather<1000000)goto animation;
    luna_weather_state_t w={0};luna_weather_metrics_t m={0};
    if(!luna_weather_snapshot(&w,&m))return;
    last_weather=now;
    lv_label_set_text(weather_city,w.configured?w.location:"尚未设置地点");
    if(w.available)lv_label_set_text_fmt(weather_temp,"%d",w.temperature_c);else lv_label_set_text(weather_temp,"--");
    lv_obj_update_layout(weather_temp);lv_obj_set_x(weather_temp,500-lv_obj_get_width(weather_temp));
    lv_label_set_text(weather_condition,w.available?w.condition:"等待天气");
    if(w.available&&m.available){
        lv_label_set_text_fmt(weather_readings[0],"%d°",m.apparent_c);
        lv_label_set_text_fmt(weather_readings[1],"%d%%",m.humidity_percent);
        lv_label_set_text_fmt(weather_readings[2],"%d.%d m/s",m.wind_decims_ms/10,m.wind_decims_ms%10);
    }else for(unsigned i=0;i<3;i++)lv_label_set_text(weather_readings[i],"--");
    if(w.available&&strlen(w.observed_at)>=16)
        lv_label_set_text_fmt(weather_note,"%s · 更新 %.5s",w.stale?"缓存 · 上次天气":"Wi-Fi 独立联网",w.observed_at+11);
    else lv_label_set_text(weather_note,w.configured?"等待 Wi-Fi 天气":"请先设置天气地点");
    lv_obj_set_style_text_color(weather_note,lv_color_hex(w.stale?PEACH:SUB),0);
    bool known=m.weather_code<=3||m.weather_code==45||m.weather_code==48||
        m.weather_code==51||m.weather_code==53||m.weather_code==55||m.weather_code==56||m.weather_code==57||
        m.weather_code==61||m.weather_code==63||m.weather_code==65||m.weather_code==66||m.weather_code==67||
        m.weather_code==71||m.weather_code==73||m.weather_code==75||m.weather_code==77||
        m.weather_code==80||m.weather_code==81||m.weather_code==82||m.weather_code==85||m.weather_code==86||
        m.weather_code==95||m.weather_code==96||m.weather_code==99;
    bool valid=w.available&&m.available&&known;
    lv_obj_set_flag(weather_sun,LV_OBJ_FLAG_HIDDEN,!valid||m.weather_code>2);
    lv_obj_set_flag(weather_cloud,LV_OBJ_FLAG_HIDDEN,!valid||m.weather_code==0);
    lv_obj_set_flag(weather_unknown,LV_OBJ_FLAG_HIDDEN,valid);
    bool rain=valid&&((m.weather_code>=51&&m.weather_code<=67)||(m.weather_code>=80&&m.weather_code<=82)||m.weather_code>=95);
    for(unsigned i=0;i<3;i++)lv_obj_set_flag(weather_drops[i],LV_OBJ_FLAG_HIDDEN,!rain);
animation:
    if(active==2&&!idle)lv_obj_set_y(weather_cloud,36-(int)(2.5*(1-cos(now/1000000.0*1.256637))));
}
static void update_clock(int64_t now)
{
    if(last_clock&&now-last_clock<1000000)return;
    last_clock=now;
    time_t epoch=time(NULL);struct tm tm; luna_time_policy_t state=luna_time_snapshot();
    bool valid=state.valid&&epoch>=1700000000&&localtime_r(&epoch,&tm);
    const char *source=valid?(state.source==LUNA_TIME_NTP?"Wi-Fi NTP 备用校时":"BLE 优先校时 · 本地持续走时"):"等待蓝牙 / Wi-Fi NTP 校时";
    char date[96]="等待有效时间";
    if(valid){
        static const char *week[]={"星期日","星期一","星期二","星期三","星期四","星期五","星期六"};
        snprintf(date,sizeof(date),"%d 年 %d 月 %d 日 · %s",tm.tm_year+1900,tm.tm_mon+1,tm.tm_mday,week[tm.tm_wday]);
    }
    if(idle){
        lv_image_set_src(idle_pet,(now/1000000)%2?&luna_pet_6:&luna_pet_7);
        lv_label_set_text(idle_date,date);
        double angles[]={valid?((tm.tm_hour%12)*30+tm.tm_min*.5+tm.tm_sec/120.0):0,
            valid?(tm.tm_min*6+tm.tm_sec*.1):0,valid?tm.tm_sec*6:0};
        const int lengths[]={110,155,177};
        for(unsigned i=0;i<3;i++){
            lv_obj_set_flag(idle_hands[i],LV_OBJ_FLAG_HIDDEN,!valid);
            double a=angles[i]*M_PI/180.0;
            hand_points[i][0]=(lv_point_precise_t){360-18*sin(a),325+18*cos(a)};
            hand_points[i][1]=(lv_point_precise_t){360+lengths[i]*sin(a),325-lengths[i]*cos(a)};
            lv_line_set_points(idle_hands[i],hand_points[i],2);
        }
        return;
    }
    if(valid){
        lv_label_set_text_fmt(clock_hm,"%02d:%02d",tm.tm_hour,tm.tm_min);lv_label_set_text_fmt(clock_ss,"%02d",tm.tm_sec);
        lv_label_set_text(clock_greeting,tm.tm_hour>=18?"夜色温柔，慢慢来":"此刻，是你的时间");
    }else{lv_label_set_text(clock_hm,"--:--");lv_label_set_text(clock_ss,"--");}
    lv_obj_update_layout(clock_hm);lv_obj_set_x(clock_ss,32+lv_obj_get_width(clock_hm)+15);
    lv_label_set_text(clock_date,date);
    lv_label_set_text(clock_source,source);
    lv_label_set_text(clock_bottom,valid?"保电时，本地继续走时":"等待有效时间");
}
static void update_pet(int64_t now,int64_t elapsed)
{
    bool responding=now<pet_response_until;
    if(!responding&&!pet_held)pet_activity+=elapsed;
    luna_pet_position_t pos=luna_pet_motion(pet_activity/1000);
    unsigned frame=responding?4+(now/350000)%2:
        pos.resting?((now/900000)%7==0?1:0):2+(now/220000)%2;
    int x=(int)pos.x-12,y=(int)pos.y-12;
    if(x<0)x=0;
    if(x>648)x=648;
    if(y<0)y=0;
    if(y>648)y=648;
    lv_obj_set_pos(pet,x,y);
    // Image mirror isn't needed for a front-facing sit/response frame.
    lv_image_set_src(pet_image,pos.left&&frame<4?pet_left_frames[frame]:pet_frames[frame]);
    lv_obj_set_flag(pet_bubble,LV_OBJ_FLAG_HIDDEN,!responding);
    if(responding){
        int bx=x+18,by=y-34;
        if(bx>648)bx=648;
        if(bx<8)bx=8;
        if(by<8)by=y+72;
        if(by>680)by=680;
        lv_obj_set_pos(pet_bubble,bx,by);
    }
}
static void update_sleep_zzz(int64_t now)
{
    // Three staggered 2.4s rises, 5Hz; only tiny glyph regions are invalidated.
    for(unsigned i=0;i<3;i++){
        unsigned phase=((now+i*800000LL)%2400000LL)/200000;
        int opacity=phase<3?(int)phase*85:phase>8?(11-(int)phase)*85:255;
        lv_obj_set_y(idle_zzz[i],610-(int)i*6-(int)phase);
        changed_opacity(idle_zzz[i],opacity);
    }
}
static void update(lv_timer_t *timer)
{
    (void)timer;int64_t now=esp_timer_get_time(),elapsed=last_frame?now-last_frame:0;last_frame=now;
    if(elapsed<0)elapsed=0;
    if(elapsed>100000)elapsed=100000;
    if(!idle&&!slider_drag&&!pet_held&&now-last_touch>=IDLE_US)set_idle(true);
    // Radio collectors continue caching real state. Do not lay out hidden
    // weather/quotas/music or animate the record while in always-bright standby.
    if(idle){update_clock(now);update_sleep_zzz(now);return;}
    if(active==2)update_weather(now);
    if(active==0)update_clock(now);
    if(active==3||active==4)update_dashboard(now);
    if(!idle&&(!last_stars||now-last_stars>=100000)){
        last_stars=now;
        int star_opacity=(int)(255*(.675-.225*cos(now/1000000.0*0.8975979)));
        for(unsigned i=0;i<6;i++)changed_opacity(twinkles[i],star_opacity);
    }
    if(luna_music_get_state(&current)){
        if(!connection_initialized||connection_online!=current.online){
        lv_label_set_text(connection,current.online?"蓝牙 · 已连接":"蓝牙 · 未连接");
        lv_obj_set_style_image_recolor(connection_icon,lv_color_hex(current.online?MINT:SUB),0);
        connection_initialized=true;connection_online=current.online;
        }
        bool changed=!music_initialized||!music_display_equal(&current,&shown_music);
        if(active==1&&(changed||slider_drag)){
        const char *t=current.available?current.title:"等待音乐",*a=current.available?current.artist:"连接电脑，打开音乐播放器";
        lv_label_set_text(title,t);lv_label_set_text(artist,a);
        // Single-line titles vertically center beside the record; long ones wrap to two lines.
        lv_obj_update_layout(title);lv_obj_set_y(artist,lv_obj_get_y(title)+lv_obj_get_height(title)+8);
        bool off=!current.online||!current.available||!current.controllable;
        disabled(play,off);disabled(previous,off);disabled(next,off);
        disabled(volume_button,!current.online||!current.volume_available);disabled(mute,!current.online||!current.volume_available);
        disabled(slider,!current.online||!current.volume_available);
        lv_image_set_src(play_icon,current.playing?&luna_icon_pause_30:&luna_icon_play_30);
        lv_image_set_src(mute_icon,current.muted?&luna_icon_mute_24:&luna_icon_volume_24);
        lv_label_set_text(music_kicker,current.online?"蓝牙实时同步":"等待电脑同步");
        if(!slider_drag){lv_slider_set_value(slider,current.volume,LV_ANIM_OFF);lv_label_set_text_fmt(volume_percent,"%u%%",current.volume);}
        lv_image_set_src(volume_icon,current.muted?&luna_icon_mute_24:&luna_icon_volume_24);
        shown_music=current;music_initialized=true;
        }
        if(!idle&&active==1&&current.online&&current.playing)record_angle=fmod(record_angle+elapsed/1000000.0*200,3600);
        lv_image_set_rotation(record_logo,(int)record_angle);
    }
    if(!idle)update_pet(now,elapsed);
}
void luna_music_ui_start(lv_obj_t *screen)
{
    luna_font_decode_init();
    root=box(screen,0,0,720,720,NIGHT,0);
    // Critical: stop GESTURE_BUBBLE here, otherwise LVGL sends the swipe to screen.
    lv_obj_remove_flag(root,LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_text_font(root,&luna_cjk_16,0);lv_obj_set_style_text_color(root,lv_color_hex(TEXT),0);
    image(root,&luna_backdrop,0,0,0,UINT32_MAX);
    const int stars[][2]={{292,42},{672,110},{28,262},{684,491},{177,616},{472,708}};
    for(unsigned i=0;i<6;i++)twinkles[i]=image(root,i%2?&luna_star_cool:&luna_star_warm,stars[i][0]-7,stars[i][1]-7,0,UINT32_MAX);
    for(unsigned i=0;i<2;i++){
        edges[i]=box(root,i?671:19,169,30,382,0x1b293b,26);lv_obj_set_style_bg_opa(edges[i],140,0);
        lv_obj_add_event_cb(edges[i],arrow_click,LV_EVENT_CLICKED,(void *)(intptr_t)(i?1:-1));
    }
    header=box(root,65,34,590,48,NIGHT,0);lv_obj_set_style_bg_opa(header,0,0);
    image(header,&luna_icon_moon_33,0,6,33,MOON);
    lv_obj_t *brand=text(header,"LUNA",45,0,&lv_font_montserrat_24,TEXT);lv_obj_set_style_text_letter_space(brand,4,0);
    lv_obj_t *tag=text(header,"your little orbit",45,31,&luna_cjk_10,SUB);lv_obj_set_style_text_letter_space(tag,2,0);
    lv_obj_t *badge=box(header,420,5,170,38,0x182235,18);
    connection_icon=image(badge,&luna_icon_bt_24,12,10,18,SUB);
    connection=bounded(badge,"蓝牙 · 未连接",37,9,123,&luna_cjk_13,SUB,false);
    for(unsigned i=0;i<5;i++){
        cards[i]=box(root,65,135,590,450,PANEL,32);lv_obj_set_style_border_width(cards[i],1,0);
        lv_obj_set_style_border_color(cards[i],lv_color_hex(0x344357),0);lv_obj_set_style_clip_corner(cards[i],true,0);
        lv_obj_set_style_shadow_width(cards[i],28,0);lv_obj_set_style_shadow_offset_y(cards[i],12,0);
        lv_obj_set_style_shadow_color(cards[i],lv_color_hex(0),0);lv_obj_set_style_shadow_opa(cards[i],34,0);
        lv_obj_set_style_transform_pivot_x(cards[i],295,0);lv_obj_set_style_transform_pivot_y(cards[i],225,0);
    }
    build_clock();build_music();build_weather();build_quota();build_computer();
    footer=box(root,79,639,562,58,NIGHT,0);lv_obj_set_style_bg_opa(footer,0,0);
    for(unsigned i=0;i<2;i++){
        lv_obj_t *arrow=button(footer,i?514:0,5,48,48,0x182235,255,arrow_click,(void *)(intptr_t)(i?1:-1));
        lv_obj_t *glyph=image(arrow,&luna_icon_chev_24,i?14:34,i?14:34,20,SUB);
        if(!i){lv_image_set_pivot(glyph,0,0);lv_image_set_rotation(glyph,1800);}
    }
    const char *names[]={"时钟","音乐","天气","额度","电脑"};
    const lv_image_dsc_t *glyphs[]={&luna_icon_clock_24,&luna_icon_play_24,&luna_icon_weather_24,&luna_icon_code_24,&luna_icon_computer_24};
    for(unsigned i=0;i<5;i++){
        nav[i]=button(footer,117+i*66,0,64,58,MOON,0,nav_click,(void *)(uintptr_t)i);
        lv_obj_set_style_radius(nav[i],20,0);
        nav_icons[i]=image(nav[i],glyphs[i],20,4,24,0x71829c);
        // CJK16 has a 24px line box, not 16px; keep its full height in 58px.
        nav_names[i]=bounded(nav[i],names[i],0,32,64,&luna_cjk_16,0x71829c,false);
        lv_obj_set_style_text_align(nav_names[i],LV_TEXT_ALIGN_CENTER,0);
    }
    pet=button(root,78,73,72,72,NIGHT,0,NULL,NULL);lv_obj_set_style_bg_opa(pet,0,LV_STATE_PRESSED);
    lv_obj_add_flag(pet,LV_OBJ_FLAG_OVERFLOW_VISIBLE);lv_obj_remove_flag(pet,LV_OBJ_FLAG_GESTURE_BUBBLE);
    pet_image=image(pet,&luna_pet_0,0,0,0,UINT32_MAX);lv_image_set_pivot(pet_image,36,36);
    // A separate root overlay has its own dirty rectangle. A child above the
    // pet's bounds can be clipped by invalidation, even with overflow visible.
    pet_bubble=box(root,96,39,64,32,0xd7caee,12);
    lv_obj_remove_flag(pet_bubble,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *greeting=text(pet_bubble,"喵～",0,0,&luna_cjk_16,0x312744);
    lv_obj_center(greeting);
    lv_obj_add_event_cb(pet,pet_event,LV_EVENT_ALL,NULL);
    volume_popup=box(root,497,257,142,310,0x29394f,22);
    lv_obj_set_style_border_width(volume_popup,1,0);lv_obj_set_style_border_color(volume_popup,lv_color_hex(0x4a576b),0);
    lv_obj_remove_flag(volume_popup,LV_OBJ_FLAG_GESTURE_BUBBLE);
    mute=button(volume_popup,50,10,42,42,0x253145,255,media_click,"music.mute");
    mute_icon=image(mute,&luna_icon_volume_24,9,9,24,SUB);
    volume_percent=bounded(volume_popup,"--%",16,59,110,&lv_font_montserrat_28,PEACH,false);
    lv_obj_set_style_text_align(volume_percent,LV_TEXT_ALIGN_CENTER,0);
    slider=lv_slider_create(volume_popup);lv_obj_set_pos(slider,66,110);lv_obj_set_size(slider,10,148);
    lv_slider_set_range(slider,0,100);lv_obj_set_style_bg_color(slider,lv_color_hex(PEACH),LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider,lv_color_hex(PEACH),LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider,7,LV_PART_KNOB);lv_obj_add_event_cb(slider,slider_event,LV_EVENT_ALL,NULL);
    lv_obj_remove_flag(slider,LV_OBJ_FLAG_GESTURE_BUBBLE);
    bounded(volume_popup,"上下拖动调节",18,275,106,&luna_cjk_13,SUB,false);
    build_idle();last_touch=esp_timer_get_time();
    bubble(root);lv_obj_add_event_cb(root,screen_touch,LV_EVENT_PRESSED,NULL);
    lv_obj_add_event_cb(root,screen_touch,LV_EVENT_PRESSING,NULL);lv_obj_add_event_cb(root,gesture,LV_EVENT_GESTURE,NULL);
    lv_obj_add_event_cb(root,screen_touch,LV_EVENT_RELEASED,NULL);lv_obj_add_event_cb(root,screen_touch,LV_EVENT_PRESS_LOST,NULL);
    choose_card(0,0);ui_timer=lv_timer_create(update,ACTIVE_UI_MS,NULL);update(NULL);
}
