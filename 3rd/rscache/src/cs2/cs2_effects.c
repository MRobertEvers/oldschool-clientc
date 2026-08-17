#include "cs2_effects.h"

#include "cs2_command.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>

/*
 * The allowlist.
 *
 * Each row names the `CS2VM2_Op_*` handler it was checked against, in
 * src/cs2vm2/cs2vm2.c. "host-free" below means the whole handler body is: pop
 * its operands, compute, push — no `vm->vm->host_exec`, no read of a varp or a
 * component, no clock, no rng.
 *
 * Conspicuously absent, and not by oversight:
 *
 *   4004 random / 4005 randominc  a different answer every call
 *   3300 clientclock and friends  read a clock
 *   the whole string family       correct folding needs cp1252-exact
 *                                 lowercase/uppercase/compare shared with the
 *                                 VM, which does not exist yet; strings are
 *                                 propagated but never folded
 */
struct cs2_effect_row
{
    int opcode;
    enum RSCache_CS2_Effect effect;
};

static const struct cs2_effect_row cs2_effect_rows[] = {
    /* Arithmetic: CS2VM2_Op_Add / _Sub / _Mul / _Div / _Mod — host-free. */
    { RSCACHE_CS2_OP_ADD, RSCACHE_CS2_EFFECT_PURE },
    { RSCACHE_CS2_OP_SUB, RSCACHE_CS2_EFFECT_PURE },
    { RSCACHE_CS2_OP_MULTIPLY, RSCACHE_CS2_EFFECT_PURE },
    { RSCACHE_CS2_OP_DIV, RSCACHE_CS2_EFFECT_PURE },
    { RSCACHE_CS2_OP_MOD, RSCACHE_CS2_EFFECT_PURE },
    /* CS2VM2_Op_Interpolate, _AddPercent, _Pow, _InvPow, _Scale — host-free. */
    { 4006, RSCACHE_CS2_EFFECT_PURE }, /* interpolate */
    { 4007, RSCACHE_CS2_EFFECT_PURE }, /* addpercent  */
    { 4012, RSCACHE_CS2_EFFECT_PURE }, /* pow         */
    { 4013, RSCACHE_CS2_EFFECT_PURE }, /* invpow      */
    { 4018, RSCACHE_CS2_EFFECT_PURE }, /* scale       */
    /* Bit ops: CS2VM2_Op_SetBit / _ClearBit / _TestBit / _And / _Or — host-free. */
    { 4008, RSCACHE_CS2_EFFECT_PURE }, /* setbit   */
    { 4009, RSCACHE_CS2_EFFECT_PURE }, /* clearbit */
    { 4010, RSCACHE_CS2_EFFECT_PURE }, /* testbit  */
    { RSCACHE_CS2_OP_AND, RSCACHE_CS2_EFFECT_PURE },
    { RSCACHE_CS2_OP_OR, RSCACHE_CS2_EFFECT_PURE },
    /* CS2VM2_Op_Min / _Max — host-free. */
    { 4016, RSCACHE_CS2_EFFECT_PURE },
    { 4017, RSCACHE_CS2_EFFECT_PURE },
};

enum RSCache_CS2_Effect
RSCache_CS2_EffectOf(int opcode)
{
    for( int i = 0; i < (int)(sizeof(cs2_effect_rows) / sizeof(cs2_effect_rows[0])); i++ )
    {
        if( cs2_effect_rows[i].opcode == opcode )
            return cs2_effect_rows[i].effect;
    }
    return RSCACHE_CS2_EFFECT_HOST;
}

bool
RSCache_CS2_EffectIsPure(int opcode)
{
    return RSCache_CS2_EffectOf(opcode) == RSCACHE_CS2_EFFECT_PURE;
}

bool
RSCache_CS2_EffectIsDeletable(int opcode)
{
    return RSCache_CS2_EffectOf(opcode) != RSCACHE_CS2_EFFECT_HOST;
}

/*
 * Folding.
 *
 * Signed overflow is undefined in C and defined in the VM, whose ints are the
 * JVM's: everything wraps. So the arithmetic below is done in `uint32_t` and
 * cast back, which is the wrap the client performs and is also the only way to
 * keep a `-fsanitize=undefined` build honest.
 */
static int
cs2_wrap(int64_t value)
{
    return (int)(uint32_t)(uint64_t)value;
}

