#!/usr/bin/env python3
"""Audit server-visible varps consumed by the revision-239 CS2 corpus.

The mechanical half answers: which numeric varps do decompiled CS2 scripts
reference, which corresponding symbols do server scripts reference, and what
does the server's .varp policy currently say?  The human half lives in
port/cs2_varps.map and records what each value means and what should own it.

Usage:
    tools/cs2_varp_audit.py --report
    tools/cs2_varp_audit.py --write-map
    tools/cs2_varp_audit.py --check

`--check` intentionally keeps resolved rows in the map.  The file is a ledger
for the original queue, not only a snapshot of today's failures.  It also fails
when a newly discovered transmission gap has no row.
"""

import argparse
import collections
import os
import re
import sys


REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TREE = os.path.join(REPO, "OSRS-Content", "osrs239-content")
MAP = os.path.join(TREE, "port", "cs2_varps.map")

GENERATED = [
    "id", "symbol", "cs2_uses", "cs2_writes", "cs2_scripts",
    "server_uses", "server_writes", "server_scripts", "definition",
    "transmit", "carrier_bits",
]
HUMAN = [
    "owner", "value_domain", "lifetime", "producer", "consumer", "timing",
    "disposition", "status", "evidence",
]
COLUMNS = GENERATED + HUMAN

DISPOSITIONS = {
    "unreviewed", "transmit-varp", "transmit-varbit", "derived",
    "client-local", "obsolete", "blocked",
}
STATUSES = {"unreviewed", "understood", "implemented", "verified", "blocked"}

CS2_REF_RE = re.compile(r"%var(\d+)\b")
CS2_WRITE_RE = re.compile(r"^\s*%var(\d+)\s*=")
SERVER_REF_RE = re.compile(r"%([A-Za-z_][A-Za-z0-9_]*)\b")
SERVER_WRITE_RE = re.compile(r"^\s*%([A-Za-z_][A-Za-z0-9_]*)\s*=")


HEADER = """\
# port/cs2_varps.map — reviewed ledger for varps shared by CS2 and server scripts.
#
# Mechanical columns are maintained by tools/cs2_varp_audit.py. Human columns
# are preserved by --write-map and must be reviewed before behavior changes.
# Tabs separate columns; commas separate file names inside a column.
#
# generated columns
#   id/symbol       revision-239 cache identity
#   cs2_*           numeric %var<ID> references in scripts/*.cs2
#   server_*        matching symbolic %name references in server .rs2 files
#   definition      server .varp file(s), or -
#   transmit        yes, no, or none (no declaration)
#   carrier_bits    varbits whose basevar is this varp
#
# human columns
#   owner           subsystem that owns the value
#   value_domain    values, ranges, sentinels, and meanings
#   lifetime        interface/session/temp/perm
#   producer        authoritative mutation source
#   consumer        client behavior driven by the value
#   timing          login/pre-open/on-change/client-only
#   disposition     unreviewed/transmit-varp/transmit-varbit/derived/
#                   client-local/obsolete/blocked
#   status          unreviewed/understood/implemented/verified/blocked
#   evidence        exact source/runtime evidence; '-' means not reviewed
"""


def walk(root, suffix):
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in (".git", "build", "dist")]
        for name in filenames:
            if name.endswith(suffix):
                yield os.path.join(dirpath, name)


def blocks(path):
    section, values, line = None, {}, 0
    with open(path, encoding="utf-8", errors="replace") as handle:
        for number, raw in enumerate(handle, 1):
            text = raw.split("//", 1)[0].strip()
            if not text:
                continue
            if text.startswith("[") and "]" in text:
                if section is not None:
                    yield section, values, line
                section = text[1:text.index("]")]
                values, line = {}, number
            elif section is not None and "=" in text:
                key, value = text.split("=", 1)
                values[key.strip()] = value.strip()
    if section is not None:
        yield section, values, line


def pack_ids():
    name_to_id = {}
    for path in (
        os.path.join(TREE, "configs", "all.varp.compack"),
        os.path.join(TREE, "pack", "varp.alloc"),
    ):
        if not os.path.exists(path):
            continue
        with open(path, encoding="utf-8", errors="replace") as handle:
            for raw in handle:
                text = raw.split("//", 1)[0].strip()
                if "=" not in text:
                    continue
                number, name = text.split("=", 1)
                try:
                    name_to_id[name.strip()] = int(number.strip())
                except ValueError:
                    pass
    return name_to_id


