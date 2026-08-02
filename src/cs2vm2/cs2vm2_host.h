#ifndef CS2VM2_HOST_H
#define CS2VM2_HOST_H

#include <stdbool.h>
#include <stdint.h>

enum CS2VM_ModelKind
{
    CS2VM_MODEL_KIND_NONE = 0,
    CS2VM_MODEL_KIND_PLAIN = 1,
    CS2VM_MODEL_KIND_NPC_HEAD = 2,
    CS2VM_MODEL_KIND_PLAYER_HEAD = 3,
    CS2VM_MODEL_KIND_PLAYER_SELF = 5,
    CS2VM_MODEL_KIND_PLAYER_CHATHEAD = 6,
};

enum CS2VM_HostRequestKind
{
    CS2VM_HOST_REQUEST_PUSHSCRIPT,

    CS2VM_HOST_REQUEST_INVS_GET_SIZE,
    CS2VM_HOST_REQUEST_INVS_GET_OBJ,
    CS2VM_HOST_REQUEST_INVS_GET_NUM,
    CS2VM_HOST_REQUEST_INVS_GET_TOTAL,
    CS2VM_HOST_REQUEST_VARS_READ_VARP_AKA_PUSH_VAR,
    CS2VM_HOST_REQUEST_VARS_READ_VARBIT,
    CS2VM_HOST_REQUEST_VARS_READ_VARC_INT,
    CS2VM_HOST_REQUEST_KEYHELD,
    CS2VM_HOST_REQUEST_KEYPRESSED,
    CS2VM_HOST_REQUEST_WIDGET_SET_OPKEY,
    CS2VM_HOST_REQUEST_WIDGET_SET_OPKEY_RATE,
    CS2VM_HOST_REQUEST_VARS_READ_VARC_STRING,
    CS2VM_HOST_REQUEST_VARS_WRITE_VARC_INT,
    CS2VM_HOST_REQUEST_VARS_WRITE_VARC_STRING,
    CS2VM_HOST_REQUEST_ENUM_LOOKUP,
    CS2VM_HOST_REQUEST_ENUM_GETOUTPUTCOUNT,
    // CC Child component
    CS2VM_HOST_REQUEST_CC_DELETEALL,
    /** CC_DELETE: remove one dynamic child — the active/dot component, not a
     *  parent's whole child list. */
    CS2VM_HOST_REQUEST_CC_DELETE,
    CS2VM_HOST_REQUEST_CC_CREATE,
    CS2VM_HOST_REQUEST_CC_COPY,
    CS2VM_HOST_REQUEST_CC_FIND,
    CS2VM_HOST_REQUEST_CC_SETPOSITION,
    CS2VM_HOST_REQUEST_CC_SETSIZE,
    CS2VM_HOST_REQUEST_CC_SETGRAPHIC,
    CS2VM_HOST_REQUEST_CC_SETGRAPHIC2,
    CS2VM_HOST_REQUEST_CC_SETTILING,
    CS2VM_HOST_REQUEST_CC_SETOUTLINE,
    CS2VM_HOST_REQUEST_CC_SETGRAPHICSHADOW,
    CS2VM_HOST_REQUEST_CC_SETCOLOUR,
    CS2VM_HOST_REQUEST_CC_SETFILL,
    CS2VM_HOST_REQUEST_CC_SETTRANS,
    CS2VM_HOST_REQUEST_CC_SETNOCLICKTHROUGH,
    CS2VM_HOST_REQUEST_CC_SETTEXT,
    CS2VM_HOST_REQUEST_CC_SETTEXTFONT,
    CS2VM_HOST_REQUEST_CC_SETTEXTALIGN,
    CS2VM_HOST_REQUEST_CC_SETTEXTSHADOW,
    CS2VM_HOST_REQUEST_CC_SETDRAGGABLE,
    CS2VM_HOST_REQUEST_CC_SETDRAGGABLEBEHAVIOR,
    CS2VM_HOST_REQUEST_CC_SETDRAGDEADZONE,
    CS2VM_HOST_REQUEST_CC_SETDRAGDEADTIME,
    CS2VM_HOST_REQUEST_CC_SETOP,
    CS2VM_HOST_REQUEST_CC_SETOBJECT,
    CS2VM_HOST_REQUEST_CC_GETID,
    CS2VM_HOST_REQUEST_CC_GETX,
    CS2VM_HOST_REQUEST_CC_GETY,
    CS2VM_HOST_REQUEST_CC_GETWIDTH,
    CS2VM_HOST_REQUEST_CC_GETHEIGHT,
    CS2VM_HOST_REQUEST_CC_GETHIDE,
    CS2VM_HOST_REQUEST_CC_SETONCLICK,
    CS2VM_HOST_REQUEST_CC_SETONHOLD,
    CS2VM_HOST_REQUEST_CC_SETONMOUSEOVER,
    CS2VM_HOST_REQUEST_CC_SETONMOUSELEAVE,
    CS2VM_HOST_REQUEST_CC_SETONDRAG,
    CS2VM_HOST_REQUEST_CC_SETONSCROLLWHEEL,
    CS2VM_HOST_REQUEST_CC_SETONKEY,
    CS2VM_HOST_REQUEST_CC_SETONOP,
    CS2VM_HOST_REQUEST_CC_SETONDRAGCOMPLETE,
    CS2VM_HOST_REQUEST_CC_SETONMOUSEREPEAT,
    // IF Interfaces
    CS2VM_HOST_REQUEST_IF_GETWIDTH,
    CS2VM_HOST_REQUEST_IF_GETHEIGHT,
    CS2VM_HOST_REQUEST_IF_GETY,
    CS2VM_HOST_REQUEST_IF_GETLAYER,
    CS2VM_HOST_REQUEST_IF_GETTOP,
    CS2VM_HOST_REQUEST_IF_GETSCROLLX,
    CS2VM_HOST_REQUEST_IF_GETSCROLLY,
    CS2VM_HOST_REQUEST_IF_GETSCROLLHEIGHT,
    CS2VM_HOST_REQUEST_IF_GETHIDE,
    CS2VM_HOST_REQUEST_IF_HASSUB,
    /* IF_HASCHILD_MODAL/OVERLAY (2704/2705): widget has parent group mounted. */
    CS2VM_HOST_REQUEST_IF_HASCHILD,
    CS2VM_HOST_REQUEST_IF_SETHIDE,
    CS2VM_HOST_REQUEST_IF_SETPOSITION,
    CS2VM_HOST_REQUEST_IF_SETSIZE,
    CS2VM_HOST_REQUEST_IF_SETSCROLLPOS,
    CS2VM_HOST_REQUEST_IF_SETSCROLLSIZE,
    CS2VM_HOST_REQUEST_IF_SETGRAPHIC,
    CS2VM_HOST_REQUEST_IF_SETTEXT,
    CS2VM_HOST_REQUEST_IF_SETOUTLINE,
    CS2VM_HOST_REQUEST_IF_SETONVARTRANSMIT,
    /* Shares the SetOnVarTransmit payload: same script/args/trigger-list shape,
     * with stat ids in the trigger list instead of varp ids. */
    CS2VM_HOST_REQUEST_IF_SETONSTATTRANSMIT,
    CS2VM_HOST_REQUEST_IF_SETONINVTRANSMIT,
    CS2VM_HOST_REQUEST_IF_SETONOP,
    CS2VM_HOST_REQUEST_IF_SETONCLICK,
    CS2VM_HOST_REQUEST_IF_SETONMOUSEOVER,
    CS2VM_HOST_REQUEST_IF_SETONMOUSELEAVE,
    CS2VM_HOST_REQUEST_IF_SETONMOUSEREPEAT,
    CS2VM_HOST_REQUEST_IF_SETONTIMER,
    CS2VM_HOST_REQUEST_IF_SETONSCROLLWHEEL,
    CS2VM_HOST_REQUEST_IF_SETONKEY,
    CS2VM_HOST_REQUEST_IF_SETONMISCTRANSMIT,
    CS2VM_HOST_REQUEST_IF_SETONHOLD,
    CS2VM_HOST_REQUEST_IF_SETONDRAG,
    CS2VM_HOST_REQUEST_IF_SETONDRAGCOMPLETE,
    CS2VM_HOST_REQUEST_IF_SETONRESIZE,
    /*
     * IF_CALLONRESIZE (2927) — run a component's on-resize listener now.
     *
     * The listener normally fires when the layout changes; this is a script
     * asking for it without one, and it is how a self-laying-out rev-230 panel
     * runs its own body: `if_setonresize(...)` registers the builder and
     * `if_callonresize` is what starts it. Seventeen scripts in cache.osrs239
     * do exactly that; the skill guide's script1911 is one of them, and until
     * this existed opening the guide aborted the client at StackMetaStub.
     *
     * `cc_callonresize` (1927) is deliberately NOT here. Its row in
     * cs2_command.gen.h claims one argument, which is not the shape any other
     * `cc_*` component op has, and no script in this cache calls it — so the
     * stack shape is unverifiable from the data and a guessed arity is exactly
     * what StackMetaStub exists to prevent. It still aborts, loudly, which is
     * the right answer until something calls it.
     */
    CS2VM_HOST_REQUEST_IF_CALLONRESIZE,
    CS2VM_HOST_REQUEST_IF_SETONSUBCHANGE,
    CS2VM_HOST_REQUEST_CC_SETONRESIZE,
    CS2VM_HOST_REQUEST_CC_SETONSUBCHANGE,
    CS2VM_HOST_REQUEST_IF_SETDRAGGABLE,
    CS2VM_HOST_REQUEST_IF_SETDRAGGABLEBEHAVIOR,
    CS2VM_HOST_REQUEST_IF_DRAGPICKUP,
    CS2VM_HOST_REQUEST_CC_DRAGPICKUP,
    CS2VM_HOST_REQUEST_SETANTIDRAG,
    CS2VM_HOST_REQUEST_IF_SETOP,
    CS2VM_HOST_REQUEST_IF_SETOPBASE,
    CS2VM_HOST_REQUEST_IF_SETOPSUBMENU,
    CS2VM_HOST_REQUEST_IF_SETTARGETPRIORITY,
    CS2VM_HOST_REQUEST_IF_CLEAROPS,
    /* Clear submenuActions[opIndex] for one op slot (CC/IF_CLEAROPSUBMENU). */
    CS2VM_HOST_REQUEST_IF_CLEAROPSUBMENU,
    CS2VM_HOST_REQUEST_IF_SETOBJECT,
    // OC Object config
    CS2VM_HOST_REQUEST_OC_PARAM,
    CS2VM_HOST_REQUEST_OC_NAME,
    CS2VM_HOST_REQUEST_OC_UNPLACEHOLDER,
    /* OC_OP/OC_IOP: ground/inventory right-click action string at a menu slot
     * (the slot is the opcode's baked-in operand, carried as op_index below).
     * Real data (ToriRS_Objtype.ground_actions/inv_actions). */
    CS2VM_HOST_REQUEST_OC_OP,
    CS2VM_HOST_REQUEST_OC_IOP,
    /* OC_EXAMINE: real data (ToriRS_Objtype.desc). */
    CS2VM_HOST_REQUEST_OC_EXAMINE,
    /* OC_PLACEHOLDER: the reverse of OC_UNPLACEHOLDER (real item -> placeholder
     * id). No placeholder linkage exists on ToriRS_Objtype, so — like
     * OC_UNPLACEHOLDER already does — this is an identity passthrough stub. */
    CS2VM_HOST_REQUEST_OC_PLACEHOLDER,
    /* OC_FIND/OC_FINDNEXT/OC_FINDRESET: a stateful item-name search iterator.
     * OC_FIND scans every resident objtype for a name substring and records the
     * matches; FINDNEXT walks them; FINDRESET clears them. Yields once (kind
     * OC_FIND) to bulk-load the obj group before the first scan. */
    CS2VM_HOST_REQUEST_OC_FIND,
    /* OC_SHIFTCLICKIOP: default shift-click op index for an item. No per-item
     * preference data exists, so this stubs to -1 (no default). */
    CS2VM_HOST_REQUEST_OC_SHIFTCLICKIOP,
    /* OC_WEARPOS/WEARPOS2/WEARPOS3: equip slot(s) an item occupies. No equip
     * slot data exists on ToriRS_Objtype, so this stubs to -1 (not equippable);
     * carries the opcode to distinguish which slot is being asked about. */
    CS2VM_HOST_REQUEST_OC_WEARPOS,
    /* OC_WEIGHT: no weight data exists on ToriRS_Objtype, so this stubs to 0. */
    CS2VM_HOST_REQUEST_OC_WEIGHT,
    /* OC_ISUBOP: sub-op string under an inventory op slot. No sub-menu nesting
     * exists on ToriRS_Objtype, so this stubs to "". */
    CS2VM_HOST_REQUEST_OC_ISUBOP,
    CS2VM_HOST_REQUEST_PARAHEIGHT,
    CS2VM_HOST_REQUEST_IF_SETON_DISCARD,
    CS2VM_HOST_REQUEST_CC_SETON_DISCARD,
    CS2VM_HOST_REQUEST_CC_SETONTIMER,
    CS2VM_HOST_REQUEST_CC_SETONVARTRANSMIT,
    CS2VM_HOST_REQUEST_CC_SETONINVTRANSMIT,
    CS2VM_HOST_REQUEST_PARAWIDTH,

