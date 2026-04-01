#include <tice.h>
#include <graphx.h>
#include <keypadc.h>
#include <string.h>
#include <stdio.h>

#include "defs.h"
#include "gfx.h"
#include "game.h"
#include "level.h"

/* jump speed lookup table (Y displacement per frame, in screen-row units).
   positive = downward, negative = upward. table index = time in frames. */
static const int16_t jump_lut[] = {
    -10, -9, -8, -7, -6,       /* 0-4: strong upward */
    -5, -5, -4, -4, -3,        /* 5-9 */
    -3, -2, -2, -1, -1,        /* 10-14 */
    -1, 0, 0, 0, 0,            /* 15-19: apex */
    0, 1, 1, 1, 1,             /* 20-24 */
    2, 2, 3, 3, 4,             /* 25-29 */
    4, 5, 5, 6, 7,             /* 30-34 */
    8, 9, 10, 10, 10           /* 35-39: terminal fall */
};

/* reversed gravity jump LUT (mirrored) */
static const int16_t jump_lut_rvrs[] = {
    10, 9, 8, 7, 6,
    5, 5, 4, 4, 3,
    3, 2, 2, 1, 1,
    1, 0, 0, 0, 0,
    0, -1, -1, -1, -1,
    -2, -2, -3, -3, -4,
    -4, -5, -5, -6, -7,
    -8, -9, -10, -10, -10
};

/* the "rest" index: where speed=0 in the LUT */
#define LUT_REST_IDX  17

/* convert LUT speed entry to pixel offset (speed * LCD_WIDTH) */
#define SPD_TO_PX(s)  ((int24_t)(s) * LCD_WIDTH)

/* tick result */
enum tick_result { TICK_CONTINUE, TICK_DIED, TICK_QUIT, TICK_DONE, TICK_PRACTICE_DIED };

/* forward declarations */
static void game_init(void);
static enum tick_result game_loop_tick(void);
static void draw_character(void);
static void erase_character(void);
static void scroll_right(void);
static void draw_new_column(void);
static void check_gravity_context(uint24_t map_col);
static void check_ship_context(uint24_t map_col);
static bool check_collision_cube(void);
static bool check_collision_cube_rvrs(void);
static bool check_collision_ship(void);
static void handle_jump_pads(void);
static void spaceship_on(void);
static void spaceship_off(void);
static void draw_pause_overlay(void);
static void game_pause(void);
static void game_die(void);
static void game_level_done(void);
static void save_checkpoint(void);
static void restore_checkpoint(void);

/* apply speed to player Y position, clamping to prevent unsigned underflow.
   on eZ80, uint24_t + negative int16_t wraps to ~16M instead of going negative,
   which would cause wild VRAM writes. */
static inline void apply_speed_y(int16_t spd)
{
    int24_t new_y = (int24_t)gs.char_pos_y + spd;
    if (new_y < 0)
        new_y = 0;
    else if (new_y > LCD_HEIGHT)
        new_y = LCD_HEIGHT;
    gs.char_pos_y = (uint24_t)new_y;
}

/* read a pixel from the current draw buffer */
static inline uint8_t read_pixel(int x, int y)
{
    if (x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT)
        return BG_COLOR;
    return gfx_GetPixel(x, y);
}

