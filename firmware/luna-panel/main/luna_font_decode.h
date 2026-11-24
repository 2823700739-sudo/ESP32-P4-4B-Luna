#pragma once
#include "lvgl.h"
// Initialize before any display draw workers use the generated Luna fonts.
void luna_font_decode_init(void);
const void *luna_font_bitmap_get(lv_font_glyph_dsc_t *glyph,lv_draw_buf_t *buffer);
