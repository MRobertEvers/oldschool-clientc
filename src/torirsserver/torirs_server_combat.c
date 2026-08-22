/*
 * Combat, minus the combat.
 *
 * What is left in this file is the simulation around a fight: whose turn it is,
 * the attack clock, approaching and squaring up, facing, hitpoints, death and
 * respawn. Every number a swing is made of — the effective levels, the attack
 * and defence rolls, the max hit, the accuracy, the experience, the animations,
 * the protection prayers — is content, in
 * `skill_combat/combat_stats.rs2`, and this file fires into it via triggers:
 * `[opnpc2,<npc>]` for the player's swing, `[ai_opplayer2,<npc>]` for the
 * npc's swing, and `[advancestat,<stat>]` for level-up messages.
 *
 * That is the reference's division. LostCity's engine has no combat formulas at
 * all; `[label,player_melee_attack]` and `[proc,npc_meleeattack]` are content,
 * and its engine contributes `randominc`, `npc_damage` and a tick. The
 * arithmetic that used to be documented at the top of this file — `base + style
 * bonus + 8`, `effective * (bonus + 64)`, the two-branch closed-form accuracy —
 * is now stated once, in RuneScript, where a server operator can change it.
 *
 * The one thing worth carrying over from that comment is *why* it is
 * computable: the cache. An OldSchool obj or npc record carries its equipment
 * bonuses in its param table — ids 0..11 are the twelve bonuses and 14 is the
 * attack rate — so a bronze scimitar really does contribute +7 slash and +6
 * strength, and a guard really does have +25 slash defence, with no
 * hand-written table anywhere. See torirs_server_objinfo.c for how those are read and
 * why the ids are trustworthy.
 *
 * The test for whether something belongs here: does it name a tick or a
 * distance, or does it name a bonus or a formula? The first is this file, the
 * second is content.
 */

#include "torirs_server.h"

#include "torirs_server_content.h"
#include "torirs_server_scene.h"

#include "ss_trigger.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static int
abs_of(int value)
{
    return value < 0 ? -value : value;
}

/** Chebyshev distance: a diagonal step costs the same as a straight one. */
static int
tile_distance(int ax, int az, int bx, int bz)
{
    int dx = abs_of(ax - bx);
    int dz = abs_of(az - bz);

    return dx > dz ? dx : dz;
}

/*
 * The npc's record, and never a number instead of one.
 *
 * `ToriRSServer_WorldNpcSpawn` always fills `def` in — a spawn nothing describes
 * gets `ToriRSServer_ContentNpcDefault()` rather than NULL — so the
 * `npc->def ? … : <constant>` this replaces was a branch that could not be
 * taken, and each of those constants was a rate stated in C beside a content
 * file already stating it. Going through here means a tick count has exactly one
 * source: the record, whose own last resort is the `[default]` block in
 * `general/configs/npc_default.npc`.
 */
static const struct ToriRSServerNpcDef*
npc_def(const struct ToriRSServerNpc* npc)
{
    return npc->def ? npc->def : ToriRSServer_ContentNpcDefault();
}

/*
 * The gap between the npc's FOOTPRINT and the player's tile, per axis.
 *
 * Zero on an axis the two overlap on, otherwise the number of tiles between
 * the near edges. `npc->x/npc->z` is the south-west anchor of a `size x size`
 * square, not the whole npc, and everything in combat that measured the anchor
 * was measuring a 1x1 npc that happens to be most of the roster.
 *
 * It is not most of the roster. A cow and a unicorn are 2x2: pressed against
 * the player's tile on their east face they are one tile away and their anchor
 * is two, so a reach test on the anchor said "not in range" while the two were
 * touching. The npc then pursued a player it was already standing against,
 * the pathfinder (which *is* footprint-aware) answered with a step around the
 * perimeter, and the fight never started — the only way to satisfy the anchor
 * test was to walk ON TOP of the player, which `npc_travel_extra`'s PLAYER_OCC
 * bit correctly forbids. Every size>1 npc in the game retaliated, faced the
 * player, followed them around and never landed a blow.
 *
 * The reference's `CoordGrid.distanceTo` is this, and `npc_player_distance` in
 * torirs_server_world.c is the same arithmetic for the mode machine's range tests.
 */
static void
npc_player_gap(
    const struct ToriRSServerPlayer* player,
    const struct ToriRSServerNpc* npc,
    int* out_dx,
    int* out_dz)
{
    int size = npc->size > 0 ? npc->size : 1;
    int dx = 0;
    int dz = 0;

    if( npc->x > player->x )
        dx = npc->x - player->x;
    else if( player->x > npc->x + size - 1 )
        dx = player->x - (npc->x + size - 1);
    if( npc->z > player->z )
        dz = npc->z - player->z;
    else if( player->z > npc->z + size - 1 )
        dz = player->z - (npc->z + size - 1);
    *out_dx = dx;
    *out_dz = dz;
}

/*
 * Melee squares up: a diagonal is NOT in range.
 *
 * OldSchool melee requires orthogonal adjacency. Two entities standing corner
 * to corner cannot hit each other — they shuffle onto a shared row or column
 * first, which is the "squaring up" every fight starts with. Chebyshev
 * distance (a diagonal costs the same as a straight step) is the right metric
 * for *walking* and the wrong one for *reach*, and using it here let fights
 * happen corner to corner and never square up.
 *
 * Stated on the footprint gap, both axes zero means the two overlap, so
 * `(dx + dz) == 1` is "sharing an edge" for rectangles exactly as it was
 * "orthogonally adjacent" for two tiles — a corner touch is 1,1 and still
 * fails, and an overlap is 0,0 and fails too.
 *
 * Only melee is orthogonal. A ranged or magic attacker with `attackrange > 1`
 * uses the diagonal-permitting distance, which is why the two cases split here
 * rather than in the caller.
 *
 * `range` is the *attacker's* reach: the player's worn `weapon_attackrange`
 * (cache param 13) on the player swing path, the npc's `attackrange` on the
 * npc path. Using the npc's reach for both sides left bows stuck at melee
 * adjacency.
 */
static int
in_attack_range_with(
    const struct ToriRSServerPlayer* player,
    const struct ToriRSServerNpc* npc,
    int range)
{
    int dx;
    int dz;

    if( player->level != npc->level )
        return 0;

    npc_player_gap(player, npc, &dx, &dz);

    if( range <= 1 )
        return (dx + dz) == 1;

    return (dx > dz ? dx : dz) <= range;
}

/** Player weapon reach from the cache's `weapon_attackrange` param, capped at
 *  10 the way LostCity's `~player_attackrange` is. Unarmed / missing = 1. */
static int
player_weapon_attackrange(const struct ToriRSServerPlayer* player)
{
    int weapon = player->worn[TORIRSSERVER_WEAR_WEAPON].obj_id;
    int param_id;
    const struct ToriRSServerObjParam* p;
    int range = 1;

    if( weapon < 0 )
        return 1;
    param_id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_PARAM, "weapon_attackrange");
    if( param_id < 0 )
        return 1;
    p = ToriRSServer_ObjParam(weapon, param_id);
    if( p && p->ival > 0 )
        range = p->ival;
    if( range > 10 )
        range = 10;
    return range;
}

static int
in_player_attack_range(
    const struct ToriRSServerPlayer* player,
    const struct ToriRSServerNpc* npc)
{
    return in_attack_range_with(player, npc, player_weapon_attackrange(player));
}

static int
in_npc_attack_range(
    const struct ToriRSServerPlayer* player,
    const struct ToriRSServerNpc* npc)
{
    return in_attack_range_with(player, npc, npc_def(npc)->attackrange);
}

/*
 * The same two tests for a target that is another npc.
 *
 * Split out rather than generalised over a union type: `npc_player_gap` reads a
 * player as a 1x1 footprint and there is nowhere in it to put the second size,
 * so a shared version would have to take four rectangle arguments and every
 * player caller would pass two constants. The rule is identical — the gap
 * between the two footprints, orthogonal for melee and Chebyshev for anything
 * with reach.
 */
static void
npc_npc_gap(
    const struct ToriRSServerNpc* a,
    const struct ToriRSServerNpc* b,
    int* out_dx,
    int* out_dz)
{
    /* One extent each: an npc's footprint is `size` x `size`, so the same
     * number bounds both axes. */
    int a_size = a->size > 0 ? a->size : 1;
    int b_size = b->size > 0 ? b->size : 1;
    int dx = 0;
    int dz = 0;

    if( b->x > a->x + a_size - 1 )
        dx = b->x - (a->x + a_size - 1);
    else if( a->x > b->x + b_size - 1 )
        dx = a->x - (b->x + b_size - 1);
    if( b->z > a->z + a_size - 1 )
        dz = b->z - (a->z + a_size - 1);
    else if( a->z > b->z + b_size - 1 )
        dz = a->z - (b->z + b_size - 1);
    *out_dx = dx;
    *out_dz = dz;
}

static int
in_npc_attack_range_npc(
    const struct ToriRSServerNpc* attacker,
    const struct ToriRSServerNpc* target)
{
    int range = npc_def(attacker)->attackrange;
    int dx;
    int dz;

    if( attacker->level != target->level )
        return 0;
    npc_npc_gap(attacker, target, &dx, &dz);
    if( range <= 1 )
        return (dx + dz) == 1;
    return (dx > dz ? dx : dz) <= range;
}

/*
 * The priority gate — the reference's `PathingEntity.playAnimation`.
 *
 * `incumbent` is whatever has already been queued for this tick (-1 if
 * nothing), `wanted` is the new sequence. The rule is `>=`, not `>`: two
 * animations of equal priority mean the later one wins, which is what makes a
 * repeated swing re-trigger rather than stick on its first frame.
 *
 * The header on ToriRSServer_AnimPlayNpc has the whole of why this exists.
 */
static int
anim_wins(int incumbent, int wanted)
{
    if( incumbent < 0 )
        return 1;
    return ToriRSServer_SeqPriority(wanted) >= ToriRSServer_SeqPriority(incumbent);
}

int
ToriRSServer_AnimPlayNpc(
    struct ToriRSServerNpc* npc,
    int seq_id,
    int delay)
{
    /* -1 means the content named nothing and the cache had no convention
     * match. Sending it would spell 65535 on the wire, which tells the client
     * to STOP whatever is playing — worse than sending nothing at all.
     *
     * The reference does send -1 (it is how a script cancels an animation), so
     * this is a deliberate difference and not an oversight: here -1 only ever
     * arrives as "unresolved", never as "cancel", because every caller that
     * means cancel has an id to send instead. */
    if( seq_id < 0 )
        return 0;
    if( getenv("TORIRS_ANIM_DEBUG") )
        fprintf(
            stderr,
            "srv: npc_anim npc=%p seq=%d delay=%d (was playing %d)\n",
            (void*)npc,
            seq_id,
            delay,
            npc->anim_id);
    if( !anim_wins(npc->anim_id, seq_id) )
        return 0;
    npc->anim_id = seq_id;
    npc->anim_delay = delay;
    npc->masks |= TORIRSSERVER_NMASK_ANIM;
    /* Whoever played the death seq, it has now been played this life — see the
     * field. A script that shows a death and the engine's own death step must
     * agree on that, or the client is sent it twice and shows it once. */
    if( seq_id == npc->death_seq && npc->death_seq >= 0 )
    {
        npc->death_seq_sent = 1;
        /* Played by a SCRIPT on a living npc: the engine's own death step only
         * reaches here with `death_tick` already armed. Recorded so phase
         * cleanup can check the script also made it dead. */
        if( npc->hitpoints > 0 && npc->death_tick < 0 )
            npc->scripted_death_pending = 2;
    }
    return 1;
}

int
ToriRSServer_AnimPlayPlayer(
    struct ToriRSServerPlayer* player,
    int seq_id,
    int delay)
{
    if( seq_id < 0 )
        return 0;
    if( !anim_wins(player->anim_id, seq_id) )
        return 0;
    player->anim_id = seq_id;
    player->anim_delay = delay;
    player->masks |= TORIRSSERVER_PMASK_SEQUENCE;
    return 1;
}

static void
play_npc_seq(struct ToriRSServerNpc* npc, int seq_id)
{
    ToriRSServer_AnimPlayNpc(npc, seq_id, 0);
}

/*
 * How far an npc's combat noise carries, in tiles.
 *
 * LostCity's `[proc,npc_death]` passes 12 to `~sound_within_distance` with an
 * `// osrs` note, and it is the only figure any reference states for this, so
 * the same number answers for all three sounds rather than three invented ones.
 */
#define TORIRSSERVER_NPC_SOUND_TILES 12

/*
 * An npc's own sound, to everyone near enough to hear it.
 *
 * `SYNTH_SOUND` is a per-player packet, so "a noise happened at this tile" has
 * to be spelled as a loop. LostCity spells it as a per-player proc
 * (`~sound_within_distance`) which only ever runs for whoever triggered it —
 * fine there, because every one of its call sites is inside a player's own
 * script. This site is not: an npc flinches and dies inside the engine, and a
 * second player standing next to the fight should hear it too. Broadcasting is
 * the same rule stated from the emitter's side instead of the listener's.
 *
 * Silence is `-1`, and it is the common case. The guard is here as well as in
 * `SS_OP_SOUND_SYNTH` because this path never goes through the opcode.
 */
