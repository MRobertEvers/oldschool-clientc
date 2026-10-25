#!/usr/bin/env python3
"""
Extend the generated rsprot codecs (3rd/rsprot/packets/) to revisions RSProt
never vendored, using RSProx as the source. Called by tools/rsprot_gen_codec.py;
runnable alone as a report:

    tools/rsprox_codec_overlay.py            # what the overlay would do at 241

## Why an overlay and not a second generator

rsprot_gen_codec.py reads RSProt's ENCODERS (server prots) and DECODERS (client
prots) and names every struct field after RSProt's Kotlin. RSProx, which is the
only source past 239, has DECODERS for both directions, written against its own
message classes -- a different set of names. Generating 241 straight from RSProx
would invent a second struct for every packet.

What the two projects DO share is the wire. At revision 239 RSProt and RSProx
describe the same bytes, so for each packet:

    our generated 239 C body        RSPROT_U2_ALT2(x, m->end);   ...
    RSProx's 239 decode body        val end = buffer.g2Alt2()   ...

line up one wire access for one wire access. That pairing is a dictionary from
RSProx's variable names to our struct fields, checked as it is built: every
pair must agree on width and byte order, or the packet is refused. RSProx's 241
decode body is then re-spelled through the dictionary -- same fields, 241's
order and transforms -- and becomes one more layout of the SAME struct.

So a 241 layout is never transcribed by hand and never guessed: it is either

  * IDENTICAL -- RSProx's 241 decode body is the 239 one, and 241 reuses the
    239 layout's version number; or
  * RESPELLED -- a flat body (one wire access per statement, no loop or branch
    in either revision) whose 239 pairing checked out field for field; or
  * REFUSED -- logged with the reason, and the packet simply has no 241 row.
    rsprot_version_pick then returns NULL at 241, which is the same "no codec
    here" every caller already handles; a hand-written 241 path is the fix.

## Version numbers

The RSProt ledger (gen/packet_versions.txt) numbers layouts 1..N from RSProt's
Kotlin hashes and is append-only. Overlay layouts are numbered in their own
ledger, gen/rsprox_versions.txt, from 100 up, so that a later RSProt vendor
allocating its next number can never collide with one already handed out here.
A 241 layout identical to an RSProt one reuses the RSProt number instead.
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import rsprox_prot  # noqa: E402

OVERLAY_REVS = (240, 241)
BASE_REV = 239
# Revisions RSProt vendors whose gaps (a revision its generator could not
# parse) are filled from an identical RSProx decoder; see Overlay._fill_gaps.
# 227 is the oldest revision this tree speaks.
FILL_REVS = range(227, 240)
OVERLAY_LEDGER = os.path.join(REPO_ROOT, "3rd", "rsprot", "gen", "rsprox_versions.txt")
OVERLAY_VERSION_FLOOR = 100

LEDGER_HEADER = """\
# RSProx-derived layout versions for 3rd/rsprot/packets/ -- APPEND-ONLY.
#
# Written by tools/rsprox_codec_overlay.py (via tools/rsprot_gen_codec.py) for
# revisions RSProt never vendored. A row freezes the version number a packet's
# layout at REV was given; numbers here start at 100 so they can never collide
# with gen/packet_versions.txt, which allocates from 1. A REV whose layout is
# byte-identical to an RSProt layout reuses that number and has no row here.
#
# Never renumber a row. A layout that changes upstream gets a NEW row and the
# old one stays, exactly as in packet_versions.txt.
CLASS\tDIR\tREV\tVER\tBODY
"""

_DECODER_CACHE = {}


def _decoders(rev):
    if rev not in _DECODER_CACHE:
        _DECODER_CACHE[rev] = rsprox_prot.decoder_files(rev)
    return _DECODER_CACHE[rev]


# --- RSProx decode bodies ----------------------------------------------------


def decode_body(path):
    """The text between the braces of `override fun decode(...): T { ... }`,
    comments stripped. None if the file has no such method."""
    with open(path) as fh:
        src = fh.read()
    src = re.sub(r"//[^\n]*", "", src)
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    i = src.find("override fun decode(")
    if i < 0:
        return None
    j = src.find("):", i)
    k = src.find("{", j)
    eq = src.find("=", j)
    if eq >= 0 and (k < 0 or eq < k):
        # `override fun decode(...): T = T` -- an expression body, which for a
        # packet means "no payload". The expression up to the end of the line is
        # the whole body.
        end = src.find("\n", eq)
        return src[eq + 1:end if end >= 0 else len(src)]
    depth = 0
    for pos in range(k, len(src)):
        c = src[pos]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return src[k + 1:pos]
    return None


def normalize(body):
    return re.sub(r"\s+", " ", body).strip()


CALL_RE = re.compile(r"buffer\.(g\w+)\(\)")
KT_ACCESS_RE = re.compile(r"^g(?:(\d)(s?)|(CombinedId)|(jstr2?|jstrnull)|(boolean))(Alt[123])?$")


def kt_access(fn):
    """g2Alt3 -> ("2", "ALT3"); gCombinedIdAlt1 -> ("COM", "ALT1"); gjstr -> ("JSTR", "").
    None for anything else (smarts, var-ints, gdata...): refused, not guessed."""
    m = KT_ACCESS_RE.match(fn)
    if not m:
        return None
    alt = m.group(6)
    alt = "ALT" + alt[3] if alt else ""
    if m.group(1):
        return (m.group(1), alt, bool(m.group(2)))
    if m.group(3):
        return ("COM", alt, False)
    if m.group(4):
        return ({"jstr": "JSTR", "jstr2": "JSTR2", "jstrnull": "JSTRNULL"}[m.group(4)], alt, False)
    return ("BOOL", alt, False)


def flat_accesses(body):
    """[(var, fn)] in wire order for a flat decode body, or (None, reason).

    Flat: every wire access sits in a `val NAME = ...` statement and nothing
    before the `return` loops or branches. One statement may hold several
    accesses (`CoordGrid(buffer.g1(), buffer.g2())`); they become NAME#0, NAME#1.
    """
    ret = re.search(r"\breturn\b", body)
    pre = body[:ret.start()] if ret else body
    if re.search(r"\b(if|for|while|when|repeat|buildList|buildMap|List|Array|ByteArray)\b\s*[({]", pre):
        return None, "loop or branch in the decode body"
    out = []
    stmts = re.split(r"\n\s*(?=val |var )", "\n" + pre)
    for st in stmts:
        st = st.strip()
        if not st:
            continue
        calls = CALL_RE.findall(st)
        if not calls:
            continue
        m = re.match(r"(?:val|var)\s+(\w+)", st)
        if not m:
            return None, f"wire access outside a val statement: {st[:60]!r}"
        name = m.group(1)
        for idx, fn in enumerate(calls):
            out.append((name if len(calls) == 1 else f"{name}#{idx}", fn))
    if ret:
        if CALL_RE.search(body[ret.start():]):
            return None, "wire access inside the return expression"
    return out, None


# --- our C bodies --------------------------------------------------------------

C_LINE_RE = re.compile(r"^\s*RSPROT_([A-Z0-9_]+)\(x, (m->\w+)\);\s*$")
C_MACRO_RE = re.compile(r"^(?:(U|I)(\d)|(COM)|(JSTR2?|JSTRNULL)|(BOOL))(?:_(ALT[123]))?$")


def c_access(macro):
    m = C_MACRO_RE.match(macro)
    if not m:
        return None
    alt = m.group(6) or ""
    if m.group(1):
        return (m.group(2), alt, m.group(1))
    if m.group(3):
        return ("COM", alt, None)
    if m.group(4):
        return (m.group(4), alt, None)
    return ("BOOL", alt, None)


def c_flat_lines(body):
    """([(macro, lvalue)], tail) or (None, None).

    Flat: every wire line is one primitive on m->field, and any line that moves
    no bytes (a field derived from a packed one, e.g. EVENT_MOUSE_CLICK_V2's
    right_click) comes after the last wire line -- `tail`, carried over
    verbatim, because RSProx derives the same values after its reads too."""
    out = []
    tail = []
    for line in body.split("\n"):
        if not line.strip():
            continue
        m = C_LINE_RE.match(line)
        if m:
            if tail:
                return None, None
            out.append((m.group(1), m.group(2)))
            continue
        if "RSPROT_" in line or "{" in line or "}" in line:
            return None, None
        tail.append(line)
    if not out:
        return None, None
    return out, tail


def _compatible(c, k):
    """Do a C access and a Kotlin access describe the same bytes?"""
    cw, calt, _pref = c
    kw, kalt, _signed = k
    if calt != kalt:
        return False
    if cw == kw:
        return True
    # A combined id is a four-byte int in the same four orders.
    if {cw, kw} == {"COM", "4"}:
        return True
    # A boolean is one plain byte.
    if {cw, kw} == {"BOOL", "1"} and not calt:
        return True
    return False


def _respell(c, k241):
    """C macro for the 241 access, keeping the 239 field's C family."""
    cw, _calt, pref = c
    kw, kalt, _signed = k241
    suffix = f"_{kalt}" if kalt else ""
    if cw == "COM":
        if kw not in ("COM", "4"):
            return None
        return "COM" + suffix
    if cw == "BOOL":
        if kw == "BOOL" or (kw == "1" and not kalt):
            return "BOOL"
        if kw == "1":
            # A transformed boolean byte: carried as the 0/1 integer it is.
            return "U1" + suffix
        return None
    if cw in ("JSTR", "JSTR2", "JSTRNULL"):
        return cw if kw == cw and not kalt else None
    if kw != cw:
        return None
    return f"{pref}{cw}{suffix}"


