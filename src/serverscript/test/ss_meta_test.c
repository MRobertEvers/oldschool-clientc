/*
 * Generated opcode/trigger table sanity, and the trigger lookup key.
 *
 * Everything here runs without the LostCity corpus — it validates the tables
 * against facts about the reference that the generator is supposed to preserve.
 * The corpus-wide check ("every opcode a real script uses has a known
 * signature") needs the reader, so it lives in the verifier test instead.
 */

#include "ss_meta.h"

#include <stdio.h>
#include <string.h>

static int g_fail = 0;

#define CHECK(cond, msg)                                                       \
    do                                                                         \
    {                                                                          \
        if( cond )                                                             \
        {                                                                      \
            printf("  ok   %s\n", (msg));                                      \
        }                                                                      \
        else                                                                   \
        {                                                                      \
            printf("  FAIL %s (%s:%d)\n", (msg), __FILE__, __LINE__);          \
            g_fail++;                                                          \
        }                                                                      \
    } while( 0 )

#define CHECK_EQ(got, want, msg)                                               \
    do                                                                         \
    {                                                                          \
        long long gv = (long long)(got), wv = (long long)(want);               \
        if( gv == wv )                                                         \
        {                                                                      \
            printf("  ok   %s == %lld\n", (msg), gv);                          \
        }                                                                      \
        else                                                                   \
        {                                                                      \
            printf("  FAIL %s: got %lld want %lld (%s:%d)\n", (msg), gv, wv,   \
                   __FILE__, __LINE__);                                        \
            g_fail++;                                                          \
        }                                                                      \
    } while( 0 )

/* ------------------------------------------------------------------ */

static void
test_opcode_names(void)
{
    printf("opcode names\n");

    CHECK_EQ(strcmp(SSVM_OpcodeName(SS_OP_PUSH_CONSTANT_INT), "PUSH_CONSTANT_INT"), 0,
             "opcode 0");
    CHECK_EQ(strcmp(SSVM_OpcodeName(SS_OP_RETURN), "RETURN"), 0, "opcode 21");
    CHECK_EQ(strcmp(SSVM_OpcodeName(SS_OP_JOIN_STRING), "JOIN_STRING"), 0, "opcode 37");
    CHECK_EQ(strcmp(SSVM_OpcodeName(SS_OP_NPC_VAR_GET), "NPC_VAR_GET"), 0,
             "npc runtime-var opcode name");
    CHECK_EQ(SSVM_OpcodeFromName("npc_var_set"), SS_OP_NPC_VAR_SET,
             "compiler command lookup sees npc_var_set");

    /* A name must always come back, or the diagnostic that was trying to
     * explain a failure becomes the failure. */
    CHECK(SSVM_OpcodeName(999999) != NULL, "out-of-range name is non-NULL");
    CHECK(SSVM_OpcodeName(-1) != NULL, "negative name is non-NULL");
    CHECK_EQ(strcmp(SSVM_OpcodeName(1234), "OP_1234"), 0, "undefined id renders as OP_n");
}

