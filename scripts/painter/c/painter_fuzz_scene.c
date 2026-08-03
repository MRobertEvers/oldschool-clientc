#include "painter_fuzz_scene.h"

#include "graphics/projection.h"
#include "graphics/shared_tables.h"
#include "painters/painters_cull_project.h"

#include <math.h>
#include <string.h>

static uint32_t
rng_u32(uint32_t* s)
{
    *s = *s * 1103515245u + 12345u;
    return *s;
}

static int
rng_range(uint32_t* s, int lo, int hi)
{
    if( hi <= lo )
        return lo;
    return lo + (int)(rng_u32(s) % (uint32_t)(hi - lo + 1));
}

static int
pick_coord_near(uint32_t* s, int cam, int gs, int focus)
{
    if( !focus )
        return rng_range(s, 0, gs - 1);
    int lo = cam - 24;
    int hi = cam + 24;
    if( lo < 0 )
        lo = 0;
    if( hi > gs - 1 )
        hi = gs - 1;
    return rng_range(s, lo, hi);
}

static int
pick_level(uint32_t* s, int levels, uint8_t level_mask, int focus)
{
    if( focus && level_mask )
    {
        int enabled[8];
        int n = 0;
        for( int l = 0; l < levels && l < 8; l++ )
            if( level_mask & (1u << l) )
                enabled[n++] = l;
        if( n > 0 )
            return enabled[rng_u32(s) % (uint32_t)n];
    }
    return rng_range(s, 0, levels - 1);
}

void
painter_fuzz_fill_config(PainterFuzzConfig* cfg, uint32_t seed)
{
    uint32_t rng = seed;
    memset(cfg, 0, sizeof(*cfg));
    cfg->seed = seed;
    cfg->grid = rng_range(&rng, 11, PAINTER_FUZZ_MAX_GRID);
    cfg->levels = rng_range(&rng, 1, 4);
    cfg->use_cullmap = (int)(rng_u32(&rng) % 3u); /* 0=nocull, 1=span, 2=baked */
    cfg->camera_sx = rng_range(&rng, 0, cfg->grid - 1);
    cfg->camera_sz = rng_range(&rng, 0, cfg->grid - 1);
    if( (rng_u32(&rng) & 7u) == 0 )
    {
        cfg->camera_sx = 0;
        cfg->camera_sz = 0;
    }
    else if( (rng_u32(&rng) & 7u) == 1 )
    {
        cfg->camera_sx = cfg->grid - 1;
        cfg->camera_sz = cfg->grid / 2;
    }
    cfg->camera_slevel = rng_range(&rng, 0, cfg->levels - 1);
    cfg->pitch = rng_range(&rng, 128, 384);
    cfg->yaw = rng_range(&rng, 0, 2047);
    cfg->level_mask = (uint8_t)(0xFu >> rng_range(&rng, 0, 3));
    if( cfg->level_mask == 0 )
        cfg->level_mask = 0xFu;
    cfg->scenery_count = rng_range(&rng, 0, 16);
    cfg->wall_count = rng_range(&rng, 0, 12);
    cfg->decor_count = rng_range(&rng, 0, 10);
    cfg->ground_decor_count = rng_range(&rng, 0, 8);
}

