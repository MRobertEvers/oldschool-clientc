/*
 * The `npc_*` / `nc_*` reads, and the hunt iterators.
 *
 * Fourth of the per-domain opcode files (mock230_ops_db.c is the first and its
 * header states the contract): `mock230_script_command` offers each domain the
 * opcode in turn and each returns 1 when it handled it.
 *
 * What is here is what can be answered from a *record* plus the npc roster:
 * the config reads (`npc_param`, `npc_category`, `nc_category`, `npc_hasop`)
 * and the three coord-and-radius searches (`npc_huntall`, `npc_hunt`,
 * `npc_findcat`). What is deliberately not here is everything that needs
 * per-npc engine state this server does not keep — see "What this file refuses"
 * at the bottom.
 *
 * ------------------------------------------------------------------
 * npc_param was implemented and wrong, which is the worst of the three states
 * ------------------------------------------------------------------
 *
 * `mock230_scripts.c` has carried a `case SS_OP_NPC_PARAM:` since before the
 * param family existed. It compared the popped param id against one
 * `mock230_content_symbol(MOCK230_PACK_PARAM, "death_drop")` — a game-facing
 * name spelled in C, which this project's own rules forbid — pushed that one
 * field, and pushed 0 for every other param. It never consulted
 * `mock230_npc_param`, which is loaded and public and would have answered; it
 * never reached `mock230_push_typed_param`, so every result landed on the int
 * stack whatever `configs/all.param` declared; and it ignored `default=`.
 *
 * That is invisible to every check this repo has. `gen_opcode_coverage.py`
 * keys off the presence of a `case SS_OP_*:` label, so 2529 has been counted as
 * covered all along, the load-time gap report has been silent about it, and
 * `--selftest` has been green through it. Meanwhile it is 291 call sites across
 * 137 files of the reference tree, and the committed combat slice reads npc
 * defence bonuses and damage types through it:
 * `skill_combat/combat_stats.rs2:383,459,489`.
 *
 * This file claims the opcode. The domain hooks run *before* the switch
 * (mock230_scripts.c's `mock230_script_command`), so the old case is now dead
 * code that can never execute. **It should be deleted**, and deleting it is a
 * second edit to mock230_scripts.c which this lane's rules do not allow — one
 * hook line and nothing else. Stated here rather than left to be discovered:
 * mock230_scripts.c's `case SS_OP_NPC_PARAM:` is unreachable and a change to it
 * changes nothing.
 *
 * ------------------------------------------------------------------
 * Reading a field means reading the ungated record
 * ------------------------------------------------------------------
 *
 * `mock230_npcinfo()` reports a "Someone" placeholder for a record with no
 * name, which is right for text and wrong for a field: 1,585 of the 9,149
 * cache.osrs239 npc records that carry a category carry no name, and 177 that
 * declare a menu op carry none. `mock230_npcinfo_record()` is the ungated row
 * and is what every read here goes through.
 *
 * ------------------------------------------------------------------
 * The active npc rides in host_tag, and there is no secondary
 * ------------------------------------------------------------------
 *
 * `SSVM_SetActive(..., SSVM_ENT_NPC, ...)` is paired with `state->host_tag =
 * slot + 1` at every one of its call sites, and the slot is what survives: a
 * parked script outlives its tick and an npc can despawn or have its slot
 * reused while the script waits, so a stored pointer would resolve to a
 * different npc wearing the same address.
 *
 * Nothing in this server ever sets the *secondary* npc, so a `.npc_category` /
 * `.npc_hasop` / `.npc_hunt` dot form is refused by the VM's own pointer table
 * (require2 = 0x020, never added) before reaching here. That is a pre-existing
 * tree-wide simplification, not a decision of this file; every `npc_*` op here
 * behaves exactly as `npc_type` and `npc_coord` already do.
 *
 * ------------------------------------------------------------------
 * Where LostCity puts these
 * ------------------------------------------------------------------
 *
 * engine/src/engine/script/handlers/NpcOps.ts — `NPC_PARAM`, `NPC_CATEGORY`
 * (:68), `NPC_HASOP` (:526), `NPC_HUNT` (:290), `NPC_HUNTALL` (:325),
 * `NPC_FINDCAT` (:369) — and NpcConfigOps.ts for `NC_CATEGORY`. Engine, one
 * handler per opcode, each a read of `NpcType` or a walk of the zone map. The
 * records themselves are content, and here they are in the cache:
 * mock230_npcinfo.c decodes them once at boot.
 */

