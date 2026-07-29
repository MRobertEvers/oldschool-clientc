/*
 * The world map, server side.
 *
 * At rev 230 the world map is not something the client opens by itself. The orb
 * on the minimap is a plain IF3 widget whose only client-side script is a click
 * sound (`opsound`); everything else is the server's:
 *
 *   1. the server arms the orb's ops with IF_SETEVENTS at login, or the client
 *      never sends the click at all (rev 230 has no clickable-by-default);
 *   2. the click arrives as IF_BUTTON<op> naming component 160:53;
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

#include "mock230.h"

#include <stdio.h>

/*
 * Ids, all verified against cache.osrs230 with the decompiled clientscripts:
 *
 * - 160:53 is the orb's inner graphic — `orbs_worldmap_setup` (proc 1700) is
 *   what installs "Floating World Map" (op 2) and "Fullscreen World Map"
 *   (op 3) on it.
 * - 1749 is `worldmap_transmitdata`, three ints into varcint 188/1078/401.
 *   Only the first (the packed coord) is read by anything the mock drives.
 * - 161:18 is the toplevel's floater slot: `orbs_worldmap_setup` tests
 *   `if_hassub(... interface_161:18)` to decide whether the orb should offer
 *   "Close Floating panel" instead of the two open verbs, so it is by
 *   definition where the map mounts.
 * - 595:4 and 595:38 are both "close". 595:4 is a zero-sized component that
 *   exists only to hold the escape binding (`worldmap_init`'s trailing
 *   `if_setopkey(1, ^key_escape, 0, interface_595:4)`); 595:38 is the red X in
 *   the corner (`worldmap_mobile_layout` repositions it, `deadman_spectator_map`
 *   hides it). Neither carries an onop script, so both are the server's.
 */
enum
{
    MOCK230_ORB_IFACE = 160,
    MOCK230_ORB_WORLDMAP_CHILD = 53,
    MOCK230_ORB_WORLDMAP_UID = (MOCK230_ORB_IFACE << 16) | MOCK230_ORB_WORLDMAP_CHILD,

    MOCK230_WORLDMAP_IFACE = 595,
    MOCK230_WORLDMAP_CLOSE_UID = (MOCK230_WORLDMAP_IFACE << 16) | 4,
    MOCK230_WORLDMAP_CLOSEX_UID = (MOCK230_WORLDMAP_IFACE << 16) | 38,
    MOCK230_WORLDMAP_SLOT_CHILD = 18,
    MOCK230_WORLDMAP_SLOT_UID = (MOCK230_ROOT_IFACE << 16) | MOCK230_WORLDMAP_SLOT_CHILD,

    MOCK230_SCRIPT_WORLDMAP_TRANSMITDATA = 1749,

