/*
 * The Porcelain core, against the shared fake engine.
 *
 * Each case pins a RULE from the plan, and each rule has a mutation that
 * turns it red -- named in the comment above the case, so the next person can
 * run it. A test that cannot be made to fail is not evidence.
 *
 * Nothing here pins a silent-failure path. Porcelain aborts on a contract
 * violation, so there is no `f(NULL) == 0` line to freeze the habit.
 */

#include "plugin/porcelain/test/porcelain_testbed.h"

#include <stdio.h>
#include <string.h>

static int g_checks;
static int g_failures;

#define CHECK(condition, message)                                                                  \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if( !(condition) )                                                                         \
        {                                                                                          \
            g_failures++;                                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (message));                    \
        }                                                                                          \
    } while( 0 )

/* ------------------------------------------------------------------------ */
/* Shared fixtures                                                          */
/* ------------------------------------------------------------------------ */

static struct ToriRS_PluginDef const DEF_A = {
    .struct_size = sizeof(struct ToriRS_PluginDef), .id = "alpha", .title = "Alpha",
    .version = "1"};
static struct ToriRS_PluginDef const DEF_B = {
    .struct_size = sizeof(struct ToriRS_PluginDef), .id = "beta", .title = "Beta", .version = "1"};

static int g_fixture_ops;

static void
fixture_op(struct ToriRS_Api* api, void* user, char const* key)
{
    (void)api;
    (void)user;
    (void)key;
    g_fixture_ops++;
}

struct Fixture
{
    char const* image;
    char const* caption;
    int gap;
    bool with_second;
    bool replace;
    bool enabled;
    bool visible_with_bar;
    int ops;
};

static void
fixture_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    struct Fixture* fixture = user;
    struct PorcelainItem item;

    memset(&item, 0, sizeof(item));
    item.key = "camera";
    item.image = fixture->image;
    item.place.kind = fixture->replace ? PORCELAIN_REPLACE : PORCELAIN_BESIDE;
    item.place.on = PORCELAIN_EL(REPORT_BUTTON);
    item.place.corner_or_side = PORCELAIN_RIGHT;
    item.place.dx = fixture->gap;
    item.w = 20;
    item.h = 20;
    item.op_label = "Screenshot";
    item.on_op = fixture_op;
    item.hit = true;
    item.enabled = fixture->enabled;
    if( fixture->visible_with_bar )
        item.visible_with = PORCELAIN_EL(CHAT_BAR);
    describe->control(describe, &item);

    if( !fixture->with_second )
        return;
    memset(&item, 0, sizeof(item));
    item.key = "plate";
    item.image = "plate.png";
    item.place.kind = PORCELAIN_INSIDE;
    item.place.on = PORCELAIN_EL(CHAT_BAR);
    item.place.corner_or_side = PORCELAIN_TOP_LEFT;
    item.w = 10;
    item.h = 10;
    describe->piece(describe, &item);
}

static void
declare_chrome(void)
{
    Testbed_DeclareElement("report_button", 400, 470, 60, 20);
    Testbed_DeclareElement("chat_bar", 0, 460, 500, 22);
    Testbed_DeclareAsset("camera.png", TORIRS_ASSET_READY);
    Testbed_DeclareAsset("plate.png", TORIRS_ASSET_READY);
}

static void
fence(struct Porcelain* porcelain)
{
    Porcelain_Fence(porcelain);
    Porcelain_Commit(Testbed_Api());
}

/* ------------------------------------------------------------------------ */
/* 1. Steady state costs nothing                                            */
/* ------------------------------------------------------------------------ */

/*
 * MUTATION: delete the `applied->item.hash == wanted->hash` branch in
 * porcelain_reconcile so every key re-applies its properties. This goes red
 * on "an unchanged description makes no engine call".
 * SECOND MUTATION: drop the `porcelain_inputs_moved` test in Porcelain_Fence
 * so describe runs every frame. Red on the describe-run count.
 */
static void
test_steady_state_costs_nothing(void)
{
    struct Fixture fixture = {.image = "camera.png", .gap = 4, .enabled = true};
    struct Porcelain* porcelain;
    struct PorcelainCounters counters;

    Testbed_Reset();
    declare_chrome();
    Testbed_BindElement("report_button");
    Testbed_BindElement("chat_bar");

    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, fixture_describe, &fixture);
    fence(porcelain);
    CHECK(Testbed_LiveControls() == 1, "the first fence creates the control");

    Testbed_ClearLog();
    Porcelain_CountersReset(porcelain);
    for( int i = 0; i < 5; i++ )
        fence(porcelain);
    Porcelain_CountersRead(porcelain, &counters);
    if( Testbed_LogCount() != 0 )
    {
        fprintf(stderr, "steady state made engine calls:\n");
        Testbed_PrintLog();
    }
    CHECK(Testbed_LogCount() == 0, "five unchanged fences make zero engine calls");
    CHECK(counters.engine_calls == 0, "and Porcelain counts none either");
    CHECK(counters.allocations == 0, "and allocates nothing");
    CHECK(counters.describe_runs == 0, "and does not re-run describe");
    CHECK(counters.revalidates == 0, "and owes no revalidate");

    /*
     * And the OTHER half of the rule: an input that moved re-runs describe,
     * but a description that came back IDENTICAL still costs one hash compare
     * per key and NOTHING ELSE -- not a walk of the property set that happens
     * to find every field equal.
     */
    Testbed_ClearLog();
    Porcelain_CountersReset(porcelain);
    Porcelain_Invalidate(porcelain);
    fence(porcelain);
    Porcelain_CountersRead(porcelain, &counters);
    CHECK(counters.describe_runs == 1, "an input that moved re-runs describe once");
    CHECK(counters.property_applies == 0,
          "and an unchanged hash does not even walk the property set");
    CHECK(Testbed_LogCount() == 0, "so the re-describe still makes zero engine calls");
    Porcelain_Close(porcelain);
}

/* ------------------------------------------------------------------------ */
/* 2. A changed property pushes only that setter                            */
/* ------------------------------------------------------------------------ */

/*
 * MUTATION: in porcelain_apply_properties, replace every `fresh || previous.x
 * != wanted->x` guard with `true`. Red: the log grows the other setters.
 */
static void
test_changed_property_pushes_one_setter(void)
{
    struct Fixture fixture = {.image = "camera.png", .gap = 4, .enabled = true};
    struct Porcelain* porcelain;

    Testbed_Reset();
    declare_chrome();
    Testbed_DeclareAsset("camera_lit.png", TORIRS_ASSET_READY);
    Testbed_BindElement("report_button");
    Testbed_BindElement("chat_bar");

    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, fixture_describe, &fixture);
    fence(porcelain);

    Testbed_ClearLog();
    fixture.image = "camera_lit.png";
    Porcelain_Invalidate(porcelain);
    fence(porcelain);

    CHECK(Testbed_LogCountWith("set_image camera") == 1, "the picture is restated once");
    CHECK(Testbed_LogCountWith("set_opacity") == 0, "the opacity is not");
    CHECK(Testbed_LogCountWith("set_on_op") == 0, "the operation is not");
    CHECK(Testbed_LogCountWith("set_anchor") == 0, "the anchor is not");
    CHECK(Testbed_LogCountWith("remove") == 0, "and the control is never re-created");
    CHECK(Testbed_LogCountWith("create_image") == 0, "no create either");
    Porcelain_Close(porcelain);
}

/* ------------------------------------------------------------------------ */
/* 3. A removed key removes only that control                               */
/* ------------------------------------------------------------------------ */

/*
 * MUTATION: make porcelain_reconcile's removal pass unconditional (remove
 * every applied item before re-applying). Red: "camera" is removed too.
 */
static void
test_removed_key_removes_one_control(void)
{
    struct Fixture fixture = {
        .image = "camera.png", .gap = 4, .enabled = true, .with_second = true};
    struct Porcelain* porcelain;

    Testbed_Reset();
    declare_chrome();
    Testbed_BindElement("report_button");
    Testbed_BindElement("chat_bar");

    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, fixture_describe, &fixture);
    fence(porcelain);
    CHECK(Testbed_LiveControls() == 2, "both items exist");

    Testbed_ClearLog();
    fixture.with_second = false;
    Porcelain_Invalidate(porcelain);
    fence(porcelain);

    CHECK(Testbed_LogCountWith("remove plate") == 1, "the dropped key is removed");
    CHECK(Testbed_LogCountWith("remove camera") == 0, "the kept key is not");
    CHECK(Testbed_LiveControls() == 1, "one control survives");
    CHECK(Testbed_Control("camera") != NULL, "and it is the one still described");
    Porcelain_Close(porcelain);
}

/* ------------------------------------------------------------------------ */
/* 4. The readiness matrix                                                  */
/* ------------------------------------------------------------------------ */

/*
 * The six inputs replayed in every one of their 720 orders converge on an
 * identical tree. This is bug class 1 in one case: four to six async inputs
 * each arrive at their own time and every plugin wrote its own convergence
 * function and missed an edge.
 *
 * MUTATION: in Porcelain_Fence, run describe only when
 * PORCELAIN_INPUT_ELEMENT moved (ignore the other five stamps). Red on every
 * order where the asset or the config lands after the element.
 */
struct MatrixState
{
    struct Porcelain* porcelain;
    int gap;
};

static void
matrix_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    struct MatrixState* state = user;
    struct PorcelainElementState element;
    enum PorcelainAssetState asset = PORCELAIN_ASSET_PENDING;
    struct PorcelainItem item;
    int gap = 0;

    if( !describe->porcelain )
        return;
    if( !Porcelain_Element(state->porcelain, PORCELAIN_EL(REPORT_BUTTON), &element) )
        return;
    (void)Porcelain_Image(state->porcelain, "camera.png", &asset);
    if( asset != PORCELAIN_ASSET_READY )
        return;
    (void)Testbed_Api()->config.get_int(Testbed_Api(), "gap", &gap);

    memset(&item, 0, sizeof(item));
    item.key = "camera";
    item.image = "camera.png";
    item.place.kind = PORCELAIN_BESIDE;
    item.place.on = PORCELAIN_EL(REPORT_BUTTON);
    item.place.corner_or_side = PORCELAIN_RIGHT;
    item.place.dx = gap;
    item.w = 20;
    item.h = 20;
    describe->control(describe, &item);
}

static void
matrix_signature(char* out, size_t capacity)
{
    struct TestbedControl const* control = Testbed_Control("camera");

    if( !control )
    {
        snprintf(out, capacity, "(no control)");
        return;
    }
    snprintf(out, capacity, "camera %d,%d %dx%d op=%d hidden=%d rel=%d live=%d", (int)control->x,
             (int)control->y, (int)control->width, (int)control->height, control->armed ? 1 : 0,
             control->hidden ? 1 : 0, (int)control->relation, Testbed_LiveControls());
}

static void
matrix_apply(int input, struct Porcelain* porcelain)
{
    switch( input )
    {
    case 0:
        Testbed_BindElement("report_button");
        break;
    case 1:
        Testbed_LandAsset("camera.png");
        Porcelain_Note(porcelain, PORCELAIN_INPUT_ASSET);
        break;
    case 2:
        Testbed_SetConfigInt("gap", 7);
        Porcelain_Note(porcelain, PORCELAIN_INPUT_CONFIG);
        break;
    case 3:
        g_testbed.screen = TORIRS_SCREEN_GAME;
        Porcelain_Note(porcelain, PORCELAIN_INPUT_SCREEN);
        break;
    case 4:
        Porcelain_Note(porcelain, PORCELAIN_INPUT_CANVAS);
        break;
    default:
        Porcelain_Invalidate(porcelain);
        break;
    }
}

static void
test_readiness_matrix(void)
{
    int order[6] = {0, 1, 2, 3, 4, 5};
    char reference[128];
    int runs = 0;
    bool first = true;
    bool diverged = false;

    /* Every permutation of six inputs, in place. */
    for( int a = 0; a < 6; a++ )
        for( int b = 0; b < 6; b++ )
            for( int c = 0; c < 6; c++ )
                for( int d = 0; d < 6; d++ )
                    for( int e = 0; e < 6; e++ )
                        for( int f = 0; f < 6; f++ )
                        {
                            struct MatrixState state;
                            char signature[128];
                            unsigned seen = 0;
                            order[0] = a;
                            order[1] = b;
                            order[2] = c;
                            order[3] = d;
                            order[4] = e;
                            order[5] = f;
                            for( int i = 0; i < 6; i++ )
                                seen |= 1u << order[i];
                            if( seen != 0x3f )
                                continue;

                            Testbed_Reset();
                            g_testbed.screen = TORIRS_SCREEN_TITLE;
                            Testbed_DeclareElement("report_button", 400, 470, 60, 20);
                            Testbed_DeclareAsset("camera.png", TORIRS_ASSET_PENDING);
                            memset(&state, 0, sizeof(state));
                            state.porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
                            Porcelain_Describe(state.porcelain, matrix_describe, &state);
                            for( int i = 0; i < 6; i++ )
                            {
                                matrix_apply(order[i], state.porcelain);
                                fence(state.porcelain);
                            }
                            /* Two settling fences: the plan allows a run to
                             * report PENDING and re-run, never to stay wrong. */
                            fence(state.porcelain);
                            fence(state.porcelain);
                            matrix_signature(signature, sizeof(signature));
                            if( first )
                            {
                                snprintf(reference, sizeof(reference), "%s", signature);
                                first = false;
                            }
                            else if( strcmp(reference, signature) != 0 && !diverged )
                            {
                                diverged = true;
                                fprintf(stderr,
                                        "readiness order %d%d%d%d%d%d gives '%s', "
                                        "reference '%s'\n",
                                        a, b, c, d, e, f, signature, reference);
                            }
                            Porcelain_Close(state.porcelain);
                            runs++;
                        }
    CHECK(runs == 720, "every permutation of the six inputs ran");
    CHECK(!diverged, "every order converges on the identical tree");
    CHECK(strcmp(reference, "camera 467,470 20x20 op=0 hidden=0 rel=1 live=1") == 0,
          "and the tree is the one the description states");
}

/* ------------------------------------------------------------------------ */
/* 5. A REPLACE target that dies takes the control with it                  */
/* ------------------------------------------------------------------------ */

/*
 * MUTATION: in porcelain_reconcile, `continue` instead of removing when
 * porcelain_item_target fails. Red: the control keeps painting at its last
 * box, stops inheriting the hide and stays armed -- which is exactly what the
 * engine's silent degrade-to-NATIVE does and why the control must go.
 */
static void
test_replace_target_dies(void)
{
    struct Fixture fixture = {
        .image = "camera.png", .enabled = true, .replace = true};
    struct Porcelain* porcelain;

    Testbed_Reset();
    declare_chrome();
    Testbed_BindElement("report_button");
    Testbed_BindElement("chat_bar");

    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, fixture_describe, &fixture);
    fence(porcelain);
    CHECK(Testbed_Control("camera") != NULL, "the camera stands in for Report");
    CHECK(Testbed_Control("camera")->relation == TORIRS_WIDGET_RELATION_REPLACE,
          "through a REPLACE anchor, not a positioned child");
    CHECK(Testbed_Control("camera")->x == 420, "and Porcelain owns its box: centred on the target");

    Testbed_ClearLog();
    Testbed_UnbindElement("report_button");
    fence(porcelain);
    fence(porcelain);
    fence(porcelain);
    CHECK(Testbed_Control("camera") == NULL, "an unbound target removes the control");
    CHECK(Testbed_LogCountWith("remove camera") == 1, "exactly once");

    Testbed_BindElement("report_button");
    fence(porcelain);
    CHECK(Testbed_Control("camera") != NULL, "and it comes back when the target rebinds");
    Porcelain_Close(porcelain);
}

/* ------------------------------------------------------------------------ */
/* 6. Arbitration                                                           */
/* ------------------------------------------------------------------------ */

/*
 * MUTATION: in Porcelain_ClaimTake, return true whenever a claim exists.
 * Red: both plugins apply and the loser records no finding.
 * SECOND MUTATION: delete Porcelain_ClaimDropAll from Porcelain_Close and
 * from Porcelain_Relinquish. Red on the relinquish case below -- the outgoing
 * provider keeps every aspect and the incoming one gets findings instead of a
 * frame.
 */
static void
replace_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    struct PorcelainItem item;

    memset(&item, 0, sizeof(item));
    item.key = (char const*)user;
    item.image = "camera.png";
    item.place.kind = PORCELAIN_REPLACE;
    item.place.on = PORCELAIN_EL(REPORT_BUTTON);
    item.w = 20;
    item.h = 20;
    item.op_label = "Op";
    item.on_op = fixture_op;
    item.hit = true;
    item.enabled = true;
    describe->control(describe, &item);
}

static void
test_arbitration_first_claim(void)
{
    struct Porcelain* first;
    struct Porcelain* second;
    struct PorcelainFinding findings[8];
    int count;

    Testbed_Reset();
    declare_chrome();
    Testbed_BindElement("report_button");

    first = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    second = Porcelain_Open(Testbed_Api(), &DEF_B, NULL);
    Porcelain_Describe(first, replace_describe, (void*)"alpha_cam");
    Porcelain_Describe(second, replace_describe, (void*)"beta_cam");
    fence(first);
    fence(second);
    Porcelain_Commit(Testbed_Api());

    CHECK(Testbed_Control("alpha_cam") != NULL, "the first claimer's control exists");
    CHECK(Testbed_Control("beta_cam") == NULL, "the loser's is not applied");
    CHECK(strcmp(Porcelain_ClaimOwner(PORCELAIN_EL(REPORT_BUTTON), PORCELAIN_ASPECT_REPLACE),
                 "alpha") == 0,
          "and the claim is inspectable");

    count = Porcelain_Findings(second, findings, 8);
    CHECK(count >= 1, "the loser records a finding");
    CHECK(findings[0].result == PORCELAIN_FINDING_ARBITRATION_LOST, "an arbitration loss");
    CHECK(strcmp(findings[0].detail, "alpha") == 0, "whose detail NAMES the winner");

    /* Swap the event order and the winner swaps: nothing about the identity
     * of the two plugins decides this, only who asked first. */
    Porcelain_Close(first);
    Porcelain_Close(second);
    Testbed_Reset();
    declare_chrome();
    Testbed_BindElement("report_button");
    second = Porcelain_Open(Testbed_Api(), &DEF_B, NULL);
    first = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(second, replace_describe, (void*)"beta_cam");
    Porcelain_Describe(first, replace_describe, (void*)"alpha_cam");
    fence(second);
    fence(first);
    Porcelain_Commit(Testbed_Api());
    CHECK(Testbed_Control("beta_cam") != NULL, "swap the order and the winner swaps");
    CHECK(Testbed_Control("alpha_cam") == NULL, "and the other one loses");
    Porcelain_Close(first);
    Porcelain_Close(second);
}

static void
test_arbitration_relinquish(void)
{
    struct Porcelain* outgoing;
    struct Porcelain* incoming;

    Testbed_Reset();
    declare_chrome();
    Testbed_BindElement("report_button");

    outgoing = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    incoming = Porcelain_Open(Testbed_Api(), &DEF_B, NULL);
    Porcelain_Describe(outgoing, replace_describe, (void*)"alpha_cam");
    Porcelain_Describe(incoming, replace_describe, (void*)"beta_cam");
    fence(outgoing);
    fence(incoming);
    Porcelain_Commit(Testbed_Api());
    CHECK(Testbed_Control("beta_cam") == NULL, "the incoming provider is refused while both live");

    /* The host keeps both frame providers alive for one fence by design. The
     * outgoing one relinquishes at the release fence, BEFORE arbitration runs
     * again -- without this the switch hands the outgoing provider every
     * aspect of all eight surfaces. */
    Porcelain_Relinquish(outgoing);
    CHECK(Porcelain_ClaimOwner(PORCELAIN_EL(REPORT_BUTTON), PORCELAIN_ASPECT_REPLACE) == NULL,
          "relinquish drops the claim");
    Porcelain_Invalidate(incoming);
    fence(incoming);
    Porcelain_Commit(Testbed_Api());
    CHECK(Testbed_Control("beta_cam") != NULL, "and the next claimer gets the element");
    CHECK(strcmp(Porcelain_ClaimOwner(PORCELAIN_EL(REPORT_BUTTON), PORCELAIN_ASPECT_REPLACE),
                 "beta") == 0,
          "with the claim now in its name");
    Porcelain_Close(outgoing);
    Porcelain_Close(incoming);
}

