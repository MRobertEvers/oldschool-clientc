"""Per-opcode documentation for CS2 opcodes implemented in cs2vm.c and cs2_host_ui.c.

Stack args are listed bottom -> top (top is popped first).
"""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class OpcodeDoc:
    summary: str
    int_in: tuple[str, ...] = ()
    str_in: tuple[str, ...] = ()
    int_out: tuple[str, ...] = ()
    str_out: tuple[str, ...] = ()
    operand: str | None = None
    notes: str | None = None


OPCODE_DOCS: dict[str, OpcodeDoc] = {
    # --- VM core (cs2vm.c) ---
    "PUSH_CONSTANT_INT": OpcodeDoc(
        summary="Push int constant",
        operand="int constant",
        int_out=("operand",),
    ),
    "PUSH_CONSTANT_STRING": OpcodeDoc(
        summary="Push string constant",
        operand="string index",
        str_out=("script_string[operand]",),
    ),
    "PUSH_VAR": OpcodeDoc(
        summary="Read varp",
        operand="varp id",
        int_out=("varp[operand]",),
    ),
    "POP_VAR": OpcodeDoc(
        summary="Write varp",
        operand="varp id",
        int_in=("value",),
        notes="varp[operand] = value",
    ),
    "PUSH_VARBIT": OpcodeDoc(
        summary="Read varbit",
        operand="varbit id",
        int_out=("varbit[operand]",),
    ),
    "POP_VARBIT": OpcodeDoc(
        summary="Write varbit",
        operand="varbit id",
        int_in=("value",),
        notes="varbit[operand] = value",
    ),
    "PUSH_VARC_INT": OpcodeDoc(
        summary="Read client int varc",
        operand="varc id",
        int_out=("varc_int[operand]",),
    ),
    "POP_VARC_INT": OpcodeDoc(
        summary="Write client int varc",
        operand="varc id",
        int_in=("value",),
        notes="varc_int[operand] = value",
    ),
    "PUSH_VARC_STRING": OpcodeDoc(
        summary="Read client string varc",
        operand="varc id",
        str_out=("varc_string[operand]",),
    ),
    "POP_VARC_STRING": OpcodeDoc(
        summary="Write client string varc",
        operand="varc id",
        str_in=("value",),
        notes="varc_string[operand] = value",
    ),
    "PUSH_INT_LOCAL": OpcodeDoc(
        summary="Push int local",
        operand="local index",
        int_out=("int_local[operand]",),
    ),
    "POP_INT_LOCAL": OpcodeDoc(
        summary="Pop int local",
        operand="local index",
        int_in=("value",),
        notes="int_local[operand] = value",
    ),
    "PUSH_STRING_LOCAL": OpcodeDoc(
        summary="Push string local",
        operand="local index",
        str_out=("str_local[operand]",),
    ),
    "POP_STRING_LOCAL": OpcodeDoc(
        summary="Pop string local",
        operand="local index",
        str_in=("value",),
        notes="str_local[operand] = value",
    ),
    "JOIN_STRING": OpcodeDoc(
        summary="Concatenate strings",
        str_in=("a", "b"),
        str_out=("a + b",),
    ),
    "POP_INT_DISCARD": OpcodeDoc(
        summary="Discard int",
        operand="repeat count",
        int_in=("value",),
        notes="discards operand times",
    ),
    "POP_STRING_DISCARD": OpcodeDoc(
        summary="Discard string",
        operand="repeat count",
        str_in=("value",),
        notes="discards operand times",
    ),
    "BRANCH": OpcodeDoc(
        summary="Unconditional branch",
        operand="branch offset",
        notes="pc += operand",
    ),
    "BRANCH_NOT": OpcodeDoc(
        summary="Branch if false",
        int_in=("cond",),
        operand="branch offset",
        notes="pc += operand if cond == 0",
    ),
    "BRANCH_EQUALS": OpcodeDoc(
        summary="Branch if equal",
        int_in=("a", "b"),
        operand="branch offset",
        notes="pc += operand if a == b",
    ),
    "BRANCH_LESS_THAN": OpcodeDoc(
        summary="Branch if less",
        int_in=("a", "b"),
        operand="branch offset",
        notes="pc += operand if a < b",
    ),
    "BRANCH_GREATER_THAN": OpcodeDoc(
        summary="Branch if greater",
        int_in=("a", "b"),
        operand="branch offset",
        notes="pc += operand if a > b",
    ),
    "BRANCH_LESS_THAN_OR_EQUALS": OpcodeDoc(
        summary="Branch if less than or equal",
        int_in=("a", "b"),
        operand="branch offset",
        notes="pc += operand if a <= b",
    ),
    "BRANCH_GREATER_THAN_OR_EQUALS": OpcodeDoc(
        summary="Branch if greater than or equal",
        int_in=("a", "b"),
        operand="branch offset",
        notes="pc += operand if a >= b",
    ),
    "SWITCH": OpcodeDoc(
        summary="Switch jump",
        int_in=("key",),
        notes="pc = case target or fall through",
    ),
    "ADD": OpcodeDoc(
        summary="Integer add",
        int_in=("a", "b"),
        int_out=("a + b",),
    ),
    "SUB": OpcodeDoc(
        summary="Integer subtract",
        int_in=("a", "b"),
        int_out=("a - b",),
    ),
    "MULTIPLY": OpcodeDoc(
        summary="Integer multiply",
        int_in=("a", "b"),
        int_out=("a * b",),
    ),
    "DIV": OpcodeDoc(
        summary="Integer divide",
        int_in=("a", "b"),
        int_out=("a / b",),
        notes="0 if b == 0",
    ),
    "MOD": OpcodeDoc(
        summary="Integer modulo",
        int_in=("a", "b"),
        int_out=("a % b",),
        notes="0 if b == 0",
    ),
    "AND": OpcodeDoc(
        summary="Bitwise and",
        int_in=("a", "b"),
        int_out=("a & b",),
    ),
    "OR": OpcodeDoc(
        summary="Bitwise or",
        int_in=("a", "b"),
        int_out=("a | b",),
    ),
    "TESTBIT": OpcodeDoc(
        summary="Test bit set",
        int_in=("value", "bit"),
        int_out=("(value & (1 << bit)) != 0",),
    ),
    "GOSUB_WITH_PARAMS": OpcodeDoc(
        summary="Call script",
        int_in=("callee args per signature",),
        int_out=("callee returns per signature",),
    ),
    "DEFINE_ARRAY": OpcodeDoc(
        summary="Define int array",
        operand="array id",
        int_in=("size",),
        notes="creates array[operand]",
    ),
    "PUSH_ARRAY_INT": OpcodeDoc(
        summary="Read array element",
        operand="array id",
        int_in=("index",),
        int_out=("array[operand][index]",),
    ),
    "POP_ARRAY_INT": OpcodeDoc(
        summary="Write array element",
        operand="array id",
        int_in=("index", "value"),
        notes="array[operand][index] = value",
    ),
    "ENUM": OpcodeDoc(
        summary="Enum lookup",
        int_in=("input_type", "output_type", "enum_id", "key"),
        int_out=("mapped value or -1",),
    ),
    "RETURN": OpcodeDoc(
        summary="Return from script",
        int_in=("return value (optional)",),
        notes="pops frame",
    ),
    # --- Host widget create/find (cs2_host_ui.c) ---
    "CC_CREATE": OpcodeDoc(
        summary="Create dynamic child component",
        operand="0 = active component, 1 = dot component",
        int_in=("parent", "type", "child_index", "is_nested"),
        notes=(
            "sets active to new child. Fixed CC_CREATE to take 4 args\n"
            "(rev ≥ 200 / osrs230), which was breaking stack analysis.\n"
            "Older revisions (< 200) used 3 args [parent, type, child_index]."
        ),
    ),
    "CC_DELETEALL": OpcodeDoc(
        summary="Delete all dynamic children of active parent",
    ),
    "CC_FIND": OpcodeDoc(
        summary="Find child by sub-id",
        int_in=("parent", "sub"),
        int_out=("1 if found (active set) else 0",),
    ),
    "IF_FIND": OpcodeDoc(
        summary="Find interface component",
        int_in=("component",),
        int_out=("1 if found (active set) else 0",),
    ),
    # --- CC setters (active component) ---
    "CC_SETHIDE": OpcodeDoc(
        summary="Hide active child",
        int_in=("hide",),
    ),
    "CC_SETTEXT": OpcodeDoc(
        summary="Set text on child component",
        operand="0 = active component, 1 = dot component",
        str_in=("text",),
    ),
    "CC_SETTEXTFONT": OpcodeDoc(
        summary="Set font on active child",
        int_in=("font_id",),
    ),
    "CC_SETTEXTALIGN": OpcodeDoc(
        summary="Set text alignment",
        int_in=("x_align", "y_align", "line_height"),
    ),
    "CC_SETTEXTSHADOW": OpcodeDoc(
        summary="Set text shadow",
        int_in=("shadowed",),
    ),
    "CC_SETCOLOUR": OpcodeDoc(
        summary="Set colour on active child",
        int_in=("colour",),
    ),
    "CC_SETFILL": OpcodeDoc(
        summary="Set rectangle fill",
        int_in=("filled",),
    ),
    "CC_SETARC": OpcodeDoc(
        summary="Set arc start/end angle on active child",
        int_in=("start_angle", "end_angle"),
        notes="widget type 10; 65536 = a full turn, 0 = straight up, clockwise",
    ),
    "IF_SETARC": OpcodeDoc(
        summary="Set arc start/end angle",
        int_in=("start_angle", "end_angle", "component"),
        notes="widget type 10; 65536 = a full turn, 0 = straight up, clockwise",
    ),
    "CC_SETTRANS": OpcodeDoc(
        summary="Set transparency",
        operand="0 = active component, 1 = dot component",
        int_in=("trans",),
        notes="0-255 opacity (255 = fully transparent)",
    ),
    "CC_SETNOCLICKTHROUGH": OpcodeDoc(
        summary="Set no-click-through",
        operand="0 = active component, 1 = dot component",
        int_in=("enabled",),
    ),
    "CC_SETOP": OpcodeDoc(
        summary="Set right-click op",
        operand="0 = active component, 1 = dot component",
        str_in=("text",),
        int_in=("index",),
        notes="index 1-10 maps to actions[index - 1]",
    ),
    "CC_SETOPBASE": OpcodeDoc(
        summary="Set context-menu target label (op base)",
        str_in=("text",),
        notes=(
            "Op base is the right-hand target text in Choose Option menus "
            "(e.g. spell name on Cast). Stored as widget opBase."
        ),
    ),
    "CC_SETTARGETVERB": OpcodeDoc(
        summary="Set target verb (stub)",
        str_in=("text",),
    ),
    "CC_CLEAROPS": OpcodeDoc(
        summary="Clear all ops on active child",
    ),
    "CC_SETDRAGGABLE": OpcodeDoc(
        summary="Set draggable parent/child",
        int_in=("parent_uid", "child_index"),
    ),
    "CC_SETDRAGGABLEBEHAVIOR": OpcodeDoc(
        summary="Set drag behavior",
        operand="0 = active component, 1 = dot component",
        int_in=("behavior",),
        notes="behavior 1 sets isScrollBar",
    ),
    "CC_SETDRAGDEADZONE": OpcodeDoc(
        summary="Set drag dead zone pixels",
        int_in=("zone",),
    ),
    "CC_SETDRAGDEADTIME": OpcodeDoc(
        summary="Set drag dead time ms",
        int_in=("time",),
    ),
    "CC_SETGRAPHIC": OpcodeDoc(
        summary="Set sprite graphic",
        int_in=("graphic_id",),
    ),
    "CC_SETPOSITION": OpcodeDoc(
        summary="Set position modes",
        int_in=("x", "y", "x_mode", "y_mode"),
    ),
    "CC_SETSIZE": OpcodeDoc(
        summary="Set size modes",
        int_in=("w", "h", "w_mode", "h_mode"),
    ),
    "CC_SETTILING": OpcodeDoc(
        summary="Set sprite tiling",
        int_in=("tiled",),
    ),
    "CC_SETOUTLINE": OpcodeDoc(
        summary="Set sprite outline",
        int_in=("outline",),
    ),
    "CC_SETGRAPHICSHADOW": OpcodeDoc(
        summary="Set sprite shadow",
        int_in=("shadow",),
    ),
    "CC_SETOBJECT": OpcodeDoc(
        summary="Set object icon on active child",
        int_in=("obj_count", "obj_id"),
    ),
    "CC_SETOBJECT_ALWAYS_NUM": OpcodeDoc(
        summary="Set object icon always showing qty",
        int_in=("obj_count", "obj_id"),
    ),
    "CC_SETOBJECT_NONUM": OpcodeDoc(
        summary="Set object icon without qty",
        int_in=("obj_id",),
    ),
    "CC_SETCOMPONENTPARAM": OpcodeDoc(
        summary="Write a param onto the component's runtime param table",
        operand="0 = active component, 1 = dot component",
        int_in=("param_id", "value", "kind"),
        notes=(
            "OldSchool-era, distinct from CC_GETPARAM (1613): the table lives on "
            "the component at runtime and starts empty (IF3 files carry no param "
            "section). VARIABLE ARITY, so the counts above are the kind == 0 case "
            "only: `kind` names the ParamType's type, and kind == 2 means the value "
            "was pushed on the STRING stack, leaving just (param_id, kind) on the "
            "int stack — script 9581 writes param 1017, declared `s`, that way. "
            "Popping three ints unconditionally steals an unrelated int from under "
            "it. Dedicated dispatch in cs2vm2.c handles both shapes, so this never "
            "reaches StackMetaStub with the wrong one."
        ),
    ),
    # --- CC getters (active component) ---
    "CC_GETCOMPONENTPARAM": OpcodeDoc(
        summary="Read a param off the component's runtime param table",
        operand="0 = active component, 1 = dot component",
        int_in=("param_id",),
        int_out=("value",),
        notes=(
            "Answers with what CC_SETCOMPONENTPARAM wrote, else the ParamType's "
            "default_int — which is what makes the scripts' `= -1` guards mean "
            "\"never tagged\"."
        ),
    ),
    "CC_GETID": OpcodeDoc(
        summary="Get active component id",
        int_out=("active_component",),
    ),
    "CC_GETX": OpcodeDoc(
        summary="Get relative X of child component",
        operand="0 = active component, 1 = dot component",
        int_out=("x",),
    ),
    "CC_GETY": OpcodeDoc(
        summary="Get relative Y of child component",
        operand="0 = active component, 1 = dot component",
        int_out=("y",),
    ),
    "CC_GETLAYER": OpcodeDoc(
        summary="Get parent component id",
        int_out=("parent_id",),
    ),
    "CC_GETOP": OpcodeDoc(
        summary="Get op text",
        int_in=("op_index",),
        str_out=("op text",),
    ),
    "CC_GETOPBASE": OpcodeDoc(
        summary="Get op base text",
        str_out=("op_base",),
    ),
    "CC_GETTEXT": OpcodeDoc(
        summary="Get text",
        str_out=("text",),
    ),
    "CC_GETWIDTH": OpcodeDoc(
        summary="Get layout width",
        operand="0 = active component, 1 = dot component",
        int_out=("width",),
    ),
    "CC_GETHEIGHT": OpcodeDoc(
        summary="Get layout height",
        operand="0 = active component, 1 = dot component",
        int_out=("height",),
    ),
    "CC_GETHIDE": OpcodeDoc(
        summary="Get hidden flag",
        int_out=("hidden ? 1 : 0",),
    ),
    "CC_GETSCROLLX": OpcodeDoc(
        summary="Get scroll X (stub)",
        int_out=("0",),
    ),
    "CC_GETSCROLLY": OpcodeDoc(
        summary="Get scroll Y (stub)",
        int_out=("0",),
    ),
    "CC_GETSCROLLWIDTH": OpcodeDoc(
        summary="Get scroll content width",
        int_out=("scroll_width",),
    ),
    "CC_GETSCROLLHEIGHT": OpcodeDoc(
        summary="Get scroll content height",
        int_out=("scroll_height",),
    ),
    # --- IF setters ---
    "IF_SETNOCLICKTHROUGH": OpcodeDoc(
        summary="Set no-click-through",
        int_in=("component", "enabled"),
    ),
    "IF_SETTRANS": OpcodeDoc(
        summary="Set transparency",
        int_in=("component", "trans"),
    ),
    "IF_SETOP": OpcodeDoc(
        summary="Set op text",
        str_in=("text",),
        int_in=("index", "component"),
    ),
    "IF_SETDRAGGABLE": OpcodeDoc(
        summary="Set draggable",
        int_in=("component", "parent_uid", "child_index"),
    ),
    "IF_SETDRAGGABLEBEHAVIOR": OpcodeDoc(
        summary="Set drag behavior",
        int_in=("component", "behavior"),
    ),
    "IF_SETHIDE": OpcodeDoc(
        summary="Set hidden",
        int_in=("component", "hide"),
    ),
    "IF_SETTEXT": OpcodeDoc(
        summary="Set text",
        str_in=("text",),
        int_in=("component",),
    ),
    "IF_SETTEXTFONT": OpcodeDoc(
        summary="Set font",
        int_in=("component", "font_id"),
    ),
    "IF_SETTEXTALIGN": OpcodeDoc(
        summary="Set text alignment",
        int_in=("x_align", "y_align", "line_height", "component"),
    ),
    "IF_SETTEXTSHADOW": OpcodeDoc(
        summary="Set text shadow",
        int_in=("component", "shadow"),
    ),
    "IF_SETFILL": OpcodeDoc(
        summary="Set fill",
        int_in=("component", "filled"),
    ),
    "IF_SETGRAPHIC": OpcodeDoc(
        summary="Set graphic",
        int_in=("graphic_id", "component"),
    ),
    "IF_SETCOLOUR": OpcodeDoc(
        summary="Set colour",
        int_in=("component", "colour"),
    ),
    "IF_SETOUTLINE": OpcodeDoc(
        summary="Set sprite outline (border type)",
        int_in=("outline", "component"),
        notes="Sets widget borderType for sprite outline rendering.",
    ),
    "IF_SETPOSITION": OpcodeDoc(
        summary="Set position",
        int_in=("x", "y", "x_mode", "y_mode", "component"),
        notes="component is top of int stack.",
    ),
    "IF_SETSIZE": OpcodeDoc(
        summary="Set size",
        int_in=("width", "height", "width_mode", "height_mode", "component"),
        notes="component is top of int stack.",
    ),
    "IF_SETSCROLLSIZE": OpcodeDoc(
        summary="Set scroll size",
        int_in=("scroll_h", "scroll_w", "component"),
    ),
    "IF_SETSCROLLPOS": OpcodeDoc(
        summary="Set scroll position",
        int_in=("scroll_y", "scroll_x", "component"),
    ),
    "IF_SETOBJECT": OpcodeDoc(
        summary="Set object icon",
        int_in=("obj_id", "obj_count", "component"),
    ),
    "IF_SETOBJECT_ALWAYS_NUM": OpcodeDoc(
        summary="Set object icon always showing qty",
        int_in=("obj_id", "obj_count", "component"),
    ),
    "IF_SETOBJECT_NONUM": OpcodeDoc(
        summary="Set object icon without qty",
        int_in=("obj_id", "obj_count", "component"),
    ),
    "IF_SETMODEL": OpcodeDoc(
        summary="Set model id",
        int_in=("model_id", "component"),
    ),
    "IF_SETMODELTRANSPARENT": OpcodeDoc(
        summary="Set model transparency",
        int_in=("transparent", "component"),
    ),
    "IF_SETOPBASE": OpcodeDoc(
        summary="Set context-menu target label (op base)",
        str_in=("text",),
        int_in=("component",),
        notes=(
            "Op base is the right-hand target text in Choose Option menus "
            "(e.g. spell name on Cast). Stored as widget opBase."
        ),
    ),
    "IF_SETTARGETVERB": OpcodeDoc(
        summary="Set target verb (stub)",
        str_in=("text",),
        int_in=("component",),
    ),
    "IF_CLEAROPS": OpcodeDoc(
        summary="Clear ops",
        int_in=("component",),
    ),
    "IF_SETOPSUBMENU": OpcodeDoc(
        summary="Set op submenu label",
        str_in=("text",),
        int_in=("op_index", "sub_index", "component"),
        notes="op_index and sub_index are 1-based in script",
    ),
    "IF_SETTARGETPRIORITY": OpcodeDoc(
        summary="Set target priority (stub)",
        int_in=("component", "priority"),
    ),
    "IF_SETDRAGDEADZONE": OpcodeDoc(
        summary="Set drag dead zone",
        int_in=("component", "zone"),
    ),
    "IF_SETDRAGDEADTIME": OpcodeDoc(
        summary="Set drag dead time",
        int_in=("component", "time"),
    ),
    # --- IF getters ---
    "IF_GETWIDTH": OpcodeDoc(
        summary="Get layout width",
        int_in=("component",),
        int_out=("width",),
    ),
    "IF_GETHEIGHT": OpcodeDoc(
        summary="Get layout height",
        int_in=("component",),
        int_out=("height",),
    ),
    "IF_GETX": OpcodeDoc(
        summary="Get relative X",
        int_in=("component",),
        int_out=("x",),
    ),
    "IF_GETY": OpcodeDoc(
        summary="Get relative Y",
        int_in=("component",),
        int_out=("y",),
    ),
    "IF_GETLAYER": OpcodeDoc(
        summary="Get parent component id",
        int_in=("component",),
        int_out=("parent_id",),
    ),
    "IF_GETSCROLLHEIGHT": OpcodeDoc(
        summary="Get scroll content height",
        int_in=("component",),
        int_out=("scroll_height",),
    ),
    "IF_GETHIDE": OpcodeDoc(
        summary="Get hidden flag",
        int_in=("component",),
        int_out=("hidden ? 1 : 0",),
    ),
    "IF_GETSCROLLX": OpcodeDoc(
        summary="Get scroll X (stub)",
        int_in=("component",),
        int_out=("0",),
    ),
    "IF_GETSCROLLY": OpcodeDoc(
        summary="Get scroll Y (stub)",
        int_in=("component",),
        int_out=("0",),
    ),
    "IF_GETSCROLLWIDTH": OpcodeDoc(
        summary="Get scroll content width",
        int_in=("component",),
        int_out=("scroll_width",),
    ),
    "IF_GETTOP": OpcodeDoc(
        summary="Get root component id",
        int_out=("top component or -1",),
    ),
    "IF_GETOP": OpcodeDoc(
        summary="Get op text",
        int_in=("component", "op_index"),
        str_out=("op text",),
    ),
    "IF_GETOPBASE": OpcodeDoc(
        summary="Get op base text",
        int_in=("component",),
        str_out=("op_base",),
    ),
    "IF_GETTEXT": OpcodeDoc(
        summary="Get text",
        int_in=("component",),
        str_out=("text",),
    ),
    # --- Inventory ---
    "INV_GETOBJ": OpcodeDoc(
        summary="Get inventory item id",
        int_in=("inv_id", "slot"),
        int_out=("obj_id or -1 if empty",),
    ),
    "INV_GETNUM": OpcodeDoc(
        summary="Get inventory stack size",
        int_in=("inv_id", "slot"),
        int_out=("count",),
    ),
    "INV_SIZE": OpcodeDoc(
        summary="Get inventory capacity",
        int_in=("inv_id",),
        int_out=("slot_count",),
    ),
    "INV_TOTAL": OpcodeDoc(
        summary="Count item across inventory",
        int_in=("inv_id", "item_id"),
        int_out=("total qty",),
    ),
    # --- SETON hooks ---
    "CC_SETONINVTRANSMIT": OpcodeDoc(
        summary="Register inv-transmit script on active child",
        operand="script id",
        notes="signature args on stack",
    ),
    "CC_SETONCLICK": OpcodeDoc(
        summary="Set onClick handler on active child",
        operand="0 = active component, 1 = dot component",
        notes="signature args on stack",
    ),
    "CC_SETONHOLD": OpcodeDoc(
        summary="Set onHold handler on active child",
        operand="0 = active component, 1 = dot component",
        notes="signature args on stack",
    ),
    "CC_SETONMOUSEOVER": OpcodeDoc(
        summary="Set onMouseOver handler on active child",
        operand="0 = active component, 1 = dot component",
        notes="signature args on stack",
    ),
    "CC_SETONMOUSELEAVE": OpcodeDoc(
        summary="Set onMouseLeave handler on active child",
        operand="0 = active component, 1 = dot component",
        notes="signature args on stack",
    ),
    "CC_SETONDRAG": OpcodeDoc(
        summary="Set onDrag handler on active child",
        operand="0 = active component, 1 = dot component",
        notes="signature args on stack",
    ),
    "CC_SETONSCROLLWHEEL": OpcodeDoc(
        summary="Set onScroll handler on active child",
        operand="0 = active component, 1 = dot component",
        notes=(
            "Stores onScroll (not onScrollWheel); wheel in opcode name is the trigger.\n"
            "signature args on stack"
        ),
    ),
    "CC_SETONKEY": OpcodeDoc(
        summary="Set onKey handler on active child",
        operand="0 = active component, 1 = dot component",
        notes="signature args on stack",
    ),
    "CC_SETONOP": OpcodeDoc(
        summary="Set onOp handler on active child",
        operand="0 = active component, 1 = dot component",
        notes="signature args on stack",
    ),
    "CC_SETONDRAGCOMPLETE": OpcodeDoc(
        summary="Set onDragComplete handler on active child",
        operand="0 = active component, 1 = dot component",
        notes="signature args on stack",
    ),
    "IF_SETONINVTRANSMIT": OpcodeDoc(
        summary="Register inv-transmit script",
        operand="script id",
        int_in=("component",),
        notes="signature args on stack",
    ),
    "IF_SETONMOUSEOVER": OpcodeDoc(
        summary="Set onMouseOver handler",
        int_in=("component",),
        notes="signature args on stack",
    ),
    "IF_SETONTIMER": OpcodeDoc(
        summary="Set onTimer handler",
        int_in=("component",),
        notes="signature args on stack",
    ),
    "IF_SETONSCROLLWHEEL": OpcodeDoc(
        summary="Set onScroll handler",
        int_in=("component",),
        notes=(
            "Stores onScroll (not onScrollWheel); wheel in opcode name is the trigger.\n"
            "signature args on stack"
        ),
    ),
    # --- String/math host ---
    "TOSTRING": OpcodeDoc(
        summary="Int to string",
        int_in=("value",),
        str_out=("decimal string",),
    ),
    "APPEND": OpcodeDoc(
        summary="Append string",
        str_in=("dest", "src"),
        str_out=("dest + src",),
    ),
    "APPEND_NUM": OpcodeDoc(
        summary="Append int as decimal",
        str_in=("dest",),
        int_in=("value",),
        str_out=("dest + value",),
    ),
    "APPEND_SIGNNUM": OpcodeDoc(
        summary="Append signed int",
        str_in=("dest",),
        int_in=("value",),
        str_out=("dest + signed value",),
    ),
    "APPEND_CHAR": OpcodeDoc(
        summary="Append char",
        str_in=("dest",),
        int_in=("ch",),
        str_out=("dest + char",),
    ),
    "STRING_LENGTH": OpcodeDoc(
        summary="String length",
        str_in=("str",),
        int_out=("strlen",),
    ),
    "PARAWIDTH": OpcodeDoc(
        summary="Wrapped max line width",
        str_in=("text",),
        int_in=("max_width", "font_id"),
        int_out=("pixels",),
    ),
    "PARAHEIGHT": OpcodeDoc(
        summary="Wrapped line count",
        str_in=("text",),
        int_in=("max_width", "font_id"),
        int_out=("lines",),
    ),
    "SCALE": OpcodeDoc(
        summary="Scale interpolate (c*a)/b",
        int_in=("a", "b", "c"),
        int_out=("result",),
        notes="b == 0 yields 0",
    ),
    "INTERPOLATE": OpcodeDoc(
        summary="Linear interpolate",
        int_in=("a", "b", "c", "d", "e"),
        int_out=("a + (b - a) * (e - c) / (d - c)",),
    ),
    "RANDOMINC": OpcodeDoc(
        summary="Random inclusive",
        int_in=("max",),
        int_out=("rand() % (max + 1)",),
    ),
    "CLEARBIT": OpcodeDoc(
        summary="Clear bit in value",
        int_in=("value", "bit"),
        int_out=("value with bit cleared",),
    ),
    "SETBIT": OpcodeDoc(
        summary="Set bit in value",
        int_in=("value", "bit"),
        int_out=("value with bit set",),
    ),
    # --- Config / client ---
    "OC_UNPLACEHOLDER": OpcodeDoc(
        summary="Resolve placeholder item",
        int_in=("item_id",),
        int_out=("underlying id or item_id",),
    ),
    "RUNWEIGHT_VISIBLE": OpcodeDoc(
        summary="Get visible player weight",
        int_out=("weight kg (0 default)",),
    ),
    "CLIENTCLOCK": OpcodeDoc(
        summary="Get client tick counter",
        int_out=("clock_ticks",),
    ),
    "COORD": OpcodeDoc(
        summary="Get player coord packed (stub)",
        int_out=("0",),
    ),
    "COORDX": OpcodeDoc(
        summary="Extract X from packed coord",
        int_in=("packed",),
        int_out=("(packed >> 14) & 0x3FFF",),
    ),
    "MAP_WORLD": OpcodeDoc(
        summary="Get world id (stub)",
        int_out=("301",),
    ),
    "MAP_MEMBERS": OpcodeDoc(
        summary="Is members world (stub)",
        int_out=("1",),
    ),
    "ON_MOBILE": OpcodeDoc(
        summary="Is mobile client (stub)",
        int_out=("0",),
    ),
    "CLIENTTYPE": OpcodeDoc(
        summary="Get client type",
        int_out=("client_type",),
        notes=(
            "Client type values (OSRS):\n"
            "  1 Desktop (legacy Java client)\n"
            "  2 Android\n"
            "  3 iOS\n"
            "  4 Enhanced (RuneLite / C++ client)\n"
            "  5 Mac\n"
            "  7 Mobile (generic)\n"
            " 10 Steam / enhanced variant"
        ),
    ),
    "ENUM_GETOUTPUTCOUNT": OpcodeDoc(
        summary="Get enum output count",
        int_in=("enum_id",),
        int_out=("count",),
    ),
    "STRUCT_PARAM": OpcodeDoc(
        summary="Read struct param",
        int_in=("struct_id", "param_id"),
        int_out=("param value",),
        str_out=("param value (string)",),
        notes="pushes int or str depending on param type",
    ),
    "CC_CLEAROPSUBMENU": OpcodeDoc(
        summary="Clear submenu entries for one op slot",
        operand="0 = active component, 1 = dot component",
        int_in=("op_index",),
        notes="op_index is 1-based (1..10).",
    ),
    "CC_COPY": OpcodeDoc(
        summary="Clone a dynamic child into another slot under the same parent",
        int_in=("parent", "src_sub", "dst_sub"),
        notes="sets active to the copy; used by the bank tab strip builder",
    ),
    "CC_OP1309": OpcodeDoc(
        summary="Client stub that discards one int",
        int_in=("value",),
    ),
    "CC_SETOPFORCELEFTCLICK": OpcodeDoc(
        summary="Force left-click to execute the op without opening the menu",
        operand="0 = active component, 1 = dot component",
        int_in=("flag",),
        notes="flag == 1 enables; any other value disables.",
    ),
    "CC_SETOPKEY": OpcodeDoc(
        summary="Bind up to 5 (keychar, keycode) pairs to an op slot on the active child",
        int_in=("opindex", "k0_char", "k0_code", "k1_char", "k1_code", "k2_char", "k2_code", "k3_char", "k3_code", "k4_char", "k4_code"),
        notes=(
            "opindex is 1-based (1..10). Pairs are read bottom-up and stop at the\n"
            "first negative keychar; a negative k0_char clears the slot."
        ),
    ),
    "CC_SETOPKEYIGNOREHELD": OpcodeDoc(
        summary="Suppress auto-repeat while the key is held",
        int_in=("opindex",),
    ),
    "CC_SETOPKEYRATE": OpcodeDoc(
        summary="Set auto-repeat rate for an op key slot",
        int_in=("opindex", "keyrate", "tickrate"),
        notes="tickrate == 0 disables repeat.",
    ),
    "CC_SETOPSUBMENU": OpcodeDoc(
        summary="Set a submenu entry label",
        operand="0 = active component, 1 = dot component",
        int_in=("op_index", "sub_index"),
        str_in=("text",),
        notes="op_index and sub_index are 1-based in script.",
    ),
    "CC_SETOPTKEY": OpcodeDoc(
        summary="Bind one (keychar, keycode) pair to the typed-key slot (op 10)",
        int_in=("keychar", "keycode"),
    ),
    "CC_SETOPTKEYIGNOREHELD": OpcodeDoc(
        summary="Suppress auto-repeat on the typed-key slot (op 10)",
    ),
    "CC_SETOPTKEYRATE": OpcodeDoc(
        summary="Set auto-repeat rate for the typed-key slot (op 10)",
        int_in=("tickrate", "keyrate"),
        notes=(
            "reference pops these in the opposite order to CC_SETOPKEYRATE. The\n"
            "values are unused downstream, so only the count matters; mirrored\n"
            "rather than \"corrected\" without a real-cache trace."
        ),
    ),
    "CC_SETTARGETPRIORITY": OpcodeDoc(
        summary="Set target priority",
        operand="0 = active component, 1 = dot component",
        int_in=("priority",),
        notes="-1 resets to 4; 1..32 stores value-1.",
    ),
    "CHAR_ISALPHA": OpcodeDoc(
        summary="ASCII letter?",
        int_in=("char",),
        int_out=("bool",),
    ),
    "CHAR_ISALPHANUMERIC": OpcodeDoc(
        summary="ASCII letter or digit?",
        int_in=("char",),
        int_out=("bool",),
    ),
    "CHAR_ISNUMERIC": OpcodeDoc(
        summary="ASCII digit?",
        int_in=("char",),
        int_out=("bool",),
    ),
    "CHAR_ISPRINTABLE": OpcodeDoc(
        summary="Can this character code be drawn?",
        int_in=("char",),
        int_out=("bool",),
        notes=(
            "printable ASCII + Latin-1 supplement, plus the cp1252 stragglers\n"
            "(euro, OE/oe, em dash, Y-diaeresis). Filters keyboard input."
        ),
    ),
    "COORDY": OpcodeDoc(
        summary="Extract the plane from packed coord. RuneScript's axes are x/y/z with y as the level, so \"y\" here is the plane and COORDZ is the second tile axis — not the other way round",
        int_in=("packed",),
        int_out=("(packed >> 28) & 0x3",),
    ),
    "COORDZ": OpcodeDoc(
        summary="Extract Z (the second tile axis) from packed coord",
        int_in=("packed",),
        int_out=("packed & 0x3FFF",),
    ),
    "ESCAPE": OpcodeDoc(
        summary="Neutralise markup so the text renders literally",
        str_in=("text",),
        str_out=("escaped text",),
        notes=(
            "'<' -> \"<lt>\", '>' -> \"<gt>\" — the escapes ToriDraw_Font decodes back.\n"
            "Scripts call this before showing player-supplied text so it cannot\n"
            "inject <col=...>/<img=...> formatting."
        ),
    ),
    "GETKEYINPUTMODE": OpcodeDoc(
        summary="Current keyboard capture mode",
        int_out=("mode",),
        notes="0 none, 1 keyboard, 2 interface-scoped, 3 widget-scoped.",
    ),
    "IF_INPUT_SETACCEPTMODE": OpcodeDoc(
        summary="",
        int_in=("value", "component"),
    ),
    "IF_INPUT_SETCHARFILTER": OpcodeDoc(
        summary="",
        int_in=("value", "component"),
    ),
    "IF_INPUT_SETCURSORCOLOUR": OpcodeDoc(
        summary="",
        int_in=("value", "component"),
    ),
    "IF_INPUT_SETCURSORHEIGHT": OpcodeDoc(
        summary="",
        int_in=("value", "component"),
    ),
    "IF_INPUT_SETCURSOROFFSET": OpcodeDoc(
        summary="",
        int_in=("value", "component"),
    ),
    "IF_INPUT_SETCURSORTRANS": OpcodeDoc(
        summary="",
        int_in=("value", "component"),
    ),
    "IF_INPUT_SETCURSORWIDTH": OpcodeDoc(
        summary="",
        int_in=("value", "component"),
    ),
    "IF_INPUT_SETLINECOUNTLIMIT": OpcodeDoc(
        summary="",
        int_in=("value", "component"),
    ),
    "IF_INPUT_SETLINEWIDTHLIMIT": OpcodeDoc(
        summary="",
        int_in=("value", "component"),
    ),
    "IF_INPUT_SETLINEWRAPPINGWIDTH": OpcodeDoc(
        summary="",
        int_in=("value", "component"),
    ),
    "IF_INPUT_SETSELECTBGCOLOUR": OpcodeDoc(
        summary="",
        int_in=("value", "component"),
    ),
    "IF_INPUT_SETSELECTCOLOUR": OpcodeDoc(
        summary="",
        int_in=("value", "component"),
    ),
    "IF_INPUT_SETSUBMITMODE": OpcodeDoc(
        summary="",
        int_in=("value", "component"),
    ),
    "IF_INPUT_SETWRAPMODE": OpcodeDoc(
        summary="",
        int_in=("value", "component"),
    ),
    "IF_OP2309": OpcodeDoc(
        summary="Client stub: pop component + one int and discard",
        int_in=("value", "component"),
    ),
    "IF_SETOPKEY": OpcodeDoc(
        summary="Bind one (keychar, keycode) pair to an op slot by component uid",
        int_in=("opindex", "keychar", "keycode", "component"),
        notes="component is on top (popped first). opindex is 1-based (1..10).",
    ),
    "IF_SETOPKEYIGNOREHELD": OpcodeDoc(
        summary="Suppress auto-repeat while the key is held",
        int_in=("opindex", "component"),
    ),
    "IF_SETOPKEYRATE": OpcodeDoc(
        summary="Set auto-repeat rate for an op key slot by component uid",
        int_in=("opindex", "keyrate", "tickrate", "component"),
        notes="tickrate == 0 disables repeat.",
    ),
    "IF_SETOPTKEY": OpcodeDoc(
        summary="Bind one (keychar, keycode) pair to the typed-key slot (op 10)",
        int_in=("keychar", "keycode", "component"),
    ),
    "IF_SETOPTKEYIGNOREHELD": OpcodeDoc(
        summary="Suppress auto-repeat on the typed-key slot (op 10)",
        int_in=("component",),
    ),
    "IF_SETOPTKEYRATE": OpcodeDoc(
        summary="Set auto-repeat rate for the typed-key slot (op 10)",
        int_in=("tickrate", "keyrate", "component"),
    ),
    "KEYHELD": OpcodeDoc(
        summary="Is the given key currently held down?",
        int_in=("keycode",),
        int_out=("held",),
        notes="keycode is an OSRS internal key code (see torirs_keymap.h), not ASCII.",
    ),
    "KEYPRESSED": OpcodeDoc(
        summary="Was the given key pressed during this frame?",
        int_in=("keycode",),
        int_out=("pressed",),
        notes="edge-triggered; cleared each frame. OSRS internal key code.",
    ),
    "LOCAL_NOTIFICATION": OpcodeDoc(
        summary="Schedule a local notification",
        int_in=("id", "delay_ms"),
        str_in=("title", "body"),
        int_out=("handle",),
        notes=(
            "pushes a handle for LOCAL_NOTIFICATION_CANCEL. script 5360\n"
            "(proc,local_notification) POP_INT_LOCALs it straight after the call, so\n"
            "the push is required or that store underflows."
        ),
    ),
    "LOCAL_NOTIFICATION_CANCEL": OpcodeDoc(
        summary="Cancel one scheduled notification",
        int_in=("handle",),
    ),
    "LOCAL_NOTIFICATION_CANCELALL": OpcodeDoc(
        summary="Cancel every scheduled notification",
    ),
    "LOCAL_NOTIFICATION_SUPPORTED": OpcodeDoc(
        summary="Are local notifications available?",
        int_out=("supported",),
    ),
    "MAX": OpcodeDoc(
        summary="Maximum of two ints.  int in: a, b (b = top)  int out: max(a, b)",
    ),
    "MEC_CATEGORY": OpcodeDoc(
        summary="Map element category (-1 when uncategorised)",
        int_in=("mecId",),
        int_out=("category",),
    ),
    "MEC_SPRITE": OpcodeDoc(
        summary="Map element icon sprite id (-1 when none)",
        int_in=("mecId",),
        int_out=("spriteId",),
    ),
    "MEC_TEXT": OpcodeDoc(
        summary="Map element label",
        int_in=("mecId",),
        str_out=("text",),
    ),
    "MEC_TEXTSIZE": OpcodeDoc(
        summary="Map element label size",
        int_in=("mecId",),
        int_out=("size",),
    ),
    "MIN": OpcodeDoc(
        summary="Minimum of two ints.  int in: a, b (b = top)  int out: min(a, b)",
    ),
    "MOBILE_BATTERYCHARGING": OpcodeDoc(
        summary="Is the device charging",
        int_out=("charging",),
        notes="Desktop is always on mains power.",
    ),
    "MOBILE_BATTERYLEVEL": OpcodeDoc(
        summary="Battery charge percentage (0..100)",
        int_out=("percent",),
        notes="Desktop has no battery; we report a full charge.",
    ),
    "MOBILE_WIFIAVAILABLE": OpcodeDoc(
        summary="Is a wifi (non-metered) connection available",
        int_out=("available",),
        notes="Desktop is treated as an unmetered connection.",
    ),
    "MOVECOORD": OpcodeDoc(
        summary="Offset a packed coord by (x, y, z), where y is the plane",
        int_in=("packed", "x", "plane", "z"),
        int_out=("packed + ((plane << 28) | (x << 14) | z)",),
    ),
    "SETKEYINPUTENABLED": OpcodeDoc(
        summary="Enable/disable keyboard capture (no-op here)",
        int_in=("enabled",),
    ),
    "SETKEYINPUTMODE_ALL": OpcodeDoc(
        summary="Release keyboard capture (input dialog type 0)",
        notes=(
            "takes no args despite the SET prefix — the doc comment is required to\n"
            "stop the generator's \"SET* pops one\" heuristic guessing wrong."
        ),
    ),
    "SETKEYINPUTMODE_KEYBOARD": OpcodeDoc(
        summary="Capture keyboard for a dialog (input dialog type 1)",
        notes="takes no args despite the SET prefix (see SETKEYINPUTMODE_ALL).",
    ),
    "UPPERCASE": OpcodeDoc(
        summary="ASCII-uppercase a string",
        str_in=("text",),
        str_out=("uppercased text",),
    ),
    "WORLDMAP_COORDINMAP": OpcodeDoc(
        summary="Does a map area cover a world coord",
        int_in=("mapId", "coord"),
        int_out=("inMap",),
    ),
    "WORLDMAP_DISABLEELEMENT": OpcodeDoc(
        summary="Enable/disable a single element",
        int_in=("elementId", "enabled"),
    ),
    "WORLDMAP_DISABLEELEMENTCATEGORY": OpcodeDoc(
        summary="Enable/disable an element category",
        int_in=("categoryId", "enabled"),
    ),
    "WORLDMAP_DISABLEELEMENTS": OpcodeDoc(
        summary="Master switch for drawing map elements",
        int_in=("enabled",),
    ),
    "WORLDMAP_ELEMENT": OpcodeDoc(
        summary="Element id of the map element event being handled",
        int_out=("elementId",),
    ),
    "WORLDMAP_ELEMENTCOORD": OpcodeDoc(
        summary="Second coord of the map element event",
        int_out=("coord",),
    ),
    "WORLDMAP_ELEMENTCOORD1": OpcodeDoc(
        summary="First coord of the map element event",
        int_out=("coord",),
    ),
    "WORLDMAP_FLASHELEMENT": OpcodeDoc(
        summary="Start flashing one map element",
        int_in=("elementId",),
    ),
    "WORLDMAP_FLASHELEMENTCATEGORY": OpcodeDoc(
        summary="Start flashing an element category",
        int_in=("categoryId",),
    ),
    "WORLDMAP_GETCONFIGBOUNDS": OpcodeDoc(
        summary="Tile bounds of a map area",
        int_in=("mapId",),
        int_out=("minX", "minY", "maxX", "maxY"),
    ),
    "WORLDMAP_GETCONFIGORIGIN": OpcodeDoc(
        summary="Packed origin coord of a map area",
        int_in=("mapId",),
        int_out=("coord",),
    ),
    "WORLDMAP_GETCONFIGSIZE": OpcodeDoc(
        summary="Size of a map area in tiles",
        int_in=("mapId",),
        int_out=("width", "height"),
    ),
    "WORLDMAP_GETCONFIGZOOM": OpcodeDoc(
        summary="Default zoom percentage of a map area",
        int_in=("mapId",),
        int_out=("zoom",),
    ),
    "WORLDMAP_GETCURRENTMAP": OpcodeDoc(
        summary="Id of the current map area (-1 when none)",
        int_out=("mapId",),
    ),
    "WORLDMAP_GETDISABLEELEMENT": OpcodeDoc(
        summary="Is one element enabled",
        int_in=("elementId",),
        int_out=("enabled",),
    ),
    "WORLDMAP_GETDISABLEELEMENTCATEGORY": OpcodeDoc(
        summary="Is an element category enabled",
        int_in=("categoryId",),
        int_out=("enabled",),
    ),
    "WORLDMAP_GETDISABLEELEMENTS": OpcodeDoc(
        summary="Is element drawing enabled",
        int_out=("enabled",),
    ),
    "WORLDMAP_GETDISPLAYCOORD": OpcodeDoc(
        summary="Map a world coord to a display position",
        int_in=("coord",),
        int_out=("x", "y"),
    ),
    "WORLDMAP_GETDISPLAYCOORD_CURRENT": OpcodeDoc(
        summary="World coord under the view centre",
        int_out=("x", "y"),
    ),
    "WORLDMAP_GETDISPLAYPOSITION": OpcodeDoc(
        summary="Centre of the view, in display tiles",
        int_out=("x", "y"),
    ),
    "WORLDMAP_GETMAP": OpcodeDoc(
        summary="Map area covering a world coord (-1 when none)",
        int_in=("coord",),
        int_out=("mapId",),
    ),
    "WORLDMAP_GETMAPNAME": OpcodeDoc(
        summary="External (display) name of a map area",
        int_in=("mapId",),
        str_out=("name",),
    ),
    "WORLDMAP_GETNEARESTICON": OpcodeDoc(
        summary="Display coord of the nearest icon of an element",
        int_in=("elementId", "sourceCoord"),
        int_out=("coord",),
    ),
    "WORLDMAP_GETSIZE": OpcodeDoc(
        summary="Visible size of the map surface, in tiles",
        int_out=("width", "height"),
    ),
    "WORLDMAP_GETSOURCECOORD": OpcodeDoc(
        summary="Map a display coord back to a world coord",
        int_in=("coord",),
        int_out=("coord",),
    ),
    "WORLDMAP_GETZOOM": OpcodeDoc(
        summary="Current zoom percentage (25/37/50/75/100/200)",
        int_out=("zoom",),
    ),
    "WORLDMAP_INIT": OpcodeDoc(
        summary="Select the map area containing the player and centre on it",
    ),
    "WORLDMAP_ISLOADED": OpcodeDoc(
        summary="Are the world map area configs available",
        int_out=("loaded",),
    ),
    "WORLDMAP_JUMPTODISPLAYCOORD": OpcodeDoc(
        summary="Pan towards a display coord",
        int_in=("coord",),
    ),
    "WORLDMAP_JUMPTODISPLAYCOORD_INSTANT": OpcodeDoc(
        summary="Snap to a display coord",
        int_in=("coord",),
    ),
    "WORLDMAP_JUMPTOMAP": OpcodeDoc(
        summary="Switch map area, panning to the player when it is in range and to the fallback coord otherwise",
        int_in=("mapId", "fallbackCoord"),
    ),
    "WORLDMAP_JUMPTOMAP_INSTANT": OpcodeDoc(
        summary="JUMPTOMAP, always using the fallback coord",
        int_in=("mapId", "fallbackCoord"),
    ),
    "WORLDMAP_JUMPTOSOURCECOORD": OpcodeDoc(
        summary="Pan towards a world coord",
        int_in=("coord",),
    ),
    "WORLDMAP_JUMPTOSOURCECOORD_INSTANT": OpcodeDoc(
        summary="Snap to a world coord",
        int_in=("coord",),
    ),
    "WORLDMAP_LISTELEMENT_NEXT": OpcodeDoc(
        summary="Continue iterating visible icons",
        int_out=("element", "coord"),
    ),
    "WORLDMAP_LISTELEMENT_START": OpcodeDoc(
        summary="Begin iterating visible icons",
        int_out=("element", "coord"),
        notes="yields (-1, -1) when the iteration is exhausted.",
    ),
    "WORLDMAP_PERPETUALFLASH": OpcodeDoc(
        summary="Flash forever instead of maxFlashCount times",
        int_in=("enabled",),
    ),
    "WORLDMAP_RESETCYCLESPERFLASH": OpcodeDoc(
        summary="Restore the default flash rate",
    ),
    "WORLDMAP_RESETMAXFLASHCOUNT": OpcodeDoc(
        summary="Restore the default flash count",
    ),
    "WORLDMAP_SETCYCLESPERFLASH": OpcodeDoc(
        summary="Client cycles between flash toggles",
        int_in=("cycles",),
    ),
    "WORLDMAP_SETMAP": OpcodeDoc(
        summary="Make a map area current (resets zoom + position)",
        int_in=("mapId",),
    ),
    "WORLDMAP_SETMAXFLASHCOUNT": OpcodeDoc(
        summary="How many times a flashing element blinks",
        int_in=("count",),
    ),
    "WORLDMAP_SETZOOM": OpcodeDoc(
        summary="Set the zoom percentage",
        int_in=("zoom",),
    ),
    "WORLDMAP_STOPCURRENTFLASHES": OpcodeDoc(
        summary="Clear every active flash",
    ),
    # --- Entity highlight (7000..7044) ---
    #
    # Eight subjects, five opcodes each, always in the same order: SETUP, ON,
    # OFF, GET, CLEAR. A `group` is one highlight style: SETUP says what group N
    # looks like, ON/OFF put a subject into or out of it, GET asks whether one is
    # in it, CLEAR empties the group. Only the ON/OFF pair of each group is named
    # in the vendored Opcodes.kt -- that pair is what identifies the subject, and
    # the other three follow from the fixed order and are confirmed by their
    # arity in the decompiler table.
    #
    # The SETUP arguments past the colour are read off the call sites, not off a
    # client: script 4763 turns mouse-over entity highlight on with
    # (6, colour, 2, 50, 90) and off with (6, -1, 0, 0, 64); script 5198 uses
    # (5, colour, 0, 70, 10) and ORs 16 into the last for its second toggle. So a
    # colour of -1 disables, and the last argument is a bit set rather than a
    # count. `flags` on ON/OFF/GET is likewise only ever a small constant (0 or 1)
    # at every call site in the rev-239 cache.
    "HIGHLIGHT_NPC_SETUP": OpcodeDoc(
        summary="Define what highlight group N looks like",
        int_in=("group", "colour", "style", "opacity", "flags",),
    ),
    "HIGHLIGHT_NPC_ON": OpcodeDoc(
        summary="Put one npc into a highlight group",
        int_in=("npc_uid", "coord", "group",),
    ),
    "HIGHLIGHT_NPC_OFF": OpcodeDoc(
        summary="Take one npc out of a highlight group",
        int_in=("npc_uid", "coord", "group",),
    ),
    "HIGHLIGHT_NPC_GET": OpcodeDoc(
        summary="Is this npc in the highlight group",
        int_in=("npc_uid", "coord", "group",),
        int_out=("highlighted",),
    ),
    "HIGHLIGHT_NPC_CLEAR": OpcodeDoc(
        summary="Empty a whole highlight group",
        int_in=("group",),
    ),
    "HIGHLIGHT_NPCTYPE_SETUP": OpcodeDoc(
        summary="Define what highlight group N looks like",
        int_in=("group", "colour", "style", "opacity", "flags",),
    ),
    "HIGHLIGHT_NPCTYPE_ON": OpcodeDoc(
        summary="Put one npc type into a highlight group",
        int_in=("npc_type", "group",),
    ),
    "HIGHLIGHT_NPCTYPE_OFF": OpcodeDoc(
        summary="Take one npc type out of a highlight group",
        int_in=("npc_type", "group",),
    ),
    "HIGHLIGHT_NPCTYPE_GET": OpcodeDoc(
        summary="Is this npc type in the highlight group",
        int_in=("npc_type", "group",),
        int_out=("highlighted",),
    ),
    "HIGHLIGHT_NPCTYPE_CLEAR": OpcodeDoc(
        summary="Empty a whole highlight group",
        int_in=("group",),
    ),
    "HIGHLIGHT_LOC_SETUP": OpcodeDoc(
        summary="Define what highlight group N looks like",
        int_in=("group", "colour", "style", "opacity", "flags",),
    ),
    "HIGHLIGHT_LOC_ON": OpcodeDoc(
        summary="Put one loc into a highlight group",
        int_in=("loc_type", "coord", "group", "flags",),
    ),
    "HIGHLIGHT_LOC_OFF": OpcodeDoc(
        summary="Take one loc out of a highlight group",
        int_in=("loc_type", "coord", "group", "flags",),
    ),
    "HIGHLIGHT_LOC_GET": OpcodeDoc(
        summary="Is this loc in the highlight group",
        int_in=("loc_type", "coord", "group", "flags",),
        int_out=("highlighted",),
    ),
    "HIGHLIGHT_LOC_CLEAR": OpcodeDoc(
        summary="Empty a whole highlight group",
        int_in=("group",),
    ),
    "HIGHLIGHT_LOCTYPE_SETUP": OpcodeDoc(
        summary="Define what highlight group N looks like",
        int_in=("group", "colour", "style", "opacity", "flags",),
    ),
    "HIGHLIGHT_LOCTYPE_ON": OpcodeDoc(
        summary="Put one loc type into a highlight group",
        int_in=("loc_type", "group",),
    ),
    "HIGHLIGHT_LOCTYPE_OFF": OpcodeDoc(
        summary="Take one loc type out of a highlight group",
        int_in=("loc_type", "group",),
    ),
    "HIGHLIGHT_LOCTYPE_GET": OpcodeDoc(
        summary="Is this loc type in the highlight group",
        int_in=("loc_type", "group",),
        int_out=("highlighted",),
    ),
    "HIGHLIGHT_LOCTYPE_CLEAR": OpcodeDoc(
        summary="Empty a whole highlight group",
        int_in=("group",),
    ),
    "HIGHLIGHT_OBJ_SETUP": OpcodeDoc(
        summary="Define what highlight group N looks like",
        int_in=("group", "colour", "style", "opacity", "flags",),
    ),
    "HIGHLIGHT_OBJ_ON": OpcodeDoc(
        summary="Put one obj into a highlight group",
        int_in=("obj_type", "coord", "group", "flags",),
    ),
    "HIGHLIGHT_OBJ_OFF": OpcodeDoc(
        summary="Take one obj out of a highlight group",
        int_in=("obj_type", "coord", "group", "flags",),
    ),
    "HIGHLIGHT_OBJ_GET": OpcodeDoc(
        summary="Is this obj in the highlight group",
        int_in=("obj_type", "coord", "group", "flags",),
        int_out=("highlighted",),
    ),
    "HIGHLIGHT_OBJ_CLEAR": OpcodeDoc(
        summary="Empty a whole highlight group",
        int_in=("group",),
    ),
    "HIGHLIGHT_OBJTYPE_SETUP": OpcodeDoc(
        summary="Define what highlight group N looks like",
        int_in=("group", "colour", "style", "opacity", "flags",),
    ),
    "HIGHLIGHT_OBJTYPE_ON": OpcodeDoc(
        summary="Put one obj type into a highlight group",
        int_in=("obj_type", "group",),
    ),
    "HIGHLIGHT_OBJTYPE_OFF": OpcodeDoc(
        summary="Take one obj type out of a highlight group",
        int_in=("obj_type", "group",),
    ),
    "HIGHLIGHT_OBJTYPE_GET": OpcodeDoc(
        summary="Is this obj type in the highlight group",
        int_in=("obj_type", "group",),
        int_out=("highlighted",),
    ),
    "HIGHLIGHT_OBJTYPE_CLEAR": OpcodeDoc(
        summary="Empty a whole highlight group",
        int_in=("group",),
    ),
    "HIGHLIGHT_PLAYER_SETUP": OpcodeDoc(
        summary="Define what highlight group N looks like",
        int_in=("group", "colour", "style", "opacity", "flags",),
    ),
    "HIGHLIGHT_PLAYER_ON": OpcodeDoc(
        summary="Put one player into a highlight group",
        int_in=("group",),
        str_in=("name",),
    ),
    "HIGHLIGHT_PLAYER_OFF": OpcodeDoc(
        summary="Take one player out of a highlight group",
        int_in=("group",),
        str_in=("name",),
    ),
    "HIGHLIGHT_PLAYER_GET": OpcodeDoc(
        summary="Is this player in the highlight group",
        int_in=("group",),
        str_in=("name",),
        int_out=("highlighted",),
    ),
    "HIGHLIGHT_PLAYER_CLEAR": OpcodeDoc(
        summary="Empty a whole highlight group",
        int_in=("group",),
    ),
    "HIGHLIGHT_TILE_SETUP": OpcodeDoc(
        summary="Define what highlight group N looks like",
        int_in=("group", "colour", "style", "opacity", "flags",),
    ),
    # The player block's four route/uid getters, read out of the reference's
    # ScriptRunnerImpl_6900To6999.cpp (its own LogAssert strings name the
    # fields). They are NOT client-op context getters despite sitting beside
    # _6900: they are about the ACTIVE PLAYER, which the client sets before
    # firing a per-player trigger and MINIMENU_FINDPLAYER sets from the menu.
    "_6902": OpcodeDoc(
        summary="Active player's route length",
        int_out=("m_routeLength (0..9)",),
        notes="The queue of tiles the server has put the player on that the\n"
        "client has not walked through yet. 0 means standing still, and\n"
        "clientscript 5203 reads it exactly that way: with a route it marks\n"
        "_6903(0), without one it marks `coord`.",
    ),
    "_6903": OpcodeDoc(
        summary="Active player's route coord at an index",
        int_in=("index",),
        int_out=("packed coord, or -1",),
        notes="Index 0 is the NEWEST entry -- ClientPlayer::AddRoutePoint\n"
        "shifts the queue up and writes the new tile at 0 -- so it is the\n"
        "player's server-side tile, ahead of the rendered position while\n"
        "walking. The reference packs it as plane << 28 | x << 14 | z after\n"
        "WorldCoordFromBuildCoord, and asserts index < m_routeLength.",
    ),
    "_6904": OpcodeDoc(
        summary="Active player's uid",
        int_out=("uid, or -1",),
    ),
    "_6905": OpcodeDoc(
        summary="Local player's uid",
        int_out=("uid, or -1",),
        notes="`_6904 = _6905` is how a per-player trigger script asks whether\n"
        "the player it fired for is this client's own -- clientscript 5203\n"
        "(the current-tile indicator) is the cache's only user of either.",
    ),
    "HIGHLIGHT_TILE_ON": OpcodeDoc(
        summary="Put one tile into a highlight group",
        int_in=("coord", "group", "flags",),
    ),
    "HIGHLIGHT_TILE_OFF": OpcodeDoc(
        summary="Take one tile out of a highlight group",
        int_in=("coord", "group", "flags",),
    ),
    "HIGHLIGHT_TILE_GET": OpcodeDoc(
        summary="Is this tile in the highlight group",
        int_in=("coord", "group", "flags",),
        int_out=("highlighted",),
    ),
    "HIGHLIGHT_TILE_CLEAR": OpcodeDoc(
        summary="Empty a whole highlight group",
        int_in=("group",),
    ),
    # 7040..7044: the ninth highlight group, shaped exactly like the PLAYER one
    # (SETUP, then a name-keyed ON/OFF/GET, then CLEAR) but postdating the
    # vendored Opcodes.kt, so its subject has no name here. Arities are from the
    # cache's own call sites and match the decompiler's table.
    "_7040": OpcodeDoc(
        summary="Define what highlight group N looks like",
        int_in=("group", "colour", "style", "opacity", "flags"),
    ),
    "_7041": OpcodeDoc(
        summary="Put one named subject into a highlight group",
        int_in=("group",),
        str_in=("name",),
    ),
    "_7042": OpcodeDoc(
        summary="Take one named subject out of a highlight group",
        int_in=("group",),
        str_in=("name",),
    ),
    "_7043": OpcodeDoc(
        summary="Is this named subject in the highlight group",
        int_in=("group",),
        str_in=("name",),
        int_out=("highlighted",),
    ),
    "_7044": OpcodeDoc(
        summary="Empty a whole highlight group",
        int_in=("group",),
    ),
}
