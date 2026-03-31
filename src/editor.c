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
#define CURSOR_COLOR 0x03 /* cyan */

static uint8_t *edit_data;
static uint24_t edit_map_size_x;
static uint8_t  edit_extra_rows;
static uint8_t *edit_map_start;
static uint24_t edit_scroll_col;
static uint8_t  cursor_x;
static uint8_t  cursor_y;
static uint8_t  current_block;

static void editor_draw_map(void);
static void editor_draw_cursor(void);
static void editor_draw_block_selector(void);
static bool editor_expand_map(void);

void editor_run(uint8_t *level_data_addr)
{
    edit_data = level_data_addr;

    /* skip: difficulty(1) + id(3) + speed(1) = 5 bytes */
    uint8_t *p = edit_data + 5;

    /* map_size_x */
    edit_map_size_x = p[0] | ((uint24_t)p[1] << 8) | ((uint24_t)p[2] << 16);
    p += 3;

    /* extra rows */
    edit_extra_rows = *p++;

    /* map data start */
    edit_map_start = p;

    /* init cursor */
    edit_scroll_col = 0;
    cursor_x = 0;
    cursor_y = WIN_ROWS / 2;
    current_block = 1;

    /* set up display */
    set_game_palette();
    gfx_FillScreen(BG_COLOR);

    /* draw floor */
    gfx_SetColor(0x06);
    gfx_HorizLine_NoClip(0, GAME_AREA_H, LCD_WIDTH);
    gfx_SetColor(0x7E);
    gfx_FillRectangle_NoClip(0, GAME_AREA_H + 1, LCD_WIDTH, LCD_HEIGHT - GAME_AREA_H - 1);

    gfx_BlitBuffer();

    editor_draw_map();

    while (true) {
        kb_Scan();
        bool redraw = false;

        /* quit */
        if (kb_IsDown(kb_KeyClear) || kb_IsDown(kb_KeyMode)) {
            /* archive the AppVar to save changes */
            break;
        }

        /* navigation */
        if (kb_IsDown(kb_KeyRight)) {
            if (cursor_x < EDITOR_COLS - 1) {
                cursor_x++;
            } else if (edit_scroll_col + EDITOR_COLS < edit_map_size_x) {
                edit_scroll_col++;
                redraw = true;
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

        /* place/remove block */
        if (kb_IsDown(kb_KeyEnter) || kb_IsDown(kb_Key2nd)) {
            uint24_t map_col = edit_scroll_col + cursor_x;
            uint24_t map_idx = (uint24_t)cursor_y * edit_map_size_x + map_col;
            edit_map_start[map_idx] = current_block;
            redraw = true;
            delay(80);
        }
        if (kb_IsDown(kb_KeyDel)) {
            uint24_t map_col = edit_scroll_col + cursor_x;
            uint24_t map_idx = (uint24_t)cursor_y * edit_map_size_x + map_col;
            edit_map_start[map_idx] = 0;
            redraw = true;
            delay(80);
        }

        /* cycle block type */
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

        /* insert 10 columns */
        if (kb_IsDown(kb_KeyAlpha)) {
            if (editor_expand_map())
                redraw = true;
            delay(200);
        }

        /* play from cursor position */
        if (kb_IsDown(kb_KeyGraphVar)) {
            level_load(ms.current_idx);
            gs.beg_lvl_to_play = edit_scroll_col + cursor_x;
            gs.flags.in_editor = true;
            gfx_game_init(); /* reload game sprites (menu overwrites them) */
            game_run();

            /* restore editor display */
            set_game_palette();
            gfx_FillScreen(BG_COLOR);
            gfx_SetColor(0x06);
            gfx_HorizLine_NoClip(0, GAME_AREA_H, LCD_WIDTH);
            gfx_SetColor(0x7E);
            gfx_FillRectangle_NoClip(0, GAME_AREA_H + 1, LCD_WIDTH,
                                      LCD_HEIGHT - GAME_AREA_H - 1);
            gfx_BlitBuffer();
            redraw = true;
        }

        if (redraw)
            editor_draw_map();

        editor_draw_cursor();
        editor_draw_block_selector();
        gfx_SwapDraw();
    }
}

static void editor_draw_map(void)
{
    /* clear game area */
    gfx_SetColor(BG_COLOR);
    gfx_FillRectangle_NoClip(0, 0, LCD_WIDTH, GAME_AREA_H);

    /* draw visible tiles */
    for (int col = 0; col < EDITOR_COLS && edit_scroll_col + col < edit_map_size_x; col++) {
        for (int row = 0; row < WIN_ROWS; row++) {
            uint24_t map_idx = (uint24_t)row * edit_map_size_x + edit_scroll_col + col;
            uint8_t tile_id = edit_map_start[map_idx];
            if (tile_id > 0 && tile_id < NUM_GAME_TILES) {
                draw_tile(GFX_VBUF, col * TILE_W, row * TILE_H, tile_id);
            }
        }
    }
}

static void editor_draw_cursor(void)
{
    int x = cursor_x * TILE_W;
    int y = cursor_y * TILE_H;

    /* draw cursor outline */
    gfx_SetColor(CURSOR_COLOR);
    gfx_Rectangle_NoClip(x, y, TILE_W, TILE_H);
    gfx_Rectangle_NoClip(x + 1, y + 1, TILE_W - 2, TILE_H - 2);
}

static bool editor_expand_map(void)
{
    uint8_t num_rows = WIN_ROWS + edit_extra_rows;
    uint24_t old_sx = edit_map_size_x;
    uint24_t new_sx = old_sx + 10;
    uint24_t expand = (uint24_t)10 * num_rows;

    /* open the AppVar */
    char *av_name = (char *)ms.levels[ms.current_idx].vat_name_addr;
    ti_var_t slot = ti_Open(av_name, "r+");
    if (!slot) return false;

    /* compute offsets before resize */
    uint8_t *orig_base = ti_GetDataPtr(slot);
    uint24_t data_off = (uint24_t)(edit_data - orig_base);
    uint24_t map_off  = (uint24_t)(edit_map_start - orig_base);

    /* resize the AppVar */
    uint24_t old_size = ti_GetSize(slot);
    uint24_t new_size = old_size + expand;
    if ((uint24_t)ti_Resize(new_size, slot) != new_size) {
        ti_Close(slot);
        return false;
    }

    /* recompute all pointers (data may have moved after resize) */
    uint8_t *base = ti_GetDataPtr(slot);
    edit_data = base + data_off;
    edit_map_start = base + map_off;
    ms.levels[ms.current_idx].data_addr = base;
    ms.levels[ms.current_idx].after_name_addr = edit_data;

    /* move context data (gravity + ship) forward */
    uint24_t ctx_off = map_off + (uint24_t)num_rows * old_sx;
    uint24_t ctx_size = old_size - ctx_off;
    memmove(base + ctx_off + expand, base + ctx_off, ctx_size);

    /* rearrange rows back-to-front to avoid overwrites */
    for (int row = num_rows - 1; row >= 1; row--) {
        uint8_t *src = edit_map_start + (uint24_t)row * old_sx;
        uint8_t *dst = edit_map_start + (uint24_t)row * new_sx;
        memmove(dst, src, old_sx);
        memset(dst + old_sx, 0, 10);
    }
    /* row 0 is already in place, just zero the new columns */
    memset(edit_map_start + old_sx, 0, 10);

    /* update map_size_x in AppVar data */
    uint8_t *sx_ptr = edit_data + 5; /* after diff(1) + id(3) + speed(1) */
    sx_ptr[0] = new_sx & 0xFF;
    sx_ptr[1] = (new_sx >> 8) & 0xFF;
    sx_ptr[2] = (new_sx >> 16) & 0xFF;

    edit_map_size_x = new_sx;

    ti_Close(slot);
    return true;
}

static void editor_draw_block_selector(void)
{
    /* draw current block preview in bottom-right */
    int preview_x = LCD_WIDTH - TILE_W - 5;
    int preview_y = GAME_AREA_H + 5;

    gfx_SetColor(0x00); /* black border */
    gfx_Rectangle_NoClip(preview_x - 1, preview_y - 1, TILE_W + 2, TILE_H + 2);

    draw_tile(GFX_VBUF, preview_x, preview_y, current_block);
}