/* ------------------------------------------------------------------------ */
/* 7. Findings                                                              */
/* ------------------------------------------------------------------------ */

/*
 * MUTATION: drop the coalescing loop in Porcelain_RecordFinding and always
 * take a free slot. Red: the table fills and the count stays 1.
 */
static void
absent_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    struct PorcelainElementState state;

    (void)user;
    /* A LANE: one element it has, two it does not. Absence is a fact about a
     * lane, so a scene where nothing at all resolves is a client still
     * mounting rather than a lane missing two controls -- see
     * test_absence_waits_for_the_lane. */
    (void)Porcelain_Element(describe->porcelain, PORCELAIN_EL(CHAT), &state);
    (void)Porcelain_Element(describe->porcelain, PORCELAIN_EL(LANE_CHROME), &state);
    (void)Porcelain_Element(describe->porcelain, PORCELAIN_EL(REPORT_BUTTON), &state);
}

static void
test_findings_coalesce(void)
{
    struct Porcelain* porcelain;
    struct PorcelainFinding findings[8];
    int count;

    Testbed_Reset();
    Testbed_DeclareElement("chat", 0, 338, 519, 165);
    Testbed_BindElement("chat");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, absent_describe, NULL);
    for( int i = 0; i < 6; i++ )
    {
        Porcelain_Invalidate(porcelain);
        fence(porcelain);
    }
    count = Porcelain_Findings(porcelain, findings, 8);
    CHECK(count == 2, "two absent elements are two findings, not twelve");
    CHECK(findings[0].count >= 1, "and the repeats are counted, not duplicated");
    CHECK(findings[0].first_frame >= 1, "with the frame the first one happened on");
    Porcelain_Close(porcelain);
}

/*
 * An element that has not resolved YET is not absent.
 *
 * This is the case the first live run found: a gameframe takes hundreds of
 * frames to mount, and a plugin that asks during the mount used to be told
 * every element it wanted was missing -- twenty-two findings that all bound
 * moments later. Nothing settles until this plugin has seen one element of
 * the lane resolve.
 *
 * MUTATION: settle regardless of any_element_bound. Red: the findings appear
 * while the lane is still mounting.
 */
static void
test_absence_waits_for_the_lane(void)
{
    struct Porcelain* porcelain;
    struct PorcelainFinding findings[8];

    Testbed_Reset();
    Testbed_DeclareElement("chat", 0, 338, 519, 165);
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, absent_describe, NULL);
    for( int i = 0; i < 20; i++ )
    {
        Porcelain_Invalidate(porcelain);
        fence(porcelain);
    }
    CHECK(Porcelain_Findings(porcelain, findings, 8) == 0,
          "a lane that has not mounted reports no absences, however long it takes");

    /* The moment one element of the lane resolves, the lane exists, and the
     * two that still will not resolve are absent rather than early. */
    Testbed_BindElement("chat");
    for( int i = 0; i < 4; i++ )
    {
        Porcelain_Invalidate(porcelain);
        fence(porcelain);
    }
    CHECK(Porcelain_Findings(porcelain, findings, 8) == 2,
          "and once it has, the two it really lacks are reported");

    /* A lane that mounts in pieces answers late, and an absence it answers is
     * not a fact about the lane. The table says what is wrong NOW.
     * MUTATION: drop porcelain_forget_absence. Red: the finding outlives the
     * element's arrival. */
    Testbed_DeclareElement("report_button", 437, 480, 79, 23);
    Testbed_BindElement("report_button");
    for( int i = 0; i < 2; i++ )
    {
        Porcelain_Invalidate(porcelain);
        fence(porcelain);
    }
    CHECK(Porcelain_Findings(porcelain, findings, 8) == 1,
          "an element that turns up takes its absence with it");
    Porcelain_Close(porcelain);
}

/*
 * MUTATION: in porcelain_watch_listener's BOUND arm, delete the
 * porcelain_expected_absence branch. Red on the second half: a stale
 * declaration then excuses a real regression for the rest of the session.
 */
static void
test_expected_absence_both_directions(void)
{
    struct Porcelain* porcelain;
    struct PorcelainFinding findings[8];
    int count;
    bool saw_expected = false;
    bool saw_unexpected = false;

    /* Direction one: declared absent, and absent. */
    Testbed_Reset();
    Testbed_DeclareElement("report_button", 400, 470, 60, 20);
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_ExpectAbsent(porcelain, PORCELAIN_EL(LANE_CHROME), "no popout strip on this lane");
    Porcelain_Describe(porcelain, absent_describe, NULL);
    Testbed_BindElement("report_button");
    fence(porcelain);
    fence(porcelain);
    fence(porcelain);
    count = Porcelain_Findings(porcelain, findings, 8);
    for( int i = 0; i < count; i++ )
        if( findings[i].result == PORCELAIN_FINDING_ABSENT_EXPECTED ||
            (findings[i].result == PORCELAIN_FINDING_ABSENT && findings[i].expected) )
            saw_expected = true;
    CHECK(saw_expected, "a declared absence that stays absent is expected, not a failure");
    Porcelain_Close(porcelain);

    /* Direction two: declared absent, and it BINDS. */
    Testbed_Reset();
    Testbed_DeclareElement("lane_chrome_0", 765, 0, 42, 500);
    Testbed_DeclareElement("report_button", 400, 470, 60, 20);
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_ExpectAbsent(porcelain, PORCELAIN_EL(LANE_CHROME), "no popout strip on this lane");
    Porcelain_Describe(porcelain, absent_describe, NULL);
    fence(porcelain);
    Testbed_BindElement("lane_chrome_0");
    fence(porcelain);
    count = Porcelain_Findings(porcelain, findings, 8);
    for( int i = 0; i < count; i++ )
        if( findings[i].result == PORCELAIN_FINDING_ABSENT_UNEXPECTEDLY_PRESENT )
            saw_unexpected = true;
    CHECK(saw_unexpected, "a declared-absent element that BINDS is a failure");
    Porcelain_Close(porcelain);
}

/* ------------------------------------------------------------------------ */
/* 8. The tier operator                                                     */
/* ------------------------------------------------------------------------ */

/*
 * RuneLite's rows. Two shipped plugins share the spelling of these
 * thresholds and disagreed on the operator: loot-beam used >= where the
 * reference is strictly >, and neither had the zero gate, so a zero threshold
 * meant "everything qualifies" here and "this tier is off" there.
 *
 * MUTATION: change one `>` to `>=` in Porcelain_Tier. Red on the
 * exactly-at-threshold rows. Change `tiers->low > 0` to `tiers->low >= 0`.
 * Red on the disabled-tier rows.
 */
static void
test_tier_table(void)
{
    struct PorcelainTiers const tiers = {.low = 20000, .medium = 100000, .high = 1000000,
                                         .insane = 10000000};
    struct PorcelainTiers const low_off = {.low = 0, .medium = 100000, .high = 1000000,
                                           .insane = 10000000};
    struct PorcelainTiers const negative = {.low = -1, .medium = -1, .high = -1, .insane = -1};

    CHECK(Porcelain_Tier(&tiers, 0) == PORCELAIN_TIER_NONE, "nothing below the low threshold");
    CHECK(Porcelain_Tier(&tiers, 20000) == PORCELAIN_TIER_NONE,
          "an item worth EXACTLY the low threshold is not a low-value item");
    CHECK(Porcelain_Tier(&tiers, 20001) == PORCELAIN_TIER_LOW, "one over it is");
    CHECK(Porcelain_Tier(&tiers, 100000) == PORCELAIN_TIER_LOW, "exactly medium is still low");
    CHECK(Porcelain_Tier(&tiers, 100001) == PORCELAIN_TIER_MEDIUM, "one over medium is medium");
    CHECK(Porcelain_Tier(&tiers, 1000001) == PORCELAIN_TIER_HIGH, "and high is high");
    CHECK(Porcelain_Tier(&tiers, 10000001) == PORCELAIN_TIER_INSANE, "and insane is insane");
    CHECK(Porcelain_Tier(&tiers, 10000000) == PORCELAIN_TIER_HIGH, "exactly insane is high");

    CHECK(Porcelain_Tier(&low_off, 1) == PORCELAIN_TIER_NONE,
          "a threshold at zero DISABLES its tier, it does not match everything");
    CHECK(Porcelain_Tier(&low_off, 100001) == PORCELAIN_TIER_MEDIUM,
          "and the tiers above it still work");
    CHECK(Porcelain_Tier(&negative, 999999999) == PORCELAIN_TIER_NONE,
          "a negative threshold disables its tier too");
}

/* ------------------------------------------------------------------------ */
/* 9. The config list refuses rather than truncates                         */
/* ------------------------------------------------------------------------ */

/*
 * MUTATION: in Porcelain_ConfigListAdd, replace the `needed > sizeof(joined)`
 * refusal with an snprintf into `joined`. Red: the stored list changes and
 * the finding disappears -- which is the live bug, a tag list cut mid-id that
 * stores a wrong species and reads back as one.
 */
static void
test_config_list_refusal(void)
{
    struct Porcelain* porcelain;
    char filler[PORCELAIN_CONFIG_VALUE_MAX];
    struct PorcelainFinding findings[8];
    int count;
    bool refused = false;

    Testbed_Reset();
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);

    CHECK(Porcelain_ConfigListAdd(porcelain, "tags", "goblin"), "the first add lands");
    CHECK(strcmp(Testbed_ConfigString("tags"), "goblin") == 0, "as the whole list");
    CHECK(Porcelain_ConfigListAdd(porcelain, "tags", "imp"), "the second joins with a comma");
    CHECK(strcmp(Testbed_ConfigString("tags"), "goblin,imp") == 0, "in order");
    CHECK(Porcelain_ConfigListAdd(porcelain, "tags", "GOBLIN"),
          "a duplicate is a no-op, matched case-insensitively");
    CHECK(strcmp(Testbed_ConfigString("tags"), "goblin,imp") == 0, "and changes nothing");

    memset(filler, 'x', sizeof(filler));
    filler[PORCELAIN_CONFIG_VALUE_MAX - 12] = '\0';
    Testbed_SetConfigString("tags", filler);
    CHECK(!Porcelain_ConfigListAdd(porcelain, "tags", "a_long_species_name"),
          "an add that would pass the ceiling is REFUSED");
    CHECK(strcmp(Testbed_ConfigString("tags"), filler) == 0,
          "and the stored list is byte-for-byte unchanged");
    count = Porcelain_Findings(porcelain, findings, 8);
    for( int i = 0; i < count; i++ )
        if( findings[i].result == PORCELAIN_FINDING_BUDGET )
            refused = true;
    CHECK(refused, "with a finding naming the refusal");
    Porcelain_Close(porcelain);
}

/* ------------------------------------------------------------------------ */
/* 10. The description is a total order                                     */
/* ------------------------------------------------------------------------ */

/*
 * MUTATION: iterate porcelain->scratch_items backwards in porcelain_reconcile.
 * Red: the create/anchor order inverts, and a later item is no longer over an
 * earlier one -- the linear anchor chain both frame providers build by hand.
 */
static void
chain_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    char const* const KEYS[3] = {"base", "stone", "face"};
    struct PorcelainItem item;

    (void)user;
    for( int i = 0; i < 3; i++ )
    {
        memset(&item, 0, sizeof(item));
        item.key = KEYS[i];
        item.image = "plate.png";
        item.place.kind = PORCELAIN_AT_ELEMENT;
        item.place.on = PORCELAIN_EL(CHAT_BAR);
        item.place.dx = i;
        item.w = 8;
        item.h = 8;
        describe->piece(describe, &item);
    }
}

static void
test_description_total_order(void)
{
    struct Porcelain* porcelain;
    int base, stone, face;

    Testbed_Reset();
    declare_chrome();
    Testbed_BindElement("chat_bar");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, chain_describe, NULL);
    fence(porcelain);

    base = Testbed_LogFind("create_image base");
    stone = Testbed_LogFind("create_image stone");
    face = Testbed_LogFind("create_image face");
    CHECK(base >= 0 && stone >= 0 && face >= 0, "all three pieces exist");
    CHECK(base < stone, "the second described piece is applied after the first");
    CHECK(stone < face, "and the third after the second");
    base = Testbed_LogFind("set_anchor base");
    stone = Testbed_LogFind("set_anchor stone");
    face = Testbed_LogFind("set_anchor face");
    CHECK(base < stone && stone < face, "and the anchor chain is stated in that same order");
    Porcelain_Close(porcelain);
}

/* ------------------------------------------------------------------------ */
/* 11. The direct path: compare-then-set, one revalidate a frame            */
/* ------------------------------------------------------------------------ */

/*
 * MUTATION: drop the `x != applied->live_x` compare in Porcelain_Set. Red:
 * the identical write reaches the engine, and an identical write defeats the
 * emit retain gate for the whole frame.
 * SECOND MUTATION: move the revalidate out of porcelain_flush_epoch and into
 * Porcelain_Set. Red on the count: xp-drop-orbs was paying fourteen
 * full-tree resolves a frame this way.
 */
static void
test_direct_path(void)
{
    struct Fixture fixture = {.image = "camera.png", .gap = 4, .enabled = true};
    struct Porcelain* porcelain;
    struct PorcelainMotion motion;

    Testbed_Reset();
    declare_chrome();
    Testbed_BindElement("report_button");
    Testbed_BindElement("chat_bar");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, fixture_describe, &fixture);
    fence(porcelain);

    Testbed_ClearLog();
    memset(&motion, 0, sizeof(motion));
    motion.mask = PORCELAIN_MOTION_X | PORCELAIN_MOTION_Y;
    motion.x = 500;
    motion.y = 300;
    Porcelain_Fence(porcelain);
    CHECK(Porcelain_Set(porcelain, "camera", &motion) == TORIRS_RESULT_OK, "a moved key sets");
    CHECK(Porcelain_Set(porcelain, "camera", &motion) == TORIRS_RESULT_OK, "twice is legal");
    CHECK(Porcelain_Set(porcelain, "camera", &motion) == TORIRS_RESULT_OK, "and three times");
    Porcelain_Commit(Testbed_Api());
    CHECK(Testbed_LogCountWith("set_position") == 1,
          "three identical writes reach the engine once: compare-then-set");
    CHECK(Testbed_LogCountWith("revalidate") == 1,
          "and exactly ONE revalidate for the whole frame");

    /* A key that is not in the applied description is a plugin that believes
     * it drew something it did not. */
    CHECK(Porcelain_Set(porcelain, "nothing", &motion) == TORIRS_RESULT_NOT_FOUND,
          "an unknown key is refused");
    Porcelain_Close(porcelain);
}

/*
 * MUTATION: in porcelain_flush_epoch, issue the revalidate once per handle
 * instead of once per epoch. Red here.
 */
static void
test_one_revalidate_for_every_plugin(void)
{
    struct Fixture first_fixture = {.image = "camera.png", .gap = 4, .enabled = true};
    struct Porcelain* first;
    struct Porcelain* second;

    Testbed_Reset();
    declare_chrome();
    Testbed_BindElement("report_button");
    Testbed_BindElement("chat_bar");
    first = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    second = Porcelain_Open(Testbed_Api(), &DEF_B, NULL);
    Porcelain_Describe(first, fixture_describe, &first_fixture);
    Porcelain_Describe(second, chain_describe, NULL);

    Testbed_ClearLog();
    Porcelain_Fence(first);
    Porcelain_Fence(second);
    Porcelain_Commit(Testbed_Api());
    CHECK(Testbed_LogCountWith("revalidate") == 1,
          "two plugins with four new controls between them cost ONE revalidate");
    Porcelain_Close(first);
    Porcelain_Close(second);
}

/* ------------------------------------------------------------------------ */
/* 12. visible_with, .enabled and .image = NULL                             */
/* ------------------------------------------------------------------------ */

/*
 * MUTATION: delete the visible_with branch in porcelain_reconcile's
 * visibility block. Red: a plate over a hidden compass stays, which is defect
 * F7 verbatim.
 */
static void
test_visibility_and_enablement(void)
{
    struct Fixture fixture = {
        .image = "camera.png", .gap = 4, .enabled = true, .visible_with_bar = true};
    struct Porcelain* porcelain;

    Testbed_Reset();
    declare_chrome();
    Testbed_BindElement("report_button");
    Testbed_BindElement("chat_bar");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, fixture_describe, &fixture);
    fence(porcelain);
    CHECK(Testbed_Control("camera") != NULL, "the control exists");
    CHECK(!Testbed_Control("camera")->hidden, "and shows while its gate shows");
    CHECK(Testbed_Control("camera")->armed, "and is armed while enabled");

    Testbed_PresentElement("chat_bar", false);
    fence(porcelain);
    CHECK(Testbed_Control("camera")->hidden, "a hidden gate hides the item in step");

    Testbed_PresentElement("chat_bar", true);
    fence(porcelain);
    CHECK(!Testbed_Control("camera")->hidden, "and showing it again brings the item back");

    /* `.enabled` false: DRAWN, INERT, NO MENU ROW. */
    fixture.enabled = false;
    Porcelain_Invalidate(porcelain);
    fence(porcelain);
    CHECK(!Testbed_Control("camera")->armed, "a disabled item has no armed operation");
    CHECK(!Testbed_Control("camera")->hidden, "but it is still drawn");
    Porcelain_Close(porcelain);
}

/*
 * `.image = NULL` on a Control or a Blocker is an invisible hit box. Both
 * frame providers compose a 1x1 transparent PNG to fake this today.
 *
 * MUTATION: make porcelain_push_item assert on a NULL image. Red immediately.
 */
static void
blocker_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    struct PorcelainPlacement place;

    (void)user;
    memset(&place, 0, sizeof(place));
    place.kind = PORCELAIN_AT_ELEMENT;
    place.on = PORCELAIN_EL(CHAT_BAR);
    describe->blocker(describe, "swallow", "Cancel", place, 100, 40, NULL, NULL);
}

static void
test_blocker_needs_no_picture(void)
{
    struct Porcelain* porcelain;

    Testbed_Reset();
    declare_chrome();
    Testbed_BindElement("chat_bar");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, blocker_describe, NULL);
    fence(porcelain);
    CHECK(Testbed_Control("swallow") != NULL, "a blocker with no image exists");
    CHECK(Testbed_LogCountWith("set_image swallow #0") == 1,
          "with the empty picture, and no composed 1x1 PNG anywhere");
    CHECK(Testbed_Control("swallow")->width == 100, "and it carries its stated box");
    Porcelain_Close(porcelain);
}

/* ------------------------------------------------------------------------ */
/* 13. Element edits are retained while described                           */
/* ------------------------------------------------------------------------ */

/*
 * MUTATION: delete the release pass for edits in porcelain_reconcile. Red:
 * the move survives a description that stopped stating it, which is defect F9
 * -- "moves not re-described survive a release".
 */
struct EditFixture
{
    bool move;
    bool hide;
};

static void
edit_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    struct EditFixture const* fixture = user;

    if( fixture->move )
        describe->move(describe, PORCELAIN_EL(CHAT_BAR),
                       (struct ToriRS_WidgetBounds){17, 357, 519, 96}, 0);
    if( fixture->hide )
        describe->hide(describe, PORCELAIN_EL(REPORT_BUTTON));
}

