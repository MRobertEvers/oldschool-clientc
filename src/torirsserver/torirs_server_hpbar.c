#include "torirsserver/torirs_server.h"
#include "torirsserver/torirs_server_ids.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * The enemy health overlay -- interface `hpbar_hud` (303).
 *
 * Four All Settings > Activities rows hang off this and could not be honoured
 * because the thing they configure was never on screen:
 *
 *   111  Show normal health overlay          %hpbar_hud_standard_disabled
 *   299  Show enemy name on health overlay   %hpbar_hud_boss_name_disabled
 *   300  Compact boss health overlay         %hpbar_hud_boss_compact_enabled
 *   301  Health overlay display type         %hpbar_hud_boss_percentage_enabled
 *
 * The last three are read by the cache's own layout scripts (2101 / 2103); the
 * client needs no code for them at all. What it needs is for somebody to OPEN
 * the interface and keep it fed, and in the reference that somebody is the
 * server -- nothing in the cache opens 303.
 *
 * ---- what it is fed ----
 *
 * All of it is named by the cache, so none of these numbers is invented:
 *
 *   %hpbar_hud_npc      (varp)    the npc TYPE; the panel takes its name and
 *                                 its `param_510` override from it
 *   %hpbar_hud_hp       (varbit)  current hitpoints
 *   %hpbar_hud_basehp   (varbit)  hitpoints at full
 *   %hpbar_hud_boss     (varbit)  draw the wide boss bar rather than the
 *                                 small one
 *
 * `hpbar_hud_boss` stays 0 here. Which npcs are "certain bosses" (setting 10's
 * wording) is a per-encounter decision the reference leaves to the boss's own
 * content, and this server has no such list -- inventing one would light the
 * wide bar for the wrong monsters, which is worse than not lighting it. A
 * boss script can raise the varbit itself and the panel follows.
 *
 * ---- the setting is INVERTED, and the cache says so in the name ----
 *
 * `hpbar_hud_standard_disabled`: 1 is off. Reading it the plain way would show
 * the overlay to exactly the players who switched it off, which is the failure
 * this whole category is full of.
 */

/** How long the overlay lingers after the last exchange, in ticks.
 *
 *  Not zero: combat drops the target for a moment between an npc dying and the
 *  next click, and an overlay that blinked out on every gap would be unreadable
 *  in a multi-npc fight. Six ticks is the reference's own "still in combat"
 *  feel -- long enough to cover a re-target, short enough that walking away
 *  clears it. */
#define TORIRSSERVER_HPBAR_LINGER_TICKS 6

/** `hpbar_open`: the panel exists but has not been fed yet / is live. */
#define TORIRSSERVER_HPBAR_OPENING 1
#define TORIRSSERVER_HPBAR_LIVE 2

static int
hpbar_target_slot(struct ToriRSServerPlayer* player)
{
    struct ToriRSServer* srv;
    struct ToriRSServerNpc* npc;

    assert(player);

    srv = player->world;
    if( !srv || player->combat_target < 0 || player->combat_target >= TORIRSSERVER_NPC_MAX )
        return -1;
    npc = &srv->npcs[player->combat_target];
    if( !npc->active || npc->death_tick >= 0 )
        return -1;
    return player->combat_target;
}

