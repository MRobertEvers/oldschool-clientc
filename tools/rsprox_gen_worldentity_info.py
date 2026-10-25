#!/usr/bin/env python3
"""
Generate the WORLDENTITY_INFO_V3..V7 stream files under 3rd/rsprot/streams/ --
worldentity_info_v<N>.{h,c} (the versions), worldentity_info_osrs<rev>.c (each
revision's constants and entry points) and worldentity_info.{h,c} (the entry
points and lookups) -- from RSProx's world-entity info clients.

    tools/rsprox_gen_worldentity_info.py           # write them, revisions 227-241
    tools/rsprox_gen_worldentity_info.py --check   # fail if a checked-in one is stale

## Where RSProx keeps them

Each revision's WorldEntityInfoV<N>Decoder calls `world.worldEntity.decode(buffer,
baseCoord, <version>)` on the client the revision's StaticRebuildDecoder built
the world with: GenericWorldEntityInfoClient (protocol/src, 227-232) or the
revision's own WorldEntityInfoClient (233+). That client's
decode() picks a decodeHighResolutionV<a> / decodeLowResolutionV<b> pair by
version. This script follows the same chain for every prot at every revision
and reads the pair it lands on.

## What is generated and what is not

The two LOOPS are fixed (rsprot_info_worldentity.c): `plain` (V3-V5: an update
section of remove / idle / move entries, then spawn records while 10 bytes
remain) and `extended` (V6+: idle and move entries carry an extended-info
block, and spawns run while any byte remains). Which loop a prot uses is
decided here, by the Kotlin, and baked into the generated reader.

The SPAWN RECORD (the fields after the index, in order, each with its byte
transform) and the EXTENDED-INFO PAYLOAD (which flag bit is which block, and
its reads) are generated per layout as straight-line C: V7 reorders and
re-transforms both every revision.

Every statement that reads the buffer has to match a shape below, and every
loop has to match its skeleton; a revision whose Kotlin does anything else is
refused with the statement, because the C would read it wrong.
"""

import argparse
import glob
import os
import re
import sys
import textwrap

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import rsprox_info_extract as ex  # noqa: E402
import rsprox_prot  # noqa: E402

OUT_DIR = os.path.join(REPO_ROOT, "3rd", "rsprot", "streams")
REVS = range(223, 242)
VERSIONS = (3, 4, 5, 6, 7)
PROTOCOL = os.path.join(ex.RSPROX, "protocol")
GENERIC = os.path.join(PROTOCOL, "src", "main", "kotlin", "net", "rsprox", "protocol", "game",
                       "outgoing", "model", "info", "worldentityinfo",
                       "GenericWorldEntityInfoClient.kt")


class Refused(Exception):
    pass


def c_read(prim):
    """`g2sAlt1` -> `rsprot_g2s_alt1`."""
    m = re.fullmatch(r"g([1-4])(s?)(?:Alt([1-3]))?", prim)
    if not m:
        raise Refused(f"read {prim} not modelled")
    return f"rsprot_g{m.group(1)}{m.group(2)}" + (f"_alt{m.group(3)}" if m.group(3) else "")


def statements(body):
    return [t.strip() for t in body.split("\n") if t.strip()]


def client_of(rev, version):
    """(client source, its functions) for WORLDENTITY_INFO_V<version> at `rev`."""
    dec = ex.client_path(f"WorldEntityInfoV{version}Decoder", rev)
    src = ex.strip_comments(open(dec).read())
    m = re.search(r"worldEntity\.decode\(\s*buffer,\s*CoordGrid\([^)]*\),\s*(\d+),?\s*\)", src)
    if not m or int(m.group(1)) != version:
        raise Refused(f"V{version} at {rev}: decoder does not pass version {version}")
    # The client is the one the revision's world is built with -- by every
    # StaticRebuild*Decoder it has (237 split it into V1 and V2).
    kinds = set()
    for f in glob.glob(os.path.join(PROTOCOL, f"osrs-{rev}", "src", "**",
                                    "StaticRebuild*Decoder.kt"), recursive=True):
        rebuild = ex.strip_comments(open(f).read())
        if "GenericWorldEntityInfoClient()" in rebuild:
            kinds.add("generic")
        elif re.search(r"\bWorldEntityInfoClient\(\)", rebuild):
            kinds.add("revision")
    if len(kinds) != 1:
        raise Refused(f"V{version} at {rev}: rebuild decoders build {sorted(kinds) or 'no'} "
                      "world-entity clients")
    path = GENERIC if kinds == {"generic"} else ex.client_path("WorldEntityInfoClient", rev)
    csrc = ex.strip_comments(open(path).read())
    return path, ex.functions(csrc)


