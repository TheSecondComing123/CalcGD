#include <tice.h>
#include <graphx.h>
#include <keypadc.h>
#include <fileioc.h>
#include <string.h>

#include "defs.h"
#include "gfx.h"
#include "menu.h"
#include "level.h"
#include "game.h"
#include "editor.h"

/* text encoding for "EMPTY" in GD font: E=5, M=13, P=16, T=20, Y=25 */
static const uint8_t TXT_EMPTY[] = { 5, 5, 13, 16, 20, 25 };

/* offsets into menu sprite data */
#define MENU_SIDE_OFFSET     0
#define MENU_SIDE2_OFFSET    5050
#define MENU_HEADER_OFFSET   (5050 + 1724)
#define MENU_DIFF_OFFSET     1442
#define MENU_DIFF_SIZE       (30 * 30 + 2)
#define MENU_HIGHSCORE_OFFSET (OFF_SHIP + SHIP_W * SHIP_H * NUM_SHIP_FRAMES)

/* shared scratch buffer for compositing info rectangles (80x250).
   used by draw_level_info, menu_create_level, and the empty-levels path.
   only one runs at a time, so sharing is safe. */
static uint8_t rect_buf[80 * 250];

static void draw_menu_bg(void);
static void draw_level_info(void);
static void menu_create_level(void);
static uint8_t get_key_letter(void);

void menu_run(void)
{
    while (true) {
        /* reload menu graphics each time we return from game/editor,
           since menu and game share the same scratch buffer */
        if (!gfx_menu_init()) {
            gfx_cleanup();
            os_ClrHome();
            os_PutStrFull("Need AppVar GDMenu");
            while (!os_GetCSC());
            return;
        }

        set_menu_palette();
        level_scan();

        draw_menu_bg();

        if (ms.num_levels == 0) {
            /* no levels found */
            /* draw info rectangle with "EMPTY" text */
            memset(rect_buf, 0x08, sizeof(rect_buf));
            int tx = center_text_x(TXT_EMPTY + 1, TXT_EMPTY[0], 25 * 8);
            draw_gd_text(rect_buf, 40 + tx, 27, 250, 80, TXT_EMPTY + 1, TXT_EMPTY[0]);

            /* copy rect to screen */
            uint8_t *screen = GFX_VBUF;
            for (int y = 0; y < 80; y++) {
                memcpy(screen + (80 + y) * LCD_WIDTH + 35, rect_buf + y * 250, 250);
            }
            gfx_SwapDraw();

            /* wait for quit */
            while (true) {
                kb_Scan();
                if (kb_IsDown(kb_KeyClear) || kb_IsDown(kb_KeyMode))
                    return;
            }
        }

        /* draw level info for current selection */
        draw_level_info();

        /* menu input loop */
        bool quit = false;
        bool play = false;
        bool edit = false;
        bool create = false;

        while (!quit && !play && !edit && !create) {
            kb_Scan();

            if (kb_IsDown(kb_KeyClear) || kb_IsDown(kb_KeyMode) || kb_IsDown(kb_KeyDel)) {
                quit = true;
            } else if (kb_IsDown(kb_KeyEnter) || kb_IsDown(kb_Key2nd)) {
                play = true;
            } else if (kb_IsDown(kb_KeyAlpha)) {
                edit = true;
            } else if (kb_IsDown(kb_KeyAdd)) {
                create = true;
            } else if (kb_IsDown(kb_KeyLeft)) {
                /* previous level */
                if (ms.current_idx == 0)
                    ms.current_idx = ms.num_levels - 1;
                else
                    ms.current_idx--;
                while (kb_IsDown(kb_KeyLeft)) kb_Scan();
                draw_level_info();
            } else if (kb_IsDown(kb_KeyRight)) {
                /* next level */
                ms.current_idx++;
                if (ms.current_idx >= ms.num_levels)
                    ms.current_idx = 0;
                while (kb_IsDown(kb_KeyRight)) kb_Scan();
                draw_level_info();
            }
        }

        while (kb_AnyKey()) kb_Scan(); /* wait for key release */

        if (quit) return;

        if (play) {
            gs.beg_lvl_to_play = 0;
            gs.flags.in_editor = false;
            if (!gfx_game_init()) {
                gfx_cleanup();
                os_ClrHome();
                os_PutStrFull("Need AppVar GDGrphc");
                while (!os_GetCSC());
                return;
            }
            game_run();
            continue; /* return to menu */
        }

        if (edit) {
            editor_run(ms.current_idx);
            continue;
        }

        if (create) {
            menu_create_level();
            continue;
        }
    }
}

