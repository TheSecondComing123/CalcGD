#ifndef GFX_H
#define GFX_H

#include "defs.h"

/* initialize 8bpp mode, load palette, load graphics from AppVars */
bool gfx_game_init(void);

/* load menu graphics from AppVar */
bool gfx_menu_init(void);

/* cleanup: restore 16bpp mode */
void gfx_cleanup(void);

/* get pointer to raw tile pixel data (21x21) by tile index */
const uint8_t *tile_data(uint8_t id);

/* get pointer to jump sprite frame (30x30) */
const uint8_t *jump_frame(uint8_t frame);

/* get pointer to spaceship sprite frame (22x22) */
const uint8_t *ship_frame(uint8_t frame);

/* get pointer to font character (28x28), char 0='A' */
const uint8_t *font_char(uint8_t ch);

/* get pointer to menu sprite data at a given offset from menu buffer */
const uint8_t *menu_data(uint24_t offset);

/* draw a tile (21x21) to buffer at screen position */
void draw_tile(uint8_t *buf, int x, int y, uint8_t tile_id);

/* draw a sprite with transparency (skip BG_COLOR pixels) */
void draw_sprite_transparent(uint8_t *buf, int x, int y,
                             const uint8_t *sprite, int w, int h);

/* draw a sprite horizontally mirrored with transparency */
void draw_sprite_transparent_mirror(uint8_t *buf, int x, int y,
                                    const uint8_t *sprite, int w, int h);

/* draw a GD font string into a buffer with clipping */
void draw_gd_text(uint8_t *buf, int x, int y, int buf_w, int buf_h,
                  const uint8_t *str, uint8_t len);

/* center text and draw */
int center_text_x(const uint8_t *str, uint8_t len, int space);

/* draw filled circle at screen address */
void draw_filled_circle(uint8_t *buf, int cx, int cy, int r, uint8_t color);

/* page flip: swap LCD base pointer */
void page_flip(void);

/* set game palette */
void set_game_palette(void);

/* set menu palette */
void set_menu_palette(void);

/* RLE decompression */
void extract_rle(const uint8_t *src, uint8_t *dst, uint24_t num_pairs);

#endif
