#!/usr/bin/env python3
"""
Derive the set of ServerScript opcodes something actually implements.

Why this is generated rather than written down: the answer already exists, twice,
as the `case SS_OP_*:` labels in the VM core and in the server's host command
seam. A hand-kept list beside them is a third copy that goes stale the first time
someone adds an opcode without remembering it — and a *wrong* coverage list is
worse than none, because the gap report built on it would either hide a missing
opcode or cry wolf about an implemented one.

Reading the source means the list cannot disagree with the code it describes.

    python3 torirsserver/gen_opcode_coverage.py            # rewrite the header
    python3 torirsserver/gen_opcode_coverage.py --check    # fail if it is stale

Both dispatchers are plain `switch (opcode)` over `case SS_OP_NAME:` labels, with
fallthrough groups for opcodes that share a body. The `opcode == SS_OP_X` tests
that also appear are *inside* case bodies, disambiguating those groups, so they
are not dispatch and are correctly ignored here.
"""

import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent.parent  # src/
REPO = SRC.parent

# (path, human label). The label reaches the generated comment so a reader can
# see which layer covers what without opening either file.
#
# The per-domain `ToriRSServer_Ops_*.c` files are *globbed* rather than listed. A
# hand-kept list of dispatch sources is the same staleness this generator exists
# to remove, one level up: adding an ops file and forgetting to list it here
# would silently under-report coverage, so the gap report would name an opcode
# that is in fact implemented and `test-ToriRSServer` would fail on content that is
# fine. The glob cannot forget.
SOURCES = [
    (SRC / "serverscript" / "ssvm.c", "VM core"),
    (SRC / "net" / "mock" / "torirs_server_scripts.c", "host commands"),
] + [
    (path, f"host commands ({path.stem.replace('ToriRSServer_Ops_', '')})")
    for path in sorted((SRC / "net" / "mock").glob("ToriRSServer_Ops_*.c"))
]

OPCODE_HEADER = SRC / "serverscript" / "ss_opcode.h"
OUTPUT = SRC / "net" / "mock" / "torirs_server_opcode_coverage.gen.h"

CASE_RE = re.compile(r"^\s*case\s+(SS_OP_[A-Z0-9_]+)\s*:", re.MULTILINE)
DEFINE_RE = re.compile(r"^#define\s+(SS_OP_[A-Z0-9_]+)\s+(\d+)", re.MULTILINE)


def opcode_values():
    """name -> numeric value, from the generated opcode header."""
    text = OPCODE_HEADER.read_text()
    return {name: int(value) for name, value in DEFINE_RE.findall(text)}


def covered_names():
    """Every SS_OP_* that appears as a case label, with where it came from."""
    found = {}
    for path, label in SOURCES:
        for name in CASE_RE.findall(path.read_text()):
            found.setdefault(name, label)
    return found


def render():
    values = opcode_values()
    found = covered_names()

    unknown = sorted(n for n in found if n not in values)
    if unknown:
        # A case label naming an opcode the header does not define would not
        # compile, so this means the header moved rather than that the code is
        # wrong. Say so instead of silently dropping it from coverage.
        raise SystemExit(
            "gen_opcode_coverage: case labels name undefined opcodes: "
            + ", ".join(unknown)
        )

    by_value = sorted((values[name], name, found[name]) for name in found)
    per_source = {}
    for _, _, label in by_value:
        per_source[label] = per_source.get(label, 0) + 1

    lines = [
        "/*",
        " * GENERATED FILE — do not edit.",
        " *",
        " * The ServerScript opcodes this server implements, derived from the",
        " * `case SS_OP_*:` labels in the sources that dispatch them. Regenerate with:",
        " *",
        " *     python3 torirsserver/gen_opcode_coverage.py",
        " *",
        " * `make -C src test-torirsserver-coverage` fails when this file is stale.",
        " *",
        " * Coverage by layer:",
    ]
    for label in sorted(per_source):
        lines.append(f" *   {per_source[label]:4d}  {label}")
    lines += [
        f" *   {len(by_value):4d}  total, of {len(values)} declared opcodes",
        " */",
        "",
        "#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_OPCODE_COVERAGE_GEN_H",
        "#define SRC_TORIRSSERVER_TORIRS_SERVER_OPCODE_COVERAGE_GEN_H",
        "",
        "#include <stdint.h>",
        "",
        f"#define TORIRSSERVER_OPCODE_COVERAGE_COUNT {len(by_value)}",
        f"#define TORIRSSERVER_OPCODE_DECLARED_COUNT {len(values)}",
        "",
        "/*",
        " * One past the highest opcode *value*, which is nothing like the number of",
        " * opcodes: RuneScript numbers them sparsely and the host commands start at",
        " * 4000, so 396 declared opcodes span values up to 10003. Anything sizing an",
        " * array by opcode wants this, not the count — using the count silently",
        " * treats every real opcode as out of range.",
        " */",
        f"#define TORIRSSERVER_OPCODE_VALUE_LIMIT {max(values.values()) + 1}",
        "",
        "/* Ascending, so a lookup can binary-search. */",
        "static const uint16_t TORIRSSERVER_OPCODE_COVERAGE[TORIRSSERVER_OPCODE_COVERAGE_COUNT] = {",
    ]
    for value, name, label in by_value:
        lines.append(f"    {value}, /* {name} ({label}) */")
    lines += [
        "};",
        "",
        "#endif",
        "",
    ]
    return "\n".join(lines)


def main():
    text = render()
    if "--check" in sys.argv:
        current = OUTPUT.read_text() if OUTPUT.exists() else ""
        if current != text:
            print(
                f"{OUTPUT.relative_to(REPO)} is stale — an opcode was implemented or "
                f"removed without regenerating it.\n"
                f"Run: python3 torirsserver/gen_opcode_coverage.py",
                file=sys.stderr,
            )
            return 1
        print(f"{OUTPUT.relative_to(REPO)} is current")
        return 0

    OUTPUT.write_text(text)
    print(f"wrote {OUTPUT.relative_to(REPO)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