def respell(body239, rx239, rx241):
    """The 241 C body, or (None, reason)."""
    lines, tail = c_flat_lines(body239)
    if lines is None:
        return None, "239 C body is not flat"
    acc239, why = flat_accesses(rx239)
    if acc239 is None:
        return None, f"RSProx 239: {why}"
    acc241, why = flat_accesses(rx241)
    if acc241 is None:
        return None, f"RSProx 241: {why}"
    if len(lines) != len(acc239):
        return None, f"239 C has {len(lines)} accesses, RSProx 239 has {len(acc239)}"
    field = {}
    cacc = {}
    for (macro, lv), (var, fn) in zip(lines, acc239):
        c = c_access(macro)
        k = kt_access(fn)
        if c is None or k is None:
            return None, f"unmodelled access {macro} / {fn}"
        if not _compatible(c, k):
            return None, f"239 pairing disagrees: {macro}({lv}) vs {var} = {fn}()"
        if var in field:
            return None, f"RSProx variable {var} read twice"
        field[var] = lv
        cacc[var] = c
    if sorted(field) != sorted(v for v, _f in acc241):
        return None, (f"RSProx 241 reads a different variable set: "
                      f"{sorted(set(field) ^ set(v for v, _f in acc241))}")
    out = []
    for var, fn in acc241:
        k = kt_access(fn)
        if k is None:
            return None, f"unmodelled 241 access {fn}"
        macro = _respell(cacc[var], k)
        if macro is None:
            return None, f"241 access {fn} cannot carry {var} ({cacc[var]})"
        out.append(f"    RSPROT_{macro}(x, {field[var]});")
    return "\n".join(out + tail), None


