#include "rs_ui_slots.h"

#include "app.h"
#include "engine/uitree_builder/task_slot_mount.h"
#include "ui/uitree.h"
#include "ui/uitree_layout.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void
RS_UISlots_Init(struct RS_UISlots* slots)
{
    assert(slots);
    memset(slots, 0, sizeof(*slots));
    for( int i = 0; i < RS_UI_SLOTS_TAB_MAX; i++ )
    {
        slots->side_overlay_id[i] = -1;
        slots->side_owner_index[i] = -1;
    }
    slots->side_modal_id = -1;
    slots->main_modal_id = -1;
    slots->main_overlay_id = -1;
    slots->chat_com_id = -1;
    slots->tut_com_id = -1;
    slots->main_modal_index = -1;
    slots->main_overlay_index = -1;
    slots->side_modal_index = -1;
    slots->chat_index = -1;
    slots->tut_index = -1;
    slots->flash_tab = -1;
}

void
RS_UISlots_InitFromTree(
    struct RS_UISlots* slots,
    struct UITree const* tree)
{
    assert(slots);
    assert(tree);

    RS_UISlots_Init(slots);

    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct UITreeComponent const* c = &tree->components[i];
        if( c->freed )
            continue;

        if( c->type == UIELEM_BUILTIN_SIDEBAR )
        {
            int tabno = c->u.sidebar.tabno;
            if( tabno < 0 || tabno >= RS_UI_SLOTS_TAB_MAX )
                continue;
            slots->side_overlay_id[tabno] =
                c->u.sidebar.componentno >= 0 ? c->u.sidebar.componentno : -1;
            slots->side_owner_index[tabno] = (int32_t)i;
            if( c->u.sidebar.selected )
                slots->side_tab = tabno;
            continue;
        }

        switch( (enum UITreeSlotTag)c->slot_tag )
        {
        case UITREE_SLOT_MAIN_MODAL:
            slots->main_modal_index = (int32_t)i;
            break;
        case UITREE_SLOT_MAIN_OVERLAY:
            slots->main_overlay_index = (int32_t)i;
            break;
        case UITREE_SLOT_SIDE_MODAL:
            slots->side_modal_index = (int32_t)i;
            break;
        case UITREE_SLOT_CHAT:
            slots->chat_index = (int32_t)i;
            break;
        case UITREE_SLOT_TUT:
            slots->tut_index = (int32_t)i;
            break;
        case UITREE_SLOT_NONE:
            break;
        }
    }
}

int
RS_UISlots_TabEnabled(
    struct RS_UISlots const* slots,
    int tabno)
{
    assert(slots);
    if( tabno < 0 || tabno >= RS_UI_SLOTS_TAB_MAX )
        return 0;
    return slots->side_overlay_id[tabno] != -1;
}

/* Wrapper protothread: run the mount, then relayout + CS1 re-request over
 * the new subtree. Enqueued on the app's serial exec pipeline so a mount a
 * packet triggers completes before the next packet is popped (and UI-driven
 * mounts serialize with packet exec the same way). */
struct Task_SlotMountRefresh
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    int32_t owner_index;
    int iface_id;
};

static int
Task_SlotMountRefresh_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_SlotMountRefresh* self = (struct Task_SlotMountRefresh*)base;

    PT_BEGIN(&self->pt);
    TASK_AWAITSELF_IF(CreateTask_SlotMount(&self->app->builder, self->owner_index, self->iface_id));
    App_RefreshAfterTreeMutation(self->app);
    PT_END(&self->pt);
}

static void
Task_SlotMountRefresh_Free(struct ToriRS_Task* base)
{
    free(base);
}

static struct ToriRS_TaskVTable Task_SlotMountRefresh_VTable = {
    .run = Task_SlotMountRefresh_Run,
    .free = Task_SlotMountRefresh_Free,
};

/* Queue a mount for one slot region on the serial exec pipeline. Never
 * blocks; the frame pump drives it. */