static void draw_menu_bg(void)
{
    /* clear screen with menu background color */
    gfx_FillScreen(BG_COLOR_MENU);

    /* draw floor */
    gfx_SetColor(0x00); /* black line */
    gfx_HorizLine_NoClip(0, GAME_AREA_H, LCD_WIDTH);
    gfx_SetColor(0x06); /* floor color */
    gfx_FillRectangle_NoClip(0, GAME_AREA_H + 1, LCD_WIDTH, LCD_HEIGHT - GAME_AREA_H - 1);

    /* draw side decorations */
    draw_sprite_transparent(GFX_VBUF, 2, 100,
                            menu_data(MENU_SIDE_OFFSET), 30, 100);
    draw_sprite_transparent_mirror(GFX_VBUF, LCD_WIDTH - 30 - 2, 100,
                                   menu_data(MENU_SIDE_OFFSET), 30, 100);

    /* draw header */
    draw_sprite_transparent(GFX_VBUF, 60, 0,
                            menu_data(MENU_HEADER_OFFSET), 200, 50);

    /* draw progress bar border */
    for (int i = 0; i < 22; i++) {
        draw_filled_circle(GFX_VBUF, 77 + i * 8, 170, 13, 0x09);
    }

    /* copy to second buffer */
    gfx_BlitBuffer();
}

static void draw_level_info(void)
{
    /* draw info rectangle */
    memset(rect_buf, 0x08, sizeof(rect_buf));

    /* get level name */
    const uint8_t *line1, *line2;
    uint8_t len1, len2;
    uint8_t lines = level_get_name(ms.current_idx, &line1, &len1, &line2, &len2);

    if (lines == 1) {
        int tx = center_text_x(line1, len1, 25 * 8);
        draw_gd_text(rect_buf, 40 + tx, 27, 250, 80, line1, len1);
    } else {
        int tx = center_text_x(line1, len1, 25 * 8);
        draw_gd_text(rect_buf, 40 + tx, 12, 250, 80, line1, len1);
        tx = center_text_x(line2, len2, 25 * 8);
        draw_gd_text(rect_buf, 40 + tx, 45, 250, 80, line2, len2);
    }

    /* draw difficulty icon */
    uint8_t diff = level_get_difficulty(ms.current_idx);
    const uint8_t *diff_icon = menu_data(MENU_DIFF_OFFSET + diff * MENU_DIFF_SIZE);
    draw_sprite_transparent(rect_buf, 5, 23, diff_icon, 30, 30);

    /* copy rectangle to screen (both buffers) */
    for (int buf = 0; buf < 2; buf++) {
        uint8_t *screen = GFX_VBUF;
        for (int y = 0; y < 80; y++) {
            memcpy(screen + (55 + y) * LCD_WIDTH + 35, rect_buf + y * 250, 250);
        }
        if (buf == 0) gfx_SwapDraw();
    }
    gfx_SwapDraw(); /* restore original draw target */

    /* draw progress bar fill */
    uint24_t level_id = level_get_id(ms.current_idx);
    uint24_t high_score = score_find(level_id);

    if (high_score > 0) {
        uint24_t map_sx = ms.levels[ms.current_idx].map_size_x;

        uint8_t pct = 0;
        if (map_sx > 0) {
            uint32_t scaled = ((uint32_t)high_score * 21u) / (uint32_t)map_sx;
            if (scaled > 21u) scaled = 21u;
            pct = (uint8_t)scaled;
        }
        if (pct > 21) pct = 21;

        for (uint8_t i = 0; i < pct; i++) {
            draw_filled_circle(GFX_VBUF, 81 + i * 8, 170, 10, 0x2E);
        }
        gfx_SwapDraw();
        for (uint8_t i = 0; i < pct; i++) {
            draw_filled_circle(GFX_VBUF, 81 + i * 8, 170, 10, 0x2E);
        }
        gfx_SwapDraw();
    }
}

