/*
 * The `*_param` family — RuneScript's view of a config record's param map.
 *
 * Second of the per-domain opcode files (torirs_server_ops_db.c is the first and its
 * header states the contract): `ToriRSServer_ScriptCommand` offers each domain the
 * opcode in turn and each returns 1 when it handled it.
 *
 * ------------------------------------------------------------------
 * The stack routing is the whole difficulty, and it lives here
 * ------------------------------------------------------------------
 *
 * `ToriRSServer_PushTypedParam` moved into this file from torirs_server_scripts.c, where
 * it was `static` and therefore unreachable from a domain file. It is a move,
 * not a copy: the function's own comment already said that duplicating it would
 * mean two places for the declared-vs-stored disagreement to be handled
 * differently, and that disagreement is the one thing in this family worth
 * being loud about. `oc_param` and `nc_param` still live in the big switch and
 * now call it through torirs_server.h.
 *
 * The reference does the same thing in the same place: `StructOps.ts` and
 * `LocConfigOps.ts` both branch on `paramType.isString()` — the *declaration* —
 * and never on what the record happens to store.
 *
 * ------------------------------------------------------------------
 * Where LostCity puts these
 * ------------------------------------------------------------------
 *
 * engine/src/engine/script/handlers/StructOps.ts (STRUCT_PARAM) and
 * LocConfigOps.ts (LC_PARAM) — engine, one handler each, each reading the
 * type's param table through `ParamHelper`. The param *values* are content
 * (config records in the cache). This tree matches: decoder in C, values in
 * cache.osrs239.
 */

#include "torirs_server.h"
#include "torirs_server_content.h"
#include "torirs_server_paramtable.h"

#include "ss_opcode.h"
#include "ssvm.h"

/*
 * Push a param onto the stack its *declaration* calls for.
 *
 * `sval`/`ival` are the stored value and `present` says whether the record
 * carried the param at all; a caller with no row passes present = 0 and the
 * other two are ignored.
 */
void
ToriRSServer_PushTypedParam(
    struct SSVM_State* state,
    int param_id,
    const char* sval,
    int32_t ival,
    int present,
    const char* record_kind,
    int record_id)
{
    char declared = ToriRSServer_ContentParamType(param_id);

    /*
     * Which stack the result goes on is decided by the declaration, because
     * that is what the script was compiled against: a script that wrote
     * `nc_param($npc, some_string_param)` has a string-typed local waiting for
     * it, and pushing an int would leave the two stacks out of step for the
     * rest of the script rather than fail here.
     *
     * 1,517 of cache.osrs239's 2,634 param records declare no type at all — the
     * config's type opcode is optional. For those the *value's own* stored kind
     * is the answer and it is not a guess: the record says whether it wrote
     * four bytes or a NUL-terminated string.
     */
    if( !declared && present )
        declared = sval ? 's' : 'i';

    if( declared == 's' )
    {
        if( present && !sval )
        {
            /* Declared a string, stored as an int. The record is wrong, and
             * which half to believe is not this opcode's call to make. */
            SSVM_Abort(state, "param %d is declared a string but %s %d stores an int",
                       param_id, record_kind, record_id);
            return;
        }
        /* `defaultstr=` (311 declared) is still read by nothing, so an absent
         * string param reports "" where the reference reports the declared
         * string — docs/osrs230_mockserver.md records the gap. */
        SSVM_PushStr(state, present ? sval : "");
        return;
    }

    if( present && sval )
    {
        SSVM_Abort(state, "param %d is declared an int but %s %d stores a string",
                   param_id, record_kind, record_id);
        return;
    }
    /*
     * A record that does not carry the param answers with the param's declared
     * `default=`, never an abort — LostCity's handlers push
     * `paramType.defaultInt` (ObjConfigOps.ts), and content relies on both
     * halves of that: `oc_param($obj, specwep) = ^true` is asked of every
     * weapon and only special-attack weapons carry the row (specwep declares
     * no default, so absent reads 0), while 365 params declare `default=-1`
     * precisely so that absence spells "no id" rather than obj 0.
     */
    SSVM_PushInt(state, present ? ival : ToriRSServer_ContentParamDefault(param_id));
}

int
ToriRSServer_OpsParam(
    struct SSVM_State* state,
    int opcode,
    int dot)
{
    (void)dot;

    switch( opcode )
    {
    /*
     * `[command,lc_param](loc $loc, param $param)(any)` — engine.rs2:755.
     *
     * Two ints in, one value out on whichever stack the param declares. Not to
     * be confused with `loc_param` (3011), which pops *one* int and takes its
     * record from the active loc; that one is not landed here.
     */
    case SS_OP_LC_PARAM:
    {
        int32_t loc_id;
        int32_t param_id;
        const struct ToriRSServerParamRow* row;

        /* Pop order mirrors the reference's `const [locId, paramId] =
         * state.popInts(2)`: the param is on top. */
        if( !SSVM_PopInt(state, &param_id) )
            return 1;
        if( !SSVM_PopInt(state, &loc_id) )
            return 1;

        row = ToriRSServer_LocParam(loc_id, param_id);
        ToriRSServer_PushTypedParam(state, param_id, row ? row->sval : NULL,
                                 row ? row->ival : 0, row != NULL, "loc", loc_id);
        return 1;
    }

    /* `[command,struct_param](struct $struct, param $param)(any)` —
     * engine.rs2:52. Same shape as lc_param over the struct table. */
    case SS_OP_STRUCT_PARAM:
    {
        int32_t struct_id;
        int32_t param_id;
        const struct ToriRSServerParamRow* row;

        if( !SSVM_PopInt(state, &param_id) )
            return 1;
        if( !SSVM_PopInt(state, &struct_id) )
            return 1;

        row = ToriRSServer_StructParam(struct_id, param_id);
        ToriRSServer_PushTypedParam(state, param_id, row ? row->sval : NULL,
                                 row ? row->ival : 0, row != NULL, "struct", struct_id);
        return 1;
    }

    default:
        return 0;
    }
}
