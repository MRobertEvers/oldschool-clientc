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
     * Host-request discriminators are the originating CS2 opcodes. Keep
     * this list one-to-one and in numeric opcode order: payload layouts may
     * be shared, but request kinds must never collapse opcode identities.
     */
#define CS2VM_HOST_REQUEST_KIND(name, opcode) \
    CS2VM_HOST_REQUEST_##name = CS2_OP_##name,
#include "cs2vm2_host_request_kinds.def"
#undef CS2VM_HOST_REQUEST_KIND
};

/** Friends / ignore accessors + mutators (3600..3609, 3621..3623).
 *  `index` is set for the indexed getters, `name` (borrowed, never owned) for
 *  the ones that take a username. */
struct CS2VM_HostRequest_Social
{
    int opcode;
    int index;
    char* name;
};

/** RESUME_COUNTDIALOG (3104). `text` is BORROWED from the VM's string pool —
 *  the handler must copy it, never free it. */
struct CS2VM_HostRequest_ResumeCountDialog
{
    char* text;
};

/** IF_RESUME_PAUSEBUTTON / CC_RESUME_PAUSEBUTTON. Component that the paused
 *  server script armed with if_addresumebutton. */
struct CS2VM_HostRequest_ResumePauseButton
{
    int component_id;
};

/** Chat filter get/set and the two sends (5000/5001/5005/5008/5009/5016).
 *  `name` is borrowed for CHAT_SENDPRIVATE, `text` for either send. */
struct CS2VM_HostRequest_Chat
{
    int opcode;
    int public_mode;
    int private_mode;
    int trade_mode;
    /** CHAT_GETHISTORYLENGTH / GETHISTORY*_BYTYPEANDLINE: which chat type. */
    int type;
    /** GETHISTORY*_BYTYPEANDLINE: line 0 is the newest message of that type. */
    int line;
    /** GETHISTORY*_BYUID / GETNEXTUID / GETPREVUID: the message handle the
     *  cache's scripts walk the history with. */
    int uid;
    /** CHAT_SETTIMESTAMPS. */
    int timestamps;
    /** CHAT_SENDPUBLIC's second argument: the packed colour/effect the line is
     *  spoken in — high byte colour, low byte effect, the same pairing the
     *  inbound PLAYER_INFO chat block carries (`colour_effect`). */
    int colour_effect;
    char* name;
    char* text;
};

enum CS2VM_OC_IntField
{
    CS2VM_OC_INT_COST,
    CS2VM_OC_INT_STACKABLE,
    CS2VM_OC_INT_MEMBERS,
    CS2VM_OC_INT_ID,
};

struct CS2VM_HostRequest_PushScript
{
    int script_id;
};

struct CS2VM_HostRequest_InvSize
{
    int inv_id;
};

struct CS2VM_HostRequest_InvGetObj
{
    int inv_id;
    int slot;
};

struct CS2VM_HostRequest_InvGetNum
{
    int inv_id;
    int slot;
};

struct CS2VM_HostRequest_InvTotal
{
    int inv_id;
    int item_id;
};

struct CS2VM_HostRequest_VarsReadVarp
{
    int varp_id;
};

struct CS2VM_HostRequest_VarsReadVarbit
{
    int varbit_id;
};

/**
 * POP_VAR / POP_VARBIT: a script-side write to the client's var state.
 *
 * Deliberately a *separate* request from the server's VARP_SMALL/LARGE/SYNC
 * path: the reference applies these optimistically to `Varps_main` only and
 * never pushes the id into its changed-varp ring, so a widget hook that writes
 * a var does not re-trigger itself. See VarPManager_ServerUpdateFn's header.
 */
struct CS2VM_HostRequest_VarsWriteVarp
{
    int varp_id;
    int value;
};

struct CS2VM_HostRequest_VarsWriteVarbit
{
    int varbit_id;
    int value;
};

struct CS2VM_HostRequest_VarsReadVarcInt
{
    int varc_id;
};

struct CS2VM_HostRequest_VarsReadVarcString
{
    int varc_id;
};

/** KEYHELD / KEYPRESSED: key_code is an OSRS internal code, not ASCII. */
struct CS2VM_HostRequest_KeyQuery
{
    int key_code;
};

#define CS2VM_OPKEY_PAIR_MAX 5
/** The SETOPTKEY ("typed key") opcode variants target this op slot implicitly. */
#define CS2VM_OPKEY_TYPED_SLOT 10

/** CC/IF_SETOPKEY and the OPT variants. op_index is 1..10 (10 = typed key);
 *  pair_count == 0 clears the slot. */
struct CS2VM_HostRequest_WidgetSetOpKey
{
    int component_id;
    int op_index;
    int pair_count;
    int key_chars[CS2VM_OPKEY_PAIR_MAX];
    int key_codes[CS2VM_OPKEY_PAIR_MAX];
};

