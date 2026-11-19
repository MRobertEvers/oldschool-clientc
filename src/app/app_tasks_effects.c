/*
 * PARALLEL TASKS: PROJECTILES AND SPOT ANIMATIONS.
 *
 * What belongs here: a scripted or plain projectile's models and sequences
 * (SCRIPTEDPROJ_ADD / _CHANGE, PROJANIM*) -- the projectile exists at packet
 * time, keyed on its slot and serial, and its flight counts from arrival --
 * and a spot animation's type, model and sequence (MAP_ANIM, an entity's
 * attached graphics), keyed on its view tile or its entity and spawn
 * serial.
 *
 * Each task here has the one shape (game/rs_parallel_task.h,
 * app/app_parallel_task.h): it embeds struct RS_ParallelTask, carries its
 * own data copied from the packet, is added with App_ParallelTaskAdd to the
 * parallel queue (app->frame_runner) as an ordinary task, and its run
 * function reads top to bottom -- await, cancelled?, still wanted?, apply --
 * with its policies as `if`s in that body and no `switch` in it. Its free
 * calls App_ParallelTaskRelease. Its kinds come from
 * RS_PENDING_KINDS_EFFECTS_FIRST .. RS_PENDING_KINDS_EFFECTS_LAST
 * (game/rs_pending.h), defined in this file; what other units of the layer
 * call into this family is declared in this family's own section of
 * app/app_tasks.h.
 *
 * THE ROW EXISTS AT ONCE. A MAP_PROJANIM, SCRIPTEDPROJ_ADD or MAP_ANIM makes
 * its World row in the packet task, in the world of the view the packet names
 * (the active view at arrival), with no scene element: its flight clock, its
 * delay and its lifetime count from the packet, a later SCRIPTEDPROJ_CHANGE
 * finds it, and a second SCRIPTEDPROJ_ADD for its slot sees the slot taken.
 * Only the BODY -- the model, the sequence, and for a scripted projectile the
 * curve scripts -- waits, in one task per row (Task_ProjectileBody,
 * Task_MapAnimBody), keyed on the row's pool index and spawn serial. When it
 * lands it re-finds the row by that key, reads the spotanim from the cache at
 * that moment, and attaches the element where the row has got to; the
 * animation is bound from the row's own clock, so it is where it would have
 * been had the body been here at arrival. Before then the row draws nothing.
 *
 * The reference does the same: its projectiles and map anims are made at
 * once and drawn when their models exist (rev-241 deob Statics.java ~48258:
 * `field1698.method14318(new class56(...), slot)` at the packet, the model
 * fetched at draw).
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; nothing outside the layer may include app/app_internal.h.
 */

#include "app/app_internal.h"
#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2vm2_host.h"
#include "cs2vm2/cs2vm2_script.h"
#include "mem/torirs_mem.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- kinds ---------------------------------------------------------------- */

enum
{
    /* A projectile's body: key RS_PendingKey_Projectile(pool index, serial). */
    APP_TASK_PROJECTILE_BODY = RS_PENDING_KINDS_EFFECTS_FIRST,
    /* A map graphic's body: key RS_PendingKey_Id(pool index, serial). */
    APP_TASK_MAP_ANIM_BODY,
};

int
App_EffectsProjectileBodyKind(void)
{
    return APP_TASK_PROJECTILE_BODY;
}

int
App_EffectsMapAnimBodyKind(void)
{
    return APP_TASK_MAP_ANIM_BODY;
}

/* ---- narration ------------------------------------------------------------ */

/* TORIRS_EFFECTS_TRACE=1: every body's way out, in every build (an OPT build
 * compiles TORIRS_LOG away, and "the graphic never drew" is otherwise silent). */
