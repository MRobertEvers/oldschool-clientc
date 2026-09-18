/*
 * Applying config to live entities: scenery animations, npc types, and player
 * appearance.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Private to this unit, declared up front so definition order is free. */
static int
app_appearance_team(
    struct App* app,
    int const slots[APPEARANCE_SLOT_COUNT]);
static void
app_player_wanted_body(
    struct App* app,
    struct WorldEntity_Player const* player,
    int slots[12]);

/*
 * LOC_ANIM: attach a sequence to the scenery element on a tile.
 *
 * ON THE SERIAL EXEC FIFO, BEHIND THE SAME PACKET'S LOC_ADD_CHANGE, and that is
 * the whole reason this is a task rather than a call.
 *
 * The reference applies a zone loc change to the scene the moment it reads it -
 * it has the whole cache in hand and a loctype's models are built on demand at
 * draw time - so a LOC_ADD_CHANGE and a LOC_ANIM in one enclosed update work in
 * the order they were written. This client cannot: a change has to wait for its
 * loc config and every model it names to become resident (the reference's own
 * `changeLocAvailable` gate), so `App_WorldLocChange` enqueues an async task.
 *
 * Applying the animation synchronously beside that made the two ops race, and
 * the loser was always the animation: `World_SceneryFindAt` ran before the
 * pending change had built the scenery, found the OLD loc or nothing at all,
 * and the sequence was dropped with no error to see. The loc then stood on
 * whatever frame its `anim=` config left it on - which is what "the loc is
 * stuck in a frame" is, every time.
 *
 * Content should not have to know any of this. The Theatre's acid pools and
 * exhumeds, the Inferno's collapsing flanks and every door script are all
 * entitled to add a loc and animate it in the same tick, exactly as the
 * reference lets them.
 *
 * The task awaits nothing itself. `ToriRS_TaskQueue_Run` runs the head task
 * until it yields and leaves the rest queued in order, so being enqueued after
 * the change IS the fix; with no change pending it lands in the same pass.
 */
void
App_WorldSceneryAnim(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int loc_shape,
    int seq_id)
{
    struct Task_AppSpawn* task;

    assert(app);
    task = app_spawn_task_new(app, APP_SPAWN_LOC_ANIM, scene_x, scene_z, level);
    task->loc_shape = loc_shape;
    task->seq_id = seq_id;
    TaskRunner_AddSettling(&app->exec_runner, &task->task);
}

void
app_world_scenery_anim_apply(
    struct App* app,
    struct World* world,
    int scene_x,
    int scene_z,
    int level,
    int loc_shape,
    int seq_id)
{
    int idx;
    assert(app);
    assert(world);
    idx = World_SceneryFindAt(world, scene_x, scene_z, level, loc_shape);
    if( idx >= 0 )
    {
        struct WorldEntity_Scenery* scenery = World_EntityPoolGet(&world->entities.scenery, idx);
        if( scenery && scenery->element_id >= 0 )
        {
            struct ToriDraw_SceneElement* element;

            /* Everything below writes to this element's model -- the quarter
             * turn is un-baked in place, and app_world_apply_seq captures a
             * bind pose and then poses it every frame. A static loc's model is
             * shared with its every other placement, so it has to become this
             * element's own before any of that. */
            ToriDraw_SceneElementModelForWrite(app->scene, scenery->element_id);
            element = ToriDraw_SceneElementGet(app->scene, scenery->element_id);

            /* A loc with no config animation is built as static scenery: its
             * map orientation is baked directly into its vertices. LOC_ANIM
             * turns that same loc into the reference client's DynamicObject,
             * whose order is the opposite — animate the unrotated model, then
             * apply the loc orientation while drawing it.
             *
             * Without this conversion, translation/rotation ops from a
             * packet-attached sequence run in world axes. The Inferno's
             * angle-3 falling walls are the visible failure: their inner
             * sections move behind the flanks even though 7559 and 7560 start
             * together. Undo the baked quarter-turn once and carry it as the
             * element yaw from then on. Shape 11 already owns the extra
             * diagonal half-turn in its draw yaw, so preserve that base.
             *
             * Config-animated locs already arrive in this representation;
             * their yaw is the desired value and the guard leaves them alone.
             */
            if( element && ToriDraw_ModelKindIsFull(element->model.kind) &&
                element->model.u.model.model &&
                (loc_shape == RSCACHE_LOC_SHAPE_SCENERY ||
                 loc_shape == RSCACHE_LOC_SHAPE_SCENERY_DIAGONAL) )
            {
                int const angle = scenery->angle & 3;
                int const base_yaw = loc_shape == RSCACHE_LOC_SHAPE_SCENERY_DIAGONAL ? 256 : 0;
                int const wanted_yaw = base_yaw + angle * 512;

                if( angle != 0 && element->world_position.yaw == base_yaw )
                {
                    ToriDraw_ModelOrient(element->model.u.model.model, (4 - angle) & 3);
                    ToriDraw_SceneElementSetPosition(
                        app->scene,
                        scenery->element_id,
                        element->world_position.x,
                        element->world_position.y,
                        element->world_position.z,
                        wanted_yaw);
                }
            }
            app_world_apply_seq(app, scenery->element_id, seq_id);
            app->need_redraw = 1;
        }
    }
}

