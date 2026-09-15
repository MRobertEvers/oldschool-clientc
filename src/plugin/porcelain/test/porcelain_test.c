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

static char g_fixture_op_key[64];

static void
fixture_op(struct ToriRS_Api* api, void* user, char const* key)
{
    (void)api;
    (void)user;
    snprintf(g_fixture_op_key, sizeof(g_fixture_op_key), "%s", key ? key : "");
    g_fixture_ops++;
}

/* The engine's side of an operation: a listener call on the node's own ref. */
static void
fire_op(char const* key)
{
    struct TestbedControl* control = Testbed_Control(key);
    struct ToriRS_WidgetEvent event;

    if( !control || !control->op )
        return;
    memset(&event, 0, sizeof(event));
    event.type = TORIRS_WIDGET_OPERATION;
    event.widget = control->ref;
    control->op(Testbed_Api(), control->op_user, &event);
}

struct Fixture
{
    char const* image;
    char const* caption;
    int gap;
    int opacity;
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
    item.opacity = fixture->opacity;
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
    CHECK(Testbed_LogCountWith("remove camera~hit") == 1, "and the hit box it stood over");
    CHECK(Testbed_LogCountWith("remove camera") == 2, "each of the two exactly once");

    Testbed_BindElement("report_button");
    fence(porcelain);
    CHECK(Testbed_Control("camera") != NULL, "and it comes back when the target rebinds");
    Porcelain_Close(porcelain);
}

/*
 * THE DEAD RING.
 *
 * A REPLACE takes the target's records out of the frame and puts the
 * replacement's in their place, so the part of the target's box the
 * replacement does not cover answers NOTHING -- the native op is gone and the
 * control is not there. Measured on the shipped camera, that is 1,497 of the
 * report button's 1,817 pixels: a click 7px left of the icon opened no menu,
 * took no screenshot and wrote no file, on the CS2 lane and on CS1 alike.
 *
 * The layer answers it with a second owned node the size of the TARGET, armed
 * with the item's op; the picture keeps its centred box and is disarmed while
 * that stands, or the pointer over the art builds the row twice.
 *
 * MUTATION: delete the porcelain_apply_replace_hit call from the reconcile.
 * Red: no camera~hit, and the 60x20 button answers only over 20x20.
 * SECOND MUTATION: drop the `!applied->hit_live` term in porcelain_apply_op.
 * Red: both nodes are armed and the icon offers Screenshot twice.
 */
static void
test_replace_answers_the_whole_target(void)
{
    struct Fixture fixture = {
        .image = "camera.png", .enabled = true, .replace = true};
    struct Porcelain* porcelain;
    struct TestbedControl const* camera;
    struct TestbedControl const* hit;

    Testbed_Reset();
    declare_chrome();
    Testbed_BindElement("report_button");
    Testbed_BindElement("chat_bar");

    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, fixture_describe, &fixture);
    fence(porcelain);

    camera = Testbed_Control("camera");
    hit = Testbed_Control("camera~hit");
    CHECK(camera && camera->x == 420 && camera->width == 20,
          "the picture keeps its own size, centred on the target");
    CHECK(hit != NULL, "and a hit box stands with it");
    CHECK(hit && hit->x == 400 && hit->y == 470 && hit->width == 60 && hit->height == 20,
          "covering the WHOLE box the REPLACE consumed, not the art's");
    CHECK(hit && hit->image.value == 0, "drawing nothing: it is a hit box, not a plate");
    CHECK(hit && hit->relation == TORIRS_WIDGET_RELATION_OVER,
          "an ordinary sibling over the target -- REPLACE is the picture's, one to an element");
    CHECK(hit && hit->armed && strcmp(hit->label, "Screenshot") == 0, "armed with the item's op");
    CHECK(camera && !camera->armed,
          "and the picture is NOT, or the art would offer the same row twice");

    g_fixture_ops = 0;
    g_fixture_op_key[0] = '\0';
    fire_op("camera~hit");
    CHECK(g_fixture_ops == 1, "the hit box runs the item's handler");
    CHECK(strcmp(g_fixture_op_key, "camera") == 0,
          "under the ITEM's key, so Porcelain_Set still finds the picture");

    Testbed_ClearLog();
    fence(porcelain);
    CHECK(Testbed_LogCount() == 0, "and an unchanged description writes nothing for either node");

    /* The target's box is the hit box's box at every fence, the same way the
     * picture's is. */
    Testbed_MoveElement("report_button", 300, 400);
    fence(porcelain);
    hit = Testbed_Control("camera~hit");
    CHECK(hit && hit->x == 300 && hit->y == 400, "a target that moved takes the hit box with it");

    /*
     * Not merely hidden when the target stops being presented: the menu
     * walk's gate for a node does not read its own widget_hidden bit, so a
     * hidden hit box would keep taking clicks over a Report button the lane
     * had put away. The picture needs no such care -- its REPLACE anchor
     * makes the engine inherit the target's visibility both ways.
     */
    Testbed_PresentElement("report_button", false);
    fence(porcelain);
    CHECK(Testbed_Control("camera~hit") == NULL, "an unpresented target takes the hit box away");
    CHECK(Testbed_Control("camera") != NULL, "while the picture stays and inherits the hide");
    CHECK(Testbed_Control("camera")->armed,
          "and takes the op back, so nothing is left with no way to answer");
    Testbed_PresentElement("report_button", true);
    fence(porcelain);
    CHECK(Testbed_Control("camera~hit") != NULL, "and it comes back with the target");
    CHECK(!Testbed_Control("camera")->armed, "with the picture disarmed again");
    Porcelain_Close(porcelain);
}

/*
 * A REPLACE that is not armed is paint standing in for paint: the plugin has
 * said in as many words that nothing should answer there, and the layer does
 * not invent a hit box to contradict it.
 *
 * MUTATION: drop the `item->enabled` term from porcelain_replace_wants_hit.
 * Red.
 */
static void
test_replace_without_a_hit_gets_no_hit_box(void)
{
    struct Fixture fixture = {
        .image = "camera.png", .enabled = false, .replace = true};
    struct Porcelain* porcelain;

    Testbed_Reset();
    declare_chrome();
    Testbed_BindElement("report_button");
    Testbed_BindElement("chat_bar");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, fixture_describe, &fixture);
    fence(porcelain);
    CHECK(Testbed_Control("camera") != NULL, "the picture stands in for Report");
    CHECK(Testbed_Control("camera~hit") == NULL, "and nothing answers where nothing was armed");
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
 * A Set is not undone by the next fence, and a target that moves still drags
 * the control with it.
 *
 * The fence's no-input-moved branch re-asserts every live item's geometry
 * from the description, which is what makes a control follow its target. It
 * also wrote over the direct path: a Set that moved a control was reverted on
 * the very next fence and had to be re-applied, so motion cost TWO
 * set_position calls per control per frame through a verb that exists to cost
 * one. xp-drop-orbs -- the one shipped plugin whose whole subject is motion
 * -- could not use the verb at all and drove its animation through the
 * description plus an invalidate, which is a whole reconcile pass per frame.
 *
 * The two boxes were always meant to say this: `desired` is what the
 * description asks for, `live_*` is what was written, and the fence has
 * nothing to say while the first of them has not moved.
 *
 * MUTATION: delete the equal-desired early return in porcelain_apply_geometry.
 * Red: "the fence does not write over it".
 * SECOND MUTATION: drop the box compare from that return and keep only
 * `desired_written`, so geometry is placed once and never re-asserted. Red:
 * "and a target that moved drags it along" -- which is the requirement the
 * re-assert exists for and the half a naive fix loses.
 */
static void
test_a_set_survives_the_next_fence(void)
{
    struct Fixture fixture = {.image = "camera.png", .gap = 4, .enabled = true};
    struct Porcelain* porcelain;
    struct PorcelainMotion motion;
    struct TestbedControl const* camera;

    Testbed_Reset();
    declare_chrome();
    Testbed_BindElement("report_button");
    Testbed_BindElement("chat_bar");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, fixture_describe, &fixture);
    fence(porcelain);

    memset(&motion, 0, sizeof(motion));
    motion.mask = PORCELAIN_MOTION_X | PORCELAIN_MOTION_Y;
    motion.x = 500;
    motion.y = 300;
    Porcelain_Fence(porcelain);
    CHECK(Porcelain_Set(porcelain, "camera", &motion) == TORIRS_RESULT_OK, "the direct path moves it");
    Porcelain_Commit(Testbed_Api());
    camera = Testbed_Control("camera");
    CHECK(camera && camera->x == 500 && camera->y == 300, "the control is where the Set put it");

    /* The frame the plugin does NOT set: nothing described moved, so the
     * layer has nothing to write. */
    Testbed_ClearLog();
    fence(porcelain);
    camera = Testbed_Control("camera");
    CHECK(camera && camera->x == 500 && camera->y == 300, "the fence does not write over it");
    CHECK(Testbed_LogCountWith("set_position") == 0, "and costs no setter to leave it alone");

    /* One animated frame end to end: ONE set_position, not two. */
    Testbed_ClearLog();
    motion.x = 501;
    Porcelain_Fence(porcelain);
    CHECK(Porcelain_Set(porcelain, "camera", &motion) == TORIRS_RESULT_OK, "the next step sets");
    Porcelain_Commit(Testbed_Api());
    CHECK(Testbed_LogCountWith("set_position") == 1,
          "an animated frame costs one set_position, which is what the direct path is for");

    /*
     * And the requirement the re-assert exists for, which had to survive: the
     * control is placed BESIDE report_button, so moving that moves it.
     */
    Testbed_MoveElement("report_button", 400, 200);
    fence(porcelain);
    camera = Testbed_Control("camera");
    CHECK(camera && camera->y == 200, "and a target that moved drags it along");
    CHECK(camera->x == 400 + 60 + 4, "to the box the description states for the new target");
    Porcelain_Close(porcelain);
}

/*
 * A fade that reaches the floor is SAID, and there is a value that means it.
 *
 * `opacity` 0 in a described item is the unset field of a zeroed struct --
 * the layer's own idiom, and the reason a zeroed PorcelainItem plus three
 * fields is a legal item -- so it means opaque. A plugin fading a control out
 * with `item.opacity = alpha` therefore had its control SNAP BACK TO FULLY
 * PAINTED on the step alpha reached zero, silently, and xp-drop-orbs fences
 * its fade at 1 with a note rather than hit it.
 *
 * Zero cannot be made to mean invisible without changing what a zeroed struct
 * means to every describe there is. So invisible got a name, and the fall to
 * zero got a finding: the one thing the layer must not do is quietly answer
 * the opposite of what the field reads as.
 *
 * MUTATION: delete the PORCELAIN_OPACITY_INVISIBLE arm in
 * porcelain_apply_properties. Red: "invisible is a value this field can
 * carry".
 * SECOND MUTATION: delete the fell-to-zero finding beside it. Red: "and a
 * fade that reached zero is reported".
 */
static void
test_a_fade_to_zero_is_not_silently_opaque(void)
{
    struct Fixture fixture = {.image = "camera.png", .gap = 4, .enabled = true};
    struct Porcelain* porcelain;
    struct PorcelainFinding findings[4];
    struct TestbedControl const* camera;

    Testbed_Reset();
    declare_chrome();
    Testbed_BindElement("report_button");
    Testbed_BindElement("chat_bar");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, fixture_describe, &fixture);
    fence(porcelain);
    camera = Testbed_Control("camera");
    CHECK(camera && camera->opacity == 255, "an item that never mentions opacity is opaque");
    CHECK(Porcelain_Findings(porcelain, findings, 4) == 0, "and says nothing about it");

    /* The fade, one step above the floor. */
    fixture.opacity = 64;
    Porcelain_Invalidate(porcelain);
    fence(porcelain);
    camera = Testbed_Control("camera");
    CHECK(camera && camera->opacity == 64, "a stated opacity reaches the engine");

    /* And the floor, said the way the field can carry it. */
    fixture.opacity = PORCELAIN_OPACITY_INVISIBLE;
    Porcelain_Invalidate(porcelain);
    fence(porcelain);
    camera = Testbed_Control("camera");
    CHECK(camera && camera->opacity == 0, "invisible is a value this field can carry");
    CHECK(Porcelain_Findings(porcelain, findings, 4) == 0, "and needs no explaining");

    /* The loop written the obvious way, which is the defect. It still reads
     * as opaque -- that cannot change without changing what a zeroed item
     * means -- but it is no longer silent. */
    fixture.opacity = 64;
    Porcelain_Invalidate(porcelain);
    fence(porcelain);
    fixture.opacity = 0;
    Porcelain_Invalidate(porcelain);
    fence(porcelain);
    camera = Testbed_Control("camera");
    CHECK(camera && camera->opacity == 255, "a described zero is still the unset field");
    CHECK(Porcelain_Findings(porcelain, findings, 4) == 1,
          "and a fade that reached zero is reported");
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
 * And it takes its box through set_size, because it has no image handle to
 * carry one: the engine resolves the token before it reads the size, so the
 * set_image an image-less control used to be given was refused whole and the
 * blocker stayed 0x0 -- present, invisible and unclickable. The fake answered
 * that call OK, which is why this test used to PIN the refusal by asserting
 * the call was made.
 *
 * MUTATION: make porcelain_push_item assert on a NULL image. Red immediately.
 * SECOND MUTATION: route an image-less item's box back through set_image in
 * porcelain_apply_geometry. Red: the blocker has no box.
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
    CHECK(Testbed_LogCountWith("set_image swallow") == 0,
          "and asks for no picture at all, composed 1x1 PNG or otherwise");
    CHECK(Testbed_LogCountWith("set_size swallow 100x40") == 1,
          "its box goes through the verb that carries a box on its own");
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
 * The same named rows read as NUMBERS, and resolved once.
 *
 * Three of the five reads the cannon builtin makes are values and two of them
 * are varps, so a layer whose only named-var verb answered a boolean over a
 * varbit left a shipped BUILTIN resolving ids and reading vars by hand. That
 * is the claim the nxt family was ported to test.
 *
 * MUTATION 1: make porcelain_var_kind always answer "varbit".
 *   Red: "a varp is read as a varp" -- the testbed answers a varp as its id
 *   negated precisely so a kind mix-up cannot look plausible.
 * MUTATION 2: delete the memo hit in porcelain_var_slot (always resolve).
 *   Red: "the name is resolved ONCE, not once per read".
 * MUTATION 3: make the `slot->id < 0` arm read the var anyway.
 *   Red: "a row this profile does not declare answers the CALLER's off value".
 * MUTATION 4: make Porcelain_Setting pass 0 as `absent` for an inverted row.
 *   Red: "an inverted row this profile lacks is OFF, not ON".
 */