static void
npc_sound_nearby(
    struct ToriRSServer* srv,
    struct ToriRSServerNpc const* npc,
    int sound_id,
    int delay)
{
    if( sound_id < 0 )
        return;
    for( int i = 0; i < srv->player_count; i++ )
    {
        struct ToriRSServerPlayer* player = &srv->players[i];

        if( !player->active || player->level != npc->level )
            continue;
        if( tile_distance(player->x, player->z, npc->x, npc->z) >
            TORIRSSERVER_NPC_SOUND_TILES )
            continue;
        /* One loop, not zero, and this is checkable rather than a preference:
         * `RS_Audio_QueueEffect` (src/game/rs_audio.c) *refuses* `loops == 0`,
         * matching the reference's `Message.queueSoundEffect` requiring
         * `var1 != 0` — the count it hands the mixer is `loops - 1`, so zero
         * means the caller asked for nothing. LostCity's own
         * `~sound_within_distance` passes 0 and would be silent here; every
         * direct `sound_synth` call site in both trees passes 1. */
        ToriRSServer_SendSynthSound(player, sound_id, 1, delay);
    }
}


static int
hitsplat_block(void)
{
    int id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_HITSPLAT, "hitsplat_block");

    return id >= 0 ? id : 26;
}

static int
hitsplat_poison(void)
{
    int id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_HITSPLAT, "hitsplat_poison");

    /* rev-230's poison hitsplat id. The fallback keeps a reduced content pack
     * observable rather than converting poison into an ordinary damage splat. */
    return id >= 0 ? id : 7;
}

static int
hitsplat_shield(void)
{
    int id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_HITSPLAT, "hitsplat_shield");

    /* The splat OldSchool draws for a hit the Nightmare Zone's absorption pool
     * ate. A pack without it falls back to a block splat, which reads as "no
     * damage" — wrong in colour, right in meaning. */
    return id >= 0 ? id : hitsplat_block();
}

/* ------------------------------------------------------------------ */
/* Which splat a VIEWER is sent                                        */
/* ------------------------------------------------------------------ */

/*
 * Content names a family; the cache carries a WRAPPER over each family that
 * asks the viewer's own settings, and the wrapper is what has to go on the
 * wire.
 *
 * `cache.osrs239`'s hitsplat table is 34 selector records over 49 appearance
 * records. A selector carries opcode 17/18 -- a varbit, a varp and a fallback,
 * then ids indexed by the var's value -- and every one of them is keyed on one
 * of exactly two varbits, both All Settings rows:
 *
 *   10236 `hitsplat_tint_disabled`    setting 5   "Hitsplat tinting"
 *   14196 `hitsplat_maxhit_disabled`  setting 279 "Max hit hitsplats"
 *
 * The tinting selectors come in me/other pairs. The "me" member resolves to the
 * same leaf whichever way the setting is set (your own damage was never the
 * thing being tinted); the "other" member resolves to the tinted leaf when
 * tinting is on and to the plain one when it is off. So sending the LEAF --
 * which is what this server did, for every hit in the game -- silently answers
 * setting 5 "off" on the player's behalf, and there is no way for them to
 * notice: the splat is a perfectly ordinary splat.
 *
 * Reading the setting is deliberately NOT done here. The client resolves the
 * wrapper against its own varbits at draw time, which is where the reference
 * resolves it, and it is why a player toggling the row re-skins the splats
 * already on screen. The one thing this has to decide is which QUESTION to ask.
 */
struct hitsplat_family
{
    char const* leaf;
    char const* me;
    char const* other;
    /** The max-hit wrapper for this family, or NULL where the cache has none. */
    char const* max_me;
};

/*
 * The four families content actually names. Every name is looked up in the
 * content pack rather than written as an id, so a cache whose table is numbered
 * differently needs no change here -- and a pack that has not named a wrapper
 * yet leaves that family exactly as it was.
 */
static const struct hitsplat_family HITSPLAT_FAMILIES[] = {
    { "hitsplat_damage", "hitsplat_damage_me", "hitsplat_damage_other",
      "hitsplat_damage_max_me" },
    { "hitsplat_block", "hitsplat_block_me", "hitsplat_block_other", NULL },
    { "hitsplat_poison", "hitsplat_poison_me", "hitsplat_poison_other", NULL },
    { "hitsplat_shield", "hitsplat_shield_me", "hitsplat_shield_other",
      "hitsplat_shield_max_me" },
};

/** A pack symbol, or -1. Wrapped so the family walk reads as a lookup. */
static int
hitsplat_symbol(char const* name)
{
    return name ? ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_HITSPLAT, name) : -1;
}

/**
 * The family `type` belongs to, or NULL.
 *
 * Matches the leaf AND both wrappers, so a script that already states
 * `hitsplat_damage_other` is understood rather than silently re-promoted to the
 * viewer's own answer.
 */
static const struct hitsplat_family*
hitsplat_family_of(int type)
{
    size_t const n = sizeof(HITSPLAT_FAMILIES) / sizeof(HITSPLAT_FAMILIES[0]);

    for( size_t i = 0; i < n; i++ )
    {
        const struct hitsplat_family* f = &HITSPLAT_FAMILIES[i];

        if( type == hitsplat_symbol(f->leaf) || type == hitsplat_symbol(f->me) ||
            type == hitsplat_symbol(f->other) || type == hitsplat_symbol(f->max_me) )
            return f;
    }
    return NULL;
}

/**
 * Was this the dealer's own maximum, and does it clear their threshold?
 *
 * `%com_maxhit` is written by `[proc,player_combat_stat]` -- the max hit is a
 * game-design calculation and lives in content, which is why this reads a varp
 * instead of computing one. A dealer whose max hit has never been computed
 * reads 0, and 0 can never equal a damage that got here, so a server with no
 * combat scripts loaded simply never promotes.
 *
 * Setting 280's threshold (varbit 14195) is the other half, and it is the one
 * thing about these two rows a var selector cannot express: it is a comparison
 * against the damage, not a lookup. Nine bits, so 0..511.
 */
static int
hitsplat_is_max_hit(
    const struct ToriRSServer* srv,
    const struct ToriRSServerPlayer* dealer,
    int damage)
{
    const struct ToriRSServerIds* ids = ToriRSServer_Ids();
    int max_hit;
    int threshold;

    (void)srv;
    if( !dealer || damage <= 0 || !ids )
        return 0;
    if( ids->varp_com_maxhit < 0 )
        return 0;

    if( ids->varp_com_maxhit >= TORIRSSERVER_VARP_COUNT )
        return 0;
    max_hit = dealer->varps[ids->varp_com_maxhit];
    if( max_hit <= 0 || damage != max_hit )
        return 0;

    threshold = ids->varbit_hitsplat_threshold >= 0
                    ? ToriRSServer_VarbitGet(dealer, ids->varbit_hitsplat_threshold)
                    : 0;
    return damage >= threshold;
}

/* ------------------------------------------------------------------ */
/* The ironman loot restriction (settings 182 / 183)                   */
/* ------------------------------------------------------------------ */

int
ToriRSServer_NpcLootRestrictedFor(
    const struct ToriRSServer* srv,
    const struct ToriRSServerNpc* npc,
    const struct ToriRSServerPlayer* player)
{
    const struct ToriRSServerIds* ids = ToriRSServer_Ids();

    assert(srv);
    assert(npc);
    assert(player);

    if( !ids || ids->varbit_ironman < 0 )
        return 0;
    /* Not an iron account: both rows are Ironman-only by their own wording, and
     * a warning shown to a main is a warning about nothing. */
    if( ToriRSServer_VarbitGet(player, ids->varbit_ironman) == 0 )
        return 0;

    for( int i = 0; i < TORIRSSERVER_PLAYER_MAX; i++ )
    {
        if( !npc->damaged_by_players[i] )
            continue;
        if( player->pid >= 0 && i == player->pid )
            continue;
        return 1;
    }
    return 0;
}

/*
 * Setting 183, "Iron loot restriction messages".
 *
 * The row's own word is "occasionally", and it is load-bearing rather than
 * vague: this is called from the damage path, which runs every time a swing
 * lands, and a line per hit would be a wall of text over a fight nobody could
 * read past. One warning per npc per player is what "occasionally" has to mean
 * here -- the player is told once about this creature and not again.
 *
 * Setting 182's indicator ICON is deliberately not implemented beside it. The
 * cache carries no asset for it (no sprite, spotanim or interface names
 * anything loot-restriction shaped), no clientscript reads varbit 13039, and the
 * NXT decompilation has no class for it either -- so there is nothing to draw
 * and no evidence about what it should look like. Its varbit is registered and
 * this rule answers it; picking a sprite would be the kind of guess that makes
 * a helper confidently wrong. See docs/NXT_ACTIVITIES_BUCKET_C.md.
 */
static void
ToriRSServer_LootRestrictionWarn(
    struct ToriRSServer* srv,
    struct ToriRSServerNpc* npc,
    struct ToriRSServerPlayer* player)
{
    const struct ToriRSServerIds* ids = ToriRSServer_Ids();

    if( !ids || ids->varbit_iron_noloot_message_off < 0 )
        return;
    if( !player->active || player->pid < 0 || player->pid >= TORIRSSERVER_PLAYER_MAX )
        return;
    /* Inverted, as the gameval name says: 1 is OFF. */
    if( ToriRSServer_VarbitGet(player, ids->varbit_iron_noloot_message_off) )
        return;
    if( npc->noloot_warned_players[player->pid] )
        return;
    if( !ToriRSServer_NpcLootRestrictedFor(srv, npc, player) )
        return;

    npc->noloot_warned_players[player->pid] = 1;
    ToriRSServer_SendMessage(
        player, "Another player has damaged this creature - you will not receive any loot.");
}

int
ToriRSServer_HitsplatForViewer(
    const struct ToriRSServer* srv,
    const struct ToriRSServerPlayer* viewer,
    int type,
    int damage,
    int dealer_slot)
{
    const struct hitsplat_family* family = hitsplat_family_of(type);
    const struct ToriRSServerPlayer* dealer = NULL;
    int viewer_is_dealer;
    int promoted;

    assert(srv);
    assert(viewer);
    if( !family )
        return type;

    if( dealer_slot >= 0 && dealer_slot < TORIRSSERVER_PLAYER_MAX &&
        srv->players[dealer_slot].active )
        dealer = &srv->players[dealer_slot];

    /*
     * Damage nobody owns is NOT "somebody else's".
     *
     * Poison ticking, a trap, an npc hitting another npc: the row's sentence is
     * "damage that you did not deal", and tinting those would tint most of what
     * a player sees while standing still. The reference leaves unowned damage on
     * its own family's `me` member, which is the untinted one, and so does this.
     */
    viewer_is_dealer = (dealer == NULL) || (dealer == viewer);

    if( viewer_is_dealer && family->max_me && hitsplat_is_max_hit(srv, dealer, damage) )
    {
        promoted = hitsplat_symbol(family->max_me);
        if( promoted >= 0 )
            return promoted;
    }

    promoted = hitsplat_symbol(viewer_is_dealer ? family->me : family->other);
    if( getenv("TORIRSSERVER_SPLAT_DEBUG") )
        fprintf(stderr, "  SPLAT viewer=%s stated=%d dealer=%d -> %d\n",
                viewer->display_name, type, dealer_slot, promoted >= 0 ? promoted : type);
    /* A pack that has not named this family's wrappers leaves the splat exactly
     * as content asked for it, rather than dropping it. */
    return promoted >= 0 ? promoted : type;
}

/**
 * Nightmare Zone absorption. The pool lives in the cache varbit the client
 * already carries (`nzone_absorb_potion_effects`, ten bits, so 0..1023 holds
 * the wiki's 1000 cap), rather than in a field here, because it is the potion's
 * state and the potion is content — the engine only spends it, because spending
 * it means reaching into the one place player hitpoints go down.
 *
 * Returns the damage left after the pool has taken what it can, and writes the
 * pool back. Poison and venom are excluded by their splat type and self-damage
 * by the caller's flag, both per the wiki.
 */
static int
absorb_player_damage(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int type, int amount)
{
    int varbit_id;
    int pool;
    int soaked;

    assert(srv);
    assert(player);

    if( amount <= 0 )
        return amount;
    if( player->hit_self_inflicted )
        return amount;
    if( type == hitsplat_poison() )
        return amount;

    varbit_id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "nzone_absorb_potion_effects");
    if( varbit_id < 0 )
        return amount;
    pool = ToriRSServer_VarbitGet(player, varbit_id);
    if( pool <= 0 )
        return amount;

    soaked = pool < amount ? pool : amount;
    ToriRSServer_VarbitSetOn(srv, player, varbit_id, pool - soaked);
    return amount - soaked;
}


/* ------------------------------------------------------------------ */
/* Disengaging                                                         */
/* ------------------------------------------------------------------ */

/*
 * FACE_ENTITY is a latch, and dropping a target does NOT clear it here.
 *
 * The client turns the entity toward whatever the last FACE_ENTITY named and
 * keeps it there — there is no timeout and no implicit clear — so the release
 * still has to be sent. What matters is *when*. In LostCity
 * `PathingEntity.setFaceEntity` is the only writer of `faceEntity` in the whole
 * engine: it is derived from the current target once per turn, at a fixed point
 * in `processPlayers`, and `clearInteraction` deliberately leaves the field
 * alone. `ToriRSServer_PlayerSetFaceEntity` is that function, and phase_player
 * calls it in that same slot — before the interaction, before the swing.
 *
 * Clearing the latch here as well made the derived value unreachable for one
 * whole tick, and that tick is the interesting one. A kill in a single blow
 * runs `set_face_entity` (latch := the npc) at the top of the turn and this
 * function (latch := -1) at the bottom of the *same* turn, and the mask is only
 * flushed once, after both. The client received nothing but the release: the
 * player killed the goblin without ever turning to look at it. Every fight
 * lasting two or more ticks hid it, because the first tick shipped the latch.
 *
 * So the release now lands on the next turn's `set_face_entity`, which runs
 * unconditionally — locked, dying or idle — for exactly this reason.
 */