void
App_WorldApplyNpcType(
    struct App* app,
    int world_idx,
    int element_id,
    int npc_type,
    int base_npc_type)
{
    struct ToriRS_Npctype* npctype;
    struct ToriRS_NpcEntityFacts facts;
    struct ToriDraw_Model* model;

    assert(app);
    npctype = CacheProvider_NpctypeGet(app->provider, npc_type);
    if( !npctype )
        return;
    /* Same gap-fill as the spawn path: the rung draws the body and names the
     * ops, the shell supplies the size and idle animation it does not state.
     * See app_npc_entity_facts. */
    app_npc_entity_facts(app, base_npc_type, npctype, &facts);
    if( getenv("TORIRS_ANIM_DEBUG") )
        TORIRS_LOG(
            "npc_retype: world_idx=%d element=%d type=%d\n", world_idx, element_id, npc_type);

    /* Retyping TO a model-less type must actually hide the npc. Building
     * nothing here would leave the old model mounted and the entity would keep
     * rendering as its previous form; an empty model is the retype's honest
     * result, and matches the spawn path's handling of the same content. */
    if( npctype->models_count <= 0 )
    {
        model = ToriDraw_ModelNew(0, 0, 0);
        if( model )
            ToriDraw_ModelSetBoundsCylinder(model);
    }
    else if( !app_world_npc_models_resident(app, npctype) )
    {
        /* The body is on the wire (NpcBodyLand re-applies this type when it
         * lands): keep whatever model the element has rather than mount
         * nothing, and say nothing -- it is not a failure. */
        model = NULL;
    }
    else
    {
        model = app_world_build_npc_model(app, npc_type, npctype);
        if( !model )
            /* Unlike the models_count<=0 branch above, this is not an honest
             * "no body" result -- it's app_world_build_model failing to
             * resolve a type that DOES have models (see the matching log in
             * app_world_spawn_npc_now). Neither branch below then touches the
             * element, so it silently keeps whatever model it already had
             * (typically the 14-bit placeholder type's, for a large npc like
             * QBD that needed a same-packet TRANSFORMATION to reach its real
             * id) -- which reads in-game as "the npc never rendered" with no
             * trace of why. Log it so that's diagnosable. */
            TORIRS_ERR("npc_retype: npc %d models failed to load\n", npc_type);
    }
    /* The depth-test opt-in is a property of the npc TYPE, so a retype has to
     * re-decide it against the new type -- exactly as the spawn path does. */
    if( model )
        app_model_apply_import_render_flags(model, app_npc_wants_zbuffer(npc_type, npctype));

    if( model && element_id >= 0 && ToriDraw_SceneElementIsLive(app->scene, element_id) )
    {
        struct ToriDraw_ModelHandle hnd;
        memset(&hnd, 0, sizeof(hnd));
        hnd.kind = TORIDRAWMK_MODEL;
        hnd.u.model.model = model;
        ToriDraw_SceneElementSetModel(app->scene, element_id, hnd);
        {
            struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(app->scene, element_id);
            if( el )
                el->anim_external = true;
            ToriDraw_SceneAnimListInvalidate(app->scene);
        }
    }
    else if( model )
    {
        ToriDraw_ModelFree(model);
    }

    {
        /* Reference CHANGETYPE swaps walkanim_l/r (Client.ts 8460-8462). */
        struct WorldEntityFacet_IdleAnimations idle = {
            .readyanim = facts.readyanim,
            .walkanim = facts.walkanim,
            /* Opcodes 15/114 rather than the -1 pair that used to sit here.
             * `World_EntityFace` takes turnanim over walkanim and
             * `World_UpdateMoverMovementAndAnimation` takes runanim over
             * walkanim at speed; both were already written and neither had
             * anything to read. The run set gets the same left/right swap the
             * walk set does -- it is the same reference line (Client.ts
             * 8460-8462), which swaps the pair for every movement set. */
            .turnanim = facts.turnanim,
            .runanim = facts.runanim,
            .walkanim_b = facts.walkanim_b,
            .walkanim_r = facts.walkanim_l,
            .walkanim_l = facts.walkanim_r,
            /* Read off the DRAWN type, and deliberately not gap-filled from the
             * shell the way size and readyanim are: it is a bare boolean whose
             * absent value and whose authored-false value are the same bit, so
             * "the rung did not state it" cannot be expressed. Same reason
             * `turn_speed` is left out of ToriRS_NpcEntityFacts. */
            .idle_anim_restart = npctype->idle_anim_restart ? 1 : 0,
        };
        World_NpcSetType(app->world, world_idx, npc_type, facts.size, &idle);
    }
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(&app->world->entities.npc, world_idx);
        /*
         * A newly-added NPC reaches its ready pose through
         * app_world_spawn_npc_now, but a revision-239 CHANGE_TYPE keeps the
         * same scene element.  Rebind the replacement type's ready sequence
         * here as well, so the element does not spend a frame (and an
         * async-load gap) driven by the former type's idle.
         *
         * Only when there is no transient animation running, though.  That is
         * the other half of the rule World_NpcSetType now states: a one-shot
         * survives a transmog, and unconditionally stamping the new readyanim
         * on top of it would put the stomp back one layer up — which is
         * exactly how the Queen Black Dragon's return-to-sleep kept being
         * erased.  When a primary track is live, the next
         * app_world_apply_entity_anim_tracks binds it onto the new model, and
         * the readyanim takes over on its own when the sequence ends.
         *
         * This is intentionally general rather than familiar-specific.  The
         * regular entity sync will take over with the new idle/walk state on
         * the next world tick, just as it does after a normal spawn.
         */
        int const primary_live = npc && npc->animation.primary.anim_id != (uint16_t)-1 &&
                                 npc->animation.primary.anim_id != 0;

        if( model && !primary_live && element_id >= 0 &&
            ToriDraw_SceneElementIsLive(app->scene, element_id) )
            app_world_apply_seq(app, element_id, facts.readyanim);
        if( npc )
        {
            npc->combat_level = npctype->combat_level;
            npc->alwaysontop = npctype->alwaysontop;
            npc->minimap_visible = npctype->minimap_visible;
            npc->interactable = npctype->interactable;
            npc->facing.turn_speed = npctype->turn_speed;
            snprintf(npc->name, sizeof(npc->name), "%s", npctype->name);
            for( int i = 0; i < 5; i++ )
                snprintf(
                    npc->actions[i].name, sizeof(npc->actions[i].name), "%s", npctype->actions[i]);
            /* The drawn type changed; base_npc_id -- the multinpc shell --
             * did not. A plugin keyed on the shell, which is how anything
             * tagging an npc has to be keyed, keeps its tag across this. */
            if( app->plugins )
            {
                struct ToriRS_NpcSnapshot retyped;
                app_plugin_fill_npc(app, npc, &retyped);
                PluginHost_NpcRetype(app->plugins, &retyped);
            }
            if( torirs_env_net_debug() )
                TORIRS_ERR(
                    "entity_sync: npc type replacement=%d element=%d tile=%d,%d size=%d "
                    "model=%s\n",
                    npc_type,
                    element_id,
                    npc->grid_position.x,
                    npc->grid_position.z,
                    npc->size,
                    model ? "installed" : "missing");
        }
    }
    app_sync_textures(app);
    app->need_redraw = 1;
}

