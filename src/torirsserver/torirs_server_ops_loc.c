/*
 * The `loc_*` / `lc_*` config reads — RuneScript's view of a loc's own record.
 *
 * Third of the per-domain opcode files (mock230_ops_db.c is the first and its
 * header states the contract): `mock230_script_command` offers each domain the
 * opcode in turn and each returns 1 when it handled it.
 *
 * The *mutating* half of the loc family — `loc_add`, `loc_change`, `loc_del`,
 * `loc_find`, `loc_coord`, `loc_type`, `loc_angle`, `loc_shape`, the two
 * find-all iterators — already lives in mock230_scripts.c's switch and stays
 * there. Everything here is a pure read of the config record, which is why it
 * can move without carrying the scene and the revert queue with it.
 *
 * ------------------------------------------------------------------
 * The arity trap: loc_param pops one, lc_param pops two
 * ------------------------------------------------------------------
 *
 * `[command,loc_param](param $param)(any)` — engine.rs2:693 — pops **one** int
 * and takes its record from the *active loc*. `[command,lc_param](loc $loc,
 * param $param)(any)` — engine.rs2:755, landed in mock230_ops_param.c — pops
 * **two** and is told which loc. Same word, same result, different stack
 * discipline; reading one as the other leaves every later value on the wrong
 * rung of the int stack, and nothing fails at the call.
 *
 * `loc_name` is the same shape one rung along: `()(string)` off the active loc,
 * where `lc_name(loc $loc)(string)` names one.
 *
 * ------------------------------------------------------------------
 * Holding the active loc by handle
 * ------------------------------------------------------------------
 *
 * The VM's pointer table (require 0x040 = SSVM_PTR_ACTIVE_LOC) has already
 * refused the opcode if no loc is active, so `active_loc` below is not
 * re-checking that. It is checking something the table cannot: a script can
 * suspend between `loc_find` and here, and a scene rebuild reallocates the loc
 * array underneath it. The handle rides in the VM's active-entity pointer —
 * `slot + 1` for a scene loc, negative for a ZoneMap record beyond the scene
 * window — and `mock230_script_loc_resolve` re-validates either kind, so a
 * resumed script either finds the same loc or finds none.
 *
 * Note what is deliberately NOT used for this: `mock230_scene_find_loc` ends in
 * `return loc_id >= 0 ? fallback : fallback`, so its loc_id argument does not
 * filter — it returns the first loc on the tile whatever id it is asked for.
 * That is correct for its own caller (a stale OPLOC id must still resolve to
 * the door somebody else already opened) and useless as an existence check.
 *
 * ------------------------------------------------------------------
 * Where LostCity puts these
 * ------------------------------------------------------------------
 *
 * engine/src/engine/script/handlers/LocOps.ts (LOC_PARAM :114, LOC_NAME :129,
 * both `checkedHandler(ActiveLoc, ...)`) and LocConfigOps.ts (LC_NAME,
 * LC_WIDTH, LC_LENGTH, each `check(state.popInt(), LocTypeValid)` then a field
 * of the type). Engine, one handler per opcode, every value read straight off
 * the config record. The records are content — they are in the cache here, and
 * mock230_locinfo.c decodes them once at boot.
 */

#include "mock230.h"
#include "mock230_content.h"
#include "mock230_paramtable.h"
#include "mock230_scene.h"

#include "ss_meta.h"
#include "ss_opcode.h"
#include "ssvm.h"

#include <stdint.h>

/*
 * The active loc, or NULL having said why.
 *
 * `SSVM_Active` honours the current op's dot flag internally; neither opcode
 * here declares one (engine.rs2 has no `.loc_param` and no `.loc_name`), so it
 * resolves to the primary slot — but going through it rather than naming
 * SSVM_PRIMARY means a future `.`-form cannot silently read the wrong entity.
 */
