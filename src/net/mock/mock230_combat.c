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
 * hand-written table anywhere. See mock230_objinfo.c for how those are read and
 * why the ids are trustworthy.
 *
 * The test for whether something belongs here: does it name a tick or a
 * distance, or does it name a bonus or a formula? The first is this file, the
 * second is content.
 */

#include "mock230.h"

#include "mock230_content.h"
#include "mock230_scene.h"

#include "ss_trigger.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
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
 * `mock230_world_npc_spawn` always fills `def` in — a spawn nothing describes
 * gets `mock230_content_npc_default()` rather than NULL — so the
 * `npc->def ? … : <constant>` this replaces was a branch that could not be
 * taken, and each of those constants was a rate stated in C beside a content
 * file already stating it. Going through here means a tick count has exactly one
 * source: the record, whose own last resort is the `[default]` block in
 * `general/configs/npc_default.npc`.
 */
static const struct Mock230NpcDef*
npc_def(const struct Mock230Npc* npc)
{
    return npc->def ? npc->def : mock230_content_npc_default();
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
    const struct Mock230Player* player,
    const struct Mock230Npc* npc,
    int range)
{
    int dx;
    int dz;

    if( player->level != npc->level )
        return 0;

    dx = abs_of(player->x - npc->x);
    dz = abs_of(player->z - npc->z);

    if( range <= 1 )
        return (dx + dz) == 1;

    return tile_distance(player->x, player->z, npc->x, npc->z) <= range;
}

/** Player weapon reach from the cache's `weapon_attackrange` param, capped at
 *  10 the way LostCity's `~player_attackrange` is. Unarmed / missing = 1. */
static int
player_weapon_attackrange(const struct Mock230Player* player)
{
    int weapon = player->worn[MOCK230_WEAR_WEAPON].obj_id;
    int param_id;
    const struct Mock230ObjParam* p;
    int range = 1;

    if( weapon < 0 )
        return 1;
    param_id = mock230_content_symbol(MOCK230_PACK_PARAM, "weapon_attackrange");
    if( param_id < 0 )
        return 1;
    p = mock230_obj_param(weapon, param_id);
    if( p && p->ival > 0 )
        range = p->ival;
    if( range > 10 )
        range = 10;
    return range;
}

static int
in_player_attack_range(
    const struct Mock230Player* player,
    const struct Mock230Npc* npc)
{
    return in_attack_range_with(player, npc, player_weapon_attackrange(player));
}

static int
in_npc_attack_range(
    const struct Mock230Player* player,
    const struct Mock230Npc* npc)
{
    return in_attack_range_with(player, npc, npc_def(npc)->attackrange);
}

/*
 * The priority gate — the reference's `PathingEntity.playAnimation`.
 *
 * `incumbent` is whatever has already been queued for this tick (-1 if
 * nothing), `wanted` is the new sequence. The rule is `>=`, not `>`: two
 * animations of equal priority mean the later one wins, which is what makes a
 * repeated swing re-trigger rather than stick on its first frame.
 *
 * The header on mock230_anim_play_npc has the whole of why this exists.
 */
static int
anim_wins(int incumbent, int wanted)
{
    if( incumbent < 0 )
        return 1;
    return mock230_seq_priority(wanted) >= mock230_seq_priority(incumbent);
}

int
mock230_anim_play_npc(
    struct Mock230Npc* npc,
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
    if( !anim_wins(npc->anim_id, seq_id) )
        return 0;
    npc->anim_id = seq_id;
    npc->anim_delay = delay;
    npc->masks |= MOCK230_NMASK_ANIM;
    return 1;
}

int
mock230_anim_play_player(
    struct Mock230Player* player,
    int seq_id,
    int delay)
{
    if( seq_id < 0 )
        return 0;
    if( !anim_wins(player->anim_id, seq_id) )
        return 0;
    player->anim_id = seq_id;
    player->anim_delay = delay;
    player->masks |= MOCK230_PMASK_SEQUENCE;
    return 1;
}