/*
 * Bring a just-mounted model to the pose the element is already holding.
 *
 * `ToriDraw_SceneElementSetModel` mounts a model at its BIND pose, and the
 * renderer poses it later in the same frame (torirs_frame's
 * ApplyAnimationResolved, and each backend's own call). In between, the model's
 * bounds cylinder is the bind pose's -- and that cylinder is the ONLY thing
 * `app_entity_model_height` reads, so it is what the overhead prayer icons, the
 * overhead chat line, the hint arrow and the health bar all hang off.
 *
 * That gap is visible because a headicon rides the APPEARANCE block: turning an
 * overhead prayer on is a model swap. The frame the prayer was enabled measured
 * the bind pose and every frame after it measured the animated one, so the icon
 * appeared at one height and snapped to another a frame later -- a jump the
 * reference cannot make, because it takes `height` from the model it has just
 * animated (ClientPlayer.getTempModel: `this.height = model.minY` after
 * getTempModel2 has applied the frame).
 *
 * It costs one extra pose per model swap and nothing per frame: the renderer's
 * own call arrives at a model already at that frame and skips.
 */
static void
app_element_pose_after_model_swap(
    struct App* app,
    int element_id)
{
    struct ToriDraw_SceneElement* element;

    assert(app);
    assert(app->scene);
    assert(element_id >= 0);

    element = ToriDraw_SceneElementGet(app->scene, element_id);
    assert(element);
    /* An element with no bound track is not posed by anything, so its mounted
     * bind pose IS the pose the renderer draws. */
    if( !element->animation )
        return;
    ToriDraw_SceneElementApplyAnimation(app->scene, element_id, true, element->anim_frame);
}

