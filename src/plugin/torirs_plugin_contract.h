#ifndef TORIRS_PLUGIN_CONTRACT_H
#define TORIRS_PLUGIN_CONTRACT_H

/* Major 3: live widgets and native events. The host checks identities and
 * callback lifetimes; plugins never receive UITree pointers or retained indices.
 * This API replaced the earlier declaration/claim contract. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TORIRS_PLUGIN_CONTRACT_MAJOR 3u
#define TORIRS_PLUGIN_CONTRACT_MINOR 0u

struct ToriRS_WidgetRef { uint64_t opaque[3]; };
/* A plugin image handle (see assets.image). Zero is "no image"; the host
 * validates every use against the owning plugin's live resources. */
struct ToriRS_ImageRef
{
    int value;
};
struct ToriRS_ScriptRef { uint64_t token; };
struct ToriRS_ScriptEvent
{
    char const* name; /* borrowed during this callback */
    int32_t script_id;
    struct ToriRS_ScriptRef ref;
    struct ToriRS_WidgetRef widget; /* Optional live widget at the approved hook site. */
};
struct ToriRS_WidgetActionRef
{
    struct ToriRS_WidgetRef widget;
    uint64_t operation;
    uint64_t revision;
};

struct ToriRS_WidgetAction
{
    struct ToriRS_WidgetActionRef ref;
    char label[256]; /* Current native menu label, copied into caller storage. */
};
/* Capacity of an owned control's operation label, including the terminator.
 * The native adapter checks this against its menu option storage. */
#define TORIRS_WIDGET_OP_LABEL_MAX 64
/*
 * How many operations one owned control can carry, numbered 1..N the way the
 * cache numbers a component's -- `op1`, `op2` in an .if, `cc_setop(n, ...)` in
 * a clientscript.
 *
 * A control with ONE op was the whole of this API until the minimap-orbs
 * prayer cover needed the two its target has: `orbs:prayerbutton` declares
 * `op1=*` (Quick-prayers) and `op2=Setup`, and a cover that hides that button
 * and offers a single row takes the quick-prayer setup panel away with it.
 * The adapter's own storage is ten slots wide (UITREE_MENU_OPTION_SLOTS,
 * static-asserted where the two meet), so this is the contract's cap and not
 * the tree's.
 */
#define TORIRS_WIDGET_OP_SLOTS 10

enum ToriRS_ContractResult
{
    TORIRS_CONTRACT_OK,
    TORIRS_CONTRACT_UNAVAILABLE,
    TORIRS_CONTRACT_STALE_REFERENCE,
    TORIRS_CONTRACT_WRONG_CONTEXT,
    TORIRS_CONTRACT_INVALID_ARGUMENT,
    TORIRS_CONTRACT_UNSUPPORTED_LAYOUT,
    TORIRS_CONTRACT_NATIVE_BLOCKED,
    TORIRS_CONTRACT_PENDING,
    TORIRS_CONTRACT_BUDGET_EXCEEDED,
    TORIRS_CONTRACT_FAILED
};

/* Execution contexts are checked by host entry points. Native callbacks may
 * mutate widgets, but cannot recursively enter an already-running script VM.
 * Later/tick-end scheduling is the way out of such a callback. */
enum ToriRS_PluginExecutionContext
{
    TORIRS_PLUGIN_STARTUP,
    TORIRS_PLUGIN_EVENT,
    TORIRS_PLUGIN_SCRIPT_CALLBACK,
    TORIRS_PLUGIN_PAINT,
    TORIRS_PLUGIN_SHUTDOWN
};

enum ToriRS_PluginOperation
{
    TORIRS_PLUGIN_READ_WIDGET,
    TORIRS_PLUGIN_WRITE_WIDGET,
    TORIRS_PLUGIN_RUN_SCRIPT,
    TORIRS_PLUGIN_SCHEDULE,
    TORIRS_PLUGIN_RELEASE_OWNED
};

static inline bool ToriRS_WidgetRefValid(struct ToriRS_WidgetRef ref)
{
    return ref.opaque[0] && ref.opaque[1] && ref.opaque[2];
}
static inline bool ToriRS_WidgetRefEqual(struct ToriRS_WidgetRef a, struct ToriRS_WidgetRef b)
{
    return a.opaque[0] == b.opaque[0] && a.opaque[1] == b.opaque[1] && a.opaque[2] == b.opaque[2];
}
bool ToriRS_PluginContextAllows(enum ToriRS_PluginExecutionContext context,
                               enum ToriRS_PluginOperation operation);