void
ToriRSServer_CombatStopPlayerAt(struct ToriRSServerPlayer* player)
{
    /* LostCity PathingEntity.clearInteraction clears target and targetOp as
     * one operation.  `combat_target` is the first half; the OPNPC/p_opnpc
     * interaction is the second.  Leaving it armed lets a death resume the
     * combat script on a later tick and acquire the same target again. */
    player->combat_target = -1;
    ToriRSServer_WorldInteractionClearAt(player);
}

void
ToriRSServer_CombatStopPlayer(struct ToriRSServer* srv)
{
    ToriRSServer_CombatStopPlayerAt(srv->active_player);
}

void
ToriRSServer_CombatStopNpc(
    struct ToriRSServer* srv,
    int slot)
{
    struct ToriRSServerNpc* npc;

    if( slot < 0 || slot >= TORIRSSERVER_NPC_MAX )
        return;
    npc = &srv->npcs[slot];
    npc->combat_target = -1;
    npc->combat_target_npc = -1;
    npc->combat_target_npc_gen = 0;
    if( npc->face_entity != -1 )
    {
        npc->face_entity = -1;
        npc->masks |= TORIRSSERVER_NMASK_FACE_ENTITY;
    }
}

/* ------------------------------------------------------------------ */
/* Stats                                                               */
/* ------------------------------------------------------------------ */

void
ToriRSServer_CombatStatMark(
    struct ToriRSServerPlayer* player,
    int stat)
{
    if( stat >= 0 && stat < TORIRSSERVER_STAT_COUNT )
        player->stat_dirty |= 1u << stat;
}

/*
 * The hitpoints stat and the player's hitpoints are the same number in two
 * places, and they have to stay that way: the health orb reads the stat, the
 * hitsplat's health bar reads the DAMAGE mask. Keeping them in one function
 * rather than assigning both at every call site is the only reason they cannot
 * drift.
 */
void
ToriRSServer_CombatSyncHitpoints(struct ToriRSServerPlayer* player)
{
    player->max_hitpoints = player->stat_level[TORIRSSERVER_STAT_HITPOINTS];
    if( player->max_hitpoints <= 0 )
        player->max_hitpoints = 1;
    if( player->hitpoints > player->max_hitpoints )
        player->hitpoints = player->max_hitpoints;
    player->stat_boosted[TORIRSSERVER_STAT_HITPOINTS] = player->hitpoints;
    ToriRSServer_CombatStatMark(player, TORIRSSERVER_STAT_HITPOINTS);
}

/*
 * The OldSchool experience table.
 *
 * Built rather than transcribed: the experience needed for level L is
 * `floor(sum(floor(i + 300 * 2^(i/7))) / 4)` over i in 1..L-1. Ninety-eight
 * numbers nobody would proofread, derived from one line that can be checked
 * against the published formula — 83 for level 2, 13,034,431 for level 99.
 *
 * **Each term is floored before it is summed.** That is not a detail: this
 * accumulated the raw doubles and floored only after the divide, which puts 94
 * of the 98 thresholds exactly one xp too high — level 10 at 1155 against the
 * reference's 1154 (`Player.ts:79-87`, `Math.floor(level + Math.pow(2, level/7)
 * * 300)` inside the loop).
 *
 * It survived that long because nothing ever asked the table a question it
 * could get wrong. The one caller that would have — a new character's hitpoints
 * — used to be two literals in `ToriRSServer_WorldInit` stating the level *and* the
 * xp independently, so the table was never consulted and the two could not
 * disagree. Moving the seed into content, where only the xp is stated and the
 * level has to follow from it, is what turned the discrepancy into a failure.
 *
 * `table[i]` is the experience at which level `i + 2` begins — the reference's
 * indexing (`getExpByLevel(level)` reads `[level - 2]`).
 */
static int g_xp_table[99];
static int g_xp_table_built;

static void
ensure_xp_table(void)
{
    long long accumulated;

    if( g_xp_table_built )
        return;
    accumulated = 0;
    for( int i = 0; i < 99; i++ )
    {
        int level = i + 1;

        accumulated +=
            (long long)floor((double)level + pow(2.0, (double)level / 7.0) * 300.0);
        g_xp_table[i] = (int)(accumulated / 4);
    }
    g_xp_table_built = 1;
}

int
ToriRSServer_CombatLevelForXp(int experience)
{
    ensure_xp_table();
    for( int i = 98; i >= 0; i-- )
    {
        if( experience >= g_xp_table[i] )
            return i + 2 < 99 ? i + 2 : 99;
    }
    return 1;
}

int
ToriRSServer_CombatXpForLevel(int level)
{
    if( level <= 1 )
        return 0;
    if( level > 99 )
        level = 99;
    ensure_xp_table();
    return g_xp_table[level - 2];
}

void
ToriRSServer_CombatSetLevel(
    struct ToriRSServerPlayer* player,
    int stat,
    int level)
{
    assert(player);
    assert(stat >= 0 && stat < TORIRSSERVER_STAT_COUNT);
    if( level < 1 )
        level = 1;
    if( level > 99 )
        level = 99;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
    player->stat_xp_tenths[stat] = ToriRSServer_CombatXpForLevel(level) * 10;
    if( stat == TORIRSSERVER_STAT_HITPOINTS )
    {
        player->hitpoints = level;
        ToriRSServer_CombatSyncHitpoints(player);
    }
    ToriRSServer_CombatStatMark(player, stat);
}

int
ToriRSServer_CombatClampXp(long long tenths)
{
    if( tenths < 0 )
        return 0;
    if( tenths > TORIRSSERVER_XP_MAX_TENTHS )
        return TORIRSSERVER_XP_MAX_TENTHS;
    return (int)tenths;
}

void
ToriRSServer_CombatAddXp(
    struct ToriRSServer* srv,
    int stat,
    int tenths)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    int before;

    if( stat < 0 || stat >= TORIRSSERVER_STAT_COUNT || tenths == 0 )
        return;
    before = player->stat_level[stat];
    /* Traced because an xp *rate* bug is otherwise unobservable: the on-screen
     * counter shows the seeded total, UPDATE_STAT carries the number but the
     * log prints only its payload length, and a wrong rate never changes a
     * level often enough to notice. Tenths, so 200 reads as 20.0 xp. */
    if( srv->verbose )
    {
        /* Widened before the sign is taken: `abs(INT_MIN)` has no answer in an
         * `int`, and the trace must not be the one thing in here that a hostile
         * amount can break. */
        long long magnitude = llabs((long long)tenths);

        printf("torirsserver: xp stat=%d %c%lld.%lld (tenths=%d)\n",
               stat,
               tenths < 0 ? '-' : '+',
               magnitude / 10,
               magnitude % 10,
               tenths);
    }
    /* Summed in 64 bits and clamped, never accumulated in place: the total is
     * an `int` and TORIRSSERVER_XP_MAX_TENTHS is most of its range, so `+=` at the
     * ceiling overflows rather than saturating. */
    player->stat_xp_tenths[stat] =
        ToriRSServer_CombatClampXp((long long)player->stat_xp_tenths[stat] + tenths);
    player->stat_level[stat] =
        ToriRSServer_CombatLevelForXp(player->stat_xp_tenths[stat] / 10);
    if( player->stat_level[stat] != before )
    {
        /* A hitpoints level-up raises the ceiling but does not heal, which is
         * what OldSchool does and is also the only behaviour that cannot
         * surprise someone mid-fight. A level *loss* does the same work in
         * reverse, and `sync_hitpoints` clamps current hitpoints to the new
         * ceiling itself. */
        if( stat == TORIRSSERVER_STAT_HITPOINTS )
            ToriRSServer_CombatSyncHitpoints(player);
        /* Only upward: `advancestat` is the level-up trigger, and content hangs
         * the fanfare interface off it. Losing a level is not an advance. */
        if( player->stat_level[stat] > before )
            ToriRSServer_ScriptsRunTriggerSpecific(srv, SS_TRIGGER_ADVANCESTAT, stat, -1, -1);
    }
    if( stat != TORIRSSERVER_STAT_HITPOINTS && stat != TORIRSSERVER_STAT_SUMMONING )
    {
        /* Boosted follows base upward so a level-up is usable at once, and back
         * down only when the base actually fell beneath it — a boost above a
         * base the player no longer has is power the experience no longer pays
         * for, but clamping unconditionally would cancel a potion on every xp
         * drop. Hitpoints is exempt because its boosted slot is current
         * hitpoints, which `sync_hitpoints` owns.
         *
         * Summoning is exempt for the same reason hitpoints is: its boosted slot
         * is not a boost, it is the *points pool*. Summoning experience is earned
         * by infusing pouches and by every special move
         * (`~summoning_familiar_special_commit`'s `stat_advance`), so the
         * upward snap here was silently refilling the pool the familiar tick had
         * just been draining — a special spent points and handed them straight
         * back. Points now rise only where something restores them on purpose
         * (the obelisk's op2, `::summoning_points`); a level-up raises the
         * ceiling and leaves the current pool where it stands, which is what
         * the live game does. */
        if( player->stat_boosted[stat] < player->stat_level[stat] )
            player->stat_boosted[stat] = player->stat_level[stat];
        else if( player->stat_level[stat] < before &&
                 player->stat_boosted[stat] > player->stat_level[stat] )
            player->stat_boosted[stat] = player->stat_level[stat];
    }
    else if( stat == TORIRSSERVER_STAT_SUMMONING &&
             player->stat_boosted[stat] > player->stat_level[stat] )
    {
        /* The one direction that must still track: the pool cannot exceed the
         * ceiling, so a lost level takes the surplus points with it. */
        player->stat_boosted[stat] = player->stat_level[stat];
    }
    ToriRSServer_CombatStatMark(player, stat);
}

/* ------------------------------------------------------------------ */
/* Rolls                                                               */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* The player's combat stat block — content's, read here                */
/* ------------------------------------------------------------------ */

/*
 * Everything about the player's side of a roll comes out of `%com_*`.
 *
 * `[proc,player_combat_stat]` (skill_combat/combat_stats.rs2) computes the
 * whole block — attack and defence rolls per damage type, the max hit, the
 * damage type and style, the anims — and this file reads it. That is the
 * reference's arrangement, and the reason for it is not tidiness: the rules in
 * that calculation are game design, and each one that lives here instead is a
 * rule a server operator cannot change and that drifts from the reference
 * silently.
 *
 * The four functions this replaces — `style_bonus`, `player_bonus`,
 * `player_damage_type`, `player_effective` — are the evidence. Between them
 * they had no prayer multiplier at all, so every Attack, Strength and Defence
 * prayer in the game was inert; nothing reported it, because there was no
 * missing call, only a missing term.
 *
 * What stays here is the roll itself: `roll_hit` compares two numbers, and
 * `max_hit`/`npc_effective` still serve the npc side, which has no varps of its
 * own (an npc's profile is params on its record, read straight off `npc->def`).
 */








/* ------------------------------------------------------------------ */
/* Applying damage                                                     */
/* ------------------------------------------------------------------ */

/*
 * Append one splat to an entity's per-tick list.
 *
 * Append rather than assign, which is the whole fix: two attackers landing on
 * the same tick are two hitsplats, and the scalar pair this replaces could only
 * remember the last of them. See `struct ToriRSServerHitmark`.
 *
 * Past the client's four slots the splat is dropped, and dropped *silently* on
 * purpose — `World_EntityAddHitmark` does exactly the same thing at the other
 * end when every slot is still live, so a fifth simultaneous hit has nowhere to
 * be drawn whatever this does. The damage itself has already been applied by
 * the caller; only the number over the head is lost.
 */
static void
ToriRSServer_HitmarkAdd(
    struct ToriRSServerHitmark* hitmarks,
    int* count,
    int damage,
    int type,
    int dealer_slot)
{
    if( *count >= TORIRSSERVER_HITMARK_MAX )
        return;
    hitmarks[*count].damage = damage;
    hitmarks[*count].type = type;
    hitmarks[*count].dealer_slot = dealer_slot;
    (*count)++;
}

/**
 * Whose damage this is, for the splat each viewer eventually gets.
 *
 * **Only correct for damage aimed at an NPC**, and that asymmetry is the whole
 * reason this is a named function rather than an inline expression.
 * `srv->active_player` is the player the world is currently running on, and
 * which end of the fight that is depends on the call:
 *
 *   - `CombatHitNpc` runs inside the ATTACKER's script, so the active player
 *     dealt the hit. That is what this answers.
 *   - `CombatHitPlayer` runs with the active player as the VICTIM -- the npc
 *     attack scripts take the target as their active player. Using this there
 *     would record every npc's hit on you as your own damage, which is not
 *     merely wrong, it is wrong in the direction that makes setting 5 look
 *     implemented while tinting nothing.
 *
 * So the player-victim paths pass -1 (unowned) explicitly instead of calling
 * this, and say so at the call site.
 */