static void
slot_mount(
    struct App* app,
    int32_t owner_index,
    int iface_id)
{
    struct Task_SlotMountRefresh* task;

    assert(app);
    if( owner_index < 0 )
    {
        if( iface_id > 0 )
            fprintf(stderr, "rs_ui_slots: no mount region for iface %d\n", iface_id);
        return;
    }
    if( !app->builder_active )
    {
        /* Cache-interface boot (dat2 path) has no RevConfig builder; slot
         * mounts there go through the CS2 openSub flow instead. */
        fprintf(stderr, "rs_ui_slots: slot mount requires the RevConfig build path\n");
        return;
    }

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_SlotMountRefresh_VTable;
    strncpy(task->task.name, "SlotMountRefresh", sizeof(task->task.name) - 1);
    task->app = app;
    task->owner_index = owner_index;
    task->iface_id = iface_id;
    PT_INIT(&task->pt);
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

void
RS_UISlots_OpenMain(struct App* app, int iface_id)
{
    assert(app);
    app->slots.main_modal_id = iface_id > 0 ? iface_id : -1;
    slot_mount(app, app->slots.main_modal_index, iface_id);
}

void
RS_UISlots_OpenMainSide(struct App* app, int main_iface_id, int side_iface_id)
{
    assert(app);
    RS_UISlots_OpenMain(app, main_iface_id);
    RS_UISlots_OpenSide(app, side_iface_id);
}

void
RS_UISlots_OpenSide(struct App* app, int iface_id)
{
    assert(app);
    app->slots.side_modal_id = iface_id > 0 ? iface_id : -1;
    slot_mount(app, app->slots.side_modal_index, iface_id);
    app->need_redraw = 1;
}

void
RS_UISlots_OpenChat(struct App* app, int iface_id)
{
    assert(app);
    /* Reference IF_OPENCHAT closes any open modals first. */
    if( app->slots.main_modal_id != -1 || app->slots.side_modal_id != -1 )
        RS_UISlots_CloseModal(app);
    app->slots.chat_com_id = iface_id > 0 ? iface_id : -1;
    slot_mount(app, app->slots.chat_index, iface_id);
}

void
RS_UISlots_OpenOverlay(struct App* app, int iface_id)
{
    assert(app);
    app->slots.main_overlay_id = iface_id > 0 ? iface_id : -1;
    slot_mount(app, app->slots.main_overlay_index, iface_id);
    app->need_redraw = 1;
}

void
RS_UISlots_OpenTut(struct App* app, int iface_id)
{
    assert(app);
    app->slots.tut_com_id = iface_id > 0 ? iface_id : -1;
    slot_mount(app, app->slots.tut_index, iface_id);
    app->need_redraw = 1;
}

void
RS_UISlots_CloseModal(struct App* app)
{
    assert(app);
    if( app->slots.main_modal_id != -1 )
    {
        app->slots.main_modal_id = -1;
        slot_mount(app, app->slots.main_modal_index, -1);
    }
    if( app->slots.side_modal_id != -1 )
    {
        app->slots.side_modal_id = -1;
        slot_mount(app, app->slots.side_modal_index, -1);
    }
    if( app->slots.chat_com_id != -1 )
    {
        app->slots.chat_com_id = -1;
        slot_mount(app, app->slots.chat_index, -1);
    }
    app->need_redraw = 1;
}

void
RS_UISlots_SetTab(struct App* app, int tabno, int iface_id)
{
    assert(app);
    if( tabno < 0 || tabno >= RS_UI_SLOTS_TAB_MAX )
        return;
    /* Wire format: 65535 clears the slot. */
    if( iface_id == 65535 )
        iface_id = -1;
    if( getenv("TORIRS_NET_DEBUG") )
        fprintf(
            stderr,
            "rs_ui_slots: settab tab=%d iface=%d owner=%d\n",
            tabno,
            iface_id,
            app->slots.side_owner_index[tabno]);
    app->slots.side_overlay_id[tabno] = iface_id > 0 ? iface_id : -1;
    slot_mount(app, app->slots.side_owner_index[tabno], iface_id);
}

void
RS_UISlots_SetSideTab(struct App* app, int tabno)
{
    assert(app);
    if( tabno < 0 || tabno >= RS_UI_SLOTS_TAB_MAX )
        return;
    if( app->slots.side_tab != tabno )
    {
        app->slots.side_tab = tabno;
        app->need_redraw = 1;
    }
}

int
RS_UISlots_CycleChatFilter(
    struct RS_UISlots* slots,
    int filter)
{
    /* Mode counts per filter, reference Client-TS gameLoop (public %4,
     * private %3, trade %3; report is a click-through, not a cycle). */
    static int const k_mode_count[RS_UI_CHAT_FILTER_COUNT] = { 4, 3, 3, 1 };

    assert(slots);
    if( filter < 0 || filter >= RS_UI_CHAT_FILTER_COUNT )
        return 0;
    slots->chat_filter_mode[filter] =
        (slots->chat_filter_mode[filter] + 1) % k_mode_count[filter];
    return slots->chat_filter_mode[filter];
}
