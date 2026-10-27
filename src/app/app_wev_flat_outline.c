/*
 * The outline flattened hull: a deck drawn as its footprint, built once.
 *
 * Not the deob's rendering. A flattened hull reads as a one-colour patch on the
 * water; drawing it the live way costs every face of every model on the deck,
 * squashed to 1% height and stacked at one depth (~18k faces a hull). This
 * strategy keeps the patch and drops the faces:
 *
 *   1. every visible face of the deck's terrain and scenery, as drawn now, is
 *      laid flat onto an XZ grid of OUTLINE_CELL-unit cells; a cell is filled
 *      when the triangle touches it (conservative, so a thin boom survives);
 *   2. filled cells are merged into rectangles, two flat-shaded triangles each,
 *      all at one height, all the config's flat HSL;
 *   3. the result is ONE scene element in the root's dynamic pool, which the
 *      painter emits in place of the deck (painter_set_world_entity_element)
 *      and the frame places with the hull's own transform.
 *
 * It is rebuilt when the deck's geometry changes (World::geometry_serial, which
 * a deck rebuild, a terrain tile or a loc add/remove bumps), when the view slot
 * goes to another hull, or when the flat HSL differs. Poses are never read
 * again: whatever pose a model held when the outline was built is the one it
 * keeps, and a flag flapping on a flattened deck changes nothing.
 *
 * What is given up against `live`: the outline is quantised to OUTLINE_CELL
 * units; faces seen exactly edge-on from above (a sail, a mast's sides) leave
 * no trace instead of a hairline; a hull carried on a flattened deck is not
 * drawn.
 *
 * @see app_wev_flat.h
 */
#include "app/app_internal.h"
#include "app/app_wev_flat.h"

/** Grid cell edge in fine units: 1/8 of a tile. */
#define OUTLINE_CELL_SHIFT 4
#define OUTLINE_CELL (1 << OUTLINE_CELL_SHIFT)
/** Twice the XZ area below which a face is edge-on from above and leaves
 *  nothing: a quarter of a cell. */
#define OUTLINE_MIN_AREA2 (OUTLINE_CELL * OUTLINE_CELL / 2)

struct OutlineView
{
    /** The scene element drawn for this view, or -1: none built, or the deck
     *  had nothing visible when it was. */
    int element_id;
    bool built;
    /** What the build read; any difference rebuilds. */
    struct World const* world;
    uint32_t geometry_serial;
    /** outline_signature of the records it was built from. A serial bump
     *  whose records hash the same (a loc removed and put back as it was,
     *  every tick on a salvaging deck) does not rebuild. */
    uint64_t signature;
    int hsl;
};

struct Outline
{
    struct OutlineView views[WORLDVIEW_MAX];
};

/** One drawable model on the deck, in the deck world's fine units. */
struct OutlineSource
{
    int vertex_count;
    int face_count;
    vertexint_t const* vx;
    vertexint_t const* vy;
    vertexint_t const* vz;
    faceint_t const* fa;
    faceint_t const* fb;
    faceint_t const* fc;
    hsl16_t const* colors_c;
    int pos_x;
    int pos_y;
    int pos_z;
    int cos_yaw;
    int sin_yaw;
};

struct OutlineRect
{
    int x;
    int z;
    int w;
    int h;
};

static bool
outline_source(
    struct ToriDraw_Scene* scene,
    int element_id,
    struct OutlineSource* out)
{
    struct ToriDraw_SceneElement* el;

    assert(scene);
    assert(out);
    if( element_id < 0 || !ToriDraw_SceneElementIsLive(scene, element_id) )
        return false;
    el = ToriDraw_SceneElementGet(scene, element_id);
    if( el->model.kind == TORIDRAWMK_GROUND )
    {
        struct ToriDraw_ModelGround const* g = el->model.u.model.ground;
        assert(g);
        *out = (struct OutlineSource){
            .vertex_count = g->vertex_count,
            .face_count = g->face_count,
            .vx = g->vertices_x,
            .vy = g->vertices_y,
            .vz = g->vertices_z,
            .fa = g->face_indices_a,
            .fb = g->face_indices_b,
            .fc = g->face_indices_c,
            .colors_c = g->face_colors_c,
        };
    }
    else if( ToriDraw_ModelKindIsFull(el->model.kind) )
    {
        struct ToriDraw_Model const* m = ToriDraw_ModelRead(el->model);
        assert(m);
        *out = (struct OutlineSource){
            .vertex_count = m->vertex_count,
            .face_count = m->face_count,
            .vx = m->vertices_x,
            .vy = m->vertices_y,
            .vz = m->vertices_z,
            .fa = m->face_indices_a,
            .fb = m->face_indices_b,
            .fc = m->face_indices_c,
            .colors_c = m->face_colors_c,
        };
    }
    else
        return false;
    if( out->face_count <= 0 || !out->vx || !out->vy || !out->vz || !out->fa || !out->fb ||
        !out->fc )
        return false;
    out->pos_x = el->world_position.x;
    out->pos_y = el->world_position.y;
    out->pos_z = el->world_position.z;
    /* The model's own yaw, in the engine sense frame_view_apply uses. */
    out->cos_yaw = ToriDraw_Cos(el->world_position.yaw & 0x7ff);
    out->sin_yaw = ToriDraw_Sin(el->world_position.yaw & 0x7ff);
    return true;
}