def pair(fns, version):
    """The (high, low) function names decode() runs for `version`."""
    body = fns["decode"]
    for m in re.finditer(r"if \(version >= (\d+)\) \{\s*(decodeHighResolution\w+)\(buffer\)\s*"
                         r"(decodeLowResolution\w+)\(buffer, baseCoord(?:, version)?\)", body):
        if version >= int(m.group(1)):
            return m.group(2), m.group(3)
    raise Refused(f"decode() has no branch for version {version}")


FINE = [
    "val bitpackedAngledCoordFineOpcodes = buffer.g1s()",
    "if (bitpackedAngledCoordFineOpcodes != 0) {",
]


def check_component(fns):
    body = re.sub(r"\s+", " ", fns["decodeAngledCoordFineComponent"])
    if not re.search(r"3 -> buffer\.g4\(\) 2 -> buffer\.g2s\(\) 1 -> buffer\.g1s\(\) else -> 0", body):
        raise Refused("decodeAngledCoordFineComponent is not g4 / g2s / g1s / 0")


def high(fns, name):
    """'plain' or ('extended', ext function) for one high-resolution section."""
    st = statements(fns[name])
    text = "\n".join(st)
    for frag in ("val count = buffer.g1()", "val opcode = buffer.g1()", "val remove = opcode == 0",
                 "if (opcode == 1) {", "val teleport = opcode == 3", FINE[0], FINE[1]):
        if frag not in text:
            raise Refused(f"{name}: missing `{frag}`")
    reads = [t for t in st if "buffer." in t]
    ext = re.findall(r"(decodeWorldEntityInfoExtendedInfo\w*)\(buffer(, flags)?\)", text)
    known = {"val count = buffer.g1()", "val opcode = buffer.g1()", FINE[0]}
    for t in reads:
        if t in known or "decodeAngledCoordFineComponent(buffer" in t:
            continue
        if t == "val flags = buffer.g1()" and ext:
            continue
        if re.match(r"val extendedInfo = decodeWorldEntityInfoExtendedInfo", t):
            continue
        raise Refused(f"{name}: read not modelled: {t}")
    if not ext:
        idle = text[text.index("if (opcode == 1) {"):]
        if not re.match(r"if \(opcode == 1\) \{\s*updates\[index\] = WorldEntityUpdateType\.Idle\s*"
                        r"continue", idle):
            raise Refused(f"{name}: idle entry is not bare")
        return "plain", None
    if len(ext) != 2 or ext[0] != ext[1]:
        raise Refused(f"{name}: extended info read {ext}, not once per idle and move")
    fn, flags_arg = ext[0]
    # One form reads its own flag byte, the other takes the byte the section
    # read just before it. Either way it is a g1, then the payload.
    if flags_arg and text.count("val flags = buffer.g1()") != 2:
        raise Refused(f"{name}: {fn} without its g1 flag byte")
    if reads_own_flags(fns, fn) == bool(flags_arg):
        raise Refused(f"{name}: {fn}'s flag byte read twice or not at all")
    return "extended", fn


def reads_own_flags(fns, name):
    """Whether an extended-info function opens with its own g1 flag byte."""
    if name not in fns:
        raise Refused(f"{name} not found")
    return statements(fns[name])[0] == "val flags = buffer.g1()"


def payload(fns, name):
    """[(bit, kind, [prims])] for an extended-info function, in read order."""
    body = fns[name]
    own = reads_own_flags(fns, name)
    out = []
    for m in re.finditer(r"if \(flags and (0x[0-9a-fA-F]+) != 0\) \{(.*?)\n\s*\}", body, flags=re.S):
        bit = int(m.group(1), 16)
        inner = m.group(2)
        seq = re.fullmatch(r"\s*val id = buffer\.(\w+)\(\)\s*val delay = buffer\.(\w+)\(\)\s*"
                           r"blocks \+= SequenceExtendedInfo\(id, delay\)\s*", inner)
        ops = re.fullmatch(r"\s*blocks \+= EnabledOpsExtendedInfo\(buffer\.(\w+)\(\)\)\s*", inner)
        if seq:
            out.append((bit, "SEQUENCE", [seq.group(1), seq.group(2)]))
        elif ops:
            out.append((bit, "OPS", [ops.group(1)]))
        else:
            raise Refused(f"{name}: block {m.group(1)} not modelled: {inner.strip()}")
    reads = len(re.findall(r"buffer\.g\w+\(", body)) - (1 if own else 0)
    if reads != sum(len(p) for _b, _k, p in out):
        raise Refused(f"{name}: a read outside the modelled blocks")
    kinds = [k for _b, k, _p in out]
    bits = [b for b, _k, _p in out]
    if not kinds or len(set(kinds)) != len(kinds) or len(set(bits)) != len(bits):
        raise Refused(f"{name}: blocks {out}")
    return out