static void
test_setting_value_reads_a_number_once(void)
{
    struct Porcelain* porcelain;
    struct PorcelainFinding findings[8];
    int count;

    Testbed_Reset();
    snprintf(g_testbed.named_ids, sizeof(g_testbed.named_ids),
             "varp:cannon_ammo=3,varp:cannon_coord=3551,varbit:cannon_low_amount=14176");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);

    CHECK(Porcelain_SettingValue(porcelain, "varbit:cannon_low_amount", 0) == 14176,
          "a bare varbit row reads its varbit");
    CHECK(Porcelain_SettingValue(porcelain, "varp:cannon_ammo", 0) == -3,
          "a varp is read as a varp");
    CHECK(Porcelain_SettingValue(porcelain, "cannon_low_amount", 0) == 14176,
          "and an unprefixed name is a varbit, which is what every settings row is");

    /* Five more reads of the three names. The PROFILE cannot renumber under a
     * running client, so a second lookup is a host call that could not answer
     * differently -- and the shipped builtins were making one per ground item
     * and four per server tick. */
    Testbed_ClearLog();
    for( int at = 0; at < 5; at++ )
    {
        (void)Porcelain_SettingValue(porcelain, "varp:cannon_ammo", 0);
        (void)Porcelain_SettingValue(porcelain, "varp:cannon_coord", 0);
        (void)Porcelain_SettingValue(porcelain, "cannon_low_amount", 0);
    }
    CHECK(Testbed_LogCountWith("named_id") == 1,
          "the name is resolved ONCE, not once per read");
    CHECK(Testbed_LogCountWith("varp") == 10, "the READS still happen, every time");

    /* Absent is the CALLER's answer, because only the caller knows what off
     * looks like for its row: 0 for a count, 1 for an inverted toggle. */
    CHECK(Porcelain_SettingValue(porcelain, "varp:no_such_row", 0) == 0,
          "a row this profile does not declare answers the CALLER's off value");
    CHECK(Porcelain_SettingValue(porcelain, "varp:no_such_row", 7) == 7,
          "whatever the caller said that was");
    CHECK(!Porcelain_Setting(porcelain, "no_such_toggle", PORCELAIN_SETTING_INVERTED),
          "an inverted row this profile lacks is OFF, not ON");

    count = Porcelain_Findings(porcelain, findings, 8);
    CHECK(count == 2, "two absent names are two findings, and the reads inside them are one each");
    Porcelain_Close(porcelain);
}

/*
 * The tile draw's one refusal, which a tile overlay reaches by arithmetic.
 *
 * A marker covers size_x * size_z tiles, so the cache-highlight renderer with a
 * crowded Activities set is many tiles a frame against 512 -- and every tile
 * past it vanished silently, because api_draw_tile returned void and the v2
 * builder answered OK whatever the allotment said.
 *
 * MUTATION 1: make v2_builder_world_tile return TORIRS_RESULT_OK again (the
 *   shipped defect exactly). Red: "the tile over the allotment is refused".
 * MUTATION 2: drop the BUDGET arm in Porcelain_Tile.
 *   Red: "the finding names the budget".
 */