    CS2VM_HOST_REQUEST_CC_SETSCROLLPOS,
    CS2VM_HOST_REQUEST_CC_SETSCROLLSIZE,
    CS2VM_HOST_REQUEST_WIDGET_SET_INT,
    CS2VM_HOST_REQUEST_WIDGET_SET_INT2,
    CS2VM_HOST_REQUEST_WIDGET_SET_MODEL_ANGLE,
    CS2VM_HOST_REQUEST_WIDGET_SET_ARC,
    CS2VM_HOST_REQUEST_WIDGET_SET_MODEL,
    CS2VM_HOST_REQUEST_WIDGET_SET_MODEL_KIND,
    CS2VM_HOST_REQUEST_WIDGET_INPUT_INT,

    CS2VM_HOST_REQUEST_CC_FINDROOT,
    CS2VM_HOST_REQUEST_CC_CHILDREN_FIND,
    CS2VM_HOST_REQUEST_IF_CHILDREN_FIND,
    CS2VM_HOST_REQUEST_CC_RESOLVE_PARENT,
    CS2VM_HOST_REQUEST_STRUCT_PARAM,
    CS2VM_HOST_REQUEST_CC_GETTEXT,
    CS2VM_HOST_REQUEST_CC_GETTRANS,
    /* CC_GETCOMPONENTPARAM / CC_SETCOMPONENTPARAM (1703/1704): the component's
     * own runtime param table, which the gameframe scripts use to tag the
     * widgets they build and recognise them again later. Nothing but these two
     * opcodes writes it — OldSchool IF3 files carry no param section — so a read
     * that misses falls through to the ParamType default, which may need a load
     * and is why the getter can yield. */
    CS2VM_HOST_REQUEST_CC_GETCOMPONENTPARAM,
    /* IF_GETCOMPONENTPARAM (2703): same table, component named by argument.
     * Shares CC_ComponentParam; `value` carries the caller's fallback. */
    CS2VM_HOST_REQUEST_IF_GETCOMPONENTPARAM,
    CS2VM_HOST_REQUEST_CC_SETCOMPONENTPARAM,
    CS2VM_HOST_REQUEST_IF_FIND,
    CS2VM_HOST_REQUEST_IF_GETX,
    CS2VM_HOST_REQUEST_IF_GETTEXT,
    CS2VM_HOST_REQUEST_IF_GETSCROLLWIDTH,
    CS2VM_HOST_REQUEST_OC_INT_PARAM,
    CS2VM_HOST_REQUEST_CLIENTCLOCK,
    /* STAT / STAT_BASE / STAT_XP (3305-3307): the skill a script is asking
     * about. `stat` is the protocol's skill index — the same one UPDATE_STAT
     * carries. */
    CS2VM_HOST_REQUEST_STAT,
    CS2VM_HOST_REQUEST_STAT_BASE,
    CS2VM_HOST_REQUEST_STAT_XP,
    /* RUNENERGY_VISIBLE (0-100) / RUNWEIGHT_VISIBLE (grams). Both arrive from
     * the server (UPDATE_RUNENERGY / UPDATE_RUNWEIGHT) and are read back by the
     * minimap orb's paint script; neither takes an argument. */
    CS2VM_HOST_REQUEST_RUNENERGY,
    CS2VM_HOST_REQUEST_RUNWEIGHT,
    /* Current pointer position in canvas coords (MOUSE_GETX / MOUSE_GETY). */
    CS2VM_HOST_REQUEST_MOUSE_GETX,
    CS2VM_HOST_REQUEST_MOUSE_GETY,
    /* The whole 6600..6640 world map family and the 6693..6699 map element
     * family go through one kind each: they share a single state object, so
     * forty request kinds would only spread one switch across three files. */
    CS2VM_HOST_REQUEST_WORLDMAP,
    CS2VM_HOST_REQUEST_MEC,

