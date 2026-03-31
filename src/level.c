#include <tice.h>
#include <fileioc.h>
#include <string.h>
#include <stdlib.h>

#include "defs.h"
#include "level.h"

#define SCORE_APPVAR  "GeomDash"
#define DATA_LEVEL_SIG "\xFF" "Epharius" "\xFF" "GD"
#define DATA_LEVEL_SIG_LEN 12

void level_scan(void)
{
    void *search_pos = NULL;
    char *name;
    ms.num_levels = 0;

    while ((name = ti_DetectVar(&search_pos, DATA_LEVEL_SIG, OS_TYPE_APPVAR)) != NULL) {
        if (ms.num_levels >= MAX_LEVELS) break;

        ti_var_t slot = ti_Open(name, "r");
        if (!slot) continue;

        /* verify signature at start of data */
        uint8_t sig_buf[DATA_LEVEL_SIG_LEN];
        ti_Read(sig_buf, DATA_LEVEL_SIG_LEN, 1, slot);
        if (memcmp(sig_buf, DATA_LEVEL_SIG, DATA_LEVEL_SIG_LEN) != 0) {
            ti_Close(slot);
            continue;
        }

        /* read name: first byte is length of first line */
        uint8_t *data_ptr = ti_GetDataPtr(slot);
        level_entry_t *le = &ms.levels[ms.num_levels];
        le->data_addr = data_ptr;
        /* after_name = past the signature + name data */
        uint8_t name_len1 = data_ptr[DATA_LEVEL_SIG_LEN];
        uint8_t *after = data_ptr + DATA_LEVEL_SIG_LEN + 1 + name_len1;
        uint8_t name_len2 = *after;
        if (name_len2 > 0)
            after += 1 + name_len2;
        else
            after += 1;
        le->after_name_addr = after;

        /* store pointer to name in VAT for editor access */
        le->vat_name_addr = (uint8_t *)name;

        ms.num_levels++;
        ti_Close(slot);
    }
}

bool level_load(uint8_t level_idx)
{
    if (level_idx >= ms.num_levels) return false;

    level_entry_t *le = &ms.levels[level_idx];
    uint8_t *p = le->after_name_addr;

    /* difficulty (1 byte) */
    p++; /* skip difficulty */

    /* id (3 bytes) */
    p += 3;

    /* speed (1 byte) */
    gs.level_speed = *p++;

    /* map_size_x (3 bytes) */
    gs.map_size_x = p[0] | ((uint24_t)p[1] << 8) | ((uint24_t)p[2] << 16);
    p += 3;
    if (gs.map_size_x == 0) return false;

    /* extra rows above 10 (1 byte) - currently always 0 */
    uint8_t extra_rows = *p++;

    /* beginning of map data */
    gs.beginning_map = p;
    gs.first_block = NULL; /* set during game init */

    /* compute bytes_to_skip from extra rows */
    gs.bytes_to_skip = (uint24_t)extra_rows * gs.map_size_x;
    gs.max_bytes_to_skip = gs.bytes_to_skip;

    /* find gravity and spaceship context data (after tile data) */
    uint24_t tile_count = (uint24_t)(WIN_ROWS + extra_rows) * gs.map_size_x;
    uint8_t *context_ptr = p + tile_count;

    /* gravity contexts */
    gs.num_gravity_remaining = *context_ptr++;
    gs.addr_gravity = context_ptr;
    /* each context is 3 bytes (position) */
    context_ptr += gs.num_gravity_remaining * 3;

    /* spaceship contexts (only if level supports it) */
    gs.flags.ship_available = false;
    if (gs.level_speed != 0) {
        gs.num_ship_remaining = *context_ptr++;
        gs.num_ship_total = gs.num_ship_remaining;
        gs.addr_spaceship = context_ptr;
        gs.flags.ship_available = true;
    } else {
        gs.num_ship_remaining = 0;
        gs.num_ship_total = 0;
        gs.addr_spaceship = NULL;
        /* old version without speed field: default speed */
        gs.level_speed = 0x0E;
    }

    return true;
}

uint8_t level_get_name(uint8_t level_idx, const uint8_t **line1, uint8_t *len1,
                       const uint8_t **line2, uint8_t *len2)
{
    if (level_idx >= ms.num_levels) {
        *len1 = 0; *len2 = 0;
        return 0;
    }

    uint8_t *p = ms.levels[level_idx].data_addr + DATA_LEVEL_SIG_LEN;
    *len1 = *p++;
    *line1 = p;
    p += *len1;
    *len2 = *p++;
    *line2 = p;

    return (*len2 > 0) ? 2 : 1;
}

uint8_t level_get_difficulty(uint8_t level_idx)
{
    if (level_idx >= ms.num_levels) return 0;
    return *ms.levels[level_idx].after_name_addr;
}

uint24_t level_get_id(uint8_t level_idx)
{
    if (level_idx >= ms.num_levels) return 0;
    uint8_t *p = ms.levels[level_idx].after_name_addr + 1; /* skip difficulty */
    return p[0] | ((uint24_t)p[1] << 8) | ((uint24_t)p[2] << 16);
}

/* --- high score management --- */

