#include "mock230_prayer.h"

#include "mock230.h"
#include "mock230_content.h"
#include "mock230_equipment.h"

#include <stdio.h>
#include <string.h>

/*
 * Prayers that cannot be up together. OldSchool groups them by what they
 * boost: one defence prayer, one strength prayer, one attack prayer, one
 * ranged, one magic, one overhead — and the combat prayers (Chivalry, Piety,
 * Rigour, Augury) belong to several groups at once, which is why this is a
 * bitmask rather than a group id.
 */
enum
{
    GRP_DEFENCE = 1 << 0,
    GRP_STRENGTH = 1 << 1,
    GRP_ATTACK = 1 << 2,
    GRP_RANGED = 1 << 3,
    GRP_MAGIC = 1 << 4,
    GRP_OVERHEAD = 1 << 5,
};

struct PrayerDef
{
    const char* name;
    int level;
    /* Drain units per tick. Compared against 60 + 2 * prayer bonus, so a
     * prayer bonus of 0 spends one prayer point every 60/rate ticks — the
     * arithmetic xrsps's PrayerSystem uses, which is OldSchool's. */
    int drain;
    int groups;
    /* Overhead sprite index, or MOCK230_HEADICON_NONE. */
    int headicon;
};

/*
 * The 29 prayers in interface 541's own order. Levels and drain rates are
 * OldSchool's, transcribed from xrsps's src/rs/prayer/prayers.ts (which is
 * where the 1/6/12/24 pattern comes from — it is not a smooth curve).
 */
static const struct PrayerDef k_prayers[MOCK230_PRAYER_COUNT] = {
    { "Thick Skin", 1, 1, GRP_DEFENCE, MOCK230_HEADICON_NONE },
    { "Burst of Strength", 4, 1, GRP_STRENGTH, MOCK230_HEADICON_NONE },
    { "Clarity of Thought", 7, 1, GRP_ATTACK, MOCK230_HEADICON_NONE },
    { "Sharp Eye", 8, 1, GRP_RANGED, MOCK230_HEADICON_NONE },
    { "Mystic Will", 9, 1, GRP_MAGIC, MOCK230_HEADICON_NONE },
    { "Rock Skin", 10, 6, GRP_DEFENCE, MOCK230_HEADICON_NONE },
    { "Superhuman Strength", 13, 6, GRP_STRENGTH, MOCK230_HEADICON_NONE },
    { "Improved Reflexes", 16, 6, GRP_ATTACK, MOCK230_HEADICON_NONE },
    { "Rapid Restore", 19, 1, 0, MOCK230_HEADICON_NONE },
    { "Rapid Heal", 22, 2, 0, MOCK230_HEADICON_NONE },
    { "Protect Item", 25, 2, 0, MOCK230_HEADICON_NONE },
    { "Hawk Eye", 26, 6, GRP_RANGED, MOCK230_HEADICON_NONE },
    { "Mystic Lore", 27, 6, GRP_MAGIC, MOCK230_HEADICON_NONE },
    { "Steel Skin", 28, 12, GRP_DEFENCE, MOCK230_HEADICON_NONE },
    { "Ultimate Strength", 31, 12, GRP_STRENGTH, MOCK230_HEADICON_NONE },
    { "Incredible Reflexes", 34, 12, GRP_ATTACK, MOCK230_HEADICON_NONE },
    { "Protect from Magic", 37, 12, GRP_OVERHEAD, MOCK230_HEADICON_PROTECT_MAGIC },
    { "Protect from Missiles", 40, 12, GRP_OVERHEAD, MOCK230_HEADICON_PROTECT_MISSILES },
    { "Protect from Melee", 43, 12, GRP_OVERHEAD, MOCK230_HEADICON_PROTECT_MELEE },
    { "Eagle Eye", 44, 12, GRP_RANGED, MOCK230_HEADICON_NONE },
    { "Mystic Might", 45, 12, GRP_MAGIC, MOCK230_HEADICON_NONE },
    { "Retribution", 46, 3, GRP_OVERHEAD, MOCK230_HEADICON_RETRIBUTION },
    { "Redemption", 49, 6, GRP_OVERHEAD, MOCK230_HEADICON_REDEMPTION },
    { "Smite", 52, 18, GRP_OVERHEAD, MOCK230_HEADICON_SMITE },
    { "Preserve", 55, 2, 0, MOCK230_HEADICON_NONE },
    { "Chivalry", 60, 24, GRP_DEFENCE | GRP_STRENGTH | GRP_ATTACK, MOCK230_HEADICON_NONE },
    { "Piety", 70, 24, GRP_DEFENCE | GRP_STRENGTH | GRP_ATTACK, MOCK230_HEADICON_NONE },
    { "Rigour", 74, 24, GRP_DEFENCE | GRP_RANGED, MOCK230_HEADICON_NONE },
    { "Augury", 77, 24, GRP_DEFENCE | GRP_MAGIC, MOCK230_HEADICON_NONE },
};

const char*
mock230_prayer_name(int prayer)
{
    if( prayer < 0 || prayer >= MOCK230_PRAYER_COUNT )
        return "prayer";
    return k_prayers[prayer].name;
}

