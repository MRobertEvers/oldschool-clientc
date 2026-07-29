/*
 * Melee combat, on OldSchool's own arithmetic.
 *
 * The formulas are OpenRune's MeleeCombatFormula
 * (game-plugins/.../combat/formula/MeleeCombatFormula.kt), which is the
 * standard OldSchool derivation:
 *
 *   effective level = base + style bonus + 8        (NPCs: base + 9)
 *   attack roll     = effective attack  * (attack bonus  + 64)
 *   defence roll    = effective defence * (defence bonus + 64)
 *   accuracy        = attack > defence
 *                       ? 1 - (defence + 2) / (2 * (attack + 1))
 *                       : attack / (2 * (defence + 1))
 *   max hit         = floor(0.5 + effective strength * (strength bonus + 64) / 640)
 *
 * What makes that computable here rather than aspirational is the *cache*. An
 * OldSchool obj or npc record carries its equipment bonuses in its param table
 * — param ids 0..11 are the twelve bonuses and 14 is the attack rate — so the
 * bronze scimitar really does contribute +7 slash and +6 strength, and the
 * guard really does have +25 slash defence, without a hand-written table
 * anywhere. See mock230_objinfo.c for how those are read and why the ids are
 * trustworthy.
 *
 * The engine owns hitpoints, hitsplats, death, respawn and swing timing.
 * Content decides who is aggressive, how hard they hit and what they drop, all
 * of it in the area configs under content/scripts.
 *
 * Arithmetic is integer throughout. Accuracy is compared as a pair of
 * cross-multiplied rolls rather than as a probability, which avoids floating
 * point in a server whose whole point is that a session replays identically.
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

static int
in_attack_range(
    const struct Mock230Player* player,
    const struct Mock230Npc* npc)
{
    int range = npc->def && npc->def->attackrange > 0 ? npc->def->attackrange
                                                      : MOCK230_ATTACK_RANGE;

    if( player->level != npc->level )
        return 0;
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
    struct Mock230Player* player = &srv->player;

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
    struct Mock230Player* player = &srv->player;
    int before;

    if( stat < 0 || stat >= MOCK230_STAT_COUNT || tenths <= 0 )
        return;
    before = player->stat_level[stat];
    player->stat_xp_tenths[stat] += tenths;
    player->stat_level[stat] = level_for_xp(player->stat_xp_tenths[stat] / 10);
    if( player->stat_level[stat] != before )
    {
        /* A hitpoints level-up raises the ceiling but does not heal, which is
         * what OldSchool does and is also the only behaviour that cannot
         * surprise someone mid-fight. */
        if( stat == MOCK230_STAT_HITPOINTS )
            mock230_combat_sync_hitpoints(player);
        mock230_send_message(srv, "You feel yourself getting stronger.");
    }
    if( player->stat_boosted[stat] < player->stat_level[stat] &&
        stat != MOCK230_STAT_HITPOINTS )
        player->stat_boosted[stat] = player->stat_level[stat];
    mock230_combat_stat_mark(player, stat);
}

/* ------------------------------------------------------------------ */
/* Rolls                                                               */
/* ------------------------------------------------------------------ */

/** The style's contribution to the effective level, per skill. */
static int
style_bonus(int style, int stat)
{
    switch( style )
    {
    case MOCK230_STYLE_ACCURATE:
        return stat == MOCK230_STAT_ATTACK ? 3 : 0;
    case MOCK230_STYLE_AGGRESSIVE:
        return stat == MOCK230_STAT_STRENGTH ? 3 : 0;
    case MOCK230_STYLE_DEFENSIVE:
        return stat == MOCK230_STAT_DEFENCE ? 3 : 0;
    default:
        return 1; /* controlled: +1 to all three */
    }
}

/** Summed bonus over everything the player is wearing. */
static int
player_bonus(
    const struct Mock230Player* player,
    int index)
{
    int total = 0;

    for( int slot = 0; slot < MOCK230_WORN_SLOTS; slot++ )
    {
        int obj_id = player->worn[slot].obj_id;

        if( obj_id < 0 )
            continue;
        total += mock230_objinfo(obj_id)->bonus[index];
    }
    return total;
}

/** Which of the three melee bonuses the player's weapon rolls with. */
static int
player_damage_type(const struct Mock230Player* player)
{
    int weapon = player->worn[MOCK230_WEAR_WEAPON].obj_id;

    if( weapon < 0 )
        return MOCK230_DAMAGE_CRUSH; /* a punch is a crush */
    return mock230_objinfo(weapon)->damagetype;
}

