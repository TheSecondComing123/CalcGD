#include <tice.h>
#include <graphx.h>
#include <fileioc.h>
#include <string.h>
#include <stdlib.h>

#include "defs.h"
#include "gfx.h"

/* game palette (256 entries, 1555 format) - first half */
static const uint16_t game_pal[256] = {
    0x25C9, 0xC823, 0x3C44, 0x83FF, 0x2422, 0xBFE0, 0xFFFF, 0x5C66,
    0x9001, 0x0000, 0x8000, 0x6A32, 0x9801, 0xB824, 0x7BE9, 0x3C44,
    0x0801, 0x4444, 0xAC23, 0xE5D0, 0xB023, 0x8400, 0xDD4C, 0xF2C8,
    0x2022, 0x514B, 0xD0C8, 0x9001, 0xF2CD, 0xC845, 0xF28C, 0xE9EA,
    0x5866, 0xFFF2, 0xBC86, 0xE128, 0x0400, 0x60E7, 0x2422, 0x6E4B,
    0xEE54, 0xD445, 0xE9A9, 0xD045, 0xE588, 0x770E, 0xF74F, 0x7B90,
    0x0800, 0x0000, 0x7BB0, 0x772E, 0xEE08, 0x0000, 0xE9CA, 0x6568,
    0xDCA7, 0x8C01, 0x1C22, 0x44E6, 0xFBD1, 0x7749, 0x6E68, 0x6E2B,
    0xCD27, 0x8000, 0xEE4B, 0x9C42, 0xDC66, 0xEE6C, 0x7739, 0x5D89,
    0xDC86, 0x0000, 0x2823, 0x2422, 0x72B6, 0x0400, 0x3D2A, 0x5E11,
    0x5C86, 0x6E48, 0x9C22, 0xA443, 0xAC64, 0x9422, 0x2863, 0xB4C5,
    0x8C43, 0x3484, 0x6A95, 0x8C01, 0x1021, 0xAC84, 0x5C66, 0x9422,
    0xFFED, 0x9442, 0x5188, 0xB927, 0x0400, 0x8801, 0xA885, 0x2422,
    0xD5AF, 0xEAB6, 0x1021, 0xDDC9, 0x0821, 0x9C84, 0x3844, 0x0400,
    0x1463, 0xEA6C, 0x0C21, 0xB444, 0xF6F8, 0x5866, 0xE5EA, 0x8C42,
    0x5E6C, 0x8400, 0xFFFF, 0x6A32, 0x7BE9, 0xFFED, 0x3000, 0xD002,
    /* second half (end-of-level palette variant) */
    0x25C9, 0xC823, 0x3C44, 0x83FF, 0x2422, 0xBFE0, 0xFFFF, 0x5C66,
    0x9001, 0x0000, 0x8000, 0x6A32, 0x9801, 0xB824, 0x7BE9, 0x3C44,
    0x0801, 0x4444, 0xAC23, 0xE5D0, 0xB023, 0x8400, 0xDD4C, 0xF2C8,
    0x2022, 0x514B, 0xD0C8, 0x9001, 0xF2CD, 0xC845, 0xF28C, 0xE9EA,
    0x5866, 0xFFF2, 0xBC86, 0xE128, 0x0400, 0x60E7, 0x2422, 0x6E4B,
    0xEE54, 0xD445, 0xE9A9, 0xD045, 0xE588, 0x770E, 0xF74F, 0x7B90,
    0x0800, 0x0000, 0x7BB0, 0x772E, 0xEE08, 0x0000, 0xE9CA, 0x6568,
    0xDCA7, 0x8C01, 0x1C22, 0x44E6, 0xFBD1, 0x7749, 0x6E68, 0x6E2B,
    0xCD27, 0x8000, 0xEE4B, 0x9C42, 0xDC66, 0xEE6C, 0x7739, 0x5D89,
    0xDC86, 0x0000, 0x2823, 0x2422, 0x72B6, 0x0400, 0x3D2A, 0x5E11,
    0x5C86, 0x6E48, 0x9C22, 0xA443, 0xAC64, 0x9422, 0x2863, 0xB4C5,
    0x8C43, 0x3484, 0x6A95, 0x8C01, 0x1021, 0xAC84, 0x5C66, 0x9422,
    0xFFED, 0x9442, 0x5188, 0xB927, 0x0400, 0x8801, 0xA885, 0x2422,
    0xD5AF, 0xEAB6, 0x1021, 0xDDC9, 0x0821, 0x9C84, 0x3844, 0x0400,
    0x1463, 0xEA6C, 0x0C21, 0xB444, 0xF6F8, 0x5866, 0xE5EA, 0x8C42,
    0x5E6C, 0x8400, 0xFFFF, 0x6A32, 0x7BE9, 0xF2C8, 0xD002, 0x0000
};

