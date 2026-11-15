#ifndef TEST_LVGL_FONT_H
#define TEST_LVGL_FONT_H
#include <stdbool.h>
#include <stdint.h>
typedef struct lv_font_t {
	int line_height, base_line;
	const struct lv_font_t *fallback;
	void *user_data;
} lv_font_t;
typedef enum { LV_FREETYPE_FONT_RENDER_MODE_BITMAP } lv_freetype_font_render_mode_t;
typedef enum { LV_FREETYPE_FONT_STYLE_NORMAL } lv_freetype_font_style_t;
lv_font_t *lv_freetype_font_create(const char *, lv_freetype_font_render_mode_t, uint32_t, lv_freetype_font_style_t);
void lv_freetype_font_delete(lv_font_t *);
#endif
