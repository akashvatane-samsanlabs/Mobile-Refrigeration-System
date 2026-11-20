#include "st7565.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "logo_bitmap.h"

#define TAG "ST7565"

/*
 * Example ESP32 classic pin assignment:
 *
 * GPIO18 -> SCLK
 * GPIO23 -> MOSI/SID
 * GPIO5  -> CS
 * GPIO21 -> A0 / RS
 * GPIO22 -> RESET
 *
 * Change these five definitions for your board.
 */
#define LCD_SCLK_GPIO 23
#define LCD_MOSI_GPIO 18
#define LCD_CS_GPIO 4
#define LCD_A0_GPIO 13
#define LCD_RESET_GPIO 15

#define LCD_SPI_HOST SPI2_HOST
#define LCD_SPI_CLOCK_HZ (1000000)

/*
 * Some ST7565 modules have a column offset because the controller
 * has 132 columns while the visible glass is 128 columns.
 *
 * Start with 0. If your logo is shifted horizontally, try 2 or 4.
 */
#define LCD_COLUMN_OFFSET 4

#define CMD_DISPLAY_OFF 0xAE
#define CMD_DISPLAY_ON 0xAF
#define CMD_ADC_NORMAL 0xA0
#define CMD_ADC_REVERSE 0xA1
#define CMD_NORMAL_DISPLAY 0xA6
#define CMD_REVERSE_DISPLAY 0xA7
#define CMD_ALL_POINTS_OFF 0xA4
#define CMD_ALL_POINTS_ON 0xA5
#define CMD_BIAS_1_9 0xA2
#define CMD_COM_NORMAL 0xC0
#define CMD_COM_REVERSE 0xC8
#define CMD_RESET 0xE2
#define CMD_POWER_CONTROL 0x2F
#define CMD_START_LINE 0x40
#define CMD_PAGE 0xB0
#define CMD_COLUMN_MSB 0x10
#define CMD_COLUMN_LSB 0x00
#define CMD_CONTRAST 0x81

static spi_device_handle_t s_lcd;
static uint8_t s_framebuffer[ST7565_WIDTH * ST7565_HEIGHT / 8];

#define ST7565_MIRROR_H 1

#define GPS_W 6
#define GPS_H 9

static const char *const gps_art[GPS_H] =
    {
        ".####.",
        "######",
        "##..##",
        "##..##",
        "######",
        ".####.",
        ".####.",
        "..##..",
        "..##..",
};

#define ARROW_W 18
#define ARROW_H 30

static const char *const arrow_art[ARROW_H] =
    {
        "..............####",
        "..............####",
        "............######",
        "............######",
        "..........########",
        "..........########",
        "........##########",
        "........##########",
        "......############",
        "......############",
        "....##############",
        "....##############",
        "..################",
        "..################",
        "##################",
        "##################",
        "..################",
        "..################",
        "....##############",
        "....##############",
        "......############",
        "......############",
        "........##########",
        "........##########",
        "..........########",
        "..........########",
        "............######",
        "............######",
        "..............####",
        "..............####",
};

#define ECO_W 20
#define ECO_H 25

static const char *const eco_art[ECO_H] =
    {
        "..................#.",
        "................###.",
        "..........#########.",
        ".......############.",
        ".....##############.",
        "....###############.",
        "...################.",
        "..#################.",
        ".##################.",
        ".#########.#######..",
        ".########.########..",
        "#######..#########..",
        "#####...#########...",
        "#####..##########...",
        "####..##########....",
        ".##...##########....",
        ".##..##########.....",
        "..#.##########......",
        "..###########.......",
        "..##########........",
        "..##.#####..........",
        ".##.................",
        ".#..................",
        "#...................",
        "#...................",
};

#define BOLT_W 20
#define BOLT_H 20

