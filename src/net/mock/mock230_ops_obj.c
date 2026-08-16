/*
 * The obj domain — the *active ground obj* (`obj_*`), and the obj record's own
 * equipment slots (`oc_wearpos*`).
 *
 * Fifth of the per-domain opcode files (mock230_ops_db.c is the first and its
 * header states the contract): `mock230_script_command` offers each domain the
 * opcode in turn and each returns 1 when it handled it.
 *
 * Two families in one file for the reason `mock230_ops_loc.c` holds `loc_*` and
 * `lc_*`: they are the instance half and the config half of one domain, and the
 * script that reads one reads the other about the same obj. The reference
 * splits them by receiver — `ObjOps.ts` for the active obj,
 * `ObjConfigOps.ts` for the record — and each opcode below cites the one it
 * came from.
 *
 * Where LostCity puts it: `engine/src/engine/script/handlers/ObjOps.ts`, one
 * handler per opcode, every value read straight off `state.activeObj`. Engine,
 * there and here — these are primitives, and the behaviour that uses them
 * (`[opobj3,_] @pickup_obj`, `player/scripts/pickup.rs2`) is content.
 *
 * `obj_add` (3500) is *not* here. It takes a coord rather than an active obj,
 * has been implemented in mock230_scripts.c's switch since the drop tables
 * landed, and moving it would be a second change wearing this one's clothes.
 * Nor are the older `oc_*` reads (`oc_name`, `oc_stackable`, `oc_category`,
 * `oc_param`) — same argument. New ones land here.
 *
 * ------------------------------------------------------------------
 * The active obj is a handle, not a slot — and that is the difference
 * ------------------------------------------------------------------
 *
 * The npc and loc conventions ride as `slot + 1` in the VM's active-entity
 * pointer. A ground obj cannot: `srv->ground[256]` is a **free list**, and
 * `mock230_world_obj_add` hands a freed index straight to the next drop. A
 * script may park between `obj_find` and `obj_takeitem` (`~pickup_obj` does not
 * today, but `pickup_obj_table` calls `p_delay(0)` and the reference's skilling
 * loops park for dozens of ticks), and on resume a bare index would resolve to
 * whatever landed in the slot meanwhile — a different item, taken silently.
 *
 * So the handle is `mock230_world_obj_handle`: the slot in the low nine bits
 * and the slot's *generation* above it, checked by
 * `mock230_world_ground_slot`. The reference does not need this because it
 * holds a real `Obj` reference and `World.removeObj` sets `isActive = false` on
 * that object; the check here is the same guarantee expressed over an index.
 *
 * ------------------------------------------------------------------
 * What the VM has already checked, and what it has not
 * ------------------------------------------------------------------
 *
 * `ss_meta.gen.h` requires `0x100` (SSVM_PTR_ACTIVE_OBJ) on every `obj_*`
 * opcode in this file, so the VM refuses the call outright when no obj is
 * active — that is not re-checked below. What it cannot check is whether the
 * obj the pointer names is still there, which is `active_obj`'s entire job.
 *
 * `obj_takeitem` additionally requires `0x101` — active obj *and* active
 * player — because it writes a container.
 *
 * The `oc_wearpos*` three require **nothing** (`ss_meta.gen.h:715-717`, both
 * pointer words 0x000): they take an obj id as an argument and read a config
 * record, so there is no receiver to have.
 */

#include "mock230.h"
#include "mock230_container.h"

#include "ss_meta.h"
#include "ss_opcode.h"
#include "ssvm.h"

#include <stdint.h>

/*
 * The active ground obj, or NULL having said why.
 *
 * "The active obj is gone" is one message for two states — the pile was taken
 * by somebody else, or the slot has been reused — because from the script's
 * side they are the same fact and the distinction would only be actionable
 * inside this file.
 */