static inline bool
outline_face_visible(
    struct OutlineSource const* s,
    int f)
{
    /* Hidden is the cache's own "this face is not drawn". */
    return !s->colors_c || s->colors_c[f] != TORIDRAWHSL16_HIDDEN;
}

static inline void
outline_point(
    struct OutlineSource const* s,
    int v,
    int* out_x,
    int* out_z)
{
    int x = s->vx[v];
    int z = s->vz[v];
    *out_x = ((x * s->cos_yaw + z * s->sin_yaw) >> 16) + s->pos_x;
    *out_z = ((z * s->cos_yaw - x * s->sin_yaw) >> 16) + s->pos_z;
}

/**
 * Does triangle (ax,az)(bx,bz)(cx,cz) touch the box [x0,x1) x [z0,z1)? The
 * caller only asks about boxes inside the triangle's bounding box, so the box
 * axes are settled; what remains is each edge as a separating line.
 */
static bool
outline_tri_touches_box(
    int64_t const* tx,
    int64_t const* tz,
    int64_t x0,
    int64_t z0,
    int64_t x1,
    int64_t z1)
{
    for( int e = 0; e < 3; ++e )
    {
        int n = (e + 1) % 3;
        int o = (e + 2) % 3;
        int64_t ex = tx[n] - tx[e];
        int64_t ez = tz[n] - tz[e];
        /* Side of a point against the edge; the third vertex sets "inside". */
        int64_t inside = ex * (tz[o] - tz[e]) - ez * (tx[o] - tx[e]);
        int64_t const cx[4] = { x0, x1, x1, x0 };
        int64_t const cz[4] = { z0, z0, z1, z1 };
        bool any_inside = false;
        for( int c = 0; c < 4 && !any_inside; ++c )
        {
            int64_t side = ex * (cz[c] - tz[e]) - ez * (cx[c] - tx[e]);
            any_inside = inside > 0 ? side >= 0 : side <= 0;
        }
        if( !any_inside )
            return false;
    }
    return true;
}

static void
outline_fill_face(
    struct OutlineSource const* s,
    int f,
    uint8_t* grid,
    int grid_w,
    int grid_h,
    int min_x,
    int min_z)
{
    int64_t tx[3];
    int64_t tz[3];
    int idx[3] = { s->fa[f], s->fb[f], s->fc[f] };
    int64_t lo_x = INT64_MAX, hi_x = INT64_MIN, lo_z = INT64_MAX, hi_z = INT64_MIN;

    for( int i = 0; i < 3; ++i )
    {
        int x, z;
        assert(idx[i] >= 0 && idx[i] < s->vertex_count);
        outline_point(s, idx[i], &x, &z);
        tx[i] = x - min_x;
        tz[i] = z - min_z;
        if( tx[i] < lo_x ) lo_x = tx[i];
        if( tx[i] > hi_x ) hi_x = tx[i];
        if( tz[i] < lo_z ) lo_z = tz[i];
        if( tz[i] > hi_z ) hi_z = tz[i];
    }
    int64_t area2 = (tx[1] - tx[0]) * (tz[2] - tz[0]) - (tz[1] - tz[0]) * (tx[2] - tx[0]);
    if( area2 < 0 )
        area2 = -area2;
    if( area2 < OUTLINE_MIN_AREA2 )
        return;

    (void)grid_h;
    int c0x = (int)(lo_x >> OUTLINE_CELL_SHIFT), c1x = (int)(hi_x >> OUTLINE_CELL_SHIFT);
    int c0z = (int)(lo_z >> OUTLINE_CELL_SHIFT), c1z = (int)(hi_z >> OUTLINE_CELL_SHIFT);
    assert(c0x >= 0 && c1x < grid_w);
    assert(c0z >= 0 && c1z < grid_h);
    for( int cz = c0z; cz <= c1z; ++cz )
        for( int cx = c0x; cx <= c1x; ++cx )
        {
            uint8_t* cell = &grid[cz * grid_w + cx];
            if( *cell )
                continue;
            int64_t bx = (int64_t)cx << OUTLINE_CELL_SHIFT;
            int64_t bz = (int64_t)cz << OUTLINE_CELL_SHIFT;
            if( outline_tri_touches_box(tx, tz, bx, bz, bx + OUTLINE_CELL, bz + OUTLINE_CELL) )
                *cell = 1;
        }
}