    /* Camera follow-height get/set (CAM_SETFOLLOWHEIGHT / CAM_GETFOLLOWHEIGHT).
     * The height at which the follow camera trails the player; the host owns the
     * value so a script can set it and read it back. */
    CS2VM_HOST_REQUEST_CAM_SETFOLLOWHEIGHT,
    CS2VM_HOST_REQUEST_CAM_GETFOLLOWHEIGHT,

    /* The whole HIGHLIGHT_* family (7000..7037: NPC / LOC / OBJ / PLAYER / TILE
     * entity-outline highlighting) goes through one kind, like WORLDMAP/MEC —
     * they share one highlight-state object the host will eventually own, so per
     * subject kinds would only spread one switch across files. The payload
     * carries the opcode and its popped args; `query` GET variants push a bool. */
    CS2VM_HOST_REQUEST_HIGHLIGHT,

    /* The MINIMENU_* mouseover / right-click-menu query family (7100..7110), one
     * kind carrying the opcode like WORLDMAP. All are no-arg getters that read
     * the current hovered target + menu-open state; the host pushes each op's
     * result (an int, or the two strings of MINIMENU_ENTRY). */
    CS2VM_HOST_REQUEST_MINIMENU,

    /* Audio-volume and client/game/device option get/set (3203..3217), one kind
     * carrying the opcode + option id + value. The host owns the values (like
     * CAM_*FOLLOWHEIGHT) and pushes the getters' results. */
    CS2VM_HOST_REQUEST_CLIENT_OPTION,