/** CC/IF_SETOPKEYRATE and SETOPKEYIGNOREHELD share one payload. */
struct CS2VM_HostRequest_WidgetSetOpKeyRate
{
    int component_id;
    int op_index;
    int rate;
    int enabled;
    /** 1 = also set ignore-held; 0 = leave unchanged. */
    int ignore_held;
};

struct CS2VM_HostRequest_VarsWriteVarcInt
{
    int varc_id;
    int value;
};

struct CS2VM_HostRequest_VarsWriteVarcString
{
    int varc_id;
    char* value;
};

struct CS2VM_HostRequest_EnumLookup
{
    int input_type;
    int output_type;
    int enum_id;
    int key;
};

struct CS2VM_HostRequest_EnumGetOutputCount
{
    int enum_id;
};

enum CS2VM_DbLoadKind
{
    CS2VM_DB_LOAD_NONE = 0,
    CS2VM_DB_LOAD_ROW = 1,
    CS2VM_DB_LOAD_INDEX = 2,
    CS2VM_DB_LOAD_TABLE = 3,
};

struct CS2VM_HostRequest_Db
{
    int opcode;    /* the DB_* opcode (7500..7510) */
    int load_kind; /* enum CS2VM_DbLoadKind — what a pending yield awaits */
    int load_id;   /* row id (LOAD_ROW) or table id (LOAD_INDEX / LOAD_TABLE) */
};

struct CS2VM_HostRequest_CC_DeleteAll
{
    int component_id;
};

struct CS2VM_HostRequest_CC_Create
{
    int parent_id;
    int component_type;
    int child_index;
    int is_nested;
    int dot_operand;
    /** CC_CREATESIBLING names the existing sibling here; the host resolves
     *  its parent before creating the new child. Other create opcodes name the
     *  parent directly. */
    int parent_is_sibling;
};

/** CC_COPY: clone dynamic child src_sub_id of parent_id into dst_sub_id. */
struct CS2VM_HostRequest_CC_Copy
{
    int parent_id;
    int src_sub_id;
    int dst_sub_id;
    int dot_operand;
};

struct CS2VM_HostRequest_CC_Find
{
    int parent_id;
    int sub_id;
    int dot_operand;
};

struct CS2VM_HostRequest_IF_GetWidth
{
    int component_id;
};

struct CS2VM_HostRequest_IF_HasChild
{
    int component_id;
    int group_id;
};

struct CS2VM_HostRequest_IF_GetHeight
{
    int component_id;
};

struct CS2VM_HostRequest_IF_GetLayer
{
    int component_id;
};

struct CS2VM_HostRequest_IF_SetHide
{
    int component_id;
    bool hidden;
};

struct CS2VM_HostRequest_IF_SetScrollPos
{
    int component_id;
    int scroll_x;
    int scroll_y;
};

struct CS2VM_HostRequest_IF_SetScrollSize
{
    int component_id;
    int scroll_width;
    int scroll_height;
};

struct CS2VM_HostRequest_IF_SetOutline
{
    int component_id;
    int outline;
};

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

struct CS2VM_HostRequest_IF_SetOnVarTransmit
{
    int component_id;
    int script_id;
    char* signature;
    int* trigger_ids;
    int trigger_count;
    int int_args[CS2VM_SETON_INT_ARG_MAX];
    int int_arg_count;
    uint64_t str_arg_mask;
    int str_arg_count;
    char str_args[CS2VM_SETON_STR_ARG_MAX][CS2VM_SETON_STR_ARG_LEN];
};

struct CS2VM_HostRequest_IF_SetOnInvTransmit
{
    int component_id;
    int script_id;
    char* signature;
    int* trigger_ids;
    int trigger_count;
    int int_args[CS2VM_SETON_INT_ARG_MAX];
    int int_arg_count;
    uint64_t str_arg_mask;
    int str_arg_count;
    char str_args[CS2VM_SETON_STR_ARG_MAX][CS2VM_SETON_STR_ARG_LEN];
};

struct CS2VM_HostRequest_IF_SetOnOp
{
    int component_id;
    int script_id;
    char* signature;
    int* trigger_ids;
    int trigger_count;
    int int_args[CS2VM_SETON_INT_ARG_MAX];
    int int_arg_count;
    uint64_t str_arg_mask;
    int str_arg_count;
    char str_args[CS2VM_SETON_STR_ARG_MAX][CS2VM_SETON_STR_ARG_LEN];
};

struct CS2VM_HostRequest_CC_SetOnOp
{
    /* Target child: dot ops (operand 1) bind the dot register, plain ops the
     * active register. Resolved at op-execution time in the VM — the host must
     * not re-read a register that later ops may have retargeted. */
    int component_id;
    int script_id;
    char* signature;
    int* trigger_ids;
    int trigger_count;
    int int_args[CS2VM_SETON_INT_ARG_MAX];
    int int_arg_count;
    uint64_t str_arg_mask;
    int str_arg_count;
    char str_args[CS2VM_SETON_STR_ARG_MAX][CS2VM_SETON_STR_ARG_LEN];
};

