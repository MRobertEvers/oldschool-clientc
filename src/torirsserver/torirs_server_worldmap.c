/*
 * The world map, server side.
 *
 * At rev 230 the world map is not something the client opens by itself. The orb
 * on the minimap is a plain IF3 widget whose only client-side script is a click
 * sound (`opsound`); everything else is the server's:
 *
 *   1. the server arms the orb's ops with IF_SETEVENTS at login, or the client
 *      never sends the click at all (rev 230 has no clickable-by-default);
 *   2. the click arrives as IF_BUTTON<op> naming the orb (160:55, or 897:50 on
 *      the mobile gameframe);
 *   3. the server pushes the player's coord into the map's varcs by running
 *      `worldmap_transmitdata` through RUNCLIENTSCRIPT — the map draws the
 *      "you are here" marker from it and picks the area to open on;
 *   4. the server mounts interface 595 as an overlay in the toplevel's floater
 *      slot (161:18), and unmounts it on the close button.
 *
 * Reference: OpenRune-Server content/.../interfaces/WorldMapEvents.kt, which is
 * the same four steps (`sendWorldMapTile` + `ifOpenOverlay("interfaces.worldmap")`
 * on op 2, `ifCloseOverlay` on `components.worldmap:close`, and a ClickWorldMap
 * handler that moves the player).
 */

#include "torirs_server.h"
#include "torirs_server_ids.h"

#include <stdio.h>

/*
 * The map itself, its floater slot, its two close buttons and the orb are named
 * in the content tree (`worldmap`, `gameframe_floater`, `worldmap:esckey`,
 * `worldmap:close`, `orbs:worldmap` / `orbs_osm:worldmap`) and resolved by
 * ToriRSServer_Ids.
 *
 * The orb was a literal for a while, and two literals were wrong in turn:
 *
 *   - 160:53, on the reasoning that OpenRune's rev-235 table names 160:55
 *     `orbs:worldmap` and 160:53 `wiki_icon`, so the rev-230 orb must be the
 *     lower id. Backwards: `interfaces/orbs.compack` names 53
 *     `wiki_icon_graphic` and 55 `worldmap`, and `orb_worldmap`'s onload
 *     (script 1492 -> proc 1700) installs its ops on 10485815 = 160:55. Arming
 *     53 armed the wiki button and the orb stayed inert.
 *   - 160:55 alone. Right for a desktop gameframe, but a phone gets
 *     `toplevel_osm`, whose orbs are `orbs_osm` (897), and its orb is 897:50.
 *     The tap arrived as IF_BUTTON2 897:50, matched nothing, and the map never
 *     opened on a phone.
 *
 * So both come from the content tree's own compacks -- this cache's names, not
 * another revision's gamevals, which is what the first literal got wrong -- the
 * same twin pair interface_orbs/orbs.rs2 binds for the run and spec orbs.
 *
 * `worldmap_transmitdata` (1749) is a *client* script id, which no pack in this
 * tree carries; it is three ints into varcint 188/1078/401 (you-are-here, clue,
 * gravestone). Jagex sends all three, the last two null (RSProx: every open in
 * indexes 4286, 4339, 4342, 4345, 4349, 4352, 4367 and 4398).
 *
 * The same goes for the four the PHONE's open and close run around their root
 * switch (index 4398 [44] and [73]): keyboard_hide (2239) before the switch to
 * toplevel_display, settings_client_mode (3998, the layout mode) on the way
 * back, then the redraws of what moved -- buff_bar (5937) and the popout
 * strip's layout (5354).
 */
enum
{
    TORIRSSERVER_SCRIPT_WORLDMAP_TRANSMITDATA = 1749,
    TORIRSSERVER_SCRIPT_KEYBOARD_HIDE = 2239,
    TORIRSSERVER_SCRIPT_SETTINGS_CLIENT_MODE = 3998,
    TORIRSSERVER_SCRIPT_POPOUT_LAYOUT = 5354,
    TORIRSSERVER_SCRIPT_BUFF_BAR_REDRAW = 5937,

