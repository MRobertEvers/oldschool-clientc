/*
 * Combat, minus the combat.
 *
 * What is left in this file is the simulation around a fight: whose turn it is,
 * the attack clock, approaching and squaring up, facing, hitpoints, death and
 * respawn. Every number a swing is made of — the effective levels, the attack
 * and defence rolls, the max hit, the accuracy, the experience, the animations,
 * the protection prayers — is content, in
 * `skill_combat/combat_stats.rs2`, and this file calls into it twice:
 * `[proc,player_melee_swing]` and `[proc,npc_meleeattack]`.
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

static void
play_player_seq(struct Mock230Player* player, int seq_id)
{
    mock230_anim_play_player(player, seq_id, 0);
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
    player->combat_target = -1;
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
 */
static int
level_for_xp(int experience)
{
    /* `table[i]` is the experience at which level `i + 2` begins, which is the
     * reference's indexing (`getExpByLevel(level)` reads `[level - 2]`). */
    static int table[99];
    static int built;

    if( !built )
    {
        long long accumulated = 0;

        for( int i = 0; i < 99; i++ )
        {
            int level = i + 1;

            accumulated +=
                (long long)floor((double)level + pow(2.0, (double)level / 7.0) * 300.0);
            table[i] = (int)(accumulated / 4);
        }
        built = 1;
    }

    for( int i = 98; i >= 0; i-- )
    {
        if( experience >= table[i] )
            return i + 2 < 99 ? i + 2 : 99;
    }
    return 1;
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
    player->stat_level[stat] = level_for_xp(player->stat_xp_tenths[stat] / 10);
    if( player->stat_level[stat] != before )
    {
        /* A hitpoints level-up raises the ceiling but does not heal, which is
         * what OldSchool does and is also the only behaviour that cannot
         * surprise someone mid-fight. */
        if( stat == MOCK230_STAT_HITPOINTS )
            mock230_combat_sync_hitpoints(player);
        mock230_scripts_run_hook(srv, srv->hooks.combat_levelup_message, NULL, 0);
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

    if( npc->hitpoints == 0 )
    {
        play_npc_seq(npc, npc->death_seq);
        /*
         * How long the corpse lies there is the record's, beside the animation
         * that has to finish inside it — `death_delay` on the npc, defaulting
         * through `general/configs/npc_default.npc`. It was three ticks in
         * mock230.h, which made the corpse of a boss and the corpse of a rat the
         * same length and neither of them changeable.
         *
         * Nothing needs to stop the corpse roaming: `advance_npcs` skips any npc
         * with a `death_tick`, so the `next_roam_tick` push that used to sit
         * here — a respawn constant used as a roam delay — was saying twice, in
         * the wrong units, what the line above already says.
         */
        npc->death_tick = srv->tick + npc_def(npc)->death_delay;
        /*
         * Capture kill attribution before combat_stop clears combat_target.
         * Clientscript 7192 needs the npc type + a per-kill event id; each
         * OBJ_ADD during [ai_queue3] then RUNCLIENTSCRIPTs the killers.
         */
        srv->loot_credit_armed = 1;
        srv->loot_credit_npc_type = npc->type;
        srv->loot_credit_event_id = ++srv->loot_credit_seq;
        memset(srv->loot_credit_players, 0, sizeof(srv->loot_credit_players));
        mock230_combat_stop_npc(srv, slot);
        for( int i = 0; i < MOCK230_PLAYER_MAX; i++ )
        {
            if( srv->players[i].active && srv->players[i].combat_target == slot )
            {
                srv->loot_credit_players[i] = 1;
                mock230_combat_stop_player_at(&srv->players[i]);
            }
        }
        /* The drop table is content: [ai_queue3,<npc>] with obj_add calls, the
         * same shape as LostCity's. mock230_scripts.c runs it and falls back to
         * the config's death_drop when nothing is bound. */
        mock230_world_npc_died(srv, slot);
        srv->loot_credit_armed = 0;
        memset(srv->loot_credit_players, 0, sizeof(srv->loot_credit_players));
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
     * The block animation is content's answer, not a param read.
     *
     * `[proc,combat_defend_anim](obj $weapon, obj $shield)(seq)` is a priority
     * chain — shield, then weapon, then unarmed — so the engine hands it both
     * hands and takes what it returns. Reading `defend_anim` off the weapon
     * here (which is what this did) meant a kiteshield never blocked with,
     * because the off-hand was never consulted at all.
     */
    {
        int32_t hands[2] = { (int32_t)player->worn[MOCK230_WEAR_WEAPON].obj_id,
                             (int32_t)player->worn[MOCK230_WEAR_SHIELD].obj_id };
        int32_t block = -1;

        if( mock230_scripts_run_hook_int(srv, srv->hooks.combat_defend_anim, hands, 2, &block) )
            play_player_seq(player, block);
    }

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
        mock230_scripts_queue_hook(srv, srv->hooks.player_death, 0, 0);
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
        mock230_scene_npc_approach(info ? info->size : 1, &approach);
        mock230_world_walk_to_approach(srv, npc->x, npc->z, &approach);
    }
}

/* ------------------------------------------------------------------ */
/* The tick                                                            */
/* ------------------------------------------------------------------ */

/** Ticks between the player's swings: the weapon's attack rate, or 4 unarmed. */
static int
player_attack_rate(const struct Mock230Player* player)
{
    int weapon = player->worn[MOCK230_WEAR_WEAPON].obj_id;
    const struct Mock230ObjInfo* info;

    if( weapon < 0 )
        return MOCK230_ATTACK_SPEED;
    info = mock230_objinfo(weapon);
    return info->has_params && info->attackrate > 0 ? info->attackrate
                                                    : MOCK230_ATTACK_SPEED;
}

/*
 * The player's swing animation.
 *
 * Named through the content pack rather than hardcoded, but still one of two:
 * the cache says what a weapon's bonuses are, not which of the ten weapon
 * classes it belongs to, so an armed swing uses the sword set. That is a
 * simplification, and a visible one — an axe swings like a sword — but the
 * alternative is guessing a weapon class from its name.
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

    /*
     * Face the target for the whole engagement, before the approach can return
     * early — same fix as the npc side. Face-entity ids below 32768 are npc
     * slots; the player's own index would be 32768 + slot.
     *
     * Only on change: FACE_ENTITY is a latch, so re-asserting it every tick is
     * wire noise the client does nothing with.
     */
    if( player->face_entity != player->combat_target )
    {
        player->face_entity = player->combat_target;
        player->masks |= MOCK230_PMASK_FACE_ENTITY;
    }

    /* The approach happened before the player moved, in
     * `mock230_combat_player_approach`. Out of range here means it has not
     * arrived yet. */
    if( !in_player_attack_range(player, npc) )
        return;

    if( player->attack_clock > 0 )
    {
        player->attack_clock--;
        return;
    }
    player->attack_clock = player_attack_rate(player);

    /*
     * The swing is content's, all of it.
     *
     * `[proc,player_melee_swing]` rolls the accuracy, rolls the damage, pays
     * the experience, lands the splat and plays the animation — which is the
     * reference's `[label,player_melee_attack]` and, allowing for one server
     * having players and this one having one, the same list.
     *
     * What this function keeps is the simulation around the swing: whose turn
     * it is, the attack clock, following a target that walks, and facing it.
     * Those are about the world rather than about combat, and the test for the
     * split is that the proc names no tick and no distance while nothing below
     * this line names a bonus or a formula.
     *
     * The active npc is set for the proc so `npc_stat`, `npc_param` and
     * `npc_damage` resolve to the target without content having to be told
     * which slot it is.
     */
    mock230_scripts_run_hook_on_npc(srv, srv->hooks.player_melee_swing, player->combat_target);
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

    if( !npc->def || npc->def->huntmode != MOCK230_HUNT_AGGRESSIVE )
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
        {
            mock230_world_npc_occupancy(npc, 0);
            npc->active = 0;
            npc->death_tick = -1;
            npc->respawn_tick = srv->tick + npc_def(npc)->respawnrate;
        }
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
    play_npc_seq(npc, npc->attack_seq);

    /*
     * The npc's swing is content's, the same way the player's is.
     *
     * `[proc,npc_meleeattack]` rolls the accuracy against the player's
     * `%com_*def`, rolls the damage off the npc's own cache params, applies the
     * protection prayer, lands the splat and queues the retaliation — which is
     * the reference's `[proc,npc_meleeattack]` plus `[proc,playerhit_n_melee]`,
     * and the same list.
     *
     * Retaliation is queued inside that proc, at the point the npc commits to a
     * swing rather than where damage lands: being attacked is what provokes it,
     * so a miss and a protected hit have to provoke it too. That used to be a
     * comment here explaining why the queue call came before two early returns.
     */
    mock230_scripts_run_hook_on_npc(srv, srv->hooks.npc_meleeattack, slot);
}

void
mock230_combat_respawn_tick(struct Mock230Server* srv)
{
    for( int slot = 0; slot < MOCK230_NPC_MAX; slot++ )
    {
        struct Mock230Npc* npc = &srv->npcs[slot];

        if( npc->active || npc->respawn_tick < 0 || srv->tick < npc->respawn_tick )
            continue;

        npc->active = 1;
        npc->respawn_tick = -1;
        npc->death_tick = -1;
        npc->x = npc->spawn_x;
        npc->z = npc->spawn_z;
        npc->level = npc->spawn_level;
        npc->hitpoints = npc->base_hitpoints;
        npc->combat_target = -1;
        npc->attack_clock = 0;
        npc->step_dir = -1;
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