static int
ToriRSServer_HitmarkDealerFromAttackerScript(const struct ToriRSServer* srv)
{
    if( !srv || !srv->active_player )
        return -1;
    return (int)(srv->active_player - &srv->players[0]);
}

void
ToriRSServer_CombatHitNpc(
    struct ToriRSServer* srv,
    int slot,
    int type,
    int amount)
{
    struct ToriRSServerNpc* npc;
    int immutable_target;

    if( slot < 0 || slot >= TORIRSSERVER_NPC_MAX )
        return;
    npc = &srv->npcs[slot];
    if( !npc->active || npc->death_tick >= 0 )
        return;
    /*
     * Already dead by content's hand, not yet by the engine's.
     *
     * `[proc,npc_default_damage]` opens with `if (npc_stat(hitpoints) = 0)
     * return;` — the reference drops a hit on an npc whose bar is empty, and
     * every player swing reaches it through `[ai_queue2,_]`. But `npc_damage`
     * from a script, a poison tick and this function's other callers do not
     * pass through that proc, and a script that zeroes hitpoints itself
     * (`npc_statsub(hitpoints, 0, 100)` — the ToB Matomenos absorbed at the
     * Maiden, Zenyte's `setHitpoints(0)`) has said "this one is dead" without
     * arming `death_tick`. A hit landing in that window used to draw a block
     * splat, read `hitpoints == 0` below and arm a SECOND death on an npc
     * already playing its first one: two `elemental_death` sends, and the
     * client — correctly, per replyMode — does not restart a seq it is
     * already on, so the corpse held its last frame and read as vanishing.
     *
     * Same rule as the content's, stated once at the engine seam all callers
     * share. Not a silent no-op for a caller bug: a 0-hp npc that is not
     * dying is a state content has deliberately created.
     */
    if( npc->hitpoints == 0 )
        return;

    /* The cache reserves category 981 for the nine attached POH combat
     * dummies. They are damage meters: a landed attack must still emit its
     * hitsplat, but cannot consume hitpoints, die, retaliate, or enter the
     * ordinary loot/respawn lifecycle.
     * https://oldschool.runescape.wiki/w/Combat_dummy
     * https://oldschool.runescape.wiki/w/Ornate_undead_combat_dummy */
    /* Category 298 is likewise cache-exclusive: 195 POH combat-stone forms
     * (three materials x (destroyed + 4x4x4 crack states)). RuneScript owns
     * their independent stab/slash/crush durability and model changes; this
     * landing boundary supplies the same essential mechanism as for a dummy:
     * show the exact splat without consuming the NPC's ordinary HP lifecycle.
     * https://oldschool.runescape.wiki/w/Combat_stone_(NPC,_marble) */
    /* Category 610 contains the cache-native Elemental Balance projections.
     * RuneScript accepts only elemental combat spells and owns both balance
     * axes; ordinary melee/ranged attacks may show a blocked splat but must
     * never destroy, retaliate through, or award XP from the puzzle actor.
     * https://oldschool.runescape.wiki/w/Elemental_balance_space */
    immutable_target = ToriRSServer_NpcCategory(npc->type) == 981 ||
                       ToriRSServer_NpcCategory(npc->type) == 298 ||
                       ToriRSServer_NpcCategory(npc->type) == 610;

    if( amount > npc->hitpoints )
        amount = npc->hitpoints;
    if( !immutable_target )
        npc->hitpoints -= amount;

    /* One mask carries the splat and the bar. A zero-damage hit is a *block*
     * splat rather than nothing — the reference shows those, and without them a
     * miss is indistinguishable from the server having ignored the swing. */
    /*
     * Who has touched this npc, accumulated from the FIRST hit.
     *
     * Recorded here rather than at the killing blow, which is where
     * `death_credit_players` is filled, because settings 182 and 183 warn
     * *while the fight is going on* -- a restriction discovered at the drop is a
     * restriction discovered too late to do anything about.
     */
    {
        int const dealer = ToriRSServer_HitmarkDealerFromAttackerScript(srv);

        if( dealer >= 0 && dealer < TORIRSSERVER_PLAYER_MAX )
            npc->damaged_by_players[dealer] = 1;
    }

    ToriRSServer_HitmarkAdd(npc->hitmarks, &npc->hitmark_count, amount,
                        amount > 0 ? type : hitsplat_block(),
                        ToriRSServer_HitmarkDealerFromAttackerScript(srv));

    /* Warn every ironman fighting this npc, not just the one who swung: the
     * player who is about to lose the drop is the one who got there FIRST, and
     * they take no action of their own at the moment somebody else joins in. */
    for( int i = 0; i < TORIRSSERVER_PLAYER_MAX; i++ )
    {
        if( srv->players[i].active && srv->players[i].combat_target == slot )
            ToriRSServer_LootRestrictionWarn(srv, npc, &srv->players[i]);
    }
    npc->damage = npc->hitmarks[0].damage;
    npc->damage_type = npc->hitmarks[0].type;
    npc->hitpoints = npc->hitpoints < 0 ? 0 : npc->hitpoints;
    npc->max_hitpoints = npc->max_hitpoints > 0 ? npc->max_hitpoints : 1;
    npc->masks |= TORIRSSERVER_NMASK_DAMAGE;
    /* The classic (rev-230) mask has room for exactly two splats and the v5
     * block has room for four; this bit is what spends the classic second slot.
     * The v5 writer reads `hitmark_count` directly and ignores it. */
    if( npc->hitmark_count >= 2 )
        npc->masks |= TORIRSSERVER_NMASK_DAMAGE2;

    /* Retaliate. An npc that is hit fights back whatever its hunt mode says —
     * aggression decides who *starts* a fight, not who finishes one.
     *
     * Flinch: the retaliation delay is *half* the attack rate, not a full one.
     * The reference is npc_combat.rs2:11, `%npc_action_delay = add(map_clock,
     * divide(npc_param(attackrate), 2)) // flinch`.
     *
     * Charging a full attackrate here left attacker and defender permanently
     * co-phased: PLAYER_INFO and NPC_INFO carried their damage masks on the
     * same tick, every attackrate ticks, for the whole fight. Halving it once
     * on the opening hit is what staggers the two cadences apart.
     *
     * `retaliate=no` opts out, and the opt-out belongs here rather than in a
     * mode or a hunt setting: `combat_target` is what the npc phase reads to
     * decide that combat owns this npc's movement, so a latch taken here is
     * already the npc leaving whatever it was doing. Scenery with hitpoints —
     * the Inferno's Ancestral Glyph, which walks a fixed row while the adds
     * chew on it for the whole Zuk phase — needs the hit, the splat and the
     * flinch below, and none of this. */
    if( !immutable_target && npc->combat_target < 0 && npc_def(npc)->retaliate )
    {
        /*
         * Switching a fight from an npc to a person is not a new fight, and the
         * flinch below is only for one that is.
         *
         * `attack_clock` is ONE deadline and both combat branches read it, so
         * an npc that has been shooting another npc arrives here mid-cycle —
         * part way through a cooldown it has already paid for. Reseeding it
         * shortened that cooldown, and a shortened cooldown is a whole extra
         * swing: the Inferno's adds shot the shield, took a hit, and hit the
         * player before the shot they had just made was due to repeat. Leaving
         * the deadline alone carries it over, so the first swing at the person
         * lands on the tick the next swing at the npc would have.
         */
        int was_fighting_npc = npc->combat_target_npc >= 0;

        npc->combat_target = srv->active_player ? srv->active_player->pid : 0;
        if( !was_fighting_npc )
            npc->attack_clock = srv->tick + npc_def(npc)->attackrate / 2;
        /*
         * A person outranks whatever npc it was fighting.
         *
         * The two targets are exclusive, and this is the direction that has to
         * be stated: an npc mid-fight with another npc has `combat_target < 0`,
         * so without the clear here it would take the player as well and the
         * npc-versus-npc branch — which runs first — would keep winning the
         * turn. The Inferno's adds are the case: hit one while it is shooting
         * the shield and it must turn on you, and stay turned.
         */
        npc->combat_target_npc = -1;
        npc->combat_target_npc_gen = 0;
    }
    /*
     * Face whoever it is now fighting, by pid.
     *
     * This used to name "the local player" (32768 + 2047), right for the client
     * the retaliation was encoded to and wrong for every other one: NPC_INFO is
     * one mask set read by every recipient, so a second observer saw the goblin
     * turn to face *them*. The masks never needed to be per-observer — the id
     * already is absolute (32768 + pool slot, as in `setFaceEntity`), and
     * `combat_target` is that pid, set four lines up.
     *
     * Guarded on there being one: a `retaliate=no` npc has no target to face,
     * and turning it toward pid -1 would be the same visible wrong as the
     * latch it just declined.
     */
    ToriRSServer_NpcFacePlayer(npc, npc->combat_target);
    /* Flinch. Overwritten below if this was the killing blow. */
    play_npc_seq(npc, npc->block_seq);
    npc_sound_nearby(srv, npc, npc->block_sound, 0);

    if( npc->hitpoints == 0 )
    {
        /*
         * Zero hitpoints starts a death; it does not *do* one.
         *
         * The reference's `[proc,npc_default_damage]` (LostCity
         * skill_combat/scripts/npc/npc_combat.rs2:93) ends the killing hit with
         * one line — `npc_queue(3, 0, 0)` — and everything anyone would call
         * "dying" happens in the `[ai_queue3]` that fires afterwards. This used
         * to play the death animation, sound the death noise, run the drop table
         * and set the despawn clock right here, on the tick the damage landed,
         * which is two ticks and a whole death script early. Killing anything
         * read as the creature blinking out of existence with its loot already
         * on the floor.
         *
         * `Npc.processQueue` decrements before it compares and the newly-added
         * request is not reached in the pass that added it, so `npc_queue(…, 0)`
         * means "next npc phase". `srv->tick + 1` is that, and
         * `ToriRSServer_CombatNpcTick` picks it up from `death_stage`.
         *
         * The masks this no longer touches are the point: the flinch animation
         * and its noise, queued a few lines up, now reach the client on their
         * own tick instead of being overwritten by a death animation in the
         * same one.
         */
        npc->death_stage = TORIRSSERVER_DEATH_QUEUED;
        npc->death_tick = srv->tick + 1;
        /*
         * Drop whatever was already armed on the npc's own queue — a healer's
         * `npc_queue(4, heal, ...)` chief among them.
         *
         * `ToriRSServer_CombatStopNpc` below only clears the *targets* pointed at
         * this npc (its attacker's combat_target_npc); it does not reach into
         * `npc->queue[]`. Without this, a heal queued a tick or two before the
         * killing blow keeps counting down through QUEUED/ARRIVE/CORPSE — the
         * npc phase only skips a `death_tick`-holding npc's mode/AI, not its
         * queue drain — and `npc_statheal` (unlike this function) has no
         * `death_tick` guard of its own. It fires, hitpoints go back above
         * zero, and REAP (below) reads that as a scripted revive exactly like
         * the Kalphite Queen's own `[ai_queue3]` heal-to-transform and cancels
         * the death outright: the npc "doesn't die".
         *
         * Cleared here rather than guarding `npc_statheal` itself so that
         * pattern keeps working — an `[ai_queue3]` death script still runs
         * *after* this point and can arm its own fresh queue entries (Jad's
         * healer-despawn `npc_queue(5, ...)`, KQ's revive) same as before.
         */
        for( int i = 0; i < TORIRSSERVER_NPC_QUEUE_MAX; i++ )
            npc->queue[i].active = 0;
        /*
         * Capture kill attribution before combat_stop clears combat_target.
         * Clientscript 7192 needs the npc type + a per-kill event id; each
         * OBJ_ADD during [ai_queue3] then RUNCLIENTSCRIPTs the killers.
         *
         * It is captured here and *spent* three ticks later, when the drop table
         * finally runs: by then neither side still names the other, so a credit
         * read at that point would find nobody. `death_credit_players` is the
         * carrier.
         */
        memset(npc->death_credit_players, 0, sizeof(npc->death_credit_players));
        /* The active player delivered this hit and owns its loot even if their
         * combat_target was already cleared or moved to another npc before a
         * delayed projectile/poison splat landed. The scan below additionally
         * retains everybody still fighting this npc. */
        if( srv->active_player && srv->active_player->active &&
            srv->active_player->pid >= 0 &&
            srv->active_player->pid < TORIRSSERVER_PLAYER_MAX )
            npc->death_credit_players[srv->active_player->pid] = 1;
        ToriRSServer_CombatStopNpc(srv, slot);
        /*
         * And the *other* half of a target: the mode.
         *
         * `combat_target` is only one of the two things that point an npc at a
         * player. `npc->mode` is the other — `playerfollow`, `opplayer<n>` and
         * the rest name a victim just as durably, and `npc_setmode` states the
         * pairing itself ("a targetless mode CLEARS THE TARGET"). Clearing one
         * and not the other is a half-cleared aggression.
         *
         * Nothing acts on it while the corpse lies there — the npc phase skips
         * anything with a `death_tick` — so it looks harmless until the respawn
         * runs the mode again. A goblin killed mid-chase came back at its spawn
         * tile still in `playerfollow` and walked straight back at whoever it
         * had been fighting, ignoring its own wander radius and its leash: a
         * fresh npc that had never been hit, hunting.
         *
         * Before the death sequence runs, so an `[ai_queue3]` that sets a mode
         * on the way out keeps it — the same ordering `npc_run_mode` uses when
         * it clears a mode before firing the trigger, and for the same reason.
         * `[proc,npc_death]` opens with `npc_setmode(none)` too; this is the
         * earlier half of the same claim, and it has to be the earlier half
         * because a mode left armed for the tick between the blow and the death
         * script is a tick of a corpse chasing somebody.
         */
        ToriRSServer_NpcResetDefaults(npc);
        for( int i = 0; i < TORIRSSERVER_PLAYER_MAX; i++ )
        {
            if( srv->players[i].active && srv->players[i].combat_target == slot )
            {
                npc->death_credit_players[i] = 1;
                ToriRSServer_CombatStopPlayerAt(&srv->players[i]);
            }
        }
        /* The drop table does *not* run here. `[proc,npc_default_death]` calls
         * `gosub(npc_death)` first and only reaches its `obj_add` once that has
         * returned — which is after `npc_del`. So the loot lands on the tick the
         * corpse disappears, and `ToriRSServer_CombatNpcTick` is where that is. */
    }
}

