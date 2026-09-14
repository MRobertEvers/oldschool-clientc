#ifndef TORIRS_PORCELAIN_INTERNAL_H
#define TORIRS_PORCELAIN_INTERNAL_H

/*
 * Porcelain's own state. Nothing here is host state: every field is either a
 * copy of an answer the public API gave, or bookkeeping about what this
 * library last told the engine. Closing a handle and re-opening it must be
 * indistinguishable from never having opened it, which is what makes "any
 * Porcelain plugin can be hand-expanded into raw api-> calls" true.
 */

#include "plugin/porcelain/torirs_porcelain.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---------------------------------------------------------------- strings */

/** A fixed, always-terminated string. Over-long input is a caller bug. */
struct PorcelainKey
{
    char text[PORCELAIN_KEY_MAX];
};
struct PorcelainName
{
    char text[PORCELAIN_NAME_MAX];
};

/* ------------------------------------------------------------- an element */

/*
 * One watched element. The role name is resolved once, at registration; the
 * watch is registered once, with watch_state, and never again. The state is
 * whatever the last STATE_CHANGED / BOUND / UNBOUND left.
 */
struct PorcelainWatch
{
    bool used;
    /** The handle this watch belongs to: the listener's `user`. */
    struct Porcelain* owner;
    struct PorcelainElement element;
    /** The element's `role` pointer is the caller's; this is Porcelain's copy. */
    char element_role[PORCELAIN_NAME_MAX];
    char role[TORIRS_UI_NAME_MAX];
    struct PorcelainElementState state;
    /** Fences this element has failed to resolve. @see PORCELAIN_ABSENT_FENCES */
    int pending_fences;
    /** The absence has already been reported once. */
    bool absence_reported;
    /** The describe run that last asked for this element. */
    uint32_t last_wanted_run;
};

/* ----------------------------------------------------------------- items */

/*
 * A described item, normalised: every borrowed string copied, every optional
 * element resolved to a kind plus a member. The property hash is over exactly
 * these fields, so "unchanged hash" and "every setter would restate what is
 * already in force" are the same statement.
 */
struct PorcelainNormalItem
{
    struct PorcelainKey key;
    enum PorcelainItemKind kind;
    bool has_image;
    struct PorcelainName image;
    struct PorcelainPlacement place;
    char place_on_role[PORCELAIN_NAME_MAX];
    char place_depth_role[PORCELAIN_NAME_MAX];
    char visible_with_role[PORCELAIN_NAME_MAX];
    int width, height;
    int opacity;
    bool has_text;
    char text[PORCELAIN_TEXT_MAX];
    uint32_t rgb;
    int align;
    bool outline;
    char op_label[TORIRS_WIDGET_OP_LABEL_MAX];
    PorcelainOpFn on_op;
    void* user;
    bool hit;
    bool enabled;
    struct PorcelainElement visible_with;
    uint64_t hash;
};

/** What Porcelain last told the engine about one live control. */
struct PorcelainAppliedItem
{
    bool live;
    /** The handle this control belongs to: the op listener's `user`. */
    struct Porcelain* owner;
    struct PorcelainNormalItem item;
    struct ToriRS_WidgetRef ref;
    struct ToriRS_WidgetRef parent;
    struct ToriRS_WidgetRef anchor_target;
    /*
     * Two boxes, not one. `desired` is what the last describe plus the
     * target's current geometry say the box should be; `live_*` is what was
     * actually written. The direct motion path writes live_* and leaves
     * `desired` alone, so a fence that finds the target unmoved does not fight
     * an animation -- and a fence that finds it moved still rebases it.
     */
    struct ToriRS_WidgetBounds desired;
    bool desired_written;
    int32_t live_x, live_y;
    int32_t live_w, live_h;
    struct ToriRS_ImageRef image_ref;
    /* The state that ref came back with. A PENDING asset resolves to a zero
     * ref, and nothing in the property pass would ever ask again: the name
     * did not change and the hash did not change, so the control kept the
     * blank it was created with for the rest of the session. */
    enum PorcelainAssetState image_state;
    /* The picture changed but has not reached the engine yet. An owned image
     * control takes its picture AND its box through one set_image, so the two
     * are written together or the fresh control pays two calls: one with a
     * 0x0 box and one with the real one. */
    bool image_dirty;
    int applied_opacity;
    bool applied_hidden;
    bool hidden_written;
};

