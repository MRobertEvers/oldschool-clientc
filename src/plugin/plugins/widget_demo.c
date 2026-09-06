/* Native widget probe, registered only with TORIRS_WIDGET_DEMO. */
#include "plugin/torirs_plugin_api.h"
#include <stdio.h>
#include <string.h>

struct WidgetDemoState {
    struct ToriRS_WidgetRef label, control, public_button;
    struct ToriRS_WidgetActionRef retained_action;
    int level;
    bool action_done;
};
static struct ToriRS_ConfigItem const WIDGET_CONFIG[]={
    {"public_friends",TORIRS_CONFIG_BOOL,"Set public chat to Friends on start","0",0,0,NULL,0},
    {NULL},
};
static struct ToriRS_ConfigSchema const WIDGET_SCHEMA={sizeof(WIDGET_SCHEMA),WIDGET_CONFIG};

/* Invoke the native public-chat "Friends" operation through the checked action
 * API. Returns UNAVAILABLE when the native button or its row is not present. */
static enum ToriRS_ContractResult widget_demo_public_friends(struct ToriRS_Api* api,struct WidgetDemoState* state,char const* why)
{
    struct ToriRS_WidgetAction actions[16];size_t count=0;
    struct ToriRS_WidgetApi* ui=&api->widgets;
    if( !state->public_button.opaque[2] ) return TORIRS_CONTRACT_UNAVAILABLE;
    enum ToriRS_ContractResult result=ui->actions(ui->context,state->public_button,actions,16,&count);
    if( result!=TORIRS_CONTRACT_OK ) return result;
    for( size_t i=0;i<count;++i )
        if( strstr(actions[i].label,"Friends") || strstr(actions[i].label,"friends") )
        {
            state->retained_action=actions[i].ref;
            result=ui->invoke(ui->context,actions[i].ref);
            api->core.log(api,"WIDGET_DEMO_ACTION label=%s result=%d via=%s",actions[i].label,result,why);
            return result;
        }
    return TORIRS_CONTRACT_UNAVAILABLE;
}
static void widget_demo_action(struct ToriRS_Api* api,struct WidgetDemoState* state)
{
    bool enabled=false;
    api->config.get_bool(api,"public_friends",&enabled);
    if( !enabled || state->action_done ) return;
    if( widget_demo_public_friends(api,state,"config")==TORIRS_CONTRACT_OK ) state->action_done=true;
}
/* The owned control's single operation: a native operation on a native widget,
 * reached through the ordinary hit test and menu dispatch. */
static void widget_demo_operation(struct ToriRS_Api* api,void* user,struct ToriRS_WidgetEvent const* event)
{
    struct WidgetDemoState* state=user;
    struct ToriRS_WidgetApi* ui=&api->widgets;
    enum ToriRS_ContractResult result=widget_demo_public_friends(api,state,"control");
    api->core.log(api,"WIDGET_DEMO_OP result=%d registration=%llu",result,(unsigned long long)event->native_revision);
    if( result==TORIRS_CONTRACT_OK ) ui->set_text(ui->context,event->widget,"Public: set");
}

