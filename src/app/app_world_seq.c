/*
 * Scene elements and their animation sequences: create, bind, catch up.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Private to this unit, declared up front so definition order is free. */
static void
app_world_catch_up_object_seq(
    struct App* app,
    int element_id,
    struct ToriDraw_Animation* anim,
    int elapsed_cycles);
static int
app_world_try_bind_seq(
    struct App* app,
    int element_id,
    int seq_id,
    int start_cycle);

/* Wrap a freshly built (owned) model in a new dynamic scene element. The
 * element owns the model from here (SceneElementRemove frees it), which is
 * why spawns copy registry models instead of sharing handles.
 *
 * The pool is the ROOT view's dynamic half, which is right for as long as
 * every entity lives in app->world. When entities start being owned by a boat
 * view they must be allocated in THAT view's dynamic pool
 * (TORIDRAW_SCENE_POOL_DYNAMIC_VIEW / WorldBuilder.dynamic_pool) instead: the
 * reconcile pass sweeps a view's own pool against its own entity list, so a
 * deck entity holding a root-pool element would be swept by the mainland's
 * next rebuild, which does not know its owner. */
int
app_world_scene_element_create(
    struct App* app,
    enum ToriDraw_ElementKind kind,
    struct ToriDraw_Model* model,
    int world_x,
    int world_y,
    int world_z)
{
    struct ToriDraw_ModelHandle hnd;
    /* Tagged here, where the caller still knows what it is making. Every
     * reader downstream would otherwise have to ask the world, and the
     * pick classifier asked by trying all four pools in turn. */
    int element_id = ElementId_Raw(ElementId_Make(
        kind, ToriDraw_SceneElementAddPool(app->scene, TORIDRAW_SCENE_POOL_DYNAMIC)));

    if( element_id < 0 )
    {
        ToriDraw_ModelFree(model);
        return -1;
    }
    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = model;
    ToriDraw_SceneElementSetModel(app->scene, element_id, hnd);
    ToriDraw_SceneElementSetPosition(app->scene, element_id, world_x, world_y, world_z, 0);
    {
        struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(app->scene, element_id);
        if( el )
        {
            el->dynamic = true;
            /*
             * Players and ground objs pick by their box, as the rev-239 deob
             * sets Model.field2167 (useAABBMouseCheck) on every player model
             * (class60) and every obj model (class396). The box is the
             * reference's: the cursor's ray against the model's 3D box
             * (ToriDraw_ProjectedModelBoxHitTest), not a screen rectangle --
             * a screen rectangle grown by 5 px is what once made boxes look
             * so much bigger than the model that entities were moved to the
             * per-face test.
             *
             * Npcs are overwritten every frame by the deob's rule
             * (app_world_sync_positions: box only for a size-1 type), which
             * picks TzKal-Zuk per face.
             */
            el->pick_aabb =
                kind == TORIDRAW_ELEMENT_KIND_PLAYER || kind == TORIDRAW_ELEMENT_KIND_OBJSTACK;
        }
    }
    return element_id;
}

/* Advance a newly-bound packet animation over the client cycles its async load
 * consumed. The reference constructs a DynamicObject at LOC_ANIM receipt, so
 * loading is synchronous from its clock's point of view; beginning at frame 0
 * when our task finishes makes two sequences from one enclosed zone update
 * start at different times.
 *
 * Use the same counters as app_world_tick_animations rather than converting a
 * cycle count to a frame by division: frame lengths vary, and frameStep=1 holds
 * these Inferno locs on their terminal frame. DynamicObject caps catch-up at
 * 100 cycles for a looping sequence, which also bounds this loop after a stall. */
static void
app_world_catch_up_object_seq(
    struct App* app,
    int element_id,
    struct ToriDraw_Animation* anim,
    int elapsed_cycles)
{
    struct ToriDraw_SceneElement* element;

    if( elapsed_cycles <= 0 )
        return;
    assert(anim);
    element = ToriDraw_SceneElementGet(app->scene, element_id);
    if( !element )
        return;
    /* A sequence that cannot terminate needs the same 100-cycle bound a looping
     * DynamicObject gets, or a long load stall is paid back one cycle at a time
     * here. anim_loop never terminates by construction. */
    if( (anim->frame_step > 0 || element->anim_loop) && elapsed_cycles > 100 )
        elapsed_cycles = 100;

    for( int cycle = 0; cycle < elapsed_cycles && element->anim_seq_id != -1; cycle++ )
    {
        if( element->is_skeletal )
        {
            int play_frames = element->skeletal_play_frames;
            if( play_frames <= 0 )
                play_frames = anim->frame_count;
            element->anim_cycle++;
            if( element->anim_cycle >= 1 )
            {
                if( anim->frame_count == play_frames )
                {
                    if( !ToriDraw_AnimationAdvanceObjectFrame(anim, &element->anim_frame) )
                        ToriDraw_SceneElementSetAnimation(app->scene, element_id, NULL, true);
                }
                else
                    element->anim_frame = (element->anim_frame + 1) % play_frames;
                element->anim_cycle = 0;
            }
        }
        else if( anim->frames && anim->frame_count > 0 )
        {
            if( element->anim_loop )
                ToriDraw_AnimationAdvanceLoopCycles(
                    anim, &element->anim_frame, &element->anim_cycle, 1);
            else if( !ToriDraw_AnimationAdvanceObjectCycles(
                         anim, &element->anim_frame, &element->anim_cycle, 1) )
                ToriDraw_SceneElementSetAnimation(app->scene, element_id, NULL, true);
        }
    }
}

/* Try binding a loaded scene animation onto an element. Returns 1 when bound
 * OR permanently unbindable (failed/empty sentinel), 0 while still loading. */
