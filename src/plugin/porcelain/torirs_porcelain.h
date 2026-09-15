#ifndef TORIRS_PORCELAIN_H
#define TORIRS_PORCELAIN_H

/*
 * Porcelain: the retained, reconciling plugin layer.
 *
 * The vocabulary, the verb table and the reasons all live in
 * plugin/torirs_plugin_api.h beside `struct ToriRS_PorcelainApi`. This header
 * is the LIBRARY's face: the concrete C entry points the table points at, the
 * accessor a host uses to install the table, and the fixed capacities that
 * make "zero allocation in the steady state" true rather than aspirational.
 *
 * Porcelain is a client of `struct ToriRS_Api` and of nothing else. It
 * includes no engine header, holds no host state, and has no authority: any
 * Porcelain plugin can be hand-expanded into raw `api->` calls, and the
 * testbed's recorded call log is that expansion.
 */

#include "plugin/torirs_plugin_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ */
/* Capacities                                                               */
/* ------------------------------------------------------------------------ */

/*
 * Every one of these is a FIXED array inside `struct Porcelain`. A describe
 * that overruns one records a budget finding and drops the item -- it never
 * grows a buffer, because a reconciler that allocates in its steady state is
 * the per-frame 2 MB the audit measured.
 */
#define PORCELAIN_ITEMS_MAX 64
/*
 * Edits one description may state.
 *
 * Forty-eight, because the desktop frame provider is the consumer that sets
 * it and thirty-two was below its floor. One pass of gameframe-layout's
 * classic-fixed layout over an OldSchool toplevel states thirty-six: seven
 * surface moves, the sidebar's fourteen mounts, the orb block's three
 * children, four masks and re-skins, and the eight chat plates the 2004
 * dressing hides. The resizable layout adds the four chat-filter boxes.
 *
 * A frame provider cannot shrink its way under a ceiling the way an overlay
 * can -- every one of those edits is a surface the frame is responsible for
 * placing -- so the budget finding it hit was not a plugin asking for too
 * much. @see the gameframe-layout port report.
 */
#define PORCELAIN_EDITS_MAX 48
#define PORCELAIN_WATCHES_MAX 48
#define PORCELAIN_FINDINGS_MAX 32
#define PORCELAIN_EXPECT_MAX 16
#define PORCELAIN_IMAGES_MAX 48
#define PORCELAIN_MODELS_MAX 16
#define PORCELAIN_DERIVED_MAX 16
#define PORCELAIN_TIMERS_MAX 16
#define PORCELAIN_READY_MAX 8
#define PORCELAIN_KEY_EDGES_MAX 4
/** Shipped data files one plugin reads. Two: prices.txt and items.txt is the
 *  widest any of the seven overlay ports wanted. */
#define PORCELAIN_TABLES_MAX 4
/** Announcements remembered for coalescing. @see Porcelain_Notify */
#define PORCELAIN_NOTIFY_MAX 16
/** Native label widgets one overlay latch suppresses. @see find_all */
#define PORCELAIN_OVERLAY_LABELS_MAX 32
/** Lane limitations one plugin declares. @see Porcelain_ExpectUnsupported */
#define PORCELAIN_EXPECT_UNSUPPORTED_MAX 8
/** Named cache vars one plugin resolves. Sixteen: the widest of the shipped
 *  builtins reads five (two cannon varps and three setting varbits) and the
 *  All Settings family has thirty rows behind one renderer, so a table sized
 *  at the current widest would push the next builtin straight into the
 *  budget finding. @see Porcelain_SettingValue */
#define PORCELAIN_VARS_MAX 16

/*
 * `ToriRS_MenuRow::pick_kind` for a container cell -- the engine's
 * UI_MINIMENU_PICK_INV_SLOT, mirrored because Porcelain is a plugin-side
 * library and may not include the engine's header. The two are pinned equal
 * by a _Static_assert in the adapter (torirs_plugin_bridge.u.c), which is the
 * one translation unit that sees both; the shipped tooltip plugin spells this
 * as a bare `2` with the name in a comment.
 */
