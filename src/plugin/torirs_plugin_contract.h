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
/*
 * on_script_pre / on_script_post (RuneLite's ScriptPreFired /
 * ScriptPostFired) for a script id the plugin watches (scripts.watch).
 * `ref` reads and writes the event's values through the same counts /
 * get_int / set_int / get_string / set_string as a script callback, index 0
 * the TOP: at PRE the script's arguments (index 0 its LAST int / string
 * argument -- where RuneLite's getIntStack()[size-1] finds it, for an invoked
 * script and a top-level one alike); at POST the values it returned. Writes
 * at PRE change what the script receives, at POST what its caller sees.
 * `invoked` is true for a gosub callee (RuneLite's null getScriptEvent);
 * `widget` is the source widget of a top-level listener script when the VM
 * names one.
 */
struct ToriRS_ScriptHookEvent
{
    int32_t script_id;
    bool invoked;
    struct ToriRS_ScriptRef ref;
    struct ToriRS_WidgetRef widget;
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

/** What kind of identity ToriRS_WidgetModel::id names, or that the
 *  component carries none. A player portrait (IF_SETPLAYERHEAD) is always
 *  the local player and has no id to check, so it reads NONE too -- the
 *  same answer as a component nothing has bound a head/model to yet. */
enum ToriRS_WidgetModelKind
{
    TORIRS_WIDGET_MODEL_NONE = 0,
    TORIRS_WIDGET_MODEL_NPC,
    TORIRS_WIDGET_MODEL_OBJ,
    TORIRS_WIDGET_MODEL_MODEL,
    /* set_model only: the local player's chathead. Never answered by a
     * model read (see above). */
    TORIRS_WIDGET_MODEL_PLAYER,
};

/**
 * What identity a component was told to show -- the RAW id the cache
 * protocol sent on IF_SETNPCHEAD / IF_SETOBJECT / IF_SETMODEL, before any
 * composite scene model is built from it. A follower asking "which npc is
 * this chathead" wants this, never a scene/model id decoded back out of a
 * graphic token or a rendered mesh.
 */
struct ToriRS_WidgetModel
{
    enum ToriRS_WidgetModelKind kind;
    int32_t id;
};

/*
 * widget:detail() -- RuneLite's Widget getters in ONE read, for a port that
 * reads a game widget the way RuneLite code does (getText, getItemId,
 * getSpriteId, getActions, getOriginalX...). Set `struct_size` before the call.
 *
 * Identity follows RuneLite, not the tree: a dynamic (cc_create) child answers
 * its PARENT's component id with its own `index`; a static component answers
 * index -1. `type` is RuneLite's WidgetType (0 layer, 2 inventory, 3
 * rectangle, 4 text, 5 graphic, 6 model, 7 text inventory, 9 line), -1 for a
 * client-built node RuneLite has no type for (the minimap, a sidebar, a
 * plugin's own text). A field the node does not carry reads RuneLite's
 * default: item -1 / quantity 0, sprite -1, model -1, animation -1, font -1,
 * text "". `sprite_id` is the CACHE sprite the node shows (-1 while the node
 * shows an obj icon or no art, or when the art is not a cache sprite);
 * `font_id` the cache font, -1 for a client chrome font. `text` and
 * `text_color` are the node's OWN values: a retained set_native_text edit is
 * drawn but not answered here (the caller wrote it and knows it).
 *
 * Geometry: x, y, width, height are the drawn CANVAS box (getCanvasLocation +
 * getWidth/getHeight); relative_x/y the laid-out position in the parent;
 * original_* the authored inputs before the size and position modes
 * (getOriginalX..., the values cc_setposition/cc_setsize wrote). `opacity` is
 * RuneLite's: transparency, 0 opaque .. 255 invisible.
 *
 * `hidden` is RuneLite's isHidden (this node or an ancestor does not show);
 * `self_hidden` the node's own hide bit (isSelfHidden). `parent_id` the
 * component id of the parent node RuneLite would answer (getParentId): for a
 * dynamic child the component it hangs from, for a mounted interface's root
 * the mount container; -1 at the top.
 */
/** Longest string set_native_text takes, in bytes. */
#define TORIRS_WIDGET_NATIVE_TEXT_MAX 4096
#define TORIRS_WIDGET_DETAIL_TEXT_MAX 512
#define TORIRS_WIDGET_DETAIL_NAME_MAX 128
#define TORIRS_WIDGET_DETAIL_ACTIONS 10
#define TORIRS_WIDGET_DETAIL_ACTION_MAX 64
struct ToriRS_WidgetDetail
{
    uint32_t struct_size;
    int32_t id, index, type, content_type;
    int32_t item_id, item_quantity, sprite_id, model_id, model_type, animation_id;
    int32_t text_color, font_id;
    bool hidden, self_hidden;
    int32_t x, y, width, height;
    int32_t original_x, original_y, original_width, original_height;
    int32_t relative_x, relative_y;
    int32_t scroll_x, scroll_y, scroll_width, scroll_height;
    int32_t opacity, parent_id;
    /** Highest op slot with a label, plus one: actions[0 .. action_count-1]
     *  is RuneLite's getActions() (an empty slot is ""). 0: none. */
    int32_t action_count;
    /** Truncated to the buffer; never markup-stripped. */
    char text[TORIRS_WIDGET_DETAIL_TEXT_MAX];
    /** The op base (getName): what the menu calls the component. */
    char name[TORIRS_WIDGET_DETAIL_NAME_MAX];
    char actions[TORIRS_WIDGET_DETAIL_ACTIONS][TORIRS_WIDGET_DETAIL_ACTION_MAX];
};

/*
 * One argument of a NATIVE component's CS2 listener (widget:listener), in
 * RuneLite's Object[] order: element 0 is the script id, the rest the
 * arguments as the script was armed with them -- the magic ScriptEvent ints
 * (-2147483645 for the widget id and so on) kept as they are.
 */
#define TORIRS_WIDGET_LISTENER_ARGS_MAX 65
#define TORIRS_WIDGET_LISTENER_STRING_MAX 80
struct ToriRS_WidgetListenerArg
{
    bool is_string;
    int32_t int_value;
    char string_value[TORIRS_WIDGET_LISTENER_STRING_MAX];
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
/* scripts.run: one argument, an int or a string, in the script's own order. */
#define TORIRS_SCRIPT_RUN_ARGS_MAX 32
#define TORIRS_SCRIPT_RUN_INTS_MAX 64
#define TORIRS_SCRIPT_RUN_STRINGS_MAX 16
#define TORIRS_SCRIPT_RUN_STRING_LEN 512
struct ToriRS_ScriptArg
{
    bool is_string;
    int32_t int_value;
    /** Borrowed for the call; NULL reads as "". */
    char const* string_value;
};
/** What a script left on the VM's stacks when it returned (getIntStack /
 *  getStringStack), bottom of the stack first. */
struct ToriRS_ScriptResult
{
    size_t int_count;
    int32_t ints[TORIRS_SCRIPT_RUN_INTS_MAX];
    size_t string_count;
    char strings[TORIRS_SCRIPT_RUN_STRINGS_MAX][TORIRS_SCRIPT_RUN_STRING_LEN];
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
    /*
     * Run one clientscript to completion NOW, on the client's own VM and host
     * (Client.runScript), and hand back what it returned.
     *
     * An event context only (never a draw callback), and only with the VM
     * idle: inside on_script_callback, or while any script is executing, it
     * is WRONG_CONTEXT, because the reference asserts the same. Ints fill the
     * script's int arguments and strings its string arguments, each in order;
     * more of either than the script declares is INVALID_ARGUMENT.
     *
     * A synchronous run cannot wait for the cache. When the script, or
     * anything it reaches (a gosub, an enum, a db row, ...), is not resident
     * the run is abandoned at that opcode, the load is queued, and the answer
     * is PENDING: ask again on a later frame. A load that ran and found
     * nothing answers that lookup's miss on the next run, as Task_CS2Run's own
     * resume does. NATIVE_BLOCKED for a script that awaits something only an
     * interface mount can supply (a component pack, a model to bake); FAILED
     * for a script error; BUDGET_EXCEEDED when it left more on a stack than a
     * ToriRS_ScriptResult holds; UNAVAILABLE on a client with no CS2 VM.
     * RUNELITE_CALLBACK opcodes inside the run raise no plugin callback.
     */
    enum ToriRS_ContractResult (*run)(void*,int32_t script_id,struct ToriRS_ScriptArg const* args,size_t arg_count,
                                      struct ToriRS_ScriptResult* out);
    /*
     * Watch (or stop watching) one clientscript id for on_script_pre /
     * on_script_post. Unwatched ids cost the VM nothing. At most
     * TORIRS_SCRIPT_WATCH_MAX ids per plugin (BUDGET_EXCEEDED past it); an id
     * outside 0..65535 is INVALID_ARGUMENT; restating a watch is OK. A
     * stopped plugin's watches go with it. UNAVAILABLE on a client with no
     * CS2 VM. Not from a draw callback.
     */
    enum ToriRS_ContractResult (*watch)(void*,int32_t script_id,bool on);
};
#define TORIRS_SCRIPT_WATCH_MAX 32
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
    /* Owned controls only: the control's box at CANVAS (x, y), under whatever
     * parent it was created in. The parent decides where it draws in the tree's
     * order -- inside that parent's subtree, under whatever the tree lists after
     * it -- and this decides where it draws on screen; the parent moving does
     * not move it. Retained until set_position states a parent-local position.
     * A scrolling ancestor still scrolls it. Native widgets: NATIVE_BLOCKED. */
    enum ToriRS_ContractResult (*set_canvas_position)(void*, struct ToriRS_WidgetRef, int32_t x, int32_t y);
    /* Owned controls only: move the control to directly after `sibling` among
     * their shared parent's children -- its place in the tree's draw order.
     * A creation appends, which on a flat frame is after the minimenu. A
     * sibling under another parent is INVALID_ARGUMENT; a dead one is
     * STALE_REFERENCE; restating the current order changes nothing. */
    enum ToriRS_ContractResult (*move_after)(void*, struct ToriRS_WidgetRef, struct ToriRS_WidgetRef sibling);
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

