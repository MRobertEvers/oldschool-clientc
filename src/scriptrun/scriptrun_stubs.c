/*
 * Link stand-ins for the client's renderer, painter and minimap, which the
 * client's world model (world/world.c, world/world_cycle.c) references and the
 * in-process script runner never reaches: its World has no scene, no painter
 * and no minimap, and every one of those calls sits behind a pointer that stays
 * NULL. Each aborts loudly if that stops being true, naming itself.
 *
 * Standalone server only (SCRIPTRUN_SRCS); the client links the real ones.
 */
#include "engine/world_builder/heightmap.h"
#include "engine/world_builder/minimap.h"
#include "painters/painters.h"
#include "ui/uitree.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Each stand-in is declared by the real header and defined with the real
 * signature: they were all `void name(void)` once, which the link accepted
 * and LTO reported as five type mismatches. A signature that changes now
 * stops the compile here. */
static _Noreturn void
scriptrun_stub_reached(const char* name)
{
    fprintf(stderr, "scriptrun: the scene-less world reached %s\n", name);
    abort();
}

/* ui/uitree_if_events.c's two tree lookups (Effective, ButtonTarget); the
 * bot keeps the table and reads it with Lookup alone. */
int32_t
UITree_FindByComponentId(
    struct UITree const* tree,
    int component_id)
{
    (void)tree;
    (void)component_id;
    scriptrun_stub_reached("UITree_FindByComponentId");
}

/* world/world.c World_Free calls these only for a non-NULL member, and a
 * scene-less World never sets one. */
void
heightmap_free(struct Heightmap* heightmap)
{
    (void)heightmap;
    scriptrun_stub_reached("heightmap_free");
}

void
minimap_free(struct Minimap* minimap)
{
    (void)minimap;
    scriptrun_stub_reached("minimap_free");
}

void
painter_free(struct Painter* painter)
{
    (void)painter;
    scriptrun_stub_reached("painter_free");
}

void
painters_cullmap_free(struct PaintersCullMap* cm)
{
    (void)cm;
    scriptrun_stub_reached("painters_cullmap_free");
}
