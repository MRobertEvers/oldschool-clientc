#!/usr/bin/env python3
"""Read our own wire the way a real OldSchool client (RuneLite) would.

RuneLite cannot currently connect to this server -- its shipped client is
revision 240 and the newest vendored protocol and archived cache are 239
(docs/RSPROT_OSRS239_PORT.md §6) -- so "what would RuneLite make of this?"
cannot be answered by asking RuneLite. This asks the next best authority: the
RSProt tables the real client's protocol is generated from.

Two checks, and the second is the one that found a real bug.

  trace   Reads `TORIRSSERVER_TRACE_OUT=1` lines off a server log and validates
          every packet the server actually sent against the revision's prot
          table: is the opcode real, and is the length the one the prot
          declares? A fixed-size prot sent at the wrong length desynchronises
          the stream from that byte on, and the client's only report is a ring
          of recent opcodes and a stack of one-letter method names.

  tables  Compares this engine's canonical packet names against the revision's
          generated tables and lists every name the engine can BUILD but the
          revision has no row for. `net_out_opcode` refuses such a name by
          returning -1, and every caller treats that as "nothing to send" --
          so the packet is built, and then silently is not sent.

          That is not hypothetical. At revision 239 the twenty-two names
          OPHELD1..5, INV_BUTTON1..5 and IF_BUTTON1..10 collapse into one
          IF_BUTTONX whose `op` is a field, and the generated table left
          IF_BUTTONX itself at PKTOUT_NAME_NONE. The result was an equip that
          did nothing, a use-on that did nothing and a sidebar tab that did
          nothing, with a perfectly healthy-looking client and a server log
          that showed only keepalives.

Usage:
    tools/runelite_debug.py tables [--rev 239]
    tools/runelite_debug.py trace <server.log> [--rev 239]

    TORIRSSERVER_TRACE_OUT=1 src/build/torirsserver 43596 --rev osrs239 2> server.log
"""

import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

VAR_SIZES = {"RSPROT_VARU8": -1, "RSPROT_VARU16": -2}


def load_rsprot_table(rev):
    """{name: (opcode, size)} from 3rd/rsprot/gen/rev<NNN>_prot.h.

    Generated straight from RSProt's Kotlin, so it is the same statement of the
    wire the real client is built against rather than a transcription of it.
    """
    path = os.path.join(ROOT, "3rd", "rsprot", "gen", "rev%d_prot.h" % rev)
    if not os.path.exists(path):
        sys.exit("no vendored RSProt table for revision %d (%s)" % (rev, path))
    out = {}
    row = re.compile(r'\{\s*"([A-Z0-9_]+)"\s*,\s*(-?\d+)\s*,\s*(-?\d+|RSPROT_VAR\w+)\s*\}')
    section = None
    for line in open(path):
        # The generated header is three arrays in one file; the array name is
        # the only thing that says which direction a row belongs to.
        if "_server[]" in line:
            section = "server"
        elif "_client[]" in line:
            section = "client"
        elif "_zone_order[]" in line:
            section = "zone"
        hit = row.search(line)
        if not hit:
            continue
        name, opcode, size = hit.group(1), int(hit.group(2)), hit.group(3)
        size = VAR_SIZES.get(size, None) if size in VAR_SIZES else int(size)
        out.setdefault(section or "unknown", {})[name] = (opcode, size)
    return out


def load_engine_table(rev, direction):
    """{prot_name: (canonical_name, opcode, size)} from src/net/rev/osrs<NNN>/."""
    fname = "packetout.h" if direction == "out" else "packetin.h"
    path = os.path.join(ROOT, "src", "net", "rev", "osrs%d" % rev, fname)
    if not os.path.exists(path):
        sys.exit("no engine table at %s" % path)
    out = {}
    row = re.compile(
        r"\{\s*(PKT\w*_NAME_\w+)\s*,\s*(-?\d+)\s*,\s*([-\w]+)\s*,\s*\"([A-Z0-9_]+)\"\s*\}"
    )
    for line in open(path):
        hit = row.search(line)
        if hit:
            out[hit.group(4)] = (hit.group(1), int(hit.group(2)), hit.group(3))
    return out


REPLACED = re.compile(r"^\s*\*\s+(PKTOUT_NAME_)?([A-Z0-9_]+)\s+->\s+(\S+)")