/* menu palette (256 entries, 1555 format) */
static const uint16_t menu_pal[256] = {
    0xFBFF, 0xB000, 0xB400, 0xB000, 0x2000, 0xFFFF, 0x3000, 0xDC23,
    0xB400, 0x0000, 0x1C00, 0x8400, 0x8800, 0xA800, 0x0421, 0x0C00,
    0xAC00, 0x1400, 0x0800, 0x2000, 0x1800, 0x1800, 0x8000, 0xA800,
    0x1000, 0xB304, 0x3F86, 0x43A6, 0x1000, 0xB400, 0x8421, 0x1400,
    0x8401, 0x18C6, 0xB2E3, 0x0842, 0x2EE3, 0x137B, 0xAAA2, 0x7BFF,
    0x0F5A, 0xDFCA, 0xA400, 0xA400, 0x14A5, 0xFFFF, 0xAAC3, 0x0000,
    0x47C7, 0x2AA2, 0x9BDA, 0xCB65, 0x1400, 0x3F65, 0xE3CA, 0x8F5A,
    0xDAD6, 0x8C42, 0xDBA8, 0xC3A7, 0x7E00, 0x2529, 0x1CE7, 0x5F18,
    0xBF44, 0x7C42, 0x0C63, 0x3B43, 0x9FFB, 0x82FF, 0x9541, 0xC210,
    0x25C4, 0xA94A, 0x73BD, 0x7C81, 0x83E5, 0x827F, 0x0DC0, 0xF7DE,
    0x8F39, 0xABFC, 0x5FC9, 0x1084, 0x6759, 0x9484, 0xEB7B, 0xB18C,
    0x0360, 0x35CD, 0xA1C2, 0xB9CE, 0x1962, 0xAFFD, 0x8D8C, 0x077A,
    0xBDEF, 0x1581, 0xFE20, 0x7E80, 0xA3FB, 0x9063, 0x77DD, 0x4A73,
    0xEF9C, 0xFF20, 0x7EA0, 0xFD60, 0x7DA0, 0x6339, 0x82DF, 0xFD01,
    0x6B7A, 0xD3C8, 0xC231, 0x2002, 0x83E2, 0x52B5, 0x4E94, 0x7C22,
    0x83A0, 0xA0E7, 0x4765, 0x4A52, 0x1759, 0x7E60, 0x8A40, 0x83E8,
    0xFCC1, 0x3E10, 0xAD6B, 0xA7FC, 0x3724, 0x04E0, 0x1DA2, 0x7F60,
    0x06E1, 0x81DF, 0x02BF, 0xBF65, 0x294A, 0x0540, 0xA3FE, 0x17BA,
    0xCF87, 0x36E5, 0x8695, 0xFCA1, 0xFF80, 0x05A1, 0x939C, 0x0720,
    0xC788, 0x97BD, 0xB2A4, 0x0A00, 0x137B, 0xCBC8, 0xFEC0, 0x023E,
    0x06A1, 0x97DE, 0x0151, 0x0AD6, 0x852A, 0x7F00, 0xFDC0, 0x073F,
    0x8179, 0xAFDB, 0x1480, 0x91F0, 0xF660, 0x8065, 0xE0A1, 0x09AE,
    0x4367, 0x9BFF, 0x8718, 0xAB99, 0x00E7, 0xF0C1, 0x81CF, 0x810E,
    0x7061, 0xD421, 0xA4C0, 0x4941, 0x30A0, 0x6EE1, 0xC4C0, 0x8155,
    0x75E0, 0x0219, 0x6141, 0x85D6, 0x38C0, 0x586B, 0x81BE, 0x6AA0,
    0x07C7, 0x1AD6, 0xDA20, 0xD501, 0x4DE0, 0xC861, 0x8FEA, 0xCB6A,
    0x4421, 0x7141, 0x3560, 0x8201, 0xCCC0, 0xE5C0, 0xA460, 0x867B,
    0xBF47, 0x3705, 0xFA05, 0x5049, 0x228A, 0x7721, 0x84AA, 0xB745,
    0xF881, 0x41A0, 0x2EEE, 0xCA26, 0x9A54, 0xA3CD, 0xDDC7, 0x4987,
    0x6981, 0x9C20, 0x3C26, 0x8347, 0x9EDB, 0xE88E, 0xDEC8, 0xFB85,
    0xCE03, 0x0D45, 0x0883, 0x2C25, 0xB449, 0x5666, 0xABF0, 0x9C8C,
    0x7366, 0xAC6B, 0xE802, 0xD24C, 0xA907, 0x23A4, 0x2BD4, 0xD047
};