static int
app_world_try_bind_seq(
    struct App* app,
    int element_id,
    int seq_id,
    int start_cycle)
{
    struct ToriDraw_Animation* anim;

    if( !ToriDraw_SceneAnimationHas(app->scene, seq_id) )
        return 0;
    /* Bind the resolved animation onto the element — the tick loop and
     * frame emitter read element->animation, which SetAnimationSeq alone
     * leaves NULL. Skip the empty sentinel (failed seqs). */
    anim = ToriDraw_SceneAnimationGet(app->scene, seq_id);
    if( ToriDraw_ElementAnimPlayable(anim) )
    {
        struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(app->scene, element_id);
        ToriDraw_SceneElementSetAnimationSeq(app->scene, element_id, seq_id);
        ToriDraw_SceneElementSetAnimation(app->scene, element_id, anim, true);
        if( el )
            ToriDraw_ElementSetAnim(el, anim);
        if( app->world )
            app_world_catch_up_object_seq(app, element_id, anim, app->world->cycle - start_cycle);
        if( getenv("TORIRS_ANIM_DEBUG") )
        {
            /* The rig, not just the binding. A seq binds to any element, but
             * types 0-3 need `vertex_bones` and type 5 needs `face_bones`; a
             * model carrying neither discards every op and stands perfectly
             * still while its neighbours animate. That failure is invisible
             * here without these three numbers — it looks exactly like a
             * sequence that was never sent. */
            struct ToriDraw_Model const* m =
                (el && ToriDraw_ModelKindIsFull(el->model.kind)) ? el->model.u.model.model : NULL;
            TORIRS_LOG(
                "seq_bind: element=%d seq=%d frames=%d skeletal=%d start=%d now=%d "
                "frame=%d cycle=%d kind=%d vbones=%d fbones=%d falpha=%d\n",
                element_id,
                seq_id,
                anim->frame_count,
                anim->skeletal ? 1 : 0,
                start_cycle,
                app->world ? app->world->cycle : start_cycle,
                el ? el->anim_frame : -1,
                el ? el->anim_cycle : -1,
                el ? (int)el->model.kind : -1,
                m && m->vertex_bones ? m->vertex_bones->bones_count : -1,
                m && m->face_bones ? m->face_bones->bones_count : -1,
                m && m->face_alphas ? 1 : 0);
        }
    }
    else if( getenv("TORIRS_ANIM_DEBUG") )
        TORIRS_LOG(
            "seq_bind: element=%d seq=%d UNBINDABLE (anim=%p frames=%d)\n",
            element_id,
            seq_id,
            (void*)anim,
            anim ? anim->frame_count : -1);
    return 1;
}

/* Queue a sequence load (no-op when cached) and attach it to the element —
 * immediately when already resident, else via the per-frame bind poll. */
void
app_world_apply_seq(
    struct App* app,
    int element_id,
    int seq_id)
{
    app_world_apply_seq_at(app, element_id, seq_id, app->world ? app->world->cycle : 0);
}

/* Queue a sequence load (no-op when cached) without binding it to anything,
 * for an element that binds it later (a projectile, at launch). */
void
app_world_load_seq(
    struct App* app,
    int seq_id)
{
    struct ToriRS_Task* task;

    if( seq_id < 0 )
        return;
    task = CreateTask_SequenceLoad(app->provider, app->scene, seq_id);
    if( task )
        ToriRS_TaskQueue_Add(app->runner.queue, task);
}

/* app_world_apply_seq for a sequence that started on world cycle
 * `start_cycle`: the bind catches its frames up from there. */
void
app_world_apply_seq_at(
    struct App* app,
    int element_id,
    int seq_id,
    int start_cycle)
{
    if( seq_id < 0 )
        return;
    app_world_load_seq(app, seq_id);

    if( app_world_try_bind_seq(app, element_id, seq_id, start_cycle) )
        return;
    bool const queued =
        AsyncPendingSeqBinds_Add(&app->seq_bind_pending, element_id, seq_id, start_cycle);
    assert(queued && "seq bind list full: the element would never animate");
    (void)queued;
}

/*
 * Forget deferred binds for an element that is going away.
 *
 * Necessary because the poll below can only ask whether the element is LIVE,
 * and scene element ids are recycled: an entry left behind by a despawned
 * entity is indistinguishable from a valid one once the id is handed out
 * again, and its sequence then binds onto whoever inherited it -- the wrong
 * creature suddenly playing somebody else's animation. Dropping at the moment
 * of death removes the ambiguity instead of trying to detect it later. Same
 * class of bug as AppEntitySpotanim::owner_entity_id; see that note.
 */
void
app_seq_bind_pending_drop(
    struct App* app,
    int element_id)
{
    AsyncPendingSeqBinds_DropElement(&app->seq_bind_pending, element_id);
}

/* Per-frame: bind deferred element/sequence pairs whose loads landed. */
void
app_world_bind_pending_seqs(struct App* app)
{
    int kept = 0;
    for( int i = 0; i < app->seq_bind_pending.count; i++ )
    {
        struct AsyncPendingSeqBind* pend = &app->seq_bind_pending.items[i];
        if( !ToriDraw_SceneElementIsLive(app->scene, pend->element_id) )
            continue; /* element despawned while loading */
        if( app_world_try_bind_seq(app, pend->element_id, pend->seq_id, pend->start_cycle) )
        {
            app->need_redraw = 1;
            continue;
        }
        app->seq_bind_pending.items[kept++] = *pend;
    }
    AsyncPendingSeqBinds_Keep(&app->seq_bind_pending, kept);
}
