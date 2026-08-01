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
#include "mock230_prayer.h"
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
 */
static int
in_attack_range(
    const struct Mock230Player* player,
    const struct Mock230Npc* npc)
{
    int range = npc->def && npc->def->attackrange > 0 ? npc->def->attackrange
                                                      : MOCK230_ATTACK_RANGE;
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

static void
play_npc_seq(struct Mock230Npc* npc, int seq_id)
{
    /* -1 means the content named nothing and the cache had no convention
     * match. Sending it would spell 65535 on the wire, which tells the client
     * to STOP whatever is playing — worse than sending nothing at all. */
    if( seq_id < 0 )
        return;
    npc->anim_id = seq_id;
    npc->anim_delay = 0;
    npc->masks |= MOCK230_NMASK_ANIM;
}

static void
play_player_seq(struct Mock230Player* player, int seq_id)
{
    if( seq_id < 0 )
        return;
    player->anim_id = seq_id;
    player->anim_delay = 0;
    player->masks |= MOCK230_PMASK_SEQUENCE;
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
mock230_combat_stop_player(struct Mock230Server* srv)
{
    struct Mock230Player* player = srv->player;

    player->combat_target = -1;
    if( player->face_entity != -1 )
    {
        player->face_entity = -1;
        player->masks |= MOCK230_PMASK_FACE_ENTITY;
    }
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
 * `floor(sum(i + 300 * 2^(i/7)) / 4)` over i in 1..L-1. Ninety-eight numbers
 * nobody would proofread, derived from one line that can be checked against the
 * published formula — 83 for level 2, 13,034,431 for level 99.
 */
static int
level_for_xp(int experience)
{
    static int table[100];
    static int built;

    if( !built )
    {
        double points = 0.0;

        table[1] = 0;
        for( int level = 1; level < 99; level++ )
        {
            points += (double)level + 300.0 * pow(2.0, (double)level / 7.0);
            table[level + 1] = (int)(points / 4.0);
        }
        built = 1;
    }

    for( int level = 99; level > 1; level-- )
    {
        if( experience >= table[level] )
            return level;
    }
    return 1;
}

void
mock230_combat_add_xp(
    struct Mock230Server* srv,
    int stat,
    int tenths)
{
    struct Mock230Player* player = srv->player;
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
        mock230_scripts_run_proc(srv, "[proc,combat_levelup_message]", NULL, 0);
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
        npc->combat_target = 0;
        npc->attack_clock = (npc->def ? npc->def->attackrate : MOCK230_ATTACK_SPEED) / 2;
    }
    npc->face_entity = MOCK230_PLAYER_TERMINATOR;
    npc->masks |= MOCK230_NMASK_FACE_ENTITY;
    /* Flinch. Overwritten below if this was the killing blow. */
    play_npc_seq(npc, npc->block_seq);

    if( npc->hitpoints == 0 )
    {
        play_npc_seq(npc, npc->death_seq);
        npc->death_tick = srv->tick + MOCK230_DEATH_TICKS;
        npc->next_roam_tick = srv->tick + MOCK230_RESPAWN_TICKS;
        mock230_combat_stop_npc(srv, slot);
        if( srv->player->combat_target == slot )
            mock230_combat_stop_player(srv);
        /* The drop table is content: [ai_queue3,<npc>] with obj_add calls, the
         * same shape as LostCity's. mock230_scripts.c runs it and falls back to
         * the config's death_drop when nothing is bound. */
        mock230_world_npc_died(srv, slot);
    }
}

void
mock230_combat_hit_player(
    struct Mock230Server* srv,
    int type,
    int amount)
{
    struct Mock230Player* player = srv->player;

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

        if( mock230_scripts_run_proc_int(srv, "[proc,combat_defend_anim]", hands, 2, &block) )
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
         * everything that would let a corpse act. The gate is cleared by the
         * script healing the player — `mock230_world_heal` sees hitpoints go
         * above zero — so even the length of the death is the script's.
         */
        player->dying = 1;
        mock230_combat_stop_player(srv);
        for( int i = 0; i < MOCK230_NPC_MAX; i++ )
        {
            if( srv->npcs[i].combat_target == 0 )
                mock230_combat_stop_npc(srv, i);
        }
        mock230_scripts_queue_named(srv, "[queue,player_death]", 0, 0);
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
    struct Mock230Player* player = srv->player;
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

    mock230_world_walk_beside(srv, npc->x, npc->z);
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


void
mock230_combat_player_tick(struct Mock230Server* srv)
{
    struct Mock230Player* player = srv->player;
    struct Mock230Npc* npc;

    /* A corpse does not swing. The script ends this by healing — nothing here
     * counts ticks or decides where the player wakes up. */
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

    /* Keep following: an npc that roams mid-fight would otherwise walk out of
     * range and the fight would silently stall. */
    if( !in_attack_range(player, npc) )
    {
        if( player->step_head >= player->step_count )
            mock230_world_walk_beside(srv, npc->x, npc->z);
        return;
    }

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
    mock230_scripts_run_proc_on_npc(srv, "[proc,player_melee_swing]", player->combat_target);
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

static void
maybe_aggress(
    struct Mock230Server* srv,
    int slot)
{
    struct Mock230Player* player = srv->player;
    struct Mock230Npc* npc = &srv->npcs[slot];
    int npc_level;

    if( !npc->def || npc->def->huntmode != MOCK230_HUNT_AGGRESSIVE )
        return;
    if( npc->def->huntrange <= 0 || npc->combat_target >= 0 )
        return;
    if( player->dying || player->level != npc->level )
        return;
    if( tile_distance(player->x, player->z, npc->x, npc->z) > npc->def->huntrange )
        return;

    npc_level = mock230_npcinfo(npc->type)->combat_level;
    if( npc_level > 0 && mock230_combat_level(player) > npc_level * 2 )
        return;

    npc->combat_target = 0;
    npc->attack_clock = 0;
}

void
mock230_combat_npc_tick(
    struct Mock230Server* srv,
    int slot)
{
    struct Mock230Player* player = srv->player;
    struct Mock230Npc* npc = &srv->npcs[slot];

    /* Death first: a dead npc neither swings nor roams, and its corpse has to
     * outlive the killing blow long enough for the animation to play. */
    if( npc->death_tick >= 0 )
    {
        if( srv->tick >= npc->death_tick )
        {
            npc->active = 0;
            npc->death_tick = -1;
            npc->respawn_tick =
                srv->tick + (npc->def ? npc->def->respawnrate : MOCK230_RESPAWN_TICKS);
        }
        return;
    }

    maybe_aggress(srv, slot);
    if( npc->combat_target < 0 )
        return;
    if( player->dying )
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
     * Set once here, before any early return, and only when it changes: the
     * mask is per-tick and re-sending an unchanged latch is pure wire noise.
     */
    if( npc->face_entity != MOCK230_PLAYER_TERMINATOR )
    {
        npc->face_entity = MOCK230_PLAYER_TERMINATOR;
        npc->masks |= MOCK230_NMASK_FACE_ENTITY;
    }

    if( !in_attack_range(player, npc) )
    {
        /* Walk toward the player rather than giving up, so a fight the player
         * backs away from resumes instead of quietly ending. A blocked step is
         * simply not taken — the npc will try again next tick, and giving it a
         * pathfinder of its own is more machinery than a mock needs. */
        int step_x = (player->x > npc->x ? 1 : (player->x < npc->x ? -1 : 0));
        int step_z = (player->z > npc->z ? 1 : (player->z < npc->z ? -1 : 0));
        int want_x;
        int want_z;
        int dir;

        /*
         * Square up rather than close diagonally.
         *
         * Melee cannot reach a diagonal (in_attack_range), so an npc that is
         * corner-to-corner with the player must step onto a shared row or
         * column. The greedy step moves BOTH axes at once, which keeps it on
         * the diagonal forever: it would arrive at the corner and sit there,
         * in "combat" but never swinging.
         *
         * One tile out on both axes means adjacent-diagonal: drop one axis so
         * the step lands orthogonally. Prefer the axis the player is further
         * along, so the approach still reads as direct.
         */
        if( step_x != 0 && step_z != 0 && abs_of(player->x - npc->x) == 1 &&
            abs_of(player->z - npc->z) == 1 )
        {
            if( abs_of(player->x - npc->x) >= abs_of(player->z - npc->z) )
                step_z = 0;
            else
                step_x = 0;
        }

        want_x = npc->x + step_x;
        want_z = npc->z + step_z;
        dir = mock230_step_direction(want_x - npc->x, want_z - npc->z);

        if( dir >= 0 && mock230_scene_can_step(npc->level, npc->x, npc->z, dir) )
        {
            npc->step_dir = dir;
            npc->x = want_x;
            npc->z = want_z;
        }
        return;
    }

    /* Facing is set above, before the approach returns early — an npc has to
     * face the player while chasing, not only once it arrives. */

    if( npc->attack_clock > 0 )
    {
        npc->attack_clock--;
        return;
    }
    npc->attack_clock = npc->def ? npc->def->attackrate : MOCK230_ATTACK_SPEED;
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
    mock230_scripts_run_proc_on_npc(srv, "[proc,npc_meleeattack]", slot);
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
        /* `tracked` stays clear, so the next NPC_INFO adds it as a new entity —
         * which is what a respawn is from the client's side. */
        npc->tracked = 0;
        npc->next_roam_tick = srv->tick + MOCK230_ATTACK_SPEED;
    }
}
