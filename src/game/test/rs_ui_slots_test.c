/*
 * Contract test for the runtime interface-slot API (RS_UISlots open/close and
 * SetTab) against a real dat1 cache + rs245_2lc RevConfig build — the surface
 * the network exec layer drives when IF_OPENMAIN/IF_OPENSIDE/IF_SETTAB arrive.
 */
#include "app.h"
#include "game/rs_if1_buttons.h"
#include "game/rs_ui_slots.h"
#include "revconfig/revconfig.h"
#include "ui/uitree.h"
#include "ui/uitree_role.h"
#include "varp/varp_manager.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define TEST_CHECK(cond)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__);                     \
            abort();                                                                               \
        }                                                                                          \
    } while( 0 )

static int
node_child_count(
    struct UITree const* tree,
    int32_t idx)
{
    int count = 0;
    if( idx < 0 )
        return -1;
    for( int32_t child = tree->components[idx].first_child; child >= 0;
         child = tree->components[child].next_sibling )
        count++;
    return count;
}

int
main(
    int argc,
    char** argv)
{
    struct App app;
    struct AppConfig cfg = { 0 };

    cfg.cache_dir = argc > 1 ? argv[1] : "../cache254";
    cfg.cache_kind = APP_CACHE_DAT1;
    cfg.config_dir = "../config";
    cfg.script_dir = "../script";
    cfg.interface_id = 84;
    cfg.revconfig_ui_ini =
        argc > 2 ? argv[2] : "../revconfig/rs245_2lc/rs245_2lc_dat1_ui.ini";
    cfg.revconfig_cache_ini =
        argc > 3 ? argv[3] : "../revconfig/rs245_2lc/rs245_2lc_dat1_cache.ini";

    App_Init(&app, &cfg);
    App_OpenRootInterface(&app, cfg.interface_id);
    App_BootWait(&app);

    /* Seeding from the INI: boot tab is 3 (selected=), tab slots carry their
     * componentno defaults, tab 7 is empty, and the slot= mount regions exist. */
    assert(app.slots.side_tab == 3);
    assert(RS_UISlots_TabEnabled(&app.slots, 3));
    assert(!RS_UISlots_TabEnabled(&app.slots, 7));
    assert(app.slots.main_modal_index >= 0);
    assert(app.slots.side_modal_index >= 0);
    assert(app.slots.chat_index >= 0);
    assert(app.slots.main_modal_id == -1 && app.slots.side_modal_id == -1);
    printf("PASS: slots seeded from tree (tab=%d)\n", app.slots.side_tab);

    /* IF_OPENMAIN: bank-style modal mounts a pack under the viewport region. */
    RS_UISlots_OpenMain(&app, 12);
    App_BootWait(&app);
    assert(app.slots.main_modal_id == 12);
    assert(node_child_count(app.tree, app.slots.main_modal_index) > 0);
    printf(
        "PASS: OpenMain(12) mounted %d children\n",
        node_child_count(app.tree, app.slots.main_modal_index));

    /* IF_CLOSE: modal cleared, mount region childless again. */
    RS_UISlots_CloseModal(&app);
    App_BootWait(&app);
    assert(app.slots.main_modal_id == -1);
    assert(node_child_count(app.tree, app.slots.main_modal_index) == 0);
    printf("PASS: CloseModal cleared the mount\n");

    /* IF_SETTAB into the empty slot 7, then clear it with 65535. */
    RS_UISlots_SetTab(&app, 7, 5608);
    App_BootWait(&app);
    assert(RS_UISlots_TabEnabled(&app.slots, 7));
    assert(node_child_count(app.tree, app.slots.side_owner_index[7]) > 0);
    printf("PASS: SetTab(7, 5608) mounted the tab pack\n");
    RS_UISlots_SetTab(&app, 7, 65535);
    App_BootWait(&app);
    assert(!RS_UISlots_TabEnabled(&app.slots, 7));
    assert(node_child_count(app.tree, app.slots.side_owner_index[7]) == 0);
    printf("PASS: SetTab(7, 65535) cleared the tab\n");

    /* IF_OPENSIDE: side modal suppresses the tab subtree via selected-tab -1
     * (host request answered by app_host_request). */
    RS_UISlots_OpenSide(&app, 3917);
    App_BootWait(&app);
    assert(app.slots.side_modal_id == 3917);
    assert(node_child_count(app.tree, app.slots.side_modal_index) > 0);
    {
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_SELECTED_TAB };
        assert(UITree_Host(&app.ui_host, &req) == -1);
    }
    RS_UISlots_CloseModal(&app);
    App_BootWait(&app);
    {
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_SELECTED_TAB };
        assert(UITree_Host(&app.ui_host, &req) == 3);
    }
    printf("PASS: OpenSide/CloseModal toggled tab suppression\n");

    /* IF_OPENCHAT mounts under the chat region and IF_CLOSE clears it. */
    RS_UISlots_OpenChat(&app, 2459);
    App_BootWait(&app);
    assert(app.slots.chat_com_id == 2459);
    assert(node_child_count(app.tree, app.slots.chat_index) > 0);
    RS_UISlots_CloseModal(&app);
    App_BootWait(&app);
    assert(app.slots.chat_com_id == -1);
    printf("PASS: OpenChat/Close cycled the chat dialog\n");

    /* IF1 button engine: a TOGGLE button whose value script reads varp 173
     * flips it (reference TOGGLE_BUTTON); SELECT snaps to the operand. */
    {
        static int toggle_script[] = { 5, 173, 0 };
        static int* scripts[] = { toggle_script };
        static int lengths[] = { 3 };
        static int operands[] = { 7 };
        int32_t idx = app.slots.main_modal_index;
        struct UITreeComponent* node = &app.tree->components[idx];
        int com_id = node->component_id = 0x7ABC;
        app.tree->id_generation++;
        node->behavior.scripts_count = 1;
        node->behavior.scripts = scripts;
        node->behavior.scripts_lengths = lengths;
        node->behavior.comparator_count = 1;
        node->behavior.script_operand = operands;

        assert(VarPManager_GetVarp(&app.varps, 173) == 0);
        TEST_CHECK(RS_IF1_ApplyButtonClick(&app, com_id, REVCONFIG_MINIMENU_IF_BUTTON_TOGGLE));
        assert(VarPManager_GetVarp(&app.varps, 173) == 1);
        TEST_CHECK(RS_IF1_ApplyButtonClick(&app, com_id, REVCONFIG_MINIMENU_IF_BUTTON_TOGGLE));
        assert(VarPManager_GetVarp(&app.varps, 173) == 0);
        TEST_CHECK(RS_IF1_ApplyButtonClick(&app, com_id, REVCONFIG_MINIMENU_IF_BUTTON_SELECT));
        assert(VarPManager_GetVarp(&app.varps, 173) == 7);
        /* Detach the synthetic behavior before shutdown frees the tree. */
        node->behavior.scripts_count = 0;
        node->behavior.scripts = NULL;
        node->behavior.scripts_lengths = NULL;
        node->behavior.comparator_count = 0;
        node->behavior.script_operand = NULL;
        printf("PASS: IF1 toggle/select buttons drive varps\n");
    }

    /*
     * RS_UISlots_TabGiven: the lane-aware "has the server given this tab",
     * which is what a plugin gameframe asks before it draws an icon.
     *
     * This lane is rung 1's: the 2004 frame carries its own fourteen sidebar
     * mounts, so IF_SETTAB is the whole of it -- and tab 7 stands for a clan
     * chat LostCity never sends, which is the same answer for a different
     * reason.
     */
    TEST_CHECK(RS_UISlots_TabGiven(&app, 3));
    TEST_CHECK(!RS_UISlots_TabGiven(&app, 7));
    TEST_CHECK(!RS_UISlots_TabGiven(&app, -1));
    /* Past the frame's own tabs: no mount, and no profile has named it, so
     * nothing says it is hidden. A frame drawing no icons at all would be a
     * worse wrong than one drawing an icon too many. */
    TEST_CHECK(RS_UISlots_TabGiven(&app, RS_UI_SLOTS_TAB_MAX - 1));
    printf("PASS: TabGiven reads IF_SETTAB on a frame that owns its mounts\n");

    /*
     * Rung 2, on a lane that does not need it: a `sidetab_<n>` role the
     * profile stated is consulted as WELL as rung 1, and either saying hidden
     * is the answer.
     *
     * Built here rather than read from a profile because no bootable lane
     * declares both channels -- osrs239 states the roles and has no sidebar
     * mounts, this one is the other way round -- and the rule that matters is
     * exactly what happens when both speak.
     */
    RS_UISlots_SetTab(&app, 7, 5608);
    App_BootWait(&app);
    {
        /* A dat1 uid is flat -- the pack's root IS 5608, where a dat2 one
         * would be (group << 16) | child. */
        int const packroot = 5608;
        uint16_t const role = UITree_RoleIntern(&app.ui_roles, "sidetab_7");
        struct UITreeRoleMatcher m = { 0 };

        m.kind = UITREE_ROLE_MATCH_ID;
        m.uid = packroot;
        m.member = -1;
        TEST_CHECK(role != 0);
        TEST_CHECK(UITree_RoleAddMatcher(&app.ui_roles, role, &m));
        TEST_CHECK(UITree_RoleNode(app.tree, &app.ui_roles, role) >= 0);
        TEST_CHECK(RS_UISlots_TabGiven(&app, 7));

        /* The semantic hide: what a CS2 IF_SETHIDE on the tab's own node does,
         * applied through the same call the opcode handler makes. */
        TEST_CHECK(UITree_ApplyHide(app.tree, packroot, 1));
        TEST_CHECK(!RS_UISlots_TabGiven(&app, 7));
        TEST_CHECK(RS_UISlots_TabEnabled(&app.slots, 7));
        printf("PASS: TabGiven honours a hidden sidetab_<n> role\n");

        TEST_CHECK(UITree_ApplyHide(app.tree, packroot, 0));
        TEST_CHECK(RS_UISlots_TabGiven(&app, 7));
        RS_UISlots_SetTab(&app, 7, 65535);
        App_BootWait(&app);
    }

    /*
     * And a `sidetab_<n>` that does not resolve is a tab that is not there.
     *
     * The cache-gameframe spelling: a sub-interface the server closed is out of
     * the tree entirely rather than hidden in it, so the role has nothing to
     * find. Stated against a uid nothing will ever mount, because what is under
     * test is the ANSWER to an unresolved chain -- an unmount arranged here
     * would be testing the unmount.
     *
     * Tab 5 is one this frame has and IF_SETTAB has given, so rung 1 says yes:
     * this is also the proof that either authority saying no is enough.
     */
    {
        uint16_t const role = UITree_RoleIntern(&app.ui_roles, "sidetab_5");
        struct UITreeRoleMatcher m = { 0 };

        m.kind = UITREE_ROLE_MATCH_ID;
        m.uid = 0x7FFE0001; /* no lane mounts into the reserved band */
        m.member = -1;
        TEST_CHECK(role != 0);
        TEST_CHECK(UITree_RoleAddMatcher(&app.ui_roles, role, &m));
        TEST_CHECK(UITree_RoleNode(app.tree, &app.ui_roles, role) < 0);
        TEST_CHECK(RS_UISlots_TabEnabled(&app.slots, 5));
        TEST_CHECK(!RS_UISlots_TabGiven(&app, 5));
        printf("PASS: TabGiven reads an unresolved sidetab_<n> as taken away\n");
    }

    App_Shutdown(&app);
    printf("ALL PASS\n");
    return 0;
}
