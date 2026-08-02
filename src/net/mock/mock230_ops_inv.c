/*
 * The inv domain's *moves* — one container to another, and a container to the
 * floor.
 *
 * Sixth of the per-domain opcode files (mock230_ops_db.c is the first and its
 * header states the contract): `mock230_script_command` offers each domain the
 * opcode in turn and each returns 1 when it handled it.
 *
 * Where LostCity puts it: engine, `engine/src/engine/script/handlers/InvOps.ts`
 * over `Player.invMoveFromSlot` / `invDel` / `invAdd`
 * (`engine/src/engine/entity/Player.ts:1679`). Primitives, there and here —
 * the *rule* that uses them (`~equip`, `~try_equip`, `~dropslot`) is content
 * in both trees.
 *
 * ------------------------------------------------------------------
 * What is here and what is deliberately not
 * ------------------------------------------------------------------
 *
 * `inv_moveitem` and its `_cert` / `_uncert` siblings moved here from
 * mock230_scripts.c's switch **unchanged in their three bank arms**, and grew a
 * fourth: a generic container-to-container move. The three bank arms come
 * first, byte for byte, so that no bank behaviour can change by this file
 * existing; the generic arm is what they used to fall past into
 *
 *     mock230: inv_moveitem %d -> %d is not modelled
 *
 * which is a printf and a silent no-op, and which every reference equip and
 * unequip path walks straight into (`inv_moveitem(inv, worn, …)`,
 * `inv_moveitem(worn, inv, …)`). Worth being precise about why that gap was
 * invisible: the opcode has a `case` label, so `gen_opcode_coverage.py` counted
 * it **covered**. Coverage is a question about labels, not about arms.
 *
 * The rest of the family — `inv_add`, `inv_del`, `inv_delslot`, `inv_getobj`,
 * `inv_itemspace`, `inv_movetoslot`, `inv_size`, `inv_total` … — stays in
 * mock230_scripts.c. They predate the split and moving them is a second change
 * wearing this one's clothes; `mock230_ops_obj.c` says the same about `oc_name`.
 * New inv opcodes land here.
 *
 * ------------------------------------------------------------------
 * Overflow goes on the floor, and that is ported rather than chosen
 * ------------------------------------------------------------------
 *
 * All three moves below can be asked to put more into a container than fits.
 * `InvOps.ts:339-348`, `:522-530` and `:245-253` all answer it the same way and
 * this is a port of that answer: whatever did not fit becomes a ground obj at
 * the player's own tile — **singly** for an unstackable obj (one pile per unit,
 * which is what makes twelve dropped bones twelve piles) and as one pile
 * otherwise.
 *
 * The duration is the reference's literal `200` in two of the three. Here it is
 * `mock230_ids()->lootdrop_duration`, which is content's `^lootdrop_duration`
 * and is 200 — the same number, resolved through the pack rather than restated
 * in C, which is PORTING_GUIDE §2.4 item 2. `inv_dropslot` takes its duration
 * as an argument in both trees and does not need it.
 */

#include "mock230.h"
#include "mock230_bank.h"
#include "mock230_container.h"
#include "mock230_ids.h"

#include "ss_opcode.h"
#include "ssvm.h"

#include <stdint.h>
#include <stdio.h>

/**
 * Put `count` of `obj_id` on the floor under the player, the way `InvOps.ts`
 * does it: one pile per unit for an unstackable obj, one pile otherwise.
 *
 * Returns the ground slot of the **last** pile, or -1. `inv_dropslot` makes
 * that its active obj, which is the reference's `state.activeObj = floorObj`
 * inside the same loop — the last write wins there too.
 */
static int
spill_to_floor(
    struct Mock230Server* srv,
    int obj_id,
    int count,
    int duration)
{
    struct Mock230Player* player = srv->active_player;
    int last = -1;

    if( count <= 0 )
        return -1;
    if( !mock230_objinfo(obj_id)->stackable && count > 1 )
    {
        for( int i = 0; i < count; i++ )
            last = mock230_world_obj_add(srv, obj_id, 1, player->x, player->z, player->level,
                                         duration);
        return last;
    }
    return mock230_world_obj_add(srv, obj_id, count, player->x, player->z, player->level,
                                 duration);
}

/** The container `inv_id` names, aborting the script having said so. */
static struct Mock230Container*
container(
    struct SSVM_State* state,
    int32_t inv_id,
    int opcode)
{
    struct Mock230Server* srv = (struct Mock230Server*)state->env->host.user;
    struct Mock230Container* row =
        srv ? mock230_container_resolve(srv, srv->active_player, inv_id) : NULL;

    if( !row )
        SSVM_Abort(state, "%s on unknown container %d", SSVM_OpcodeName(opcode), (int)inv_id);
    return row;
}

/** Take up to `count` of `obj_id` out, first matching slot first, and say how
 *  much came out. `Player.invDel` without its slot hint, which no caller here
 *  passes. */
