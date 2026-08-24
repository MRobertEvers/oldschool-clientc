#ifndef CS2VM2_HOST_H
#define CS2VM2_HOST_H

#include "cs2_opcode.h"

#include <stdbool.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

enum CS2VM_ModelKind
{
    CS2VM_MODEL_KIND_NONE = 0,
    CS2VM_MODEL_KIND_PLAIN = 1,
    CS2VM_MODEL_KIND_NPC_HEAD = 2,
    CS2VM_MODEL_KIND_PLAYER_HEAD = 3,
    CS2VM_MODEL_KIND_PLAYER_SELF = 5,
    CS2VM_MODEL_KIND_PLAYER_CHATHEAD = 6,
};

/**
 * The CS2 `windowmode` domain — the values `^windowmode_fixed` and
 * `^windowmode_resizable` compile to, and the values GETWINDOWMODE /
 * SETWINDOWMODE and their `default` siblings speak.
 *
 * This is opcode surface, not content: the numbers belong to the CS2 dialect
 * the client implements, alongside CS2_OP_* and `clienttype`, and no cache
 * record or content pack states them. The authority is the dialect's own type
 * table (runestar cs2 `windowmode-names.tsv`: 1 fixed, 2 resizable).
 */
enum CS2VM_WindowMode
{
    CS2VM_WINDOW_MODE_FIXED = 1,
    CS2VM_WINDOW_MODE_RESIZABLE = 2,
};

/**
 * The same table's *names*, for the places a human states a mode (the boot
 * manifest's `[ui:boot] windowmode`, the `--windowmode` flag). Returns 0 for
 * anything else, which every caller reads as "unset, keep the default".
 *
 * Here rather than in the manifest parser so the spelling stays next to the
 * numbering it belongs to: both halves are the dialect's, not this client's.
 */
static inline int
CS2VM_WindowModeFromName(char const* name)
{
    assert(name);
    if( strcmp(name, "fixed") == 0 )
        return CS2VM_WINDOW_MODE_FIXED;
    if( strcmp(name, "resizable") == 0 )
        return CS2VM_WINDOW_MODE_RESIZABLE;
    return 0;
}

/** The inverse, for log lines. Never NULL. */
static inline char const*
CS2VM_WindowModeName(int mode)
{
    if( mode == CS2VM_WINDOW_MODE_FIXED )
        return "fixed";
    if( mode == CS2VM_WINDOW_MODE_RESIZABLE )
        return "resizable";
    return "unset";
}

enum CS2VM_HostRequestKind
{
    /*
     * Host-request discriminators are the originating CS2 opcodes. Every row
     * also generates its own exact request struct and union arm, in numeric
     * opcode order. Each row carries that request's direct field declarations.
     */
#define CS2VM_HOST_REQUEST_KIND(name, opcode, fields) \
    CS2VM_HOST_REQUEST_##name = CS2_OP_##name,
#include "cs2vm2_host_request_kinds.def"
#undef CS2VM_HOST_REQUEST_KIND
};

/** Friends / ignore accessors + mutators (3600..3609, 3621..3623).
 *  `index` is set for the indexed getters, `name` (borrowed, never owned) for
 *  the ones that take a username. */

/** RESUME_COUNTDIALOG (3104). `text` is BORROWED from the VM's string pool —
 *  the handler must copy it, never free it. */

/** IF_RESUME_PAUSEBUTTON / CC_RESUME_PAUSEBUTTON. Component that the paused
 *  server script armed with if_addresumebutton. */

/** Chat filter get/set and the two sends (5000/5001/5005/5008/5009/5016).
 *  `name` is borrowed for CHAT_SENDPRIVATE, `text` for either send. */

enum CS2VM_OC_IntField
{
    CS2VM_OC_INT_COST,
    CS2VM_OC_INT_STACKABLE,
    CS2VM_OC_INT_MEMBERS,
    CS2VM_OC_INT_ID,
};

/**
 * POP_VAR / POP_VARBIT: a script-side write to the client's var state.
 *
 * Deliberately a *separate* request from the server's VARP_SMALL/LARGE/SYNC
 * path: the reference applies these optimistically to `Varps_main` only and
 * never pushes the id into its changed-varp ring, so a widget hook that writes
 * a var does not re-trigger itself. See VarPManager_ServerUpdateFn's header.
 */