static void
app_effects_trace(
    char const* fmt,
    ...)
{
    static int on = -1;
    va_list ap;

    if( on < 0 )
        on = getenv("TORIRS_EFFECTS_TRACE") != NULL;
    if( !on )
        return;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

/* ---- the ground a projectile leaves from ----------------------------------- */

/*
 * The ground under a ROOT-frame point, composed through a hull when one is
 * there: a point over a live boat's deck answers the DECK's heightmap plus
 * the hull's y and bob — the same composition the emit path and the overlay
 * anchors use — and root terrain otherwise. A projectile fired from (or at)
 * the deck must measure its height from the planking, not from the grass or
 * water the hull happens to be sitting on: the deob never faces the question
 * because its projectile lives in the boat's own world, where "ground" IS
 * the deck; this is that answer for the root-world approximation.
 */
static int
app_effects_ground_composed(
    struct App* app,
    int fine_x,
    int fine_z,
    int level)
{
    for( int view_id = 1; view_id < WORLDVIEW_MAX; view_id++ )
    {
        struct Wev* wev;
        struct WevDeckBox box;
        int deck_x;
        int deck_z;
        int deck_level;
        struct Worldview* view;

        if( !Wevs_IsLive(&app->wevs, view_id) ||
            !WorldviewRegistry_IsLive(&app->worldviews, view_id) )
            continue;
        wev = Wevs_Get(&app->wevs, view_id);
        if( wev->parent_view_id != WORLDVIEW_ROOT )
            continue;
        /* Membership is the HULL's footprint — the gunwale rule. The deck box
         * is the whole zone reservation and the templates carry walkable
         * staging ground around the parked hull, so a shore tile beside the
         * boat is inside the box but not on the planking; composing its
         * ground from the deck template would bend a shot aimed past the
         * rail. The footprint is the painter's own answer to "which root
         * tiles does this hull cover", so the two cannot disagree; a config
         * with no stated extent (the wire-sized Zenith) makes the footprint
         * the whole box, which is the right fallback too. */
        if( wev->config )
        {
            int min_tile_x;
            int min_tile_z;
            int size_x;
            int size_z;
            int abs_tile_x = (fine_x >> 7) + app->world->_base_tile_x;
            int abs_tile_z = (fine_z >> 7) + app->world->_base_tile_z;

            Wev_FootprintTiles(wev, 0, &min_tile_x, &min_tile_z, &size_x, &size_z);
            if( abs_tile_x < min_tile_x || abs_tile_x >= min_tile_x + size_x ||
                abs_tile_z < min_tile_z || abs_tile_z >= min_tile_z + size_z )
                continue;
        }
        app_wev_deck_box(app, wev, app->world, &box);
        Wev_DeckFromParent(&box, fine_x, fine_z, &deck_x, &deck_z);
        if( !Wev_DeckContainsDeckPoint(&box, deck_x, deck_z) )
            continue;
        view = WorldviewRegistry_Get(&app->worldviews, view_id);
        deck_level = app_wev_deck_level(app, view_id);
        if( deck_level >= COLLISION_LEVELS )
            deck_level = COLLISION_LEVELS - 1;
        return wev->y + wev->bob_y + World_HeightAt(view->world, deck_x, deck_z, deck_level);
    }
    return World_HeightAt(app->world, fine_x, fine_z, level);
}

/* The ground a projectile in `world` leaves from: composed through a hull in
 * the root world, the view's own ground in a deck's. */
static int
app_effects_ground(
    struct App* app,
    struct World* world,
    int fine_x,
    int fine_z,
    int level)
{
    assert(app);
    assert(world);
    if( world == app->world )
        return app_effects_ground_composed(app, fine_x, fine_z, level);
    return World_HeightAt(world, fine_x, fine_z, level);
}

/* ---- the curve VM ---------------------------------------------------------- */

/*
 * The curve VM's host. A curve clientscript is pure arithmetic: the only thing
 * it asks of a host is a GOSUB, and the body task made every script it can
 * call resident before it marked the curves ready (app_effects_curve_callees),
 * so a callee is never awaited here. Anything else is a curve script this
 * client cannot run synchronously.
 */
static int
app_curve_host_exec(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request)
{
    struct App* app = (struct App*)CS2VM_USER(thread);
    struct CS2VM2_Script* script;

    assert(app);
    assert(request);
    assert(request->kind == CS2VM_HOST_REQUEST_GOSUB_WITH_PARAMS);
    script = CacheProvider_ClientScriptGet(app->provider, request->u.GOSUB_WITH_PARAMS.script_id);
    assert(script);
    return CS2VM2_PushCallScript(thread, script);
}

/*
 * World_CurveFn: one curve clientscript run synchronously on `progress`
 * (deob class56.method1470 -- a script event of [script, progress], its first
 * int result read back). Resident by construction: the world runs a
 * projectile's curves only once its body task has loaded the script and its
 * callees (WorldEntity_Projectile.curves_ready).
 */
static int
app_world_curve(
    void* userdata,
    int script_id,
    int progress)
{
    struct App* app = (struct App*)userdata;
    struct CS2VM2_Script* script;
    struct CS2VM2_Thread* thread;
    struct CS2VM2_ThreadError err = { 0 };
    enum CS2VM2_ThreadStatus status;
    int value = 0;

    assert(app);
    script = CacheProvider_ClientScriptGet(app->provider, script_id);
    assert(script);
    if( !app->curve_vm )
    {
        app->curve_vm = CS2VM2_Acquire();
        assert(app->curve_vm);
        CS2VM2_BindHost(app->curve_vm, app, app_curve_host_exec);
    }
    thread = CS2VM2_ThreadMain(app->curve_vm);
    CS2VM2_ThreadStart(thread, script);
    CS2VM2_SetIntCurrentFrameLocal(thread, 0, progress);
    status = CS2VM2_ThreadRun(thread, &err);
    if( status != CS2VM2_THREAD_DONE || CS2VM2_PopInt(thread, &value) != CS2VM_EXECNO_OK )
    {
        TORIRS_ERR("curve script %d failed at opcode %d (script %d pc %d)\n",
            script_id, err.opcode, err.script_id, err.pc);
        return 0;
    }
    return value;
}

/* ---- a projectile's body --------------------------------------------------- */

enum
{
    APP_EFFECTS_CURVE_SCRIPTS_MAX = 8
};

struct Task_ProjectileBody
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_ParallelTask par;
    struct App* app;
    /* The row: its pool index and spawn serial in the view's world. */
    int idx;
    uint32_t serial;
    /* The graphic it draws; -1 for a model-only one (the developer hotkey),
     * which draws `model_id` animated by `seq_id`. */
    int spotanim_id;
    int model_id;
    int seq_id;
    /* A scripted projectile: the curve scripts, then whatever they GOSUB,
     * and the cursor of the one being loaded (both survive the awaits). */
    int scripted;
    int curve_scripts[APP_EFFECTS_CURVE_SCRIPTS_MAX];
    int curve_script_count;
    int curve_script_cursor;
};

