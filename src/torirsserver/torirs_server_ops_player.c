/*
 * The `p_*` ops that re-issue a player's own interaction.
 *
 * Fifth of the per-domain opcode files (`torirs_server_ops_db.c` is the first and its
 * header states the contract): `ToriRSServer_ScriptCommand` offers each domain the
 * opcode in turn and each returns 1 when it handled it.
 *
 * Two opcodes today, `p_oploc` and `p_opobj`, and the file exists rather than a
 * case in the big switch because `docs/serverscript.md` says new opcodes go in a
 * domain file and because the rest of the `p_*` family — `p_opnpc`, `p_opheld`,
 * `p_opplayer` — belongs here beside them as each is written or corrected.
 *
 * ------------------------------------------------------------------
 * A re-issue is not a call into the engine's handler
 * ------------------------------------------------------------------
 *
 * This is the distinction the whole file turns on, and the tree already contains
 * the counter-example. `SS_OP_P_OPNPC` (`torirs_server_scripts.c`) reads the op number
 * and, for Attack, latches `combat_target` itself — the same conclusion
 * `ToriRSServer_CombatEngage` reaches, written out a second time rather than called.
 * So a script saying `p_opnpc(2)` does not re-enter the dispatch: it *is* the
 * combat engine, reached by another name. (Anything that must hold for both
 * spellings therefore has to be written twice; TORIRSSERVER_AFK_COMBAT_TICKS is the
 * live example.) That makes `[opnpc2,_] p_opnpc(2)` look like an eviction of the `opnpc`
 * fallback row while moving nothing, which is the trap `PORTING_GUIDE` §6 names
 * explicitly and the reason that row's `blocked_on` says what it says.
 *
 * `PlayerOps.ts:389-403` is not that. It stops the current action, queues a
 * waypoint when the loc is out of operable distance, and then **sets an
 * interaction** — `setInteraction(SCRIPT, activeLoc, APLOC1 + type)` — which the
 * engine resolves on a later tick through the same path a click takes. So the
 * script hands the interaction back to the dispatch; it does not borrow the
 * dispatch's conclusion. That is what makes it safe for content to bind a verb
 * the engine also answers: the binding can re-issue rather than replace.
 *
 * Two details of the reference are load-bearing and both are easy to drop:
 *
 *  - **it returns silently when the loc's own `op[type]` is empty.** Not an
 *    error, not a message. A resume loop that re-issues op 1 on a loc that has
 *    since changed into something with no op 1 simply stops, which is what
 *    `p_oploc(1); // gets delayed by a tick >:(` in `skill_woodcutting`'s resume
 *    loop depends on.
 *  - **the range test decides the waypoint, not the interaction.** The
 *    interaction is set either way; being far away only adds the walk.
 */

#include "torirs_server.h"
#include "torirs_server_scene.h"

#include "ss_meta.h"
#include "ss_opcode.h"
#include "ss_trigger.h"
#include "ssvm.h"

#include <stdint.h>