def load_replacements(rev):
    """{canonical name: replacement prot} from packetout.h's own header block.

    The generator writes that block for exactly this question -- "this name has
    no row, and here is what took its place" -- so reading it back is how a
    fall-through builder proves it is covered rather than broken. Parsed rather
    than duplicated here: a second copy would disagree the first time a
    revision moves one.
    """
    path = os.path.join(ROOT, "src", "net", "rev", "osrs%d" % rev, "packetout.h")
    out = {}
    inside = False
    for line in open(path):
        if "Canonical names with NO row" in line:
            inside = True
            continue
        if inside:
            if "*/" in line or line.strip() in ("*", "*\n"):
                if "*/" in line:
                    break
                continue
            hit = REPLACED.match(line)
            if hit:
                out["PKTOUT_NAME_" + hit.group(2)] = hit.group(3)
            elif line.strip().startswith("* ") and "->" not in line:
                break
    return out


def engine_buildable_names():
    """Canonical outbound names net_out.c actually builds a packet for.

    Read off the source rather than from a list kept beside it: a list would go
    stale exactly when a new builder is added, which is the moment it matters.
    """
    path = os.path.join(ROOT, "src", "net", "net_out.c")
    return set(re.findall(r"PKTOUT_NAME_[A-Z0-9_]+", open(path).read()))


def cmd_tables(args):
    engine_out = load_engine_table(args.rev, "out")
    named = {c for (c, _o, _s) in engine_out.values()} - {"PKTOUT_NAME_NONE"}
    buildable = engine_buildable_names() - {"PKTOUT_NAME_NONE"}

    replaced = load_replacements(args.rev)
    prot_named = {p for p, (c, _o, _s) in engine_out.items() if c != "PKTOUT_NAME_NONE"}

    covered, unsendable = [], []
    for name in sorted(buildable - named):
        repl = replaced.get(name)
        if repl and repl in prot_named:
            covered.append((name, repl))
        else:
            unsendable.append(name)
    print("revision %d, client -> server" % args.rev)
    print("  %d prot rows, %d carrying a canonical name" % (len(engine_out), len(named)))
    print("  %d canonical names net_out.c can build" % len(buildable))
    if covered:
        print("  %d name(s) collapsed into another prot, which carries a canonical"
              " name:" % len(covered))
        for name, repl in covered:
            print("    %-28s -> %s" % (name.replace("PKTOUT_NAME_", ""), repl))
    if not unsendable:
        print("  every buildable name is either sendable or covered by a"
              " replacement")
        return 0
    print()
    print("  %d name(s) net_out.c BUILDS that this revision cannot send:" % len(unsendable))
    for name in unsendable:
        print("    %s" % name)
    print()
    print("  Each of these is refused by net_out_opcode and reported to nobody.")
    print("  Either the revision replaced it (map the replacement's prot to a")
    print("  canonical name in tools/rsprot_gen_rev.py's CANON_OUT and let the")
    print("  builder fall through to it), or the client should stop building it.")
    return 1


DEOB_DEFAULT = os.path.expanduser("~/Documents/git_repos/Deobfuscator/instr/src")

# The two prot tables inside the deobfuscated client. Both are plain lists of
# `new classNNN(opcode, size)`, and which direction each carries is not written
# anywhere in the jar -- it was established by finding the outbound packet
# allocator (`Statics.method13536`), which takes a class246, and the inbound
# decode switch (`client.method2413`), which compares against class243.
DEOB_PROT_CLASS = {"server": "class243", "client": "class246"}


def load_deob_table(direction, deob_src=DEOB_DEFAULT):
    """{opcode: size} straight out of the client's own prot list.

    This is the only table that is authoritative about what the real client
    will accept: RSProt is a faithful transcription of it and our own header is
    a transcription of RSProt, and a transcription can be wrong. A size that
    disagrees here is not a cosmetic difference — the client frames the stream
    by it, so one wrong row desynchronises everything after it.
    """
    name = DEOB_PROT_CLASS[direction]
    path = os.path.join(deob_src, name + ".java")
    if not os.path.exists(path):
        return None
    out = {}
    row = re.compile(r"new %s\((-?\d+),\s*(-?\d+)\)" % name)
    for hit in row.finditer(open(path).read()):
        out[int(hit.group(1))] = int(hit.group(2))
    return out