uint24_t score_find(uint24_t level_id)
{
    ti_var_t slot = ti_Open(SCORE_APPVAR, "r");
    if (!slot) return 0;

    uint24_t size = ti_GetSize(slot);
    if (size < 6) { ti_Close(slot); return 0; }

    uint8_t *data = ti_GetDataPtr(slot);
    uint24_t count = size / 6; /* each entry: 3 bytes id + 3 bytes score */

    for (uint24_t i = 0; i < count; i++) {
        uint8_t *entry = data + i * 6;
        uint24_t id = entry[0] | ((uint24_t)entry[1] << 8) | ((uint24_t)entry[2] << 16);
        if (id == level_id) {
            uint24_t score = entry[3] | ((uint24_t)entry[4] << 8) | ((uint24_t)entry[5] << 16);
            ti_Close(slot);
            return score;
        }
    }

    ti_Close(slot);
    return 0;
}

void score_update(uint24_t level_id, uint24_t progress)
{
    ti_var_t slot = ti_Open(SCORE_APPVAR, "r+");
    if (!slot) {
        /* AppVar doesn't exist yet; create it.
           "w" is safe here because "r+" only fails when the var is absent
           (fileioc dearchives automatically for "r+"). */
        slot = ti_Open(SCORE_APPVAR, "w");
        if (!slot) return;
    }

    uint24_t size = ti_GetSize(slot);
    uint8_t *data = ti_GetDataPtr(slot);
    uint24_t count = size / 6;

    /* search for existing entry */
    for (uint24_t i = 0; i < count; i++) {
        uint8_t *entry = data + i * 6;
        uint24_t id = entry[0] | ((uint24_t)entry[1] << 8) | ((uint24_t)entry[2] << 16);
        if (id == level_id) {
            uint24_t old = entry[3] | ((uint24_t)entry[4] << 8) | ((uint24_t)entry[5] << 16);
            if (progress > old) {
                entry[3] = progress & 0xFF;
                entry[4] = (progress >> 8) & 0xFF;
                entry[5] = (progress >> 16) & 0xFF;
            }
            ti_SetArchiveStatus(true, slot);
            ti_Close(slot);
            return;
        }
    }

    /* add new entry at end */
    ti_Seek(0, SEEK_END, slot);
    uint8_t entry[6];
    entry[0] = level_id & 0xFF;
    entry[1] = (level_id >> 8) & 0xFF;
    entry[2] = (level_id >> 16) & 0xFF;
    entry[3] = progress & 0xFF;
    entry[4] = (progress >> 8) & 0xFF;
    entry[5] = (progress >> 16) & 0xFF;
    ti_Write(entry, 6, 1, slot);
    ti_SetArchiveStatus(true, slot);
    ti_Close(slot);
}

bool level_create(const uint8_t *name_buf, uint8_t difficulty)
{
    /* name_buf format: [len1][chars...][len2][chars...] */
    uint8_t len1 = name_buf[0];
    uint8_t len2 = name_buf[1 + len1];
    uint8_t total_name = 1 + len1 + 1 + len2;

    /* build AppVar name from the level name (L + letters, max 8 chars) */
    char av_name[9];
    av_name[0] = 'L';
    uint8_t j = 1;
    for (uint8_t i = 0; i < len1 && j < 8; i++) {
        uint8_t ch = name_buf[1 + i];
        if (ch != ' ')
            av_name[j++] = ch + 0x40;
    }
    av_name[j] = 0;

    /* check if name exists */
    ti_var_t slot = ti_Open(av_name, "r");
    if (slot) {
        ti_Close(slot);
        /* try appending digits */
        for (uint8_t d = '0'; d <= '9'; d++) {
            av_name[j] = d;
            av_name[j + 1] = 0;
            slot = ti_Open(av_name, "r");
            if (!slot) break;
            ti_Close(slot);
            if (d == '9') return false; /* all names taken */
        }
    }

    /* calculate AppVar size */
    uint24_t map_data_size = 40 * WIN_ROWS; /* default 40 columns */
    (void)map_data_size; /* size is implicit from sequential writes */

    slot = ti_Open(av_name, "w");
    if (!slot) return false;

    /* write signature */
    ti_Write(DATA_LEVEL_SIG, DATA_LEVEL_SIG_LEN, 1, slot);

    /* write name */
    ti_Write(name_buf, total_name, 1, slot);

    /* difficulty */
    ti_PutC(difficulty, slot);

    /* hash ID (3 bytes) */
    uint24_t hash = 0;
    for (uint8_t i = 0; i < len1; i++)
        hash = hash * 10 + name_buf[1 + i];
    hash |= 0xFF0000; /* ensure MSB != 0 for version detection */
    uint8_t id_bytes[3] = { hash & 0xFF, (hash >> 8) & 0xFF, (hash >> 16) & 0xFF };
    ti_Write(id_bytes, 3, 1, slot);

    /* speed */
    ti_PutC(15, slot);

    /* map_size_x = 40 (3 bytes) */
    uint8_t sx[3] = { 40, 0, 0 };
    ti_Write(sx, 3, 1, slot);

    /* extra rows = 0 */
    ti_PutC(0, slot);

    /* empty tile data */
    for (uint24_t i = 0; i < map_data_size; i++)
        ti_PutC(0, slot);

    /* gravity contexts: count=0 */
    ti_PutC(0, slot);

    /* spaceship contexts: count=0 */
    ti_PutC(0, slot);

    ti_Close(slot);
    return true;
}