static void
test_world_tile_budget_reaches_the_plugin(void)
{
    struct Porcelain* porcelain;
    struct ToriRS_Graphics* draw;
    struct PorcelainFinding findings[8];
    int count;
    bool budget = false;

    Testbed_Reset();
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    draw = Testbed_Graphics((struct ToriRS_Rect){0, 0, 800, 500}, true);

    /* Three: a 2x2 footprint is four tiles, so the allotment runs out INSIDE
     * one marker, which is the shape that made the defect invisible -- three
     * corners of a square drawn and the fourth gone. */
    g_testbed.tile_budget = 3;
    CHECK(Porcelain_Tile(porcelain, draw, 3200, 3200, 0, 0x00ff00u, 0x00ff00u, 40),
          "a tile inside the allotment is drawn");
    CHECK(Porcelain_Tile(porcelain, draw, 3201, 3200, 0, 0x00ff00u, 0x00ff00u, 40), "and the next");
    CHECK(Porcelain_Tile(porcelain, draw, 3200, 3201, 0, 0x00ff00u, 0x00ff00u, 40),
          "and the last one in it");
    CHECK(!Porcelain_Tile(porcelain, draw, 3201, 3201, 0, 0x00ff00u, 0x00ff00u, 40),
          "the tile over the allotment is refused");
    for( int at = 0; at < 20; at++ )
        (void)Porcelain_Tile(porcelain, draw, 3300 + at, 3300, 0, 0x00ff00u, 0x00ff00u, 40);
    count = Porcelain_Findings(porcelain, findings, 8);
    CHECK(count == 1, "twenty-one refused tiles in one frame are ONE finding");
    for( int at = 0; at < count; at++ )
        if( findings[at].result == PORCELAIN_FINDING_BUDGET &&
            strcmp(findings[at].verb, "world_tile") == 0 &&
            strstr(findings[at].detail, "budget") != NULL )
            budget = true;
    CHECK(budget, "the finding names the budget");
    CHECK(findings[0].count == 21, "and counts every tile it swallowed");
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
 * An element the lane has NOT GOT costs nothing per frame.
 *
 * This is the layer's central claim, and for one release it was false. An
 * unresolved watch was re-asked by porcelain_resolve_pending on every fence
 * for ever -- which was tolerable while few plugins left one behind, and
 * stopped being tolerable the moment Porcelain_Usable was fixed to CREATE its
 * four lane-chrome watches: every caller that took a draw context then left
 * four unresolved watches on any lane with no docked strip and paid four role
 * lookups a frame, for ever, to keep asking about a strip that is not there.
 *
 * The line is PENDING against ABSENT, which is what the two words already
 * mean. A PENDING element may still be mounting and the re-ask is what makes
 * a provider come up cleanly; the absence clock bounds that window at two
 * fences. An ABSENT one is settled, and the only thing that can unsettle it
 * is a new tree -- so a `@tree` subscription, one for the whole handle, is
 * what a re-ask hangs off from then on.
 *
 * MUTATION: drop the `tree_moved ||` from the re-ask gate in
 * porcelain_resolve_pending so it runs on every fence again. Red: "twenty
 * fences with nothing publishing make zero engine calls".
 * SECOND MUTATION: drop the `tree_moved ||` half instead, so an ABSENT watch
 * is never re-asked at all. Red: "a publication re-asks the one watch that
 * has not resolved".
 * THIRD MUTATION: drop the `|| watch->state.bind != PORCELAIN_ABSENT` half,
 * so a PENDING watch waits for a publication too. Red: "a PENDING element is
 * re-asked while the lane may still be mounting it".
 */
static void
test_an_unresolved_element_costs_nothing_at_rest(void)
{
    struct Porcelain* porcelain;
    struct PorcelainElementState state;

    Testbed_Reset();
    Testbed_DeclareElement("viewport", 4, 4, 512, 334);
    Testbed_BindElement("viewport");
    /* Declared and NOT bound: the lane knows the name and has not mounted it,
     * which is exactly the shape of a strip a lane has no room for. */
    Testbed_DeclareElement("chat", 17, 357, 519, 165);

    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    CHECK(Porcelain_Element(porcelain, PORCELAIN_EL(VIEWPORT), &state),
          "the viewport is bound");
    CHECK(!Porcelain_Element(porcelain, PORCELAIN_EL(CHAT), &state),
          "and the chat is not");
    CHECK(Testbed_LogCountWith("watch_tree") == 1,
          "one @tree subscription for the whole handle, taken beside the first watch");

    /* While it is PENDING the lane may still be arriving, so it IS re-asked:
     * one find, for the one watch that has not resolved. */
    Testbed_ClearLog();
    fence(porcelain);
    CHECK(Testbed_LogCountWith("find chat") == 1,
          "a PENDING element is re-asked while the lane may still be mounting it");
    CHECK(Testbed_LogCount() == 1, "and the bound one is not re-asked at all");

    /* Past the absence clock, so the watch is settled rather than early. */
    for( int i = 0; i < PORCELAIN_ABSENT_FENCES + 2; i++ )
        fence(porcelain);

    Testbed_ClearLog();
    for( int i = 0; i < 20; i++ )
        fence(porcelain);
    if( Testbed_LogCount() != 0 )
    {
        fprintf(stderr, "an unresolved watch made engine calls at rest:\n");
        Testbed_PrintLog();
    }
    CHECK(Testbed_LogCount() == 0,
          "twenty fences with nothing publishing make zero engine calls");

    /*
     * And the other half, or the line above would pass on a layer that had
     * simply stopped asking: a publication IS re-asked, and the number is one
     * per unresolved watch and not one per anything else.
     *
     * MUTATION: make porcelain_resolve_pending ignore tree_moved entirely.
     * Red: the publication buys no re-ask and the chat never binds below.
     */
    Testbed_ClearLog();
    Testbed_PublishTree();
    fence(porcelain);
    CHECK(Testbed_LogCountWith("find chat") == 1,
          "a publication re-asks the one watch that has not resolved");
    CHECK(Testbed_LogCount() == 1, "and nothing else at all");

    /* And it binds, on the publication that brings it. */
    Testbed_BindElement("chat");
    fence(porcelain);
    CHECK(Porcelain_Element(porcelain, PORCELAIN_EL(CHAT), &state),
          "an element that arrives late still binds");

    Testbed_ClearLog();
    for( int i = 0; i < 20; i++ )
        fence(porcelain);
    CHECK(Testbed_LogCount() == 0, "and the settled pair costs nothing again");
    Porcelain_Close(porcelain);
}

/*
 * A draw context does not ask whether this lane has a docked strip.
 *
 * Porcelain_DrawContext used to derive the USABLE rect for every caller
 * alongside the canvas box. The canvas one is load-bearing -- it decides
 * `canvas_space`, which is the difference between a tooltip clamped to the
 * canvas and one flipped up over the minimap. The usable one was read by
 * nobody: every caller of this verb is an overlay clamping to the pass it was
 * handed, and the usable canvas is a PLACEMENT area, which is a frame
 * provider's question and has its own verb.
 *
 * It was not free. Subtracting a strip means asking whether there IS one, and
 * that CREATES four lane-chrome watches -- charged to a caller that never
 * mentioned them, on a lane that mostly has no strip at all.
 *
 * MUTATION: put `Porcelain_Element(porcelain, PORCELAIN_EL(USABLE), ...)`
 * back into Porcelain_DrawContext. Red: the draw context opens four watches
 * on lane_chrome roles.
 */
static void
test_a_draw_context_does_not_ask_about_lane_chrome(void)
{
    struct Porcelain* porcelain;
    struct PorcelainDrawContext context;
    struct PorcelainElementState state;
    struct ToriRS_WidgetBounds usable;

    Testbed_Reset();
    Testbed_DeclareElement("viewport", 4, 4, 512, 334);
    Testbed_BindElement("viewport");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    CHECK(Porcelain_Element(porcelain, PORCELAIN_EL(VIEWPORT), &state), "the viewport is bound");

    Testbed_ClearLog();
    CHECK(Porcelain_DrawContext(porcelain,
                                Testbed_Graphics((struct ToriRS_Rect){0, 0, 800, 500}, true),
                                PORCELAIN_EL(NONE), &context),
          "the canvas pass answers a context");
    CHECK(context.canvas_space, "which is canvas space, so the canvas box was derived");
    CHECK(Testbed_LogCountWith("watch_state lane_chrome") == 0,
          "and not one lane_chrome role was watched for it");

    /* The verb that DOES own the question still asks it. */
    Testbed_ClearLog();
    CHECK(Porcelain_Usable(porcelain, &usable), "Porcelain_Usable answers the placement area");
    CHECK(Testbed_LogCountWith("watch_state lane_chrome") == 4,
          "by watching all four strip members, which is its job and not a draw's");
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
    /*
     * And NOTHING is written for it yet.
     *
     * Porcelain_Image answers a pending asset with a zero handle, the property
     * pass stores that, and the geometry pass wrote it -- the engine refuses a
     * zero image ref outright, so every control described before its art
     * landed cost one REFUSED set_image. A refusal nobody can act on is what
     * the clean-findings gate exists to keep out. Measured on the gate's
     * remount lane: one per tab face whose PRESSED picture is first named
     * after the frame root is replaced.
     *
     * MUTATION: drop the image_state test from porcelain_apply_box's
     * set_image arm. Red here, with one refused write.
     */
    CHECK(Testbed_LogCountWith("set_image camera") == 0,
          "and no zero image ref is written while it is still loading");

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

    /*
     * And the other order, which is the only HONEST one.
     *
     * A plugin discovers a lane limitation by asking -- Porcelain_Require
     * answering false IS the discovery -- so the declaration can only come
     * after the finding that prompted it. Labelling at record time alone left
     * that first refusal expected=0 for ever, and the only way to pass the
     * clean gate was to declare unconditionally, on every lane, including the
     * ones where the feature works. That is a declaration that is false half
     * the time, which is what the bidirectional rule exists to refuse.
     *
     * This fixes the TABLE and not the trace line, which was already written
     * at birth with the label it had then -- so a plugin that needs its
     * capture clean still declares before it asks. @see
     * Porcelain_RelabelUnsupported.
     *
     * MUTATION: delete the Porcelain_RelabelUnsupported call in
     * Porcelain_ExpectUnsupported. Red: "the refusal that PROMPTED the
     * declaration is covered by it".
     */
    Testbed_Reset();
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    CHECK(!Porcelain_Require(porcelain, "highlight_groups", "cache highlights"),
          "the requirement is what tells the plugin the lane cannot do this");
    count = Porcelain_Findings(porcelain, findings, 8);
    CHECK(count == 1, "and it is one finding");
    CHECK(!findings[0].expected, "undeclared, at the instant it was recorded");
    Porcelain_ExpectUnsupported(porcelain, "cache highlights", "no CS2 highlight groups here");
    count = Porcelain_Findings(porcelain, findings, 8);
    CHECK(count == 2, "the declaration is its own finding, as before");
    for( int i = 0; i < count; i++ )
        CHECK(findings[i].expected,
              "the refusal that PROMPTED the declaration is covered by it");
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
    /** N SELECTs of three options each; the first carries an over-long one. */
    int option_rows;
    /** The well declares an on_menu. Off by default, because a row that never
     *  asked for secondary clicks must not be handed one. */
    bool well_takes_menu;

    /* What the handlers saw. */
    int actions;
    int action_kind;
    int action_value;
    char action_text[64];
    char action_key[64];
    int paints;
    /* The secondary-click handler's own tally, deliberately NOT `actions`:
     * the point of a separate slot is that the two never run into each other. */
    int menus;
    int menu_x;
    int menu_y;
    char menu_key[64];
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
panel_fixture_menu(struct ToriRS_Api* api, void* user, struct PorcelainRowAction const* action)
{
    struct PanelFixture* fixture = user;
    (void)api;
    fixture->menus++;
    fixture->menu_x = action->x;
    fixture->menu_y = action->y;
    snprintf(fixture->menu_key, sizeof(fixture->menu_key), "%s", action->key);
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

    /*
     * A page of SELECTs that fills the shared option pool EXACTLY, with the
     * first one refused.
     *
     * The pool is per page, so the only place a leak shows is a later row that
     * no longer fits -- and it has to be a page whose rows fit precisely,
     * because a pool with room to spare cannot tell a refusal that gives its
     * options back from one that keeps them.
     */
    if( fixture->option_rows > 0 )
    {
        for( int i = 0; i < fixture->option_rows; i++ )
        {
            /* The FIRST row is the wide one, and its LAST option is the one
             * that cannot be said: four options are in the pool before the
             * fifth is refused, and four is what has to come back. Three
             * apiece for the rest leaves the pool exactly full without it. */
            int const count = i == 0 ? 5 : 3;
            for( int o = 0; o < count; o++ )
            {
                memset(&options[o], 0, sizeof(options[o]));
                options[o].struct_size = sizeof(options[o]);
                options[o].value = (i == 0 && o == count - 1) ? PANEL_LONG_VALUE
                                                              : PANEL_OPTION_VALUES[o];
                options[o].label = PANEL_OPTION_VALUES[o];
                options[o].enabled = true;
            }
            snprintf(PANEL_FLOOD_KEYS[i], sizeof(PANEL_FLOOD_KEYS[i]), "s%d", i);
            memset(&row, 0, sizeof(row));
            row.key = PANEL_FLOOD_KEYS[i];
            row.kind = PORCELAIN_ROW_SELECT;
            row.label = "Pick";
            row.text = "auto";
            row.options = options;
            row.option_count = count;
            describe->row(describe, &row);
        }
        return;
    }

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
        /* The LAST option, not the first: a refusal on the first writes
         * nothing into the shared pool, so it could not tell a rewind that
         * works from one that does not happen. */
        options[i].value =
            (fixture->long_option_value && i == fixture->option_count - 1)
                ? PANEL_LONG_VALUE
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
    if( fixture->well_takes_menu )
        row.on_menu = panel_fixture_menu;
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
 * Renaming a row -- ANY row -- is a setter now.
 *
 * It was not. KEY_VALUE, TOGGLE, SELECT and ACTION_ROW are built from `label`
 * and the host's patch path had no arm for one, so the layer folded the label
 * into the DECLARATION IDENTITY and renaming one of the four rebuilt the page.
 * That was honest about the host it had, and it was the last unnecessary
 * rebuild in the row model: a rebuild is a flash with the scroll thrown away
 * and every retained custom run on the page retired, paid for changing a word.
 * The host has panel.set_label now, so the identity is (key, kind) and nothing
 * else.
 *
 * MUTATION: put row->label back into panel_identity_hash for the four kinds --
 * the renamed SELECT rebuilds again and "renaming a select is a setter too"
 * goes red.
 * MUTATION: drop the label arm from panel_apply_properties -- no rebuild and
 * no setter either, the host keeps the old name, and the name assertion goes
 * red without the rebuild one moving.
 * MUTATION: make panel_kind_has_label answer true for every kind -- the
 * HEADING, whose one string already travels as text, gets a set_label the host
 * refuses for its kind, and the finding assertion goes red.
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
    CHECK(Testbed_LogCountWith("panel_set_label") == 0,
          "and a kind whose one string travels as text is never sent a label");
    CHECK(panel_findings_with(porcelain, PORCELAIN_FINDING_REFUSED) == 0,
          "so nothing is refused for it");
    Porcelain_Close(porcelain);

    /* A SELECT carries a NAME beside its chosen entry, and the host can
     * restate one. Renaming it costs that setter and not the page. */
    Testbed_Reset();
    panel_fixture_init(&fixture);
    porcelain = panel_start(&fixture, PORCELAIN_FACE_BOTH);
    Porcelain_CountersReset(porcelain);
    fixture.select_label = "Game frame";
    panel_restate(porcelain);
    Porcelain_PanelCountersRead(porcelain, &counters);
    CHECK(counters.rebuilds == 0, "renaming a select is a setter too, not a rebuild");
    CHECK(strcmp(panel_row("frame")->label, "Game frame") == 0,
          "and the row the host already holds carries the new name");
    CHECK(Testbed_LogCountWith("panel_set_options") == 0,
          "with its option list left alone: a name is not a choice");
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
 * A duplicate key drops the ROW, and the page it was on stands.
 *
 * It used to poison the whole run, which is the distinction this round had to
 * draw: half a description IS the flicker class, but a row the LAYER refused
 * is not half a description -- the rest of it is complete and correct. Under
 * the old rule one bad key blanked the page it appeared on.
 *
 * MUTATION: drop the duplicate-key scan in Porcelain_Row -- the second row
 * under "head" reaches the host, whose declaration is idempotent on a repeated
 * id, so it silently vanishes and every setter aimed at it lands on the first;
 * the finding assertion goes red.
 * MUTATION: call panel_poison instead of panel_refuse_row -- the whole
 * description is discarded, `dropped_rows` stays at zero and the page-intact
 * assertion goes red with it.
 * MUTATION: rewind to panel->option_count instead of row->option_first in
 * panel_refuse_row -- the SELECT's options are orphaned in the pool, the next
 * row's option_first is wrong by three, and the frame row's list goes red.
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
          "a duplicate key is one finding, naming the key");
    Porcelain_PanelCountersRead(porcelain, &counters);
    CHECK(counters.dropped_rows == 1, "and drops exactly that row");
    CHECK(counters.rebuilds == 0, "the rows that were fine keep the page they were declared on");
    CHECK(Testbed_PanelRowCount() == 4, "which still holds all four of them");
    CHECK(panel_row("frame")->option_count == 3,
          "with the option pool rewound, so no later row reads another's options");
    Porcelain_Close(porcelain);
}

/*
 * An over-long string drops its ROW and tells the plugin which; the page it
 * was on is declared without it.
 *
 * Two separate rules, and the round that wrote this test only had the first.
 * The layer must never TRUNCATE -- a truncated stable value names a different
 * option and reads back as though the person had chosen it. But it also must
 * not discard the description: it used to, so one over-long provider id
 * blanked a whole settings page, and "half a description is the flicker class"
 * was being used to justify throwing away a description that was not half.
 *
 * MUTATION: replace the length test in panel_copy_or_refuse with a truncating
 * copy -- the option's stable value becomes a prefix, the row publishes a
 * choice nobody offered, and the refusal assertion goes red.
 * MUTATION: call panel_poison instead of panel_refuse_row for the string
 * ceiling -- the whole page is discarded, and both the dropped-row count and
 * the surviving-rows assertion go red.
 * MUTATION: lower PORCELAIN_OPTION_VALUE_MAX back to 96 -- a value the HOST
 * would have taken is refused by the layer, and the ceilings-agree assertion
 * goes red.
 * MUTATION: raise PORCELAIN_ROWS_MAX above the flood count -- the overrun
 * assertion goes red because nothing overruns.
 */
static void
test_panel_refuses_rather_than_truncates(void)
{
    struct PanelFixture fixture;
    struct PorcelainPanelCounters counters;
    struct Porcelain* porcelain;

    memset(PANEL_LONG_VALUE, 'v', sizeof(PANEL_LONG_VALUE) - 1);
    PANEL_LONG_VALUE[sizeof(PANEL_LONG_VALUE) - 1] = '\0';

    /* The layer's ceiling is the HOST's ceiling. A narrower one is the layer
     * inventing a refusal that is about itself and not about the lane. */
    CHECK(PORCELAIN_OPTION_VALUE_MAX == PORCELAIN_CONFIG_VALUE_MAX,
          "an option value is refused where the host would refuse it");
    CHECK(PORCELAIN_OPTION_LABEL_MAX == PORCELAIN_CONFIG_VALUE_MAX,
          "and so is its label");
    CHECK(PORCELAIN_OPTION_DETAIL_MAX == PORCELAIN_CONFIG_VALUE_MAX,
          "and its detail");

    Testbed_Reset();
    panel_fixture_init(&fixture);
    porcelain = panel_start(&fixture, PORCELAIN_FACE_BOTH);
    Porcelain_CountersReset(porcelain);
    fixture.long_option_value = true;
    panel_restate(porcelain);
    CHECK(panel_findings_with(porcelain, PORCELAIN_FINDING_REFUSED) == 1,
          "an option value over the ceiling is refused, never truncated");
    CHECK(Testbed_LogCountWith("panel_set_options") == 0, "and never reaches the row");
    Porcelain_PanelCountersRead(porcelain, &counters);
    CHECK(counters.dropped_rows == 1, "the row it was on is dropped, and only that row");
    /* The SELECT is gone, so the row SEQUENCE changed and the page is declared
     * again without it -- which is the legitimate rebuild, not a discard. */
    Testbed_PanelBuild(porcelain, TORIRS_PANEL_VIEW_PAGE);
    CHECK(Testbed_PanelRowCount() == 3, "and the other three rows are still a page");
    /* Testbed_PanelRow, not the panel_row() helper: that one answers a zeroed
     * static for a missing id, which would make an absence look like a row. */
    CHECK(Testbed_PanelRow("head") != NULL, "the heading survived a refusal below it");
    CHECK(Testbed_PanelRow("pause") != NULL, "and so did the button after it");
    CHECK(Testbed_PanelRow("boxes") != NULL, "and so did the well after that");
    CHECK(Testbed_PanelRow("frame") == NULL,
          "while the row that could not be said is not there");

    /*
     * And the options the refused row had already written are given back.
     *
     * The pool is shared by every row on the page, so options orphaned in it
     * are not merely wasted: they are the offset every LATER row's option
     * slice is measured from. Nothing on one page makes that visible, which is
     * why the leak is measured over many describes -- without the rewind the
     * pool loses two slots per run and reaches its ceiling, and a description
     * that always fitted starts answering a budget finding.
     */
    Testbed_Reset();
    panel_fixture_init(&fixture);
    /* The rows that SURVIVE fill the pool exactly: 42 of three options each
     * is 126 of the 128, and the refused first row had taken four. */
    fixture.option_rows = PORCELAIN_ROW_OPTIONS_MAX / 3 + 1;
    porcelain = panel_start(&fixture, PORCELAIN_FACE_BOTH);
    CHECK(panel_findings_with(porcelain, PORCELAIN_FINDING_REFUSED) == 1,
          "the one row that cannot be said is refused");
    CHECK(panel_findings_with(porcelain, PORCELAIN_FINDING_BUDGET) == 0,
          "and the options it had already taken are given back, so nothing overruns");
    CHECK(Testbed_PanelRowCount() == fixture.option_rows - 1,
          "leaving every other row on the page");
    CHECK(Testbed_PanelRow("s0") == NULL, "without the refused one");
    CHECK(panel_row("s1")->option_count == 3, "and the row after it with its own three");
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
/* Frames                                                                   */
/* ------------------------------------------------------------------------ */

struct FrameFixture
{
    char const* key;
    bool unsupported;
    bool wants_viewport;
    int runs;
};

static void
frame_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    struct FrameFixture* fixture = user;
    struct PorcelainElementState state;
    struct PorcelainItem item;

    fixture->runs++;
    if( fixture->unsupported )
    {
        describe->unsupported(describe, "this is a desktop frame; choose Stone Drawer");
        return;
    }
    /* The viewport-bind gate every provider opens with. It is also what gives
     * a canvas placement its frame root: the root is walked up from a bound
     * element, and a description that watches nothing has no root to hang
     * from. */
    (void)Porcelain_Element(describe->porcelain, PORCELAIN_EL(VIEWPORT), &state);
    memset(&item, 0, sizeof(item));
    item.key = fixture->key;
    item.image = "piece.png";
    if( fixture->wants_viewport )
    {
        item.place.kind = PORCELAIN_AT_ELEMENT;
        item.place.on = PORCELAIN_EL(VIEWPORT);
    }
    else
    {
        item.place.kind = PORCELAIN_AT_CANVAS;
    }
    item.w = 16;
    item.h = 16;
    describe->piece(describe, &item);
}

/* The desktop frame's map housing: a plate over the COMPASS, the later of the
 * two surfaces it frames, placed beside the MINIMAP. */
static void
housing_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    struct FrameFixture* fixture = user;
    struct PorcelainItem item;

    fixture->runs++;
    memset(&item, 0, sizeof(item));
    item.key = fixture->key;
    item.image = "piece.png";
    item.place.kind = PORCELAIN_AT_ELEMENT;
    item.place.on = PORCELAIN_EL(MINIMAP);
    item.place.depth = PORCELAIN_EL(COMPASS);
    item.w = 172;
    item.h = 156;
    describe->piece(describe, &item);
}

/* A surround piece at a canvas coordinate that also states a depth target. */
static void
canvas_depth_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    struct FrameFixture* fixture = user;
    struct PorcelainItem item;

    fixture->runs++;
    memset(&item, 0, sizeof(item));
    item.key = fixture->key;
    item.image = "piece.png";
    item.place.kind = PORCELAIN_AT_CANVAS;
    item.place.depth = PORCELAIN_EL(VIEWPORT);
    item.w = 32;
    item.h = 32;
    describe->piece(describe, &item);
}

