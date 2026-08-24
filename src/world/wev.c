#include "wev.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/*
 * World entities (sailing) — config decode, corner bake, target queue and the
 * class458 interpolator. See wev.h for the shapes and docs/SAILING.md §5.
 */

const int WEV_FOOTPRINT_MARGIN[WEV_FOOTPRINT_MARGINS] = { 256, 334, 362 };

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ */
/* Config decode                                                       */
/* ------------------------------------------------------------------ */

/*
 * A private cursor rather than RSCache_Buffer: this file is linked into
 * decode-only tests and the interpolator lives beside it, so it stays free of
 * cache-library types. Reads past the end latch `err` and return 0; the
 * decoder stops and `_consumed` stays short, which the loader warns about.
 */
struct WevCur
{
    uint8_t const* data;
    int size;
    int pos;
    int err;
};

static int
wc_g1(struct WevCur* c)
{
    if( c->err || c->pos + 1 > c->size )
    {
        c->err = 1;
        return 0;
    }
    return c->data[c->pos++];
}

static int
wc_g2(struct WevCur* c)
{
    if( c->err || c->pos + 2 > c->size )
    {
        c->err = 1;
        return 0;
    }
    c->pos += 2;
    return (c->data[c->pos - 2] << 8) | c->data[c->pos - 1];
}

/** Signed u16 read: the pivot and bounds ops carry negatives (0xffc0 = -64,
 * 0xff00 = -256 in cache.osrs239). */
static int
wc_g2s(struct WevCur* c)
{
    return (int16_t)wc_g2(c);
}

/** NUL-terminated string, heap-allocated. NULL on a missing terminator. */
static char*
wc_gstr(struct WevCur* c)
{
    int start = c->pos;
    int len = 0;
    char* out;

    if( c->err )
        return NULL;
    while( c->pos < c->size && c->data[c->pos] != 0 )
        c->pos++;
    if( c->pos >= c->size )
    {
        c->err = 1;
        return NULL;
    }
    len = c->pos - start;
    c->pos++; /* the terminator */
    out = malloc((size_t)len + 1);
    assert(out);
    memcpy(out, c->data + start, (size_t)len);
    out[len] = '\0';
    return out;
}

void
WevConfig_Init(
    struct WevConfig* config,
    int id)
{
    assert(config);
    memset(config, 0, sizeof(*config));
    config->id = id;
    /* -1 for the ids that have a real 0 (category 0 could exist); click mode
     * 2 is the documented default ("interactive contents win", SAILING.md
     * §5.4); the flat HSL default applies whenever op 27 is absent, which in
     * cache.osrs239 is always. */
    config->category = -1;
    config->click_mode = 2;
    config->anim_id = -1;
    config->op26 = -1;
    config->flat_hsl = WEV_FLAT_HSL_DEFAULT;
}

/**
 * Bake the 16-orientation footprint corner tables (deob class575/class556):
 * the bounds box, inflated by each margin, rotated into each of the 16 yaw
 * buckets (bucket * 128 angle units of 2048). Load-time only, so double trig
 * rather than a fixed-point table.
 */
static void
wev_config_bake_corners(struct WevConfig* config)
{
    for( int m = 0; m < WEV_FOOTPRINT_MARGINS; m++ )
    {
        int hx = abs(config->bounds_w) / 2 + WEV_FOOTPRINT_MARGIN[m];
        int hz = abs(config->bounds_h) / 2 + WEV_FOOTPRINT_MARGIN[m];
        /* Corner order pre-rotation: (-x,-z), (+x,-z), (+x,+z), (-x,+z). */
        int const bx[4] = { -hx, hx, hx, -hx };
        int const bz[4] = { -hz, -hz, hz, hz };

        for( int o = 0; o < WEV_ORIENTATIONS; o++ )
        {
            double theta = (double)(o * 128) * (2.0 * M_PI / 2048.0);
            double s = sin(theta);
            double co = cos(theta);

            for( int k = 0; k < 4; k++ )
            {
                double cx = config->bounds_off_x + bx[k];
                double cz = config->bounds_off_z + bz[k];

                config->corner_x[m][o][k] = (int)lround(cx * co - cz * s);
                config->corner_z[m][o][k] = (int)lround(cx * s + cz * co);
            }
        }
    }
}