#include "mock230.h"
#include "mock230_content.h"
#include "mock230_scene.h"

#include "ss_meta.h"
#include "ss_opcode.h"
#include "ssvm.h"

#include <stdint.h>

/*
 * The active npc, or NULL having said why.
 *
 * The VM's pointer table has already refused the opcode when no npc is active
 * (require 0x010), so this is not re-checking that. It is checking what the
 * table cannot: whether the slot still holds the same live npc.
 */
static struct Mock230Npc*
active_npc(
    struct SSVM_State* state,
    int opcode)
{
    struct Mock230Server* srv = (struct Mock230Server*)state->env->host.user;
    int slot = (int)state->host_tag - 1;

    if( !srv || slot < 0 || slot >= MOCK230_NPC_MAX || !srv->npcs[slot].active )
    {
        SSVM_Abort(state, "%s: the active npc is gone", SSVM_OpcodeName(opcode));
        return NULL;
    }
    return &srv->npcs[slot];
}

/*
 * Make `slot` the active npc and report success, exactly as `npc_find` does.
 *
 * The reference's `state.pointerAdd(ActiveNpc[state.intOperand])` addresses the
 * secondary npc for a dot form; this server has only a primary, so the three
 * finders here set the primary the same way every other finder in the tree
 * does. See the header.
 */
static void
set_active_npc(
    struct SSVM_State* state,
    struct Mock230Server* srv,
    int slot)
{
    SSVM_SetActive(state, SSVM_ENT_NPC, SSVM_PRIMARY, &srv->npcs[slot]);
    state->host_tag = slot + 1;
    SSVM_PointerAdd(state, SSVM_PTR_ACTIVE_NPC);
}

/*
 * The filter `npc_hunt` and `npc_huntall` share, and the reason they are not
 * `npc_findallany`.
 *
 * `NpcHuntAllCommandIterator` (ScriptIterators.ts:264-291) skips any npc whose
 * *type* declares no `op[1]` — the second menu op, conventionally Attack.
 * `npc_hasop`'s own indexing (`npcType.op[op - 1]`, NpcOps.ts:538) is what
 * fixes which element that is. So "huntall" does not mean "every npc nearby";
 * it means every npc nearby that something could be done to, and an
 * implementation without the filter would hand content the scenery.
 */
static int
huntable(int npc_type)
{
    const struct Mock230NpcInfo* info = mock230_npcinfo_record(npc_type);

    return info && info->ops[1] != NULL;
}

/** Chebyshev tiles, which is what the reference's `distanceToSW` reduces to for
 *  the size-1 npcs this roster holds — the same measure `npc_find` uses. */
static int
coord_range(
    const struct Mock230Npc* npc,
    int32_t coord)
{
    int dx = npc->x - mock230_coord_x(coord);
    int dz = npc->z - mock230_coord_z(coord);

    if( dx < 0 )
        dx = -dx;
    if( dz < 0 )
        dz = -dz;
    return dx > dz ? dx : dz;
}

/*
 * Euclidean *squared*, which is what `npc_hunt` and `npc_findcat` rank by
 * (NpcOps.ts:305, :385) — not the Chebyshev range they filter by. The two
 * disagree on diagonals, so ranking by the filter's measure would pick a
 * different npc from the reference whenever two candidates sit at the same
 * Chebyshev range.
 */
static int
euclidean_sq(
    const struct Mock230Npc* npc,
    int32_t coord)
{
    int dx = npc->x - mock230_coord_x(coord);
    int dz = npc->z - mock230_coord_z(coord);

    return (dx * dx) + (dz * dz);
}

/*
 * The one walk the three searches share: every live npc on the coord's level
 * within `distance`, in slot order.
 *
 * `checkvis` is LostCity HuntVis: 0 off, 1 lineofsight, 2 lineofwalk. The ray
 * runs source→npc (ScriptIterators.ts NpcIterator).
 */
static int
search_matches(
    const struct Mock230Npc* npc,
    int32_t coord,
    int32_t distance,
    int32_t checkvis)
{
    if( !npc->active || npc->level != mock230_coord_level(coord) )
        return 0;
    if( coord_range(npc, coord) > distance )
        return 0;
    return mock230_scene_checkvis(checkvis, npc->level, mock230_coord_x(coord),
                                  mock230_coord_z(coord), npc->x, npc->z);
}

