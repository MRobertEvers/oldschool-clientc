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

/*
 * op slot (0-based: op number - 1) -> the action id the client's own row
 * builder would use for that slot. The rev-254 OP*1..5 action ids are NOT
 * contiguous (revconfig.h:52-69), so `OP*1 + slot` only resolves correctly
 * for slot 0 -- these are the only correct answer.
 *
 * Exported (were static to this file) for the quest driver's
 * DrivePointer_ActionForSlot (src/plugin/torirs_plugin_drive_pointer.c),
 * which does the same `App_MinimenuRowFind`-style matching by ACTION ID,
 * never by row text (docs/QUEST_DRIVER_PLAN.md S5.2). Owner of this export:
 * verbs-pointer (docs/ARCHITECT.md).
 */
int
opnpc_action_for_slot(int slot);

int
oploc_action_for_slot(int slot);

int
opobj_action_for_slot(int slot);

#endif