#define PORCELAIN_MENU_PICK_INV_SLOT 2

/*
 * Panel capacities. Rows match the host's own control budget
 * (TORIRS_PLUGIN_WIDGETS_MAX) and the option pool matches its select pool
 * (TORIRS_PLUGIN_SELECT_OPTIONS_MAX), so a description that fits here is one
 * the host will accept: a ceiling that is lower than the engine's would
 * refuse pages the engine can present, and a higher one would push the
 * refusal past the frame that caused it.
 *
 * The pool is per PANEL and not per handle: six plugins mount the shared
 * pane, so a per-handle pool would be 32 copies of a table 26 of them never
 * touch.
 */
#define PORCELAIN_PANELS_MAX 8
#define PORCELAIN_ROWS_MAX 48
#define PORCELAIN_ROW_OPTIONS_MAX 128
/*
 * A row's label and text, at the host's own config-value ceiling. Porcelain
 * REFUSES a string that does not fit and never truncates one: a truncated
 * stable value picks a different option and reads back as if the person had
 * chosen it, which is the defect config_list_add already exists to refuse.
 */
#define PORCELAIN_ROW_TEXT_MAX 192
/*
 * An option's three strings, at the SAME ceiling and by the same name.
 *
 * They were 96 while the host accepted 192, so Porcelain refused strings the
 * host would have taken -- and until this round a refusal poisoned the run,
 * so one over-long provider id blanked a whole settings page. A layer ceiling
 * below the host's is a refusal that is about the layer and not about the
 * lane, which is the one thing a portability library must not invent.
 */
#define PORCELAIN_OPTION_VALUE_MAX PORCELAIN_ROW_TEXT_MAX
#define PORCELAIN_OPTION_LABEL_MAX PORCELAIN_ROW_TEXT_MAX
#define PORCELAIN_OPTION_DETAIL_MAX PORCELAIN_ROW_TEXT_MAX
/*
 * A row's key, at the HOST's own row-id ceiling and not at Porcelain's
 * element-key one, which is wider. A key the layer accepted and the host then
 * refused would be a finding per row per build, arriving a page late; at the
 * narrower ceiling the refusal lands on the describe that wrote it.
 */
#define PORCELAIN_ROW_KEY_MAX TORIRS_PLUGIN_WIDGET_ID_MAX
/** Frame offers one plugin may describe. gameframe-layout ships three. */
#define PORCELAIN_FRAME_OFFERS_MAX 4
/**
 * Sidebar tabs in the portable vocabulary.
 *
 * Fourteen, which is what every revision since 2001 numbers, and sixteen only
 * so a fifteenth does not need a new constant. This is a VOCABULARY and not a
 * lane fact: which of the fourteen a lane HAS, and what number it gives each,
 * are both answered by the profile's `[tabs]` map.
 */
#define PORCELAIN_TAB_MAX 16
/*
 * Rows or columns one root can run its stones in.
 *
 * One per tab, because that is the real ceiling and not a guess: a root that
 * runs its fourteen stones in a single ROW has fourteen COLUMNS, and both
 * questions are asked of every root. A smaller number would make the common
 * desktop root answer a budget finding for arranging its tabs the ordinary
 * way.
 */
#define PORCELAIN_TAB_GROUPS_MAX PORCELAIN_TAB_MAX

/** Handles alive at once. Also the arbitration order: open order. */
#define PORCELAIN_HANDLES_MAX 32
/** Claims across every handle: elements times aspects, bounded. */
#define PORCELAIN_CLAIMS_MAX 128

#define PORCELAIN_KEY_MAX 48
#define PORCELAIN_NAME_MAX 64
#define PORCELAIN_TEXT_MAX 128
#define PORCELAIN_DETAIL_MAX 96
#define PORCELAIN_VERB_MAX 24

