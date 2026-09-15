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
    /*
     * The TARGET this control's parent was derived from, and the value of the
     * ELEMENT input stamp when it was asked.
     *
     * The parent is re-derived only when one of these says it could have
     * changed, because asking the engine for it unconditionally would cost a
     * call per item per reconcile and break the rule that an identical
     * re-description costs nothing at all. The element stamp and not the
     * fence counter: a widget event raised BETWEEN two fences is stamped with
     * the fence that has not ended yet, so a fence counter cannot tell that
     * move apart from the create it followed.
     */
    struct ToriRS_WidgetRef target_ref;
    uint32_t target_element_stamp;
    /** This item's index in the last description. The order the engine draws
     *  this plugin's own children in, so "over everything I own" is the
     *  highest of them. @see ToriRS_PorcelainApi::raise */
    int order;
    /** A setter answered STALE_REFERENCE: the engine has dropped this node
     *  and the next reconcile builds it again.
     *  @see porcelain_note_item_result */
    bool stale;
};

/* ----------------------------------------------------------------- edits */

enum PorcelainEditKind
{
    PORCELAIN_EDIT_MOVE = 0,
    PORCELAIN_EDIT_HIDE,
    PORCELAIN_EDIT_SKIN,
    PORCELAIN_EDIT_OPACITY,
    PORCELAIN_EDIT_RAISE
};

struct PorcelainNormalEdit
{
    enum PorcelainEditKind kind;
    struct PorcelainElement element;
    char element_role[PORCELAIN_NAME_MAX];
    struct ToriRS_WidgetBounds box;
    int anchor_modes;
    /** RAISE: what to sit over. Kind NONE means this plugin's own topmost
     *  control. @see ToriRS_PorcelainApi::raise */
    struct PorcelainElement over;
    char over_role[PORCELAIN_NAME_MAX];
    bool behind;
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
    /** The covering declaration's reason, copied at record time and again
     *  when a relabel brings a declaration to a finding that predates it.
     *  Empty when nothing declared this finding. @see PorcelainFinding::why */
    char why[PORCELAIN_DETAIL_MAX];
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

/*
 * One named cache var whose id this handle has already resolved.
 *
 * The name a profile declares is a boot fact: `[varbit:bird_nest]` does not
 * move while the profile is loaded, so resolving it again on every ground-item
 * spawn -- which is what the shipped builtins did -- is a host call that can
 * never change its answer. `id < 0` is memoised too, and it is the interesting
 * half: a row this profile does not declare must stay undeclared rather than
 * be re-asked once per tick for the life of the session.
 */
struct PorcelainVarSlot
{
    bool used;
    /** The cache KIND -- "varbit" or "varp" -- and not the spelling the
     *  caller used, so `bird_nest` and `varbit:bird_nest` share one slot. */
    char kind[8];
    char name[PORCELAIN_NAME_MAX];
    /** The profile's id, or -1 for "this profile does not declare the row". */
    int id;
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

/* ----------------------------------------------------------------- rows */

/** One copied select option. Nothing here is the caller's memory. */
struct PorcelainOption
{
    char value[PORCELAIN_OPTION_VALUE_MAX];
    char label[PORCELAIN_OPTION_LABEL_MAX];
    char detail[PORCELAIN_OPTION_DETAIL_MAX];
    bool enabled;
};

/*
 * A described row, normalised.
 *
 * Two hashes and not one, because the row model has two questions where the
 * element model has one. `identity` is the part the host builds a row FROM
 * and cannot restate afterwards -- a change to it is a rebuild. `properties`
 * is everything its patch path can apply. Folding them together would make
 * every caption a rebuild, which is the exact defect the host fix H1 removed.
 */
struct PorcelainNormalRow
{
    struct PorcelainKey key;
    enum PorcelainRowKind kind;
    char label[PORCELAIN_ROW_TEXT_MAX];
    char text[PORCELAIN_ROW_TEXT_MAX];
    int value;
    int height;
    bool disabled;
    uint64_t hit_key;
    uint64_t paint_key;
    /** Porcelain_Reidentify named this key inside the run that made it. */
    bool force_reidentify;
    int option_first;
    int option_count;
    PorcelainRowActionFn on_action;
    PorcelainRowActionFn on_menu;
    PorcelainRowPaintFn paint;
    void* user;
    uint64_t identity;
    uint64_t properties;
    /* The property hash, split by the setter that carries each part, so a
     * caption change costs a set_text and NOT also the set_value that says
     * the button is still available. */
    uint64_t text_hash;
    /* The row's NAME, on the four kinds that have one beside their value. It
     * was part of the identity hash until the host grew a patch arm for it. */
    uint64_t label_hash;
    uint64_t options_hash;
    /* The int this kind actually pushes: a BUTTON's availability, a TOGGLE's
     * checked state, a PROGRESS bar. */
    int pushed_value;
};

/*
 * What the HOST holds for one row.
 *
 * Only the key and the three hashes, because every string the reconciler
 * would push is read from the live description and never from here. A second
 * copy of every caption and every option would be 40 KB a panel to answer a
 * question two 64-bit compares already answer.
 */
struct PorcelainDeclaredRow
{
    struct PorcelainKey key;
    enum PorcelainRowKind kind;
    uint64_t identity;
    uint64_t properties;
    uint64_t hit_key;
    uint64_t paint_key;
    uint64_t text_hash;
    uint64_t label_hash;
    uint64_t options_hash;
    int pushed_value;
    int height;
    /**
     * The HOST moved this row behind the description's back.
     *
     * Set by Porcelain_Restate and cleared by the reconcile that honours it.
     * It lives on the DECLARED row and not on the scratch one because that is
     * what it is about: the plugin's description is unchanged and correct, and
     * it is the host's copy that drifted. @see Porcelain_Restate.
     */
    bool restate;
};

/*
 * One plugin's panel. Claimed by Porcelain_Panel and released by
 * Porcelain_Close; `declared` is what the HOST has, `rows` is what the last
 * describe said, and the difference between the two is the whole reconcile.
 */
struct PorcelainPanel
{
    bool used;
    struct Porcelain* owner;
    unsigned faces;
    int width;
    char icon[PORCELAIN_NAME_MAX];
    bool registered;