struct Painter*
painter_fuzz_build_scene(
    const PainterFuzzConfig* cfg,
    struct PaintersCullMap** out_cm,
    PainterFuzzAddedCounts* added)
{
    uint32_t rng = cfg->seed;
    int gs = cfg->grid;
    int levels = cfg->levels;

    struct Painter* painter = painter_new(
        gs,
        gs,
        levels,
        PAINTER_NEW_CTX_BUCKET | PAINTER_NEW_CTX_WORLD3D);
    if( !painter )
        return NULL;

    struct PaintersCullMap* cm = NULL;
    if( cfg->use_cullmap == 2 )
    {
        struct ToriDrawTrigTables tables = {
            .sin = ToriDraw_GetSinTable(),
            .cos = ToriDraw_GetCosTable(),
            .tan = ToriDraw_GetTanTable(),
        };
        struct ToriDrawTrigFns trig;
        ToriDraw_TrigFnsFromTables(&trig, &tables);
        cm = painters_cullmap_build_toridraw(25, 50, 512, 384, &trig);
        if( !cm )
        {
            painter_free(painter);
            return NULL;
        }
        painter_set_cullmap(painter, cm);
    }
    else
    {
        cm = painters_cullmap_new_nocull();
        if( !cm )
        {
            painter_free(painter);
            return NULL;
        }
        painter_set_cullmap(painter, cm);
        if( cfg->use_cullmap == 1 )
        {
            struct PaintersCullSpanParams params;
            struct PaintersCullSpan span;
            int dist = cfg->pitch * 3 + 600;
            int ph = (int)((double)dist * sin((double)cfg->pitch * 2.0 * 3.14159265358979323846 /
                                              2048.0));
            memset(&params, 0, sizeof(params));
            params.pitch = cfg->pitch;
            params.yaw = cfg->yaw;
            params.eye_height = ph;
            params.y_lo = PCULL_FRUSTUM_Y_START;
            params.y_hi = PCULL_FRUSTUM_Y_END;
            params.near_clip = 50;
            params.far_clip = 100000;
            params.screen_width = 512;
            params.screen_height = 384;
            params.fov_rpi2048 = 512;
            params.dz_min = -25;
            params.dz_max = 25;
            painters_cullspan_build(&span, &params);
            painter_set_cullspan(painter, &span);
        }
    }
    painter_set_camera_angles(painter, cfg->pitch, cfg->yaw);
    painter_set_level_mask(painter, cfg->level_mask);

    if( added )
        memset(added, 0, sizeof(*added));

    uint8_t lmask = cfg->level_mask ? cfg->level_mask : 0xFu;

    for( int i = 0; i < cfg->scenery_count; i++ )
    {
        int focus = (rng_u32(&rng) % 4u) != 0;
        int sx = pick_coord_near(&rng, cfg->camera_sx, gs, focus);
        int sz = pick_coord_near(&rng, cfg->camera_sz, gs, focus);
        int slevel = pick_level(&rng, levels, lmask, focus);
        int w = rng_range(&rng, 1, 4);
        int h = rng_range(&rng, 1, 4);
        if( sx + w > gs )
            w = gs - sx;
        if( sz + h > gs )
            h = gs - sz;
        if( w < 1 )
            w = 1;
        if( h < 1 )
            h = 1;
        if( painter_add_normal_scenery(painter, sx, sz, slevel, 1000 + i, w, h) >= 0 && added )
            added->scenery++;
    }

    for( int i = 0; i < cfg->wall_count; i++ )
    {
        int focus = (rng_u32(&rng) % 4u) != 0;
        int sx = pick_coord_near(&rng, cfg->camera_sx, gs, focus);
        int sz = pick_coord_near(&rng, cfg->camera_sz, gs, focus);
        int slevel = pick_level(&rng, levels, lmask, focus);
        struct PaintersTile* tile = painter_tile_at(painter, sx, sz, slevel);
        if( tile->wall_a != -1 && tile->wall_b != -1 )
            continue;
        int wall_ab = (tile->wall_a == -1) ? WALL_A : WALL_B;
        int side = 1 << rng_range(&rng, 0, 7);
        if( painter_add_wall(painter, sx, sz, slevel, 2000 + i, wall_ab, side) >= 0 && added )
            added->wall++;
    }

    for( int i = 0; i < cfg->decor_count; i++ )
    {
        int focus = (rng_u32(&rng) % 4u) != 0;
        int sx = pick_coord_near(&rng, cfg->camera_sx, gs, focus);
        int sz = pick_coord_near(&rng, cfg->camera_sz, gs, focus);
        int slevel = pick_level(&rng, levels, lmask, focus);
        struct PaintersTile* tile = painter_tile_at(painter, sx, sz, slevel);
        if( tile->wall_decor_a != -1 && tile->wall_decor_b != -1 )
            continue;
        int wall_ab = (tile->wall_decor_a == -1) ? WALL_A : WALL_B;
        int side = 1 << rng_range(&rng, 0, 7);
        int through = (rng_u32(&rng) & 3u) == 0 ? THROUGHWALL : 0;
        if( painter_add_wall_decor(
                painter, sx, sz, slevel, 3000 + i, wall_ab, side, through) >= 0 &&
            added )
            added->walldecor++;
    }

    for( int i = 0; i < cfg->ground_decor_count; i++ )
    {
        int focus = (rng_u32(&rng) % 4u) != 0;
        int sx = pick_coord_near(&rng, cfg->camera_sx, gs, focus);
        int sz = pick_coord_near(&rng, cfg->camera_sz, gs, focus);
        int slevel = pick_level(&rng, levels, lmask, focus);
        struct PaintersTile* tile = painter_tile_at(painter, sx, sz, slevel);
        if( tile->ground_decor == -1 )
        {
            painter_add_ground_decor(painter, sx, sz, slevel, 4000 + i);
            if( added )
                added->grounddecor++;
        }
    }

    if( levels >= 2 && (rng_u32(&rng) & 3u) == 0 )
    {
        int focus = (rng_u32(&rng) % 4u) != 0;
        int sx = pick_coord_near(&rng, cfg->camera_sx, gs, focus);
        int sz = pick_coord_near(&rng, cfg->camera_sz, gs, focus);
        int upper = rng_range(&rng, 1, levels - 1);
        painter_tile_set_bridge(painter, sx, sz, upper, sx, sz, 0);
    }

    if( levels >= 2 && (rng_u32(&rng) & 3u) == 0 )
    {
        int focus = (rng_u32(&rng) % 4u) != 0;
        int sx = pick_coord_near(&rng, cfg->camera_sx, gs, focus);
        int sz = pick_coord_near(&rng, cfg->camera_sz, gs, focus);
        int from = rng_range(&rng, 0, levels - 1);
        int to = rng_range(&rng, 0, levels - 1);
        if( from != to )
            painter_tile_copyto(painter, sx, sz, from, sx, sz, to);
    }

    for( int i = 0; i < 4; i++ )
    {
        if( (rng_u32(&rng) & 1u) == 0 )
            continue;
        int focus = (rng_u32(&rng) % 4u) != 0;
        int sx = pick_coord_near(&rng, cfg->camera_sx, gs, focus);
        int sz = pick_coord_near(&rng, cfg->camera_sz, gs, focus);
        int slevel = pick_level(&rng, levels, lmask, focus);
        int draw = rng_range(&rng, 0, levels - 1);
        painter_tile_set_draw_level(painter, sx, sz, slevel, draw);
    }

    for( int i = 0; i < 3; i++ )
    {
        int focus = (rng_u32(&rng) % 4u) != 0;
        int sx = pick_coord_near(&rng, cfg->camera_sx, gs, focus);
        int sz = pick_coord_near(&rng, cfg->camera_sz, gs, focus);
        int slevel = pick_level(&rng, levels, lmask, focus);
        struct PaintersTile* tile = painter_tile_at(painter, sx, sz, slevel);
        if( tile->ground_object_bottom == -1 )
        {
            painter_add_ground_object(painter, sx, sz, slevel, 5000 + i, GROUND_OBJECT_BOTTOM);
            if( added )
                added->groundobj++;
        }
    }

    painter_mark_static_count(painter);
    *out_cm = cm;
    return painter;
}