static bool gfx_started = false;

void extract_rle(const uint8_t *src, uint8_t *dst, uint24_t num_pairs)
{
    for (uint24_t i = 0; i < num_pairs; i++) {
        uint8_t count = *src++;
        uint8_t value = *src++;
        memset(dst, value, count);
        dst += count;
    }
}

static bool extract_rle_checked(const uint8_t *src, uint24_t src_len,
                                uint8_t *dst, uint24_t num_pairs,
                                uint24_t dst_cap)
{
    uint24_t written = 0;
    uint24_t read_off = 0;

    for (uint24_t i = 0; i < num_pairs; i++) {
        if (read_off + 2 > src_len)
            return false;

        uint8_t count = *src++;
        uint8_t value = *src++;
        read_off += 2;

        if ((uint24_t)count > dst_cap - written)
            return false;

        memset(dst + written, value, count);
        written += count;
    }

    return true;
}

static bool load_appvar_rle(const char *name, uint8_t *dst, uint8_t header_skip,
                            uint24_t dst_cap)
{
    ti_var_t slot = ti_Open(name, "r");
    if (!slot) return false;

    ti_Seek(header_skip, SEEK_SET, slot);

    /* read 3-byte RLE pair count */
    uint24_t num_pairs = 0;
    ti_Read(&num_pairs, 3, 1, slot);

    /* read remaining data into a temp buffer and decompress */
    uint24_t tell = ti_Tell(slot);
    uint24_t size = ti_GetSize(slot);
    if (tell > size) {
        ti_Close(slot);
        return false;
    }
    uint24_t remaining = size - tell;
    if (remaining == 0) {
        ti_Close(slot);
        return false;
    }

    uint8_t *rle_buf = malloc(remaining);
    if (!rle_buf) {
        ti_Close(slot);
        return false;
    }
    ti_Read(rle_buf, remaining, 1, slot);
    ti_Close(slot);

    bool ok = extract_rle_checked(rle_buf, remaining, dst, num_pairs, dst_cap);
    free(rle_buf);
    return ok;
}

void set_game_palette(void)
{
    gfx_SetPalette(game_pal, sizeof(game_pal), 0);
}

void set_menu_palette(void)
{
    gfx_SetPalette(menu_pal, sizeof(menu_pal), 0);
}

bool gfx_game_init(void)
{
    if (!gfx_started) {
        gfx_Begin();
        gfx_started = true;
    }

    set_game_palette();

    /* load game graphics: skip 17 bytes of AppVar header */
    if (!load_appvar_rle("GDGrphc", TILES_GAME_BUF, 17, GAME_GFX_SIZE))
        return false;

    return true;
}

bool gfx_menu_init(void)
{
    /* load menu graphics: skip 16 bytes of AppVar header */
    return load_appvar_rle("GDMenu", TILES_MENU_BUF, 16, MENU_GFX_SIZE);
}

void gfx_cleanup(void)
{
    if (gfx_started) {
        gfx_End();
        gfx_started = false;
    }
}

const uint8_t *tile_data(uint8_t id)
{
    return TILES_GAME_BUF + OFF_TILES + (uint24_t)id * TILE_W * TILE_H;
}

const uint8_t *jump_frame(uint8_t frame)
{
    return TILES_GAME_BUF + OFF_JUMP + (uint24_t)frame * SPR_W * SPR_H;
}

const uint8_t *ship_frame(uint8_t frame)
{
    return TILES_GAME_BUF + OFF_SHIP + (uint24_t)frame * SHIP_W * SHIP_H;
}

const uint8_t *font_char(uint8_t ch)
{
    return TILES_MENU_BUF + OFF_FONT + (uint24_t)ch * FONT_W * FONT_H;
}

const uint8_t *menu_data(uint24_t offset)
{
    return TILES_MENU_BUF + OFF_MENU_TILES + offset;
}

void draw_tile(uint8_t *buf, int x, int y, uint8_t tile_id)
{
    const uint8_t *src = tile_data(tile_id);

    /* clip to screen bounds */
    int draw_w = TILE_W;
    int draw_h = TILE_H;
    if (x + draw_w > LCD_WIDTH)  draw_w = LCD_WIDTH - x;
    if (y + draw_h > LCD_HEIGHT) draw_h = LCD_HEIGHT - y;
    if (x < 0 || y < 0 || draw_w <= 0 || draw_h <= 0) return;

    uint8_t *dst = buf + y * LCD_WIDTH + x;

    for (int row = 0; row < draw_h; row++) {
        for (int col = 0; col < draw_w; col++) {
            uint8_t px = src[row * TILE_W + col];
            if (px != BG_COLOR)
                dst[col] = px;
        }
        dst += LCD_WIDTH;
    }
}