static int
container_del(
    struct Mock230Container* row,
    int obj_id,
    int count)
{
    int remaining = count;

    for( int i = 0; i < row->slots && remaining > 0; i++ )
    {
        if( row->items[i].obj_id != obj_id )
            continue;
        if( row->items[i].count > remaining )
        {
            mock230_container_set(row, i, obj_id, row->items[i].count - remaining);
            remaining = 0;
        }
        else
        {
            remaining -= row->items[i].count;
            mock230_container_set(row, i, -1, 0);
        }
    }
    return count - remaining;
}

int
mock230_ops_inv(
    struct SSVM_State* state,
    int opcode,
    int dot)
{
    struct Mock230Server* srv = (struct Mock230Server*)state->env->host.user;

    (void)dot;

    switch( opcode )
    {
    /*
     * `[command,inv_movefromslot](inv $from_inv, inv $to_inv, int $from_slot)`
     * — engine.rs2:861. Three ints in, nothing out.
     *
     * `Player.invMoveFromSlot` (Player.ts:1679): empty the from-slot, add all
     * of it to the to-container, spill what did not fit.
     *
     * The reference **throws** on an empty from-slot rather than returning, and
     * that is ported as an abort rather than softened: `~try_equip` calls this
     * only inside `if (inv_getobj(inv, $inv_slot) ! null)`, so reaching it with
     * an empty slot means the caller's own test disagreed with the container,
     * and silently doing nothing there is how a swap half-happens.
     *
     * Note the reference's own use is `inv_movefromslot(inv, inv, $inv_slot)` —
     * from a container to *itself*. That is not a mistake and it is why the
     * body must delete before it adds: it is "move this to the first free slot
     * of the same container", which is what makes a stackable item that landed
     * in a swapped-out cell merge onto the stack it belongs to.
     */
    case SS_OP_INV_MOVEFROMSLOT:
    {
        int32_t from_inv;
        int32_t to_inv;
        int32_t from_slot;
        struct Mock230Container* from;
        struct Mock230Container* to;
        int obj_id;
        int count;
        int moved;

        if( !SSVM_PopInt(state, &from_slot) || !SSVM_PopInt(state, &to_inv) ||
            !SSVM_PopInt(state, &from_inv) )
            return 1;
        from = container(state, from_inv, opcode);
        to = container(state, to_inv, opcode);
        if( !from || !to )
            return 1;
        if( from_slot < 0 || from_slot >= from->slots )
        {
            SSVM_Abort(state, "inv_movefromslot: invalid from slot %d", (int)from_slot);
            return 1;
        }
        obj_id = from->items[from_slot].obj_id;
        count = from->items[from_slot].count;
        if( obj_id < 0 )
        {
            SSVM_Abort(state, "inv_movefromslot: slot %d is empty", (int)from_slot);
            return 1;
        }
        mock230_container_set(from, (int)from_slot, -1, 0);
        moved = mock230_container_add(to, obj_id, count, 0);
        spill_to_floor(srv, obj_id, count - moved, mock230_ids()->lootdrop_duration);
        return 1;
    }

    /*
     * `[command,inv_dropslot](inv $inv, coord $coord, int $slot, int $duration)`
     * — engine.rs2:837. Four ints in, nothing out.
     *
     * `InvOps.ts:213`. Empty the slot and put what was in it on the floor at
     * `$coord`, singly for an unstackable obj, then make the last pile the
     * active obj so the caller can keep acting on it.
     *
     * Two halves of the reference are deliberately absent and both are data
     * this tree does not have: the `wealth_event` log (no such log), and the
     * `invType.protect` / `scope` access check (no `fields/inv.ini`, the same
     * single gap `mock230_container_scope` is blocked on — its header says so).
     * Neither changes what the drop does; both are stated so that a tree which
     * grows `inv.ini` knows this is one of the places that has to learn.
     *
     * `$coord` is honoured rather than assumed to be the player's tile: the
     * reference reads it, `~dropslot` passes `coord`, and a drop that ignored
     * it would be indistinguishable from a correct one until the first script
     * that drops somewhere else.
     */
    case SS_OP_INV_DROPSLOT:
    {
        int32_t inv_id;
        int32_t coord;
        int32_t slot;
        int32_t duration;
        struct Mock230Container* row;
        int obj_id;
        int count;
        int last = -1;

        if( !SSVM_PopInt(state, &duration) || !SSVM_PopInt(state, &slot) ||
            !SSVM_PopInt(state, &coord) || !SSVM_PopInt(state, &inv_id) )
            return 1;
        row = container(state, inv_id, opcode);
        if( !row )
            return 1;
        if( slot < 0 || slot >= row->slots )
        {
            SSVM_Abort(state, "inv_dropslot: invalid slot %d", (int)slot);
            return 1;
        }
        obj_id = row->items[slot].obj_id;
        count = row->items[slot].count;
        if( obj_id < 0 )
        {
            SSVM_Abort(state, "inv_dropslot: slot %d is empty", (int)slot);
            return 1;
        }
        mock230_container_set(row, (int)slot, -1, 0);
        if( !mock230_objinfo(obj_id)->stackable && count > 1 )
        {
            for( int i = 0; i < count; i++ )
                last = mock230_world_obj_add(srv, obj_id, 1, mock230_coord_x(coord),
                                             mock230_coord_z(coord), mock230_coord_level(coord),
                                             (int)duration);
        }
        else
        {
            last = mock230_world_obj_add(srv, obj_id, count, mock230_coord_x(coord),
                                         mock230_coord_z(coord), mock230_coord_level(coord),
                                         (int)duration);
        }
        /* `state.activeObj = floorObj; state.pointerAdd(ActiveObj)` — the last
         * pile, because the reference's loop assigns on every iteration. The
         * handle rather than the slot, for the reason mock230_ops_obj.c's
         * header gives. */
        if( last >= 0 )
            SSVM_SetActive(state, SSVM_ENT_OBJ, SSVM_PRIMARY,
                           (void*)mock230_world_obj_handle(srv, last));
        return 1;
    }

    /*
     * inv_moveitem / _cert / _uncert — engine.rs2:873, :869, :871.
     * `(inv $from, inv $to, obj $obj, int $count)`, nothing out.
     *
     * The three bank arms below are what this opcode has always done and are
     * moved here unchanged. The cert pair is what makes a bank hold one stack
     * of an item rather than two: depositing un-notes on the way in, and
     * withdrawing notes on the way out only if the caller asked for it. Both
     * directions go through mock230_bank, which owns the space checks and their
     * three messages.
     *
     * The generic arm is appended **after** them, so the bank cannot change
     * behaviour by this arm existing. It is `InvOps.ts:499`: delete from the
     * source, add what actually came out to the destination, spill the rest.
     * Deleting first is the reference's order and it matters for
     * `inv_moveitem(worn, inv, …)` on a full backpack — take the item off the
     * body, fail to place it, and it lands at your feet rather than being
     * duplicated or destroyed.
     *
     * `_cert` / `_uncert` fall into the generic arm too, without their
     * note conversion, and that is a **stated limit rather than a silent one**:
     * the note form is `mock230_bank`'s, keyed on the bank's own paired-obj
     * table, and there is no note conversion outside it in this tree. Nothing
     * calls the cert forms on a non-bank pair today. If something does, it gets
     * the plain move — which is the one wrong answer of the three available
     * (the others being "no-op" and "abort"), because it moves the right item
     * in the right direction and only its *form* is unconverted.
     */
    case SS_OP_INV_MOVEITEM:
    case SS_OP_INV_MOVEITEM_CERT:
    case SS_OP_INV_MOVEITEM_UNCERT:
    {
        int32_t from_inv;
        int32_t to_inv;
        int32_t obj_id;
        int32_t count;

        if( !SSVM_PopInt(state, &count) || !SSVM_PopInt(state, &obj_id) ||
            !SSVM_PopInt(state, &to_inv) || !SSVM_PopInt(state, &from_inv) )
            return 1;
        if( from_inv == mock230_ids()->inv_bank )
        {
            int slot = -1;

            for( int i = 0; i < srv->active_player->bank.size; i++ )
                if( srv->active_player->bank.slots[i].obj_id == obj_id )
                    slot = i;
            if( slot < 0 )
                return 1;
            /* The opcode decides the form, not the bank's own note toggle:
             * `inv_moveitem_cert` means "as a note" wherever it is called
             * from. Set the flag, move, put it back. */
            {
                int saved = srv->active_player->bank.note_mode;

                srv->active_player->bank.note_mode = opcode == SS_OP_INV_MOVEITEM_CERT;
                mock230_bank_withdraw(srv, slot, (int)count);
                srv->active_player->bank.note_mode = saved;
            }
            return 1;
        }
        if( to_inv == mock230_ids()->inv_bank && from_inv == mock230_ids()->inv_backpack )
        {
            for( int i = 0; i < MOCK230_INV_SLOTS && count > 0; i++ )
            {
                if( srv->active_player->inv[i].obj_id != obj_id )
                    continue;
                count -= mock230_bank_deposit(srv, i, (int)count);
            }
            return 1;
        }
        if( to_inv == mock230_ids()->inv_bank && from_inv == mock230_ids()->inv_worn )
        {
            /* Straight off the body and into the bank, which is what the
             * deposit-worn button is. Going via the backpack would need a free
             * slot the player may not have. */
            for( int i = 0; i < MOCK230_WORN_SLOTS && count > 0; i++ )
            {
                if( srv->active_player->worn[i].obj_id != obj_id )
                    continue;
                count -= mock230_bank_deposit_worn(srv, i, (int)count);
            }
            return 1;
        }
        {
            struct Mock230Container* from = container(state, from_inv, opcode);
            struct Mock230Container* to = container(state, to_inv, opcode);
            int taken;
            int moved;

            if( !from || !to )
                return 1;
            taken = container_del(from, (int)obj_id, (int)count);
            if( taken <= 0 )
                return 1;
            moved = mock230_container_add(to, (int)obj_id, taken, 0);
            spill_to_floor(srv, (int)obj_id, taken - moved, mock230_ids()->lootdrop_duration);
        }
        return 1;
    }

    default:
        return 0;
    }
}