/**
 * Greedy rectangles over the filled cells: grow right along the row, then down
 * while the whole span below is filled and unclaimed. Claimed cells are set to
 * 2. Returns the rectangle count; *io_rects grows as needed.
 */
static int
outline_rects(
    uint8_t* grid,
    int grid_w,
    int grid_h,
    struct OutlineRect** io_rects,
    int* io_cap)
{
    int count = 0;
    for( int z = 0; z < grid_h; ++z )
        for( int x = 0; x < grid_w; ++x )
        {
            if( grid[z * grid_w + x] != 1 )
                continue;
            int w = 1;
            while( x + w < grid_w && grid[z * grid_w + x + w] == 1 )
                ++w;
            int h = 1;
            for( ; z + h < grid_h; ++h )
            {
                bool full = true;
                for( int i = 0; i < w && full; ++i )
                    full = grid[(z + h) * grid_w + x + i] == 1;
                if( !full )
                    break;
            }
            for( int j = 0; j < h; ++j )
                memset(&grid[(z + j) * grid_w + x], 2, (size_t)w);
            if( count == *io_cap )
            {
                *io_cap = *io_cap ? *io_cap * 2 : 64;
                *io_rects = ToriMem_Realloc(
                    ToriMem_Scene(), *io_rects, (size_t)*io_cap * sizeof(**io_rects));
                assert(*io_rects);
            }
            (*io_rects)[count++] = (struct OutlineRect){ x, z, w, h };
        }
    return count;
}

/**
 * The deck's outline as a model centred on (*out_x, *out_z) at height *out_y,
 * all in the deck world's fine units; NULL when nothing on it is visible.
 */
/** What one build read and made, for TORIRS_WEV_DEBUG. */
struct OutlineStats
{
    int elements;
    int faces;
    int grid_w;
    int grid_h;
    int cells;
    int rects;
};

static struct ToriDraw_Model*
outline_build_model(
    struct ToriDraw_Scene* scene,
    struct World* deck,
    int hsl,
    int* out_x,
    int* out_y,
    int* out_z,
    struct OutlineStats* stats)
{
    struct World_EntityPool* pools[2] = { &deck->entities.terrain, &deck->entities.scenery };
    int min_x = INT_MAX, max_x = INT_MIN, min_z = INT_MAX, max_z = INT_MIN, low_y = INT_MIN;
    int sources = 0;

    assert(scene);
    assert(deck);

    /* Pass 1: the extent of every visible face, and the bottom of the stack
     * (Y grows downwards). The patch sits at the bottom: squashed, the live
     * deck's faces span its whole height, and a full hull overlapping it must
     * stay in front of all of them, its own low deck floor included. */
    for( int p = 0; p < 2; ++p )
        for( int i = World_EntityPoolHead(pools[p]); i != WORLD_ENTITY_NIL;
             i = World_EntityPoolNext(pools[p], i) )
        {
            /* Both records lead with element_id. */
            int element_id = p == 0
                ? ((struct WorldEntity_Terrain*)World_EntityPoolGet(pools[p], i))->element_id
                : ((struct WorldEntity_Scenery*)World_EntityPoolGet(pools[p], i))->element_id;
            struct OutlineSource s;
            if( !outline_source(scene, element_id, &s) )
                continue;
            ++stats->elements;
            for( int f = 0; f < s.face_count; ++f )
            {
                if( !outline_face_visible(&s, f) )
                    continue;
                int const v[3] = { s.fa[f], s.fb[f], s.fc[f] };
                for( int k = 0; k < 3; ++k )
                {
                    int x, z;
                    assert(v[k] >= 0 && v[k] < s.vertex_count);
                    outline_point(&s, v[k], &x, &z);
                    if( x < min_x ) min_x = x;
                    if( x > max_x ) max_x = x;
                    if( z < min_z ) min_z = z;
                    if( z > max_z ) max_z = z;
                    if( s.vy[v[k]] + s.pos_y > low_y )
                        low_y = s.vy[v[k]] + s.pos_y;
                }
                ++sources;
            }
        }
    stats->faces = sources;
    if( sources == 0 )
        return NULL;