/*
 * An element stays PENDING for this many fences of failed resolution before
 * it is called ABSENT. Two and not one because the first fence of a session
 * runs before any tree has published, and calling every element absent there
 * would fire a finding for the whole vocabulary on every boot.
 *
 * Counted from the last time ANYTHING new bound, not from the first ask.
 * @see porcelain_note_new_binding.
 */
#define PORCELAIN_ABSENT_FENCES 2

/*
 * And this many times that, while a frame PROVIDER is still answering PENDING.
 *
 * A provider names its whole vocabulary on the fence its gate element binds --
 * the viewport, which the lane mounts first -- and the toplevel's chat,
 * sidebar, modal and orb block arrive several fences later. At the plain two
 * all four were reported ABSENT, once each, on every desktop lane, and bound
 * immediately afterwards; a finding nobody can act on is worse than none,
 * because the clean-findings gate is how a real absence gets seen.
 *
 * A MULTIPLIER and not a suspension, which is the whole point: a provider is
 * PENDING precisely because something it asked about has not resolved, so
 * suspending the clock while it waits is a deadlock -- the element cannot be
 * called absent, so the frame cannot come up, so the element cannot be called
 * absent. Sixteen fences is long enough for a toplevel to finish mounting and
 * short enough that a surface the lane truly lacks is still named before the
 * frame has been up a second.
 */
#define PORCELAIN_ABSENT_FRAME_GRACE 8

/*
 * Operations per subject in a menu tag. Sixteen, and not the two and the four
 * the two shipped plugins each picked by hand, so one encoding serves both
 * and a third plugin does not need a fifth.
 */
#define PORCELAIN_MENU_TAG_OPS 16

/*
 * The host's config value ceiling, mirrored because Porcelain is a
 * plugin-side library and may not include the host's header. It is
 * TORIRS_PLUGIN_CONFIG_VALUE_MAX in plugin/torirs_plugin_host.h; the two are
 * pinned equal by porcelain_test.c, because a list joined against the wrong
 * ceiling is exactly the truncated value config_list_add exists to refuse.
 */
#define PORCELAIN_CONFIG_VALUE_MAX 192

/*
 * Items one stored list may hold. Ninety-six is the most a value of
 * PORCELAIN_CONFIG_VALUE_MAX bytes can express -- one-character items with a
 * comma between each -- so the table can never be the binding limit and the
 * refusal is always the value ceiling, which is the one a person can see.
 */
#define PORCELAIN_CONFIG_LIST_MAX ((PORCELAIN_CONFIG_VALUE_MAX + 1) / 2)

/*
 * Describe runs after which an image nothing asked for is released. Four and
 * not one because the readiness matrix legitimately drops an image for a
 * fence or two while an element rebinds, and re-requesting it costs a decode.
 */
#define PORCELAIN_IMAGE_IDLE_RUNS 4

/* ------------------------------------------------------------------------ */
/* The table                                                                */
/* ------------------------------------------------------------------------ */

/**
 * The static verb table. A host assigns it to `api->porcelain`; nothing about
 * it is per-process, so the same pointer serves every runtime.
 */
struct ToriRS_PorcelainApi const* ToriRS_PorcelainApiTable(void);

/* ------------------------------------------------------------------------ */
/* The verbs, as plain C functions                                          */
/* ------------------------------------------------------------------------ */

/*
 * These ARE the table's targets. A C plugin may call either spelling; Lua
 * reaches the same bodies through the table. Contract violations abort here
 * and not at the caller's next frame: one assert per parameter.
 */

struct Porcelain* Porcelain_Open(struct ToriRS_Api* api, struct ToriRS_PluginDef const* def,
                                 void* state);
void Porcelain_Close(struct Porcelain* porcelain);
void Porcelain_Describe(struct Porcelain* porcelain, PorcelainDescribeFn fn, void* user);
void Porcelain_Invalidate(struct Porcelain* porcelain);
void Porcelain_Note(struct Porcelain* porcelain, enum PorcelainInput input);
void Porcelain_Fence(struct Porcelain* porcelain);
void Porcelain_Commit(struct ToriRS_Api* api);
void Porcelain_Relinquish(struct Porcelain* porcelain);