    /* While the map is up, the marker follows the player on a three-tick clock
     * counted from the open, and only when the tile changed: 437 -> 440,
     * 100 -> 103 -> 106 -> 109, 505 -> 508 -> 511 (indexes 4286, 4339, 4349),
     * nothing at all in 4342/4345 where the player stood still. */
    TORIRSSERVER_WORLDMAP_TILE_PERIOD = 3,

    /* `worldmap:toggles` -- the key's filter row, cells 0..4, op 1 -- armed on
     * every open, right after the mount. */
    TORIRSSERVER_WORLDMAP_TOGGLES_FIRST = 0,
    TORIRSSERVER_WORLDMAP_TOGGLES_LAST = 4,
    TORIRSSERVER_WORLDMAP_TOGGLES_EVENTS = 1 << 1,

    /* Ops 1..3 of the orb. Bit n arms op n; bit 0 is the op-less plain click,
     * which the orb never uses. The close buttons get bit 0 as well as bit 1:
     * the escape binding fires op 1, but whether the red X reaches the menu as
     * an op row or as a bare click depends on what the cache gave it, and both
     * shapes mean the same thing here. */
    TORIRSSERVER_ORB_EVENTS = (1 << 1) | (1 << 2) | (1 << 3),
    TORIRSSERVER_WORLDMAP_CLOSE_EVENTS = 0x1 | (1 << 1),
};

/* The 30-bit coord every world map op speaks: level, then the two tile axes. */
static int
pack_coord(
    int level,
    int abs_x,
    int abs_z)
{
    return ((level & 0x3) << 28) | ((abs_x & 0x3fff) << 14) | (abs_z & 0x3fff);
}

/* The tile the world map should mark and open on. A rider's own x/z are deck-
 * instance pool tiles (the off-map staging band) at a deck plane — nonsense on
 * the world map — so the marker is their scene anchor: the deck tile projected
 * through the hull into the real world, the same seam every other vessel
 * consumer reads. Level 0 for a rider: the anchor is open water. */
static int
worldmap_marker_coord(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    int x = player->x;
    int z = player->z;
    int level = player->level;

    ToriRSServer_PlayerSceneAnchor(srv, player, &x, &z);
    if( x != player->x || z != player->z )
        level = 0;
    return pack_coord(level, x, z);
}

/* Push the player's tile into varcint 188, which is what the map's own scripts
 * read for "you are here" and for choosing which area to open on. Sent on every
 * open and then on the refresh clock while the map is up, because the marker
 * is stale the moment the player walks. */
static void
send_worldmap_tile(struct ToriRSServer* srv)
{
    int args[3];

    args[0] = worldmap_marker_coord(srv);
    args[1] = -1;
    args[2] = -1;
    ToriRSServer_SendRunClientscript(srv->active_player, TORIRSSERVER_SCRIPT_WORLDMAP_TRANSMITDATA, args, 3);
    srv->active_player->worldmap_tile_sent = args[0];
}

static void
run_script(
    struct ToriRSServerPlayer* player,
    int script_id)
{
    ToriRSServer_SendRunClientscript(player, script_id, NULL, 0);
}

/* settings_client_mode with the layout the player saved, which is what the
 * phone's own client mode is reported as. */
static void
run_settings_client_mode(struct ToriRSServerPlayer* player)
{
    int args[1];

    args[0] = player->client_layout_mode;
    ToriRSServer_SendRunClientscript(player, TORIRSSERVER_SCRIPT_SETTINGS_CLIENT_MODE, args, 1);
}

/* Is this a phone on its own gameframe? Then the map does not float over it:
 * the root becomes toplevel_display for as long as the map is up. */
static int
worldmap_switches_top(struct ToriRSServerPlayer* player)
{
    struct ToriRSServerIds const* ids = ToriRSServer_Ids();

    return ToriRSServer_PlayerIsMobile(player) && ids->iface_toplevel_osm > 0 &&
           ids->iface_toplevel_display > 0 && ToriRSServer_PlayerGameframeIface(player) == ids->iface_toplevel_osm;
}

