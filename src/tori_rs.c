#include "tori_rs.h"

#include "graphics/dash.h"
#include "graphics/lighting.h"
#include "osrs/buildcachedat.h"
#include "osrs/dash_utils.h"
#include "osrs/gameproto_parse.h"
#include "osrs/gameproto_process.h"
#include "osrs/gio.h"
#include "osrs/loginproto.h"
#include "osrs/packetbuffer.h"
#include "osrs/packetin.h"
#include "osrs/rscache/cache.h"
#include "osrs/rscache/cache_dat.h"
#include "osrs/rscache/tables/model.h"
#include "osrs/rscache/tables_dat/config_component.h"
#include "osrs/rscache/tables_dat/config_versionlist_mapsquare.h"
#include "osrs/rscache/tables_dat/configs_dat.h"
#include "osrs/scenebuilder.h"

// clang-format off
#include "osrs/gameproto_packets_write.u.c"
#include "tori_rs_input.u.c"
#include "tori_rs_net.u.c"
#include "tori_rs_cycle.u.c"
#include "tori_rs_init.u.c"
#include "tori_rs_frame.u.c"
// clang-format on

#include <math.h>
#include <stdlib.h>
#include <string.h>

bool
LibToriRS_GameIsRunning(struct GGame* game)
{
    return game->running;
}