def low(fns, name, version):
    """(lookahead, [field], ext function or None) for one low-resolution section."""
    body = fns[name]
    m = re.search(r"while \(buffer\.isReadable(?:\((\d+)\))?\) \{", body)
    if not m:
        raise Refused(f"{name}: no record loop")
    lookahead = int(m.group(1) or 1)
    st = statements(body[m.end():])
    fields = []
    ext = None
    i = 0
    while i < len(st):
        t = st[i]
        i += 1
        if "buffer" not in t:
            continue
        if t == "val index = buffer.g2()":
            if fields:
                raise Refused(f"{name}: index is not the first field")
            fields.append(("INDEX", None))
        elif (mm := re.fullmatch(r"val size([XZ]) = buffer\.(\w+)\(\) \* 8", t)):
            fields.append((f"SIZE_{mm.group(1)}", mm.group(2)))
        elif (mm := re.fullmatch(r"val packedSize = buffer\.(\w+)\(\)", t)):
            fields.append(("PACKED_SIZE", mm.group(1)))
        elif (mm := re.fullmatch(r"val level = buffer\.(\w+)\(\)", t)):
            fields.append(("LEVEL", mm.group(1)))
        elif (mm := re.fullmatch(r"val id = buffer\.(\w+)\(\)", t)):
            fields.append(("ID", mm.group(1)))
        elif (mm := re.fullmatch(r"val priority = buffer\.(\w+)\(\)", t)):
            fields.append(("PRIORITY", mm.group(1)))
        elif (mm := re.fullmatch(r"val extendedInfoFlags = buffer\.(\w+)\(\)", t)):
            fields.append(("EXT_FLAGS", mm.group(1)))
        elif t == FINE[0]:
            if st[i] != FINE[1]:
                raise Refused(f"{name}: fine transform not modelled")
            i += 1
            while i < len(st) and "decodeAngledCoordFineComponent(buffer" in st[i]:
                i += 1
            fields.append(("FINE", None))
        elif (mm := re.fullmatch(r"val centerFineOffset([XZ]) = if \(version >= (\d+)\) "
                                 r"buffer\.(\w+)\(\) else null", t)):
            if version >= int(mm.group(2)):
                fields.append((f"CENTRE_{mm.group(1)}", mm.group(3)))
        elif (mm := re.fullmatch(r"val extendedInfo = (decodeWorldEntityInfoExtendedInfo\w*)"
                                 r"\(buffer(, extendedInfoFlags)?\)", t)):
            if reads_own_flags(fns, mm.group(1)) == bool(mm.group(2)):
                raise Refused(f"{name}: {t}: flag byte read twice or not at all")
            ext = mm.group(1)
            fields.append(("EXT_PAYLOAD" if mm.group(2) else "EXT", None))
        elif t.startswith("val coord = CoordGrid(baseCoord") or t.startswith("coordFine ="):
            continue
        else:
            raise Refused(f"{name}: read not modelled: {t}")
    kinds = [k for k, _p in fields]
    if kinds[0] != "INDEX" or "FINE" not in kinds:
        raise Refused(f"{name}: fields {kinds}")
    if ("PACKED_SIZE" in kinds) == ("SIZE_X" in kinds):
        raise Refused(f"{name}: sizes {kinds}")
    if ("EXT_PAYLOAD" in kinds) != ("EXT_FLAGS" in kinds):
        raise Refused(f"{name}: extended-info flags and payload {kinds}")
    return lookahead, fields, ext