void
ToriRSServer_HpBarTick(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    struct ToriRSServerIds const* ids = ToriRSServer_Ids();
    int slot;
    int want_type = -1;
    int want_hp = 0;
    int want_max = 0;

    assert(srv);
    assert(player);

    if( ids->iface_hpbar_hud <= 0 )
        return;

    slot = hpbar_target_slot(player);
    if( slot >= 0 )
    {
        struct ToriRSServerNpc const* npc = &srv->npcs[slot];
        want_type = npc->type;
        want_hp = npc->hitpoints;
        want_max = npc->max_hitpoints > 0 ? npc->max_hitpoints : npc->hitpoints;
        player->hpbar_linger = TORIRSSERVER_HPBAR_LINGER_TICKS;
        player->hpbar_last_type = want_type;
        player->hpbar_last_hp = want_hp;
        player->hpbar_last_max = want_max;
    }
    else if( player->hpbar_linger > 0 )
    {
        /* Hold the last reading rather than re-reading a dead npc: the slot may
         * already belong to somebody else. */
        player->hpbar_linger--;
        want_type = player->hpbar_last_type;
        want_hp = player->hpbar_last_hp;
        want_max = player->hpbar_last_max;
    }

    /*
     * The setting, read the way its name spells it.
     *
     * Checked here rather than at the open, so switching it off mid-fight
     * closes the panel on the next tick instead of leaving the last one up.
     */
    if( want_type >= 0 && ToriRSServer_VarbitGet(player, ids->varbit_hpbar_hud_standard_off) )
        want_type = -1;

    if( want_type < 0 )
    {
        if( !player->hpbar_open )
            return;
        player->hpbar_open = 0;
        player->hpbar_linger = 0;
        if( getenv("TORIRS_HPBAR_DEBUG") )
            fprintf(stderr, "hpbar: close\n");
        ToriRSServer_WorldSetVarpOn(srv, player, ids->varp_hpbar_hud_npc, -1);
        ToriRSServer_SendIfClosesub(player, ToriRSServer_PlayerFloater(player));
        return;
    }

    /*
     * The open comes FIRST, and the data one tick later.
     *
     * The panel paints from a var-transmit hook, not from its onload:
     * clientscript 2099 is 303's onload and all it does is register
     * `if_setonvartransmit("script2102(...){var1682, var1683}")`. So a value
     * written before the mount has finished is a change with nobody listening,
     * and the panel stays blank until the npc's hitpoints happen to move.
     *
     * Writing on the next tick instead costs one tick of an empty panel and
     * cannot race the mount, which on this client is a task rather than an
     * immediate build.
     */
    if( !player->hpbar_open )
    {
        int const floater = ToriRSServer_PlayerFloater(player);
        player->hpbar_open = TORIRSSERVER_HPBAR_OPENING;
        if( getenv("TORIRS_HPBAR_DEBUG") )
            fprintf(
                stderr,
                "hpbar: open iface %d into %d:%d (live gameframe %d) for npc type %d (%d/%d)\n",
                ids->iface_hpbar_hud,
                TORIRSSERVER_COM_GROUP(floater),
                TORIRSSERVER_COM_CHILD(floater),
                ToriRSServer_PlayerGameframeIface(player),
                want_type,
                want_hp,
                want_max);
        ToriRSServer_SendIfOpensub(
            player,
            TORIRSSERVER_COM_GROUP(floater),
            TORIRSSERVER_COM_CHILD(floater),
            ids->iface_hpbar_hud,
            1);
        return;
    }

    {
        /*
         * Re-send on the first data tick even when nothing changed.
         *
         * The varp writers dedupe, and they are right to -- but a panel that
         * just opened onto the same npc at the same hitpoints would then be
         * fed nothing at all, because the last thing that changed those values
         * happened before it existed. Marking the carriers re-queues them
         * without pretending they changed.
         */
        int const first = player->hpbar_open == TORIRSSERVER_HPBAR_OPENING;
        int base;

        player->hpbar_open = TORIRSSERVER_HPBAR_LIVE;
        ToriRSServer_WorldSetVarpOn(srv, player, ids->varp_hpbar_hud_npc, want_type);
        base = ToriRSServer_VarbitSetOn(srv, player, ids->varbit_hpbar_hud_hp, want_hp);
        ToriRSServer_VarbitSetOn(srv, player, ids->varbit_hpbar_hud_basehp, want_max);
        if( first )
        {
            ToriRSServer_WorldMarkVarp(player, ids->varp_hpbar_hud_npc);
            if( base >= 0 )
                ToriRSServer_WorldMarkVarp(player, base);
        }
    }
}
