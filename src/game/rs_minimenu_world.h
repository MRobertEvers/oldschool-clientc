#ifndef SRC_RS_MINIMENU_WORLD_H
#define SRC_RS_MINIMENU_WORLD_H

struct RS_MinimenuBuildCtx;
struct UIMinimenu;

/**
 * World pickset -> menu rows (v1 ui_click_build_minimenu_from_pickset):
 * "Walk here" targeting the nearest picked terrain tile, then per-pick NPC
 * ("<op> @yel@<name> (level-N)" + Examine) and scenery ("<op> @cya@<name>"
 * + Examine) rows in pickset order. Names/actions come from the spawned world
 * entities, so no cache fetch happens at menu-build time. Called from
 * RS_Minimenu_Build when the click landed in the world viewport.
 */
void
RS_Minimenu_AddWorldRows(
    struct RS_MinimenuBuildCtx const* ctx,
    struct UIMinimenu* menu);

#endif