static struct Mock230SceneLoc*
active_loc(
    struct SSVM_State* state,
    int opcode)
{
    struct Mock230Server* srv = (struct Mock230Server*)state->env->host.user;
    struct Mock230SceneLoc* loc =
        mock230_script_loc_resolve(srv, SSVM_Active(state, SSVM_ENT_LOC));

    if( !loc )
    {
        SSVM_Abort(state, "%s: the active loc is gone", SSVM_OpcodeName(opcode));
        return NULL;
    }
    return loc;
}

/*
 * The reference runs `check(id, LocTypeValid)` on every `lc_*` argument and
 * throws when the id names no record, so an id that is out of the config's
 * range is a content bug and has to be loud. 0 is a real loc id, so there is no
 * sentinel to answer with — the alternative to aborting is silently reporting a
 * 1x1 nameless loc, which reads as data rather than as a mistake.
 *
 * Skipped entirely when the table is empty: a server booted without a cache
 * still runs, and turning that into "every lc_* call aborts" would be a worse
 * failure than the one it is guarding against.
 */
static int
check_loc_id(
    struct SSVM_State* state,
    int opcode,
    int32_t loc_id)
{
    if( mock230_locinfo_count() <= 0 )
        return 1;
    if( mock230_loc_known(loc_id) )
        return 1;
    SSVM_Abort(state, "%s: loc %d is not in the config", SSVM_OpcodeName(opcode),
               (int)loc_id);
    return 0;
}

/*
 * `'null'` is the reference's own fallback for a missing name — LocOps.ts:130
 * `name ?? 'null'`, LocConfigOps.ts `name ?? debugname ?? 'null'` — not a game
 * string this file invented. 32,161 of cache.osrs239's 62,194 loc records carry
 * no name at all, and none of the 30,033 that do spells it "null", so content
 * comparing against it is asking exactly "is this loc nameless".
 */
static void
push_loc_name(
    struct SSVM_State* state,
    int loc_id)
{
    const char* name = mock230_loc_name(loc_id);

    /* SSVM_PushStr copies into the state's string pool, which owns everything
     * it holds — the table's pointer must not be handed to the VM. */
    SSVM_PushStr(state, name ? name : "null");
}