static struct ToriRS_GameframeEvent
frame_event(char const* id, bool active, int width, int height, char* reason,
            size_t reason_capacity)
{
    struct ToriRS_GameframeEvent event;

    memset(&event, 0, sizeof(event));
    event.offer_id = id;
    event.active = active;
    event.canvas = TORIRS_FRAME_CANVAS_FIXED;
    event.width = width;
    event.height = height;
    event.safe.width = width;
    event.safe.height = height;
    event.reason = reason;
    event.reason_capacity = reason_capacity;
    return event;
}

/*
 * One describe function per offer, and a RELEASE that takes the frame off.
 *
 * Mutation: make Porcelain_FrameEvent's release arm return without running
 * the empty describe and "a release removes every owned control" goes red --
 * which is the shipped providers' own defect, an undress that runs from
 * on_frame_start on a provider the host will never start again.
 */
static void
test_frame_offer_describes_and_releases(void)
{
    struct FrameFixture classic = {.key = "classic_piece"};
    struct FrameFixture modern = {.key = "modern_piece"};
    struct Porcelain* porcelain;
    char reason[TORIRS_FRAME_REASON_MAX];
    struct ToriRS_GameframeEvent event;

    Testbed_Reset();
    Testbed_DeclareElement("viewport", 4, 4, 512, 334);
    Testbed_BindElement("viewport");
    Testbed_DeclareAsset("piece.png", TORIRS_ASSET_READY);
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Frame(porcelain, "classic-fixed", TORIRS_FRAME_CANVAS_FIXED, 765, 503,
                    frame_describe, &classic);
    Porcelain_Frame(porcelain, "modern-fixed", TORIRS_FRAME_CANVAS_FIXED, 765, 503,
                    frame_describe, &modern);

    reason[0] = '\0';
    event = frame_event("classic-fixed", true, 765, 503, reason, sizeof(reason));
    CHECK(Porcelain_FrameEvent(porcelain, &event) == TORIRS_FRAME_READY,
          "an offer whose description applies answers READY");
    Porcelain_Commit(Testbed_Api());
    /* At least one: the fence takes a second pass when registering a watch
     * inside the run raises BOUND synchronously. What matters here is WHICH
     * description ran. */
    CHECK(classic.runs >= 1, "the offer's OWN describe ran");
    CHECK(modern.runs == 0, "and the other offer's did not");
    CHECK(Testbed_Control("classic_piece") != NULL, "the described piece exists");

    reason[0] = '\0';
    event = frame_event("modern-fixed", true, 765, 503, reason, sizeof(reason));
    CHECK(Porcelain_FrameEvent(porcelain, &event) == TORIRS_FRAME_READY,
          "switching offers re-describes");
    Porcelain_Commit(Testbed_Api());
    CHECK(modern.runs >= 1, "the second offer's describe ran");
    CHECK(Testbed_Control("modern_piece") != NULL, "its piece exists");
    CHECK(Testbed_Control("classic_piece") == NULL, "and the first offer's is gone");

    reason[0] = '\0';
    event = frame_event("modern-fixed", false, 765, 503, reason, sizeof(reason));
    (void)Porcelain_FrameEvent(porcelain, &event);
    Porcelain_Commit(Testbed_Api());
    CHECK(Testbed_LiveControls() == 0, "a release removes every owned control");
    Porcelain_Close(porcelain);
}

/*
 * The frame arm of Porcelain_Unsupported: a refusal inside the description
 * has to reach the host as UNSUPPORTED with the reason, or the lane's own
 * frame comes down and nothing replaces it.
 *
 * Mutation: drop the Porcelain_FrameNoteUnsupported call from
 * Porcelain_Unsupported and "unsupported inside a frame describe answers
 * UNSUPPORTED" goes red while the finding still stands -- which is exactly
 * how a silent refusal looks.
 */
static void
test_frame_unsupported_reaches_the_host(void)
{
    struct FrameFixture fixture = {.key = "piece", .unsupported = true};
    struct Porcelain* porcelain;
    struct PorcelainFinding findings[8];
    char reason[TORIRS_FRAME_REASON_MAX];
    struct ToriRS_GameframeEvent event;

    Testbed_Reset();
    Testbed_DeclareElement("viewport", 4, 4, 512, 334);
    Testbed_BindElement("viewport");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Frame(porcelain, "classic-fixed", TORIRS_FRAME_CANVAS_FIXED, 765, 503,
                    frame_describe, &fixture);
    reason[0] = '\0';
    event = frame_event("classic-fixed", true, 765, 503, reason, sizeof(reason));
    CHECK(Porcelain_FrameEvent(porcelain, &event) == TORIRS_FRAME_UNSUPPORTED,
          "unsupported inside a frame describe answers UNSUPPORTED");
    CHECK(strcmp(reason, "this is a desktop frame; choose Stone Drawer") == 0,
          "and writes the reason the host shows");
    CHECK(Porcelain_Findings(porcelain, findings, 8) == 1, "with one finding beside it");
    Porcelain_Close(porcelain);
}

/*
 * PENDING is re-asked, every fence. The one-shot PENDING nobody re-asked is
 * the defect that kept the desktop provider from ever coming up.
 *
 * Mutation: make porcelain_frame_run_pending return false and "a description
 * waiting for the scene answers PENDING" goes red -- the host would take the
 * frame as laid out while nothing had been placed.
 */
static void
test_frame_pending_until_the_lane_binds(void)
{
    struct FrameFixture fixture = {.key = "piece", .wants_viewport = true};
    struct Porcelain* porcelain;
    char reason[TORIRS_FRAME_REASON_MAX];
    struct ToriRS_GameframeEvent event;

    Testbed_Reset();
    Testbed_DeclareElement("viewport", 4, 4, 512, 334);
    Testbed_DeclareAsset("piece.png", TORIRS_ASSET_READY);
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Frame(porcelain, "stone-drawer", TORIRS_FRAME_CANVAS_WINDOW, 640, 437,
                    frame_describe, &fixture);

    reason[0] = '\0';
    event = frame_event("stone-drawer", true, 1280, 720, reason, sizeof(reason));
    CHECK(Porcelain_FrameEvent(porcelain, &event) == TORIRS_FRAME_PENDING,
          "a description waiting for the scene answers PENDING");
    CHECK(reason[0] != '\0', "and says what it is waiting for");
    Porcelain_Commit(Testbed_Api());

    Testbed_BindElement("viewport");
    reason[0] = '\0';
    event = frame_event("stone-drawer", true, 1280, 720, reason, sizeof(reason));
    CHECK(Porcelain_FrameEvent(porcelain, &event) == TORIRS_FRAME_READY,
          "the next fence re-asks and the frame comes up");
    Porcelain_Commit(Testbed_Api());
    CHECK(Testbed_Control("piece") != NULL, "with the piece placed");
    Porcelain_Close(porcelain);
}

/*
 * An offer nobody described, and a plugin that describes no frame at all.
 * Both are refusals with a finding, never a dropped answer: the lane's frame
 * is already coming down by the time this is asked.
 *
 * Mutation: return TORIRS_FRAME_READY from either refusal arm and the checks
 * below go red.
 */
static void
test_frame_event_refusals(void)
{
    struct FrameFixture fixture = {.key = "piece"};
    struct Porcelain* porcelain;
    struct PorcelainFinding findings[8];
    char reason[TORIRS_FRAME_REASON_MAX];
    struct ToriRS_GameframeEvent event;

    Testbed_Reset();
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    reason[0] = '\0';
    event = frame_event("classic-fixed", true, 765, 503, reason, sizeof(reason));
    CHECK(Porcelain_FrameEvent(porcelain, &event) == TORIRS_FRAME_UNSUPPORTED,
          "a plugin that described no frame refuses the offer");
    CHECK(Porcelain_Findings(porcelain, findings, 8) == 1, "with one finding");
    Porcelain_Close(porcelain);

    Testbed_Reset();
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Frame(porcelain, "classic-fixed", TORIRS_FRAME_CANVAS_FIXED, 765, 503,
                    frame_describe, &fixture);
    reason[0] = '\0';
    event = frame_event("modern-resizable", true, 765, 503, reason, sizeof(reason));
    CHECK(Porcelain_FrameEvent(porcelain, &event) == TORIRS_FRAME_UNSUPPORTED,
          "an offer id nothing is bound to refuses too");
    CHECK(strstr(reason, "modern-resizable") != NULL, "and the reason names it");
    CHECK(fixture.runs == 0, "and no other offer's description was run in its place");
    Porcelain_Close(porcelain);
}

/*
 * USABLE subtracts a lane strip only when it is PRESENTED and spans a full
 * edge. 601's strip is bound, laid out 58x46 and hidden: it cuts nothing, and
 * a plan that moved the whole frame because the role existed would be wrong
 * on every capture of that root.
 *
 * Mutation: drop the `!watch->state.presented` test in porcelain_usable and
 * "a hidden strip cuts nothing" goes red.
 */
static void
test_usable_subtracts_only_a_presented_strip(void)
{
    struct Porcelain* porcelain;
    struct ToriRS_WidgetBounds usable;
    struct PorcelainElementState state;

    /* Nothing has bound: there is no frame root and so no canvas to report.
     * False, not a 0x0 box, because those are different answers. */
    Testbed_Reset();
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    CHECK(!Porcelain_Usable(porcelain, &usable),
          "before anything binds there is no usable canvas");
    Porcelain_Close(porcelain);

    /* The 601 shape: bound, laid out, hidden. */
    Testbed_Reset();
    Testbed_DeclareElement("viewport", 0, 0, 800, 500);
    Testbed_BindElement("viewport");
    Testbed_DeclareElement("lane_chrome_0", 11, 432, 58, 46);
    Testbed_BindElement("lane_chrome_0");
    Testbed_PresentElement("lane_chrome_0", false);
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    (void)Porcelain_Element(porcelain, PORCELAIN_CHROME_EL(0), &state);
    (void)Porcelain_Element(porcelain, PORCELAIN_EL(VIEWPORT), &state);
    Porcelain_Fence(porcelain);
    Porcelain_Commit(Testbed_Api());
    CHECK(Porcelain_Usable(porcelain, &usable), "with a frame root there is one");
    CHECK(usable.width == 800 && usable.height == 500, "a hidden strip cuts nothing");
    Porcelain_Close(porcelain);

    /* The same full-height strip, hidden. The shape above proves the box
     * test; this proves the PRESENTED one, and they are different halves:
     * 601's strip is small enough that the box test alone would spare it, and
     * a strip that is the right shape and simply not drawn is the case that
     * moves the whole frame for nothing. */
    Testbed_Reset();
    Testbed_DeclareElement("viewport", 0, 0, 800, 500);
    Testbed_BindElement("viewport");
    Testbed_DeclareElement("lane_chrome_0", 760, 0, 40, 500);
    Testbed_BindElement("lane_chrome_0");
    Testbed_PresentElement("lane_chrome_0", false);
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    (void)Porcelain_Element(porcelain, PORCELAIN_CHROME_EL(0), &state);
    (void)Porcelain_Element(porcelain, PORCELAIN_EL(VIEWPORT), &state);
    Porcelain_Fence(porcelain);
    Porcelain_Commit(Testbed_Api());
    CHECK(Porcelain_Usable(porcelain, &usable), "the canvas is answered");
    CHECK(usable.width == 800,
          "a strip the right shape to cut an edge still cuts nothing while it is hidden");
    Porcelain_Close(porcelain);

    /* A presented, full-height strip docked on the right moves that edge in. */
    Testbed_Reset();
    Testbed_DeclareElement("viewport", 0, 0, 800, 500);
    Testbed_BindElement("viewport");
    Testbed_DeclareElement("lane_chrome_0", 760, 0, 40, 500);
    Testbed_BindElement("lane_chrome_0");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    (void)Porcelain_Element(porcelain, PORCELAIN_CHROME_EL(0), &state);
    (void)Porcelain_Element(porcelain, PORCELAIN_EL(VIEWPORT), &state);
    Porcelain_Fence(porcelain);
    Porcelain_Commit(Testbed_Api());
    CHECK(Porcelain_Usable(porcelain, &usable), "the canvas is still answered");
    CHECK(usable.x == 0 && usable.width == 760,
          "a presented strip that spans a full edge moves that edge in");
    Porcelain_Close(porcelain);

    /*
     * The same strip, and NOBODY asked for the chrome element first.
     *
     * Every case above opens with Porcelain_Element(CHROME_EL(0)), which is
     * what registers the watch -- and the rect was read off an EXISTING watch
     * and quietly answered the un-subtracted root when there was none. A
     * caller that simply asks for the usable canvas, which is the only thing
     * this verb is for, got the whole root and no sign anything was missing.
     * Measured on the desktop frame: the resizable layout took OldSchool's
     * 807-column canvas whole, which pushed the popout strip out to 807, which
     * grew the canvas to 849, a frame at a time.
     *
     * Mutation: pass `false` to Porcelain_WatchFor in porcelain_usable and
     * this goes red while every case above stays green.
     */
    Testbed_Reset();
    Testbed_DeclareElement("viewport", 0, 0, 800, 500);
    Testbed_BindElement("viewport");
    Testbed_DeclareElement("lane_chrome_0", 760, 0, 40, 500);
    Testbed_BindElement("lane_chrome_0");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    (void)Porcelain_Element(porcelain, PORCELAIN_EL(VIEWPORT), &state);
    Porcelain_Fence(porcelain);
    Porcelain_Commit(Testbed_Api());
    /* The first ask is what registers the watch; the fence after it is what
     * binds and stamps. A verb that registers nothing never gets either. */
    (void)Porcelain_Usable(porcelain, &usable);
    Porcelain_Fence(porcelain);
    Porcelain_Commit(Testbed_Api());
    CHECK(Porcelain_Usable(porcelain, &usable), "the canvas is answered");
    CHECK(usable.x == 0 && usable.width == 760,
          "and the strip is subtracted for a caller that watched nothing itself");
    Porcelain_Close(porcelain);
}

/* A re-skin of the compass, art and mask, for the asset-arrival case. */
static void
skin_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    (void)user;
    describe->skin(describe, PORCELAIN_EL(COMPASS), "rose.png", NULL);
}

/*
 * A re-skin whose picture has not decoded yet is asked AGAIN.
 *
 * A skin is applied only when the description changed, and an asset landing
 * changes no description: same element, same name, same hash. So a provider
 * that named its compass rose on the first describe -- which is what a frame
 * provider does, because the description IS the frame -- had the write dropped
 * once and the lane kept its own rose for the session. The frame's own art
 * crosses the IO queue like everything else and is never ready on the fence
 * that first names it.
 *
 * Mutation: replace the `applied->edit.hash = 0` in porcelain_apply_edit's
 * SKIN arm with a no-op and "the skin lands once its picture decodes" goes red
 * with zero set_image calls.
 */
