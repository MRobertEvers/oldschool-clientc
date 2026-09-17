#ifndef TORIRS_PORCELAIN_TESTBED_H
#define TORIRS_PORCELAIN_TESTBED_H

/*
 * One shared fake engine for every Porcelain test.
 *
 * The record's seventh bug class is that each unit test shipped its own fake
 * engine, 1,256 to 3,382 lines of it, and every new engine callback had to be
 * added to all of them. There is one here, and it does two things no per-test
 * fake did:
 *
 *   - It RECORDS every call in order. "An unchanged description makes zero
 *     engine calls" is then a readable assertion and not a belief, and the
 *     recorded log IS the hand-expansion into raw api-> calls that the plan
 *     says any Porcelain plugin must have.
 *   - It lets a test move one input at a time -- an element binds, an asset
 *     lands, the config changes -- so the readiness matrix can replay the six
 *     inputs in every order against the same description.
 */

#include "plugin/porcelain/torirs_porcelain.h"

#define TESTBED_ELEMENTS_MAX 32
#define TESTBED_WATCHES_MAX 64
#define TESTBED_CONTROLS_MAX 64
#define TESTBED_ASSETS_MAX 128
#define TESTBED_CONFIG_MAX 16
#define TESTBED_SURFACES_MAX 24
#define TESTBED_LOG_MAX 512
#define TESTBED_LOG_LINE 120

struct TestbedElement
{
    bool used;
    char role[64];
    bool bound;
    struct ToriRS_WidgetBounds local;
    struct ToriRS_WidgetBounds bounds;
    bool presented;
    bool own_hidden;
    bool native_hidden;
    bool input_present;
    uint32_t graphic_token;
    /** This element (or something below it) paints a PICTURE, which is what a
     *  REPLACE of it consumes. Off by default: a declared element is a box
     *  with no art until a test says otherwise, and the plugins that ask are
     *  asking precisely because most targets are not the same answer. */
    bool paints_own_art;
    uint32_t facets;
    uint64_t incarnation;
    struct ToriRS_WidgetRef ref;
    struct ToriRS_WidgetRef parent;
    /* The role this element lives INSIDE, empty for one that stands alone.
     * The lane's own containment, which `parent` above does not carry: every
     * declared element hangs off one shared synthetic parent there, so a fake
     * that knew only that could not tell that hiding an orb takes the button
     * in it off the screen too. @see Testbed_ElementInside. */
    char inside[64];
    /* Where a `raise` put this native element, and against what. A frame
     * provider anchors the LANE's own nodes, so unlike every overlay verb the
     * subject here is an element and not an owned control. */
    struct ToriRS_WidgetRef anchor;
    enum ToriRS_WidgetRelation anchor_relation;
};

struct TestbedControl
{
    bool live;
    char key[64];
    struct ToriRS_WidgetRef ref;
    struct ToriRS_WidgetRef parent;
    struct ToriRS_WidgetRef anchor;
    enum ToriRS_WidgetRelation relation;
    int32_t x, y, width, height;
    /** x/y were written by set_canvas_position, not set_position. */
    bool canvas_positioned;
    int opacity;
    bool hidden;
    bool armed;
    char label[64];
    ToriRS_WidgetListener op;
    void* op_user;
    char text[128];
    /** The picture last written to it, so a blank control is readable. */
    struct ToriRS_ImageRef image;
};

struct TestbedAsset
{
    bool used;
    char name[64];
    enum ToriRS_AssetState state;
    int value;
    /** Bytes an assets.bytes read answers, for a shipped table. */
    char body[256];
    bool has_body;
    /** Still held by the plugin: a table that forgot to release shows here. */
    bool held;
    /** The picture's own size, once it is READY. */
    int width, height;
};

struct TestbedConfigRow
{
    bool used;
    char key[64];
    char value[PORCELAIN_CONFIG_VALUE_MAX];
    int number;
    bool has_number;
};

