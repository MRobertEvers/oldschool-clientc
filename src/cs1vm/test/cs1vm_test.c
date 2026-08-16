/*
 * CS1VM core unit tests. The VM is engine-agnostic, so the whole suite runs
 * against a fake host backed by plain arrays — no cache, no UI tree.
 */

#include "cs1vm.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

#define TEST_ASSERT(cond, msg)                                                                     \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                        \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

#define FAKE_SKILL_MAX 25
#define FAKE_VARP_MAX 64
#define FAKE_LOG_MAX 32

struct FakeRequestLog
{
    enum CS1VM_HostRequestKind kind;
    int a;
    int b;
};

struct FakeHost
{
    int stat_level[FAKE_SKILL_MAX];
    int stat_base_level[FAKE_SKILL_MAX];
    int stat_xp[FAKE_SKILL_MAX];
    int stat_xp_remaining[FAKE_SKILL_MAX];
    int varp[FAKE_VARP_MAX];
    int varbit[FAKE_VARP_MAX];
    int combat_level;
    int total_level;
    int runenergy;
    int runweight;
    int coord_x;
    int coord_z;
    int inv_count;

    /* Yield simulation: while > 0, the chosen kind yields and decrements. */
    enum CS1VM_HostRequestKind yield_kind;
    int yield_countdown;
    /* Set to make the chosen kind yield unconditionally (contract violation). */
    int yield_forever;

    struct FakeRequestLog log[FAKE_LOG_MAX];
    int log_count;
};

static void
fake_host_init(struct FakeHost* host)
{
    memset(host, 0, sizeof(*host));
    host->yield_kind = CS1VM_HOST_REQUEST_INV_COUNT;
}

static void
fake_log(
    struct FakeHost* host,
    enum CS1VM_HostRequestKind kind,
    int a,
    int b)
{
    if( host->log_count >= FAKE_LOG_MAX )
        return;
    host->log[host->log_count].kind = kind;
    host->log[host->log_count].a = a;
    host->log[host->log_count].b = b;
    host->log_count++;
}

static int
fake_host_exec(
    struct CS1VM* vm,
    struct CS1VM_HostRequest* request)
{
    struct FakeHost* host = (struct FakeHost*)CS1VM_USER(vm);

    if( request->kind == host->yield_kind && (host->yield_forever || host->yield_countdown > 0) )
    {
        if( !host->yield_forever )
            host->yield_countdown--;
        return CS1VM_EXECNO_YIELD;
    }

    switch( request->kind )
    {
    case CS1VM_HOST_REQUEST_STAT_LEVEL:
        fake_log(host, request->kind, request->u.stat.skill, 0);
        request->out_value = host->stat_level[request->u.stat.skill % FAKE_SKILL_MAX];
        break;
    case CS1VM_HOST_REQUEST_STAT_BASE_LEVEL:
        fake_log(host, request->kind, request->u.stat.skill, 0);
        request->out_value = host->stat_base_level[request->u.stat.skill % FAKE_SKILL_MAX];
        break;
    case CS1VM_HOST_REQUEST_STAT_XP:
        fake_log(host, request->kind, request->u.stat.skill, 0);
        request->out_value = host->stat_xp[request->u.stat.skill % FAKE_SKILL_MAX];
        break;
    case CS1VM_HOST_REQUEST_STAT_XP_REMAINING:
        fake_log(host, request->kind, request->u.stat.skill, 0);
        request->out_value = host->stat_xp_remaining[request->u.stat.skill % FAKE_SKILL_MAX];
        break;
    case CS1VM_HOST_REQUEST_INV_COUNT:
        fake_log(host, request->kind, request->u.inv.component_id, request->u.inv.obj_id);
        request->out_value = host->inv_count;
        break;
    case CS1VM_HOST_REQUEST_INV_CONTAINS:
        fake_log(host, request->kind, request->u.inv.component_id, request->u.inv.obj_id);
        request->out_value = host->inv_count > 0 ? CS1VM_VALUE_INFINITY : 0;
        break;
    case CS1VM_HOST_REQUEST_VARP:
        fake_log(host, request->kind, request->u.varp.varp_id, 0);
        request->out_value = host->varp[request->u.varp.varp_id % FAKE_VARP_MAX];
        break;
    case CS1VM_HOST_REQUEST_VARBIT:
        fake_log(host, request->kind, request->u.varbit.varbit_id, 0);
        request->out_value = host->varbit[request->u.varbit.varbit_id % FAKE_VARP_MAX];
        break;
    case CS1VM_HOST_REQUEST_COMBAT_LEVEL:
        fake_log(host, request->kind, 0, 0);
        request->out_value = host->combat_level;
        break;
    case CS1VM_HOST_REQUEST_TOTAL_LEVEL:
        fake_log(host, request->kind, 0, 0);
        request->out_value = host->total_level;
        break;
    case CS1VM_HOST_REQUEST_RUNENERGY:
        fake_log(host, request->kind, 0, 0);
        request->out_value = host->runenergy;
        break;
    case CS1VM_HOST_REQUEST_RUNWEIGHT:
        fake_log(host, request->kind, 0, 0);
        request->out_value = host->runweight;
        break;
    case CS1VM_HOST_REQUEST_COORD_X:
        fake_log(host, request->kind, 0, 0);
        request->out_value = host->coord_x;
        break;
    case CS1VM_HOST_REQUEST_COORD_Z:
        fake_log(host, request->kind, 0, 0);
        request->out_value = host->coord_z;
        break;
    }

