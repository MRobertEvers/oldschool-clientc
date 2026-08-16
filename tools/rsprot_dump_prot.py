#!/usr/bin/env python3
"""
Dump an RSProt revision's wire tables as JSON.

RSProt states a revision's protocol as two Kotlin files per direction:

    .../game/outgoing/prot/GameServerProtId.kt    const val NAME = <opcode>
    .../game/outgoing/prot/GameServerProt.kt      NAME(GameServerProtId.NAME, <size>)

and the mirror pair under incoming/. `size` is either a literal byte count, or
`Prot.VAR_BYTE` / `Prot.VAR_SHORT`. An opcode of `-1` in GameServerProt means the
packet has no top-level opcode of its own (it is only reachable inside an
enclosed zone packet) — those rows are kept, with code -1, because their payload
sizes are still the truth for the sub-packet.

This is the one place that reads Kotlin. Everything downstream reads the JSON,
so a revision bump is a re-run rather than a re-transcription.

    tools/rsprot_dump_prot.py 239 > /tmp/osrs239.json
"""

import json
import os
import re
import sys

RSPROT = os.path.expanduser("~/Documents/git_repos/rsprot")

VAR = {"Prot.VAR_BYTE": -1, "Prot.VAR_SHORT": -2, "VAR_BYTE": -1, "VAR_SHORT": -2}


def _prot_dir(rev, direction):
    return os.path.join(
        RSPROT,
        "protocol",
        f"osrs-{rev}",
        f"osrs-{rev}-desktop",
        "src/main/kotlin/net/rsprot/protocol/game",
        direction,
        "prot",
    )


def _parse_ids(path):
    """GameServerProtId.kt / GameClientProtId.kt -> {NAME: opcode}."""
    out = {}
    with open(path) as fh:
        for line in fh:
            m = re.match(r"\s*(?:internal\s+)?const val (\w+)\s*=\s*(-?\d+)", line)
            if m:
                out[m.group(1)] = int(m.group(2))
    return out


def _parse_prot(path, ids):
    """GameServerProt.kt / GameClientProt.kt -> [{name, code, size}] in file order."""
    rows = []
    src = open(path).read()
    # NAME(<idref>, <size>),  — idref is GameXProtId.NAME or a bare -1.
    #
    # Two things this pattern has to survive, both of which dropped rows in
    # silence before they were handled:
    #   - sizes appear both symbolically (`Prot.VAR_SHORT`) and as the bare int
    #     the symbol equals (`-2`), so the size alternative must accept a sign;
    #   - ktlint wraps long entries over four lines, so the argument list must be
    #     allowed to span newlines and to carry a trailing comma.
    # `expected` turns any future formatting the pattern cannot read into a hard
    # failure rather than a quietly shorter table.
    pat = re.compile(
        r"^ {4}(\w+)\(\s*(?:\w+ProtId\.(\w+)|(-?\d+))\s*,\s*(-?[\w.]+)\s*,?\s*\)",
        re.M | re.S,
    )
    expected = len(re.findall(r"^ {4}[A-Z][A-Z_0-9]*\(", src, re.M))
    for m in pat.finditer(src):
        name, idref, literal, size = m.groups()
        if idref is not None:
            if idref not in ids:
                raise SystemExit(f"{path}: {name} references unknown id {idref}")
            code = ids[idref]
        else:
            code = int(literal)
        rows.append({"name": name, "code": code, "size": VAR.get(size, None)})
        if rows[-1]["size"] is None:
            try:
                rows[-1]["size"] = int(size)
            except ValueError:
                raise SystemExit(f"{path}: {name} has unparseable size {size!r}")
    if len(rows) != expected:
        got = {r["name"] for r in rows}
        missed = [
            n
            for n in re.findall(r"^ {4}([A-Z][A-Z_0-9]*)\(", src, re.M)
            if n not in got
        ]
        raise SystemExit(f"{path}: parsed {len(rows)} of {expected} rows; missed {missed}")
    return rows


def dump(rev):
    server_dir = _prot_dir(rev, "outgoing")
    client_dir = _prot_dir(rev, "incoming")
    server_ids = _parse_ids(os.path.join(server_dir, "GameServerProtId.kt"))
    client_ids = _parse_ids(os.path.join(client_dir, "GameClientProtId.kt"))
    return {
        "revision": rev,
        "server": _parse_prot(os.path.join(server_dir, "GameServerProt.kt"), server_ids),
        "client": _parse_prot(os.path.join(client_dir, "GameClientProt.kt"), client_ids),
    }


if __name__ == "__main__":
    rev = sys.argv[1] if len(sys.argv) > 1 else "239"
    json.dump(dump(rev), sys.stdout, indent=1)
    sys.stdout.write("\n")