    struct PorcelainNormalRow rows[PORCELAIN_ROWS_MAX];
    int row_count;
    struct PorcelainOption options[PORCELAIN_ROW_OPTIONS_MAX];
    int option_count;
    /** The run that filled `rows`; a build older than this replays nothing. */
    bool row_set_ready;
    /** A describe overran a row or option capacity: the last good
     *  description stands, exactly as a poisoned item run leaves it. */
    bool row_scratch_poisoned;

    struct PorcelainDeclaredRow declared[PORCELAIN_ROWS_MAX];
    int declared_count;
    /** The host holds this declaration and it is ours. */
    bool built;
    int built_view;
    /*
     * The host asked for a declaration on a face we cover and got nothing,
     * because that run's rows had overrun a capacity. The host will not ask
     * again by itself, so the first fence with a description that fits owes
     * it an invalidate -- once, and never on a face that is not ours, which
     * would be a rebuild every fence for the life of the session.
     */
    bool declaration_owed;

    struct PorcelainPanelCounters counters;
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
    /**
     * Porcelain_Invalidate was called from INSIDE a describe.
     *
     * The bump is held here until the fence is over. @see Porcelain_Invalidate
     * for why: bumping the stamp from inside the run makes the fence re-run
     * the describe and reconcile the SECOND pass, so the description that was
     * being written when the plugin asked to be asked again is discarded.
     */
    bool invalidate_after_fence;
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
    struct PorcelainVarSlot vars[PORCELAIN_VARS_MAX];

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
    /** This handle's panel, or NULL until Porcelain_Panel claims one. */
    struct PorcelainPanel* panel;
    struct ToriRS_PorcelainDescribe builder;
    /** One element of this lane has resolved at least once, so its interface
     *  exists and an element that still will not resolve is absent rather than
     *  early. @see porcelain_resolve_pending. */
    bool any_element_bound;

    /**
     * The one `@tree` subscription this handle takes, and what it is for.
     *
     * An element that is not in the tree can only appear when the tree
     * changes, so that -- and not a clock -- is when an unresolved watch is
     * worth re-asking about. @see porcelain_resolve_pending.
     */
    bool tree_subscribed;
    /** A topology publication has landed since the last pending poll. */
    bool tree_moved;
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
/** Mark every UNSUPPORTED finding already recorded against `feature` as
 *  expected. The absence half has had this since round two
 *  (porcelain_relabel_absence); without the same arm on this half a plugin
 *  that declared a limitation the instant it discovered it -- which is the
 *  only moment it CAN discover one -- still failed the clean gate for the
 *  finding that told it. @see Porcelain_ExpectUnsupported. */
void Porcelain_RelabelUnsupported(struct Porcelain* porcelain, char const* feature);
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

/* ------------------------------------------------------------- the panel */

/** Run the describe function once, outside a fence. The panel build callback
 *  can arrive before this plugin has ever fenced, and declaring an empty page
 *  there and filling it a frame later is a flicker with a rebuild in it. */
void Porcelain_DescribeNow(struct Porcelain* porcelain);
/** Start a describe run's row scratch. Called from porcelain_run_describe. */
void Porcelain_PanelRunBegin(struct Porcelain* porcelain);
/** Diff the described rows against what the host holds. Called from the
 *  reconcile, after the poisoned check: half a description is the flicker
 *  class here for exactly the reason it is there. */
void Porcelain_PanelReconcile(struct Porcelain* porcelain);
/** Release this handle's panel. Called from Porcelain_Close. */
void Porcelain_PanelClose(struct Porcelain* porcelain);
/** Drop every panel. Called from Porcelain_ResetForTesting. */
void Porcelain_PanelResetForTesting(void);
/** Zero this handle's panel counters, or do nothing where it has no panel.
 *  Called from Porcelain_CountersReset: a test that resets the counters and
 *  then reads a rebuild count wants the window it just opened. */
void Porcelain_PanelCountersReset(struct Porcelain* porcelain);
/* ------------------------------------------- frames (porcelain_frames.c) */

/** The frame arm of Porcelain_Unsupported: the describe run's refusal has to
 *  reach the host as TORIRS_FRAME_UNSUPPORTED and not only as a finding, or
 *  the lane's own frame comes down with nothing to replace it. A no-op for a
 *  handle that provides no frame. */
void Porcelain_FrameNoteUnsupported(struct Porcelain* porcelain, char const* reason);
/** Drop this handle's frame row. Takes NULL: reached from Porcelain_Close. */
void Porcelain_FrameForget(struct Porcelain* porcelain);
/** True while a frame provider has answered PENDING and not yet READY.
 *  @see PORCELAIN_ABSENT_FRAME_GRACE for why the absence clock SLOWS there --
 *  it must not stop, because a provider is PENDING precisely because
 *  something it asked about has not resolved. */
bool Porcelain_FrameWaiting(struct Porcelain* porcelain);
/** @see Porcelain_ResetForTesting. */
void Porcelain_FrameResetForTesting(void);

#endif /* TORIRS_PORCELAIN_INTERNAL_H */