struct CS2VM_HostRequest_IF_ClearOps
{
    int component_id;
};

/** IF_CALLONRESIZE — the component whose on-resize listener to run. */
struct CS2VM_HostRequest_IF_CallOnResize
{
    int component_id;
};

/** CC_TRIGGEROP — the dot/active component whose on_op listener to run, and
 *  the op index to report through it (event_opindex). */
/**
 * One sound request, shared by all four sound opcodes.
 *
 * Which fields are meaningful depends on `kind`: SYNTH uses id/loops/delay,
 * JINGLE uses id/delay, SONG uses id and the four fade fields, and
 * SONG_WITHSECONDARY adds secondary_id. Fades arrive in client cycles, as the
 * scripts and the wire both carry them; the conversion to milliseconds happens
 * where the player is called, so both entry points convert the same way.
 */
struct CS2VM_HostRequest_Sound
{
    int id;
    int secondary_id;
    int loops;
    int delay;
    int fade_out_delay;
    int fade_out_speed;
    int fade_in_delay;
    int fade_in_speed;
};

struct CS2VM_HostRequest_CC_TriggerOp
{
    int component_id;
    int op_index;
};

/** IF_TRIGGEROPLOCAL — component to click and the sub-id that becomes
 *  last_slot (quest id when childIndex was -1 and the signature carried "i"). */
struct CS2VM_HostRequest_IF_TriggerOpLocal
{
    int component_id;
    int sub;
};

struct CS2VM_HostRequest_IF_ClearOpSubmenu
{
    int component_id;
    int op_index; /* 1-based */
};

struct CS2VM_HostRequest_IF_SetOp
{
    int component_id;
    int index;
    char* text;
};

struct CS2VM_HostRequest_IF_SetOpBase
{
    int component_id;
    char* text;
};

/** CC/IF_SETTARGETVERB: the selection verb used when WidgetFlags' target mask
 * is nonzero (for example an object-backed backpack cell's generic action). */
struct CS2VM_HostRequest_IF_SetTargetVerb
{
    int component_id;
    char* text;
};

struct CS2VM_HostRequest_IF_SetOpSubmenu
{
    int component_id;
    int sub_index;
    int op_index;
    char* text;
};

struct CS2VM_HostRequest_IF_SetTargetPriority
{
    int component_id;
    int priority;
};

struct CS2VM_HostRequest_CC_SetPosition
{
    int component_id;
    int x;
    int y;
    int xmode;
    int ymode;
};

struct CS2VM_HostRequest_CC_SetSize
{
    int component_id;
    int width;
    int height;
    int wmode;
    int hmode;
};

struct CS2VM_HostRequest_CC_SetGraphic
{
    int component_id;
    int graphic_id;
};

struct CS2VM_HostRequest_CC_SetTiling
{
    int component_id;
    int tiling;
};

struct CS2VM_HostRequest_CC_SetOutline
{
    int component_id;
    int outline;
};

struct CS2VM_HostRequest_CC_SetGraphicShadow
{
    int component_id;
    int shadow;
};

struct CS2VM_HostRequest_CC_SetColour
{
    int component_id;
    int colour;
};

struct CS2VM_HostRequest_CC_SetFill
{
    int component_id;
    int filled;
};

struct CS2VM_HostRequest_CC_SetTrans
{
    int component_id;
    int trans;
};

struct CS2VM_HostRequest_CC_SetNoClickThrough
{
    int component_id;
    int enabled;
};

struct CS2VM_HostRequest_CC_SetText
{
    int component_id;
    char* text;
};

struct CS2VM_HostRequest_CC_SetTextFont
{
    int component_id;
    int font_id;
};

struct CS2VM_HostRequest_CC_SetTextAlign
{
    int component_id;
    int x_align;
    int y_align;
    int line_height;
};

struct CS2VM_HostRequest_CC_SetTextShadow
{
    int component_id;
    int shadowed;
};

struct CS2VM_HostRequest_CC_SetDraggable
{
    int component_id;
    int parent_uid;
    int child_index;
};

/** IF/CC_DRAGPICKUP — start (or re-target) a component drag with an explicit
 *  grab offset inside the widget. Reference Client.dragTryPickup. */
struct CS2VM_HostRequest_DragPickup
{
    int component_id;
    int pickup_x;
    int pickup_y;
};

struct CS2VM_HostRequest_CC_SetDraggableBehavior
{
    int component_id;
    int behavior;
};

struct CS2VM_HostRequest_CC_SetDragDeadZone
{
    int component_id;
    int zone;
};

struct CS2VM_HostRequest_CC_SetDragDeadTime
{
    int component_id;
    int time;
};