int
mock230_ops_loc(
    struct SSVM_State* state,
    int opcode,
    int dot)
{
    (void)dot;

    switch( opcode )
    {
    /* `[command,loc_param](param $param)(any)` — engine.rs2:693. One int in,
     * one value out on whichever stack `configs/all.param` declares. */
    case SS_OP_LOC_PARAM:
    {
        int32_t param_id;
        struct Mock230SceneLoc* loc;
        const struct Mock230ParamRow* row;

        if( !SSVM_PopInt(state, &param_id) )
            return 1;
        loc = active_loc(state, opcode);
        if( !loc )
            return 1;

        row = mock230_loc_param(loc->loc_id, param_id);
        mock230_push_typed_param(state, param_id, row ? row->sval : NULL,
                                 row ? row->ival : 0, row != NULL, "loc", loc->loc_id);
        return 1;
    }

    /* `[command,loc_name]()(string)` — engine.rs2:699. Nothing in, one string
     * out; the one op in this file that touches the string stack. */
    case SS_OP_LOC_NAME:
    {
        struct Mock230SceneLoc* loc = active_loc(state, opcode);

        if( !loc )
            return 1;
        push_loc_name(state, loc->loc_id);
        return 1;
    }

    /* `[command,lc_name](loc $loc)(string)` — engine.rs2:753. */
    case SS_OP_LC_NAME:
    {
        int32_t loc_id;

        if( !SSVM_PopInt(state, &loc_id) )
            return 1;
        if( !check_loc_id(state, opcode, loc_id) )
            return 1;
        push_loc_name(state, loc_id);
        return 1;
    }

    /*
     * `[command,loc_category]()(category)` — engine.rs2:667, `.loc_category` at
     * :669 — and `[command,lc_category](loc $loc)(category)` at :757.
     *
     * The `loc_param` / `lc_param` arity trap this file's header documents, one
     * rung along and with the same shape: one pops nothing and reads the active
     * loc, the other pops the id it is told.
     *
     * `mock230_loc_category` is the merged read — the authored overlay first, the
     * cache's config opcode 61 second. This is the whole reason
     * `[oploc1,_door_closed]` can bind: the rung `interaction_category` feeds the
     * trigger lookup with, and the value this opcode pushes, are the same
     * function, so a script that *tests* the category and a script bound *to* the
     * category cannot disagree.
     *
     * **One divergence from the reference, stated.** `LocOps.ts:57` and
     * `LocConfigOps.ts:28` push `locType.category` raw, which is 0 for a record
     * that states none. Here it is -1, which is RuneScript's `null` for every
     * config-typed value and is what the `(category)` return type means. 0 is not
     * available to mean "none" on this side: `pack/category.pack` reserves it and
     * `mock230_pack` refuses to name it, so content comparing against 0 would be
     * comparing against a name that must never exist.
     */
    case SS_OP_LOC_CATEGORY:
    {
        struct Mock230SceneLoc* loc = active_loc(state, opcode);

        if( !loc )
            return 1;
        SSVM_PushInt(state, mock230_loc_category(loc->loc_id));
        return 1;
    }

    case SS_OP_LC_CATEGORY:
    {
        int32_t loc_id;

        if( !SSVM_PopInt(state, &loc_id) )
            return 1;
        if( !check_loc_id(state, opcode, loc_id) )
            return 1;
        SSVM_PushInt(state, mock230_loc_category(loc_id));
        return 1;
    }

    /*
     * `[command,lc_debugname](loc $loc)(string)` — engine.rs2:761.
     *
     * The *symbol*, not the display name: `LocConfigOps.ts:36` pushes
     * `debugname`, which is the `[block]` header of a `.loc` file, where `lc_name`
     * pushes the record's `name=`. Here the symbol table is `pack/loc.pack` and
     * `mock230_content_symbol_name` is its reader.
     *
     * It has one caller in the whole reference tree and that caller is why it is
     * here: `ladders+stairs/scripts/stairs.rs2:431` is
     * `mes("Unhandled stairs: <lc_debugname(loc_type)> at")` — the reference's own
     * "this loc reached a script that does not handle it" line. Porting that file
     * without the opcode loses the diagnostic that says which locs the port missed.
     *
     * `'null'` is the reference's own fallback (`debugname ?? 'null'`), the same
     * string `push_loc_name` uses and for the same reason.
     */
    case SS_OP_LC_DEBUGNAME:
    {
        int32_t loc_id;
        const char* symbol;

        if( !SSVM_PopInt(state, &loc_id) )
            return 1;
        if( !check_loc_id(state, opcode, loc_id) )
            return 1;
        symbol = mock230_content_symbol_name(MOCK230_PACK_LOC, loc_id);
        SSVM_PushStr(state, symbol ? symbol : "null");
        return 1;
    }

    /*
     * `[command,lc_width](loc $loc)(int)` — engine.rs2:763 — and
     * `[command,lc_length]` at :765.
     *
     * These are the record's *unrotated* footprint, which is what the reference
     * pushes (`locType.width` / `.length`). `struct Mock230SceneLoc` also
     * carries a size_x/size_z, but that pair has already been rotated by the
     * placed angle, so answering from a scene loc would return the length for a
     * loc facing east. Different question, same field names.
     */
    case SS_OP_LC_WIDTH:
    case SS_OP_LC_LENGTH:
    {
        int32_t loc_id;
        int width;
        int length;

        if( !SSVM_PopInt(state, &loc_id) )
            return 1;
        if( !check_loc_id(state, opcode, loc_id) )
            return 1;
        mock230_loc_footprint(loc_id, &width, &length);
        SSVM_PushInt(state, opcode == SS_OP_LC_WIDTH ? width : length);
        return 1;
    }

    default:
        return 0;
    }
}