static void
test_skin_waits_for_its_picture(void)
{
    struct Porcelain* porcelain;

    Testbed_Reset();
    Testbed_DeclareElement("compass", 545, 4, 33, 33);
    Testbed_BindElement("compass");
    Testbed_DeclareAsset("rose.png", TORIRS_ASSET_PENDING);
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, skin_describe, NULL);
    fence(porcelain);
    CHECK(Testbed_LogCountWith("set_image compass") == 0,
          "a picture that has not decoded is not written");

    Testbed_LandAsset("rose.png");
    fence(porcelain);
    fence(porcelain);
    CHECK(Testbed_LogCountWith("set_image compass") > 0,
          "the skin lands once its picture decodes");
    Porcelain_Close(porcelain);
}

/*
 * NativeSize is asked by ELEMENT, and that is the whole of the G55 fix: both
 * shipped providers pass their own private surface enum straight in as the
 * API's, the two numberings disagree, and CHAT matches by luck.
 *
 * Mutation: swap TORIRS_SURFACE_COMPASS and TORIRS_SURFACE_SIDEBAR in
 * porcelain_surface_of and "COMPASS asks for the compass surface" goes red
 * with the exact wrong-surface answer the defect produces.
 */
static void
test_native_size_is_asked_by_element(void)
{
    struct Porcelain* porcelain;
    struct ToriRS_WidgetBounds box;
    struct PorcelainFinding findings[8];

    Testbed_Reset();
    /* Stated so the refusals below cannot pass by accident: an element that
     * is no surface has to refuse even when surface zero is answerable. */
    Testbed_DeclareSurface(TORIRS_SURFACE_VIEWPORT, -1, 0, 0, 512, 334);
    Testbed_DeclareSurface(TORIRS_SURFACE_CHAT, -1, 0, 0, 519, 165);
    Testbed_DeclareSurface(TORIRS_SURFACE_COMPASS, -1, 0, 0, 33, 33);
    Testbed_DeclareSurface(TORIRS_SURFACE_ORBS, -1, 0, 0, 207, 197);
    /* 601's globe: 34x34 inset from the block, where 548 draws it 30x30. */
    Testbed_DeclareSurface(TORIRS_SURFACE_ORBS, 1, 153, 45, 34, 34);
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);

    CHECK(Porcelain_NativeSize(porcelain, PORCELAIN_EL(CHAT), &box), "the lane states the chat");
    CHECK(box.width == 519 && box.height == 165, "at the size the pack authored");
    CHECK(box.x == 0 && box.y == 0, "a whole surface has no offset inside itself");

    CHECK(Porcelain_NativeSize(porcelain, PORCELAIN_EL(COMPASS), &box),
          "COMPASS asks for the compass surface");
    CHECK(box.width == 33, "and gets the compass's own size");

    CHECK(Porcelain_NativeSize(porcelain, PORCELAIN_EL(ORBS), &box), "ORBS is the block");
    CHECK(box.width == 207 && box.height == 197, "at the block's authored size");

    {
        struct PorcelainElement const globe = {PORCELAIN_EL_ORBS, 1, NULL};
        CHECK(Porcelain_NativeSize(porcelain, globe, &box), "ORBS[1] is a member of the block");
        CHECK(box.x == 153 && box.y == 45, "answered BLOCK-relative");
        CHECK(box.width == 34 && box.height == 34,
              "at this root's own size, not the one a different toplevel draws");
    }

    CHECK(Porcelain_Findings(porcelain, findings, 8) == 0, "and none of that is a finding");

    /* A surface the lane states no pixel size for. Not zero: a proportional
     * box has no pixel count, and the caller has to know it must choose. */
    CHECK(!Porcelain_NativeSize(porcelain, PORCELAIN_EL(MINIMAP), &box),
          "a surface with no stated pixel size refuses");
    /* An element that is no surface at all. */
    CHECK(!Porcelain_NativeSize(porcelain, PORCELAIN_EL(CANVAS), &box),
          "and the derived elements are not surfaces");
    CHECK(Porcelain_Findings(porcelain, findings, 8) == 2, "each refusal is one finding");
    Porcelain_Close(porcelain);
}

/*
 * ORB names a stat orb by profile role; ORBS numbers the members of the
 * block. They are not the same set -- member 0 of the block is the activity
 * adviser, not the hitpoints globe -- so ORB has no member box to answer.
 *
 * Mutation: return element.member from the PORCELAIN_EL_ORB arm of
 * porcelain_surface_member and this goes red by answering the adviser's box
 * for the run orb.
 */
static void
test_orb_kinds_are_not_block_members(void)
{
    struct Porcelain* porcelain;
    struct ToriRS_WidgetBounds box;

    Testbed_Reset();
    Testbed_DeclareSurface(TORIRS_SURFACE_ORBS, -1, 0, 0, 207, 197);
    Testbed_DeclareSurface(TORIRS_SURFACE_ORBS, 2, 85, 143, 30, 30);
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    CHECK(!Porcelain_NativeSize(porcelain, PORCELAIN_ORB_EL(PORCELAIN_ORB_RUN), &box),
          "a stat orb is not a numbered member of the orb block");
    Porcelain_Close(porcelain);
}

/*
 * A member the lane does not number is a refusal, and a loud one.
 *
 * Mutation: drop the `is_member && member < 0` arm and a tab this lane never
 * numbered is asked of surface member -1, which is NativeSize's own question
 * and would answer the whole sidebar as though it were the tab's mount.
 */
static void
test_native_size_member_refusals(void)
{
    struct Porcelain* porcelain;
    struct ToriRS_WidgetBounds box;

    Testbed_Reset();
    Testbed_DeclareSurface(TORIRS_SURFACE_SIDEBAR, -1, 0, 0, 190, 261);
    Testbed_DeclareSurface(TORIRS_SURFACE_SIDEBAR, 3, 0, 0, 190, 261);
    snprintf(g_testbed.named_ids, sizeof(g_testbed.named_ids), "tab:inventory=3 ");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    CHECK(Porcelain_NativeSize(porcelain, PORCELAIN_TAB_EL("inventory"), &box),
          "a tab the lane numbers answers with its mount's box");
    CHECK(box.width == 190 && box.height == 261, "the sidebar's own rectangle");
    CHECK(!Porcelain_NativeSize(porcelain, PORCELAIN_TAB_EL("clan"), &box),
          "a tab this lane does not number has no box");
    Porcelain_Close(porcelain);
}

/*
 * LaneIcon answers the lane's own number for a tab, and answers -1 for a tab
 * the lane NUMBERS but does not MOUNT. The two are different questions, and
 * answering only the first is the recorded defect: a 2004 rail over an
 * OldSchool lane wearing the 2004 icon set left the live seventh rock bare
 * and put the ignore face on Friends.
 *
 * Mutation: delete the Porcelain_Element check and "a numbered tab the lane
 * never mounts has no icon" goes red.
 */
static void
test_lane_icon_needs_the_tab_and_the_panel(void)
{
    struct Porcelain* porcelain;

    Testbed_Reset();
    snprintf(g_testbed.named_ids, sizeof(g_testbed.named_ids), "tab:inventory=3 tab:clan=7 ");
    Testbed_DeclareElement("sidetab_3", 660, 200, 30, 30);
    Testbed_BindElement("sidetab_3");
    /* Declared and never bound: the lane numbers it and mounts nothing. */
    Testbed_DeclareElement("sidetab_7", 660, 240, 30, 30);
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    CHECK(Porcelain_LaneIcon(porcelain, PORCELAIN_TAB_EL("inventory")) == 3,
          "a mounted tab answers this lane's own number for it");
    CHECK(Porcelain_LaneIcon(porcelain, PORCELAIN_TAB_EL("clan")) == -1,
          "a numbered tab the lane never mounts has no icon");
    CHECK(Porcelain_LaneIcon(porcelain, PORCELAIN_TAB_EL("music")) == -1,
          "and a tab this lane does not number at all has none either");
    Porcelain_Close(porcelain);
}

/* Four stones, two rows -- the 548/161 shape, with nothing stated. */
static void
frames_declare_two_rows(void)
{
    Testbed_DeclareElement("sidetab_0", 550, 470, 30, 30);
    Testbed_BindElement("sidetab_0");
    Testbed_DeclareElement("sidetab_1", 520, 470, 30, 30);
    Testbed_BindElement("sidetab_1");
    Testbed_DeclareElement("sidetab_3", 520, 430, 30, 30);
    Testbed_BindElement("sidetab_3");
    Testbed_DeclareElement("sidetab_10", 550, 430, 30, 30);
    Testbed_BindElement("sidetab_10");
    snprintf(g_testbed.named_ids, sizeof(g_testbed.named_ids),
             "tab:combat=0 tab:stats=1 tab:inventory=3 tab:logout=10 ");
}

/*
 * With no `[tabs:<root>]` section the arrangement is DERIVED from the boxes
 * the lane's own stones report: a group is a shared row, and within a row the
 * order is x ascending. That is right on 548, on 161 and on a root the
 * profile never listed, which is the case the whole design turns on.
 *
 * Mutation: sort on `order` alone (drop the group compare from the insertion
 * sort) and "the top row holds inventory then logout" goes red by mixing the
 * two rows together.
 */
static void
test_tab_groups_derive_from_the_boxes(void)
{
    struct Porcelain* porcelain;
    struct PorcelainElement group[PORCELAIN_TAB_MAX];
    int count;

    Testbed_Reset();
    frames_declare_two_rows();
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);

    CHECK(Porcelain_TabGroupCount(porcelain, PORCELAIN_TAB_ROWS) == 2,
          "four stones on two y values are two rows");
    CHECK(Porcelain_TabGroupCount(porcelain, PORCELAIN_TAB_COLUMNS) == 2,
          "and on two x values, two columns");

    count = Porcelain_TabGroup(porcelain, PORCELAIN_TAB_ROWS, 0, group, PORCELAIN_TAB_MAX);
    CHECK(count == 2, "the top row holds two stones");
    CHECK(count == 2 && strcmp(group[0].role, "inventory") == 0 &&
              strcmp(group[1].role, "logout") == 0,
          "the top row holds inventory then logout, in screen order");

    count = Porcelain_TabGroup(porcelain, PORCELAIN_TAB_ROWS, 1, group, PORCELAIN_TAB_MAX);
    CHECK(count == 2, "the bottom row holds the other two");
    CHECK(count == 2 && strcmp(group[0].role, "stats") == 0 &&
              strcmp(group[1].role, "combat") == 0,
          "and stats sits left of combat, which walking the tab NUMBERS would reverse");
    Porcelain_Close(porcelain);
}

/*
 * A root that states its arrangement wins over the derivation, and the root
 * is a KEY -- `cache.frame_root()` joined to the tab name -- never a compare.
 * 601 stacks thirteen stones in two columns that a row walk reads as thirteen
 * rows of one, and 164 hangs logout off the top bar where a row walk puts it
 * at the head of the run.
 *
 * Mutation: ignore the "tabcol"/"tabpos" answers (always take the derived
 * branch) and the stated order below goes red.
 */
static void
test_tab_groups_take_the_stated_override(void)
{
    struct Porcelain* porcelain;
    struct PorcelainElement group[PORCELAIN_TAB_MAX];
    struct PorcelainElement detached;
    int count;

    Testbed_Reset();
    frames_declare_two_rows();
    Testbed_SetFrameRoot(601);
    /* The profile's own rows, keyed <root>:<name>. The boxes above say two
     * rows; the section says one column in an order the boxes do not have. */
    snprintf(g_testbed.named_ids, sizeof(g_testbed.named_ids),
             "tab:combat=0 tab:stats=1 tab:inventory=3 tab:logout=10 "
             "tabcol:601:inventory=0 tabpos:601:inventory=0 "
             "tabcol:601:combat=0 tabpos:601:combat=1 "
             "tabcol:601:stats=0 tabpos:601:stats=2 "
             "tabdetach:601:logout=1 ");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);

    CHECK(Porcelain_TabGroupCount(porcelain, PORCELAIN_TAB_COLUMNS) == 1,
          "the stated arrangement is one column");
    count = Porcelain_TabGroup(porcelain, PORCELAIN_TAB_COLUMNS, 0, group, PORCELAIN_TAB_MAX);
    CHECK(count == 3, "holding the three tabs it names");
    CHECK(count == 3 && strcmp(group[0].role, "inventory") == 0 &&
              strcmp(group[1].role, "combat") == 0 && strcmp(group[2].role, "stats") == 0,
          "in the order the profile stated, not the one the boxes derive");

    detached = Porcelain_TabDetached(porcelain);
    CHECK(detached.kind == PORCELAIN_EL_TAB, "the root hangs one tab outside every group");
    CHECK(detached.role && strcmp(detached.role, "logout") == 0, "and it is logout");
    Porcelain_Close(porcelain);
}

/*
 * A detached tab is in no group at all, which is what detached MEANS -- and a
 * root that detaches nothing answers NONE rather than an absent tab.
 *
 * Mutation: drop the tabdetach skip in porcelain_tab_seats and logout comes
 * back into the derived row, which is the hard FRAME_TAB_SCREEN_ORDER table's
 * own bug.
 */
static void
test_detached_tab_is_in_no_group(void)
{
    struct Porcelain* porcelain;
    struct PorcelainElement group[PORCELAIN_TAB_MAX];
    struct PorcelainElement detached;
    int count;

    Testbed_Reset();
    frames_declare_two_rows();
    Testbed_SetFrameRoot(164);
    snprintf(g_testbed.named_ids, sizeof(g_testbed.named_ids),
             "tab:combat=0 tab:stats=1 tab:inventory=3 tab:logout=10 "
             "tabdetach:164:logout=1 ");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    count = Porcelain_TabGroup(porcelain, PORCELAIN_TAB_ROWS, 0, group, PORCELAIN_TAB_MAX);
    CHECK(count == 1, "the top row is one stone once logout is off it");
    CHECK(count == 1 && strcmp(group[0].role, "inventory") == 0, "and it is inventory");
    detached = Porcelain_TabDetached(porcelain);
    CHECK(detached.role && strcmp(detached.role, "logout") == 0, "logout is the detached one");
    Porcelain_Close(porcelain);

    Testbed_Reset();
    frames_declare_two_rows();
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    detached = Porcelain_TabDetached(porcelain);
    CHECK(detached.kind == PORCELAIN_EL_NONE,
          "a root that detaches nothing answers NONE, not an absent tab");
    Porcelain_Close(porcelain);
}

