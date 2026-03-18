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
            /* expand map by 10 columns - would need to resize AppVar */
            /* skip for now as it's complex */
            delay(200);
        }

        /* play from cursor position */
        if (kb_IsDown(kb_KeyGraphVar)) {
            level_load(ms.current_idx);
            gs.beg_lvl_to_play = edit_scroll_col + cursor_x;
            gs.flags.in_editor = true;
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
                draw_tile(gfx_vbuffer, col * TILE_W, row * TILE_H, tile_id);
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

static void editor_draw_block_selector(void)
{
    /* draw current block preview in bottom-right */
    int preview_x = LCD_WIDTH - TILE_W - 5;
    int preview_y = GAME_AREA_H + 5;

    gfx_SetColor(0x00); /* black border */
    gfx_Rectangle_NoClip(preview_x - 1, preview_y - 1, TILE_W + 2, TILE_H + 2);

    draw_tile(gfx_vbuffer, preview_x, preview_y, current_block);
}
