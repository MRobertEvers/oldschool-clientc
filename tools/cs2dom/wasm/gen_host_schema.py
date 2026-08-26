#!/usr/bin/env python3
"""Generate ABI-stable C and typed TypeScript CS2 Host request descriptors.

The VM deliberately gives its host one tagged C union.  JavaScript must not
duplicate the compiler's struct layout, especially for bool, pointers, and the
large SETON payloads.  This generator turns the authoritative .def manifest
into C metadata whose offsets are still evaluated by the C compiler and a
logical TypeScript Host surface.  The TS catalog joins two existing generated
opcode artifacts for bytecode operand and stack shapes; it never infers either
shape from C request fields.
"""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass
from pathlib import Path


CALL = "CS2VM_HOST_REQUEST_KIND("


@dataclass(frozen=True)
class Field:
    declaration: str
    name: str
    kind: str
    capacity: str
    count_field: str | None = None
    stride: str = "0"


@dataclass(frozen=True)
class Request:
    name: str
    opcode: int
    fields: tuple[Field, ...]


@dataclass(frozen=True)
class CommandSignature:
    name: str
    stack_input: str
    stack_output: str


@dataclass(frozen=True)
class WireOperandRule:
    int8_from: int
    int8_opcodes: frozenset[int]


@dataclass(frozen=True)
class RequestSemantics:
    request: Request
    command: CommandSignature
    operand: str
    barrier: str
    target_effect: str
    result_kind: str
    result_type: str
    stack_output: str
    result_source: str
    executable_reviewed: bool


@dataclass(frozen=True)
class ExecutableReview:
    opcode: int
    name: str
    adapter: str
    native_reference: str


COMMAND_ROW = re.compile(
    r'^\s*\[(\d+),"([^"]+)","[^"]*",'
    r'"([^"]*)","([^"]*)",(?:0|1),"[^"]*"\],',
    re.MULTILINE,
)
DECODE_OPERAND_ROW = re.compile(r"^\s*\[(\d+)\]\s*=\s*([0-3]),", re.MULTILINE)
OPERAND_KINDS = {
    0: "none",
    1: "int8",
    2: "int32",
    3: "string",
}
STACK_PATTERN = re.compile(r"[is?]*")

# These are logical Host return shapes, not merely C VM stack pushes.  Target
# requests return a component reference to the adapter, which separately
# updates active/dot and may push a boolean.  Child iterator requests return
# the refs used to seed C's iterator even when the opcode itself pushes no
# value.  Keep these exact lists in lockstep with wasm_runtime.js until both
# adapters consume the generated table.
TARGET_RESULT_REQUESTS = frozenset({
    "CC_CREATE",
    "CC_COPY",
    "CC_CREATECHILD",
    "CC_CREATESIBLING",
    "CC_FIND",
    "IF_FIND",
    "OVERLAY_CC_CREATE",
    "OVERLAY_FIND",
    "OVERLAY_CC_FIND",
    "CC_CHILDREN_FINDNEXT",
})
CHILD_ITERATOR_REQUESTS = frozenset({
    "IF_CHILDREN_FIND",
    "IF_CHILDREN_COLLECT",
    "CC_CHILDREN_FIND_COUNT",
})
POLYMORPHIC_RESULT_REQUESTS = frozenset({
    "ENUM",
    "CC_GETPARAM",
    "NC_PARAM",
    "LC_PARAM",
    "OC_PARAM",
    "STRUCT_PARAM",
})
RESULT_PATTERN_OVERRIDES = {
    "PUSH_VAR": "i",
    "PUSH_VARBIT": "i",
    "PUSH_VARC_INT": "i",
    "PUSH_VARC_STRING": "s",
    "PUSH_VARC_STRING_OLD": "s",
    "CC_GETCOMPONENTPARAM": "i",
    # The legacy command table overstates these arities; C/native pushes one.
    "WORLDMAP_GETSOURCECOORD": "i",
    "WORLDMAP_GETNEARESTICON": "i",
    "MEC_SPRITE": "i",
}