int
WevConfig_Decode(
    struct WevConfig* config,
    int id,
    uint8_t const* data,
    int size)
{
    struct WevCur c;
    int terminated = 0;

    assert(config);
    assert(data);
    assert(size > 0);

    WevConfig_Init(config, id);
    c.data = data;
    c.size = size;
    c.pos = 0;
    c.err = 0;

    /*
     * Opcode table per SAILING.md §5.4 with three corrections verified
     * against cache.osrs239's bytes (see wev.h): op 14 is a parameterless
     * flag and the right-click strings are 15..19; ops 24 (u8) and 26 (u16)
     * are undocumented but present throughout the live data.
     */
    for( ;; )
    {
        int opcode;

        if( c.pos >= c.size )
            break;
        opcode = wc_g1(&c);
        if( opcode == 0 )
        {
            terminated = 1;
            break;
        }
        switch( opcode )
        {
        case 2:
            config->plane = wc_g1(&c);
            break;
        case 4:
            config->pivot_x = wc_g2s(&c);
            break;
        case 5:
            config->pivot_z = wc_g2s(&c);
            break;
        case 6:
            config->bounds_w = wc_g2s(&c);
            break;
        case 7:
            config->bounds_h = wc_g2s(&c);
            break;
        case 8:
            config->bounds_off_x = wc_g2s(&c);
            break;
        case 9:
            config->bounds_off_z = wc_g2s(&c);
            break;
        case 12:
            free(config->name);
            config->name = wc_gstr(&c);
            break;
        case 14:
            config->flag14 = true;
            break;
        case 15:
        case 16:
        case 17:
        case 18:
        case 19:
            free(config->ops[opcode - 15]);
            config->ops[opcode - 15] = wc_gstr(&c);
            break;
        case 20:
            config->category = wc_g2(&c);
            break;
        case 23:
            config->click_mode = wc_g1(&c);
            break;
        case 24:
            config->op24 = wc_g1(&c);
            break;
        case 25:
            /* u16 read; every live value (13424..13428) is < 32768 so this
             * cannot be told apart from a bigsmart — see wev.h. */
            config->anim_id = wc_g2(&c);
            break;
        case 26:
            config->op26 = wc_g2(&c);
            break;
        case 27:
            /* Absent from every entry of cache.osrs239; the u16 size is the
             * doc's, unverified by data. Default 39188 stands otherwise. */
            config->flat_hsl = wc_g2(&c);
            break;
        default:
            /* Unknown opcode: stop rather than guess a width — a wrong guess
             * misparses the rest of the record with no other symptom. The
             * short `_consumed` is the loader's cue.
             *
             * The id is cleared with it. Everything downstream tests presence
             * through WevConfigTable_Has, so a half-decoded record that keeps
             * its id reads as a real boat built from default dimensions —
             * the failure would surface as a wrongly-sized hull, far from
             * here. Absent is the honest answer. */
            config->_consumed = c.pos - 1;
            config->id = -1;
            return 0;
        }
        if( c.err )
        {
            config->_consumed = c.pos;
            config->id = -1;
            return 0;
        }
    }

    config->_consumed = c.pos;
    if( !terminated || c.pos != size )
    {
        /* Ran off the end with no opcode-0 terminator, or stopped short of
         * it. Same reasoning as the unknown-opcode path: report absent. */
        config->id = -1;
        return 0;
    }
    wev_config_bake_corners(config);
    return 1;
}