struct CS2VM_HostRequest_CC_SetObject
{
    int component_id;
    int obj_id;
    int count;
    /** Count-text mode from which opcode variant ran: 0 = draw when
     *  stackable (plain SETOBJECT), 1 = always (_ALWAYS_NUM),
     *  2 = never (_NONUM). */
    int num_mode;
};

struct CS2VM_HostRequest_CC_GetId
{
    int component_id;
};

/** CC_GETOP / IF_GETOP: one-based operation slot on the resolved component. */
struct CS2VM_HostRequest_WidgetGetOp
{
    int component_id;
    int op_index;
};

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

struct CS2VM_HostRequest_CC_ComponentParam
{
    int component_id;
    int param_id;
    /** Setter only, and only when `kind` is not STRING. */
    int value;
    /** Setter only, and only when `kind` is STRING: the value, owned by the VM's
     *  string pool, so a host that keeps it must copy it. */
    char const* str_value;
    /** Setter only. One of CS2_CC_COMPONENTPARAM_KIND_*. */
    int kind;
};

struct CS2VM_HostRequest_IF_SetObject
{
    int component_id;
    int obj_id;
    int count;
    /** See CS2VM_HostRequest_CC_SetObject.num_mode. */
    int num_mode;
};

struct CS2VM_HostRequest_OC_Param
{
    int param_id;
    int item_id;
};

/** NC_PARAM (6513) / LC_PARAM (6514): the same shape as OC_PARAM over an npc
 *  or a loc type. `type_id` is the record; the answer is its param, or the
 *  ParamType's default when the record does not carry that key. */
struct CS2VM_HostRequest_TypeParam
{
    int param_id;
    int type_id;
};

struct CS2VM_HostRequest_OC_Name
{
    int item_id;
};

struct CS2VM_HostRequest_NC_Name
{
    int npc_id;
};

struct CS2VM_HostRequest_OC_Unplaceholder
{
    int item_id;
};

/** OC_OP/OC_IOP shared payload. `opcode` distinguishes ground vs inventory;
 *  `op_index` is the normalized zero-based menu slot (0..4). */
struct CS2VM_HostRequest_OC_Op
{
    int opcode;
    int item_id;
    int op_index;
};

/** OC_FIND/OC_FINDNEXT/OC_FINDRESET shared payload. `opcode` distinguishes which
 *  of the three fired. `query` is OC_FIND's popped search string (NULL for the
 *  other two); it is *borrowed* — still owned by the VM handler, which keeps it
 *  on the stack across a load yield and frees it on completion. */
struct CS2VM_HostRequest_OC_Find
{
    int opcode;
    char* query;
};

/** OC_WEARPOS/WEARPOS2/WEARPOS3 shared payload. `opcode` distinguishes which
 *  slot is being asked about. */
struct CS2VM_HostRequest_OC_WearPos
{
    int opcode;
    int item_id;
};

/** oc_isubop(obj, opIndex, subIndex) -> string. */
struct CS2VM_HostRequest_OC_Isubop
{
    int item_id;
    int op_index;
    int sub_index;
};

struct CS2VM_HostRequest_ParaHeight
{
    int font_id;
    int max_width;
    char* text;
};

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

enum CS2VM_WidgetInputField
{
    CS2VM_WIDGET_INPUT_SUBMITMODE,
    CS2VM_WIDGET_INPUT_SELECTCOLOUR,
    CS2VM_WIDGET_INPUT_ACCEPTMODE,
    CS2VM_WIDGET_INPUT_WRAPMODE,
    CS2VM_WIDGET_INPUT_LINEWRAPPINGWIDTH,
    CS2VM_WIDGET_INPUT_SELECTBGCOLOUR,
    CS2VM_WIDGET_INPUT_LINECOUNTLIMIT,
    CS2VM_WIDGET_INPUT_CURSORCOLOUR,
    CS2VM_WIDGET_INPUT_CURSORTRANS,
    CS2VM_WIDGET_INPUT_CURSORWIDTH,
    CS2VM_WIDGET_INPUT_CURSORHEIGHT,
    CS2VM_WIDGET_INPUT_CURSOROFFSET,
    CS2VM_WIDGET_INPUT_LINEWIDTHLIMIT,
    CS2VM_WIDGET_INPUT_CHARFILTER,
};

struct CS2VM_HostRequest_WidgetSetInt
{
    int component_id;
    enum CS2VM_WidgetIntField field;
    int value;
};

struct CS2VM_HostRequest_WidgetSetInt2
{
    int component_id;
    enum CS2VM_WidgetIntField field;
    int value_a;
    int value_b;
};

struct CS2VM_HostRequest_WidgetSetModelAngle
{
    int component_id;
    int offset_x;
    int offset_y;
    int angle_x;
    int angle_y;
    int angle_z;
    int zoom;
};

struct CS2VM_HostRequest_WidgetSetArc
{
    int component_id;
    int arc_start;
    int arc_end;
};