static void game_init(void)
{
    bool ship_available = gs.flags.ship_available;
    bool in_editor = gs.flags.in_editor;
    bool practice_mode = gs.flags.practice_mode;

    memset(&gs.flags, 0, sizeof(gs.flags));
    gs.flags.ship_available = ship_available;
    gs.flags.in_editor = in_editor;
    gs.flags.practice_mode = practice_mode;

    gs.disp_blk_frm_x = 0;
    gs.disp_blk_frm_y = 0;
    gs.prev_speed = 0;
    gs.prev_speed_div320 = 0;
    gs.mn = 0;
    gs.sprite_size = SPR_W;
    gs.spr_frame = 0;
    gs.prev_spr_frame = 0;
    gs.current_spr_buf = 0;
    gs.current_tail_idx = 0;

    /* initial player position: row 7, at SPR_POS_X */
    gs.char_pos_y = TILE_H * 7;
    gs.prev_pos_y = gs.char_pos_y;

    /* start falling */
    gs.flags.jumping = true;
    gs.jmp_speed_idx = LUT_REST_IDX;

    /* set starting map position */
    uint8_t *map = gs.beginning_map;
    gs.first_block = map + gs.bytes_to_skip + gs.beg_lvl_to_play;

    /* reset context pointers/counters to level starts */
    gs.num_gravity_remaining = gs.num_gravity_total;
    gs.addr_gravity = gs.beginning_map + gs.addr_gravity_base_off;

    if (gs.flags.ship_available) {
        gs.num_ship_remaining = gs.num_ship_total;
        gs.addr_spaceship = gs.beginning_map + gs.addr_spaceship_base_off;
    }

    /* clear behind-sprite buffers */
    memset(gs.behind_spr1, BG_COLOR, sizeof(gs.behind_spr1));
    memset(gs.behind_spr2, BG_COLOR, sizeof(gs.behind_spr2));

    /* init tail buffer */
    for (int i = 0; i <= TAIL_COUNT; i++) {
        gs.tail_buf[i].size = 1;
        gs.tail_buf[i].pos = 0;
    }

    /* advance gravity contexts past the start column */
    uint8_t gravity_passed = 0;
    while (gs.num_gravity_remaining > 0) {
        uint24_t trigger = gs.addr_gravity[0] |
            ((uint24_t)gs.addr_gravity[1] << 8) |
            ((uint24_t)gs.addr_gravity[2] << 16);
        if (trigger > gs.beg_lvl_to_play) break;
        gs.addr_gravity += 3;
        gs.num_gravity_remaining--;
        gravity_passed++;
    }
    if (gravity_passed & 1) {
        gs.flags.gravity_reversed = true;
        gs.char_pos_y = TILE_H * 2;
    }

    /* advance ship contexts past the start column */
    if (gs.flags.ship_available) {
        uint8_t ship_passed = 0;
        while (gs.num_ship_remaining > 0) {
            uint24_t trigger = gs.addr_spaceship[0] |
                ((uint24_t)gs.addr_spaceship[1] << 8) |
                ((uint24_t)gs.addr_spaceship[2] << 16);
            if (trigger > gs.beg_lvl_to_play) break;
            gs.addr_spaceship += 3;
            gs.num_ship_remaining--;
            ship_passed++;
        }
        if (ship_passed & 1)
            spaceship_on();
    }
}

static void clear_game_screen(void)
{
    /* clear both buffers */
    for (int buf = 0; buf < 2; buf++) {
        gfx_SetDrawBuffer();

        /* fill game area with background color */
        gfx_SetColor(BG_COLOR);
        gfx_FillRectangle_NoClip(0, 0, LCD_WIDTH, GAME_AREA_H);

        /* draw floor: white line then solid floor */
        gfx_SetColor(0x06); /* white */
        gfx_HorizLine_NoClip(0, GAME_AREA_H, LCD_WIDTH);
        gfx_SetColor(0x7E); /* floor color */
        gfx_FillRectangle_NoClip(0, GAME_AREA_H + 1, LCD_WIDTH, LCD_HEIGHT - GAME_AREA_H - 1);

        if (buf == 0) gfx_SwapDraw();
    }
}

/* draw the initial visible portion of the map */
static void draw_initial_map(void)
{
    uint24_t base_col = gs.first_block - gs.beginning_map;
    if (base_col >= gs.bytes_to_skip)
        base_col -= gs.bytes_to_skip;

    for (int col = 0; col <= WIN_COLS; col++) {
        uint24_t abs_col = base_col + col;
        if (abs_col >= gs.map_size_x)
            continue;

        for (int row = 0; row < WIN_ROWS; row++) {
            uint24_t map_offset = (uint24_t)row * gs.map_size_x + abs_col;
            uint8_t *block_ptr = gs.beginning_map + map_offset;
            uint8_t tile_id = *block_ptr;
            if (tile_id > 0 && tile_id < NUM_GAME_TILES) {
                int sx = col * TILE_W - gs.disp_blk_frm_x;
                int sy = row * TILE_H;
                draw_tile(GFX_VBUF, sx, sy, tile_id);
            }
        }
    }
    /* copy to other buffer */
    gfx_BlitBuffer();
}

void game_run(void)
{
    gs.attempts = 0;
    gs.checkpoint.valid = false;

    while (true) {
        if (!level_load(ms.current_idx))
            break;

        gs.attempts++;

        set_game_palette();
        clear_game_screen();
        game_init();
        draw_initial_map();

        /* set up timer for frame sync */
        timer_Control = TIMER2_DISABLE;
        timer_2_Counter = (uint24_t)gs.level_speed << 16;
        timer_2_ReloadValue = (uint24_t)gs.level_speed << 16;
        timer_Control = TIMER2_ENABLE | TIMER2_32K | TIMER2_DOWN | TIMER2_0INT;

        /* main game loop */
        enum tick_result result;
        do {
            result = game_loop_tick();

            /* practice mode: respawn at checkpoint on death */
            if (result == TICK_PRACTICE_DIED) {
                gs.attempts++;
                restore_checkpoint();
                clear_game_screen();
                draw_initial_map();
                result = TICK_CONTINUE;
            }
        } while (result == TICK_CONTINUE);

        /* disable timer */
        timer_Control = TIMER2_DISABLE;

        if (result == TICK_QUIT || result == TICK_DONE)
            break;

        /* TICK_DIED: show death screen with attempt count, then retry */
        /* in practice mode without a checkpoint, also restart from beginning */
    }

    level_unload();
}