struct ToriRS_PluginRequirement
{
    char const* capability;
    uint32_t version;
    bool optional;
};

struct ToriRS_WidgetBounds { int32_t x, y, width, height; };

/*
 * ToriRS_WidgetState::facets -- what the LANE says about this widget, as
 * opposed to what the tree says about the node.
 *
 * Every other field of the state is geometry or presentation the tree can
 * answer for any node at all. These cannot be: "has the player been given this
 * tab", "is the run orb lit", "has the server taken the minimap away" are
 * facts held in varps, varbits, slot state and a MINIMAP_TOGGLE mode, spelled
 * differently on a dat1 client and a cache one, and a plugin that dresses a
 * stone or an orb needs them on the same fence as the box it is dressing.
 *
 * A bit is set only for a node the facet is ABOUT -- SELECTED is meaningless
 * on the compass -- and a facet this lane cannot derive reads zero. Zero is
 * therefore always "no", never "unknown": a lane whose profile declares no
 * cutscene varbit has no cutscene to hide the HUD for, which is a fact about
 * the revision and not a gap in the answer.
 */
enum ToriRS_WidgetFacet
{
    /** A sidebar tab the lane has GIVEN the player: the panel exists and
     *  nothing has taken it away. A tab this is clear on must not be drawn
     *  with an icon -- the click would open nothing. */
    TORIRS_WIDGET_FACET_GIVEN = 1u << 0,
    /** The sidebar tab currently showing. Exactly one of a frame's tabs. */
    TORIRS_WIDGET_FACET_SELECTED = 1u << 1,
    /** The sidebar tab the game has FLAGGED to flash (a tutorial "look at
     *  your inventory"). The flag, not the blink: the icon's half-second gap
     *  is the frame's to draw, and a facet that pulsed with it would raise a
     *  state change twice a second for every watcher. */
    TORIRS_WIDGET_FACET_FLASHING = 1u << 2,
    /** The minimap or compass surface is permitted to paint at all. Separate
     *  from `presented`, which is whether THIS node draws: the server can
     *  withhold the map while the widget is perfectly visible. */
    TORIRS_WIDGET_FACET_DRAWN = 1u << 3,
    /** The compass rose is live, so north is readable. Reported on the
     *  minimap too: the same MINIMAP_TOGGLE mode governs both. */
    TORIRS_WIDGET_FACET_ORIENTED = 1u << 4,
    /** A click on the minimap walks. Clear in the modes that draw the map and
     *  refuse the step, where a frame must not show a walk cursor. */
    TORIRS_WIDGET_FACET_WALKABLE = 1u << 5,
    /** This orb's toggle is on: run is on, or the special attack is armed. */
    TORIRS_WIDGET_FACET_ACTIVE = 1u << 6,
    /** A cutscene is running and the lane has folded the gameplay HUD away.
     *  Set on every watched widget, because it is a fact about the SCREEN --
     *  a plugin's own decoration has to go with it. */
    TORIRS_WIDGET_FACET_HIDDEN_BY_CUTSCENE = 1u << 7,
};

/*
 * Everything a plugin that REPLACES or DECORATES a native widget has to
 * follow: where the widget is, whether it paints, who hid it, whether it can
 * still be clicked, and whether its art or caption changed underneath.
 *
 * Read it with ToriRS_WidgetApi::state, or take it for granted: the host
 * stamps it once a frame for every bound watch and raises
 * TORIRS_WIDGET_STATE_CHANGED when any field below moves. That is the whole
 * point of the struct -- without it a tab stone or a compass plate has to poll
 * the individual getters every frame to notice a CS2 if_sethide or a move.
 */
