/*
 * Server IF_SETEVENTS and the target-selection mask, as the minimenu and the
 * CS2 host read them.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Adapter so rs_minimenu_build can ask about server-declared events without
 * knowing what an App is. Uses the node-aware lookup so a dynamic child
 * inherits its parent's IF_SETEVENTS range (popout:buttons, bank items, …). */
int
app_minimenu_events_for_component(
    void* user,
    int com_id,
    int sub_id)
{
    if( sub_id >= 0 )
        return App_IfEventsGetAt((struct App const*)user, com_id, sub_id);
    return (int)App_IfEventsGetEffective((struct App const*)user, com_id);
}

/* The CS2 host's twin of the above, for IF/CC_GETTARGETMASK. It reports
 * *presence* rather than an effective value because that is what the reference
 * split needs (deob method12093): where the server declared nothing the widget's
 * own decoded target mask answers, and that one is already normalised per cache
 * generation on the node — shifting a dat1 mask like a dat2 events word would
 * turn a real answer into noise. */
int
app_cs2_events_override_for_component(
    void* user,
    int com_id,
    int* out_events)
{
    unsigned events = 0;
    if( !UIIfEventTable_Lookup(&((struct App const*)user)->if_events, com_id, -1, &events) )
        return 0;
    if( out_events )
        *out_events = (int)events;
    return 1;
}

void
App_IfEventsSet(
    struct App* app,
    int com_id,
    int from,
    int to,
    int events)
{
    assert(app);
    UIIfEventTable_Set(&app->if_events, com_id, from, to, events);

    if( torirs_env_net_debug() )
        TORIRS_LOG(
            "if_setevents: com=%d (%d:%d) slots=%d..%d events=0x%x\n",
            com_id,
            (com_id >> 16) & 0xffff,
            com_id & 0xffff,
            from,
            to,
            events);
    app->need_redraw = 1;
}

void
App_IfEventsClear(struct App* app)
{
    assert(app);
    UIIfEventTable_Clear(&app->if_events);
}

int
App_IfEventsGet(
    struct App const* app,
    int com_id)
{
    return App_IfEventsGetAt(app, com_id, -1);
}

int
App_IfEventsGetAt(
    struct App const* app,
    int com_id,
    int sub_id)
{
    assert(app);
    return UIIfEventTable_At(&app->if_events, com_id, sub_id);
}

unsigned
App_IfEventsGetEffective(
    struct App const* app,
    int com_id)
{
    assert(app);
    return UIIfEventTable_Effective(&app->if_events, app->tree, com_id);
}

/*
 * Which target kinds a component actually offers — deob `method12079`'s
 * `method7577(method12093(this, widget))`, the same pair `rs_cs2_target_mask`
 * keeps for IF/CC_GETTARGETMASK and `component_effective_target_mask`
 * (rs_minimenu_build.c) uses to decide whether to BUILD the target row.
 *
 * The arm has to agree with the builder or the row and the arm disagree about
 * what the click meant: reading only the widget's decoded `target_mask` armed
 * a script-built button with mask 0, and `add_world_select_row` then refused
 * every world row while target mode was active — a menu with the verb on it
 * and nothing the verb could be used on. A `cc_create`d child is memset to
 * zero and no `cc_` opcode can write a target mask, so its whole declaration
 * is the server's `if_setevents` (sailing's crew panel: `^if_event_op_all +
 * 16384`, bit 14 -> mask 0x8 PLAYER).
 *
 * The shift is IF3-only, exactly as in the two functions above: a dat1
 * component stores `targetMask` unshifted in `click_mask`, so shifting an IF1
 * events word by 11 would turn a real answer into noise.
 */
int
app_component_target_mask(
    struct App const* app,
    int com_id)
{
    int32_t idx;
    struct UITreeComponent const* node;
    int mask;

    assert(app);
    if( !app->tree )
        return 0;
    idx = UITree_FindByComponentId(app->tree, com_id);
    if( idx < 0 )
        return 0;
    node = &app->tree->components[idx];
    mask = (int)node->behavior.target_mask;
    if( node->if3 )
        mask |= (int)((App_IfEventsGetEffective(app, com_id) >> TORIRS_TARGET_MASK_IF3_SHIFT) &
                      TORIRS_TARGET_MASK_IF3_BITS);
    return mask;
}

/*
 * The identity the SERVER knows the armed component by — the one that goes on
 * the wire in OPPLAYERT/OPNPCT/OPLOCT/OPOBJT/OPHELDT.
 *
 * `targetsel.component_id` is the TREE id, because that is what the local
 * target-enter/leave hooks are dispatched against. For a dynamic child that id
 * is a runtime allocation the server has never heard of, so putting it on the
 * wire meant `[opplayert,sailing_sidepanel:crew_content_clicklayer]` could
 * never match and the armed click did nothing at all. `app_if_button_target`
 * is the same (container, index-within-it) resolution IF_BUTTON already does,
 * and it is a no-op for a static spellbook button.
 *
 * Resolved here rather than stored at arm time because the armed component's
 * parent is what the packet needs and `targetsel` has no wire-id field; the
 * tree cannot have moved the child to a different container between the arm
 * and the click without destroying it, and a destroyed child resolves to
 * itself, which is what an unarmed cast already sends.
 */
int
app_targetsel_wire_component(struct App const* app)
{
    int com;
    int sub;

    assert(app);
    UIIfEventTable_ButtonTarget(app->tree, app->targetsel.component_id, &com, &sub);
    return com;
}
