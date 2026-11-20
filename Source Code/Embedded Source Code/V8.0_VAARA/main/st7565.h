#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define ST7565_WIDTH 128
#define ST7565_HEIGHT 64
#define ST7565_PAGES 8

typedef struct
{
    char c;
    uint8_t w;      // glyph width in columns
    uint8_t col[5]; // one byte per column, bit0 = top row (6 rows)
} small_glyph_t;

esp_err_t st7565_init(void);
void st7565_clear(void);
void st7565_update(void);
void st7565_draw_rect(uint8_t x, uint8_t y, uint8_t width, uint8_t height, bool on);
void st7565_set_pixel(uint8_t x, uint8_t y, bool on);

void st7565_draw_bitmap(uint8_t x, uint8_t y,
                        uint8_t width, uint8_t height,
                        const uint8_t *bitmap);
void st7565_draw_vertical_line(uint8_t x, uint8_t y, uint8_t height, bool on);
void st7565_draw_horizontal_line(uint8_t x, uint8_t y,
                                 uint8_t width, bool on);
void st7565_invert(bool enable);
void st7565_draw_border(void);
void delay_ms(uint32_t ms);
void st7565_draw_string(uint8_t x, uint8_t y,
                        const char *str, bool on);
void st7565_draw_char(uint8_t x, uint8_t y, char c, bool on);

void st7565_draw_string_scaled(uint8_t x,
                               uint8_t y,
                               const char *str,
                               uint8_t scale,
                               bool on);
void st7565_draw_char_scaled(uint8_t x,
                             uint8_t y,
                             char c,
                             uint8_t scale,
                             bool on);
void st7565_draw_battery_percentage(uint8_t x,
                                    uint8_t y,
                                    uint8_t width,
                                    uint8_t height,
                                    uint8_t percentage);

void draw_battery(int x, int y, int percent);
void draw_signal(int x, int y, int level);
int value_to_level(int value);
void draw_gps(int x, int y, bool on);
void fb_fill_rect(int x, int y, int w, int h, bool on);
void draw_arrow_left(int x, int y, bool on);
void draw_arrow_right(int x, int y, bool on);
void draw_arrow(int x, int y, bool right, bool on);
void draw_eco_mode(int x, int y, bool on);
void draw_bolt(int x, int y, bool on);
void draw_auto_mode(int x, int y, bool on);
void draw_normal_mode(int x, int y, bool on);
void draw_charger_logo(int x, int y, bool on);
// void st7565_draw_string_small(uint8_t x, uint8_t y,
//                               const char *str, bool on);
// void st7565_draw_char_small(uint8_t x, uint8_t y, char c, bool on);

void st7565_draw_string_small1(uint8_t x, uint8_t y, const char *str,
                               bool rotate180, bool on);
const small_glyph_t *find_glyph(char c);
void draw_glyph(uint8_t x, uint8_t y, const small_glyph_t *g,
                bool rotate180, bool on);

void st7565_draw_char_8x10(uint8_t x, uint8_t y, char c, bool on);
void st7565_draw_string_8x10(uint8_t x, uint8_t y,
                             const char *str, bool on);