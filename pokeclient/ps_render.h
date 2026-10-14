#ifndef PS_RENDER_H
#define PS_RENDER_H

/*
 * The PokeSprite client's frame: App_Render's job, with the world drawn by
 * ps_world instead of the 3D painter.
 *
 * The frame is built exactly as the 3D client builds it (App_BuildFrame: the
 * UI emit walk, every side effect the app relies on) and every command but the
 * 3D pass goes to the stock soft renderer, in order. At BEGIN_3D -- the world
 * viewport, at its place in the UI's draw order -- the 2D world is drawn into
 * the rect the command carries; the pass's DRAW_MODELs are dropped. So the
 * interface, the minimap, menus and chat draw over and under the world exactly
 * where they do in the 3D client.
 */

#include <stdint.h>

struct App;
struct PS_World;

void
PS_Render(
    struct App* app,
    struct PS_World* world,
    int* pixels,
    int width,
    int height,
    uint64_t now_ms);

#endif