static void
test_edits_are_retained_and_released(void)
{
    struct EditFixture fixture = {.move = true, .hide = true};
    struct Porcelain* porcelain;

    Testbed_Reset();
    declare_chrome();
    Testbed_BindElement("report_button");
    Testbed_BindElement("chat_bar");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, edit_describe, &fixture);
    fence(porcelain);
    CHECK(Testbed_LogCountWith("set_position chat_bar 17,357") == 1, "the move is applied");
    CHECK(Testbed_LogCountWith("set_hidden report_button 1") == 1, "and the hide");

    Testbed_ClearLog();
    fixture.move = false;
    Porcelain_Invalidate(porcelain);
    fence(porcelain);
    CHECK(Testbed_LogCountWith("reset chat_bar") == 1,
          "a move that is no longer described is released");
    CHECK(Testbed_LogCountWith("reset report_button") == 0, "and the hide that still is, is not");
    Porcelain_Close(porcelain);
}

/*
 * PORCELAIN_KEEP_RELATIVE: a member keeps its block-relative offset when the
 * block moves. Without it the orb block moved as one box loses members 1 and 2.
 *
 * MUTATION: delete the KEEP_RELATIVE branch in porcelain_apply_edit. Red: the
 * member is moved to INT32_MIN.
 */
static void
keep_relative_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    (void)user;
    describe->move(describe, PORCELAIN_ORB_EL(PORCELAIN_ORB_RUN),
                   (struct ToriRS_WidgetBounds){PORCELAIN_KEEP_RELATIVE, 120, 0, 0}, 0);
}

static void
test_keep_relative(void)
{
    struct Porcelain* porcelain;

    Testbed_Reset();
    Testbed_DeclareElement("orb_run", 33, 90, 26, 26);
    Testbed_BindElement("orb_run");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, keep_relative_describe, NULL);
    fence(porcelain);
    CHECK(Testbed_LogCountWith("set_position orb_run 33,120") == 1,
          "KEEP_RELATIVE keeps the member's own x and moves only its y");
    Porcelain_Close(porcelain);
}

/* ------------------------------------------------------------------------ */
/* 14. The small helpers                                                    */
/* ------------------------------------------------------------------------ */

/*
 * MUTATION: change PORCELAIN_MENU_TAG_OPS to 4. Red: two subjects collide.
 */
static void
test_menu_tag(void)
{
    CHECK(Porcelain_MenuTag(0, 0) == 0, "subject 0 op 0 is tag 0");
    CHECK(Porcelain_MenuTag(0, 1) != Porcelain_MenuTag(1, 0),
          "an op of one subject never aliases another subject");
    CHECK(Porcelain_MenuTag(7, 15) != Porcelain_MenuTag(8, 0),
          "not even at the top of a subject's range");
    CHECK(Porcelain_MenuTag(1234, 3) == 1234u * PORCELAIN_MENU_TAG_OPS + 3u,
          "and the encoding is the stated one");
}

/*
 * MUTATION: make Porcelain_Setting read the varbit without checking the
 * capability. Red: an absent var answers as a live reading, which is the
 * silent 1.0 multiplier.
 */
static void
test_setting_absent_is_off(void)
{
    struct Porcelain* porcelain;
    struct PorcelainFinding findings[8];
    int count;
    bool absent = false;

    Testbed_Reset();
    snprintf(g_testbed.capabilities, sizeof(g_testbed.capabilities),
             "varbit:special_attack_armed");
    snprintf(g_testbed.named_ids, sizeof(g_testbed.named_ids), "varbit:special_attack_armed=301");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);

    CHECK(Porcelain_Setting(porcelain, "special_attack_armed", 0),
          "a declared varbit reads as a setting");
    CHECK(!Porcelain_Setting(porcelain, "special_attack_armed", PORCELAIN_SETTING_INVERTED),
          "and the inversion flag inverts it");
    CHECK(!Porcelain_Setting(porcelain, "spicy_stew_boost", 0),
          "a var this revision does not declare is OFF, not a live reading");
    for( int i = 0; i < 20; i++ )
        (void)Porcelain_Setting(porcelain, "spicy_stew_boost", 0);
    count = Porcelain_Findings(porcelain, findings, 8);
    for( int i = 0; i < count; i++ )
        if( findings[i].result == PORCELAIN_FINDING_ABSENT )
            absent = true;
    CHECK(absent, "with a finding");
    CHECK(count == 1, "ONE finding across twenty-one reads");
    Porcelain_Close(porcelain);
}

/*
 * MUTATION: delete the touch branch in Porcelain_KeyEdge. Red: the feature
 * reports itself armed on a lane where input.key_held can never be true.
 */
static void
edge_seen(struct ToriRS_Api* api, void* user, bool down)
{
    (void)api;
    *(int*)user += down ? 1 : 100;
}

static void
test_key_edge(void)
{
    struct Porcelain* porcelain;
    int edges = 0;

    Testbed_Reset();
    Testbed_SetConfigString("reveal_key", "shift");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    CHECK(Porcelain_KeyEdge(porcelain, "reveal_key", edge_seen, &edges),
          "a desktop lane arms the edge");
    fence(porcelain);
    CHECK(edges == 0, "nothing fires while the key is up");
    Porcelain_NoteKey(porcelain, TORIRS_KEY_SHIFT, true);
    CHECK(edges == 1, "the press is one edge");
    Porcelain_NoteKey(porcelain, TORIRS_KEY_SHIFT, true);
    CHECK(edges == 1, "and a repeat of the same press is not a second");
    Porcelain_NoteKey(porcelain, TORIRS_KEY_SHIFT, false);
    CHECK(edges == 101, "the release is the other edge");
    Porcelain_NoteKey(porcelain, TORIRS_KEY_ESCAPE, true);
    CHECK(edges == 101, "and a key this binding does not name is not an edge");

    /*
     * A press that opens and closes inside one frame IS two edges. The fence
     * poll this replaced could not see one at all: it sampled key_held once a
     * frame, so a tap was a hotkey that sometimes did nothing.
     *
     * MUTATION: drop the code!=key test in Porcelain_NoteKey. Red: every key
     * drives every binding.
     */
    edges = 0;
    Porcelain_NoteKey(porcelain, TORIRS_KEY_SHIFT, true);
    Porcelain_NoteKey(porcelain, TORIRS_KEY_SHIFT, false);
    CHECK(edges == 101, "a press and release inside one frame is both edges");
    Porcelain_Close(porcelain);

    /*
     * A key is a NUMBER; the five names are a convenience. A hotkey a user
     * can rebind to any key was unreachable while this understood five
     * strings and nothing else.
     *
     * MUTATION: delete the strtol arm in porcelain_key_code. Red: a numeric
     * binding reads as "off" and reports itself absent.
     */
    Testbed_Reset();
    Testbed_SetConfigString("reveal_key", "119");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    edges = 0;
    CHECK(Porcelain_KeyEdge(porcelain, "reveal_key", edge_seen, &edges),
          "a numeric binding arms");
    fence(porcelain);
    Porcelain_NoteKey(porcelain, 119, true);
    CHECK(edges == 1, "and takes the edge for that code");
    Porcelain_Close(porcelain);

    Testbed_Reset();
    g_testbed.touch = true;
    Testbed_SetConfigString("reveal_key", "shift");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    edges = 0;
    CHECK(!Porcelain_KeyEdge(porcelain, "reveal_key", edge_seen, &edges),
          "a touch lane answers ABSENT rather than arming a dead key");
    fence(porcelain);
    CHECK(edges == 0, "and the callback never fires");
    Porcelain_Close(porcelain);
}

/*
 * MUTATION: drop the `slot->inputs_hash == hash` test in Porcelain_Derived.
 * Red: the paint runs every call, which is the per-frame recompose the
 * icon_revision hash caused.
 * SECOND MUTATION: make the FAILED arm fall through and retry. Red on the
 * terminal count.
 */
struct PaintCounter
{
    int paints;
    bool fail;
};

static bool
counting_paint(struct ToriRS_Api* api, void* user, uint32_t* argb, int width, int height)
{
    struct PaintCounter* counter = user;

    (void)api;
    (void)argb;
    (void)width;
    (void)height;
    counter->paints++;
    return !counter->fail;
}

static void
test_derived_once_per_input(void)
{
    struct Porcelain* porcelain;
    struct PaintCounter counter = {0, false};
    enum PorcelainDerivedState state = PORCELAIN_DERIVED_PENDING;
    int inputs[2] = {26, 26};
    struct PorcelainFinding findings[8];
    int count;
    bool failed = false;

    Testbed_Reset();
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    for( int i = 0; i < 5; i++ )
        (void)Porcelain_Derived(porcelain, "side_tiled", inputs, sizeof(inputs), 8, 8,
                                counting_paint, &counter, &state);
    CHECK(counter.paints == 1, "painted at most once per (key, hash of inputs)");
    CHECK(state == PORCELAIN_DERIVED_READY, "and READY once composed");

    inputs[0] = 34;
    (void)Porcelain_Derived(porcelain, "side_tiled", inputs, sizeof(inputs), 8, 8, counting_paint,
                            &counter, &state);
    CHECK(counter.paints == 2, "a changed input repaints exactly once");

    counter.fail = true;
    inputs[0] = 99;
    for( int i = 0; i < 4; i++ )
        (void)Porcelain_Derived(porcelain, "side_tiled", inputs, sizeof(inputs), 8, 8,
                                counting_paint, &counter, &state);
    CHECK(state == PORCELAIN_DERIVED_FAILED, "a paint that refuses is FAILED");
    CHECK(counter.paints == 3, "and FAILED is TERMINAL: it is not retried");
    count = Porcelain_Findings(porcelain, findings, 8);
    for( int i = 0; i < count; i++ )
        if( findings[i].result == PORCELAIN_FINDING_DERIVED_FAILED )
            failed = true;
    CHECK(failed, "with one finding, not a silently square minimap");
    Porcelain_Close(porcelain);
}

/*
 * MUTATION: make Porcelain_Require return true unconditionally. Red: the
 * feature reports itself on where the lane cannot carry it.
 */
static void
test_require_reports_the_feature(void)
{
    struct Porcelain* porcelain;
    struct PorcelainFinding findings[8];
    int count;

    Testbed_Reset();
    snprintf(g_testbed.capabilities, sizeof(g_testbed.capabilities), "cs2_scripts");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    CHECK(Porcelain_Require(porcelain, "cs2_scripts", "native ground-item captions"),
          "a capability the lane answers arms its feature");
    CHECK(!Porcelain_Require(porcelain, "loot_events", "kill announcements"),
          "one it does not answer disables the feature");
    count = Porcelain_Findings(porcelain, findings, 8);
    CHECK(count == 1, "with exactly one finding");
    CHECK(findings[0].result == PORCELAIN_FINDING_UNSUPPORTED, "an unsupported capability");
    CHECK(strcmp(findings[0].detail, "kill announcements") == 0,
          "whose detail names the FEATURE, not the capability");
    Porcelain_Close(porcelain);
}

/*
 * A count is lane DATA. Eight filters on the desktop, four on 2004; nobody
 * picks a number.
 *
 * MUTATION: return a constant 8 from Porcelain_Count's CHAT_FILTER arm. Red
 * on the four-filter lane.
 */
static void
test_counts_are_lane_data(void)
{
    struct Porcelain* porcelain;

    Testbed_Reset();
    for( int i = 0; i < 4; i++ )
    {
        char role[32];
        snprintf(role, sizeof(role), "chat_plate_%d", i);
        Testbed_DeclareElement(role, i * 20, 470, 18, 18);
        Testbed_BindElement(role);
    }
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    CHECK(Porcelain_Count(porcelain, PORCELAIN_EL_CHAT_FILTER) == 4,
          "a lane with four filter plates answers four");
    CHECK(Porcelain_Count(porcelain, PORCELAIN_EL_ORB) == 0,
          "and a lane with no native orbs answers zero, not a guess");
    Porcelain_Close(porcelain);

    Testbed_Reset();
    for( int i = 0; i < 8; i++ )
    {
        char role[32];
        snprintf(role, sizeof(role), "chat_plate_%d", i);
        Testbed_DeclareElement(role, i * 20, 470, 18, 18);
        Testbed_BindElement(role);
    }
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    CHECK(Porcelain_Count(porcelain, PORCELAIN_EL_CHAT_FILTER) == 8,
          "and a lane with eight answers eight, from the same code");
    Porcelain_Close(porcelain);
}

/*
 * A TAB resolves through the profile's own tab map when the lane numbers its
 * sidebar, and through an authored builtin when it does not. Both are DATA.
 *
 * MUTATION: delete the named_id branch in porcelain_resolve_tab. Red on the
 * numbered lane.
 */
static void
tab_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    struct PorcelainElementState state;

    (void)user;
    (void)Porcelain_Element(describe->porcelain, PORCELAIN_TAB_EL("inventory"), &state);
}

static void
test_tab_resolves_by_data(void)
{
    struct Porcelain* porcelain;

    /* A lane that numbers its sidebar: the [tabs] map answers. */
    Testbed_Reset();
    Testbed_DeclareElement("sidetab_3", 660, 200, 30, 30);
    Testbed_BindElement("sidetab_3");
    snprintf(g_testbed.named_ids, sizeof(g_testbed.named_ids), "tab:inventory=3");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, tab_describe, NULL);
    fence(porcelain);
    CHECK(Testbed_LogCountWith("watch_state sidetab_3") == 1,
          "a numbered sidebar resolves TAB(inventory) through the profile map");
    Porcelain_Close(porcelain);

    /* A lane with an authored builtin: the role answers directly. */
    Testbed_Reset();
    Testbed_DeclareElement("tab_inventory", 660, 200, 30, 30);
    Testbed_BindElement("tab_inventory");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, tab_describe, NULL);
    fence(porcelain);
    CHECK(Testbed_LogCountWith("watch_state tab_inventory") == 1,
          "and an authored builtin resolves it without a map");
    CHECK(Testbed_LogCountWith("watch_state sidetab_") == 0, "with no numbered spelling anywhere");
    Porcelain_Close(porcelain);
}

/*
 * The spelling is re-asked until something binds.
 *
 * Resolution runs the first time a plugin asks for an element, and the first
 * ask of a session runs before any tree has published: NEITHER spelling
 * answers, and a library that froze its choice there would watch the wrong
 * name for the rest of the run. That is precisely what happened live --
 * `tab:inventory` absent on the 2004 lane with the role `tab_inventory` bound
 * beside it.
 *
 * MUTATION: delete the porcelain_watch_respell() call in
 * porcelain_resolve_pending. Red: the element never binds.
 */
static void
test_spelling_is_re_asked(void)
{
    struct Porcelain* porcelain;
    struct PorcelainElementState state;
    int watches_before;

    /* A numbered sidebar whose stones publish LATE. Neither spelling answers
     * at the first ask, so the fallback is the authored one -- wrong here. */
    Testbed_Reset();
    Testbed_DeclareElement("sidetab_3", 660, 200, 30, 30);
    snprintf(g_testbed.named_ids, sizeof(g_testbed.named_ids), "tab:inventory=3");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, tab_describe, NULL);
    fence(porcelain);
    CHECK(!Porcelain_Element(porcelain, PORCELAIN_TAB_EL("inventory"), &state),
          "an element neither spelling answers yet is not bound");
    CHECK(Testbed_LogCountWith("watch_state tab_inventory") == 1,
          "and the watch opens on the name the plugin would have used by hand");
    watches_before = Testbed_LiveWatches();

    Testbed_BindElement("sidetab_3");
    fence(porcelain);
    fence(porcelain);
    CHECK(Porcelain_Element(porcelain, PORCELAIN_TAB_EL("inventory"), &state),
          "the stone binding under the OTHER spelling still reaches the element");
    CHECK(Testbed_LogCountWith("watch_state sidetab_3") == 1,
          "because the spelling is re-asked while the element is unresolved");
    CHECK(Testbed_LiveWatches() == watches_before,
          "and the name it gave up is handed back, not leaked");
    Porcelain_Close(porcelain);
}

/*
 * A lane that authored a `tab_<name>` builtin AND numbers its sidebar answers
 * the builtin. Both facts are true on the 2004 lane -- `panel_inventory =
 * slot(sidebar, 3)` makes the profile answer a tab number there too -- so a
 * resolver that took the number the moment it was offered watched a
 * `sidetab_3` the lane does not have.
 *
 * MUTATION: in porcelain_spellings, put the sidetab spelling first for TAB.
 * Red: the watch opens on sidetab_3 and the builtin is never reached.
 */
static void
test_tab_prefers_the_spelling_that_answers(void)
{
    struct Porcelain* porcelain;
    struct PorcelainElementState state;

    Testbed_Reset();
    Testbed_DeclareElement("tab_inventory", 660, 200, 30, 30);
    /* The profile answers a number as well: this lane has both. */
    snprintf(g_testbed.named_ids, sizeof(g_testbed.named_ids), "tab:inventory=3");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, tab_describe, NULL);
    fence(porcelain);
    Testbed_BindElement("tab_inventory");
    fence(porcelain);
    fence(porcelain);
    CHECK(Porcelain_Element(porcelain, PORCELAIN_TAB_EL("inventory"), &state),
          "the authored builtin binds the element");
    CHECK(Testbed_LogCountWith("watch_state sidetab_") == 0,
          "and no numbered spelling is ever watched on a lane that answers the name");
    Porcelain_Close(porcelain);
}

/*
 * CHAT_FILTER has the same two shapes. Where the cache enumerates its plates
 * the element is `chat_plate_<n>`; where it does not, a filter is member <n>
 * of the frame's chat-buttons slot, which two of the four 2004 filters have
 * no authored role name for at all.
 *
 * MUTATION: drop the `chat_buttons:%d` spelling from porcelain_spellings.
 * Red: the member lane binds nothing and counts zero.
 */
static void
chat_filter_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    struct PorcelainElementState state;

    (void)user;
    (void)Porcelain_Element(describe->porcelain, PORCELAIN_CHAT_FILTER_EL(3), &state);
}

static void
test_chat_filter_resolves_on_both_shapes(void)
{
    struct Porcelain* porcelain;
    struct PorcelainElementState state;

    /* The enumerated-plate shape. */
    Testbed_Reset();
    for( int i = 0; i < 8; i++ )
    {
        char role[32];
        snprintf(role, sizeof(role), "chat_plate_%d", i);
        Testbed_DeclareElement(role, i * 60, 480, 56, 22);
        Testbed_BindElement(role);
    }
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, chat_filter_describe, NULL);
    fence(porcelain);
    CHECK(Porcelain_Element(porcelain, PORCELAIN_CHAT_FILTER_EL(3), &state),
          "a plate lane binds CHAT_FILTER(3)");
    CHECK(Testbed_LogCountWith("watch_state chat_plate_3") == 1, "under the plate spelling");
    CHECK(Porcelain_Count(porcelain, PORCELAIN_EL_CHAT_FILTER) == 8, "and counts its eight");
    Porcelain_Close(porcelain);

    /* The slot-member shape: four filters, and no plate anywhere. */
    Testbed_Reset();
    for( int i = 0; i < 4; i++ )
    {
        char role[32];
        snprintf(role, sizeof(role), "chat_buttons:%d", i);
        Testbed_DeclareElement(role, i * 100, 467, 100, 32);
        Testbed_BindElement(role);
    }
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, chat_filter_describe, NULL);
    fence(porcelain);
    CHECK(Porcelain_Element(porcelain, PORCELAIN_CHAT_FILTER_EL(3), &state),
          "a slot-member lane binds the same element");
    CHECK(state.box.x == 300, "at the member's own box");
    CHECK(Testbed_LogCountWith("watch_state chat_buttons:3") == 1, "under the member spelling");
    CHECK(Porcelain_Count(porcelain, PORCELAIN_EL_CHAT_FILTER) == 4,
          "and counts four, which is this lane's real number");
    Porcelain_Close(porcelain);
}