/*
 * One row of the fake host's page model.
 *
 * The host's own model is what the reconciler is talking to, so the fake
 * keeps the parts the rules are about: the id, the kind, the serial that a
 * reidentify mints, and the strings a patch writes. `declared` is how many
 * times this row has been built from scratch, which is how a test tells a
 * patch from a rebuild without reading the log.
 */
struct TestbedPanelRow
{
    bool used;
    char id[64];
    int kind;
    char label[192];
    char text[192];
    int value;
    int height;
    int option_count;
    /* At PORCELAIN_OPTION_*_MAX, which is the HOST's own ceiling: a fake
     * narrower than the real thing turns a legal string into a test failure. */
    char option_value[16][192];
    char option_label[16][192];
    bool option_enabled[16];
    char selected[192];
    uint32_t serial;
    int declared;
};

/*
 * One authored surface box, as the LANE states it -- what
 * frame.surface_native_size and frame.surface_member_native_box answer.
 *
 * `member` -1 is the surface as a whole. Declared per scenario rather than
 * defaulted, because "the lane states no pixel size for this surface" is a
 * real answer (a proportional box) and has to be reachable in a test.
 */
struct TestbedSurface
{
    bool used;
    int surface;
    int member;
    int x, y, width, height;
};

struct Testbed
{
    struct ToriRS_Api api;
    struct ToriRS_GameApi game;

    /* The fake page model. */
    struct TestbedPanelRow panel_rows[64];
    int panel_row_count;
    /* Rises on every panel.request. */
    int panel_requests;
    char panel_icon[64];
    int panel_width;
    /* Rises on every panel.invalidate: the number the row model exists to
     * keep at zero for anything short of a changed row SET. */
    int panel_invalidates;
    uint32_t panel_next_serial;
    /** Where the fake page is scrolled to, for the scroll verbs. */
    int panel_scroll;
    /** No page of this plugin's is up: panel.scroll answers -1. */
    bool panel_no_page;
    /* The view the last Testbed_PanelBuild drove. */
    int panel_view;
    /* A setter naming an id the model does not hold. The host answers those
     * NOT_FOUND, and a reconciler that leaked one would be writing into a
     * page that is not there. */
    int panel_orphan_setters;
    /* Refuse the next set_options, whatever it says. */
    bool panel_refuse_options;
    /* Refuse every set_anchor, the way the engine does for a target that is
     * not laid out yet. @see Testbed_RefuseAnchors. */
    bool refuse_anchors;

    struct TestbedElement elements[TESTBED_ELEMENTS_MAX];
    /* Refs the engine has FREED. The tree refuses one from every entry point,
     * and a fake that could not name them made a dead node read exactly like
     * the top of the tree. @see fake_parent. */
    struct ToriRS_WidgetRef retired[TESTBED_ELEMENTS_MAX * 2];
    int retired_count;
    struct
    {
        bool used;
        char role[64];
        ToriRS_WidgetListener fn;
        void* user;
    } watches[TESTBED_WATCHES_MAX];
    /*
     * The `@tree` subscription, kept OUT of the role table on purpose: it is
     * keyed by no role, it must not answer a role-keyed raise, and it must
     * not show up in Testbed_LiveWatches, which exists to catch a re-spelled
     * element leaking a subscription.
     */
    struct
    {
        ToriRS_WidgetListener fn;
        void* user;
    } tree_watch;
    /* Topology publications. Declaring, binding and unbinding an element move
     * it; MOVING one does not, which is the host's own rule. */
    uint64_t tree_generation;
    struct TestbedControl controls[TESTBED_CONTROLS_MAX];
    struct TestbedAsset assets[TESTBED_ASSETS_MAX];
    struct TestbedConfigRow config[TESTBED_CONFIG_MAX];
    struct TestbedSurface surfaces[TESTBED_SURFACES_MAX];
    /* The gameframe root, as cache.frame_root answers it. A KEY, never a
     * comparison: it is joined to a tab name and handed to the profile. */
    int frame_root;