static enum tick_result game_loop_tick(void)
{
    /* wait for timer interrupt (frame sync) */
    while (!timer_ChkInterrupt(2, TIMER2_RELOADED))
        ;
    timer_AckInterrupt(2, TIMER2_RELOADED);

    /* scan keyboard */
    kb_Scan();

    /* check quit keys */
    if (kb_IsDown(kb_KeyClear) || kb_IsDown(kb_KeyMode))
        return TICK_QUIT;

    /* practice mode: place checkpoint with Alpha key */
    if (gs.flags.practice_mode && kb_IsDown(kb_KeyAlpha)) {
        save_checkpoint();
        /* brief green flash at top-right to confirm */
        gfx_SetColor(0x05); /* green */
        gfx_FillRectangle_NoClip(LCD_WIDTH - 12, 2, 10, 10);
    }

    /* check pause */
    if (kb_IsDown(kb_KeyEnter)) {
        game_pause();
        return TICK_CONTINUE;
    }

    /* check jump/action */
    bool action = kb_IsDown(kb_Key2nd);

    if (gs.flags.spaceship_on) {
        /* spaceship mode */
        if (action) {
            /* fly up */
            if (gs.flags.gravity_reversed) {
                if (gs.jmp_speed_idx < JUMP_LUT_SIZE - 1)
                    gs.jmp_speed_idx++;
            } else {
                if (gs.jmp_speed_idx > 0)
                    gs.jmp_speed_idx--;
            }
        } else {
            /* fall */
            if (gs.flags.gravity_reversed) {
                if (gs.jmp_speed_idx > 0)
                    gs.jmp_speed_idx--;
            } else {
                if (gs.jmp_speed_idx < JUMP_LUT_SIZE - 1)
                    gs.jmp_speed_idx++;
            }
        }

        /* apply speed */
        int16_t spd = gs.flags.gravity_reversed ?
            jump_lut_rvrs[gs.jmp_speed_idx] : jump_lut[gs.jmp_speed_idx];
        apply_speed_y(spd);

    } else {
        /* cube mode */
        if (action) {
            if (!gs.flags.jumping) {
                /* start jump */
                gs.flags.jumping = true;
                gs.flags.jump_again = false;
                gs.flags.jump_used = true;
                gs.jmp_speed_idx = 0;
                if (gs.flags.gravity_reversed)
                    gs.jmp_speed_idx = JUMP_LUT_SIZE - 1;
            } else {
                /* plan another jump for when we land */
                int16_t spd = gs.flags.gravity_reversed ?
                    jump_lut_rvrs[gs.jmp_speed_idx] : jump_lut[gs.jmp_speed_idx];
                if (!gs.flags.gravity_reversed && spd > 0)
                    gs.flags.jump_again = true;
                else if (gs.flags.gravity_reversed && spd < 0)
                    gs.flags.jump_again = true;
            }
        } else {
            gs.flags.jump_used = false;
        }

        if (gs.flags.jumping) {
            /* advance jump LUT index */
            if (gs.flags.gravity_reversed) {
                if (gs.jmp_speed_idx > 0) gs.jmp_speed_idx--;
            } else {
                if (gs.jmp_speed_idx < JUMP_LUT_SIZE - 1) gs.jmp_speed_idx++;
            }

            /* apply speed */
            int16_t spd = gs.flags.gravity_reversed ?
                jump_lut_rvrs[gs.jmp_speed_idx] : jump_lut[gs.jmp_speed_idx];
            apply_speed_y(spd);

            /* rotate sprite */
            gs.spr_frame++;
            if (gs.spr_frame >= NUM_JUMP_FRAMES)
                gs.spr_frame = 0;
        }
    }

    /* scroll map */
    gs.disp_blk_frm_x += SCROLL_SPD;
    if (gs.disp_blk_frm_x >= TILE_W) {
        gs.disp_blk_frm_x -= TILE_W;
        gs.first_block++;
    }

    /* compute current map column */
    uint24_t map_col = gs.first_block - gs.beginning_map;
    if (map_col >= gs.bytes_to_skip)
        map_col -= gs.bytes_to_skip;

    /* check gravity/spaceship context changes */
    check_gravity_context(map_col);
    check_ship_context(map_col);

    /* check level completion */
    if (map_col >= gs.map_size_x - 1) {
        game_level_done();
        return TICK_DONE;
    }

    /* scroll the draw buffer left */
    scroll_right();

    /* draw new tiles on right edge */
    draw_new_column();

