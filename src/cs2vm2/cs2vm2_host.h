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
    CS2VM_HOST_REQUEST_IF_SETHIDE,
    CS2VM_HOST_REQUEST_IF_SETPOSITION,
    CS2VM_HOST_REQUEST_IF_SETSIZE,
    CS2VM_HOST_REQUEST_IF_SETSCROLLPOS,
    CS2VM_HOST_REQUEST_IF_SETSCROLLSIZE,
    CS2VM_HOST_REQUEST_IF_SETGRAPHIC,
    CS2VM_HOST_REQUEST_IF_SETTEXT,
    CS2VM_HOST_REQUEST_IF_SETOUTLINE,
    CS2VM_HOST_REQUEST_IF_SETONVARTRANSMIT,
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
    CS2VM_HOST_REQUEST_IF_SETOBJECT,
    // OC Object config
    CS2VM_HOST_REQUEST_OC_PARAM,
    CS2VM_HOST_REQUEST_OC_NAME,
    CS2VM_HOST_REQUEST_OC_UNPLACEHOLDER,
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
    CS2VM_HOST_REQUEST_IF_FIND,
    CS2VM_HOST_REQUEST_IF_GETX,
    CS2VM_HOST_REQUEST_IF_GETTEXT,
    CS2VM_HOST_REQUEST_IF_GETSCROLLWIDTH,
    CS2VM_HOST_REQUEST_OC_INT_PARAM,
    CS2VM_HOST_REQUEST_CLIENTCLOCK,
    /* The whole 6600..6640 world map family and the 6693..6699 map element
     * family go through one kind each: they share a single state object, so
     * forty request kinds would only spread one switch across three files. */
    CS2VM_HOST_REQUEST_WORLDMAP,
    CS2VM_HOST_REQUEST_MEC,
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

struct CS2VM_HostRequest_IF_SetOnVarTransmit
{
    int component_id;
    int script_id;
    char* signature;
    int* trigger_ids;
    int trigger_count;
    int int_args[32];
    int int_arg_count;
    uint32_t str_arg_mask;
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
    int int_args[32];
    int int_arg_count;
    uint32_t str_arg_mask;
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
    int int_args[32];
    int int_arg_count;
    uint32_t str_arg_mask;
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
    int int_args[32];
    int int_arg_count;
    uint32_t str_arg_mask;
    int str_arg_count;
    char str_args[CS2VM_SETON_STR_ARG_MAX][CS2VM_SETON_STR_ARG_LEN];
};

struct CS2VM_HostRequest_IF_ClearOps
{
    int component_id;
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
};

struct CS2VM_HostRequest_CC_GetId
{
    int component_id;
};

struct CS2VM_HostRequest_IF_SetObject
{
    int component_id;
    int obj_id;
    int count;
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
        struct CS2VM_HostRequest_CC_DeleteAll cc_delete_all;
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
        struct CS2VM_HostRequest_ParaHeight para_height;
        struct CS2VM_HostRequest_IF_GetWidth if_get_width;
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
        struct CS2VM_HostRequest_TargetFind if_find;
        struct CS2VM_HostRequest_IF_GetWidth if_getx;
        struct CS2VM_HostRequest_IF_GetWidth if_gettext;
        struct CS2VM_HostRequest_IF_GetWidth if_getscrollwidth;
        struct CS2VM_HostRequest_OC_IntParam oc_int_param;
        struct CS2VM_HostRequest_WorldMap worldmap;
        struct CS2VM_HostRequest_MEC mec;
    } u;
};

struct CS2VM2_Thread;

typedef int (*CS2VM2_HostExec_Fn)(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request);

#endif /* CS2VM2_HOST_H */