    /* Minimap zoom controls (7250..7254), one kind carrying the opcode + value.
     * The host owns the zoom so SETZOOM round-trips through GETZOOM. */
    CS2VM_HOST_REQUEST_MINIMAP,

    /* Mobile local notifications (3170..3173), one kind carrying the opcode plus
     * the scheduling payload. Desktop has no notification centre, so the host
     * stubs the family (see exec_local_notification). */
    CS2VM_HOST_REQUEST_LOCAL_NOTIFICATION,

    /* LOGOUT (5630): no payload, no return value. The client has no logout flow
     * wired up yet, so the host just records the request (see
     * RS_CS2Host.logout_requested) for whatever drives the actual disconnect. */
    CS2VM_HOST_REQUEST_LOGOUT,

    /* IF_CLOSE (3103): no payload, no return value. Every framed interface's X
     * runs it — `steelborder` binds op 1 to clientscript 29, whose whole body is
     * `if_close`. It closes nothing locally: the reference sends CLOSE_MODAL and
     * the server unmounts. The host records the request (see
     * RS_CS2Host.close_modal_requested) and the App drains it on the next tick,
     * which is the same shape as LOGOUT above. */
    CS2VM_HOST_REQUEST_IF_CLOSE,

    /* RESUME_COUNTDIALOG (3104): the answer to a server script parked on
     * P_COUNTDIALOG. Same split as IF_CLOSE — the host has no socket, so it
     * queues the send and the App turns it into RESUME_P_COUNTDIALOG.
     *
     * The payload is a STRING because the opcode's callers push one:
     * `resume_countdialog(tostring($n))`. The wire carries an int, and the
     * conversion belongs on the App side, where the chatbox's own "Enter
     * amount" path already does exactly the same atol. */
    CS2VM_HOST_REQUEST_RESUME_COUNTDIALOG,