/*
 * A capacity smaller than the group is a budget finding and a short answer,
 * never a silently truncated arrangement.
 *
 * Mutation: drop the finding and the group comes back short with nothing
 * saying so -- the stone the frame never draws.
 */
static void
test_tab_group_capacity_is_a_finding(void)
{
    struct Porcelain* porcelain;
    struct PorcelainElement group[1];
    struct PorcelainFinding findings[8];
    int count;

    Testbed_Reset();
    frames_declare_two_rows();
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    count = Porcelain_TabGroup(porcelain, PORCELAIN_TAB_ROWS, 0, group, 1);
    CHECK(count == 1, "a short buffer takes what it can");
    CHECK(Porcelain_Findings(porcelain, findings, 8) == 1, "and says it was short");
    CHECK(findings[0].result == PORCELAIN_FINDING_BUDGET, "as a budget finding");
    Porcelain_Close(porcelain);
}

/*
 * A depth target has to PAINT, not merely resolve.
 *
 * The live defect the minimap-orbs port measured 1:1 across the six minimap
 * states: the desktop frame's housing plate anchors OVER whichever of compass
 * or minimap RESOLVES, and on the three states that suppress the compass the
 * plate keeps its own native draw index -- later than the orb column -- and
 * paints over the whole thing, the lane's own art included.
 *
 * Mutation: restore `if( Porcelain_Element(...) ) anchor = depth_state.ref;`
 * in porcelain_apply_anchor and "a depth target that draws nothing is not
 * anchored to" goes red with exactly that picture.
 */
static void
test_depth_target_must_paint(void)
{
    struct Porcelain* porcelain;
    struct FrameFixture fixture = {.key = "housing"};
    struct PorcelainFinding findings[8];
    struct TestbedControl const* control;
    struct TestbedElement const* minimap;

    Testbed_Reset();
    Testbed_DeclareElement("minimap", 550, 4, 146, 151);
    Testbed_BindElement("minimap");
    Testbed_DeclareElement("compass", 517, 4, 33, 33);
    Testbed_BindElement("compass");
    Testbed_PresentElement("compass", false);
    Testbed_DeclareAsset("piece.png", TORIRS_ASSET_READY);
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, housing_describe, &fixture);
    fence(porcelain);

    control = Testbed_Control("housing");
    minimap = Testbed_Element("minimap");
    CHECK(control != NULL, "the housing plate is placed");
    CHECK(control && minimap && ToriRS_WidgetRefEqual(control->anchor, minimap->ref),
          "a depth target that draws nothing is not anchored to");
    CHECK(Porcelain_Findings(porcelain, findings, 8) == 1, "and the provider is told it lost");
    Porcelain_Close(porcelain);
}

/*
 * A canvas placement costs no anchor, and STATING a depth target on one buys
 * the anchor back.
 *
 * Both halves are load-bearing and they are load-bearing for different
 * plugins. An overlay wants the anchorless form: one live anchor switches on
 * the frame's depth handling, 13.5 ms a frame by the audit, and the minimap
 * orbs deliberately take a canvas placement with no depth so as not to pay
 * it. A frame PROVIDER needs the opposite -- its surround parents to the
 * clipping root because nothing else can hold it unclipped, and it has to
 * draw UNDER the lane's chat, panels and orb block, all of which are inside
 * that same subtree. Refusing the depth target there is what made a Porcelain
 * frame provider impossible: measured on classic548, the surround painted
 * over the inventory's contents, the orb block and the XP button.
 *
 * Mutation: restore the early return in porcelain_apply_anchor for the three
 * parenting kinds without its `.depth` test, and "a stated depth target buys
 * the anchor" goes red. Delete the test in porcelain_push_item that stopped
 * recording the UNSUPPORTED finding, and "and costs no finding" goes red.
 */
static void
test_canvas_placement_depth_is_opt_in(void)
{
    struct Porcelain* porcelain;
    struct FrameFixture fixture = {.key = "surround"};
    struct PorcelainFinding findings[8];
    struct TestbedControl const* control;
    struct TestbedElement const* viewport;

    Testbed_Reset();
    Testbed_DeclareElement("viewport", 4, 4, 512, 334);
    Testbed_BindElement("viewport");
    Testbed_DeclareAsset("piece.png", TORIRS_ASSET_READY);
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, canvas_depth_describe, &fixture);
    fence(porcelain);
    control = Testbed_Control("surround");
    viewport = Testbed_Element("viewport");
    CHECK(control != NULL, "the surround piece is placed");
    CHECK(control && viewport && ToriRS_WidgetRefEqual(control->anchor, viewport->ref),
          "a stated depth target buys the anchor a canvas placement does not take");
    CHECK(Porcelain_Findings(porcelain, findings, 8) == 0, "and costs no finding");
    Porcelain_Close(porcelain);

    /* The other half: no depth stated, no anchor, which is what every overlay
     * relies on. */
    Testbed_Reset();
    Testbed_DeclareElement("viewport", 4, 4, 512, 334);
    Testbed_BindElement("viewport");
    Testbed_DeclareAsset("piece.png", TORIRS_ASSET_READY);
    fixture.runs = 0;
    fixture.wants_viewport = false;
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, frame_describe, &fixture);
    fence(porcelain);
    CHECK(Testbed_LogCountWith("set_anchor") == 0,
          "a canvas placement that states no depth still emits no anchor");
    Porcelain_Close(porcelain);
}



/* A surface raised over everything this plugin owns, the way a frame provider
 * puts the lane's chat back above its own surround. */
static void
raise_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    struct FrameFixture* fixture = user;
    struct PorcelainItem item;

    fixture->runs++;
    memset(&item, 0, sizeof(item));
    item.key = fixture->key;
    item.image = "piece.png";
    item.place.kind = PORCELAIN_AT_CANVAS;
    item.place.depth = PORCELAIN_EL(VIEWPORT);
    item.w = 32;
    item.h = 32;
    describe->piece(describe, &item);
    describe->raise(describe, PORCELAIN_EL(CHAT), PORCELAIN_EL(NONE), false);
}

/*
 * A REFUSED anchor is asked for again.
 *
 * The engine refuses an anchor whose target has not been laid out yet, and
 * for a frame provider that is the ordinary first pass: the map housing is
 * described on the fence the frame is and the compass it sits over is placed
 * three frames later. The layer recorded the target it ASKED for rather than
 * the one that was WRITTEN, so the next fence compared equal, decided there
 * was nothing to do, and the plate kept its native draw index for the rest of
 * the session -- over the orb block's globe, its wiki banner and the activity
 * adviser. The provider this replaced re-anchored on every plan pass and
 * recovered by accident.
 *
 * MUTATION: drop the result test after set_anchor in porcelain_apply_anchor
 * (`(void)result;`). Red: the anchor is written once and never retried.
 */
static void
test_a_refused_anchor_is_retried(void)
{
    struct Porcelain* porcelain;
    struct FrameFixture fixture = {.key = "surround"};
    struct TestbedControl const* control;
    struct TestbedElement const* viewport;

    Testbed_Reset();
    Testbed_DeclareElement("viewport", 4, 4, 512, 334);
    Testbed_BindElement("viewport");
    Testbed_DeclareAsset("piece.png", TORIRS_ASSET_READY);
    Testbed_RefuseAnchors(true);
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, canvas_depth_describe, &fixture);
    fence(porcelain);
    control = Testbed_Control("surround");
    CHECK(control != NULL, "the piece is placed even though its anchor was refused");
    CHECK(control && !ToriRS_WidgetRefValid(control->anchor),
          "and the refused anchor did not land");

    /* The lane's own trigger, stated: the compass binding, the canvas moving
     * or the provider invalidating all re-run the description, and the
     * question is only whether the reconcile then asks for the anchor AGAIN
     * or believes the one it recorded. */
    Testbed_RefuseAnchors(false);
    Testbed_ClearLog();
    Porcelain_Invalidate(porcelain);
    fence(porcelain);
    fence(porcelain);
    control = Testbed_Control("surround");
    viewport = Testbed_Element("viewport");
    CHECK(Testbed_LogCountWith("set_anchor") > 0, "the layer asks again once it can succeed");
    CHECK(control && viewport && ToriRS_WidgetRefEqual(control->anchor, viewport->ref),
          "and the piece ends up over the element it named");
    Porcelain_Close(porcelain);
}

/*
 * The same rule for a RAISE, which is the other half of the same defect.
 *
 * `raise` is a frame provider's verb for putting a native surface back above
 * the chrome it just drew, and the target it names is the provider's own
 * topmost control -- which on the first fence has only just been created. An
 * engine that refuses that ordering once must not be believed for ever.
 *
 * MUTATION: drop the result test in porcelain_apply_edit's RAISE arm
 * (`(void)result;`). Red: the chat is never raised.
 */
static void
test_a_refused_raise_is_retried(void)
{
    struct Porcelain* porcelain;
    struct FrameFixture fixture = {.key = "surround"};
    struct TestbedElement const* chat;
    struct TestbedControl const* control;

    Testbed_Reset();
    Testbed_DeclareElement("viewport", 4, 4, 512, 334);
    Testbed_DeclareElement("chat", 17, 357, 519, 165);
    Testbed_BindElement("viewport");
    Testbed_BindElement("chat");
    Testbed_DeclareAsset("piece.png", TORIRS_ASSET_READY);
    Testbed_RefuseAnchors(true);
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, raise_describe, &fixture);
    fence(porcelain);
    chat = Testbed_Element("chat");
    CHECK(chat && !ToriRS_WidgetRefValid(chat->anchor), "the refused raise did not land");

    Testbed_RefuseAnchors(false);
    Porcelain_Invalidate(porcelain);
    fence(porcelain);
    fence(porcelain);
    chat = Testbed_Element("chat");
    control = Testbed_Control("surround");
    CHECK(chat && control && ToriRS_WidgetRefEqual(chat->anchor, control->ref),
          "the raise is asked again and puts the chat over the frame's own chrome");
    Porcelain_Close(porcelain);
}


/* A provider that asks about the whole toplevel: the viewport it gates on and
 * the chat that arrives several fences later. */
static void
mounting_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    struct FrameFixture* fixture = user;
    struct PorcelainElementState state;
    struct PorcelainItem item;

    fixture->runs++;
    (void)Porcelain_Element(describe->porcelain, PORCELAIN_EL(VIEWPORT), &state);
    (void)Porcelain_Element(describe->porcelain, PORCELAIN_EL(CHAT), &state);
    memset(&item, 0, sizeof(item));
    item.key = fixture->key;
    item.image = "piece.png";
    item.place.kind = PORCELAIN_AT_CANVAS;
    item.w = 16;
    item.h = 16;
    describe->piece(describe, &item);
}

/*
 * A toplevel that is still mounting is not a toplevel with things MISSING.
 *
 * The absence clock calls an element ABSENT after two fences of failed
 * resolution, which is right for an overlay: it asks about one target and
 * either the lane has it or it does not. A frame provider names the WHOLE
 * vocabulary on the fence its gate element binds -- the viewport, which every
 * lane mounts first -- and the chat, the sidebar, the modal and the orb block
 * of the same toplevel arrive several fences behind it. At the plain two,
 * every desktop lane in the gate reported all four ABSENT, once each, and
 * bound them on the fence after. A finding nobody can act on is worse than no
 * finding, because the clean-findings gate is how a real absence gets seen.
 *
 * The grace is a MULTIPLIER and not a suspension, and that is the part worth
 * testing: a provider is PENDING precisely because something it asked about
 * has not resolved, so stopping the clock while it waits is a deadlock -- the
 * element cannot be called absent, so the frame cannot come up, so the
 * element cannot be called absent. The second half of this case is that the
 * finding still arrives.
 *
 * MUTATION: drop the Porcelain_FrameWaiting term from the absence test in
 * porcelain_resolve_pending. Red: the chat is reported absent while the lane
 * is still mounting it.
 */
static void
test_a_mounting_frame_is_not_an_absent_one(void)
{
    struct FrameFixture fixture = {.key = "surround"};
    struct Porcelain* porcelain;
    struct PorcelainFinding findings[8];
    char reason[TORIRS_FRAME_REASON_MAX];
    struct ToriRS_GameframeEvent event;

    Testbed_Reset();
    Testbed_DeclareElement("viewport", 4, 4, 512, 334);
    Testbed_DeclareElement("chat", 17, 357, 519, 165);
    Testbed_BindElement("viewport");
    Testbed_DeclareAsset("piece.png", TORIRS_ASSET_READY);
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Frame(porcelain, "classic-fixed", TORIRS_FRAME_CANVAS_FIXED, 765, 503,
                    mounting_describe, &fixture);
    reason[0] = '\0';
    event = frame_event("classic-fixed", true, 765, 503, reason, sizeof(reason));
    CHECK(Porcelain_FrameEvent(porcelain, &event) == TORIRS_FRAME_PENDING,
          "a provider whose chat has not mounted answers PENDING");
    Porcelain_Commit(Testbed_Api());

    for( int i = 0; i < PORCELAIN_ABSENT_FENCES + 2; i++ )
        fence(porcelain);
    CHECK(Porcelain_Findings(porcelain, findings, 8) == 0,
          "and the chat it is waiting for is not called absent while the lane mounts");

    Testbed_BindElement("chat");
    fence(porcelain);
    fence(porcelain);
    CHECK(Porcelain_Findings(porcelain, findings, 8) == 0, "still nothing once it arrives");

    /* The other half: the clock RUNS. A surface this lane truly does not have
     * is still named, and the grace only decides how long that takes. */
    for( int i = 0; i < PORCELAIN_ABSENT_FENCES * PORCELAIN_ABSENT_FRAME_GRACE + 2; i++ )
        fence(porcelain);
    Porcelain_Close(porcelain);

    Testbed_Reset();
    Testbed_DeclareElement("viewport", 4, 4, 512, 334);
    Testbed_BindElement("viewport");
    Testbed_DeclareAsset("piece.png", TORIRS_ASSET_READY);
    fixture.runs = 0;
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Frame(porcelain, "classic-fixed", TORIRS_FRAME_CANVAS_FIXED, 765, 503,
                    mounting_describe, &fixture);
    reason[0] = '\0';
    event = frame_event("classic-fixed", true, 765, 503, reason, sizeof(reason));
    (void)Porcelain_FrameEvent(porcelain, &event);
    Porcelain_Commit(Testbed_Api());
    for( int i = 0; i < PORCELAIN_ABSENT_FENCES * PORCELAIN_ABSENT_FRAME_GRACE + 4; i++ )
        fence(porcelain);
    CHECK(Porcelain_Findings(porcelain, findings, 8) > 0,
          "a chat the lane never mounts is still reported, after the grace");
    Porcelain_Close(porcelain);
}