    /* collision detection */
    bool died = false;
    if (gs.flags.spaceship_on) {
        died = check_collision_ship();
    } else if (gs.flags.gravity_reversed) {
        died = check_collision_cube_rvrs();
    } else {
        died = check_collision_cube();
    }

    if (died) {
        game_die();
        if (gs.flags.practice_mode && gs.checkpoint.valid)
            return TICK_PRACTICE_DIED;
        return TICK_DIED;
    }

    /* handle jump pads (on ground) */
    if (!gs.flags.spaceship_on)
        handle_jump_pads();

    /* erase previous sprite position */
    erase_character();

    /* draw sprite at new position */
    draw_character();

    /* practice mode indicator: small "P" in top-right corner */
    if (gs.flags.practice_mode) {
        gfx_SetTextFGColor(0x05); /* green */
        gfx_SetTextBGColor(BG_COLOR);
        gfx_PrintStringXY("P", LCD_WIDTH - 10, 2);
    }

    /* swap display buffers */
    page_flip();

    /* save previous state */
    gs.prev_pos_y = gs.char_pos_y;
    gs.prev_spr_frame = gs.spr_frame;

    /* handle queued jump */
    if (gs.flags.jump_again && !gs.flags.jumping) {
        gs.flags.jumping = true;
        gs.flags.jump_again = false;
        gs.jmp_speed_idx = 0;
        if (gs.flags.gravity_reversed)
            gs.jmp_speed_idx = JUMP_LUT_SIZE - 1;
    }

    return TICK_CONTINUE;
}

static void scroll_right(void)
{
    /* shift the visible game area left by SCROLL_SPD pixels */
    uint8_t *buf = GFX_VBUF;
    for (int y = 0; y < GAME_AREA_H; y++) {
        uint8_t *row = buf + y * LCD_WIDTH;
        memmove(row, row + SCROLL_SPD, LCD_WIDTH - SCROLL_SPD);
    }
}

static void draw_new_column(void)
{
    /* draw the SCROLL_SPD pixel-wide strip on the right edge */
    int draw_x = LCD_WIDTH - SCROLL_SPD;

    for (int row = 0; row < WIN_ROWS; row++) {
        uint8_t *map_ptr = gs.first_block - gs.bytes_to_skip + (uint24_t)row * gs.map_size_x;

        /* figure out which tile(s) to draw */
        int tiles_to_draw = 1;
        int start_col = gs.disp_blk_frm_x;

        if (start_col + SCROLL_SPD > TILE_W)
            tiles_to_draw = 2;

        for (int t = 0; t < tiles_to_draw; t++) {
            /* bounds check: don't read past end of map row */
            uint24_t abs_col = (gs.first_block - gs.beginning_map -
                                gs.bytes_to_skip) + WIN_COLS + t;
            if (abs_col >= gs.map_size_x) {
                /* past map end: fill with background */
                for (int py = 0; py < TILE_H && row * TILE_H + py < GAME_AREA_H; py++) {
                    uint8_t *dst = GFX_VBUF + (row * TILE_H + py) * LCD_WIDTH + draw_x;
                    memset(dst, BG_COLOR, SCROLL_SPD);
                }
                continue;
            }
            uint8_t tile_id = map_ptr[WIN_COLS + t];
            if (tile_id == 0 || tile_id >= NUM_GAME_TILES) {
                /* empty tile - fill with background */
                for (int py = 0; py < TILE_H && row * TILE_H + py < GAME_AREA_H; py++) {
                    uint8_t *dst = GFX_VBUF + (row * TILE_H + py) * LCD_WIDTH + draw_x;
                    int w = SCROLL_SPD;
                    if (t == 1) w = start_col + SCROLL_SPD - TILE_W;
                    if (t == 0 && tiles_to_draw == 2) w = TILE_W - start_col;
                    if (w > 0 && w <= SCROLL_SPD)
                        memset(dst + (t == 0 ? 0 : TILE_W - start_col), BG_COLOR, w);
                }
                continue;
            }

            const uint8_t *td = tile_data(tile_id);
            int src_x_start, copy_w;
            int dst_x_off;

            if (tiles_to_draw == 1) {
                src_x_start = start_col;
                copy_w = SCROLL_SPD;
                dst_x_off = 0;
            } else if (t == 0) {
                src_x_start = start_col;
                copy_w = TILE_W - start_col;
                dst_x_off = 0;
            } else {
                src_x_start = 0;
                copy_w = start_col + SCROLL_SPD - TILE_W;
                dst_x_off = TILE_W - start_col;
            }

            if (copy_w <= 0) continue;

            for (int py = 0; py < TILE_H && row * TILE_H + py < GAME_AREA_H; py++) {
                const uint8_t *src_row = td + py * TILE_W + src_x_start;
                uint8_t *dst = GFX_VBUF + (row * TILE_H + py) * LCD_WIDTH + draw_x + dst_x_off;
                for (int px = 0; px < copy_w; px++) {
                    uint8_t c = src_row[px];
                    if (c != BG_COLOR)
                        dst[px] = c;
                    else
                        dst[px] = BG_COLOR;
                }
            }
        }
    }
}