/*
 * A count is ONE PAST THE HIGHEST MEMBER PRESENT -- find_all's own contract --
 * and not "however many answered before the first miss". The 2004 sidebar's
 * tab 7 carries no interface, so a count that stopped at the first hole would
 * report seven stones on a lane that has fourteen.
 *
 * MUTATION: break out of porcelain_count_members' loops at the first member
 * that does not resolve. Red: 7 instead of 14.
 */
static void
test_count_spans_a_hole(void)
{
    struct Porcelain* porcelain;

    Testbed_Reset();
    for( int i = 0; i < 14; i++ )
    {
        char role[32];
        if( i == 7 )
            continue; /* the unused 2004 slot */
        snprintf(role, sizeof(role), "sidebar:%d", i);
        Testbed_DeclareElement(role, 660, 200 + i * 10, 30, 30);
        Testbed_BindElement(role);
    }
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    CHECK(Porcelain_Count(porcelain, PORCELAIN_EL_TAB) == 14,
          "a family with a hole still counts one past its highest member");
    Porcelain_Close(porcelain);
}

/* ------------------------------------------------------------------------ */
/* The overlay verbs                                                        */
/* ------------------------------------------------------------------------ */

/*
 * The drawable rect of a pass, and an element's box on it.
 *
 * Only the passes that ARE the canvas can answer a canvas-space box. A panel
 * well's drawable rect has its own origin, and a tooltip clamped against the
 * wrong rectangle is the retired placement bug that flipped it up over the
 * minimap.
 *
 * MUTATION: set out->canvas_space = true unconditionally. Red: the well pass
 * reports a viewport box it cannot use.
 */
static void
test_draw_context_says_which_space(void)
{
    struct Porcelain* porcelain;
    struct PorcelainElementState state;
    struct PorcelainDrawContext context;
    struct PorcelainFinding findings[4];

    Testbed_Reset();
    Testbed_DeclareElement("viewport", 4, 4, 512, 334);
    Testbed_BindElement("viewport");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    CHECK(Porcelain_Element(porcelain, PORCELAIN_EL(VIEWPORT), &state), "the viewport is bound");

    /* The world pass: the whole canvas, at origin zero. */
    CHECK(Porcelain_DrawContext(porcelain,
                                Testbed_Graphics((struct ToriRS_Rect){0, 0, 800, 500}, true),
                                PORCELAIN_EL(VIEWPORT), &context),
          "the world pass answers a context");
    CHECK(context.bounds.width == 800 && context.bounds.height == 500,
          "whose drawable rect is the pass's own");
    CHECK(context.canvas_space, "and which IS canvas space");
    CHECK(context.usable.width == 800, "so the usable canvas is answered");
    CHECK(context.element_bound && context.element.x == 4 && context.element.width == 512,
          "and the element's box is usable in it");

    /* A panel well: its own origin, its own size. */
    CHECK(Porcelain_DrawContext(porcelain,
                                Testbed_Graphics((struct ToriRS_Rect){20, 40, 200, 120}, true),
                                PORCELAIN_EL(VIEWPORT), &context),
          "a well answers a context too");
    CHECK(context.bounds.width == 200, "at the well's size");
    CHECK(!context.canvas_space, "but it is not canvas space");
    CHECK(!context.element_bound && context.element.width == 0,
          "so no canvas-space element box is handed out");
    CHECK(context.usable.width == 0, "and neither is the usable canvas");
    Porcelain_Close(porcelain);

    /*
     * A pass that set no region at all -- which is what the world pass did
     * until the engine learned to set one.
     *
     * MUTATION: return true from Porcelain_DrawContext when context() is
     * false. Red: the caller draws against a zeroed rectangle and no finding
     * says so.
     */
    Testbed_Reset();
    Testbed_DeclareElement("viewport", 4, 4, 512, 334);
    Testbed_BindElement("viewport");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    CHECK(!Porcelain_DrawContext(porcelain,
                                 Testbed_Graphics((struct ToriRS_Rect){0, 0, 800, 500}, false),
                                 PORCELAIN_EL(VIEWPORT), &context),
          "a pass with no region answers nothing");
    CHECK(Porcelain_Findings(porcelain, findings, 4) == 1, "with exactly one finding");
    CHECK(findings[0].result == PORCELAIN_FINDING_REFUSED, "a refusal, not silence");
    Porcelain_Close(porcelain);
}

/*
 * A refused menu route is a finding, never a dropped false. The host's route
 * table is shared and bounded; over it, `add` answers false, and both shipped
 * overlays threw that answer away.
 *
 * MUTATION: return api->menu.add's bool from Porcelain_MenuAdd without
 * recording anything. Red: no finding.
 */
static void
test_menu_add_refusal_is_a_finding(void)
{
    struct Porcelain* porcelain;
    struct ToriRS_MenuBuildEvent menu;
    struct PorcelainFinding findings[4];

    Testbed_Reset();
    memset(&menu, 0, sizeof(menu));
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    g_testbed.menu_routes_left = 1;
    CHECK(Porcelain_MenuAdd(porcelain, &menu, "Examine", 7u), "the first row is routed");
    CHECK(!Porcelain_MenuAdd(porcelain, &menu, "Drop", 8u), "the second is refused");
    CHECK(Porcelain_Findings(porcelain, findings, 4) == 1, "and the refusal is one finding");
    CHECK(findings[0].result == PORCELAIN_FINDING_REFUSED, "a refusal");
    CHECK(strcmp(findings[0].detail, "Drop") == 0, "whose detail names the row that was lost");
    Porcelain_Close(porcelain);
}

/*
 * The world hull's TWO refusals reach the plugin.
 *
 * `ToriRS_Graphics::world_hull` is declared to answer a result and the engine
 * answered TORIRS_RESULT_OK unconditionally: the host's api_draw_hull was
 * void, so the per-frame draw budget and the per-entity APPEARANCE claim were
 * both thrown away between them. What it cost on screen is not an error --
 * it is half the outlines in a mass of tagged npcs, with the plugin, the
 * layer and the player all unable to tell.
 *
 * MUTATION 1: make api_draw_hull's budget arm `return TORIRS_RESULT_OK`.
 *   Red: "the draw over the allotment is refused".
 * MUTATION 2: make its claim arm `return TORIRS_RESULT_OK`.
 *   Red: "a claimed entity's outline is refused".
 * MUTATION 3: make v2_builder_world_hull ignore api_draw_hull's answer and
 *   return TORIRS_RESULT_OK, which is the shipped defect exactly.
 *   Red: both of the above.
 * MUTATION 4: drop the BUDGET arm in Porcelain_Hull.
 *   Red: "the finding names the budget".
 */
static void
test_world_hull_refusals_reach_the_plugin(void)
{
    struct Porcelain* porcelain;
    struct ToriRS_Graphics* draw;
    struct PorcelainFinding findings[8];
    int count;
    bool budget = false;
    bool lost = false;

    Testbed_Reset();
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    draw = Testbed_Graphics((struct ToriRS_Rect){0, 0, 800, 500}, true);

    g_testbed.hull_budget = 2;
    CHECK(Porcelain_Hull(porcelain, draw, 11, 0xff0000u, 0, TORIRS_HULL_MESH),
          "a draw inside the allotment is drawn");
    CHECK(Porcelain_Hull(porcelain, draw, 12, 0xff0000u, 0, TORIRS_HULL_MESH),
          "and so is the last one in it");
    CHECK(!Porcelain_Hull(porcelain, draw, 13, 0xff0000u, 0, TORIRS_HULL_MESH),
          "the draw over the allotment is refused");
    /* Twenty more entities in the same frame are the SAME finding: a mass of
     * npcs must not flood a fixed table with one row each. */
    for( int at = 0; at < 20; at++ )
        (void)Porcelain_Hull(porcelain, draw, 100 + at, 0xff0000u, 0, TORIRS_HULL_MESH);
    count = Porcelain_Findings(porcelain, findings, 8);
    CHECK(count == 1, "twenty-one refusals in one frame are ONE finding");
    for( int at = 0; at < count; at++ )
        if( findings[at].result == PORCELAIN_FINDING_BUDGET &&
            strcmp(findings[at].verb, "world_hull") == 0 &&
            strstr(findings[at].detail, "budget") != NULL )
            budget = true;
    CHECK(budget, "the finding names the budget");
    CHECK(findings[0].count == 21, "and counts every draw it swallowed");
    Porcelain_Close(porcelain);

    /*
     * The other refusal, which is not a bug in what the plugin asked for: an
     * entity whose APPEARANCE another plugin holds is that plugin's to
     * outline. The loser is still entitled to know it lost.
     */
    Testbed_Reset();
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    draw = Testbed_Graphics((struct ToriRS_Rect){0, 0, 800, 500}, true);
    g_testbed.hull_claimed_element = 42;
    CHECK(Porcelain_Hull(porcelain, draw, 41, 0x00ff00u, 128, TORIRS_HULL_BOUNDS),
          "an unclaimed entity is everybody's");
    CHECK(!Porcelain_Hull(porcelain, draw, 42, 0x00ff00u, 128, TORIRS_HULL_BOUNDS),
          "a claimed entity's outline is refused");
    count = Porcelain_Findings(porcelain, findings, 8);
    for( int at = 0; at < count; at++ )
        if( findings[at].result == PORCELAIN_FINDING_ARBITRATION_LOST &&
            strcmp(findings[at].verb, "world_hull") == 0 )
            lost = true;
    CHECK(lost, "and says it was an arbitration, not a budget");
    Porcelain_Close(porcelain);
}

/*
 * A control whose TARGET moved to another parent is re-made under it.
 *
 * The parent was asked for once, at create, and never again: a frame provider
 * that rebuilds a subtree leaves the control a child of the node it was born
 * under, and every fence after that writes the target's new parent-local box
 * into a control whose origin is somewhere else. That is not a missing
 * control and not an error -- it is a constant, lane-dependent offset, which
 * is exactly the shape that survives every screenshot. The minimap-orbs port
 * carries a settle counter, a per-orb incarnation guard and a frame-root
 * rebuild guard to work around it.
 *
 * MUTATION: delete the re-parent arm in porcelain_reconcile. Red: "the
 * control is a child of the parent the target moved to".
 * SECOND MUTATION: make porcelain_item_target_moved return false always.
 * Red: the same line -- the cheap trigger is load-bearing, not an
 * optimisation.
 */
static void
reparent_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    struct PorcelainItem item;

    (void)user;
    memset(&item, 0, sizeof(item));
    item.key = "cover";
    item.image = "camera.png";
    item.place.kind = PORCELAIN_REPLACE;
    item.place.on = PORCELAIN_ROLE_EL("orb_run");
    /* Stated, so the settled fences below do not pay an image_size lookup for
     * a natural size the description could have said. */
    item.w = 57;
    item.h = 34;
    item.enabled = true;
    Porcelain_Control(describe, &item);
}

static void
test_a_moved_target_takes_its_control_with_it(void)
{
    struct Porcelain* porcelain;
    struct ToriRS_WidgetRef second_parent;
    struct PorcelainCounters counters;

    Testbed_Reset();
    Testbed_DeclareAsset("camera.png", TORIRS_ASSET_READY);
    Testbed_DeclareElement("orb_run", 10, 103, 57, 34);
    Testbed_BindElement("orb_run");

    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, reparent_describe, NULL);
    fence(porcelain);
    CHECK(Testbed_LiveControls() == 1, "the cover is created beside its target");
    CHECK(ToriRS_WidgetRefEqual(Testbed_Control("cover")->parent,
                                Testbed_Element("orb_run")->parent),
          "under the target's parent");

    /* The provider rebuilds the orb block: same element, new parent, and the
     * re-place that comes with it is what the layer actually sees. */
    second_parent = Testbed_Element("orb_run")->parent;
    second_parent.opaque[1] = 77;
    Testbed_Element("orb_run")->parent = second_parent;
    Porcelain_CountersReset(porcelain);
    Testbed_MoveElement("orb_run", 10, 140);
    fence(porcelain);

    Porcelain_CountersRead(porcelain, &counters);
    CHECK(Testbed_LiveControls() == 1, "there is still exactly one cover");
    CHECK(ToriRS_WidgetRefEqual(Testbed_Control("cover")->parent, second_parent),
          "and the control is a child of the parent the target moved to");
    CHECK(counters.reparents == 1, "counted once, as a remove and a create");

    /* And it settles: a target that stops moving stops costing anything. */
    Porcelain_CountersReset(porcelain);
    Testbed_ClearLog();
    for( int at = 0; at < 4; at++ )
        fence(porcelain);
    Porcelain_CountersRead(porcelain, &counters);
    CHECK(counters.reparents == 0, "a settled tree re-parents nothing");
    CHECK(counters.engine_calls == 0, "and asks the engine nothing at all");
    Porcelain_Close(porcelain);
}

/*
 * A plugin can record a finding of its own.
 *
 * Two shipped ledger rows are core.log lines because it could not: a
 * NATIVE_BLOCKED coming back from widgets.invoke inside an op callback is the
 * PLUGIN's result, not one of Porcelain's calls, so the press correctly
 * stopped and said so where the gate cannot read it.
 *
 * MUTATION: make Porcelain_Finding a no-op. Red: no finding.
 * SECOND MUTATION: drop the coalescing key so the verb is ignored. Red: the
 * two different verbs collapse into one row.
 */
static void
test_plugin_records_its_own_finding(void)
{
    struct Porcelain* porcelain;
    struct PorcelainFinding findings[8];
    int count;

    Testbed_Reset();
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Finding(porcelain, "invoke", PORCELAIN_ROLE_EL("orb_run"),
                      PORCELAIN_FINDING_REFUSED, "native blocked");
    for( int at = 0; at < 9; at++ )
        Porcelain_Finding(porcelain, "invoke", PORCELAIN_ROLE_EL("orb_run"),
                          PORCELAIN_FINDING_REFUSED, "native blocked");
    Porcelain_Finding(porcelain, "prices", PORCELAIN_EL(NONE), PORCELAIN_FINDING_ASSET_MISSING,
                      "prices.txt");
    count = Porcelain_Findings(porcelain, findings, 8);
    CHECK(count == 2, "two verbs are two findings, and ten of one are still one");
    CHECK(strcmp(findings[0].verb, "invoke") == 0, "the plugin's own verb is what it said");
    CHECK(findings[0].count == 10, "coalesced on (verb, element, result) like every other");
    CHECK(findings[0].element.kind == PORCELAIN_EL_ROLE, "and it names the element it was about");
    CHECK(strcmp(findings[1].detail, "prices.txt") == 0, "the second carries its own detail");
    Porcelain_Close(porcelain);
}

/*
 * The menu tag round-trips.
 *
 * It only ever encoded, so every consumer spelled the operations-per-subject
 * constant by hand and raising it would silently re-target every retained
 * menu row in every shipped plugin.
 *
 * MUTATION: make Porcelain_MenuUntag divide by a literal 16 while
 * PORCELAIN_MENU_TAG_OPS is something else. Red: the round trip.
 */
static void
test_menu_tag_round_trips(void)
{
    int subject = -1;
    int op = -1;

    Porcelain_MenuUntag(Porcelain_MenuTag(1234, 3), &subject, &op);
    CHECK(subject == 1234, "the subject comes back");
    CHECK(op == 3, "and so does the operation");
    Porcelain_MenuUntag(Porcelain_MenuTag(0, 0), &subject, &op);
    CHECK(subject == 0, "including subject zero");
    CHECK(op == 0, "and operation zero");
    Porcelain_MenuUntag(Porcelain_MenuTag(7, PORCELAIN_MENU_TAG_OPS - 1), &subject, &op);
    CHECK(subject == 7, "and the highest operation does not carry into the subject");
    CHECK(op == PORCELAIN_MENU_TAG_OPS - 1, "which is what a hand-spelled 16 could not promise");
}

/*
 * "Is the reveal key down" is a QUESTION, not a subscription.
 *
 * Answering it through the edge form cost a config key the plugin never
 * wanted, a fence every frame and a mirrored boolean -- up to two engine
 * calls per frame to answer something asked once per right-click.
 *
 * MUTATION: delete the `code < 0` arm in Porcelain_KeyDown. Red: a key name
 * that resolves to nothing reads as "not held" instead of as a finding.
 * SECOND MUTATION: delete the touch arm. Red: the touch lane answers from a
 * keyboard frame it does not have.
 */
static void
test_key_down_is_asked_not_subscribed(void)
{
    struct Porcelain* porcelain;
    struct PorcelainFinding findings[4];
    int count;
    bool absent = false;

    Testbed_Reset();
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    g_testbed.key_held = TORIRS_KEY_SHIFT;
    CHECK(Porcelain_KeyDown(porcelain, "shift"), "the held key answers true by name");
    CHECK(!Porcelain_KeyDown(porcelain, "ctrl"), "and a key that is not held answers false");
    g_testbed.key_held = 119;
    CHECK(Porcelain_KeyDown(porcelain, "119"), "a decimal code is the same vocabulary");
    CHECK(!Porcelain_KeyDown(porcelain, "off"), "and 'off' resolves to nothing");
    count = Porcelain_Findings(porcelain, findings, 4);
    for( int at = 0; at < count; at++ )
        if( findings[at].result == PORCELAIN_FINDING_ABSENT &&
            strcmp(findings[at].verb, "key_down") == 0 )
            absent = true;
    CHECK(absent, "which is a finding, not a silent 'the modifier is up'");
    Porcelain_Close(porcelain);

    Testbed_Reset();
    g_testbed.touch = true;
    g_testbed.key_held = TORIRS_KEY_SHIFT;
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    CHECK(!Porcelain_KeyDown(porcelain, "shift"),
          "a touch lane has no keyboard frame, so it answers false with a finding");
    CHECK(Porcelain_Findings(porcelain, findings, 4) == 1, "one finding");
    Porcelain_Close(porcelain);
}

/*
 * A binding that goes away must not leave the watch holding `down`.
 *
 * The fence used to `continue` past an unresolvable key code without clearing
 * it: a key held when the config went to `off` and still held when it came
 * back never transitioned again, so the feature stayed dead until the player
 * released and pressed. There was no test either way, which is why the loop
 * could be written that way at all.
 *
 * MUTATION: move `watch->code = code` above the `code != watch->code` test.
 * Red: the release across the absent window never fires.
 */
static void
test_key_edge_clears_down_across_an_absent_binding(void)
{
    struct Porcelain* porcelain;
    int edges = 0;

    Testbed_Reset();
    Testbed_SetConfigString("reveal_key", "shift");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    CHECK(Porcelain_KeyEdge(porcelain, "reveal_key", edge_seen, &edges), "the edge arms");
    fence(porcelain);
    Porcelain_NoteKey(porcelain, TORIRS_KEY_SHIFT, true);
    CHECK(edges == 1, "the key goes down");

    /* The binding goes away while the key is still held. */
    edges = 0;
    Testbed_SetConfigString("reveal_key", "off");
    fence(porcelain);
    CHECK(edges == 100, "a binding that goes away releases the key it was holding");

    /* And when it comes back, a fresh press is a fresh edge -- which it could
     * not be while `down` was still true underneath. */
    edges = 0;
    Testbed_SetConfigString("reveal_key", "shift");
    fence(porcelain);
    CHECK(edges == 0, "the binding coming back is not itself an edge");
    Porcelain_NoteKey(porcelain, TORIRS_KEY_SHIFT, true);
    CHECK(edges == 1, "and the next press fires again");
    Porcelain_Close(porcelain);
}

/*
 * The stored list is SORTED and deduplicated, and has a removal half.
 *
 * `config_list_add` joined `current + "," + item`, which is only the ledger
 * row's sorted list when the caller happens to add in order; and the removal
 * half had no verb at all, so a plugin taking a species out of a tag list did
 * a raw config.set with its own join -- exactly the shape config_list_add
 * exists to stop.
 *
 * MUTATION: delete the porcelain_list_sort call. Red: "sorted, whatever order
 * they arrived in".
 * SECOND MUTATION: make Porcelain_ConfigListRemove write when the item was
 * absent. Red: "an absent item costs no write".
 */