    return CS1VM_EXECNO_OK;
}

/* Builds a single-script component set over a caller-owned stream. */
static void
scripts_one(
    struct CS1VM_ComponentScripts* set,
    int** slot,
    int* lengths,
    int* stream,
    int len)
{
    memset(set, 0, sizeof(*set));
    slot[0] = stream;
    lengths[0] = len;
    set->scripts_count = 1;
    set->scripts = slot;
    set->scripts_lengths = lengths;
}

static int
eval_one(
    struct FakeHost* host,
    int* stream,
    int len,
    enum CS1VM_EvalStatus* status_out)
{
    struct CS1VM vm;
    struct CS1VM_ComponentScripts set;
    int* slot[1];
    int lengths[1];
    int value = 0;

    CS1VM_Init(&vm);
    CS1VM_BindHost(&vm, host, fake_host_exec);
    scripts_one(&set, slot, lengths, stream, len);

    enum CS1VM_EvalStatus status = CS1VM_EvalValue(&vm, &set, 0, &value);
    if( status_out )
        *status_out = status;
    return value;
}

static void
test_constant_and_return(void)
{
    printf("TEST: constant / return\n");

    struct FakeHost host;
    fake_host_init(&host);

    int stream[] = { CS1_OP_PUSH_CONSTANT, 42, CS1_OP_RETURN };
    enum CS1VM_EvalStatus status;
    int value = eval_one(&host, stream, 3, &status);

    TEST_ASSERT(status == CS1VM_EVAL_DONE, "constant script completes");
    TEST_ASSERT(value == 42, "constant script returns 42");

    int empty[] = { CS1_OP_RETURN };
    value = eval_one(&host, empty, 1, &status);
    TEST_ASSERT(status == CS1VM_EVAL_DONE, "bare return completes");
    TEST_ASSERT(value == 0, "bare return yields accumulator 0");
}

