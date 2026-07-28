/*
 * Baseline melee combat.
 *
 * The split follows the rest of the mock: the engine owns the mechanics that
 * have to be consistent — hitpoints, hitsplats, death, respawn, swing timing —
 * and content decides policy, engaging a target with `p_opnpc(2)` and landing
 * scripted hits with `npc_damage` / `damage`.
 *
 * Everything visible here already existed as a wire feature. The DAMAGE mask
 * carries the hitsplat *and* the health bar above it (damage, type, health,
 * total_health), so a hit is one mask write rather than a packet of its own,
 * and an npc dying is the ordinary NPC_INFO remove path.
 */

#include "mock230.h"

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
    return tile_distance(player->x, player->z, npc->x, npc->z) <= MOCK230_ATTACK_RANGE;
}

/* Deterministic roll, so a session replays identically. */
static int
roll_damage(
    struct Mock230Server* srv,
    int maximum)
{
    if( maximum <= 0 )
        return 0;
    return mock230_random(srv, 0, maximum);
}

/*
 * The player's swing animation.
 *
 * Names come from the cache: `human_unarmedpunch` (422) bare-handed,
 * `human_sword_slash` (390) with something in the weapon slot. That second
 * choice is the one simplification here — the cache does not say which of the
 * ten weapon classes an item belongs to (the obj params are combat bonuses and
 * attack speed, nothing else), so every armed swing uses the sword set rather
 * than guessing between axe, blunt, staff and the rest.
 */
static int
player_attack_seq(const struct Mock230Player* player)
{
    if( player->worn[MOCK230_WEAR_WEAPON].obj_id >= 0 )
        return mock230_seq_by_name("human_sword_slash");
    return mock230_seq_by_name("human_unarmedpunch");
}

static void
play_npc_seq(struct Mock230Npc* npc, int seq_id)
{
    /* -1 means the cache had no such name. Sending it would spell 65535 on the
     * wire, which tells the client to STOP whatever is playing — worse than
     * sending nothing at all. */
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

    /* Retaliate. An npc that is hit fights back, which is the whole reason a
     * baseline needs npc-side combat at all. */
    if( npc->combat_target < 0 )
    {
        npc->combat_target = 0;
        npc->attack_clock = MOCK230_ATTACK_SPEED;
    }
    npc->face_entity = MOCK230_PLAYER_TERMINATOR;
    npc->masks |= MOCK230_NMASK_FACE_ENTITY;
    /* Flinch. Overwritten below if this was the killing blow. */
    play_npc_seq(npc, npc->block_seq);

    if( npc->hitpoints == 0 )
    {
        play_npc_seq(npc, npc->death_seq);
        npc->death_tick = srv->tick + MOCK230_DEATH_TICKS;
        npc->combat_target = -1;
        /* Stop roaming and stop being a valid target the moment it dies. */
        npc->next_roam_tick = srv->tick + MOCK230_RESPAWN_TICKS;
        if( srv->player.combat_target == slot )
            srv->player.combat_target = -1;
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
    player->max_hitpoints = player->max_hitpoints > 0 ? player->max_hitpoints
                                                      : MOCK230_PLAYER_MAX_HP;
    player->masks |= MOCK230_PMASK_DAMAGE;
    play_player_seq(player, mock230_seq_by_name("human_unarmedblock"));

    if( player->hitpoints == 0 )
    {
        /* No death mechanic beyond a reset: the mock has no respawn point, no
         * item loss and no death interface, and inventing them would be a lot
         * of behaviour nothing asked for. */
        mock230_send_message(srv, "Oh dear, you are dead! (healed to full)");
        player->hitpoints = player->max_hitpoints;
        player->combat_target = -1;
        for( int i = 0; i < MOCK230_NPC_MAX; i++ )
        {
            if( srv->npcs[i].combat_target == 0 )
                srv->npcs[i].combat_target = -1;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Engagement                                                          */
/* ------------------------------------------------------------------ */

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

    player->combat_target = slot;
    /* Swing on the tick the player arrives rather than after a full interval:
     * an opening delay reads as the click having been dropped. */
    player->attack_clock = 0;

    /* Walk to a tile beside it, the way every other op does. */
    mock230_world_walk_beside(srv, npc->x, npc->z);
}

/* ------------------------------------------------------------------ */
/* The tick                                                            */
/* ------------------------------------------------------------------ */

void
mock230_combat_player_tick(struct Mock230Server* srv)
{
    struct Mock230Player* player = &srv->player;
    struct Mock230Npc* npc;

    if( player->combat_target < 0 )
        return;

    npc = &srv->npcs[player->combat_target];
    if( !npc->active || npc->death_tick >= 0 )
    {
        player->combat_target = -1;
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
    player->attack_clock = MOCK230_ATTACK_SPEED;

    /* Swing first, then land the hit: the block/death animation the hit sets on
     * the npc must not be overwritten by anything here. */
    play_player_seq(player, player_attack_seq(player));
    mock230_combat_hit_npc(srv, player->combat_target, MOCK230_HIT_DAMAGE,
                           roll_damage(srv, 4));
}

void
mock230_combat_npc_tick(
    struct Mock230Server* srv,
    int slot)
{
    struct Mock230Player* player = &srv->player;
    struct Mock230Npc* npc = &srv->npcs[slot];

    /* Death first: a dead npc neither swings nor roams, and its corpse has to
     * outlive the killing blow long enough for the animation to play. */
    if( npc->death_tick >= 0 )
    {
        if( srv->tick >= npc->death_tick )
        {
            npc->active = 0;
            npc->death_tick = -1;
            npc->respawn_tick = srv->tick + MOCK230_RESPAWN_TICKS;
        }
        return;
    }

    if( npc->combat_target < 0 )
        return;

    if( !in_attack_range(player, npc) )
    {
        /* Walk toward the player rather than giving up, so a fight the player
         * backs away from resumes instead of quietly ending. */
        int step_x = npc->x + (player->x > npc->x ? 1 : (player->x < npc->x ? -1 : 0));
        int step_z = npc->z + (player->z > npc->z ? 1 : (player->z < npc->z ? -1 : 0));

        npc->step_dir = mock230_step_direction(step_x - npc->x, step_z - npc->z);
        if( npc->step_dir >= 0 )
        {
            npc->x = step_x;
            npc->z = step_z;
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
    npc->attack_clock = MOCK230_ATTACK_SPEED;
    play_npc_seq(npc, npc->attack_seq);
    mock230_combat_hit_player(srv, MOCK230_HIT_DAMAGE, roll_damage(srv, 3));
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