def carrier_counts():
    counts = collections.Counter()
    path = os.path.join(TREE, "configs", "all.varbit")
    for _, values, _ in blocks(path):
        base = values.get("basevar")
        if base:
            counts[base] += 1
    return counts


def scan_cs2():
    facts = collections.defaultdict(lambda: {
        "uses": 0, "writes": 0, "scripts": set(), "locations": [],
    })
    root = os.path.join(TREE, "scripts")
    for path in walk(root, ".cs2"):
        rel = os.path.relpath(path, root)
        with open(path, encoding="utf-8", errors="replace") as handle:
            for number, raw in enumerate(handle, 1):
                text = raw.split("//", 1)[0]
                assigned = CS2_WRITE_RE.match(text)
                # `=` is both assignment and equality in RuneScript.  A line
                # beginning with a varp can still be a continued condition;
                # assignments are complete statements and end in `;`.
                if assigned and text.rstrip().endswith(";"):
                    facts[int(assigned.group(1))]["writes"] += 1
                for match in CS2_REF_RE.finditer(text):
                    varp_id = int(match.group(1))
                    fact = facts[varp_id]
                    fact["uses"] += 1
                    fact["scripts"].add(rel)
                    fact["locations"].append("%s:%d" % (rel, number))
    return facts


def scan_server(name_to_id):
    facts = collections.defaultdict(lambda: {
        "uses": 0, "writes": 0, "scripts": set(), "locations": [],
    })
    root = os.path.join(TREE, "server")
    for path in walk(root, ".rs2"):
        rel = os.path.relpath(path, root)
        with open(path, encoding="utf-8", errors="replace") as handle:
            for number, raw in enumerate(handle, 1):
                text = raw.split("//", 1)[0]
                assigned = SERVER_WRITE_RE.match(text)
                if (assigned and text.rstrip().endswith(";") and
                        assigned.group(1) in name_to_id):
                    facts[name_to_id[assigned.group(1)]]["writes"] += 1
                for match in SERVER_REF_RE.finditer(text):
                    name = match.group(1)
                    if name not in name_to_id:
                        continue
                    varp_id = name_to_id[name]
                    fact = facts[varp_id]
                    fact["uses"] += 1
                    fact["scripts"].add(rel)
                    fact["locations"].append("%s:%d" % (rel, number))
    return facts


def scan_definitions(name_to_id):
    definitions = collections.defaultdict(list)
    transmit = {}
    root = os.path.join(TREE, "server")
    for path in walk(root, ".varp"):
        rel = os.path.relpath(path, root)
        for name, values, line in blocks(path):
            if name not in name_to_id:
                continue
            varp_id = name_to_id[name]
            definitions[varp_id].append("%s:%d" % (rel, line))
            current = "yes" if values.get("transmit") == "yes" else "no"
            if transmit.get(varp_id) == "yes" or current == "yes":
                transmit[varp_id] = "yes"
            else:
                transmit[varp_id] = "no"
    return definitions, transmit


def measure():
    name_to_id = pack_ids()
    id_to_name = {number: name for name, number in name_to_id.items()}
    carriers = carrier_counts()
    cs2 = scan_cs2()
    server = scan_server(name_to_id)
    definitions, transmit = scan_definitions(name_to_id)
    rows = {}
    for varp_id in sorted(set(cs2) & set(server)):
        symbol = id_to_name.get(varp_id, "varp_%d" % varp_id)
        rows[varp_id] = {
            "id": str(varp_id),
            "symbol": symbol,
            "cs2_uses": str(cs2[varp_id]["uses"]),
            "cs2_writes": str(cs2[varp_id]["writes"]),
            "cs2_scripts": ",".join(sorted(cs2[varp_id]["scripts"])),
            "server_uses": str(server[varp_id]["uses"]),
            "server_writes": str(server[varp_id]["writes"]),
            "server_scripts": ",".join(sorted(server[varp_id]["scripts"])),
            "definition": ",".join(sorted(definitions.get(varp_id, []))) or "-",
            "transmit": transmit.get(varp_id, "none"),
            "carrier_bits": str(carriers.get(symbol, 0)),
        }
    return rows