void
ToriRSServer_CombatPoisonNpc(
    struct ToriRSServer* srv,
    int slot,
    const struct ToriRSServerPlayer* source,
    int severity)
{
    struct ToriRSServerNpc* npc;

    if( slot < 0 || slot >= TORIRSSERVER_NPC_MAX || severity <= 0 )
        return;
    assert(srv);
    npc = &srv->npcs[slot];
    if( !npc->active || npc->death_tick >= 0 )
        return;

    /* ContentAPI.applyPoison retains a strictly stronger timer and replaces
     * an equal/weaker one, including the source credited for the hit. */
    if( npc->poison_severity > severity )
        return;
    npc->poison_severity = severity;
    npc->poison_clock = srv->tick + 30;
    npc->poison_source_pid = source && source->active ? source->pid : -1;
    npc->poison_source_gen = source && source->active ? source->login_generation : 0;
}

void
ToriRSServer_CombatNpcPoisonTick(struct ToriRSServer* srv, int slot)
{
    struct ToriRSServerNpc* npc;
    struct ToriRSServerPlayer* source = NULL;
    struct ToriRSServerPlayer* saved_active;
    int damage;

    if( slot < 0 || slot >= TORIRSSERVER_NPC_MAX )
        return;
    assert(srv);
    npc = &srv->npcs[slot];
    if( !npc->active || npc->poison_severity <= 0 )
        return;
    if( npc->death_tick >= 0 )
    {
        npc->poison_severity = 0;
        return;
    }
    if( srv->tick < npc->poison_clock )
        return;

    damage = (npc->poison_severity + 4) / 5;
    npc->poison_severity--;
    if( npc->poison_severity > 0 )
        npc->poison_clock = srv->tick + 30;

    if( npc->poison_source_pid >= 0 && npc->poison_source_pid < TORIRSSERVER_PLAYER_MAX )
    {
        struct ToriRSServerPlayer* candidate = &srv->players[npc->poison_source_pid];

        if( candidate->active && candidate->login_generation == npc->poison_source_gen )
            source = candidate;
    }

    /* The normal damage path attributes retaliation through active_player.
     * A delayed poison hit must restore its captured source, and a stale one
     * must not fall through to whichever player the NPC phase ran after. */
    saved_active = srv->active_player;
    ToriRSServer_WorldSetActive(srv, source);
    ToriRSServer_CombatHitNpc(srv, slot, hitsplat_poison(), damage);
    ToriRSServer_WorldSetActive(srv, saved_active);
}

void
ToriRSServer_CombatHitmarkPlayer(
    struct ToriRSServer* srv,
    int type,
    int amount)
{
    struct ToriRSServerPlayer* player = srv->active_player;

    assert(player);
    if( amount < 0 )
        amount = 0;
    /* Dealer -1, not the active player: this runs with the VICTIM active (an
     * npc's attack script takes its target as the active player), so asking
     * who is active here answers "you", and every npc hit in the game would be
     * recorded as your own damage. -1 is also the right ANSWER for the only
     * case this server currently produces -- an npc hitting a player is damage
     * no player dealt. Player-versus-player would need the attacker threaded
     * through from the script that called this. */
    ToriRSServer_HitmarkAdd(player->hitmarks, &player->hitmark_count, amount,
                        amount > 0 ? type : hitsplat_block(), -1);
    player->damage = player->hitmarks[0].damage;
    player->damage_type = player->hitmarks[0].type;
    player->masks |= TORIRSSERVER_PMASK_DAMAGE;
    if( player->hitmark_count >= 2 )
        player->masks |= TORIRSSERVER_PMASK_DAMAGE2;
}

void
ToriRSServer_CombatHitmarkNpc(
    struct ToriRSServer* srv,
    int slot,
    int type,
    int amount)
{
    struct ToriRSServerNpc* npc;

    if( slot < 0 || slot >= TORIRSSERVER_NPC_MAX )
        return;
    npc = &srv->npcs[slot];
    if( !npc->active || npc->death_tick >= 0 )
        return;
    if( amount < 0 )
        amount = 0;

    /*
     * Everything `ToriRSServer_CombatHitNpc` does to make the splat visible, and
     * none of what it does to the fight: no hitpoints move, no `combat_target`
     * is set, no `npc_action_delay` is charged. The caller owns the health
     * change, if there is one at all.
     *
     * The mask is what matters beyond the number. `put_npc_extended_v5` offers
     * a HEADBAR only alongside a hitmark, so this is also the one way content
     * can put the overhead health bar over an npc whose health is moving
     * without anybody hitting it.
     */
    ToriRSServer_HitmarkAdd(npc->hitmarks, &npc->hitmark_count, amount, type,
                        ToriRSServer_HitmarkDealerFromAttackerScript(srv));
    npc->damage = npc->hitmarks[0].damage;
    npc->damage_type = npc->hitmarks[0].type;
    npc->max_hitpoints = npc->max_hitpoints > 0 ? npc->max_hitpoints : 1;
    npc->masks |= TORIRSSERVER_NMASK_DAMAGE;
    /* The classic (rev-230) mask has room for exactly two splats; the v5 block
     * reads `hitmark_count` and ignores this bit. Same split as the damage
     * path -- see the note there. */
    if( npc->hitmark_count >= 2 )
        npc->masks |= TORIRSSERVER_NMASK_DAMAGE2;
}

void
ToriRSServer_CombatHitPlayer(
    struct ToriRSServer* srv,
    int type,
    int amount)
{
    struct ToriRSServerPlayer* player = srv->active_player;

    /*
     * `::god` absorbs the hit here rather than at any call site, because this
     * is the only place player hitpoints go down — content's damage() opcode
     * and every engine path both land here. Zeroing `amount` keeps the rest of
     * the function honest: a block splat is still sent, retaliation and the
     * defend animation still run, so the encounter behaves exactly as it does
     * without the flag apart from the subtraction.
     */
    if( player->godmode )
        amount = 0;

    /*
     * Absorption sits beside `::god` for the same reason it does: this is the
     * only place player hitpoints go down, so a pool that is meant to stand in
     * front of every source has exactly one place it can stand. A fully
     * absorbed hit still produces a splat and still counts as a hit for
     * retaliation — only the subtraction is skipped.
     */
    int after = absorb_player_damage(srv, player, type, amount);
    /* Fully absorbed: the splat is the shield, not the block splat the
     * zero-amount default below would otherwise pick — a soaked hit and a
     * missed swing must not look the same. */
    int absorbed_fully = after == 0 && amount > 0;

    amount = after;
    /* One-shot: consumed whether or not a pool was there to spend. */
    player->hit_self_inflicted = 0;

    if( amount > player->hitpoints )
        amount = player->hitpoints;
    player->hitpoints -= amount;

    /* Dealer -1 -- see the twin of this call in `CombatHitmarkPlayer`. */
    ToriRSServer_HitmarkAdd(player->hitmarks, &player->hitmark_count, amount,
                        amount > 0 ? type : (absorbed_fully ? hitsplat_shield() : hitsplat_block()),
                        -1);
    player->damage = player->hitmarks[0].damage;
    player->damage_type = player->hitmarks[0].type;
    player->hitpoints = player->hitpoints < 0 ? 0 : player->hitpoints;
    player->masks |= TORIRSSERVER_PMASK_DAMAGE;
    /* The classic second slot — see the npc twin of this line. */
    if( player->hitmark_count >= 2 )
        player->masks |= TORIRSSERVER_PMASK_DAMAGE2;
    ToriRSServer_CombatSyncHitpoints(player);

    /*
     * The block animation is content's — [ai_opplayer2,_] plays
     * anim(%com_defendanim). The engine no longer drives it.
     */

    if( player->hitpoints == 0 && !player->dying )
    {
        /*
         * Death is content's, all of it.
         *
         * `[queue,player_death]` plays the animation, waits, says both lines,
         * teleports, restores and clears the prayers — the reference's
         * `[queue,player_death]` does exactly that list, and its engine
         * contributes nothing but the varps the script writes. What was here
         * was the whole sequence in C: a seq name, a tick count, a respawn
         * coordinate and two strings, none of which a server operator could
         * change and all of which are the first things one would.
         *
         * The engine keeps two facts, and only because they are about the
         * simulation rather than about dying: combat stops, and `dying` gates
         * everything that would let a corpse act. The gate is cleared in
         * `ToriRSServer_CombatPlayerTick`, on the tick the script's `stat_heal`
         * puts hitpoints back above zero — so even the length of the death is
         * the script's.
         */
        /* A lock belongs to the live encounter action, never to the corpse or
         * its respawn. The death queue itself must remain free to run. */
        ToriRSServer_WorldPlayerUnlock(srv);
        player->dying = 1;
        ToriRSServer_CombatStopPlayer(srv);
        for( int i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
        {
            if( srv->npcs[i].combat_target == player->pid )
                ToriRSServer_CombatStopNpc(srv, i);
        }
        /* Content queues [queue,player_death] from wrappers; raw hit paths fire
         * PLAYERDEATH so the sequence still starts. */
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_PLAYERDEATH, -1, -1, -1);
    }
}

/* ------------------------------------------------------------------ */
/* Engagement                                                          */
/* ------------------------------------------------------------------ */

/** Does this npc's cache record offer an Attack option? That is the same test
 *  the client's minimenu makes, so the two ends cannot disagree about what is
 *  a valid target. */
int
ToriRSServer_CombatAttackable(int npc_type)
{
    const struct ToriRSServerNpcInfo* info = ToriRSServer_NpcInfo(npc_type);

    for( int i = 0; i < 5; i++ )
    {
        if( info->ops[i] && strcmp(info->ops[i], "Attack") == 0 )
            return 1;
    }
    return 0;
}

/*
 * Has this player gone quiet long enough to stop fighting?
 *
 * The wiki's Auto Retaliate rule: retaliation follows the attacker "for 20
 * minutes if no player input is given, after which players stop attacking all
 * together even if they are attacked by monsters".
 *
 * It reads as a rule about retaliation and it is really a rule about *combat*,
 * which is why the test lives on engage rather than in the retaliation queue.
 * Every swing re-arms the OPNPC2 interaction through `p_opnpc(2)` — the
 * retaliation queue, the melee label, the ranged loop and the special all end
 * that way — so one gate here stops the fight at its next swing instead of
 * stopping only the fights that started by being hit.
 *
 * A player who is actually playing never reaches it: the packet carrying their
 * click resets `last_input_tick` before its own handler engages.
 */
int
ToriRSServer_CombatPlayerAfk(const struct ToriRSServerPlayer* player)
{
    if( !player || !player->world ||
        player->last_input_tick == TORIRSSERVER_INPUT_TICK_NEVER )
        return 0;
    return player->world->tick - player->last_input_tick >= TORIRSSERVER_AFK_COMBAT_TICKS;
}

void
ToriRSServer_CombatEngage(
    struct ToriRSServer* srv,
    int slot)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    struct ToriRSServerNpc* npc;

    if( slot < 0 || slot >= TORIRSSERVER_NPC_MAX )
        return;
    npc = &srv->npcs[slot];
    if( !npc->active || npc->death_tick >= 0 )
        return;
    if( player->dying )
        return;
    if( ToriRSServer_CombatPlayerAfk(player) )
    {
        /* Drop whatever fight was running rather than only declining the new
         * one: leaving `combat_target` set would keep the approach walking the
         * player after a monster it has stopped swinging at. */
        ToriRSServer_CombatStopPlayer(srv);
        return;
    }

    player->combat_target = slot;
    /* Swing on the tick the player arrives rather than after a full interval:
     * an opening delay reads as the click having been dropped. */
    player->attack_clock = 0;

    {
        struct CollisionApproach approach;
        const struct ToriRSServerNpcInfo* info = ToriRSServer_NpcInfo(npc->type);
        int size = info ? info->size : 1;

        /*
         * Arm OPNPC2 so content owns the swing loop (LostCity: combat is the
         * interaction). The engine clock no longer fires OPNPC2 — p_opnpc(2)
         * re-arms this after each swing / action_delay wait.
         */
        ToriRSServer_WorldInteractionSet(srv, TORIRSSERVER_INTERACT_NPC, 2, slot, npc->type,
                                      npc->x, npc->z, npc->level, size, size);
        /*
         * Only walk if the click did not already land in range.
         *
         * `ToriRSServer_SceneNpcApproach` builds a melee-adjacency shape (the
         * npc's own footprint, not the attacker's reach), so issuing this
         * unconditionally sent every ranged/magic "Attack" click on a
         * satisfied target straight toward melee adjacency — a bow already
         * standing at range 9 of a range-10 target got yanked in on every
         * click. `ToriRSServer_CombatPlayerApproach` (the per-tick repath,
         * below) already gates the same walk on `in_player_attack_range`;
         * this is that same gate, just on the click that starts the fight
         * rather than the tick that continues it.
         */
        if( in_player_attack_range(player, npc) )
            ToriRSServer_WorldStepsClear(player);
        else
        {
            ToriRSServer_SceneNpcApproach(size, &approach);
            ToriRSServer_WorldWalkToApproach(srv, npc->x, npc->z, &approach);
        }
    }
}