/* Every script the curve scripts GOSUB, appended to the task's list (each
 * once): the world runs them synchronously, so nothing may be loaded late. */
static void
app_effects_curve_callees(
    struct Task_ProjectileBody* self,
    struct CS2VM2_Script const* script)
{
    assert(self);
    assert(script);
    for( int i = 0; i < script->op_count; i++ )
    {
        int callee;
        int seen = 0;

        if( script->opcodes[i] != CS2_OP_GOSUB_WITH_PARAMS )
            continue;
        callee = script->int_operands[i];
        for( int j = 0; j < self->curve_script_count; j++ )
            seen |= self->curve_scripts[j] == callee;
        if( seen )
            continue;
        assert(self->curve_script_count < APP_EFFECTS_CURVE_SCRIPTS_MAX);
        self->curve_scripts[self->curve_script_count++] = callee;
    }
}

/*
 * The body lands: dress the row, wherever its flight has got to. The model is
 * read from the cache now; the element starts where the projectile is (at its
 * start before launch) and app_world_sync_positions moves it from the next
 * frame on, binding the sequence at the launch cycle aged by the cycles flown
 * since -- the flight's own clock, which counts from the packet.
 */
static void
app_effects_projectile_dress(
    struct App* app,
    struct WorldEntity_Projectile* p,
    struct ToriDraw_Model* model,
    int seq_id,
    int scripted)
{
    int element_id;

    assert(app);
    assert(p);
    assert(model);
    assert(p->element_id < 0);
    if( p->launched )
        element_id = app_world_scene_element_create(
            app, TORIDRAW_ELEMENT_KIND_PROJECTILE, model, (int)p->x, (int)p->y, (int)p->z);
    else
        element_id = app_world_scene_element_create(
            app, TORIDRAW_ELEMENT_KIND_PROJECTILE, model, p->src_x, p->h1, p->src_z);
    if( element_id < 0 )
        return;
    p->element_id = element_id;
    p->unbound_seq = seq_id;
    if( scripted )
        p->curves_ready = true;
    /* Reference ClientProj.move wraps animFrame to 0 at the end of the frame
     * list and never drops the sequence. Flight time routinely outlasts the
     * sequence — a 12-cycle spotanim seq against a 30+ cycle flight is normal —
     * so without this the model spends most of the flight back in its un-posed
     * bind pose. Spotanim models hide geometry by scaling it to zero in every
     * frame, so what that actually looks like is extra parts of the model
     * appearing mid-air partway to the target. */
    ToriDraw_SceneElementSetAnimLoop(app->scene, element_id, true);
    if( torirs_env_net_debug() )
        app_effects_trace(
            "projectile body: element=%d spotanim=%d seq=%d cycle=%d t1=%d t2=%d "
            "launched=%d target=%d\n",
            element_id,
            p->spotanim_id,
            seq_id,
            p->cycle,
            p->t1,
            p->t2,
            p->launched ? 1 : 0,
            p->target);
    app_sync_textures(app);
    RedrawRequest_Raise(&app->redraw_request);
}