bool Porcelain_Element(struct Porcelain* porcelain, struct PorcelainElement element,
                       struct PorcelainElementState* out);
int Porcelain_Count(struct Porcelain* porcelain, enum PorcelainElementKind family);

enum ToriRS_Result Porcelain_Set(struct Porcelain* porcelain, char const* key,
                                 struct PorcelainMotion const* motion);

int Porcelain_Findings(struct Porcelain* porcelain, struct PorcelainFinding* out, int capacity);
void Porcelain_ExpectAbsent(struct Porcelain* porcelain, struct PorcelainElement element,
                            char const* why);
void Porcelain_ExpectUnsupported(struct Porcelain* porcelain, char const* feature,
                                 char const* why);
bool Porcelain_Has(struct Porcelain* porcelain, char const* capability);
bool Porcelain_Require(struct Porcelain* porcelain, char const* capability, char const* feature);

int Porcelain_Tier(struct PorcelainTiers const* tiers, int64_t value);
bool Porcelain_TiersFromConfig(struct Porcelain* porcelain, struct PorcelainTiers* out);
bool Porcelain_ConfigListAdd(struct Porcelain* porcelain, char const* key, char const* item);
uint32_t Porcelain_MenuTag(int subject, int op);
bool Porcelain_Setting(struct Porcelain* porcelain, char const* varbit_name, unsigned flags);
int Porcelain_SettingValue(struct Porcelain* porcelain, char const* name, int absent);
bool Porcelain_KeyEdge(struct Porcelain* porcelain, char const* config_key, PorcelainEdgeFn fn,
                       void* user);
void Porcelain_NoteKey(struct Porcelain* porcelain, int key, bool down);
/**
 * A handle for a shipped picture, requested on first ask and cached.
 *
 * WHEN a plugin first asks is observable, so ask at a fixed point.
 *
 * The engine hands out image slots in allocation order and a capture prints
 * the slot id. Moving two of these calls out of on_start and into a describe
 * shifted every slot allocated after them and reddened the `scene` field on
 * all seven gate lanes for no behaviour change whatever -- a gate cycle spent
 * on a renumbering. A slot id is an internal handle and nothing reads it, but
 * it IS captured, so claim every picture a plugin knows it needs from
 * on_start and let the describes ask for handles that already exist.
 */
struct ToriRS_ImageRef Porcelain_Image(struct Porcelain* porcelain, char const* name,
                                       enum PorcelainAssetState* out_state);
bool Porcelain_ImageSize(struct Porcelain* porcelain, char const* name, int* out_width,
                         int* out_height);

/**
 * Forget the handle cached for `name`, because it no longer means that
 * picture.
 *
 * Call it after composing over a name this handle may already have resolved.
 * @see the definition for the refusal it exists to prevent.
 */
void Porcelain_ImageForget(struct Porcelain* porcelain, char const* name);
struct ToriRS_ModelRef Porcelain_Model(struct Porcelain* porcelain, char const* name,
                                       enum PorcelainAssetState* out_state);
struct ToriRS_ImageRef Porcelain_Derived(struct Porcelain* porcelain, char const* key,
                                         void const* inputs, size_t inputs_len, int width,
                                         int height, PorcelainPaintFn paint, void* user,
                                         enum PorcelainDerivedState* out_state);
void Porcelain_WhenReady(struct Porcelain* porcelain, unsigned what, PorcelainReadyFn fn,
                         void* user);
void Porcelain_Every(struct Porcelain* porcelain, enum PorcelainCadence cadence,
                     PorcelainTickFn fn, void* user);
void Porcelain_EveryServerTick(struct Porcelain* porcelain, PorcelainTickFn fn, void* user);
void Porcelain_EveryMs(struct Porcelain* porcelain, int milliseconds, PorcelainTickFn fn,
                       void* user);