def layout(rev, version):
    path, fns = client_of(rev, version)
    check_component(fns)
    hi, lo = pair(fns, version)
    skeleton, hi_ext = high(fns, hi)
    lookahead, fields, lo_ext = low(fns, lo, version)
    if skeleton == "plain" and (lookahead != 10 or lo_ext):
        raise Refused(f"V{version} at {rev}: plain loop with lookahead {lookahead}, ext {lo_ext}")
    if skeleton == "extended" and (lookahead != 1 or not lo_ext):
        raise Refused(f"V{version} at {rev}: extended loop with lookahead {lookahead}, ext {lo_ext}")
    pay = None
    if skeleton == "extended":
        pay = payload(fns, hi_ext)
        if lo_ext != hi_ext and payload(fns, lo_ext) != pay:
            raise Refused(f"V{version} at {rev}: spawns and updates read different payloads")
    return {"skeleton": skeleton, "fields": fields, "payload": pay,
            "source": os.path.relpath(path, ex.RSPROX)}


# --- emission -------------------------------------------------------------


def rev_ranges(revs):
    out = []
    for r in revs:
        if out and out[-1][1] == r - 1:
            out[-1] = (out[-1][0], r)
        else:
            out.append((r, r))
    return out


def rev_text(revs):
    return ", ".join(f"{lo}" if lo == hi else f"{lo}-{hi}" for lo, hi in rev_ranges(revs))


def emit_payload(name, pay):
    out = ["static void", f"{name}(RsprotBuf *b, int32_t flags, RsprotInfoWeExt *e)", "{",
           "    e->flags = flags;"]
    for bit, kind, prims in pay:
        out.append(f"    if (flags & 0x{bit:x}) {{")
        if kind == "SEQUENCE":
            out += ["        e->has_sequence = 1;",
                    f"        e->sequence_id = {c_read(prims[0])}(b);",
                    f"        e->sequence_delay = {c_read(prims[1])}(b);"]
        else:
            out += ["        e->has_ops = 1;", f"        e->ops = {c_read(prims[0])}(b);"]
        out.append("    }")
    return out + ["}", ""]


def emit_spawn(name, fields, payload_fn, ext_fn):
    kinds = [k for k, _p in fields]
    out = ["static void", f"{name}(RsprotBuf *b, RsprotInfoWeAdd *a)", "{"]
    if "EXT_FLAGS" in kinds:
        out += ["    int32_t ext_flags;", ""]
    if "ID" not in kinds:
        out.append("    a->id = -1;")
    if "PRIORITY" not in kinds:
        out.append("    a->priority = -1;")
    for kind, prim in fields[1:]:
        if kind in ("SIZE_X", "SIZE_Z"):
            out.append(f"    a->size_{kind[-1].lower()} = {c_read(prim)}(b) * 8;")
        elif kind == "PACKED_SIZE":
            out += ["    {", f"        int32_t packed = {c_read(prim)}(b);", "",
                    "        a->size_x = ((packed >> 4) & 0xf) * 8;",
                    "        a->size_z = (packed & 0xf) * 8;", "    }"]
        elif kind == "LEVEL":
            out += ["    a->has_level = 1;", f"    a->level = {c_read(prim)}(b);"]
        elif kind == "ID":
            out.append(f"    a->id = {c_read(prim)}(b);")
        elif kind == "PRIORITY":
            out.append(f"    a->priority = {c_read(prim)}(b);")
        elif kind == "EXT_FLAGS":
            out.append(f"    ext_flags = {c_read(prim)}(b);")
        elif kind == "FINE":
            out.append("    rsprot_info_we_fine(b, &a->x, &a->y, &a->z, &a->angle);")
        elif kind in ("CENTRE_X", "CENTRE_Z"):
            if kind == "CENTRE_X":
                out.append("    a->has_centre = 1;")
            out.append(f"    a->centre_{kind[-1].lower()} = {c_read(prim)}(b);")
        elif kind == "EXT":
            out.append(f"    {ext_fn}(b, &a->ext);")
        elif kind == "EXT_PAYLOAD":
            out.append(f"    {payload_fn}(b, ext_flags, &a->ext);")
    return out + ["}", ""]


def wrap_params(sig, width=92):
    """A parameter list broken between parameters, never inside one."""
    lines = [""]
    for p in sig.split(", "):
        piece = p if not lines[-1] else ", " + p
        if lines[-1] and len(lines[-1]) + len(piece) > width:
            lines[-1] += ","
            lines.append(p)
        else:
            lines[-1] += piece
    return "\n    ".join(lines)


