#include <tice.h>
#include <graphx.h>
#include <keypadc.h>
#include <string.h>

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

/* forward declarations */
static void game_init(void);
static bool game_loop_tick(void);
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
static void game_pause(void);
static void game_die(void);
static void game_level_done(void);

/* read a pixel from the current draw buffer */
static inline uint8_t read_pixel(int x, int y)
{
    return gfx_GetPixel(x, y);
}

static void game_init(void)
{
    memset(&gs.flags, 0, sizeof(gs.flags));

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

    /* clear behind-sprite buffers */
    memset(gs.behind_spr1, BG_COLOR, sizeof(gs.behind_spr1));
    memset(gs.behind_spr2, BG_COLOR, sizeof(gs.behind_spr2));

    /* init tail buffer */
    for (int i = 0; i <= TAIL_COUNT; i++) {
        gs.tail_buf[i].size = 1;
        gs.tail_buf[i].pos = 0;
    }

    /* check initial spaceship/gravity state based on context toggles already passed */
    if (gs.flags.ship_available) {
        uint8_t toggles_passed = gs.num_ship_total - gs.num_ship_remaining;
        if (toggles_passed & 1)
            spaceship_on();
    }
    if (gs.num_gravity_remaining & 1) {
        gs.flags.gravity_reversed = true;
        gs.char_pos_y -= 2 * LCD_WIDTH; /* adjust for reversed gravity starting pos */
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
    for (int col = 0; col <= WIN_COLS; col++) {
        for (int row = 0; row < WIN_ROWS; row++) {
            uint24_t map_offset = (uint24_t)row * gs.map_size_x + col;
            uint8_t *block_ptr = gs.first_block - gs.bytes_to_skip + map_offset;
            uint8_t tile_id = *block_ptr;
            if (tile_id > 0 && tile_id < NUM_GAME_TILES) {
                int sx = col * TILE_W - gs.disp_blk_frm_x;
                int sy = row * TILE_H;
                draw_tile(gfx_vbuffer, sx, sy, tile_id);
            }
        }
    }
    /* copy to other buffer */
    gfx_BlitBuffer();
}

void game_run(void)
{
    set_game_palette();
    clear_game_screen();
    game_init();
    draw_initial_map();

    /* set up timer for frame sync */
    timer_Control = TIMER2_DISABLE;
    timer_2_Counter = gs.level_speed << 16;
    timer_2_ReloadValue = gs.level_speed << 16;
    timer_Control = TIMER2_ENABLE | TIMER2_32K | TIMER2_DOWN | TIMER2_INT;

    /* spawn animation: draw sprite pixel by pixel */
    const uint8_t *spr = jump_frame(0);
    for (int row = 0; row < SPR_H; row++) {
        for (int col = 0; col < SPR_W; col++) {
            uint8_t px = spr[row * SPR_W + col];
            gfx_SetColor(px);
            gfx_SetPixel(SPR_POS_X + col, gs.char_pos_y / LCD_WIDTH * 0 + (gs.char_pos_y % LCD_HEIGHT) + row); /* simplified */
        }
    }

    /* main game loop */
    while (game_loop_tick())
        ;

    /* disable timer */
    timer_Control = TIMER2_DISABLE;
}

static bool game_loop_tick(void)
{
    /* wait for timer interrupt (frame sync) */
    while (!(timer_IntStatus & TIMER2_INT))
        ;
    timer_IntStatus = TIMER2_INT; /* acknowledge */

    /* scan keyboard */
    kb_Scan();

    /* check quit keys */
    if (kb_IsDown(kb_KeyClear) || kb_IsDown(kb_KeyMode))
        return false;

    /* check pause */
    if (kb_IsDown(kb_KeyEnter)) {
        game_pause();
        return true;
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
        gs.char_pos_y += spd;

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
            gs.char_pos_y += spd;

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
        return false;
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
        return false;
    }

    /* handle jump pads (on ground) */
    if (!gs.flags.spaceship_on)
        handle_jump_pads();

    /* erase previous sprite position */
    erase_character();

    /* draw sprite at new position */
    draw_character();

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

    return true;
}

static void scroll_right(void)
{
    /* shift the visible game area left by SCROLL_SPD pixels */
    uint8_t *buf = gfx_vbuffer;
    for (int y = 0; y < GAME_AREA_H; y++) {
        uint8_t *row = buf + y * LCD_WIDTH;
        memmove(row, row + SCROLL_SPD, LCD_WIDTH - SCROLL_SPD);
    }
}

static void draw_new_column(void)
{
    /* draw the SCROLL_SPD pixel-wide strip on the right edge */
    int draw_x = LCD_WIDTH - SCROLL_SPD;

    for (int row = 0; row < WIN_ROWS + 1; row++) {
        uint8_t *map_ptr = gs.first_block - gs.bytes_to_skip + (uint24_t)row * gs.map_size_x;

        /* figure out which tile(s) to draw */
        int tiles_to_draw = 1;
        int start_col = gs.disp_blk_frm_x;

        if (start_col + SCROLL_SPD > TILE_W)
            tiles_to_draw = 2;

        for (int t = 0; t < tiles_to_draw; t++) {
            uint8_t tile_id = map_ptr[WIN_COLS + t];
            if (tile_id == 0 || tile_id >= NUM_GAME_TILES) {
                /* empty tile - fill with background */
                for (int py = 0; py < TILE_H && row * TILE_H + py < GAME_AREA_H; py++) {
                    uint8_t *dst = gfx_vbuffer + (row * TILE_H + py) * LCD_WIDTH + draw_x;
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
                uint8_t *dst = gfx_vbuffer + (row * TILE_H + py) * LCD_WIDTH + draw_x + dst_x_off;
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

    /* bounds check */
    if (sy < 0 || sy + h > LCD_HEIGHT * 2) return;

    /* save background behind sprite */
    uint8_t *save_buf = (gs.current_spr_buf == 0) ? gs.behind_spr1 : gs.behind_spr2;
    for (int row = 0; row < h; row++) {
        uint8_t *screen_row = gfx_vbuffer + (sy + row) * LCD_WIDTH + SPR_POS_X;
        memcpy(save_buf + row * w, screen_row, w);
    }

    /* draw sprite with transparency */
    draw_sprite_transparent(gfx_vbuffer, SPR_POS_X, sy, spr, w, h);

    gs.current_spr_buf ^= 1;
}

static void erase_character(void)
{
    int sy = gs.prev_pos_y;
    int w = gs.sprite_size;
    int h = gs.sprite_size;

    if (sy < 0 || sy + h > LCD_HEIGHT * 2) return;

    /* restore background from saved buffer */
    uint8_t *save_buf = (gs.current_spr_buf == 0) ? gs.behind_spr1 : gs.behind_spr2;
    for (int row = 0; row < h; row++) {
        uint8_t *screen_row = gfx_vbuffer + (sy + row) * LCD_WIDTH + SPR_POS_X;
        memcpy(screen_row, save_buf + row * w, w);
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

static void game_pause(void)
{
    /* simple pause: wait until Enter is released, then wait for it again */
    while (kb_IsDown(kb_KeyEnter)) kb_Scan();

    while (true) {
        kb_Scan();
        if (kb_IsDown(kb_KeyEnter)) break;
        if (kb_IsDown(kb_KeyClear) || kb_IsDown(kb_KeyMode)) break;
    }

    while (kb_IsDown(kb_KeyEnter) || kb_IsDown(kb_KeyClear)) kb_Scan();
}

static void game_die(void)
{
    /* save progress */
    uint24_t map_col = gs.first_block - gs.beginning_map;
    if (map_col >= gs.bytes_to_skip)
        map_col -= gs.bytes_to_skip;

    if (!gs.flags.in_editor) {
        uint24_t id = level_get_id(ms.current_idx);
        score_update(id, map_col);
    }

    /* brief flash effect */
    gfx_SetColor(0x06); /* white */
    gfx_FillRectangle_NoClip(SPR_POS_X - 5, gs.char_pos_y - 5,
                              gs.sprite_size + 10, gs.sprite_size + 10);
    gfx_SwapDraw();
    delay(200);
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