/* ----------------------------------------------------------------- edits */

enum PorcelainEditKind
{
    PORCELAIN_EDIT_MOVE = 0,
    PORCELAIN_EDIT_HIDE,
    PORCELAIN_EDIT_SKIN,
    PORCELAIN_EDIT_OPACITY
};

struct PorcelainNormalEdit
{
    enum PorcelainEditKind kind;
    struct PorcelainElement element;
    char element_role[PORCELAIN_NAME_MAX];
    struct ToriRS_WidgetBounds box;
    int anchor_modes;
    bool has_image;
    struct PorcelainName image;
    bool has_mask;
    struct PorcelainName mask;
    int opacity;
    uint64_t hash;
};

struct PorcelainAppliedEdit
{
    bool live;
    struct PorcelainNormalEdit edit;
    struct ToriRS_WidgetRef ref;
};

/* -------------------------------------------------------------- findings */

struct PorcelainFindingSlot
{
    bool used;
    struct PorcelainFinding pub;
    char verb[PORCELAIN_VERB_MAX];
    char detail[PORCELAIN_DETAIL_MAX];
    char role[PORCELAIN_NAME_MAX];
};

struct PorcelainExpectAbsent
{
    bool used;
    struct PorcelainElement element;
    char element_role[PORCELAIN_NAME_MAX];
    char why[PORCELAIN_DETAIL_MAX];
    bool reported_present;
};

/** A declared lane limitation, matched against an UNSUPPORTED finding's
 *  DETAIL -- which is the feature name on every route that records one. */
struct PorcelainExpectUnsupported
{
    bool used;
    char feature[PORCELAIN_DETAIL_MAX];
    char why[PORCELAIN_DETAIL_MAX];
};

/* ---------------------------------------------------------------- assets */

struct PorcelainImageSlot
{
    bool used;
    struct PorcelainName name;
    struct ToriRS_ImageRef ref;
    enum PorcelainAssetState state;
    bool terminal_reported;
    uint32_t last_used_run;
};

struct PorcelainModelSlot
{
    bool used;
    struct PorcelainName name;
    struct ToriRS_ModelRef ref;
    enum PorcelainAssetState state;
    bool terminal_reported;
};

struct PorcelainDerivedSlot
{
    bool used;
    struct PorcelainKey key;
    uint64_t inputs_hash;
    int width, height;
    struct ToriRS_ImageRef ref;
    enum PorcelainDerivedState state;
    bool terminal_reported;
};

/* ----------------------------------------------------- cadence, readiness */

/*
 * One timer, keyed on (fn, user). Registering the same pair again re-intervals
 * it in place: the table is fixed at PORCELAIN_TIMERS_MAX and an append-only
 * registration leaked a slot every time a user moved a refresh slider.
 */
struct PorcelainTimer
{
    bool used;
    /** Zero means "a cadence", not "every 0 ms". @see PorcelainTimer::cadence */
    int milliseconds;
    enum PorcelainCadence cadence;
    bool is_ms;
    uint64_t next_due_ms;
    /** When it last fired, so the callback is told the REAL elapsed time. */
    uint64_t last_fired_ms;
    PorcelainTickFn fn;
    void* user;
};

struct PorcelainReadyWatch
{
    bool used;
    unsigned what;
    PorcelainReadyFn fn;
    void* user;
    bool fired;
};

struct PorcelainKeyEdgeWatch
{
    bool used;
    char config_key[PORCELAIN_NAME_MAX];
    PorcelainEdgeFn fn;
    void* user;
    bool down;
    bool absent_reported;
    /** The code the config value last resolved to, re-read at the fence so a
     *  rebind takes effect without a reload. -1 is "no key bound". */
    int code;
};