static void
play_npc_seq(struct Mock230Npc* npc, int seq_id)
{
    mock230_anim_play_npc(npc, seq_id, 0);
}

/*
 * How far an npc's combat noise carries, in tiles.
 *
 * LostCity's `[proc,npc_death]` passes 12 to `~sound_within_distance` with an
 * `// osrs` note, and it is the only figure any reference states for this, so
 * the same number answers for all three sounds rather than three invented ones.
 */
#define MOCK230_NPC_SOUND_TILES 12

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
    struct Mock230Server* srv,
    struct Mock230Npc const* npc,
    int sound_id,
    int delay)
{
    if( sound_id < 0 )
        return;
    for( int i = 0; i < srv->player_count; i++ )
    {
        struct Mock230Player* player = &srv->players[i];

        if( !player->active || player->level != npc->level )
            continue;
        if( tile_distance(player->x, player->z, npc->x, npc->z) >
            MOCK230_NPC_SOUND_TILES )
            continue;
        /* One loop, not zero, and this is checkable rather than a preference:
         * `RS_Audio_QueueEffect` (src/game/rs_audio.c) *refuses* `loops == 0`,
         * matching the reference's `Message.queueSoundEffect` requiring
         * `var1 != 0` — the count it hands the mixer is `loops - 1`, so zero
         * means the caller asked for nothing. LostCity's own
         * `~sound_within_distance` passes 0 and would be silent here; every
         * direct `sound_synth` call site in both trees passes 1. */
        mock230_send_synth_sound(player, sound_id, 1, delay);
    }
}


static int
hitsplat_block(void)
{
    int id = mock230_content_symbol(MOCK230_PACK_HITSPLAT, "hitsplat_block");

    return id >= 0 ? id : 26;
}


/* ------------------------------------------------------------------ */
/* Disengaging                                                         */
/* ------------------------------------------------------------------ */

/*
 * FACE_ENTITY is a latch, not a per-tick state.
 *
 * The client turns the entity toward whatever the last FACE_ENTITY named and
 * keeps it there — there is no timeout and no implicit clear. So every path
 * that drops a combat target has to send -1 (65535 on the wire, which is how
 * the client spells "face nothing"), and every path means: the target died, the
 * player died, the player walked away, or the player started doing something
 * else.
 *
 * Missing one of them is invisible until you notice a goblin has been staring
 * at a spot on the ground since the fight before last.
 */
void
mock230_combat_stop_player_at(struct Mock230Player* player)
{
    /* LostCity PathingEntity.clearInteraction clears target and targetOp as
     * one operation.  `combat_target` is the first half; the OPNPC/p_opnpc
     * interaction is the second.  Leaving it armed lets a death resume the
     * combat script on a later tick and acquire the same target again. */
    player->combat_target = -1;
    mock230_world_interaction_clear_at(player);
    if( player->face_entity != -1 )
    {
        player->face_entity = -1;
        player->masks |= MOCK230_PMASK_FACE_ENTITY;
    }
}

void
mock230_combat_stop_player(struct Mock230Server* srv)
{
    mock230_combat_stop_player_at(srv->active_player);
}

void
mock230_combat_stop_npc(
    struct Mock230Server* srv,
    int slot)
{
    struct Mock230Npc* npc;

    if( slot < 0 || slot >= MOCK230_NPC_MAX )
        return;
    npc = &srv->npcs[slot];
    npc->combat_target = -1;
    if( npc->face_entity != -1 )
    {
        npc->face_entity = -1;
        npc->masks |= MOCK230_NMASK_FACE_ENTITY;
    }
}

/* ------------------------------------------------------------------ */
/* Stats                                                               */
/* ------------------------------------------------------------------ */