static const char *const bolt_art[BOLT_H] =
    {
        ".......######.......",
        ".....##########.....",
        "...####......####...",
        "..###.......##.###..",
        "..##.......###..##..",
        ".##.......###....##.",
        ".##......####....##.",
        "##......#####.....##",
        "##......#####.....##",
        "##.....##########.##",
        "##........#####...##",
        "##.......#####....##",
        "##......####......##",
        ".##....####......##.",
        ".......###.......##.",
        "......##........##..",
        ".....##........###..",
        "....#........####...",
        ".......########.....",
        ".......######.......",
};

#define AUTO_W 20
#define AUTO_H 20

/*
 * Auto mode logo (two circular arrows around the letter A), 20x20 px,
 * listed top to bottom as it looks on the physical display.
 * '#' = pixel on, '.' = pixel off.
 */
static const char *const auto_art[AUTO_H] =
    {
        ".......#####........",
        ".....#######....##..",
        "...####........####.",
        "..###.........######",
        "..##............##..",
        ".##..............##.",
        ".##......##......##.",
        "##......#..#......##",
        "##......#..#......##",
        "##......####......##",
        "##......#..#......##",
        "##.....#....#.....##",
        "##.....#....#.....##",
        ".##..............##.",
        ".##..............##.",
        "..##............##..",
        "######.........###..",
        ".####........####...",
        "..##....#######.....",
        "........#####.......",
};

#define SUN_W 20
#define SUN_H 20

/*
 * Sun icon (ring with 8 rays), 20x20 px, listed top to bottom as it
 * looks on the physical display. '#' = pixel on, '.' = pixel off.
 */
static const char *const sun_art[SUN_H] =
    {
        ".........##.........",
        ".........##.........",
        ".........##.........",
        "...##..........##...",
        "...##..........##...",
        "........####........",
        "......########......",
        "......##....##......",
        ".....##......##.....",
        "###..##......##..###",
        "###..##......##..###",
        ".....##......##.....",
        "......##....##......",
        "......########......",
        "........####........",
        "...##..........##...",
        "...##..........##...",
        ".........##.........",
        ".........##.........",
        ".........##.........",
};

#define PLUG_W 8
#define PLUG_H 9

static const char *const plug_art[PLUG_H] =
    {
        "..#..#..", /* prongs */
        "..#..#..",
        "########", /* flange */
        ".######.", /* body */
        ".######.",
        "..####..",
        "...##...", /* neck / cable */
        "...##...",
        "..##....", /* cable curving left */
};

void delay_ms(uint32_t ms)
{
    volatile uint32_t i;
    volatile uint32_t j;

    for (i = 0; i < ms; i++)
    {
        for (j = 0; j < 10000; j++)
        {
            __asm__ volatile("nop");
        }
    }
}

void fb_fill_rect(int x, int y, int w, int h, bool on)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            st7565_set_pixel(x + i, y + j, on);
}
void draw_charger_logo(int x, int y, bool on)
{
    /* Clear the area first so redraws never leave leftovers */
    fb_fill_rect(x, y, PLUG_W, PLUG_H, false);

    if (!on)
        return;

    for (int row = 0; row < PLUG_H; row++)
    {
        /* vertical mirror: physical top row = highest framebuffer y */
        int fy = y + (PLUG_H - 1 - row);

        int col = 0;
        while (col < PLUG_W)
        {
            if (plug_art[row][col] == '#')
            {
                int start = col;

                while (col < PLUG_W && plug_art[row][col] == '#')
                    col++;

                int len = col - start;

                /* Horizontal mirror correction: flip each run inside the icon width */
                int fx = x + PLUG_W - start - len;

                fb_fill_rect(fx, fy, len, 1, true);
            }
            else
            {
                col++;
            }
        }
    }
}

void draw_normal_mode(int x, int y, bool on)
{
    /* Clear the area first so redraws never leave leftovers */
    fb_fill_rect(x, y, SUN_W, SUN_H, false);

    if (!on)
        return;

    for (int row = 0; row < SUN_H; row++)
    {
        /* vertical mirror: physical top row = highest framebuffer y */
        int fy = y + (SUN_H - 1 - row);

        int col = 0;
        while (col < SUN_W)
        {
            if (sun_art[row][col] == '#')
            {
                int start = col;

                while (col < SUN_W && sun_art[row][col] == '#')
                    col++;

                fb_fill_rect(x + start, fy, col - start, 1, true);
            }
            else
            {
                col++;
            }
        }
    }
}