    char log[TESTBED_LOG_MAX][TESTBED_LOG_LINE];
    int log_count;
    /* Counted separately so a test can say "no engine call at all" without
     * reading the log. */
    int calls;

    int screen;
    uint64_t frame_ms;
    bool touch;
    bool skill_stated;
    bool player_present;
    /* Capabilities this lane answers, comma separated. */
    char capabilities[256];
    /* Named ids the profile declares, as "kind:name=value" rows. */
    char named_ids[256];
    int key_held;
    /* The pointer, in canvas coords, and whether the lane has one at all --
     * `input.pointer` answers false on a lane with no mouse, and a plugin
     * that tests a rectangle against the pointer has to be driven through
     * both answers. */
    int pointer_x;
    int pointer_y;
    bool pointer_present;
    int next_ref;

    /* The overlay surfaces. */
    /** Rows menu.add will still accept before it refuses, as the host's route
     *  table does. */
    int menu_routes_left;
    /** What core.notify was handed, in order. */
    char notified[8][128];
    int notify_count;
    /** The region the fake graphics context answers, and whether it has one. */
    struct ToriRS_Rect draw_region;
    bool draw_region_valid;
    /*
     * The world-hull draw, modelled with BOTH of its refusals -- the host's
     * per-frame allotment and the per-entity APPEARANCE claim. A testbed that
     * only ever answered OK is what let the engine's own `return OK` stand
     * under a declared result type for as long as it did.
     */
    int hull_budget;
    int hull_used;
    /** The one element another plugin is holding, or -1. */
    int hull_claimed_element;
    /*
     * The world-tile draw, with the one refusal it has. A tile is a place, so
     * there is no claim arm here -- only the frame's allotment, which a tile
     * overlay reaches by multiplication because a marker is drawn per tile of
     * a footprint.
     */
    int tile_budget;
    int tile_used;
};

/** The single instance. Reset it between scenarios. */
extern struct Testbed g_testbed;

void Testbed_Reset(void);
struct ToriRS_Api* Testbed_Api(void);

/* Elements ---------------------------------------------------------------- */

/** Declare an element this lane HAS but has not bound yet. */
struct TestbedElement* Testbed_DeclareElement(char const* role, int x, int y, int width,
                                              int height);
/** Bind it, raising BOUND on every watch that named it. */
void Testbed_BindElement(char const* role);
/** Unbind it, raising UNBOUND. */
void Testbed_UnbindElement(char const* role);
/** Move it, raising STATE_CHANGED. */
void Testbed_MoveElement(char const* role, int x, int y);
/** Hide or show it, raising STATE_CHANGED. */
void Testbed_PresentElement(char const* role, bool presented);

/**
 * Make every set_anchor answer UNAVAILABLE, or stop.
 *
 * The engine refuses an anchor whose target has not been laid out yet, which
 * on a frame provider's first pass is the ordinary case: the plate is
 * described on the fence the frame is, three frames before the surface it
 * sits over is placed.
 */
void Testbed_RefuseAnchors(bool refuse);

/**
 * Destroy one owned control behind the layer's back, the way the engine does
 * when the tree it hangs in is replaced. Every setter naming it then answers
 * STALE_REFERENCE, which is what a frame root swap looks like from inside the
 * layer.
 */
/* The engine frees the element's node and reports nothing: the watch stays
 * BOUND holding a reference the tree can no longer resolve. */
void Testbed_KillElementNode(char const* role);
void Testbed_KillControl(char const* key);
struct TestbedElement* Testbed_Element(char const* role);
/**
 * Declare that `role` lives inside `container_role`, as interface 160's run
 * button lives inside the run orb.
 *
 * Only a hide reads it, and it is what makes one faithful: the engine's hide
 * is a SUBTREE statement, so a plugin that hides a node it covers takes every
 * button in it out of the picture as well -- and a plugin that then presses
 * one of those buttons is pressing through its own hide.
 */
void Testbed_ElementInside(char const* role, char const* container_role);

/* Assets ------------------------------------------------------------------ */