def banner(lines):
    """A comment of `lines`, each a paragraph wrapped to 76 columns ("" is a
    blank line)."""
    body = []
    for para in _paras(lines):
        body += [f" * {l}" for l in textwrap.wrap(para, 76)] if para else [" *"]
    return (["/*"] + body +
            [" *", " * GENERATED by tools/rsprox_gen_worldentity_info.py from RSProx "
             f"({rsprox_prot.source_hash()}) -- do not edit.", " */"])


def _paras(lines):
    """Join consecutive non-blank lines into paragraphs; "" separates them."""
    out = [""]
    for l in lines:
        if l == "":
            out += ["", ""]
        else:
            out[-1] = (out[-1] + " " + l).strip()
    return [p for p in out if p != ""] if len(out) == 1 else out


SIG = "const uint8_t *data, int32_t len, RsprotInfoList *out"


def emit_version(version, revs):
    """{path: text}: WORLDENTITY_INFO_V<version>'s reader and its constants' shape."""
    stem = f"worldentity_info_v{version}"
    type_name = f"WorldEntityInfoV{version}"
    covered = [r for r, _lay in revs]
    skeletons = {lay["skeleton"] for _r, lay in revs}
    if len(skeletons) != 1:
        raise Refused(f"WORLDENTITY_INFO_V{version}: loops {sorted(skeletons)} across its revisions")
    skeleton = skeletons.pop()
    loop = ("bare update entries, then spawns while 10 bytes remain" if skeleton == "plain"
            else "update entries that end in extended info, then spawns to the last byte")
    h = [f"#ifndef RSPROT_STREAM_WORLDENTITY_INFO_V{version}_H",
         f"#define RSPROT_STREAM_WORLDENTITY_INFO_V{version}_H", ""]
    h += banner([f"WORLDENTITY_INFO_V{version} -- revs {rev_text(covered)}: the version, its",
                 "constants' shape, and the reader each revision's entry point calls. It reads",
                 f"{loop} (rsprot_info_worldentity.c); a revision's constants are its spawn",
                 "record" + (" and its extended-info payload" if skeleton == "extended" else "") +
                 ", in that revision's field order and byte transforms."])
    h += ["", '#include "rsprot_info.h"', '#include "rsprot_info_internal.h"', "",
          f"typedef struct {type_name}", "{",
          "    /** The spawn record after its index. */",
          "    RsprotWeSpawnRead spawn;"]
    if skeleton == "extended":
        h += ["    /** An update entry's extended info: its flag byte, then the payload. */",
              "    RsprotWeExtRead ext;"]
    h += [f"}} {type_name};", "",
          f"int {stem}(", "    " + wrap_params(f"const {type_name} *k, " + SIG) + ");", "", "#endif"]
    c = banner([f"WORLDENTITY_INFO_V{version} -- revs {rev_text(covered)}. See {stem}.h."])
    c += [f'#include "{stem}.h"', "", "#include <assert.h>", "",
          "int", f"{stem}(", "    " + wrap_params(f"const {type_name} *k, " + SIG) + ")", "{",
          "    assert(k);"]
    if skeleton == "plain":
        c.append("    return rsprot_info_worldentity_plain_read(k->spawn, data, len, out);")
    else:
        c += ["    return rsprot_info_worldentity_extended_read(",
              "        k->spawn, k->ext, data, len, out);"]
    c.append("}")
    return {os.path.join(OUT_DIR, stem + ".h"): "\n".join(h) + "\n",
            os.path.join(OUT_DIR, stem + ".c"): "\n".join(c) + "\n"}


