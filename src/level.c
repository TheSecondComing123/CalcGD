#include <tice.h>
#include <fileioc.h>
#include <string.h>

#include "defs.h"
#include "level.h"

#define SCORE_APPVAR "GeomDash"
#define DATA_LEVEL_SIG "\xFF" "Epharius" "\xFF" "GD"

typedef struct {
    uint8_t difficulty;
    uint24_t level_id;
    uint8_t level_speed;
    uint24_t map_size_x;
    uint8_t extra_rows;
    uint24_t map_off;
} level_meta_t;

static ti_var_t loaded_level_slot;

static bool parse_level_meta(uint8_t *data, uint24_t size, level_meta_t *meta)
{
    if (size < LEVEL_SIG_LEN + 1) return false;
    if (memcmp(data, DATA_LEVEL_SIG, LEVEL_SIG_LEN) != 0) return false;

    uint24_t off = LEVEL_SIG_LEN;

    uint8_t len1 = data[off++];
    if ((uint32_t)off + len1 + 1u > size) return false;
    off += len1;

    uint8_t len2 = data[off++];
    if ((uint32_t)off + len2 + 1u + 3u + 1u + 3u + 1u > size) return false;
    off += len2;

    meta->difficulty = data[off++];
    if (meta->difficulty > 3) return false;

    meta->level_id = data[off] | ((uint24_t)data[off + 1] << 8) | ((uint24_t)data[off + 2] << 16);
    off += 3;

    meta->level_speed = data[off++];

    meta->map_size_x = data[off] | ((uint24_t)data[off + 1] << 8) | ((uint24_t)data[off + 2] << 16);
    off += 3;
    if (meta->map_size_x == 0) return false;

    meta->extra_rows = data[off++];
    meta->map_off = off;

    return true;
}

void level_unload(void)
{
    if (loaded_level_slot) {
        ti_Close(loaded_level_slot);
        loaded_level_slot = 0;
    }
}

void level_scan(void)
{
    void *search_pos = NULL;
    char *name;

    level_unload();
    ms.num_levels = 0;

    while ((name = ti_DetectVar(&search_pos, DATA_LEVEL_SIG, OS_TYPE_APPVAR)) != NULL) {
        if (ms.num_levels >= MAX_LEVELS) break;

        ti_var_t slot = ti_Open(name, "r");
        if (!slot) continue;

        uint24_t size = ti_GetSize(slot);
        uint8_t *data = ti_GetDataPtr(slot);

        level_meta_t meta;
        if (!parse_level_meta(data, size, &meta)) {
            ti_Close(slot);
            continue;
        }

        level_entry_t *le = &ms.levels[ms.num_levels];
        memset(le, 0, sizeof(*le));

        for (uint8_t i = 0; i < 8 && name[i]; i++)
            le->vat_name[i] = name[i];

        uint24_t off = LEVEL_SIG_LEN;
        uint8_t len1 = data[off++];
        le->name_len1 = (len1 > 8) ? 8 : len1;
        for (uint8_t i = 0; i < le->name_len1; i++)
            le->name_line1[i] = data[off + i];
        off += len1;

        uint8_t len2 = data[off++];
        le->name_len2 = (len2 > 8) ? 8 : len2;
        for (uint8_t i = 0; i < le->name_len2; i++)
            le->name_line2[i] = data[off + i];

        le->difficulty = meta.difficulty;
        le->level_id = meta.level_id;
        le->map_size_x = meta.map_size_x;

        ms.num_levels++;
        ti_Close(slot);
    }

    if (ms.num_levels == 0) {
        ms.current_idx = 0;
    } else if (ms.current_idx >= ms.num_levels) {
        ms.current_idx = 0;
    }
}

bool level_load(uint8_t level_idx)
{
    if (level_idx >= ms.num_levels) return false;

    level_unload();

    ti_var_t slot = ti_Open(ms.levels[level_idx].vat_name, "r");
    if (!slot) return false;

    uint24_t size = ti_GetSize(slot);
    uint8_t *data = ti_GetDataPtr(slot);
    level_meta_t meta;

    if (!parse_level_meta(data, size, &meta)) {
        ti_Close(slot);
        return false;
    }

    gs.level_speed = meta.level_speed;
    gs.map_size_x = meta.map_size_x;
    gs.map_data_off = meta.map_off;

    gs.beginning_map = data + meta.map_off;
    gs.first_block = NULL;

    gs.bytes_to_skip = (uint24_t)meta.extra_rows * gs.map_size_x;
    gs.max_bytes_to_skip = gs.bytes_to_skip;

    uint32_t num_rows = (uint32_t)WIN_ROWS + meta.extra_rows;
    uint32_t tile_count32 = num_rows * (uint32_t)gs.map_size_x;
    if (tile_count32 > 0xFFFFFFu) {
        ti_Close(slot);
        return false;
    }

    uint24_t tile_count = (uint24_t)tile_count32;
    if ((uint32_t)meta.map_off + tile_count + 1u > size) {
        ti_Close(slot);
        return false;
    }

    uint24_t context_off = meta.map_off + tile_count;

    gs.num_gravity_remaining = data[context_off++];
    gs.num_gravity_total = gs.num_gravity_remaining;
    gs.addr_gravity_base_off = context_off - meta.map_off;

    if ((uint32_t)context_off + (uint32_t)gs.num_gravity_remaining * 3u > size) {
        ti_Close(slot);
        return false;
    }
    gs.addr_gravity = data + context_off;
    context_off += gs.num_gravity_remaining * 3;

    gs.flags.ship_available = false;
    gs.num_ship_remaining = 0;
    gs.num_ship_total = 0;
    gs.addr_spaceship = NULL;
    gs.addr_spaceship_base_off = 0;

    if (gs.level_speed != 0) {
        if (context_off >= size) {
            ti_Close(slot);
            return false;
        }

        gs.num_ship_remaining = data[context_off++];
        gs.num_ship_total = gs.num_ship_remaining;
        gs.addr_spaceship_base_off = context_off - meta.map_off;

        if ((uint32_t)context_off + (uint32_t)gs.num_ship_remaining * 3u > size) {
            ti_Close(slot);
            return false;
        }

        gs.addr_spaceship = data + context_off;
        gs.flags.ship_available = true;
    } else {
        /* old version without speed field */
        gs.level_speed = 0x0E;
    }

    loaded_level_slot = slot;
    return true;
}