void
ToriRSServer_WorldMapLogin(struct ToriRSServer* srv)
{
    /* from/to are -1: these are plain widgets, not grids, so there is no cell
     * range to arm. The orb armed is the one this player's gameframe mounts --
     * the same test the login uses to pick `toplevel_osm`. */
    int const orb = ToriRSServer_PlayerIsMobile(srv->active_player) && ToriRSServer_Ids()->iface_toplevel_osm > 0
                        ? ToriRSServer_Ids()->com_orbs_osm_worldmap
                        : ToriRSServer_Ids()->com_orbs_worldmap;

    ToriRSServer_SendIfSetevents(srv->active_player, orb, -1, -1, TORIRSSERVER_ORB_EVENTS);
    ToriRSServer_SendIfSetevents(
        srv->active_player, ToriRSServer_Ids()->com_worldmap_esckey, -1, -1, TORIRSSERVER_WORLDMAP_CLOSE_EVENTS);
    ToriRSServer_SendIfSetevents(
        srv->active_player, ToriRSServer_Ids()->com_worldmap_close, -1, -1, TORIRSSERVER_WORLDMAP_CLOSE_EVENTS);
    srv->active_player->worldmap_open = 0;
    srv->active_player->worldmap_tile_sent = -1;
    srv->active_player->worldmap_opened_tick = 0;
    srv->active_player->worldmap_return_top = 0;
}

/*
 * Open the map, as Jagex does, in the same tick as the click.
 *
 * Desktop (every rev-241 open in indexes 4286..4367): 1749 with the player's
 * tile, IF_OPENSUB worldmap into the root's floater as an overlay, then
 * `worldmap:toggles` armed.
 *
 * Phone (index 4398 [44]): the same three, but inside a root switch --
 * keyboard_hide, toplevel_osm -> toplevel_display with every slot carried (the
 * switch remounts the orbs itself), and floater_blankmodal in displaymodal
 * BEHIND the map, a modal, which is what keeps the world under it from taking
 * a tap.
 */
void
ToriRSServer_WorldMapOpen(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    struct ToriRSServerIds const* ids = ToriRSServer_Ids();

    if( player->worldmap_open )
        return;
    player->worldmap_return_top = 0;
    if( worldmap_switches_top(player) )
    {
        player->worldmap_return_top = ToriRSServer_PlayerGameframeIface(player);
        run_script(player, TORIRSSERVER_SCRIPT_KEYBOARD_HIDE);
        ToriRSServer_GameframeSwitchTop(player, ids->iface_toplevel_display);
    }
    /* Order matters: the coord has to be in the varc before interface 595's
     * onload runs, or `worldmap_loadmap` opens on the main area's origin
     * instead of on the player. The client's exec pipeline is a strict FIFO,
     * so "sent first" is "applied first". */
    send_worldmap_tile(srv);
    if( player->worldmap_return_top )
    {
        ToriRSServer_SendIfOpensub(player,
                                   TORIRSSERVER_COM_GROUP(ids->com_toplevel_display_displaymodal),
                                   TORIRSSERVER_COM_CHILD(ids->com_toplevel_display_displaymodal),
                                   ids->iface_floater_blankmodal,
                                   0);
        run_script(player, TORIRSSERVER_SCRIPT_POPOUT_LAYOUT);
    }
    ToriRSServer_SendIfOpensub(
        player, ids->iface_gameframe, TORIRSSERVER_COM_CHILD(ids->com_gameframe_floater), ids->iface_worldmap, 1);
    ToriRSServer_SendIfSetevents(player,
                                 ids->com_worldmap_toggles,
                                 TORIRSSERVER_WORLDMAP_TOGGLES_FIRST,
                                 TORIRSSERVER_WORLDMAP_TOGGLES_LAST,
                                 TORIRSSERVER_WORLDMAP_TOGGLES_EVENTS);
    player->worldmap_open = 1;
    player->worldmap_opened_tick = srv->tick;
    if( srv->verbose )
        fprintf(stderr, "torirsserver: world map opened%s\n", player->worldmap_return_top ? " (toplevel_display)" : "");
}