/** KEYHELD / KEYPRESSED: key_code is an OSRS internal code, not ASCII. */

#define CS2VM_OPKEY_PAIR_MAX 5
/** The SETOPTKEY ("typed key") opcode variants target this op slot implicitly. */
#define CS2VM_OPKEY_TYPED_SLOT 10

/** CC/IF_SETOPKEY and the OPT variants. op_index is 1..10 (10 = typed key);
 *  pair_count == 0 clears the slot. */

/** CC/IF_SETOPKEYRATE and SETOPKEYIGNOREHELD carry the same rate fields in
 * their distinct exact payloads. */

enum CS2VM_DbLoadKind
{
    CS2VM_DB_LOAD_NONE = 0,
    CS2VM_DB_LOAD_ROW = 1,
    CS2VM_DB_LOAD_INDEX = 2,
    CS2VM_DB_LOAD_TABLE = 3,
};

/** CC_COPY: clone dynamic child src_sub_id of parent_id into dst_sub_id. */

/* Hook string args ('s'/'W'/'X' signature chars). str_arg_mask bit i marks
 * signature position i as a string; strings fill str_args[] in position order
 * (k-th set bit -> str_args[k]). int_args[i] is unused at string positions. */
/* The generic inventory-grid builders install repaint hooks carrying five
 * (script 149) or nine (script 150) operation labels. */
#define CS2VM_SETON_STR_ARG_MAX 16
/*
 * 256, not 80. A hook's string argument is frequently a whole line of UI copy —
 * `if_setonmouserepeat("tooltip_mouserepeat(…, ~prayer_gettooltiptext($obj1),
 * …)")` hands over the entire tooltip — and the copy into this struct is a
 * `strncpy` that truncates in silence. At 80 the Ancient Curses descriptions
 * lost their tails one character past "opponent's P", and because the tooltip
 * box is then sized from the string it was given, the result read as a layout
 * bug rather than as a lost argument. OSRS's own longest is 152 characters
 * (Deflect Summoning); 256 clears every string any cache in this tree arms.
 */
#define CS2VM_SETON_STR_ARG_LEN 256

/*
 * How many arguments a hook registration can carry.
 *
 * This is a cache fact, not a taste: the widest `if_seton*` signature in
 * cache.osrs239 is 44 arguments (`script3040` in the side journal's onload).
 * The cap used to be 32, which silently dropped everything past position 31 —
 * and the dropped tail is where the panel-shaped arguments live, so
 * `~side_journal_switchtab`'s `tab_line` component arrived as 0, its
 * `cc_create` no-op'd, and the `cc_setsize` that followed landed on whatever
 * the active component still was: the interface *root*. A 190x261 sidebar
 * panel resolved to 500x1 and the whole quest tab drew off-screen.
 *
 * 64 leaves headroom over the 44 this cache needs. str_arg_mask is one bit per
 * position, so it has to widen with it.
 */
#define CS2VM_SETON_INT_ARG_MAX 64

/** IF_CALLONRESIZE — the component whose on-resize listener to run. */

/** CC_TRIGGEROP — the dot/active component whose on_op listener to run, and
 *  the op index to report through it (event_opindex). */
/**
 * Payload fields used by the four distinct sound opcode requests.
 *
 * Which fields are meaningful depends on `kind`: SYNTH uses id/loops/delay,
 * JINGLE uses id/delay, SONG uses id and the four fade fields, and
 * SONG_WITHSECONDARY adds secondary_id. Fades arrive in client cycles, as the
 * scripts and the wire both carry them; the conversion to milliseconds happens
 * where the player is called, so both entry points convert the same way.
 */

/** IF_TRIGGEROPLOCAL — component to click and the sub-id that becomes
 *  last_slot (quest id when childIndex was -1 and the signature carried "i"). */

/** CC/IF_SETTARGETVERB: the selection verb used when WidgetFlags' target mask
 * is nonzero (for example an object-backed backpack cell's generic action). */

/** IF/CC_DRAGPICKUP — start (or re-target) a component drag with an explicit
 *  grab offset inside the widget. Reference Client.dragTryPickup. */

