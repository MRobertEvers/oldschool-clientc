#ifndef CORE_TASKS_DAT1_MODELLOAD_H
#define CORE_TASKS_DAT1_MODELLOAD_H

#include "../../core/dat1/dat1io.h"
#include "../../gamecache/gamecache.h"
#include "../../gamecache/toridraw_cachemodel.h"
#include "../../games/game_handle.h"
#include "../../games/model_viewer.h"

PT_STATE_BEGIN(dat1_model_load_task)
struct GameHandle game;
int model_id;
PT_STATE_END(dat1_model_load_task)

PT_THREAD(dat1_model_load_task)
{
    struct CacheModel* model = NULL;
    struct ToriDraw_Model* td = NULL;
    PT_BEGIN();

    dat1io_model_fetch(ctx, state->model_id);
    PT_YIELD();

    model = dat1io_model_decode(ctx, state->model_id);
    if( !model )
        return PT_FINISHED;

    td = toridraw_model_new_from_cache_model(model);
    model_free(model);
    if( !td )
        return PT_FINISHED;

    struct ToriDraw_ModelHandle model_handle = {
        .kind = TORIDRAWMK_MODEL,
        .u.model.model = td,
    };

    gamecache_model_add(state->game.u.model_viewer->gamecache, state->model_id, model_handle);

    PT_END();
}
#endif