# Requests which alter component identity/order or the implicit active/dot
# target are strict topology barriers.  The complete create/find/delete blocks
# are included so an added request cannot accidentally become bufferable.
TOPOLOGY_REQUESTS = frozenset({
    "CC_CREATE", "CC_DELETE", "CC_DELETEALL", "OVERLAY_CC_CREATE",
    "OVERLAY_CC_DELETEALL", "CC_COPY", "CC_CREATECHILD", "CC_CREATESIBLING",
    "CC_FIND", "IF_FIND", "OVERLAY_FIND", "OVERLAY_CC_FIND",
    "IF_CHILDREN_FIND", "IF_CHILDREN_COLLECT", "CC_CHILDREN_FIND_COUNT",
    "CC_CHILDREN_FINDNEXT", "CC_GETLAYER", "CC_GETID", "IF_GETLAYER",
    "IF_HASSUB", "IF_HASCHILD_OVERLAY", "IF_GETTOP", "IF_CLOSE",
    "MINIMENU_FINDCOMPONENT",
    "OVERLAY_NPC_CREATE", "OVERLAY_LOC_CREATE", "OVERLAY_PLAYER_CREATE",
    "OVERLAY_COORD_CREATE", "OVERLAY_NPC_GET", "OVERLAY_LOC_GET",
    "OVERLAY_PLAYER_GET", "OVERLAY_COORD_GET", "OVERLAY_NPC_DESTROY",
    "OVERLAY_LOC_DESTROY", "OVERLAY_PLAYER_DESTROY", "OVERLAY_COORD_DESTROY",
})

# These queries must observe the current working-tree layout, including pending
# position/size/scroll/visibility mutations.  Global viewport/font measurements
# share the same barrier because their answer participates in layout decisions.
GEOMETRY_REQUESTS = frozenset({
    "CC_GETX", "CC_GETY", "CC_GETWIDTH", "CC_GETHEIGHT", "CC_GETHIDE",
    "CC_GETSCROLLX", "CC_GETSCROLLY", "CC_GETSCROLLWIDTH", "CC_GETSCROLLHEIGHT",
    "IF_GETX", "IF_GETY", "IF_GETWIDTH", "IF_GETHEIGHT", "IF_GETHIDE",
    "IF_GETSCROLLX", "IF_GETSCROLLY", "IF_GETSCROLLWIDTH", "IF_GETSCROLLHEIGHT",
    "PARAWIDTH", "PARAHEIGHT", "VIEWPORT_GETEFFECTIVESIZE", "VIEWPORT_GETZOOM",
    "VIEWPORT_GETFOV", "UIZOOM_GET", "UIZOOM_GETDEFAULT", "SAFEAREA_GETMINX",
    "SAFEAREA_GETMINY", "SAFEAREA_GETMAXX", "SAFEAREA_GETMAXY",
})

TARGET_EFFECT_BY_REQUEST = {
    **{name: "dynamic" for name in TARGET_RESULT_REQUESTS},
    "IF_CHILDREN_FIND": "dynamic",
    "IF_CHILDREN_COLLECT": "dynamic",
    "MINIMENU_FINDCOMPONENT": "both",
}

# Only these requests are currently proven safe to sit between observer
# barriers.  Everything else with no result is conservatively external.  This
# deliberately makes a newly-added void opcode a hard boundary until reviewed.
BUFFERABLE_STATE_WRITES = frozenset({
    "POP_VAR", "POP_VARBIT", "POP_VARC_INT", "POP_VARC_STRING_OLD",
    "POP_VARC_STRING",
})
BUFFERABLE_COMPONENT_OPCODE_RANGES = (
    (1000, 1439),
    (1704, 1704),
    (2000, 2439),
    (2704, 2704),
)
NON_BUFFERABLE_COMPONENT_REQUESTS = frozenset({
    "CC_RESUME_PAUSEBUTTON", "IF_RESUME_PAUSEBUTTON",
})


def balanced_calls(text: str) -> list[str]:
    calls: list[str] = []
    cursor = 0
    while True:
        start = text.find(CALL, cursor)
        if start < 0:
            return calls
        pos = start + len(CALL)
        depth = 1
        quote: str | None = None
        while pos < len(text) and depth:
            char = text[pos]
            if quote:
                if char == "\\":
                    pos += 2
                    continue
                if char == quote:
                    quote = None
            elif char in "'\"":
                quote = char
            elif char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
            pos += 1
        if depth:
            raise ValueError("unterminated CS2VM_HOST_REQUEST_KIND")
        calls.append(text[start + len(CALL) : pos - 1])
        cursor = pos


def split_top(value: str) -> list[str]:
    result: list[str] = []
    depth = 0
    start = 0
    for index, char in enumerate(value):
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
        elif char == "," and depth == 0:
            result.append(value[start:index].strip())
            start = index + 1
    result.append(value[start:].strip())
    return result