static void draw_character(void)
{
    const uint8_t *spr;
    int w, h;

    if (gs.flags.spaceship_on) {
        spr = ship_frame(gs.spr_frame % NUM_SHIP_FRAMES);
        w = SHIP_W;
        h = SHIP_H;
    } else {
        spr = jump_frame(gs.spr_frame);
        w = SPR_W;
        h = SPR_H;
    }

    int sy = gs.char_pos_y;

    /* always save/restore the full SPR_W x SPR_H area to avoid stride
       mismatch when switching between cube (30x30) and ship (22x22) */
    uint8_t *save_buf = (gs.current_spr_buf == 0) ? gs.behind_spr1 : gs.behind_spr2;

    if (sy < 0 || sy + SPR_H > LCD_HEIGHT) {
        /* offscreen: fill save buffer with BG so erase stays consistent */
        memset(save_buf, BG_COLOR, SPR_W * SPR_H);
        gs.current_spr_buf ^= 1;
        return;
    }

    /* save background behind sprite (always full 30x30) */
    for (int row = 0; row < SPR_H; row++) {
        uint8_t *screen_row = GFX_VBUF + (sy + row) * LCD_WIDTH + SPR_POS_X;
        memcpy(save_buf + row * SPR_W, screen_row, SPR_W);
    }

    /* draw sprite with transparency */
    draw_sprite_transparent(GFX_VBUF, SPR_POS_X, sy, spr, w, h);

    gs.current_spr_buf ^= 1;
}

static void erase_character(void)
{
    int sy = gs.prev_pos_y;

    if (sy < 0 || sy + SPR_H > LCD_HEIGHT) return;

    /* restore background (always full 30x30, matching draw_character) */
    uint8_t *save_buf = (gs.current_spr_buf == 0) ? gs.behind_spr1 : gs.behind_spr2;
    for (int row = 0; row < SPR_H; row++) {
        uint8_t *screen_row = GFX_VBUF + (sy + row) * LCD_WIDTH + SPR_POS_X;
        memcpy(screen_row, save_buf + row * SPR_W, SPR_W);
    }
}

static void check_gravity_context(uint24_t map_col)
{
    if (gs.num_gravity_remaining == 0) return;

    uint24_t trigger_col = gs.addr_gravity[0] |
        ((uint24_t)gs.addr_gravity[1] << 8) |
        ((uint24_t)gs.addr_gravity[2] << 16);

    if (map_col >= trigger_col) {
        gs.addr_gravity += 3;
        gs.num_gravity_remaining--;
        gs.flags.gravity_reversed = !gs.flags.gravity_reversed;
    }
}

static void check_ship_context(uint24_t map_col)
{
    if (!gs.flags.ship_available || gs.num_ship_remaining == 0) return;

    uint24_t trigger_col = gs.addr_spaceship[0] |
        ((uint24_t)gs.addr_spaceship[1] << 8) |
        ((uint24_t)gs.addr_spaceship[2] << 16);

    if (map_col >= trigger_col) {
        gs.addr_spaceship += 3;
        gs.num_ship_remaining--;
        if (gs.flags.spaceship_on)
            spaceship_off();
        else
            spaceship_on();
    }
}

/* pixel-based collision for normal cube mode */
static bool check_collision_cube(void)
{
    int px = SPR_POS_X;
    int py = gs.char_pos_y;

    /* out of bounds = death */
    if (py < 0 || py + SPR_H > GAME_AREA_H + TILE_H)
        return true;

    /* top-left corner */
    if (read_pixel(px + 3, py + 5) != BG_COLOR)
        return true;

    /* right side (center) */
    uint8_t r = read_pixel(px + SPR_W - 3, py + SPR_H / 2);
    if (r != BG_COLOR && (r >= 0x80 || r < COLOR_YELLOW_LO))
        return true;

    /* bottom-right */
    r = read_pixel(px + SPR_W - 5, py + SPR_H - 3);
    if (r >= 0x80 && r < COLOR_RAMP_LO)
        return true;

    /* bottom-left */
    r = read_pixel(px + 5, py + SPR_H - 3);
    if (r >= 0x80 && r < COLOR_RAMP_LO)
        return true;

    /* check if standing on ground */
    uint8_t below = read_pixel(px + SPR_W / 2, py + SPR_H + 1);
    if (below != BG_COLOR && gs.flags.jumping) {
        /* we've landed */
        if (below < 0x80 || below >= COLOR_RAMP_LO) {
            gs.flags.jumping = false;
            gs.flags.bot_reached = true;
            /* snap to tile boundary */
            gs.char_pos_y = ((py + SPR_H) / TILE_H) * TILE_H - SPR_H;
            gs.jmp_speed_idx = LUT_REST_IDX;
            gs.spr_frame = 0;
        }
    } else if (below == BG_COLOR && !gs.flags.jumping) {
        /* fell off edge */
        gs.flags.jumping = true;
        gs.jmp_speed_idx = LUT_REST_IDX + 1;
    }

    return false;
}