void Porcelain_CancelEvery(struct Porcelain* porcelain, PorcelainTickFn fn, void* user);
void Porcelain_Tick(struct Porcelain* porcelain, enum PorcelainCadence cadence);

/* The overlay verbs. An overlay plugin draws, adds menu rows, follows the
 * pointer and reads a shipped table; these are the six shapes the ledger's
 * seven overlay ports each wrote for themselves. */
bool Porcelain_DrawContext(struct Porcelain* porcelain, struct ToriRS_Graphics* draw,
                           struct PorcelainElement element, struct PorcelainDrawContext* out);
bool Porcelain_MenuAdd(struct Porcelain* porcelain, struct ToriRS_MenuBuildEvent* menu,
                       char const* text, uint32_t action_id);
void Porcelain_NoteMenu(struct Porcelain* porcelain, struct ToriRS_MenuBuildEvent const* menu);
bool Porcelain_Hover(struct Porcelain* porcelain, struct PorcelainHover* out);
void Porcelain_NativeOverlay(struct Porcelain* porcelain, char const* labels_role,
                             char const* callback, PorcelainScriptFn fn, void* user);
void Porcelain_NoteScript(struct Porcelain* porcelain, struct ToriRS_ScriptEvent const* event);
bool Porcelain_Table(struct Porcelain* porcelain, char const* asset, PorcelainParseFn parse,
                     void* user);
void Porcelain_Notify(struct Porcelain* porcelain, char const* kind, int subject,
                      char const* text);

/** The latch's state, for a test and for a plugin that reports its own. */
enum PorcelainNativeOverlayState Porcelain_NativeOverlayState(struct Porcelain* porcelain);

/* --- round three: the refusals a port could not read, and the partners the
 *     one-way verbs never had. Kept together so a merge is an append. --- */

/** draw->world_hull with its two refusals made loud: BUDGET when the frame's
 *  allotment ran out, ARBITRATION_LOST when another plugin holds the entity's
 *  APPEARANCE. False means nothing was drawn. */
bool Porcelain_Hull(struct Porcelain* porcelain, struct ToriRS_Graphics* draw, int element_id,
                    uint32_t rgb, int alpha, int shape);
/** draw->world_tile with its one refusal made loud: BUDGET when the frame's
 *  allotment ran out. False means nothing was drawn. A tile marker is drawn
 *  per tile of a footprint, so a crowded Activities set is the one overlay
 *  that reaches the 512 ceiling by arithmetic rather than by accident.
 *
 *  `alpha` is the wash and `outline_width` the border, each drawn only when
 *  its own number is non-zero -- a thickness of 0 is a fill with no border,
 *  which is what the cache's hovered-tile group asks for. */
bool Porcelain_Tile(struct Porcelain* porcelain, struct ToriRS_Graphics* draw, int tile_x,
                    int tile_z, int level, uint32_t fill_rgb, uint32_t outline_rgb,
                    int outline_width, int alpha);
/** A plugin's own finding, in the channel Porcelain's verbs already use.
 *  `result` is a PorcelainFindingResult and may not be OK. */
void Porcelain_Finding(struct Porcelain* porcelain, char const* verb,
                       struct PorcelainElement element, int result, char const* detail);
/** The inverse of Porcelain_MenuTag, so the 16 lives in one place. */
void Porcelain_MenuUntag(uint32_t tag, int* out_subject, int* out_op);
/** Is this key held NOW. `key` is the edge form's value vocabulary: a name, a
 *  decimal code or a single character. False with one finding on a touch lane
 *  and for a key that does not resolve. */
bool Porcelain_KeyDown(struct Porcelain* porcelain, char const* key);
/** Take one item out of a stored list. Absent is true and costs no write. */
bool Porcelain_ConfigListRemove(struct Porcelain* porcelain, char const* key, char const* item);
/** State the whole list at once: sorted, deduplicated, refused rather than
 *  truncated over the value ceiling. */
bool Porcelain_ConfigListSet(struct Porcelain* porcelain, char const* key,
                             char const* const* items, int count);

