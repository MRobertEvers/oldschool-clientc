#include "plugin/torirs_plugin.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Item stats: what the thing under the pointer would DO.
 *
 * A port of RuneLite's `itemstats` plugin (ItemStatOverlay, ItemStatChanges and
 * the calculators beneath them). Hover an inventory cell, a worn slot or a bank
 * cell and a tooltip says how much a shark heals RIGHT NOW, how much of a
 * prayer potion would be wasted at 60/99, and what swapping this weapon for the
 * one already equipped does to every combat bonus.
 *
 * Two halves, from two different sources, and the difference matters:
 *
 *   - CONSUMABLES are a TABLE, because no cache states them. How much a shark
 *     heals is server behaviour; the cache knows the item is called "Shark" and
 *     that eating it plays an animation. RuneLite carries the same table for
 *     the same reason.
 *
 *   - EQUIPMENT is the CACHE where the cache knows. An OldSchool obj record
 *     keeps its twelve bonuses in its own param block (ids 0..11, with 14 the
 *     attack rate and 12/189 the ranged strength), which is the same block this
 *     tree's server reads to run real accuracy formulas. So on those revisions
 *     every item in the game answers, including ones authored after this file.
 *
 *     A DAT1 cache states none of it -- no params, no wearpos, no attack rate,
 *     because in 2004 the bonuses were the SERVER's and the equipment screen
 *     was text it sent. For those worlds the numbers are shipped too, in
 *     `bonuses.txt` beside the font (see tools/item_bonus_bake), and read ONLY
 *     when the open cache carries nothing -- so an OldSchool session can never
 *     be told about a different game's balance.
 *
 * The table is keyed by NAME and not by item id, which is the one real
 * departure from the reference. RuneLite runs against one revision and can name
 * `ItemID.SHARK`; this client boots caches from 2004 to 2024 and the same food
 * is a different number in each. A name survives that -- a Shark has been
 * "Shark" throughout -- and it survives being read by a person, which an id
 * table cannot. Doses are stripped before the lookup, so one row covers
 * "Prayer potion(1)" through "(4)", and so are the fraction words, so one row
 * covers a cake, a 2/3 cake and a slice of it.
 *
 * What is deliberately NOT ported:
 *
 *   - Weight. RuneLite reads it from its bundled item_stats.json; no cache
 *     states it, so the row would be a column of zeroes.
 *   - Magic damage. Param 299 looks like it (Kodai wand 150 = +15%, imbued god
 *     cape 20 = +2%), but the occult necklace reads 50 where the game shows
 *     +10% on both cache.osrs230 and cache.osrs239 -- so the param is not the
 *     whole number, and a row that is confidently wrong for the most famous
 *     magic-damage item in the game is worse than no row.
 *   - Potion DURATIONS (RuneLite's PotionDuration) and the leagues
 *     combat-mastery multipliers, which are varbits no revision here defines.
 *   - Spicy stew, whose boost is four quest varbits.
 */

/* ------------------------------------------------------------------ model */

/*
 * Skill indices are the revision's own -- the numbers api->stat takes, which
 * have not moved since 2001. Run energy is not one of them: it arrives in a
 * packet of its own and is a percent rather than a level, so it gets an id
 * outside the table and its own pair of accessors.
 */
enum
{
    IS_ATTACK = 0,
    IS_DEFENCE = 1,
    IS_STRENGTH = 2,
    IS_HITPOINTS = 3,
    IS_RANGED = 4,
    IS_PRAYER = 5,
    IS_MAGIC = 6,
    IS_COOKING = 7,
    IS_WOODCUTTING = 8,
    IS_FLETCHING = 9,
    IS_FISHING = 10,
    IS_FIREMAKING = 11,
    IS_CRAFTING = 12,
    IS_SMITHING = 13,
    IS_MINING = 14,
    IS_HERBLORE = 15,
    IS_AGILITY = 16,
    IS_THIEVING = 17,
    IS_SLAYER = 18,
    IS_FARMING = 19,
    IS_RUNECRAFT = 20,
    IS_HUNTER = 21,
    IS_CONSTRUCTION = 22,
    IS_SKILL_COUNT = 23,
    /** Not a skill. @see is_stat_value. */
    IS_RUN_ENERGY = -2
};

/*
 * Positivity, in RuneLite's own order -- the order is load-bearing, because
 * merging a set of changes takes the MAXIMUM of it (Combo.calculate).
 */
enum is_positivity
{
    IS_WORSE = 0,
    IS_NO_CHANGE,
    IS_BETTER_CAPPED,
    IS_BETTER_SOMECAPPED,
    IS_BETTER_UNCAPPED,
    IS_POSITIVITY_COUNT
};

/* How one op's delta is computed. IS_PERC is RuneLite's DeltaPercentage
 * against the base level; the rest are the calculators that need more than a
 * percentage and a constant. */
enum is_calc
{
    /** DeltaPercentage against the BASE level: SimpleStatBoost. */
    IS_PERC = 0,
    /** DeltaPercentage against the BOOSTED level: BoostedStatBoost. The
     *  difference is what makes a drain get harsher the higher you are
     *  boosted. */
    IS_PERC_BOOSTED,
    /** ceil(base * perc), for the two fruits that heal a rounded-up share. */
    IS_PERC_CEIL,
    IS_ANGLERFISH,
    IS_COOKED_BREAM,
    IS_COOKED_MOSS_LIZARD,
    IS_PRAYER_POTION,
    IS_STAMINA,
    IS_ROCK_CAKE_EAT,
    IS_ROCK_CAKE_GUZZLE,
    IS_LOCATOR_ORB,
    IS_CAVE_NIGHTSHADE,
    IS_NETTLE_TEA
};

/*
 * One stat change an item would make.
 *
 * `num` is a percentage in HUNDREDTHS, so RuneLite's `perc(.15, 5)` is
 * { .num = 15, .delta = 5 }. Integer arithmetic rather than a double because
 * the reference's own calculator truncates -- `((int)(max * perc))` -- and C's
 * integer division truncates toward zero the same way, so the two agree
 * exactly, including on the negative percentages a drain uses.
 */
struct is_op
{
    short stat;
    unsigned char calc;
    /** StatBoost.boost: a boost may push the level PAST its base, a heal may
     *  only restore up to it. */
    unsigned char boost;
    /** CappedStatBoost: the boost stops at base + cap rather than at the
     *  delta, which is how the ancient brew's prayer boost behaves. */
    unsigned char capped;
    /** A second calculator whose result brackets the first: RangeStatBoost,
     *  the "5~7" a snail or a spider heals for. */
    unsigned char ranged;
    short num;
    short delta;
    short num_hi;
    short delta_hi;
    short cap_num;
    short cap_delta;
};

/* Families whose op list is not fixed: they depend on what the player has
 * equipped, or on which stats are currently drained. Each one fills the op
 * array before the generic evaluation runs, so there is still exactly one
 * evaluator. */
enum is_family
{
    IS_FAMILY_NONE = 0,
    /** SuperRestore: prayer, then every drained skill. */
    IS_FAMILY_SUPER_RESTORE,
    /** SaradominBrew: hp and defence up, the four attacking stats down. */
    IS_FAMILY_SARADOMIN_BREW,
    /** AncientBrew: prayer capped, magic up, the three melee stats down. */
    IS_FAMILY_ANCIENT_BREW,
    /** MoonlightPotion: the best effect the player's Herblore level unlocks,
     *  per stat. */
    IS_FAMILY_MOONLIGHT,
    /** Ambrosia: a boost sized to bring the stat all the way back first. */
    IS_FAMILY_AMBROSIA,
    /** MixedPotion: the base potion plus a fixed heal. */
    IS_FAMILY_MIXED
};

#define IS_OPS_MAX 32

struct is_effect
{
    struct is_op const* ops;
    int op_count;
    unsigned char family;
    /** IS_FAMILY_MIXED: hitpoints healed on top of the base potion. */
    short family_arg;
    /** IS_FAMILY_SARADOMIN_BREW / SUPER_RESTORE / ANCIENT_BREW: the
     *  percentages the reference passes to their constructors, in hundredths. */
    short perc_a;
    short perc_b;
    short perc_c;
    short delta_a;
    short delta_b;
    /** IS_FAMILY_MIXED: the potion this is a mix of. */
    struct is_effect const* base;
};

struct is_item
{
    /** Normalized name: lowercase, no dose suffix, no fraction word. */
    char const* name;
    struct is_effect const* effect;
};

/* One computed change, ready to print. */
struct is_change
{
    short stat;
    int relative;
    int theoretical;
    int absolute;
    /** RangeStatChange: the low end of each, when the op was a range. */
    int min_relative;
    int min_theoretical;
    unsigned char has_range;
    unsigned char positivity;
};

/* ---------------------------------------------------------------- the api */

static struct ToriRS_PluginApi const* g_api;

/*
 * What the pointer was over, as of the last hover rebuild.
 *
 * Stashed rather than asked for at draw time because the client answers this
 * question exactly once per frame, while building the mouseover line: an
 * inventory cell's item id is a field on the minimenu row it produces, and
 * there is no second path to it. The reference does the same thing from the
 * other end -- ItemStatOverlay reads `client.getMenuEntries()` while
 * rendering.
 *
 * `frame` is what keeps it honest. The right-click menu suppresses the hover
 * rebuild entirely (which is RuneLite's `client.isMenuOpen()` gate, for free),
 * so a stash nobody refreshed goes stale and stops drawing rather than
 * following the pointer around under an open menu.
 */
static struct
{
    int obj_id;
    int component_id;
    int slot;
    long frame;
} g_hover;

static long g_frame;

/* ------------------------------------------------------------ the player */

/*
 * The player's numbers, sampled once per tooltip.
 *
 * Once, and not per op: a combo potion asks for the same level five times, and
 * a super restore walks every skill in the game. Sampling once also means every
 * row of one tooltip is computed against the same instant, which is what makes
 * the totals add up.
 */
struct is_player
{
    int current[IS_SKILL_COUNT];
    int base[IS_SKILL_COUNT];
    int run_energy;
};

static void
is_player_sample(struct ToriRS_PluginCtx* ctx, struct is_player* out)
{
    assert(ctx);
    assert(out);

    memset(out, 0, sizeof(*out));
    for( int skill = 0; skill < IS_SKILL_COUNT; skill++ )
        g_api->stat(ctx, skill, &out->current[skill], &out->base[skill]);
    out->run_energy = g_api->run_energy(ctx);
}

/** The BOOSTED level, or the run meter. RuneLite's Stat.getValue. */
static int
is_stat_value(struct is_player const* player, int stat)
{
    assert(player);
    if( stat == IS_RUN_ENERGY )
        return player->run_energy;
    if( stat < 0 || stat >= IS_SKILL_COUNT )
        return 0;
    return player->current[stat];
}

/** The EARNED level, which is the ceiling a heal restores to. Run energy's is
 *  the constant 100, exactly as EnergyStat says. */
static int
is_stat_maximum(struct is_player const* player, int stat)
{
    assert(player);
    if( stat == IS_RUN_ENERGY )
        return 100;
    if( stat < 0 || stat >= IS_SKILL_COUNT )
        return 0;
    return player->base[stat];
}

static char const*
is_stat_name(struct ToriRS_PluginCtx* ctx, int stat)
{
    char const* name;

    assert(ctx);
    if( stat == IS_RUN_ENERGY )
        return "Run Energy";
    name = g_api->skill_name(ctx, stat);
    return name ? name : "?";
}

/* ----------------------------------------------------- the calculators */

/*
 * DeltaPercentage: `((int)(max * perc)) * sign(delta) + delta`.
 *
 * The sign flip is the reference's and it is not a rounding detail: a drain
 * written `perc(.02, -3)` means "three, plus two percent MORE" and not "three
 * minus two percent", so the percentage has to follow the constant's sign.
 */
static int
is_delta_perc(int against, int num, int delta)
{
    int scaled = (against * num) / 100;
    if( delta < 0 )
        scaled = -scaled;
    return scaled + delta;
}

/* ceil against a positive percentage, without pulling in <math.h> for one
 * call: the strawberry heals 6% of the base level rounded UP, so a level-10
 * player still gets 1. */
static int
is_delta_perc_ceil(int against, int num)
{
    int scaled = against * num;
    if( scaled <= 0 )
        return 0;
    return (scaled + 99) / 100;
}

/* Anglerfish: a flat share of the base level plus a constant that steps with
 * it. Verbatim from RuneLite's Anglerfish. */
static int
is_anglerfish_heal(int max_hp)
{
    int c;
    if( max_hp <= 24 )
        c = 2;
    else if( max_hp <= 49 )
        c = 4;
    else if( max_hp <= 74 )
        c = 6;
    else if( max_hp <= 92 )
        c = 8;
    else
        c = 13;
    return (max_hp / 10) + c;
}

static int
is_min(int a, int b)
{
    return a < b ? a : b;
}

static int
is_max(int a, int b)
{
    return a > b ? a : b;
}

/*
 * Is a holy wrench (or a thing that carries its effect) to hand?
 *
 * The reference walks RuneLite's ItemVariationMapping for every prayer cape
 * and every imbued ring of the gods; there is no such mapping here, so this
 * matches on the NAME the cache states, which is what the rest of this file is
 * keyed by anyway. Worn cape and ring first, then the backpack, exactly as
 * PrayerPotion does -- a wrench in the bank does nothing.
 */
static int
is_has_holy_wrench(struct ToriRS_PluginCtx* ctx)
{
    static char const* const k_wrench[] = {
        "holy wrench", "prayer cape", "max cape", "ring of the gods"
    };
    int const worn[] = { 1 /* cape */, 12 /* ring */ };
    struct ToriRS_PluginObjInfo info;

    assert(ctx);

    for( int i = 0; i < (int)(sizeof(worn) / sizeof(worn[0])); i++ )
    {
        int obj_id = -1;
        if( !g_api->inv_slot(ctx, TORIRS_PLUGIN_INV_WORN, worn[i], &obj_id, NULL) )
            continue;
        if( obj_id < 0 || !g_api->obj_info(ctx, obj_id, &info) )
            continue;
        for( int k = 0; k < (int)(sizeof(k_wrench) / sizeof(k_wrench[0])); k++ )
        {
            /* A prefix test, so "Ring of the gods (i)" and "Max cape" both
             * match while "Ring of recoil" cannot. */
            size_t len = strlen(k_wrench[k]);
            char lowered[64];
            snprintf(lowered, sizeof(lowered), "%s", info.name);
            for( char* p = lowered; *p; p++ )
                if( *p >= 'A' && *p <= 'Z' )
                    *p = (char)(*p - 'A' + 'a');
            if( strncmp(lowered, k_wrench[k], len) == 0 )
                return 1;
        }
    }

    for( int slot = 0, size = g_api->inv_size(ctx, TORIRS_PLUGIN_INV_BACKPACK); slot < size;
         slot++ )
    {
        int obj_id = -1;
        char lowered[64];
        if( !g_api->inv_slot(ctx, TORIRS_PLUGIN_INV_BACKPACK, slot, &obj_id, NULL) )
            continue;
        if( obj_id < 0 || !g_api->obj_info(ctx, obj_id, &info) )
            continue;
        snprintf(lowered, sizeof(lowered), "%s", info.name);
        for( char* p = lowered; *p; p++ )
            if( *p >= 'A' && *p <= 'Z' )
                *p = (char)(*p - 'A' + 'a');
        for( int k = 0; k < (int)(sizeof(k_wrench) / sizeof(k_wrench[0])); k++ )
            if( strncmp(lowered, k_wrench[k], strlen(k_wrench[k])) == 0 )
                return 1;
    }
    return 0;
}

/** Is the named item in the given worn slot? Used by the two potions whose
 *  strength depends on one piece of equipment. */
static int
is_worn_named(struct ToriRS_PluginCtx* ctx, int slot, char const* prefix)
{
    struct ToriRS_PluginObjInfo info;
    char lowered[64];
    int obj_id = -1;

    assert(ctx);
    assert(prefix);

    if( !g_api->inv_slot(ctx, TORIRS_PLUGIN_INV_WORN, slot, &obj_id, NULL) )
        return 0;
    if( obj_id < 0 || !g_api->obj_info(ctx, obj_id, &info) )
        return 0;
    snprintf(lowered, sizeof(lowered), "%s", info.name);
    for( char* p = lowered; *p; p++ )
        if( *p >= 'A' && *p <= 'Z' )
            *p = (char)(*p - 'A' + 'a');
    return strncmp(lowered, prefix, strlen(prefix)) == 0;
}

/* RuneLite's StatBoost.heals(): how much this op MOVES the stat, before the
 * cap at the top is applied. */
static int
is_op_heals(
    struct ToriRS_PluginCtx* ctx,
    struct is_player const* player,
    struct is_op const* op,
    int use_high_end)
{
    int max = is_stat_maximum(player, op->stat);
    int value = is_stat_value(player, op->stat);
    int num = use_high_end ? op->num_hi : op->num;
    int delta = use_high_end ? op->delta_hi : op->delta;

    assert(ctx);
    assert(player);
    assert(op);

    switch( op->calc )
    {
    case IS_PERC:
        break;
    case IS_PERC_BOOSTED:
        return is_delta_perc(value, num, delta);
    case IS_PERC_CEIL:
        return is_delta_perc_ceil(max, num);
    case IS_ANGLERFISH:
        return is_anglerfish_heal(max);
    case IS_COOKED_BREAM:
        return is_max(
            7,
            is_min(
                is_stat_value(player, IS_COOKING) / 3, is_stat_value(player, IS_FISHING) / 3));
    case IS_COOKED_MOSS_LIZARD:
        return is_min(
            is_stat_value(player, IS_COOKING) / 3, is_stat_value(player, IS_HUNTER) / 2);
    case IS_PRAYER_POTION:
    {
        /* max(base * perc, 0) + delta, with a holy wrench worth two more
         * percentage points. */
        int num_used = num + (is_has_holy_wrench(ctx) ? 2 : 0);
        return (max * num_used) / 100 + delta;
    }
    case IS_STAMINA:
        /* A ring of endurance doubles it. */
        return is_worn_named(ctx, 12, "ring of endurance") ? delta * 2 : delta;
    case IS_ROCK_CAKE_EAT:
        return value <= 1 ? 0 : -1;
    case IS_ROCK_CAKE_GUZZLE:
        return value <= 1 ? 0 : -(value / 10 + 1);
    case IS_LOCATOR_ORB:
        return -is_max(0, is_min(value - 1, 10));
    case IS_CAVE_NIGHTSHADE:
        return value < 20 ? -value / 2 : -15;
    case IS_NETTLE_TEA:
        /* Only restores run energy while hitpoints are down. */
        return is_stat_value(player, IS_HITPOINTS) < is_stat_maximum(player, IS_HITPOINTS) ? 5
                                                                                          : 0;
    default:
        break;
    }

    if( op->capped )
    {
        /* CappedStatBoost: the boost may not carry the stat past base + cap,
         * however much of it is left. */
        int cap = is_delta_perc(max, op->cap_num, op->cap_delta);
        int computed = is_delta_perc(max, num, delta);
        if( computed + value <= max + cap )
            return computed;
        if( value > max + cap )
            return 0;
        return max + cap - value;
    }
    return is_delta_perc(max, num, delta);
}

/*
 * RuneLite's StatBoost.effect(): turn a delta into a change, with the cap at
 * the top of the stat applied.
 *
 * The leagues combat-mastery multipliers and the sunlight cuffs are not here:
 * both are varbits and an item this client's revisions do not have, and a
 * multiplier read off a var that does not exist would be a silent 1.0 anyway.
 */
static void
is_op_effect(
    struct ToriRS_PluginCtx* ctx,
    struct is_player const* player,
    struct is_op const* op,
    int use_high_end,
    struct is_change* out)
{
    int value = is_stat_value(player, op->stat);
    int max = is_stat_maximum(player, op->stat);
    int delta = is_op_heals(ctx, player, op, use_high_end);
    int hit_cap = 0;
    int new_value;

    assert(out);

    if( op->boost && delta > 0 )
        max += delta;
    if( value > max )
        max = value;
    new_value = value + delta;
    if( new_value > max )
    {
        new_value = max;
        hit_cap = 1;
    }
    if( new_value < 0 )
        new_value = 0;

    memset(out, 0, sizeof(*out));
    out->stat = op->stat;
    out->absolute = new_value;
    out->relative = new_value - value;
    out->theoretical = delta;
    if( out->relative > 0 )
        out->positivity = hit_cap ? IS_BETTER_CAPPED : IS_BETTER_UNCAPPED;
    else if( out->relative == 0 )
        out->positivity = IS_NO_CHANGE;
    else
        out->positivity = IS_WORSE;
}

/* RangeStatBoost: both ends computed, then merged into one row that reads
 * "+5~7". The positivity is the AVERAGE of the two ordinals, which is the
 * reference's own rule. */
static void
is_op_apply(
    struct ToriRS_PluginCtx* ctx,
    struct is_player const* player,
    struct is_op const* op,
    struct is_change* out)
{
    struct is_change lo;
    struct is_change hi;

    assert(out);

    if( !op->ranged )
    {
        is_op_effect(ctx, player, op, 0, out);
        return;
    }

    is_op_effect(ctx, player, op, 0, &lo);
    is_op_effect(ctx, player, op, 1, &hi);

    memset(out, 0, sizeof(*out));
    out->stat = op->stat;
    out->has_range = 1;
    out->absolute = is_max(lo.absolute, hi.absolute);
    out->relative = is_max(lo.relative, hi.relative);
    out->theoretical = is_max(lo.theoretical, hi.theoretical);
    out->min_relative = is_min(lo.relative, hi.relative);
    out->min_theoretical = is_min(lo.theoretical, hi.theoretical);
    out->positivity = (unsigned char)((lo.positivity + hi.positivity) / 2);
}

/* --------------------------------------------------------- the behaviours */

/*
 * The table's vocabulary, in the reference's own words: `food(4)` heals four,
 * `boost(ATTACK, perc(.15, 5))` is a super attack potion, `drain` is the
 * BoostedStatBoost every ale applies to the stat it takes from.
 *
 * Percentages are hundredths (`perc(.15, 5)` is `15, 5`) so the whole table is
 * integers.
 */
#define IS_OP_MAKE(stat_, calc_, boost_, num_, delta_)                                        \
    {                                                                                          \
        (stat_), (calc_), (boost_), 0, 0, (num_), (delta_), 0, 0, 0, 0                         \
    }
#define IS_FOOD(d) IS_OP_MAKE(IS_HITPOINTS, IS_PERC, 0, 0, (d))
#define IS_FOODP(p, d) IS_OP_MAKE(IS_HITPOINTS, IS_PERC, 0, (p), (d))
#define IS_BOOST(s, d) IS_OP_MAKE((s), IS_PERC, 1, 0, (d))
#define IS_BOOSTP(s, p, d) IS_OP_MAKE((s), IS_PERC, 1, (p), (d))
#define IS_HEAL(s, d) IS_OP_MAKE((s), IS_PERC, 0, 0, (d))
#define IS_HEALP(s, p, d) IS_OP_MAKE((s), IS_PERC, 0, (p), (d))
/* BoostedStatBoost(stat, false, perc(p, -d)): a drain measured against the
 * level you are AT, which is why it bites harder when boosted. */
#define IS_DRAIN(s, p, d) IS_OP_MAKE((s), IS_PERC_BOOSTED, 0, (p), (d))
#define IS_SPECIAL(s, calc_) IS_OP_MAKE((s), (calc_), 0, 0, 0)
#define IS_SPECIAL_BOOST(s, calc_) IS_OP_MAKE((s), (calc_), 1, 0, 0)
#define IS_RANGE_FOOD(a, b)                                                                    \
    {                                                                                          \
        IS_HITPOINTS, IS_PERC, 0, 0, 1, 0, (a), 0, (b), 0, 0                                   \
    }
#define IS_CAPPED(s, p, d, cp, cd)                                                             \
    {                                                                                          \
        (s), IS_PERC, 1, 1, 0, (p), (d), 0, 0, (cp), (cd)                                       \
    }

#define IS_EFFECT(sym, ...)                                                                    \
    static struct is_op const sym##_OPS[] = { __VA_ARGS__ };                                   \
    static struct is_effect const sym = {                                                      \
        sym##_OPS,                                                                             \
        (int)(sizeof(sym##_OPS) / sizeof(sym##_OPS[0])),                                        \
        IS_FAMILY_NONE,                                                                        \
        0, 0, 0, 0, 0, 0, NULL                                                                  \
    }

#define IS_EFFECT_FAMILY(sym, fam, a, b, c, da, db)                                            \
    static struct is_effect const sym = { NULL, 0, (fam), 0, (a), (b), (c), (da), (db), NULL }

/* A mix: the base potion, plus the hitpoints the vial of water carries. */
#define IS_EFFECT_MIXED(sym, heal_, base_)                                                     \
    static struct is_effect const sym = {                                                      \
        NULL, 0, IS_FAMILY_MIXED, (heal_), 0, 0, 0, 0, 0, &(base_)                              \
    }

/* -- food -- */

IS_EFFECT(EF_FOOD1, IS_FOOD(1));
IS_EFFECT(EF_FOOD2, IS_FOOD(2));
IS_EFFECT(EF_FOOD3, IS_FOOD(3));
IS_EFFECT(EF_FOOD4, IS_FOOD(4));
IS_EFFECT(EF_FOOD5, IS_FOOD(5));
IS_EFFECT(EF_FOOD6, IS_FOOD(6));
IS_EFFECT(EF_FOOD7, IS_FOOD(7));
IS_EFFECT(EF_FOOD8, IS_FOOD(8));
IS_EFFECT(EF_FOOD9, IS_FOOD(9));
IS_EFFECT(EF_FOOD10, IS_FOOD(10));
IS_EFFECT(EF_FOOD11, IS_FOOD(11));
IS_EFFECT(EF_FOOD12, IS_FOOD(12));
IS_EFFECT(EF_FOOD13, IS_FOOD(13));
IS_EFFECT(EF_FOOD14, IS_FOOD(14));
IS_EFFECT(EF_FOOD15, IS_FOOD(15));
IS_EFFECT(EF_FOOD16, IS_FOOD(16));
IS_EFFECT(EF_FOOD18, IS_FOOD(18));
IS_EFFECT(EF_FOOD19, IS_FOOD(19));
IS_EFFECT(EF_FOOD20, IS_FOOD(20));
IS_EFFECT(EF_FOOD21, IS_FOOD(21));
IS_EFFECT(EF_FOOD22, IS_FOOD(22));
IS_EFFECT(EF_FOOD_BAD_KARAMBWAN, IS_FOOD(-5));
IS_EFFECT(EF_ANGLERFISH, IS_SPECIAL_BOOST(IS_HITPOINTS, IS_ANGLERFISH));
IS_EFFECT(EF_STRAWBERRY, IS_OP_MAKE(IS_HITPOINTS, IS_PERC_CEIL, 0, 6, 0));
IS_EFFECT(EF_WATERMELON, IS_OP_MAKE(IS_HITPOINTS, IS_PERC_CEIL, 0, 5, 0));
IS_EFFECT(EF_SWEETCORN, IS_FOODP(10, 1));
IS_EFFECT(EF_BREAM, IS_SPECIAL(IS_HITPOINTS, IS_COOKED_BREAM));
IS_EFFECT(EF_MOSS_LIZARD, IS_SPECIAL(IS_HITPOINTS, IS_COOKED_MOSS_LIZARD));
IS_EFFECT(EF_PAPAYA, IS_FOOD(8), IS_HEAL(IS_RUN_ENERGY, 5));
IS_EFFECT(EF_CUP_OF_TEA, IS_FOOD(3), IS_BOOSTP(IS_ATTACK, 2, 2));
IS_EFFECT(EF_NETTLE_TEA, IS_FOOD(3), IS_SPECIAL(IS_RUN_ENERGY, IS_NETTLE_TEA));
IS_EFFECT(EF_SNAIL1, IS_RANGE_FOOD(5, 7));
IS_EFFECT(EF_SNAIL2, IS_RANGE_FOOD(5, 8));
IS_EFFECT(EF_SNAIL3, IS_RANGE_FOOD(7, 9));
IS_EFFECT(EF_SPIDER_STICK, IS_RANGE_FOOD(7, 10));
IS_EFFECT(EF_SLIMY_EEL, IS_RANGE_FOOD(6, 10));
IS_EFFECT(EF_CAVE_EEL, IS_RANGE_FOOD(8, 12));

/* -- pies, which heal twice and boost once -- */

IS_EFFECT(EF_GARDEN_PIE, IS_HEAL(IS_HITPOINTS, 6), IS_BOOST(IS_FARMING, 3));
IS_EFFECT(EF_FISH_PIE, IS_HEAL(IS_HITPOINTS, 6), IS_BOOST(IS_FISHING, 3));
IS_EFFECT(EF_BOTANICAL_PIE, IS_HEAL(IS_HITPOINTS, 7), IS_BOOST(IS_HERBLORE, 4));
IS_EFFECT(EF_MUSHROOM_PIE, IS_HEAL(IS_HITPOINTS, 8), IS_BOOST(IS_CRAFTING, 4));
IS_EFFECT(EF_ADMIRAL_PIE, IS_HEAL(IS_HITPOINTS, 8), IS_BOOST(IS_FISHING, 5));
IS_EFFECT(
    EF_WILD_PIE,
    IS_HEAL(IS_HITPOINTS, 11),
    IS_BOOST(IS_SLAYER, 5),
    IS_BOOST(IS_RANGED, 4));
IS_EFFECT(
    EF_SUMMER_PIE,
    IS_HEAL(IS_HITPOINTS, 11),
    IS_BOOST(IS_AGILITY, 5),
    IS_HEAL(IS_RUN_ENERGY, 10));
IS_EFFECT(EF_DRAGONFRUIT_PIE, IS_HEAL(IS_HITPOINTS, 10), IS_BOOST(IS_FLETCHING, 4));

/* -- alcohol: a little food, a boost somewhere, and a drain everywhere else.
 *    Numbers from the wiki's Temporary skill drain table, as the reference
 *    says. -- */

IS_EFFECT(EF_JUG_OF_WINE, IS_FOOD(11), IS_HEAL(IS_ATTACK, -2));
IS_EFFECT(EF_HALF_WINE, IS_FOOD(7), IS_HEAL(IS_ATTACK, -2));
IS_EFFECT(EF_BAD_WINE, IS_HEAL(IS_ATTACK, -3));
IS_EFFECT(
    EF_SPIRIT_DRINK,
    IS_FOOD(5),
    IS_BOOSTP(IS_STRENGTH, 5, 1),
    IS_DRAIN(IS_ATTACK, 2, -3));
IS_EFFECT(
    EF_BLURBERRY_SPECIAL,
    IS_FOOD(7),
    IS_BOOSTP(IS_STRENGTH, 5, 2),
    IS_DRAIN(IS_ATTACK, 2, -3));
IS_EFFECT(
    EF_DRUNK_DRAGON,
    IS_FOOD(5),
    IS_BOOSTP(IS_STRENGTH, 5, 2),
    IS_DRAIN(IS_ATTACK, 2, -3));
IS_EFFECT(
    EF_WIZARD_BLIZZARD,
    IS_FOOD(5),
    IS_BOOSTP(IS_STRENGTH, 6, 1),
    IS_DRAIN(IS_ATTACK, 2, -3));
IS_EFFECT(EF_GROG, IS_FOOD(3), IS_BOOSTP(IS_STRENGTH, 4, 1), IS_DRAIN(IS_ATTACK, 5, -3));
IS_EFFECT(EF_BEER, IS_FOOD(1), IS_BOOSTP(IS_STRENGTH, 2, 1), IS_DRAIN(IS_ATTACK, 6, -1));
IS_EFFECT(EF_KEG_OF_BEER, IS_FOOD(15), IS_BOOSTP(IS_STRENGTH, 10, 2), IS_DRAIN(IS_ATTACK, 50, -4));
IS_EFFECT(EF_ASGARNIAN_ALE, IS_FOOD(1), IS_BOOST(IS_STRENGTH, 2), IS_DRAIN(IS_ATTACK, 5, -2));
IS_EFFECT(
    EF_MATURE_ASGARNIAN_ALE,
    IS_FOOD(1),
    IS_BOOST(IS_STRENGTH, 3),
    IS_DRAIN(IS_ATTACK, 5, -3));
IS_EFFECT(
    EF_AXEMANS_FOLLY,
    IS_FOOD(1),
    IS_BOOST(IS_WOODCUTTING, 1),
    IS_DRAIN(IS_ATTACK, 2, -2),
    IS_DRAIN(IS_STRENGTH, 2, -2));
IS_EFFECT(
    EF_MATURE_AXEMANS_FOLLY,
    IS_FOOD(2),
    IS_BOOST(IS_WOODCUTTING, 2),
    IS_DRAIN(IS_ATTACK, 2, -3),
    IS_DRAIN(IS_STRENGTH, 2, -3));
IS_EFFECT(
    EF_BANDIT_BREW,
    IS_FOOD(1),
    IS_BOOST(IS_THIEVING, 1),
    IS_BOOST(IS_ATTACK, 1),
    IS_DRAIN(IS_DEFENCE, 6, -3),
    IS_DRAIN(IS_STRENGTH, 6, -3));
IS_EFFECT(
    EF_CHEFS_DELIGHT,
    IS_FOOD(1),
    IS_BOOSTP(IS_COOKING, 5, 1),
    IS_DRAIN(IS_ATTACK, 5, -2),
    IS_DRAIN(IS_STRENGTH, 5, -2));
IS_EFFECT(
    EF_MATURE_CHEFS_DELIGHT,
    IS_FOOD(2),
    IS_BOOSTP(IS_COOKING, 5, 2),
    IS_DRAIN(IS_ATTACK, 5, -3),
    IS_DRAIN(IS_STRENGTH, 5, -3));
IS_EFFECT(
    EF_CIDER,
    IS_FOOD(1),
    IS_BOOST(IS_FARMING, 1),
    IS_DRAIN(IS_ATTACK, 2, -2),
    IS_DRAIN(IS_STRENGTH, 2, -2));
IS_EFFECT(
    EF_MATURE_CIDER,
    IS_FOOD(2),
    IS_BOOST(IS_FARMING, 2),
    IS_DRAIN(IS_ATTACK, 2, -3),
    IS_DRAIN(IS_STRENGTH, 2, -3));
IS_EFFECT(EF_DRAGON_BITTER, IS_FOOD(1), IS_BOOST(IS_STRENGTH, 2), IS_DRAIN(IS_ATTACK, 5, -2));
IS_EFFECT(
    EF_MATURE_DRAGON_BITTER,
    IS_FOOD(2),
    IS_BOOST(IS_STRENGTH, 3),
    IS_DRAIN(IS_ATTACK, 5, -2));
IS_EFFECT(
    EF_DWARVEN_STOUT,
    IS_FOOD(1),
    IS_BOOST(IS_MINING, 1),
    IS_BOOST(IS_SMITHING, 1),
    IS_DRAIN(IS_ATTACK, 4, -2),
    IS_DRAIN(IS_DEFENCE, 4, -2),
    IS_DRAIN(IS_STRENGTH, 4, -2));
IS_EFFECT(
    EF_MATURE_DWARVEN_STOUT,
    IS_FOOD(2),
    IS_BOOST(IS_MINING, 2),
    IS_BOOST(IS_SMITHING, 2),
    IS_DRAIN(IS_ATTACK, 4, -3),
    IS_DRAIN(IS_DEFENCE, 4, -3),
    IS_DRAIN(IS_STRENGTH, 4, -3));
IS_EFFECT(
    EF_GREENMANS_ALE,
    IS_FOOD(1),
    IS_BOOST(IS_HERBLORE, 1),
    IS_DRAIN(IS_ATTACK, 4, -2),
    IS_DRAIN(IS_DEFENCE, 4, -2),
    IS_DRAIN(IS_STRENGTH, 4, -2));
IS_EFFECT(
    EF_MATURE_GREENMANS_ALE,
    IS_FOOD(2),
    IS_BOOST(IS_HERBLORE, 2),
    IS_DRAIN(IS_ATTACK, 4, -3),
    IS_DRAIN(IS_DEFENCE, 4, -3),
    IS_DRAIN(IS_STRENGTH, 4, -3));
IS_EFFECT(
    EF_SLAYERS_RESPITE,
    IS_FOOD(1),
    IS_BOOST(IS_SLAYER, 2),
    IS_DRAIN(IS_ATTACK, 2, -2),
    IS_DRAIN(IS_DEFENCE, 2, -2),
    IS_DRAIN(IS_STRENGTH, 2, -2));
IS_EFFECT(
    EF_MATURE_SLAYERS_RESPITE,
    IS_FOOD(2),
    IS_BOOST(IS_SLAYER, 4),
    IS_DRAIN(IS_ATTACK, 2, -3),
    IS_DRAIN(IS_DEFENCE, 2, -3),
    IS_DRAIN(IS_STRENGTH, 2, -3));
IS_EFFECT(
    EF_WIZARDS_MIND_BOMB,
    IS_FOOD(1),
    IS_BOOSTP(IS_MAGIC, 2, 2),
    IS_DRAIN(IS_ATTACK, 5, -1),
    IS_DRAIN(IS_DEFENCE, 5, -1),
    IS_DRAIN(IS_STRENGTH, 5, -1));
IS_EFFECT(
    EF_MATURE_WIZARDS_MIND_BOMB,
    IS_FOOD(2),
    IS_BOOSTP(IS_MAGIC, 2, 3),
    IS_DRAIN(IS_ATTACK, 5, -2),
    IS_DRAIN(IS_DEFENCE, 5, -2),
    IS_DRAIN(IS_STRENGTH, 5, -2));

/* -- sq'irk juice -- */

IS_EFFECT(EF_SQUIRK_WINTER, IS_HEAL(IS_RUN_ENERGY, 5));
IS_EFFECT(EF_SQUIRK_SPRING, IS_HEAL(IS_RUN_ENERGY, 10), IS_BOOST(IS_THIEVING, 1));
IS_EFFECT(EF_SQUIRK_AUTUMN, IS_HEAL(IS_RUN_ENERGY, 15), IS_BOOST(IS_THIEVING, 2));
IS_EFFECT(EF_SQUIRK_SUMMER, IS_HEAL(IS_RUN_ENERGY, 20), IS_BOOST(IS_THIEVING, 3));

/* -- combat potions -- */

IS_EFFECT(EF_ATTACK_POT, IS_BOOSTP(IS_ATTACK, 10, 3));
IS_EFFECT(EF_STRENGTH_POT, IS_BOOSTP(IS_STRENGTH, 10, 3));
IS_EFFECT(EF_DEFENCE_POT, IS_BOOSTP(IS_DEFENCE, 10, 3));
IS_EFFECT(EF_COMBAT_POT, IS_BOOSTP(IS_ATTACK, 10, 3), IS_BOOSTP(IS_STRENGTH, 10, 3));
IS_EFFECT(EF_SUPER_ATTACK, IS_BOOSTP(IS_ATTACK, 15, 5));
IS_EFFECT(EF_SUPER_STRENGTH, IS_BOOSTP(IS_STRENGTH, 15, 5));
IS_EFFECT(EF_SUPER_DEFENCE, IS_BOOSTP(IS_DEFENCE, 15, 5));
IS_EFFECT(
    EF_SUPER_COMBAT,
    IS_BOOSTP(IS_ATTACK, 15, 5),
    IS_BOOSTP(IS_STRENGTH, 15, 5),
    IS_BOOSTP(IS_DEFENCE, 15, 5));
IS_EFFECT(EF_MAGIC_POT, IS_BOOST(IS_MAGIC, 4));
IS_EFFECT(EF_MAGIC_ESSENCE, IS_BOOST(IS_MAGIC, 3));
IS_EFFECT(EF_RANGING_POT, IS_BOOSTP(IS_RANGED, 10, 4));
IS_EFFECT(EF_SUPER_RANGING, IS_BOOSTP(IS_RANGED, 15, 5));
IS_EFFECT(EF_SUPER_MAGIC, IS_BOOSTP(IS_MAGIC, 15, 5));
IS_EFFECT(EF_IMBUED_HEART, IS_BOOSTP(IS_MAGIC, 10, 1));
IS_EFFECT(EF_SATURATED_HEART, IS_BOOSTP(IS_MAGIC, 10, 4));
IS_EFFECT(EF_BASTION, IS_BOOSTP(IS_RANGED, 10, 4), IS_BOOSTP(IS_DEFENCE, 15, 5));
IS_EFFECT(EF_BATTLEMAGE, IS_BOOST(IS_MAGIC, 4), IS_BOOSTP(IS_DEFENCE, 15, 5));
IS_EFFECT(
    EF_ZAMORAK_BREW,
    IS_BOOSTP(IS_ATTACK, 20, 2),
    IS_BOOSTP(IS_STRENGTH, 12, 2),
    IS_HEALP(IS_PRAYER, 10, 0),
    IS_DRAIN(IS_DEFENCE, 10, -2),
    IS_DRAIN(IS_HITPOINTS, -12, 0));

/* Divine potions: the boost, plus the ten hitpoints the divine version costs. */
IS_EFFECT(EF_DIVINE_ATTACK, IS_BOOSTP(IS_ATTACK, 15, 5), IS_HEAL(IS_HITPOINTS, -10));
IS_EFFECT(EF_DIVINE_STRENGTH, IS_BOOSTP(IS_STRENGTH, 15, 5), IS_HEAL(IS_HITPOINTS, -10));
IS_EFFECT(EF_DIVINE_DEFENCE, IS_BOOSTP(IS_DEFENCE, 15, 5), IS_HEAL(IS_HITPOINTS, -10));
IS_EFFECT(EF_DIVINE_MAGIC, IS_BOOST(IS_MAGIC, 4), IS_HEAL(IS_HITPOINTS, -10));
IS_EFFECT(EF_DIVINE_RANGING, IS_BOOSTP(IS_RANGED, 10, 4), IS_HEAL(IS_HITPOINTS, -10));
IS_EFFECT(
    EF_DIVINE_COMBAT,
    IS_BOOSTP(IS_ATTACK, 15, 5),
    IS_BOOSTP(IS_STRENGTH, 15, 5),
    IS_BOOSTP(IS_DEFENCE, 15, 5),
    IS_HEAL(IS_HITPOINTS, -10));
IS_EFFECT(
    EF_DIVINE_BASTION,
    IS_BOOSTP(IS_RANGED, 10, 4),
    IS_BOOSTP(IS_DEFENCE, 15, 5),
    IS_HEAL(IS_HITPOINTS, -10));
IS_EFFECT(
    EF_DIVINE_BATTLEMAGE,
    IS_BOOST(IS_MAGIC, 4),
    IS_BOOSTP(IS_DEFENCE, 15, 5),
    IS_HEAL(IS_HITPOINTS, -10));

/* Overloads. The NMZ one is the five supers and fifty hitpoints; the raid
 * ones scale with the vial's own strength. */
IS_EFFECT(
    EF_OVERLOAD_NMZ,
    IS_BOOSTP(IS_ATTACK, 15, 5),
    IS_BOOSTP(IS_STRENGTH, 15, 5),
    IS_BOOSTP(IS_DEFENCE, 15, 5),
    IS_BOOSTP(IS_RANGED, 15, 5),
    IS_BOOSTP(IS_MAGIC, 15, 5),
    IS_HEAL(IS_HITPOINTS, -50));
IS_EFFECT(
    EF_OVERLOAD_COX_PLUS,
    IS_BOOSTP(IS_ATTACK, 16, 6),
    IS_BOOSTP(IS_STRENGTH, 16, 6),
    IS_BOOSTP(IS_DEFENCE, 16, 6),
    IS_BOOSTP(IS_RANGED, 16, 6),
    IS_BOOSTP(IS_MAGIC, 16, 6),
    IS_HEAL(IS_HITPOINTS, -50));
IS_EFFECT(
    EF_OVERLOAD_COX_MINUS,
    IS_BOOSTP(IS_ATTACK, 10, 4),
    IS_BOOSTP(IS_STRENGTH, 10, 4),
    IS_BOOSTP(IS_DEFENCE, 10, 4),
    IS_BOOSTP(IS_RANGED, 10, 4),
    IS_BOOSTP(IS_MAGIC, 10, 4),
    IS_HEAL(IS_HITPOINTS, -50));
IS_EFFECT(
    EF_ELDER_POTION,
    IS_BOOSTP(IS_ATTACK, 13, 5),
    IS_BOOSTP(IS_STRENGTH, 13, 5),
    IS_BOOSTP(IS_DEFENCE, 13, 5));
IS_EFFECT(EF_TWISTED_POTION, IS_BOOSTP(IS_RANGED, 13, 5), IS_BOOSTP(IS_DEFENCE, 13, 5));
IS_EFFECT(EF_KODAI_POTION, IS_BOOSTP(IS_MAGIC, 13, 5), IS_BOOSTP(IS_DEFENCE, 13, 5));

/* -- restores -- */

IS_EFFECT(
    EF_RESTORE_POT,
    IS_HEALP(IS_ATTACK, 30, 10),
    IS_HEALP(IS_STRENGTH, 30, 10),
    IS_HEALP(IS_DEFENCE, 30, 10),
    IS_HEALP(IS_RANGED, 30, 10),
    IS_HEALP(IS_MAGIC, 30, 10));
IS_EFFECT(EF_ENERGY_POT, IS_HEAL(IS_RUN_ENERGY, 15));
IS_EFFECT(EF_SUPER_ENERGY_POT, IS_HEAL(IS_RUN_ENERGY, 20));
IS_EFFECT(EF_STAMINA_POT, IS_OP_MAKE(IS_RUN_ENERGY, IS_STAMINA, 0, 0, 20));
/* PrayerPotion(7): a quarter of the base prayer level plus seven, and two
 * percentage points more with a holy wrench to hand. */
IS_EFFECT(EF_PRAYER_POT, IS_OP_MAKE(IS_PRAYER, IS_PRAYER_POTION, 0, 25, 7));
IS_EFFECT(EF_GUTHIX_REST, IS_BOOST(IS_HITPOINTS, 5), IS_HEAL(IS_RUN_ENERGY, 5));

/* -- skilling potions -- */

IS_EFFECT(EF_AGILITY_POT, IS_BOOST(IS_AGILITY, 3));
IS_EFFECT(EF_FISHING_POT, IS_BOOST(IS_FISHING, 3));
IS_EFFECT(EF_HUNTER_POT, IS_BOOST(IS_HUNTER, 3));

/* -- the odd ones out -- */

IS_EFFECT(EF_ROCK_CAKE,
    IS_SPECIAL(IS_HITPOINTS, IS_ROCK_CAKE_EAT),
    IS_SPECIAL(IS_HITPOINTS, IS_ROCK_CAKE_GUZZLE));
IS_EFFECT(EF_LOCATOR_ORB, IS_SPECIAL(IS_HITPOINTS, IS_LOCATOR_ORB));
IS_EFFECT(EF_NIGHTSHADE, IS_SPECIAL(IS_HITPOINTS, IS_CAVE_NIGHTSHADE));
IS_EFFECT(
    EF_JANGERBERRIES,
    IS_BOOST(IS_ATTACK, 2),
    IS_BOOST(IS_STRENGTH, 1),
    IS_HEAL(IS_PRAYER, 1),
    IS_HEAL(IS_DEFENCE, -1));
IS_EFFECT(EF_GNOME_MINT_CAKE, IS_HEAL(IS_RUN_ENERGY, 50));
IS_EFFECT(EF_CASTLEWARS_BANDAGE, IS_HEALP(IS_HITPOINTS, 10, 0), IS_HEAL(IS_RUN_ENERGY, 30));
IS_EFFECT(EF_GAUNTLET_POTION, IS_HEALP(IS_PRAYER, 25, 7), IS_HEAL(IS_RUN_ENERGY, 40));

/* The families, whose op lists depend on the player. */
IS_EFFECT_FAMILY(EF_SUPER_RESTORE, IS_FAMILY_SUPER_RESTORE, 25, 0, 0, 8, 0);
IS_EFFECT_FAMILY(EF_SANFEW_SERUM, IS_FAMILY_SUPER_RESTORE, 30, 0, 0, 4, 0);
IS_EFFECT_FAMILY(EF_REVITALISATION, IS_FAMILY_SUPER_RESTORE, 30, 0, 0, 11, 0);
IS_EFFECT_FAMILY(EF_SARADOMIN_BREW, IS_FAMILY_SARADOMIN_BREW, 15, 20, 10, 2, 2);
IS_EFFECT_FAMILY(EF_XERICS_AID, IS_FAMILY_SARADOMIN_BREW, 15, 20, 10, 5, 4);
IS_EFFECT_FAMILY(EF_ANCIENT_BREW, IS_FAMILY_ANCIENT_BREW, 5, 0, 0, 2, 0);
IS_EFFECT_FAMILY(EF_FORGOTTEN_BREW, IS_FAMILY_ANCIENT_BREW, 8, 0, 0, 3, 0);
IS_EFFECT_FAMILY(EF_MOONLIGHT_POTION, IS_FAMILY_MOONLIGHT, 0, 0, 0, 0, 0);
IS_EFFECT_FAMILY(EF_AMBROSIA, IS_FAMILY_AMBROSIA, 0, 0, 0, 0, 0);

/* Mixes: the vial of water's own heal on top of the potion. */
IS_EFFECT_MIXED(EF_MIX_ATTACK, 3, EF_ATTACK_POT);
IS_EFFECT_MIXED(EF_MIX_STRENGTH, 3, EF_STRENGTH_POT);
IS_EFFECT_MIXED(EF_MIX_COMBAT, 3, EF_COMBAT_POT);
IS_EFFECT_MIXED(EF_MIX_DEFENCE, 6, EF_DEFENCE_POT);
IS_EFFECT_MIXED(EF_MIX_MAGIC, 6, EF_MAGIC_POT);
IS_EFFECT_MIXED(EF_MIX_RANGING, 6, EF_RANGING_POT);
IS_EFFECT_MIXED(EF_MIX_SUPER_ATTACK, 6, EF_SUPER_ATTACK);
IS_EFFECT_MIXED(EF_MIX_SUPER_STRENGTH, 6, EF_SUPER_STRENGTH);
IS_EFFECT_MIXED(EF_MIX_SUPER_DEFENCE, 6, EF_SUPER_DEFENCE);
IS_EFFECT_MIXED(EF_MIX_MAGIC_ESSENCE, 6, EF_MAGIC_ESSENCE);
IS_EFFECT_MIXED(EF_MIX_ZAMORAK, 6, EF_ZAMORAK_BREW);
IS_EFFECT_MIXED(EF_MIX_ANCIENT, 6, EF_ANCIENT_BREW);
IS_EFFECT_MIXED(EF_MIX_RESTORE, 3, EF_RESTORE_POT);
IS_EFFECT_MIXED(EF_MIX_ENERGY, 3, EF_ENERGY_POT);
IS_EFFECT_MIXED(EF_MIX_PRAYER, 6, EF_PRAYER_POT);
IS_EFFECT_MIXED(EF_MIX_SUPER_ENERGY, 6, EF_SUPER_ENERGY_POT);
IS_EFFECT_MIXED(EF_MIX_SUPER_RESTORE, 6, EF_SUPER_RESTORE);
IS_EFFECT_MIXED(EF_MIX_STAMINA, 6, EF_STAMINA_POT);
IS_EFFECT_MIXED(EF_MIX_AGILITY, 6, EF_AGILITY_POT);
IS_EFFECT_MIXED(EF_MIX_FISHING, 6, EF_FISHING_POT);
IS_EFFECT_MIXED(EF_MIX_HUNTING, 6, EF_HUNTER_POT);

/* ------------------------------------------------------------- the table */

/*
 * Names as the cache states them, lowercased and with the dose and the
 * fraction word already taken off -- so "Prayer potion(3)" arrives here as
 * "prayer potion", and "Half a redberry pie" as "redberry pie".
 *
 * The order does not matter (the lookup is a walk over a table this size, once
 * per hover), but the grouping does: it is the reference's grouping, so a row
 * added there can be found here.
 */
static struct is_item const IS_ITEMS[] = {
    /* food(-5) .. food(22) */
    { "poison karambwan", &EF_FOOD_BAD_KARAMBWAN },
    { "potato", &EF_FOOD1 },
    { "onion", &EF_FOOD1 },
    { "cabbage", &EF_FOOD1 },
    { "pot of cream", &EF_FOOD1 },
    { "anchovies", &EF_FOOD1 },
    { "equa leaves", &EF_FOOD1 },
    { "tomato", &EF_FOOD2 },
    { "banana", &EF_FOOD2 },
    { "sliced banana", &EF_FOOD2 },
    { "orange", &EF_FOOD2 },
    { "orange slices", &EF_FOOD2 },
    { "orange chunks", &EF_FOOD2 },
    { "pineapple ring", &EF_FOOD2 },
    { "pineapple chunks", &EF_FOOD2 },
    { "cheese", &EF_FOOD2 },
    { "spinach roll", &EF_FOOD2 },
    { "lemon", &EF_FOOD2 },
    { "lemon chunks", &EF_FOOD2 },
    { "lemon slices", &EF_FOOD2 },
    { "lime", &EF_FOOD2 },
    { "lime chunks", &EF_FOOD2 },
    { "lime slices", &EF_FOOD2 },
    { "dwellberries", &EF_FOOD2 },
    { "king worm", &EF_FOOD2 },
    { "shrimps", &EF_FOOD3 },
    { "cooked meat", &EF_FOOD3 },
    { "cooked chicken", &EF_FOOD3 },
    { "roe", &EF_FOOD3 },
    { "chocolate bar", &EF_FOOD3 },
    { "ugthanki meat", &EF_FOOD3 },
    { "toad's legs", &EF_FOOD3 },
    { "sardine", &EF_FOOD4 },
    { "cake", &EF_FOOD4 },
    { "chocolatey milk", &EF_FOOD4 },
    { "baked potato", &EF_FOOD4 },
    { "edible seaweed", &EF_FOOD4 },
    { "moonlight mead", &EF_FOOD4 },
    { "bread", &EF_FOOD5 },
    { "herring", &EF_FOOD5 },
    { "chocolate cake", &EF_FOOD5 },
    { "chocolate slice", &EF_FOOD5 },
    { "cooked rabbit", &EF_FOOD5 },
    { "redberry pie", &EF_FOOD5 },
    { "caviar", &EF_FOOD5 },
    { "scrambled egg", &EF_FOOD5 },
    { "fried mushrooms", &EF_FOOD5 },
    { "fried onions", &EF_FOOD5 },
    { "chilli con carne", &EF_FOOD5 },
    { "mackerel", &EF_FOOD6 },
    { "meat pie", &EF_FOOD6 },
    { "roast bird meat", &EF_FOOD6 },
    { "square sandwich", &EF_FOOD6 },
    { "triangle sandwich", &EF_FOOD6 },
    { "roll", &EF_FOOD6 },
    { "baguette", &EF_FOOD6 },
    { "giant carp", &EF_FOOD6 },
    { "mature moonlight mead", &EF_FOOD6 },
    { "giant frog legs", &EF_FOOD6 },
    { "choc-ice", &EF_FOOD7 },
    { "trout", &EF_FOOD7 },
    { "cod", &EF_FOOD7 },
    { "plain pizza", &EF_FOOD7 },
    { "apple pie", &EF_FOOD7 },
    { "roast rabbit", &EF_FOOD7 },
    { "chocchip crunchies", &EF_FOOD7 },
    { "spicy crunchies", &EF_FOOD7 },
    { "pike", &EF_FOOD8 },
    { "roast beast meat", &EF_FOOD8 },
    { "meat pizza", &EF_FOOD8 },
    { "worm crunchies", &EF_FOOD8 },
    { "toad crunchies", &EF_FOOD8 },
    { "egg and tomato", &EF_FOOD8 },
    { "peach", &EF_FOOD8 },
    { "pineapple punch", &EF_FOOD9 },
    { "fruit blast", &EF_FOOD9 },
    { "salmon", &EF_FOOD9 },
    { "anchovy pizza", &EF_FOOD9 },
    { "tuna", &EF_FOOD10 },
    { "cooked chompy", &EF_FOOD10 },
    { "field ration", &EF_FOOD10 },
    { "dragonfruit", &EF_FOOD10 },
    { "stew", &EF_FOOD11 },
    { "pineapple pizza", &EF_FOOD11 },
    { "cooked fishcake", &EF_FOOD11 },
    { "vegetable batta", &EF_FOOD11 },
    { "worm batta", &EF_FOOD11 },
    { "toad batta", &EF_FOOD11 },
    { "cheese+tom batta", &EF_FOOD11 },
    { "fruit batta", &EF_FOOD11 },
    { "mushroom & onion", &EF_FOOD11 },
    { "lava eel", &EF_FOOD11 },
    { "lobster", &EF_FOOD12 },
    { "worm hole", &EF_FOOD12 },
    { "veg ball", &EF_FOOD12 },
    { "bass", &EF_FOOD13 },
    { "tuna and corn", &EF_FOOD13 },
    { "potato with butter", &EF_FOOD14 },
    { "chilli potato", &EF_FOOD14 },
    { "swordfish", &EF_FOOD14 },
    { "pumpkin", &EF_FOOD14 },
    { "easter egg", &EF_FOOD14 },
    { "cooked oomlie wrap", &EF_FOOD14 },
    { "tangled toad's legs", &EF_FOOD15 },
    { "chocolate bomb", &EF_FOOD15 },
    { "cooked jubbly", &EF_FOOD15 },
    { "monkfish", &EF_FOOD16 },
    { "potato with cheese", &EF_FOOD16 },
    { "egg potato", &EF_FOOD16 },
    { "cooked karambwan", &EF_FOOD18 },
    { "curry", &EF_FOOD19 },
    { "ugthanki kebab", &EF_FOOD19 },
    { "mushroom potato", &EF_FOOD20 },
    { "shark", &EF_FOOD20 },
    { "sea turtle", &EF_FOOD21 },
    { "manta ray", &EF_FOOD22 },
    { "dark crab", &EF_FOOD22 },
    { "tuna potato", &EF_FOOD22 },
    { "anglerfish", &EF_ANGLERFISH },
    { "strawberry", &EF_STRAWBERRY },
    { "watermelon slice", &EF_WATERMELON },
    { "sweetcorn", &EF_SWEETCORN },
    { "cooked bream", &EF_BREAM },
    { "cooked moss lizard", &EF_MOSS_LIZARD },
    { "papaya fruit", &EF_PAPAYA },
    { "cup of tea", &EF_CUP_OF_TEA },
    { "nettle tea", &EF_NETTLE_TEA },
    { "thin snail meat", &EF_SNAIL1 },
    { "lean snail meat", &EF_SNAIL2 },
    { "fat snail meat", &EF_SNAIL3 },
    { "spider on stick", &EF_SPIDER_STICK },
    { "spider on shaft", &EF_SPIDER_STICK },
    { "cooked slimy eel", &EF_SLIMY_EEL },
    { "cave eel", &EF_CAVE_EEL },

    /* pies */
    { "garden pie", &EF_GARDEN_PIE },
    { "fish pie", &EF_FISH_PIE },
    { "botanical pie", &EF_BOTANICAL_PIE },
    { "mushroom pie", &EF_MUSHROOM_PIE },
    { "admiral pie", &EF_ADMIRAL_PIE },
    { "wild pie", &EF_WILD_PIE },
    { "summer pie", &EF_SUMMER_PIE },
    { "dragonfruit pie", &EF_DRAGONFRUIT_PIE },

    /* alcohol */
    { "jug of wine", &EF_JUG_OF_WINE },
    { "bottle of wine", &EF_JUG_OF_WINE },
    { "half full wine jug", &EF_HALF_WINE },
    { "jug of bad wine", &EF_BAD_WINE },
    { "sgg", &EF_SPIRIT_DRINK },
    { "brandy", &EF_SPIRIT_DRINK },
    { "gin", &EF_SPIRIT_DRINK },
    { "vodka", &EF_SPIRIT_DRINK },
    { "whisky", &EF_SPIRIT_DRINK },
    { "blurberry special", &EF_BLURBERRY_SPECIAL },
    { "drunk dragon", &EF_DRUNK_DRAGON },
    { "choc saturday", &EF_DRUNK_DRAGON },
    { "wizard blizzard", &EF_WIZARD_BLIZZARD },
    { "grog", &EF_GROG },
    { "beer", &EF_BEER },
    { "keg of beer", &EF_KEG_OF_BEER },
    { "asgarnian ale", &EF_ASGARNIAN_ALE },
    { "mature asgarnian ale", &EF_MATURE_ASGARNIAN_ALE },
    { "axeman's folly", &EF_AXEMANS_FOLLY },
    { "mature axeman's folly", &EF_MATURE_AXEMANS_FOLLY },
    { "bandit's brew", &EF_BANDIT_BREW },
    { "chef's delight", &EF_CHEFS_DELIGHT },
    { "mature chef's delight", &EF_MATURE_CHEFS_DELIGHT },
    { "cider", &EF_CIDER },
    { "mature cider", &EF_MATURE_CIDER },
    { "dragon bitter", &EF_DRAGON_BITTER },
    { "mature dragon bitter", &EF_MATURE_DRAGON_BITTER },
    { "dwarven stout", &EF_DWARVEN_STOUT },
    { "mature dwarven stout", &EF_MATURE_DWARVEN_STOUT },
    { "greenman's ale", &EF_GREENMANS_ALE },
    /* The apostrophe survives undosed and is dropped in the kegs. */
    { "greenmans ale", &EF_GREENMANS_ALE },
    { "mature greenman's ale", &EF_MATURE_GREENMANS_ALE },
    { "mature greenmans ale", &EF_MATURE_GREENMANS_ALE },
    { "slayer's respite", &EF_SLAYERS_RESPITE },
    { "mature slayer's respite", &EF_MATURE_SLAYERS_RESPITE },
    { "wizard's mind bomb", &EF_WIZARDS_MIND_BOMB },
    /* The dosed kegs drop the wizard: "Mind bomb(2)". */
    { "mind bomb", &EF_WIZARDS_MIND_BOMB },
    { "mature wizard's mind bomb", &EF_MATURE_WIZARDS_MIND_BOMB },
    { "mature mind bomb", &EF_MATURE_WIZARDS_MIND_BOMB },

    /* sq'irk juice */
    { "winter sq'irkjuice", &EF_SQUIRK_WINTER },
    { "spring sq'irkjuice", &EF_SQUIRK_SPRING },
    { "autumn sq'irkjuice", &EF_SQUIRK_AUTUMN },
    { "summer sq'irkjuice", &EF_SQUIRK_SUMMER },

    /* combat potions */
    { "attack potion", &EF_ATTACK_POT },
    { "strength potion", &EF_STRENGTH_POT },
    { "defence potion", &EF_DEFENCE_POT },
    { "combat potion", &EF_COMBAT_POT },
    { "super attack", &EF_SUPER_ATTACK },
    { "super strength", &EF_SUPER_STRENGTH },
    { "super defence", &EF_SUPER_DEFENCE },
    { "super combat potion", &EF_SUPER_COMBAT },
    { "magic potion", &EF_MAGIC_POT },
    { "magic essence", &EF_MAGIC_ESSENCE },
    { "ranging potion", &EF_RANGING_POT },
    { "super ranging", &EF_SUPER_RANGING },
    { "super magic potion", &EF_SUPER_MAGIC },
    { "imbued heart", &EF_IMBUED_HEART },
    { "saturated heart", &EF_SATURATED_HEART },
    { "bastion potion", &EF_BASTION },
    { "battlemage potion", &EF_BATTLEMAGE },
    { "zamorak brew", &EF_ZAMORAK_BREW },
    { "saradomin brew", &EF_SARADOMIN_BREW },
    { "xeric's aid", &EF_XERICS_AID },
    { "ancient brew", &EF_ANCIENT_BREW },
    { "forgotten brew", &EF_FORGOTTEN_BREW },
    { "moonlight potion", &EF_MOONLIGHT_POTION },
    { "divine super attack potion", &EF_DIVINE_ATTACK },
    { "divine super strength potion", &EF_DIVINE_STRENGTH },
    { "divine super defence potion", &EF_DIVINE_DEFENCE },
    { "divine magic potion", &EF_DIVINE_MAGIC },
    { "divine ranging potion", &EF_DIVINE_RANGING },
    { "divine super combat potion", &EF_DIVINE_COMBAT },
    { "divine bastion potion", &EF_DIVINE_BASTION },
    { "divine battlemage potion", &EF_DIVINE_BATTLEMAGE },
    /*
     * "Overload" is the ONE collision this table's key cannot resolve: the
     * Nightmare Zone potion (11730..11733) and the Chambers of Xeric one
     * (20989..20992) are both called "Overload (4)" on cache.osrs230, and only
     * an id tells them apart. The NMZ numbers win because that is the potion
     * drunk outside a raid, where a tooltip is read at leisure; inside the
     * Chambers the two differ by two percentage points. The (+) and (-) tiers
     * carry their tier in their own names and are exact.
     */
    { "overload", &EF_OVERLOAD_NMZ },
    { "overload (+)", &EF_OVERLOAD_COX_PLUS },
    { "overload (-)", &EF_OVERLOAD_COX_MINUS },
    { "elder potion", &EF_ELDER_POTION },
    { "elder (+)", &EF_ELDER_POTION },
    { "elder (-)", &EF_ELDER_POTION },
    { "twisted potion", &EF_TWISTED_POTION },
    { "twisted (+)", &EF_TWISTED_POTION },
    { "twisted (-)", &EF_TWISTED_POTION },
    { "kodai potion", &EF_KODAI_POTION },
    { "kodai (+)", &EF_KODAI_POTION },
    { "kodai (-)", &EF_KODAI_POTION },
    { "revitalisation potion", &EF_REVITALISATION },
    { "revitalisation (+)", &EF_REVITALISATION },
    { "xeric's aid (+)", &EF_XERICS_AID },

    /* restores */
    { "restore potion", &EF_RESTORE_POT },
    { "super restore", &EF_SUPER_RESTORE },
    { "sanfew serum", &EF_SANFEW_SERUM },
    { "prayer potion", &EF_PRAYER_POT },
    { "energy potion", &EF_ENERGY_POT },
    { "super energy", &EF_SUPER_ENERGY_POT },
    { "stamina potion", &EF_STAMINA_POT },
    { "guthix rest", &EF_GUTHIX_REST },
    { "ambrosia", &EF_AMBROSIA },

    /* skilling potions */
    { "agility potion", &EF_AGILITY_POT },
    { "fishing potion", &EF_FISHING_POT },
    { "hunter potion", &EF_HUNTER_POT },

    /* the odd ones out */
    { "dwarven rock cake", &EF_ROCK_CAKE },
    { "locator orb", &EF_LOCATOR_ORB },
    { "cave nightshade", &EF_NIGHTSHADE },
    { "jangerberries", &EF_JANGERBERRIES },
    { "mint cake", &EF_GNOME_MINT_CAKE },
    { "bandages", &EF_CASTLEWARS_BANDAGE },
    { "egniol potion", &EF_GAUNTLET_POTION },

    /* mixes */
    { "attack mix", &EF_MIX_ATTACK },
    { "strength mix", &EF_MIX_STRENGTH },
    { "combat mix", &EF_MIX_COMBAT },
    { "defence mix", &EF_MIX_DEFENCE },
    { "magic mix", &EF_MIX_MAGIC },
    { "ranging mix", &EF_MIX_RANGING },
    { "superattack mix", &EF_MIX_SUPER_ATTACK },
    { "super str. mix", &EF_MIX_SUPER_STRENGTH },
    { "super def. mix", &EF_MIX_SUPER_DEFENCE },
    { "magic essence mix", &EF_MIX_MAGIC_ESSENCE },
    { "zamorak mix", &EF_MIX_ZAMORAK },
    { "ancient mix", &EF_MIX_ANCIENT },
    { "restore mix", &EF_MIX_RESTORE },
    { "energy mix", &EF_MIX_ENERGY },
    { "prayer mix", &EF_MIX_PRAYER },
    { "super energy mix", &EF_MIX_SUPER_ENERGY },
    { "super restore mix", &EF_MIX_SUPER_RESTORE },
    { "stamina mix", &EF_MIX_STAMINA },
    { "agility mix", &EF_MIX_AGILITY },
    { "fishing mix", &EF_MIX_FISHING },
    { "hunting mix", &EF_MIX_HUNTING },
};

/*
 * Normalize a cache name into a table key.
 *
 * Three things come off, and each of them is what lets one row stand for a
 * shelf of items:
 *
 *   - The dose or portion suffix: "Prayer potion(3)" -> "prayer potion".
 *   - The fraction word: "Half a redberry pie", "2/3 cake", "Slice of cake"
 *     all name the same behaviour as the whole, because a bite of a pie heals
 *     what the pie's bite heals.
 *   - Case, and the markup the minimenu builder leaves in a name.
 *
 * A dose suffix is stripped only when it is DIGITS: "Overload (+)" keeps its
 * suffix, because that is a different potion and not a different dose.
 */
static void
is_normalize_name(char const* in, char* out, int out_size)
{
    static char const* const k_prefix[] = {
        "half a ", "part ", "slice of ", "premade ", "2/3 ", "1/3 ", "1/2 ", "3/4 ", "1/4 "
    };
    int at = 0;
    char const* src = in;

    assert(in);
    assert(out);
    assert(out_size > 1);

    /* Colour tags, which a minimenu name carries and a cache name does not. */
    while( *src )
    {
        if( src[0] == '@' && src[1] && src[2] && src[3] && src[4] == '@' )
        {
            src += 5;
            continue;
        }
        if( src[0] == '<' )
        {
            char const* close = strchr(src, '>');
            if( close )
            {
                src = close + 1;
                continue;
            }
        }
        if( at < out_size - 1 )
        {
            char ch = *src;
            if( ch >= 'A' && ch <= 'Z' )
                ch = (char)(ch - 'A' + 'a');
            out[at++] = ch;
        }
        src++;
    }
    out[at] = '\0';

    /* The dose suffix, and only when it is a number. */
    if( at > 3 && out[at - 1] == ')' )
    {
        int open = at - 2;
        int digits = 0;
        while( open > 0 && out[open] >= '0' && out[open] <= '9' )
        {
            open--;
            digits++;
        }
        if( digits > 0 && out[open] == '(' )
        {
            while( open > 0 && out[open - 1] == ' ' )
                open--;
            out[open] = '\0';
            at = open;
        }
    }

    /*
     * The MATURE suffix, which is the same idea wearing a different hat: an
     * aged ale is "Asgarnian ale(m)" undosed and "(m1)".."(m4)" in a keg, and
     * it is a different drink from the young one -- twice the boost and a
     * harsher drain. Rewritten to the prefix the table names it by rather than
     * given nine rows of its own.
     */
    if( at > 3 && out[at - 1] == ')' )
    {
        int open = at - 2;
        while( open > 0 && out[open] >= '0' && out[open] <= '9' )
            open--;
        if( open > 0 && out[open] == 'm' && out[open - 1] == '(' )
        {
            int body = open - 1;
            while( body > 0 && out[body - 1] == ' ' )
                body--;
            out[body] = '\0';
            if( body + 7 < out_size )
            {
                memmove(out + 7, out, (size_t)body + 1);
                memcpy(out, "mature ", 7);
                at = body + 7;
            }
        }
    }

    for( int i = 0; i < (int)(sizeof(k_prefix) / sizeof(k_prefix[0])); i++ )
    {
        size_t len = strlen(k_prefix[i]);
        if( (size_t)at > len && strncmp(out, k_prefix[i], len) == 0 )
        {
            memmove(out, out + len, (size_t)at - len + 1);
            break;
        }
    }
}

static struct is_effect const*
is_lookup(char const* cache_name)
{
    char key[64];

    assert(cache_name);

    is_normalize_name(cache_name, key, sizeof(key));
    if( key[0] == '\0' )
        return NULL;
    for( int i = 0; i < (int)(sizeof(IS_ITEMS) / sizeof(IS_ITEMS[0])); i++ )
        if( strcmp(IS_ITEMS[i].name, key) == 0 )
            return IS_ITEMS[i].effect;
    return NULL;
}

/* ---------------------------------------------------- families -> op list */

/*
 * A family's ops, built against the player as they are right now.
 *
 * Every family exists because its op list is not knowable in advance: a super
 * restore's is "every skill that is currently down", a saradomin brew's drains
 * only the stats above 1, and a moonlight potion's depends on the drinker's
 * Herblore level. Building them here keeps ONE evaluator -- what comes out is
 * an ordinary op array, indistinguishable from a table row's.
 */
static int
is_effect_ops(
    struct ToriRS_PluginCtx* ctx,
    struct is_player const* player,
    struct is_effect const* effect,
    struct is_op* out,
    int out_max)
{
    int at = 0;

    assert(ctx);
    assert(player);
    assert(effect);
    assert(out);

    switch( effect->family )
    {
    case IS_FAMILY_NONE:
        for( int i = 0; i < effect->op_count && at < out_max; i++ )
            out[at++] = effect->ops[i];
        return at;

    case IS_FAMILY_SUPER_RESTORE:
    {
        /* Prayer first (through the prayer-potion calculator, wrench and all),
         * then every skill that is actually drained -- a restore shows nothing
         * for a stat that is already at its level, which is most of them. */
        static int const k_restored[] = {
            IS_ATTACK,  IS_DEFENCE,     IS_STRENGTH, IS_RANGED,   IS_MAGIC,
            IS_COOKING, IS_WOODCUTTING, IS_FLETCHING, IS_FISHING, IS_FIREMAKING,
            IS_CRAFTING, IS_SMITHING,   IS_MINING,   IS_HERBLORE, IS_AGILITY,
            IS_THIEVING, IS_SLAYER,     IS_FARMING,  IS_RUNECRAFT, IS_HUNTER,
            IS_CONSTRUCTION
        };
        struct is_op prayer = IS_OP_MAKE(IS_PRAYER, IS_PRAYER_POTION, 0, 0, 0);
        prayer.num = effect->perc_a;
        prayer.delta = effect->delta_a;
        if( at < out_max )
            out[at++] = prayer;
        for( int i = 0; i < (int)(sizeof(k_restored) / sizeof(k_restored[0])) && at < out_max;
             i++ )
        {
            struct is_op op = IS_OP_MAKE(0, IS_PERC, 0, 0, 0);
            if( is_stat_value(player, k_restored[i]) >= is_stat_maximum(player, k_restored[i]) )
                continue;
            op.stat = (short)k_restored[i];
            op.num = effect->perc_a;
            op.delta = effect->delta_a;
            out[at++] = op;
        }
        return at;
    }

    case IS_FAMILY_SARADOMIN_BREW:
    {
        static int const k_drained[] = { IS_ATTACK, IS_STRENGTH, IS_RANGED, IS_MAGIC };
        struct is_op hp = IS_OP_MAKE(IS_HITPOINTS, IS_PERC, 1, 0, 0);
        struct is_op def = IS_OP_MAKE(IS_DEFENCE, IS_PERC, 1, 0, 0);

        hp.num = effect->perc_a;
        hp.delta = effect->delta_a;
        def.num = effect->perc_b;
        def.delta = effect->delta_a;
        if( at < out_max )
            out[at++] = hp;
        if( at < out_max )
            out[at++] = def;
        for( int i = 0; i < (int)(sizeof(k_drained) / sizeof(k_drained[0])) && at < out_max;
             i++ )
        {
            struct is_op op = IS_OP_MAKE(0, IS_PERC_BOOSTED, 0, 0, 0);
            /* The reference's own gate: a stat at 1 has nothing left to give. */
            if( is_stat_value(player, k_drained[i]) <= 1 )
                continue;
            op.stat = (short)k_drained[i];
            op.num = effect->perc_c;
            op.delta = (short)-effect->delta_b;
            out[at++] = op;
        }
        return at;
    }

    case IS_FAMILY_ANCIENT_BREW:
    {
        static int const k_drained[] = { IS_ATTACK, IS_STRENGTH, IS_DEFENCE };
        struct is_op prayer = IS_CAPPED(IS_PRAYER, 10, 2, 5, 0);
        struct is_op magic = IS_OP_MAKE(IS_MAGIC, IS_PERC, 1, 0, 0);

        magic.num = effect->perc_a;
        magic.delta = effect->delta_a;
        if( at < out_max )
            out[at++] = prayer;
        if( at < out_max )
            out[at++] = magic;
        for( int i = 0; i < (int)(sizeof(k_drained) / sizeof(k_drained[0])) && at < out_max;
             i++ )
        {
            struct is_op op = IS_DRAIN(0, 10, -2);
            if( is_stat_value(player, k_drained[i]) <= 0 )
                continue;
            op.stat = (short)k_drained[i];
            out[at++] = op;
        }
        return at;
    }

    case IS_FAMILY_MOONLIGHT:
    {
        /* One row per stat, and the row is the STRONGEST effect the drinker's
         * Herblore level unlocks -- the reference picks the highest level
         * requirement that is satisfied, so a level-70 herbalist gets the
         * divine super defence arm rather than the plain one. */
        int const herblore = is_stat_value(player, IS_HERBLORE);
        struct is_op op;

        if( herblore >= 45 )
            op = (struct is_op)IS_BOOSTP(IS_ATTACK, 15, 5);
        else
            op = (struct is_op)IS_BOOSTP(IS_ATTACK, 10, 3);
        if( herblore >= 3 && at < out_max )
            out[at++] = op;

        if( herblore >= 70 )
            op = (struct is_op)IS_BOOSTP(IS_DEFENCE, 20, 7);
        else if( herblore >= 66 )
            op = (struct is_op)IS_BOOSTP(IS_DEFENCE, 15, 5);
        else
            op = (struct is_op)IS_BOOSTP(IS_DEFENCE, 10, 3);
        if( herblore >= 30 && at < out_max )
            out[at++] = op;

        if( herblore >= 55 )
            op = (struct is_op)IS_BOOSTP(IS_STRENGTH, 15, 5);
        else
            op = (struct is_op)IS_BOOSTP(IS_STRENGTH, 10, 3);
        if( herblore >= 12 && at < out_max )
            out[at++] = op;

        if( herblore >= 38 && at < out_max )
        {
            /* max(prayer * 25%, herblore * 30%) + 7, which is the one prayer
             * restore in the game that scales with the drinker's Herblore. */
            struct is_op prayer = IS_OP_MAKE(IS_PRAYER, IS_PERC, 0, 0, 0);
            int by_prayer = is_stat_maximum(player, IS_PRAYER) * 25 / 100;
            int by_herblore = herblore * 30 / 100;
            prayer.num = 0;
            prayer.delta = (short)(is_max(by_prayer, by_herblore) + 7);
            out[at++] = prayer;
        }
        return at;
    }

    case IS_FAMILY_AMBROSIA:
    {
        /* The boost is sized to carry the stat all the way back FIRST, so its
         * delta depends on how far down it is. */
        int hp_to_max = is_max(
            0, is_stat_maximum(player, IS_HITPOINTS) - is_stat_value(player, IS_HITPOINTS));
        int prayer_to_max = is_max(
            0, is_stat_maximum(player, IS_PRAYER) - is_stat_value(player, IS_PRAYER));
        struct is_op hp = IS_BOOSTP(IS_HITPOINTS, 25, 0);
        struct is_op prayer = IS_BOOSTP(IS_PRAYER, 20, 0);
        struct is_op run = IS_HEAL(IS_RUN_ENERGY, 100);

        hp.delta = (short)(2 + hp_to_max);
        prayer.delta = (short)(5 + prayer_to_max);
        if( at < out_max )
            out[at++] = hp;
        if( at < out_max )
            out[at++] = prayer;
        if( at < out_max )
            out[at++] = run;
        return at;
    }

    case IS_FAMILY_MIXED:
    {
        /*
         * A mix is the potion plus the vial's own heal, and it is listed
         * heal-first because that is the order the reference builds it in.
         *
         * What is NOT ported is the zamorak mix's hitpoints MERGE: there, the
         * potion itself takes hitpoints away while the mix gives some back,
         * and RuneLite folds the two into one row. Here they stay two rows,
         * one up and one down, which says the same thing and cannot be wrong
         * about the order they apply in.
         */
        struct is_op heal = IS_FOOD(0);
        heal.delta = effect->family_arg;
        if( at < out_max )
            out[at++] = heal;
        assert(effect->base);
        at += is_effect_ops(ctx, player, effect->base, out + at, out_max - at);
        return at;
    }

    default:
        break;
    }
    return at;
}

/** Every change one item would make, most significant first (the reference's
 *  order, which is the order the ops were declared in). */
static int
is_changes(
    struct ToriRS_PluginCtx* ctx,
    struct is_player const* player,
    struct is_effect const* effect,
    struct is_change* out,
    int out_max)
{
    struct is_op ops[IS_OPS_MAX];
    int count = is_effect_ops(ctx, player, effect, ops, IS_OPS_MAX);
    int at = 0;

    for( int i = 0; i < count && at < out_max; i++ )
        is_op_apply(ctx, player, &ops[i], &out[at++]);
    return at;
}

/* --------------------------------------------------------------- the face */

/*
 * The tooltip is set in a font this plugin SHIPS, in
 * `script/plugins/assets/item-stats/`: `text.png` is one row of glyphs with
 * the drop shadow already baked in, and `text.ini` says where each glyph is
 * and how far the pen moves after it. Both are cut by
 * `tools/fontbake_atlas.py` from a `fontbake` of the cache's own p11.
 *
 * A plugin's own face rather than api->draw_text, for two reasons. draw_text
 * draws in the client's HITSPLAT face -- a chunky combat number, not a
 * caption -- and it draws one string per overlay item, centred, with no way to
 * measure it, which a twenty-row block of aligned labels cannot be built out
 * of. Composing into one image instead means the whole tooltip is a single
 * blit, sized to its own contents, in the same face on every revision this
 * client boots: the atlas is a file, so a cache with no p11 changes nothing.
 */
struct is_glyph
{
    int x;
    int y;
    int w;
    int h;
    int off_x;
    int off_y;
    int advance;
};

#define IS_GLYPH_FIRST 32
#define IS_GLYPH_COUNT 96
static struct is_glyph g_glyph[IS_GLYPH_COUNT];
static int g_glyph_ready;
static int g_glyph_line_h = 10;
static int g_glyph_row_h = 10;

static int g_img_text = -1;
static uint32_t* g_text_px;
static int g_text_w;
static int g_text_h;

/* The plate the tooltip is drawn on, and how much of it is margin. */
#define IS_TIP_ARGB 0xE0221E19u
#define IS_TIP_BORDER 4
/* The line box: the atlas states its ascent, and the two descender pixels are
 * the rest of the cache's own 12-pixel line. */
#define IS_LINE_PITCH (g_glyph_line_h + 2)

#define IS_SCRATCH_W 288
#define IS_SCRATCH_H 352
static uint32_t g_scratch[IS_SCRATCH_W * IS_SCRATCH_H];

static uint32_t
is_over(uint32_t dst, uint32_t src)
{
    uint32_t const sa = src >> 24;
    uint32_t da;
    uint32_t out_a;

    if( sa == 0 )
        return dst;
    if( sa == 255 )
        return src;
    da = dst >> 24;
    out_a = sa + da * (255 - sa) / 255;
    if( out_a == 0 )
        return 0;
    {
        uint32_t const r =
            (((src >> 16) & 0xFF) * sa + ((dst >> 16) & 0xFF) * da * (255 - sa) / 255) / out_a;
        uint32_t const g =
            (((src >> 8) & 0xFF) * sa + ((dst >> 8) & 0xFF) * da * (255 - sa) / 255) / out_a;
        uint32_t const b =
            ((src & 0xFF) * sa + (dst & 0xFF) * da * (255 - sa) / 255) / out_a;
        return (out_a << 24) | (r << 16) | (g << 8) | b;
    }
}

/*
 * Read text.ini into g_glyph.
 *
 * A glyph line is one whose SECOND byte is '=' -- which is what lets the space
 * glyph, a line beginning with a space, be read by the same rule as every
 * other and keeps it apart from the header keys and the comments.
 */
static int
is_load_glyphs(struct ToriRS_PluginCtx* ctx)
{
    char const* at;
    int size = 0;

    assert(ctx);

    if( g_glyph_ready )
        return 1;
    if( !g_api->asset_load(ctx, "text.ini") )
        return 0;
    at = (char const*)g_api->asset_data(ctx, "text.ini", &size);
    if( !at || size <= 0 )
        return 0;

    for( char const* end = at + size; at < end; )
    {
        /* The asset is a byte range and not a C string, so every line is
         * copied out before atoi/sscanf run to a NUL that is not there. */
        char line[128];
        char const* start = at;
        char const* stop = start;
        size_t len;

        while( stop < end && *stop != '\n' )
            stop++;
        at = stop < end ? stop + 1 : end;
        if( stop > start && stop[-1] == '\r' )
            stop--;
        len = (size_t)(stop - start);
        if( len >= sizeof(line) )
            len = sizeof(line) - 1;
        memcpy(line, start, len);
        line[len] = '\0';

        if( len > 11 && strncmp(line, "row_height=", 11) == 0 )
        {
            g_glyph_row_h = atoi(line + 11);
            continue;
        }
        if( len > 12 && strncmp(line, "line_height=", 12) == 0 )
        {
            g_glyph_line_h = atoi(line + 12);
            continue;
        }
        if( len < 3 || line[1] != '=' )
            continue;
        {
            int const index = (unsigned char)line[0] - IS_GLYPH_FIRST;
            struct is_glyph* g;

            if( index < 0 || index >= IS_GLYPH_COUNT )
                continue;
            g = &g_glyph[index];
            if( sscanf(
                    line + 2,
                    "%d %d %d %d %d %d %d",
                    &g->x,
                    &g->y,
                    &g->w,
                    &g->h,
                    &g->off_x,
                    &g->off_y,
                    &g->advance) == 7 )
                g_glyph_ready = 1;
        }
    }
    return g_glyph_ready;
}

static int
is_text_width(char const* text)
{
    int width = 0;

    assert(text);
    for( char const* p = text; *p; p++ )
    {
        int const index = (unsigned char)*p - IS_GLYPH_FIRST;
        if( index >= 0 && index < IS_GLYPH_COUNT )
            width += g_glyph[index].advance;
    }
    return width;
}

/*
 * `text` into `buf` at the pen, tinted.
 *
 * The multiply works because of how the atlas is baked: every glyph pixel is
 * either white ink or the black drop shadow, so scaling by a colour gives that
 * colour and leaves the shadow black. One baked row therefore serves all five
 * of the reference's positivity colours, and a user who edits one of them in
 * the settings panel gets it without a rebake.
 */
static void
is_text(int x, int top, char const* text, int w, int h, uint32_t tint)
{
    int pen = x;

    assert(text);
    if( !g_glyph_ready || !g_text_px )
        return;

    for( char const* p = text; *p; p++ )
    {
        int const index = (unsigned char)*p - IS_GLYPH_FIRST;
        struct is_glyph const* g;

        if( index < 0 || index >= IS_GLYPH_COUNT )
            continue;
        g = &g_glyph[index];
        for( int gy = 0; gy < g->h; gy++ )
        {
            int const ty = top + g->off_y + gy;
            if( ty < 0 || ty >= h )
                continue;
            for( int gx = 0; gx < g->w; gx++ )
            {
                int const tx = pen + g->off_x + gx;
                uint32_t px;
                uint32_t a;

                if( tx < 0 || tx >= w )
                    continue;
                px = g_text_px[(g->y + gy) * g_text_w + (g->x + gx)];
                a = px >> 24;
                if( a == 0 )
                    continue;
                {
                    uint32_t r = ((px >> 16) & 0xFF) * ((tint >> 16) & 0xFF) / 255;
                    uint32_t gg = ((px >> 8) & 0xFF) * ((tint >> 8) & 0xFF) / 255;
                    uint32_t b = (px & 0xFF) * (tint & 0xFF) / 255;
                    g_scratch[ty * w + tx] =
                        is_over(g_scratch[ty * w + tx], (a << 24) | (r << 16) | (gg << 8) | b);
                }
            }
        }
        pen += g->advance;
    }
}

/* ------------------------------------------------------------ the tooltip */

/** One line of the panel: a label in the plate's own ink, and a value in the
 *  colour that says what the change is worth. */
struct is_row
{
    char left[72];
    char right[32];
    uint32_t left_rgb;
    uint32_t right_rgb;
};

#define IS_ROWS_MAX 40

static struct is_row g_rows[IS_ROWS_MAX];
static int g_row_count;

/** The composed panel, and what it was composed from -- so a pointer resting
 *  on one item recomposes nothing. */
static int g_tip_image = -1;
static int g_tip_w;
static int g_tip_h;
static int g_tip_obj;
static long g_tip_frame;

static uint32_t
is_positivity_rgb(struct ToriRS_PluginCtx* ctx, int positivity)
{
    assert(ctx);
    switch( positivity )
    {
    case IS_BETTER_UNCAPPED:
        return g_api->cfg_color(ctx, "color_better");
    case IS_BETTER_SOMECAPPED:
        return g_api->cfg_color(ctx, "color_better_some_capped");
    case IS_BETTER_CAPPED:
        return g_api->cfg_color(ctx, "color_better_capped");
    case IS_WORSE:
        return g_api->cfg_color(ctx, "color_worse");
    default:
        return g_api->cfg_color(ctx, "color_no_change");
    }
}

static void
is_row_add(char const* left, uint32_t left_rgb, char const* right, uint32_t right_rgb)
{
    struct is_row* row;

    if( g_row_count >= IS_ROWS_MAX )
        return;
    row = &g_rows[g_row_count++];
    memset(row, 0, sizeof(*row));
    snprintf(row->left, sizeof(row->left), "%s", left ? left : "");
    snprintf(row->right, sizeof(row->right), "%s", right ? right : "");
    row->left_rgb = left_rgb;
    row->right_rgb = right_rgb;
}

/** "+5", "-3", and for a range "+5~7" -- RangeStatChange's own formatting,
 *  minus its plus-or-minus sign, which the RS charset has no glyph for. */
static void
is_format_boost(char* out, int out_size, int value, int min_value, int has_range)
{
    assert(out);
    if( !has_range || value == min_value )
    {
        snprintf(out, out_size, "%+d", value);
        return;
    }
    if( (min_value < 0) == (value < 0) )
        snprintf(out, out_size, "%+d~%d", min_value, value < 0 ? -value : value);
    else
        snprintf(out, out_size, "%+d~%+d", min_value, value);
}

/*
 * The consumable half: one line per stat the item would move.
 *
 * The line is the reference's, field for field --
 * `relative/theoretical (absolute) Stat` -- with each of the three parts
 * switched on by its own config row, because which of them a player wants is
 * the whole question the reference's own settings ask. Relative is "what you
 * would actually gain", theoretical is "what the item is worth before the cap
 * eats it", and absolute is "where the stat lands".
 */
static void
is_build_consumable(
    struct ToriRS_PluginCtx* ctx,
    struct is_player const* player,
    struct is_effect const* effect)
{
    struct is_change changes[IS_OPS_MAX];
    int const relative = g_api->cfg_bool(ctx, "show_relative");
    int const theoretical = g_api->cfg_bool(ctx, "show_theoretical");
    int const absolute = g_api->cfg_bool(ctx, "show_absolute");
    int count;

    assert(ctx);
    assert(effect);

    if( !relative && !theoretical && !absolute )
        return;

    count = is_changes(ctx, player, effect, changes, IS_OPS_MAX);
    for( int i = 0; i < count; i++ )
    {
        struct is_change const* c = &changes[i];
        char line[72];
        char part[32];
        int at = 0;

        line[0] = '\0';
        if( relative )
        {
            is_format_boost(part, sizeof(part), c->relative, c->min_relative, c->has_range);
            at += snprintf(line + at, sizeof(line) - (size_t)at, "%s", part);
        }
        if( theoretical )
        {
            is_format_boost(
                part, sizeof(part), c->theoretical, c->min_theoretical, c->has_range);
            at += snprintf(
                line + at, sizeof(line) - (size_t)at, "%s%s", relative ? "/" : "", part);
        }
        if( absolute )
        {
            int const bracket = relative || theoretical;
            at += snprintf(
                line + at,
                sizeof(line) - (size_t)at,
                bracket ? " (%d)" : "%d",
                c->absolute);
        }
        snprintf(
            line + at, sizeof(line) - (size_t)at, " %s", is_stat_name(ctx, c->stat));
        is_row_add(line, is_positivity_rgb(ctx, c->positivity), NULL, 0);
    }
}

/* --------------------------------------------- bonuses this cache lacks */

/*
 * The equipment table the plugin ships, for revisions whose cache states no
 * bonuses at all.
 *
 * That is every DAT1 world -- the 2004-2005 caches, and every LostCity server
 * this client boots. There the bonuses were the SERVER's: the equipment screen
 * was text it sent, and the obj record carries no params, no wearpos and no
 * attack rate. A client cannot compute what it was never told, so the numbers
 * are shipped, exactly as RuneLite ships item_stats.json -- and the honest
 * source for them is an OldSchool cache, which is this game's own answer for an
 * item of that name. See tools/item_bonus_bake.
 *
 * Read ONLY when the open cache states nothing: an OldSchool session answers
 * out of its own record, so the table can never contradict the cache the player
 * is actually running.
 */
struct is_bonus_row
{
    /** Normalized with the same is_normalize_name the lookup uses, so the two
     *  sides cannot drift: the file ships raw lowercase names. */
    char name[56];
    short slot;
    short two_handed;
    short bonus[TORIRS_PLUGIN_BONUS_COUNT];
    short ranged_strength;
    short speed;
};

static struct is_bonus_row* g_bonus;
static int g_bonus_count;
/** 0 not tried, 1 loaded, -1 the asset is absent -- which is a legitimate
 *  install (the table is only needed by the older revisions) and must not be
 *  retried every frame. */
static int g_bonus_state;

static int
is_load_bonuses(struct ToriRS_PluginCtx* ctx)
{
    char const* at;
    int size = 0;
    int cap = 0;

    assert(ctx);

    if( g_bonus_state != 0 )
        return g_bonus_state > 0;
    if( !g_api->asset_load(ctx, "bonuses.txt") )
        return 0; /* still reading; asked again next frame */
    at = (char const*)g_api->asset_data(ctx, "bonuses.txt", &size);
    if( !at || size <= 0 )
    {
        g_bonus_state = -1;
        return 0;
    }

    for( char const* end = at + size; at < end; )
    {
        /* The asset is a byte range and not a C string, so each line is copied
         * out before sscanf runs to a NUL that is not there. */
        char line[160];
        char const* start = at;
        char const* stop = start;
        char const* eq;
        size_t len;
        struct is_bonus_row row;
        int v[16];

        while( stop < end && *stop != '\n' )
            stop++;
        at = stop < end ? stop + 1 : end;
        if( stop > start && stop[-1] == '\r' )
            stop--;
        len = (size_t)(stop - start);
        if( len >= sizeof(line) )
            len = sizeof(line) - 1;
        memcpy(line, start, len);
        line[len] = '\0';

        if( line[0] == ';' || line[0] == '\0' )
            continue;
        eq = strchr(line, '=');
        if( !eq || eq == line )
            continue;
        if( sscanf(
                eq + 1,
                "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
                &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7],
                &v[8], &v[9], &v[10], &v[11], &v[12], &v[13], &v[14], &v[15]) != 16 )
            continue;

        memset(&row, 0, sizeof(row));
        {
            char raw[64];
            size_t name_len = (size_t)(eq - line);
            if( name_len >= sizeof(raw) )
                name_len = sizeof(raw) - 1;
            memcpy(raw, line, name_len);
            raw[name_len] = '\0';
            is_normalize_name(raw, row.name, sizeof(row.name));
        }
        if( row.name[0] == '\0' )
            continue;
        row.slot = (short)v[0];
        row.two_handed = (short)v[1];
        for( int i = 0; i < TORIRS_PLUGIN_BONUS_COUNT; i++ )
            row.bonus[i] = (short)v[2 + i];
        row.ranged_strength = (short)v[14];
        row.speed = (short)v[15];

        if( g_bonus_count == cap )
        {
            cap = cap ? cap * 2 : 512;
            g_bonus = realloc(g_bonus, (size_t)cap * sizeof(*g_bonus));
            assert(g_bonus);
        }
        g_bonus[g_bonus_count++] = row;
    }

    /* The bytes are the host's and are not needed once parsed. */
    g_api->asset_release(ctx, "bonuses.txt");
    g_bonus_state = g_bonus_count > 0 ? 1 : -1;
    return g_bonus_state > 0;
}

static struct is_bonus_row const*
is_bonus_lookup(struct ToriRS_PluginCtx* ctx, char const* cache_name)
{
    char key[56];

    assert(ctx);
    assert(cache_name);

    if( !is_load_bonuses(ctx) )
        return NULL;
    is_normalize_name(cache_name, key, sizeof(key));
    if( key[0] == '\0' )
        return NULL;
    for( int i = 0; i < g_bonus_count; i++ )
        if( strcmp(g_bonus[i].name, key) == 0 )
            return &g_bonus[i];
    return NULL;
}

/* ------------------------------------------------------- equipment stats */

/*
 * The bonuses an item carries, and where it is worn.
 *
 * `speed` is the attack rate in ticks; unarmed is 4, which is the reference's
 * own UNARMED constant and the reason a weapon's speed row reads sensibly with
 * nothing equipped.
 */
struct is_equip
{
    int bonus[TORIRS_PLUGIN_BONUS_COUNT];
    int ranged_strength;
    int speed;
    int wearpos;
    int two_handed;
    int stated;
};

/* Worn slots, in the numbering an objtype's wearpos uses -- the same index the
 * WORN container is addressed by. */
enum
{
    IS_SLOT_CAPE = 1,
    IS_SLOT_WEAPON = 3,
    IS_SLOT_SHIELD = 5,
    IS_SLOT_RING = 12
};

static void
is_equip_from_info(struct ToriRS_PluginObjInfo const* info, struct is_equip* out);

/*
 * What this item does to a combat roll, from whichever source knows.
 *
 * The cache first, always: an OldSchool record states its own bonuses and is
 * the truth for the session being played. Only when it states NOTHING -- a
 * dat1 world, where the numbers were the server's -- does the shipped table
 * answer, and then it also supplies the slot and the two-handedness, because a
 * dat1 record carries no wearpos either.
 *
 * @return 1 when the bonuses are known, 0 when neither source has them, and 0
 * leaves `out` zeroed rather than claiming an item has none.
 */
static int
is_equip_resolve(
    struct ToriRS_PluginCtx* ctx,
    struct ToriRS_PluginObjInfo const* info,
    struct is_equip* out)
{
    struct is_bonus_row const* row;

    assert(ctx);
    assert(info);
    assert(out);

    if( info->has_bonuses )
    {
        is_equip_from_info(info, out);
        return 1;
    }

    row = is_bonus_lookup(ctx, info->name);
    if( !row || row->slot < 0 )
        return 0;

    memset(out, 0, sizeof(*out));
    out->wearpos = row->slot;
    out->two_handed = row->two_handed;
    out->ranged_strength = row->ranged_strength;
    out->speed = row->speed >= 0 ? row->speed : 0;
    out->stated = 1;
    for( int i = 0; i < TORIRS_PLUGIN_BONUS_COUNT; i++ )
        out->bonus[i] = row->bonus[i];
    return 1;
}

static void
is_equip_from_info(struct ToriRS_PluginObjInfo const* info, struct is_equip* out)
{
    assert(info);
    assert(out);

    memset(out, 0, sizeof(*out));
    out->wearpos = info->wearpos;
    out->stated = info->has_bonuses;
    out->ranged_strength = info->ranged_strength;
    /*
     * ZERO for a record that states no rate, and unarmed's four only where
     * is_equip_unarmed puts it.
     *
     * Four here instead looks harmless and is not: equipping a two-handed
     * weapon takes the OFF-HAND off as well, so the shield's speed is
     * subtracted too -- and a shield that claimed unarmed's rate took four
     * ticks off every two-hander's speed row, which read as the crossbow being
     * faster than the bare hand it replaced.
     */
    out->speed = info->attack_rate >= 0 ? info->attack_rate : 0;
    out->two_handed = info->wearpos == IS_SLOT_WEAPON &&
                      (info->wearpos2 == IS_SLOT_SHIELD || info->wearpos3 == IS_SLOT_SHIELD);
    for( int i = 0; i < TORIRS_PLUGIN_BONUS_COUNT; i++ )
        out->bonus[i] = info->bonus[i];
}

/** Nothing in the slot: every bonus zero, and unarmed's attack speed. */
static void
is_equip_unarmed(struct is_equip* out)
{
    assert(out);
    memset(out, 0, sizeof(*out));
    out->wearpos = IS_SLOT_WEAPON;
    out->speed = 4;
    out->stated = 1;
}

static void
is_equip_subtract(struct is_equip* self, struct is_equip const* other)
{
    assert(self);
    assert(other);
    for( int i = 0; i < TORIRS_PLUGIN_BONUS_COUNT; i++ )
        self->bonus[i] -= other->bonus[i];
    self->ranged_strength -= other->ranged_strength;
    self->speed -= other->speed;
}

/** What is worn in `slot`, or 0 when the slot is empty / the container is not
 *  known / the record is not resident. */
static int
is_worn_equip(struct ToriRS_PluginCtx* ctx, int slot, struct is_equip* out)
{
    struct ToriRS_PluginObjInfo info;
    int obj_id = -1;

    assert(ctx);
    assert(out);

    if( !g_api->inv_slot(ctx, TORIRS_PLUGIN_INV_WORN, slot, &obj_id, NULL) )
        return 0;
    if( obj_id < 0 || !g_api->obj_info(ctx, obj_id, &info) )
        return 0;
    return is_equip_resolve(ctx, &info, out);
}

/*
 * One bonus row: "Stab: 82 (+15)".
 *
 * `value` is what the hovered item has and `diff` what equipping it would
 * change, and the row is skipped entirely when both are zero -- which is what
 * keeps a cape's tooltip four lines long instead of fifteen. `inverse` is for
 * attack speed, where a smaller number is the better one.
 */
static void
is_bonus_row(
    struct ToriRS_PluginCtx* ctx,
    char const* label,
    int value,
    int diff,
    int inverse,
    int show_base)
{
    char left[72];
    char right[32];
    uint32_t const neutral = g_api->cfg_color(ctx, "color_no_change");
    uint32_t rgb;

    assert(ctx);
    assert(label);

    if( value == 0 && diff == 0 )
        return;
    if( diff == 0 && !(g_api->cfg_bool(ctx, "always_show_base_stats") && show_base) )
        return;

    if( diff > 0 )
        rgb = is_positivity_rgb(ctx, inverse ? IS_WORSE : IS_BETTER_UNCAPPED);
    else if( diff < 0 )
        rgb = is_positivity_rgb(ctx, inverse ? IS_BETTER_UNCAPPED : IS_WORSE);
    else
        rgb = neutral;

    if( g_api->cfg_bool(ctx, "always_show_base_stats") && show_base )
    {
        snprintf(left, sizeof(left), "%s: %d ", label, value);
        if( diff != 0 )
            snprintf(right, sizeof(right), "(%+d)", diff);
        else
            right[0] = '\0';
    }
    else
    {
        snprintf(left, sizeof(left), "%s: ", label);
        snprintf(right, sizeof(right), "%+d", diff);
    }
    is_row_add(left, neutral, right, rgb);
}

/*
 * The equipment half: this item's bonuses, and what wearing it would change.
 *
 * The comparison is the reference's, including its two awkward cases. Equipping
 * a SHIELD while a two-handed weapon is worn takes the weapon off, so the
 * difference is measured against that weapon minus unarmed; equipping a
 * two-handed WEAPON takes the shield off too, so the off-hand's bonuses come
 * out as well. Getting either wrong makes the numbers look right and be wrong
 * exactly when a player is deciding between two setups.
 */
static void
is_build_equipment(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginObjInfo const* info)
{
    struct is_equip self;
    struct is_equip diff;
    struct is_equip other;
    struct is_equip offhand;
    int have_other = 0;
    int have_offhand = 0;
    uint32_t const header_rgb = g_api->cfg_color(ctx, "color_header");
    int rows_before;

    assert(ctx);
    assert(info);

    if( !is_equip_resolve(ctx, info, &self) || self.wearpos < 0 )
        return;

    diff = self;

    have_other = is_worn_equip(ctx, self.wearpos, &other);
    if( !have_other && self.wearpos == IS_SLOT_SHIELD )
    {
        struct is_equip weapon;
        if( is_worn_equip(ctx, IS_SLOT_WEAPON, &weapon) && weapon.two_handed )
        {
            /* shield - (2h - unarmed): taking the two-hander off gives the
             * unarmed attack speed back. */
            struct is_equip unarmed;
            is_equip_unarmed(&unarmed);
            is_equip_subtract(&weapon, &unarmed);
            other = weapon;
            have_other = 1;
        }
    }
    if( self.wearpos == IS_SLOT_WEAPON )
    {
        if( !have_other )
        {
            is_equip_unarmed(&other);
            have_other = 1;
        }
        if( self.two_handed )
            have_offhand = is_worn_equip(ctx, IS_SLOT_SHIELD, &offhand);
    }

    if( have_other )
        is_equip_subtract(&diff, &other);
    if( have_offhand )
        is_equip_subtract(&diff, &offhand);

    is_bonus_row(ctx, "Prayer", self.bonus[TORIRS_PLUGIN_BONUS_PRAYER],
        diff.bonus[TORIRS_PLUGIN_BONUS_PRAYER], 0, 1);
    /* Speed only where the item is a weapon: a cape has no attack rate, and
     * printing unarmed's four for one would invent a stat. */
    if( self.wearpos == IS_SLOT_WEAPON )
        is_bonus_row(ctx, "Speed", self.speed, diff.speed, 1, 1);
    is_bonus_row(ctx, "Melee Str", self.bonus[TORIRS_PLUGIN_BONUS_STRENGTH],
        diff.bonus[TORIRS_PLUGIN_BONUS_STRENGTH], 0, 1);
    is_bonus_row(ctx, "Range Str", self.ranged_strength, diff.ranged_strength, 0, 1);

    rows_before = g_row_count;
    is_bonus_row(ctx, "Stab", self.bonus[TORIRS_PLUGIN_BONUS_ATTACK_STAB],
        diff.bonus[TORIRS_PLUGIN_BONUS_ATTACK_STAB], 0, 1);
    is_bonus_row(ctx, "Slash", self.bonus[TORIRS_PLUGIN_BONUS_ATTACK_SLASH],
        diff.bonus[TORIRS_PLUGIN_BONUS_ATTACK_SLASH], 0, 1);
    is_bonus_row(ctx, "Crush", self.bonus[TORIRS_PLUGIN_BONUS_ATTACK_CRUSH],
        diff.bonus[TORIRS_PLUGIN_BONUS_ATTACK_CRUSH], 0, 1);
    is_bonus_row(ctx, "Magic", self.bonus[TORIRS_PLUGIN_BONUS_ATTACK_MAGIC],
        diff.bonus[TORIRS_PLUGIN_BONUS_ATTACK_MAGIC], 0, 1);
    is_bonus_row(ctx, "Range", self.bonus[TORIRS_PLUGIN_BONUS_ATTACK_RANGE],
        diff.bonus[TORIRS_PLUGIN_BONUS_ATTACK_RANGE], 0, 1);
    /* The header goes in only if the group produced anything, which is why it
     * is inserted after the fact rather than printed first. */
    if( g_row_count > rows_before && g_row_count < IS_ROWS_MAX )
    {
        memmove(
            &g_rows[rows_before + 1],
            &g_rows[rows_before],
            sizeof(g_rows[0]) * (size_t)(g_row_count - rows_before));
        g_row_count++;
        memset(&g_rows[rows_before], 0, sizeof(g_rows[0]));
        snprintf(g_rows[rows_before].left, sizeof(g_rows[rows_before].left), "Attack bonus");
        g_rows[rows_before].left_rgb = header_rgb;
    }

    rows_before = g_row_count;
    is_bonus_row(ctx, "Stab", self.bonus[TORIRS_PLUGIN_BONUS_DEFENCE_STAB],
        diff.bonus[TORIRS_PLUGIN_BONUS_DEFENCE_STAB], 0, 1);
    is_bonus_row(ctx, "Slash", self.bonus[TORIRS_PLUGIN_BONUS_DEFENCE_SLASH],
        diff.bonus[TORIRS_PLUGIN_BONUS_DEFENCE_SLASH], 0, 1);
    is_bonus_row(ctx, "Crush", self.bonus[TORIRS_PLUGIN_BONUS_DEFENCE_CRUSH],
        diff.bonus[TORIRS_PLUGIN_BONUS_DEFENCE_CRUSH], 0, 1);
    is_bonus_row(ctx, "Magic", self.bonus[TORIRS_PLUGIN_BONUS_DEFENCE_MAGIC],
        diff.bonus[TORIRS_PLUGIN_BONUS_DEFENCE_MAGIC], 0, 1);
    is_bonus_row(ctx, "Range", self.bonus[TORIRS_PLUGIN_BONUS_DEFENCE_RANGE],
        diff.bonus[TORIRS_PLUGIN_BONUS_DEFENCE_RANGE], 0, 1);
    if( g_row_count > rows_before && g_row_count < IS_ROWS_MAX )
    {
        memmove(
            &g_rows[rows_before + 1],
            &g_rows[rows_before],
            sizeof(g_rows[0]) * (size_t)(g_row_count - rows_before));
        g_row_count++;
        memset(&g_rows[rows_before], 0, sizeof(g_rows[0]));
        snprintf(g_rows[rows_before].left, sizeof(g_rows[rows_before].left), "Defence bonus");
        g_rows[rows_before].left_rgb = header_rgb;
    }
}

/* ------------------------------------------------------------- the events */

/*
 * What the pointer is over, off the hover rebuild.
 *
 * An INV_SLOT row is a row about an inventory CELL, and the cell's item is the
 * row's target -- which is the whole reason the bridge fills `target_id` for
 * this pick kind. Any row of the pass will do, because a hover pass builds
 * rows for one thing: the last one is the acting row, the rest are the same
 * cell's other verbs.
 */
static enum ToriRS_PluginVerdict
is_on_menu_build(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)ctx;
    struct ToriRS_PluginEvMenuBuild* ev = (struct ToriRS_PluginEvMenuBuild*)event;

    (void)userdata;
    assert(ctx);
    assert(ev);

    /* The right-click menu's build is not a hover: the pointer is over the
     * menu itself by then, and following it would leave a tooltip pinned to a
     * cell nobody is pointing at. */
    if( !ev->hover_pass )
        return TORIRS_PLUGIN_PASS;

    g_hover.obj_id = -1;
    g_hover.component_id = -1;
    g_hover.slot = -1;
    for( int i = 0; i < ev->row_count; i++ )
    {
        struct ToriRS_PluginMenuRow const* row = &ev->rows[i];
        if( row->pick_kind != 2 /* UI_MINIMENU_PICK_INV_SLOT */ || row->target_id < 0 )
            continue;
        g_hover.obj_id = row->target_id;
        g_hover.component_id = row->component_id;
        g_hover.slot = row->slot;
        break;
    }
    g_hover.frame = g_frame;
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
is_on_frame(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)ctx;
    (void)event;
    (void)userdata;
    g_frame++;
    return TORIRS_PLUGIN_PASS;
}

/** Ask for the atlas, and read its pixels back once they land. */
static void
is_load_art(struct ToriRS_PluginCtx* ctx)
{
    assert(ctx);

    if( g_img_text < 0 )
        g_img_text = g_api->image_load(ctx, "text.png");
    is_load_glyphs(ctx);

    if( !g_text_px && g_img_text >= 0 &&
        g_api->image_size(ctx, g_img_text, &g_text_w, &g_text_h) )
    {
        int const pixels = g_text_w * g_text_h;
        static uint32_t s_text_px[512 * 64];
        if( pixels > 0 && pixels <= (int)(sizeof(s_text_px) / sizeof(s_text_px[0])) &&
            g_api->image_pixels(ctx, g_img_text, s_text_px, pixels) == pixels )
            g_text_px = s_text_px;
    }
}

/*
 * Compose the panel, and blit it beside the pointer.
 *
 * Composed only when the ITEM changes, because the contents cannot change
 * while the pointer sits still -- but blitted every frame, because the panel
 * follows the pointer. That split is xp_orbs' and it is what keeps a
 * twenty-row super-restore tooltip off the per-frame budget.
 */
static enum ToriRS_PluginVerdict
is_on_draw_canvas(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    struct ToriRS_PluginEvDrawCanvas* ev = (struct ToriRS_PluginEvDrawCanvas*)event;
    struct ToriRS_PluginObjInfo info;
    struct is_player player;
    int mouse_x = 0;
    int mouse_y = 0;
    int x;
    int y;

    (void)userdata;
    assert(ctx);
    assert(ev);

    is_load_art(ctx);
    if( !g_glyph_ready || !g_text_px )
        return TORIRS_PLUGIN_PASS;

    /* A stash nobody refreshed this frame or last is not a hover any more:
     * either the pointer left the cell, or the right-click menu went up and
     * the hover rebuild stopped running. */
    if( g_hover.obj_id < 0 || g_frame - g_hover.frame > 1 )
        return TORIRS_PLUGIN_PASS;
    if( !g_api->mouse_pos(ctx, &mouse_x, &mouse_y) )
        return TORIRS_PLUGIN_PASS;
    if( !g_api->obj_info(ctx, g_hover.obj_id, &info) )
        return TORIRS_PLUGIN_PASS;

    /* A noted stack is the base item wearing paper: it has no ops, no params
     * and no name of its own worth reading, so the question is asked again of
     * what it stands for. */
    if( info.cert_link >= 0 && !g_api->obj_info(ctx, info.cert_link, &info) )
        return TORIRS_PLUGIN_PASS;

    if( g_tip_image < 0 || g_tip_obj != info.obj_id || g_frame - g_tip_frame > 30 )
    {
        int width = 0;
        int height;

        g_row_count = 0;
        is_player_sample(ctx, &player);

        if( g_api->cfg_bool(ctx, "consumable_stats") )
        {
            struct is_effect const* effect = is_lookup(info.name);
            if( effect )
                is_build_consumable(ctx, &player, effect);
        }
        if( g_api->cfg_bool(ctx, "equipment_stats") )
            is_build_equipment(ctx, &info);

        if( g_row_count == 0 )
            return TORIRS_PLUGIN_PASS;

        for( int i = 0; i < g_row_count; i++ )
        {
            int const w = is_text_width(g_rows[i].left) + is_text_width(g_rows[i].right);
            if( w > width )
                width = w;
        }
        width += IS_TIP_BORDER * 2;
        height = IS_TIP_BORDER * 2 + g_row_count * IS_LINE_PITCH;
        if( width > IS_SCRATCH_W )
            width = IS_SCRATCH_W;
        if( height > IS_SCRATCH_H )
            height = IS_SCRATCH_H;

        /* The plate is written rather than cleared and then covered: every
         * pixel of it is the panel. */
        for( int i = 0; i < width * height; i++ )
            g_scratch[i] = IS_TIP_ARGB;
        for( int i = 0; i < g_row_count; i++ )
        {
            int const top = IS_TIP_BORDER + i * IS_LINE_PITCH;
            is_text(IS_TIP_BORDER, top, g_rows[i].left, width, height, g_rows[i].left_rgb);
            if( g_rows[i].right[0] )
                is_text(
                    IS_TIP_BORDER + is_text_width(g_rows[i].left),
                    top,
                    g_rows[i].right,
                    width,
                    height,
                    g_rows[i].right_rgb);
        }

        g_tip_image = g_api->image_compose(ctx, "tooltip.png", width, height, g_scratch);
        if( g_tip_image < 0 )
            return TORIRS_PLUGIN_PASS;
        g_tip_w = width;
        g_tip_h = height;
        g_tip_obj = info.obj_id;
        g_tip_frame = g_frame;
    }

    x = mouse_x + 12;
    y = mouse_y + 16;
    if( x + g_tip_w > ev->width )
        x = mouse_x - g_tip_w - 4;
    if( y + g_tip_h > ev->height )
        y = mouse_y - g_tip_h - 4;
    if( x < 0 )
        x = 0;
    if( y < 0 )
        y = 0;
    g_api->draw_image(ctx, ev->surface, g_tip_image, x, y, 0, 0, 0, 0, 0);
    return TORIRS_PLUGIN_PASS;
}

/* A settings change repaints: the five colours and the three number rows all
 * live in the composed image, so a panel kept from before the change would
 * show the old ones until the pointer moved. */
static enum ToriRS_PluginVerdict
is_on_config_changed(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)ctx;
    (void)event;
    (void)userdata;
    g_tip_image = -1;
    g_tip_obj = -1;
    return TORIRS_PLUGIN_PASS;
}

