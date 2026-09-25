/*
 * The mouseover ENTRY has one source: the menu's acting row.
 *
 * `_7100` (MINIMENU_TYPE) and the four FIND ops ask what the pointer is on;
 * `_7101` (MINIMENU_ENTRY), `_7110` (NUMOPS) and `_7109` (FINDCOMPONENT) ask
 * about the row that names it. They are four questions about ONE entry and the
 * reference answers all of them off the entry the menu would act on.
 *
 * This client answered them off two different things. The text came from the
 * scratch menu `app_hover_text_update` composes; the SUBJECT came from
 * `world_pickset` in the logic tick -- the nearest non-terrain hit of the last
 * render, which is a different entry whenever the priority sort moves a row or
 * the menu declines to build one. Measured on the Lumbridge fixture at
 * 330,120: the hover line read "Talk-to Romeo" and the pickset's first hit was
 * loc 7143 "Fountain", so clientscript 5350 -- the cache's own "Highlight
 * entities on mouse-over" -- put the FOUNTAIN in its highlight group and
 * `nxt-highlight` drew a cyan hull round a fountain the pointer was not on.
 *
 * The fixture reproduces that shape without a cache: a `multinpc_hidden` npc
 * ahead of a visible one in the pick set. The hidden npc is in the pool and
 * `World_NpcGetByElementId` answers for it, so the old walk published it; the
 * menu builds no row for it at all, so no row is about it. The check that
 * makes the rest of the test mean anything is the first one -- that the two
 * sources really do disagree here.
 */

#include "app.h"
#include "app/app_internal.h"
#include "game/rs_clientop.h"
#include "game/rs_minimenu_build.h"
#include "game/rs_minimenu_world.h"
#include "test_harness.h"
#include "ui/uitree_minimenu.h"
#include "world.h"
#include "world_pickset.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_failures;

#define NPC_HIDDEN_ELEMENT 101
#define NPC_VISIBLE_ELEMENT 102
#define NPC_VISIBLE_ID 5037

static struct UIMinimenuOption const*
menu_acting_row(struct UIMinimenu const* menu)
{
    TEST_ASSERT(menu->option_count > 0, "the menu has rows to act on");
    return &menu->options[menu->option_count - 1];
}