# --- hand-checked 241 layouts -------------------------------------------------
#
# The packets the respelling cannot take, each transcribed from the RSProx 241
# decoder named beside it and written in the generated codec's own vocabulary.
# A row here is a hand transcription and says so; keep it to the packets whose
# body has a loop, a branch, or a 239 pairing the two upstreams disagree on.
# Bodies use the FINAL struct names (after rsprot_gen_codec.py's rename).
MANUAL = {
    # LocAddChangeV2Decoder (osrs-241), wire order: g1Alt1 locProperties,
    # g1 opFlags, g1Alt1 coordInZone, g1Alt2 opOverrideCount,
    # [g1Alt2 index, gjstr]*count, g2 id. The index keeps RSProt's +1 on the
    # wire (XFORM -1), as at 239.
    ("out", "LOC_ADD_CHANGE_V2", 241): (
        "    RSPROT_U1_ALT1(x, m->loc_properties_packed);\n"
        "    RSPROT_U1(x, m->op_flags);\n"
        "    RSPROT_U1_ALT1(x, m->coord_in_zone_packed);\n"
        "    RSPROT_U1_ALT2(x, m->ops_count);\n"
        "    if ((!(m->ops_count == 0))) {\n"
        "        for (int32_t rs_i_ops = 0; rs_i_ops < m->ops_count; rs_i_ops++) {\n"
        "            Rsprot_MsgLocAddChangeV2_opsElem *elem = &m->ops[rs_i_ops];\n"
        "            RSPROT_XFORM(x, U1_ALT2, elem->key, -1);\n"
        "            RSPROT_JSTR(x, elem->value);\n"
        "        }\n"
        "    }\n"
        "    RSPROT_U2(x, m->id);"),
    # StaticRebuildV2Decoder (osrs-241): [player-info init block, hand-built],
    # g2 zoneZ, g2Alt1 zoneX, g2s worldArea. Only the trailing header is a
    # codec; the bit block is written whole by its owner, as at 239.
    ("out", "REBUILD_NORMAL_V2", 241): (
        "    RSPROT_U2(x, m->zone_z);\n"
        "    RSPROT_U2_ALT1(x, m->zone_x);\n"
        "    RSPROT_U2(x, m->world_area);"),
    # RebuildRegionV2Decoder (osrs-241): g2 zoneX, g1Alt2 reload, g2Alt1 zoneZ,
    # then g2 mapsquare count and the bit grid (hand-built, as at 239).
    ("out", "REBUILD_REGION_V2", 241): (
        "    RSPROT_U2(x, m->zone_x);\n"
        "    RSPROT_U1_ALT2(x, m->reload);\n"
        "    RSPROT_U2_ALT1(x, m->zone_z);"),
    # UpdateZonePartialEnclosedDecoder (osrs-241): g1 zoneZ, g1 zoneX,
    # g1Alt3 level, then the indexed sub-packets.
    ("out", "UPDATE_ZONE_PARTIAL_ENCLOSED", 241): (
        "    RSPROT_U1(x, m->zone_z);\n"
        "    RSPROT_U1(x, m->zone_x);\n"
        "    RSPROT_U1_ALT3(x, m->level);"),
    # BugReportDecoder (osrs-241): gjstr, g1Alt2 type, gjstr. RSProt and RSProx
    # disagree at 239 on which string comes first and on the type byte's
    # transform (RSProt g1Alt3, RSProx g1), so there is no pairing to respell
    # through; 241 follows RSProx's names and order.
    ("in", "BUG_REPORT", 241): (
        "    RSPROT_JSTR(x, m->instructions);\n"
        "    RSPROT_U1_ALT2(x, m->type);\n"
        "    RSPROT_JSTR(x, m->description);"),
    # ClientCheatDecoder (osrs-241): gjstr command, then -- only if bytes remain
    # -- g1 autocomplete == 1. The optional byte has no codec construct; a
    # sender that omits it is a valid 241 sender, so the layout is the command.
    ("in", "CLIENT_CHEAT", 241): "    RSPROT_JSTR(x, m->command);",

    # --- 240: the same six, from RSProx's osrs-240 decoders. ---
    # LocAddChangeV2Decoder (osrs-240): g1Alt3 opFlags, g1 opOverrideCount,
    # [g1Alt1 index, gjstr]*count, g1 coordInZone, g2Alt2 id, g1 locProperties.
    ("out", "LOC_ADD_CHANGE_V2", 240): (
        "    RSPROT_U1_ALT3(x, m->op_flags);\n"
        "    RSPROT_U1(x, m->ops_count);\n"
        "    if ((!(m->ops_count == 0))) {\n"
        "        for (int32_t rs_i_ops = 0; rs_i_ops < m->ops_count; rs_i_ops++) {\n"
        "            Rsprot_MsgLocAddChangeV2_opsElem *elem = &m->ops[rs_i_ops];\n"
        "            RSPROT_XFORM(x, U1_ALT1, elem->key, -1);\n"
        "            RSPROT_JSTR(x, elem->value);\n"
        "        }\n"
        "    }\n"
        "    RSPROT_U1(x, m->coord_in_zone_packed);\n"
        "    RSPROT_U2_ALT2(x, m->id);\n"
        "    RSPROT_U1(x, m->loc_properties_packed);"),
    # StaticRebuildV2Decoder (osrs-240): [player-info init block], g2Alt3 zoneZ,
    # g2sAlt3 worldArea, g2Alt3 zoneX.
    ("out", "REBUILD_NORMAL_V2", 240): (
        "    RSPROT_U2_ALT3(x, m->zone_z);\n"
        "    RSPROT_U2_ALT3(x, m->world_area);\n"
        "    RSPROT_U2_ALT3(x, m->zone_x);"),
    # RebuildRegionV2Decoder (osrs-240): g2Alt2 zoneZ, g2Alt1 zoneX, g1Alt1
    # reload, then g2 mapsquare count and the bit grid.
    ("out", "REBUILD_REGION_V2", 240): (
        "    RSPROT_U2_ALT2(x, m->zone_z);\n"
        "    RSPROT_U2_ALT1(x, m->zone_x);\n"
        "    RSPROT_U1_ALT1(x, m->reload);"),
    # UpdateZonePartialEnclosedDecoder (osrs-240): g1Alt3 zoneX, g1Alt1 zoneZ,
    # g1Alt2 level, then the indexed sub-packets.
    ("out", "UPDATE_ZONE_PARTIAL_ENCLOSED", 240): (
        "    RSPROT_U1_ALT3(x, m->zone_x);\n"
        "    RSPROT_U1_ALT1(x, m->zone_z);\n"
        "    RSPROT_U1_ALT2(x, m->level);"),
    # BugReportDecoder (osrs-240): gjstr description, gjstr instructions,
    # g1Alt3 type.
    ("in", "BUG_REPORT", 240): (
        "    RSPROT_JSTR(x, m->description);\n"
        "    RSPROT_JSTR(x, m->instructions);\n"
        "    RSPROT_U1_ALT3(x, m->type);"),
    # ClientCheatDecoder (osrs-240): as 241.
    ("in", "CLIENT_CHEAT", 240): "    RSPROT_JSTR(x, m->command);",
}