struct CS2VM_HostRequest_WidgetSetModel
{
    int component_id;
    int model_id;
};

struct CS2VM_HostRequest_WidgetSetModelKind
{
    int component_id;
    enum CS2VM_ModelKind model_kind;
    int model_id;
};

struct CS2VM_HostRequest_WidgetInputInt
{
    int component_id;
    enum CS2VM_WidgetInputField field;
    int value;
};

struct CS2VM_HostRequest_TargetFind
{
    int component_id;
    int dot_operand;
};

struct CS2VM_HostRequest_CC_ChildrenFind
{
    int parent_id;
    int start_index;
};

struct CS2VM_HostRequest_IF_ChildrenFind
{
    int uid;
    int start_index;
    int dot_operand;
};

struct CS2VM_HostRequest_StructParam
{
    int struct_id;
    int param_id;
};

struct CS2VM_HostRequest_OC_IntParam
{
    int item_id;
    enum CS2VM_OC_IntField field;
};

/** Any 6600..6640 opcode. `arg0`/`arg1` hold its popped int args in push
 *  order (arg0 pushed first), unused ones left at 0. */
struct CS2VM_HostRequest_WorldMap
{
    int opcode;
    int arg0;
    int arg1;
};

/** Any 6693..6699 map element config opcode. */
struct CS2VM_HostRequest_MEC
{
    int opcode;
    int mec_id;
};

/** STAT / STAT_BASE / STAT_XP. `stat` is the protocol skill index. */
struct CS2VM_HostRequest_Stat
{
    int stat;
};

/** CAM_SETFOLLOWHEIGHT payload; the getter carries no args. */
struct CS2VM_HostRequest_CamSetFollowHeight
{
    int height;
};

/** CAM_FORCEANGLE (5504): snap the orbit camera. `angle_x` is the pitch in the
 *  script's 128..383 units, `angle_y` the yaw in 0..2047 — the same units
 *  CAM_GETANGLE_XA/YA read back. */
struct CS2VM_HostRequest_CamForceAngle
{
    int angle_x;
    int angle_y;
};

/** Any HIGHLIGHT_* opcode. `args` holds its popped int args in push order
 *  (args[0] pushed first); the widest variant is any group's SETUP, which pops
 *  5. `query` marks the GET variants, which must push a bool result. The one
 *  string argument in the family -- the PLAYER group's name -- is popped and
 *  dropped by the VM rather than carried here: no host state keys on it. */
#define CS2VM_HIGHLIGHT_ARG_MAX 5
struct CS2VM_HostRequest_Highlight
{
    int opcode;
    int args[CS2VM_HIGHLIGHT_ARG_MAX];
    int arg_count;
    bool query;
    /**
     * The subject the PLAYER family's ON / OFF / GET take off the STRING
     * stack, and NULL for every other form.
     *
     * Borrowed from the VM's string pool, like CS2VM_HostRequest_Loot::name:
     * the pool entry dies with the frame, so a host that keeps it must copy
     * it. This used to be popped and dropped on the floor, which left
     * `highlight_player_on(_6900, 5)` -- the mouse-over player highlight --
     * with no subject to record.
     */
    char* name;
};

/**
 * LOC_FIND (6803) and COORD_INSCENE (6951): the two "is this still there"
 * gates every static-overlay script opens with.
 *
 * LOC_FIND also has a side effect -- it makes the loc it found the ACTIVE LOC,
 * which is what the OVERLAY_LOC_* ops and the `_6800/_6801/_6802` getters then
 * answer about. `loc_type` is unused by COORD_INSCENE.
 */
struct CS2VM_HostRequest_SubjectFind
{
    int opcode;
    int coord;
    int loc_type;
};

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
struct CS2VM_HostRequest_EntityOverlay
{
    int opcode;
    int args[CS2VM_OVERLAY_ARG_MAX];
    int arg_count;
    bool query;
    int dot_operand;
};

/** Any MINIMENU_* opcode (7100..7110); they take no args, so only the opcode
 *  distinguishes them. The host pushes the result. */
struct CS2VM_HostRequest_Minimenu
{
    int opcode;
};

/** Any volume / *OPTION_* opcode (3203..3217). `option_id` is the option key for
 *  the CLIENT/GAME/DEVICE families (0 for the direct volume ops); `value` is the
 *  SET payload (unused by the getters). The host pushes getter results. */
struct CS2VM_HostRequest_ClientOption
{
    int opcode;
    int option_id;
    int value;
};

/** Any minimap zoom opcode (7250..7254). `value` is the setters' popped arg
 *  (unused by GETZOOM, which the host answers). */
struct CS2VM_HostRequest_Minimap
{
    int opcode;
    int value;
};