static void
test_entry_subject_is_the_acting_row(void)
{
    struct WorldEntityFacet_IdleAnimations idle = World_TestDefaultIdle();
    struct World* world = World_TestMakeReady(104);
    struct World_PickSet picks;
    struct UIMinimenu menu;
    struct UIMinimenuOption const* acting;
    struct RS_ClientOpContext subject;
    struct App* app;
    int hidden_index;
    int visible_index;

    printf("TEST: the mouseover entry's subject is the acting row's\n");

    world->local_pid = 7;

    hidden_index = World_NpcSpawn(world, NPC_HIDDEN_ELEMENT, 500, 0, 25, 25, 1, idle);
    {
        struct WorldEntity_NPC* hidden = World_EntityPoolGet(&world->entities.npc, hidden_index);
        hidden->multinpc_hidden = true;
        hidden->server_slot = 11;
        snprintf(hidden->name, sizeof(hidden->name), "QuestGhost");
        snprintf(hidden->actions[0].name, sizeof(hidden->actions[0].name), "Talk-to");
    }

    visible_index = World_NpcSpawn(world, NPC_VISIBLE_ELEMENT, NPC_VISIBLE_ID, 0, 25, 25, 1, idle);
    {
        struct WorldEntity_NPC* visible = World_EntityPoolGet(&world->entities.npc, visible_index);
        visible->server_slot = 5;
        snprintf(visible->name, sizeof(visible->name), "Romeo");
        snprintf(visible->actions[0].name, sizeof(visible->actions[0].name), "Talk-to");
    }

    /* The hidden npc is nearer, so it is the pick set's own answer to "what is
     * under the pointer" -- and no row is about it. */
    World_PickSetReset(&picks);
    World_PickSetAdd(&picks, NPC_HIDDEN_ELEMENT, WORLD_PICK_NPC, 25, 25, 0, 0);
    World_PickSetAdd(&picks, NPC_VISIBLE_ELEMENT, WORLD_PICK_NPC, 25, 25, 0, 0);

    {
        char player_ops[5][40];
        int player_ops_primary[5] = { 0 };
        struct RS_MinimenuBuildCtx ctx = {
            .selection = { .mode = RS_MINIMENU_SELECT_NONE },
            .player_ops = (char const(*)[40])player_ops,
            .player_ops_primary = player_ops_primary,
            .world = world,
            .world_pickset = &picks,
            .click_in_world = true,
        };

        memset(player_ops, 0, sizeof(player_ops));
        UIMinimenu_Reset(&menu);
        RS_Minimenu_AddWorldRows(&ctx, &menu);
        UIMinimenu_SortPriorityActions(&menu);
    }

    acting = menu_acting_row(&menu);

    /* The premise. Without this the rest of the test passes for the wrong
     * reason: if the two sources agreed here, publishing from either would
     * look correct and the regression would sail through. */
    TEST_ASSERT(
        picks.items[0].element_id == NPC_HIDDEN_ELEMENT,
        "the pick set's first hit is the hidden npc");
    TEST_ASSERT(
        acting->pick.id == NPC_VISIBLE_ELEMENT,
        "and the acting row is about the visible one -- the two sources disagree");
    TEST_ASSERT(
        strstr(acting->text, "Romeo") != NULL,
        "the acting row names Romeo, which is what the hover line draws");

    app = calloc(1, sizeof(*app));
    TEST_ASSERT(app != NULL, "app fixture allocated");
    app->world = world;
    RS_ClientOpReset(&app->host.clientop);

    app_minimenu_entry_publish(app, &menu);

    TEST_ASSERT(
        app->host.clientop.mouseover_type == RS_MINIMENU_TYPE_NPC,
        "_7100 says the pointer is on an npc");
    TEST_ASSERT(
        app->host.clientop.mouseover.kind == RS_CLIENTOP_NPC,
        "and the subject is an npc-kind context");
    TEST_ASSERT(
        app->host.clientop.mouseover.type == NPC_VISIBLE_ID,
        "the subject is the npc the acting row is about, not the pick set's first hit");
    TEST_ASSERT(
        strcmp(app->host.clientop.mouseover.name, "Romeo") == 0,
        "named Romeo, the same subject `_7101` publishes");
    TEST_ASSERT(
        strstr(app->host.clientop.mouseover_op, "Romeo") != NULL,
        "and `_7101` and `_7100` therefore describe one entry");

    /* The resolver, asked directly about each source, is what says the two
     * halves cannot have come from the same row by luck. */
    {
        struct UIMinimenuPick hidden_pick = {
            .kind = UI_MINIMENU_PICK_NPC,
            .id = NPC_HIDDEN_ELEMENT,
        };
        TEST_ASSERT(
            app_minimenu_pick_subject(app, &hidden_pick, &subject) == RS_MINIMENU_TYPE_NPC,
            "the pick set's first hit does resolve -- it is in the pool");
        TEST_ASSERT(
            subject.type == 500,
            "to a different npc from the one the entry names");
    }

    /* A row that is not about a world entity is NONE, which is what 5350 bails
     * on, and the context comes back absent rather than zeroed: a zeroed coord
     * is the corner of the map square. */
    {
        struct UIMinimenuPick ui_pick = {
            .kind = UI_MINIMENU_PICK_UI,
            .id = 30,
        };
        TEST_ASSERT(
            app_minimenu_pick_subject(app, &ui_pick, &subject) == RS_MINIMENU_TYPE_NONE,
            "a widget row names no world subject");
        TEST_ASSERT(subject.kind == -1, "and leaves the subject absent");
        TEST_ASSERT(subject.coord == -1, "with no coord rather than coord 0");
    }

    /* An interface row is type 7 even when its widget has no component id.
     * The engine's plugin buttons in the pop-out column are owned nodes with
     * id -1; reported as NONE, proc 4728 drew no tooltip over them. */
    {
        struct UIMinimenu owned_menu;
        struct UIMinimenuPick owned_pick = {
            .kind = UI_MINIMENU_PICK_UI,
            .id = -1,
        };
        UIMinimenu_Reset(&owned_menu);
        UIMinimenu_AddOption(
            &owned_menu, "Cancel", REVCONFIG_MINIMENU_CANCEL, -1,
            (struct UIMinimenuPick){ .kind = UI_MINIMENU_PICK_NONE });
        UIMinimenu_AddOption(
            &owned_menu, "Open <col=ff9040>XP Tracker</col>",
            RS_MINIMENU_ACTION_PLUGIN_WIDGET, 0, owned_pick);
        app_minimenu_entry_publish(app, &owned_menu);
        TEST_ASSERT(
            app->host.clientop.mouseover_type == RS_MINIMENU_TYPE_COMPONENT,
            "an owned widget's row is an interface row");
        TEST_ASSERT(
            app->host.clientop.mouseover_component == -1,
            "and names no component for `_7109` to latch");
    }

    /* A menu the pointer is on nothing for clears the whole entry, subject
     * included -- the four halves go together in both directions. */
    app_minimenu_entry_publish(app, NULL);
    TEST_ASSERT(
        app->host.clientop.mouseover_type == RS_MINIMENU_TYPE_NONE,
        "no menu, no type");
    TEST_ASSERT(app->host.clientop.mouseover.kind == -1, "no menu, no subject");
    TEST_ASSERT(app->host.clientop.mouseover_opcount == 0, "no menu, no op count");
    TEST_ASSERT(app->host.clientop.mouseover_component == -1, "no menu, no component");

    free(app);
    World_Free(world);
}

int
main(void)
{
    g_failures = 0;
    test_entry_subject_is_the_acting_row();
    printf("%s: %d failure(s)\n", g_failures ? "FAIL" : "PASS", g_failures);
    return g_failures ? 1 : 0;
}