    /* Viewport FOV/zoom get/set/clamp (6200..6205), one kind carrying the opcode
     * and its popped args. The host owns both values so a SET/CLAMP round-trips
     * through the matching GET (like CAM_*FOLLOWHEIGHT). */
    CS2VM_HOST_REQUEST_VIEWPORT,

    /* UI zoom get/set/reset/default (6210..6214), one kind carrying the opcode +
     * value. The host owns the current zoom; GETDEFAULT answers a fixed
     * constant without touching it. */
    CS2VM_HOST_REQUEST_UIZOOM,

    /* Safe-area bounds (6220..6223, 6231), one kind carrying the opcode. No
     * payload beyond that — the host answers from the live canvas size. */
    CS2VM_HOST_REQUEST_SAFEAREA,

    /* CAM_GETYAW: no payload, no setter in this range. Same situation as
     * CAM_*FOLLOWHEIGHT — the host owns the value, but nothing writes it yet
     * (no live link to the render-side camera from RS_CS2Host). */
    CS2VM_HOST_REQUEST_CAM_GETYAW,

    /* CAM_FORCEANGLE (5504) and CAM_GETANGLE_XA/YA (5505/5506): the orbit
     * camera's pitch and yaw in the units scripts see — pitch 128..383,
     * yaw 0..2047, which is exactly how the reference stores orbitCameraPitch /
     * orbitCameraYaw. The host mirrors the live camera (app pushes it in every
     * logic tick) and raises a force flag the app consumes on the way back. */
    CS2VM_HOST_REQUEST_CAM_FORCEANGLE,
    CS2VM_HOST_REQUEST_CAM_GETANGLE_XA,
    CS2VM_HOST_REQUEST_CAM_GETANGLE_YA,