    /* Pass 2: the grid. */
    min_x &= ~(OUTLINE_CELL - 1);
    min_z &= ~(OUTLINE_CELL - 1);
    int grid_w = ((max_x - min_x) >> OUTLINE_CELL_SHIFT) + 1;
    int grid_h = ((max_z - min_z) >> OUTLINE_CELL_SHIFT) + 1;
    uint8_t* grid = ToriMem_Calloc(ToriMem_Scene(), (size_t)grid_w * (size_t)grid_h, 1);
    assert(grid);
    for( int p = 0; p < 2; ++p )
        for( int i = World_EntityPoolHead(pools[p]); i != WORLD_ENTITY_NIL;
             i = World_EntityPoolNext(pools[p], i) )
        {
            int element_id = p == 0
                ? ((struct WorldEntity_Terrain*)World_EntityPoolGet(pools[p], i))->element_id
                : ((struct WorldEntity_Scenery*)World_EntityPoolGet(pools[p], i))->element_id;
            struct OutlineSource s;
            if( !outline_source(scene, element_id, &s) )
                continue;
            for( int f = 0; f < s.face_count; ++f )
                if( outline_face_visible(&s, f) )
                    outline_fill_face(&s, f, grid, grid_w, grid_h, min_x, min_z);
        }

    struct OutlineRect* rects = NULL;
    int cap = 0;
    stats->grid_w = grid_w;
    stats->grid_h = grid_h;
    for( int c = 0; c < grid_w * grid_h; ++c )
        stats->cells += grid[c] != 0;
    int count = outline_rects(grid, grid_w, grid_h, &rects, &cap);
    stats->rects = count;
    ToriMem_Free(ToriMem_Scene(), grid);
    if( count == 0 )
    {
        ToriMem_Free(ToriMem_Scene(), rects);
        return NULL;
    }

    /* Centred, so the bounds cylinder is tight and the element's position is
     * the middle of the patch. */
    int origin_x = min_x + ((grid_w << OUTLINE_CELL_SHIFT) >> 1);
    int origin_z = min_z + ((grid_h << OUTLINE_CELL_SHIFT) >> 1);
    int vertex_count = count * 4;
    int face_count = count * 2;
    assert(vertex_count <= INT16_MAX);
    struct ToriDraw_Model* model = ToriDraw_ModelNew(vertex_count, face_count, 0);
    assert(model);
    /* lib:toridraw: the model's arrays; ToriDraw_ModelFree frees them. */
    model->vertices_x = TORIDRAW_CALLOC((size_t)vertex_count, sizeof(*model->vertices_x));
    model->vertices_y = TORIDRAW_CALLOC((size_t)vertex_count, sizeof(*model->vertices_y));
    model->vertices_z = TORIDRAW_CALLOC((size_t)vertex_count, sizeof(*model->vertices_z));
    model->face_indices_a = TORIDRAW_CALLOC((size_t)face_count, sizeof(*model->face_indices_a));
    model->face_indices_b = TORIDRAW_CALLOC((size_t)face_count, sizeof(*model->face_indices_b));
    model->face_indices_c = TORIDRAW_CALLOC((size_t)face_count, sizeof(*model->face_indices_c));
    model->face_colors_a = TORIDRAW_CALLOC((size_t)face_count, sizeof(*model->face_colors_a));
    model->face_colors_b = TORIDRAW_CALLOC((size_t)face_count, sizeof(*model->face_colors_b));
    model->face_colors_c = TORIDRAW_CALLOC((size_t)face_count, sizeof(*model->face_colors_c));
    assert(model->vertices_x);
    assert(model->vertices_y);
    assert(model->vertices_z);
    assert(model->face_indices_a);
    assert(model->face_indices_b);
    assert(model->face_indices_c);
    assert(model->face_colors_a);
    assert(model->face_colors_b);
    assert(model->face_colors_c);
    for( int r = 0; r < count; ++r )
    {
        int x0 = min_x + (rects[r].x << OUTLINE_CELL_SHIFT) - origin_x;
        int z0 = min_z + (rects[r].z << OUTLINE_CELL_SHIFT) - origin_z;
        int x1 = x0 + (rects[r].w << OUTLINE_CELL_SHIFT);
        int z1 = z0 + (rects[r].h << OUTLINE_CELL_SHIFT);
        int const qx[4] = { x0, x1, x1, x0 };
        int const qz[4] = { z0, z0, z1, z1 };
        int v = r * 4;
        for( int k = 0; k < 4; ++k )
        {
            model->vertices_x[v + k] = (vertexint_t)qx[k];
            model->vertices_z[v + k] = (vertexint_t)qz[k];
        }
        /* Wound to face the sky, the way the camera always looks at it. */
        int const tri[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };
        for( int t = 0; t < 2; ++t )
        {
            int f = r * 2 + t;
            model->face_indices_a[f] = (faceint_t)(v + tri[t][0]);
            model->face_indices_b[f] = (faceint_t)(v + tri[t][1]);
            model->face_indices_c[f] = (faceint_t)(v + tri[t][2]);
            model->face_colors_a[f] = (hsl16_t)hsl;
            model->face_colors_b[f] = (hsl16_t)hsl;
            model->face_colors_c[f] = TORIDRAWHSL16_FLAT;
        }
    }
    ToriMem_Free(ToriMem_Scene(), rects);
    ToriDraw_ModelSetBoundsCylinder(model);

