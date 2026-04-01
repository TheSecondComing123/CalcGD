#include <tice.h>
#include <graphx.h>
#include <keypadc.h>
#include <fileioc.h>
#include <string.h>

#include "defs.h"
#include "gfx.h"
#include "editor.h"
#include "game.h"
#include "level.h"

#define EDITOR_COLS 15
#define CURSOR_COLOR 0x03

static ti_var_t edit_slot;
static uint8_t edit_level_idx;

static uint8_t *edit_data;
static uint24_t edit_map_size_x;
static uint8_t edit_extra_rows;
static uint8_t *edit_map_start;
static uint24_t edit_map_off;
static uint24_t edit_scroll_col;
static uint8_t cursor_x;
static uint8_t cursor_y;
static uint8_t current_block;

static void editor_draw_map(void);
static void editor_draw_map_single(void);
static void editor_draw_cursor(void);
static void editor_draw_block_selector(void);
static bool editor_expand_map(void);
static void editor_clamp_cursor(void);

static bool editor_parse_level(void)
{
    if (!edit_slot) return false;

    uint24_t size = ti_GetSize(edit_slot);
    uint8_t *base = ti_GetDataPtr(edit_slot);

    if (size < LEVEL_SIG_LEN + 1) return false;

    uint24_t off = LEVEL_SIG_LEN;
    uint8_t len1 = base[off++];
    if ((uint32_t)off + len1 + 1u > size) return false;
    off += len1;

    uint8_t len2 = base[off++];
    if ((uint32_t)off + len2 + 5u + 3u + 1u > size) return false;

    edit_data = base + off + len2;

    uint8_t *p = edit_data + 5;
    edit_map_size_x = p[0] | ((uint24_t)p[1] << 8) | ((uint24_t)p[2] << 16);
    if (edit_map_size_x == 0) return false;
    p += 3;

    edit_extra_rows = *p++;

    edit_map_start = p;
    edit_map_off = (uint24_t)(edit_map_start - base);

    uint32_t rows = (uint32_t)WIN_ROWS + edit_extra_rows;
    uint32_t tiles = rows * (uint32_t)edit_map_size_x;
    if (tiles > (uint32_t)(size - edit_map_off)) return false;

    return true;
}