int
mock230_ops_npc(
    struct SSVM_State* state,
    int opcode,
    int dot)
{
    struct Mock230Server* srv = (struct Mock230Server*)state->env->host.user;

    (void)dot;

    switch( opcode )
    {
    /*
     * `[command,npc_param](param $param)(any)` — engine.rs2:563. One int in,
     * one value out on whichever stack `configs/all.param` declares it on.
     *
     * The cache half is keyed on `npc->type`, not on the slot: params belong to
     * the record, and an npc that has been through `npc_changetype` must read
     * the new type's.
     *
     * The authored half is keyed on `npc->def`, and after a `npc_changetype`
     * those two disagree — `SS_OP_NPC_CHANGETYPE` writes `npc->type` and leaves
     * `npc->def` on the block the npc spawned from. That is pre-existing (every
     * other reader of `def` has the same view, including `npc_stat` and the
     * combat animations) and the reference does re-derive from the new type
     * (`Npc.changeType` -> `NpcType.get`). Recorded rather than papered over:
     * reseeding the def belongs with `npc_changetype`, not here.
     */
    case SS_OP_NPC_PARAM:
    {
        int32_t param_id;
        int32_t authored;
        struct Mock230Npc* npc;
        const struct Mock230NpcParam* row;

        if( !SSVM_PopInt(state, &param_id) )
            return 1;
        npc = active_npc(state, opcode);
        if( !npc )
            return 1;

        /*
         * Authored overlay first, cache record second — the rank-1-over-rank-0
         * merge CONTENT_ARCHITECTURE.md §3.1 states, and the same precedence
         * `npc_def_seed_from_cache` already gives the combat bonuses.
         *
         * It is not an optimisation. `death_drop` is a server-band param: the
         * value exists only in a rank-1 `.npc` overlay and is nowhere in the
         * cache record's param table, so a cache-only read answers 0 and the
         * drop tables add obj 0. That regression is the reason this branch is
         * here and the reason there is a fallback at all.
         *
         * Authored values are always ints — `apply_param` parses `atoi` or a
         * pack symbol — so they go through the same typed push with sval NULL,
         * which lets a param declared a string over an authored int abort here
         * rather than desynchronise the stacks downstream.
         */
        if( mock230_content_npc_param(npc->def, param_id, &authored) )
        {
            mock230_push_typed_param(state, param_id, NULL, authored, 1, "npc", npc->type);
            return 1;
        }
        row = mock230_npc_param(npc->type, param_id);
        mock230_push_typed_param(state, param_id, row ? row->sval : NULL,
                                 row ? row->ival : 0, row != NULL, "npc", npc->type);
        return 1;
    }

    /*
     * `[command,npc_category]()(category)` — engine.rs2:521 — and
     * `[command,nc_category](npc $npc)(category)` at :741. Same field, one off
     * the active npc's type and one off an id on the stack.
     *
     * The reference runs `check(type, NpcTypeValid)` on the `nc_` form and
     * throws for an id with no record. Here the record table can legitimately
     * be absent (a server booted without a cache still runs), so an unknown id
     * reports the decoder's own "no category stated" 0 rather than aborting —
     * and 0 is deliberately not a name in pack/category.pack, so content
     * comparing against a named category cannot match it by accident.
     */
    case SS_OP_NPC_CATEGORY:
    case SS_OP_NC_CATEGORY:
    {
        const struct Mock230NpcInfo* info;
        int32_t npc_type;

        if( opcode == SS_OP_NC_CATEGORY )
        {
            if( !SSVM_PopInt(state, &npc_type) )
                return 1;
        }
        else
        {
            struct Mock230Npc* npc = active_npc(state, opcode);

            if( !npc )
                return 1;
            npc_type = npc->type;
        }

        info = mock230_npcinfo_record(npc_type);
        SSVM_PushInt(state, info ? info->category : 0);
        return 1;
    }

    /*
     * `[command,npc_hasop](int $op)(boolean)` — engine.rs2:649.
     *
     * 1-based, and that is the whole content of the opcode: NpcOps.ts:538 reads
     * `npcType.op[op - 1]`, so `npc_hasop(1)` asks about op1. Off by one here
     * answers about the neighbouring menu entry, which for the usual
     * `op2=Attack` record is the difference between "can be talked to" and "can
     * be attacked".
     *
     * An op index outside 1..5 is 0 rather than an abort: the reference reads
     * past its own array and gets undefined, which is falsy, and content that
     * asks about a sixth op is asking a question whose answer is genuinely no.
     */
    case SS_OP_NPC_HASOP:
    {
        int32_t op;
        struct Mock230Npc* npc;
        const struct Mock230NpcInfo* info;

        if( !SSVM_PopInt(state, &op) )
            return 1;
        npc = active_npc(state, opcode);
        if( !npc )
            return 1;

        info = mock230_npcinfo_record(npc->type);
        if( !info || op < 1 || op > 5 )
        {
            SSVM_PushInt(state, 0);
            return 1;
        }
        SSVM_PushInt(state, info->ops[op - 1] != NULL ? 1 : 0);
        return 1;
    }

    /*
     * `[command,npc_huntall](coord $source, int $distance, int $checkvis)` —
     * engine.rs2:25. Fills the iterator `npc_findnext` walks.
     *
     * The single global cursor is shared with `npc_findall` and the loc
     * iterators, matching the reference's one-iterator-per-script-state
     * arrangement closely enough to have the same consequence: a script that
     * suspends mid-loop and resumes after another has iterated sees the other's
     * list. Stated, not fixed — see the note on `srv->iterator`.
     */
    case SS_OP_NPC_HUNTALL:
    {
        int32_t coord;
        int32_t distance;
        int32_t checkvis;

        if( !SSVM_PopInt(state, &checkvis) )
            return 1;
        if( !SSVM_PopInt(state, &distance) )
            return 1;
        if( !SSVM_PopInt(state, &coord) )
            return 1;
        if( !srv )
            return 1;

        srv->iterator.count = 0;
        srv->iterator.cursor = 0;
        srv->iterator.kind = SSVM_ENT_NPC;
        for( int slot = 0; slot < MOCK230_NPC_MAX; slot++ )
        {
            const struct Mock230Npc* npc = &srv->npcs[slot];

            if( !search_matches(npc, coord, distance, checkvis) || !huntable(npc->type) )
                continue;
            if( srv->iterator.count <
                (int)(sizeof(srv->iterator.slots) / sizeof(srv->iterator.slots[0])) )
                srv->iterator.slots[srv->iterator.count++] = slot;
        }
        return 1;
    }

    /*
     * `[command,npc_hunt](coord $source, int $distance, int $checkvis)(boolean)`
     * — engine.rs2:21 — and
     * `[command,npc_findcat](coord, category $category, int $distance,
     * int $checkvis)(boolean)` at :545. Both walk the same candidates and keep
     * the nearest by euclidean-squared; they differ in the filter and in
     * popping a category.
     *
     * The tie-break is the reference's and it is not the obvious one:
     * `npcDistance <= closestDistance` (NpcOps.ts:307, :387) keeps the *last*
     * npc at an equal distance, where `<` would keep the first. With the walk in
     * slot order that is a different npc, so it is copied rather than tidied.
     */
    case SS_OP_NPC_HUNT:
    case SS_OP_NPC_FINDCAT:
    {
        int32_t coord;
        int32_t category = 0;
        int32_t distance;
        int32_t checkvis;
        int best = -1;
        int best_distance = 0;

        if( !SSVM_PopInt(state, &checkvis) )
            return 1;
        if( !SSVM_PopInt(state, &distance) )
            return 1;
        if( opcode == SS_OP_NPC_FINDCAT && !SSVM_PopInt(state, &category) )
            return 1;
        if( !SSVM_PopInt(state, &coord) )
            return 1;
        if( !srv )
        {
            SSVM_PushInt(state, 0);
            return 1;
        }

        for( int slot = 0; slot < MOCK230_NPC_MAX; slot++ )
        {
            const struct Mock230Npc* npc = &srv->npcs[slot];
            int rank;

            if( !search_matches(npc, coord, distance, checkvis) )
                continue;
            if( opcode == SS_OP_NPC_HUNT )
            {
                if( !huntable(npc->type) )
                    continue;
            }
            else
            {
                const struct Mock230NpcInfo* info = mock230_npcinfo_record(npc->type);

                if( !info || info->category != category )
                    continue;
            }

            rank = euclidean_sq(npc, coord);
            if( best < 0 || rank <= best_distance )
            {
                best = slot;
                best_distance = rank;
            }
        }

        if( best < 0 )
        {
            SSVM_PushInt(state, 0);
            return 1;
        }
        set_active_npc(state, srv, best);
        SSVM_PushInt(state, 1);
        return 1;
    }

    default:
        return 0;
    }
}