void
mock230_combat_stat_mark(
    struct Mock230Player* player,
    int stat)
{
    if( stat >= 0 && stat < MOCK230_STAT_COUNT )
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
mock230_combat_sync_hitpoints(struct Mock230Player* player)
{
    player->max_hitpoints = player->stat_level[MOCK230_STAT_HITPOINTS];
    if( player->max_hitpoints <= 0 )
        player->max_hitpoints = 1;
    if( player->hitpoints > player->max_hitpoints )
        player->hitpoints = player->max_hitpoints;
    player->stat_boosted[MOCK230_STAT_HITPOINTS] = player->hitpoints;
    mock230_combat_stat_mark(player, MOCK230_STAT_HITPOINTS);
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
 * — used to be two literals in `mock230_world_init` stating the level *and* the
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
mock230_combat_level_for_xp(int experience)
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
mock230_combat_xp_for_level(int level)
{
    if( level <= 1 )
        return 0;
    if( level > 99 )
        level = 99;
    ensure_xp_table();
    return g_xp_table[level - 2];
}

void
mock230_combat_set_level(
    struct Mock230Player* player,
    int stat,
    int level)
{
    assert(player);
    assert(stat >= 0 && stat < MOCK230_STAT_COUNT);
    if( level < 1 )
        level = 1;
    if( level > 99 )
        level = 99;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
    player->stat_xp_tenths[stat] = mock230_combat_xp_for_level(level) * 10;
    if( stat == MOCK230_STAT_HITPOINTS )
    {
        player->hitpoints = level;
        mock230_combat_sync_hitpoints(player);
    }
    mock230_combat_stat_mark(player, stat);
}

void
mock230_combat_add_xp(
    struct Mock230Server* srv,
    int stat,
    int tenths)
{
    struct Mock230Player* player = srv->active_player;
    int before;

    if( stat < 0 || stat >= MOCK230_STAT_COUNT || tenths <= 0 )
        return;
    before = player->stat_level[stat];
    /* Traced because an xp *rate* bug is otherwise unobservable: the on-screen
     * counter shows the seeded total, UPDATE_STAT carries the number but the
     * log prints only its payload length, and a wrong rate never changes a
     * level often enough to notice. Tenths, so 200 reads as 20.0 xp. */
    if( srv->verbose )
        printf("mock230: xp stat=%d +%d.%d (tenths=%d)\n",
               stat,
               tenths / 10,
               tenths % 10,
               tenths);
    player->stat_xp_tenths[stat] += tenths;
    player->stat_level[stat] =
        mock230_combat_level_for_xp(player->stat_xp_tenths[stat] / 10);
    if( player->stat_level[stat] != before )
    {
        /* A hitpoints level-up raises the ceiling but does not heal, which is
         * what OldSchool does and is also the only behaviour that cannot
         * surprise someone mid-fight. */
        if( stat == MOCK230_STAT_HITPOINTS )
            mock230_combat_sync_hitpoints(player);
        mock230_scripts_run_trigger_specific(srv, SS_TRIGGER_ADVANCESTAT, stat, -1, -1);
    }
    if( player->stat_boosted[stat] < player->stat_level[stat] &&
        stat != MOCK230_STAT_HITPOINTS )
        player->stat_boosted[stat] = player->stat_level[stat];
    mock230_combat_stat_mark(player, stat);
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

void
mock230_combat_hit_npc(
    struct Mock230Server* srv,
    int slot,
    int type,
    int amount)
{
    struct Mock230Npc* npc;

    if( slot < 0 || slot >= MOCK230_NPC_MAX )
        return;
    npc = &srv->npcs[slot];
    if( !npc->active || npc->death_tick >= 0 )
        return;

    if( amount > npc->hitpoints )
        amount = npc->hitpoints;
    npc->hitpoints -= amount;

    /* One mask carries the splat and the bar. A zero-damage hit is a *block*
     * splat rather than nothing — the reference shows those, and without them a
     * miss is indistinguishable from the server having ignored the swing. */
    npc->damage = amount;
    npc->damage_type = amount > 0 ? type : hitsplat_block();
    npc->hitpoints = npc->hitpoints < 0 ? 0 : npc->hitpoints;
    npc->max_hitpoints = npc->max_hitpoints > 0 ? npc->max_hitpoints : 1;
    npc->masks |= MOCK230_NMASK_DAMAGE;

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
     * on the opening hit is what staggers the two cadences apart. */
    if( npc->combat_target < 0 )
    {
        npc->combat_target = srv->active_player ? srv->active_player->pid : 0;
        npc->attack_clock = npc_def(npc)->attackrate / 2;
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
     */
    mock230_npc_face_player(npc, npc->combat_target);
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
         * `mock230_combat_npc_tick` picks it up from `death_stage`.
         *
         * The masks this no longer touches are the point: the flinch animation
         * and its noise, queued a few lines up, now reach the client on their
         * own tick instead of being overwritten by a death animation in the
         * same one.
         */
        npc->death_stage = MOCK230_DEATH_QUEUED;
        npc->death_tick = srv->tick + 1;
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
        mock230_combat_stop_npc(srv, slot);
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
        npc->mode = mock230_world_npc_default_mode(npc);
        for( int i = 0; i < MOCK230_PLAYER_MAX; i++ )
        {
            if( srv->players[i].active && srv->players[i].combat_target == slot )
            {
                npc->death_credit_players[i] = 1;
                mock230_combat_stop_player_at(&srv->players[i]);
            }
        }
        /* The drop table does *not* run here. `[proc,npc_default_death]` calls
         * `gosub(npc_death)` first and only reaches its `obj_add` once that has
         * returned — which is after `npc_del`. So the loot lands on the tick the
         * corpse disappears, and `mock230_combat_npc_tick` is where that is. */
    }
}

void
mock230_combat_hit_player(
    struct Mock230Server* srv,
    int type,
    int amount)
{
    struct Mock230Player* player = srv->active_player;

    if( amount > player->hitpoints )
        amount = player->hitpoints;
    player->hitpoints -= amount;

    player->damage = amount;
    player->damage_type = amount > 0 ? type : hitsplat_block();
    player->hitpoints = player->hitpoints < 0 ? 0 : player->hitpoints;
    player->masks |= MOCK230_PMASK_DAMAGE;
    mock230_combat_sync_hitpoints(player);

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
         * `mock230_combat_player_tick`, on the tick the script's `stat_heal`
         * puts hitpoints back above zero — so even the length of the death is
         * the script's.
         */
        player->dying = 1;
        mock230_combat_stop_player(srv);
        for( int i = 0; i < MOCK230_NPC_MAX; i++ )
        {
            if( srv->npcs[i].combat_target == player->pid )
                mock230_combat_stop_npc(srv, i);
        }
        /* Content queues [queue,player_death] from wrappers; raw hit paths fire
         * PLAYERDEATH so the sequence still starts. */
        mock230_scripts_run_trigger(srv, SS_TRIGGER_PLAYERDEATH, -1, -1, -1);
    }
}

/* ------------------------------------------------------------------ */
/* Engagement                                                          */
/* ------------------------------------------------------------------ */

/** Does this npc's cache record offer an Attack option? That is the same test
 *  the client's minimenu makes, so the two ends cannot disagree about what is
 *  a valid target. */
int
mock230_combat_attackable(int npc_type)
{
    const struct Mock230NpcInfo* info = mock230_npcinfo(npc_type);

    for( int i = 0; i < 5; i++ )
    {
        if( info->ops[i] && strcmp(info->ops[i], "Attack") == 0 )
            return 1;
    }
    return 0;
}

void
mock230_combat_engage(
    struct Mock230Server* srv,
    int slot)
{
    struct Mock230Player* player = srv->active_player;
    struct Mock230Npc* npc;

    if( slot < 0 || slot >= MOCK230_NPC_MAX )
        return;
    npc = &srv->npcs[slot];
    if( !npc->active || npc->death_tick >= 0 )
        return;
    if( player->dying )
        return;

    player->combat_target = slot;
    /* Swing on the tick the player arrives rather than after a full interval:
     * an opening delay reads as the click having been dropped. */
    player->attack_clock = 0;

    {
        struct CollisionApproach approach;
        const struct Mock230NpcInfo* info = mock230_npcinfo(npc->type);
        int size = info ? info->size : 1;

        /*
         * Arm OPNPC2 so content owns the swing loop (LostCity: combat is the
         * interaction). The engine clock no longer fires OPNPC2 — p_opnpc(2)
         * re-arms this after each swing / action_delay wait.
         */
        mock230_world_interaction_set(srv, MOCK230_INTERACT_NPC, 2, slot, npc->type,
                                      npc->x, npc->z, npc->level, size, size);
        mock230_scene_npc_approach(size, &approach);
        mock230_world_walk_to_approach(srv, npc->x, npc->z, &approach);
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
mock230_combat_player_approach(struct Mock230Server* srv)
{
    struct Mock230Player* player = srv->active_player;
    struct Mock230Npc* npc;

    if( player->dying || player->combat_target < 0 )
        return;
    npc = &srv->npcs[player->combat_target];
    if( !npc->active || npc->death_tick >= 0 )
        return;

    if( in_player_attack_range(player, npc) )
    {
        mock230_world_steps_clear(player);
        return;
    }
    /* Nothing else is walking the player while a combat target is set: every
     * other click clears the target first (see the OPNPC/OPLOC/MOVE handlers),
     * so this owns the step queue and can recompute it outright. */
    {
        struct CollisionApproach approach;
        const struct Mock230NpcInfo* info = mock230_npcinfo(npc->type);
        mock230_scene_npc_approach(info ? info->size : 1, &approach);
        mock230_world_walk_to_approach(srv, npc->x, npc->z, &approach);
    }
}

void
mock230_combat_player_tick(struct Mock230Server* srv)
{
    struct Mock230Player* player = srv->active_player;
    struct Mock230Npc* npc;

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
        mock230_combat_stop_player(srv);
        return;
    }

    /* Facing is mock230_player_set_face_entity in phase_player (before
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
mock230_combat_level(const struct Mock230Player* player)
{
    /* OldSchool's melee formula:
     * floor(0.25 * (defence + hitpoints + floor(prayer / 2)) + 0.325 * (attack
     * + strength)). Scaled by 1000 so it stays in integers. */
    int base = player->stat_level[MOCK230_STAT_DEFENCE] +
               player->stat_level[MOCK230_STAT_HITPOINTS] +
               (player->stat_level[MOCK230_STAT_PRAYER] / 2);
    int melee = player->stat_level[MOCK230_STAT_ATTACK] +
                player->stat_level[MOCK230_STAT_STRENGTH];

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
target_within_maxrange(
    const struct Mock230Player* player,
    const struct Mock230Npc* npc)
{
    int range = npc_def(npc)->maxrange;
    int dx = abs_of(player->x - npc->spawn_x);
    int dz = abs_of(player->z - npc->spawn_z);

    if( dx > range + 1 || dz > range + 1 )
        return 0;
    if( dx == range + 1 && dz == range + 1 )
        return 0;
    return 1;
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
static struct Mock230Player*
nearest_victim(
    struct Mock230Server* srv,
    const struct Mock230Npc* npc)
{
    struct Mock230Player* best = NULL;
    int best_distance = 0;

    for( int i = 0; i < srv->player_count; i++ )
    {
        struct Mock230Player* player = &srv->players[i];
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
    struct Mock230Server* srv,
    int slot)
{
    struct Mock230Npc* npc = &srv->npcs[slot];
    struct Mock230Player* player;
    int npc_level;

    /* `npc->huntmode`, not `npc->def->huntmode`: the def seeds it at spawn and
     * `npc_sethuntmode` overrides it per npc. Reading the def here would make
     * that opcode a no-op for the one thing content uses it for. */
    if( !npc->def || npc->huntmode != MOCK230_HUNT_AGGRESSIVE )
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
    if( !target_within_maxrange(player, npc) )
        return;

    npc_level = mock230_npcinfo(npc->type)->combat_level;
    if( npc_level > 0 && mock230_combat_level(player) > npc_level * 2 )
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
 *     D+1   MOCK230_DEATH_QUEUED  — stop, face nothing, arrivedelay
 *     D+1+a MOCK230_DEATH_ARRIVE  — death sound and death animation
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
    struct Mock230Server* srv,
    int slot)
{
    struct Mock230Npc* npc = &srv->npcs[slot];

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
    case MOCK230_DEATH_QUEUED:
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
            npc->death_stage = MOCK230_DEATH_ARRIVE;
            npc->death_tick =
                srv->tick + (npc->last_movement == srv->tick - 1 ? 1 : 2);
            return;
        }
        /* Standing still: the reference's `npc_arrivedelay` returns without
         * suspending and the next line runs in the same tick. */
        /* FALLTHROUGH */

    case MOCK230_DEATH_ARRIVE:
        /*
         * Sound then animation, in the reference's order, and both a clear tick
         * after the flinch that preceded them — which is the whole point of the
         * schedule. They used to be queued in `mock230_combat_hit_npc` beside
         * the flinch, so the death animation overwrote the flinch in one mask
         * and the two noises arrived together.
         */
        npc_sound_nearby(srv, npc, npc->death_sound, 0);
        play_npc_seq(npc, npc->death_seq);
        npc->death_stage = MOCK230_DEATH_CORPSE;
        npc->death_tick = srv->tick + npc_def(npc)->death_delay;
        return;

    case MOCK230_DEATH_CORPSE:
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
        npc->death_stage = MOCK230_DEATH_REAP;
        npc->death_tick = srv->tick;
        srv->loot_credit_armed = 1;
        srv->loot_credit_npc_type = npc->type;
        srv->loot_credit_event_id = ++srv->loot_credit_seq;
        memcpy(srv->loot_credit_players, npc->death_credit_players,
               sizeof(srv->loot_credit_players));
        mock230_world_npc_died(srv, slot);
        srv->loot_credit_armed = 0;
        memset(srv->loot_credit_players, 0, sizeof(srv->loot_credit_players));
        memset(npc->death_credit_players, 0, sizeof(npc->death_credit_players));
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

    case MOCK230_DEATH_REAP:
    default:
        /*
         * An `[ai_queue3]` that puts hitpoints back is not a death.
         *
         * `[ai_queue3,kalphite_queen]` is the case: `npc_changetype` plus
         * `npc_statheal(hitpoints, 0, 100)`, and no `gosub(npc_death)` — which
         * in the reference is precisely how content says "this one does not die
         * here, it changes form". The engine owns the death now, so the same
         * sentence has to be readable from the hitpoints, and it is the rule the
         * player side already uses (`mock230_combat_player_tick`: hitpoints back
         * above zero means the death is over).
         */
        if( npc->hitpoints > 0 )
        {
            npc->death_tick = -1;
            npc->death_stage = MOCK230_DEATH_NONE;
            return;
        }
        mock230_world_npc_occupancy(npc, 0);
        npc->active = 0;
        npc->death_tick = -1;
        npc->death_stage = MOCK230_DEATH_NONE;
        /* A script may have armed `npc_setrespawn` during [ai_queue3] (GWD
         * minion sync). Keep that clock; otherwise use the def rate. */
        if( npc->respawn_tick < 0 )
            npc->respawn_tick = srv->tick + npc_def(npc)->respawnrate;
        return;
    }
}

void
mock230_combat_npc_tick(
    struct Mock230Server* srv,
    int slot)
{
    struct Mock230Npc* npc = &srv->npcs[slot];
    struct Mock230Player* player;

    /* Death first: a dead npc neither swings nor roams, and its corpse has to
     * outlive the killing blow long enough for the animation to play. */
    if( npc->death_tick >= 0 )
    {
        if( srv->tick >= npc->death_tick )
            npc_death_step(srv, slot);
        return;
    }

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
        mock230_combat_stop_npc(srv, slot);
        return;
    }
    /* This npc's turn is the *target's* turn: everything below writes to the
     * player it is fighting, and phase 4 runs outside any per-player loop. */
    mock230_world_set_active(srv, player);
    if( player->dying )
    {
        mock230_combat_stop_npc(srv, slot);
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
    if( !target_within_maxrange(player, npc) )
    {
        mock230_combat_stop_npc(srv, slot);
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
     * change-gate is inside `mock230_npc_face_player`, which is where all five
     * facing sites get it now rather than only this one.
     *
     * `player` is `players[npc->combat_target]`, resolved above: the pid, not
     * "the local player". Every observer's NPC_INFO carries the same absolute
     * number, so both clients see the goblin facing the person it is fighting.
     */
    mock230_npc_face_player(npc, npc->combat_target);

    if( !in_npc_attack_range(player, npc) )
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
        struct CollisionApproach approach = {
            .kind = COLL_APPROACH_RECT_ADJACENT,
            .loc_width = 1,
            .loc_length = 1,
            .mover_size = 1,
        };

        /* Moves *then* gives up, which is the reference's order
         * (`const moved = this.updateMovement(); if (moved && !givechase)`) and
         * visible: a `givechase=no` npc takes one step before turning away. */
        if( mock230_world_npc_walk_to_approach(npc, player->x, player->z, &approach) &&
            !npc_def(npc)->givechase )
            mock230_combat_stop_npc(srv, slot);
        return;
    }

    /* Facing is set above, before the approach returns early — an npc has to
     * face the player while chasing, not only once it arrives. */

    if( npc->attack_clock > 0 )
    {
        npc->attack_clock--;
        return;
    }
    npc->attack_clock = npc_def(npc)->attackrate;

    /*
     * The npc's swing is content's — [ai_opplayer2,<npc>] owns it and does
     * npc_anim itself. Fire the trigger; content handles the animation, roll,
     * damage and retaliation queue.
     */
    mock230_scripts_run_trigger(srv, SS_TRIGGER_AI_OPPLAYER2, npc->type, -1, slot);
}

void
mock230_combat_respawn_tick(struct Mock230Server* srv)
{
    for( int slot = 0; slot < MOCK230_NPC_MAX; slot++ )
    {
        struct Mock230Npc* npc = &srv->npcs[slot];

        if( npc->active || !npc->def || npc->respawn_tick < 0 ||
            srv->tick < npc->respawn_tick )
            continue;

        npc->active = 1;
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
        npc->mode = mock230_world_npc_default_mode(npc);
        npc->huntmode = npc_def(npc)->huntmode;
        /* Runtime vars describe one life, not one pool slot. A type change
         * keeps them; a respawn is the boundary that clears them. */
        memset(npc->script_vars, 0, sizeof(npc->script_vars));
        npc->patrol_index = 0;
        npc->patrol_pause = 0;
        npc->attack_clock = 0;
        npc->step_dir = -1;
        /* A respawn is a fresh npc to every observer, so it faces the way a
         * fresh npc does rather than wherever it was walking when it died. */
        npc->face_dir = MOCK230_FACE_SOUTH;
        npc->masks = 0;
        npc->last_step_x = npc->x - 1;
        npc->last_step_z = npc->z;
        npc->follow_x = npc->last_step_x;
        npc->follow_z = npc->last_step_z;
        npc->waypoint_index = -1;
        npc->stuck_counter = 0;
        mock230_world_npc_occupancy(npc, 1);
        /* Nothing to clear: each player's own `npc_tracked` set dropped this
         * npc on the tick it went inactive, so the next NPC_INFO adds it as a
         * new entity — which is what a respawn is from the client's side. */
        /* And `[ai_spawn]` runs again, which is where a behaviour's timer is
         * set. Without this a monster's heartbeat stops the first time it dies
         * and never comes back — the world decays one death at a time, and the
         * npc that stops behaving is never the one you were watching. */
        npc->spawn_pending = 1;
        /* Staggered exactly as a fresh spawn is, through the one function that
         * knows the roam cadence. It used to be `+ MOCK230_ATTACK_SPEED` — an
         * attack rate borrowed to mean a walk delay, which is the kind of reuse
         * that survives because both numbers happen to be small. */
        mock230_world_npc_roam_stagger(srv, npc);
    }
}
