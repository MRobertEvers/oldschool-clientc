#!/usr/bin/env python3
"""Derive the CS2 park classification from the C client's own yield planner.

A host operation *parks* when it needs something that is not loaded yet. The
C VM expresses that by rolling the opcode back and re-executing it once the
loader finishes (``docs/CS2_EXECUTION.md``); a JavaScript generator expresses
the same contract by suspending and retrying the call. Either way the question
"can this operation park?" has exactly one answer, and it is already written
down — in ``task_cs2_plan_yield``, whose ``default:`` arm aborts precisely
because reaching it means a request yielded that had no business yielding.

So this reads that switch rather than restating it. Everything the planner
gives an explicit arm can park; everything else cannot, on the C client's own
assertion. A hand-kept list would drift the first time an opcode is wired, and
the drift would show up as a browser runtime that silently never loads an
asset.

Usage:
    python3 tools/cs2dom/scripts/gen_host_park.py [--check]

Writes tools/cs2dom/src/generated/cs2_host_park.js. ``--check`` re-generates
into memory and fails if the committed file differs, which is what CI runs.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
PLANNER = REPO / "src" / "game" / "task_cs2_run.c"
KINDS_DEF = REPO / "src" / "cs2vm2" / "cs2vm2_host_request_kinds.def"
OUTPUT = REPO / "tools" / "cs2dom" / "src" / "generated" / "cs2_host_park.js"

# The planner names its load classes by function. The suffix is the class a
# JavaScript driver has to service, so it is the name carried through.
PLAN_FUNCTIONS = {
    "task_cs2_plan_component": "component",
    "task_cs2_plan_pushscript": "script",
    "task_cs2_plan_setgraphic": "sprite",
    "task_cs2_plan_font": "font",
    "task_cs2_plan_setobject": "obj",
    "task_cs2_plan_widget_set_model": "model",
    "task_cs2_plan_widget_set_model_kind": "model",
    "task_cs2_plan_obj": "obj",
    "task_cs2_plan_struct": "struct",
    "task_cs2_plan_enum": "enum",
    "task_cs2_plan_npc": "npc",
    "task_cs2_plan_npc_name": "npc",
    "task_cs2_plan_npc_param": "npc",
    "task_cs2_plan_loc_param": "loc",
    "task_cs2_plan_worldmap": "worldmap",
    "task_cs2_plan_mapelement": "mapelement",
    "task_cs2_plan_db": "db",
    "task_cs2_plan_objall": "obj",
}

# The three families the planner routes by opcode range rather than by a case
# label, named by the predicate that tests them.
RANGE_FAMILIES = {
    "task_cs2_kind_is_worldmap": "worldmap",
    "task_cs2_kind_is_mapelement": "mapelement",
    "task_cs2_kind_is_db": "db",
}


def read(path: Path) -> str:
    if not path.exists():
        sys.exit(f"missing input: {path}")
    return path.read_text(encoding="utf-8")


def parse_request_kinds(text: str) -> dict[str, int]:
    """name -> opcode, from the authoritative request manifest."""
    kinds: dict[str, int] = {}
    for match in re.finditer(
        r"CS2VM_HOST_REQUEST_KIND\(\s*([A-Z0-9_]+)\s*,\s*(-?\d+)\s*,", text
    ):
        kinds[match.group(1)] = int(match.group(2))
    if not kinds:
        sys.exit("parsed no host request kinds")
    return kinds


def parse_planner_cases(text: str) -> dict[str, str]:
    """Request name -> park class, from the explicit arms of the yield switch."""
    body = extract_function(text, "task_cs2_plan_yield")
    cases: dict[str, str] = {}
    pending: list[str] = []
    for line in body.splitlines():
        case = re.search(r"case\s+CS2VM_HOST_REQUEST_([A-Z0-9_]+)\s*:", line)
        if case:
            pending.append(case.group(1))
            continue
        call = re.search(r"(task_cs2_plan_[a-z0-9_]+)\s*\(\s*self\s*\)", line)
        if call and pending:
            plan = call.group(1)
            if plan not in PLAN_FUNCTIONS:
                sys.exit(
                    f"{plan} is a new load class; add it to PLAN_FUNCTIONS with the "
                    "name a JavaScript driver services it under"
                )
            for name in pending:
                cases[name] = PLAN_FUNCTIONS[plan]
            pending = []
    return cases


def parse_range_families(text: str, kinds: dict[str, int]) -> dict[str, str]:
    """Request name -> park class, for the families tested by opcode range."""
    families: dict[str, str] = {}
    for function, park in RANGE_FAMILIES.items():
        body = extract_function(text, function)
        bounds = re.findall(
            r"kind\s*(>=|<=)\s*CS2VM_HOST_REQUEST_([A-Z0-9_]+)", body
        )
        # The predicates are written as one or more `low <= kind <= high` pairs.
        if len(bounds) % 2 != 0:
            sys.exit(f"{function} does not read as low/high opcode range pairs")
        for i in range(0, len(bounds), 2):
            (low_op, low_name), (high_op, high_name) = bounds[i], bounds[i + 1]
            if low_op != ">=" or high_op != "<=":
                sys.exit(f"{function} range pair {i // 2} is not low..high")
            low, high = kinds[low_name], kinds[high_name]
            for name, opcode in kinds.items():
                if low <= opcode <= high:
                    families[name] = park
    return families


def parse_component_family(text: str) -> set[str]:
    """The requests that carry a component id, and so can need its group baked."""
    names = {
        match.group(1)
        for match in re.finditer(
            r"TASK_CS2_COMPONENT_ID_CASE\(\s*([A-Z0-9_]+)\s*,", text
        )
    }
    if not names:
        sys.exit("parsed no component-carrying requests")
    return names


def extract_function(text: str, name: str) -> str:
    """The braced body of a top-level function definition."""
    match = re.search(rf"^{re.escape(name)}\(", text, re.MULTILINE)
    if not match:
        sys.exit(f"could not find {name} in {PLANNER.name}")
    start = text.index("{", match.end())
    depth = 0
    for index in range(start, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start : index + 1]
    sys.exit(f"unterminated body for {name}")


def generate() -> str:
    planner_text = read(PLANNER)
    kinds = parse_request_kinds(read(KINDS_DEF))

    park: dict[str, str] = {}
    park.update(parse_range_families(planner_text, kinds))
    park.update(parse_planner_cases(planner_text))
    # A component-carrying request parks on its group before anything else the
    # planner would do with it, so this family wins where the two overlap —
    # exactly the order `task_cs2_plan_yield` tests them in.
    for name in parse_component_family(planner_text):
        park[name] = "component"

    by_opcode = sorted(
        (kinds[name], name, park[name]) for name in park if name in kinds
    )
    missing = sorted(name for name in park if name not in kinds)
    if missing:
        sys.exit(f"planner names requests absent from the manifest: {missing}")

    classes = sorted({entry[2] for entry in by_opcode})

    lines = [
        "/* Generated by tools/cs2dom/scripts/gen_host_park.py — do not edit.",
        " * Source of truth: src/game/task_cs2_run.c (task_cs2_plan_yield) and",
        " * src/cs2vm2/cs2vm2_host_request_kinds.def.",
        " *",
        " * An operation listed here may PARK: it can need something the runtime has",
        " * not loaded, in which case the host answers HOST_PARK and the caller must",
        " * retry after servicing the named class. An operation absent from this table",
        " * always answers immediately — that is the C planner's own assertion, since",
        " * a request reaching its `default:` arm aborts the script.",
        " */",
        "",
        "/** The sentinel a host method returns instead of an answer it cannot give yet. */",
        "export const HOST_PARK = Symbol('cs2 host park');",
        "",
        "/** Load classes a driver must be able to service. */",
        "export const PARK_CLASSES = Object.freeze([",
    ]
    lines += [f"    '{name}'," for name in classes]
    lines += [
        "]);",
        "",
        "/** CS2 opcode -> the load class that opcode can park on. */",
        "export const PARK_CLASS_BY_OPCODE = Object.freeze(new Map([",
    ]
    for opcode, name, park_class in by_opcode:
        lines.append(f"    [{opcode}, '{park_class}'], /* {name} */")
    lines += [
        "]));",
        "",
        "/** True when this opcode must be called in a retry loop. */",
        "export function opcodeMayPark(opcode) {",
        "    return PARK_CLASS_BY_OPCODE.has(opcode);",
        "}",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check", action="store_true", help="fail if the committed file is stale"
    )
    args = parser.parse_args()

    generated = generate()
    if args.check:
        if not OUTPUT.exists():
            print(f"{OUTPUT} has not been generated", file=sys.stderr)
            return 1
        if OUTPUT.read_text(encoding="utf-8") != generated:
            print(
                f"{OUTPUT} is stale; re-run tools/cs2dom/scripts/gen_host_park.py",
                file=sys.stderr,
            )
            return 1
        return 0

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(generated, encoding="utf-8")
    count = generated.count("], /*")
    print(f"wrote {OUTPUT.relative_to(REPO)} ({count} park-capable opcodes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
