#ifndef LEVEL_H
#define LEVEL_H

#include "defs.h"

/* scan calculator VAT for level AppVars (name starts with 'L', contains signature) */
void level_scan(void);

/* load level data pointers from the currently selected level entry.
   populates gs map fields. returns false if level data is invalid. */
bool level_load(uint8_t level_idx);

/* close the currently loaded level AppVar slot, if any */
void level_unload(void);

/* find high score for a given level ID in the GeomDash AppVar.
   returns the high score value, or 0 if not found. */
uint24_t score_find(uint24_t level_id);

/* add/update high score for a level. progress is in map columns. */
void score_update(uint24_t level_id, uint24_t progress);

/* get level name pointers (up to two lines). returns line count (1 or 2). */
uint8_t level_get_name(uint8_t level_idx, const uint8_t **line1, uint8_t *len1,
                       const uint8_t **line2, uint8_t *len2);

/* get level difficulty (0-3) */
uint8_t level_get_difficulty(uint8_t level_idx);

/* get level hash ID */
uint24_t level_get_id(uint8_t level_idx);

/* create a new level AppVar */
bool level_create(const uint8_t *name_buf, uint8_t difficulty);

#endif