def parse_field(declaration: str, field_names: set[str]) -> Field:
    declaration = " ".join(declaration.split())
    match = re.search(r"([A-Za-z_][A-Za-z0-9_]*)(\s*(?:\[[^]]+\])*)$", declaration)
    if not match:
        raise ValueError(f"cannot parse request field: {declaration}")
    name = match.group(1)
    suffix = re.sub(r"\s+", "", match.group(2))
    prefix = declaration[: match.start(1)].strip()

    count_field = None
    if name == "trigger_ids":
        count_field = "trigger_count"
    elif name in {"key_chars", "key_codes"}:
        count_field = "pair_count"
    elif name == "args" and "arg_count" in field_names:
        count_field = "arg_count"
    elif name == "int_args" and "int_arg_count" in field_names:
        count_field = "int_arg_count"
    elif name == "str_args" and "str_arg_count" in field_names:
        count_field = "str_arg_count"

    if suffix:
        dimensions = re.findall(r"\[([^]]+)\]", suffix)
        if prefix == "int" and len(dimensions) == 1:
            return Field(declaration, name, "CS2W_FIELD_I32_ARRAY", dimensions[0], count_field)
        if prefix == "char" and len(dimensions) == 2:
            return Field(
                declaration,
                name,
                "CS2W_FIELD_STRING_ARRAY",
                dimensions[0],
                count_field,
                dimensions[1],
            )
        raise ValueError(f"unsupported array request field: {declaration}")

    if prefix == "int*":
        return Field(declaration, name, "CS2W_FIELD_I32_POINTER", "0", count_field)
    if prefix in {"char*", "char const*"}:
        return Field(declaration, name, "CS2W_FIELD_STRING", "1")
    if prefix == "bool":
        return Field(declaration, name, "CS2W_FIELD_BOOL", "1")
    if prefix == "unsigned char":
        return Field(declaration, name, "CS2W_FIELD_U8", "1")
    if prefix == "uint64_t":
        return Field(declaration, name, "CS2W_FIELD_U64", "2")
    if prefix == "int" or prefix.startswith("enum "):
        return Field(declaration, name, "CS2W_FIELD_I32", "1")
    raise ValueError(f"unsupported request field type: {declaration}")


def parse(source: Path) -> list[Request]:
    requests: list[Request] = []
    for call in balanced_calls(source.read_text()):
        parts = split_top(call)
        if len(parts) != 3:
            raise ValueError(f"expected three request arguments: {call[:80]}")
        name, opcode_text, fields_text = parts
        if not re.fullmatch(r"[A-Z0-9_]+", name):
            raise ValueError(f"invalid request name: {name}")
        opcode = int(opcode_text, 0)
        if not (fields_text.startswith("(") and fields_text.endswith(")")):
            raise ValueError(f"invalid request fields for {name}")
        declarations = tuple(
            declaration.strip()
            for declaration in fields_text[1:-1].split(";")
            if declaration.strip()
        )
        names = {
            match.group(1)
            for declaration in declarations
            if (match := re.search(r"([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[[^]]+\])*$", declaration))
        }
        requests.append(Request(name, opcode, tuple(parse_field(item, names) for item in declarations)))
    if not requests:
        raise ValueError(f"no requests found in {source}")
    opcodes = [request.opcode for request in requests]
    if opcodes != sorted(opcodes):
        raise ValueError("request manifest must remain in opcode order")
    if len(opcodes) != len(set(opcodes)):
        raise ValueError("request manifest contains duplicate opcodes")
    names = [request.name for request in requests]
    if len(names) != len(set(names)):
        raise ValueError("request manifest contains duplicate names")
    return requests


def parse_command_signatures(source: Path) -> dict[int, CommandSignature]:
    """Read stack shapes from the checked-in generated command catalog."""
    commands: dict[int, CommandSignature] = {}
    for opcode_text, name, stack_input, stack_output in COMMAND_ROW.findall(
        source.read_text(encoding="utf-8")
    ):
        opcode = int(opcode_text)
        if opcode in commands:
            raise ValueError(f"duplicate command signature for opcode {opcode}")
        if not STACK_PATTERN.fullmatch(stack_input):
            raise ValueError(f"invalid stack input pattern for opcode {opcode}: {stack_input!r}")
        if not STACK_PATTERN.fullmatch(stack_output):
            raise ValueError(f"invalid stack output pattern for opcode {opcode}: {stack_output!r}")
        commands[opcode] = CommandSignature(name, stack_input, stack_output)
    if not commands:
        raise ValueError(f"no command signatures found in {source}")
    return commands