/*
 * Team-cape id carried by these slots (reference
 * ClientPlayer.decodeAppearance: while reading the 12 worn slots it keeps the
 * ObjType.team of every equipped obj, so the LAST non-zero one wins).
 *
 * Resolved off resident objtypes, which is safe only where the body was just
 * built from the same slots: the build refuses an appearance whose configs are
 * not all resident.
 */
static int
app_appearance_team(
    struct App* app,
    int const slots[APPEARANCE_SLOT_COUNT])
{
    int team = 0;

    for( int s = 0; s < APPEARANCE_SLOT_COUNT; s++ )
    {
        int obj_id = Appearance_SlotObj(slots[s]);
        struct ToriRS_Objtype const* obj;
        if( obj_id < 0 )
            continue;
        obj = CacheProvider_ObjtypeGet(app->provider, obj_id);
        assert(obj);
        if( obj->team != 0 )
            team = obj->team;
    }
    return team;
}

/*
 * The body the player wants right now: the appearance, with the held-item
 * override of a playing primary seq folded in (reference
 * ClientPlayer.getSequencedModel via SeqType.replaceheldleft/right, opcodes
 * 6/7 -- a woodcutting or mining seq swaps the worn item for an obj that is
 * not part of the appearance), in slot 5 (left) / slot 3 (right).
 */
static void
app_player_wanted_body(
    struct App* app,
    struct WorldEntity_Player const* player,
    int slots[12])
{
    struct WorldEntityFacet_Animation const* anim = &player->animation;

    memcpy(slots, player->appearance.slots, sizeof(player->appearance.slots));
    if( anim->primary.anim_id != (uint16_t)-1 && anim->primary.anim_id != 0 &&
        anim->primary.delay == 0 )
    {
        struct ToriDraw_Animation* prim =
            ToriDraw_SceneAnimationGet(app->scene, anim->primary.anim_id);
        if( prim && prim->frame_count > 0 )
        {
            /* Cache-sourced appearance slots -- converted here so the override
             * is in the same vocabulary as the appearance it overwrites. */
            if( prim->replaceheldright >= 0 )
                slots[3] = Appearance_FromCacheValue(prim->replaceheldright);
            if( prim->replaceheldleft >= 0 )
                slots[5] = Appearance_FromCacheValue(prim->replaceheldleft);
        }
    }
}

void
app_world_reconcile_player_body(
    struct App* app,
    struct WorldEntity_Player* player)
{
    int slots[12];
    struct ToriDraw_Model* model;
    struct ToriDraw_ModelHandle hnd;