void editor_run(uint8_t level_idx)
{
    if (level_idx >= ms.num_levels) return;

    ms.current_idx = level_idx;
    edit_level_idx = level_idx;
    edit_slot = ti_Open(ms.levels[level_idx].vat_name, "r+");
    if (!edit_slot) return;

    if (!editor_parse_level()) {
        ti_Close(edit_slot);
        edit_slot = 0;
        return;
    }

    if (!gfx_game_init()) {
        ti_Close(edit_slot);
        edit_slot = 0;
        return;
    }

    edit_scroll_col = 0;
    cursor_x = 0;
    cursor_y = WIN_ROWS / 2;
    editor_clamp_cursor();
    current_block = 1;

    set_game_palette();
    gfx_FillScreen(BG_COLOR);

    gfx_SetColor(0x06);
    gfx_HorizLine_NoClip(0, GAME_AREA_H, LCD_WIDTH);
    gfx_SetColor(0x7E);
    gfx_FillRectangle_NoClip(0, GAME_AREA_H + 1, LCD_WIDTH, LCD_HEIGHT - GAME_AREA_H - 1);

    gfx_BlitBuffer();
    editor_draw_map();

    while (true) {
        kb_Scan();
        bool redraw = false;

        if (kb_IsDown(kb_KeyClear) || kb_IsDown(kb_KeyMode))
            break;

        if (kb_IsDown(kb_KeyRight)) {
            uint24_t cur_col = edit_scroll_col + cursor_x;
            if (cur_col + 1 < edit_map_size_x) {
                if (cursor_x < EDITOR_COLS - 1) {
                    cursor_x++;
                } else {
                    edit_scroll_col++;
                    redraw = true;
                }
            }
            delay(80);
        }
        if (kb_IsDown(kb_KeyLeft)) {
            if (cursor_x > 0) {
                cursor_x--;
            } else if (edit_scroll_col > 0) {
                edit_scroll_col--;
                redraw = true;
            }
            delay(80);
        }
        if (kb_IsDown(kb_KeyUp)) {
            if (cursor_y > 0) cursor_y--;
            delay(80);
        }
        if (kb_IsDown(kb_KeyDown)) {
            if (cursor_y < WIN_ROWS - 1) cursor_y++;
            delay(80);
        }

        if (kb_IsDown(kb_KeyEnter) || kb_IsDown(kb_Key2nd)) {
            uint24_t map_col = edit_scroll_col + cursor_x;
            if (map_col < edit_map_size_x) {
                uint24_t map_idx = (uint24_t)cursor_y * edit_map_size_x + map_col;
                edit_map_start[map_idx] = current_block;
                redraw = true;
            }
            delay(80);
        }
        if (kb_IsDown(kb_KeyDel)) {
            uint24_t map_col = edit_scroll_col + cursor_x;
            if (map_col < edit_map_size_x) {
                uint24_t map_idx = (uint24_t)cursor_y * edit_map_size_x + map_col;
                edit_map_start[map_idx] = 0;
                redraw = true;
            }
            delay(80);
        }

        if (kb_IsDown(kb_KeyAdd)) {
            current_block++;
            if (current_block >= NUM_GAME_TILES) current_block = 1;
            delay(80);
        }
        if (kb_IsDown(kb_KeySub)) {
            if (current_block <= 1) current_block = NUM_GAME_TILES - 1;
            else current_block--;
            delay(80);
        }

        if (kb_IsDown(kb_KeyAlpha)) {
            if (editor_expand_map()) {
                editor_clamp_cursor();
                redraw = true;
            }
            delay(200);
        }

        if (kb_IsDown(kb_KeyGraphVar)) {
            uint24_t play_col = edit_scroll_col + cursor_x;

            ti_SetArchiveStatus(true, edit_slot);
            ti_Close(edit_slot);
            edit_slot = 0;

            gs.beg_lvl_to_play = play_col;
            gs.flags.in_editor = true;
            if (gfx_game_init())
                game_run();

            edit_slot = ti_Open(ms.levels[edit_level_idx].vat_name, "r+");
            if (!edit_slot)
                return;
            if (!editor_parse_level()) {
                ti_Close(edit_slot);
                edit_slot = 0;
                return;
            }

            if (edit_scroll_col + cursor_x >= edit_map_size_x) {
                if (edit_map_size_x > EDITOR_COLS) {
                    edit_scroll_col = edit_map_size_x - EDITOR_COLS;
                    cursor_x = EDITOR_COLS - 1;
                } else {
                    edit_scroll_col = 0;
                    cursor_x = (uint8_t)(edit_map_size_x - 1);
                }
            }
            editor_clamp_cursor();

            set_game_palette();
            gfx_FillScreen(BG_COLOR);
            gfx_SetColor(0x06);
            gfx_HorizLine_NoClip(0, GAME_AREA_H, LCD_WIDTH);
            gfx_SetColor(0x7E);
            gfx_FillRectangle_NoClip(0, GAME_AREA_H + 1, LCD_WIDTH, LCD_HEIGHT - GAME_AREA_H - 1);
            gfx_BlitBuffer();
            redraw = true;
        }

        if (redraw)
            editor_draw_map();

        editor_draw_cursor();
        editor_draw_block_selector();
        gfx_SwapDraw();
    }

    ti_SetArchiveStatus(true, edit_slot);
    ti_Close(edit_slot);
    edit_slot = 0;
}

static void editor_draw_map(void)
{
    for (uint8_t buf = 0; buf < 2; buf++) {
        editor_draw_map_single();
        if (buf == 0)
            gfx_SwapDraw();
    }
    gfx_SwapDraw();
}