static void
test_core_arities(void)
{
    const struct SSVM_OpcodeMeta* m;

    printf("core language arities\n");

    m = SSVM_OpcodeMeta(SS_OP_PUSH_CONSTANT_INT);
    CHECK(m->int_in == 0 && m->int_out == 1, "PUSH_CONSTANT_INT pushes one int");

    m = SSVM_OpcodeMeta(SS_OP_PUSH_CONSTANT_STRING);
    CHECK(m->str_in == 0 && m->str_out == 1, "PUSH_CONSTANT_STRING pushes one string");

    m = SSVM_OpcodeMeta(SS_OP_BRANCH_EQUALS);
    CHECK(m->int_in == 2 && m->int_out == 0, "BRANCH_EQUALS pops two ints");

    m = SSVM_OpcodeMeta(SS_OP_BRANCH);
    CHECK(m->int_in == 0, "BRANCH is unconditional and pops nothing");

    m = SSVM_OpcodeMeta(SS_OP_SWITCH);
    CHECK(m->int_in == 1, "SWITCH pops the key");

    /* RETURN must not pop: the callee's return values are already on the stack
     * where the caller will read them. */
    m = SSVM_OpcodeMeta(SS_OP_RETURN);
    CHECK(m->int_in == 0 && m->str_in == 0, "RETURN pops nothing");

    /* Structurally variadic: the count comes from the operand or the callee's
     * declared args, so no static arity can describe them. */
    CHECK(SSVM_OpcodeMeta(SS_OP_JOIN_STRING)->variadic, "JOIN_STRING is variadic");
    CHECK(SSVM_OpcodeMeta(SS_OP_GOSUB_WITH_PARAMS)->variadic,
          "GOSUB_WITH_PARAMS is variadic");
    CHECK(SSVM_OpcodeMeta(SS_OP_JUMP_WITH_PARAMS)->variadic,
          "JUMP_WITH_PARAMS is variadic");
}

static void
test_command_arities(void)
{
    const struct SSVM_OpcodeMeta* m;

    printf("command arities from engine.rs2\n");

    /* [command,mes](string $text) */
    m = SSVM_OpcodeMeta(SS_OP_MES);
    CHECK(m->str_in == 1 && m->int_in == 0 && m->int_out == 0, "mes takes one string");

    /* [command,inv_total](inv $inv, obj $obj)(int) */
    m = SSVM_OpcodeMeta(SS_OP_INV_TOTAL);
    CHECK(m->int_in == 2 && m->int_out == 1, "inv_total(inv, obj) -> int");

    /* [command,inv_add](inv $inv, obj $obj, int $count) */
    m = SSVM_OpcodeMeta(SS_OP_INV_ADD);
    CHECK(m->int_in == 3 && m->int_out == 0, "inv_add(inv, obj, count)");

    /* [command,p_delay](int $delay) */
    m = SSVM_OpcodeMeta(SS_OP_P_DELAY);
    CHECK(m->int_in == 1, "p_delay(int)");

    /* Every ScriptVarType except `string` is an int, so a coord argument must
     * count against int_in. [command,distance](coord, coord)(int) */
    m = SSVM_OpcodeMeta(SS_OP_DISTANCE);
    CHECK(m->int_in == 2 && m->int_out == 1, "coord args land on the int stack");

    /* [command,oc_name](obj $obj)(string) */
    m = SSVM_OpcodeMeta(SS_OP_OC_NAME);
    CHECK(m->int_in == 1 && m->str_out == 1, "oc_name(obj) -> string");

    m = SSVM_OpcodeMeta(SS_OP_PROJANIM_MAP);
    CHECK(m->int_in == 9 && m->int_out == 0,
          "projanim_map(from, to, spotanim, heights, timing, arc)");

    m = SSVM_OpcodeMeta(SS_OP_NPC_VAR_GET);
    CHECK(m->int_in == 1 && m->int_out == 1, "npc_var_get(slot) -> int");
    m = SSVM_OpcodeMeta(SS_OP_NPC_VAR_SET);
    CHECK(m->int_in == 2 && m->int_out == 0, "npc_var_set(slot, value)");
}