/** CC_GETOP / IF_GETOP: one-based operation slot on the resolved component. */

/**
 * CC_SETCOMPONENTPARAM's `kind` argument: which stack the value arrived on.
 *
 * It tracks the ParamType's own type — script 9581 writes param 1017 (declared
 * `s` in cache.osrs239) with kind 2 and the value on the string stack, and every
 * other write in the cache is an int param with kind 0. Only these two values
 * are known to occur, so anything that is not STRING is read as an int.
 */
#define CS2_CC_COMPONENTPARAM_KIND_INT 0
#define CS2_CC_COMPONENTPARAM_KIND_STRING 2

/** NC_PARAM (6513) / LC_PARAM (6514): the same shape as OC_PARAM over an npc
 *  or a loc type. `type_id` is the record; the answer is its param, or the
 *  ParamType's default when the record does not carry that key. */

/** OC_OP/OC_IOP payload fields. `opcode` records ground vs inventory;
 *  `op_index` is the normalized zero-based menu slot (0..4). */

/** OC_FIND/OC_FINDNEXT/OC_FINDRESET payload fields. `opcode` records which of
 *  the three fired. `query` is OC_FIND's popped search string (NULL for the
 *  other two); it is *borrowed* — still owned by the VM handler, which keeps it
 *  on the stack across a load yield and frees it on completion. */

/** OC_WEARPOS/WEARPOS2/WEARPOS3 payload fields. `opcode` records which slot is
 *  being asked about. */

/** oc_isubop(obj, opIndex, subIndex) -> string. */

enum CS2VM_WidgetIntField
{
    CS2VM_WIDGET_INT_HFLIP,
    CS2VM_WIDGET_INT_VFLIP,
    CS2VM_WIDGET_INT_ANGLE_2D,
    CS2VM_WIDGET_INT_FILL_COLOUR,
    CS2VM_WIDGET_INT_LINE_WIDTH,
    CS2VM_WIDGET_INT_LINE_DIRECTION,
    CS2VM_WIDGET_INT_FILL_MODE,
    CS2VM_WIDGET_INT_TRANS_BOT,
    CS2VM_WIDGET_INT_NO_SCROLL_THROUGH,
    CS2VM_WIDGET_INT_NO_CLICK_THROUGH,
    CS2VM_WIDGET_INT_PINCH,
    CS2VM_WIDGET_INT_CLICKMASK,
    CS2VM_WIDGET_INT_FORCE_LEFT_CLICK,
    CS2VM_WIDGET_INT_DRAG_DEAD_ZONE,
    CS2VM_WIDGET_INT_DRAG_DEAD_TIME,
    CS2VM_WIDGET_INT_MODEL_ANIM,
    CS2VM_WIDGET_INT_MODEL_ORTHOG,
    CS2VM_WIDGET_INT_MODEL_TRANSPARENT,
    CS2VM_WIDGET_INT_RESUME_PAUSEBUTTON,
};

/** Any 6600..6640 opcode. `arg0`/`arg1` hold its popped int args in push
 *  order (arg0 pushed first), unused ones left at 0. */

/** Any 6693..6699 map element config opcode. */

/** STAT / STAT_BASE / STAT_XP. `stat` is the protocol skill index. */

/** CAM_SETFOLLOWHEIGHT payload; the getter carries no args. */

/** CAM_FORCEANGLE (5504): snap the orbit camera. `angle_x` is the pitch in the
 *  script's 128..383 units, `angle_y` the yaw in 0..2047 — the same units
 *  CAM_GETANGLE_XA/YA read back. */

/** Any HIGHLIGHT_* opcode. `args` holds its popped int args in push order
 *  (args[0] pushed first); the widest variant is any group's SETUP, which pops
 *  5. `query` marks the GET variants, which must push a bool result. The one
 *  string argument in the family -- the PLAYER group's name -- is popped and
 *  dropped by the VM rather than carried here: no host state keys on it. */
#define CS2VM_HIGHLIGHT_ARG_MAX 5

/**
 * LOC_FIND (6803) and COORD_INSCENE (6951): the two "is this still there"
 * gates every static-overlay script opens with.
 *
 * LOC_FIND also has a side effect -- it makes the loc it found the ACTIVE LOC,
 * which is what the OVERLAY_LOC_* ops and the `_6800/_6801/_6802` getters then
 * answer about. `loc_type` is unused by COORD_INSCENE.
 */