    *out_x = origin_x;
    *out_y = low_y;
    *out_z = origin_z;
    return model;
}

static inline uint64_t
outline_mix(
    uint64_t h,
    uint64_t v)
{
    /* FNV-1a over a whole word at a time; collisions only cost a stale patch
     * until the next real change. */
    return (h ^ v) * 0x100000001b3ull;
}

static uint64_t
outline_model_shape(
    struct ToriDraw_Scene* scene,
    int element_id)
{
    struct OutlineSource s;
    if( !outline_source(scene, element_id, &s) )
        return 0;
    return ((uint64_t)(uint32_t)s.vertex_count << 32) | (uint32_t)s.face_count;
}

/**
 * What the outline depends on, read from the deck's records rather than their
 * faces: each terrain tile and each loc's identity, placement and model size.
 * A few dozen records a deck, so it is cheap enough to ask on every serial
 * bump; a pose is deliberately not in it.
 */
static uint64_t
outline_signature(
    struct ToriDraw_Scene* scene,
    struct World* deck)
{
    struct World_EntityPool* pool;
    uint64_t h = 0xcbf29ce484222325ull;

    assert(scene);
    assert(deck);
    pool = &deck->entities.terrain;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Terrain* t = World_EntityPoolGet(pool, i);
        h = outline_mix(h, (uint64_t)i);
        h = outline_mix(h, outline_model_shape(scene, t->element_id));
    }
    pool = &deck->entities.scenery;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Scenery* sc = World_EntityPoolGet(pool, i);
        h = outline_mix(h, (uint64_t)(uint32_t)sc->loc_id);
        h = outline_mix(
            h,
            ((uint64_t)(uint32_t)sc->grid_position.x << 32) |
                ((uint64_t)(uint32_t)sc->grid_position.z << 8) |
                (uint64_t)(uint32_t)sc->grid_position.level);
        h = outline_mix(h, ((uint64_t)(uint32_t)sc->shape << 32) | (uint32_t)sc->angle);
        h = outline_mix(h, outline_model_shape(scene, sc->element_id));
    }
    return h;
}

static void
outline_drop(
    struct OutlineView* v,
    struct App* app)
{
    assert(v);
    assert(app);
    if( v->element_id >= 0 )
        ToriDraw_SceneElementRemove(app->scene, v->element_id);
    *v = (struct OutlineView){ .element_id = -1 };
}

static void
outline_ensure(
    struct OutlineView* v,
    struct App* app,
    struct Wev const* wev)
{
    struct Worldview* view;
    struct World* deck;
    int hsl;

    assert(v);
    assert(app);
    assert(wev);
    assert(wev->config);
    assert(WorldviewRegistry_IsLive(&app->worldviews, wev->id));

    view = WorldviewRegistry_Get(&app->worldviews, wev->id);
    deck = view->world;
    assert(deck);
    hsl = wev->config->flat_hsl;
    if( v->built && v->world == deck && v->hsl == hsl )
    {
        if( v->geometry_serial == deck->geometry_serial )
            return;
        uint64_t signature = outline_signature(app->scene, deck);
        v->geometry_serial = deck->geometry_serial;
        if( signature == v->signature )
            return;
    }