static int
Task_ProjectileBody_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_ProjectileBody* self = (struct Task_ProjectileBody*)base;
    struct App* app = self->app;

    PT_BEGIN(&self->pt);

    /* 1. The graphic: its type, model and sequence (a model-only one: the
     *    model, then the sequence). */
    if( self->spotanim_id >= 0 )
        PT_PARALLEL_AWAIT_ASSET(
            app->assets, AssetKey_Make(ASSET_KIND_SPOTANIM, self->spotanim_id, 0));
    else
        PT_PARALLEL_AWAIT_ASSET(app->assets, AssetKey_Make(ASSET_KIND_MODEL, self->model_id, 0));
    if( RS_ParallelTask_Cancelled(&self->par) )
        PT_EXIT(&self->pt);
    /* Failed or abandoned: the projectile flies its course unseen. */
    if( !RS_ParallelTask_AssetHere(&self->par) )
    {
        app_effects_trace("projectile body: spotanim %d model %d did not load\n",
            self->spotanim_id, self->model_id);
        PT_EXIT(&self->pt);
    }
    if( self->spotanim_id < 0 && self->seq_id >= 0 )
    {
        /* Bound at launch whether or not it came: a model with no frames is
         * drawn still. */
        PT_PARALLEL_AWAIT_ASSET(app->assets, AssetKey_Make(ASSET_KIND_SEQUENCE, self->seq_id, 0));
        if( RS_ParallelTask_Cancelled(&self->par) )
            PT_EXIT(&self->pt);
    }

    /* 2. A scripted one's curve scripts and, as each lands, the scripts it
     *    calls: the world runs them synchronously every cycle. */
    for( ; self->scripted && self->curve_script_cursor < self->curve_script_count;
         self->curve_script_cursor++ )
    {
        if( !CacheProvider_ClientScriptHas(
                app->provider, self->curve_scripts[self->curve_script_cursor]) )
            PT_TASK_AWAITSELF_IF(CreateTask_ClientScriptLoad(
                app->provider, self->curve_scripts[self->curve_script_cursor]));
        if( RS_ParallelTask_Cancelled(&self->par) )
            PT_EXIT(&self->pt);
        {
            struct CS2VM2_Script* script = CacheProvider_ClientScriptGet(
                app->provider, self->curve_scripts[self->curve_script_cursor]);
            /* A cache without the script cannot fly the shot: it stays at
             * its start, unseen, and ends at its end cycle. */
            if( !script )
            {
                TORIRS_ERR("projectile body: curve script %d not in the cache\n",
                    self->curve_scripts[self->curve_script_cursor]);
                PT_EXIT(&self->pt);
            }
            app_effects_curve_callees(self, script);
        }
    }

    /* 3. Still wanted: the scene it was made in, the row (not a later one in
     *    its slot), and a flight that is not over. */
    {
        struct World* world = App_ParallelTaskWorld(app, &self->par, "projectile body");
        struct WorldEntity_Projectile* p;
        struct ToriRS_Spotanimtype* spot;
        struct ToriDraw_Model* model;

        if( !world )
        {
            app_effects_trace("projectile body: spotanim %d: its scene is gone\n", self->spotanim_id);
            PT_EXIT(&self->pt);
        }
        p = World_ProjectileFind(world, self->idx, self->serial);
        if( !p )
        {
            app_effects_trace("projectile body: spotanim %d landed after its flight\n", self->spotanim_id);
            PT_EXIT(&self->pt);
        }
        if( p->cycle > p->t2 )
            PT_EXIT(&self->pt);

        /* 4. Apply, reading the type and the model from the cache now. */
        if( self->spotanim_id < 0 )
        {
            int model_ids[1] = { self->model_id };

            model = app_world_build_model(app, model_ids, 1, NULL, 128, 128, APP_LIGHT_ACTOR, 0, 0);
            if( !model )
            {
                TORIRS_ERR("projectile body: model %d failed to build\n", self->model_id);
                PT_EXIT(&self->pt);
            }
            app_effects_projectile_dress(app, p, model, self->seq_id, 0);
            PT_EXIT(&self->pt);
        }
        spot = CacheProvider_SpotanimtypeGet(app->provider, self->spotanim_id);
        if( !spot )
        {
            app_effects_trace("projectile body: spotanim %d not resident\n", self->spotanim_id);
            PT_EXIT(&self->pt);
        }
        model = app_world_build_spotanim_model(app, spot);
        if( !model )
        {
            TORIRS_ERR("projectile body: spotanim %d model %d failed\n", self->spotanim_id, spot->model);
            PT_EXIT(&self->pt);
        }
        app_effects_projectile_dress(app, p, model, spot->seq, self->scripted);
    }

    PT_END(&self->pt);
}