static void
test_arithmetic(void)
{
    printf("TEST: arithmetic folding\n");

    struct FakeHost host;
    fake_host_init(&host);
    enum CS1VM_EvalStatus status;

    /* Default operator is add: 10 + 5 = 15. */
    int add[] = { CS1_OP_PUSH_CONSTANT, 10, CS1_OP_PUSH_CONSTANT, 5, CS1_OP_RETURN };
    TEST_ASSERT(eval_one(&host, add, 5, &status) == 15, "implicit add");

    /* 10 - 4 = 6 */
    int sub[] = { CS1_OP_PUSH_CONSTANT, 10, CS1_OP_ARITH_SUB, CS1_OP_PUSH_CONSTANT,
                  4,                     CS1_OP_RETURN };
    TEST_ASSERT(eval_one(&host, sub, 6, &status) == 6, "subtract");

    /* 10 * 3 = 30 */
    int mul[] = { CS1_OP_PUSH_CONSTANT, 10, CS1_OP_ARITH_MUL, CS1_OP_PUSH_CONSTANT,
                  3,                     CS1_OP_RETURN };
    TEST_ASSERT(eval_one(&host, mul, 6, &status) == 30, "multiply");

    /* 30 / 4 = 7 (integer division) */
    int divi[] = { CS1_OP_PUSH_CONSTANT, 30, CS1_OP_ARITH_DIV, CS1_OP_PUSH_CONSTANT,
                   4,                     CS1_OP_RETURN };
    TEST_ASSERT(eval_one(&host, divi, 6, &status) == 7, "divide");

    /* Divide by zero skips the fold, leaving the accumulator untouched. */
    int div0[] = { CS1_OP_PUSH_CONSTANT, 10, CS1_OP_ARITH_DIV, CS1_OP_PUSH_CONSTANT,
                   0,                     CS1_OP_RETURN };
    TEST_ASSERT(eval_one(&host, div0, 6, &status) == 10, "divide by zero is skipped");

    /* Operator applies only to the next value, then resets to add:
     * 10 - 4 + 2 = 8 */
    int reset[] = { CS1_OP_PUSH_CONSTANT, 10, CS1_OP_ARITH_SUB,     CS1_OP_PUSH_CONSTANT, 4,
                    CS1_OP_PUSH_CONSTANT, 2,  CS1_OP_RETURN };
    TEST_ASSERT(eval_one(&host, reset, 8, &status) == 8, "operator resets to add after use");
}

static void
test_host_reads(void)
{
    printf("TEST: host state reads\n");

    struct FakeHost host;
    enum CS1VM_EvalStatus status;

    fake_host_init(&host);
    host.stat_level[3] = 74;
    int lvl[] = { CS1_OP_STAT_LEVEL, 3, CS1_OP_RETURN };
    TEST_ASSERT(eval_one(&host, lvl, 3, &status) == 74, "stat level read");
    TEST_ASSERT(host.log_count == 1 && host.log[0].a == 3, "stat level passes skill id");

    fake_host_init(&host);
    host.stat_base_level[3] = 70;
    int base[] = { CS1_OP_STAT_BASE_LEVEL, 3, CS1_OP_RETURN };
    TEST_ASSERT(eval_one(&host, base, 3, &status) == 70, "stat base level read");

    fake_host_init(&host);
    host.stat_xp[3] = 123456;
    int xp[] = { CS1_OP_STAT_XP, 3, CS1_OP_RETURN };
    TEST_ASSERT(eval_one(&host, xp, 3, &status) == 123456, "stat xp read");

    fake_host_init(&host);
    host.stat_xp_remaining[3] = 999;
    int xpr[] = { CS1_OP_STAT_XP_REMAINING, 3, CS1_OP_RETURN };
    TEST_ASSERT(eval_one(&host, xpr, 3, &status) == 999, "stat xp remaining read");

    fake_host_init(&host);
    host.varp[7] = 55;
    int varp[] = { CS1_OP_PUSH_VARP, 7, CS1_OP_RETURN };
    TEST_ASSERT(eval_one(&host, varp, 3, &status) == 55, "varp read");

    fake_host_init(&host);
    host.varbit[9] = 3;
    int varbit[] = { CS1_OP_PUSH_VARBIT, 9, CS1_OP_RETURN };
    TEST_ASSERT(eval_one(&host, varbit, 3, &status) == 3, "varbit read");

    fake_host_init(&host);
    host.combat_level = 126;
    int cb[] = { CS1_OP_COMBAT_LEVEL, CS1_OP_RETURN };
    TEST_ASSERT(eval_one(&host, cb, 2, &status) == 126, "combat level read (no operand)");

    fake_host_init(&host);
    host.total_level = 1500;
    int total[] = { CS1_OP_TOTAL_LEVEL, CS1_OP_RETURN };
    TEST_ASSERT(eval_one(&host, total, 2, &status) == 1500, "total level read");

    fake_host_init(&host);
    host.runenergy = 88;
    int energy[] = { CS1_OP_RUNENERGY, CS1_OP_RETURN };
    TEST_ASSERT(eval_one(&host, energy, 2, &status) == 88, "run energy read");

    fake_host_init(&host);
    host.runweight = 21;
    int weight[] = { CS1_OP_RUNWEIGHT, CS1_OP_RETURN };
    TEST_ASSERT(eval_one(&host, weight, 2, &status) == 21, "run weight read");

    fake_host_init(&host);
    host.coord_x = 3200;
    host.coord_z = 3200;
    int cx[] = { CS1_OP_COORD_X, CS1_OP_RETURN };
    TEST_ASSERT(eval_one(&host, cx, 2, &status) == 3200, "coord x read");
    int cz[] = { CS1_OP_COORD_Z, CS1_OP_RETURN };
    TEST_ASSERT(eval_one(&host, cz, 2, &status) == 3200, "coord z read");
}

