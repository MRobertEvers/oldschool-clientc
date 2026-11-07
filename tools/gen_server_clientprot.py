#!/usr/bin/env python3
"""Generate the server's client -> server read table for one OSRS revision.

    tools/gen_server_clientprot.py 242 [--check]

Writes src/torirsserver/wire/osrs<N>_clientprot_table.h: every flat client
prot the server's inbound translator (mock<N>_inbound.c) reads, as the list of
wire accesses RSProt's spec states for that revision (3rd/rsprot/spec/packets,
the same layouts the client's codecs write with), each with the field it
carries. `--check` compares against the file instead of writing it.

The prots are the ones the 241 table carried (seeded from RSProx's osrs-241
decoders); generating 241 from the spec reproduces that table access for
access, which is what makes this a statement of the spec and not a new
transcription.
"""
from __future__ import annotations

import argparse
import glob
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "3rd", "rsprot", "tools"))
from rsprot_spec import load_packet, load_table, parse_rev  # noqa: E402

# The prots a revision's table carries, by RSProt prot name. 241's list.
PROTS = [
    "CLANCHANNEL_FULL_REQUEST", "CLANSETTINGS_FULL_REQUEST", "CLICKWORLDMAP",
    "CONNECTION_TELEMETRY", "DETECT_MODIFIED_CLIENT", "EVENT_APPLET_FOCUS",
    "EVENT_CAMERA_POSITION", "EVENT_MOUSE_CLICK_V1", "EVENT_MOUSE_CLICK_V2",
    "EVENT_MOUSE_SCROLL", "IF_BUTTON", "IF_BUTTOND", "IF_BUTTONT", "IF_BUTTONX",
    "IF_CRMVIEW_OP", "IF_SCRIPT_TRIGGER", "IF_SUBOP",
    "MEMBERSHIP_PROMOTION_ELIGIBILITY", "MOVE_GAMECLICK", "MOVE_MINIMAPCLICK",
    "OPLOC1_V2", "OPLOC2_V2", "OPLOC3_V2", "OPLOC4_V2", "OPLOC5_V2", "OPLOC6",
    "OPLOCT", "OPNPC1_V2", "OPNPC2_V2", "OPNPC3_V2", "OPNPC4_V2", "OPNPC5_V2",
    "OPNPC6", "OPNPCT", "OPOBJ1_V2", "OPOBJ2_V2", "OPOBJ3_V2", "OPOBJ4_V2",
    "OPOBJ5_V2", "OPOBJ6", "OPOBJT", "OPPLAYER1", "OPPLAYER2", "OPPLAYER3",
    "OPPLAYER4", "OPPLAYER5", "OPPLAYER6", "OPPLAYER7", "OPPLAYER8", "OPPLAYERT",
    "OPWORLDENTITY1", "OPWORLDENTITY2", "OPWORLDENTITY3", "OPWORLDENTITY4",
    "OPWORLDENTITY5", "OPWORLDENTITY6", "OPWORLDENTITYT", "RESUME_PAUSEBUTTON",
    "RESUME_P_COUNTDIALOG", "RESUME_P_OBJDIALOG", "RSEVEN_STATUS",
    "SEND_PING_REPLY", "SET_CHATFILTERSETTINGS", "SET_HEADING", "SOUND_JINGLEEND",
    "TELEPORT", "WINDOW_STATUS",
]