static void
Task_ProjectileBody_Free(struct ToriRS_Task* base)
{
    struct Task_ProjectileBody* self = (struct Task_ProjectileBody*)base;

    App_ParallelTaskRelease(self->app, &self->par);
    ToriMem_Free(ToriMem_Process(), self);
}

static struct ToriRS_TaskVTable Task_ProjectileBody_VTable = {
    .run = Task_ProjectileBody_Run,
    .free = Task_ProjectileBody_Free,
};

static struct Task_ProjectileBody*
app_effects_projectile_body_new(
    struct App* app,
    struct World const* world,
    int idx)
{
    struct Task_ProjectileBody* task;
    struct WorldEntity_Projectile const* p;

    assert(app);
    assert(world);
    p = World_EntityPoolGet(&((struct World*)world)->entities.projectile, idx);
    assert(p);
    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_ProjectileBody_VTable;
    strncpy(task->task.name, "ProjectileBody", sizeof(task->task.name) - 1);
    task->app = app;
    task->idx = idx;
    task->serial = p->serial;
    task->spotanim_id = p->spotanim_id;
    task->model_id = -1;
    task->seq_id = -1;
    PT_INIT(&task->pt);
    return task;
}

static void
app_effects_projectile_body_add(
    struct App* app,
    struct Task_ProjectileBody* task,
    int view)
{
    assert(app);
    assert(task);
    App_ParallelTaskAdd(
        app,
        &task->task,
        &task->par,
        APP_TASK_PROJECTILE_BODY,
        RS_PendingKey_Projectile(task->idx, task->serial),
        view);
}

/* ---- a map graphic's body -------------------------------------------------- */

struct Task_MapAnimBody
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_ParallelTask par;
    struct App* app;
    int idx;
    uint32_t serial;
    int spotanim_id;
};

/* The graphic will not get a body: take its row out, so a lifetime that was
 * never known does not keep it listed for ever. Only the row this task was
 * made for, and only while it is still bodiless. */
static void
app_effects_map_anim_drop(struct Task_MapAnimBody const* self)
{
    struct App* app = self->app;
    struct Worldview* wv;
    struct WorldEntity_Spotanim* s;

    if( !WorldviewRegistry_IsLive(&app->worldviews, self->par.view) )
        return;
    wv = WorldviewRegistry_Get(&app->worldviews, self->par.view);
    if( !wv->world )
        return;
    s = World_SpotanimFind(wv->world, self->idx, self->serial);
    if( s && s->element_id < 0 )
        World_SpotanimDespawn(wv->world, self->idx);
}

/*
 * The body lands. The row has counted its delay and its active cycles since
 * the packet; the sequence's length is its lifetime, and a graphic whose
 * whole life passed while its body was loading is not shown at all (the
 * reference's is gone by then too). One that is already showing is bound
 * aged by the cycles it has been active; one still in its delay is parked
 * (anim_external) until WorldEventKind_SpotanimStarted hands it back on the
 * cycle it first draws (App_WorldDrainEntityRemoved).
 */
static void
app_effects_map_anim_dress(
    struct App* app,
    struct World* world,
    int idx,
    struct WorldEntity_Spotanim* s,
    struct ToriRS_Spotanimtype const* spot)
{
    struct ToriDraw_Model* model;
    int element_id;
    int lifetime;