static void
test_config_list_is_a_set(void)
{
    struct Porcelain* porcelain;
    char const* three[3] = {"zamorak", "Guthix", "saradomin"};
    struct PorcelainCounters counters;

    Testbed_Reset();
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);

    CHECK(Porcelain_ConfigListAdd(porcelain, "tags", "zulrah"), "the first add lands");
    CHECK(Porcelain_ConfigListAdd(porcelain, "tags", "abyssal"), "and so does a second");
    CHECK(strcmp(Testbed_ConfigString("tags"), "abyssal,zulrah") == 0,
          "sorted, whatever order they arrived in");

    CHECK(Porcelain_ConfigListRemove(porcelain, "tags", "ZULRAH"),
          "removal matches case-insensitively, like the duplicate test");
    CHECK(strcmp(Testbed_ConfigString("tags"), "abyssal") == 0, "and takes only that item out");

    Porcelain_CountersReset(porcelain);
    CHECK(Porcelain_ConfigListRemove(porcelain, "tags", "never_tagged"),
          "removing something absent is the state the caller asked for");
    Porcelain_CountersRead(porcelain, &counters);
    CHECK(counters.engine_calls == 1, "an absent item costs no write, only the read");

    CHECK(Porcelain_ConfigListSet(porcelain, "tags", three, 3), "the whole list at once");
    CHECK(strcmp(Testbed_ConfigString("tags"), "Guthix,saradomin,zamorak") == 0,
          "sorted, with the stored spelling kept");
    CHECK(Porcelain_ConfigListSet(porcelain, "tags", NULL, 0), "and an empty list is legal");
    CHECK(strcmp(Testbed_ConfigString("tags"), "") == 0, "it clears the key");
    Porcelain_Close(porcelain);
}

static void
hover_menu_row(struct ToriRS_MenuBuildEvent* menu, bool hover_pass, int obj, int slot,
               int component_id)
{
    memset(menu, 0, sizeof(*menu));
    menu->hover_pass = hover_pass;
    menu->row_count = 1;
    menu->rows[0].text = "Use";
    menu->rows[0].pick_kind = PORCELAIN_MENU_PICK_INV_SLOT;
    menu->rows[0].target_id = obj;
    menu->rows[0].slot = slot;
    menu->rows[0].component_id = component_id;
    menu->rows[0].npc_slot = -1;
    menu->rows[0].player_pid = -1;
}

/*
 * The hovered cell carries the container it came out of.
 *
 * The same obj hovered in the bank and in the inventory are two different
 * questions, and the shipped tooltip captured `component_id` and `slot` and
 * then never read either -- so a bank hover keyed identically to an inventory
 * one and the tooltip said the wrong thing.
 *
 * MUTATION: return PORCELAIN_CONTAINER_INV from porcelain_container_of
 * unconditionally. Red: the two hovers agree.
 */
static void
test_hover_carries_the_container(void)
{
    struct Porcelain* porcelain;
    struct ToriRS_MenuBuildEvent menu;
    struct PorcelainHover inventory;
    struct PorcelainHover elsewhere;
    struct TestbedElement* cell;

    Testbed_Reset();
    Testbed_DeclareElement("panel_inventory", 547, 205, 190, 261);
    Testbed_BindElement("panel_inventory");
    Testbed_DeclareElement("panel_equipment", 547, 205, 190, 261);
    Testbed_BindElement("panel_equipment");
    cell = Testbed_DeclareElement("component:1001", 560, 220, 36, 32);
    cell->parent = Testbed_Element("panel_inventory")->ref;
    Testbed_BindElement("component:1001");
    cell = Testbed_DeclareElement("component:1500", 560, 220, 36, 32);
    cell->parent = Testbed_Element("panel_equipment")->ref;
    Testbed_BindElement("component:1500");
    /* A cell in a container this vocabulary cannot name: it hangs off the
     * shared root, not off a declared panel. */
    Testbed_DeclareElement("component:2002", 100, 100, 36, 32);
    Testbed_BindElement("component:2002");

    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    hover_menu_row(&menu, true, 995, 3, 1001);
    Porcelain_NoteMenu(porcelain, &menu);
    CHECK(Porcelain_Hover(porcelain, &inventory), "the hover pass answers a hovered cell");
    CHECK(inventory.obj == 995, "with the item in the cell");
    CHECK(inventory.slot == 3, "and its slot");
    CHECK(inventory.container == PORCELAIN_CONTAINER_INV, "and the container it belongs to");

    hover_menu_row(&menu, true, 995, 3, 1500);
    Porcelain_NoteMenu(porcelain, &menu);
    CHECK(Porcelain_Hover(porcelain, &elsewhere), "the same obj worn is still a hover");
    CHECK(elsewhere.container == PORCELAIN_CONTAINER_WORN,
          "and a worn slot is a worn slot, not an inventory cell");

    hover_menu_row(&menu, true, 995, 3, 2002);
    Porcelain_NoteMenu(porcelain, &menu);
    CHECK(Porcelain_Hover(porcelain, &elsewhere), "the same obj elsewhere is still a hover");
    CHECK(elsewhere.container != PORCELAIN_CONTAINER_INV,
          "a cell in a container this vocabulary cannot name is not an inventory cell");
    CHECK(elsewhere.container_id != inventory.container_id,
          "and it carries its own container id, so a key built from it cannot collide");

    /*
     * A right-click build is not a hover.
     *
     * MUTATION: delete the `if( !menu->hover_pass ) return;` guard in
     * Porcelain_NoteMenu. Red: the stash follows the pointer under an open
     * menu.
     */
    hover_menu_row(&menu, false, 1337, 9, 1001);
    Porcelain_NoteMenu(porcelain, &menu);
    CHECK(Porcelain_Hover(porcelain, &elsewhere) && elsewhere.obj == 995,
          "a right-click build leaves the hover where it was");

    /*
     * And a hover is live for one frame.
     *
     * MUTATION: widen the liveness window in Porcelain_Hover. Red: a stale
     * hover keeps answering.
     */
    fence(porcelain);
    fence(porcelain);
    fence(porcelain);
    CHECK(!Porcelain_Hover(porcelain, &elsewhere),
          "a hover nothing has restated is not a hover");
    Porcelain_Close(porcelain);
}

static int g_overlay_calls;

static bool
overlay_caption(struct ToriRS_Api* api, void* user, struct ToriRS_ScriptEvent const* event)
{
    (void)api;
    (void)user;
    (void)event;
    g_overlay_calls++;
    return true;
}

/*
 * The suppress-then-format latch, in three states.
 *
 * While the lane's own captions do not yet carry the plugin's fields the
 * natives are hidden and the plugin's stand-ins are what is seen; the FIRST
 * callback is the handoff, and from there the natives are handed back to
 * their own visibility. A lane with no script VM never raises the callback at
 * all, and an overlay that sat suppressing there would hide captions it was
 * never going to replace.
 *
 * MUTATION: start the latch in FORMATTING. Red: nothing is ever suppressed.
 */
static void
test_native_overlay_latch(void)
{
    struct Porcelain* porcelain;
    struct ToriRS_ScriptEvent event;
    struct PorcelainFinding findings[4];

    Testbed_Reset();
    snprintf(g_testbed.capabilities, sizeof(g_testbed.capabilities), "cs2_scripts");
    for( int i = 0; i < 3; i++ )
    {
        char role[64];
        snprintf(role, sizeof(role), "ground_item_labels#%d", i);
        Testbed_DeclareElement(role, 100, 100 + i * 12, 80, 12);
        Testbed_BindElement(role);
    }
    g_overlay_calls = 0;
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_NativeOverlay(porcelain, "ground_item_labels", "groundItemCaption",
                            overlay_caption, NULL);
    CHECK(Porcelain_NativeOverlayState(porcelain) == PORCELAIN_NATIVE_OVERLAY_SUPPRESSING,
          "a lane with the hook starts by suppressing the natives");
    Testbed_ClearLog();
    fence(porcelain);
    CHECK(Testbed_LogCountWith("set_hidden ground_item_labels") == 3,
          "every native label the role matches is hidden");

    memset(&event, 0, sizeof(event));
    event.name = "groundItemCaption";
    Testbed_ClearLog();
    Porcelain_NoteScript(porcelain, &event);
    CHECK(Porcelain_NativeOverlayState(porcelain) == PORCELAIN_NATIVE_OVERLAY_FORMATTING,
          "the first callback is the handoff");
    CHECK(Testbed_LogCountWith("reset ground_item_labels") == 3,
          "which hands every suppressed native back to its own visibility");
    CHECK(g_overlay_calls == 1, "and routes the caption to the plugin");
    Testbed_ClearLog();
    fence(porcelain);
    CHECK(Testbed_LogCountWith("set_hidden") == 0, "nothing is suppressed after the handoff");
    Porcelain_Close(porcelain);

    /*
     * A handle that CLOSES while it is still suppressing hands the natives
     * back itself: a plugin stopped before the cache's captions ever carried
     * its fields must not leave them hidden.
     *
     * MUTATION: delete the Porcelain_OverlayRelease call in Porcelain_Close.
     * Red: the labels stay hidden after the plugin is gone.
     */
    Testbed_Reset();
    snprintf(g_testbed.capabilities, sizeof(g_testbed.capabilities), "cs2_scripts");
    for( int i = 0; i < 3; i++ )
    {
        char role[64];
        snprintf(role, sizeof(role), "ground_item_labels#%d", i);
        Testbed_DeclareElement(role, 100, 100 + i * 12, 80, 12);
        Testbed_BindElement(role);
    }
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_NativeOverlay(porcelain, "ground_item_labels", "groundItemCaption",
                            overlay_caption, NULL);
    fence(porcelain);
    Testbed_ClearLog();
    Porcelain_Close(porcelain);
    CHECK(Testbed_LogCountWith("reset ground_item_labels") == 3,
          "closing while suppressing hands every native back");

    /*
     * A lane without the hook.
     *
     * MUTATION: delete the capability gate in Porcelain_NativeOverlay. Red:
     * the latch suppresses natives on a lane whose callback never fires, and
     * nothing reports it.
     */
    Testbed_Reset();
    for( int i = 0; i < 3; i++ )
    {
        char role[64];
        snprintf(role, sizeof(role), "ground_item_labels#%d", i);
        Testbed_DeclareElement(role, 100, 100 + i * 12, 80, 12);
        Testbed_BindElement(role);
    }
    g_overlay_calls = 0;
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_NativeOverlay(porcelain, "ground_item_labels", "groundItemCaption",
                            overlay_caption, NULL);
    CHECK(Porcelain_NativeOverlayState(porcelain) == PORCELAIN_NATIVE_OVERLAY_ABSENT,
          "a lane with no script VM answers ABSENT");
    CHECK(Porcelain_Findings(porcelain, findings, 4) == 1, "with one finding");
    CHECK(findings[0].result == PORCELAIN_FINDING_UNSUPPORTED, "an unsupported capability");
    Testbed_ClearLog();
    fence(porcelain);
    CHECK(Testbed_LogCountWith("set_hidden") == 0, "and nothing native is touched");
    memset(&event, 0, sizeof(event));
    event.name = "groundItemCaption";
    Porcelain_NoteScript(porcelain, &event);
    CHECK(g_overlay_calls == 0, "the callback never fires there");
    Porcelain_Close(porcelain);
}

static int g_parse_calls;
static char g_parsed[64];

static bool
table_parse(struct ToriRS_Api* api, void* user, void const* data, size_t size)
{
    (void)api;
    (void)user;
    g_parse_calls++;
    snprintf(g_parsed, sizeof(g_parsed), "%.*s", (int)size, (char const*)data);
    return true;
}

static bool
table_refuse(struct ToriRS_Api* api, void* user, void const* data, size_t size)
{
    (void)api;
    (void)user;
    (void)data;
    (void)size;
    g_parse_calls++;
    return false;
}

/*
 * A shipped table is read once, parsed once, and released.
 *
 * Two shipped overlays each opened prices.txt and each held its bytes for the
 * life of the process.
 *
 * MUTATION: delete the `slot->parsed` early return. Red: parsed on every
 * call.
 */
static void
test_table_is_read_once(void)
{
    struct Porcelain* porcelain;
    struct PorcelainFinding findings[8];

    Testbed_Reset();
    Testbed_DeclareFile("prices.txt", "995=1");
    g_parse_calls = 0;
    g_parsed[0] = '\0';
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    CHECK(Porcelain_Table(porcelain, "prices.txt", table_parse, NULL), "a shipped table parses");
    CHECK(g_parse_calls == 1, "once");
    CHECK(strcmp(g_parsed, "995=1") == 0, "over the file's own bytes");
    /* MUTATION: drop the assets.release call. Red: the bytes stay resident. */
    CHECK(!Testbed_AssetHeld("prices.txt"), "and the bytes are released, not held");
    CHECK(Porcelain_Table(porcelain, "prices.txt", table_parse, NULL), "asking again succeeds");
    CHECK(g_parse_calls == 1, "without a second parse");

    /* Absent: one finding, and it is remembered. */
    CHECK(!Porcelain_Table(porcelain, "nowhere.txt", table_parse, NULL),
          "a table this build does not ship is not available");
    CHECK(!Porcelain_Table(porcelain, "nowhere.txt", table_parse, NULL), "and stays so");
    CHECK(Porcelain_Findings(porcelain, findings, 8) == 1, "with exactly one finding");
    CHECK(findings[0].result == PORCELAIN_FINDING_ASSET_MISSING, "naming the absence");
    Porcelain_Close(porcelain);

    /* A parse that refuses the bytes is a finding too, and terminal. */
    Testbed_Reset();
    Testbed_DeclareFile("bonuses.txt", "junk");
    g_parse_calls = 0;
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    CHECK(!Porcelain_Table(porcelain, "bonuses.txt", table_refuse, NULL),
          "a parse that refuses the bytes fails the table");
    CHECK(!Testbed_AssetHeld("bonuses.txt"), "the bytes are released either way");
    CHECK(!Porcelain_Table(porcelain, "bonuses.txt", table_refuse, NULL), "and it is terminal");
    CHECK(g_parse_calls == 1, "so the refusing parse runs once");
    CHECK(Porcelain_Findings(porcelain, findings, 8) == 1, "with one finding");
    Porcelain_Close(porcelain);
}

/*
 * One announcement per (kind, subject) per frame.
 *
 * A stack of twelve bones landing is twelve spawn events and one line; the
 * highlight announcement and the tier announcement about the same obj are two
 * different things and stay two lines.
 *
 * MUTATION: drop the (kind, subject) compare in Porcelain_Notify. Red: the
 * repeat is announced again.
 */
static void
test_notify_coalesces_per_subject(void)
{
    struct Porcelain* porcelain;
    struct PorcelainFinding findings[4];

    Testbed_Reset();
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Notify(porcelain, "drop", 995, "drop: bones");
    CHECK(g_testbed.notify_count == 1, "an announcement reaches the player");
    Porcelain_Notify(porcelain, "drop", 995, "drop: bones");
    CHECK(g_testbed.notify_count == 1, "and the same one again in the same frame does not");
    Porcelain_Notify(porcelain, "drop", 996, "drop: coins");
    CHECK(g_testbed.notify_count == 2, "a different subject is a different line");
    Porcelain_Notify(porcelain, "highlight", 995, "highlighted drop: bones");
    CHECK(g_testbed.notify_count == 3, "and so is a different kind about the same subject");
    fence(porcelain);
    Porcelain_Notify(porcelain, "drop", 995, "drop: bones");
    CHECK(g_testbed.notify_count == 4, "a later frame re-arms it");
    Porcelain_Close(porcelain);

    /*
     * A host with no notifier at all.
     *
     * MUTATION: call api->core.notify without testing it. Red: a crash rather
     * than a finding.
     */
    Testbed_Reset();
    g_testbed.api.core.notify = NULL;
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Notify(porcelain, "drop", 995, "drop: bones");
    CHECK(Porcelain_Findings(porcelain, findings, 4) == 1,
          "a host with no notifier answers one finding");
    CHECK(findings[0].result == PORCELAIN_FINDING_UNSUPPORTED, "an unsupported route");
    Porcelain_Close(porcelain);
}

/* ------------------------------------------------------------------------ */
/* The gaps two ports found                                                 */
/* ------------------------------------------------------------------------ */

static void
late_image_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    struct PorcelainItem item;

    (void)user;
    memset(&item, 0, sizeof(item));
    item.key = "camera";
    item.image = "camera.png";
    item.place.kind = PORCELAIN_REPLACE;
    item.place.on = PORCELAIN_EL(REPORT_BUTTON);
    item.w = 20;
    item.h = 20;
    describe->control(describe, &item);
}

/*
 * A picture that was not ready when the control was made still arrives.
 *
 * The property pass resolves an image only when the item is fresh or the NAME
 * changed, and an asset landing changes neither -- so the control kept the
 * blank it was created with for the whole session, and both the reference
 * plugin here and the shipped screenshot port worked round it by refusing to
 * describe at all until the asset was READY. Every port would have hit it.
 *
 * MUTATION: delete the porcelain_refresh_image call in the reconcile. Red:
 * the picture never reaches the control.
 */
static void
test_a_late_picture_still_arrives(void)
{
    struct Porcelain* porcelain;
    struct TestbedControl const* camera;

    Testbed_Reset();
    Testbed_DeclareElement("report_button", 400, 470, 60, 20);
    Testbed_BindElement("report_button");
    Testbed_DeclareAsset("camera.png", TORIRS_ASSET_PENDING);
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, late_image_describe, NULL);
    fence(porcelain);
    camera = Testbed_Control("camera");
    CHECK(camera && camera->live, "the control exists while its picture is still loading");

    Testbed_LandAsset("camera.png");
    fence(porcelain);
    fence(porcelain);
    camera = Testbed_Control("camera");
    CHECK(camera && camera->live, "and it is still there once the asset lands");
    CHECK(Testbed_LogCountWith("set_image camera") >= 1, "with the picture written to it");
    CHECK(camera->image.value != 0, "so the control is not blank for ever");

    /* And the settled description still costs nothing: a picture that has
     * landed is never re-asked. */
    Porcelain_CountersReset(porcelain);
    fence(porcelain);
    fence(porcelain);
    {
        struct PorcelainCounters counters;
        Porcelain_CountersRead(porcelain, &counters);
        CHECK(counters.setters == 0, "a landed picture is not re-written every fence");
    }
    Porcelain_Close(porcelain);
}

static int g_natural_w;
static int g_natural_h;

static void
natural_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    struct PorcelainItem item;

    (void)user;
    memset(&item, 0, sizeof(item));
    item.key = "camera";
    item.image = "camera.png";
    item.place.kind = PORCELAIN_REPLACE;
    item.place.on = PORCELAIN_EL(REPORT_BUTTON);
    item.w = g_natural_w;
    item.h = g_natural_h;
    describe->control(describe, &item);
}

/*
 * Zero width is the PICTURE's size, which is what the field is documented to
 * mean. It used to read the TARGET's, so a w=0 camera over an 80x22 report
 * button was stretched to 80x22 -- and the only way round it was to ask
 * api->assets for the size of the handle this layer had just handed out.
 *
 * MUTATION: drop the Porcelain_ImageSize arm in porcelain_place_box. Red: the
 * control takes the report button's size.
 */