static void
is_init(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api)
{
    assert(ctx);
    assert(api);
    assert(api->abi_version == TORIRS_PLUGIN_ABI);

    g_api = api;
    g_hover.obj_id = -1;
    g_hover.frame = -1000;
    g_tip_image = -1;
    g_tip_obj = -1;
    g_glyph_ready = 0;
    g_img_text = -1;
    g_text_px = NULL;

    api->subscribe(ctx, TORIRS_PLUGIN_EV_FRAME_START, is_on_frame, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_MENU_BUILD, is_on_menu_build, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_DRAW_CANVAS, is_on_draw_canvas, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_CONFIG_CHANGED, is_on_config_changed, NULL);
}

static void
is_shutdown(struct ToriRS_PluginCtx* ctx)
{
    assert(ctx);
    if( g_img_text >= 0 )
        g_api->image_release(ctx, g_img_text);
    if( g_tip_image >= 0 )
        g_api->image_release(ctx, g_tip_image);
    g_img_text = -1;
    g_tip_image = -1;
    g_tip_obj = -1;
    g_text_px = NULL;
    g_glyph_ready = 0;
    free(g_bonus);
    g_bonus = NULL;
    g_bonus_count = 0;
    g_bonus_state = 0;
}

/*
 * The reference's own settings, with two of them dropped.
 *
 * `showWeight` and the GE panel are gone because neither has anything to read:
 * no cache states a weight, and this client has no Grand Exchange. The rest are
 * RuneLite's, defaults included -- including the three number rows, whose
 * combination is the whole question of what a stat tooltip is for.
 */