    assert(app);
    assert(world);
    assert(s);
    assert(spot);
    lifetime = WorldSeqSourceToriDraw_TotalDuration(&app->seq_source, spot->seq);
    if( lifetime < 0 )
        lifetime = 0;
    s->lifetime = lifetime;
    if( s->active && s->active_cycle >= lifetime )
    {
        app_effects_trace(
            "map anim body: id=%d expired %d cycles before its assets\n",
            s->spotanim_id,
            s->active_cycle - lifetime);
        World_SpotanimDespawn(world, idx);
        return;
    }
    model = app_world_build_spotanim_model(app, spot);
    if( !model )
    {
        TORIRS_ERR("map anim body: spotanim %d model %d failed\n", s->spotanim_id, spot->model);
        World_SpotanimDespawn(world, idx);
        return;
    }
    element_id = app_world_scene_element_create(
        app,
        TORIDRAW_ELEMENT_KIND_SPOTANIM,
        model,
        (int)s->draw_position.x,
        (int)s->draw_position.y,
        (int)s->draw_position.z);
    if( element_id < 0 )
    {
        World_SpotanimDespawn(world, idx);
        return;
    }
    s->element_id = element_id;
    if( s->active )
    {
        /* Showing since active_cycle cycles ago: the sequence started then. */
        app_world_apply_seq_at(
            app, element_id, spot->seq, (app->world ? app->world->cycle : 0) - s->active_cycle);
    }
    else
    {
        struct ToriDraw_SceneElement* el;

        app_world_apply_seq(app, element_id, spot->seq);
        /* A delayed spotanim is invisible until World flips it active, so its
         * sequence must not run in the meantime. Park it as anim_external — the
         * flag the naive per-element tick uses to mean "someone else owns this
         * element's frames" — and let WorldEventKind_SpotanimStarted hand it back
         * on the cycle it first draws. Without this the whole delay is spent
         * animating out of sight: `spotanim_map`'s delay is a projectile's flight
         * time, which is routinely longer than the sequence, so the splash
         * surfaced already finished and sat on a cleared final frame. */
        el = ToriDraw_SceneElementGet(app->scene, element_id);
        if( el )
        {
            el->anim_external = true;
            /* anim_list membership is filtered on anim_external, so the cached
             * list is now stale (toridraw_scene.h: the caller mutating this
             * flag directly owns the invalidation). */
            ToriDraw_SceneAnimListInvalidate(app->scene);
        }
    }
    app_effects_trace(
        "map anim body: id=%d element=%d tile=%d,%d level=%d model=%d seq=%d life=%d "
        "active=%d active_cycle=%d idle=%d\n",
        s->spotanim_id,
        element_id,
        (int)(s->draw_position.x >> 7),
        (int)(s->draw_position.z >> 7),
        s->level,
        spot->model,
        spot->seq,
        lifetime,
        s->active ? 1 : 0,
        s->active_cycle,
        s->idle_cycles);
    app_sync_textures(app);
    RedrawRequest_Raise(&app->redraw_request);
}

static int
Task_MapAnimBody_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_MapAnimBody* self = (struct Task_MapAnimBody*)base;
    struct App* app = self->app;

    (void)io;
    PT_BEGIN(&self->pt);

    /* 1. The graphic: its type, model and sequence. */
    PT_PARALLEL_AWAIT_ASSET(app->assets, AssetKey_Make(ASSET_KIND_SPOTANIM, self->spotanim_id, 0));

    /* 2. Cancelled (a logout): it will never get a body. */
    if( RS_ParallelTask_Cancelled(&self->par) )
    {
        app_effects_map_anim_drop(self);
        PT_EXIT(&self->pt);
    }
    /* Failed or abandoned: nothing will ever draw it. */
    if( !RS_ParallelTask_AssetHere(&self->par) )
    {
        app_effects_trace("map anim body: spotanim %d did not load\n", self->spotanim_id);
        app_effects_map_anim_drop(self);
        PT_EXIT(&self->pt);
    }

    /* 3. Still wanted: the scene it was made in (a rebuild cleared its row
     *    with it), and the row itself. */
    {
        struct World* world = App_ParallelTaskWorld(app, &self->par, "map anim body");
        struct WorldEntity_Spotanim* s;
        struct ToriRS_Spotanimtype* spot;

        if( !world )
        {
            app_effects_trace("map anim body: spotanim %d: its scene is gone\n", self->spotanim_id);
            PT_EXIT(&self->pt);
        }
        s = World_SpotanimFind(world, self->idx, self->serial);
        if( !s )
        {
            app_effects_trace("map anim body: spotanim %d: its row is gone\n", self->spotanim_id);
            PT_EXIT(&self->pt);
        }

        /* 4. Apply, reading the type from the cache now. */
        spot = CacheProvider_SpotanimtypeGet(app->provider, self->spotanim_id);
        if( !spot )
        {
            app_effects_trace("map anim body: spotanim %d not resident\n", self->spotanim_id);
            World_SpotanimDespawn(world, self->idx);
            PT_EXIT(&self->pt);
        }
        app_effects_map_anim_dress(app, world, self->idx, s, spot);
    }

    PT_END(&self->pt);
}

static void
Task_MapAnimBody_Free(struct ToriRS_Task* base)
{
    struct Task_MapAnimBody* self = (struct Task_MapAnimBody*)base;

    App_ParallelTaskRelease(self->app, &self->par);
    ToriMem_Free(ToriMem_Process(), self);
}

