/*
 * The sailing helm's ids, the selected heading's expiry, and the arrows' load
 * state.
 *
 * A selected heading is shown for exactly 30 client cycles from the cycle it
 * was sent on: on the 30th it is gone. A selection saved and put back on a
 * different clock keeps the cycles it had left, and one that had expired comes
 * back expired. An arrow's model load is queued once.
 *
 * RevConfigRefs_Get is stubbed here, so the ids the reset asks for are named
 * by the test and nothing else is linked.
 *
 *   make -C src test-rs-sailing-hud
 */
#include "game/rs_sailing_hud.h"

#include "revconfig/revconfig_refs.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

#define CHECK(condition, ...)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if( !(condition) )                                                                         \
        {                                                                                          \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/* The revision the stub answers for: every name the reset asks for, and the
 * id it has. A name not listed is absent (-1), as on a pre-sailing revision. */
struct TestRef
{
    char const* kind;
    char const* name;
    int id;
};

static struct TestRef const k_sailing_refs[] = {
    { "varbit", "varb19205_sailing_player_at_helm", 19205 },
    { "varbit", "varb19233_sailing_captain_role", 19233 },
    { "varbit", "sailing_crew_duty_1", 101 },
    { "varbit", "sailing_crew_duty_2", 102 },
    { "varbit", "sailing_crew_duty_3", 103 },
    { "varbit", "sailing_crew_duty_4", 104 },
    { "varbit", "sailing_crew_duty_5", 105 },
    { "varbit", "sailing_crew_roster_1", 201 },
    { "varbit", "sailing_crew_roster_2", 202 },
    { "varbit", "sailing_crew_roster_3", 203 },
    { "varbit", "sailing_crew_roster_4", 204 },
    { "varbit", "sailing_crew_roster_5", 205 },
    { "category", "sailing_crew", 1500 },
    { "model", "sailing_heading_hover", 60001 },
    { "model", "sailing_heading_selected", 60002 },
};

static struct TestRef const* g_refs;
static int g_ref_count;

int
RevConfigRefs_Get(
    struct RevConfigRefs const* refs,
    char const* kind,
    char const* name)
{
    (void)refs;
    for( int i = 0; i < g_ref_count; i++ )
        if( !strcmp(g_refs[i].kind, kind) && !strcmp(g_refs[i].name, name) )
            return g_refs[i].id;
    return -1;
}

static void
reset_with(
    struct RS_SailingHud* hud,
    struct TestRef const* refs,
    int count)
{
    struct RevConfigRefs table;

    memset(&table, 0, sizeof(table));
    g_refs = refs;
    g_ref_count = count;
    RS_SailingHud_Reset(hud, &table);
}

static void
test_reset_resolves_the_ids(void)
{
    struct RS_SailingHud hud;

    memset(&hud, 0x5a, sizeof(hud));
    reset_with(&hud, k_sailing_refs, (int)(sizeof(k_sailing_refs) / sizeof(k_sailing_refs[0])));
    CHECK(hud.ids.at_helm_varbit == 19205, "at-helm varbit %d", hud.ids.at_helm_varbit);
    CHECK(hud.ids.captain_role_varbit == 19233, "role varbit %d", hud.ids.captain_role_varbit);
    for( int slot = 0; slot < RS_SAILING_HUD_CREW_SLOTS; slot++ )
    {
        CHECK(
            hud.ids.crew_duty_varbit[slot] == 101 + slot,
            "duty slot %d is %d",
            slot,
            hud.ids.crew_duty_varbit[slot]);
        CHECK(
            hud.ids.crew_roster_varbit[slot] == 201 + slot,
            "roster slot %d is %d",
            slot,
            hud.ids.crew_roster_varbit[slot]);
    }
    CHECK(hud.ids.crew_category == 1500, "crew category %d", hud.ids.crew_category);
    CHECK(
        hud.ids.arrow_model[RS_SAILING_HUD_ARROW_HOVER] == 60001,
        "hover model %d",
        hud.ids.arrow_model[RS_SAILING_HUD_ARROW_HOVER]);
    CHECK(
        hud.ids.arrow_model[RS_SAILING_HUD_ARROW_SELECTED] == 60002,
        "selected model %d",
        hud.ids.arrow_model[RS_SAILING_HUD_ARROW_SELECTED]);
    for( int i = 0; i < RS_SAILING_HUD_ARROWS; i++ )
    {
        CHECK(RS_SailingHud_ArrowElement(&hud, i) == -1, "arrow %d starts built", i);
        CHECK(hud.arrow_loading[i] == 0, "arrow %d starts loading", i);
    }
    CHECK(RS_SailingHud_SelectedHeading(&hud, 0) == -1, "a heading is selected after reset");

    /* A revision without sailing: every id is absent. */
    reset_with(&hud, NULL, 0);
    CHECK(hud.ids.at_helm_varbit == -1, "at-helm varbit %d without sailing", hud.ids.at_helm_varbit);
    CHECK(hud.ids.crew_category == -1, "crew category %d without sailing", hud.ids.crew_category);
    CHECK(hud.ids.arrow_model[0] == -1, "hover model %d without sailing", hud.ids.arrow_model[0]);
}

static void
test_selected_heading_expires_after_30_cycles(void)
{
    struct RS_SailingHud hud;
    uint64_t const sent = 1000;

    reset_with(&hud, NULL, 0);
    RS_SailingHud_Select(&hud, 7, sent);
    CHECK(RS_SailingHud_SelectedHeading(&hud, sent) == 7, "not shown on the cycle it was sent");
    CHECK(
        RS_SailingHud_SelectedHeading(&hud, sent + RS_SAILING_HUD_SELECTED_CYCLES - 1) == 7,
        "gone before its last cycle");
    CHECK(
        RS_SailingHud_SelectedHeading(&hud, sent + RS_SAILING_HUD_SELECTED_CYCLES) == -1,
        "still shown on the 30th cycle");
    CHECK(RS_SailingHud_SelectedHeading(&hud, sent + 500) == -1, "shown long after it expired");

    /* Heading 0 is a real heading, not "none". */
    RS_SailingHud_Select(&hud, 0, sent);
    CHECK(RS_SailingHud_SelectedHeading(&hud, sent + 1) == 0, "heading 0 read as none");

    /* A second send restarts the window from its own cycle. */
    RS_SailingHud_Select(&hud, 3, sent);
    RS_SailingHud_Select(&hud, 4, sent + 20);
    CHECK(RS_SailingHud_SelectedHeading(&hud, sent + 40) == 4, "a resend did not restart the window");
    CHECK(RS_SailingHud_SelectedHeading(&hud, sent + 50) == -1, "a resend's window ran long");
}

static void
test_selection_survives_a_new_clock(void)
{
    struct RS_SailingHud hud;
    int heading = -9;
    uint64_t remaining = 99;

    reset_with(&hud, NULL, 0);
    RS_SailingHud_Select(&hud, 12, 100);
    RS_SailingHud_SelectionSave(&hud, 110, &heading, &remaining);
    CHECK(heading == 12, "saved heading %d", heading);
    CHECK(remaining == 20, "saved %llu cycles left, not 20", (unsigned long long)remaining);

    /* Put back on a clock far from the one it was saved on. */
    RS_SailingHud_SelectionRestore(&hud, 5000, heading, remaining);
    CHECK(RS_SailingHud_SelectedHeading(&hud, 5019) == 12, "restored selection ended early");
    CHECK(RS_SailingHud_SelectedHeading(&hud, 5020) == -1, "restored selection ran long");

    /* An expired selection saves no cycles, keeps its heading, and comes back
     * expired. */
    RS_SailingHud_Select(&hud, 5, 100);
    RS_SailingHud_SelectionSave(&hud, 200, &heading, &remaining);
    CHECK(heading == 5, "expired selection saved heading %d", heading);
    CHECK(remaining == 0, "expired selection saved %llu cycles", (unsigned long long)remaining);
    RS_SailingHud_SelectionRestore(&hud, 7, heading, remaining);
    CHECK(RS_SailingHud_SelectedHeading(&hud, 7) == -1, "an expired selection came back shown");
}

static void
test_arrow_load_and_elements(void)
{
    struct RS_SailingHud hud;
    int out[4] = { -7, -7, -7, -7 };

    reset_with(&hud, NULL, 0);
    CHECK(RS_SailingHud_ArrowLoadBegin(&hud, 1), "the first load was not begun");
    CHECK(!RS_SailingHud_ArrowLoadBegin(&hud, 1), "a load was begun twice");
    CHECK(RS_SailingHud_ArrowLoadBegin(&hud, 0), "the other arrow's load was refused");

    CHECK(RS_SailingHud_ArrowElementsClaim(&hud, out, 4) == 0, "unbuilt arrows were claimed");
    RS_SailingHud_ArrowElementSet(&hud, 1, 44);
    CHECK(RS_SailingHud_ArrowElement(&hud, 1) == 44, "element was not remembered");
    CHECK(RS_SailingHud_ArrowElementsClaim(&hud, out, 4) == 1, "one built arrow not claimed once");
    CHECK(out[0] == 44, "claimed %d", out[0]);
    RS_SailingHud_ArrowElementSet(&hud, 0, 33);
    CHECK(RS_SailingHud_ArrowElementsClaim(&hud, out, 4) == 2, "two built arrows not both claimed");
    CHECK(out[0] == 33 && out[1] == 44, "claimed %d, %d: hover arrow not first", out[0], out[1]);
    out[0] = out[1] = -7;
    CHECK(RS_SailingHud_ArrowElementsClaim(&hud, out, 1) == 1, "the room limit was not kept");
    CHECK(out[1] == -7, "a claim wrote past its room");
    CHECK(RS_SailingHud_ArrowElementsClaim(&hud, out, 0) == 0, "a claim with no room wrote");
}

int
main(void)
{
    test_reset_resolves_the_ids();
    test_selected_heading_expires_after_30_cycles();
    test_selection_survives_a_new_clock();
    test_arrow_load_and_elements();

    if( g_failures )
    {
        printf("rs_sailing_hud_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("rs_sailing_hud_test: OK\n");
    return 0;
}