int
ToriRSServer_OpsPlayer(
    struct SSVM_State* state,
    int opcode,
    int dot)
{
    struct ToriRSServer* srv = (struct ToriRSServer*)state->env->host.user;

    (void)dot;

    switch( opcode )
    {
    /*
     * A temporary scene for cutaways such as Construction's scrying pool.
     * The coord and lifetime are content policy; the world layer owns the
     * protocol barrier, input lock and restoration of the real scene.
     */
    case SS_OP_REMOTE_VIEW_START:
    {
        int32_t coord;
        int32_t ticks;

        if( !SSVM_PopInt(state, &ticks) || !SSVM_PopInt(state, &coord) )
            return 1;
        if( ticks < 1 || ticks > 200 )
        {
            SSVM_Abort(state, "remote_view_start: duration %d is not 1..200 ticks",
                       (int)ticks);
            return 1;
        }
        ToriRSServer_WorldRemoteViewStart(srv->active_player, ToriRSServer_CoordX(coord),
                                        ToriRSServer_CoordZ(coord), ToriRSServer_CoordLevel(coord),
                                        (int)ticks);
        return 1;
    }

    case SS_OP_REMOTE_VIEW_END:
        ToriRSServer_WorldRemoteViewEnd(srv->active_player);
        return 1;

    /*
     * `[command,p_oploc](int $op)` — engine.rs2:179, `PlayerOps.ts:389`.
     *
     * The VM's pointer table has already refused this without an active player
     * (protected) and an active loc (require 0x044), so neither is re-checked
     * here. What is re-checked is that the *slot* still names a live loc, for the
     * reason `torirs_server_ops_loc.c`'s header gives: the pointer is a slot + 1 and a
     * scene rebuild reallocates the array underneath a suspended script.
     */
    case SS_OP_P_OPLOC:
    {
        int32_t op_num;
        int slot;
        struct ToriRSServerSceneLoc* loc;

        if( !SSVM_PopInt(state, &op_num) )
            return 1;
        if( op_num < 1 || op_num > 5 )
        {
            /* `throw new Error(\`Invalid oploc: ${type + 1}\`)` — a content bug,
             * and loud in the reference too. */
            SSVM_Abort(state, "p_oploc: op %d is not 1..5", (int)op_num);
            return 1;
        }

        slot = (int)((intptr_t)SSVM_Active(state, SSVM_ENT_LOC)) - 1;
        loc = slot >= 0 ? ToriRSServer_SceneLoc(slot) : NULL;
        if( !loc || !loc->active )
        {
            SSVM_Abort(state, "p_oploc: the active loc is gone");
            return 1;
        }

        /*
         * `if (!locType.op || !locType.op[type]) return;` — the silent return.
         * The op number is 1-based here and 0-based there; `ToriRSServer_SceneLocOp`
         * takes the 1-based form, which is also what the packet carries.
         *
         * Against the multiloc-RESOLVED child, exactly as `handle_oploc` does,
         * and this is the same gap that file's comment describes on the other
         * side of the wire: a scene loc entity is always the BASE id, and a
         * multiloc base commonly carries no ops at all — every op lives on the
         * morph. `farming_veg_patch_1` is the shape: the base declares only
         * `multivarbit` and its twelve `multiloc<N>` states, while `op1=Rake`
         * is on `veg_patch_weeds_1/2/3`.
         *
         * Asking the base therefore answered "no such op" for the one case the
         * check exists to allow, and `p_oploc` returned silently — so a skilling
         * loop over a multiloc could not resume. It killed raking on the first
         * call: `~farming_rake_patch` arms `%action_delay` and re-issues, and
         * with the re-issue dropped the interaction was never re-latched, so the
         * tick that adds the weeds and advances the patch state never arrived.
         * No message, no anim past the first, no weeds — which reads as a
         * content bug and is not one.
         */
        {
            int resolved = ToriRSServer_LocResolveTransform(srv->active_player, loc->loc_id);

            if( resolved < 0 )
                resolved = loc->loc_id;
            if( !ToriRSServer_SceneLocOp(resolved, (int)op_num) )
                return 1;
        }

        /*
         * `stopAction()` then `setInteraction`. Clearing first matters for the
         * same reason the op dispatch clears before running a script: whatever
         * the player was doing is over, and the thing being set must survive it.
         */
        ToriRSServer_WorldClearPendingAction(srv);
        ToriRSServer_WorldInteractionClear(srv);
        ToriRSServer_WorldInteractionSet(srv, TORIRSSERVER_INTERACT_LOC, (int)op_num, -1, loc->loc_id,
                                      loc->x, loc->z, loc->level,
                                      loc->size_x > 0 ? loc->size_x : 1,
                                      loc->size_z > 0 ? loc->size_z : 1);
        /*
         * `queueWaypoint` only when out of operable distance. Here the walk is
         * unconditional and that is a deliberate narrowing, not an oversight:
         * `ToriRSServer_WorldWalkToApproach` computes a route from where the player
         * already is, so a player already beside the loc gets a zero-length one.
         * The observable difference is nil and the alternative would be a second,
         * separate answer to "is this close enough" beside the one
         * `ToriRSServer_WorldProcessInteraction` already applies a tick later.
         */
        {
            struct CollisionApproach approach;
            int loc_slot = ToriRSServer_SceneFindLoc(loc->x, loc->z, loc->level, loc->loc_id);
            ToriRSServer_SceneLocApproach(loc_slot, &approach);
            ToriRSServer_WorldWalkToApproach(srv, loc->x, loc->z, &approach);
        }

        /*
         * Deliberately NOT `ToriRSServer_WorldProcessInteraction` here.
         *
         * `handle_oploc` calls it because a packet arrives between ticks and the
         * player may already be standing in range. A script is *inside* a tick,
         * and resolving the interaction now would run the `[oploc<n>]` script
         * from within itself — the infinite recursion `[oploc1,_] p_oploc(1)`
         * would otherwise be. The reference has the same shape and the same
         * comment in `woodcut.rs2`: "gets delayed by a tick >:(".
         */
        return 1;
    }

    /*
     * `[command,p_opobj](int $op)` — engine.rs2, `PlayerOps.ts:1047`.
     *
     * Same re-issue shape as `p_oploc`: stop the current action, queue a walk
     * onto the pile's tile, and set an interaction the dispatch resolves next
     * tick. Firemaking's Light loop (`p_opobj(4)`) is the caller that made this
     * land.
     *
     * The silent "op empty" return the reference makes against `ObjType.op` is
     * not here yet: `ToriRSServerObjInfo` only loads inventory `if_ops`, not ground
     * `op`s. A resume against a pile that lost its verb is rare (logs keep
     * Light); if the pile itself is gone the generation check aborts.
     */
    case SS_OP_P_OPOBJ:
    {
        int32_t op_num;
        struct ToriRSServerGroundObj* obj;
        intptr_t handle;
        int slot;

        if( !SSVM_PopInt(state, &op_num) )
            return 1;
        if( op_num < 1 || op_num > 5 )
        {
            SSVM_Abort(state, "p_opobj: op %d is not 1..5", (int)op_num);
            return 1;
        }

        handle = (intptr_t)SSVM_Active(state, SSVM_ENT_OBJ);
        slot = ToriRSServer_WorldGroundSlot(srv, handle);
        if( slot < 0 )
        {
            SSVM_Abort(state, "p_opobj: the active obj is gone");
            return 1;
        }
        obj = &srv->ground[slot];

        ToriRSServer_WorldClearPendingAction(srv);
        ToriRSServer_WorldInteractionClear(srv);
        ToriRSServer_WorldInteractionSet(srv, TORIRSSERVER_INTERACT_OBJ, (int)op_num, -1,
                                      obj->obj_id, obj->x, obj->z, obj->level, 1, 1);
        /* Onto the tile, same as handle_opobj — a pile is used from on top. */
        {
            struct CollisionApproach exact;
            ToriRSServer_SceneObjApproach(0, &exact);
            ToriRSServer_WorldWalkToApproach(srv, obj->x, obj->z, &exact);
        }
        return 1;
    }

    /*
     * `[command,p_opplayer](int $op)` — `PlayerOps.ts`, the `n` opcode:
     *
     *     const target = state._activePlayer2;
     *     if (!target) return;
     *     state.activePlayer.stopAction();
     *     state.activePlayer.setInteraction(SCRIPT, target, APPLAYER1 + type);
     *
     * The target is the *secondary* active player, which is the only way a
     * player-versus-player interaction can start in this tree: rev 230 assigns
     * no OPPLAYER wire opcode (see `torirs_server_world.c`'s note beside
     * `useon_interact`), so no click reaches here. Content that has two players
     * in hand — the `.` dialect — can.
     *
     * The silent return on a missing target is the reference's, not a guard: a
     * script whose second player logged out between ticks stops interacting
     * rather than aborting.
     */
    case SS_OP_P_OPPLAYER:
    {
        int32_t op_num;
        struct ToriRSServerPlayer* target;
        int slot;

        if( !SSVM_PopInt(state, &op_num) )
            return 1;
        if( op_num < 1 || op_num > 5 )
        {
            /* `throw new Error(\`Invalid opplayer: ${type + 1}\`)`. */
            SSVM_Abort(state, "p_opplayer: op %d is not 1..5", (int)op_num);
            return 1;
        }

        target = (struct ToriRSServerPlayer*)SSVM_ActiveSlot(state, SSVM_ENT_PLAYER, SSVM_SECONDARY);
        if( !target || !target->active || target == srv->active_player )
            return 1;
        slot = (int)(target - srv->players);
        if( slot < 0 || slot >= srv->player_count )
            return 1;

        ToriRSServer_WorldClearPendingAction(srv);
        ToriRSServer_WorldInteractionClear(srv);
        ToriRSServer_WorldInteractionSet(srv, TORIRSSERVER_INTERACT_PLAYER, (int)op_num, slot,
                                      target->pid, target->x, target->z, target->level, 1, 1);
        {
            struct CollisionApproach approach;
            ToriRSServer_SceneNpcApproach(1, &approach);
            ToriRSServer_WorldWalkToApproach(srv, target->x, target->z, &approach);
        }
        /* Not resolved here, for the same reason `p_oploc` does not: an
         * `[opplayer<n>]` that re-issues itself would recurse inside its own
         * tick. */
        return 1;
    }

    default:
        return 0;
    }
}