def read_map():
    rows = {}
    if not os.path.exists(MAP):
        return rows
    with open(MAP, encoding="utf-8", errors="replace") as handle:
        for number, raw in enumerate(handle, 1):
            text = raw.rstrip("\n")
            if not text or text.startswith("#"):
                continue
            fields = text.split("\t")
            if len(fields) != len(COLUMNS):
                raise ValueError("%s:%d: expected %d tab fields, got %d" %
                                 (MAP, number, len(COLUMNS), len(fields)))
            row = dict(zip(COLUMNS, fields))
            rows[int(row["id"])] = row
    return rows


def default_human():
    return {
        "owner": "-", "value_domain": "-", "lifetime": "-",
        "producer": "-", "consumer": "-", "timing": "-",
        "disposition": "unreviewed", "status": "unreviewed", "evidence": "-",
    }


def merged_rows(measured, saved):
    ids = sorted(set(saved) | {
        number for number, row in measured.items() if row["transmit"] != "yes"
    })
    out = []
    for number in ids:
        if number not in measured:
            row = dict(saved[number])
        else:
            row = dict(measured[number])
            row.update({key: saved.get(number, default_human()).get(key, "-")
                        for key in HUMAN})
        out.append(row)
    return out


def format_map(rows):
    lines = [HEADER.rstrip(), "#", "# " + "\t".join(COLUMNS)]
    for row in rows:
        lines.append("\t".join(row.get(column, "-") for column in COLUMNS))
    return "\n".join(lines) + "\n"


def check(measured, saved):
    errors = []
    current_gaps = {number for number, row in measured.items() if row["transmit"] != "yes"}
    missing = sorted(current_gaps - set(saved))
    if missing:
        errors.append("new/unclassified gap(s): %s" % ", ".join(map(str, missing)))

    for number, saved_row in sorted(saved.items()):
        current = measured.get(number)
        if not current:
            errors.append("varp %d no longer has both CS2 and server references" % number)
            continue
        for column in GENERATED:
            if saved_row[column] != current[column]:
                errors.append("varp %d %s stale: map=%r tree=%r" %
                              (number, column, saved_row[column], current[column]))
        disposition = saved_row["disposition"]
        status = saved_row["status"]
        if disposition not in DISPOSITIONS:
            errors.append("varp %d unknown disposition %r" % (number, disposition))
        if status not in STATUSES:
            errors.append("varp %d unknown status %r" % (number, status))
        if disposition == "transmit-varp" and current["carrier_bits"] != "0":
            errors.append("varp %d is a carrier; transmit-varp is unsafe" % number)
        if status in ("implemented", "verified"):
            if disposition == "unreviewed" or saved_row["evidence"] == "-":
                errors.append("varp %d is %s without disposition/evidence" %
                              (number, status))
            if disposition == "transmit-varp" and current["transmit"] != "yes":
                errors.append("varp %d says implemented but transmit is %s" %
                              (number, current["transmit"]))

    if errors:
        for error in errors:
            print("cs2-varp-audit: ERROR: " + error, file=sys.stderr)
        return 1
    print("cs2-varp-audit: %d ledger row(s), %d current gap(s), clean" %
          (len(saved), len(current_gaps)))
    return 0


def report(measured):
    gaps = [(number, row) for number, row in sorted(measured.items())
            if row["transmit"] != "yes"]
    print("shared varps: %d; current gaps: %d" % (len(measured), len(gaps)))
    for number, row in gaps:
        print("%4d %-32s transmit=%-4s cs2=%s" %
              (number, row["symbol"], row["transmit"], row["cs2_scripts"]))


def main():
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--report", action="store_true")
    mode.add_argument("--write-map", action="store_true")
    mode.add_argument("--check", action="store_true")
    args = parser.parse_args()

    measured = measure()
    if args.report:
        report(measured)
        return 0
    try:
        saved = read_map()
    except ValueError as error:
        print("cs2-varp-audit: ERROR: %s" % error, file=sys.stderr)
        return 1
    if args.write_map:
        with open(MAP, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(format_map(merged_rows(measured, saved)))
        print("wrote %s" % MAP)
        return 0
    return check(measured, saved)


if __name__ == "__main__":
    raise SystemExit(main())