/* pixel-based collision for reversed gravity cube mode */
static bool check_collision_cube_rvrs(void)
{
    int px = SPR_POS_X;
    int py = gs.char_pos_y;

    if (py < -TILE_H || py + SPR_H > GAME_AREA_H + TILE_H)
        return true;

    /* bottom-left (now at top visually due to reversed gravity) */
    if (read_pixel(px + 3, py + SPR_H - 5) != BG_COLOR)
        return true;

    /* right side */
    uint8_t r = read_pixel(px + SPR_W - 3, py + SPR_H / 2);
    if (r != BG_COLOR && (r >= 0x80 || r < COLOR_YELLOW_LO))
        return true;

    /* top-right */
    r = read_pixel(px + SPR_W - 5, py + 3);
    if (r >= 0x80 && r < COLOR_RAMP_LO)
        return true;

    /* top-left */
    r = read_pixel(px + 5, py + 3);
    if (r >= 0x80 && r < COLOR_RAMP_LO)
        return true;

    /* check ceiling (ground in reversed mode) */
    uint8_t above = read_pixel(px + SPR_W / 2, py - 1);
    if (above != BG_COLOR && gs.flags.jumping) {
        if (above < 0x80 || above >= COLOR_RAMP_LO) {
            gs.flags.jumping = false;
            gs.flags.top_reached = true;
            gs.char_pos_y = (py / TILE_H + 1) * TILE_H;
            gs.jmp_speed_idx = LUT_REST_IDX;
            gs.spr_frame = 0;
        }
    } else if (above == BG_COLOR && !gs.flags.jumping) {
        gs.flags.jumping = true;
        gs.jmp_speed_idx = LUT_REST_IDX - 1;
    }

    return false;
}

/* pixel-based collision for spaceship mode */
static bool check_collision_ship(void)
{
    int px = SPR_POS_X;
    int py = gs.char_pos_y;
    int w = SHIP_W;
    int h = SHIP_H;

    if (py < 0 || py + h > GAME_AREA_H + TILE_H)
        return true;

    /* top edge */
    if (read_pixel(px + w / 2, py + 1) != BG_COLOR)
        return true;

    /* bottom edge */
    if (read_pixel(px + w / 2, py + h - 1) != BG_COLOR)
        return true;

    /* right edge */
    uint8_t r = read_pixel(px + w - 2, py + h / 2);
    if (r != BG_COLOR && r < COLOR_YELLOW_LO)
        return true;

    /* left edge */
    r = read_pixel(px + 2, py + h / 2);
    if (r != BG_COLOR && r < COLOR_YELLOW_LO)
        return true;

    return false;
}

static void handle_jump_pads(void)
{
    int px = SPR_POS_X;
    int py = gs.char_pos_y;

    /* check for yellow jump pads below */
    for (int dx = 5; dx < SPR_W - 5; dx += 10) {
        uint8_t c = read_pixel(px + dx, py + SPR_H + 2);
        if (c >= COLOR_RAMP_LO) {
            /* auto-bounce (ramp/pad) */
            gs.flags.jumping = true;
            gs.flags.jump_used = false;
            gs.jmp_speed_idx = 0;
            if (gs.flags.gravity_reversed)
                gs.jmp_speed_idx = JUMP_LUT_SIZE - 1;
            return;
        }
        if (c >= COLOR_YELLOW_LO && c < COLOR_YELLOW_HI) {
            /* yellow orb - bounce only if pressing 2nd */
            if (kb_IsDown(kb_Key2nd) && !gs.flags.jump_used) {
                gs.flags.jumping = true;
                gs.flags.jump_used = true;
                gs.flags.jump_again = false;
                gs.jmp_speed_idx = 0;
                if (gs.flags.gravity_reversed)
                    gs.jmp_speed_idx = JUMP_LUT_SIZE - 1;
                return;
            }
        }
    }
}

static void spaceship_on(void)
{
    gs.flags.spaceship_on = true;
    gs.sprite_size = SHIP_W;
    gs.spr_frame = 6; /* middle frame */
    gs.flags.jumping = true;
}