static void
test_zero_size_is_the_pictures_own(void)
{
    struct Porcelain* porcelain;
    struct TestbedControl const* camera;
    int width = 0;
    int height = 0;

    Testbed_Reset();
    Testbed_DeclareElement("report_button", 400, 470, 80, 22);
    Testbed_BindElement("report_button");
    Testbed_DeclareImage("camera.png", TORIRS_ASSET_READY, 26, 23);
    g_natural_w = 0;
    g_natural_h = 0;
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, natural_describe, NULL);
    fence(porcelain);
    camera = Testbed_Control("camera");
    CHECK(camera && camera->width == 26 && camera->height == 23,
          "a stated size of zero takes the picture's own");
    CHECK(Porcelain_ImageSize(porcelain, "camera.png", &width, &height),
          "and the size is askable without reaching past the layer");
    CHECK(width == 26 && height == 23, "at the same numbers");
    Porcelain_Close(porcelain);

    /* An explicit size still wins, and a control with no picture still falls
     * back to what it stands in for. */
    Testbed_Reset();
    Testbed_DeclareElement("report_button", 400, 470, 80, 22);
    Testbed_BindElement("report_button");
    Testbed_DeclareImage("camera.png", TORIRS_ASSET_READY, 26, 23);
    g_natural_w = 40;
    g_natural_h = 40;
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, natural_describe, NULL);
    fence(porcelain);
    camera = Testbed_Control("camera");
    CHECK(camera && camera->width == 40, "a stated size is still the stated size");
    Porcelain_Close(porcelain);
}

static void
absent_report_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    struct PorcelainElementState state;
    struct PorcelainItem item;

    (void)user;
    /* Something of this lane's interface HAS resolved: until one element
     * does, nothing is absent, only early. */
    (void)Porcelain_Element(describe->porcelain, PORCELAIN_EL(CHAT_BAR), &state);
    if( !Porcelain_Element(describe->porcelain, PORCELAIN_EL(REPORT_BUTTON), &state) )
        return;
    memset(&item, 0, sizeof(item));
    item.key = "camera";
    item.image = "camera.png";
    item.place.kind = PORCELAIN_REPLACE;
    item.place.on = PORCELAIN_EL(REPORT_BUTTON);
    item.w = 20;
    item.h = 20;
    describe->control(describe, &item);
}

/*
 * A lane limitation can be declared where it is FOUND.
 *
 * ExpectAbsent was on_start-only, but at on_start every element is PENDING --
 * so the only declaration a plugin could make was an unconditional one, and
 * an unconditional declaration failed every lane that HAS the element with
 * ABSENT_UNEXPECTEDLY_PRESENT. The shipped screenshot port could not call it
 * at all. Declaring at the fence re-labels the absence already reported.
 *
 * MUTATION: delete the porcelain_relabel_absence call. Red: the absence stays
 * an unexpected finding and the clean gate still fails.
 */
static void
test_an_absence_can_be_declared_when_it_is_found(void)
{
    struct Porcelain* porcelain;
    struct PorcelainFinding findings[8];
    struct PorcelainElementState state;

    Testbed_Reset();
    Testbed_DeclareElement("chat_bar", 0, 460, 500, 22);
    Testbed_BindElement("chat_bar");
    Testbed_DeclareAsset("camera.png", TORIRS_ASSET_READY);
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, absent_report_describe, NULL);
    fence(porcelain);
    fence(porcelain);
    fence(porcelain);
    CHECK(!Porcelain_Element(porcelain, PORCELAIN_EL(REPORT_BUTTON), &state),
          "this lane has no report button");
    CHECK(Porcelain_Findings(porcelain, findings, 8) == 1, "and says so, once");
    CHECK(!findings[0].expected, "unexpected until the plugin declares it");

    Porcelain_ExpectAbsent(porcelain, PORCELAIN_EL(REPORT_BUTTON), "no report button here");
    CHECK(Porcelain_Findings(porcelain, findings, 8) == 1, "the declaration adds no finding");
    CHECK(findings[0].expected, "it marks the one already recorded");
    CHECK(findings[0].result == PORCELAIN_FINDING_ABSENT_EXPECTED, "as a declared absence");
    Porcelain_Close(porcelain);

    /* The other direction still bites: a declaration on a lane that HAS the
     * element is a stale declaration, and stays loud. */
    Testbed_Reset();
    Testbed_DeclareElement("report_button", 400, 470, 60, 20);
    Testbed_BindElement("report_button");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_ExpectAbsent(porcelain, PORCELAIN_EL(REPORT_BUTTON), "stale");
    (void)Porcelain_Element(porcelain, PORCELAIN_EL(REPORT_BUTTON), &state);
    fence(porcelain);
    CHECK(Porcelain_Findings(porcelain, findings, 8) == 1, "a stale declaration is a finding");
    CHECK(findings[0].result == PORCELAIN_FINDING_ABSENT_UNEXPECTEDLY_PRESENT,
          "and it fails loudly");
    Porcelain_Close(porcelain);
}

/*
 * A declared limitation that is not an element absence.
 *
 * Every UNSUPPORTED finding was expected=0, so a plugin that honestly said
 * "this lane cannot measure a string" FAILED the clean gate for saying it,
 * and going quiet was the only way to pass. Two ports hit this independently.
 *
 * MUTATION: drop the porcelain_expected_unsupported arm in
 * Porcelain_RecordFinding. Red: the declared limitation is unexpected.
 */
static void
test_a_limitation_can_be_declared(void)
{
    struct Porcelain* porcelain;
    struct PorcelainFinding findings[8];
    int count;

    Testbed_Reset();
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_ExpectUnsupported(porcelain, "text measure", "no verb measures a string");
    count = Porcelain_Findings(porcelain, findings, 8);
    CHECK(count == 1, "the declaration is itself one finding -- the point is that it shows");
    CHECK(findings[0].result == PORCELAIN_FINDING_UNSUPPORTED, "an unsupported feature");
    CHECK(findings[0].expected, "and it is expected, so the clean gate still passes");

    /* And it covers a later refusal through a different route: Require
     * records verb=require, the describe builder records verb=unsupported,
     * and both carry the feature name as the detail. */
    CHECK(!Porcelain_Require(porcelain, "text_measure_cap", "text measure"),
          "a capability this lane does not answer still turns the feature off");
    count = Porcelain_Findings(porcelain, findings, 8);
    CHECK(count == 2, "with its own finding");
    for( int i = 0; i < count; i++ )
        CHECK(findings[i].expected, "and every one of them is declared");
    Porcelain_Close(porcelain);
}

static void
within_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    struct PorcelainItem item;

    memset(&item, 0, sizeof(item));
    item.key = "corner";
    item.image = "camera.png";
    item.place.kind = *(int*)user ? PORCELAIN_WITHIN : PORCELAIN_INSIDE;
    item.place.on = PORCELAIN_EL(VIEWPORT);
    item.place.corner_or_side = PORCELAIN_BOTTOM_RIGHT;
    item.place.dx = 4;
    item.place.dy = 4;
    item.w = 20;
    item.h = 20;
    describe->control(describe, &item);
}

/*
 * WITHIN is a child of the element with NO anchor.
 *
 * INSIDE makes a sibling under the target's parent and anchors it OVER, which
 * REPLACE needs and a corner ornament does not. One live anchor is what makes
 * UITree_FrameHasDepth true, and the ledger prices that at 13.5 ms a frame on
 * osrs239 -- so a corner camera that cost no anchor before the layer existed
 * paid one for going through it.
 *
 * MUTATION: delete the WITHIN arm in porcelain_apply_anchor. Red: the child
 * takes an anchor.
 */
static void
test_within_is_a_child_with_no_anchor(void)
{
    struct Porcelain* porcelain;
    struct TestbedControl const* corner;
    int within;

    Testbed_Reset();
    Testbed_DeclareElement("viewport", 4, 4, 512, 334);
    Testbed_BindElement("viewport");
    Testbed_DeclareAsset("camera.png", TORIRS_ASSET_READY);
    within = 0;
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, within_describe, &within);
    fence(porcelain);
    corner = Testbed_Control("corner");
    CHECK(corner && corner->live, "INSIDE places a corner ornament");
    CHECK(Testbed_LogCountWith("set_anchor corner") == 1, "and pays an anchor for it");
    CHECK(corner->x == 4 + 512 - 20 - 4, "in the target's PARENT's coordinates");
    Porcelain_Close(porcelain);

    Testbed_Reset();
    Testbed_DeclareElement("viewport", 4, 4, 512, 334);
    Testbed_BindElement("viewport");
    Testbed_DeclareAsset("camera.png", TORIRS_ASSET_READY);
    within = 1;
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, within_describe, &within);
    fence(porcelain);
    corner = Testbed_Control("corner");
    CHECK(corner && corner->live, "WITHIN places the same ornament");
    CHECK(Testbed_LogCountWith("set_anchor corner") == 0, "and pays NO anchor");
    CHECK(ToriRS_WidgetRefEqual(corner->parent, Testbed_Element("viewport")->ref),
          "because the element itself is the parent");
    CHECK(corner->x == 512 - 20 - 4, "so its coordinates are the element's own");
    Porcelain_Close(porcelain);
}

static void
readout_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    struct PorcelainItem item;

    (void)user;
    memset(&item, 0, sizeof(item));
    item.key = "fps";
    item.place.kind = PORCELAIN_INSIDE;
    item.place.on = PORCELAIN_EL(VIEWPORT);
    item.place.corner_or_side = PORCELAIN_TOP_LEFT;
    item.w = 60;
    item.h = 14;
    item.text = "--";
    item.rgb = 0xffff00u;
    describe->text(describe, &item);
}

/*
 * The direct path carries a STRING.
 *
 * A per-frame readout IS a string that changes every frame, and the motion
 * struct carried x, y, opacity and an image -- so moving a number meant
 * describing and invalidating, a whole reconcile pass to write four
 * characters.
 *
 * MUTATION: delete the PORCELAIN_MOTION_TEXT arm in Porcelain_Set. Red: the
 * text never moves.
 */
static void
test_the_direct_path_carries_text(void)
{
    struct Porcelain* porcelain;
    struct PorcelainMotion motion;
    struct TestbedControl const* fps;
    struct PorcelainCounters counters;

    Testbed_Reset();
    Testbed_DeclareElement("viewport", 4, 4, 512, 334);
    Testbed_BindElement("viewport");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, readout_describe, NULL);
    fence(porcelain);
    fps = Testbed_Control("fps");
    CHECK(fps && strcmp(fps->text, "--") == 0, "the described readout starts at its placeholder");

    memset(&motion, 0, sizeof(motion));
    motion.mask = PORCELAIN_MOTION_TEXT | PORCELAIN_MOTION_RGB;
    motion.text = "61 fps";
    motion.rgb = 0x00ff00u;
    CHECK(Porcelain_Set(porcelain, "fps", &motion) == TORIRS_RESULT_OK, "the direct path takes it");
    fps = Testbed_Control("fps");
    CHECK(strcmp(fps->text, "61 fps") == 0, "and the string reaches the control");

    /* The same string twice is not a second setter, and the next fence does
     * not undo it: the applied item carries what the direct path wrote. */
    Porcelain_CountersReset(porcelain);
    CHECK(Porcelain_Set(porcelain, "fps", &motion) == TORIRS_RESULT_OK, "restating is legal");
    fence(porcelain);
    Porcelain_CountersRead(porcelain, &counters);
    CHECK(counters.setters == 0, "and costs nothing");
    CHECK(strcmp(Testbed_Control("fps")->text, "61 fps") == 0,
          "and the fence does not put the placeholder back");
    Porcelain_Close(porcelain);
}

static int g_ticks;
static uint64_t g_last_elapsed;

static void
fps_tick(struct ToriRS_Api* api, void* user, uint64_t elapsed_ms)
{
    (void)api;
    (void)user;
    g_ticks++;
    g_last_elapsed = elapsed_ms;
}

/*
 * A clock says how long it has actually been, and re-registering it does not
 * leak a slot.
 *
 * A frames-per-second figure is drawn_delta * 1000 / elapsed; a clock that
 * assumed its nominal interval printed a number wrong by however much the
 * frame budget slipped. And registration was append-only into a sixteen-slot
 * table, so a user dragging a refresh-interval slider leaked a slot per
 * change and then stopped the readout with a budget finding.
 *
 * MUTATION: pass timer->milliseconds instead of the measured elapsed. Red:
 * the tick is told 500 when 900 ms passed.
 * MUTATION: make porcelain_timer_slot always take a free slot. Red: the
 * re-interval is a second timer and the table fills.
 */
static void
test_a_clock_is_measured_and_re_intervalled(void)
{
    struct Porcelain* porcelain;
    struct PorcelainFinding findings[4];

    Testbed_Reset();
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    g_ticks = 0;
    g_last_elapsed = 0;
    Porcelain_EveryMs(porcelain, 500, fps_tick, NULL);
    g_testbed.frame_ms = 1000;
    fence(porcelain);
    CHECK(g_ticks == 1, "the first due tick fires");
    CHECK(g_last_elapsed == 500, "and is told its interval, having no earlier firing to measure");

    /* The frame budget slipped: 900 ms passed, not the 500 that was asked
     * for, and a rate that divided by 500 would be almost double. */
    g_testbed.frame_ms = 1900;
    fence(porcelain);
    CHECK(g_ticks == 2, "the next due tick fires");
    CHECK(g_last_elapsed == 900, "and is told the REAL elapsed time, not the interval");

    /* Seventeen re-registrations into a sixteen-slot table. */
    for( int i = 0; i < 17; i++ )
        Porcelain_EveryMs(porcelain, 100 + i, fps_tick, NULL);
    CHECK(Porcelain_Findings(porcelain, findings, 4) == 0,
          "re-registering the same handler re-intervals it rather than leaking a slot");
    g_testbed.frame_ms = 3000;
    fence(porcelain);
    CHECK(g_ticks == 3, "and there is still exactly one timer");

    Porcelain_CancelEvery(porcelain, fps_tick, NULL);
    g_testbed.frame_ms = 9000;
    fence(porcelain);
    CHECK(g_ticks == 3, "a cancelled clock stops");
    Porcelain_Close(porcelain);
}

/* ------------------------------------------------------------------------ */
/* Panels: the row model                                                    */
/* ------------------------------------------------------------------------ */

/*
 * One fixture drives every panel case. The row SET it describes is fixed --
 * heading, select, button, well -- unless a field asks for more, so a case
 * that changes one property is changing exactly one thing and the rebuild
 * counter is a real answer rather than a coincidence.
 */
struct PanelFixture
{
    char const* heading;
    char const* select_label;
    char const* selected;
    int option_count;
    char const* caption;
    bool button_disabled;
    int well_height;
    uint64_t well_hit_key;

    /* Shapes a case asks for. */
    uint64_t well_paint_key;
    bool extra_row;
    bool rename_heading;
    bool force_reidentify;
    bool reidentify_unknown;
    bool duplicate_key;
    bool long_option_value;
    int flood_rows;

    /* What the handlers saw. */
    int actions;
    int action_kind;
    int action_value;
    char action_text[64];
    char action_key[64];
    int paints;
};

static char const* const PANEL_OPTION_VALUES[5] = {"auto", "classic", "modern", "stone", "native"};
static char PANEL_LONG_VALUE[PORCELAIN_OPTION_VALUE_MAX + 8];
static char PANEL_FLOOD_KEYS[PORCELAIN_ROWS_MAX + 4][8];

static void
panel_fixture_action(struct ToriRS_Api* api, void* user, struct PorcelainRowAction const* action)
{
    struct PanelFixture* fixture = user;
    (void)api;
    fixture->actions++;
    fixture->action_kind = action->kind;
    fixture->action_value = action->value;
    snprintf(fixture->action_text, sizeof(fixture->action_text), "%s", action->text);
    snprintf(fixture->action_key, sizeof(fixture->action_key), "%s", action->key);
}

static void
panel_fixture_paint(struct ToriRS_Api* api, void* user, char const* key,
                    struct ToriRS_Graphics* draw)
{
    struct PanelFixture* fixture = user;
    (void)api;
    (void)key;
    (void)draw;
    fixture->paints++;
}

static void
panel_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    struct PanelFixture* fixture = user;
    struct ToriRS_SelectOption options[5];
    struct PorcelainRow row;

    if( fixture->flood_rows > 0 )
    {
        for( int i = 0; i < fixture->flood_rows; i++ )
        {
            snprintf(PANEL_FLOOD_KEYS[i], sizeof(PANEL_FLOOD_KEYS[i]), "r%d", i);
            memset(&row, 0, sizeof(row));
            row.key = PANEL_FLOOD_KEYS[i];
            row.kind = PORCELAIN_ROW_LABEL;
            row.text = "flood";
            describe->row(describe, &row);
        }
        return;
    }

    memset(&row, 0, sizeof(row));
    row.key = "head";
    row.kind = PORCELAIN_ROW_HEADING;
    row.label = fixture->rename_heading ? "Renamed" : fixture->heading;
    describe->row(describe, &row);

    for( int i = 0; i < fixture->option_count; i++ )
    {
        memset(&options[i], 0, sizeof(options[i]));
        options[i].struct_size = sizeof(options[i]);
        options[i].value = (fixture->long_option_value && i == 0) ? PANEL_LONG_VALUE
                                                                 : PANEL_OPTION_VALUES[i];
        options[i].label = PANEL_OPTION_VALUES[i];
        options[i].enabled = true;
    }
    memset(&row, 0, sizeof(row));
    row.key = "frame";
    row.kind = PORCELAIN_ROW_SELECT;
    row.label = fixture->select_label;
    row.text = fixture->selected;
    row.options = options;
    row.option_count = fixture->option_count;
    row.on_action = panel_fixture_action;
    row.user = fixture;
    describe->row(describe, &row);

    memset(&row, 0, sizeof(row));
    row.key = "pause";
    row.kind = PORCELAIN_ROW_BUTTON;
    row.label = fixture->caption;
    row.disabled = fixture->button_disabled;
    row.on_action = panel_fixture_action;
    row.user = fixture;
    describe->row(describe, &row);

    memset(&row, 0, sizeof(row));
    row.key = "boxes";
    row.kind = PORCELAIN_ROW_CUSTOM;
    row.height = fixture->well_height;
    row.hit_key = fixture->well_hit_key;
    row.paint_key = fixture->well_paint_key;
    row.paint = panel_fixture_paint;
    row.on_action = panel_fixture_action;
    row.user = fixture;
    describe->row(describe, &row);

    if( fixture->extra_row )
    {
        memset(&row, 0, sizeof(row));
        row.key = "detail";
        row.kind = PORCELAIN_ROW_KEY_VALUE;
        row.label = "Kills";
        row.text = "7";
        describe->row(describe, &row);
    }
    if( fixture->duplicate_key )
    {
        memset(&row, 0, sizeof(row));
        row.key = "head";
        row.kind = PORCELAIN_ROW_LABEL;
        row.text = "twice";
        describe->row(describe, &row);
    }
    if( fixture->force_reidentify )
        describe->reidentify(describe, "boxes");
    if( fixture->reidentify_unknown )
        describe->reidentify(describe, "nothing_here");
}

static void
panel_fixture_init(struct PanelFixture* fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->heading = "Rendering";
    fixture->select_label = "Gameframe";
    fixture->selected = "auto";
    fixture->option_count = 3;
    fixture->caption = "Pause";
    fixture->well_height = 120;
    fixture->well_hit_key = 0x1111;
    fixture->well_paint_key = 0x9001;
}

/**
 * The declared row, or a zeroed stand-in.
 *
 * Only so that a rule that BROKE reads as a failed check rather than as a
 * crash in the assertion that was about to report it. Nothing here tolerates
 * an absent row in the library: `Testbed_PanelRow` itself still answers NULL
 * and the cases that care assert on it directly.
 */
static struct TestbedPanelRow*
panel_row(char const* id)
{
    static struct TestbedPanelRow absent;
    struct TestbedPanelRow* row = Testbed_PanelRow(id);
    if( row )
        return row;
    memset(&absent, 0, sizeof(absent));
    return &absent;
}

