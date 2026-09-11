"""Reviewed executable-semantics declarations shared by the C and TS VMs.

This module is deliberately not derived from opcode names, documentation prose,
or the inferred stack tables.  Every row below is an explicit contract audited
against ``CS2VM2_RunOp`` and the corresponding C intrinsic.  This reviewed
slice covers constants, locals, control flow/calls, arrays, string joins, stack
discards, and the bank closure's unambiguous state-independent integer math.
Unsupported opcodes stay on the existing C/WASM path until an equally explicit
row is added here.

Stack role tuples are written in bottom-to-top order.  Thus ("lhs", "rhs")
means that ``rhs`` is popped first by a stack-machine implementation.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from pathlib import Path
import re
from typing import Callable, Mapping, Sequence


class OperandKind(str, Enum):
    NONE = "none"
    INT8 = "int8"
    INT32 = "int32"
    STRING = "string"


class TargetEffect(str, Enum):
    NONE = "none"
    ACTIVE = "active"
    DOT = "dot"
    BOTH = "both"
    DYNAMIC = "dynamic"


class BarrierKind(str, Enum):
    NONE = "none"
    READ = "read"
    TOPOLOGY = "topology"
    GEOMETRY = "geometry"
    EXTERNAL = "external"


class ReplayKind(str, Enum):
    PURE = "pure"
    CHECKPOINT = "checkpoint"
    UNDO_LOG = "undo-log"


class StackEffectKind(str, Enum):
    """How to interpret the explicit stack-role columns for an opcode.

    Most rows have a fixed effect.  The remaining values deliberately make
    operand/signature/type-dependent effects visible instead of publishing a
    plausible but false fixed pop/push count to either generated backend.
    """

    FIXED = "fixed"
    JOIN_STRING_COUNT = "join-string-count"
    CALLEE_SIGNATURE = "callee-signature"
    ARRAY_ELEMENT_TYPE = "array-element-type"


class Dialect(str, Enum):
    CANONICAL = "canonical"
    RS2_DAT2 = "rs2-dat2"


@dataclass(frozen=True, slots=True)
class Intrinsic:
    """One handwritten operation implemented once per VM backend."""

    name: str
    c_handler: str


@dataclass(frozen=True, slots=True)
class OpcodeSemantics:
    opcode: int
    name: str
    operand: OperandKind
    int_pops: tuple[str, ...]
    string_pops: tuple[str, ...]
    int_pushes: tuple[str, ...]
    string_pushes: tuple[str, ...]
    intrinsic: str
    target_effect: TargetEffect
    barrier: BarrierKind
    may_yield: bool
    replay: ReplayKind
    dialects: tuple[Dialect, ...]
    stack_effect: StackEffectKind = StackEffectKind.FIXED


INTRINSICS: Mapping[str, Intrinsic] = {
    "pushIntConstant": Intrinsic("pushIntConstant", "CS2VM2_Op_PushConstantInt"),
    "pushStringConstant": Intrinsic("pushStringConstant", "CS2VM2_Op_PushConstantString"),
    "branch": Intrinsic("branch", "CS2VM2_Op_Branch"),
    "branchIntNotEquals": Intrinsic("branchIntNotEquals", "CS2VM2_Op_BranchNotEquals"),
    "branchIntEquals": Intrinsic("branchIntEquals", "CS2VM2_Op_BranchEquals"),
    "branchIntLessThan": Intrinsic("branchIntLessThan", "CS2VM2_Op_BranchLessThan"),
    "branchIntGreaterThan": Intrinsic("branchIntGreaterThan", "CS2VM2_Op_BranchGreaterThan"),
    "returnFrame": Intrinsic("returnFrame", "CS2VM2_Op_Return"),
    "branchIntLessThanOrEquals": Intrinsic(
        "branchIntLessThanOrEquals", "CS2VM2_Op_BranchLessThanOrEquals"
    ),
    "branchIntGreaterThanOrEquals": Intrinsic(
        "branchIntGreaterThanOrEquals", "CS2VM2_Op_BranchGreaterThanOrEquals"
    ),
    "pushIntLocal": Intrinsic("pushIntLocal", "CS2VM2_Op_PushIntLocal"),
    "popIntLocal": Intrinsic("popIntLocal", "CS2VM2_Op_PopIntLocal"),
    "pushStringLocal": Intrinsic("pushStringLocal", "CS2VM2_Op_PushStrLocal"),
    "popStringLocal": Intrinsic("popStringLocal", "CS2VM2_Op_PopStrLocal"),
    "joinStrings": Intrinsic("joinStrings", "CS2VM2_Op_JoinString"),
    "discardInt": Intrinsic("discardInt", "CS2VM2_Op_PopIntDiscard"),
    "discardString": Intrinsic("discardString", "CS2VM2_Op_PopStrDiscard"),
    "callScriptWithParams": Intrinsic(
        "callScriptWithParams", "CS2VM2_Op_GosubWithParams"
    ),
    "defineArray": Intrinsic("defineArray", "CS2VM2_Op_DefineArray"),
    "pushArrayElement": Intrinsic("pushArrayElement", "CS2VM2_Op_PushArrayInt"),
    "popArrayElement": Intrinsic("popArrayElement", "CS2VM2_Op_PopArrayInt"),
    "switchBranch": Intrinsic("switchBranch", "CS2VM2_Op_Switch"),
    "intAdd": Intrinsic("intAdd", "CS2VM2_Op_Add"),
    "intSubtract": Intrinsic("intSubtract", "CS2VM2_Op_Sub"),
    "intMultiply": Intrinsic("intMultiply", "CS2VM2_Op_Mul"),
    "intDivide": Intrinsic("intDivide", "CS2VM2_Op_Div"),
    "intModulo": Intrinsic("intModulo", "CS2VM2_Op_Mod"),
    "intInterpolate": Intrinsic("intInterpolate", "CS2VM2_Op_Interpolate"),
    "intSetBit": Intrinsic("intSetBit", "CS2VM2_Op_SetBit"),
    "intTestBit": Intrinsic("intTestBit", "CS2VM2_Op_TestBit"),
    "intMinimum": Intrinsic("intMinimum", "CS2VM2_Op_Min"),
    "intMaximum": Intrinsic("intMaximum", "CS2VM2_Op_Max"),
    "intScale": Intrinsic("intScale", "CS2VM2_Op_Scale"),
    "intGetBitRange": Intrinsic("intGetBitRange", "CS2VM2_Op_GetBitRange"),
    "moveCoord": Intrinsic("moveCoord", "CS2VM2_Op_MoveCoord"),
    "intPower": Intrinsic("intPower", "CS2VM2_Op_Pow"),
    "appendStrings": Intrinsic("appendStrings", "CS2VM2_Op_Append"),
    "lowercaseAscii": Intrinsic("lowercaseAscii", "CS2VM2_Op_Lowercase"),
    "intToString": Intrinsic("intToString", "CS2VM2_Op_ToString"),
    "compareClientStrings": Intrinsic("compareClientStrings", "CS2VM2_Op_Compare"),
    "escapeMarkup": Intrinsic("escapeMarkup", "CS2VM2_Op_Escape"),
    "stringLength": Intrinsic("stringLength", "CS2VM2_Op_StringLength"),
    "substring": Intrinsic("substring", "CS2VM2_Op_Substring"),
    "removeTags": Intrinsic("removeTags", "CS2VM2_Op_RemoveTags"),
    "stringIndexOfString": Intrinsic(
        "stringIndexOfString", "CS2VM2_Op_StringIndexOfString"
    ),
    "onMobile": Intrinsic("onMobile", "CS2VM2_Op_OnMobile"),
    "clientType": Intrinsic("clientType", "CS2VM2_Op_ClientType"),
    "arrayLength": Intrinsic("arrayLength", "CS2VM2_Op_ArrayLength"),
}


_BOTH_DIALECTS = (Dialect.CANONICAL, Dialect.RS2_DAT2)


# Do not replace these rows with a helper that infers behavior from the opcode
# name.  Repetition here is intentional: reviewers can see every executable
# property that will be emitted into both backends.
OPCODE_SEMANTICS: tuple[OpcodeSemantics, ...] = (
    OpcodeSemantics(
        opcode=0,
        name="PUSH_CONSTANT_INT",
        operand=OperandKind.INT32,
        int_pops=(),
        string_pops=(),
        int_pushes=("operand",),
        string_pushes=(),
        intrinsic="pushIntConstant",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=3,
        name="PUSH_CONSTANT_STRING",
        operand=OperandKind.STRING,
        int_pops=(),
        string_pops=(),
        int_pushes=(),
        string_pushes=("operand",),
        intrinsic="pushStringConstant",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=6,
        name="BRANCH",
        operand=OperandKind.INT32,
        int_pops=(),
        string_pops=(),
        int_pushes=(),
        string_pushes=(),
        intrinsic="branch",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=7,
        name="BRANCH_NOT",
        operand=OperandKind.INT32,
        int_pops=("lhs", "rhs"),
        string_pops=(),
        int_pushes=(),
        string_pushes=(),
        intrinsic="branchIntNotEquals",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=8,
        name="BRANCH_EQUALS",
        operand=OperandKind.INT32,
        int_pops=("lhs", "rhs"),
        string_pops=(),
        int_pushes=(),
        string_pushes=(),
        intrinsic="branchIntEquals",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=9,
        name="BRANCH_LESS_THAN",
        operand=OperandKind.INT32,
        int_pops=("lhs", "rhs"),
        string_pops=(),
        int_pushes=(),
        string_pushes=(),
        intrinsic="branchIntLessThan",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=10,
        name="BRANCH_GREATER_THAN",
        operand=OperandKind.INT32,
        int_pops=("lhs", "rhs"),
        string_pops=(),
        int_pushes=(),
        string_pushes=(),
        intrinsic="branchIntGreaterThan",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=21,
        name="RETURN",
        operand=OperandKind.INT8,
        int_pops=(),
        string_pops=(),
        int_pushes=(),
        string_pushes=(),
        intrinsic="returnFrame",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=31,
        name="BRANCH_LESS_THAN_OR_EQUALS",
        operand=OperandKind.INT32,
        int_pops=("lhs", "rhs"),
        string_pops=(),
        int_pushes=(),
        string_pushes=(),
        intrinsic="branchIntLessThanOrEquals",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=32,
        name="BRANCH_GREATER_THAN_OR_EQUALS",
        operand=OperandKind.INT32,
        int_pops=("lhs", "rhs"),
        string_pops=(),
        int_pushes=(),
        string_pushes=(),
        intrinsic="branchIntGreaterThanOrEquals",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=33,
        name="PUSH_INT_LOCAL",
        operand=OperandKind.INT32,
        int_pops=(),
        string_pops=(),
        int_pushes=("intLocal[operand]",),
        string_pushes=(),
        intrinsic="pushIntLocal",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=34,
        name="POP_INT_LOCAL",
        operand=OperandKind.INT32,
        int_pops=("value",),
        string_pops=(),
        int_pushes=(),
        string_pushes=(),
        intrinsic="popIntLocal",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=35,
        name="PUSH_STRING_LOCAL",
        operand=OperandKind.INT32,
        int_pops=(),
        string_pops=(),
        int_pushes=(),
        string_pushes=("stringLocal[operand]",),
        intrinsic="pushStringLocal",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=36,
        name="POP_STRING_LOCAL",
        operand=OperandKind.INT32,
        int_pops=(),
        string_pops=("value",),
        int_pushes=(),
        string_pushes=(),
        intrinsic="popStringLocal",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=37,
        name="JOIN_STRING",
        operand=OperandKind.INT32,
        int_pops=(),
        string_pops=(),
        int_pushes=(),
        string_pushes=("joined",),
        intrinsic="joinStrings",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
        stack_effect=StackEffectKind.JOIN_STRING_COUNT,
    ),
    OpcodeSemantics(
        opcode=38,
        name="POP_INT_DISCARD",
        operand=OperandKind.INT8,
        int_pops=("value",),
        string_pops=(),
        int_pushes=(),
        string_pushes=(),
        intrinsic="discardInt",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=39,
        name="POP_STRING_DISCARD",
        operand=OperandKind.INT8,
        int_pops=(),
        string_pops=("value",),
        int_pushes=(),
        string_pushes=(),
        intrinsic="discardString",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=40,
        name="GOSUB_WITH_PARAMS",
        operand=OperandKind.INT32,
        int_pops=(),
        string_pops=(),
        int_pushes=(),
        string_pushes=(),
        intrinsic="callScriptWithParams",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
        stack_effect=StackEffectKind.CALLEE_SIGNATURE,
    ),
    OpcodeSemantics(
        opcode=44,
        name="DEFINE_ARRAY",
        operand=OperandKind.INT32,
        int_pops=("size",),
        string_pops=(),
        int_pushes=(),
        string_pushes=(),
        intrinsic="defineArray",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=45,
        name="PUSH_ARRAY_INT",
        operand=OperandKind.INT32,
        int_pops=("index",),
        string_pops=(),
        int_pushes=(),
        string_pushes=(),
        intrinsic="pushArrayElement",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
        stack_effect=StackEffectKind.ARRAY_ELEMENT_TYPE,
    ),
    OpcodeSemantics(
        opcode=46,
        name="POP_ARRAY_INT",
        operand=OperandKind.INT32,
        int_pops=("index",),
        string_pops=(),
        int_pushes=(),
        string_pushes=(),
        intrinsic="popArrayElement",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
        stack_effect=StackEffectKind.ARRAY_ELEMENT_TYPE,
    ),
    OpcodeSemantics(
        opcode=60,
        name="SWITCH",
        operand=OperandKind.INT32,
        int_pops=("key",),
        string_pops=(),
        int_pushes=(),
        string_pushes=(),
        intrinsic="switchBranch",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=3325,
        name="MOVECOORD",
        operand=OperandKind.INT8,
        int_pops=("packed", "x", "plane", "z"),
        string_pops=(),
        int_pushes=("moved",),
        string_pushes=(),
        intrinsic="moveCoord",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=4000,
        name="ADD",
        operand=OperandKind.INT8,
        int_pops=("lhs", "rhs"),
        string_pops=(),
        int_pushes=("result",),
        string_pushes=(),
        intrinsic="intAdd",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=4001,
        name="SUB",
        operand=OperandKind.INT8,
        int_pops=("lhs", "rhs"),
        string_pops=(),
        int_pushes=("result",),
        string_pushes=(),
        intrinsic="intSubtract",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=4002,
        name="MULTIPLY",
        operand=OperandKind.INT8,
        int_pops=("lhs", "rhs"),
        string_pops=(),
        int_pushes=("result",),
        string_pushes=(),
        intrinsic="intMultiply",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=4003,
        name="DIV",
        operand=OperandKind.INT8,
        int_pops=("lhs", "rhs"),
        string_pops=(),
        int_pushes=("quotient",),
        string_pushes=(),
        intrinsic="intDivide",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=4006,
        name="INTERPOLATE",
        operand=OperandKind.INT8,
        int_pops=("a", "b", "c", "d", "e"),
        string_pops=(),
        int_pushes=("result",),
        string_pushes=(),
        intrinsic="intInterpolate",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=4008,
        name="SETBIT",
        operand=OperandKind.INT8,
        int_pops=("value", "bit"),
        string_pops=(),
        int_pushes=("result",),
        string_pushes=(),
        intrinsic="intSetBit",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=4010,
        name="TESTBIT",
        operand=OperandKind.INT8,
        int_pops=("value", "bit"),
        string_pops=(),
        int_pushes=("isSet",),
        string_pushes=(),
        intrinsic="intTestBit",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=4011,
        name="MOD",
        operand=OperandKind.INT8,
        int_pops=("lhs", "rhs"),
        string_pops=(),
        int_pushes=("remainder",),
        string_pushes=(),
        intrinsic="intModulo",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=4012,
        name="POW",
        operand=OperandKind.INT8,
        int_pops=("base", "exponent"),
        string_pops=(),
        int_pushes=("power",),
        string_pushes=(),
        intrinsic="intPower",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=4016,
        name="MIN",
        operand=OperandKind.INT8,
        int_pops=("lhs", "rhs"),
        string_pops=(),
        int_pushes=("minimum",),
        string_pushes=(),
        intrinsic="intMinimum",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=4017,
        name="MAX",
        operand=OperandKind.INT8,
        int_pops=("lhs", "rhs"),
        string_pops=(),
        int_pushes=("maximum",),
        string_pushes=(),
        intrinsic="intMaximum",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=4018,
        name="SCALE",
        operand=OperandKind.INT8,
        int_pops=("a", "b", "c"),
        string_pops=(),
        int_pushes=("scaled",),
        string_pushes=(),
        intrinsic="intScale",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=4029,
        name="GETBIT_RANGE",
        operand=OperandKind.INT8,
        int_pops=("value", "low", "high"),
        string_pops=(),
        int_pushes=("range",),
        string_pushes=(),
        intrinsic="intGetBitRange",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=4101,
        name="APPEND",
        operand=OperandKind.INT8,
        int_pops=(),
        string_pops=("dest", "src"),
        int_pushes=(),
        string_pushes=("appended",),
        intrinsic="appendStrings",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=4103,
        name="LOWERCASE",
        operand=OperandKind.INT8,
        int_pops=(),
        string_pops=("text",),
        int_pushes=(),
        string_pushes=("lowercase",),
        intrinsic="lowercaseAscii",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=4106,
        name="TOSTRING",
        operand=OperandKind.INT8,
        int_pops=("value",),
        string_pops=(),
        int_pushes=(),
        string_pushes=("decimal",),
        intrinsic="intToString",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=4107,
        name="COMPARE",
        operand=OperandKind.INT8,
        int_pops=(),
        string_pops=("lhs", "rhs"),
        int_pushes=("ordering",),
        string_pushes=(),
        intrinsic="compareClientStrings",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=4111,
        name="ESCAPE",
        operand=OperandKind.INT8,
        int_pops=(),
        string_pops=("text",),
        int_pushes=(),
        string_pushes=("escaped",),
        intrinsic="escapeMarkup",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=4117,
        name="STRING_LENGTH",
        operand=OperandKind.INT8,
        int_pops=(),
        string_pops=("text",),
        int_pushes=("length",),
        string_pushes=(),
        intrinsic="stringLength",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=4118,
        name="SUBSTRING",
        operand=OperandKind.INT8,
        int_pops=("start", "end"),
        string_pops=("text",),
        int_pushes=(),
        string_pushes=("substring",),
        intrinsic="substring",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=4119,
        name="REMOVETAGS",
        operand=OperandKind.INT8,
        int_pops=(),
        string_pops=("text",),
        int_pushes=(),
        string_pushes=("plain",),
        intrinsic="removeTags",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=4121,
        name="STRING_INDEXOF_STRING",
        operand=OperandKind.INT8,
        int_pops=("start",),
        string_pops=("haystack", "needle"),
        int_pushes=("index",),
        string_pushes=(),
        intrinsic="stringIndexOfString",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=6518,
        name="ON_MOBILE",
        operand=OperandKind.INT8,
        int_pops=(),
        string_pops=(),
        int_pushes=("isMobile",),
        string_pushes=(),
        intrinsic="onMobile",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=6519,
        name="CLIENTTYPE",
        operand=OperandKind.INT8,
        int_pops=(),
        string_pops=(),
        int_pushes=("clientType",),
        string_pushes=(),
        intrinsic="clientType",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
    OpcodeSemantics(
        opcode=8003,
        name="ARRAY_LENGTH",
        operand=OperandKind.INT8,
        int_pops=(),
        string_pops=("handle",),
        int_pushes=("length",),
        string_pushes=(),
        intrinsic="arrayLength",
        target_effect=TargetEffect.NONE,
        barrier=BarrierKind.NONE,
        may_yield=False,
        replay=ReplayKind.PURE,
        dialects=_BOTH_DIALECTS,
    ),
)


FOUNDATION_OPCODE_NAMES = frozenset(
    {
        "PUSH_CONSTANT_INT",
        "PUSH_CONSTANT_STRING",
        "BRANCH",
        "BRANCH_NOT",
        "BRANCH_EQUALS",
        "BRANCH_LESS_THAN",
        "BRANCH_GREATER_THAN",
        "RETURN",
        "BRANCH_LESS_THAN_OR_EQUALS",
        "BRANCH_GREATER_THAN_OR_EQUALS",
        "PUSH_INT_LOCAL",
        "POP_INT_LOCAL",
        "PUSH_STRING_LOCAL",
        "POP_STRING_LOCAL",
        "JOIN_STRING",
        "POP_INT_DISCARD",
        "POP_STRING_DISCARD",
        "GOSUB_WITH_PARAMS",
        "DEFINE_ARRAY",
        "PUSH_ARRAY_INT",
        "POP_ARRAY_INT",
        "SWITCH",
        "MOVECOORD",
        "ADD",
        "SUB",
        "MULTIPLY",
        "DIV",
        "INTERPOLATE",
        "SETBIT",
        "TESTBIT",
        "MOD",
        "POW",
        "MIN",
        "MAX",
        "SCALE",
        "GETBIT_RANGE",
        "APPEND",
        "LOWERCASE",
        "TOSTRING",
        "COMPARE",
        "ESCAPE",
        "STRING_LENGTH",
        "SUBSTRING",
        "REMOVETAGS",
        "STRING_INDEXOF_STRING",
        "ON_MOBILE",
        "CLIENTTYPE",
        "ARRAY_LENGTH",
    }
)


_C_OPERAND_BY_SCHEMA = {
    OperandKind.NONE: "CS2_OPERAND_NONE",
    OperandKind.INT8: "CS2_OPERAND_INT8",
    OperandKind.INT32: "CS2_OPERAND_INT32",
    OperandKind.STRING: "CS2_OPERAND_STRING",
}


def _production_dispatch_handlers(dispatch_source: Path) -> dict[str, str]:
    """Read only explicit ``case -> named C intrinsic`` pairs from RunOp."""

    text = dispatch_source.read_text(encoding="utf-8")
    try:
        start = text.index("\nCS2VM2_RunOp(")
        end = text.index("\n/* Fills *int_args", start)
    except ValueError as error:
        raise ValueError(f"cannot locate CS2VM2_RunOp in {dispatch_source}") from error
    body = text[start:end]
    pattern = re.compile(
        r"case\s+CS2_OP_([A-Z0-9_]+)\s*:\s*"
        r"return\s+(CS2VM2_Op_[A-Za-z0-9_]+)\s*\(",
        re.MULTILINE,
    )
    return dict(pattern.findall(body))


def validate_opcode_semantics(
    entries: Sequence[tuple[str, int]],
    dispatch_source: Path,
    operand_kind_for_opcode: Callable[[int], str],
    handler_kind_for_opcode: Callable[[int], str],
) -> None:
    """Reject any semantic or production-dispatch drift before generation."""

    names_to_ids = {name: opcode for name, opcode in entries}
    ids_to_names = {opcode: name for name, opcode in entries}
    seen_ids: set[int] = set()
    seen_names: set[str] = set()
    covered_names: set[str] = set()
    errors: list[str] = []
    production_handlers = _production_dispatch_handlers(dispatch_source)

    for semantic in OPCODE_SEMANTICS:
        if semantic.opcode in seen_ids:
            errors.append(f"duplicate semantic opcode {semantic.opcode}")
        if semantic.name in seen_names:
            errors.append(f"duplicate semantic name {semantic.name}")
        seen_ids.add(semantic.opcode)
        seen_names.add(semantic.name)
        covered_names.add(semantic.name)

        if names_to_ids.get(semantic.name) != semantic.opcode:
            errors.append(
                f"{semantic.name}: schema id {semantic.opcode}, "
                f"opcode table has {names_to_ids.get(semantic.name)!r}"
            )
        if ids_to_names.get(semantic.opcode) != semantic.name:
            errors.append(
                f"opcode {semantic.opcode}: schema name {semantic.name}, "
                f"opcode table has {ids_to_names.get(semantic.opcode)!r}"
            )

        intrinsic = INTRINSICS.get(semantic.intrinsic)
        if intrinsic is None:
            errors.append(f"{semantic.name}: unknown intrinsic {semantic.intrinsic!r}")
        else:
            actual_handler = production_handlers.get(semantic.name)
            if actual_handler != intrinsic.c_handler:
                errors.append(
                    f"{semantic.name}: schema intrinsic maps to {intrinsic.c_handler}, "
                    f"production dispatch has {actual_handler!r}"
                )

        expected_operand = _C_OPERAND_BY_SCHEMA[semantic.operand]
        actual_operand = operand_kind_for_opcode(semantic.opcode)
        if actual_operand != expected_operand:
            errors.append(
                f"{semantic.name}: schema operand {expected_operand}, "
                f"opcode metadata has {actual_operand}"
            )
        if handler_kind_for_opcode(semantic.opcode) != "CS2_HANDLER_VM":
            errors.append(f"{semantic.name}: opcode metadata does not classify it as VM-core")
        if not semantic.dialects:
            errors.append(f"{semantic.name}: no supported dialect declared")
        if len(set(semantic.dialects)) != len(semantic.dialects):
            errors.append(f"{semantic.name}: duplicate dialect declaration")

    if covered_names != FOUNDATION_OPCODE_NAMES:
        missing = sorted(FOUNDATION_OPCODE_NAMES - covered_names)
        extra = sorted(covered_names - FOUNDATION_OPCODE_NAMES)
        errors.append(f"foundation coverage drift: missing={missing}, extra={extra}")

    declared_intrinsics = {semantic.intrinsic for semantic in OPCODE_SEMANTICS}
    unused_intrinsics = sorted(set(INTRINSICS) - declared_intrinsics)
    if unused_intrinsics:
        errors.append(f"intrinsics without semantic rows: {unused_intrinsics}")

    if errors:
        raise ValueError("invalid explicit opcode semantics:\n  " + "\n  ".join(errors))


def c_enum_suffix(value: str) -> str:
    """Format an already-declared identifier for generated C; no semantics inferred."""

    return re.sub(r"(?<!^)(?=[A-Z])", "_", value).replace("-", "_").upper()
