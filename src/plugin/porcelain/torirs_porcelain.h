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
#define PORCELAIN_EDITS_MAX 32
#define PORCELAIN_WATCHES_MAX 48
#define PORCELAIN_FINDINGS_MAX 32
#define PORCELAIN_EXPECT_MAX 16
#define PORCELAIN_IMAGES_MAX 48
#define PORCELAIN_MODELS_MAX 16
#define PORCELAIN_DERIVED_MAX 16
#define PORCELAIN_TIMERS_MAX 16
#define PORCELAIN_READY_MAX 8
#define PORCELAIN_KEY_EDGES_MAX 4

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
 */
#define PORCELAIN_ABSENT_FENCES 2

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
bool Porcelain_Has(struct Porcelain* porcelain, char const* capability);
bool Porcelain_Require(struct Porcelain* porcelain, char const* capability, char const* feature);

int Porcelain_Tier(struct PorcelainTiers const* tiers, int64_t value);
bool Porcelain_TiersFromConfig(struct Porcelain* porcelain, struct PorcelainTiers* out);
bool Porcelain_ConfigListAdd(struct Porcelain* porcelain, char const* key, char const* item);
uint32_t Porcelain_MenuTag(int subject, int op);
bool Porcelain_Setting(struct Porcelain* porcelain, char const* varbit_name, unsigned flags);
bool Porcelain_KeyEdge(struct Porcelain* porcelain, char const* config_key, PorcelainEdgeFn fn,
                       void* user);
struct ToriRS_ImageRef Porcelain_Image(struct Porcelain* porcelain, char const* name,
                                       enum PorcelainAssetState* out_state);
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
void Porcelain_Tick(struct Porcelain* porcelain, enum PorcelainCadence cadence);

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
void Porcelain_Skin(struct ToriRS_PorcelainDescribe* describe, struct PorcelainElement element,
                    char const* image, char const* mask);
void Porcelain_Opacity(struct ToriRS_PorcelainDescribe* describe, struct PorcelainElement element,
                       int opacity);
void Porcelain_Unsupported(struct ToriRS_PorcelainDescribe* describe, char const* reason);

/* ------------------------------------------------------------------------ */
/* Not implemented yet                                                      */
/* ------------------------------------------------------------------------ */

/*
 * These are specified and NOT built. Each names the plan section that carries
 * its contract, so the port that needs one knows where to read rather than
 * inventing a second shape for it.
 *
 * TODO(plan #api "Frames"): Porcelain_Frame, Porcelain_Usable,
 *   Porcelain_NativeSize, Porcelain_LaneIcon, Porcelain_TabGroupCount,
 *   Porcelain_TabGroup, Porcelain_TabDetached, Porcelain_Unsupported's frame
 *   arm. The USABLE rect is already derived internally (a PRESENTED
 *   LANE_CHROME that spans a full edge, and nothing else), so the verb is a
 *   read-out of work that exists; the rest needs
 *   UITree_FrameSlotMemberNativeBox and the [tabs:<root>] override first.
 *
 * TODO(plan #api "Panels"): Porcelain_Panel, Porcelain_Row,
 *   Porcelain_Reidentify. The row model is its own reconciler over
 *   api->panel, and it depends on the host fixes the plan's "Engine changes"
 *   lists as H1..H5 (BUTTON caption by patch, the dropped same-count rule,
 *   the per-row re-identity verb, the 2048 well ceiling).
 *
 * TODO(plan #api "Data helpers"): Porcelain_Hover (needs the container kind
 *   on the menu build), Porcelain_NativeOverlay (the three-state latch),
 *   Porcelain_DrawContext (needs the engine to set the draw region for
 *   DRAW_WORLD), Porcelain_MenuAdd, Porcelain_Table, Porcelain_Notify.
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