/* ------------------------------------------------------------- overlays */

/** One shipped data file, read and parsed exactly once. */
struct PorcelainTableSlot
{
    bool used;
    struct PorcelainName asset;
    bool parsed;
    /** Terminal: absent, errored, or the parse refused the bytes. */
    bool failed;
    bool reported;
};

/** One announcement, so a repeat within the frame is one line. */
struct PorcelainNotifySlot
{
    bool used;
    char kind[PORCELAIN_NAME_MAX];
    int subject;
    uint32_t frame;
};

/*
 * The CS2 caption hook's latch. Registration decides ABSENT once, from the
 * capability and from whether the label role resolves; after that the only
 * transition is SUPPRESSING -> FORMATTING, on the first callback.
 */
struct PorcelainNativeOverlay
{
    bool used;
    enum PorcelainNativeOverlayState state;
    char labels_role[PORCELAIN_NAME_MAX];
    char callback[PORCELAIN_NAME_MAX];
    PorcelainScriptFn fn;
    void* user;
    /** The natives this latch hid, so the handoff can hand each one back. */
    struct ToriRS_WidgetRef hidden[PORCELAIN_OVERLAY_LABELS_MAX];
    int hidden_count;
    bool handoff_reported;
};

/* ---------------------------------------------------------------- handle */

struct Porcelain
{
    bool used;
    /** Open order. Arbitration's "first claimer by host event order". */
    int order;
    struct ToriRS_Api* api;
    struct ToriRS_PluginDef const* def;
    char plugin_id[PORCELAIN_NAME_MAX];
    void* state;

    PorcelainDescribeFn describe_fn;
    void* describe_user;

    /** Bumped by every input; compared against `applied_stamp`. */
    uint32_t stamp[PORCELAIN_INPUT_COUNT];
    uint32_t applied_stamp[PORCELAIN_INPUT_COUNT];
    bool ever_described;
    /** Inside a describe run: the builder verbs assert on this. */
    bool describing;
    /** The run counter, for image idling and watch liveness. */
    uint32_t run;
    /** Porcelain's own frame counter, stamped into findings and states. */
    uint32_t frame;

    struct PorcelainWatch watches[PORCELAIN_WATCHES_MAX];
    /* The clipping root, derived once by a parent walk and held until an
     * element rebinds. The audit measured a 32-hop root walk PER FENCE in a
     * shipped plugin; doing it again here would import the defect. */
    struct ToriRS_WidgetRef frame_root;
    bool frame_root_known;
    uint32_t frame_root_stamp;

    struct PorcelainNormalItem scratch_items[PORCELAIN_ITEMS_MAX];
    int scratch_item_count;
    struct PorcelainNormalEdit scratch_edits[PORCELAIN_EDITS_MAX];
    int scratch_edit_count;
    /** The describe run errored or overran; the applied description stands. */
    bool scratch_poisoned;

    struct PorcelainAppliedItem applied_items[PORCELAIN_ITEMS_MAX];
    int applied_item_count;
    struct PorcelainAppliedEdit applied_edits[PORCELAIN_EDITS_MAX];
    int applied_edit_count;

    struct PorcelainFindingSlot findings[PORCELAIN_FINDINGS_MAX];
    struct PorcelainExpectAbsent expects[PORCELAIN_EXPECT_MAX];
    struct PorcelainExpectUnsupported expect_unsupported[PORCELAIN_EXPECT_UNSUPPORTED_MAX];

    struct PorcelainImageSlot images[PORCELAIN_IMAGES_MAX];
    struct PorcelainModelSlot models[PORCELAIN_MODELS_MAX];
    struct PorcelainDerivedSlot derived[PORCELAIN_DERIVED_MAX];