static struct ToriRS_PluginConfigItem const ITEM_STATS_CONFIG[] = {
    { "consumable_stats", TORIRS_PLUGIN_CFG_BOOL, "Show food and potion effects", "1", 0, 0, NULL, 0 },
    { "equipment_stats", TORIRS_PLUGIN_CFG_BOOL, "Show equipment bonuses", "1", 0, 0, NULL, 0 },
    { "show_relative", TORIRS_PLUGIN_CFG_BOOL, "Show what you would gain", "1", 0, 0, NULL, 0 },
    { "show_absolute", TORIRS_PLUGIN_CFG_BOOL, "Show where the stat lands", "1", 0, 0, NULL, 0 },
    { "show_theoretical", TORIRS_PLUGIN_CFG_BOOL, "Show the boost before the cap", "0", 0, 0, NULL, 0 },
    { "always_show_base_stats", TORIRS_PLUGIN_CFG_BOOL, "Always show the item's own bonuses", "0", 0, 0, NULL, 0 },
    { "color_better", TORIRS_PLUGIN_CFG_COLOR, "Better (nothing wasted)", "#33EE33", 0, 0, NULL, 0 },
    { "color_better_some_capped", TORIRS_PLUGIN_CFG_COLOR, "Better (some wasted)", "#9CEE33", 0, 0, NULL, 0 },
    { "color_better_capped", TORIRS_PLUGIN_CFG_COLOR, "Better (capped)", "#EEEE33", 0, 0, NULL, 0 },
    { "color_no_change", TORIRS_PLUGIN_CFG_COLOR, "No change", "#EEEEEE", 0, 0, NULL, 0 },
    { "color_worse", TORIRS_PLUGIN_CFG_COLOR, "Worse", "#EE3333", 0, 0, NULL, 0 },
    { "color_header", TORIRS_PLUGIN_CFG_COLOR, "Group heading", "#FF981F", 0, 0, NULL, 0 },
    { NULL, TORIRS_PLUGIN_CFG_BOOL, NULL, NULL, 0, 0, NULL, 0 },
};

struct ToriRS_PluginDef const TORIRS_PLUGIN_ITEM_STATS = {
    .name = "item-stats",
    .title = "Item Stats",
    .version = "1.0.0",
    .priority = 0,
    .config = ITEM_STATS_CONFIG,
    .init = is_init,
    .shutdown = is_shutdown,
};