/* ------------------------------------------------------------------ */
/* The tick                                                            */
/* ------------------------------------------------------------------ */

/*
 * Player swing timing lives in content (`%action_delay` + `p_opnpc(2)`), not
 * an engine attack_clock that fires OPNPC2. combat_player_tick only cleans up
 * a dead target / finished death.
 */

/*
 * Re-path to the target, every tick, *before* the player takes a step.
 *
 * The reference's order, and it is not a detail: `Player.processInteraction`
 * runs `pathToTarget()` and only then `updateMovement()`, so a step is always
 * aimed at where the target is now. This used to top the route up after the
 * move and only when the queue had run dry, which meant every step the player
 * took was aimed one tick into the past.
 *
 * With a stationary target that is invisible. With one that moves — which npcs
 * only became once they could chase — it is a deadlock: the npc closes onto the
 * tile between the two, the player's stale step lands on that same tile, and
 * they end up stacked. Neither is then in range (a shared tile is not adjacent),
 * so both walk again, and the pair shuffles across the map without a blow being
 * struck. That is what the combat selftest saw the moment npcs could pursue.
 *
 * Clearing the queue on arrival is the other half: the reference's walk ends
 * when the target is reached, and a step left over from the approach would walk
 * the player straight back out of range on the next tick.
 */
void
ToriRSServer_CombatPlayerApproach(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    struct ToriRSServerNpc* npc;

    if( player->dying || player->combat_target < 0 )
        return;
    npc = &srv->npcs[player->combat_target];
    if( !npc->active || npc->death_tick >= 0 )
        return;

    if( in_player_attack_range(player, npc) )
    {
        ToriRSServer_WorldStepsClear(player);
        return;
    }
    /* Nothing else is walking the player while a combat target is set: every
     * other click clears the target first (see the OPNPC/OPLOC/MOVE handlers),
     * so this owns the step queue and can recompute it outright. */
    {
        struct CollisionApproach approach;
        const struct ToriRSServerNpcInfo* info = ToriRSServer_NpcInfo(npc->type);
        ToriRSServer_SceneNpcApproach(info ? info->size : 1, &approach);
        ToriRSServer_WorldWalkToApproach(srv, npc->x, npc->z, &approach);
    }
}

void
ToriRSServer_CombatPlayerTick(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    struct ToriRSServerNpc* npc;

    /* TORIRSSERVER_HP_TRACE=1: every hitpoints change, once per tick. Under `::god`
     * a change is by definition a gate that was missed, and hitpoints are
     * written from half a dozen places (the damage funnel, three stat opcodes,
     * the level setter, the save loader) — sampling the value beats adding a
     * print to each and still missing the one that mattered. */
    static int hp_trace = -1;
    if( hp_trace < 0 )
        hp_trace = getenv("TORIRSSERVER_HP_TRACE") != NULL;
    if( hp_trace )
    {
        static int last_hp = -1;
        if( player->hitpoints != last_hp )
        {
            fprintf(stderr,
                    "hp_trace: tick=%d hp=%d boosted=%d max=%d god=%d dying=%d\n",
                    srv->tick, player->hitpoints,
                    player->stat_boosted[TORIRSSERVER_STAT_HITPOINTS],
                    player->max_hitpoints, player->godmode, player->dying);
            last_hp = player->hitpoints;
        }
    }

    /*
     * A corpse does not swing, and the script is what stops it being one.
     *
     * `[queue,player_death]` ends with `stat_heal(hitpoints, 99, 100)`, and this
     * is the engine noticing: hitpoints above zero means the death is over.
     * Nothing here counts ticks or decides where the player wakes up, which is
     * the whole point — the delay, the coordinate and the heal are all lines in
     * player/death.rs2.
     *
     * It sits here rather than in `stat_heal` because more than one command
     * restores hitpoints — `stat_heal`, `stat_boost`, a level-up — and the
     * engine should not have to know which of them the script used. The
     * phase order makes it prompt anyway: queues are drained at the top of
     * phase 5 and this runs at the bottom of it, so the tick that heals is the
     * tick that revives.
     */
    if( player->dying && player->hitpoints > 0 )
        player->dying = 0;
    if( player->dying )
        return;
    if( player->combat_target < 0 )
        return;

    npc = &srv->npcs[player->combat_target];
    if( !npc->active || npc->death_tick >= 0 )
    {
        ToriRSServer_CombatStopPlayer(srv);
        return;
    }

    /* Facing is ToriRSServer_PlayerSetFaceEntity in phase_player (before
     * approach / interaction), matching LostCity setFaceEntity.
     *
     * Swings are content's via the OPNPC2 interaction (set by engage /
     * p_opnpc(2)), not an engine attack_clock firing OPNPC2. Dead-target
     * cleanup above is the only combat work left in this tick slot. */
    (void)npc;
}

/*
 * Aggression.
 *
 * OldSchool's rule, kept: a monster stops being aggressive once the player's
 * combat level is more than twice its own. Without that the Lumbridge goblins
 * would still be mobbing a player who has outgrown them by an order of
 * magnitude, which is a worse first impression than no aggression at all.
 */
int
ToriRSServer_CombatLevel(const struct ToriRSServerPlayer* player)
{
    /* OldSchool's melee formula:
     * floor(0.25 * (defence + hitpoints + floor(prayer / 2)) + 0.325 * (attack
     * + strength)). Scaled by 1000 so it stays in integers. */
    int base = player->stat_level[TORIRSSERVER_STAT_DEFENCE] +
               player->stat_level[TORIRSSERVER_STAT_HITPOINTS] +
               (player->stat_level[TORIRSSERVER_STAT_PRAYER] / 2);
    int melee = player->stat_level[TORIRSSERVER_STAT_ATTACK] +
                player->stat_level[TORIRSSERVER_STAT_STRENGTH];

    return ((base * 250) + (melee * 325)) / 1000;
}

/*
 * The leash — the reference's `Npc.targetWithinMaxRange`, op-trigger branch.
 *
 * Measured from where the npc *spawned*, not from where it is standing, which
 * is the whole point: a chase that measured from the npc would extend itself by
 * one tile every step and never end. `maxrange + 1` and the corner exclusion are
 * both the reference's, and the corner one is not cosmetic — without it the
 * reachable area is a square with four tiles of extra diagonal.
 *
 * It matters more here than it reads: until the chase could route around a wall
 * an npc could barely leave its own tile, so nothing was holding the leash.
 */
static int
tile_within_maxrange(
    const struct ToriRSServer* srv,
    int x,
    int z,
    const struct ToriRSServerNpc* npc)
{
    int range = npc_def(npc)->maxrange;
    int home_x = npc->spawn_x;
    int home_z = npc->spawn_z;
    int dx;
    int dz;

    /*
     * A leash is measured from HOME, and a familiar's home is its owner, not
     * the tile it was summoned on.
     *
     * `spawn_x/spawn_z` is the right anchor for everything the world places:
     * a goblin belongs to its patch of ground and must not be dragged across
     * the map. A summoned familiar belongs to a person. Measuring it from the
     * summon tile leashes it to wherever the pouch happened to be clicked, so
     * an owner who walks fifteen tiles and then attacks something has a
     * familiar the engine refuses to let fight at all — and the refusal is
     * silent, because `npc_vs_npc_tick` simply drops the target and the
     * familiar reverts to following. That is indistinguishable from "my
     * familiar ignores combat", which is what it was reported as.
     *
     * `TORIRSSERVER_FAMILIAR_LEASH` is the owner-relative range, and it is content's
     * number as well: `~summoning_familiar_assist_allowed` refuses a target
     * more than ten tiles from the owner, so the two halves agree on when a
     * fight is over instead of one of them holding a target the other has
     * given up. An owned npc whose owner has logged out falls back to the
     * spawn anchor rather than becoming unleashed.
     */
    if( npc->owner_gen != 0 && srv )
    {
        const struct ToriRSServerPlayer* owner =
            ToriRSServer_WorldNpcOwner((struct ToriRSServer*)srv, npc);

        if( owner )
        {
            home_x = owner->x;
            home_z = owner->z;
            range = TORIRSSERVER_FAMILIAR_LEASH;
        }
    }

    dx = abs_of(x - home_x);
    dz = abs_of(z - home_z);

    if( dx > range + 1 || dz > range + 1 )
        return 0;
    if( dx == range + 1 && dz == range + 1 )
        return 0;
    return 1;
}

static int
target_within_maxrange(
    const struct ToriRSServer* srv,
    const struct ToriRSServerPlayer* player,
    const struct ToriRSServerNpc* npc)
{
    return tile_within_maxrange(srv, player->x, player->z, npc);
}

/*
 * The player an aggressive npc would notice, or NULL.
 *
 * Nearest first, which is the only tie-break that does not depend on pool
 * order — with one player the question never arose and `srv->active_player` was
 * the answer, which in phase 4 is nobody's turn at all and was a null
 * dereference the moment a second player existed. The reference picks by hunt
 * *profile* (`HuntType`, and the `.hunt` files that configure it); nearest is
 * the placeholder until those land, and is stated here rather than implied.
 */
static struct ToriRSServerPlayer*
nearest_victim(
    struct ToriRSServer* srv,
    const struct ToriRSServerNpc* npc)
{
    struct ToriRSServerPlayer* best = NULL;
    int best_distance = 0;

    for( int i = 0; i < srv->player_count; i++ )
    {
        struct ToriRSServerPlayer* player = &srv->players[i];
        int distance;

        if( !player->active || player->dying || player->level != npc->level )
            continue;
        distance = tile_distance(player->x, player->z, npc->x, npc->z);
        if( distance > npc->def->huntrange )
            continue;
        if( !best || distance < best_distance )
        {
            best = player;
            best_distance = distance;
        }
    }
    return best;
}

static void
maybe_aggress(
    struct ToriRSServer* srv,
    int slot)
{
    struct ToriRSServerNpc* npc = &srv->npcs[slot];
    struct ToriRSServerPlayer* player;
    int npc_level;

    /* `npc->huntmode`, not `npc->def->huntmode`: the def seeds it at spawn and
     * `npc_sethuntmode` overrides it per npc. Reading the def here would make
     * that opcode a no-op for the one thing content uses it for. */
    if( !npc->def || npc->huntmode != TORIRSSERVER_HUNT_AGGRESSIVE )
        return;
    if( npc->def->huntrange <= 0 || npc->combat_target >= 0 )
        return;
    player = nearest_victim(srv, npc);
    if( !player )
        return;
    /*
     * In range to notice, but outside the leash: do not start.
     *
     * The reference reaches the same end by a longer road — it sets the
     * interaction, `validateTarget` refuses it on the next tick and the npc
     * drops straight back to its default mode. Checking here instead is one
     * tick cheaper and, more usefully, stable: an npc standing at the edge of
     * its range would otherwise take a target and give it up on alternate
     * ticks for as long as the player stood there, sending a FACE_ENTITY latch
     * each way.
     */
    if( !target_within_maxrange(srv, player, npc) )
        return;

    npc_level = ToriRSServer_NpcInfo(npc->type)->combat_level;
    if( npc_level > 0 && ToriRSServer_CombatLevel(player) > npc_level * 2 )
        return;

    npc->combat_target = player->pid;
    npc->attack_clock = 0;
}

/*
 * One step of `[proc,npc_death]`.
 *
 * The reference (LostCity skill_combat/scripts/npc/npc_death.rs2) is nine lines
 * and three of them suspend, so a death is not an event on one tick but a small
 * script spread over four:
 *
 *     npc_walk(npc_coord); npc_setmode(none);
 *     npc_arrivedelay;                 // 0, 1 or 2 ticks
 *     ~sound_within_distance(death_sound, …)
 *     npc_anim(death_anim, 0);
 *     npc_delay(1);                    // tick + 1 + 1, so two ticks
 *     npc_del;
 *
 * and its caller `[proc,npc_default_death]` only reaches `obj_add` after that
 * has returned — which is *after* `npc_del`. Written as a ledger, for a killing
 * blow whose damage lands on tick D:
 *
 *     D     hitpoints reach 0, flinch animation, `npc_queue(3, 0, 0)`
 *     D+1   TORIRSSERVER_DEATH_QUEUED  — stop, face nothing, arrivedelay
 *     D+1+a TORIRSSERVER_DEATH_ARRIVE  — death sound and death animation
 *     D+1+a+death_delay           — the drop table, then npc_del
 *
 * with `a` 0 for something standing still, 1 if it moved on the previous tick
 * and 2 if it moved on this one. `death_delay` is the reference's `npc_delay(1)`
 * (two ticks) as a per-npc record field, so a boss can lie there longer than a
 * rat.
 *
 * This lives in the engine rather than in `[proc,npc_death]` for one reason
 * worth stating: `[ai_queue3]` is an *exclusive* trigger (type, then category,
 * then `_`), and 494 drop tables in the tree bind it by type or category. A
 * content-side death would be skipped by every one of them, and the npcs that
 * skipped it would never despawn. The schedule is the reference's; the place is
 * ours, and the split is the one PORTING_GUIDE §2.3 already documents for
 * hitpoints, the animation, the delay and the despawn.
 */