/** Widest scripted-entity-overlay op: OVERLAY_COORD_CREATE takes six ints. */
#define CS2VM_OVERLAY_ARG_MAX 6

/**
 * Any scripted-entity-overlay opcode: the 7200..7214 family plus the four that
 * address an overlay's layer where the panel forms address a component id
 * (OVERLAY_FIND / OVERLAY_CC_FIND / OVERLAY_CC_CREATE / OVERLAY_CC_DELETEALL).
 *
 * Shaped like the highlight request for the same reason: the arity comes from
 * the generated table, so the VM half never has to be edited again when a
 * member of the family is implemented. `dot_operand` carries the `.` form for
 * the ops that set the active component. See game/rs_entity_overlay.h.
 */

/** Any MINIMENU_* opcode (7100..7110); they take no args, so only the opcode
 *  distinguishes them. The host pushes the result. */

/** Any volume / *OPTION_* opcode (3203..3217). `option_id` is the option key for
 *  the CLIENT/GAME/DEVICE families (0 for the direct volume ops); `value` is the
 *  SET payload (unused by the getters). The host pushes getter results. */

/** Any minimap zoom opcode (7250..7254). `value` is the setters' popped arg
 *  (unused by GETZOOM, which the host answers). */

/** Any local-notification opcode (3170..3173). Only LOCAL_NOTIFICATION fills the
 *  payload; CANCEL uses `id` alone, and CANCELALL/SUPPORTED carry nothing. The
 *  strings are borrowed for the duration of the call — the op frees them. */

/** Any VIEWPORT_* opcode (6200..6205). `args` holds the popped ints in push
 *  order (SETFOV/SETZOOM pop 2, CLAMPFOV pops 4, the GETs pop none). */
#define CS2VM_VIEWPORT_ARG_MAX 4

/** Any UIZOOM_* opcode (6210..6214). `value` is UIZOOM_SET's popped arg (unused
 *  by the others). */

/** Any SAFEAREA_* opcode (6220..6223, 6231); no args to carry. */

/** Any CLIENTOP_* opcode (6700..6709). SET carries slot + script_id + label;
 *  DEL carries only slot (`is_set` false, label NULL, script_id unused). */

/** A client-op context getter. The opcode is the whole request: these take no
 *  arguments and the host pushes the answer. */

/** 6902..6905. `index` is ACTIVEPLAYER_GETROUTECOORD's route index and -1 for
 *  the three forms that pop nothing. */

/** Any loot-tracker opcode (7400-family + 7600-family). `name` is borrowed
 *  from the VM's string pool for the ops that pop a string; the host must
 *  copy it if kept. `int_args` hold remaining int arguments in pop order. */

/** Any hiscores opcode (7809/7811). Only the opcode distinguishes them. */

/** SETWINDOWMODE / SETDEFAULTWINDOWMODE payload: one enum CS2VM_WindowMode. */

/*
 * Every hosted opcode has its own public struct and union arm. The final
 * manifest argument is a parenthesized declaration list expanded directly
 * into that opcode's named request struct; no shared public request type
 * exists.
 */
#define CS2VM_HOST_REQUEST_FIELDS(...) __VA_ARGS__
#define CS2VM_HOST_REQUEST_KIND(name, opcode, fields) \
    struct CS2VM_HostRequest_##name                  \
    {                                                 \
        CS2VM_HOST_REQUEST_FIELDS fields              \
    };
#include "cs2vm2_host_request_kinds.def"
#undef CS2VM_HOST_REQUEST_KIND
#undef CS2VM_HOST_REQUEST_FIELDS

struct CS2VM_HostRequest
{
    enum CS2VM_HostRequestKind kind;

    union
    {
#define CS2VM_HOST_REQUEST_KIND(name, opcode, fields) \
    struct CS2VM_HostRequest_##name name;
#include "cs2vm2_host_request_kinds.def"
#undef CS2VM_HOST_REQUEST_KIND
    } u;
};

struct CS2VM2_Thread;

typedef int (*CS2VM2_HostExec_Fn)(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request);

#endif /* CS2VM2_HOST_H */
