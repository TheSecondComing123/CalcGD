#ifndef EDITOR_H
#define EDITOR_H

#include "defs.h"

/* run the level editor for the given level AppVar name.
   addr points to the level data (after name/difficulty/id/speed). */
void editor_run(uint8_t *level_data_addr);

#endif
