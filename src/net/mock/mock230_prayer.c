#include "mock230_prayer.h"

#include "mock230.h"
#include "mock230_content.h"
#include "mock230_equipment.h"
#include "mock230_ids.h"

#include <stdio.h>
#include <string.h>

/*
 * There is no prayer table in this file.
 *
 * The 29 prayers — their names, levels, drain rates, exclusion groups and
 * overhead icons — are content, and they are read from
 * `skill_prayer/configs/prayers.prayer`. What is here is the arithmetic:
 * which prayers turn each other off, how fast points drain against the wearer's
 * prayer bonus, and how a click on a button becomes a toggle.
 *
 * A prayer's index is its position in that file, which is also its bit in
 * `player->prayer_active`. Nothing derives an index from a component id.
 */

const char*
mock230_prayer_name(int prayer)
{
    const struct Mock230PrayerDef* def = mock230_content_prayer(prayer);

    return def ? def->name : "prayer";
}

int
mock230_prayer_headicon_mask(const struct Mock230Player* player)
{
    int count = mock230_content_prayer_count();
    int mask = 0;

    for( int i = 0; i < count; i++ )
    {
        const struct Mock230PrayerDef* def = mock230_content_prayer(i);

        if( (player->prayer_active & (1u << i)) == 0 )
            continue;
        if( def->headicon < 0 )
            continue;
        mask |= 1 << def->headicon;
    }
    return mask;
}

int
mock230_prayer_protecting(
    const struct Mock230Player* player,
    int headicon)
{
    if( headicon < 0 )
        return 0;
    return (mock230_prayer_headicon_mask(player) & (1 << headicon)) != 0;
}

/* Appearance, not a packet of its own: the overhead icon rides the appearance
 * block, so a change here has to re-send it. */
static void
prayer_changed(struct Mock230Server* srv)
{
    srv->player->masks |= MOCK230_PMASK_APPEARANCE;
}

void
mock230_prayer_clear(struct Mock230Server* srv)
{
    if( srv->player->prayer_active == 0 )
        return;
    srv->player->prayer_active = 0;
    srv->player->prayer_drain_acc = 0;
    prayer_changed(srv);
}

int
mock230_prayer_toggle(
    struct Mock230Server* srv,
    int prayer)
{
    struct Mock230Player* player = srv->player;
    const struct Mock230PrayerDef* def = mock230_content_prayer(prayer);

    if( !def )
        return 0;

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
        char line[96];

        snprintf(line, sizeof(line), "You need a Prayer level of %d to use %s.", def->level,
                 def->name);
        mock230_send_message(srv, line);
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
        int count = mock230_content_prayer_count();

        for( int i = 0; i < count; i++ )
            if( (mock230_content_prayer(i)->groups & def->groups) != 0 )
                player->prayer_active &= ~(1u << i);
    }
    player->prayer_active |= 1u << prayer;
    prayer_changed(srv);
    return 1;
}

void
mock230_prayer_tick(struct Mock230Server* srv)
{
    struct Mock230Player* player = srv->player;
    int count = mock230_content_prayer_count();
    int rate = 0;
    int resistance;
    int drained = 0;

    if( player->prayer_active == 0 )
    {
        player->prayer_drain_acc = 0;
        return;
    }
    for( int i = 0; i < count; i++ )
        if( player->prayer_active & (1u << i) )
            rate += mock230_content_prayer(i)->drain;
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
    int count = mock230_content_prayer_count();

    for( int i = 0; i < count; i++ )
    {
        const struct Mock230PrayerDef* def = mock230_content_prayer(i);

        if( def->button < 0 )
            continue;
        mock230_send_if_setevents(srv, def->button, -1, -1, 1 << 1);
    }
}

int
mock230_prayer_handle_button(
    struct Mock230Server* srv,
    int component,
    int op)
{
    int count = mock230_content_prayer_count();

    if( MOCK230_COM_GROUP(component) != mock230_ids()->iface_prayerbook )
        return 0;

    for( int i = 0; i < count; i++ )
    {
        if( mock230_content_prayer(i)->button != component )
            continue;
        /* Claimed either way: an op the mock does not implement on a prayer
         * button is still a prayer-book click, not something for the next
         * router to try. */
        if( op == 1 )
            mock230_prayer_toggle(srv, i);
        return 1;
    }
    /* Some other component of the book — the filter menu, the level readout. */
    return 0;
}

int
mock230_prayer_headicon(const char* symbol)
{
    return mock230_content_constant_int(symbol, -1);
}

int
mock230_prayer_index(const char* symbol)
{
    int count = mock230_content_prayer_count();

    for( int i = 0; i < count; i++ )
        if( strcmp(mock230_content_prayer(i)->symbol, symbol) == 0 )
            return i;
    return -1;
}
