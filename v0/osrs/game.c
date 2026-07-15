#include "game.h"

#include "osrs/lua_sidecar/lua_gametypes.h"
#include "osrs/lua_sidecar/lua_sidecar_misc.h"
#include "osrs/painters_cullmap_baked_path.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
game_add_message(
    struct GGame* game,
    int type,
    const char* text,
    const char* sender)
{
    (void)game;
    (void)type;
    (void)text;
    (void)sender;
}