static void
test_inv_ops(void)
{
    printf("TEST: inventory opcodes\n");

    struct FakeHost host;
    enum CS1VM_EvalStatus status;

    /* Both inv ops consume two operands: {component_id, obj_id}. */
    fake_host_init(&host);
    host.yield_kind = CS1VM_HOST_REQUEST_VARP; /* keep inv ops from yielding */
    host.inv_count = 12;
    int count[] = { CS1_OP_INV_COUNT, 5570, 995, CS1_OP_RETURN };
    TEST_ASSERT(eval_one(&host, count, 4, &status) == 12, "inv count read");
    TEST_ASSERT(
        host.log_count == 1 && host.log[0].a == 5570 && host.log[0].b == 995,
        "inv count passes {component_id, obj_id}");

    fake_host_init(&host);
    host.yield_kind = CS1VM_HOST_REQUEST_VARP;
    host.inv_count = 1;
    int has[] = { CS1_OP_INV_CONTAINS, 5570, 995, CS1_OP_RETURN };
    TEST_ASSERT(
        eval_one(&host, has, 4, &status) == CS1VM_VALUE_INFINITY, "inv contains -> infinity");

    fake_host_init(&host);
    host.yield_kind = CS1VM_HOST_REQUEST_VARP;
    host.inv_count = 0;
    TEST_ASSERT(eval_one(&host, has, 4, &status) == 0, "inv contains absent -> 0");
}

static void
test_varp_derived_ops(void)
{
    printf("TEST: VM-side varp math\n");

    struct FakeHost host;
    enum CS1VM_EvalStatus status;

    /* Op 7 scales a varp to a percentage: raw * 100 / 46875. */
    fake_host_init(&host);
    host.varp[4] = 46875;
    int scaled[] = { CS1_OP_PUSH_VARP_SCALED, 4, CS1_OP_RETURN };
    TEST_ASSERT(eval_one(&host, scaled, 3, &status) == 100, "varp scaled to 100%");
    TEST_ASSERT(
        host.log_count == 1 && host.log[0].kind == CS1VM_HOST_REQUEST_VARP,
        "scaled varp reads through the plain VARP request");

    fake_host_init(&host);
    host.varp[4] = 23437;
    TEST_ASSERT(eval_one(&host, scaled, 3, &status) == 49, "varp scaled truncates");

    /* Op 13 tests one bit of a varp. */
    fake_host_init(&host);
    host.varp[6] = 0x8; /* bit 3 set */
    int bit_set[] = { CS1_OP_VARP_TESTBIT, 6, 3, CS1_OP_RETURN };
    TEST_ASSERT(eval_one(&host, bit_set, 4, &status) == 1, "varp bit set -> 1");

    int bit_clear[] = { CS1_OP_VARP_TESTBIT, 6, 2, CS1_OP_RETURN };
    fake_host_init(&host);
    host.varp[6] = 0x8;
    TEST_ASSERT(eval_one(&host, bit_clear, 4, &status) == 0, "varp bit clear -> 0");
}

static void
test_unknown_opcode(void)
{
    printf("TEST: unknown opcode contributes zero\n");

    struct FakeHost host;
    fake_host_init(&host);
    enum CS1VM_EvalStatus status;

    int stream[] = { CS1_OP_PUSH_CONSTANT, 7, 99 /* unknown */, CS1_OP_RETURN };
    TEST_ASSERT(eval_one(&host, stream, 4, &status) == 7, "unknown opcode folds 0");
    TEST_ASSERT(status == CS1VM_EVAL_DONE, "unknown opcode is not fatal");
}

