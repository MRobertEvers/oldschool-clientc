#!/usr/bin/env python3
"""
Read an OSRS revision's wire tables out of RSProx's Kotlin -- the read-only
upstream checkout at ~/rsprox-index-data/rsprox-upstream (never written here).

RSProx states each revision the way RSProt does, two Kotlin files a direction:

    protocol/osrs-<N>/src/main/kotlin/net/rsprox/protocol/v<N>/game/
        outgoing/decoder/prot/GameServerProtId.kt   const val NAME = <opcode>
        outgoing/decoder/prot/GameServerProt.kt     NAME(GameServerProtId.NAME, <size>)
        incoming/decoder/prot/GameClientProt{Id,}.kt   (client -> server)

with `NAME(-1, <size>)` for the zone sub-packets that have no top-level opcode,
and `Prot.VAR_BYTE` / `Prot.VAR_SHORT` for the framed sizes.

RSProx is the authority for revisions RSProt never vendored (240 and 241 at the
time of writing). It names a handful of prots differently from RSProt -- the
`_V1` suffixes RSProx adds when a `_V2` appears, `RESUME_P_LONGDIALOG` for
RSProt's `RESUME_P_COUNTDIALOG_LONG` -- and RSPROT_ALIASES below is the whole
list, so a table written in RSProt's spelling can still be compared.

Used by tools/rsprox_prot_compare.py and tools/rsprot_gen_rev.py --source rsprox.
"""

import os
import re
import subprocess

RSPROX = os.path.expanduser("~/rsprox-index-data/rsprox-upstream")

VAR = {"Prot.VAR_BYTE": -1, "Prot.VAR_SHORT": -2, "VAR_BYTE": -1, "VAR_SHORT": -2}

# RSProt spelling -> RSProx spelling, for prots the two projects name apart.
RSPROT_ALIASES = {
    "OBJ_CUSTOMISE_SPECIFIC": "OBJ_CUSTOMISE_SPECIFIC_V1",
    "UPDATE_STOCKMARKET_SLOT": "UPDATE_STOCKMARKET_SLOT_V1",
    "UPDATE_TRADINGPOST": "UPDATE_TRADINGPOST_V1",
    "RESUME_P_COUNTDIALOG_LONG": "RESUME_P_LONGDIALOG",
}


def protocol_dir(rev):
    return os.path.join(
        RSPROX, "protocol", f"osrs-{rev}", "src", "main", "kotlin", "net", "rsprox",
        "protocol", f"v{rev}")


def _prot_dir(rev, direction):
    return os.path.join(protocol_dir(rev), "game", direction, "decoder", "prot")


def _load_side(rev, direction, enum_name):
    base = _prot_dir(rev, direction)
    with open(os.path.join(base, f"{enum_name}Id.kt")) as fh:
        ids = {m.group(1): int(m.group(2))
               for m in re.finditer(r"const val (\w+)\s*=\s*(-?\d+)", fh.read())}
    with open(os.path.join(base, f"{enum_name}.kt")) as fh:
        src = fh.read()
    rows = []
    pat = re.compile(
        r"^\s*(\w+)\(\s*(?:" + enum_name + r"Id\.(\w+)|(-?\d+))\s*,\s*([\w.]+|-?\d+)\s*,?\s*\)",
        re.M)
    for m in pat.finditer(src):
        size = m.group(4)
        size = VAR[size] if size in VAR else int(size)
        code = ids[m.group(2)] if m.group(2) else int(m.group(3))
        rows.append({"name": m.group(1), "code": code, "size": size})
    if not rows:
        raise SystemExit(f"no prots parsed from {base}/{enum_name}.kt")
    return rows


def load_prots(rev):
    """{"server": [...], "client": [...]}, each a list of {name, code, size} in
    declaration order. server = GameServerProt (server -> client)."""
    return {
        "server": _load_side(rev, "outgoing", "GameServerProt"),
        "client": _load_side(rev, "incoming", "GameClientProt"),
    }


def zone_order(rev):
    """UPDATE_ZONE_PARTIAL_ENCLOSED's sub-packet index: the ORDINAL of RSProx's
    IndexedZoneProtDecoder enum, declaration order."""
    path = os.path.join(protocol_dir(rev), "game", "outgoing", "decoder", "codec", "zone",
                        "header", "UpdateZonePartialEnclosedDecoder.kt")
    with open(path) as fh:
        src = fh.read()
    body = src[src.index("enum class IndexedZoneProtDecoder"):]
    return re.findall(r"^\s+(\w+)\(\w+Decoder\(\)\),", body, re.M)


def decoder_files(rev):
    """{("out"|"in", PROT): path} for every decoder class naming its prot.
    "out" is server -> client (RSProx's outgoing), "in" client -> server."""
    out = {}
    for direction, sub, enum_name in (("out", "outgoing", "GameServerProt"),
                                      ("in", "incoming", "GameClientProt")):
        base = os.path.join(protocol_dir(rev), "game", sub, "decoder", "codec")
        for root, _dirs, files in os.walk(base):
            for f in files:
                if not f.endswith(".kt"):
                    continue
                path = os.path.join(root, f)
                with open(path) as fh:
                    src = fh.read()
                m = re.search(r"override val prot: \w+\s*=\s*" + enum_name + r"\.(\w+)", src)
                if m:
                    out[(direction, m.group(1))] = path
    return out


def source_hash():
    try:
        return subprocess.check_output(
            ["git", "-C", RSPROX, "rev-parse", "--short", "HEAD"],
            stderr=subprocess.DEVNULL).decode().strip()
    except Exception:
        return "unknown"