    assert(app);
    assert(player);

    app_player_wanted_body(app, player, slots);
    if( memcmp(slots, player->body.slots, sizeof(slots)) == 0 &&
        memcmp(player->appearance.colors, player->body.colors, sizeof(player->body.colors)) ==
            0 &&
        player->gender == player->body.gender )
        return;

    /* Not yet while any part is still loading (PlayerBodyLand /
     * PlayerHeldLand are fetching it): the element keeps the last whole body,
     * and the next frame asks again. The appearance itself must be whole too
     * when a held item stands in for part of it -- the team below reads its
     * configs, and the override ends back on it. */
    if( !PlayerModel_AppearanceResident(app->provider, player->appearance.slots, player->gender) )
        return;
    model = PlayerModel_BuildFromAppearance(
        app->provider, slots, player->appearance.colors, player->gender);
    if( !model )
        return;
    if( !ToriDraw_SceneElementIsLive(app->scene, player->element_id) )
    {
        ToriDraw_ModelFree(model);
        return;
    }

    /* SceneElementSetModel disposes the previous model; the element's
     * animation binding survives, so the current seq keeps driving the new
     * model. */
    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = model;
    ToriDraw_SceneElementSetModel(app->scene, player->element_id, hnd);
    app_element_pose_after_model_swap(app, player->element_id);

    memcpy(player->body.slots, slots, sizeof(slots));
    memcpy(player->body.colors, player->appearance.colors, sizeof(player->body.colors));
    player->body.gender = player->gender;
    /* Every obj config of the appearance is resident (checked above). */
    player->team = app_appearance_team(app, player->appearance.slots);
    app_sync_textures(app);
    app->need_redraw = 1;
}

void
App_WorldApplyPlayerAppearance(
    struct App* app,
    int world_idx,
    struct PktPlayerAppearance const* appearance)
{
    assert(app);
    assert(appearance);

    /* Data only. The body is derived from it by
     * app_world_reconcile_player_body on the next frame, once whole. */
    {
        struct WorldEntityFacet_IdleAnimations idle = {
            .readyanim = appearance->readyanim,
            .walkanim = appearance->walkanim,
            .turnanim = appearance->turnanim,
            .runanim = appearance->runanim,
            .walkanim_b = appearance->walkanim_b,
            .walkanim_r = appearance->walkanim_r,
            .walkanim_l = appearance->walkanim_l,
        };
        World_PlayerSetAppearance(
            app->world,
            world_idx,
            appearance->slots,
            appearance->identkit,
            appearance->colors,
            &idle,
            appearance->name,
            appearance->combat_level,
            appearance->gender);

        /* Overhead prayer/skull headicon bitmask (reference
         * ClientPlayer.headicons, appearance g1). SetAppearance carries no
         * headicon field, so copy it onto the entity directly for the overlay
         * pass. */
        {
            struct WorldEntity_Player* ent =
                World_EntityPoolGet(&app->world->entities.player, world_idx);
            if( ent )
            {
                ent->headicon = appearance->headicon;
            }
        }

        /* Local player's real name now known: sync it to the chatbox so the
         * public-chat local echo shows the login name instead of the default
         * "Player". The reference echoes with this.localPlayer.name
         * (Client.ts:3405-3417); the server echoes the same name through
         * PLAYER_INFO CHAT, so the two must match. */
        if( appearance->name[0] )
        {
            int local_idx = -1;
            if( RS_EntitySync_FindPlayer(
                    &app->esync,
                    app->esync.local_pid >= 0 ? app->esync.local_pid : 2047,
                    &local_idx,
                    NULL) &&
                local_idx == world_idx )
            {
                strncpy(app->chat.username, appearance->name, sizeof(app->chat.username) - 1);
                /* Same string to the CS2 host, which answers CHAT_PLAYERNAME
                 * with it — clientscript 223 builds the chatbox input line
                 * from that op, so the two spellings have one source. */
                snprintf(
                    app->host.local_player_name,
                    sizeof(app->host.local_player_name),
                    "%s",
                    appearance->name);
            }
        }
    }
    app->need_redraw = 1;
}
