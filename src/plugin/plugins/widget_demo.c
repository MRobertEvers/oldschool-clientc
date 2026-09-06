/* Native binding probe, registered only with TORIRS_WIDGET_DEMO. */
#include "plugin/torirs_plugin_api.h"

static void widget_demo_binding(struct ToriRS_Api* api, void* user, struct ToriRS_WidgetEvent const* event)
{
    struct ToriRS_WidgetApi* ui = &api->widgets;
    struct ToriRS_WidgetBounds before, after;
    (void)user;
    if( event->type == TORIRS_WIDGET_UNBOUND )
    {
        ui->reset(ui->context,event->widget);
        api->core.log(api,"WIDGET_DEMO_UNBOUND");
        return;
    }
    if( event->type != TORIRS_WIDGET_BOUND ) return;
    if( ui->position(ui->context,event->widget,&before) != TORIRS_CONTRACT_OK ) return;
    if( ui->set_position(ui->context,event->widget,before.x-12,before.y) != TORIRS_CONTRACT_OK ) return;
    ui->revalidate(ui->context,event->widget);
    ui->position(ui->context,event->widget,&after);
    api->core.log(api,"WIDGET_DEMO api=%u before=%d,%d %dx%d after=%d,%d %dx%d",
        api->major_version,before.x,before.y,before.width,before.height,
        after.x,after.y,after.width,after.height);
}
static void widget_demo_start(struct ToriRS_Api* api, void* user)
{
    (void)user;
    api->widgets.watch(api->widgets.context,"sidebar",widget_demo_binding,NULL);
}
struct ToriRS_PluginDef const TORIRS_PLUGIN_WIDGET_DEMO = {
    .struct_size=sizeof(struct ToriRS_PluginDef),
    .id="widget-demo",.title="Widget API Probe",.version="1",
    .callbacks={.struct_size=sizeof(struct ToriRS_PluginCallbacks),.on_start=widget_demo_start},
};