static void editor_draw_map_single(void)
{
    gfx_SetColor(BG_COLOR);
    gfx_FillRectangle_NoClip(0, 0, LCD_WIDTH, GAME_AREA_H);

    for (int col = 0; col < EDITOR_COLS && edit_scroll_col + col < edit_map_size_x; col++) {
        for (int row = 0; row < WIN_ROWS; row++) {
            uint24_t map_idx = (uint24_t)row * edit_map_size_x + edit_scroll_col + col;
            uint8_t tile_id = edit_map_start[map_idx];
            if (tile_id > 0 && tile_id < NUM_GAME_TILES)
                draw_tile(GFX_VBUF, col * TILE_W, row * TILE_H, tile_id);
        }
    }
}

static void editor_draw_cursor(void)
{
    int x = cursor_x * TILE_W;
    int y = cursor_y * TILE_H;

    gfx_SetColor(CURSOR_COLOR);
    gfx_Rectangle_NoClip(x, y, TILE_W, TILE_H);
    gfx_Rectangle_NoClip(x + 1, y + 1, TILE_W - 2, TILE_H - 2);
}

static bool editor_expand_map(void)
{
    if (!edit_slot) return false;

    uint16_t num_rows = (uint16_t)WIN_ROWS + edit_extra_rows;
    if (num_rows == 0)
        return false;

    uint24_t old_sx = edit_map_size_x;
    uint24_t new_sx = old_sx + 10;
    uint24_t expand = (uint24_t)10 * (uint24_t)num_rows;

    uint24_t old_size = ti_GetSize(edit_slot);
    uint24_t new_size = old_size + expand;
    if (new_size < old_size)
        return false;

    uint8_t *orig_base = ti_GetDataPtr(edit_slot);
    uint24_t data_off = (uint24_t)(edit_data - orig_base);
    uint24_t map_off = (uint24_t)(edit_map_start - orig_base);

    if ((uint24_t)ti_Resize(new_size, edit_slot) != new_size)
        return false;

    uint8_t *base = ti_GetDataPtr(edit_slot);
    edit_data = base + data_off;
    edit_map_start = base + map_off;

    uint32_t ctx_off32 = (uint32_t)map_off + (uint32_t)num_rows * (uint32_t)old_sx;
    if (ctx_off32 > old_size)
        return false;
    uint24_t ctx_off = (uint24_t)ctx_off32;

    uint24_t ctx_size = old_size - ctx_off;
    memmove(base + ctx_off + expand, base + ctx_off, ctx_size);

    for (int row = (int)num_rows - 1; row >= 1; row--) {
        uint8_t *src = edit_map_start + (uint24_t)row * old_sx;
        uint8_t *dst = edit_map_start + (uint24_t)row * new_sx;
        memmove(dst, src, old_sx);
        memset(dst + old_sx, 0, 10);
    }
    memset(edit_map_start + old_sx, 0, 10);

    uint8_t *sx_ptr = edit_data + 5;
    sx_ptr[0] = new_sx & 0xFF;
    sx_ptr[1] = (new_sx >> 8) & 0xFF;
    sx_ptr[2] = (new_sx >> 16) & 0xFF;

    edit_map_size_x = new_sx;
    ms.levels[edit_level_idx].map_size_x = new_sx;

    return true;
}

static void editor_draw_block_selector(void)
{
    int preview_x = LCD_WIDTH - TILE_W - 5;
    int preview_y = GAME_AREA_H + 5;

    gfx_SetColor(0x00);
    gfx_Rectangle_NoClip(preview_x - 1, preview_y - 1, TILE_W + 2, TILE_H + 2);

    draw_tile(GFX_VBUF, preview_x, preview_y, current_block);
}

static void editor_clamp_cursor(void)
{
    if (edit_map_size_x == 0) {
        cursor_x = 0;
        return;
    }

    uint24_t max_x = (edit_map_size_x > EDITOR_COLS) ? (EDITOR_COLS - 1) : (edit_map_size_x - 1);
    if (cursor_x > max_x)
        cursor_x = (uint8_t)max_x;
}