static struct Mock230GroundObj*
active_obj(
    struct SSVM_State* state,
    int opcode)
{
    struct Mock230Server* srv = (struct Mock230Server*)state->env->host.user;
    intptr_t handle = (intptr_t)SSVM_Active(state, SSVM_ENT_OBJ);
    int slot;

    if( !srv )
    {
        SSVM_Abort(state, "%s: no server", SSVM_OpcodeName(opcode));
        return NULL;
    }
    slot = mock230_world_ground_slot(srv, handle);
    if( slot < 0 )
    {
        SSVM_Abort(state, "%s: the active obj is gone", SSVM_OpcodeName(opcode));
        return NULL;
    }
    if( !srv->active_player ||
        !mock230_world_ground_visible_to(srv, slot, srv->active_player->pid) )
    {
        SSVM_Abort(state, "%s: the active obj is not visible to this player",
                   SSVM_OpcodeName(opcode));
        return NULL;
    }
    return &srv->ground[slot];
}

int
mock230_ops_obj(
    struct SSVM_State* state,
    int opcode,
    int dot)
{
    struct Mock230Server* srv = (struct Mock230Server*)state->env->host.user;

    (void)dot;

    switch( opcode )
    {
    /*
     * `[command,obj_type]()(obj)` — engine.rs2:721. Nothing in, the obj id out.
     *
     * The reference runs `check(state.activeObj.type, ObjTypeValid)` first; an
     * id got here by being on the floor, and `mock230_world_obj_add` refuses a
     * negative one, so there is nothing left for that check to catch.
     */
    case SS_OP_OBJ_TYPE:
    {
        struct Mock230GroundObj* obj = active_obj(state, opcode);

        if( !obj )
            return 1;
        SSVM_PushInt(state, obj->obj_id);
        return 1;
    }

    /*
     * `[command,obj_count]()(int)` — engine.rs2:719.
     *
     * The reference gates on `obj.isValid(activePlayer.hash64)` and pushes 0
     * for an obj this player may not see yet — its per-killer loot window,
     * where a drop belongs to its killer for a minute before going public.
     * There is no receiver on `struct Mock230GroundObj` and no such window
     * here, so the guard degenerates to "it is on the floor", which
     * `active_obj` has already established. Stated rather than silently
     * dropped: when receivers land (mock230_zone.h names the same gap for the
     * state write) this is one of the two places that has to learn about them,
     * the other being obj_takeitem.
     */
    case SS_OP_OBJ_COUNT:
    {
        struct Mock230GroundObj* obj = active_obj(state, opcode);

        if( !obj )
            return 1;
        SSVM_PushInt(state, obj->count);
        return 1;
    }

    /* `[command,obj_coord]()(coord)` — engine.rs2:725. The same packing every
     * other coord op in this server uses, so `distance(coord, obj_coord)`
     * compares like with like. */
    case SS_OP_OBJ_COORD:
    {
        struct Mock230GroundObj* obj = active_obj(state, opcode);

        if( !obj )
            return 1;
        SSVM_PushInt(state, mock230_coord_pack(obj->level, obj->x, obj->z));
        return 1;
    }

    /*
     * `[command,obj_takeitem](inv $inv)` — engine.rs2:723. Pops the container,
     * pushes nothing.
     *
     * Two deliberate differences from `ObjOps.ts`, both in the same direction:
     *
     * 1. The reference calls `invAdd(...)` with `assureFullInsertion` defaulted
     *    to true and then removes the obj **unconditionally** — so a full
     *    backpack destroys the pile. Its own content never reaches that
     *    (`~pickup_obj_check_for_space` runs `inv_itemspace` first), but the
     *    opcode is a primitive and content that forgets the check should not
     *    delete items. Nothing is added and nothing is removed here unless all
     *    of it fits.
     * 2. No `wealth_event`; there is no wealth log in this tree.
     *
     * The removal is `mock230_world_ground_take`, which is the engine's own
     * take: the zone event is queued while the obj is still filed, the obj is
     * unfiled so a client loading the zone afterwards is sent state without it,
     * and a map spawn is armed to return. That is why the pile vanishes for
     * every client that can see it and not just for the taker.
     */
    case SS_OP_OBJ_TAKEITEM:
    {
        int32_t inv_id;
        struct Mock230GroundObj* obj;
        struct Mock230Container* row;
        int slot;

        if( !SSVM_PopInt(state, &inv_id) )
            return 1;
        obj = active_obj(state, opcode);
        if( !obj )
            return 1;
        row = mock230_container_resolve(srv, srv->active_player, inv_id);
        if( !row )
        {
            SSVM_Abort(state, "obj_takeitem into unknown container %d", (int)inv_id);
            return 1;
        }
        if( mock230_container_add(row, obj->obj_id, obj->count, 1) <= 0 )
            return 1;
        slot = (int)(obj - srv->ground);
        mock230_world_ground_take(srv, slot);
        return 1;
    }

    /*
     * `[command,obj_del]` — engine.rs2:717. Nothing in, nothing out.
     *
     * `World.removeObj(activeObj, ObjType.respawnrate)` in the reference. The
     * duration is the one thing that does not port literally: `respawnrate` is
     * a per-obj server-band field with no decoder, no `fields/obj.ini` row and
     * no value anywhere in this tree. `mock230_world_ground_take` substitutes
     * content's `^lootdrop_duration` for it, which is not a new invention —
     * it is the substitution the engine's own take has been making all along,
     * and `removeObj` ignores the duration entirely for a non-spawn obj, so
     * the two agree exactly on every drop. Where they differ is a map spawn:
     * the reference brings each obj back at its own rate, this brings all of
     * them back at one. That is a data gap, and it is the pre-existing one.
     */
    case SS_OP_OBJ_DEL:
    {
        struct Mock230GroundObj* obj = active_obj(state, opcode);

        if( !obj )
            return 1;
        mock230_world_ground_take(srv, (int)(obj - srv->ground));
        return 1;
    }

    /*
     * `[command,oc_wearpos](obj $obj)(int)` — engine.rs2:781, and the same
     * shape for `oc_wearpos2` (:783) and `oc_wearpos3` (:785). Reference
     * handler: `ObjConfigOps.ts`, `check(popInt(), ObjTypeValid).wearpos{,2,3}`.
     *
     * `wearpos` is where the item goes; `wearpos_2` / `wearpos_3` are the
     * further slots it *claims* — a two-handed weapon's shield slot, the hair
     * and jaw a full helm covers. Config opcodes 13, 14 and 27, decoded into
     * `Mock230ObjInfo` at boot for every obj in the cache.
     *
     * -1 is RuneScript `null` (`ssc_symbols.c:1186`), and it is the value
     * `mock230_objinfo` already reports for an unworn item and for an id the
     * cache does not describe. No sentinel is invented here and none is needed:
     * the two spellings coincide, which is why `if (oc_wearpos($obj) = null)`
     * in the reference's `~try_equip` ports as written.
     *
     * The danger these three close is not that they were missing. All three
     * have `known = 1` in `ss_meta.gen.h`, so a script calling one **compiled
     * and ran** — into `unimplemented_stub`, which pushes 0. And 0 is
     * `^wearpos_hat`, a legal slot. So `~wearpos_conflicts` would have found
     * every worn item conflicting with every other and unequipped the lot,
     * silently and plausibly, with nothing in the log.
     */
    case SS_OP_OC_WEARPOS:
    case SS_OP_OC_WEARPOS2:
    case SS_OP_OC_WEARPOS3:
    {
        int32_t obj_id;
        const struct Mock230ObjInfo* info;

        if( !SSVM_PopInt(state, &obj_id) )
            return 1;
        info = mock230_objinfo((int)obj_id);
        SSVM_PushInt(state, opcode == SS_OP_OC_WEARPOS    ? info->wearpos
                            : opcode == SS_OP_OC_WEARPOS2 ? info->wearpos_2
                                                          : info->wearpos_3);
        return 1;
    }

    /*
     * `[command,oc_cost](obj $obj)(int)` — engine.rs2:803.
     * `ObjConfigOps.ts` `OC_COST` → `ObjType.cost`. Store value, not high alch.
     */
    case SS_OP_OC_COST:
    {
        int32_t obj_id;

        if( !SSVM_PopInt(state, &obj_id) )
            return 1;
        SSVM_PushInt(state, mock230_objinfo((int)obj_id)->cost);
        return 1;
    }

    /*
     * `[command,oc_members](obj $obj)(boolean)` — engine.rs2:793.
     */
    case SS_OP_OC_MEMBERS:
    {
        int32_t obj_id;

        if( !SSVM_PopInt(state, &obj_id) )
            return 1;
        SSVM_PushInt(state, mock230_objinfo((int)obj_id)->members);
        return 1;
    }

    /*
     * `[command,oc_tradeable](obj $obj)(boolean)` — engine.rs2:805.
     */
    case SS_OP_OC_TRADEABLE:
    {
        int32_t obj_id;

        if( !SSVM_PopInt(state, &obj_id) )
            return 1;
        SSVM_PushInt(state, mock230_objinfo((int)obj_id)->tradeable);
        return 1;
    }

    /*
     * `[command,obj_addall](coord $coord, namedobj $obj, int $count, int $duration)`
     * — engine.rs2:727. `ObjOps.ts` OBJ_ADDALL: like obj_add but always public
     * (no receiver). Used for death bones.
     */
    case SS_OP_OBJ_ADDALL:
    {
        int32_t values[4];
        int last = -1;

        for( int i = 3; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        if( values[1] < 0 || values[2] <= 0 )
            return 1;
        {
            int obj_id = (int)values[1];
            int count = (int)values[2];
            int duration = values[3] > 0 ? (int)values[3] : -1;
            int x = mock230_coord_x(values[0]);
            int z = mock230_coord_z(values[0]);
            int level = mock230_coord_level(values[0]);

            if( !mock230_objinfo(obj_id)->stackable || count == 1 )
            {
                for( int i = 0; i < count; i++ )
                    last = mock230_world_obj_add(srv, obj_id, 1, x, z, level, duration);
            }
            else
            {
                last = mock230_world_obj_add(srv, obj_id, count, x, z, level, duration);
            }
            if( last >= 0 )
                SSVM_SetActive(state, SSVM_ENT_OBJ, SSVM_PRIMARY,
                               (void*)mock230_world_obj_handle(srv, last));
        }
        return 1;
    }

    /*
     * `[command,obj_find](coord $coord, obj $obj)(boolean)` — engine.rs2:743.
     * `ObjOps.ts` OBJ_FIND: look for that obj on that tile, make it active on a
     * hit, and push whether there was one.
     *
     * This is the *other* way an obj becomes active. Every existing route is a
     * side effect of putting one there (`obj_add`, `obj_addall`, `inv_drop*`) or
     * of a player clicking one (`pending_active_obj`), so a script could only
     * ever act on an obj it had just created or been handed. `obj_find` is how
     * content asks about an obj that was already lying there — which is what
     * every caller in the reference wants: `quest_death.rs2` reads five tiles it
     * never dropped anything on to see which cannonball is on each.
     *
     * Two differences from the reference, both because its extra state does not
     * exist here rather than because a decision was made:
     *
     * 1. No receiver filter. `World.getObj(..., player.hash64)` skips a pile
     *    that is still private to somebody else; `mock230_world_ground_find` has
     *    no receiver to compare against (mock230_zone.h names that gap), so a
     *    drop is visible to every script the tick it lands. Content cannot tell
     *    the difference yet because nothing in this tree drops privately.
     * 2. No `ObjTypeValid` / `CoordValid` aborts. A bad obj id or a tile off the
     *    map simply does not match, and "not there" is the honest answer to
     *    "is there one there" — where an abort would turn a content typo in a
     *    five-way `|` chain into a dead script.
     *
     * On a miss the active obj is deliberately left alone rather than cleared.
     * That is the reference's behaviour (it returns before `pointerAdd`) and it
     * is what makes `if (obj_find(...) = false) { ... }` safe to write after an
     * earlier find — a failed lookup is not allowed to steal the obj a previous
     * one established.
     */
    case SS_OP_OBJ_FIND:
    {
        int32_t coord;
        int32_t obj_id;
        int slot;

        if( !SSVM_PopInt(state, &obj_id) )
            return 1;
        if( !SSVM_PopInt(state, &coord) )
            return 1;

        slot = mock230_world_ground_find(srv, mock230_coord_x(coord), mock230_coord_z(coord),
                                        mock230_coord_level(coord), (int)obj_id);
        if( slot < 0 )
        {
            SSVM_PushInt(state, 0);
            return 1;
        }
        SSVM_SetActive(state, SSVM_ENT_OBJ, SSVM_PRIMARY,
                       (void*)mock230_world_obj_handle(srv, slot));
        SSVM_PushInt(state, 1);
        return 1;
    }

    default:
        return 0;
    }
}
