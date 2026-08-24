#!/usr/bin/env python3
"""Generate ABI-stable host-request descriptors for the CS2VM wasm bridge.

The VM deliberately gives its host one tagged C union.  JavaScript must not
duplicate the compiler's struct layout, especially for bool, pointers, and the
large SETON payloads.  This generator turns the authoritative .def manifest
into C metadata whose offsets are still evaluated by the C compiler.
"""

from __future__ import annotations

import argparse
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
    return requests


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


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    emit(parse(args.source), args.output, args.source)


if __name__ == "__main__":
    main()