/** Any local-notification opcode (3170..3173). Only LOCAL_NOTIFICATION fills the
 *  payload; CANCEL uses `id` alone, and CANCELALL/SUPPORTED carry nothing. The
 *  strings are borrowed for the duration of the call — the op frees them. */
struct CS2VM_HostRequest_LocalNotification
{
    int opcode;
    int id;
    int delay_ms;
    char const* title;
    char const* body;
};

/** Any VIEWPORT_* opcode (6200..6205). `args` holds the popped ints in push
 *  order (SETFOV/SETZOOM pop 2, CLAMPFOV pops 4, the GETs pop none). */
#define CS2VM_VIEWPORT_ARG_MAX 4
struct CS2VM_HostRequest_Viewport
{
    int opcode;
    int args[CS2VM_VIEWPORT_ARG_MAX];
    int arg_count;
};

/** Any UIZOOM_* opcode (6210..6214). `value` is UIZOOM_SET's popped arg (unused
 *  by the others). */
struct CS2VM_HostRequest_UiZoom
{
    int opcode;
    int value;
};

/** Any SAFEAREA_* opcode (6220..6223, 6231); no args to carry. */
struct CS2VM_HostRequest_SafeArea
{
    int opcode;
};

/** Any CLIENTOP_* opcode (6700..6709). SET carries slot + script_id + label;
 *  DEL carries only slot (`is_set` false, label NULL, script_id unused). */
struct CS2VM_HostRequest_ClientOp
{
    int opcode;
    bool is_set;
    int slot;
    int script_id;
    char* label;
};

/** A client-op context getter. The opcode is the whole request: these take no
 *  arguments and the host pushes the answer. */
struct CS2VM_HostRequest_ClientOpContext
{
    int opcode;
};

/** 6902..6905. `index` is `_6903`'s route index and -1 for the three forms
 *  that pop nothing. */
struct CS2VM_HostRequest_ActivePlayer
{
    int opcode;
    int index;
};

/** Any loot-tracker opcode (7400-family + 7600-family). `name` is borrowed
 *  from the VM's string pool for the ops that pop a string; the host must
 *  copy it if kept. `int_args` hold remaining int arguments in pop order. */
struct CS2VM_HostRequest_Loot
{
    int opcode;
    char* name;
    int int_args[4];
    int int_arg_count;
};

/** Any hiscores opcode (7809/7811). Only the opcode distinguishes them. */
struct CS2VM_HostRequest_Hiscores
{
    int opcode;
};

/** SETWINDOWMODE / SETDEFAULTWINDOWMODE payload: one enum CS2VM_WindowMode. */
struct CS2VM_HostRequest_WindowMode
{
    int mode;
};

struct CS2VM_HostRequest
{
    enum CS2VM_HostRequestKind kind;