    /* CLIENTOP_* (6700..6709): install/remove transient client-owned context-menu
     * ops on an NPC/LOC/OBJ/PLAYER/TILE slot. One kind carrying the opcode +
     * popped args; the host stubs them until an enhanced-menu system exists. */
    CS2VM_HOST_REQUEST_CLIENTOP,

    /* Client database family (DB_* opcodes 7500..7510), one kind carrying the
     * opcode and its popped args. Reads DBROW config (kind 38) and the
     * DBTABLEINDEX (cache table 21); the host owns the active find-iterator
     * (matched row ids + cursor). The payload's load_kind/load_id say which
     * resource a yield is waiting on (a row or a table index). */
    CS2VM_HOST_REQUEST_DB,

    /* Friends / ignore list family (3600..3609, 3621..3623), one kind carrying
     * the opcode and its popped args — the world-map family's shape, for the
     * same reason: twelve near-identical accessors over one store.
     *
     * The store is the client's own friend list (struct RS_Social), fed by
     * UPDATE_FRIENDLIST / UPDATE_IGNORELIST / FRIENDLIST_LOADED. The four
     * mutators (FRIEND_ADD/DEL, IGNORE_ADD/DEL) also queue an outbound packet;
     * see CS2VM_HOST_REQUEST_CHAT for why that is a queue and not a send. */
    CS2VM_HOST_REQUEST_SOCIAL,

    /* Chat filter modes and the private send (5000/5001/5005/5009/5016), one
     * kind carrying the opcode. CHAT_SETFILTER and CHAT_SENDPRIVATE both have
     * to reach the network, which this host has no pointer to — so they park
     * the request on the host and the App drains it on the next tick, which is
     * the same shape LOGOUT and IF_CLOSE already use. */
    CS2VM_HOST_REQUEST_CHAT,

    /* MAP_WORLD (3318): this client's world id. The friends panel prints it in
     * its header and compares each friend's world against it to colour the
     * same-world rows green, so a stubbed 0 made every row yellow and the
     * header read "World 0". */
    CS2VM_HOST_REQUEST_MAP_WORLD,