/* The describe-builder verbs. Legal only inside a describe run. */
void Porcelain_Control(struct ToriRS_PorcelainDescribe* describe, struct PorcelainItem const* item);
void Porcelain_Piece(struct ToriRS_PorcelainDescribe* describe, struct PorcelainItem const* item);
void Porcelain_Text(struct ToriRS_PorcelainDescribe* describe, struct PorcelainItem const* item);
void Porcelain_Blocker(struct ToriRS_PorcelainDescribe* describe, char const* key,
                       char const* label, struct PorcelainPlacement place, int width, int height,
                       PorcelainOpFn on_op, void* user);
void Porcelain_Move(struct ToriRS_PorcelainDescribe* describe, struct PorcelainElement element,
                    struct ToriRS_WidgetBounds box, int anchor_modes);
void Porcelain_Hide(struct ToriRS_PorcelainDescribe* describe, struct PorcelainElement element);
/** @see ToriRS_PorcelainApi::raise. `over` of kind NONE means "above
 *  everything this plugin owns". */
void Porcelain_Raise(struct ToriRS_PorcelainDescribe* describe, struct PorcelainElement element,
                     struct PorcelainElement over, bool behind);
void Porcelain_Skin(struct ToriRS_PorcelainDescribe* describe, struct PorcelainElement element,
                    char const* image, char const* mask);
void Porcelain_Opacity(struct ToriRS_PorcelainDescribe* describe, struct PorcelainElement element,
                       int opacity);
void Porcelain_Unsupported(struct ToriRS_PorcelainDescribe* describe, char const* reason);

/* ------------------------------------------------------------------------ */
/* Panels                                                                   */
/* ------------------------------------------------------------------------ */

/*
 * The row model is its own reconciler over `api->panel`, and it answers one
 * question the element reconciler does not have to: what is a REBUILD.
 *
 * The host's page reconciler validates by IDENTITY ONLY -- serial, kind, id,
 * count -- and three separate shipped bugs came from a property check inside
 * its validate loop, which turns a caption into a rebuild and a rebuild into
 * a flicker. So the rule here is the same rule, stated from the plugin's
 * side: the ordered sequence of (key, kind, identity label) IS the
 * declaration, a change to it is the page's one legitimate rebuild, and
 * every other difference is a setter on the row it names.
 */

/** on_start only. `icon_asset` NULL asks for the baked wrench, which is a
 *  meaning and not an absence. A refused registration is a finding. */
void Porcelain_Panel(struct Porcelain* porcelain, char const* icon_asset, int width,
                     unsigned faces);
/** Describe-only. Rows are emitted in description order. */
void Porcelain_Row(struct ToriRS_PorcelainDescribe* describe, struct PorcelainRow const* row);
/** Describe-only. Force ONE described row a fresh serial at this fence, for
 *  an identity input that is not expressible as PorcelainRow::hit_key. */
void Porcelain_Reidentify(struct ToriRS_PorcelainDescribe* describe, char const* key);
/**
 * The HOST's copy of ONE row drifted; state it again.
 *
 * For a refusal: the host commits a result before it dispatches, so a plugin
 * that says no is looking at a control showing a value nobody saved, while
 * its own description -- the thing the reconciler diffs -- has not moved.
 * Callable from anywhere, and an action handler is where it belongs. Costs
 * that row's setters and nothing else. @see the definition for why a feature
 * pick, which writes to config, never needs it.
 */
void Porcelain_Restate(struct Porcelain* porcelain, char const* key);
/** The reader's place, in logical pixels past the top; -1 with no page up. */
int Porcelain_PanelScroll(struct Porcelain* porcelain);
/** Move it. Clamped by the presenter's next layout, never here. */
void Porcelain_PanelScrollTo(struct Porcelain* porcelain, int scroll);

/* The three host callbacks a panel plugin forwards. Porcelain installs no
 * callbacks of its own: the definition belongs to the plugin and the host
 * already registered it. */
void Porcelain_PanelBuild(struct Porcelain* porcelain, struct ToriRS_PanelBuilder* builder,
                          int view);