    struct PorcelainTimer timers[PORCELAIN_TIMERS_MAX];
    struct PorcelainReadyWatch ready[PORCELAIN_READY_MAX];
    struct PorcelainKeyEdgeWatch key_edges[PORCELAIN_KEY_EDGES_MAX];

    struct PorcelainTableSlot tables[PORCELAIN_TABLES_MAX];
    struct PorcelainNotifySlot notifies[PORCELAIN_NOTIFY_MAX];
    struct PorcelainNativeOverlay overlay;
    /** The last hovered cell and the frame it was stamped in. A hover older
     *  than one frame is not a hover: the right-click menu stops the rebuild,
     *  and a tooltip that kept following the pointer under an open menu is
     *  what this window exists to prevent. */
    struct PorcelainHover hover;
    bool hover_live;

    /** Something was written this epoch, so the commit owes a revalidate. */
    bool dirty;
    /** The epoch this handle last fenced in. */
    uint32_t fenced_epoch;

    struct PorcelainCounters counters;
    struct ToriRS_PorcelainDescribe builder;
    /** One element of this lane has resolved at least once, so its interface
     *  exists and an element that still will not resolve is absent rather than
     *  early. @see porcelain_resolve_pending. */
    bool any_element_bound;
};

/* ----------------------------------------------------------- arbitration */

struct PorcelainClaim
{
    bool used;
    uint64_t element_key;
    enum PorcelainAspect aspect;
    struct Porcelain* owner;
    /** Kept for the loser's finding, which must name the winner. */
    char owner_id[PORCELAIN_NAME_MAX];
    /** The owner's describe run that last re-asserted this claim. A claim the
     *  owner stopped describing is released, or a plugin that once drew an orb
     *  would own that orb for the life of the process. */
    uint32_t taken_run;
};

/* --------------------------------------------------------- shared helpers */

uint64_t Porcelain_HashBytes(uint64_t seed, void const* data, size_t length);
uint64_t Porcelain_HashString(uint64_t seed, char const* text);
void Porcelain_CopyString(char* destination, size_t capacity, char const* source);
uint64_t Porcelain_ElementKey(struct PorcelainElement element);
void Porcelain_FormatElement(struct PorcelainElement element, char* out, size_t capacity);

void Porcelain_RecordFinding(struct Porcelain* porcelain, char const* verb,
                             struct PorcelainElement element, int result, char const* detail);
bool Porcelain_ResolveRole(struct Porcelain* porcelain, struct PorcelainElement element, char* out,
                           size_t capacity);
struct PorcelainWatch* Porcelain_WatchFor(struct Porcelain* porcelain,
                                          struct PorcelainElement element, bool create);

bool Porcelain_ClaimTake(struct Porcelain* porcelain, struct PorcelainElement element,
                         enum PorcelainAspect aspect, char const** out_winner);
void Porcelain_ClaimDropAll(struct Porcelain* porcelain);

/** Per-fence helper work: asset state transitions, timers, readiness, key
 *  edges. Separated from the reconcile because none of it touches the tree. */
void Porcelain_HelpersFence(struct Porcelain* porcelain);
/** Hand every native this handle's overlay latch suppressed back to its own
 *  visibility. A handle that closed while suppressing would otherwise leave
 *  the cache's own captions hidden until the next tree rebuild. */
void Porcelain_OverlayRelease(struct Porcelain* porcelain);
/** Release every image, model and derived image this handle holds. */
void Porcelain_ReleaseAllAssets(struct Porcelain* porcelain);
/** Mark an image as still wanted by this run, without an engine call. The
 *  lazy release counts UNUSED runs, and an item whose hash did not change
 *  never reaches Porcelain_Image -- so without this the picture a settled
 *  description depends on would be released underneath it. */
void Porcelain_ImageTouch(struct Porcelain* porcelain, char const* name);
/** Drop this owner's claims that the run just finished did not re-assert. */
void Porcelain_ClaimDropStale(struct Porcelain* porcelain);

#endif /* TORIRS_PORCELAIN_INTERNAL_H */