static void
npc_death_step(
    struct ToriRSServer* srv,
    int slot)
{
    struct ToriRSServerNpc* npc = &srv->npcs[slot];

    /*
     * A parked script owns the npc, and a death is not allowed to run underneath
     * one. `Npc.processNpc` says the same by returning early from `isValid()`
     * while delayed — no timers, no queues, no modes — and the reason it matters
     * here is `[ai_queue3]`: `[ai_queue3,kalphite_queen]` suspends on
     * `npc_delay(0)` half way through its form change, and reaping the npc while
     * its script is parked orphans the rest of it.
     *
     * Deferring by a tick rather than skipping: the step is still owed.
     */
    if( srv->tick < npc->delayed_until || npc->active_script )
    {
        npc->death_tick = srv->tick + 1;
        return;
    }

    switch( npc->death_stage )
    {
    case TORIRSSERVER_DEATH_QUEUED:
        /*
         * `npc_walk(npc_coord)` and `npc_setmode(none)`. The mode was put back
         * at the blow; the route was not, and a half-walked route is what the
         * reference's walk-to-your-own-tile cancels. Nothing steps a dying npc
         * (`advance_npcs` skips anything holding a `death_tick`), so this is
         * about what the npc wakes up with if a script revives it, not about
         * the corpse moving.
         */
        npc->waypoint_index = -1;
        /*
         * `npc_arrivedelay` — the same three arms as SS_OP_NPC_ARRIVEDELAY, and
         * for the same reason: "let the step I am mid-way through finish before
         * I fall over". `last_movement` is the moving tick *plus one*, so
         * `< tick - 1` is "has not moved for two ticks" and needs no wait at
         * all. Deliberately not routed through `delayed_until`: that field
         * parks a *script*, and phase 4 offering a resume to an npc that has
         * none would be a second owner of the same clock.
         */
        if( npc->last_movement >= srv->tick - 1 )
        {
            npc->death_stage = TORIRSSERVER_DEATH_ARRIVE;
            npc->death_tick =
                srv->tick + (npc->last_movement == srv->tick - 1 ? 1 : 2);
            return;
        }
        /* Standing still: the reference's `npc_arrivedelay` returns without
         * suspending and the next line runs in the same tick. */
        /* FALLTHROUGH */

    case TORIRSSERVER_DEATH_ARRIVE:
        /*
         * Sound then animation, in the reference's order, and both a clear tick
         * after the flinch that preceded them — which is the whole point of the
         * schedule. They used to be queued in `ToriRSServer_CombatHitNpc` beside
         * the flinch, so the death animation overwrote the flinch in one mask
         * and the two noises arrived together.
         */
        /*
         * ONCE PER LIFE. If a script already showed this death — a Matomenos
         * absorbed at the Maiden's feet, a red at Verzik — and a hit then
         * landed inside it, the client is already on `death_seq` and most
         * likely parked on its last frame. A second send does not restart it
         * (replyMode 2), so what it produces is a corpse that never moved and
         * then vanished. The sound goes with it: one death, one noise. The
         * stage machinery below still runs, so the corpse is held and reaped
         * on the engine's clock either way.
         */
        if( !npc->death_seq_sent )
        {
            npc_sound_nearby(srv, npc, npc->death_sound, 0);
            play_npc_seq(npc, npc->death_seq);
        }
        else if( getenv("TORIRS_ANIM_DEBUG") )
            fprintf(stderr,
                    "srv: npc death_seq %d already sent this life — not re-sent "
                    "(a script played it first)\n",
                    npc->death_seq);
        npc->death_stage = TORIRSSERVER_DEATH_CORPSE;
        npc->death_tick = srv->tick + npc_def(npc)->death_delay;
        return;

    case TORIRSSERVER_DEATH_CORPSE:
        /*
         * The drop table, and then `npc_del`.
         *
         * The order within the tick is the reference's — `gosub(npc_death)`
         * ends with `npc_del` and `[proc,npc_default_death]` does its `obj_add`
         * on the line after — but the two statements are swapped here because
         * `active_npc()` refuses an inactive npc, so a script running after
         * `active = 0` could not read `npc_coord` to drop anything on. Both
         * land in the same tick's packets either way, so no client can tell.
         *
         * Kill credit is re-armed from what the blow captured: the players who
         * were fighting this npc three ticks ago, which is nothing the world
         * still records by now.
         *
         * The reap is a stage of its own rather than the tail of this one,
         * because the script may suspend: `[ai_queue3]` runs exactly once, and
         * a stage boundary is what says so.
         */
        npc->death_stage = TORIRSSERVER_DEATH_REAP;
        npc->death_tick = srv->tick;
        npc->loot_credit_npc_type = npc->type;
        npc->loot_credit_event_id = ++srv->loot_credit_seq;
        srv->loot_credit_armed = 1;
        srv->loot_credit_npc_type = npc->loot_credit_npc_type;
        srv->loot_credit_event_id = npc->loot_credit_event_id;
        memcpy(srv->loot_credit_players, npc->death_credit_players,
               sizeof(srv->loot_credit_players));
        ToriRSServer_WorldNpcDied(srv, slot);
        srv->loot_credit_armed = 0;
        memset(srv->loot_credit_players, 0, sizeof(srv->loot_credit_players));
        /* A drop script may npc_delay before it reaches obj_add. Keep the
         * attribution on the npc until its parked state finishes; the resume
         * path re-arms the same event around each continuation. */
        if( !npc->active_script )
        {
            memset(npc->death_credit_players, 0, sizeof(npc->death_credit_players));
            npc->loot_credit_event_id = 0;
            npc->loot_credit_npc_type = 0;
        }
        if( !npc->active )
            return; /* the script did its own npc_del */
        if( srv->tick < npc->delayed_until || npc->active_script )
        {
            /* Parked mid-script — the guard at the top of this function picks it
             * back up, and the reap stage stops the table running twice. */
            npc->death_tick = srv->tick + 1;
            return;
        }
        /* FALLTHROUGH */

    case TORIRSSERVER_DEATH_REAP:
    default:
        /*
         * An `[ai_queue3]` that puts hitpoints back is not a death.
         *
         * `[ai_queue3,kalphite_queen]` is the case: `npc_changetype` plus
         * `npc_statheal(hitpoints, 0, 100)`, and no `gosub(npc_death)` — which
         * in the reference is precisely how content says "this one does not die
         * here, it changes form". The engine owns the death now, so the same
         * sentence has to be readable from the hitpoints, and it is the rule the
         * player side already uses (`ToriRSServer_CombatPlayerTick`: hitpoints back
         * above zero means the death is over).
         */
        if( npc->hitpoints > 0 )
        {
            npc->death_tick = -1;
            npc->death_stage = TORIRSSERVER_DEATH_NONE;
            return;
        }
        ToriRSServer_WorldNpcOccupancy(npc, 0);
        ToriRSServer_WorldNpcFree(srv, slot);
        npc->death_tick = -1;
        npc->death_stage = TORIRSSERVER_DEATH_NONE;
        /*
         * Only a *world* npc comes back on its own — see `despawns_on_death`
         * for the two lifecycles and for what respawning both cost the Inferno.
         *
         * A script may still arm `npc_setrespawn` during [ai_queue3] (GWD
         * minion sync) and that clock is kept for either lifecycle: content
         * asking for a respawn outranks the default.
         */
        if( npc->respawn_tick < 0 && !npc->despawns_on_death )
            npc->respawn_tick = srv->tick + npc_def(npc)->respawnrate;
        return;
    }
}

/*
 * Continue an OP -> AP combat handoff.
 *
 * A number of NPC-specific `[ai_opplayer2,<npc>]` handlers do not make a
 * swing themselves.  They select `applayer<n>` and leave the actual ranged or
 * magic action to `[ai_applayer<n>,<npc>]`.  That normally reaches
 * `npc_run_mode`, but `advance_npcs` deliberately does not run modes while an
 * NPC has a combat target: combat owns its movement.  Without this bridge the
 * AP action is never called, so a wizard, witch, dragon, etc. simply stops at
 * the first OP trigger.
 *
 * This is intentionally next-turn dispatch, rather than immediately running
 * AP after OP.  Some OP handlers both attack and set AP for their following
 * action; calling it in the same turn would double-fire those.  It also gives
 * `npc_delay` ownership of special attack cadence: a parked AP script keeps
 * its mode, and must not be re-entered until it resumes.
 *
 * As in `npc_run_mode`, clear before firing so a handler which selects another
 * mode preserves that selection.  AP modes keep `combat_target` -- unlike
 * targetless modes, they name the player the NPC is already fighting.
 */
static int
npc_dispatch_combat_applayer_mode(
    struct ToriRSServer* srv,
    struct ToriRSServerNpc* npc,
    int slot)
{
    int op;

    if( npc->mode < TORIRSSERVER_NPCMODE_APPLAYER1 || npc->mode > TORIRSSERVER_NPCMODE_APPLAYER5 )
        return 0;

    /* `npc_delay` makes an NPC invalid for the rest of its turn.  The script
     * which set AP commonly remains parked on that delay, so retain its mode
     * and let phase_npcs resume it before trying the handoff again. */
    if( srv->tick < npc->delayed_until || npc->active_script )
        return 1;

    op = npc->mode - TORIRSSERVER_NPCMODE_APPLAYER1;
    npc->mode = TORIRSSERVER_NPCMODE_NONE;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_AI_APPLAYER1 + op, npc->type, -1, slot);
    return 1;
}

/*
 * One turn of a fight between two npcs.
 *
 * The same machine as the player fight below it — validate, face, reach, close,
 * clock, swing — with `[ai_opnpc2,<attacker>]` where that one fires
 * `[ai_opplayer2]`. An npc's target used to be a *pid* and nothing else, so the
 * only thing a monster could be made to fight was a person: an encounter whose
 * monsters attack a piece of scenery (the Inferno's adds and the Ancestral
 * Glyph) had to drive the whole thing from `[ai_timer]` and carry its own
 * attack clock, facing and range beside the engine's.
 *
 * The trigger runs with the target armed as the SECONDARY npc, which is what
 * `.npc_` addresses — the reference's `activeNpc2`, and the only way the script
 * can say anything about the thing it is hitting.
 *
 * Returns 1 when the fight is still on and has claimed this npc's turn, 0 when
 * it has ended and the ordinary player-combat path below should run instead.
 */
static int
npc_vs_npc_tick(
    struct ToriRSServer* srv,
    int slot)
{
    struct ToriRSServerNpc* npc = &srv->npcs[slot];
    int target_slot = npc->combat_target_npc;
    struct ToriRSServerNpc* target;
    int in_reach;

    if( target_slot < 0 || target_slot >= TORIRSSERVER_NPC_MAX )
    {
        npc->combat_target_npc = -1;
        return 0;
    }
    target = &srv->npcs[target_slot];
    /*
     * The generation is what makes a stale slot safe. A target that died and
     * whose slot was reused is a *different* npc, and continuing to shoot it
     * would be an attack nobody asked for on whatever spawned there next.
     */
    if( !target->active || target->death_tick >= 0 || target == npc ||
        target->generation != npc->combat_target_npc_gen ||
        target->level != npc->level ||
        !tile_within_maxrange(srv, target->x, target->z, npc) )
    {
        ToriRSServer_CombatStopNpc(srv, slot);
        return 0;
    }

    /* Face for the whole engagement, not only once in reach — the same rule and
     * the same reason as the player path. */
    ToriRSServer_NpcFaceNpc(npc, target_slot);

    in_reach = in_npc_attack_range_npc(npc, target);
    if( in_reach && npc_def(npc)->attackrange > 1 &&
        !ToriRSServer_SceneApproached(npc->level, target->x, target->z, npc->x, npc->z,
                                  target->size > 0 ? target->size : 1,
                                  target->size > 0 ? target->size : 1,
                                  npc->size > 0 ? npc->size : 1,
                                  npc->size > 0 ? npc->size : 1) )
        in_reach = 0;

    if( !in_reach )
    {
        struct CollisionApproach approach = {
            .kind = COLL_APPROACH_RECT_ADJACENT,
            .loc_width = target->size > 0 ? target->size : 1,
            .loc_length = target->size > 0 ? target->size : 1,
            .mover_size = npc->size > 0 ? npc->size : 1,
        };

        if( ToriRSServer_WorldNpcWalkToApproach(npc, target->x, target->z, &approach) &&
            !npc_def(npc)->givechase )
            ToriRSServer_CombatStopNpc(srv, slot);
        return 1;
    }

    if( srv->tick < npc->attack_clock )
        return 1;
    npc->attack_clock = srv->tick + npc_def(npc)->attackrate;
    ToriRSServer_ScriptsRunTriggerNpc2(srv, SS_TRIGGER_AI_OPNPC2, npc->type, -1, slot,
                                     target_slot);
    return 1;
}