    /* RuneLite's Widget getters in one read; see ToriRS_WidgetDetail. Set
     * `out->struct_size` first. A read: any widget, any owner, any context. */
    enum ToriRS_ContractResult (*detail)(void*, struct ToriRS_WidgetRef, struct ToriRS_WidgetDetail* out);
    /* The three child lists RuneLite separates, each in draw order and each
     * shaped like `children` (count past capacity is BUDGET_EXCEEDED):
     * dynamic = the cc_create children (getDynamicChildren), static = the
     * interface's own components under this one (getStaticChildren), nested =
     * the root(s) of an interface mounted IN this component
     * (getNestedChildren). A plugin's own owned controls are in none. */
    enum ToriRS_ContractResult (*dynamic_children)(void*, struct ToriRS_WidgetRef,
                                                 struct ToriRS_WidgetRef*, size_t capacity, size_t* count);
    enum ToriRS_ContractResult (*static_children)(void*, struct ToriRS_WidgetRef,
                                                struct ToriRS_WidgetRef*, size_t capacity, size_t* count);
    enum ToriRS_ContractResult (*nested_children)(void*, struct ToriRS_WidgetRef,
                                                struct ToriRS_WidgetRef*, size_t capacity, size_t* count);
    /* Retained edits on a NATIVE text widget's string and colour (RuneLite's
     * setText / setTextColor on a game widget: dialogue option highlighting).
     * Owner-scoped exactly like set_hidden: the newest owner's edit is drawn,
     * the widget's own text and colour stay as the game set them, and reset /
     * plugin teardown shows them again. The GAME writing a new string (or
     * colour) to the widget drops that edit, RuneLite's order: a script's
     * setText overwrites a plugin's, and the plugin writes again if it still
     * wants to; so does a cc_create rebuild of a dynamic child. The colour replaces the BASE colour
     * (a hover or active colour the cache gives the node still wins while it
     * applies). Text is at most 4096 bytes (BUDGET_EXCEEDED past it). An owned
     * control or a non-text widget is NATIVE_BLOCKED (use set_text). */
    enum ToriRS_ContractResult (*set_native_text)(void*, struct ToriRS_WidgetRef, char const* text);
    enum ToriRS_ContractResult (*set_native_text_color)(void*, struct ToriRS_WidgetRef, uint32_t rgb);
    /* An owned ITEM control (createChild(GRAPHIC) + setItemId): keyed and
     * idempotent like create_image. set_item shows obj `obj_id` with stack
     * `quantity` on it (the client's own obj icon, its count drawn by the
     * cache's stackable rule); obj -1 clears it. PENDING while the obj's model
     * is not resident: the load is queued and the icon appears by itself when
     * it lands (the obj is already on the control for every reader). On a
     * NATIVE item cell (a game graphic/objbox: the bank's slots) set_item is
     * a retained re-lay, owner-scoped like set_native_text: the cell shows
     * the plugin's obj and quantity to every reader (its icon, stack count,
     * menu target, detail) until reset/teardown puts the game's back, or the
     * GAME writes the cell itself (a script's cc_setobject), which drops the
     * edit -- RuneLite's order. Any other native widget is NATIVE_BLOCKED.
     * set_opacity on a native widget is the same kind of retained edit of
     * its transparency. */
    enum ToriRS_ContractResult (*create_item)(void*, struct ToriRS_WidgetRef parent, char const* key, struct ToriRS_WidgetRef* out);
    enum ToriRS_ContractResult (*set_item)(void*, struct ToriRS_WidgetRef, int32_t obj_id, int32_t quantity);
    /* A CACHE sprite (setSpriteId). On an owned image control it is the
     * control's art at the control's size; on a native sprite or graphic it
     * is a retained re-skin exactly like set_image with size 0,0 (reset drops
     * it). A sprite the cache does not have is UNAVAILABLE; one still loading
     * is bound and fills in when it lands. */
    enum ToriRS_ContractResult (*set_sprite)(void*, struct ToriRS_WidgetRef, int32_t sprite_id);
    /* A NATIVE component's CS2 listener as RuneLite's Object[]: `name` in
     * RuneLite's spelling ("onInvTransmit", "onKey", "onOp", "onTimer",
     * "onVarTransmit", "onClick", "onMouseOver", "onMouseLeave",
     * "onMouseRepeat", "onHold", "onRelease", "onDrag", "onDragComplete",
     * "onScrollWheel", "onTargetEnter", "onTargetLeave", "onDialogAbort",
     * "onLoad"). UNAVAILABLE when nothing is armed there -- always for
     * "onLoad", which this client runs at the mount and does not keep. An
     * unknown name is INVALID_ARGUMENT. Read-only. */
    enum ToriRS_ContractResult (*listener)(void*, struct ToriRS_WidgetRef, char const* name,
                                         struct ToriRS_WidgetListenerArg* out, size_t capacity, size_t* count);
    /* An owned RECTANGLE control (createChild(RECTANGLE)): keyed and capped
     * like create_image. set_rect gives it its colour and whether it is
     * filled (an outline is 1 px); its opacity is set_opacity's. */
    enum ToriRS_ContractResult (*create_rect)(void*, struct ToriRS_WidgetRef parent, char const* key, struct ToriRS_WidgetRef* out);
    enum ToriRS_ContractResult (*set_rect)(void*, struct ToriRS_WidgetRef, uint32_t rgb, bool filled);
    /* An owned MODEL control (createChild(MODEL)): draws nothing until
     * set_model binds what it shows -- a cache model id
     * (TORIRS_WIDGET_MODEL_MODEL), an npc's chathead by npc id (NPC), an
     * obj's inventory model (OBJ) or the local player's chathead
     * (TORIRS_WIDGET_MODEL_PLAYER, id ignored). PENDING while the model is
     * not resident: the load is queued and the control binds it by itself
     * when it is composited. set_model_pose is the camera (RuneLite's
     * rotationX/Y/Z in 2048ths of a turn, modelZoom; 0 zoom keeps it) and
     * set_model_anim the sequence it plays (-1 none). Owned only:
     * NATIVE_BLOCKED on a game widget. */
    enum ToriRS_ContractResult (*create_model)(void*, struct ToriRS_WidgetRef parent, char const* key, struct ToriRS_WidgetRef* out);
    enum ToriRS_ContractResult (*set_model)(void*, struct ToriRS_WidgetRef, int32_t kind, int32_t id);
    enum ToriRS_ContractResult (*set_model_pose)(void*, struct ToriRS_WidgetRef, int32_t x_angle, int32_t y_angle,
                                               int32_t z_angle, int32_t zoom);
    enum ToriRS_ContractResult (*set_model_anim)(void*, struct ToriRS_WidgetRef, int32_t sequence);
    /* A NATIVE layer's scroll size (RuneLite's setScrollWidth/Height: the
     * bank tab's laid-out list is taller than the build made it). A plain
     * write, as RuneLite's is: the game's next scroll-size write (the
     * script's own rebuild) replaces it, and nothing restores it on reset.
     * NATIVE_BLOCKED on anything but a native layer. */
    enum ToriRS_ContractResult (*set_scroll_size)(void*, struct ToriRS_WidgetRef, int32_t width, int32_t height);
    /* A NATIVE widget's own hide, written through like set_item's re-lay
     * (RuneLite's setHidden on a game widget: false SHOWS a node the game hid,
     * which set_hidden -- a presentation hide that only ever adds hiding --
     * cannot). reset/teardown restores the game's; the game's own hide write
     * drops the edit. NATIVE_BLOCKED on an owned control. */
    enum ToriRS_ContractResult (*set_native_hidden)(void*, struct ToriRS_WidgetRef, bool hidden);

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
