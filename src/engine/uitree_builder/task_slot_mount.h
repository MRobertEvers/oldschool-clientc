#ifndef TASK_SLOT_MOUNT_H
#define TASK_SLOT_MOUNT_H

#include "asyncio.h"

#include <stdint.h>

struct UITreeBuilder;
struct App;

/**
 * Mount an interface pack under a slot owner node at runtime (reference
 * IF_OPENMAIN / IF_OPENSIDE / IF_OPENCHAT / IF_SETTAB): loads the pack and
 * what of its assets a later packet reads, clears the owner's children, and
 * re-bakes through the same path the initial RevConfig build used; its
 * models are App_SlotVisualsAdd's (below). iface_id <= 0 just clears the
 * slot.
 *
 * Runtime mounts carry no INI inv= binding; INV widgets in the pack keep
 * their pack inv sources and fill via UPDATE_INV state sync.
 *
 * `app` stamps RS_EVENT_SLOT_MOUNTED at completion -- the dat1 lane's
 * mount fence, ridden by both ui.await_open and ui.await_close since this
 * one event covers a mount and a clear alike. The task's only caller always
 * has an App in scope, so this is asserted, not optional.
 */
struct ToriRS_Task*
CreateTask_SlotMount(
    struct App* app,
    struct UITreeBuilder* builder,
    int32_t owner_index,
    int iface_id);

/*
 * The App layer's half of a slot mount (app/app_tasks_mounts.c), declared
 * here because the slot mount is the one caller.
 *
 * What the mount waits for is what a later packet or script reads: the
 * ComponentPack, its sprites, fonts and npc configs (through the assets table,
 * GroupMount_AskComponentPackParts) and the bake. Its models and the default avatar only change what is drawn:
 * App_SlotVisualsAdd gives each model a widget under `owner_index` still
 * waits for one parallel task, which binds it when it arrives, and wants the
 * worn and backpack icons not baked yet. Returns how many model tasks it
 * added. App_SlotVisualsCancel cancels those of the nodes under
 * `owner_index` now, before the slot is cleared or remounted: a dat1
 * component id is flat, so its tasks cannot be found by an interface group.
 */
int
App_SlotVisualsAdd(
    struct App* app,
    int32_t owner_index);
void
App_SlotVisualsCancel(
    struct App* app,
    int32_t owner_index);

#endif