void
ToriRSServer_CombatNpcTick(
    struct ToriRSServer* srv,
    int slot)
{
    struct ToriRSServerNpc* npc = &srv->npcs[slot];
    struct ToriRSServerPlayer* player;

    /* Death first: a dead npc neither swings nor roams, and its corpse has to
     * outlive the killing blow long enough for the animation to play. */
    if( npc->death_tick >= 0 )
    {
        if( srv->tick >= npc->death_tick )
            npc_death_step(srv, slot);
        return;
    }

    if( npc->combat_target_npc >= 0 && npc_vs_npc_tick(srv, slot) )
        return;

    maybe_aggress(srv, slot);
    if( npc->combat_target < 0 )
        return;
    /* `combat_target` is a *pid* — a pool index — not the flag "is fighting the
     * player" it used to be while there was only one. A target who logged out
     * leaves a hole in the pool, so the npc gives up rather than swinging at
     * whoever takes the slot next. */
    player = &srv->players[npc->combat_target];
    if( !player->active )
    {
        ToriRSServer_CombatStopNpc(srv, slot);
        return;
    }
    /* This npc's turn is the *target's* turn: everything below writes to the
     * player it is fighting, and phase 4 runs outside any per-player loop. */
    ToriRSServer_WorldSetActive(srv, player);
    if( player->dying )
    {
        ToriRSServer_CombatStopNpc(srv, slot);
        return;
    }

    /*
     * Give up on a target that has been dragged out of the npc's area.
     *
     * The reference validates the target *before* running the mode
     * (`processMovementInteraction` -> `validateTarget` -> `resetDefaults`), so
     * this sits before the approach for the same reason: an npc that is about to
     * stop must not take one more step first.
     */
    if( !target_within_maxrange(srv, player, npc) )
    {
        ToriRSServer_CombatStopNpc(srv, slot);
        return;
    }

    /*
     * Face the target for the whole engagement, not only once in reach.
     *
     * This used to be set *after* the range check, which returns early while
     * closing — so an npc chasing the player never turned to face them, and
     * because FACE_ENTITY is a latch it kept pointing wherever it last looked.
     * A goblin would walk sideways across the yard still staring at the spot
     * it was standing when the fight started.
     *
     * Set once here, before any early return, and only when it changes — the
     * change-gate is inside `ToriRSServer_NpcFacePlayer`, which is where all five
     * facing sites get it now rather than only this one.
     *
     * `player` is `players[npc->combat_target]`, resolved above: the pid, not
     * "the local player". Every observer's NPC_INFO carries the same absolute
     * number, so both clients see the goblin facing the person it is fighting.
     */
    ToriRSServer_NpcFacePlayer(npc, npc->combat_target);

    /* A ranged or magic attacker also needs *approached* line of sight before
     * its reach counts — the same cast `npc_run_mode`'s AP dispatch makes
     * (player → npc, backwards, because that is the direction the reference
     * asks it in). Without this, giving the Inferno's rangers their
     * wiki-stated arena-wide `attackrange` let a retaliating Jal-Xil shoot
     * through the pillars, and hiding behind one is the fight's whole
     * mechanic. Melee (`attackrange <= 1`) keeps the plain adjacency test:
     * the shared-edge wall check in the approach already answers fences. */
    int npc_size = npc->size > 0 ? npc->size : 1;
    int in_reach = in_npc_attack_range(player, npc);

    if( in_reach && npc_def(npc)->attackrange > 1 &&
        !ToriRSServer_SceneApproached(
            npc->level, player->x, player->z, npc->x, npc->z, 1, 1, npc_size, npc_size) )
        in_reach = 0;

    if( !in_reach )
    {
        /*
         * Pursue — the reference's `aiMode()`: path to the target, take one
         * tile of it, and drop the target instead if this npc does not chase.
         *
         * The destination is the tile *beside* the player rather than the
         * player's own, which is where squaring up comes from: melee cannot
         * reach a diagonal, so an npc that closed to the nearest corner would
         * stand there in combat and never swing.
         *
         * What this replaces was a single greedy step with the whole approach
         * rule inlined beside it, and no route behind it — "a blocked step is
         * simply not taken, the npc will try again next tick". That is only
         * true if the block goes away, and a wall does not: the npc walked into
         * the same tile every tick for the rest of the fight while the player
         * strolled off. It is the reason npcs did not pursue at all, and it was
         * invisible in open ground, which is where anyone testing stands.
         */
        /* The destination is the player under a 1x1 adjacent approach — melee
         * cannot reach a diagonal, and standing on the player's tile is wrong.
         * What this replaces was a single greedy step with the whole approach
         * rule inlined beside it, and no route behind it — "a blocked step is
         * simply not taken, the npc will try again next tick". That is only
         * true if the block goes away, and a wall does not. */
        /*
         * `mover_size` is the NPC's own footprint, not 1.
         *
         * The approach and the reach test have to be asking the same question:
         * this decides when the npc stops walking, `in_npc_attack_range`
         * decides when it swings, and a 2x2 npc that stopped on the anchor
         * rule stopped one tile short of where the swing rule wanted it — or,
         * where the map allowed, on top of the player. `mover_size` is the
         * only thing `collision_test_rect_adjacent` needs to answer for a
         * rectangle instead of a tile (and it also reads the shared edge's
         * wall bit, so an npc no longer counts a fence as arrival).
         */
        struct CollisionApproach approach = {
            .kind = COLL_APPROACH_RECT_ADJACENT,
            .loc_width = 1,
            .loc_length = 1,
            .mover_size = npc->size > 0 ? npc->size : 1,
        };

        /* Moves *then* gives up, which is the reference's order
         * (`const moved = this.updateMovement(); if (moved && !givechase)`) and
         * visible: a `givechase=no` npc takes one step before turning away. */
        if( ToriRSServer_WorldNpcWalkToApproach(npc, player->x, player->z, &approach) &&
            !npc_def(npc)->givechase )
            ToriRSServer_CombatStopNpc(srv, slot);
        return;
    }

    /* Facing is set above, before the approach returns early — an npc has to
     * face the player while chasing, not only once it arrives. */

    /* A specialised OP handler may have selected an AP action.  The ordinary
     * mode phase cannot consume it while this NPC is in combat (by design), so
     * combat does it here before its next clock-owned OP swing. */
    if( npc_dispatch_combat_applayer_mode(srv, npc, slot) )
        return;

    if( srv->tick < npc->attack_clock )
        return;
    npc->attack_clock = srv->tick + npc_def(npc)->attackrate;

    /*
     * The npc's swing is content's — [ai_opplayer2,<npc>] owns it and does
     * npc_anim itself. Fire the trigger; content handles the animation, roll,
     * damage and retaliation queue.
     */
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_AI_OPPLAYER2, npc->type, -1, slot);
}

void
ToriRSServer_CombatRespawnTick(struct ToriRSServer* srv)
{
    for( int slot = 0; slot < TORIRSSERVER_NPC_MAX; slot++ )
    {
        struct ToriRSServerNpc* npc = &srv->npcs[slot];

        if( npc->active || !npc->def || npc->respawn_tick < 0 ||
            srv->tick < npc->respawn_tick )
            continue;

        npc->generation = (uint16_t)(npc->generation + 1u);
        if( npc->generation == 0 )
            npc->generation = 1;
        npc->active = 1;
        /* Should already be 0 by now — the death that set respawn_tick ran
         * its own tick's phase_cleanup reap strictly before any later tick's
         * phase_world could reach here (see docs/torirs_server_npc_slot_reap.md).
         * Explicit anyway: a slot stuck at pending_free=1 would be silently
         * invisible to npc_spawn's scan forever. */
        npc->pending_free = 0;
        npc->respawn_tick = -1;
        npc->death_tick = -1;
        npc->x = npc->spawn_x;
        npc->z = npc->spawn_z;
        npc->level = npc->spawn_level;
        npc->hitpoints = npc->base_hitpoints;
        npc->combat_target = -1;
        /*
         * A respawn is a fresh npc, and that has to include what it is *doing*.
         *
         * The death above puts the mode back already; this is the second half of
         * the same claim, for the state a script can still change while the
         * corpse is on the ground (`[ai_queue3]` runs during the death) and for
         * `huntmode`, which death does not touch at all because it is a standing
         * disposition rather than a target. `npc_sethuntmode` overrides the
         * record per npc — a boss that turned aggressive for one phase stayed
         * aggressive for every life after it, which reads as the record being
         * wrong rather than as the override outliving the npc it was made for.
         *
         * Content that wants any of it back gets it back: `spawn_pending` below
         * re-runs `[ai_spawn]`, which is where those overrides are set.
         */
        ToriRSServer_NpcResetDefaults(npc);
        npc->huntmode = npc_def(npc)->huntmode;
        /* Runtime vars describe one life, not one pool slot. A type change
         * keeps them; a respawn is the boundary that clears them. */
        memset(npc->script_vars, 0, sizeof(npc->script_vars));
        /*
         * Damage attribution and its warning latch, for the same reason and at
         * the same boundary: a respawned npc has been damaged by nobody, and an
         * ironman who was warned about the LAST one must be warned about this
         * one. Left alone at death on purpose -- a corpse another player damaged
         * is still a corpse they damaged, and the drop table reads it. */
        memset(npc->damaged_by_players, 0, sizeof(npc->damaged_by_players));
        memset(npc->noloot_warned_players, 0, sizeof(npc->noloot_warned_players));
        /*
         * And the npc's own queue, which is where DAMAGE lives.
         *
         * `npc_queue(2, $damage, $delay)` is how every hit in the tree is
         * delivered — combat_stats.rs2 queues it, `[ai_queue2,_]` turns it into
         * a splat — so a queue entry that outlives the npc it was armed on is a
         * hit landing on somebody else.
         *
         * The killing blow already empties the queue (`ToriRSServer_CombatHitNpc`,
         * at DEATH_QUEUED). What it cannot empty is what arrives AFTER it, and
         * two attackers on one tick is all that takes: the second swing queues
         * onto an npc that is already dying, the npc phase never drains it (it
         * skips anything holding a `death_tick`), and the entry sat in the slot
         * until this function handed the slot back. The respawned monster then
         * walked into the world and immediately took the previous life's hit —
         * "that thing spawned already hurt", or, when a player was watching the
         * spot, a hitsplat on an npc nobody had swung at.
         *
         * This function reactivates the record IN PLACE, unlike `npc_spawn`,
         * which memsets first. Everything a new life must not inherit therefore
         * has to be named here, and everything below this line is the rest of
         * that list: state that describes one life rather than one pool slot.
         * `test-ToriRSServer`'s NPC LIFECYCLE UNDER DAMAGE section is what catches
         * the next one that goes missing.
         */
        for( int q = 0; q < TORIRSSERVER_NPC_QUEUE_MAX; q++ )
            npc->queue[q].active = 0;
        /* The splat list and the pair the classic mask spends. Cleared every
         * tick in phase_cleanup anyway, so this is belt and braces — but a
         * respawn arriving with a full four-splat list would silently drop the
         * first real hit of its new life, which is the same defect wearing a
         * different hat. */
        memset(npc->hitmarks, 0, sizeof(npc->hitmarks));
        npc->hitmark_count = 0;
        npc->damage = 0;
        npc->damage_type = 0;
        /* Drained stats are damage by another name — `npc_statsub` is what a
         * BGS, a Darklight or an Arclight spec leaves behind, and none of it
         * belongs to the monster that replaces the one it was spent on. */
        memset(npc->stat_drain, 0, sizeof(npc->stat_drain));
        /* A parked script's deadline and a freeze both describe the life that
         * ended: a monster that died mid-`npc_delay` used to come back unable
         * to take its turn — which includes draining its queue, so its first
         * hits went missing too. */
        npc->delayed_until = 0;
        npc->frozen_ticks = 0;
        /* A pending `npc_changetype` reversion describes the life that ended.
         * Left running it would fire on whatever form the new life is standing
         * in and change it out from under a fresh `[ai_spawn]`. */
        npc->changetype_delay = 0;
        /* A new life: its death has not been shown yet. */
        npc->death_seq_sent = 0;
        npc->death_seq_tick = -1;
        npc->scripted_death_pending = 0;
        npc->poison_severity = 0;
        npc->poison_clock = 0;
        npc->poison_source_pid = -1;
        npc->poison_source_gen = 0;
        npc->patrol_index = 0;
        npc->patrol_pause = 0;
        npc->attack_clock = 0;
        npc->step_dir = -1;
        npc->run_dir = -1;
        /* A respawn is a fresh npc to every observer, so it faces the way a
         * fresh npc does rather than wherever it was walking when it died. */
        npc->face_dir = TORIRSSERVER_FACE_SOUTH;
        npc->masks = 0;
        npc->last_step_x = npc->x - 1;
        npc->last_step_z = npc->z;
        npc->follow_x = npc->last_step_x;
        npc->follow_z = npc->last_step_z;
        npc->waypoint_index = -1;
        npc->stuck_counter = 0;
        ToriRSServer_WorldNpcOccupancy(npc, 1);
        /* Nothing to clear: each player's own `npc_tracked` set dropped this
         * npc on the tick it went inactive, so the next NPC_INFO adds it as a
         * new entity — which is what a respawn is from the client's side. */
        /* And `[ai_spawn]` runs again, which is where a behaviour's timer is
         * set. Without this a monster's heartbeat stops the first time it dies
         * and never comes back — the world decays one death at a time, and the
         * npc that stops behaving is never the one you were watching. */
        npc->spawn_pending = 1;
        /* Staggered exactly as a fresh spawn is, through the one function that
         * knows the roam cadence. It used to be `+ TORIRSSERVER_ATTACK_SPEED` — an
         * attack rate borrowed to mean a walk delay, which is the kind of reuse
         * that survives because both numbers happen to be small. */
        ToriRSServer_WorldNpcRoamStagger(srv, npc);
    }
}