/** Open, register, describe, fence and open the page: the settled start. */
static struct Porcelain*
panel_start(struct PanelFixture* fixture, unsigned faces)
{
    struct Porcelain* porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Panel(porcelain, "panel_icon.png", 320, faces);
    Porcelain_Describe(porcelain, panel_describe, fixture);
    fence(porcelain);
    Testbed_PanelBuild(porcelain, TORIRS_PANEL_VIEW_PAGE);
    return porcelain;
}

/** Restate the description and reconcile it, as a config change would. */
static void
panel_restate(struct Porcelain* porcelain)
{
    Porcelain_Invalidate(porcelain);
    fence(porcelain);
}

static int
panel_findings_with(struct Porcelain* porcelain, int result)
{
    struct PorcelainFinding found[PORCELAIN_FINDINGS_MAX];
    int const count = Porcelain_Findings(porcelain, found, PORCELAIN_FINDINGS_MAX);
    int matching = 0;
    for( int i = 0; i < count; i++ )
        if( found[i].result == result )
            matching++;
    return matching;
}

/*
 * MUTATION: in Porcelain_Panel, pass `panel->icon` unconditionally as
 * descriptor.icon_asset -- "panel_request  320" replaces the NULL spelling and
 * the baked-wrench assertion goes red.
 * MUTATION: register twice -- the assert(!porcelain->panel) fires.
 */
static void
test_panel_registers_once(void)
{
    struct PanelFixture fixture;
    struct Porcelain* porcelain;

    Testbed_Reset();
    panel_fixture_init(&fixture);
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Panel(porcelain, "panel_icon.png", 320, PORCELAIN_FACE_BOTH);
    CHECK(g_testbed.panel_requests == 1, "Porcelain_Panel registers the shared pane exactly once");
    CHECK(Testbed_LogCountWith("panel_request panel_icon.png 320") == 1,
          "with the icon and the width it was given");
    Porcelain_Close(porcelain);

    Testbed_Reset();
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Panel(porcelain, NULL, 320, PORCELAIN_FACE_BOTH);
    CHECK(Testbed_LogCountWith("panel_request - 320") == 1,
          "a NULL icon asks for the baked wrench, which is a meaning and not an absence");
    Porcelain_Close(porcelain);
}

/*
 * MUTATION: drop the `if( !(panel->faces & panel_face_of_view(view)) ) return`
 * in Porcelain_PanelBuild -- the settings face declares the page's four rows
 * and "a face the description does not cover declares nothing" goes red.
 */
static void
test_panel_faces(void)
{
    struct PanelFixture fixture;
    struct Porcelain* porcelain;
    char first_face[8][64];
    int first_count;

    /* PAGE only: the settings face is left to the generated form. */
    Testbed_Reset();
    panel_fixture_init(&fixture);
    porcelain = panel_start(&fixture, PORCELAIN_FACE_PAGE);
    CHECK(Testbed_PanelRowCount() == 4, "the page face declares the described rows");
    Testbed_PanelBuild(porcelain, TORIRS_PANEL_VIEW_SETTINGS);
    CHECK(Testbed_PanelRowCount() == 0,
          "a face the description does not cover declares nothing");
    /* And no setter chases the rows that are no longer there. */
    panel_restate(porcelain);
    CHECK(g_testbed.panel_orphan_setters == 0,
          "and no setter is aimed at a page the host is no longer holding");
    CHECK(g_testbed.panel_invalidates == 0,
          "nor is a rebuild asked for on a face that is not ours");
    Porcelain_Close(porcelain);

    /* BOTH: the same key sequence on either face. */
    Testbed_Reset();
    panel_fixture_init(&fixture);
    porcelain = panel_start(&fixture, PORCELAIN_FACE_BOTH);
    first_count = Testbed_PanelRowCount();
    for( int i = 0; i < first_count; i++ )
        snprintf(first_face[i], sizeof(first_face[i]), "%s", Testbed_PanelRowAt(i)->id);
    Testbed_PanelBuild(porcelain, TORIRS_PANEL_VIEW_SETTINGS);
    CHECK(Testbed_PanelRowCount() == first_count,
          "faces = PAGE | SETTINGS is the same description on both faces");
    for( int i = 0; i < first_count && i < Testbed_PanelRowCount(); i++ )
        CHECK(strcmp(first_face[i], Testbed_PanelRowAt(i)->id) == 0,
              "in the same key order");
    Porcelain_Close(porcelain);
}

/*
 * MUTATION: delete the `if( wanted->properties == have->properties ) continue`
 * early-out -- row_applies rises to four and "an unchanged description walks
 * no row's properties" goes red while the setter counts stay zero, which is
 * precisely the difference the counter exists to show.
 */
static void
test_panel_steady_state_costs_nothing(void)
{
    struct PanelFixture fixture;
    struct PorcelainPanelCounters counters;
    struct Porcelain* porcelain;

    Testbed_Reset();
    panel_fixture_init(&fixture);
    porcelain = panel_start(&fixture, PORCELAIN_FACE_BOTH);
    Testbed_ClearLog();
    Porcelain_CountersReset(porcelain);

    for( int i = 0; i < 4; i++ )
        panel_restate(porcelain);

    Porcelain_PanelCountersRead(porcelain, &counters);
    CHECK(counters.setters == 0, "an unchanged description makes no panel setter call");
    CHECK(counters.rebuilds == 0, "and asks for no rebuild");
    CHECK(counters.reidentifies == 0, "and mints no identity");
    CHECK(counters.row_applies == 0, "an unchanged description walks no row's properties");
    CHECK(Testbed_LogCountWith("panel_") == 0, "nothing at all reaches the panel API");
    Porcelain_Close(porcelain);
}

/*
 * The host fix H1, from the plugin's side. Before it, panel.set_text on a
 * BUTTON returned OK and applied nothing, so Pause never became Unpause.
 *
 * MUTATION: put PORCELAIN_ROW_BUTTON into panel_label_is_identity -- the
 * caption becomes declaration identity, one rebuild is counted and "a changed
 * caption is a setter" goes red.
 * MUTATION: drop the `if( text_moved )` guard in the BUTTON arm -- the second
 * assertion ("and its availability is not restated") goes red at two setters.
 */
static void
test_panel_caption_is_a_setter(void)
{
    struct PanelFixture fixture;
    struct PorcelainPanelCounters counters;
    struct Porcelain* porcelain;
    uint32_t serial;

    Testbed_Reset();
    panel_fixture_init(&fixture);
    porcelain = panel_start(&fixture, PORCELAIN_FACE_BOTH);
    CHECK(strcmp(panel_row("pause")->text, "Pause") == 0,
          "a button is declared with its caption");
    serial = panel_row("pause")->serial;
    Testbed_ClearLog();
    Porcelain_CountersReset(porcelain);

    fixture.caption = "Unpause";
    panel_restate(porcelain);

    Porcelain_PanelCountersRead(porcelain, &counters);
    CHECK(counters.rebuilds == 0, "a changed caption is a setter, never a rebuild");
    CHECK(counters.setters == 1, "and its availability is not restated alongside it");
    CHECK(Testbed_LogCountWith("panel_set_text pause Unpause") == 1,
          "the caption reaches the row the page already has");
    CHECK(strcmp(panel_row("pause")->text, "Unpause") == 0, "and the row now reads it");
    CHECK(panel_row("pause")->serial == serial,
          "with the row keeping the identity a click was authored against");
    CHECK(panel_row("pause")->declared == 1, "and never being declared a second time");
    Porcelain_Close(porcelain);
}

/*
 * The host fix H2. `enabled` was stored by the builder and read by nobody.
 *
 * MUTATION: make panel_pushed_value return row->value for BUTTON too -- the
 * declared availability is 0 for a live button and "a button is declared
 * available" goes red.
 * MUTATION: drop the `if( value_moved )` guard's else-arm by returning early
 * in the BUTTON case -- "disabling a button is a setter" goes red at zero.
 */
static void
test_panel_button_availability(void)
{
    struct PanelFixture fixture;
    struct PorcelainPanelCounters counters;
    struct Porcelain* porcelain;

    Testbed_Reset();
    panel_fixture_init(&fixture);
    porcelain = panel_start(&fixture, PORCELAIN_FACE_BOTH);
    CHECK(panel_row("pause")->value == 1,
          "a button is declared available unless the row says otherwise");
    Testbed_ClearLog();
    Porcelain_CountersReset(porcelain);

    fixture.button_disabled = true;
    panel_restate(porcelain);

    Porcelain_PanelCountersRead(porcelain, &counters);
    CHECK(counters.setters == 1, "disabling a button is one setter");
    CHECK(counters.rebuilds == 0, "and not a rebuild");
    CHECK(Testbed_LogCountWith("panel_set_value pause 0") == 1,
          "carried as the value the host draws dim from");
    CHECK(Testbed_LogCountWith("panel_set_text pause") == 0,
          "with the caption it did not change left alone");

    /* And a fresh declaration keeps it disabled: the build half and the patch
     * half have to agree, or a rebuild would silently re-arm the command. */
    fixture.extra_row = true;
    panel_restate(porcelain);
    Testbed_PanelBuild(porcelain, TORIRS_PANEL_VIEW_PAGE);
    CHECK(panel_row("pause")->value == 0,
          "and a rebuild declares it disabled rather than re-arming it");
    Porcelain_Close(porcelain);
}

/*
 * The host fix H3. A changed option COUNT used to be refused, and both
 * settings pages answered the refusal by rebuilding the whole page.
 *
 * MUTATION: fold row->options_hash into panel_identity_hash -- the option
 * count becomes declaration identity, one rebuild is counted and "a changed
 * option count is a setter" goes red.
 */
static void
test_panel_option_count_is_a_setter(void)
{
    struct PanelFixture fixture;
    struct PorcelainPanelCounters counters;
    struct Porcelain* porcelain;
    uint32_t serial;

    Testbed_Reset();
    panel_fixture_init(&fixture);
    porcelain = panel_start(&fixture, PORCELAIN_FACE_BOTH);
    CHECK(panel_row("frame")->option_count == 3, "a select is declared with its options");
    serial = panel_row("frame")->serial;
    Testbed_ClearLog();
    Porcelain_CountersReset(porcelain);

    /* A provider became available: the catalogue GREW. */
    fixture.option_count = 4;
    panel_restate(porcelain);

    Porcelain_PanelCountersRead(porcelain, &counters);
    CHECK(counters.rebuilds == 0, "a changed option count is a setter, never a rebuild");
    CHECK(counters.setters == 1, "one setter, and only on the row that changed");
    CHECK(Testbed_LogCountWith("panel_set_options frame auto 4") == 1,
          "carrying the whole list and the selection together");
    CHECK(panel_row("frame")->option_count == 4, "the row now holds four");
    CHECK(panel_row("frame")->serial == serial, "and keeps its identity");

    /* And shrinking is the same answer: a saved-but-unavailable row went. */
    Porcelain_CountersReset(porcelain);
    fixture.option_count = 2;
    fixture.selected = "classic";
    panel_restate(porcelain);
    Porcelain_PanelCountersRead(porcelain, &counters);
    CHECK(counters.rebuilds == 0, "and so is a catalogue that shrank");
    CHECK(panel_row("frame")->option_count == 2, "down to two");
    CHECK(strcmp(panel_row("frame")->selected, "classic") == 0,
          "with the selection the description named");
    Porcelain_Close(porcelain);
}

/*
 * MUTATION: compare only panel->row_count in panel_sequence_matches -- the
 * renamed SELECT no longer rebuilds, the row keeps a label the host cannot
 * restate, and "renaming a select is a rebuild" goes red.
 * MUTATION: add every kind to panel_label_is_identity -- the renamed HEADING
 * rebuilds too and "renaming a heading is a setter" goes red.
 */
static void
test_panel_label_identity_split(void)
{
    struct PanelFixture fixture;
    struct PorcelainPanelCounters counters;
    struct Porcelain* porcelain;

    /* A HEADING's string is patched: the host draws it from a readout that
     * the text half feeds. */
    Testbed_Reset();
    panel_fixture_init(&fixture);
    porcelain = panel_start(&fixture, PORCELAIN_FACE_BOTH);
    Porcelain_CountersReset(porcelain);
    fixture.rename_heading = true;
    panel_restate(porcelain);
    Porcelain_PanelCountersRead(porcelain, &counters);
    CHECK(counters.rebuilds == 0, "renaming a heading is a setter");
    CHECK(strcmp(panel_row("head")->text, "Renamed") == 0, "and the row reads it");
    Porcelain_Close(porcelain);

    /* A SELECT's label is what the host BUILT the row from and its patch path
     * has no arm for: the only honest answer is a rebuild. */
    Testbed_Reset();
    panel_fixture_init(&fixture);
    porcelain = panel_start(&fixture, PORCELAIN_FACE_BOTH);
    Porcelain_CountersReset(porcelain);
    fixture.select_label = "Game frame";
    panel_restate(porcelain);
    Porcelain_PanelCountersRead(porcelain, &counters);
    CHECK(counters.rebuilds == 1, "renaming a select is a rebuild, and is said to be one");
    Testbed_PanelBuild(porcelain, TORIRS_PANEL_VIEW_PAGE);
    CHECK(strcmp(panel_row("frame")->label, "Game frame") == 0,
          "and the fresh declaration carries the new name");
    Porcelain_Close(porcelain);
}

/*
 * MUTATION: return true unconditionally from panel_sequence_matches -- the
 * new row is never declared, every later setter lands on a page that does not
 * hold it, and both the rebuild count and panel_orphan_setters go red.
 */
static void
test_panel_row_set_is_the_one_rebuild(void)
{
    struct PanelFixture fixture;
    struct PorcelainPanelCounters counters;
    struct Porcelain* porcelain;

    Testbed_Reset();
    panel_fixture_init(&fixture);
    porcelain = panel_start(&fixture, PORCELAIN_FACE_BOTH);
    Porcelain_CountersReset(porcelain);

    /* A detail block opened: the page HAS a row it did not have. */
    fixture.extra_row = true;
    panel_restate(porcelain);

    Porcelain_PanelCountersRead(porcelain, &counters);
    CHECK(counters.rebuilds == 1, "a changed row set is the page's one legitimate rebuild");
    CHECK(Testbed_PanelRowCount() == 0, "the host clears the page it was holding");
    Testbed_PanelBuild(porcelain, TORIRS_PANEL_VIEW_PAGE);
    CHECK(Testbed_PanelRowCount() == 5, "and the next declaration carries the new row");
    CHECK(Testbed_PanelRow("detail") != NULL, "which is the one that arrived");

    /* Closing it again is the same answer, and nothing is left dangling. */
    Porcelain_CountersReset(porcelain);
    fixture.extra_row = false;
    panel_restate(porcelain);
    Testbed_PanelBuild(porcelain, TORIRS_PANEL_VIEW_PAGE);
    Porcelain_PanelCountersRead(porcelain, &counters);
    CHECK(counters.rebuilds == 1, "and so is closing it");
    CHECK(Testbed_PanelRow("detail") == NULL, "with the row gone");
    CHECK(g_testbed.panel_orphan_setters == 0,
          "and no setter ever aimed at a row the page does not hold");
    Porcelain_Close(porcelain);
}

/*
 * Host fix H4 from the plugin's side: the well reaches past the 512 that
 * shipped, and growing it is a property of a widget the page already has.
 *
 * MUTATION: put row->height into panel_identity_hash -- growth becomes a
 * rebuild and "growth without a rebuild" goes red.
 */
static void
test_panel_well_grows_without_a_rebuild(void)
{
    struct PanelFixture fixture;
    struct PorcelainPanelCounters counters;
    struct Porcelain* porcelain;
    uint32_t serial;

    Testbed_Reset();
    panel_fixture_init(&fixture);
    porcelain = panel_start(&fixture, PORCELAIN_FACE_BOTH);
    serial = panel_row("boxes")->serial;
    Porcelain_CountersReset(porcelain);
    Testbed_ClearLog();

    /* Overview plus twelve skill boxes: past the ceiling that shipped. */
    fixture.well_height = 650;
    panel_restate(porcelain);

    Porcelain_PanelCountersRead(porcelain, &counters);
    CHECK(counters.rebuilds == 0, "a well that grew is a setter, never a rebuild");
    CHECK(counters.setters == 1, "one setter, on the well alone");
    CHECK(Testbed_LogCountWith("panel_set_height boxes 650") == 1,
          "carrying a height past the 512 that used to clip the tenth box away");
    /* And the ceiling it is carried to is real. Porcelain cannot see the
     * host's constant from its own sources -- it is a plugin-side library --
     * so the pin lives here, where the test can see both. Its twin is
     * TORIRS_CHROME_M_CUSTOM_H_MAX in ui/torirs_chrome_metrics.h; the two
     * were raised together and a well is clipped by whichever is lower --
     * which is why that twin is now pinned EQUAL to this one by a
     * _Static_assert in torirs_plugin_bridge.u.c, the one translation unit
     * that sees both. Until it was, raising one and not the other produced a
     * row the height the plugin asked for and a picture that was short. */
    CHECK(TORIRS_PANEL_CUSTOM_HEIGHT_MAX >= 650,
          "and the host's own ceiling reaches it: 512 stopped the page growing at box ten");
    CHECK(panel_row("boxes")->height == 650, "and the row is that tall");
    CHECK(panel_row("boxes")->serial == serial,
          "with no identity minted: the y-to-item mapping did not move");
    Porcelain_Close(porcelain);
}

/*
 * Host fix H5. The well is ONE control, so a click is arithmetic on the order
 * that was painted; when that order changes the row must take a new input
 * identity, and the page, the scroll and every other row must not.
 *
 * MUTATION: fold row->hit_key into panel_property_hash and delete the
 * hit_key compare -- no identity is minted, the well keeps the serial a stale
 * click was authored against, and "a changed hit key re-identifies" goes red.
 * MUTATION: call api->panel.invalidate instead of reidentify -- the rebuild
 * count and the neighbours' serials both go red.
 */
static void
test_panel_hit_key_reidentifies_one_row(void)
{
    struct PanelFixture fixture;
    struct PorcelainPanelCounters counters;
    struct Porcelain* porcelain;
    uint32_t well_serial;
    uint32_t button_serial;
    uint32_t select_serial;

    Testbed_Reset();
    panel_fixture_init(&fixture);
    porcelain = panel_start(&fixture, PORCELAIN_FACE_BOTH);
    well_serial = panel_row("boxes")->serial;
    button_serial = panel_row("pause")->serial;
    select_serial = panel_row("frame")->serial;
    Porcelain_CountersReset(porcelain);
    Testbed_ClearLog();

    /* A band arrived: every later y means a different source now. */
    fixture.well_hit_key = 0x2222;
    fixture.well_height = 157;
    panel_restate(porcelain);

    Porcelain_PanelCountersRead(porcelain, &counters);
    CHECK(counters.reidentifies == 1, "a changed hit key mints one row a new identity");
    CHECK(counters.rebuilds == 0, "and never a rebuild");
    CHECK(Testbed_LogCountWith("panel_reidentify boxes") == 1, "naming the row that moved");
    CHECK(panel_row("boxes")->serial != well_serial,
          "so a click queued against the old picture is refused");
    CHECK(panel_row("pause")->serial == button_serial,
          "while every other row keeps its identity");
    CHECK(panel_row("frame")->serial == select_serial, "and its retained state");
    CHECK(Testbed_PanelRowCount() == 4, "and the page keeps every row it had");

    /* A readout changing is NOT an identity: this is the half that made a
     * tracker rebuild its page twice a second. */
    Porcelain_CountersReset(porcelain);
    fixture.caption = "Unpause";
    panel_restate(porcelain);
    Porcelain_PanelCountersRead(porcelain, &counters);
    CHECK(counters.reidentifies == 0, "a value change mints no identity at all");
    Porcelain_Close(porcelain);
}

