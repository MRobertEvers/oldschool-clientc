/* Initial live-widget transport probe. Register only with TORIRS_WIDGET_DEMO.
 * It deliberately uses the same public API as product plugins. Native load/
 * rebuild subscriptions replace this one-time readiness retry in the next slice. */
#include "plugin/torirs_plugin_api.h"

struct WidgetDemoState { struct ToriRS_WidgetRef sidebar; int done; };

static void widget_demo_tick(struct ToriRS_Api* api, void* user, struct ToriRS_TickEvent const* event)
{
    struct WidgetDemoState* state = user;
    struct ToriRS_WidgetApi* ui = &api->widgets;
    struct ToriRS_WidgetBounds before, after;
    (void)event;
    if( state->done || !api->core.capability(api, "widgets.geometry") ) return;
    if( ui->find(ui->context, "sidebar", &state->sidebar) != TORIRS_CONTRACT_OK ) return;
    if( ui->position(ui->context, state->sidebar, &before) != TORIRS_CONTRACT_OK || before.width <= 0 ) return;
    enum ToriRS_ContractResult moved = ui->set_position(ui->context, state->sidebar, before.x - 12, before.y);
    if( moved != TORIRS_CONTRACT_OK ) return;
    ui->revalidate(ui->context, state->sidebar);
    ui->position(ui->context, state->sidebar, &after);
    api->core.log(api, "WIDGET_DEMO api=%u before=%d,%d %dx%d after=%d,%d %dx%d",
                  api->major_version, before.x,before.y,before.width,before.height,
                  after.x,after.y,after.width,after.height);
    state->done = 1;
}

struct ToriRS_PluginDef const TORIRS_PLUGIN_WIDGET_DEMO = {
    .struct_size = sizeof(struct ToriRS_PluginDef),
    .id = "widget-demo", .title = "Widget API Probe", .version = "1",
    .state_size = sizeof(struct WidgetDemoState),
    .callbacks = {.struct_size = sizeof(struct ToriRS_PluginCallbacks), .on_logic_tick = widget_demo_tick},
};