def cmd_prots(args):
    """Diff our revision table against the client's own."""
    direction = args.direction
    deob = load_deob_table(direction, args.deob_src)
    if deob is None:
        sys.exit("no deob source at %s — point --deob-src at Deobfuscator/instr/src"
                 % args.deob_src)

    fname = "packetin.h" if direction == "server" else "packetout.h"
    path = os.path.join(ROOT, "src", "net", "rev", "osrs%d" % args.rev, fname)
    ours = {}
    names = {}
    row = re.compile(
        r"\{\s*(PKT\w*_NAME_\w+)\s*,\s*(-?\d+)\s*,\s*([-\w]+)\s*,\s*\"([A-Z0-9_]+)\"\s*\}"
    )
    if direction == "server":
        row = re.compile(
            r"\{\s*(-?\d+)\s*,\s*([-\w]+)\s*,\s*(PKT\w*_NAME_\w+)\s*,\s*\"([A-Z0-9_]+)\"\s*\}"
        )
    for line in open(path):
        hit = row.search(line)
        if not hit:
            continue
        if direction == "server":
            opcode, size, _canon, prot = int(hit.group(1)), hit.group(2), hit.group(3), hit.group(4)
        else:
            _canon, opcode, size, prot = hit.group(1), int(hit.group(2)), hit.group(3), hit.group(4)
        ours[opcode] = size_to_int(size)
        names[opcode] = prot

    missing, extra, mismatched = [], [], []
    for opcode, size in sorted(deob.items()):
        if opcode not in ours:
            missing.append((opcode, size))
        elif ours[opcode] != size:
            mismatched.append((opcode, names[opcode], ours[opcode], size))
    for opcode in sorted(ours):
        if opcode not in deob:
            extra.append((opcode, names[opcode]))

    print("revision %d, %s -> %s"
          % (args.rev, "server" if direction == "server" else "client",
             "client" if direction == "server" else "server"))
    print("  client declares %d prots, we declare %d" % (len(deob), len(ours)))
    for opcode, prot, ourside, theirs in mismatched:
        print("  SIZE %d (%s): we say %s, the client says %s"
              % (opcode, prot, ourside, theirs))
    for opcode, size in missing:
        print("  MISSING opcode %d (client size %d) — we have no row; a packet on it"
              " cannot be framed" % (opcode, size))
    for opcode, prot in extra:
        print("  EXTRA opcode %d (%s) — the client has no such prot; sending it"
              " desynchronises the stream" % (opcode, prot))
    total = len(mismatched) + len(missing) + len(extra)
    print("  %d disagreement(s)" % total)
    return 1 if total else 0


def size_to_int(size):
    """The two headers spell the variable lengths differently; the client spells
    them as the raw -1/-2 the prot table carries."""
    if size in ("PKTOUT_LENGTH_VARU8", "PKTIN_LENGTH_VARU8", "-1"):
        return -1
    if size in ("PKTOUT_LENGTH_VARU16", "PKTIN_LENGTH_VARU16", "-2"):
        return -2
    try:
        return int(size)
    except ValueError:
        return None


TRACE = re.compile(r"torirsserver: -> op\s+(\d+)\s+(\S+)\s+(\d+) byte")


def cmd_trace(args):
    rsprot = load_rsprot_table(args.rev)
    server = rsprot.get("server", {})
    by_opcode = {op: (name, size) for name, (op, size) in server.items()}

    seen, bad = 0, 0
    for line in open(args.log):
        hit = TRACE.search(line)
        if not hit:
            continue
        seen += 1
        opcode, prot, length = int(hit.group(1)), hit.group(2), int(hit.group(3))
        if opcode not in by_opcode:
            print("  opcode %d (%s) is not in RSProt's revision-%d server set"
                  % (opcode, prot, args.rev))
            bad += 1
            continue
        real_name, real_size = by_opcode[opcode]
        if prot != "?" and prot != real_name:
            print("  opcode %d: we call it %s, RSProt calls it %s"
                  % (opcode, prot, real_name))
            bad += 1
        # Negative declared sizes are var-byte/var-short: any length is legal.
        if real_size is not None and real_size >= 0 and length != real_size:
            print("  %s (op %d): sent %d bytes, prot declares %d"
                  % (real_name, opcode, length, real_size))
            bad += 1

    if not seen:
        print("no TORIRSSERVER_TRACE_OUT lines in %s — run the server with"
              " TORIRSSERVER_TRACE_OUT=1 and capture stderr" % args.log)
        return 1
    print("%d packet(s) checked against RSProt revision %d, %d problem(s)"
          % (seen, args.rev, bad))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    t = sub.add_parser("tables", help="audit the engine's tables against RSProt")
    t.add_argument("--rev", type=int, default=239)
    t.set_defaults(func=cmd_tables)

    p = sub.add_parser("prots", help="diff our revision table against the deob client's own")
    p.add_argument("--rev", type=int, default=239)
    p.add_argument("--direction", choices=["server", "client"], default="server",
                   help="'server' = server->client (packetin), 'client' = client->server")
    p.add_argument("--deob-src", default=DEOB_DEFAULT)
    p.set_defaults(func=cmd_prots)

    r = sub.add_parser("trace", help="validate a captured outbound stream")
    r.add_argument("log")
    r.add_argument("--rev", type=int, default=239)
    r.set_defaults(func=cmd_trace)

    args = ap.parse_args()
    sys.exit(args.func(args))


if __name__ == "__main__":
    main()
