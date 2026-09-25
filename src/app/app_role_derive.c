/*
 * quest-driver: derived revconfig roles.
 *
 * Owner: core-revconfig (docs/ARCHITECT.md).
 *
 * A `derive=<fact>` role carries no match expression. The engine answers it at
 * the publication fence through UITreeRoleTable.fallback -- a hook that has
 * existed and been unset since the role table was written -- which is
 * consulted BEFORE the matcher chain, so a derived role never runs one.
 *
 * Why a fallback rather than more match grammar: the four facts below are not
 * properties of a component id at all. "The live node under chat_modal_host
 * whose effective IF_SETEVENTS carries RS_MINIMENU_EVENT_CLICK" is a question
 * about the running tree, and it is the SAME question on both lanes, which is
 * exactly what makes it the thing a lane-agnostic driver should name. Writing
 * it as ids would mean one match line per dialogue template per revision, and
 * every one of them would be a line that silently stops matching.
 *
 * Facts this function knows (keep this list, `tools/revconfig_roles_from_pack.py`'s
 * KNOWN_DERIVE_FACTS and `make -C src check-revconfig-roles` all in step --
 * the checker fails a derive= fact this file does not answer for):
 *
 *   dialog_continue    the armed continue row under chat_modal_host
 *   pause_pending      UITree_PausePendingIndex(tree)
 *
 * Two more are documented here because they are the same design and will
 * land the same way, but are OUT OF SCOPE for this pass (the osrs239 dat2
 * lane, docs/QUEST_DRIVER_PLAN.md step 6) and deliberately answered "unknown"
 * below -- a future rs289lc-lane pass (step 12) adds them:
 *
 *   chat_modal_host    app->slots.chat_com_id (dat1; the cache lane binds this
 *                      role by match expression instead)
 *   button_type(N)     the dat1 spelling of dialog_continue: buttontype=pause
 *                      baked into the .if, which if_button_action_for_type
 *                      already maps to RESUME_PAUSEBUTTON
 *
 * The install point (App_RoleDeriveInstall, below) still needs a call from
 * app boot, after app->ui_roles is loaded (src/app.c, App_New) -- that file
 * is closed to this pass (docs/ARCHITECT.md); see the report this pass filed.
 */

#include "app.h"

#include "game/rs_minimenu_build.h" /* RS_MINIMENU_EVENT_CLICK */
#include "plugin/torirs_plugin_drive.h"

#include <assert.h>
#include <string.h>

/*
 * `dialog_continue`: the live node under chat_modal_host whose effective
 * IF_SETEVENTS carries RS_MINIMENU_EVENT_CLICK -- the same test
 * add_component_rows (rs_minimenu_build.c:1073) makes per row, through
 * UIIfEventTable_Effective (App_IfEventsGetEffective). Every dialogue
 * template's prompt is a different depth under chat_modal_host and none of it
 * is an authored role, so this walks the whole live subtree rather than
 * assuming a fixed depth, returning the first (lowest, depth-first) match.
 *
 * Recursion is bounded by the tree's own depth, which a chat page never comes
 * close to challenging.
 */
static int32_t
role_derive_find_armed_continue(struct App const* app, int32_t parent)
{
    struct UITree const* tree;

    assert(app);
    tree = app->tree;
    assert(tree);

    if( parent < 0 || (uint32_t)parent >= tree->component_count )
        return -1;

    for( int32_t child = tree->components[parent].first_child; child >= 0;
         child = tree->components[child].next_sibling )
    {
        int32_t found;

        if( !tree->components[child].freed &&
            (App_IfEventsGetEffective(app, tree->components[child].component_id) &
             RS_MINIMENU_EVENT_CLICK) )
            return child;

        found = role_derive_find_armed_continue(app, child);
        if( found >= 0 )
            return found;
    }
    return -1;
}

int
App_RoleDeriveFallback(void* user, char const* fact, int argument, int* out_component_id)
{
    struct App* app;

    assert(user);
    assert(fact);
    assert(out_component_id);
    (void)argument;

    app = (struct App*)user;

    /*
     * Both answers below are tree INDICES, matching UITree_RoleNode's own
     * contract (@see UITree_FrameSlotNode, UITree_FindByComponentId): -1 is a
     * legitimate "not right now", not a fault, so this always returns 1 once
     * `fact` is recognised -- the caller's -2 ("not a derive= role, run the
     * matcher chain") is reserved for a fact this function does not know.
     */

    if( strcmp(fact, "pause_pending") == 0 )
    {
        *out_component_id = UITree_PausePendingIndex(app->tree);
        return 1;
    }

    if( strcmp(fact, "dialog_continue") == 0 )
    {
        int32_t host = UITree_RoleNodeByName(app->tree, &app->ui_roles, "chat_modal_host");
        *out_component_id = host >= 0 ? role_derive_find_armed_continue(app, host) : -1;
        return 1;
    }

    /* button_type(N) and chat_modal_host (dat1) are the rs289lc lane's
     * derived facts (docs/ARCHITECT.md step 12, out of this pass's scope) --
     * left unknown on purpose: `make -C src check-revconfig-roles` must fail
     * loudly if either name ever reaches an osrs239 ini before this function
     * answers for it. */
    return 0;
}

/*
 * The trampoline installed as UITreeRoleTable.fallback: translates the
 * (tree, table, role_id, user) shape UITree_RoleNode calls into the
 * (fact, argument) shape App_RoleDeriveFallback answers, and translates -2
 * ("decline, this role has no derive= fact -- or the fact answered it does
 * not know") back the other way.
 */
static int32_t
role_derive_trampoline(
    struct UITree const* tree,
    struct UITreeRoleTable const* table,
    uint16_t role_id,
    void* user)
{
    char const* fact;
    int argument;
    int component_id;

    assert(tree);
    assert(table);
    (void)tree; /* App_RoleDeriveFallback reaches the tree through `user`. */

    fact = UITree_RoleDeriveFact(table, role_id, &argument);
    if( !fact )
        return -2;

    if( !App_RoleDeriveFallback(user, fact, argument, &component_id) )
        return -2;

    return (int32_t)component_id;
}

void
App_RoleDeriveInstall(struct App* app)
{
    assert(app);
    app->ui_roles.fallback = role_derive_trampoline;
    app->ui_roles.fallback_user = app;
}