    /* IF_SETONFRIENDTRANSMIT (2420). Shares the SetOnOp payload with every
     * other listener registration; it is a distinct kind only so the slot
     * resolver can hand back runtime_hooks.on_friend_transmit. Like the misc
     * channel it carries no trigger list — one dirty flag re-runs every
     * registered hook. */
    CS2VM_HOST_REQUEST_IF_SETONFRIENDTRANSMIT,
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

/** Chat filter get/set and the private send (5000/5001/5005/5009/5016).
 *  `name`/`text` are borrowed for CHAT_SENDPRIVATE only. */
struct CS2VM_HostRequest_Chat
{
    int opcode;
    int public_mode;
    int private_mode;
    int trade_mode;
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
};

struct CS2VM_HostRequest_Db
{
    int opcode;    /* the DB_* opcode (7500..7510) */
    int load_kind; /* enum CS2VM_DbLoadKind — what a pending yield awaits */
    int load_id;   /* row id (LOAD_ROW) or table id (LOAD_INDEX) */
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
#define CS2VM_SETON_STR_ARG_MAX 4
#define CS2VM_SETON_STR_ARG_LEN 80

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

struct CS2VM_HostRequest_OC_Name
{
    int item_id;
};

struct CS2VM_HostRequest_OC_Unplaceholder
{
    int item_id;
};

/** OC_OP/OC_IOP shared payload. `opcode` distinguishes ground vs inventory;
 *  `op_index` is the menu slot (0..4), carried in from the opcode's baked-in
 *  operand rather than popped off the stack. */
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
 *  (args[0] pushed first); the widest variant (NPC_SETUP / OBJTYPE_SETUP) pops
 *  5. `query` marks the GET variants, which must push a bool result. */
#define CS2VM_HIGHLIGHT_ARG_MAX 5
struct CS2VM_HostRequest_Highlight
{
    int opcode;
    int args[CS2VM_HIGHLIGHT_ARG_MAX];
    int arg_count;
    bool query;
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

struct CS2VM_HostRequest
{
    enum CS2VM_HostRequestKind kind;

    union
    {
        struct CS2VM_HostRequest_PushScript push_script;
        struct CS2VM_HostRequest_InvSize invs_get_size;
        struct CS2VM_HostRequest_InvGetObj invs_get_obj;
        struct CS2VM_HostRequest_InvGetNum invs_get_num;
        struct CS2VM_HostRequest_InvTotal invs_get_total;
        struct CS2VM_HostRequest_VarsReadVarp vars_read_varp;
        struct CS2VM_HostRequest_VarsReadVarbit vars_read_varbit;
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
        struct CS2VM_HostRequest_OC_Name oc_name;
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
        struct CS2VM_HostRequest_IF_SetOpSubmenu if_set_op_submenu;
        struct CS2VM_HostRequest_IF_SetTargetPriority if_set_target_priority;
        struct CS2VM_HostRequest_IF_ClearOps if_clear_ops;
        struct CS2VM_HostRequest_IF_CallOnResize if_call_on_resize;
        struct CS2VM_HostRequest_IF_ClearOpSubmenu if_clear_op_submenu;
        struct CS2VM_HostRequest_IF_SetObject if_set_object;
        struct CS2VM_HostRequest_IF_SetScrollPos cc_set_scroll_pos;
        struct CS2VM_HostRequest_IF_SetScrollSize cc_set_scroll_size;
        struct CS2VM_HostRequest_WidgetSetInt widget_set_int;
        struct CS2VM_HostRequest_WidgetSetInt2 widget_set_int2;
        struct CS2VM_HostRequest_WidgetSetModelAngle widget_set_model_angle;
        struct CS2VM_HostRequest_WidgetSetArc widget_set_arc;
        struct CS2VM_HostRequest_WidgetSetModel widget_set_model;
        struct CS2VM_HostRequest_WidgetSetModelKind widget_set_model_kind;
        struct CS2VM_HostRequest_WidgetInputInt widget_input_int;
        struct CS2VM_HostRequest_TargetFind cc_findroot;
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
        struct CS2VM_HostRequest_Minimenu minimenu;
        struct CS2VM_HostRequest_ClientOption client_option;
        struct CS2VM_HostRequest_Minimap minimap;
        struct CS2VM_HostRequest_LocalNotification local_notification;
        struct CS2VM_HostRequest_Viewport viewport;
        struct CS2VM_HostRequest_UiZoom uizoom;
        struct CS2VM_HostRequest_SafeArea safearea;
        struct CS2VM_HostRequest_ClientOp clientop;
        struct CS2VM_HostRequest_Social social;
        struct CS2VM_HostRequest_Chat chat;
        struct CS2VM_HostRequest_ResumeCountDialog resume_countdialog;
        struct CS2VM_HostRequest_IF_SetOnOp if_set_on_friend_transmit;
    } u;
};

struct CS2VM2_Thread;

typedef int (*CS2VM2_HostExec_Fn)(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request);

#endif /* CS2VM2_HOST_H */