bool Porcelain_PanelAction(struct Porcelain* porcelain,
                           struct ToriRS_PanelActionEvent const* event);
bool Porcelain_PanelDraw(struct Porcelain* porcelain, char const* node,
                         struct ToriRS_Graphics* draw);

/**
 * What the row reconciler did, for the tests that pin the rules above.
 *
 * `rebuilds` is the number the whole family exists to keep at zero: the plan
 * says growth, a changed option count and a changed caption must each cost a
 * setter, and without this counter "it did not rebuild" is a belief.
 */
struct PorcelainPanelCounters
{
    uint32_t builds;
    uint32_t rebuilds;
    uint32_t reidentifies;
    /** Rows restated because the HOST moved them. @see Porcelain_Restate */
    uint32_t restates;
    /**
     * Rows the LAYER refused and dropped, leaving the rest of the description.
     *
     * A refusal used to discard the whole run, so one over-long provider id
     * blanked an entire settings page. This counter is what makes "the row
     * went and the page did not" a measurement rather than a belief.
     */
    uint32_t dropped_rows;
    uint32_t redraws;
    uint32_t setters;
    /** Rows whose property set was walked at all. An unchanged hash must not
     *  reach a single per-field compare. */
    uint32_t row_applies;
    uint32_t actions;
    uint32_t paints;
};
void Porcelain_PanelCountersRead(struct Porcelain* porcelain,
                                 struct PorcelainPanelCounters* out);
/* Frames                                                                   */
/* ------------------------------------------------------------------------ */

/*
 * @see ToriRS_PorcelainApi's frames block for the contracts. The split is the
 * one the two shipped providers already live with: the OFFERS are static data
 * in ToriRS_PluginDef.frames, and what a provider writes by hand is the
 * description of each one and the reconcile that takes it back off.
 */
void Porcelain_Frame(struct Porcelain* porcelain, char const* offer_id, int canvas,
                     int min_width, int min_height, PorcelainDescribeFn fn, void* user);
int Porcelain_FrameEvent(struct Porcelain* porcelain,
                         struct ToriRS_GameframeEvent const* event);
bool Porcelain_Usable(struct Porcelain* porcelain, struct ToriRS_WidgetBounds* out);
bool Porcelain_NativeSize(struct Porcelain* porcelain, struct PorcelainElement element,
                          struct ToriRS_WidgetBounds* out);
int Porcelain_LaneIcon(struct Porcelain* porcelain, struct PorcelainElement tab);
int Porcelain_TabGroupCount(struct Porcelain* porcelain, int axis);
int Porcelain_TabGroup(struct Porcelain* porcelain, int axis, int group,
                       struct PorcelainElement* out, int capacity);
struct PorcelainElement Porcelain_TabDetached(struct Porcelain* porcelain);

/**
 * The tab names the portable vocabulary knows, NULL-terminated.
 *
 * A plugin walking the sidebar needs the names before it can ask anything
 * about them, and the alternative -- every plugin spelling its own fourteen --
 * is the remap table this layer exists to delete. Which of them this lane HAS
 * is still a question, answered by Porcelain_Element(TAB(name)).
 */
char const* const* Porcelain_TabNames(void);

/* ------------------------------------------------------------------------ */
/* Not implemented yet                                                      */
/* ------------------------------------------------------------------------ */