struct ToriRS_WidgetState
{
    uint32_t struct_size;
    /** Drawn canvas space, as ToriRS_WidgetApi::bounds answers it. */
    struct ToriRS_WidgetBounds bounds;
    /** Native-parent-local, unscrolled, as ToriRS_WidgetApi::position. */
    struct ToriRS_WidgetBounds local;
    /** Paints this frame: exactly the ToriRS_WidgetApi::visible answer. */
    bool presented;
    /** The node's OWN hide bit -- a CS2 if_sethide or a dat1 IF_SETTAB.
     *  Separate from native_hidden because a plugin that un-hides a stone
     *  needs to know which of the two said no. */
    bool own_hidden;
    /** The engine's native suppression bits, not the script's. */
    bool native_hidden;
    /** Present to the native hit test AS THE LANE LEFT IT: a decoration that
     *  must stay clickable follows this and not `presented`. The plugin
     *  layer's own hiding is not folded in, so a plugin that covers a native
     *  control and keeps its action can still read the control it covers;
     *  a hide the cache or a script authored answers false as it always did. */
    bool input_present;
    /** A CHANGE token for a node that carries art, zero for one that does
     *  not. NEVER an identity: equal tokens mean "the art did not change",
     *  and nothing may be decoded back out of it. */
    uint32_t graphic_token;
    /** This node OR ANYTHING BELOW IT paints a picture this frame -- what a
     *  REPLACE of this widget would consume. Text does not count: a
     *  replacement standing where a caption stood owes the caption nothing.
     *
     *  Distinct from `graphic_token`, which is zero on a container whose art
     *  is on a child and therefore cannot tell a button that paints its own
     *  plate from one whose plate belongs to the strip behind it. That is the
     *  whole reason this field exists: the first leaves a HOLE when it is
     *  replaced and the second leaves nothing, and a replacement that brings
     *  a plate to the second covers the lane's own art with a slab. */
    bool paints_own_art;
    /** FNV-1a 64 of a text node's current string; zero for a non-text node.
     *  A text node with an empty string hashes to the FNV basis, not zero. */
    uint64_t text_hash;
    /** What the LANE says about this widget: a bitmask of
     *  enum ToriRS_WidgetFacet. Zero is "no" and never "unknown". */
    uint32_t facets;
    /** The reference's incarnation, so a state that arrived for a replaced
     *  node can be told from one for the node the plugin still holds. */
    uint64_t incarnation;
};

/* Event payload strings and argument views are borrowed for the callback.
 * Widget references can be retained, but must still be live when next used. */
enum ToriRS_WidgetEventType
{
    /** The mount seam: a widget's interface was opened or closed. NOT RAISED
     *  YET -- the seam is task_slot_mount, and until something raises them a
     *  plugin should watch BOUND and UNBOUND, which carry the same news about
     *  the element it actually asked for. They are declared because the seam
     *  is the one place that can tell an interface CHANGING from a widget
     *  merely being rebound, which BOUND cannot. */
    TORIRS_WIDGET_LOADED,
    TORIRS_WIDGET_CLOSED,
    /** The element moved, was hidden, was re-skinned or retyped. Raised once
     *  per publication fence per watch registered through `watch_state`. */
    TORIRS_WIDGET_STATE_CHANGED,
    TORIRS_WIDGET_OPERATION,
    TORIRS_SCRIPT_CALLBACK,
    TORIRS_WIDGET_BOUND,
    TORIRS_WIDGET_UNBOUND,
    TORIRS_WIDGET_TREE_CHANGED
};

struct ToriRS_WidgetEvent
{
    enum ToriRS_WidgetEventType type;
    struct ToriRS_WidgetRef widget;
    uint64_t native_revision;
    /* Script fields are available only with the CS2 capability. */
    int32_t script_id;
    char const* callback_name;
    /* TORIRS_WIDGET_OPERATION: which of the control's ops was chosen, 1-based
     * and numbered as set_on_op armed them. One for a control with one op. */
    int32_t operation;
    /* Present for a semantic binding subscription. Borrowed for this call. */
    char const* role;
};

struct ToriRS_Api;

enum ToriRS_WidgetRelation
{
    TORIRS_WIDGET_RELATION_NATIVE = 0,
    TORIRS_WIDGET_RELATION_OVER,
    TORIRS_WIDGET_RELATION_BEHIND,
    TORIRS_WIDGET_RELATION_REPLACE,
};
/* Access is valid only during this plugin's synchronous script callback.
 * Stack indices are zero-based from the top. Setters cannot resize stacks. */
struct ToriRS_ScriptApi
{
    void* context;
    bool (*available)(void*);
    /* Coalesced native rebuild at its normal safe point, also valid during
     * shutdown. This schedules no plugin callback or arbitrary script. */
    enum ToriRS_ContractResult (*invalidate)(void*,char const* callback_name);
    enum ToriRS_ContractResult (*counts)(void*,struct ToriRS_ScriptRef,size_t* ints,size_t* strings);
    enum ToriRS_ContractResult (*get_int)(void*,struct ToriRS_ScriptRef,size_t index,int32_t*);
    enum ToriRS_ContractResult (*set_int)(void*,struct ToriRS_ScriptRef,size_t index,int32_t);
    enum ToriRS_ContractResult (*get_string)(void*,struct ToriRS_ScriptRef,size_t index,char*,size_t capacity,size_t* required);
    enum ToriRS_ContractResult (*set_string)(void*,struct ToriRS_ScriptRef,size_t index,char const*);
};
typedef void (*ToriRS_WidgetListener)(struct ToriRS_Api*, void* user,
                                    struct ToriRS_WidgetEvent const* event);

