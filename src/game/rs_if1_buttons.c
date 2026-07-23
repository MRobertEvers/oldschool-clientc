#include "rs_if1_buttons.h"

#include "app.h"
#include "revconfig/revconfig.h"
#include "rs_clientcode.h"
#include "rs_ui_slots.h"
#include "ui/uitree.h"
#include "varp/varp_manager.h"

#include <assert.h>
#include <stdio.h>

/* IF1 value-script opcode 5 = "read varp"; a button whose first script starts
 * with it toggles/selects that varp (reference TOGGLE_BUTTON/SELECT_BUTTON). */
static int
button_varp_id(struct UITreeComponent const* c)
{
    struct UITreeBehavior const* b = &c->behavior;
    if( b->scripts_count <= 0 || !b->scripts || !b->scripts[0] )
        return -1;
    if( !b->scripts_lengths || b->scripts_lengths[0] < 2 )
        return -1;
    if( b->scripts[0][0] != 5 )
        return -1;
    return b->scripts[0][1];
}

static void
sink_if_button(
    struct App* app,
    int com_id)
{
    if( app->button_sink.if_button )
        app->button_sink.if_button(app->button_sink.user, com_id);
}

int
RS_IF1_ApplyButtonClick(
    struct App* app,
    int com_id,
    int action)
{
    int32_t idx;
    struct UITreeComponent const* c;

    assert(app);
    idx = UITree_FindByComponentId(app->tree, com_id);
    if( idx < 0 )
        return 0;
    c = &app->tree->components[idx];

    switch( action )
    {
    case REVCONFIG_MINIMENU_IF_BUTTON:
    {
        int notify = 1;
        /* client_code is an old-gen (IF1/CS1) baked behavior; modern components
         * never carry one, but gate on ui logic too so intent is explicit. */
        if( c->behavior.client_code > 0 && App_UiLogic(app) == APP_UI_LOGIC_CS1 )
            notify = RS_ClientCode_Button(app, c);
        if( notify )
            sink_if_button(app, com_id);
        app->need_redraw = 1;
        return 1;
    }

    case REVCONFIG_MINIMENU_IF_BUTTON_TOGGLE:
    {
        int varp = button_varp_id(c);
        sink_if_button(app, com_id);
        if( varp >= 0 )
        {
            int value = VarPManager_GetVarp(&app->varps, varp);
            VarPManager_SetVarpOptimistic(&app->varps, varp, 1 - value);
        }
        app->need_redraw = 1;
        return 1;
    }

    case REVCONFIG_MINIMENU_IF_BUTTON_SELECT:
    {
        int varp = button_varp_id(c);
        sink_if_button(app, com_id);
        if( varp >= 0 && c->behavior.script_operand && c->behavior.comparator_count > 0 )
        {
            int target = c->behavior.script_operand[0];
            if( VarPManager_GetVarp(&app->varps, varp) != target )
                VarPManager_SetVarpOptimistic(&app->varps, varp, target);
        }
        app->need_redraw = 1;
        return 1;
    }

    case REVCONFIG_MINIMENU_RESUME_PAUSEBUTTON:
        if( app->button_sink.resume_pausebutton )
            app->button_sink.resume_pausebutton(app->button_sink.user, com_id);
        /* Reference waits for the server's next dialog page; offline, clear
         * the dialog so "Click here to continue" still does something. */
        if( !app->button_sink.resume_pausebutton && app->slots.chat_com_id != -1 )
            RS_UISlots_CloseModal(app);
        app->need_redraw = 1;
        return 1;

    case REVCONFIG_MINIMENU_CLOSE_MODAL:
        RS_UISlots_CloseModal(app);
        /* A local close click notifies the server (reference CLOSE_MODAL);
         * server-initiated IF_CLOSE does not echo. Reuses the pausebutton
         * sink slot's App so app.c can route it. */
        if( app->button_sink.close_modal )
            app->button_sink.close_modal(app->button_sink.user);
        return 1;

    default:
        return 0;
    }
}