static void
test_truncated_stream(void)
{
    printf("TEST: truncated stream\n");

    struct FakeHost host;
    fake_host_init(&host);
    enum CS1VM_EvalStatus status;

    /* Operand missing after the opcode. */
    int stream[] = { CS1_OP_PUSH_CONSTANT };
    int value = eval_one(&host, stream, 1, &status);
    TEST_ASSERT(status == CS1VM_EVAL_DONE, "truncated stream still completes");
    TEST_ASSERT(value == CS1VM_VALUE_ERR_EXCEPTION, "truncated stream evaluates to -1");
}

static void
test_missing_script(void)
{
    printf("TEST: missing script index\n");

    struct FakeHost host;
    struct CS1VM vm;
    struct CS1VM_ComponentScripts set;
    int value = 0;

    fake_host_init(&host);
    CS1VM_Init(&vm);
    CS1VM_BindHost(&vm, &host, fake_host_exec);
    memset(&set, 0, sizeof(set));

    enum CS1VM_EvalStatus status = CS1VM_EvalValue(&vm, &set, 0, &value);
    TEST_ASSERT(status == CS1VM_EVAL_DONE, "missing script completes");
    TEST_ASSERT(value == CS1VM_VALUE_ERR_NO_SCRIPT, "missing script evaluates to -2");
}

static void
test_yield_and_retry(void)
{
    printf("TEST: yield rolls back and retries\n");

    struct FakeHost host;
    struct CS1VM vm;
    struct CS1VM_ComponentScripts set;
    int* slot[1];
    int lengths[1];
    int value = 0;

    /* 100 + inv_count, where the inv read yields once before succeeding. */
    int stream[] = { CS1_OP_PUSH_CONSTANT, 100, CS1_OP_INV_COUNT, 5570, 995, CS1_OP_RETURN };

    fake_host_init(&host);
    host.yield_kind = CS1VM_HOST_REQUEST_INV_COUNT;
    host.yield_countdown = 1;
    host.inv_count = 5;

    CS1VM_Init(&vm);
    CS1VM_BindHost(&vm, &host, fake_host_exec);
    scripts_one(&set, slot, lengths, stream, 6);

    enum CS1VM_EvalStatus status = CS1VM_EvalValue(&vm, &set, 0, &value);
    TEST_ASSERT(status == CS1VM_EVAL_YIELDED, "host yield surfaces as CS1VM_EVAL_YIELDED");
    TEST_ASSERT(vm.acc == 100, "accumulator rolled back to pre-opcode value");
    TEST_ASSERT(vm.pc == 2, "pc rolled back to the yielding opcode");

    /* The driver would service the load here; the retry re-runs from scratch. */
    status = CS1VM_EvalValue(&vm, &set, 0, &value);
    TEST_ASSERT(status == CS1VM_EVAL_DONE, "retry completes");
    TEST_ASSERT(value == 105, "retry folds the loaded value");
}

static void
test_yield_halt(void)
{
    printf("TEST: double yield at one site is a contract error\n");

    struct FakeHost host;
    struct CS1VM vm;
    struct CS1VM_ComponentScripts set;
    int* slot[1];
    int lengths[1];
    int value = 0;

    int stream[] = { CS1_OP_INV_COUNT, 5570, 995, CS1_OP_RETURN };

    fake_host_init(&host);
    host.yield_kind = CS1VM_HOST_REQUEST_INV_COUNT;
    host.yield_forever = 1;

    CS1VM_Init(&vm);
    CS1VM_BindHost(&vm, &host, fake_host_exec);
    scripts_one(&set, slot, lengths, stream, 4);

    enum CS1VM_EvalStatus status = CS1VM_EvalValue(&vm, &set, 0, &value);
    TEST_ASSERT(status == CS1VM_EVAL_YIELDED, "first yield is legal");

    status = CS1VM_EvalValue(&vm, &set, 0, &value);
    TEST_ASSERT(status == CS1VM_EVAL_ERROR, "second yield at the same site errors");
    TEST_ASSERT(vm.last_error_pc == 0, "error records the offending pc");
}