bool
RSCache_CS2_EffectFoldInt(int opcode, const int* args, int arg_count, int* out)
{
    assert(args || arg_count == 0);
    assert(out);
    if( !RSCache_CS2_EffectIsPure(opcode) )
        return false;

    /* The signature is the arity contract; a mismatch means the caller built a
     * node this opcode could not have produced. */
    const struct RSCache_CS2_CommandInfo* info = RSCache_CS2_CommandGet(opcode);
    if( !info || info->arg_count != arg_count || info->def_count != 1 )
        return false;

    switch( opcode )
    {
    case RSCACHE_CS2_OP_ADD:
        *out = cs2_wrap((int64_t)args[0] + args[1]);
        return true;
    case RSCACHE_CS2_OP_SUB:
        *out = cs2_wrap((int64_t)args[0] - args[1]);
        return true;
    case RSCACHE_CS2_OP_MULTIPLY:
        *out = cs2_wrap((int64_t)args[0] * args[1]);
        return true;
    case RSCACHE_CS2_OP_DIV:
        /* CS2VM2_Op_Div returns CS2VM_EXECNO_ERROR on a zero divisor, which
         * stops the script. Folding would answer where the client halts. */
        if( args[1] == 0 )
            return false;
        /* INT_MIN / -1 traps on the hardware and wraps in the VM's Java-shaped
         * arithmetic; spell the wrap rather than executing the trap. */
        if( args[0] == (-2147483647 - 1) && args[1] == -1 )
        {
            *out = args[0];
            return true;
        }
        *out = args[0] / args[1];
        return true;
    case RSCACHE_CS2_OP_MOD:
        if( args[1] == 0 )
            return false;
        if( args[0] == (-2147483647 - 1) && args[1] == -1 )
        {
            *out = 0;
            return true;
        }
        *out = args[0] % args[1];
        return true;
    case 4006:
    {
        /* CS2VM2_Op_Interpolate. Its multiply is 32-bit and its divide
         * truncates; both are reproduced rather than corrected. */
        int a = args[0];
        int b = args[1];
        int c = args[2];
        int d = args[3];
        int e = args[4];
        int denom = d - c;
        if( denom == 0 )
        {
            *out = a;
            return true;
        }
        int mul = cs2_wrap((int64_t)cs2_wrap((int64_t)b - a) * cs2_wrap((int64_t)e - c));
        *out = cs2_wrap((int64_t)a + mul / denom);
        return true;
    }
    case 4007:
        /* CS2VM2_Op_AddPercent: value + (int)((int64)value * percent / 100). */
        *out = cs2_wrap((int64_t)args[0] +
                        cs2_wrap(((int64_t)args[0] * args[1]) / 100));
        return true;
    case 4008:
        *out = cs2_wrap((int64_t)(uint32_t)((uint32_t)args[0] | (1u << (args[1] & 31))));
        return true;
    case 4009:
        *out = cs2_wrap((int64_t)(uint32_t)((uint32_t)args[0] & ~(1u << (args[1] & 31))));
        return true;
    case 4010:
        *out = ((uint32_t)args[0] & (1u << (args[1] & 31))) != 0 ? 1 : 0;
        return true;
    case 4012:
        /* CS2VM2_Op_Pow goes through `double`, so the fold has to as well —
         * (int)pow(a, b) is not the same as an integer power loop. */
        *out = (int)pow((double)args[0], (double)args[1]);
        return true;
    case 4013:
    {
        /* CS2VM2_Op_InvPow: the exponent-th integer *root*, not a power. */
        int base = args[0];
        int exponent = args[1];
        if( base == 0 )
        {
            *out = 0;
            return true;
        }
        switch( exponent )
        {
        case 0:
            *out = 2147483647;
            return true;
        case 1:
            *out = base;
            return true;
        case 2:
            *out = (int)sqrt((double)base);
            return true;
        case 3:
            *out = (int)cbrt((double)base);
            return true;
        case 4:
            *out = (int)sqrt(sqrt((double)base));
            return true;
        default:
            *out = (int)pow((double)base, 1.0 / exponent);
            return true;
        }
    }
    case RSCACHE_CS2_OP_AND:
        *out = (int)((uint32_t)args[0] & (uint32_t)args[1]);
        return true;
    case RSCACHE_CS2_OP_OR:
        *out = (int)((uint32_t)args[0] | (uint32_t)args[1]);
        return true;
    case 4016:
        *out = args[0] < args[1] ? args[0] : args[1];
        return true;
    case 4017:
        *out = args[0] > args[1] ? args[0] : args[1];
        return true;
    case 4018:
    {
        /*
         * CS2VM2_Op_Scale pops c, b, a — so in push order the arguments are
         * (a, b, c) and the answer is c * a / b, which is not what the name
         * suggests and is what the client does.
         */
        int a = args[0];
        int b = args[1];
        int c = args[2];
        if( b == 0 )
        {
            *out = 0;
            return true;
        }
        *out = (int)(((int64_t)c * (int64_t)a) / (int64_t)b);
        return true;
    }
    default:
        return false;
    }
}