static void spaceship_off(void)
{
    gs.flags.spaceship_on = false;
    gs.sprite_size = SPR_W;
    gs.spr_frame = 0;
    gs.flags.jumping = true;
    gs.jmp_speed_idx = LUT_REST_IDX;
}

static void draw_pause_overlay(void)
{
    const char *prac_msg = gs.flags.practice_mode ?
        "[Alpha] Practice: ON " : "[Alpha] Practice: OFF";

    gfx_SetColor(0x09); /* black panel */
    gfx_FillRectangle_NoClip(32, 62, LCD_WIDTH - 64, 118);
    gfx_SetColor(0x06); /* white border */
    gfx_Rectangle_NoClip(32, 62, LCD_WIDTH - 64, 118);

    gfx_SetTextFGColor(0x06); /* white */
    gfx_SetTextBGColor(0x09); /* black */
    gfx_SetTextScale(2, 2);
    gfx_PrintStringXY("PAUSED", (LCD_WIDTH - gfx_GetStringWidth("PAUSED")) / 2, 80);
    gfx_SetTextScale(1, 1);
    gfx_PrintStringXY(prac_msg, (LCD_WIDTH - gfx_GetStringWidth(prac_msg)) / 2, 120);
    gfx_PrintStringXY("[Enter] Resume", (LCD_WIDTH - gfx_GetStringWidth("[Enter] Resume")) / 2, 140);
}

static void game_pause(void)
{
    while (kb_IsDown(kb_KeyEnter)) kb_Scan();

    /* keep gameplay pixels in the draw buffer and show overlay on screen only */
    gfx_BlitScreen();
    gfx_SetDrawScreen();
    draw_pause_overlay();

    while (true) {
        kb_Scan();
        if (kb_IsDown(kb_KeyEnter)) break;
        if (kb_IsDown(kb_KeyClear) || kb_IsDown(kb_KeyMode)) break;
        if (kb_IsDown(kb_KeyAlpha)) {
            gs.flags.practice_mode = !gs.flags.practice_mode;
            if (!gs.flags.practice_mode)
                gs.checkpoint.valid = false;
            while (kb_IsDown(kb_KeyAlpha)) kb_Scan();
            draw_pause_overlay();
        }
    }

    while (kb_AnyKey()) kb_Scan();

    /* restore preserved gameplay frame and resume drawing to the back buffer */
    gfx_BlitBuffer();
    gfx_SetDrawBuffer();
}

#define DEATH_PARTICLES 16
#define DEATH_FRAMES    12

typedef struct {
    int16_t x, y;
    int8_t  vx, vy;
    uint8_t size;
    uint8_t color;
} particle_t;

static void save_checkpoint(void)
{
    checkpoint_t *cp = &gs.checkpoint;
    cp->first_block_off = gs.first_block - gs.beginning_map;
    cp->char_pos_y = gs.char_pos_y;
    cp->bytes_to_skip = gs.bytes_to_skip;
    cp->disp_blk_frm_x = gs.disp_blk_frm_x;
    cp->disp_blk_frm_y = gs.disp_blk_frm_y;
    cp->jmp_speed_idx = gs.jmp_speed_idx;
    cp->spr_frame = gs.spr_frame;
    cp->gravity_reversed = gs.flags.gravity_reversed;
    cp->spaceship_on = gs.flags.spaceship_on;
    cp->num_gravity_remaining = gs.num_gravity_remaining;
    cp->addr_gravity_off = gs.addr_gravity - gs.beginning_map;
    cp->num_ship_remaining = gs.num_ship_remaining;
    cp->addr_spaceship_off = gs.addr_spaceship ? (gs.addr_spaceship - gs.beginning_map) : 0;
    cp->valid = true;
}

static void restore_checkpoint(void)
{
    checkpoint_t *cp = &gs.checkpoint;
    if (!cp->valid) return;

    gs.first_block = gs.beginning_map + cp->first_block_off;
    gs.char_pos_y = cp->char_pos_y;
    gs.prev_pos_y = cp->char_pos_y;
    gs.bytes_to_skip = cp->bytes_to_skip;
    gs.disp_blk_frm_x = cp->disp_blk_frm_x;
    gs.disp_blk_frm_y = cp->disp_blk_frm_y;
    gs.jmp_speed_idx = cp->jmp_speed_idx;
    gs.spr_frame = cp->spr_frame;
    gs.prev_spr_frame = cp->spr_frame;
    gs.flags.gravity_reversed = cp->gravity_reversed;
    gs.flags.spaceship_on = cp->spaceship_on;
    gs.num_gravity_remaining = cp->num_gravity_remaining;
    gs.addr_gravity = gs.beginning_map + cp->addr_gravity_off;
    gs.num_ship_remaining = cp->num_ship_remaining;
    gs.addr_spaceship = cp->addr_spaceship_off ? (gs.beginning_map + cp->addr_spaceship_off) : NULL;

    gs.sprite_size = cp->spaceship_on ? SHIP_W : SPR_W;
    gs.flags.jumping = true;
    gs.flags.jump_again = false;
    gs.flags.top_reached = false;
    gs.flags.bot_reached = false;
    gs.flags.already_erased = false;
    gs.flags.jump_used = false;
    gs.prev_speed = 0;
    gs.prev_speed_div320 = 0;
    gs.mn = 0;
    gs.current_spr_buf = 0;

    memset(gs.behind_spr1, BG_COLOR, sizeof(gs.behind_spr1));
    memset(gs.behind_spr2, BG_COLOR, sizeof(gs.behind_spr2));
}