static int
player_effective(
    const struct Mock230Player* player,
    int stat,
    int style)
{
    int level = player->stat_boosted[stat] > 0 ? player->stat_boosted[stat]
                                               : player->stat_level[stat];

    return level + style_bonus(style, stat) + 8;
}

/*
 * An npc's effective level is its base plus 9.
 *
 * The 9 is not arbitrary and it is not the player's 8: OldSchool gives every
 * monster the equivalent of a permanent stance bonus on top of the same +8, so
 * a monster with 1 attack rolls as though it had 10. Using the player's +8 here
 * makes every monster fractionally less accurate than it should be, which is
 * the kind of error that never produces a bug report.
 */
static int
npc_effective(int level)
{
    return (level > 0 ? level : 1) + 9;
}

/**
 * Did the swing land?
 *
 * The reference computes a probability and compares it to a uniform draw. This
 * compares the two rolls directly, which is the same test without floating
 * point: with A = attack roll and D = defence roll, a hit happens with
 * probability 1 - (D+2)/(2(A+1)) when A > D and A/(2(D+1)) otherwise. Both
 * cases are `rand(denominator) < numerator` after clearing the fraction.
 */
static int
roll_hit(
    struct Mock230Server* srv,
    int attack_roll,
    int defence_roll)
{
    if( attack_roll <= 0 )
        return 0;
    if( attack_roll > defence_roll )
    {
        /* 1 - (D+2)/(2(A+1)) = (2A + 2 - D - 2) / (2(A+1)) = (2A - D) / (2A + 2) */
        int denominator = (2 * attack_roll) + 2;
        int numerator = (2 * attack_roll) - defence_roll;

        return mock230_random(srv, 0, denominator - 1) < numerator;
    }
    {
        /* A / (2(D+1)) */
        int denominator = 2 * (defence_roll + 1);

        return mock230_random(srv, 0, denominator - 1) < attack_roll;
    }
}