void draw_auto_mode(int x, int y, bool on)
{
    /* Clear the area first so redraws never leave leftovers */
    fb_fill_rect(x, y, AUTO_W, AUTO_H, false);

    if (!on)
        return;

    for (int row = 0; row < AUTO_H; row++)
    {
        /* vertical mirror: physical top row = highest framebuffer y */
        int fy = y + (AUTO_H - 1 - row);

        int col = 0;
        while (col < AUTO_W)
        {
            if (auto_art[row][col] == '#')
            {
                int start = col;

                while (col < AUTO_W && auto_art[row][col] == '#')
                    col++;

                int len = col - start;

                /* Horizontal mirror correction: flip each run inside the icon width */
                int fx = x + AUTO_W - start - len;

                fb_fill_rect(fx, fy, len, 1, true);
            }
            else
            {
                col++;
            }
        }
    }
}

void draw_bolt(int x, int y, bool on)
{
    /* Clear the area first so redraws never leave leftovers */
    fb_fill_rect(x, y, BOLT_W, BOLT_H, false);

    if (!on)
        return;

    for (int row = 0; row < BOLT_H; row++)
    {
        /* vertical mirror: physical top row = highest framebuffer y */
        int fy = y + (BOLT_H - 1 - row);

        int col = 0;
        while (col < BOLT_W)
        {
            if (bolt_art[row][col] == '#')
            {
                int start = col;

                while (col < BOLT_W && bolt_art[row][col] == '#')
                    col++;

                int len = col - start;

                /*
                 * Horizontal mirror correction: the LCD is mirrored left
                 * to right, so each run is flipped inside the icon width.
                 */
                int fx = x + BOLT_W - start - len;

                fb_fill_rect(fx, fy, len, 1, true);
            }
            else
            {
                col++;
            }
        }
    }
}
void draw_eco_mode(int x, int y, bool on)
{
    /* Clear the area first so redraws never leave leftovers */
    fb_fill_rect(x, y, ECO_W, ECO_H, false);

    if (!on)
        return;

    for (int row = 0; row < ECO_H; row++)
    {
        /* vertical mirror: physical top row = highest framebuffer y */
        int fy = y + (ECO_H - 1 - row);

        int col = 0;
        while (col < ECO_W)
        {
            if (eco_art[row][col] == '#')
            {
                int start = col;

                while (col < ECO_W && eco_art[row][col] == '#')
                    col++;

                int len = col - start;

                /*
                 * Horizontal mirror correction: the LCD is mirrored left
                 * to right, so each run is flipped inside the icon width.
                 */
                int fx = x + ECO_W - start - len;

                fb_fill_rect(fx, fy, len, 1, true);
            }
            else
            {
                col++;
            }
        }
    }
}

void draw_arrow(int x, int y, bool right, bool on)
{
    fb_fill_rect(x, y, ARROW_W, ARROW_H, false);

    if (!on)
        return;

    for (int row = 0; row < ARROW_H; row++)
    {
        /* vertical mirror: physical top row = highest framebuffer y */
        int fy = y + (ARROW_H - 1 - row);

        int col = 0;
        while (col < ARROW_W)
        {
            if (arrow_art[row][col] == '#')
            {
                int start = col;

                while (col < ARROW_W && arrow_art[row][col] == '#')
                    col++;

                int len = col - start;

                /*
                 * arrow_art is the LEFT arrow (tip on the left).
                 * Left arrow  -> draw the art as written.
                 * Right arrow -> flip it horizontally.
                 */
                int fx = right ? (x + ARROW_W - start - len)
                               : (x + start);

                fb_fill_rect(fx, fy, len, 1, true);
            }
            else
            {
                col++;
            }
        }
    }
}
void draw_arrow_left(int x, int y, bool on)
{
    draw_arrow(x, y, false, on);
}
void draw_arrow_right(int x, int y, bool on)
{
    draw_arrow(x, y, true, on);
}