/*
 * ------------------------------------------------------------------
 * What this file refuses, and why each refusal is a measurement
 * ------------------------------------------------------------------
 *
 * Counts re-measured over LostCity_Server/content/scripts (comments stripped,
 * engine.rs2 excluded), 2026-08-01. Every one of these is `known = 1` in
 * ss_meta.gen.h, so the VM's stub pops what the signature declares, pushes
 * zeros, and says so once per env — the stacks stay consistent and the gap
 * stays visible. That is the intended state, not a regression.
 *
 * `npc_walk` (108 uses / 45 files) and `npc_walktrigger` (9/2).
 *     The largest npc gap in the tree, and it is engine state rather than an
 *     opcode. NpcOps.ts:451 is `activeNpc.queueWaypoint(x, z)`: a *queue* the
 *     tick drains. `struct Mock230Npc` has no destination and no waypoint
 *     queue; the only mover is `mock230_world_npc_walk_to`, which takes one
 *     step toward a target passed in by its caller and is called only from
 *     inside phase 4 (`advance_npcs`) and the combat tick. Landing it honestly
 *     means a waypoint queue on the npc plus a drain in `advance_npcs`, ahead
 *     of the mode machine and behind combat — engine work in mock230_world.c,
 *     which is not what an ops file is. Faking it as a teleport would make 45
 *     files *look* ported while every npc arrived instantly, which is worse
 *     than the stub because it is silent.
 *
 * `npc_statheal` (16/10), `npc_statsub` (9/6), `npc_statadd` (1/1).
 *     All three write `npc.levels[stat]` (NpcOps.ts:241, :492, :506) against a
 *     `baseLevels[stat]` they clamp to. This engine has no per-npc mutable stat
 *     store: `SS_OP_NPC_STAT` reads `npc->def->attack/strength/defence/...`
 *     straight off the content block and hitpoints is the only number that
 *     moves. Writing a value nothing reads is worse than the stub.
 *
 * `npc_changetype_keepall` (36/17).
 *     Verified at Npc.ts:426-449: the *only* difference from `npc_changetype`
 *     is whether `levels[]`/`baseLevels[]` are re-derived from the new type —
 *     the same store the row above says does not exist here. So the two are the
 *     same operation in this engine, and the cheapest correct fix is one
 *     fallthrough line, `case SS_OP_NPC_CHANGETYPE_KEEPALL:` above
 *     `case SS_OP_NPC_CHANGETYPE:` in mock230_scripts.c. Not done here because
 *     the alternative from a domain file is to copy that case's body or to
 *     shadow a *working* opcode, and both are worse than one line by whoever
 *     holds the mock230_scripts.c exception.
 *
 * `npc_heropoints` (13/13).
 *     NpcOps.ts:478 accumulates damage per attacking player into a table the
 *     drop code later reads. Neither exists: `SS_OP_NPC_FINDHERO` pushes the
 *     constant 1 because there is one player. Popping the damage and dropping
 *     it would be stack-correct and silent, which is exactly what the stub does
 *     out loud.
 *
 * `npc_sethuntmode` (14/7), `npc_sethunt` (1/1).
 *     A `hunt` config namespace, a per-npc huntMode/huntrange, and a per-tick
 *     hunt pass. `npc_hunt` / `npc_huntall` above are pure searches and now
 *     honour HuntVis (`checkvis`); the remaining set-hunt opcodes still need
 *     the config namespace and the tick pass.
 *
 * `npc_arrivedelay` (2/2).
 *     Needs `lastMovement` compared against the current tick (NpcOps.ts:542) —
 *     a field written by the mover, in mock230_world.c. Two uses.
 *
 * `npc_inrange` (2/1).
 *     `targetWithinMaxRange()` (Npc.ts:635) reads the npc's `target`,
 *     `targetOp`, its spawn tile and `type.maxrange`. This server has a
 *     `combat_target` and nothing else that answers "what is this npc
 *     interacting with", so the question cannot be asked here yet.
 *
 * `npc_findallzone` (0 uses) and `nc_desc` (0 uses).
 *     No caller anywhere in the reference tree. `nc_desc` additionally cannot
 *     be answered — a dat2 npc record has no description at this revision.
 */