# RSProt access token -> the table's access (RSProx's g<N>Alt<M> names).
ACCESS = {
    "U1": "G1", "I1": "G1", "BOOL": "G1", "I2_12": "G2", "U1_ADD128": "G1_ALT1",
    "U1_NEG": "G1_ALT2", "U1_SUB128": "G1_ALT3",
    "U2_12": "G2", "U2_21": "G2_ALT1",
    "U2_12_ADD128": "G2_ALT2",
    "U2_21_ADD128": "G2_ALT3",
    "U3_123": "G3", "U3_321": "G3_ALT1", "U3_132": "G3_ALT2", "U3_213": "G3_ALT3",
    "U4_1234": "G4", "COM_1234": "G4",
    "U4_4321": "G4_ALT1", "COM_4321": "G4_ALT1",
    "U4_3412": "G4_ALT2", "COM_3412": "G4_ALT2",
    "U4_2143": "G4_ALT3", "COM_2143": "G4_ALT3",
}
ACCESS_ORDER = ["G1", "G1_ALT1", "G1_ALT2", "G1_ALT3", "G2", "G2_ALT1", "G2_ALT2", "G2_ALT3",
                "G3", "G3_ALT1", "G3_ALT2", "G3_ALT3", "G4", "G4_ALT1", "G4_ALT2", "G4_ALT3"]


def packets_by_prot() -> dict[str, dict]:
    out = {}
    for f in sorted(glob.glob(os.path.join(REPO, "3rd", "rsprot", "spec", "packets", "*.ini"))):
        p = load_packet(f)
        if p["dir"] == "in":
            out.setdefault(p["prot"], []).append(p)
    return out


def layout_for(packets: list[dict], rev) -> tuple[dict, dict] | None:
    for p in packets:
        for layout in p["layouts"]:
            if any(lo.family == rev.family and lo <= rev <= hi for lo, hi in layout["revs"]):
                return p, layout
    return None


CODEC_ROW = re.compile(r"\{ RSPROT_OSRS_ID\((\d+)\), RSPROT_OSRS_ID\((\d+)\), \(RsprotCodecFn\)(\w+_in) \}")
ACCESS_CALL = re.compile(r"^\s*RSPROT_(\w+)\(x, m->(\w+)\);\s*$")


def hand_codec_layout(name: str, rev) -> tuple[str, list[list[str]]] | None:
    """A prot RSProt writes by hand (3rd/rsprot/packets/<prot>.c, no spec INI):
    the access calls of the `_in` codec its version table names for `rev`."""
    path = os.path.join(REPO, "3rd", "rsprot", "packets", f"{name.lower()}.c")
    if not os.path.exists(path) or rev.family != "osrs":
        return None
    text = open(path).read()
    fn = None
    for lo, hi, f in CODEC_ROW.findall(text):
        if int(lo) <= rev.major <= int(hi):
            fn = f
    if fn is None:
        return None
    body = re.search(r"\n" + fn + r"\(RsprotExec \*x, \w+ \*m\)\n\{\n(.*?)\n\}", text, re.S)
    assert body, f"{path}: no body for {fn}"
    rows = []
    for line in body.group(1).splitlines():
        if not line.strip():
            continue
        m = ACCESS_CALL.match(line)
        assert m, f"{path} {fn}: not a flat access: {line.strip()}"
        rows.append([m.group(1), m.group(2)])
    return f"3rd/rsprot/packets/{name.lower()}.c ({fn})", rows