void draw_gps(int x, int y, bool on)
{
    /* Clear the area first so redraws never leave leftovers */
    fb_fill_rect(x, y, GPS_W, GPS_H, false);

    if (!on)
        return;

    for (int row = 0; row < GPS_H; row++)
    {
        /* row 0 is the physical top, which is the highest framebuffer y */
        int fy = y + (GPS_H - 1 - row);

        int col = 0;
        while (col < GPS_W)
        {
            if (gps_art[row][col] == '#')
            {
                int start = col;

                while (col < GPS_W && gps_art[row][col] == '#')
                    col++;

                /* One rect per horizontal run; the art is symmetric,
                 * so the horizontal mirror correction is not needed. */
                fb_fill_rect(x + start, fy, col - start, 1, true);
            }
            else
            {
                col++;
            }
        }
    }
}
void draw_signal(int x, int y, int level)
{
    const int heights[4] = {2, 4, 7, 11};
    const int BAR_W = 2;
    const int STEP = 4;

    if (level < 0)
        level = 0;

    if (level > 4)
        level = 4;

    for (int b = 0; b < 4; b++)
    {
        /* Keep your horizontal mirror correction */
        int bx = x + (3 - b) * STEP;

        int h = heights[b];

        if (b < level)
        {
            /*
             * Draw DOWN in framebuffer coordinates.
             * Because your LCD is vertically mirrored,
             * it will appear UP on the physical display.
             */
            fb_fill_rect(bx,
                         y,
                         BAR_W,
                         h,
                         true);
        }
        else
        {
            fb_fill_rect(bx,
                         y,
                         BAR_W,
                         h,
                         false);
        }
    }
}
int value_to_level(int csq)
{
    /* 99 = unknown / not known */
    if (csq == 99)
        return 0;

    /* Invalid value */
    if (csq < 0)
        return 0;

    /* 0 - 9 */
    if (csq <= 9)
        return 1;

    /* 10 - 14 */
    if (csq <= 14)
        return 2;

    /* 15 - 19 */
    if (csq <= 19)
        return 3;

    /* 20 - 31 */
    return 4;
}

// /* Battery icon like your image: thick outline, right terminal, inner block */
/* Small battery icon: 2 px outline, terminal on the right, inner block */
void draw_battery(int x, int y, int percent)
{
    const int W = 16;
    const int H = 9;   // body size
    const int T = 1;   // outline thickness
    const int GAP = 1; // gap between outline and inner block

    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;

    fb_fill_rect(x, y, W, H, true);                          // outer
    fb_fill_rect(x + T, y + T, W - 2 * T, H - 2 * T, false); // hollow
    fb_fill_rect(x + W, y + 3, 2, H - 6, true);              // terminal (2 x 6)

    int max_w = W - 2 * (T + GAP); // 16 px
    int fill_w = max_w * percent / 100;
    fb_fill_rect(x + T + GAP, y + T + GAP, fill_w, H - 2 * (T + GAP), true);
}

