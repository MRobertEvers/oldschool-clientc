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
    g_testbed.key_held = TORIRS_KEY_SHIFT;
    fence(porcelain);
    CHECK(edges == 1, "the press is one edge");
    fence(porcelain);
    CHECK(edges == 1, "and holding it is not a second");
    g_testbed.key_held = 0;
    fence(porcelain);
    CHECK(edges == 101, "the release is the other edge");
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
    test_hover_carries_the_container();
    test_native_overlay_latch();
    test_table_is_read_once();
    test_notify_coalesces_per_subject();

    printf("porcelain: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