void
WevConfig_FreeContents(struct WevConfig* config)
{
    assert(config);
    free(config->name);
    config->name = NULL;
    for( int i = 0; i < WEV_CONFIG_OPS; i++ )
    {
        free(config->ops[i]);
        config->ops[i] = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* Config table                                                        */
/* ------------------------------------------------------------------ */

void
WevConfigTable_Init(struct WevConfigTable* table)
{
    assert(table);
    table->entries = NULL;
    table->count = 0;
}

void
WevConfigTable_Set(
    struct WevConfigTable* table,
    struct WevConfig* entries,
    int count)
{
    assert(table);
    assert(entries);
    assert(count > 0);
    assert(!table->entries);
    table->entries = entries;
    table->count = count;
}

void
WevConfigTable_Free(struct WevConfigTable* table)
{
    if( !table )
        return;
    for( int i = 0; i < table->count; i++ )
    {
        if( table->entries[i].id >= 0 )
            WevConfig_FreeContents(&table->entries[i]);
    }
    free(table->entries);
    table->entries = NULL;
    table->count = 0;
}

bool
WevConfigTable_Has(
    struct WevConfigTable const* table,
    int id)
{
    assert(table);
    assert(id >= 0);
    return id < table->count && table->entries[id].id >= 0;
}

struct WevConfig const*
WevConfigTable_Get(
    struct WevConfigTable const* table,
    int id)
{
    assert(table);
    assert(id >= 0);
    assert(id < table->count);
    assert(table->entries[id].id >= 0);
    return &table->entries[id];
}

/* ------------------------------------------------------------------ */
/* Wev registry                                                        */
/* ------------------------------------------------------------------ */

void
Wevs_Init(struct Wevs* wevs)
{
    assert(wevs);
    memset(wevs, 0, sizeof(*wevs));
    for( int i = 0; i < WORLDVIEW_MAX; i++ )
    {
        wevs->wevs[i].id = i;
        wevs->wevs[i].parent_view_id = WORLDVIEW_PARENT_NONE;
    }
}

bool
Wevs_IsLive(
    struct Wevs const* wevs,
    int id)
{
    assert(wevs);
    assert(id >= 0);
    assert(id < WORLDVIEW_MAX);
    return wevs->wevs[id].live;
}

struct Wev*
Wevs_Get(
    struct Wevs* wevs,
    int id)
{
    assert(wevs);
    assert(id >= 0);
    assert(id < WORLDVIEW_MAX);
    assert(wevs->wevs[id].live);
    return &wevs->wevs[id];
}

struct Wev*
Wevs_Spawn(
    struct Wevs* wevs,
    int id,
    int parent_view_id,
    struct WevConfig const* config,
    int config_id,
    int x,
    int z,
    int angle,
    int priority_group,
    unsigned op_mask)
{
    struct Wev* wev;

    assert(wevs);
    /* Id 0 is the root view; a world entity can never claim it. */
    assert(id > WORLDVIEW_ROOT);
    assert(id < WORLDVIEW_MAX);
    assert(parent_view_id >= 0);
    assert(parent_view_id < WORLDVIEW_MAX);
    assert(parent_view_id != id);
    assert(config);
    wev = &wevs->wevs[id];
    assert(!wev->live);
    assert(wevs->lists[parent_view_id].count < WORLDVIEW_MAX);

    wev->live = true;
    wev->id = id;
    wev->view_id = id;
    wev->parent_view_id = parent_view_id;
    wev->config = config;
    wev->config_id = config_id;
    wev->x = x;
    wev->y = 0;
    wev->z = z;
    wev->angle = angle & 0x7FF;
    /* Slot 0 is the resting target from the first frame on, so a delta that
     * arrives before any movement has a reference to chain off. */
    wev->queue[0].x = wev->x;
    wev->queue[0].z = wev->z;
    wev->queue[0].angle = wev->angle;
    wev->queue[0].enqueue_cycle = 0.0;
    wev->queue_count = 0;
    wev->interp_armed = false;
    wev->priority_group = priority_group;
    wev->op_mask = op_mask;
    wev->seq_id = -1;
    wev->seq_delay = 0;

    wevs->lists[parent_view_id].ids[wevs->lists[parent_view_id].count++] = id;
    return wev;
}

void
Wevs_Despawn(
    struct Wevs* wevs,
    int id)
{
    struct Wev* wev;
    int parent;
    int found = -1;

    assert(wevs);
    assert(id > WORLDVIEW_ROOT);
    assert(id < WORLDVIEW_MAX);
    wev = &wevs->wevs[id];
    assert(wev->live);
    /* A view that still hosts nested entities cannot go: the server despawns
     * children before their carrier, and a packet that does otherwise has
     * orphaned a live view. */
    assert(wevs->lists[id].count == 0);

    parent = wev->parent_view_id;
    for( int i = 0; i < wevs->lists[parent].count; i++ )
    {
        if( wevs->lists[parent].ids[i] == id )
        {
            found = i;
            break;
        }
    }
    assert(found >= 0);
    for( int i = found; i < wevs->lists[parent].count - 1; i++ )
        wevs->lists[parent].ids[i] = wevs->lists[parent].ids[i + 1];
    wevs->lists[parent].count--;

    memset(wev, 0, sizeof(*wev));
    wev->id = id;
    wev->parent_view_id = WORLDVIEW_PARENT_NONE;
}

int
Wevs_ViewListCount(
    struct Wevs const* wevs,
    int view_id)
{
    assert(wevs);
    assert(view_id >= 0);
    assert(view_id < WORLDVIEW_MAX);
    return wevs->lists[view_id].count;
}

struct Wev*
Wevs_ViewListAt(
    struct Wevs* wevs,
    int view_id,
    int index)
{
    assert(wevs);
    assert(view_id >= 0);
    assert(view_id < WORLDVIEW_MAX);
    assert(index >= 0);
    assert(index < wevs->lists[view_id].count);
    return &wevs->wevs[wevs->lists[view_id].ids[index]];
}

/* ------------------------------------------------------------------ */
/* Interpolator (deob class458)                                        */
/* ------------------------------------------------------------------ */

void
Wev_ApplyMove(
    struct Wev* wev,
    int dx,
    int dy,
    int dz,
    int dangle,
    bool snap,
    double cycle)
{
    int ref_x;
    int ref_z;
    int ref_angle;

    assert(wev);
    assert(wev->live);
    (void)dy; /* height is terrain-driven, never wire-driven */

    /* Deltas chain off the current target — where the entity will be once the
     * queue drains — not the mid-lerp transform. Slot 0 always holds it. */
    ref_x = wev->queue[0].x;
    ref_z = wev->queue[0].z;
    ref_angle = wev->queue[0].angle;

    if( snap )
    {
        /* Op 3: teleport. The queue is abandoned and the transform lands
         * immediately; the next enqueue chains off here. */
        wev->queue_count = 0;
        wev->interp_armed = false;
        wev->queue[0].x = ref_x + dx;
        wev->queue[0].z = ref_z + dz;
        wev->queue[0].angle = (ref_angle + dangle) & 0x7FF;
        wev->queue[0].enqueue_cycle = cycle;
        wev->x = wev->queue[0].x;
        wev->z = wev->queue[0].z;
        wev->angle = wev->queue[0].angle;
        return;
    }

    /*
     * Op 2: enqueue at slot 0 (newest), one segment per game tick.
     *
     * A tenth pending segment is dropped, not asserted: the pending count
     * saturates at WEV_TARGET_PENDING_MAX and the rotate then pushes the
     * oldest entry off the end of the ring (deob class467.method10446, which
     * caps `field5691` the same way). Nine pending is already 5.4 s of
     * unplayed movement, so a server that overruns it is lagging, not
     * malformed — and the destination survives regardless, because the
     * interpolator chases slot 0.
     */
    if( wev->queue_count < WEV_TARGET_PENDING_MAX )
        wev->queue_count++;
    for( int i = wev->queue_count; i > 0; i-- )
        wev->queue[i] = wev->queue[i - 1];
    wev->queue[0].x = ref_x + dx;
    wev->queue[0].z = ref_z + dz;
    wev->queue[0].angle = (ref_angle + dangle) & 0x7FF;
    wev->queue[0].enqueue_cycle = cycle;
}

/*
 * One step of the armed segment (deob class458.method10198). Writes the
 * entity transform and returns true when the window is spent.
 *
 * `method10181`'s lerp clamps the fraction into [0,1] before applying it, so
 * a completed step lands exactly on the target — which is why the deob's
 * extra "settle the previous segment at cycle - 1" call before re-arming is
 * a no-op here and is not reproduced. On a continuous clock it would not be
 * one: evaluating a finished segment a whole cycle in the past can only drag
 * the hull backwards.
 */
static bool
wev_interp_step(
    struct Wev* wev,
    double now_cycle)
{
    double f;
    int d;

    if( wev->interp_start_cycle >= wev->interp_end_cycle )
    {
        /* Window already spent when it was armed — land, do not divide. */
        wev->x = wev->interp_to_x;
        wev->z = wev->interp_to_z;
        wev->angle = wev->interp_to_angle;
        return true;
    }

    f = (now_cycle - wev->interp_start_cycle) /
        (wev->interp_end_cycle - wev->interp_start_cycle);
    d = (wev->interp_to_angle - wev->interp_from_angle) & 0x7FF;
    if( d > 1024 )
        d -= 2048;
    if( f >= 1.0 )
    {
        wev->x = wev->interp_to_x;
        wev->z = wev->interp_to_z;
        wev->angle = wev->interp_to_angle;
        return true;
    }
    if( f < 0.0 )
        f = 0.0;
    wev->x =
        wev->interp_from_x + (int)((double)(wev->interp_to_x - wev->interp_from_x) * f);
    wev->z =
        wev->interp_from_z + (int)((double)(wev->interp_to_z - wev->interp_from_z) * f);
    wev->angle = (wev->interp_from_angle + (int)((double)d * f)) & 0x7FF;
    return false;
}

void
Wev_Interpolate(
    struct Wev* wev,
    double now_cycle)
{
    assert(wev);
    assert(wev->live);

    if( wev->queue_count == 0 )
    {
        /* Nothing owed: rest on the current target (deob method10499). */
        wev->x = wev->queue[0].x;
        wev->z = wev->queue[0].z;
        wev->angle = wev->queue[0].angle;
        wev->interp_armed = false;
        return;
    }

    if( !wev->interp_armed )
    {
        /* Arm from wherever the hull is toward the NEWEST target, over
         * [now - 1, target enqueue + 30] (deob class458.method10184). */
        wev->interp_from_x = wev->x;
        wev->interp_from_z = wev->z;
        wev->interp_from_angle = wev->angle;
        wev->interp_to_x = wev->queue[0].x;
        wev->interp_to_z = wev->queue[0].z;
        wev->interp_to_angle = wev->queue[0].angle;
        wev->interp_start_cycle = now_cycle - 1.0;
        wev->interp_end_cycle =
            wev->queue[0].enqueue_cycle + (double)WEV_INTERP_CYCLES;
        wev->interp_armed = true;
    }

    if( wev_interp_step(wev, now_cycle) )
    {
        /* One segment paid off. A backlog re-arms next frame against the same
         * slot-0 target with an already-expired window, so it drains without
         * replaying the stale path. */
        wev->queue_count--;
        wev->interp_armed = false;
    }
}

void
Wevs_Frame(
    struct Wevs* wevs,
    double frame_cycles,
    WevHeightFn height_fn,
    void* height_userdata)
{
    int worklist[WORLDVIEW_MAX];
    int head = 0;
    int tail = 0;

    assert(wevs);
    assert(height_fn);
    assert(frame_cycles >= 0.0);
    wevs->clock += frame_cycles;

    /* Iterative descent, root first (project requirement: no recursion).
     * Every entity reached pushes its own view, so nested boats interpolate
     * after their carrier; the worklist bound is the view cap itself. */
    worklist[tail++] = WORLDVIEW_ROOT;
    while( head < tail )
    {
        int view = worklist[head++];

        for( int i = 0; i < wevs->lists[view].count; i++ )
        {
            struct Wev* wev = &wevs->wevs[wevs->lists[view].ids[i]];

            assert(wev->live);
            Wev_Interpolate(wev, wevs->clock);
            /* Height is not interpolated: overwritten every frame from the
             * terrain of the view the hull floats in — the root for a boat,
             * the carrier's deck for a nested entity. */
            wev->y = height_fn(
                height_userdata,
                view,
                wev->x,
                wev->z,
                wev->config->plane);
            assert(tail < WORLDVIEW_MAX);
            worklist[tail++] = wev->view_id;
        }
    }
}
