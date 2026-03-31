#include <tice.h>
#include <graphx.h>
#include <keypadc.h>
#include <fileioc.h>
#include <string.h>

#include "defs.h"
#include "gfx.h"
#include "menu.h"

/* global state */
game_state_t gs;
menu_state_t ms;

int main(void)
{
    os_ClrHome();

    if (!gfx_game_init()) {
        gfx_cleanup();
        os_ClrHome();
        os_PutStrFull("Need AppVar GDGrphc");
        while (!os_GetCSC());
        return 1;
    }

    menu_run();

    gfx_cleanup();
    return 0;
}