/** floor(0.5 + effective * (strength bonus + 64) / 640), in integers. */
static int
max_hit(int effective_strength, int strength_bonus)
{
    int numerator = effective_strength * (strength_bonus + 64);
    /* +320 is the 0.5 the reference adds before flooring, scaled by 640. */
    int hit = (numerator + 320) / 640;

    return hit > 0 ? hit : 0;
}

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
    npc->damage_type = amount > 0 ? type : MOCK230_HIT_BLOCK;
    npc->hitpoints = npc->hitpoints < 0 ? 0 : npc->hitpoints;
    npc->max_hitpoints = npc->max_hitpoints > 0 ? npc->max_hitpoints : 1;
    npc->masks |= MOCK230_NMASK_DAMAGE;

    /* Retaliate. An npc that is hit fights back whatever its hunt mode says —
     * aggression decides who *starts* a fight, not who finishes one. */
    if( npc->combat_target < 0 )
    {
        npc->combat_target = 0;
        npc->attack_clock = npc->def ? npc->def->attackrate : MOCK230_ATTACK_SPEED;
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
        if( srv->player.combat_target == slot )
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
    struct Mock230Player* player = &srv->player;

    if( amount > player->hitpoints )
        amount = player->hitpoints;
    player->hitpoints -= amount;

    player->damage = amount;
    player->damage_type = amount > 0 ? type : MOCK230_HIT_BLOCK;
    player->hitpoints = player->hitpoints < 0 ? 0 : player->hitpoints;
    player->masks |= MOCK230_PMASK_DAMAGE;
    mock230_combat_sync_hitpoints(player);

    {
        int weapon = player->worn[MOCK230_WEAR_WEAPON].obj_id;
        int block = mock230_content_symbol(MOCK230_PACK_SEQ,
                                           weapon >= 0 ? "human_sword_block"
                                                       : "human_unarmedblock");
        play_player_seq(player, block);
    }

    if( player->hitpoints == 0 && player->death_tick < 0 )
    {
        /* Death is scheduled, not immediate: the animation has to reach the
         * client before the teleport does, and both travel in the same tick's
         * PLAYER_INFO. */
        play_player_seq(player, mock230_content_symbol(MOCK230_PACK_SEQ, "human_death"));
        player->death_tick = srv->tick + MOCK230_DEATH_TICKS;
        mock230_combat_stop_player(srv);
        for( int i = 0; i < MOCK230_NPC_MAX; i++ )
        {
            if( srv->npcs[i].combat_target == 0 )
                mock230_combat_stop_npc(srv, i);
        }
        mock230_send_message(srv, "Oh dear, you are dead!");
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
    struct Mock230Player* player = &srv->player;
    struct Mock230Npc* npc;

    if( slot < 0 || slot >= MOCK230_NPC_MAX )
        return;
    npc = &srv->npcs[slot];
    if( !npc->active || npc->death_tick >= 0 )
        return;
    if( player->death_tick >= 0 )
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
static int
player_attack_seq(const struct Mock230Player* player)
{
    if( player->worn[MOCK230_WEAR_WEAPON].obj_id >= 0 )
        return mock230_content_symbol(MOCK230_PACK_SEQ, "human_sword_slash");
    return mock230_content_symbol(MOCK230_PACK_SEQ, "human_unarmedpunch");
}

void
mock230_combat_player_tick(struct Mock230Server* srv)
{
    struct Mock230Player* player = &srv->player;
    struct Mock230Npc* npc;
    int style;
    int damage_type;
    int attack_roll;
    int defence_roll;

    if( player->death_tick >= 0 )
    {
        if( srv->tick >= player->death_tick )
            mock230_world_player_respawn(srv);
        return;
    }
    if( player->combat_target < 0 )
        return;

    npc = &srv->npcs[player->combat_target];
    if( !npc->active || npc->death_tick >= 0 )
    {
        mock230_combat_stop_player(srv);
        return;
    }

    /* Keep following: an npc that roams mid-fight would otherwise walk out of
     * range and the fight would silently stall. */
    if( !in_attack_range(player, npc) )
    {
        if( player->step_head >= player->step_count )
            mock230_world_walk_beside(srv, npc->x, npc->z);
        return;
    }

    /* Face-entity ids below 32768 are npc slots; the player's own index would
     * be 32768 + slot. */
    player->face_entity = player->combat_target;
    player->masks |= MOCK230_PMASK_FACE_ENTITY;

    if( player->attack_clock > 0 )
    {
        player->attack_clock--;
        return;
    }
    player->attack_clock = player_attack_rate(player);

    style = mock230_world_attack_style(srv);
    damage_type = player_damage_type(player);

    attack_roll = player_effective(player, MOCK230_STAT_ATTACK, style) *
                  (player_bonus(player, MOCK230_PARAM_STABATTACK + damage_type) + 64);
    defence_roll = npc_effective(npc->def ? npc->def->defence : 1) *
                   ((npc->def ? npc->def->bonus[MOCK230_PARAM_STABDEFENCE + damage_type]
                              : 0) +
                    64);

    /* Swing first, then land the hit: the block/death animation the hit sets on
     * the npc must not be overwritten by anything here. */
    play_player_seq(player, player_attack_seq(player));

    if( !roll_hit(srv, attack_roll, defence_roll) )
    {
        mock230_combat_hit_npc(srv, player->combat_target, MOCK230_HIT_BLOCK, 0);
        return;
    }

    {
        int strength = player_effective(player, MOCK230_STAT_STRENGTH, style);
        int ceiling = max_hit(strength, player_bonus(player, MOCK230_PARAM_STRENGTHBONUS));
        int damage = mock230_random(srv, 0, ceiling);
        int target = player->combat_target;

        mock230_combat_hit_npc(srv, target, MOCK230_HIT_DAMAGE, damage);

        /*
         * Experience: 4 points per damage to the style's skill, and 4/3 to
         * hitpoints. Awarded on damage dealt rather than on the kill, which is
         * what OldSchool does and what makes a long fight feel like progress.
         */
        if( damage > 0 )
        {
            switch( style )
            {
            case MOCK230_STYLE_ACCURATE:
                mock230_combat_add_xp(srv, MOCK230_STAT_ATTACK, damage * 40);
                break;
            case MOCK230_STYLE_AGGRESSIVE:
                mock230_combat_add_xp(srv, MOCK230_STAT_STRENGTH, damage * 40);
                break;
            case MOCK230_STYLE_DEFENSIVE:
                mock230_combat_add_xp(srv, MOCK230_STAT_DEFENCE, damage * 40);
                break;
            default:
                /* Controlled splits it three ways. */
                mock230_combat_add_xp(srv, MOCK230_STAT_ATTACK, damage * 13);
                mock230_combat_add_xp(srv, MOCK230_STAT_STRENGTH, damage * 13);
                mock230_combat_add_xp(srv, MOCK230_STAT_DEFENCE, damage * 13);
                break;
            }
            mock230_combat_add_xp(srv, MOCK230_STAT_HITPOINTS, damage * 13);
        }
    }
}

/*
 * Aggression.
 *
 * OldSchool's rule, kept: a monster stops being aggressive once the player's
 * combat level is more than twice its own. Without that the Lumbridge goblins
 * would still be mobbing a player who has outgrown them by an order of
 * magnitude, which is a worse first impression than no aggression at all.
 */
static int
player_combat_level(const struct Mock230Player* player)
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
    struct Mock230Player* player = &srv->player;
    struct Mock230Npc* npc = &srv->npcs[slot];
    int npc_level;

    if( !npc->def || npc->def->huntmode != MOCK230_HUNT_AGGRESSIVE )
        return;
    if( npc->def->huntrange <= 0 || npc->combat_target >= 0 )
        return;
    if( player->death_tick >= 0 || player->level != npc->level )
        return;
    if( tile_distance(player->x, player->z, npc->x, npc->z) > npc->def->huntrange )
        return;

    npc_level = mock230_npcinfo(npc->type)->combat_level;
    if( npc_level > 0 && player_combat_level(player) > npc_level * 2 )
        return;

    npc->combat_target = 0;
    npc->attack_clock = 0;
}

void
mock230_combat_npc_tick(
    struct Mock230Server* srv,
    int slot)
{
    struct Mock230Player* player = &srv->player;
    struct Mock230Npc* npc = &srv->npcs[slot];
    int attack_roll;
    int defence_roll;
    int damage_type;

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
    if( player->death_tick >= 0 )
    {
        mock230_combat_stop_npc(srv, slot);
        return;
    }

    if( !in_attack_range(player, npc) )
    {
        /* Walk toward the player rather than giving up, so a fight the player
         * backs away from resumes instead of quietly ending. A blocked step is
         * simply not taken — the npc will try again next tick, and giving it a
         * pathfinder of its own is more machinery than a mock needs. */
        int want_x = npc->x + (player->x > npc->x ? 1 : (player->x < npc->x ? -1 : 0));
        int want_z = npc->z + (player->z > npc->z ? 1 : (player->z < npc->z ? -1 : 0));
        int dir = mock230_step_direction(want_x - npc->x, want_z - npc->z);

        if( dir >= 0 && mock230_scene_can_step(npc->level, npc->x, npc->z, dir) )
        {
            npc->step_dir = dir;
            npc->x = want_x;
            npc->z = want_z;
        }
        return;
    }

    npc->face_entity = MOCK230_PLAYER_TERMINATOR;
    npc->masks |= MOCK230_NMASK_FACE_ENTITY;

    if( npc->attack_clock > 0 )
    {
        npc->attack_clock--;
        return;
    }
    npc->attack_clock = npc->def ? npc->def->attackrate : MOCK230_ATTACK_SPEED;
    play_npc_seq(npc, npc->attack_seq);

    damage_type = npc->def ? npc->def->damagetype : MOCK230_DAMAGE_CRUSH;
    attack_roll = npc_effective(npc->def ? npc->def->attack : 1) *
                  ((npc->def ? npc->def->bonus[MOCK230_PARAM_STABATTACK + damage_type] : 0) +
                   64);
    defence_roll = player_effective(player, MOCK230_STAT_DEFENCE,
                                    mock230_world_attack_style(srv)) *
                   (player_bonus(player, MOCK230_PARAM_STABDEFENCE + damage_type) + 64);

    if( !roll_hit(srv, attack_roll, defence_roll) )
    {
        mock230_combat_hit_player(srv, MOCK230_HIT_BLOCK, 0);
        return;
    }
    /*
     * Protection prayers are applied to the DAMAGE, after the accuracy roll —
     * that is OldSchool's order, and it is why a protected hit still splashes a
     * blue 0 rather than not landing. Against monsters the reduction is total;
     * the 40% PvP figure has no meaning here, since nothing but npcs attacks.
     *
     * Every npc in the mock is a melee attacker, so protect-from-melee is the
     * only one with an effect today. The lookup is by damage type so that stops
     * being true the moment a ranged one exists.
     */
    if( mock230_prayer_protecting(player, MOCK230_HEADICON_PROTECT_MELEE) )
    {
        mock230_combat_hit_player(srv, MOCK230_HIT_BLOCK, 0);
        return;
    }
    {
        int strength = npc_effective(npc->def ? npc->def->strength : 1);
        int bonus = npc->def ? npc->def->bonus[MOCK230_PARAM_STRENGTHBONUS] : 0;

        mock230_combat_hit_player(srv, MOCK230_HIT_DAMAGE,
                                  mock230_random(srv, 0, max_hit(strength, bonus)));
    }
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