/*
 * These are specified and NOT built. Each names the plan section that carries
 * its contract, so the port that needs one knows where to read rather than
 * inventing a second shape for it.
 *
 * TODO(plan #api "Frames"): the SCROLLBAR element the gaps table proposes.
 *   frame_skin_scrollbar is a declared no-op and six shipped assets are dead
 *   because the chat and sidebar scrollbars are painted by the client's emit
 *   path rather than by widget nodes; the verb has no engine half to stand
 *   on until UITree_ScrollbarSkin exists. The rest of the Frames block is
 *   built -- @see the frames section above.
 *
 * TODO(plan #api "Panels"): what the row model still cannot express.
 *   A ROW cannot be scrolled to. The page's scroll is readable and writable
 *   (@see Porcelain_PanelScroll), but turning a key into a pixel offset needs
 *   the presenter to answer where a row sits, and no verb asks.
 *
 *   The three that were here are built: the secondary-click channel is
 *   TORIRS_PANEL_ACTION_MENU plus PorcelainRow::on_menu, the page's scroll is
 *   the two verbs above, and `label` is a patched property on all four kinds
 *   that carry one rather than declaration identity -- so the row model's
 *   identity is now (key, kind) and nothing else, and the only rebuild left
 *   is a changed row SET.
 *
 * TODO(plan #api "Data helpers"): Porcelain_MenuSubjects -- the distinct
 *   subjects of a menu build, yielded once each with a resolved snapshot.
 *   The other six data helpers are built; this one needs an engine half that
 *   resolves an npc or loc snapshot from a menu row, which does not exist.
 *
 * TODO(plan #gaps-absorbed): Porcelain_ElementInkEdge, Porcelain_ControlHover,
 *   Porcelain_TextSize, Porcelain_SkillIcon, Porcelain_SkillCount,
 *   Porcelain_MenuSubjects, Porcelain_DerivedRect. Every one of them is a
 *   proposed verb whose engine half does not exist yet.
 *
 * TODO(plan #host "Facets in the adapter"): PorcelainElementState::facets is
 *   plumbed end to end and reads ZERO from every adapter today. Nothing here
 *   derives a facet: the derivation belongs to the lane adapter, keyed on a
 *   profile fact, and a lane-shaped branch in this library would be the
 *   defect the element table exists to prevent.
 */

/* ------------------------------------------------------------------------ */
/* Test seams                                                               */
/* ------------------------------------------------------------------------ */

/**
 * Engine calls and allocations this handle has made since the counters were
 * last reset. The steady-state claim ("an unchanged description makes zero
 * engine calls, zero allocations") is only a claim until a test reads these.
 */
struct PorcelainCounters
{
    uint32_t engine_calls;
    uint32_t allocations;
    uint32_t describe_runs;
    uint32_t creates;
    uint32_t removes;
    uint32_t setters;
    uint32_t revalidates;
    /* Items whose property set was walked at all. The plan's rule is that an
     * unchanged HASH makes no engine call AT ALL -- one hash compare per key
     * and nothing else. Without this counter that rule is indistinguishable
     * from "every per-field compare happened to match", which is a second
     * line of defence and not the rule. */
    uint32_t property_applies;
    /** Controls re-made because the node they belonged under changed. The
     *  engine has no re-parent verb, so this is a remove plus a create; it
     *  must stay at zero on a settled tree, and a number that climbs every
     *  frame is a target that is remounting, not a bug in this counter. */
    uint32_t reparents;
    /**
     * Controls re-made because the control ITSELF was destroyed, with the
     * node it hung under surviving.
     *
     * Distinct from `reparents`, and the distinction is the bug: a `layout`
     * verb can rebuild everything under a frame root that is still the same
     * node, so the parent compare says nothing moved while the control is
     * already freed. The layer then wrote to a dead handle and reported a
     * refusal the plugin could neither prevent nor declare. Zero on a settled
     * tree; one per owned control per root rebuild.
     */
    uint32_t recreates;
};
void Porcelain_CountersRead(struct Porcelain* porcelain, struct PorcelainCounters* out);
void Porcelain_CountersReset(struct Porcelain* porcelain);

/**
 * Drop every handle and every claim. A process-wide reset for a test that
 * runs many independent scenarios in one binary; never called by a plugin.
 */
void Porcelain_ResetForTesting(void);

/** Which handle owns `aspect` of `element`, or NULL. Inspectable by rule. */
char const* Porcelain_ClaimOwner(struct PorcelainElement element, enum PorcelainAspect aspect);

#ifdef __cplusplus
}
#endif

#endif /* TORIRS_PORCELAIN_H */