def parse_decode_operands(source: Path) -> dict[int, str]:
    """Read raw Dat2 operand widths from the generated decoder authority."""
    operands: dict[int, str] = {}
    for opcode_text, kind_text in DECODE_OPERAND_ROW.findall(
        source.read_text(encoding="utf-8")
    ):
        opcode = int(opcode_text)
        if opcode in operands:
            raise ValueError(f"duplicate decode operand for opcode {opcode}")
        operands[opcode] = OPERAND_KINDS[int(kind_text)]
    if not operands:
        raise ValueError(f"no decode operands found in {source}")
    return operands


def parse_native_wire_rule(source: Path) -> WireOperandRule:
    """Parse the small precedence rule used by the production C decoder."""
    text = source.read_text(encoding="utf-8")
    defines = {
        name: int(value, 0)
        for name, value in re.findall(
            r"^#define\s+([A-Z0-9_]+)\s+((?:0x)?[0-9A-Fa-f]+)\s*$", text, re.MULTILINE
        )
    }
    function = re.search(
        r"static\s+bool\s+cs2_operand_uses_int8\s*\(int\s+opcode\)\s*"
        r"\{\s*return\s+(.+?);\s*\}",
        text,
        re.DOTALL,
    )
    if function is None:
        raise ValueError(f"cannot locate cs2_operand_uses_int8 in {source}")
    terms = [" ".join(term.split()) for term in function.group(1).split("||")]
    int8_from: int | None = None
    exact: set[int] = set()
    for term in terms:
        threshold = re.fullmatch(r"opcode\s*>=\s*(\d+)", term)
        if threshold:
            if int8_from is not None:
                raise ValueError("native wire operand rule has multiple thresholds")
            int8_from = int(threshold.group(1))
            continue
        equality = re.fullmatch(r"opcode\s*==\s*([A-Z0-9_]+|\d+)", term)
        if equality:
            token = equality.group(1)
            if token.isdigit():
                exact.add(int(token))
            elif token in defines:
                exact.add(defines[token])
            else:
                raise ValueError(f"native wire operand rule uses unknown macro {token}")
            continue
        raise ValueError(f"unsupported native wire operand rule term: {term!r}")
    if int8_from is None:
        raise ValueError("native wire operand rule has no opcode threshold")
    return WireOperandRule(int8_from, frozenset(exact))


def parse_executable_reviews(source: Path) -> dict[int, ExecutableReview]:
    """Read the deliberately handwritten executable-Host allowlist.

    Stack metadata is useful for auditing but is not executable evidence.  A
    row enters this file only after its TypeScript adapter has been checked
    against the named native function, and the generator validates the exact
    opcode/name pair before setting the routing bit.
    """
    document = json.loads(source.read_text(encoding="utf-8"))
    if not isinstance(document, dict) or set(document) != {"schema", "reviewed"}:
        raise ValueError("executable Host review manifest must contain schema and reviewed")
    if document["schema"] != "cs2dom-host-executable-semantics/1":
        raise ValueError("unsupported executable Host review schema")
    rows = document["reviewed"]
    if not isinstance(rows, list):
        raise ValueError("executable Host reviewed rows must be an array")
    reviews: dict[int, ExecutableReview] = {}
    names: set[str] = set()
    for row in rows:
        if not isinstance(row, dict) or set(row) != {
            "opcode", "name", "adapter", "nativeReference"
        }:
            raise ValueError("malformed executable Host review row")
        opcode = row["opcode"]
        name = row["name"]
        adapter = row["adapter"]
        native_reference = row["nativeReference"]
        if not isinstance(opcode, int) or opcode < 0:
            raise ValueError("executable Host review opcode must be a non-negative integer")
        if not isinstance(name, str) or not re.fullmatch(r"[A-Z0-9_]+", name):
            raise ValueError(f"invalid executable Host review name: {name!r}")
        if not isinstance(adapter, str) or not adapter:
            raise ValueError(f"executable Host review {name} has no adapter")
        if not isinstance(native_reference, str) or not native_reference:
            raise ValueError(f"executable Host review {name} has no native reference")
        if opcode in reviews:
            raise ValueError(f"duplicate executable Host review opcode {opcode}")
        if name in names:
            raise ValueError(f"duplicate executable Host review name {name}")
        reviews[opcode] = ExecutableReview(opcode, name, adapter, native_reference)
        names.add(name)
    return reviews