/* Builds a two-script component set with comparators. */
static void
scripts_with_comparators(
    struct CS1VM_ComponentScripts* set,
    int** slots,
    int* lengths,
    int scripts_count,
    int* comparator,
    int* operand,
    int comparator_count)
{
    memset(set, 0, sizeof(*set));
    set->scripts_count = scripts_count;
    set->scripts = slots;
    set->scripts_lengths = lengths;
    set->comparator = comparator;
    set->operand = operand;
    set->comparator_count = comparator_count;
}

static bool
eval_active_one(
    struct FakeHost* host,
    int* stream,
    int len,
    int comparator,
    int operand,
    enum CS1VM_EvalStatus* status_out)
{
    struct CS1VM vm;
    struct CS1VM_ComponentScripts set;
    int* slots[1];
    int lengths[1];
    int comparators[1];
    int operands[1];
    bool active = false;

    slots[0] = stream;
    lengths[0] = len;
    comparators[0] = comparator;
    operands[0] = operand;

    CS1VM_Init(&vm);
    CS1VM_BindHost(&vm, host, fake_host_exec);
    scripts_with_comparators(&set, slots, lengths, 1, comparators, operands, 1);

    enum CS1VM_EvalStatus status = CS1VM_EvalActive(&vm, &set, &active);
    if( status_out )
        *status_out = status;
    return active;
}

static void
test_comparators(void)
{
    printf("TEST: comparator table\n");

    struct FakeHost host;
    enum CS1VM_EvalStatus status;
    int stream[] = { CS1_OP_PUSH_CONSTANT, 10, CS1_OP_RETURN };

    fake_host_init(&host);
    TEST_ASSERT(
        eval_active_one(&host, stream, 3, CS1VM_COMPARATOR_EQUAL, 10, &status),
        "equal: 10 == 10 is active");
    TEST_ASSERT(
        !eval_active_one(&host, stream, 3, CS1VM_COMPARATOR_EQUAL, 11, &status),
        "equal: 10 == 11 is inactive");

    TEST_ASSERT(
        eval_active_one(&host, stream, 3, CS1VM_COMPARATOR_LESS, 11, &status),
        "less: 10 < 11 is active");
    TEST_ASSERT(
        !eval_active_one(&host, stream, 3, CS1VM_COMPARATOR_LESS, 10, &status),
        "less: 10 < 10 is inactive");

    TEST_ASSERT(
        eval_active_one(&host, stream, 3, CS1VM_COMPARATOR_GREATER, 9, &status),
        "greater: 10 > 9 is active");
    TEST_ASSERT(
        !eval_active_one(&host, stream, 3, CS1VM_COMPARATOR_GREATER, 10, &status),
        "greater: 10 > 10 is inactive");

    TEST_ASSERT(
        eval_active_one(&host, stream, 3, CS1VM_COMPARATOR_NOT_EQUAL, 11, &status),
        "not equal: 10 != 11 is active");
    TEST_ASSERT(
        !eval_active_one(&host, stream, 3, CS1VM_COMPARATOR_NOT_EQUAL, 10, &status),
        "not equal: 10 != 10 is inactive");
}