void st7565_draw_battery_percentage(
    uint8_t x,
    uint8_t y,
    uint8_t width,
    uint8_t height,
    uint8_t percentage)
{
    /* Limit percentage to 100 */
    if (percentage > 100)
    {
        percentage = 100;
    }

    /* Make sure battery is large enough */
    if (width < 10 || height < 8)
    {
        return;
    }

    /* =========================================
     * BATTERY OUTER BODY
     * ========================================= */

    st7565_draw_rect(
        x,
        y,
        width,
        height,
        true);

    /* =========================================
     * BATTERY TERMINAL
     * ========================================= */

    uint8_t terminal_width = 3;
    uint8_t terminal_height = height / 2;

    st7565_draw_rect(
        x + width,
        y + (height - terminal_height) / 2,
        terminal_width,
        terminal_height,
        true);

    /* =========================================
     * INNER BATTERY AREA
     * ========================================= */

    uint8_t border = 3;

    uint8_t inner_x = x + border;
    uint8_t inner_y = y + border;

    uint8_t inner_width =
        width - (2 * border);

    uint8_t inner_height =
        height - (2 * border);

    /* =========================================
     * BATTERY LEVEL
     *
     * 0%   = empty
     * 25%  = quarter
     * 50%  = half
     * 75%  = three quarter
     * 100% = full
     * ========================================= */

    uint8_t fill_width =
        ((uint16_t)inner_width * percentage) / 100U;

    if (fill_width > 0)
    {
        st7565_draw_rect(
            inner_x,
            inner_y,
            fill_width,
            inner_height,
            true);
    }
}
void st7565_draw_char_scaled(uint8_t x,
                             uint8_t y,
                             char c,
                             uint8_t scale,
                             bool on)
{
    uint8_t index;

    /* SPACE */
    if (c == ' ')
    {
        return;
    }

    /* A-Z */
    if (c >= 'A' && c <= 'Z')
    {
        index = (uint8_t)(c - 'A') + 1;
    }
    /* 0-9 */
    else if (c >= '0' && c <= '9')
    {
        index = (uint8_t)(c - '0') + 27;
    }
    /* '-' */
    else if (c == '-')
    {
        index = 37;
    }
    /* '.' */
    else if (c == '.')
    {
        index = 38;
    }
    else if (c == '%')
    {
        index = 39;
    }
    else
    {
        return;
    }

    for (uint8_t col = 0; col < 5; col++)
    {
        /*
         * Reverse columns because your display
         * is horizontally mirrored.
         */
        uint8_t data = font5x7[index][4 - col];

        for (uint8_t row = 0; row < 7; row++)
        {
            /*
             * Font MSB = top pixel
             */
            if (data & (1U << (6 - row)))
            {
                for (uint8_t dx = 0; dx < scale; dx++)
                {
                    for (uint8_t dy = 0; dy < scale; dy++)
                    {
                        st7565_set_pixel(
                            x + (col * scale) + dx,
                            y + (row * scale) + dy,
                            on);
                    }
                }
            }
        }
    }
}

void st7565_draw_string_scaled(uint8_t x,
                               uint8_t y,
                               const char *str,
                               uint8_t scale,
                               bool on)
{
    uint8_t len = strlen(str);

    for (uint8_t i = 0; i < len; i++)
    {
        st7565_draw_char_scaled(
            x + (len - 1 - i) * 6 * scale,
            y,
            str[i],
            scale,
            on);
    }
}

void st7565_draw_char(uint8_t x, uint8_t y, char c, bool on)
{
    if (c >= 'A' && c <= 'Z')
    {
        uint8_t index = c - 'A' + 1;

        for (uint8_t col = 0; col < 5; col++)
        {
            uint8_t data = font5x7[index][col];

            for (uint8_t row = 0; row < 7; row++)
            {
                if (data & (1 << row))
                {
                    /*
                     * Rotate character 180 degrees:
                     * X -> reverse column
                     * Y -> reverse row
                     */
                    st7565_set_pixel(x + (4 - col),
                                     y + (6 - row),
                                     on);
                }
            }
        }
    }
}

void st7565_draw_string(uint8_t x, uint8_t y,
                        const char *str, bool on)
{
    int len = strlen(str);

    for (int i = len - 1; i >= 0; i--)
    {
        st7565_draw_char(x, y, str[i], on);

        x += 6;
    }
}

const small_glyph_t *find_glyph(char c)
{
    for (uint8_t i = 0; i < FONT_COUNT; i++)
        if (font4x6[i].c == c)
            return &font4x6[i];
    return NULL;
}

// Draw one glyph. If rotate180 is true, mirror it inside its own cell.
void draw_glyph(uint8_t x, uint8_t y, const small_glyph_t *g,
                bool rotate180, bool on)
{
    for (uint8_t col = 0; col < g->w; col++)
    {
        for (uint8_t row = 0; row < 6; row++)
        {
            if (g->col[col] & (1 << row))
            {
                uint8_t px = rotate180 ? x + (g->w - 1 - col) : x + col;
                uint8_t py = rotate180 ? y + (5 - row) : y + row;
                st7565_set_pixel(px, py, on);
            }
        }
    }
}