def wire_operand(opcode: int, table_operand: str, rule: WireOperandRule) -> str:
    # Native consults this rule before the generated table.  Some legacy table
    # overrides are therefore intentionally unreachable.
    return "int8" if opcode >= rule.int8_from or opcode in rule.int8_opcodes else table_operand


def result_type(pattern: str) -> tuple[str, str]:
    if not pattern:
        return "void", "void"
    types = tuple("number" if item == "i" else "string" if item == "s"
                  else "number | string" for item in pattern)
    if len(types) == 1:
        kind = "polymorphic" if pattern == "?" else "int" if pattern == "i" else "string"
        return kind, types[0]
    return "tuple", "readonly [" + ", ".join(types) + "]"


def logical_result(request: Request, command: CommandSignature) -> tuple[str, str, str, str]:
    """Return logical Host result kind/type and the actual VM stack pattern."""
    stack_output = RESULT_PATTERN_OVERRIDES.get(request.name, command.stack_output)
    if request.name == "DB_GETFIELD":
        return "db-field", "CS2HostDynamicStackResult", "?", "wasm-adapter-special-case"
    if request.name in CHILD_ITERATOR_REQUESTS:
        return (
            "children", "readonly CS2HostComponentRef[]", stack_output,
            "wasm-adapter-special-case",
        )
    if request.name in TARGET_RESULT_REQUESTS:
        return (
            "component", "CS2HostComponentRef | null", stack_output,
            "wasm-adapter-special-case",
        )
    if request.name in POLYMORPHIC_RESULT_REQUESTS:
        return (
            "polymorphic", "number | string", "?", "wasm-adapter-special-case",
        )
    kind, ts_type = result_type(stack_output)
    source = ("wasm-adapter-override" if request.name in RESULT_PATTERN_OVERRIDES
              else "generated-command-metadata")
    return kind, ts_type, stack_output, source


def barrier_for(request: Request, result_kind: str) -> str:
    if request.name in TOPOLOGY_REQUESTS:
        return "topology"
    if request.name in GEOMETRY_REQUESTS:
        return "geometry"
    if result_kind != "void":
        return "read"
    if request.name in BUFFERABLE_STATE_WRITES:
        return "none"
    if request.name not in NON_BUFFERABLE_COMPONENT_REQUESTS and any(
        low <= request.opcode <= high
        for low, high in BUFFERABLE_COMPONENT_OPCODE_RANGES
    ):
        return "none"
    return "external"


def build_semantics(
    requests: list[Request],
    commands: dict[int, CommandSignature],
    operands: dict[int, str],
    wire_rule: WireOperandRule,
    executable_reviews: dict[int, ExecutableReview] | None = None,
) -> list[RequestSemantics]:
    executable_reviews = executable_reviews or {}
    request_by_opcode = {request.opcode: request for request in requests}
    for opcode, review in executable_reviews.items():
        request = request_by_opcode.get(opcode)
        if request is None:
            raise ValueError(f"executable Host review opcode {opcode} is absent from schema")
        if request.name != review.name:
            raise ValueError(
                f"executable Host review {opcode} names {review.name}, expected {request.name}"
            )
    semantics: list[RequestSemantics] = []
    for request in requests:
        command = commands.get(request.opcode)
        if command is None:
            raise ValueError(
                f"HOST request {request.name} ({request.opcode}) has no command signature"
            )
        table_operand = operands.get(request.opcode)
        if table_operand is None:
            raise ValueError(
                f"HOST request {request.name} ({request.opcode}) has no Dat2 operand form"
            )
        operand = wire_operand(request.opcode, table_operand, wire_rule)
        result_kind, ts_type, stack_output, result_source = logical_result(request, command)
        barrier = barrier_for(request, result_kind)
        target_effect = TARGET_EFFECT_BY_REQUEST.get(request.name, "none")
        semantics.append(RequestSemantics(
            request=request,
            command=command,
            operand=operand,
            barrier=barrier,
            target_effect=target_effect,
            result_kind=result_kind,
            result_type=ts_type,
            stack_output=stack_output,
            result_source=result_source,
            executable_reviewed=request.opcode in executable_reviews,
        ))

    expected_names = {request.name for request in requests}
    for label, classified in (
        ("target-result", TARGET_RESULT_REQUESTS),
        ("child-iterator", CHILD_ITERATOR_REQUESTS),
        ("polymorphic-result", POLYMORPHIC_RESULT_REQUESTS),
        ("result override", frozenset(RESULT_PATTERN_OVERRIDES)),
        ("topology", TOPOLOGY_REQUESTS),
        ("geometry", GEOMETRY_REQUESTS),
        ("bufferable state write", BUFFERABLE_STATE_WRITES),
        ("non-bufferable component", NON_BUFFERABLE_COMPONENT_REQUESTS),
    ):
        stale = sorted(classified - expected_names)
        if stale:
            raise ValueError(f"{label} classification names absent from schema: {', '.join(stale)}")

    if len(semantics) != len(requests):
        raise ValueError("not every HOST request received semantics")
    valid_barriers = {"none", "read", "topology", "geometry", "external"}
    invalid = [item.request.name for item in semantics if item.barrier not in valid_barriers]
    if invalid:
        raise ValueError(f"HOST requests have invalid barriers: {', '.join(invalid)}")
    return semantics