static void menu_create_level(void)
{
    uint8_t name_buf[22]; /* [len1][chars_1...][len2][chars_2...] */
    memset(name_buf, 0, sizeof(name_buf));
    uint8_t difficulty = 0;
    bool second_line = false;

    while (true) {
        /* draw name entry screen */
        memset(rect_buf, 0x08, sizeof(rect_buf));

        /* draw current name */
        uint8_t len1 = name_buf[0];
        if (len1 > 0) {
            int tx = center_text_x(name_buf + 1, len1, 25 * 8);
            if (second_line) {
                draw_gd_text(rect_buf, 40 + tx, 12, 250, 80, name_buf + 1, len1);
                uint8_t len2 = name_buf[1 + len1];
                if (len2 > 0) {
                    tx = center_text_x(name_buf + 2 + len1, len2, 25 * 8);
                    draw_gd_text(rect_buf, 40 + tx, 45, 250, 80, name_buf + 2 + len1, len2);
                }
            } else {
                draw_gd_text(rect_buf, 40 + tx, 27, 250, 80, name_buf + 1, len1);
            }
        }

        /* draw difficulty icon */
        const uint8_t *diff_icon = menu_data(MENU_DIFF_OFFSET + difficulty * MENU_DIFF_SIZE);
        draw_sprite_transparent(rect_buf, 5, 23, diff_icon, 30, 30);

        /* copy to screen */
        uint8_t *screen = GFX_VBUF;
        for (int y = 0; y < 80; y++) {
            memcpy(screen + (55 + y) * LCD_WIDTH + 35, rect_buf + y * 250, 250);
        }
        gfx_SwapDraw();

        delay(100);

        /* get key input */
        uint8_t key = get_key_letter();

        if (key == 0xFF) return; /* CLEAR - cancel */
        if (key == 0xFE) {
            /* DEL - delete last character */
            if (second_line) {
                uint8_t len1b = name_buf[0];
                uint8_t *len2p = &name_buf[1 + len1b];
                if (*len2p > 0) {
                    (*len2p)--;
                } else {
                    second_line = false;
                }
            } else {
                if (name_buf[0] > 0) name_buf[0]--;
            }
        } else if (key == 0xFD) {
            /* ENTER */
            if (second_line) {
                /* create the level */
                uint8_t total_name = 1 + name_buf[0] + 1 + name_buf[1 + name_buf[0]];
                if (total_name <= sizeof(name_buf) && level_create(name_buf, difficulty))
                    return;
            } else if (name_buf[0] > 0) {
                second_line = true;
                name_buf[1 + name_buf[0]] = 0; /* len2 = 0 */
            }
        } else if (key == 0xFC) {
            /* UP - change difficulty */
            difficulty = (difficulty + 1) & 0x03;
        } else if (key == 0xFB) {
            /* DOWN - change difficulty */
            if (difficulty == 0) difficulty = 3;
            else difficulty--;
        } else {
            /* letter input */
            uint8_t *len_ptr;
            if (second_line) {
                len_ptr = &name_buf[1 + name_buf[0]];
            } else {
                len_ptr = &name_buf[0];
            }
            if (*len_ptr < 8) {
                (*len_ptr)++;
                uint8_t char_off;
                if (len_ptr == &name_buf[0])
                    char_off = 1 + *len_ptr - 1;
                else
                    char_off = 2 + name_buf[0] + *len_ptr - 1;

                if (char_off < sizeof(name_buf))
                    name_buf[char_off] = key;
            }
        }
    }
}