void st7565_draw_string_small1(uint8_t x, uint8_t y, const char *str,
                               bool rotate180, bool on)
{
    // total width = sum of glyph widths + 1px gap between glyphs
    uint8_t total = 0;
    for (const char *p = str; *p; p++)
    {
        const small_glyph_t *g = find_glyph(*p);
        if (g)
            total += g->w + 1;
    }
    if (total)
        total--; // no gap after the last glyph

    uint8_t offset = 0;
    for (const char *p = str; *p; p++)
    {
        const small_glyph_t *g = find_glyph(*p);
        if (!g)
            continue;

        // rotated: place the glyph from the right end so the order is reversed too
        uint8_t gx = rotate180 ? x + (total - offset - g->w) : x + offset;
        draw_glyph(gx, y, g, rotate180, on);
        offset += g->w + 1;
    }
}
void st7565_draw_char_8x10(uint8_t x, uint8_t y, char c, bool on)
{
    uint8_t index;

    if (c >= 'A' && c <= 'Z')
    {
        index = c - 'A' + 1;
    }
    else if (c >= '0' && c <= '9')
    {
        index = c - '0' + 27;
    }
    else
    {
        return;
    }

    for (uint8_t col = 0; col < 5; col++)
    {
        uint8_t data = font5x7[index][col];

        for (uint8_t row = 0; row < 7; row++)
        {
            if (data & (1 << row))
            {
                uint8_t x_start = (col * 8) / 5;
                uint8_t x_end = ((col + 1) * 8) / 5;

                uint8_t y_start = (row * 10) / 7;
                uint8_t y_end = ((row + 1) * 10) / 7;

                for (uint8_t dx = x_start; dx < x_end; dx++)
                {
                    for (uint8_t dy = y_start; dy < y_end; dy++)
                    {
                        st7565_set_pixel(
                            x + (7 - dx),
                            y + (9 - dy),
                            on);
                    }
                }
            }
        }
    }
}
void st7565_draw_string_8x10(uint8_t x, uint8_t y,
                             const char *str, bool on)
{
    uint8_t len = strlen(str);

    for (uint8_t i = 0; i < len; i++)
    {
        st7565_draw_char_8x10(
            x + (len - 1 - i) * 9,
            y,
            str[i],
            on);
    }
}
// void st7565_draw_char_small(uint8_t x, uint8_t y, char c, bool on)
// {
//     if (c >= 'A' && c <= 'Z')
//     {
//         uint8_t index = c - 'A' + 1;

//         for (uint8_t col = 0; col < 5; col++)
//         {
//             uint8_t data = font5x7[index][col];

//             for (uint8_t row = 0; row < 7; row++)
//             {
//                 if (data & (1 << row))
//                 {
//                     /*
//                      * 4x6 size
//                      * 180-degree rotation
//                      *
//                      * Skip only the last column and last row.
//                      */
//                     if (col < 5 && row < 7)
//                     {
//                         st7565_set_pixel(
//                             x + (4 - col),
//                             y + (5 - row),
//                             on
//                         );
//                     }
//                 }
//             }
//         }
//     }
// }

// void st7565_draw_string_small(uint8_t x, uint8_t y,
//                               const char *str, bool on)
// {
//     int len = strlen(str);

//     for (int i = len - 1; i >= 0; i--)
//     {
//         st7565_draw_char_small(x, y, str[i], on);