def offset(request: Request, field: str) -> str:
    return (
        f"offsetof(struct CS2VM_HostRequest, u.{request.name}) + "
        f"offsetof(struct CS2VM_HostRequest_{request.name}, {field})"
    )


def emit(requests: list[Request], output: Path, source: Path) -> None:
    lines = [
        "/* Generated by wasm/gen_host_schema.py; do not edit. */",
        f"/* Source: {source.name} */",
        "",
    ]
    for request in requests:
        lines.append(f"static const struct CS2W_FieldDescriptor cs2w_fields_{request.name}[] = {{")
        for field in request.fields:
            count_offset = offset(request, field.count_field) if field.count_field else "CS2W_NO_OFFSET"
            lines.append(
                "    { "
                f'"{field.name}", {field.kind}, {offset(request, field.name)}, '
                f"{field.capacity}, {field.stride}, {count_offset} "
                "},"
            )
        lines.append("};")
        lines.append("")

    lines.extend(
        [
            "static const struct CS2W_RequestDescriptor cs2w_requests[] = {",
        ]
    )
    for request in requests:
        lines.append(
            "    { "
            f"{request.opcode}, \"{request.name}\", cs2w_fields_{request.name}, "
            f"(int)(sizeof(cs2w_fields_{request.name}) / sizeof(cs2w_fields_{request.name}[0])) "
            "},"
        )
    lines.extend(["};", ""])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines))


def emit_js(requests: list[Request], output: Path, source: Path) -> None:
    """Emit the browser-side name/opcode catalog from the same ABI manifest."""
    lines = [
        "/* Generated by wasm/gen_host_schema.py; do not edit. */",
        f"/* Source: {source.name} */",
        "export const CS2_HOST_REQUESTS = Object.freeze([",
    ]
    lines.extend(f'    Object.freeze([{request.opcode}, "{request.name}"]),' for request in requests)
    lines.extend([
        "]);",
        "export const CS2_HOST_REQUEST_NAMES = Object.freeze(",
        "    CS2_HOST_REQUESTS.map(([, name]) => name));",
        "",
    ])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines))


def ts_field_name(field: Field) -> str:
    # A request's discriminant is also named `kind`; preserve the reflected
    # adapter's collision-free spelling for that payload member.
    return "value_kind" if field.name == "kind" else field.name


def ts_field_type(field: Field) -> str:
    if field.kind in {"CS2W_FIELD_I32", "CS2W_FIELD_U8"}:
        return "number"
    if field.kind == "CS2W_FIELD_BOOL":
        return "boolean"
    if field.kind == "CS2W_FIELD_STRING":
        return "string | null"
    if field.kind in {"CS2W_FIELD_I32_ARRAY", "CS2W_FIELD_I32_POINTER"}:
        return "readonly number[]"
    if field.kind == "CS2W_FIELD_U64":
        return "readonly [low: number, high: number]"
    if field.kind == "CS2W_FIELD_STRING_ARRAY":
        return "readonly string[]"
    raise ValueError(f"unsupported TypeScript field kind: {field.kind}")