# --- the ledger ----------------------------------------------------------------


def load_overlay_ledger(path=OVERLAY_LEDGER):
    rows = {}
    if not os.path.exists(path):
        return rows
    with open(path) as fh:
        for line in fh:
            line = line.rstrip("\n")
            if not line or line.startswith("#") or line.startswith("CLASS\t"):
                continue
            cls, direction, rev, ver, body = line.split("\t")
            rows[(cls, direction, int(rev))] = (int(ver), body)
    return rows


def append_overlay_ledger(new_rows, path=OVERLAY_LEDGER):
    if not new_rows:
        return
    exists = os.path.exists(path)
    with open(path, "a") as fh:
        if not exists:
            fh.write(LEDGER_HEADER)
        for cls, direction, rev, ver, body in new_rows:
            fh.write(f"{cls}\t{direction}\t{rev}\t{ver}\t{body}\n")


def _ledger_body(c_body):
    """One-line form of a C body for the ledger: the statements, `;`-joined."""
    return " ".join(l.strip() for l in c_body.split("\n") if l.strip())


class Overlay:
    """One generator run's worth of overlay state."""

    def __init__(self, write_ledger=True, ledger_path=OVERLAY_LEDGER):
        self.ledger = load_overlay_ledger(ledger_path)
        self.ledger_path = ledger_path
        self.write_ledger = write_ledger
        self.new_rows = []
        self.report = []  # (prot, dir, rev, outcome, detail)

    def extend(self, cls, direction, prot, versions, rev_to_ver):
        """Add OVERLAY_REVS rows to one packet's (versions, rev_to_ver) in place,
        then fill the FILL_REVS gaps."""
        for rev in OVERLAY_REVS:
            outcome, detail = self._extend_one(cls, direction, prot, versions, rev_to_ver, rev)
            self.report.append((prot, direction, rev, outcome, detail))
        for rev, ver, src in self._fill_gaps(direction, prot, rev_to_ver):
            versions[ver]["revs"].append(rev)
            rev_to_ver[rev] = ver
            self.report.append((prot, direction, rev, "FILLED", f"v{ver} (RSProx {rev} == {src})"))

    def _fill_gaps(self, direction, prot, rev_to_ver):
        """Rows for the revisions RSProt could not generate, by identity.

        RSProt's generator skips a revision whose encoder it cannot parse
        (UrlOpenEncoder enciphers its bytes at 221-234) or whose class it has no
        ledger for (CamMoveToArc, 227-235). RSProx still has a decoder there,
        and when that decoder's body is IDENTICAL to RSProx's decoder at a
        revision this packet does carry a layout for, the wire is the same --
        so the gap reuses that layout's number. Nothing is respelled or guessed:
        no identical decoder, no row."""
        rx_name = rsprox_prot.RSPROT_ALIASES.get(prot, prot)
        bodies = {}
        for rev in FILL_REVS:
            path = _decoders(rev).get((direction, rx_name))
            if path is not None:
                b = decode_body(path)
                if b is not None:
                    bodies[rev] = normalize(b)
        out = []
        for rev in FILL_REVS:
            if rev in rev_to_ver or rev not in bodies:
                continue
            src = next((r for r in sorted(rev_to_ver, key=lambda r: abs(r - rev))
                        if bodies.get(r) == bodies[rev]), None)
            if src is not None:
                out.append((rev, rev_to_ver[src], src))
        return out

    def _extend_one(self, cls, direction, prot, versions, rev_to_ver, rev):
        base_ver = rev_to_ver.get(BASE_REV)
        if base_ver is None:
            # A prot RSProt retired before 239 -- nothing at 239 to extend.
            return "NOT239", f"no {BASE_REV} layout (prot predates {BASE_REV})"
        rx_name = rsprox_prot.RSPROT_ALIASES.get(prot, prot)
        d239 = _decoders(BASE_REV).get((direction, rx_name))
        d241 = _decoders(rev).get((direction, rx_name))
        if d239 is None:
            return "REFUSED", f"RSProx has no {BASE_REV} decoder for {rx_name}"
        if d241 is None:
            # The prot was deleted (CAM_*_V2 at 241): no row is the right answer.
            return "GONE", f"RSProx {rev} has no {rx_name} -- deleted at this revision"
        b239, b241 = decode_body(d239), decode_body(d241)
        if b239 is None or b241 is None:
            return "REFUSED", "RSProx decoder has no decode body"
        body239 = versions[base_ver]["body"]
        manual = MANUAL.get((direction, rx_name, rev)) or MANUAL.get((direction, prot, rev))
        if manual is not None:
            body = manual
            how = "MANUAL"
        elif normalize(b239) == normalize(b241):
            body = body239
            how = "IDENTICAL"
        else:
            body, why = respell(body239, b239, b241)
            if body is None:
                return "REFUSED", why
            how = "RESPELLED"

        # A layout identical to one we already carry reuses its number.
        ver = None
        for v, slot in versions.items():
            if slot["body"] == body:
                ver = v
                break
        key = (cls, direction, rev)
        led = self.ledger.get(key)
        if ver is None:
            if led is not None:
                ver = led[0]
                if _ledger_body(body) != led[1]:
                    raise SystemExit(
                        f"rsprox ledger: {cls}/{direction} rev {rev} is v{ver} with body\n"
                        f"  {led[1]}\nbut RSProx now generates\n  {_ledger_body(body)}\n"
                        f"Append a new row; never edit a frozen one.")
            else:
                used = [v for v in versions] + [
                    v for (c, d, _r), (v, _b) in self.ledger.items() if c == cls and d == direction]
                ver = max(used + [OVERLAY_VERSION_FLOOR - 1]) + 1
                if ver < OVERLAY_VERSION_FLOOR:
                    ver = OVERLAY_VERSION_FLOOR
                self.ledger[key] = (ver, _ledger_body(body))
                self.new_rows.append((cls, direction, rev, ver, _ledger_body(body)))
            if ver in versions and versions[ver]["body"] != body:
                raise SystemExit(f"{cls}/{direction}: overlay v{ver} collides with an RSProt layout")
            versions[ver] = {"body": body, "revs": []}
        versions[ver]["revs"].append(rev)
        rev_to_ver[rev] = ver
        return how, f"v{ver}"

    def finish(self):
        if self.write_ledger:
            append_overlay_ledger(self.new_rows, self.ledger_path)