static struct ToriRS_TaskVTable Task_MapAnimBody_VTable = {
    .run = Task_MapAnimBody_Run,
    .free = Task_MapAnimBody_Free,
};

/* ---- the packet half ----------------------------------------------------- */

/* The world of the view the packet names: the active view at arrival. */
static struct World*
app_effects_view_world(
    struct App* app,
    int* out_view)
{
    struct World* world;

    assert(app);
    assert(out_view);
    *out_view = WorldviewRegistry_Active(&app->worldviews);
    world = App_ActiveWorldview(app)->world;
    assert(world);
    return world;
}

int
App_WorldSpotanimSpawn(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int spotanim_id,
    int height,
    int delay)
{
    struct World* world;
    struct WorldEntity_Spotanim* s;
    struct Task_MapAnimBody* task;
    int view;
    int world_x;
    int world_z;
    int world_y;
    int idx;

    assert(app);
    assert(spotanim_id >= 0);
    world = app_effects_view_world(app, &view);
    world_x = scene_x * 128 + 64;
    world_z = scene_z * 128 + 64;
    /* Reference y = getAvH(x,z) - height, on the view's own ground; world y
     * is negative-up so subtracting raises the effect above the ground. */
    world_y = World_HeightAt(world, world_x, world_z, level) - height;
    idx = World_SpotanimSpawn(
        world, -1, level, world_x, world_z, world_y, 0, delay, WORLD_SPOTANIM_LIFETIME_PENDING);
    s = World_EntityPoolGet(&world->entities.spotanim, idx);
    assert(s);
    /* Which graphic it is, for the quest driver's hazard readers
     * (WorldEntity_Spotanim.spotanim_id); World itself never reads it. */
    s->spotanim_id = spotanim_id;

    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_MapAnimBody_VTable;
    strncpy(task->task.name, "MapAnimBody", sizeof(task->task.name) - 1);
    task->app = app;
    task->idx = idx;
    task->serial = s->serial;
    task->spotanim_id = spotanim_id;
    PT_INIT(&task->pt);
    App_ParallelTaskAdd(
        app,
        &task->task,
        &task->par,
        APP_TASK_MAP_ANIM_BODY,
        RS_PendingKey_Id(idx, s->serial),
        view);
    return idx;
}

int
App_WorldProjectileSpawn(
    struct App* app,
    int src_x,
    int src_z,
    int src_level,
    int dst_x,
    int dst_z,
    int dst_level,
    int spotanim_id,
    int src_height,
    int dst_height,
    int start_delay,
    int end_delay,
    int peak,
    int arc,
    int target,
    int source)
{
    struct World* world;
    struct WorldEntity_Projectile* p;
    int view;
    int fine_src_x;
    int fine_src_z;
    int src_y;
    int idx;

    assert(app);
    assert(spotanim_id >= 0);
    world = app_effects_view_world(app, &view);
    fine_src_x = src_x * 128 + 64;
    fine_src_z = src_z * 128 + 64;
    /* World y is negative-up: reference y = getAvH(src) - h1, h1 = src_height*4.
     * In the root world the SOURCE ground composes through a hull when the
     * shooter stands on one — a bow fired from the deck leaves at deck + chest,
     * not at the terrain the hull is parked over (a beached hull's planking can
     * sit well below the bank beside it). The deob never faces this: its
     * projectile lives in the boat's world, where ground IS the deck, which is
     * what a projectile addressed to a deck's view does here. The DESTINATION
     * deliberately does NOT compose: the landing samples this world's own
     * ground exactly as the deob's `getAvH(dst, proj.plane) - h2` does, and
     * World re-aims a tracked target with that same rule every cycle. */
    src_y = app_effects_ground(app, world, fine_src_x, fine_src_z, src_level) - src_height * 4;

    /* peak -> angle, arc -> startpos, end_height = dst_height*4 (World computes
     * dst y as height_fn(dst) - end_height, matching getAvH(dst) - h2). `target`
     * goes through in its wire encoding so World re-aims the arc at that
     * entity's live position every cycle (reference addProjectiles). The clock
     * starts now, at the packet: the body lands into the flight wherever it
     * has got to. */
    idx = World_ProjectileSpawn(
        world,
        -1,
        src_level,
        dst_level,
        fine_src_x,
        fine_src_z,
        dst_x * 128 + 64,
        dst_z * 128 + 64,
        src_y,
        src_height * 4,
        dst_height * 4,
        start_delay,
        end_delay,
        peak,
        arc,
        target,
        source);
    p = World_EntityPoolGet(&world->entities.projectile, idx);
    assert(p);
    /* Which graphic it is, for the quest driver's hazard readers
     * (WorldEntity_Projectile.spotanim_id); World itself never reads it. */
    p->spotanim_id = spotanim_id;
    app_effects_projectile_body_add(app, app_effects_projectile_body_new(app, world, idx), view);
    return idx;
}