/*
 * A node the ENGINE destroyed is rebuilt, and is nobody's refusal.
 *
 * Every owned control hangs in somebody else's tree, and when that tree is
 * replaced -- a frame root swap, a toplevel rebuild -- the engine takes the
 * children with it. The layer learns which elements rebound on the fence
 * AFTER, so for one fence a description that still matches writes setters at
 * a node that is gone. The engine answers STALE_REFERENCE, which the layer
 * recorded as a REFUSED finding: a line the plugin is told about and can do
 * nothing with, on the gate's clean-findings rule, for the layer's own
 * housekeeping. Measured on the remount lane: one, `set_image face.03`, on
 * the fence between the toplevel being replaced and the root rebinding.
 *
 * MUTATION: delete the STALE_REFERENCE arm of porcelain_note_item_result.
 * Red: the rebuild is reported as a refusal and the control is written at the
 * same dead node for as long as the description keeps matching.
 */
static void
test_a_destroyed_node_is_rebuilt_not_reported(void)
{
    struct Porcelain* porcelain;
    struct Fixture fixture = {.image = "camera.png", .gap = 4, .enabled = true};
    struct PorcelainFinding findings[8];
    struct TestbedControl const* camera;

    Testbed_Reset();
    declare_chrome();
    Testbed_BindElement("report_button");
    Testbed_BindElement("chat_bar");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Describe(porcelain, fixture_describe, &fixture);
    fence(porcelain);
    CHECK(Testbed_Control("camera") != NULL, "the control exists");

    /* The engine drops it, and the description then MOVES it -- which is what
     * puts a setter at the dead node. The element it hangs against has NOT
     * rebound yet, which is the real order: a toplevel is replaced, its
     * children die at once, and the layer hears which elements moved on the
     * fence after. */
    Testbed_KillControl("camera");
    fixture.gap = 12;
    Porcelain_Invalidate(porcelain);
    fence(porcelain);
    CHECK(Porcelain_Findings(porcelain, findings, 8) == 0,
          "a node the engine destroyed is not reported to the plugin as a refusal");
    CHECK(Testbed_Control("camera") == NULL,
          "and nothing is built against the dead parent it hung in");

    /* And now the tree it hung in comes back. */
    Testbed_UnbindElement("report_button");
    fence(porcelain);
    Testbed_BindElement("report_button");
    fence(porcelain);
    fence(porcelain);
    camera = Testbed_Control("camera");
    CHECK(camera && camera->live, "and the layer builds it again once its target rebinds");
    CHECK(Porcelain_Findings(porcelain, findings, 8) == 0, "still with nothing to report");
    Porcelain_Close(porcelain);
}

/*
 * A TAB's spelling is re-derived while it is unresolved.
 *
 * Both lanes NUMBER their tabs -- the 2004 profile's map is derived from its
 * own `panel_<name>` rows -- so a number answering says nothing about how
 * this lane spells the stone, and the first ask always lands before the
 * gameframe exists. Settling on the numbered spelling from the number alone
 * put every dat1 tab on a role that cannot exist, which reads as fourteen
 * tabs the lane does not have.
 *
 * Mutation: drop the verifying find from porcelain_resolve_tab's numbered
 * branch, or the re-derivation in porcelain_resolve_pending, and "the 2004
 * spelling wins once the frame exists" goes red.
 */
static void
test_tab_spelling_is_re_derived(void)
{
    struct Porcelain* porcelain;
    struct PorcelainElementState state;

    /* The dat1 shape: a number from the profile, and a stone spelled
     * tab_<name> that does not exist yet. */
    Testbed_Reset();
    snprintf(g_testbed.named_ids, sizeof(g_testbed.named_ids), "tab:inventory=3 ");
    Testbed_DeclareElement("tab_inventory", 660, 200, 30, 30);
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    CHECK(!Porcelain_Element(porcelain, PORCELAIN_TAB_EL("inventory"), &state),
          "asked on a cold tree the tab is pending, not bound");
    Porcelain_Fence(porcelain);
    Porcelain_Commit(Testbed_Api());
    Testbed_BindElement("tab_inventory");
    Porcelain_Fence(porcelain);
    Porcelain_Commit(Testbed_Api());
    CHECK(Porcelain_Element(porcelain, PORCELAIN_TAB_EL("inventory"), &state),
          "the 2004 spelling wins once the frame exists");
    CHECK(Testbed_LogCountWith("watch_state sidetab_") == 0,
          "and nothing was ever watched under a numbered spelling this lane has not got");
    Porcelain_Close(porcelain);

    /* A tab the lane NUMBERS and does not mount reports its absence under the
     * name a plugin would have looked for by hand -- the authored spelling,
     * because that is the one this lane uses for the thirteen it does have. */
    Testbed_Reset();
    snprintf(g_testbed.named_ids, sizeof(g_testbed.named_ids), "tab:clan=7 ");
    Testbed_DeclareElement("tab_inventory", 660, 200, 30, 30);
    Testbed_BindElement("tab_inventory");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    (void)Porcelain_Element(porcelain, PORCELAIN_TAB_EL("inventory"), &state);
    (void)Porcelain_Element(porcelain, PORCELAIN_TAB_EL("clan"), &state);
    for( int i = 0; i < 4; i++ )
    {
        Porcelain_Fence(porcelain);
        Porcelain_Commit(Testbed_Api());
    }
    /* The absence is recorded when the description next asks for it: an
     * element nobody wants is not a finding. */
    (void)Porcelain_Element(porcelain, PORCELAIN_TAB_EL("clan"), &state);
    {
        struct PorcelainFinding findings[8];
        int const count = Porcelain_Findings(porcelain, findings, 8);
        CHECK(count == 1, "the tab this lane does not mount is one finding");
        CHECK(count == 1 && findings[0].result == PORCELAIN_FINDING_ABSENT, "an absence");
        CHECK(count == 1 && strcmp(findings[0].detail, "tab_clan") == 0,
              "named the way this lane spells its stones, not by a number it never mounts");
    }
    Porcelain_Close(porcelain);

    /* The cache shape: the same number, and a numbered stone that arrives
     * later. The re-derivation has to reach that one too. */
    Testbed_Reset();
    snprintf(g_testbed.named_ids, sizeof(g_testbed.named_ids), "tab:inventory=3 ");
    Testbed_DeclareElement("sidetab_3", 660, 200, 30, 30);
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    (void)Porcelain_Element(porcelain, PORCELAIN_TAB_EL("inventory"), &state);
    Porcelain_Fence(porcelain);
    Porcelain_Commit(Testbed_Api());
    Testbed_BindElement("sidetab_3");
    Porcelain_Fence(porcelain);
    Porcelain_Commit(Testbed_Api());
    CHECK(Porcelain_Element(porcelain, PORCELAIN_TAB_EL("inventory"), &state),
          "and the numbered spelling wins on a lane that mounts one");
    Porcelain_Close(porcelain);
}


/*
 * A row is a BAND, not an exact coordinate.
 *
 * A 2004 stone is not on a grid: the sideicons carry their own baked offsets,
 * and the seven stones of rs254lc's top rail report y 171, 171, 171, 172,
 * 173, 173, 173. Grouping on the exact coordinate reads that one rail as
 * five rows -- and the order inside them comes out in y order rather than
 * left to right, which is the account-icon-where-friends-belongs defect
 * arriving by another door.
 *
 * Mutation: band on equality (make the derived arm state extent 0) and both
 * checks below go red: five rows instead of two, in the wrong order.
 */
static void
test_tab_rows_are_bands_not_coordinates(void)
{
    struct Porcelain* porcelain;
    struct PorcelainElement group[PORCELAIN_TAB_MAX];
    int count;

    Testbed_Reset();
    /* rs254lc's own numbers, measured. */
    Testbed_DeclareElement("tab_combat", 545, 173, 33, 36);
    Testbed_BindElement("tab_combat");
    Testbed_DeclareElement("tab_stats", 569, 171, 33, 36);
    Testbed_BindElement("tab_stats");
    Testbed_DeclareElement("tab_quests", 598, 171, 33, 36);
    Testbed_BindElement("tab_quests");
    Testbed_DeclareElement("tab_inventory", 631, 172, 33, 36);
    Testbed_BindElement("tab_inventory");
    Testbed_DeclareElement("tab_friends", 570, 468, 33, 36);
    Testbed_BindElement("tab_friends");
    Testbed_DeclareElement("tab_logout", 633, 470, 33, 36);
    Testbed_BindElement("tab_logout");
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);

    CHECK(Porcelain_TabGroupCount(porcelain, PORCELAIN_TAB_ROWS) == 2,
          "a rail whose stones sit two pixels apart is one row, not three");
    count = Porcelain_TabGroup(porcelain, PORCELAIN_TAB_ROWS, 0, group, PORCELAIN_TAB_MAX);
    CHECK(count == 4, "the top rail holds its four stones");
    CHECK(count == 4 && strcmp(group[0].role, "combat") == 0 &&
              strcmp(group[1].role, "stats") == 0 && strcmp(group[2].role, "quests") == 0 &&
              strcmp(group[3].role, "inventory") == 0,
          "and they come back left to right, not in y order");
    Porcelain_Close(porcelain);
}


/*
 * More offers than the layer holds is a budget finding, never a binding that
 * quietly replaces another: an offer the host asks for and nothing answers is
 * the lane's frame coming down with nothing to replace it.
 *
 * Mutation: drop the finding in Porcelain_Frame and the fifth offer vanishes
 * with nothing said.
 */
static void
test_frame_offer_budget(void)
{
    struct FrameFixture fixture = {.key = "piece"};
    struct Porcelain* porcelain;
    struct PorcelainFinding findings[8];
    char reason[TORIRS_FRAME_REASON_MAX];
    struct ToriRS_GameframeEvent event;

    Testbed_Reset();
    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    Porcelain_Frame(porcelain, "one", TORIRS_FRAME_CANVAS_FIXED, 765, 503, frame_describe,
                    &fixture);
    Porcelain_Frame(porcelain, "two", TORIRS_FRAME_CANVAS_FIXED, 765, 503, frame_describe,
                    &fixture);
    Porcelain_Frame(porcelain, "three", TORIRS_FRAME_CANVAS_FIXED, 765, 503, frame_describe,
                    &fixture);
    Porcelain_Frame(porcelain, "four", TORIRS_FRAME_CANVAS_FIXED, 765, 503, frame_describe,
                    &fixture);
    CHECK(Porcelain_Findings(porcelain, findings, 8) == 0, "four offers fit");
    Porcelain_Frame(porcelain, "five", TORIRS_FRAME_CANVAS_FIXED, 765, 503, frame_describe,
                    &fixture);
    CHECK(Porcelain_Findings(porcelain, findings, 8) == 1, "the fifth is a finding");
    CHECK(findings[0].result == PORCELAIN_FINDING_BUDGET, "a budget one");
    reason[0] = '\0';
    event = frame_event("five", true, 765, 503, reason, sizeof(reason));
    CHECK(Porcelain_FrameEvent(porcelain, &event) == TORIRS_FRAME_UNSUPPORTED,
          "and the offer that did not fit refuses rather than running another's description");
    Porcelain_Close(porcelain);
}

/* ------------------------------------------------------------------------ */
/* Round four: the gaps nine ports left                                     */
/* ------------------------------------------------------------------------ */

/*
 * A secondary click in a CUSTOM well reaches its own handler and no other.
 *
 * A well is ONE control, so everything inside it is arithmetic on a
 * coordinate -- and before TORIRS_PANEL_ACTION_MENU the well could be told
 * where it was clicked but never which button did it. The loot tracker's band
 * and cell operations therefore had nowhere to live and stayed as buttons
 * standing under a selected row, which is the one thing that blocked its port
 * outright.
 *
 * MUTATION: route MENU to row->on_action when on_menu is NULL -- the well that
 * asked for clicks only is handed a right click as though it were one, and
 * "a row with no on_menu is not handed the click as an activation" goes red.
 * MUTATION: pick the handler with `row->on_menu ? row->on_menu : row->on_action`
 * -- an ordinary ACTIVATE on a row that HAS an on_menu goes to the menu
 * handler instead, and the activation tally goes red.
 */
static void
test_panel_well_secondary_click(void)
{
    struct PanelFixture fixture;
    struct Porcelain* porcelain;

    /* A well that declares one gets the click, with the well-local point. */
    Testbed_Reset();
    panel_fixture_init(&fixture);
    fixture.well_takes_menu = true;
    porcelain = panel_start(&fixture, PORCELAIN_FACE_BOTH);
    CHECK(Testbed_PanelAction(porcelain, "boxes", TORIRS_PANEL_ACTION_MENU, 0, ""),
          "a described well takes a secondary click");
    CHECK(fixture.menus == 1, "its own handler runs");
    CHECK(fixture.actions == 0, "and the click handler does not");
    CHECK(strcmp(fixture.menu_key, "boxes") == 0, "the row names itself");

    /* And an ordinary activation still goes where it always did. */
    CHECK(Testbed_PanelAction(porcelain, "boxes", TORIRS_PANEL_ACTION_ACTIVATE, 0, ""),
          "and a primary click is still a primary click");
    CHECK(fixture.actions == 1, "which runs the click handler");
    CHECK(fixture.menus == 1, "and not the menu one");
    Porcelain_Close(porcelain);

    /* A row that never asked for secondary clicks is not handed one. Every
     * row written before this channel existed is that row. */
    Testbed_Reset();
    panel_fixture_init(&fixture);
    porcelain = panel_start(&fixture, PORCELAIN_FACE_BOTH);
    CHECK(Testbed_PanelAction(porcelain, "boxes", TORIRS_PANEL_ACTION_MENU, 0, ""),
          "a well with no on_menu still answers for its own key");
    CHECK(fixture.actions == 0,
          "but a row with no on_menu is not handed the click as an activation");
    CHECK(fixture.menus == 0, "and nothing else runs either");
    Porcelain_Close(porcelain);
}