    /* Ops 1..3 of the orb. Bit n arms op n; bit 0 is the op-less plain click,
     * which the orb never uses. The close buttons get bit 0 as well as bit 1:
     * the escape binding fires op 1, but whether the red X reaches the menu as
     * an op row or as a bare click depends on what the cache gave it, and both
     * shapes mean the same thing here. */
    MOCK230_ORB_EVENTS = (1 << 1) | (1 << 2) | (1 << 3),
    MOCK230_WORLDMAP_CLOSE_EVENTS = 0x1 | (1 << 1),
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

/* Push the player's tile into varcint 188, which is what the map's own scripts
 * read for "you are here" and for choosing which area to open on. Sent on every
 * open and then once per tick while the map is up, because the marker is stale
 * the moment the player walks. */
static void
send_worldmap_tile(struct Mock230Server* srv)
{
    struct Mock230Player* player = &srv->player;
    int args[1];

    args[0] = pack_coord(player->level, player->x, player->z);
    mock230_send_run_clientscript(srv, MOCK230_SCRIPT_WORLDMAP_TRANSMITDATA, args, 1);
}

void
mock230_worldmap_login(struct Mock230Server* srv)
{
    /* from/to are -1: these are plain widgets, not grids, so there is no cell
     * range to arm. */
    mock230_send_if_setevents(srv, MOCK230_ORB_WORLDMAP_UID, -1, -1, MOCK230_ORB_EVENTS);
    mock230_send_if_setevents(
        srv, MOCK230_WORLDMAP_CLOSE_UID, -1, -1, MOCK230_WORLDMAP_CLOSE_EVENTS);
    mock230_send_if_setevents(
        srv, MOCK230_WORLDMAP_CLOSEX_UID, -1, -1, MOCK230_WORLDMAP_CLOSE_EVENTS);
    srv->player.worldmap_open = 0;
    srv->player.worldmap_tile_sent = -1;
}

void
mock230_worldmap_open(struct Mock230Server* srv)
{
    if( srv->player.worldmap_open )
        return;
    /* Order matters: the coord has to be in the varc before interface 595's
     * onload runs, or `worldmap_loadmap` opens on the main area's origin
     * instead of on the player. The client's exec pipeline is a strict FIFO,
     * so "sent first" is "applied first". */
    send_worldmap_tile(srv);
    mock230_send_if_opensub(
        srv, MOCK230_ROOT_IFACE, MOCK230_WORLDMAP_SLOT_CHILD, MOCK230_WORLDMAP_IFACE, 1);
    srv->player.worldmap_open = 1;
    srv->player.worldmap_tile_sent = pack_coord(srv->player.level, srv->player.x, srv->player.z);
    if( srv->verbose )
        fprintf(stderr, "mock230: world map opened\n");
}

void
mock230_worldmap_close(struct Mock230Server* srv)
{
    if( !srv->player.worldmap_open )
        return;
    mock230_send_if_closesub(srv, MOCK230_WORLDMAP_SLOT_UID);
    srv->player.worldmap_open = 0;
    if( srv->verbose )
        fprintf(stderr, "mock230: world map closed\n");
}

int
mock230_worldmap_handle_button(
    struct Mock230Server* srv,
    int uid,
    int op)
{
    if( uid == MOCK230_ORB_WORLDMAP_UID )
    {
        /* Every open verb toggles: clicking the orb with the map already up
         * closes it, which is what the reference does and what the orb's own
         * "Close Floating panel" op expects. Ops 2 and 3 are "floating" and
         * "fullscreen"; the mock has one presentation, so both open it. */
        if( srv->player.worldmap_open )
            mock230_worldmap_close(srv);
        else if( op == 2 || op == 3 )
            mock230_worldmap_open(srv);
        return 1;
    }
    if( uid == MOCK230_WORLDMAP_CLOSE_UID || uid == MOCK230_WORLDMAP_CLOSEX_UID )
    {
        (void)op;
        mock230_worldmap_close(srv);
        return 1;
    }
    return 0;
}

void
mock230_worldmap_click(
    struct Mock230Server* srv,
    int level,
    int abs_x,
    int abs_z)
{
    if( srv->verbose )
        fprintf(stderr, "mock230: <- CLICK_WORLD_MAP %d,%d,%d\n", level, abs_x, abs_z);
    /* The reference gates this on an admin privilege and moves the player
     * there. The mock has one player and no privilege system, so it is
     * unconditional — the point of the packet here is that the round trip is
     * observable at all. */
    mock230_world_teleport(srv, level, abs_x, abs_z);
    if( srv->player.worldmap_open )
        send_worldmap_tile(srv);
}

void
mock230_worldmap_tick(struct Mock230Server* srv)
{
    int packed;

    if( !srv->player.worldmap_open )
        return;
    packed = pack_coord(srv->player.level, srv->player.x, srv->player.z);
    if( packed == srv->player.worldmap_tile_sent )
        return;
    srv->player.worldmap_tile_sent = packed;
    send_worldmap_tile(srv);
}