static void
test_vararg_separation(void)
{
    const struct SSVM_OpcodeMeta* base;
    const struct SSVM_OpcodeMeta* var;

    printf("queue vs queue* are different opcodes\n");

    /* engine.rs2 declares both `[command,queue]` and `[command,queue*]`. They
     * are separate opcodes (QUEUE 2093, QUEUEVARARG 2094); folding the `*` form
     * onto the base name would mislabel QUEUE as variadic and leave QUEUEVARARG
     * with no signature at all. */
    base = SSVM_OpcodeMeta(SS_OP_QUEUE);
    var = SSVM_OpcodeMeta(SS_OP_QUEUEVARARG);

    CHECK(base->known && var->known, "both forms have a signature");
    CHECK(!base->variadic, "queue is not variadic");
    CHECK(var->variadic, "queue* is variadic");
    CHECK_EQ(base->int_in, 3, "queue(queue, delay, arg)");

    CHECK(SSVM_OpcodeMeta(SS_OP_STRONGQUEUEVARARG)->variadic, "strongqueue* variadic");
    CHECK(SSVM_OpcodeMeta(SS_OP_WEAKQUEUEVARARG)->variadic, "weakqueue* variadic");
    CHECK(SSVM_OpcodeMeta(SS_OP_LONGQUEUEVARARG)->variadic, "longqueue* variadic");
    CHECK(!SSVM_OpcodeMeta(SS_OP_STRONGQUEUE)->variadic, "strongqueue not variadic");
}

static void
test_pointer_masks(void)
{
    const struct SSVM_OpcodeMeta* m;

    printf("pointer requirements\n");

    /* ANIM requires active_player, and its dot form active_player2. */
    m = SSVM_OpcodeMeta(SS_OP_ANIM);
    CHECK(m->require & SSVM_PTR_ACTIVE_PLAYER, "anim requires active_player");
    CHECK(m->require2 & SSVM_PTR_ACTIVE_PLAYER2, ".anim requires active_player2");

    m = SSVM_OpcodeMeta(SS_OP_NPC_SAY);
    CHECK(m->require & SSVM_PTR_ACTIVE_NPC, "npc_say requires active_npc");

    m = SSVM_OpcodeMeta(SS_OP_NPC_SETOWNER);
    CHECK((m->require & (SSVM_PTR_ACTIVE_NPC | SSVM_PTR_PROTECTED_PLAYER)) ==
              (SSVM_PTR_ACTIVE_NPC | SSVM_PTR_PROTECTED_PLAYER),
          "npc_setowner requires active_npc and protected player");
    m = SSVM_OpcodeMeta(SS_OP_NPC_OWNER);
    CHECK(m->require == SSVM_PTR_ACTIVE_NPC && m->int_out == 1,
          "npc_owner requires active_npc and returns one int");
    m = SSVM_OpcodeMeta(SS_OP_NPC_FINDOWNED);
    CHECK(m->require == SSVM_PTR_PROTECTED_PLAYER && m->int_out == 1,
          "npc_findowned requires protected player and returns boolean");
    m = SSVM_OpcodeMeta(SS_OP_NPC_VAR_GET);
    CHECK(m->require == SSVM_PTR_ACTIVE_NPC,
          "npc_var_get requires the primary active npc");
    m = SSVM_OpcodeMeta(SS_OP_NPC_VAR_SET);
    CHECK(m->require == SSVM_PTR_ACTIVE_NPC,
          "npc_var_set requires the primary active npc");

    /* A pure computation must require nothing, or every arithmetic op would
     * abort in a script with no active entity. */
    m = SSVM_OpcodeMeta(SS_OP_ADD);
    CHECK_EQ(m->require, 0, "add requires no pointer");
}

static void
test_known_coverage(void)
{
    int known = 0;
    int named = 0;
    int i;

    printf("table coverage\n");

    for( i = 0; i < SS_OPCODE_MAX; i++ )
    {
        /* A real name, as opposed to the OP_<n> fallback for an id the
         * reference never defined. */
        if( strncmp(SSVM_OpcodeName(i), "OP_", 3) != 0 )
            named++;
        if( SSVM_OpcodeMetaKnown(i) )
            known++;
    }

    CHECK_EQ(named, SS_OPCODE_COUNT, "every defined opcode has a name");

    /* LC_OP, OC_IOP, OC_OP and MAP_LOC are the only unknowns: the reference
     * declares the ids but implements no handler (and engine.rs2 has no
     * signature for MAP_LOC either), so their arity is genuinely unknown and
     * they must stay known=0 so the VM refuses to execute them. Guessing would
     * silently corrupt the stack. */
    CHECK_EQ(SS_OPCODE_COUNT - known, 4, "exactly four opcodes lack a signature");
    CHECK(!SSVM_OpcodeMetaKnown(SS_OP_LC_OP), "lc_op has no signature");
    CHECK(!SSVM_OpcodeMetaKnown(SS_OP_OC_IOP), "oc_iop has no signature");
    CHECK(!SSVM_OpcodeMetaKnown(SS_OP_OC_OP), "oc_op has no signature");
    CHECK(!SSVM_OpcodeMetaKnown(SS_OP_MAP_LOC), "map_loc has no signature");
}

