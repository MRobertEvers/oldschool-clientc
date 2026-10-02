#!/usr/bin/env python3
"""raid_coverage -- grade a raid room test's tick ledger against its encounter spec
table (docs/RAID_ORCHESTRATOR.md sections 5 and 6).

    python3 tools/raid_gate/raid_coverage.py tob_maiden [tob_bloat ...] [--ledger PATH]

For test id <raid>_<room> the spec table is docs/minigames/<raid docs>/encounters/
<room>.tsv (tools/raid_gate/spec_check.py defines it); a whole-raid id (tob_entry,
cox_solo, toa_150) is graded against every room table of its raid. The ledger is
build/quest_gate/<id>/ledger.tsv (or --ledger).

THE ROW CONTRACT. For every spec row the test writes one PASS ledger row named
`spec.<mechanic_id>` whose detail starts with

    measured <value>[ <unit>][, <free text>] (spec <value>[ <unit>], grade <A-E>, tol <tolerance>)

e.g. `measured 10 ticks, 40 of 40 gaps (spec 10 ticks, grade B, tol exact)`,
`measured 9 (spec 9, grade B, tol +-1)`, `measured 3 (spec 1-5, grade E, tol approx); approximation, M40`.
<value> is a number, a range a-b, or a comma list; a measured list is every
instance (a distribution), and every instance must satisfy the tolerance:
exact: measured == spec (or in the spec list); +-N: within N of the spec (a
spec range: within N of its ends); range: inside the spec range; approx (grade
E only): no numeric check, but the detail must carry "approximation, M<n>".
The grade in the row must equal the table's grade, so a promoted or demoted
grade is a conscious edit of the table, never of the test alone.

Verdict per test: FULL (every spec row measured and within tolerance) or a list of
findings: `unmeasured <id>`, `out of tolerance <id>: measured .. vs spec ..`,
`grade mismatch <id>`, `malformed <id>`. Exit 0 when every requested test is FULL.
"""
import glob
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "quest_gate"))
import ledger  # noqa: E402

RAID_DOCS = {"tob": "theater_of_blood", "toa": "tombs_of_amascut", "cox": "cox"}
FULL_RAID_ROOMS = {"entry", "solo", "150"}
ROW_RE = re.compile(r"^measured\s+(?P<m>[-\d.,]+(?:-[\d.]+)?)(?:\s+[a-z]+)?[^()]*\(spec\s+(?P<s>[-\d.,?]+(?:-[\d.]+)?)(?:\s+[a-z]+)?,\s*grade\s+(?P<g>[A-E]),\s*tol\s+(?P<t>exact|\+-\d+|range|approx)\)")


def spec_tables(test_id):
    raid, _, room = test_id.partition("_")
    assert raid in RAID_DOCS, "unknown raid prefix in %s" % test_id
    directory = os.path.join(ROOT, "docs", "minigames", RAID_DOCS[raid], "encounters")
    if room in FULL_RAID_ROOMS:
        return sorted(p for p in glob.glob(os.path.join(directory, "*.tsv"))
                      if os.path.basename(p)[:-4] not in FULL_RAID_ROOMS)
    path = os.path.join(directory, "%s.tsv" % room)
    return [path] if os.path.isfile(path) else []


def read_spec(path):
    rows = []
    with open(path, encoding="utf-8") as f:
        lines = [l.rstrip("\n") for l in f if l.strip() and not l.startswith("#")]
    header = lines[0].split("\t")
    for line in lines[1:]:
        rows.append(dict(zip(header, line.split("\t"))))
    return rows


def numbers(text):
    if text == "?":
        return None
    if re.match(r"^-?[\d.]+-[\d.]+$", text):
        a, b = text.rsplit("-", 1)
        return ("range", float(a), float(b))
    return ("list", [float(x) for x in text.split(",")])


def within(measured, spec, tol):
    """Every measured instance against the spec under tol."""
    if tol == "approx":
        return True
    m = numbers(measured)
    s = numbers(spec)
    if m is None or s is None:
        return False
    values = [m[1], m[2]] if m[0] == "range" else m[1]
    if tol == "exact":
        if s[0] == "range":
            return all(s[1] <= v <= s[2] for v in values)
        return all(v in s[1] for v in values)
    if tol.startswith("+-"):
        n = float(tol[2:])
        if s[0] == "range":
            return all(s[1] - n <= v <= s[2] + n for v in values)
        return all(any(abs(v - sv) <= n for sv in s[1]) for v in values)
    if tol == "range":
        if s[0] == "range":
            return all(s[1] <= v <= s[2] for v in values)
        lo, hi = min(s[1]), max(s[1])
        return all(lo <= v <= hi for v in values)
    return False


def grade(test_id, ledger_path=None):
    tables = spec_tables(test_id)
    if not tables:
        return ["no spec table for %s under docs/minigames/<raid>/encounters/" % test_id]
    path = ledger_path or os.path.join(ROOT, "build", "quest_gate", test_id, "ledger.tsv")
    rows, _ = ledger.read(path)
    if rows is None:
        return ["no ledger at %s" % os.path.relpath(path, ROOT)]
    by_step = {}
    for r in rows:
        by_step.setdefault(r["step"], []).append(r)
    findings = []
    total = 0
    for table in tables:
        for spec in read_spec(table):
            total += 1
            mid = spec["mechanic_id"]
            hits = [r for r in by_step.get("spec." + mid, []) if r["verdict"] == "PASS"]
            if not hits:
                findings.append("unmeasured %s (%s)" % (mid, spec["quantity"]))
                continue
            row = hits[-1]
            m = ROW_RE.match(row["detail"])
            if not m:
                findings.append("malformed %s: %r" % (mid, row["detail"][:120]))
                continue
            if m.group("g") != spec["grade"]:
                findings.append("grade mismatch %s: row says %s, table says %s" % (mid, m.group("g"), spec["grade"]))
            if m.group("t") != spec["tolerance"]:
                findings.append("tolerance mismatch %s: row says %s, table says %s" % (mid, m.group("t"), spec["tolerance"]))
            if m.group("s") != spec["spec_value"]:
                findings.append("spec value mismatch %s: row says %s, table says %s" % (mid, m.group("s"), spec["spec_value"]))
            if spec["tolerance"] == "approx" and not re.search(r"approximation, M\d+", row["detail"]):
                findings.append("grade E row %s does not say 'approximation, Mn'" % mid)
            if not within(m.group("m"), spec["spec_value"], spec["tolerance"]):
                findings.append("out of tolerance %s: measured %s vs spec %s (tol %s)" % (
                    mid, m.group("m"), spec["spec_value"], spec["tolerance"]))
    return findings, total


def main(argv):
    ledger_path = None
    ids = []
    it = iter(argv[1:])
    for a in it:
        if a == "--ledger":
            ledger_path = next(it)
        else:
            ids.append(a)
    if not ids:
        print(__doc__)
        return 2
    bad = 0
    for test_id in ids:
        result = grade(test_id, ledger_path)
        if isinstance(result, list):
            print("%s: %s" % (test_id, "; ".join(result)))
            bad += 1
            continue
        findings, total = result
        if findings:
            bad += 1
            print("%s: %d of %d spec rows measured; %d finding(s)" % (
                test_id, total - sum(1 for f in findings if f.startswith("unmeasured")), total, len(findings)))
            for f in findings:
                print("    " + f)
        else:
            print("%s: FULL (%d spec rows measured within tolerance)" % (test_id, total))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