int
mock230_prayer_headicon_mask(const struct Mock230Player* player)
{
    int mask = 0;

    for( int i = 0; i < MOCK230_PRAYER_COUNT; i++ )
    {
        if( (player->prayer_active & (1u << i)) == 0 )
            continue;
        if( k_prayers[i].headicon == MOCK230_HEADICON_NONE )
            continue;
        mask |= 1 << k_prayers[i].headicon;
    }
    return mask;
}

int
mock230_prayer_protecting(
    const struct Mock230Player* player,
    int headicon)
{
    return (mock230_prayer_headicon_mask(player) & (1 << headicon)) != 0;
}

/* Appearance, not a packet of its own: the overhead icon rides the appearance
 * block, so a change here has to re-send it. */
static void
prayer_changed(struct Mock230Server* srv)
{
    srv->player.masks |= MOCK230_PMASK_APPEARANCE;
}

void
mock230_prayer_clear(struct Mock230Server* srv)
{
    if( srv->player.prayer_active == 0 )
        return;
    srv->player.prayer_active = 0;
    srv->player.prayer_drain_acc = 0;
    prayer_changed(srv);
}

int
mock230_prayer_toggle(
    struct Mock230Server* srv,
    int prayer)
{
    struct Mock230Player* player = &srv->player;
    const struct PrayerDef* def;

    if( prayer < 0 || prayer >= MOCK230_PRAYER_COUNT )
        return 0;
    def = &k_prayers[prayer];

    if( player->prayer_active & (1u << prayer) )
    {
        player->prayer_active &= ~(1u << prayer);
        prayer_changed(srv);
        return 1;
    }

    /* The level check is against the BASE level, not the boosted one: a prayer
     * potion raises the points you have, never the prayers you may use. */
    if( player->stat_level[MOCK230_STAT_PRAYER] < def->level )
    {
        {
            char line[96];
            snprintf(
                line, sizeof(line), "You need a Prayer level of %d to use %s.", def->level,
                def->name);
            mock230_send_message(srv, line);
        }
        return 0;
    }
    if( player->stat_boosted[MOCK230_STAT_PRAYER] <= 0 )
    {
        mock230_send_message(srv, "You have run out of Prayer points. Recharge at an altar.");
        return 0;
    }

    /* Everything sharing a group with it goes off. */
    if( def->groups )
    {
        for( int i = 0; i < MOCK230_PRAYER_COUNT; i++ )
            if( (k_prayers[i].groups & def->groups) != 0 )
                player->prayer_active &= ~(1u << i);
    }
    player->prayer_active |= 1u << prayer;
    prayer_changed(srv);
    return 1;
}

void
mock230_prayer_tick(struct Mock230Server* srv)
{
    struct Mock230Player* player = &srv->player;
    int rate = 0;
    int resistance;
    int drained = 0;

    if( player->prayer_active == 0 )
    {
        player->prayer_drain_acc = 0;
        return;
    }
    for( int i = 0; i < MOCK230_PRAYER_COUNT; i++ )
        if( player->prayer_active & (1u << i) )
            rate += k_prayers[i].drain;
    if( rate <= 0 )
        return;

    /* Prayer bonus doubles as drain resistance — the one place the equipment
     * screen's "Prayer: +N" has a mechanical effect. */
    resistance = 60 + 2 * mock230_equipment_bonus(srv, MOCK230_PARAM_PRAYERBONUS);
    if( resistance < 1 )
        resistance = 1;

    player->prayer_drain_acc += rate;
    while( player->prayer_drain_acc >= resistance &&
           player->stat_boosted[MOCK230_STAT_PRAYER] > 0 )
    {
        player->prayer_drain_acc -= resistance;
        player->stat_boosted[MOCK230_STAT_PRAYER]--;
        drained++;
    }
    if( !drained )
        return;

    mock230_combat_stat_mark(player, MOCK230_STAT_PRAYER);
    if( player->stat_boosted[MOCK230_STAT_PRAYER] <= 0 )
    {
        player->stat_boosted[MOCK230_STAT_PRAYER] = 0;
        mock230_prayer_clear(srv);
        mock230_send_message(srv, "You have run out of Prayer points.");
    }
}

void
mock230_prayer_arm_buttons(struct Mock230Server* srv)
{
    for( int i = 0; i < MOCK230_PRAYER_COUNT; i++ )
        mock230_send_if_setevents(
            srv,
            (MOCK230_PRAYER_IFACE << 16) | (MOCK230_PRAYER_FIRST_BUTTON + i),
            -1,
            -1,
            1 << 1);
}

int
mock230_prayer_handle_button(
    struct Mock230Server* srv,
    int component,
    int op)
{
    int group = (component >> 16) & 0xffff;
    int child = (component & 0xffff) - MOCK230_PRAYER_FIRST_BUTTON;

    if( group != MOCK230_PRAYER_IFACE || child < 0 || child >= MOCK230_PRAYER_COUNT )
        return 0;
    if( op != 1 )
        return 1;
    mock230_prayer_toggle(srv, child);
    return 1;
}
