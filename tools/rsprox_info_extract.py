#!/usr/bin/env python3
"""
Read RSProx's info-stream clients (PlayerInfoClient.kt / NpcInfoClient.kt) into
per-revision READ PROGRAMS: for every decode function, the ordered list of wire
reads, each named after the Kotlin variable it lands in.

    tools/rsprox_info_extract.py PlayerInfoClient 241            # every function
    tools/rsprox_info_extract.py NpcInfoClient 239 decodeTinting

Used by tools/rsprox_gen_info.py, which turns the programs into
3rd/rsprot/streams/rsprot_info_tables.c. This module only READS; the decisions
about what a program means live in the generator, next to the templates it
checks every program against.

## Naming a read

A read is `buffer.gX(...)` (or `bitBuf.gBits(n)` in the bit sections). Its name
is the variable the statement assigns:

    val id = buffer.g2Alt2()                      id
    var flag = buffer.g1()  /  flag = ...         flag
    flag += buffer.g1() shl 8                     flag+
    recolours += buffer.g2Alt1()                  recolours+     (a list append)
    array[i] = buffer.g1Alt3().toByte()           array[]
    when (val type = buffer.gSmart1or2()) {       type
    blocks += SayExtendedInfo(buffer.gjstr())     value          (the block IS the read)
    buffer.gdata(data)                            data           (with the ByteArray's length)

and a read with no target of its own on its line -- the body of a multi-line
`val mirror = if (...) { buffer.g1Alt2() == 1 }` or `val identKit = List(n) {
buffer.g2() }` -- takes the name of the innermost `val` statement still open
around it.
"""

import glob
import os
import re
import sys

RSPROX = os.path.expanduser("~/rsprox-index-data/rsprox-upstream")


def client_path(cls, rev):
    hits = glob.glob(os.path.join(RSPROX, "protocol", f"osrs-{rev}", "src", "**", f"{cls}.kt"),
                     recursive=True)
    if len(hits) != 1:
        raise SystemExit(f"{cls}.kt at {rev}: {len(hits)} candidates")
    return hits[0]


def strip_comments(src):
    src = re.sub(r"//[^\n]*", "", src)
    return re.sub(r"/\*.*?\*/", "", src, flags=re.S)


def functions(src):
    """{name: body text between the outer braces} for every `fun` in the file."""
    out = {}
    for m in re.finditer(r"\bfun (\w+)\(", src):
        depth = 0
        p = m.end() - 1
        while p < len(src):
            if src[p] == "(":
                depth += 1
            elif src[p] == ")":
                depth -= 1
                if depth == 0:
                    break
            p += 1
        k = src.find("{", p)
        eq = src.find("=", p)
        if k < 0 or (0 <= eq < k and "\n" not in src[p:eq]):
            continue  # expression body: never a decoder here
        depth = 0
        for q in range(k, len(src)):
            if src[q] == "{":
                depth += 1
            elif src[q] == "}":
                depth -= 1
                if depth == 0:
                    out[m.group(1)] = src[k + 1:q]
                    break
    return out


def constants(src):
    """{NAME: int} for every `const val NAME[: Int] = 0x.. | ..` (an `or` of
    literals included, as UNUSED_FLAGS is written)."""
    out = {}
    for m in re.finditer(r"const val (\w+)\s*(?::\s*Int)?\s*=\s*([0-9xXa-fA-F_ ]+(?:or [0-9xXa-fA-F_ ]+)*)", src):
        v = 0
        for part in m.group(2).split("or"):
            part = part.strip().replace("_", "")
            if part:
                v |= int(part, 0)
        out[m.group(1)] = v
    return out


READ_RE = re.compile(r"(?:buffer|bitBuf)\.(g\w+)\(([^()]*)\)")
SKIP_RE = re.compile(r"buffer\.skipRead\((\d+)\)")


def _depth_delta(t):
    return t.count("{") + t.count("(") - t.count("}") - t.count(")")


def program(body):
    """[(name, fn, arg)] in source order. `arg` is the call's argument text
    (gBits' width, gdata's array name)."""
    prog = []
    open_vals = []  # (depth when opened, name)
    depth = 0
    arrays = {}
    for line in body.split("\n"):
        t = line.strip()
        if not t:
            continue
        m = re.match(r"val (\w+) = ByteArray\((\w+)\)", t)
        if m:
            arrays[m.group(1)] = m.group(2)
        own = None
        mv = re.match(r"(?:val|var)\s+(\w+)(?:\s*:\s*[\w<>?, ]+)?\s*=", t)
        if mv:
            own = mv.group(1)
        else:
            mw = re.search(r"when \((?:val|var)\s+(\w+)\s*=", t)
            if mw:
                own = mw.group(1)
            else:
                ma = re.match(r"(\w+)\[[^\]]*\]\s*=", t)
                if ma:
                    own = ma.group(1) + "[]"
                else:
                    mp = re.match(r"(\w+)\s*\+=", t)
                    if mp and mp.group(1) != "blocks":
                        own = mp.group(1) + "+"
                    else:
                        mr = re.match(r"(\w+(?:\.\w+)?)\s*=[^=]", t)
                        if mr:
                            own = mr.group(1)
        for rm in READ_RE.finditer(t):
            fn, arg = rm.group(1), rm.group(2)
            if fn.startswith("gdata"):
                name = arg
                prog.append(("data", fn, arrays.get(arg, "?")))
                continue
            if own:
                name = own
            elif t.startswith("blocks +=") or t.startswith("blocks.add"):
                name = "value"
            elif open_vals:
                name = open_vals[-1][1]
            else:
                name = "?"
            prog.append((name, fn, arg))
        for sm in SKIP_RE.finditer(t):
            prog.append(("skip", "skipRead", sm.group(1)))
        d = _depth_delta(t)
        # Close the statements this line finished BEFORE opening its own: an
        # open `val` ends at the first line after it that brings the depth
        # back to where it opened.
        depth += d
        while open_vals and open_vals[-1][2] and depth <= open_vals[-1][0]:
            open_vals.pop()
        for ov in open_vals:
            ov[2] = True
        if mv and (t.endswith("=") or d > 0):
            open_vals.append([depth - d, mv.group(1), False])
    return prog


def load(cls, rev):
    src = strip_comments(open(client_path(cls, rev)).read())
    return functions(src), constants(src)


def main():
    cls = sys.argv[1]
    rev = int(sys.argv[2])
    only = sys.argv[3] if len(sys.argv) > 3 else None
    fns, _consts = load(cls, rev)
    for name, body in fns.items():
        if only and name != only:
            continue
        prog = program(body)
        if prog:
            print(f"{name}: " + " ".join(f"{n}:{f}" + (f"({a})" if a else "") for n, f, a in prog))


if __name__ == "__main__":
    main()
