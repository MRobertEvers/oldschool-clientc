/* Native widget probe, registered only with TORIRS_WIDGET_DEMO. */
#include "plugin/torirs_plugin_api.h"
#include <stdio.h>
#include <string.h>

struct WidgetDemoState { struct ToriRS_WidgetRef label; };
static void widget_demo_update(struct ToriRS_Api* api, void* user, struct ToriRS_TickEvent const* tick)
{
    struct WidgetDemoState* state=user;
    struct ToriRS_SkillSnapshot skill={.struct_size=sizeof(skill)};
    char text[64];
    (void)tick;
    if( !state->label.opaque[2] || !api->game || !api->game->skill(api,2,&skill) ) return;
    snprintf(text,sizeof(text),"Strength: %d",skill.current_level);
    api->widgets.set_text(api->widgets.context,state->label,text);
}
static void widget_demo_binding(struct ToriRS_Api* api, void* user, struct ToriRS_WidgetEvent const* event)
{
    struct WidgetDemoState* state=user;
    struct ToriRS_WidgetApi* ui=&api->widgets;
    if( strcmp(event->role,"viewport")==0 )
    {
        if( event->type==TORIRS_WIDGET_UNBOUND )
        {
            if( state->label.opaque[2] ) ui->remove(ui->context,state->label);
            state->label=(struct ToriRS_WidgetRef){0};
            return;
        }
        if( ui->create_text(ui->context,event->widget,"strength",&state->label)!=TORIRS_CONTRACT_OK ) return;
        ui->set_position(ui->context,state->label,12,40);
        ui->set_text_color(ui->context,state->label,0xffffff);
        widget_demo_update(api,user,NULL);
        ui->revalidate(ui->context,state->label);
        return;
    }
    if( event->type==TORIRS_WIDGET_UNBOUND )
    {
        ui->reset(ui->context,event->widget);
        api->core.log(api,"WIDGET_DEMO_UNBOUND");
        return;
    }
    struct ToriRS_WidgetBounds before,after;
    if( ui->position(ui->context,event->widget,&before)!=TORIRS_CONTRACT_OK ) return;
    if( ui->set_position(ui->context,event->widget,before.x-12,before.y)!=TORIRS_CONTRACT_OK ) return;
    ui->revalidate(ui->context,event->widget);
    ui->position(ui->context,event->widget,&after);
    api->core.log(api,"WIDGET_DEMO api=%u before=%d,%d %dx%d after=%d,%d %dx%d",
        api->major_version,before.x,before.y,before.width,before.height,
        after.x,after.y,after.width,after.height);
}
static void widget_demo_start(struct ToriRS_Api* api, void* user)
{
    api->widgets.watch(api->widgets.context,"sidebar",widget_demo_binding,user);
    api->widgets.watch(api->widgets.context,"viewport",widget_demo_binding,user);
}
struct ToriRS_PluginDef const TORIRS_PLUGIN_WIDGET_DEMO={
    .struct_size=sizeof(struct ToriRS_PluginDef),
    .id="widget-demo",.title="Widget API Probe",.version="1",
    .state_size=sizeof(struct WidgetDemoState),
    .callbacks={.struct_size=sizeof(struct ToriRS_PluginCallbacks),.on_start=widget_demo_start,
                .on_server_tick=widget_demo_update},
};