uint8_t level_get_name(uint8_t level_idx, const uint8_t **line1, uint8_t *len1,
                       const uint8_t **line2, uint8_t *len2)
{
    if (level_idx >= ms.num_levels) {
        *line1 = NULL;
        *line2 = NULL;
        *len1 = 0;
        *len2 = 0;
        return 0;
    }

    level_entry_t *le = &ms.levels[level_idx];
    *line1 = le->name_line1;
    *len1 = le->name_len1;
    *line2 = le->name_line2;
    *len2 = le->name_len2;

    return (le->name_len2 > 0) ? 2 : 1;
}

uint8_t level_get_difficulty(uint8_t level_idx)
{
    if (level_idx >= ms.num_levels) return 0;
    return ms.levels[level_idx].difficulty;
}

uint24_t level_get_id(uint8_t level_idx)
{
    if (level_idx >= ms.num_levels) return 0;
    return ms.levels[level_idx].level_id;
}

/* --- high score management --- */

uint24_t score_find(uint24_t level_id)
{
    ti_var_t slot = ti_Open(SCORE_APPVAR, "r");
    if (!slot) return 0;

    uint24_t size = ti_GetSize(slot);
    if (size < 6) { ti_Close(slot); return 0; }

    uint8_t *data = ti_GetDataPtr(slot);
    uint24_t count = size / 6;

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
        slot = ti_Open(SCORE_APPVAR, "w");
        if (!slot) return;
    }

    uint24_t size = ti_GetSize(slot);
    uint8_t *data = ti_GetDataPtr(slot);
    uint24_t count = size / 6;

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
    if (!name_buf) return false;
    if (difficulty > 3) return false;

    uint8_t len1 = name_buf[0];
    if (len1 > 8) return false;

    uint8_t len2_idx = 1 + len1;
    if (len2_idx >= 22) return false;

    uint8_t len2 = name_buf[len2_idx];
    if (len2 > 8) return false;

    uint8_t total_name = 1 + len1 + 1 + len2;
    if (total_name > 22) return false;

    char av_name[9];
    av_name[0] = 'L';
    uint8_t j = 1;
    for (uint8_t i = 0; i < len1 && j < 8; i++) {
        uint8_t ch = name_buf[1 + i];
        if (ch != ' ')
            av_name[j++] = ch + 0x40;
    }
    av_name[j] = 0;

    ti_var_t slot = ti_Open(av_name, "r");
    if (slot) {
        ti_Close(slot);
        uint8_t suffix_pos = (j < 8) ? j : 7;
        for (uint8_t d = '0'; d <= '9'; d++) {
            av_name[suffix_pos] = d;
            av_name[suffix_pos + 1] = 0;
            slot = ti_Open(av_name, "r");
            if (!slot) break;
            ti_Close(slot);
            if (d == '9') return false;
        }
    }

    uint24_t map_data_size = 40 * WIN_ROWS;

    slot = ti_Open(av_name, "w");
    if (!slot) return false;

    ti_Write(DATA_LEVEL_SIG, LEVEL_SIG_LEN, 1, slot);
    ti_Write(name_buf, total_name, 1, slot);

    ti_PutC(difficulty, slot);

    uint24_t hash = 0;
    for (uint8_t i = 0; i < len1; i++)
        hash = hash * 10 + name_buf[1 + i];
    hash |= 0xFF0000;
    uint8_t id_bytes[3] = { hash & 0xFF, (hash >> 8) & 0xFF, (hash >> 16) & 0xFF };
    ti_Write(id_bytes, 3, 1, slot);

    ti_PutC(15, slot);

    uint8_t sx[3] = { 40, 0, 0 };
    ti_Write(sx, 3, 1, slot);

    ti_PutC(0, slot);

    for (uint24_t i = 0; i < map_data_size; i++)
        ti_PutC(0, slot);

    ti_PutC(0, slot);
    ti_PutC(0, slot);

    ti_Close(slot);
    return true;
}
