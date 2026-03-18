#ifndef DEFS_H
#define DEFS_H

#include <stdint.h>
#include <stdbool.h>
#include <graphx.h>

/* flat pointer to the current draw buffer */
#define GFX_VBUF  ((uint8_t*)&gfx_vbuffer)

/* screen (use toolchain defines if available) */
#ifndef LCD_WIDTH
#define LCD_WIDTH        320
#endif
#ifndef LCD_HEIGHT
#define LCD_HEIGHT       240
#endif

/* tiles */
#define TILE_W           21
#define TILE_H           21
#define NUM_GAME_TILES   56

/* player cube sprite */
#define SPR_W            30
#define SPR_H            30
#define SPR_POS_X        80
#define NUM_JUMP_FRAMES  11

/* spaceship sprite */
#define SHIP_W           22
#define SHIP_H           22
#define NUM_SHIP_FRAMES  13

/* font */
#define FONT_W           28
#define FONT_H           28
#define NUM_FONT_CHARS   26

/* map / window */
#define WIN_ROWS         10
#define WIN_COLS         16
#define SCROLL_SPD       8

/* gameplay */
#define BG_COLOR         0x7F
#define BG_COLOR_MENU    0x07
#define TAIL_COUNT       8
#define TAIL_FIRST_CLR   0xFC
#define MAX_LEVELS       16

/* jump pad color detection (pixel-based collision) */
#define COLOR_YELLOW_LO  0x7A
#define COLOR_YELLOW_HI  0x7E
#define COLOR_RAMP_LO    0xFA

/* safe RAM areas for decompressed graphics */
#define TILES_GAME_BUF   ((uint8_t*)0xD03B56)
#define TILES_MENU_BUF   ((uint8_t*)0xD09466)

/* derived sizes */
#define GAME_AREA_H      (TILE_H * WIN_ROWS)

/* decompressed data offsets */
#define OFF_TILES        0
#define OFF_JUMP         (TILE_W * TILE_H * NUM_GAME_TILES)
#define OFF_SHIP         (OFF_JUMP + SPR_W * SPR_H * NUM_JUMP_FRAMES)
#define OFF_FONT         0
#define OFF_MENU_TILES   (FONT_W * FONT_H * NUM_FONT_CHARS)

/* jump speed lookup table */
#define JUMP_LUT_SIZE    40

/* level AppVar signature */
#define LEVEL_SIG_BYTE   0xFF
#define LEVEL_SIG_STR    "Epharius"
#define LEVEL_SIG_TAG    "GD"

/* direction flags */
typedef struct {
    bool jumping;
    bool jump_again;
    bool gravity_reversed;
    bool spaceship_on;
    bool top_reached;
    bool bot_reached;
    bool change_dir;
    bool already_erased;
    bool pause_left;
    bool no_level;
    bool jump_used;
    bool ship_available;
    bool in_editor;
} game_flags_t;

/* level info stored in menu */
typedef struct {
    uint8_t *data_addr;
    uint8_t *after_name_addr;
    uint8_t *vat_name_addr;
} level_entry_t;

/* tail particle */
typedef struct {
    uint8_t size;
    uint24_t pos;
} tail_element_t;

/* game state */
typedef struct {
    /* map */
    uint8_t *first_block;
    uint8_t *beginning_map;
    uint24_t map_size_x;
    uint24_t bytes_to_skip;
    uint24_t max_bytes_to_skip;
    uint8_t  disp_blk_frm_x;
    uint8_t  disp_blk_frm_y;

    /* player */
    uint24_t char_pos_y;
    uint24_t prev_pos_y;
    int24_t  jmp_speed_idx;
    uint8_t  spr_frame;
    uint8_t  prev_spr_frame;
    uint8_t  sprite_size;

    /* gravity & vehicle context */
    uint8_t *addr_gravity;
    uint8_t  num_gravity_remaining;
    uint8_t *addr_spaceship;
    uint8_t  num_ship_remaining;
    uint8_t  num_ship_total;

    /* scroll sync */
    int24_t  prev_speed;
    int24_t  prev_speed_div320;
    int24_t  mn;

    /* tail effect */
    tail_element_t tail_buf[TAIL_COUNT + 1];
    uint8_t  current_tail_idx;

    /* behind-sprite buffers for erase/restore */
    uint8_t  behind_spr1[SPR_W * SPR_H];
    uint8_t  behind_spr2[SPR_W * SPR_H];
    uint8_t  current_spr_buf; /* 0 or 1 */

    /* double buffer tracking */
    uint8_t *draw_buffer;

    /* flags */
    game_flags_t flags;

    /* level speed (timer value) */
    uint8_t  level_speed;

    /* editor: start offset for play-from-position */
    uint24_t beg_lvl_to_play;
} game_state_t;

/* menu state */
typedef struct {
    level_entry_t levels[MAX_LEVELS];
    uint8_t       num_levels;
    uint8_t       current_idx;
} menu_state_t;

/* global game state */
extern game_state_t gs;
extern menu_state_t ms;

#endif
