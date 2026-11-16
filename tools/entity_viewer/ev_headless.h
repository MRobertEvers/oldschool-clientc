#ifndef TOOLS_ENTITY_VIEWER_EV_HEADLESS_H
#define TOOLS_ENTITY_VIEWER_EV_HEADLESS_H

/* See ev_headless.c. */

struct Tool_Dat2Cache;

/** A texture's average colour as HSL16 (the ev_build flatten hook), -1 when it
 *  has none. The first call loads the cache's textures and needs `user` to be
 *  the cache; later calls may pass NULL. */
int
ev_headless_flatten_texture(
    int id,
    void* user);

/** Load the cache's textures and install them into ev_render (ev_init first),
 *  so textured faces draw textured; warns and falls back to flat when they do
 *  not load. */
void
ev_headless_install_textures(struct Tool_Dat2Cache* cache);

#endif
