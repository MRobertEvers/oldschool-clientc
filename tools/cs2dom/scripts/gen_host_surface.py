#!/usr/bin/env python3
"""Generate the HostKernel surface every CS2 back end calls.

The generated JavaScript names a method for every command the browser runtime
has to answer, with its arguments in the order a call site supplies them, the
values it returns, and whether it can park.

The source is the DECOMPILER's command table, not the C VM's request manifest,
and the difference matters. `cs2vm2_host_request_kinds.def` describes the C
struct a request is marshalled into — its field order is C's convenience, and
for the `if_*` forms it puts the component first where the bytecode pushes it
last. The decompiler's table describes the *call*: `cs2_proto_pool` lists each
argument's prototype in push order, which is exactly the order the AST hands
them to the emitter and therefore the order the JavaScript method receives
them. Generating from the request manifest would produce a surface whose
argument names are subtly transposed against the code calling it.

Usage:
    python3 tools/cs2dom/scripts/gen_host_surface.py [--check]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
COMMAND_TABLE = REPO / "3rd" / "rscache" / "src" / "cs2" / "cs2_command.gen.h"
TYPES_HEADER = REPO / "3rd" / "rscache" / "src" / "cs2" / "cs2_types.h"
TYPES_SOURCE = REPO / "3rd" / "rscache" / "src" / "cs2" / "cs2_types.c"
PARK_TABLE = REPO / "tools" / "cs2dom" / "src" / "generated" / "cs2_host_park.js"
OUTPUT = REPO / "tools" / "cs2dom" / "src" / "generated" / "cs2_host_surface.js"

# Command kinds the DECOMPILER resolves away — a switch becomes a switch, a
# branch becomes an if, an assign becomes an assignment. None of them survive
# into the syntax tree as an operation, so none is a method anyone can call.
# Filtering by kind rather than by an opcode list means a newly-named opcode of
# a structural kind cannot slip in as a phantom host method.
STRUCTURAL_KINDS = {
    "unknown",
    "switch",
    "branch",
    "branch_compare",
    "proc",
    "return",
    "discard",
    "assign",
    "join_string",
    "define_array",
    "push_array_int",
    "pop_array_int",
}

# NOT structural, despite the name: RSCACHE_CS2_CMD_CLIENTSCRIPT is the
# `if_seton*` family. The kind describes their ARGUMENT — a script binding
# rather than a value — not their role. They are host operations like any
# other, and leaving them out produced a surface with no way to install a
# hook, which is most of what interface scripts do.

# Operations the emitter computes with an intrinsic instead of asking the host.
# This must stay in step with cs2_js_emit.js's INTRINSICS map plus its handful
# of statement shapes; a mismatch shows up as a surface advertising a method
# nothing ever calls, which `--check` in CI will not catch on its own.
INTRINSIC_OPCODES = {
    63, 8000,
    4000, 4001, 4002, 4003, 4006, 4007, 4008, 4009, 4010, 4011, 4012, 4013,
    4014, 4015, 4016, 4017, 4018, 4025, 4026, 4029, 4030, 4035, 4036,
    4100, 4101, 4103, 4106, 4107, 4111, 4112, 4113, 4114, 4116, 4117, 4118,
    4119, 4120, 4121, 4122,
    8003, 8023, 8024,
}


def read(path: Path) -> str:
    if not path.exists():
        sys.exit(f"missing input: {path}")
    return path.read_text(encoding="utf-8")


def parse_proto_stack_kinds(text: str) -> dict[str, str]:
    """RSCACHE_CS2_PROTO_X -> 'i' or 's', the stack the value lives on.

    Only `RSCACHE_CS2_TYPE_STRING` is a string — `RSCache_CS2_TypeStackType`
    says so and everything else, `char` and `mes` and `coord` included, is an
    int. This is what an unimplemented command needs in order to fake its
    results the way the reference does: the count alone would push an int
    where a string belongs and unbalance the caller.
    """
    block = re.search(r"cs2_prototypes\[RSCACHE_CS2_PROTO_COUNT_\] = \{(.*?)\n\};", text, re.S)
    if not block:
        sys.exit("could not find cs2_prototypes")
    kinds: dict[str, str] = {}
    for symbol, plain in re.findall(
        r"\[(RSCACHE_CS2_PROTO_[A-Z0-9_]+)\] = CS2_PLAIN\((\w+)\)", block.group(1)
    ):
        kinds[symbol] = "s" if plain == "STRING" else "i"
    for symbol, named in re.findall(
        r"\[(RSCACHE_CS2_PROTO_[A-Z0-9_]+)\] = CS2_NAMED\((\w+),", block.group(1)
    ):
        kinds[symbol] = "s" if named == "STRING" else "i"
    for symbol, literal in re.findall(
        r"\[(RSCACHE_CS2_PROTO_[A-Z0-9_]+)\] = \{\s*RSCACHE_CS2_TYPE_(\w+)", block.group(1)
    ):
        kinds.setdefault(symbol, "s" if literal == "STRING" else "i")
    return kinds


def parse_proto_names(text: str) -> dict[str, str]:
    """RSCACHE_CS2_PROTO_X -> the lowercase word a JavaScript parameter uses."""
    block = re.search(r"enum RSCache_CS2_ProtoId\s*\{(.*?)\n\};", text, re.S)
    if not block:
        sys.exit("could not find enum RSCache_CS2_ProtoId")
    names: dict[str, str] = {}
    for match in re.finditer(r"(RSCACHE_CS2_PROTO_[A-Z0-9_]+)", block.group(1)):
        symbol = match.group(1)
        names[symbol] = symbol[len("RSCACHE_CS2_PROTO_") :].lower()
    return names


def parse_proto_pool(text: str) -> list[str]:
    block = re.search(r"cs2_proto_pool\[\] = \{(.*?)\n\};", text, re.S)
    if not block:
        sys.exit("could not find cs2_proto_pool")
    return re.findall(r"RSCACHE_CS2_PROTO_[A-Z0-9_]+", block.group(1))


def parse_command_table(text: str) -> dict[int, dict]:
    block = re.search(r"cs2_command_table\[\] = \{(.*?)\n\};", text, re.S)
    if not block:
        sys.exit("could not find cs2_command_table")
    rows: dict[int, dict] = {}
    pattern = re.compile(
        r"\[(\d+)\] = \{\s*(NULL|\"[^\"]*\")\s*,\s*(RSCACHE_CS2_CMD_\w+)\s*,"
        r"\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(true|false)"
    )
    for match in pattern.finditer(block.group(1)):
        name = None if match.group(2) == "NULL" else match.group(2).strip('"')
        rows[int(match.group(1))] = {
            "name": name,
            "kind": match.group(3)[len("RSCACHE_CS2_CMD_") :].lower(),
            "arg_offset": int(match.group(4)),
            "arg_count": int(match.group(5)),
            "def_offset": int(match.group(6)),
            "def_count": int(match.group(7)),
            "dot_capable": match.group(8) == "true",
        }
    if not rows:
        sys.exit("parsed no command rows")
    return rows


def parse_park_classes(text: str) -> dict[int, str]:
    return {
        int(match.group(1)): match.group(2)
        for match in re.finditer(r"\[(\d+), '([a-z]+)'\]", text)
    }


def parameter_names(protos: list[str], proto_names: dict[str, str]) -> list[str]:
    """Argument names, deduplicated, in call order."""
    used: dict[str, int] = {}
    out: list[str] = []
    for proto in protos:
        base = proto_names.get(proto, "arg")
        base = re.sub(r"[^a-z0-9]", "", base) or "arg"
        if base[0].isdigit():
            base = f"a{base}"
        count = used.get(base, 0)
        used[base] = count + 1
        out.append(base if count == 0 else f"{base}{count + 1}")
    return out


def method_name(command: str, dot: bool) -> str:
    return f"dot_{command}" if dot else command


def generate() -> str:
    table_text = read(COMMAND_TABLE)
    proto_names = parse_proto_names(read(TYPES_HEADER))
    proto_kinds = parse_proto_stack_kinds(read(TYPES_SOURCE))
    pool = parse_proto_pool(table_text)
    rows = parse_command_table(table_text)
    park = parse_park_classes(read(PARK_TABLE))

    methods = []
    for opcode in sorted(rows):
        row = rows[opcode]
        if row["kind"] in STRUCTURAL_KINDS or opcode in INTRINSIC_OPCODES:
            continue
        # An opcode with neither a name nor a signature is a table hole, not a
        # command; the decompiler refuses those before code generation.
        if row["name"] is None and row["arg_count"] == 0 and row["def_count"] == 0:
            continue
        command = row["name"] or f"op{opcode}"
        if row["name"] and row["name"].startswith("_"):
            command = f"op{opcode}"

        args = pool[row["arg_offset"] : row["arg_offset"] + row["arg_count"]]
        defs = pool[row["def_offset"] : row["def_offset"] + row["def_count"]]
        methods.append(
            {
                "opcode": opcode,
                "method": method_name(command, dot=False),
                "command": command,
                "kind": row["kind"],
                "params": parameter_names(args, proto_names),
                "results": [proto_names.get(p, "int") for p in defs],
                # One 'i' or 's' per result. An unimplemented command fakes
                # its results from this, the way the reference's stack stub
                # does; the names above cannot say which stack a value is on.
                "resultKinds": "".join(proto_kinds.get(p, "i") for p in defs),
                "park": park.get(opcode),
                "dotCapable": row["dot_capable"],
            }
        )

    lines = [
        "/* Generated by tools/cs2dom/scripts/gen_host_surface.py — do not edit.",
        " * Source of truth: 3rd/rscache/src/cs2/cs2_command.gen.h (the decompiler's",
        " * command table) and generated/cs2_host_park.js.",
        " *",
        " * One row per method the HostKernel must answer. `params` are in CALL order —",
        " * the order the bytecode pushed them and therefore the order generated code",
        " * passes them — which is not the field order of the C request struct.",
        " * `results` is how many values the call leaves behind: none, one returned",
        " * bare, or several returned as an array. `resultKinds` says which stack",
        " * each of those lives on — 'i' or 's' — which is what an unimplemented",
        " * command needs in order to fake them without unbalancing its caller.",
        " *",
        " * A `dotCapable` command also needs a `dot_<name>` method, which targets the",
        " * dot component instead of the active one.",
        " */",
        "",
        "/** name -> { opcode, params, results, resultKinds, park, dotCapable } */",
        "export const HOST_SURFACE = Object.freeze(new Map([",
    ]
    for entry in methods:
        params = ", ".join(f"'{p}'" for p in entry["params"])
        results = ", ".join(f"'{r}'" for r in entry["results"])
        park = f"'{entry['park']}'" if entry["park"] else "null"
        lines.append(
            f"    ['{entry['method']}', {{ opcode: {entry['opcode']}, "
            f"params: [{params}], results: [{results}], "
            f"resultKinds: '{entry['resultKinds']}', "
            f"park: {park}, dotCapable: {str(entry['dotCapable']).lower()} }}],"
        )
    lines += [
        "]));",
        "",
        "/** Methods a complete HostKernel must implement, including the dot forms. */",
        "export function hostMethodNames() {",
        "    const names = [];",
        "    for( const [name, row] of HOST_SURFACE )",
        "    {",
        "        names.push(name);",
        "        if( row.dotCapable ) names.push(`dot_${name}`);",
        "    }",
        "    return names;",
        "}",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if the file is stale")
    args = parser.parse_args()

    generated = generate()
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text(encoding="utf-8") != generated:
            print(
                f"{OUTPUT} is stale; re-run tools/cs2dom/scripts/gen_host_surface.py",
                file=sys.stderr,
            )
            return 1
        return 0

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(generated, encoding="utf-8")
    print(f"wrote {OUTPUT.relative_to(REPO)} ({generated.count('opcode:')} methods)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