static uint8_t get_key_letter(void)
{
    while (true) {
        kb_Scan();

        if (kb_IsDown(kb_KeyClear)) { while (kb_IsDown(kb_KeyClear)) kb_Scan(); return 0xFF; }
        if (kb_IsDown(kb_KeyDel))   { while (kb_IsDown(kb_KeyDel)) kb_Scan();   return 0xFE; }
        if (kb_IsDown(kb_KeyEnter)) { while (kb_IsDown(kb_KeyEnter)) kb_Scan(); return 0xFD; }
        if (kb_IsDown(kb_KeyUp))    { while (kb_IsDown(kb_KeyUp)) kb_Scan();    return 0xFC; }
        if (kb_IsDown(kb_KeyDown))  { while (kb_IsDown(kb_KeyDown)) kb_Scan();  return 0xFB; }

        /* letter keys - map to GD font character indices */
        /* A=1, B=2, ... Z=26, space=' ' */
        if (kb_IsDown(kb_KeyMath))   { while (kb_AnyKey()) kb_Scan(); return 1; }  /* A */
        if (kb_IsDown(kb_KeyApps))   { while (kb_AnyKey()) kb_Scan(); return 2; }  /* B */
        if (kb_IsDown(kb_KeyPrgm))   { while (kb_AnyKey()) kb_Scan(); return 3; }  /* C */
        if (kb_IsDown(kb_KeyRecip))  { while (kb_AnyKey()) kb_Scan(); return 4; }  /* D */
        if (kb_IsDown(kb_KeySin))    { while (kb_AnyKey()) kb_Scan(); return 5; }  /* E */
        if (kb_IsDown(kb_KeyCos))    { while (kb_AnyKey()) kb_Scan(); return 6; }  /* F */
        if (kb_IsDown(kb_KeyTan))    { while (kb_AnyKey()) kb_Scan(); return 7; }  /* G */
        if (kb_IsDown(kb_KeyPower))  { while (kb_AnyKey()) kb_Scan(); return 8; }  /* H */
        if (kb_IsDown(kb_KeySquare)) { while (kb_AnyKey()) kb_Scan(); return 9; }  /* I */
        if (kb_IsDown(kb_KeyComma))  { while (kb_AnyKey()) kb_Scan(); return 10; } /* J */
        if (kb_IsDown(kb_KeyLParen)) { while (kb_AnyKey()) kb_Scan(); return 11; } /* K */
        if (kb_IsDown(kb_KeyRParen)) { while (kb_AnyKey()) kb_Scan(); return 12; } /* L */
        if (kb_IsDown(kb_KeyDiv))    { while (kb_AnyKey()) kb_Scan(); return 13; } /* M */
        if (kb_IsDown(kb_KeyLog))    { while (kb_AnyKey()) kb_Scan(); return 14; } /* N */
        if (kb_IsDown(kb_Key7))      { while (kb_AnyKey()) kb_Scan(); return 15; } /* O */
        if (kb_IsDown(kb_Key8))      { while (kb_AnyKey()) kb_Scan(); return 16; } /* P */
        if (kb_IsDown(kb_Key9))      { while (kb_AnyKey()) kb_Scan(); return 17; } /* Q */
        if (kb_IsDown(kb_KeyMul))    { while (kb_AnyKey()) kb_Scan(); return 18; } /* R */
        if (kb_IsDown(kb_KeyLn))     { while (kb_AnyKey()) kb_Scan(); return 19; } /* S */
        if (kb_IsDown(kb_Key4))      { while (kb_AnyKey()) kb_Scan(); return 20; } /* T */
        if (kb_IsDown(kb_Key5))      { while (kb_AnyKey()) kb_Scan(); return 21; } /* U */
        if (kb_IsDown(kb_Key6))      { while (kb_AnyKey()) kb_Scan(); return 22; } /* V */
        if (kb_IsDown(kb_KeySub))    { while (kb_AnyKey()) kb_Scan(); return 23; } /* W */
        if (kb_IsDown(kb_KeySto))  { while (kb_AnyKey()) kb_Scan(); return 24; } /* X */
        if (kb_IsDown(kb_Key1))      { while (kb_AnyKey()) kb_Scan(); return 25; } /* Y */
        if (kb_IsDown(kb_Key2))      { while (kb_AnyKey()) kb_Scan(); return 26; } /* Z */
        if (kb_IsDown(kb_Key0))      { while (kb_AnyKey()) kb_Scan(); return ' '; }
    }
}