static void
test_active_edge_cases(void)
{
    printf("TEST: active-state edge cases\n");

    struct FakeHost host;
    struct CS1VM vm;
    struct CS1VM_ComponentScripts set;
    bool active = true;

    fake_host_init(&host);
    CS1VM_Init(&vm);
    CS1VM_BindHost(&vm, &host, fake_host_exec);

    /* No comparators: never active. */
    memset(&set, 0, sizeof(set));
    enum CS1VM_EvalStatus status = CS1VM_EvalActive(&vm, &set, &active);
    TEST_ASSERT(status == CS1VM_EVAL_DONE, "empty set completes");
    TEST_ASSERT(!active, "component without comparators is never active");

    /* Two comparators, AND semantics: both must pass. */
    int s0[] = { CS1_OP_PUSH_CONSTANT, 10, CS1_OP_RETURN };
    int s1[] = { CS1_OP_PUSH_CONSTANT, 20, CS1_OP_RETURN };
    int* slots[2] = { s0, s1 };
    int lengths[2] = { 3, 3 };
    int comparators[2] = { CS1VM_COMPARATOR_EQUAL, CS1VM_COMPARATOR_EQUAL };
    int operands[2] = { 10, 20 };

    scripts_with_comparators(&set, slots, lengths, 2, comparators, operands, 2);
    active = false;
    CS1VM_EvalActive(&vm, &set, &active);
    TEST_ASSERT(active, "both comparators passing is active");

    operands[1] = 21;
    active = true;
    CS1VM_EvalActive(&vm, &set, &active);
    TEST_ASSERT(!active, "one failing comparator makes the component inactive");

    /* comparator_count shorter than scripts_count: only the paired prefix runs.
     * Script 1 would fail, but it is not paired with a comparator. */
    operands[1] = 21;
    scripts_with_comparators(&set, slots, lengths, 2, comparators, operands, 1);
    active = false;
    CS1VM_EvalActive(&vm, &set, &active);
    TEST_ASSERT(active, "only the comparator-paired prefix is evaluated");

    /* A comparator with no matching script evaluates the -2 sentinel, which
     * fails its comparison rather than being silently skipped. */
    operands[1] = 21;
    scripts_with_comparators(&set, slots, lengths, 1, comparators, operands, 2);
    active = true;
    CS1VM_EvalActive(&vm, &set, &active);
    TEST_ASSERT(!active, "comparator without a script fails on the -2 sentinel");

    /* The sentinel is an ordinary value: a comparator that happens to target it
     * passes even with no scripts present. */
    operands[0] = CS1VM_VALUE_ERR_NO_SCRIPT;
    scripts_with_comparators(&set, slots, lengths, 0, comparators, operands, 1);
    active = false;
    CS1VM_EvalActive(&vm, &set, &active);
    TEST_ASSERT(active, "comparator targeting the -2 sentinel matches");
}

static void
test_active_yield_propagates(void)
{
    printf("TEST: yield inside the comparator pass propagates\n");

    struct FakeHost host;
    struct CS1VM vm;
    struct CS1VM_ComponentScripts set;
    int* slots[1];
    int lengths[1];
    int comparators[1] = { CS1VM_COMPARATOR_GREATER };
    int operands[1] = { 0 };
    bool active = true;

    int stream[] = { CS1_OP_INV_COUNT, 5570, 995, CS1_OP_RETURN };

    fake_host_init(&host);
    host.yield_kind = CS1VM_HOST_REQUEST_INV_COUNT;
    host.yield_countdown = 1;
    host.inv_count = 3;

    CS1VM_Init(&vm);
    CS1VM_BindHost(&vm, &host, fake_host_exec);
    slots[0] = stream;
    lengths[0] = 4;
    scripts_with_comparators(&set, slots, lengths, 1, comparators, operands, 1);

    enum CS1VM_EvalStatus status = CS1VM_EvalActive(&vm, &set, &active);
    TEST_ASSERT(status == CS1VM_EVAL_YIELDED, "comparator pass surfaces the yield");
    TEST_ASSERT(!active, "yielded evaluation reports inactive");

    status = CS1VM_EvalActive(&vm, &set, &active);
    TEST_ASSERT(status == CS1VM_EVAL_DONE, "retry completes");
    TEST_ASSERT(active, "retry sees the loaded count and activates");
}

static void
test_no_host_bound(void)
{
    printf("TEST: unbound host reads as zero\n");

    struct CS1VM vm;
    struct CS1VM_ComponentScripts set;
    int* slot[1];
    int lengths[1];
    int value = -1;

    int stream[] = { CS1_OP_PUSH_VARP, 7, CS1_OP_RETURN };

    CS1VM_Init(&vm);
    scripts_one(&set, slot, lengths, stream, 3);

    enum CS1VM_EvalStatus status = CS1VM_EvalValue(&vm, &set, 0, &value);
    TEST_ASSERT(status == CS1VM_EVAL_DONE, "unbound host still completes");
    TEST_ASSERT(value == 0, "unbound host reads default to 0");
}

int
main(void)
{
    test_constant_and_return();
    test_arithmetic();
    test_host_reads();
    test_inv_ops();
    test_varp_derived_ops();
    test_unknown_opcode();
    test_truncated_stream();
    test_missing_script();
    test_yield_and_retry();
    test_yield_halt();
    test_comparators();
    test_active_edge_cases();
    test_active_yield_propagates();
    test_no_host_bound();

    if( g_failures )
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }

    printf("All tests passed.\n");
    return 0;
}