    union
    {
        struct CS2VM_HostRequest_WindowMode window_mode;
        struct CS2VM_HostRequest_PushScript push_script;
        struct CS2VM_HostRequest_InvSize invs_get_size;
        struct CS2VM_HostRequest_InvGetObj invs_get_obj;
        struct CS2VM_HostRequest_InvGetNum invs_get_num;
        struct CS2VM_HostRequest_InvTotal invs_get_total;
        struct CS2VM_HostRequest_VarsReadVarp vars_read_varp;
        struct CS2VM_HostRequest_VarsReadVarbit vars_read_varbit;
        struct CS2VM_HostRequest_VarsWriteVarp vars_write_varp;
        struct CS2VM_HostRequest_VarsWriteVarbit vars_write_varbit;
        struct CS2VM_HostRequest_VarsReadVarcInt vars_read_varc_int;
        struct CS2VM_HostRequest_KeyQuery key_query;
        struct CS2VM_HostRequest_WidgetSetOpKey widget_set_opkey;
        struct CS2VM_HostRequest_WidgetSetOpKeyRate widget_set_opkey_rate;
        struct CS2VM_HostRequest_VarsReadVarcString vars_read_varc_string;
        struct CS2VM_HostRequest_VarsWriteVarcInt vars_write_varc_int;
        struct CS2VM_HostRequest_VarsWriteVarcString vars_write_varc_string;
        struct CS2VM_HostRequest_EnumLookup enum_lookup;
        struct CS2VM_HostRequest_EnumGetOutputCount enum_get_output_count;
        struct CS2VM_HostRequest_Db db;
        struct CS2VM_HostRequest_CC_DeleteAll cc_delete_all;
        /* CC_DELETE carries the same single component id as CC_DELETEALL, so it
         * shares the payload type; the request KIND is what says whether that
         * id is a parent to clear or a child to remove. */
        struct CS2VM_HostRequest_CC_DeleteAll cc_delete;
        struct CS2VM_HostRequest_CC_Create cc_create;
        struct CS2VM_HostRequest_CC_Copy cc_copy;
        struct CS2VM_HostRequest_CC_Find cc_find;
        struct CS2VM_HostRequest_CC_SetPosition cc_set_position;
        struct CS2VM_HostRequest_CC_SetSize cc_set_size;
        struct CS2VM_HostRequest_CC_SetGraphic cc_set_graphic;
        struct CS2VM_HostRequest_CC_SetGraphic cc_set_graphic2;
        struct CS2VM_HostRequest_CC_SetTiling cc_set_tiling;
        struct CS2VM_HostRequest_CC_SetOutline cc_set_outline;
        struct CS2VM_HostRequest_CC_SetGraphicShadow cc_set_graphic_shadow;
        struct CS2VM_HostRequest_CC_SetColour cc_set_colour;
        struct CS2VM_HostRequest_CC_SetFill cc_set_fill;
        struct CS2VM_HostRequest_CC_SetTrans cc_set_trans;
        struct CS2VM_HostRequest_CC_SetNoClickThrough cc_set_no_click_through;
        struct CS2VM_HostRequest_CC_SetText cc_set_text;
        struct CS2VM_HostRequest_CC_SetTextFont cc_set_text_font;
        struct CS2VM_HostRequest_CC_SetTextAlign cc_set_text_align;
        struct CS2VM_HostRequest_CC_SetTextShadow cc_set_text_shadow;
        struct CS2VM_HostRequest_CC_SetDraggable cc_set_draggable;
        struct CS2VM_HostRequest_CC_SetDraggableBehavior cc_set_draggable_behavior;
        struct CS2VM_HostRequest_CC_SetDragDeadZone cc_set_drag_dead_zone;
        struct CS2VM_HostRequest_CC_SetDragDeadTime cc_set_drag_dead_time;
        struct CS2VM_HostRequest_CC_SetObject cc_set_object;
        struct CS2VM_HostRequest_CC_GetId cc_get_id;
        struct CS2VM_HostRequest_CC_SetOnOp cc_set_on_op;
        struct CS2VM_HostRequest_OC_Param oc_param;
        struct CS2VM_HostRequest_TypeParam nc_param;
        struct CS2VM_HostRequest_TypeParam lc_param;
        struct CS2VM_HostRequest_OC_Name oc_name;
        struct CS2VM_HostRequest_NC_Name nc_name;
        struct CS2VM_HostRequest_OC_Unplaceholder oc_unplaceholder;
        struct CS2VM_HostRequest_OC_Op oc_op;
        struct CS2VM_HostRequest_OC_Op oc_iop;
        struct CS2VM_HostRequest_OC_Name oc_examine;
        struct CS2VM_HostRequest_OC_Unplaceholder oc_placeholder;
        struct CS2VM_HostRequest_OC_Find oc_find;
        struct CS2VM_HostRequest_OC_Name oc_shiftclickiop;
        struct CS2VM_HostRequest_OC_WearPos oc_wearpos;
        struct CS2VM_HostRequest_OC_Name oc_weight;
        struct CS2VM_HostRequest_OC_Isubop oc_isubop;
        struct CS2VM_HostRequest_ParaHeight para_height;
        struct CS2VM_HostRequest_IF_GetWidth if_get_width;
        struct CS2VM_HostRequest_IF_HasChild if_has_child;
        struct CS2VM_HostRequest_IF_GetHeight if_get_height;
        struct CS2VM_HostRequest_WidgetGetOp widget_get_op;
        struct CS2VM_HostRequest_IF_GetLayer if_get_layer;
        struct CS2VM_HostRequest_IF_GetLayer if_get_scroll_x;
        struct CS2VM_HostRequest_IF_GetLayer if_get_scroll_y;
        struct CS2VM_HostRequest_IF_GetLayer if_get_scroll_height;
        struct CS2VM_HostRequest_IF_SetHide if_set_hide;
        struct CS2VM_HostRequest_IF_SetScrollPos if_set_scroll_pos;
        struct CS2VM_HostRequest_IF_SetScrollSize if_set_scroll_size;
        struct CS2VM_HostRequest_CC_SetGraphic if_set_graphic;
        struct CS2VM_HostRequest_CC_SetText if_set_text;
        struct CS2VM_HostRequest_IF_SetOutline if_set_outline;
        struct CS2VM_HostRequest_IF_SetOnVarTransmit if_set_on_var_transmit;
        struct CS2VM_HostRequest_IF_SetOnInvTransmit if_set_on_inv_transmit;
        struct CS2VM_HostRequest_IF_SetOnOp if_set_on_op;
        struct CS2VM_HostRequest_IF_SetOnOp if_set_on_click;
        struct CS2VM_HostRequest_IF_SetOnOp if_set_on_mouse_over;
        struct CS2VM_HostRequest_IF_SetOnOp if_set_on_mouse_leave;
        struct CS2VM_HostRequest_IF_SetOnOp if_set_on_mouse_repeat;
        struct CS2VM_HostRequest_IF_SetOnOp if_set_on_timer;
        struct CS2VM_HostRequest_IF_SetOnOp if_set_on_scroll_wheel;
        struct CS2VM_HostRequest_IF_SetOnOp if_set_on_key;
        struct CS2VM_HostRequest_IF_SetOnOp if_set_on_misc_transmit;
        struct CS2VM_HostRequest_IF_SetOp if_set_op;
        struct CS2VM_HostRequest_IF_SetOpBase if_set_op_base;
        struct CS2VM_HostRequest_IF_SetTargetVerb if_set_target_verb;
        struct CS2VM_HostRequest_IF_SetOpSubmenu if_set_op_submenu;
        struct CS2VM_HostRequest_IF_SetTargetPriority if_set_target_priority;
        struct CS2VM_HostRequest_IF_ClearOps if_clear_ops;
        struct CS2VM_HostRequest_IF_CallOnResize if_call_on_resize;
        struct CS2VM_HostRequest_CC_TriggerOp cc_trigger_op;
        struct CS2VM_HostRequest_Sound sound;
        struct CS2VM_HostRequest_IF_TriggerOpLocal if_triggeroplocal;
        struct CS2VM_HostRequest_IF_ClearOpSubmenu if_clear_op_submenu;
        struct CS2VM_HostRequest_IF_SetObject if_set_object;
        struct CS2VM_HostRequest_IF_SetScrollPos cc_set_scroll_pos;
        struct CS2VM_HostRequest_IF_SetScrollSize cc_set_scroll_size;
        struct CS2VM_HostRequest_WidgetSetInt widget_set_int;
        struct CS2VM_HostRequest_WidgetSetInt2 widget_set_int2;
        struct CS2VM_HostRequest_DragPickup drag_pickup;
        struct CS2VM_HostRequest_WidgetSetModelAngle widget_set_model_angle;
        struct CS2VM_HostRequest_WidgetSetArc widget_set_arc;
        struct CS2VM_HostRequest_WidgetSetModel widget_set_model;
        struct CS2VM_HostRequest_WidgetSetModelKind widget_set_model_kind;
        struct CS2VM_HostRequest_WidgetInputInt widget_input_int;
        struct CS2VM_HostRequest_CC_ChildrenFind cc_children_find;
        struct CS2VM_HostRequest_IF_ChildrenFind if_children_find;
        struct CS2VM_HostRequest_CC_GetId cc_resolve_parent;
        struct CS2VM_HostRequest_StructParam struct_param;
        struct CS2VM_HostRequest_CC_GetId cc_gettext;
        struct CS2VM_HostRequest_CC_GetId cc_gettrans;
        struct CS2VM_HostRequest_CC_ComponentParam cc_component_param;
        struct CS2VM_HostRequest_TargetFind if_find;
        struct CS2VM_HostRequest_IF_GetWidth if_getx;
        struct CS2VM_HostRequest_IF_GetWidth if_gettext;
        struct CS2VM_HostRequest_IF_GetWidth if_getscrollwidth;
        struct CS2VM_HostRequest_OC_IntParam oc_int_param;
        struct CS2VM_HostRequest_WorldMap worldmap;
        struct CS2VM_HostRequest_MEC mec;
        struct CS2VM_HostRequest_CamSetFollowHeight cam_set_follow_height;
        struct CS2VM_HostRequest_Stat stat;
        struct CS2VM_HostRequest_CamForceAngle cam_force_angle;
        struct CS2VM_HostRequest_Highlight highlight;
        struct CS2VM_HostRequest_EntityOverlay entity_overlay;
        struct CS2VM_HostRequest_SubjectFind subject_find;
        struct CS2VM_HostRequest_Minimenu minimenu;
        struct CS2VM_HostRequest_ClientOption client_option;
        struct CS2VM_HostRequest_Minimap minimap;
        struct CS2VM_HostRequest_LocalNotification local_notification;
        struct CS2VM_HostRequest_Viewport viewport;
        struct CS2VM_HostRequest_UiZoom uizoom;
        struct CS2VM_HostRequest_SafeArea safearea;
        struct CS2VM_HostRequest_ClientOp clientop;
        struct CS2VM_HostRequest_ClientOpContext clientop_context;
        struct CS2VM_HostRequest_ActivePlayer active_player;
        struct CS2VM_HostRequest_Social social;
        struct CS2VM_HostRequest_Chat chat;
        struct CS2VM_HostRequest_ResumeCountDialog resume_countdialog;
        struct CS2VM_HostRequest_ResumePauseButton resume_pausebutton;
        struct CS2VM_HostRequest_IF_SetOnOp if_set_on_friend_transmit;
        struct CS2VM_HostRequest_Loot loot;
        struct CS2VM_HostRequest_Hiscores hiscores;
    } u;
};

struct CS2VM2_Thread;

typedef int (*CS2VM2_HostExec_Fn)(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request);

#endif /* CS2VM2_HOST_H */