def main():
    """Report-only run against the packets in 3rd/rsprot/packets/ as checked in."""
    pkt_dir = os.path.join(REPO_ROOT, "3rd", "rsprot", "packets")
    ov = Overlay(write_ledger=False)
    for f in sorted(os.listdir(pkt_dir)):
        if not f.endswith(".c"):
            continue
        src = open(os.path.join(pkt_dir, f)).read()
        hdr = open(os.path.join(pkt_dir, f[:-2] + ".h")).read()
        m = re.search(r"^ \* (\w+) -- (server|client) ->", hdr, re.M)
        if not m:
            continue
        prot = m.group(1)
        direction = "out" if m.group(2) == "server" else "in"
        versions, rev_to_ver = {}, {}
        for fm in re.finditer(r"/\* v(\d+) -- revs [^*]*\*/\nvoid\n\w+\([^)]*\)\n\{\n(.*?)\n\}\n",
                              src, re.S):
            versions[int(fm.group(1))] = {"body": fm.group(2), "revs": []}
        for rm in re.finditer(r"\{ (\d+), (\d+), \(RsprotCodecFn\)\w+_v(\d+)_\w+ \}", src):
            for r in range(int(rm.group(1)), int(rm.group(2)) + 1):
                rev_to_ver[r] = int(rm.group(3))
        if not rev_to_ver or any(v not in versions for v in rev_to_ver.values()):
            print(f"SKIPPED   {direction:<3} --- {prot:<40} not in generated shape (hand-written?)")
            continue
        ov.extend(prot, direction, prot, versions, rev_to_ver)
    counts = {}
    for prot, d, rev, outcome, detail in ov.report:
        counts[outcome] = counts.get(outcome, 0) + 1
        print(f"{outcome:<9} {d:<3} {rev} {prot:<40} {detail}")
    print(counts)


if __name__ == "__main__":
    main()