def size_of(value) -> int:
    if value == "VARU8":
        return -1
    if value == "VARU16":
        return -2
    return int(value)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rev", type=int)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    rev = parse_rev(f"osrs{args.rev}")
    tag = f"osrs{args.rev}"
    up = f"OSRS{args.rev}"
    camel = f"Osrs{args.rev}"

    table = load_table(os.path.join(REPO, "3rd", "rsprot", "spec", "tables", f"{tag}.ini"))
    client = {row[0]: row for row in table["client"]}
    by_prot = packets_by_prot()

    layouts = []
    roles = set()
    for prot in PROTS:
        name = prot
        # The prot table may name a newer version of the same packet.
        assert name in client, f"{tag} has no client prot {name}"
        candidates = [p for key, ps in by_prot.items() for p in ps
                      if p["prot"] == name or p["file"] == name.lower()]
        found = layout_for(candidates, rev)
        if found:
            packet, layout = found
            source = f"3rd/rsprot/spec/packets/{packet['file']}.ini"
        else:
            hand = hand_codec_layout(name, rev)
            assert hand, f"{name}: no layout for {tag}"
            source, body = hand
            packet, layout = {"file": name.lower()}, {"body": body}
        fields = []
        for row in layout["body"]:
            if not isinstance(row, list):
                # IF_SCRIPT_TRIGGER's argument blob: the table is its header and
                # the translator copies the rest.
                if row.strip().startswith("RSPROT_REST("):
                    continue
                # Derived fields (a bit unpacked from a read value) touch no
                # byte; anything that reads or branches is not a flat layout.
                assert "RSPROT_" not in row and not re.search(r"\b(if|for|while)\b", row), \
                    f"{name}: a code line that reads or branches: {row.strip()}"
                continue
            access, field = row
            assert access in ACCESS, f"{name}: access {access}"
            fields.append((ACCESS[access], field.upper()))
            roles.add(field.upper())
        layouts.append((name, size_of(client[name][2]), packet, fields, source))

    out = []
    w = out.append
    w(f"#ifndef SRC_TORIRSSERVER_WIRE_{up}_CLIENTPROT_TABLE_H")
    w(f"#define SRC_TORIRSSERVER_WIRE_{up}_CLIENTPROT_TABLE_H")
    w("")
    w("/*")
    w(f" * GENERATED by tools/gen_server_clientprot.py {args.rev} -- do not edit.")
    w(" *")
    w(f" * Revision {args.rev}'s flat client -> server packets as data, for the server's")
    w(f" * reader ({tag}_cp_read, {tag}_clientprot.c): each layout is RSProt's spec")
    w(f" * layout for {tag} (3rd/rsprot/spec/packets), the one the client's codecs")
    w(" * write with, as wire accesses in order with the field each carries.")
    w(" *")
    w(" * Access names follow RSProx: G2_ALT1 is g2Alt1 (little endian), G2_ALT2 big")
    w(" * endian with the low byte +128, G2_ALT3 little endian +128; G1_ALT1 v+128,")
    w(" * G1_ALT2 -v, G1_ALT3 128-v; G4 also carries combined ids.")
    w(" */")
    w("")
    w("#include <stdint.h>")
    w("")
    w(f"enum {camel}CpAccess")
    w("{")
    for a in ACCESS_ORDER:
        w(f"    {up}_CP_{a},")
    w("};")
    w("")
    w(f"enum {camel}CpRole")
    w("{")
    for r in sorted(roles):
        w(f"    {up}_CP_{r},")
    w(f"    {up}_CP_ROLE_COUNT")
    w("};")
    w("")
    w(f"#define {up}_CP_MAX_FIELDS {max(len(f) for _, _, _, f, _ in layouts)}")
    w("")
    w(f"struct {camel}CpField")
    w("{")
    w("    uint8_t access;")
    w("    uint8_t role;")
    w("};")
    w("")
    w(f"struct {camel}CpLayout")
    w("{")
    w("    char const* prot;  /* RSProt's prot name */")
    w("    int size;          /* the prot table's size: -1 var-byte, -2 var-short */")
    w("    int field_count;")
    w(f"    struct {camel}CpField fields[{up}_CP_MAX_FIELDS];")
    w("};")
    for name, size, packet, fields, source in sorted(layouts, key=lambda x: x[2]["file"]):
        w("")
        w(f"/* {source} */")
        w(f"static const struct {camel}CpLayout g_{tag}_cp_{name.lower()} = {{")
        w(f"    \"{name}\", {size}, {len(fields)},")
        w("    {")
        for a, r in fields:
            w(f"        {{ {up}_CP_{a}, {up}_CP_{r} }},")
        w("    },")
        w("};")
    w("")
    w("#endif")
    text = "\n".join(out) + "\n"
    path = os.path.join(REPO, "src", "torirsserver", "wire", f"{tag}_clientprot_table.h")
    if args.check:
        same = os.path.exists(path) and open(path).read() == text
        print(f"{path}: {'current' if same else 'STALE'}")
        return 0 if same else 1
    with open(path, "w") as fh:
        fh.write(text)
    print(f"wrote {path} ({len(layouts)} prots)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