/* Initial production slice: shared widget lookup plus explicitly revision-
 * scoped native lookup, live setters, native layout, owned children/listeners.
 * Further product APIs are added alongside their ports, not via an alternate
 * declarative execution path. Setters are client-thread operations. */
struct ToriRS_WidgetApi
{
    void* context;
    enum ToriRS_ContractResult (*find)(void*, char const* role, struct ToriRS_WidgetRef*);
    /* Every widget carrying `role`, in the ROLE'S OWN NUMBERING: for a role
     * spread over members (sidebar tabs, chat filters, orb-block children)
     * slot m is member m, a member this frame does not have is an invalid
     * reference (ToriRS_WidgetRefValid false) left in its slot, and count is
     * one past the highest member present. A caller placing member m indexes
     * refs[m] directly; a caller iterating skips the invalid slots. A role
     * with no numbering answers its one widget; an adapter role such as
     * ground_item_labels answers its current matches, all valid. Never
     * compacted: closing over a gap would hand tab 9 to the plan for tab 8.
     * With capacity 0, count alone is answered as BUDGET_EXCEEDED. */
    enum ToriRS_ContractResult (*find_all)(void*, char const* role, struct ToriRS_WidgetRef*, size_t capacity, size_t* count);
    enum ToriRS_ContractResult (*get_widget)(void*, int32_t component_id, struct ToriRS_WidgetRef*);
    enum ToriRS_ContractResult (*children)(void*, struct ToriRS_WidgetRef,
                                         struct ToriRS_WidgetRef*, size_t capacity, size_t* count);
    /* Retained actions expire when widget identity, operation state or the
     * native menu row changes. Invoke always checks current native authority. */
    enum ToriRS_ContractResult (*visible)(void*,struct ToriRS_WidgetRef,bool*);
    enum ToriRS_ContractResult (*actions)(void*,struct ToriRS_WidgetRef,struct ToriRS_WidgetAction*,size_t capacity,size_t* count);
    enum ToriRS_ContractResult (*invoke)(void*,struct ToriRS_WidgetActionRef);
    enum ToriRS_ContractResult (*parent)(void*,struct ToriRS_WidgetRef,struct ToriRS_WidgetRef*);
    enum ToriRS_ContractResult (*bounds)(void*, struct ToriRS_WidgetRef, struct ToriRS_WidgetBounds*);
    /* Native-parent-local, unscrolled geometry; bounds is drawn canvas space. */
    enum ToriRS_ContractResult (*position)(void*, struct ToriRS_WidgetRef, struct ToriRS_WidgetBounds*);
    enum ToriRS_ContractResult (*get_text)(void*, struct ToriRS_WidgetRef, char*, size_t capacity, size_t* required);
    /* One read of everything a follower needs; see ToriRS_WidgetState. Set
     * `out->struct_size` before the call. The host stamps the same answer once
     * a frame for every bound watch and raises TORIRS_WIDGET_STATE_CHANGED on
     * a difference, so a plugin that only wants to REACT need never call it. */
    enum ToriRS_ContractResult (*state)(void*, struct ToriRS_WidgetRef, struct ToriRS_WidgetState* out);
    enum ToriRS_ContractResult (*set_position)(void*, struct ToriRS_WidgetRef, int32_t x, int32_t y);
    enum ToriRS_ContractResult (*set_size)(void*, struct ToriRS_WidgetRef, int32_t width, int32_t height);
    enum ToriRS_ContractResult (*set_hidden)(void*, struct ToriRS_WidgetRef, bool hidden);
    enum ToriRS_ContractResult (*set_projection_height)(void*,struct ToriRS_WidgetRef,int32_t height);
    enum ToriRS_ContractResult (*set_text_outline)(void*,struct ToriRS_WidgetRef,bool outline);
    enum ToriRS_ContractResult (*revalidate)(void*, struct ToriRS_WidgetRef);
    enum ToriRS_ContractResult (*reset)(void*, struct ToriRS_WidgetRef);
    /* Keys are scoped to owner and parent; repeated create_text returns the
     * same live child. These content setters/remove apply only to owned text. */
    enum ToriRS_ContractResult (*create_text)(void*, struct ToriRS_WidgetRef parent, char const* key, struct ToriRS_WidgetRef*);
    enum ToriRS_ContractResult (*set_text)(void*, struct ToriRS_WidgetRef, char const*);
    enum ToriRS_ContractResult (*set_text_color)(void*, struct ToriRS_WidgetRef, uint32_t rgb);
    enum ToriRS_ContractResult (*set_text_align)(void*, struct ToriRS_WidgetRef, int horizontal, int vertical);
    enum ToriRS_ContractResult (*remove)(void*, struct ToriRS_WidgetRef);
    /* Owned controls only: arm menu operation `op` (1..TORIRS_WIDGET_OP_SLOTS,
     * numbered as the cache numbers a component's) with this label (shorter
     * than TORIRS_WIDGET_OP_LABEL_MAX). The listener receives
     * TORIRS_WIDGET_OPERATION through the normal native hit test and retained
     * menu checks, with ToriRS_WidgetEvent::operation carrying the op that was
     * chosen -- one listener answers for every op on the control, as one
     * IF_BUTTON handler answers for every op on a component. Op 1 is the
     * left-click default, and the rows read down the menu in op order.
     * Replacing the listener retires earlier retained rows. A NULL listener
     * removes that one operation; the control keeps the others. */
    enum ToriRS_ContractResult (*set_on_op)(void*,struct ToriRS_WidgetRef,int op,char const* label,ToriRS_WidgetListener,void* user);
    /* Owned image controls. create_image returns this owner's keyed child
     * (idempotent like create_text). set_image installs one of this plugin's
     * live images and the control's size in canvas pixels; releasing the image
     * later blanks the control. set_opacity: 255 opaque .. 0 invisible, owned
     * widgets only. */
    enum ToriRS_ContractResult (*create_image)(void*,struct ToriRS_WidgetRef parent,char const* key,struct ToriRS_WidgetRef* out);
    enum ToriRS_ContractResult (*set_image)(void*,struct ToriRS_WidgetRef,struct ToriRS_ImageRef image,int width,int height);
    /* Native re-skin. set_image on a native sprite, graphic or compass with
     * width and height 0 retains this plugin's image as that widget's art while
     * the widget shows a graphic of its own (the widget keeps its geometry; a
     * non-zero size is INVALID_ARGUMENT). set_mask on a native minimap, compass
     * or sprite retains the clip: transparent pixels are the window; an empty
     * image reference removes the native mask. Other widgets: NATIVE_BLOCKED.
     * Releasing the image drops the skin; reset drops this owner's edits. */
    enum ToriRS_ContractResult (*set_mask)(void*,struct ToriRS_WidgetRef,struct ToriRS_ImageRef image);
    enum ToriRS_ContractResult (*set_opacity)(void*,struct ToriRS_WidgetRef,int opacity);
    /* Depth relative to a named widget: drawn and hit directly OVER it, directly
     * BEHIND it, or in its place (REPLACE, which inherits the target's native
     * visibility both ways). Retained per owner on any widget this plugin may
     * edit; the latest writer wins and reset drops it. NATIVE clears this
     * owner's relation and needs no target. Self, ancestor/descendant pairs and
     * cycles are INVALID_ARGUMENT; a dead target is STALE_REFERENCE. */
    enum ToriRS_ContractResult (*set_anchor)(void*,struct ToriRS_WidgetRef,struct ToriRS_WidgetRef target,enum ToriRS_WidgetRelation relation);

    /* Follow a semantic binding at native publication boundaries. A new
     * subscription receives BOUND when available; replacement sends UNBOUND
     * for the old incarnation then BOUND for the new one. Hidden is still
     * bound. NULL listener unregisters this owner's subscription for the role. */
    enum ToriRS_ContractResult (*watch)(void*, char const* role, ToriRS_WidgetListener, void* user);
    /* As `watch`, and additionally raises TORIRS_WIDGET_STATE_CHANGED once per
     * publication fence in which the bound widget's native state (box,
     * presented, own and native hides, input presence, graphic token, text)
     * moved; read it with `state`. Opt-in: a plain `watch` never receives
     * STATE_CHANGED, so a listener written for BOUND/UNBOUND alone keeps its
     * meaning. */
    enum ToriRS_ContractResult (*watch_state)(void*, char const* role, ToriRS_WidgetListener, void* user);
    /* Initial notification and subsequent topology publications. Geometry or
     * hiding alone do not trigger this. Event widget is empty; query live refs.
     * A replacement subscription starts at the next publication fence. */
    enum ToriRS_ContractResult (*watch_tree)(void*, ToriRS_WidgetListener, void* user);

};

#endif