int
App_WorldScriptedProjSpawn(
    struct App* app,
    struct WorldEntity_ScriptedProjSpec const* sp)
{
    struct World* world;
    struct WorldEntity_Projectile* p;
    struct Task_ProjectileBody* task;
    int view;
    int src_x;
    int src_z;
    int src_y;
    int idx;

    assert(app);
    assert(sp);
    assert(sp->spotanim_id >= 0);
    world = app_effects_view_world(app, &view);
    /* One per slot (the reference's `if (var16 == null)`): the caller asked. */
    assert(World_ProjectileFindScriptedSlot(world, sp->slot) < 0);

    src_x = sp->src_x * 128 + 64;
    src_z = sp->src_z * 128 + 64;
    /* Raw: the deob subtracts the packet's height from the ground as it is. */
    src_y = app_effects_ground(app, world, src_x, src_z, sp->src_level) - sp->src_height;
    World_SetCurveFn(world, app_world_curve, app);
    idx = World_ProjectileSpawn(
        world,
        -1,
        sp->src_level,
        sp->dst_level,
        src_x,
        src_z,
        sp->dst_x * 128 + 64,
        sp->dst_z * 128 + 64,
        src_y,
        sp->src_height,
        sp->dst_height,
        sp->start_delay,
        sp->end_delay,
        0,
        0,
        sp->target,
        sp->source);
    World_ProjectileMakeScripted(
        world,
        idx,
        sp->curve[0],
        sp->curve[1],
        sp->curve[2],
        sp->src_offset_x,
        sp->src_offset_z,
        sp->dst_offset_x,
        sp->dst_offset_z,
        sp->slot);
    p = World_EntityPoolGet(&world->entities.projectile, idx);
    assert(p);
    p->spotanim_id = sp->spotanim_id;

    task = app_effects_projectile_body_new(app, world, idx);
    task->scripted = 1;
    for( int i = 0; i < 3; i++ )
        task->curve_scripts[i] = sp->curve[i];
    task->curve_script_count = 3;
    app_effects_projectile_body_add(app, task, view);
    return idx;
}

/*
 * Hotkey 0's shot (app/app_tool_projectile.c): `model_id` animated by
 * `seq_id`, source -> destination in scene tiles of the active view. Made at
 * once like a server's; its body lands as a server projectile's does. Arc
 * math lives in World_ProjectileSetTarget/Move. `target` is the wire
 * target-entity id when a synced NPC sits on the destination tile,
 * WORLD_PROJECTILE_TARGET_NONE for a plain tile shot.
 */
int
App_WorldModelProjectileSpawn(
    struct App* app,
    int model_id,
    int seq_id,
    int src_tile_x,
    int src_tile_z,
    int src_level,
    int tile_x,
    int tile_z,
    int target)
{
    struct World* world;
    struct Task_ProjectileBody* task;
    int view;
    int src_x, src_z, src_y;
    int range;
    int idx;

    assert(app);
    assert(model_id >= 0);
    world = app_effects_view_world(app, &view);
    src_x = src_tile_x * 128 + 64;
    src_z = src_tile_z * 128 + 64;
    /* World y is negative-up: start slightly above the source ground. */
    src_y = World_HeightAt(world, src_x, src_z, src_level) - 160;
    range = abs(tile_x - src_tile_x);
    if( abs(tile_z - src_tile_z) > range )
        range = abs(tile_z - src_tile_z);
    idx = World_ProjectileSpawn(
        world,
        -1,
        src_level,
        src_level,
        src_x,
        src_z,
        tile_x * 128 + 64,
        tile_z * 128 + 64,
        src_y,
        160,
        144, /* end height above target ground (36 * 4) */
        0,
        60 + range * 5, /* cycles: base flight + per-tile stretch */
        15,             /* launch slope (1/2048 circle units) */
        64,
        target,
        WORLD_PROJECTILE_TARGET_NONE);
    task = app_effects_projectile_body_new(app, world, idx);
    task->model_id = model_id;
    task->seq_id = seq_id;
    app_effects_projectile_body_add(app, task, view);
    app_effects_trace("spawn_projectile: %d,%d -> %d,%d target=%d\n",
        src_tile_x, src_tile_z, tile_x, tile_z, target);
    return idx;
}