//         x += 6;   // small character spacing
//     }
// }
void st7565_draw_horizontal_line(uint8_t x, uint8_t y,
                                 uint8_t width, bool on)
{
    if (y >= ST7565_HEIGHT || x >= ST7565_WIDTH)
        return;

    for (uint8_t i = 0; i < width; i++)
    {
        if ((x + i) >= ST7565_WIDTH)
            break;

        st7565_set_pixel(x + i, y, on);
    }
}
void st7565_draw_vertical_line(uint8_t x, uint8_t y, uint8_t height, bool on)
{
    if (x >= ST7565_WIDTH || y >= ST7565_HEIGHT)
        return;

    for (uint8_t i = 0; i < height; i++)
    {
        if ((y + i) >= ST7565_HEIGHT)
            break;

        st7565_set_pixel(x, y + i, on);
    }
}
void st7565_draw_rect(uint8_t x, uint8_t y,
                      uint8_t width, uint8_t height,
                      bool on)
{
    if (width == 0 || height == 0)
    {
        return;
    }

    uint16_t x2 = (uint16_t)x + width - 1;
    uint16_t y2 = (uint16_t)y + height - 1;

    if (x >= ST7565_WIDTH || y >= ST7565_HEIGHT)
    {
        return;
    }

    if (x2 >= ST7565_WIDTH)
    {
        x2 = ST7565_WIDTH - 1;
    }

    if (y2 >= ST7565_HEIGHT)
    {
        y2 = ST7565_HEIGHT - 1;
    }

    /* Top and bottom */
    for (uint16_t xx = x; xx <= x2; xx++)
    {
        st7565_set_pixel((uint8_t)xx, y, on);
        st7565_set_pixel((uint8_t)xx, (uint8_t)y2, on);
    }

    /* Left and right */
    for (uint16_t yy = y; yy <= y2; yy++)
    {
        st7565_set_pixel(x, (uint8_t)yy, on);
        st7565_set_pixel((uint8_t)x2, (uint8_t)yy, on);
    }
}

static esp_err_t write_cmd(uint8_t cmd)
{
    gpio_set_level(LCD_A0_GPIO, 0);

    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };

    return spi_device_polling_transmit(s_lcd, &t);
}

static esp_err_t write_data(const uint8_t *data, size_t len)
{
    if (len == 0)
    {
        return ESP_OK;
    }

    gpio_set_level(LCD_A0_GPIO, 1);

    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };

    return spi_device_polling_transmit(s_lcd, &t);
}

static void gpio_init_lcd(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << LCD_A0_GPIO) |
                        (1ULL << LCD_RESET_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&cfg));
    gpio_set_level(LCD_A0_GPIO, 0);
    gpio_set_level(LCD_RESET_GPIO, 1);
}

static esp_err_t spi_init_lcd(void)
{
    const spi_bus_config_t bus_cfg = {
        .mosi_io_num = LCD_MOSI_GPIO,
        .miso_io_num = -1,
        .sclk_io_num = LCD_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = ST7565_WIDTH,
    };

    esp_err_t err = spi_bus_initialize(
        LCD_SPI_HOST,
        &bus_cfg,
        SPI_DMA_CH_AUTO);
    if (err != ESP_OK)
    {
        return err;
    }

    const spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = LCD_SPI_CLOCK_HZ,
        .mode = 0,
        .spics_io_num = LCD_CS_GPIO,
        .queue_size = 1,
    };

    return spi_bus_add_device(
        LCD_SPI_HOST,
        &dev_cfg,
        &s_lcd);
}

