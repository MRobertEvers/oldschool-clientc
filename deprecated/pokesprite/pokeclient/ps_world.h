#ifndef PS_WORLD_H
#define PS_WORLD_H

/*
 * The 2D world: what the PokeSprite client draws where the 3D client draws its
 * scene. It reads the same world state the 3D client renders from (the
 * minimap's tiles, the npc and player pools) and draws it top-down: tiles,
 * then every actor back to front as a sheet from io_server.
 */

#include "ps_fetch.h"

#include <stdint.h>

#define PS_TILE_PX 32 /* one 128-unit OSRS tile on screen */

struct App;
struct PS_World;

struct PS_World*
PS_WorldNew(struct PS_Fetch* fetch);

void
PS_WorldFree(struct PS_World* world);

/**
 * Draw the world into `pixels` (stride `stride` ints, 0xAARRGGBB) inside the
 * clip rect [x0, x1) x [y0, y1), centred on (cx, cy). `now_ms` drives the
 * sheet animation clocks.
 */
void
PS_WorldDraw(
    struct PS_World* world,
    struct App* app,
    int* pixels,
    int stride,
    int x0,
    int y0,
    int x1,
    int y1,
    int cx,
    int cy,
    uint64_t now_ms);

struct ToriRS_PickHits;

/**
 * The world's pick hits for the mouse at (mx, my), from the LAST draw: the tile
 * under it, then every actor whose drawn pixels' bounds hold it, in draw order
 * (back to front -- the order the 3D raster reports hits in, which
 * App_PickFinish expects).
 */
void
PS_WorldPick(
    struct PS_World* world,
    int mx,
    int my,
    struct ToriRS_PickHits* hits);

#endif