void Testbed_DeclareAsset(char const* name, enum ToriRS_AssetState state);
/** An image asset with a natural size behind it. */
void Testbed_DeclareImage(char const* name, enum ToriRS_AssetState state, int width, int height);
/** Land a pending asset, as the loader would. */
void Testbed_LandAsset(char const* name);
/** A shipped data file with bytes behind it. */
void Testbed_DeclareFile(char const* name, char const* body);
/** True while a plugin still holds the file: a table must release it. */
bool Testbed_AssetHeld(char const* name);

/* The draw pass ----------------------------------------------------------- */

/** A graphics builder whose context answers `region`, or nothing when
 *  `valid` is false -- the world pass before it set one. */
struct ToriRS_Graphics* Testbed_Graphics(struct ToriRS_Rect region, bool valid);

/* Surfaces ---------------------------------------------------------------- */

/** State the authored box of a surface (`member` -1) or one of its members. */
void Testbed_DeclareSurface(int surface, int member, int x, int y, int width, int height);
/** The gameframe root cache.frame_root answers. */
void Testbed_SetFrameRoot(int root);

/* Config ------------------------------------------------------------------ */

void Testbed_SetConfigString(char const* key, char const* value);
void Testbed_SetConfigInt(char const* key, int value);
char const* Testbed_ConfigString(char const* key);

/* The log ----------------------------------------------------------------- */

void Testbed_ClearLog(void);
int Testbed_LogCount(void);
char const* Testbed_LogLine(int index);
/** How many recorded lines begin with `prefix`. */
int Testbed_LogCountWith(char const* prefix);
/** The index of the first line beginning with `prefix`, or -1. */
int Testbed_LogFind(char const* prefix);
void Testbed_PrintLog(void);

/**
 * Free every owned control the way a root rebuild does: the controls go, the
 * element they hung under stays bound, and its parent ref does not change.
 *
 * That combination is the whole point of the seam and the reason the layer's
 * parent compare was not enough -- a `layout` verb can rebuild everything
 * under a frame root that is still the same node. Nothing tells the plugin,
 * because no verb asks whether an owned control still exists; every setter
 * aimed at one afterwards answers STALE_REFERENCE.
 */
void Testbed_DestroyOwnedControls(void);

struct TestbedControl* Testbed_Control(char const* key);
int Testbed_LiveControls(void);
/** Subscriptions the host still holds. A re-spelled element must not grow it. */
int Testbed_LiveWatches(void);
/**
 * Publish a topology change with no element moving.
 *
 * The signal the layer polls its unresolved watches on. A test that wants to
 * say "and the poll does NOT run when nothing published" needs its opposite,
 * which is simply not calling this.
 */
void Testbed_PublishTree(void);

/* The panel ------------------------------------------------------------- */

/** Drive one on_ui_build for `view`, as the host's page selection does. */
void Testbed_PanelBuild(struct Porcelain* porcelain, int view);
/**
 * Move the HOST's copy of one SELECT's chosen value, behind the layer's back.
 *
 * Which is exactly what the host does before it dispatches a pick: it commits
 * the result to its own widget model and only then tells the plugin, so a
 * plugin that refuses is looking at a control already showing the value.
 * Nothing about the plugin's description moved, which is why the reconciler
 * cannot see it and why Porcelain_Restate has to exist.
 */
void Testbed_PanelSetSelected(char const* id, char const* value);
/** No page of this plugin's is up: the scroll verbs answer accordingly. */
void Testbed_SetPanelAbsent(bool absent);
/** Drive one on_ui_action. */
bool Testbed_PanelAction(struct Porcelain* porcelain, char const* id, int action, int value,
                         char const* text);
struct TestbedPanelRow* Testbed_PanelRow(char const* id);
/** The row at `index` in declaration order, or NULL. */
struct TestbedPanelRow* Testbed_PanelRowAt(int index);
int Testbed_PanelRowCount(void);

#endif /* TORIRS_PORCELAIN_TESTBED_H */