static void hardware_reset(void)
{
    gpio_set_level(LCD_RESET_GPIO, 0);
    delay_ms(20);

    gpio_set_level(LCD_RESET_GPIO, 1);
    delay_ms(50);
}
esp_err_t st7565_init(void)
{
    gpio_init_lcd();

    esp_err_t err = spi_init_lcd();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "SPI init failed: %s", esp_err_to_name(err));
        return err;
    }

    hardware_reset();

    /*
     * ST7565R-G initialization for 64128K 128x64 module.
     * The 64128K family is specified with 1/65 duty and 1/9 bias.
     */
    ESP_ERROR_CHECK(write_cmd(CMD_DISPLAY_OFF));
    ESP_ERROR_CHECK(write_cmd(CMD_RESET));

    delay_ms(5);

    ESP_ERROR_CHECK(write_cmd(CMD_BIAS_1_9));
    ESP_ERROR_CHECK(write_cmd(CMD_ADC_REVERSE));
    ESP_ERROR_CHECK(write_cmd(CMD_COM_NORMAL));

    /*
     * Booster + regulator + follower.
     */
    ESP_ERROR_CHECK(write_cmd(CMD_POWER_CONTROL));

    delay_ms(50);

    /*
     * Electronic volume / contrast.
     * 0x18 is only a starting point; adjust if required.
     */

    ESP_ERROR_CHECK(write_cmd(CMD_CONTRAST));
    ESP_ERROR_CHECK(write_cmd(0x25));

    ESP_ERROR_CHECK(write_cmd(CMD_START_LINE | 0));
    ESP_ERROR_CHECK(write_cmd(CMD_NORMAL_DISPLAY));
    ESP_ERROR_CHECK(write_cmd(CMD_ALL_POINTS_OFF));

    st7565_clear();
    st7565_update();

    ESP_ERROR_CHECK(write_cmd(CMD_DISPLAY_ON));

    ESP_LOGI(TAG, "64128K ST7565R initialized");

    return ESP_OK;
}

void st7565_clear(void)
{
    for (size_t i = 0; i < sizeof(s_framebuffer); ++i)
    {
        s_framebuffer[i] = 0x00;
    }
}

void st7565_set_pixel(uint8_t x, uint8_t y, bool on)
{
    if (x >= ST7565_WIDTH || y >= ST7565_HEIGHT)
    {
        return;
    }

    const size_t index =
        (size_t)x + ((size_t)(y / 8) * ST7565_WIDTH);

    const uint8_t mask = (uint8_t)(1U << (y & 7U));

    if (on)
    {
        s_framebuffer[index] |= mask;
    }
    else
    {
        s_framebuffer[index] &= (uint8_t)~mask;
    }
}

void st7565_draw_bitmap(uint8_t x, uint8_t y,
                        uint8_t width, uint8_t height,
                        const uint8_t *bitmap)
{
    if (!bitmap || x >= ST7565_WIDTH || y >= ST7565_HEIGHT)
    {
        return;
    }

    if (x + width > ST7565_WIDTH)
    {
        width = ST7565_WIDTH - x;
    }

    if (y + height > ST7565_HEIGHT)
    {
        height = ST7565_HEIGHT - y;
    }

    const uint8_t bytes_per_row = (uint8_t)((width + 7U) / 8U);

    for (uint8_t row = 0; row < height; ++row)
    {
        for (uint8_t col = 0; col < width; ++col)
        {

            /*
             * 180-degree rotation:
             *
             * Destination:
             *      x + col
             *      y + row
             *
             * Source:
             *      width  - 1 - col
             *      height - 1 - row
             */

            uint8_t src_col = width - 1U - col;
            uint8_t src_row = height - 1U - row;

            uint8_t b = bitmap[(size_t)src_row * bytes_per_row +
                               (src_col / 8U)];

            uint8_t mask =
                (uint8_t)(1U << (7U - (src_col & 7U)));

            st7565_set_pixel(
                (uint8_t)(x + col),
                (uint8_t)(y + row),
                (b & mask) != 0);
        }
    }
}
void st7565_update(void)
{
    for (uint8_t page = 0; page < ST7565_PAGES; ++page)
    {

        uint8_t column = LCD_COLUMN_OFFSET;

        ESP_ERROR_CHECK(write_cmd(CMD_PAGE | page));

        ESP_ERROR_CHECK(write_cmd(
            CMD_COLUMN_MSB | ((column >> 4) & 0x0F)));

        ESP_ERROR_CHECK(write_cmd(
            CMD_COLUMN_LSB | (column & 0x0F)));

        ESP_ERROR_CHECK(write_data(
            &s_framebuffer[(size_t)page * ST7565_WIDTH],
            ST7565_WIDTH));
    }
}

void st7565_invert(bool enable)
{
    ESP_ERROR_CHECK(write_cmd(
        enable ? CMD_REVERSE_DISPLAY : CMD_NORMAL_DISPLAY));
}
