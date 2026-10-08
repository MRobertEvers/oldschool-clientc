/*
 * Link stand-ins for the client's renderer, painter and minimap, which the
 * client's world model (world/world.c, world/world_cycle.c) references and the
 * in-process script runner never reaches: its World has no scene, no painter
 * and no minimap, and every one of those calls sits behind a pointer that stays
 * NULL. Each aborts loudly if that stops being true, naming itself.
 *
 * Standalone server only (SCRIPTRUN_SRCS); the client links the real ones.
 */
#include <stdio.h>
#include <stdlib.h>

#define SCRIPTRUN_STUB(name)                                                                       \
    void name(void);                                                                               \
    void name(void)                                                                                \
    {                                                                                              \
        fprintf(stderr, "scriptrun: the scene-less world reached %s\n", #name);                    \
        abort();                                                                                   \
    }

/* world/world.c World_Free calls these only for a non-NULL member, and a
 * scene-less World never sets one. */
SCRIPTRUN_STUB(heightmap_free)
SCRIPTRUN_STUB(minimap_free)
SCRIPTRUN_STUB(painter_free)
SCRIPTRUN_STUB(painters_cullmap_free)
