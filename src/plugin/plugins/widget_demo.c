/*
 * Native widget probe, registered only with TORIRS_WIDGET_DEMO.
 *
 * NOT PORTED TO PORCELAIN, AND NOT AN OVERSIGHT.
 *
 * This file is the raw widget contract's control. It is the only thing in the
 * tree that calls actions/invoke on a RETAINED action ref after the widget it
 * came from has gone, that arms the same owned control twice to watch the
 * first registration's menu rows die, that reads a native box, writes it back
 * twelve pixels left and reads it AGAIN, and that walks a live CS2 stack by
 * index inside the callback. Those calls are the demonstration; a port would
 * replace every one of them with a description and delete the thing the file
 * exists to show.
 *
 * Worse than useless, it would be misleading. Run a probe of the raw contract
 * THROUGH a reconciler and it stops measuring the contract: every assertion
 * the matrix makes about it (--widget-offset 12 --widget-moves 1,
 * owned_widget_armed, WIDGET_DEMO_OLD_ACTION result=) would then be evidence
 * about Porcelain rather than about the host underneath it, with Porcelain as
 * both the subject and the instrument. The -12 nudge is the clearest case: the
 * layer's first rule is that an unchanged description costs no setter, so the
 * second nudge would correctly be a no-op and the probe would be measuring the
 * hash compare it was written to see past.
 *
 * Two of its rows are invariants the LAYER has to preserve, which is the other
 * half of the reason: "retained action refs expire" and "widget requests
 * inside an op callback are allowed" are facts about the host gate that
 * Porcelain inherits unchanged and cannot restate.
 *
 * The Porcelain-side demonstrations are their own files and always were --
 * script/plugins/_porcelainprobe.lua, _porcelainframeprobe.lua,
 * _r3placeprobe.lua. If a third is wanted, it is a new sibling, never a
 * conversion of this one.
 */
#include "plugin/torirs_plugin_api.h"
#include <stdio.h>
#include <string.h>

/*
 * WHICH SURFACE THE -12 NUDGE IS WRITTEN ON, AND WHY IT IS NOT `sidebar`.
 *
 * The probe's third row is "read a native box, write it back twelve pixels
 * left, read it AGAIN" -- and a readback that agrees with itself is only half
 * the assertion. The other half is that the PAINTER honoured it, because a
 * write the painter drops reads back exactly like one it took.
 *
 * `sidebar` is the frame slot, and the node a frame slot answers with is its
 * side-modal REGION on both lanes: `slot=side_modal` on the 2004 profile, the
 * `sidemodal` box (clientCode 1354, `[role:frame_sidebar]`) on a cache one.
 * That region is where an interface opens OVER the tabs, so unless a side
 * modal is up it is an empty container -- nothing is parented to it and moving
 * it moves no pixels. On the CS1 lane that is exactly what happened: the
 * readback said 553 -> 541 and the fourteen tab mounts, which are its
 * SIBLINGS, stayed at 553 with the inventory drawn in one of them.
 *
 * So the nudge is written on the panel the lane actually draws. Every profile
 * declares `[role:panel_<name>]` for its side panels -- `slot(sidebar, 3)` on
 * the dat1 lane, `id(if(149, 0))` on the dat2 one -- which is one role name,
 * one lookup and no number compiled into C. The tab this resolves to is the
 * revision's own answer to "where does the inventory live", and a lane that
 * declares no such panel reports UNAVAILABLE and the probe simply never binds.
 */
#define PANEL_ROLE "panel_inventory"

struct WidgetDemoState {
    struct ToriRS_WidgetRef label, control, public_button;
    struct ToriRS_WidgetActionRef retained_action;
    int level;
    bool action_done;
};
static struct ToriRS_ConfigItem const WIDGET_CONFIG[]={
    {"public_friends",TORIRS_CONFIG_BOOL,"Set public chat to Friends on start","0",0,0,NULL,0},
    /* Re-arm the owned control with a new label. Retained menu rows built for
     * the earlier registration must then die instead of firing. */
    {"rearm",TORIRS_CONFIG_BOOL,"Replace the owned control's operation","0",0,0,NULL,0},
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
        /*
         * The two labels sit down the viewport's MIDDLE, not at its top.
         *
         * At the old 12,40 they were buried on toplevel 601: that root hangs
         * its chat drawer over the top-left of the world, so of "Public:
         * Friends" only the last ~15 px survived and "Strength: 99" was a
         * fragment of the "99" -- 56 of the label's 71 cyan pixels overpainted.
         * The other four lanes were fine, which is exactly why it shipped.
         *
         * Half the viewport's own height and not a second magic number: the
         * drawer is pinned to the top on the root that has one, and the middle
         * of the world is clear on every lane this plugin runs on. A fixed
         * offset that cleared 601 would be a number chosen for one root, which
         * is the per-lane rule this project does not allow.
         */
        struct ToriRS_WidgetBounds view={0};
        int label_y=40;

        if( ui->bounds(ui->context,event->widget,&view)==TORIRS_CONTRACT_OK && view.height>64 )
            label_y=view.height/2;
        if( ui->create_text(ui->context,event->widget,"strength",&state->label)!=TORIRS_CONTRACT_OK ) return;
        state->level=-1;
        ui->set_position(ui->context,state->label,12,label_y);
        ui->set_text_color(ui->context,state->label,0xffffff);
        widget_demo_update(api,user,NULL);
        ui->revalidate(ui->context,state->label);
        if( ui->create_text(ui->context,event->widget,"public",&state->control)!=TORIRS_CONTRACT_OK ) return;
        ui->set_position(ui->context,state->control,12,label_y+16);
        ui->set_text(ui->context,state->control,"Public: Friends");
        ui->set_text_color(ui->context,state->control,0x00ffff);
        enum ToriRS_ContractResult armed=ui->set_on_op(ui->context,state->control,1,"Set public chat to friends",widget_demo_operation,user);
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
static void widget_demo_config(struct ToriRS_Api* api,void* user,char const* key)
{
    struct WidgetDemoState* state=user;
    struct ToriRS_WidgetApi* ui=&api->widgets;
    bool rearm=false;
    if( strcmp(key,"rearm")!=0 || !state->control.opaque[2] ) return;
    api->config.get_bool(api,"rearm",&rearm);
    if( !rearm ) return;
    enum ToriRS_ContractResult result=ui->set_on_op(ui->context,state->control,1,"Re-armed: set public chat to friends",widget_demo_operation,user);
    api->core.log(api,"WIDGET_DEMO_REARM result=%d",result);
}
static void widget_demo_start(struct ToriRS_Api* api, void* user)
{
    *(struct WidgetDemoState*)user=(struct WidgetDemoState){.level=-1};
    api->core.log(api,"WIDGET_DEMO_SCRIPT_CALLBACKS available=%d",api->scripts.available(api->scripts.context));
    api->scripts.invalidate(api->scripts.context,"groundItemCaption");
    api->widgets.watch(api->widgets.context,"public_chat_button",widget_demo_binding,user);
    api->widgets.watch(api->widgets.context,PANEL_ROLE,widget_demo_binding,user);
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
                .on_logic_tick=widget_demo_update,.on_script_callback=widget_demo_script,.on_stop=widget_demo_stop,
                .on_config_changed=widget_demo_config},
};