/*
 * Close it. Desktop: IF_CLOSESUB of the floater and nothing else. Phone (index
 * 4398 [73]): the floater, then displaymodal, then the switch back to the root
 * it came from, and the redraws of what the switch moved --
 * in the recorded order, settings_client_mode on both sides of the switch.
 */
void
ToriRSServer_WorldMapClose(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    struct ToriRSServerIds const* ids = ToriRSServer_Ids();
    int const return_top = player->worldmap_return_top;

    if( !player->worldmap_open )
        return;
    ToriRSServer_SendIfClosesub(player, ids->com_gameframe_floater);
    if( return_top )
    {
        ToriRSServer_SendIfClosesub(player, ids->com_toplevel_display_displaymodal);
        run_settings_client_mode(player);
        ToriRSServer_GameframeSwitchTop(player, return_top);
        run_script(player, TORIRSSERVER_SCRIPT_BUFF_BAR_REDRAW);
        run_settings_client_mode(player);
        run_script(player, TORIRSSERVER_SCRIPT_BUFF_BAR_REDRAW);
        run_script(player, TORIRSSERVER_SCRIPT_POPOUT_LAYOUT);
    }
    player->worldmap_open = 0;
    player->worldmap_return_top = 0;
    if( srv->verbose )
        fprintf(stderr, "torirsserver: world map closed\n");
}

int
ToriRSServer_WorldMapHandleButton(
    struct ToriRSServer* srv,
    int uid,
    int op)
{
    if( uid == ToriRSServer_Ids()->com_orbs_worldmap || uid == ToriRSServer_Ids()->com_orbs_osm_worldmap )
    {
        /* Every open verb toggles: clicking the orb with the map already up
         * closes it, which is what the reference does and what the orb's own
         * "Close Floating panel" op expects. Ops 2 and 3 are "floating" and
         * "fullscreen"; the mock has one presentation, so both open it. */
        if( srv->active_player->worldmap_open )
            ToriRSServer_WorldMapClose(srv);
        else if( op == 2 || op == 3 )
            ToriRSServer_WorldMapOpen(srv);
        return 1;
    }
    if( uid == ToriRSServer_Ids()->com_worldmap_esckey || uid == ToriRSServer_Ids()->com_worldmap_close )
    {
        (void)op;
        ToriRSServer_WorldMapClose(srv);
        return 1;
    }
    return 0;
}

void
ToriRSServer_WorldMapClick(
    struct ToriRSServer* srv,
    int level,
    int abs_x,
    int abs_z)
{
    if( srv->verbose )
        fprintf(stderr, "torirsserver: <- CLICK_WORLD_MAP %d,%d,%d\n", level, abs_x, abs_z);
    /* The reference gates this on an admin privilege and moves the player
     * there. So does this server, by the same rule as a ::command: anyone on a
     * development server, only staff where passwords are checked. */
    if( !ToriRSServer_PlayerMayCheat(srv, srv->active_player) )
    {
        fprintf(stderr, "torirsserver: %s is not staff; refused world map teleport\n",
                srv->active_player->display_name);
        return;
    }
    ToriRSServer_WorldTeleport(srv, level, abs_x, abs_z);
    if( srv->active_player->worldmap_open )
        send_worldmap_tile(srv);
}

void
ToriRSServer_WorldMapTick(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player = srv->active_player;

    if( !player->worldmap_open )
        return;
    if( (srv->tick - player->worldmap_opened_tick) % TORIRSSERVER_WORLDMAP_TILE_PERIOD != 0 )
        return;
    if( worldmap_marker_coord(srv) == player->worldmap_tile_sent )
        return;
    send_worldmap_tile(srv);
}