    outline_drop(v, app);
    v->built = true;
    v->world = deck;
    v->geometry_serial = deck->geometry_serial;
    v->signature = outline_signature(app->scene, deck);
    v->hsl = hsl;

    int x = 0, y = 0, z = 0;
    struct OutlineStats stats = { 0 };
    struct ToriDraw_Model* model = outline_build_model(app->scene, deck, hsl, &x, &y, &z, &stats);
    if( app_wev_debug_enabled() )
        fprintf(
            stderr,
            "wev: OUTLINE view %d serial %u: %d elements, %d faces -> %dx%d cells, %d filled, "
            "%d rects at %d,%d,%d\n",
            wev->id,
            (unsigned)deck->geometry_serial,
            stats.elements,
            stats.faces,
            stats.grid_w,
            stats.grid_h,
            stats.cells,
            stats.rects,
            x,
            y,
            z);
    if( !model )
        return;
    /* Root-dynamic like the heading arrows: no view's rebuild may sweep it,
     * and the root's rebuild is told it is claimed (outline_claim_elements). */
    v->element_id = ToriDraw_SceneElementAddPool(app->scene, TORIDRAW_SCENE_POOL_DYNAMIC);
    assert(v->element_id >= 0);
    ToriDraw_SceneElementSetModel(app->scene, v->element_id, ToriDraw_ModelHandleOwned(model));
    /* Deck-local: the frame composes the hull's transform around it, the
     * 0.01 Y scale included. */
    ToriDraw_SceneElementSetPosition(app->scene, v->element_id, x, y, z, 0);
}

static void*
outline_create(void)
{
    struct Outline* outline = ToriMem_Calloc(ToriMem_Scene(), 1, sizeof(*outline));
    assert(outline);
    for( int i = 0; i < WORLDVIEW_MAX; ++i )
        outline->views[i].element_id = -1;
    return outline;
}

static void
outline_destroy(
    void* state,
    struct App* app)
{
    struct Outline* outline = state;
    if( !outline )
        return;
    assert(app);
    for( int i = 0; i < WORLDVIEW_MAX; ++i )
        outline_drop(&outline->views[i], app);
    ToriMem_Free(ToriMem_Scene(), outline);
}

static void
outline_bind_painter(
    void* state,
    struct App* app,
    struct Wev const* wev,
    struct Painter* parent_painter,
    int camera_sx,
    int camera_sz)
{
    struct Outline* outline = state;
    struct OutlineView* v;

    (void)camera_sx;
    (void)camera_sz;
    assert(outline);
    assert(app);
    assert(wev);
    assert(parent_painter);
    assert(wev->id > 0 && wev->id < WORLDVIEW_MAX);

    v = &outline->views[wev->id];
    outline_ensure(v, app, wev);
    if( v->element_id >= 0 )
        painter_set_world_entity_element(parent_painter, wev->id, v->element_id);
}

static void
outline_bind_frame(
    void* state,
    struct App* app,
    struct Wev const* wev,
    struct ToriRS_FrameViewXform* xf)
{
    (void)state;
    (void)app;
    (void)wev;
    assert(wev);
    assert(xf);
    /* The live strategy's placement, so the patch sits where the squashed
     * deck did; the colour is already in the model, so no copy is made. */
    xf->flatten_scale = 0.01f;
    xf->flatten_y_offset = -1200;
    xf->flat_hsl = -1;
    xf->flat = true;
}

static int
outline_claim_elements(
    void* state,
    int* out_element_ids,
    int max)
{
    struct Outline* outline = state;
    int n = 0;

    assert(outline);
    assert(out_element_ids);
    assert(max >= 0);
    for( int i = 0; i < WORLDVIEW_MAX && n < max; ++i )
        if( outline->views[i].element_id >= 0 )
            out_element_ids[n++] = outline->views[i].element_id;
    return n;
}

static void
outline_release_view(
    void* state,
    struct App* app,
    int view_id)
{
    struct Outline* outline = state;

    assert(outline);
    assert(app);
    assert(view_id > 0 && view_id < WORLDVIEW_MAX);
    outline_drop(&outline->views[view_id], app);
}

struct AppWevFlatOps const g_app_wev_flat_outline = {
    .name = "outline",
    .create = outline_create,
    .destroy = outline_destroy,
    .bind_painter = outline_bind_painter,
    .bind_frame = outline_bind_frame,
    .claim_elements = outline_claim_elements,
    .release_view = outline_release_view,
};