static void
test_triggers(void)
{
    printf("triggers\n");

    CHECK_EQ(SS_TRIGGER_PROC, 0, "proc");
    CHECK_EQ(SS_TRIGGER_OPNPC1, 10, "opnpc1");
    CHECK_EQ(SS_TRIGGER_IF_BUTTON, 147, "if_button");
    CHECK_EQ(SS_TRIGGER_LOGIN, 157, "login");
    CHECK_EQ(SS_TRIGGER_AI_DESPAWN, 167, "ai_despawn");

    /*
     * 167 is the last id ServerTriggerType.ts defines, and the boundary is the
     * assertion: rev-230-only triggers go strictly above it, so a future
     * LostCity trigger can never land on one. Same rule EXTRA_OPCODES follows
     * at 11000.
     */
    CHECK_EQ(SS_TRIGGER_IF_BUTTON1, 168, "if_button1 is the first non-reference trigger");
    CHECK_EQ(SS_TRIGGER_IF_BUTTON10, 177, "if_button10");
    CHECK_EQ(SS_TRIGGER_IF_BUTTON10 - SS_TRIGGER_IF_BUTTON1, 9, "ten contiguous op triggers");
    CHECK_EQ(SS_TRIGGER_IF_OPEN, 178, "if_open is the open-side twin of if_close");
    CHECK_EQ(SS_TRIGGER_FRIENDLOGIN, 179, "friendlogin is rev-230 presence");
    CHECK_EQ(SS_TRIGGER_FRIENDLOGOUT, 180, "friendlogout");
    CHECK_EQ(SS_TRIGGER_PLAYERDEATH, 181, "playerdeath when HP hits 0");
    CHECK_EQ(SS_TRIGGER_MAX, 182, "trigger table size");

    /* The numbered form is a *different* trigger from the op-less click, not a
     * relabelling of it: `[if_button,x]` still answers a plain click and
     * `[if_button1,x]` answers op 1. Collapsing them would make every armed
     * component's op-less events fire its op-1 script. */
    CHECK(SS_TRIGGER_IF_BUTTON1 != SS_TRIGGER_IF_BUTTON, "op 1 is not the op-less click");
    CHECK_EQ(strcmp(SSVM_TriggerName(SS_TRIGGER_IF_BUTTON2), "if_button2"), 0,
             "name of if_button2");
    CHECK_EQ(SSVM_TriggerFromName("if_button2"), SS_TRIGGER_IF_BUTTON2, "if_button2 by name");
    CHECK_EQ(SSVM_TriggerFromName("if_button10"), SS_TRIGGER_IF_BUTTON10, "if_button10 by name");

    CHECK_EQ(strcmp(SSVM_TriggerName(SS_TRIGGER_OPNPC1), "opnpc1"), 0, "name of opnpc1");
    CHECK_EQ(SSVM_TriggerFromName("opnpc1"), SS_TRIGGER_OPNPC1, "opnpc1 by name");
    CHECK_EQ(SSVM_TriggerFromName("ai_timer"), SS_TRIGGER_AI_TIMER, "ai_timer by name");
    CHECK_EQ(SSVM_TriggerFromName("not_a_trigger"), -1, "unknown name");

    /* Gaps in the id space are reserved, not usable. */
    CHECK_EQ(strcmp(SSVM_TriggerName(22), ""), 0, "reserved id 22 has no name");

    /* The engine stores the ap trigger and derives the op trigger by adding 7,
     * rather than carrying both. That only works because the relation holds for
     * every interaction family. */
    CHECK_EQ(SS_TRIGGER_APNPC1 + SS_TRIGGER_AP_TO_OP, SS_TRIGGER_OPNPC1, "npc ap->op");
    CHECK_EQ(SS_TRIGGER_APLOC1 + SS_TRIGGER_AP_TO_OP, SS_TRIGGER_OPLOC1, "loc ap->op");
    CHECK_EQ(SS_TRIGGER_APOBJ1 + SS_TRIGGER_AP_TO_OP, SS_TRIGGER_OPOBJ1, "obj ap->op");
    CHECK_EQ(SS_TRIGGER_APPLAYER1 + SS_TRIGGER_AP_TO_OP, SS_TRIGGER_OPPLAYER1,
             "player ap->op");
    CHECK_EQ(SS_TRIGGER_APNPC5 + SS_TRIGGER_AP_TO_OP, SS_TRIGGER_OPNPC5, "npc ap5->op5");
}