void draw_sprite_transparent(uint8_t *buf, int x, int y,
                             const uint8_t *sprite, int w, int h)
{
    if (!buf || !sprite || w <= 0 || h <= 0)
        return;

    int start_col = 0;
    int start_row = 0;
    int end_col = w;
    int end_row = h;

    if (x < 0) start_col = -x;
    if (y < 0) start_row = -y;
    if (x + end_col > LCD_WIDTH) end_col = LCD_WIDTH - x;
    if (y + end_row > LCD_HEIGHT) end_row = LCD_HEIGHT - y;

    if (start_col >= end_col || start_row >= end_row)
        return;

    for (int row = start_row; row < end_row; row++) {
        const uint8_t *src_row = sprite + row * w;
        uint8_t *dst = buf + (y + row) * LCD_WIDTH + x + start_col;
        for (int col = start_col; col < end_col; col++) {
            uint8_t px = src_row[col];
            if (px != BG_COLOR)
                dst[col - start_col] = px;
        }
    }
}

void draw_sprite_transparent_mirror(uint8_t *buf, int x, int y,
                                    const uint8_t *sprite, int w, int h)
{
    if (!buf || !sprite || w <= 0 || h <= 0)
        return;

    int start_col = 0;
    int start_row = 0;
    int end_col = w;
    int end_row = h;

    if (x < 0) start_col = -x;
    if (y < 0) start_row = -y;
    if (x + end_col > LCD_WIDTH) end_col = LCD_WIDTH - x;
    if (y + end_row > LCD_HEIGHT) end_row = LCD_HEIGHT - y;

    if (start_col >= end_col || start_row >= end_row)
        return;

    for (int row = start_row; row < end_row; row++) {
        const uint8_t *src_row = sprite + row * w;
        uint8_t *dst = buf + (y + row) * LCD_WIDTH + x + start_col;
        for (int col = start_col; col < end_col; col++) {
            uint8_t px = src_row[w - 1 - col];
            if (px != BG_COLOR)
                dst[col - start_col] = px;
        }
    }
}

void draw_gd_text(uint8_t *buf, int x, int y, int buf_w, int buf_h,
                  const uint8_t *str, uint8_t len)
{
    if (!buf || !str || buf_w <= 0 || buf_h <= 0)
        return;

    for (uint8_t i = 0; i < len; i++) {
        uint8_t ch = str[i];
        if (ch == ' ') {
            x += 10;
            continue;
        }
        if (ch < 1 || ch > NUM_FONT_CHARS) {
            x += 25;
            continue;
        }
        /* character index: 1-based in the original format */
        const uint8_t *glyph = font_char(ch - 1);
        for (int row = 0; row < FONT_H; row++) {
            int dy = y + row;
            if (dy < 0 || dy >= buf_h)
                continue;
            uint8_t *dst = buf + dy * buf_w;
            for (int col = 0; col < FONT_W; col++) {
                int dx = x + col;
                if (dx < 0 || dx >= buf_w)
                    continue;
                uint8_t px = glyph[row * FONT_W + col];
                if (px != BG_COLOR_MENU)
                    dst[dx] = px;
            }
        }
        x += 25; /* character advance */
    }
}

int center_text_x(const uint8_t *str, uint8_t len, int space)
{
    int w = 0;
    for (uint8_t i = 0; i < len; i++) {
        w += (str[i] == ' ') ? 10 : 25;
    }
    return (space - w) / 2;
}

void draw_filled_circle(uint8_t *buf, int cx, int cy, int r, uint8_t color)
{
    for (int dy = -r; dy <= r; dy++) {
        int half_w = 0;
        /* integer sqrt: find max dx where dx*dx + dy*dy <= r*r */
        while ((half_w + 1) * (half_w + 1) + dy * dy <= r * r)
            half_w++;
        int y = cy + dy;
        if (y < 0 || y >= LCD_HEIGHT) continue;
        uint8_t *row = buf + y * LCD_WIDTH;
        for (int dx = -half_w; dx <= half_w; dx++) {
            int x = cx + dx;
            if (x >= 0 && x < LCD_WIDTH)
                row[x] = color;
        }
    }
}

void page_flip(void)
{
    gfx_SwapDraw();
}