static void game_die(void)
{
    /* save progress */
    uint24_t map_col = gs.first_block - gs.beginning_map;
    if (map_col >= gs.bytes_to_skip)
        map_col -= gs.bytes_to_skip;

    if (!gs.flags.in_editor && !gs.flags.practice_mode) {
        uint24_t id = level_get_id(ms.current_idx);
        score_update(id, map_col);
    }

    /* particle explosion from player center */
    int cx = SPR_POS_X + gs.sprite_size / 2;
    int cy = gs.char_pos_y + gs.sprite_size / 2;

    particle_t parts[DEATH_PARTICLES];
    for (int i = 0; i < DEATH_PARTICLES; i++) {
        parts[i].x = cx;
        parts[i].y = cy;
        /* spread velocities in a ring pattern */
        int angle_idx = i * 8 / DEATH_PARTICLES;
        static const int8_t dx[] = { 4, 3, 0, -3, -4, -3, 0, 3 };
        static const int8_t dy[] = { 0, -3, -4, -3, 0, 3, 4, 3 };
        parts[i].vx = dx[angle_idx] + (i & 1 ? 1 : -1);
        parts[i].vy = dy[angle_idx] + (i & 1 ? -1 : 1);
        parts[i].size = 3 + (i % 3);
        parts[i].color = (i & 1) ? 0x06 : 0x09; /* white / black */
    }

    for (int frame = 0; frame < DEATH_FRAMES; frame++) {
        /* erase player sprite area (clipped: player may die near screen edge) */
        gfx_SetColor(BG_COLOR);
        gfx_FillRectangle(SPR_POS_X - 2, (int)gs.char_pos_y - 2,
                          gs.sprite_size + 4, gs.sprite_size + 4);

        /* draw particles */
        for (int i = 0; i < DEATH_PARTICLES; i++) {
            parts[i].x += parts[i].vx;
            parts[i].y += parts[i].vy;
            parts[i].vy += 1; /* gravity */

            int sz = parts[i].size - frame / 4;
            if (sz < 1) sz = 1;

            int px = parts[i].x - sz / 2;
            int py = parts[i].y - sz / 2;
            if (px >= 0 && px + sz < LCD_WIDTH && py >= 0 && py + sz < LCD_HEIGHT) {
                gfx_SetColor(parts[i].color);
                gfx_FillRectangle_NoClip(px, py, sz, sz);
            }
        }

        gfx_SwapDraw();
        delay(40);
    }

    /* show attempt count */
    char buf[20];
    snprintf(buf, sizeof(buf), "Attempt %lu", (unsigned long)gs.attempts);
    gfx_SetTextFGColor(0x06); /* white */
    gfx_SetTextBGColor(BG_COLOR);
    gfx_SetTextScale(2, 2);
    int tw = gfx_GetStringWidth(buf);
    gfx_PrintStringXY(buf, (LCD_WIDTH - tw) / 2, LCD_HEIGHT / 2 - 8);
    gfx_SwapDraw();
    gfx_PrintStringXY(buf, (LCD_WIDTH - tw) / 2, LCD_HEIGHT / 2 - 8);
    gfx_SetTextScale(1, 1);
    delay(800);
}

static void game_level_done(void)
{
    if (!gs.flags.in_editor) {
        uint24_t id = level_get_id(ms.current_idx);
        score_update(id, gs.map_size_x);
    }

    /* level complete animation: lines converging */
    for (int i = 0; i < 60; i++) {
        gfx_SetColor(0x06); /* white */
        int x1 = i * 3;
        int x2 = LCD_WIDTH - i * 3;
        if (x1 < LCD_WIDTH)
            gfx_VertLine_NoClip(x1, 0, LCD_HEIGHT);
        if (x2 >= 0 && x2 < LCD_WIDTH)
            gfx_VertLine_NoClip(x2, 0, LCD_HEIGHT);
        gfx_SwapDraw();
        delay(30);
    }

    delay(500);
}