static void
test_lookup_key(void)
{
    uint64_t a;
    uint64_t b;
    int32_t uid_231_5;
    int32_t uid_217_5;

    printf("lookup key\n");

    /* Layout: trigger | kind << 8 | subject << 10. */
    CHECK_EQ(SSVM_LookupKey(SS_TRIGGER_OPNPC1, SS_LOOKUP_TYPE, 3105),
             (uint64_t)10 | (2ull << 8) | (3105ull << 10), "packing");
    CHECK_EQ(SSVM_LookupKeyTrigger(0x00729E93), 147, "trigger byte unpacks to if_button");

    /* THE TRAP. Interface triggers put a packed component uid ((iface << 16) |
     * child) in the subject field. A rev-230 uid shifted left 10 exceeds 2^33,
     * so a 32-bit key wraps — and a wrapped key still resolves, just to the
     * wrong script. The symptom is "the wrong dialogue opened", never a crash,
     * which is exactly why this assertion exists. */
    uid_231_5 = (231 << 16) | 5;
    uid_217_5 = (217 << 16) | 5;

    a = SSVM_LookupKey(SS_TRIGGER_IF_BUTTON, SS_LOOKUP_TYPE, uid_231_5);
    b = SSVM_LookupKey(SS_TRIGGER_IF_BUTTON, SS_LOOKUP_TYPE, uid_217_5);

    CHECK(a != b, "component uids 231:5 and 217:5 produce different keys");
    CHECK(a > 0xffffffffull, "a rev-230 component key exceeds 32 bits");
    CHECK_EQ(a, (uint64_t)147 | (2ull << 8) | ((uint64_t)(uint32_t)uid_231_5 << 10),
             "component key value");

    /* The same overflow collides mapzone subjects in groups of four in the
     * reference, because only the low 2 bits of the mapsquare-x survive
     * subject << 10 in an int32. Widening the key separates them. */
    a = SSVM_LookupKey(SS_TRIGGER_MAPZONE, SS_LOOKUP_TYPE, 29 << 20);
    b = SSVM_LookupKey(SS_TRIGGER_MAPZONE, SS_LOOKUP_TYPE, 33 << 20);
    CHECK(a != b, "mapzone subjects four mapsquares apart do not collide");
}

int
main(void)
{
    test_opcode_names();
    test_core_arities();
    test_command_arities();
    test_vararg_separation();
    test_pointer_masks();
    test_known_coverage();
    test_triggers();
    test_lookup_key();

    if( g_fail )
    {
        printf("ss meta test: %d FAILED\n", g_fail);
        return 1;
    }
    printf("ss meta test: all passed\n");
    return 0;
}