static void widget_demo_update(struct ToriRS_Api* api, void* user, struct ToriRS_TickEvent const* tick)
{
    struct WidgetDemoState* state=user;
    struct ToriRS_SkillSnapshot skill={.struct_size=sizeof(skill)};
    char text[64];
    (void)tick;
    widget_demo_action(api,state);
    if( !state->label.opaque[2] || !api->game || !api->game->skill(api,2,&skill) ) return;
    if( state->level==skill.current_level ) return;
    state->level=skill.current_level;
    snprintf(text,sizeof(text),"Strength: %d",skill.current_level);
    api->widgets.set_text(api->widgets.context,state->label,text);
}
static void widget_demo_binding(struct ToriRS_Api* api, void* user, struct ToriRS_WidgetEvent const* event)
{
    struct WidgetDemoState* state=user;
    struct ToriRS_WidgetApi* ui=&api->widgets;
    if( strcmp(event->role,"public_chat_button")==0 )
    {
        state->public_button=event->type==TORIRS_WIDGET_BOUND ? event->widget : (struct ToriRS_WidgetRef){0};
        if( event->type==TORIRS_WIDGET_UNBOUND && state->retained_action.operation )
            api->core.log(api,"WIDGET_DEMO_OLD_ACTION result=%d",ui->invoke(ui->context,state->retained_action));
        return;
    }
    if( strcmp(event->role,"viewport")==0 )
    {
        if( event->type==TORIRS_WIDGET_UNBOUND )
        {
            if( state->label.opaque[2] ) ui->remove(ui->context,state->label);
            if( state->control.opaque[2] ) ui->remove(ui->context,state->control);
            state->label=(struct ToriRS_WidgetRef){0};
            state->control=(struct ToriRS_WidgetRef){0};
            return;
        }
        if( ui->create_text(ui->context,event->widget,"strength",&state->label)!=TORIRS_CONTRACT_OK ) return;
        state->level=-1;
        ui->set_position(ui->context,state->label,12,40);
        ui->set_text_color(ui->context,state->label,0xffffff);
        widget_demo_update(api,user,NULL);
        ui->revalidate(ui->context,state->label);
        if( ui->create_text(ui->context,event->widget,"public",&state->control)!=TORIRS_CONTRACT_OK ) return;
        ui->set_position(ui->context,state->control,12,56);
        ui->set_text(ui->context,state->control,"Public: Friends");
        ui->set_text_color(ui->context,state->control,0x00ffff);
        enum ToriRS_ContractResult armed=ui->set_on_op(ui->context,state->control,"Set public chat to friends",widget_demo_operation,user);
        ui->revalidate(ui->context,state->control);
        struct ToriRS_WidgetBounds box={0};
        ui->bounds(ui->context,state->control,&box);
        api->core.log(api,"WIDGET_DEMO_OP_BOUNDS armed=%d x=%d y=%d w=%d h=%d",armed,box.x,box.y,box.width,box.height);
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
    *(struct WidgetDemoState*)user=(struct WidgetDemoState){.level=-1};
    api->core.log(api,"WIDGET_DEMO_SCRIPT_CALLBACKS available=%d",api->scripts.available(api->scripts.context));
    api->scripts.invalidate(api->scripts.context,"groundItemCaption");
    api->widgets.watch(api->widgets.context,"public_chat_button",widget_demo_binding,user);
    api->widgets.watch(api->widgets.context,"sidebar",widget_demo_binding,user);
    api->widgets.watch(api->widgets.context,"viewport",widget_demo_binding,user);
}
static void widget_demo_script(struct ToriRS_Api* api,void* user,struct ToriRS_ScriptEvent const* event)
{
    (void)user;
    if( strcmp(event->name,"groundItemCaption")!=0 ) return;
    int32_t id,count;
    if( api->scripts.get_int(api->scripts.context,event->ref,9,&id)!=TORIRS_CONTRACT_OK ||
        api->scripts.get_int(api->scripts.context,event->ref,8,&count)!=TORIRS_CONTRACT_OK ) return;
    char text[64];snprintf(text,sizeof(text),"C item %d x%d",id,count);
    api->scripts.set_string(api->scripts.context,event->ref,0,text);
    api->scripts.set_int(api->scripts.context,event->ref,6,0x00ffff);
}
static void widget_demo_stop(struct ToriRS_Api* api,void* user)
{ (void)user;api->scripts.invalidate(api->scripts.context,"groundItemCaption"); }
struct ToriRS_PluginDef const TORIRS_PLUGIN_WIDGET_DEMO={
    .struct_size=sizeof(struct ToriRS_PluginDef),
    .id="widget-demo",.title="Widget API Probe",.version="1",
    .state_size=sizeof(struct WidgetDemoState),.config=&WIDGET_SCHEMA,
    .callbacks={.struct_size=sizeof(struct ToriRS_PluginCallbacks),.on_start=widget_demo_start,
                .on_logic_tick=widget_demo_update,.on_script_callback=widget_demo_script,.on_stop=widget_demo_stop},
};