def emit_revision(rev, versions):
    """{path: text}: the constants of the WORLDENTITY_INFO versions `rev`
    carries, and their entry points. `versions`: [(version, layout)]."""
    rstem = f"worldentity_info_osrs{rev}"
    c = banner([f"WORLDENTITY_INFO at revision {rev}: the constants of the version(s) it carries",
                "(" + ", ".join(f"V{v}" for v, _l in versions) + "), and their entry points. The spawn record's",
                "field order and byte transforms, and the extended-info payload's, are this",
                "revision's."])
    c += ['#include "worldentity_info.h"'] + [f'#include "worldentity_info_v{v}.h"' for v, _l in versions]
    entries = []
    for v, lay in versions:
        base = f"{rstem}_v{v}"
        payload_fn = ext_fn = None
        c += ["", f"/* WORLDENTITY_INFO_V{v}. */"]
        if lay["skeleton"] == "extended":
            payload_fn = f"{base}_ext_payload"
            ext_fn = f"{base}_ext"
            c += emit_payload(payload_fn, lay["payload"])
            c += ["static void", f"{ext_fn}(RsprotBuf *b, RsprotInfoWeExt *e)", "{",
                  f"    {payload_fn}(b, rsprot_g1(b), e);", "}", ""]
        c += emit_spawn(f"{base}_spawn", lay["fields"], payload_fn, ext_fn)
        c += [f"static const WorldEntityInfoV{v} k_{base} = {{", f"    .spawn = {base}_spawn,"]
        if ext_fn:
            c.append(f"    .ext = {ext_fn},")
        c += ["};", "",
              "int", f"worldentity_info_v{v}_osrs{rev}(", "    " + wrap_params(SIG) + ")", "{",
              f"    return worldentity_info_v{v}(&k_{base}, data, len, out);", "}"]
        entries.append((v, f"worldentity_info_v{v}_osrs{rev}"))
    return {os.path.join(OUT_DIR, rstem + ".c"): "\n".join(c) + "\n"}, entries


def emit_public(entries):
    """worldentity_info.h / .c: every entry point, and a lookup per version."""
    h = ["#ifndef RSPROT_STREAM_WORLDENTITY_INFO_H", "#define RSPROT_STREAM_WORLDENTITY_INFO_H", ""]
    h += banner(["WORLDENTITY_INFO_V3..V7 -- server -> client: the per-revision entry points.", "",
                 "worldentity_info_v<X>_osrs<N> reads revision N's WORLDENTITY_INFO_V<X> through",
                 "the version, worldentity_info_v<X> (worldentity_info_v<X>.c)."])
    h += ["", '#include "rsprot_info.h"', ""]
    for rev in sorted(entries):
        for _v, fn in entries[rev]:
            h += [f"int {fn}(", "    " + wrap_params(SIG) + ");"]
    c = banner(["The revision -> entry point lookups for WORLDENTITY_INFO."])
    c += ['#include "worldentity_info.h"', "", "#include <stddef.h>"]
    h.append("")
    for v in VERSIONS:
        rows = [(rev, fn) for rev in sorted(entries) for vv, fn in entries[rev] if vv == v]
        h += [f"/** worldentity_info_v{v}_osrs<revision>, or NULL where it has no such prot. */",
              f"RsprotWorldEntityInfoReadFn worldentity_info_v{v}_osrs(int revision);"]
        c += ["", "RsprotWorldEntityInfoReadFn", f"worldentity_info_v{v}_osrs(int revision)", "{",
              "    switch (revision) {"] + [f"    case {r}: return {fn};" for r, fn in rows] + \
             ["    default: return NULL;", "    }", "}"]
    h += ["", "#endif"]
    return {os.path.join(OUT_DIR, "worldentity_info.h"): "\n".join(h) + "\n",
            os.path.join(OUT_DIR, "worldentity_info.c"): "\n".join(c) + "\n"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    by_version = {v: [] for v in VERSIONS}
    failed = False
    for rev in REVS:
        names = {r["name"] for r in rsprox_prot.load_prots(rev)["server"]}
        for v in VERSIONS:
            if f"WORLDENTITY_INFO_V{v}" not in names:
                continue
            try:
                lay = layout(rev, v)
                by_version[v].append((rev, lay))
                print(f"rev {rev} V{v}: {lay['skeleton']}, "
                      f"{' '.join(k for k, _p in lay['fields'])}", file=sys.stderr)
            except Refused as e:
                print(f"REFUSED rev {rev} V{v}: {e}", file=sys.stderr)
                failed = True
    if failed:
        raise SystemExit(1)
    files = {}
    by_rev = {}
    for v in VERSIONS:
        files.update(emit_version(v, by_version[v]))
        for rev, lay in by_version[v]:
            by_rev.setdefault(rev, []).append((v, lay))
    entries = {}
    for rev in sorted(by_rev):
        f, entries[rev] = emit_revision(rev, by_rev[rev])
        files.update(f)
    files.update(emit_public(entries))
    if args.check:
        stale = [p for p, text in files.items() if not os.path.exists(p) or open(p).read() != text]
        if stale:
            raise SystemExit(f"{', '.join(stale)} stale; run tools/rsprox_gen_worldentity_info.py")
        return
    for p, text in files.items():
        with open(p, "w") as fh:
            fh.write(text)


if __name__ == "__main__":
    main()