def quoted(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def emit_ts(
    semantics: list[RequestSemantics],
    output: Path,
    source: Path,
    commands_source: Path,
    operands_source: Path,
    native_decoder_source: Path,
    executable_reviews_source: Path | None = None,
) -> None:
    """Emit the reviewable logical Host API and complete routing catalog."""
    barrier_counts = {
        barrier: sum(item.barrier == barrier for item in semantics)
        for barrier in ("none", "read", "topology", "geometry", "external")
    }
    lines = [
        "/* Generated by wasm/gen_host_schema.py; do not edit.",
        f" * Requests: {source.name} ({len(semantics)} rows).",
        f" * Stack shapes: {commands_source.name}.",
        f" * Raw Dat2 operands: {operands_source.name} + {native_decoder_source.name} precedence.",
        (f" * Executable Host reviews: {executable_reviews_source.name}."
         if executable_reviews_source else " * Executable Host reviews: none."),
        " * Command stack rows may be inferred; executableReviewed is the mandatory TS gate.",
        " * Production C/WASM routing does not consume this migration artifact yet. */",
        "",
        'export type CS2HostOperandKind = "none" | "int8" | "int32" | "string";',
        'export type CS2HostBarrierKind = "none" | "read" | "topology" | "geometry" | "external";',
        'export type CS2HostTargetEffect = "none" | "active" | "dot" | "both" | "dynamic";',
        'export type CS2HostResultKind = "void" | "int" | "string" | "tuple" |',
        '    "polymorphic" | "component" | "children" | "db-field";',
        'export type CS2HostStackSource = "generated-command-metadata";',
        'export type CS2HostResultSource = "generated-command-metadata" |',
        '    "wasm-adapter-override" | "wasm-adapter-special-case";',
        "",
        "export interface CS2HostComponentRef {",
        "    readonly componentId: number;",
        "    readonly subId?: number;",
        "    readonly generation?: number;",
        "}",
        "",
        "export interface CS2HostDynamicStackResult {",
        '    readonly pattern: string; // non-empty sequence of "i" and "s"',
        "    readonly values: readonly (number | string)[];",
        "}",
        "",
        "export interface CS2HostRequestMetadata {",
        "    readonly opcode: number;",
        "    readonly name: CS2HostRequestKind;",
        "    readonly operand: CS2HostOperandKind;",
        "    readonly stackInput: string;",
        "    readonly stackOutput: string;",
        "    readonly stackSource: CS2HostStackSource;",
        "    readonly resultSource: CS2HostResultSource;",
        "    readonly barrier: CS2HostBarrierKind;",
        "    readonly targetEffect: CS2HostTargetEffect;",
        "    readonly resultKind: CS2HostResultKind;",
        "    /** Schema/catalog presence alone never authorizes TS execution. */",
        "    readonly executableReviewed: boolean;",
        "}",
        "",
        "export const CS2_HOST_OPCODE_BY_KIND = {",
    ]
    for item in semantics:
        lines.append(f"    {item.request.name}: {item.request.opcode},")
    lines += [
        "} as const;",
        "",
        "export type CS2HostRequestKind = keyof typeof CS2_HOST_OPCODE_BY_KIND;",
        "export type CS2HostOpcode = (typeof CS2_HOST_OPCODE_BY_KIND)[CS2HostRequestKind];",
        "",
        "export interface CS2HostRequestPayloadByKind {",
    ]
    for item in semantics:
        request = item.request
        lines.append(f"    readonly {request.name}: {{")
        for field in request.fields:
            lines.append(f"        readonly {ts_field_name(field)}: {ts_field_type(field)};")
        lines.append("    };")
    lines += [
        "}",
        "",
        "export interface CS2HostArgumentTupleByKind {",
    ]
    for item in semantics:
        fields = ", ".join(
            f"{ts_field_name(field)}: {ts_field_type(field)}"
            for field in item.request.fields
        )
        lines.append(f"    readonly {item.request.name}: readonly [{fields}];")
    lines += [
        "}",
        "",
        "export interface CS2HostResultByKind {",
    ]
    for item in semantics:
        lines.append(f"    readonly {item.request.name}: {item.result_type};")
    lines += [
        "}",
        "",
        "/** Specialized, allocation-free call surface for the TypeScript VM. */",
        "export type CS2Host = {",
        "    readonly [K in CS2HostRequestKind]: (",
        "        ...args: CS2HostArgumentTupleByKind[K]",
        "    ) => CS2HostResultByKind[K];",
        "};",
        "",
        "/** Tagged shape retained for the C/WASM adapter and trace tooling. */",
        "export type CS2HostRequest<K extends CS2HostRequestKind = CS2HostRequestKind> = {",
        "    readonly [P in K]: Readonly<{",
        "        kind: P;",
        "        opcode: (typeof CS2_HOST_OPCODE_BY_KIND)[P];",
        "        payload: CS2HostRequestPayloadByKind[P];",
        "    }>;",
        "}[K];",
        "",
        "export type CS2HostResult<K extends CS2HostRequestKind> = CS2HostResultByKind[K];",
        "",
        "export const CS2_HOST_REQUEST_METADATA = [",
    ]
    for item in semantics:
        request = item.request
        lines += [
            "    {",
            f"        opcode: {request.opcode},",
            f"        name: {quoted(request.name)},",
            f"        operand: {quoted(item.operand)},",
            f"        stackInput: {quoted(item.command.stack_input)},",
            f"        stackOutput: {quoted(item.stack_output)},",
            '        stackSource: "generated-command-metadata",',
            f"        resultSource: {quoted(item.result_source)},",
            f"        barrier: {quoted(item.barrier)},",
            f"        targetEffect: {quoted(item.target_effect)},",
            f"        resultKind: {quoted(item.result_kind)},",
            f"        executableReviewed: {str(item.executable_reviewed).lower()},",
        ]
        lines += ["    },"]
    lines += [
        "] as const satisfies readonly CS2HostRequestMetadata[];",
        "",
        "export const CS2_HOST_REQUEST_METADATA_BY_OPCODE: Readonly<",
        "    Partial<Record<number, CS2HostRequestMetadata>>",
        "> = Object.freeze({",
    ]
    for index, item in enumerate(semantics):
        lines.append(f"    {item.request.opcode}: CS2_HOST_REQUEST_METADATA[{index}],")
    lines += [
        "});",
        "",
        "export const CS2_HOST_REQUEST_METADATA_BY_KIND: Readonly<",
        "    Record<CS2HostRequestKind, CS2HostRequestMetadata>",
        "> = Object.freeze({",
    ]
    for index, item in enumerate(semantics):
        lines.append(f"    {item.request.name}: CS2_HOST_REQUEST_METADATA[{index}],")
    lines += ["});", "", "export const CS2_HOST_BARRIER_OPCODES = Object.freeze({"]
    for barrier in ("none", "read", "topology", "geometry", "external"):
        opcodes = ", ".join(
            str(item.request.opcode) for item in semantics if item.barrier == barrier
        )
        lines.append(f"    {barrier}: Object.freeze([{opcodes}]),")
    lines += [
        "} as const);",
        "",
        "export const CS2_HOST_BARRIER_COUNTS = Object.freeze({",
    ]
    for barrier, count in barrier_counts.items():
        lines.append(f"    {barrier}: {count},")
    lines += [
        "} as const);",
        "",
        "/**",
        " * Whole-closure routing must call this gate before selecting TS Host",
        " * execution. A true row has an explicit native-reference review in",
        " * the executable semantics manifest; schema presence alone is false.",
        " */",
        "export function cs2HostOpcodeHasReviewedExecutableSemantics(opcode: number): boolean {",
        "    return CS2_HOST_REQUEST_METADATA_BY_OPCODE[opcode]?.executableReviewed === true;",
        "}",
        "",
    ]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--format", choices=("c", "js", "ts"), default="c")
    parser.add_argument(
        "--commands",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "src" / "cs2_commands.js",
        help="generated command stack catalog used by --format ts",
    )
    parser.add_argument(
        "--operands",
        type=Path,
        default=(Path(__file__).resolve().parents[3] / "src" / "osrs" / "rscache" /
                 "dat2a" / "dat2a_cs2_opcode_decode.c"),
        help="generated Dat2 operand table used by --format ts",
    )
    parser.add_argument(
        "--native-decoder",
        type=Path,
        default=(Path(__file__).resolve().parents[3] / "3rd" / "rscache" / "src" /
                 "datatypes" / "clientscript.c"),
        help="production C decoder whose int8 precedence is used by --format ts",
    )
    parser.add_argument(
        "--executable-semantics",
        type=Path,
        default=Path(__file__).resolve().parent / "cs2_host_executable_semantics.json",
        help="hand-reviewed native-to-TypeScript Host executable allowlist",
    )
    args = parser.parse_args()
    requests = parse(args.source)
    if args.format == "js":
        emit_js(requests, args.output, args.source)
    elif args.format == "ts":
        commands = parse_command_signatures(args.commands)
        operands = parse_decode_operands(args.operands)
        wire_rule = parse_native_wire_rule(args.native_decoder)
        executable_reviews = parse_executable_reviews(args.executable_semantics)
        semantics = build_semantics(
            requests, commands, operands, wire_rule, executable_reviews,
        )
        emit_ts(
            semantics,
            args.output,
            args.source,
            args.commands,
            args.operands,
            args.native_decoder,
            args.executable_semantics,
        )
    else:
        emit(requests, args.output, args.source)


if __name__ == "__main__":
    main()