/*
 * Restating a row the HOST moved: the row's setters, and not the page.
 *
 * The host commits a PICK to its widget model and bumps its model revision
 * BEFORE it dispatches, so a plugin that REFUSES one is looking at a control
 * already showing the value nobody saved -- while its own description, which
 * is the thing the reconciler diffs, has not moved at all. Every hash matches,
 * no setter fires, and the control keeps a choice that was never saved. The
 * client-settings port worked around this by reaching past the layer to
 * api->panel.set_options with the values the mirror already held.
 *
 * MUTATION: drop `|| have->restate` from the hash compare in
 * Porcelain_PanelReconcile -- the equal hash wins, nothing is pushed, and
 * "the row is stated again" goes red.
 * MUTATION: pass `false` for `force` into panel_apply_properties -- the
 * restate reaches the apply and every per-property compare matches, so no
 * engine call is made and the same assertion goes red for the other reason.
 * MUTATION: drop the Porcelain_Invalidate from Porcelain_Restate -- no input
 * moved, the fence never reconciles, and the assertion goes red on a frame
 * where nothing at all happened.
 * MUTATION: look the key up in panel->rows instead of panel->declared -- a key
 * the HOST does not hold is accepted, and the unknown-key finding goes red.
 */
static void
test_panel_restate_after_a_refused_pick(void)
{
    struct PanelFixture fixture;
    struct PorcelainPanelCounters counters;
    struct Porcelain* porcelain;

    Testbed_Reset();
    panel_fixture_init(&fixture);
    porcelain = panel_start(&fixture, PORCELAIN_FACE_BOTH);
    Porcelain_CountersReset(porcelain);

    /* The host moved its copy and the description did not: exactly the state
     * a refused pick leaves behind. */
    Testbed_PanelSetSelected("frame", "modern");
    fence(porcelain);
    Porcelain_PanelCountersRead(porcelain, &counters);
    CHECK(counters.setters == 0,
          "an unchanged description is still an unchanged description");

    Porcelain_Restate(porcelain, "frame");
    fence(porcelain);
    Porcelain_PanelCountersRead(porcelain, &counters);
    CHECK(counters.restates == 1, "the row is stated again");
    CHECK(counters.rebuilds == 0, "without the page, which is the flicker this replaces");
    CHECK(strcmp(panel_row("frame")->selected, "auto") == 0,
          "and the host's copy is the description's again");
    CHECK(panel_findings_with(porcelain, PORCELAIN_FINDING_REFUSED) == 0,
          "with nothing refused");

    /* A key the host does not hold is a finding, not an abort: the page can
     * be rebuilt between the action and the fence that restates. */
    Porcelain_Restate(porcelain, "not_a_row");
    CHECK(panel_findings_with(porcelain, PORCELAIN_FINDING_REFUSED) == 1,
          "restating a key the host does not hold is one finding");

    /*
     * And a key this run has DESCRIBED but the host has not been given yet is
     * the same answer, which is the case that tells the two tables apart: the
     * verb is about the host's copy, so the scratch is the wrong place to look
     * it up -- and looking there would also index the declaration by a row
     * number that belongs to a different, longer list.
     */
    fixture.extra_row = true;
    panel_restate(porcelain);
    CHECK(Testbed_PanelRow("detail") == NULL,
          "the new row is described and the host has not been asked for it yet");
    Porcelain_Restate(porcelain, "detail");
    {
        /* The SECOND time, on the same (verb, element, result), so it
         * coalesces into the first line's count rather than adding a line --
         * which is the findings table's own rule and not a shortcoming here. */
        struct PorcelainFinding found[PORCELAIN_FINDINGS_MAX];
        int const count = Porcelain_Findings(porcelain, found, PORCELAIN_FINDINGS_MAX);
        uint32_t restates = 0;
        for( int i = 0; i < count; i++ )
            if( strcmp(found[i].verb, "restate") == 0 )
                restates = found[i].count;
        CHECK(restates == 2,
              "restating a row the host has never been given is a finding too");
    }
    Porcelain_Close(porcelain);
}

/*
 * Invalidate from INSIDE a describe reconciles the description that asked.
 *
 * It did not. The bump moved an input, the fence's two-pass loop saw inputs
 * had moved and ran the describe a SECOND time, and it was the second pass's
 * scratch that reached the reconcile -- so a describe that stated nothing in
 * order to have its controls removed removed nothing at all, because the keys
 * were still in the final scratch. Silently: no counter, no finding, no
 * assert. That cost the minimap-orbs port a regression that reached the
 * integration branch, and the plugin had to span its drop across two fences.
 *
 * MUTATION: bump the stamp directly in Porcelain_Invalidate instead of
 * deferring while `describing` -- the wipe is re-described in the same fence,
 * the control survives, and "the empty description is the one reconciled"
 * goes red.
 * MUTATION: drop the deferred consume at the end of Porcelain_Fence -- the
 * re-describe never happens and "and the next fence puts it back" goes red.
 */
static bool g_invalidate_wipe;

static void
invalidate_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    struct PorcelainItem item;

    (void)user;
    if( g_invalidate_wipe )
    {
        /* Describe NOTHING, then ask to be asked again -- which is what the
         * verb plainly reads as, and now what it does. */
        g_invalidate_wipe = false;
        Porcelain_Invalidate(describe->porcelain);
        return;
    }
    memset(&item, 0, sizeof(item));
    item.key = "wiped";
    item.image = "orb.png";
    item.place.kind = PORCELAIN_INSIDE;
    item.place.on = PORCELAIN_ROLE_EL("minimap");
    item.place.corner_or_side = PORCELAIN_TOP_LEFT;
    item.w = 10;
    item.h = 10;
    describe->piece(describe, &item);
}

static void
test_invalidate_inside_a_describe_keeps_the_description(void)
{
    struct Porcelain* porcelain;

    Testbed_Reset();
    Testbed_DeclareElement("minimap", 550, 4, 150, 150);
    Testbed_BindElement("minimap");
    Testbed_DeclareImage("orb.png", TORIRS_ASSET_READY, 10, 10);

    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    g_invalidate_wipe = false;
    Porcelain_Describe(porcelain, invalidate_describe, NULL);
    fence(porcelain);
    CHECK(Testbed_Control("wiped") != NULL, "the control is there to begin with");

    /* The wipe: one describe that says nothing and invalidates. */
    g_invalidate_wipe = true;
    Porcelain_Invalidate(porcelain);
    fence(porcelain);
    CHECK(Testbed_Control("wiped") == NULL,
          "the empty description is the one reconciled, so the control goes");

    fence(porcelain);
    CHECK(Testbed_Control("wiped") != NULL,
          "and the next fence puts it back, under the parent it now belongs to");
    Porcelain_Close(porcelain);
}

/*
 * A control the engine freed under a surviving parent is RE-MADE, not written
 * to and reported.
 *
 * A `layout` verb rebuilds the whole HUD, and the frame root can be the same
 * NODE on the other side of that while everything under it -- the owned
 * controls included -- has been torn down. The layer's parent compare then
 * says nothing moved, the control is kept, and every setter aimed at it comes
 * back STALE_REFERENCE and files a finding the plugin can neither prevent nor
 * declare: it has no element to key an absence on, the refusal is transient
 * rather than a lane fact, and declaring it unconditionally would be a stale
 * declaration on every lane where the layout never switches.
 *
 * MUTATION: drop the porcelain_item_alive arm from the reconcile -- the
 * setters write to a dead handle, the refusal comes back, and both the
 * no-findings assertion and the live-control one go red.
 * MUTATION: make porcelain_item_alive answer true unconditionally -- the same
 * two assertions, and `recreates` stays at zero.
 */
static void
test_control_freed_under_a_surviving_parent(void)
{
    struct PorcelainCounters counters;
    struct Porcelain* porcelain;

    Testbed_Reset();
    Testbed_DeclareElement("minimap", 550, 4, 150, 150);
    Testbed_BindElement("minimap");
    Testbed_DeclareImage("orb.png", TORIRS_ASSET_READY, 10, 10);

    porcelain = Porcelain_Open(Testbed_Api(), &DEF_A, NULL);
    g_invalidate_wipe = false;
    Porcelain_Describe(porcelain, invalidate_describe, NULL);
    fence(porcelain);
    CHECK(Testbed_Control("wiped") != NULL, "the control is there to begin with");

    Porcelain_CountersReset(porcelain);
    /* The controls go; the element stays bound and its parent ref does not
     * move. Nothing tells the plugin, because no verb asks. */
    Testbed_DestroyOwnedControls();
    Testbed_MoveElement("minimap", 550, 4);
    Porcelain_Invalidate(porcelain);
    fence(porcelain);

    Porcelain_CountersRead(porcelain, &counters);
    CHECK(counters.recreates == 1, "the layer notices the control is gone and re-makes it");
    CHECK(Testbed_Control("wiped") != NULL, "so there is a live control again");
    /* A real buffer: Porcelain_Findings answers 0 for a capacity of 0 without
     * looking, so asking with none would pass however many there were. */
    {
        struct PorcelainFinding found[PORCELAIN_FINDINGS_MAX];
        CHECK(Porcelain_Findings(porcelain, found, PORCELAIN_FINDINGS_MAX) == 0,
              "and the plugin is told nothing, because there was nothing it could have done");
    }
    Porcelain_Close(porcelain);
}

/*
 * The reader's place survives a page rebuild, and a plugin can move it.
 *
 * Nothing read or set the page's scroll, so the one legitimate rebuild the row
 * model still has -- a detail block opened, a flag list grew -- sent the page
 * back to the top under whoever was reading it. The HOST carries the place
 * across a rebuild of the same page by itself; this verb is for moving it,
 * which is a different intent.
 *
 * MUTATION: answer 0 instead of -1 from api_panel_scroll with no page up --
 * "no page is not the top of one" goes red, and a plugin restoring a place
 * nobody took cannot tell the two apart.
 * MUTATION: drop the refusal finding from Porcelain_PanelScrollTo -- moving a
 * reader who is not there is silent, and the finding assertion goes red.
 */
static void
test_panel_scroll_is_readable_and_writable(void)
{
    struct PanelFixture fixture;
    struct Porcelain* porcelain;

    Testbed_Reset();
    panel_fixture_init(&fixture);
    porcelain = panel_start(&fixture, PORCELAIN_FACE_BOTH);

    CHECK(Porcelain_PanelScroll(porcelain) == 0, "a fresh page is at the top");
    Porcelain_PanelScrollTo(porcelain, 64);
    CHECK(Porcelain_PanelScroll(porcelain) == 64, "and a plugin can move the reader");
    CHECK(panel_findings_with(porcelain, PORCELAIN_FINDING_REFUSED) == 0,
          "with nothing refused");

    Testbed_SetPanelAbsent(true);
    CHECK(Porcelain_PanelScroll(porcelain) == -1,
          "no page is not the top of one: the two must not read alike");
    Porcelain_PanelScrollTo(porcelain, 8);
    CHECK(panel_findings_with(porcelain, PORCELAIN_FINDING_REFUSED) == 1,
          "and moving a reader who is not there is one finding");
    Testbed_SetPanelAbsent(false);
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
    test_replace_answers_the_whole_target();
    test_replace_without_a_hit_gets_no_hit_box();
    test_arbitration_first_claim();
    test_arbitration_relinquish();
    test_findings_coalesce();
    test_absence_waits_for_the_lane();
    test_expected_absence_both_directions();
    test_tier_table();
    test_config_list_refusal();
    test_description_total_order();
    test_direct_path();
    test_a_set_survives_the_next_fence();
    test_a_fade_to_zero_is_not_silently_opaque();
    test_one_revalidate_for_every_plugin();
    test_visibility_and_enablement();
    test_blocker_needs_no_picture();
    test_edits_are_retained_and_released();
    test_keep_relative();
    test_menu_tag();
    test_setting_absent_is_off();
    test_setting_value_reads_a_number_once();
    test_world_tile_budget_reaches_the_plugin();
    test_key_edge();
    test_derived_once_per_input();
    test_require_reports_the_feature();
    test_counts_are_lane_data();
    test_tab_resolves_by_data();
    test_spelling_is_re_asked();
    test_an_unresolved_element_costs_nothing_at_rest();
    test_a_draw_context_does_not_ask_about_lane_chrome();
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
    test_panel_well_secondary_click();
    test_panel_restate_after_a_refused_pick();
    test_invalidate_inside_a_describe_keeps_the_description();
    test_control_freed_under_a_surviving_parent();
    test_panel_scroll_is_readable_and_writable();
    test_panel_refuses_rather_than_truncates();
    test_panel_actions_route_by_key();
    test_panel_draw_routes_by_node();
    test_panel_refused_setter_is_a_finding();
    test_panel_opened_before_the_first_fence();
    test_panel_declaration_owed_after_a_refusal();
    test_panel_close_releases_the_pane();
    test_frame_offer_describes_and_releases();
    test_frame_unsupported_reaches_the_host();
    test_frame_pending_until_the_lane_binds();
    test_frame_event_refusals();
    test_usable_subtracts_only_a_presented_strip();
    test_skin_waits_for_its_picture();
    test_native_size_is_asked_by_element();
    test_orb_kinds_are_not_block_members();
    test_native_size_member_refusals();
    test_lane_icon_needs_the_tab_and_the_panel();
    test_tab_groups_derive_from_the_boxes();
    test_tab_groups_take_the_stated_override();
    test_detached_tab_is_in_no_group();
    test_tab_group_capacity_is_a_finding();
    test_frame_offer_budget();
    test_depth_target_must_paint();
    test_canvas_placement_depth_is_opt_in();
    test_a_refused_anchor_is_retried();
    test_a_refused_raise_is_retried();
    test_a_mounting_frame_is_not_an_absent_one();
    test_a_destroyed_node_is_rebuilt_not_reported();
    test_tab_spelling_is_re_derived();
    test_tab_rows_are_bands_not_coordinates();

    printf("porcelain: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
