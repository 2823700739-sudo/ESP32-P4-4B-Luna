// LVGL 9.3 compressed-font RLE state is global. Serialize only decompression;
// keep both draw workers for all other draw work. Never patch managed LVGL.
#include "luna_font_decode.h"
#include "src/font/lv_font_fmt_txt.h"
#include <assert.h>
#ifdef LUNA_NATIVE_UI
#include <windows.h>
static HANDLE font_lock;
void luna_font_decode_init(void){if(!font_lock)font_lock=CreateMutex(NULL,FALSE,NULL);}
#define FONT_LOCK() WaitForSingleObject(font_lock,INFINITE)
#define FONT_UNLOCK() ReleaseMutex(font_lock)
#else
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_check.h"
static StaticSemaphore_t lock_storage;
static SemaphoreHandle_t font_lock;
void luna_font_decode_init(void){if(!font_lock)font_lock=xSemaphoreCreateMutexStatic(&lock_storage);assert(font_lock);}
#define FONT_LOCK() xSemaphoreTake(font_lock,portMAX_DELAY)
#define FONT_UNLOCK() xSemaphoreGive(font_lock)
#endif
const void *luna_font_bitmap_get(lv_font_glyph_dsc_t *glyph,lv_draw_buf_t *buffer)
{
    const lv_font_fmt_txt_dsc_t *dsc=glyph->resolved_font->dsc;
    if(dsc->bitmap_format==LV_FONT_FMT_TXT_PLAIN||glyph->req_raw_bitmap)
        return lv_font_get_bitmap_fmt_txt(glyph,buffer);
    // Initialized by UI start (not lazily from racing draw threads).
    assert(font_lock);
    FONT_LOCK();
    const void *result=lv_font_get_bitmap_fmt_txt(glyph,buffer);
    FONT_UNLOCK();return result;
}