/*
 * A well is ONE retained control with a retained picture, so a readout that
 * changed inside it moves nothing the host can see -- not its height, not its
 * identity, not a property. Without a redraw it keeps the bitmap it staged.
 *
 * MUTATION: delete the `else if( wanted->paint_key != have->paint_key )` arm
 * -- "a changed picture is one redraw" goes red at zero.
 * MUTATION: delete the `have->paint_key = wanted->paint_key` inside the
 * reidentify arm -- a re-identified well takes a redraw on top of the
 * invalidation the remint already gave it, and "a re-identified well is not
 * also redrawn" goes red at one.
 */
static void
test_panel_paint_key_redraws_one_row(void)
{
    struct PanelFixture fixture;
    struct PorcelainPanelCounters counters;
    struct Porcelain* porcelain;

    Testbed_Reset();
    panel_fixture_init(&fixture);
    porcelain = panel_start(&fixture, PORCELAIN_FACE_BOTH);
    Porcelain_CountersReset(porcelain);
    Testbed_ClearLog();

    /* Six figures inside the well moved. The page did not. */
    fixture.well_paint_key = 0x9002;
    panel_restate(porcelain);

    Porcelain_PanelCountersRead(porcelain, &counters);
    CHECK(counters.redraws == 1, "a changed picture is one redraw");
    CHECK(counters.setters == 0, "and no setter at all");
    CHECK(counters.rebuilds == 0, "and never a rebuild");
    CHECK(counters.reidentifies == 0, "and no identity: the y-to-item mapping did not move");
    CHECK(Testbed_LogCountWith("panel_redraw boxes") == 1, "naming the well that changed");

    /* A well that re-identifies is dirty by construction -- the old bitmap
     * belonged to the old identity -- so it must not also be redrawn. */
    Porcelain_CountersReset(porcelain);
    fixture.well_hit_key = 0x2222;
    fixture.well_paint_key = 0x9003;
    panel_restate(porcelain);
    Porcelain_PanelCountersRead(porcelain, &counters);
    CHECK(counters.reidentifies == 1, "a changed hit key still mints an identity");
    CHECK(counters.redraws == 0, "and a re-identified well is not also redrawn");
    Porcelain_Close(porcelain);
}

/*
 * MUTATION: delete the `|| wanted->force_reidentify` term -- the explicit
 * verb does nothing and "Porcelain_Reidentify mints the row it names" goes
 * red.
 * MUTATION: make Porcelain_Reidentify assert instead of recording a finding
 * for an undescribed key -- the process aborts and the refusal assertion
 * never runs.
 */
static void
test_panel_explicit_reidentify(void)
{
    struct PanelFixture fixture;
    struct PorcelainPanelCounters counters;
    struct Porcelain* porcelain;
    uint32_t well_serial;
    uint32_t button_serial;

    Testbed_Reset();
    panel_fixture_init(&fixture);
    porcelain = panel_start(&fixture, PORCELAIN_FACE_BOTH);
    well_serial = panel_row("boxes")->serial;
    button_serial = panel_row("pause")->serial;
    Porcelain_CountersReset(porcelain);

    fixture.force_reidentify = true;
    panel_restate(porcelain);

    Porcelain_PanelCountersRead(porcelain, &counters);
    CHECK(counters.reidentifies == 1, "Porcelain_Reidentify mints the row it names");
    CHECK(counters.rebuilds == 0, "without rebuilding the page");
    CHECK(panel_row("boxes")->serial != well_serial, "the named row takes a new serial");
    CHECK(panel_row("pause")->serial == button_serial, "and nothing else does");
    Porcelain_Close(porcelain);

    /* A key this run did not describe is a refusal with a finding: the
     * identity the plugin asked to retire is NOT retired, and a click
     * authored against the old picture would otherwise be delivered. */
    Testbed_Reset();
    panel_fixture_init(&fixture);
    fixture.reidentify_unknown = true;
    porcelain = panel_start(&fixture, PORCELAIN_FACE_BOTH);
    CHECK(panel_findings_with(porcelain, PORCELAIN_FINDING_REFUSED) == 1,
          "reidentifying a key this run did not describe is one finding");
    CHECK(Testbed_LogCountWith("panel_reidentify") == 0, "and reaches no engine call");
    Porcelain_Close(porcelain);
}

/*
 * MUTATION: drop the duplicate-key scan in Porcelain_Row -- the second row
 * under "head" is silently dropped by the host's idempotent declaration, the
 * page holds four rows instead of five, and "a duplicate key refuses the run"
 * goes red.
 * MUTATION: stop setting row_scratch_poisoned in panel_poison -- the page is
 * declared from a half-built description and the retained-declaration
 * assertion goes red.
 */
static void
test_panel_duplicate_key_refuses_the_run(void)
{
    struct PanelFixture fixture;
    struct PorcelainPanelCounters counters;
    struct Porcelain* porcelain;

    Testbed_Reset();
    panel_fixture_init(&fixture);
    porcelain = panel_start(&fixture, PORCELAIN_FACE_BOTH);
    Porcelain_CountersReset(porcelain);

    fixture.duplicate_key = true;
    panel_restate(porcelain);

    CHECK(panel_findings_with(porcelain, PORCELAIN_FINDING_REFUSED) == 1,
          "a duplicate key refuses the run with one finding");
    Porcelain_PanelCountersRead(porcelain, &counters);
    CHECK(counters.rebuilds == 0, "the applied declaration stands: half a page is the flicker");
    CHECK(counters.setters == 0, "and nothing is patched from a description that was refused");
    CHECK(Testbed_PanelRowCount() == 4, "the page the host holds is untouched");
    Porcelain_Close(porcelain);
}

/*
 * MUTATION: replace the length test in panel_copy_or_refuse with a truncating
 * copy -- the option's stable value becomes a prefix, the row publishes a
 * choice nobody offered, and the refusal assertion goes red.
 * MUTATION: raise PORCELAIN_ROWS_MAX above the flood count -- the overrun
 * assertion goes red because nothing overruns.
 */
static void
test_panel_refuses_rather_than_truncates(void)
{
    struct PanelFixture fixture;
    struct Porcelain* porcelain;

    memset(PANEL_LONG_VALUE, 'v', sizeof(PANEL_LONG_VALUE) - 1);
    PANEL_LONG_VALUE[sizeof(PANEL_LONG_VALUE) - 1] = '\0';

    Testbed_Reset();
    panel_fixture_init(&fixture);
    porcelain = panel_start(&fixture, PORCELAIN_FACE_BOTH);
    Porcelain_CountersReset(porcelain);
    fixture.long_option_value = true;
    panel_restate(porcelain);
    CHECK(panel_findings_with(porcelain, PORCELAIN_FINDING_REFUSED) == 1,
          "an option value over the ceiling is refused, never truncated");
    CHECK(Testbed_LogCountWith("panel_set_options") == 0, "and never reaches the row");
    CHECK(panel_row("frame")->option_count == 3,
          "the row keeps the list it was declared with");
    Porcelain_Close(porcelain);

    /* And the key ceiling the refusal uses is the HOST's own row-id ceiling,
     * not Porcelain's wider element key: a key the layer took and the host
     * then refused would be a finding per row per build, a page late. */
    CHECK(PORCELAIN_ROW_KEY_MAX == TORIRS_PLUGIN_WIDGET_ID_MAX,
          "a row key is refused at the ceiling the host will refuse it at");
    CHECK(PORCELAIN_ROW_KEY_MAX <= PORCELAIN_KEY_MAX,
          "and it fits in the copy Porcelain keeps of it");

    /* And a description with more rows than the layer holds is the same
     * answer: the last good page stands and the budget is a finding. */
    Testbed_Reset();
    panel_fixture_init(&fixture);
    porcelain = panel_start(&fixture, PORCELAIN_FACE_BOTH);
    fixture.flood_rows = PORCELAIN_ROWS_MAX + 2;
    panel_restate(porcelain);
    CHECK(panel_findings_with(porcelain, PORCELAIN_FINDING_BUDGET) == 1,
          "a description over the row budget is one budget finding");
    CHECK(Testbed_PanelRowCount() == 4, "with the page the host holds left alone");
    Porcelain_Close(porcelain);
}

/*
 * MUTATION: route on `row->kind` instead of the key in panel_row_by_key --
 * the pick lands on the wrong row and the key assertion goes red.
 * MUTATION: record a finding for an unknown id in Porcelain_PanelAction --
 * the generated settings form's own rows each become a refusal and "an id
 * Porcelain did not choose is not a finding" goes red.
 */
static void
test_panel_actions_route_by_key(void)
{
    struct PanelFixture fixture;
    struct Porcelain* porcelain;

    Testbed_Reset();
    panel_fixture_init(&fixture);
    porcelain = panel_start(&fixture, PORCELAIN_FACE_BOTH);

    CHECK(Testbed_PanelAction(porcelain, "frame", TORIRS_PANEL_ACTION_PICK, 1, "classic"),
          "a pick on a described row is routed");
    CHECK(fixture.actions == 1, "exactly once");
    CHECK(strcmp(fixture.action_key, "frame") == 0, "to the row whose key the host named");
    CHECK(strcmp(fixture.action_text, "classic") == 0,
          "carrying the option's stable value, never its label");
    CHECK(fixture.action_kind == TORIRS_PANEL_ACTION_PICK, "and the kind that happened");

    CHECK(Testbed_PanelAction(porcelain, "pause", TORIRS_PANEL_ACTION_ACTIVATE, 0, ""),
          "and a button press is routed too");
    CHECK(strcmp(fixture.action_key, "pause") == 0, "to the button");

    /* The generated settings form's rows arrive under ids Porcelain never
     * chose. Calling each of them a refusal would fill the findings table
     * with the host's own correct behaviour. */
    CHECK(!Testbed_PanelAction(porcelain, "generated_colour", TORIRS_PANEL_ACTION_TEXT, 0, "#fff"),
          "an id Porcelain did not choose is not routed");
    CHECK(panel_findings_with(porcelain, PORCELAIN_FINDING_REFUSED) == 0,
          "and is not a finding: the settings face adds to the host's generated form");
    CHECK(fixture.actions == 2, "and reaches no handler");
    Porcelain_Close(porcelain);
}

/*
 * MUTATION: drop the `!row->paint` term in Porcelain_PanelDraw -- a paintless
 * row claims the pass and the "only a described well paints" assertion goes
 * red.
 */
static void
test_panel_draw_routes_by_node(void)
{
    struct PanelFixture fixture;
    struct ToriRS_Graphics* draw = (struct ToriRS_Graphics*)&fixture;
    struct Porcelain* porcelain;

    Testbed_Reset();
    panel_fixture_init(&fixture);
    porcelain = panel_start(&fixture, PORCELAIN_FACE_BOTH);

    CHECK(Porcelain_PanelDraw(porcelain, "boxes", draw), "a described well takes its draw pass");
    CHECK(fixture.paints == 1, "exactly once");
    CHECK(!Porcelain_PanelDraw(porcelain, "pause", draw),
          "and a row with no paint does not claim one");
    CHECK(!Porcelain_PanelDraw(porcelain, "nothing_here", draw),
          "nor does a node this description never declared");
    CHECK(fixture.paints == 1, "so only a described well paints");
    Porcelain_Close(porcelain);
}

/*
 * MUTATION: ignore the result in panel_note_result -- the refused set_options
 * is silent and "a refused setter is a finding" goes red. Silence is the class
 * the record says hurt most.
 */
static void
test_panel_refused_setter_is_a_finding(void)
{
    struct PanelFixture fixture;
    struct Porcelain* porcelain;

    Testbed_Reset();
    panel_fixture_init(&fixture);
    porcelain = panel_start(&fixture, PORCELAIN_FACE_BOTH);
    g_testbed.panel_refuse_options = true;

    fixture.option_count = 4;
    panel_restate(porcelain);

    CHECK(panel_findings_with(porcelain, PORCELAIN_FINDING_REFUSED) == 1,
          "a refused setter is a finding with the row it named");
    Porcelain_Close(porcelain);
}

/*
 * MUTATION: delete the `if( !panel->row_set_ready ) Porcelain_DescribeNow`
 * arm -- the page opens empty, the next fence rebuilds it, and both the row
 * count and the rebuild assertion go red.
 */
static void
test_panel_opened_before_the_first_fence(void)
{
    struct PanelFixture fixture;
    struct PorcelainPanelCounters counters;
    struct Porcelain* porcelain;

    Testbed_Reset();
    panel_fixture_init(&fixture);
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Panel(porcelain, NULL, 320, PORCELAIN_FACE_BOTH);
    Porcelain_Describe(porcelain, panel_describe, &fixture);

    /* The page is opened before this plugin has ever fenced. */
    Testbed_PanelBuild(porcelain, TORIRS_PANEL_VIEW_PAGE);
    CHECK(Testbed_PanelRowCount() == 4,
          "a page opened before the first fence declares the description, not an empty page");

    fence(porcelain);
    Porcelain_PanelCountersRead(porcelain, &counters);
    CHECK(counters.rebuilds == 0, "and the first fence after it asks for no rebuild");
    Porcelain_Close(porcelain);
}

/*
 * A page that opened while the description was refused.
 *
 * Nothing else will ask for a declaration again -- a build callback arrives
 * when the host clears the page, and it already has -- so the first describe
 * that fits owes one invalidate, once.
 *
 * MUTATION: delete the `panel->declaration_owed = true` line -- the page
 * stays empty for the life of the session and "the first description that
 * fits asks for the declaration it owes" goes red at zero rebuilds.
 * MUTATION: delete the `panel->declaration_owed = false` at the top of
 * Porcelain_PanelBuild -- a flag left standing from the refused build
 * survives the face switch below and "a face that is not ours is never asked
 * for a rebuild" goes red.
 */
static void
test_panel_declaration_owed_after_a_refusal(void)
{
    struct PanelFixture fixture;
    struct PorcelainPanelCounters counters;
    struct Porcelain* porcelain;

    Testbed_Reset();
    panel_fixture_init(&fixture);
    fixture.flood_rows = PORCELAIN_ROWS_MAX + 2;
    porcelain = panel_start(&fixture, PORCELAIN_FACE_PAGE);
    CHECK(Testbed_PanelRowCount() == 0, "a page whose description was refused declares nothing");
    CHECK(panel_findings_with(porcelain, PORCELAIN_FINDING_BUDGET) >= 1, "and says so");

    Porcelain_CountersReset(porcelain);
    fixture.flood_rows = 0;
    panel_restate(porcelain);
    Porcelain_PanelCountersRead(porcelain, &counters);
    CHECK(counters.rebuilds == 1, "the first description that fits asks for the declaration it owes");
    Testbed_PanelBuild(porcelain, TORIRS_PANEL_VIEW_PAGE);
    CHECK(Testbed_PanelRowCount() == 4, "and the page arrives");

    Porcelain_CountersReset(porcelain);
    for( int i = 0; i < 3; i++ )
        panel_restate(porcelain);
    Porcelain_PanelCountersRead(porcelain, &counters);
    CHECK(counters.rebuilds == 0, "and it is owed exactly once, not on every fence after it");

    /* A face this description does not cover clears the debt as surely as a
     * good declaration does: the page it would rebuild is not ours. */
    Porcelain_CountersReset(porcelain);
    fixture.flood_rows = PORCELAIN_ROWS_MAX + 2;
    panel_restate(porcelain);
    Testbed_PanelBuild(porcelain, TORIRS_PANEL_VIEW_PAGE);
    Testbed_PanelBuild(porcelain, TORIRS_PANEL_VIEW_SETTINGS);
    Porcelain_CountersReset(porcelain);
    fixture.flood_rows = 0;
    panel_restate(porcelain);
    Porcelain_PanelCountersRead(porcelain, &counters);
    CHECK(counters.rebuilds == 0, "a face that is not ours is never asked for a rebuild");
    Porcelain_Close(porcelain);
}

/*
 * MUTATION: stop clearing the slot in Porcelain_PanelClose -- the pool is
 * exhausted part-way through the loop and Porcelain_Panel's assert(panel)
 * aborts the run, which is the loud answer a leaked slot deserves.
 */
static void
test_panel_close_releases_the_pane(void)
{
    struct PanelFixture fixture;
    struct Porcelain* porcelain;

    Testbed_Reset();
    panel_fixture_init(&fixture);

    /* More opens than the pool holds, so a slot that is not released is a
     * pool that runs out rather than a leak nobody notices. */
    for( int i = 0; i < PORCELAIN_PANELS_MAX + 2; i++ )
    {
        porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
        Porcelain_Panel(porcelain, NULL, 320, PORCELAIN_FACE_PAGE);
        Porcelain_Describe(porcelain, panel_describe, &fixture);
        fence(porcelain);
        Testbed_PanelBuild(porcelain, TORIRS_PANEL_VIEW_PAGE);
        CHECK(Testbed_PanelRowCount() == 4, "a reopened handle declares its own page");
        Porcelain_Close(porcelain);
    }
    CHECK(g_testbed.panel_requests == PORCELAIN_PANELS_MAX + 2,
          "a closed panel releases its slot, so the pool is a capacity and not a lifetime");
}

/* ------------------------------------------------------------------------ */

int
main(void)
{
    test_steady_state_costs_nothing();
    test_changed_property_pushes_one_setter();
    test_removed_key_removes_one_control();
    test_readiness_matrix();
    test_replace_target_dies();
    test_arbitration_first_claim();
    test_arbitration_relinquish();
    test_findings_coalesce();
    test_absence_waits_for_the_lane();
    test_expected_absence_both_directions();
    test_tier_table();
    test_config_list_refusal();
    test_description_total_order();
    test_direct_path();
    test_one_revalidate_for_every_plugin();
    test_visibility_and_enablement();
    test_blocker_needs_no_picture();
    test_edits_are_retained_and_released();
    test_keep_relative();
    test_menu_tag();
    test_setting_absent_is_off();
    test_key_edge();
    test_derived_once_per_input();
    test_require_reports_the_feature();
    test_counts_are_lane_data();
    test_tab_resolves_by_data();
    test_spelling_is_re_asked();
    test_tab_prefers_the_spelling_that_answers();
    test_chat_filter_resolves_on_both_shapes();
    test_count_spans_a_hole();
    test_draw_context_says_which_space();
    test_menu_add_refusal_is_a_finding();
    test_a_moved_target_takes_its_control_with_it();
    test_world_hull_refusals_reach_the_plugin();
    test_plugin_records_its_own_finding();
    test_menu_tag_round_trips();
    test_key_down_is_asked_not_subscribed();
    test_key_edge_clears_down_across_an_absent_binding();
    test_config_list_is_a_set();
    test_hover_carries_the_container();
    test_native_overlay_latch();
    test_table_is_read_once();
    test_notify_coalesces_per_subject();
    test_a_late_picture_still_arrives();
    test_zero_size_is_the_pictures_own();
    test_an_absence_can_be_declared_when_it_is_found();
    test_a_limitation_can_be_declared();
    test_within_is_a_child_with_no_anchor();
    test_the_direct_path_carries_text();
    test_a_clock_is_measured_and_re_intervalled();

    test_panel_registers_once();
    test_panel_faces();
    test_panel_steady_state_costs_nothing();
    test_panel_caption_is_a_setter();
    test_panel_button_availability();
    test_panel_option_count_is_a_setter();
    test_panel_label_identity_split();
    test_panel_row_set_is_the_one_rebuild();
    test_panel_well_grows_without_a_rebuild();
    test_panel_hit_key_reidentifies_one_row();
    test_panel_paint_key_redraws_one_row();
    test_panel_explicit_reidentify();
    test_panel_duplicate_key_refuses_the_run();
    test_panel_refuses_rather_than_truncates();
    test_panel_actions_route_by_key();
    test_panel_draw_routes_by_node();
    test_panel_refused_setter_is_a_finding();
    test_panel_opened_before_the_first_fence();
    test_panel_declaration_owed_after_a_refusal();
    test_panel_close_releases_the_pane();

    printf("porcelain: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
