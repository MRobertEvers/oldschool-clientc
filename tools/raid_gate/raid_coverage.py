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

SCOPE. A test plays one mode at one party size and cannot measure every row of a
table, so the test writes one PASS row `spec.scope` whose detail starts
`mode=<entry|normal|hard> party=<n>` (or the caller passes --mode/--party). Rows
out of that scope are skipped and listed, never counted: a row whose mechanic_id or
quantity names another mode (hard/hm, entry/story, normal/regular), a row with no mode
marker whose entry_/.entry/_entry sibling exists when the mode is entry, and a row
that names a party size or scale other than the test's (party, scale, _3_5, 2_5, duo,
trio, 4p, 5p, team, per_player) unless it also says solo/_1. Without a scope row
every row counts (mode all).

TEXT ROWS. A spec row whose unit is `text` is graded by equality: the detail reads
`measured <text>[; free text] (spec <text>[ text], grade <G>, tol exact)` -- the
measured text runs to the first `;`, the spec text to `, grade` -- and the two texts
must match after trimming and case-folding.

SIDECAR. `<room>.scope.tsv` beside the table (mechanic_id, scope, note) states a row's
scope outright -- all, entry, normal, hard, party (two or more players), stat (a
distribution no single room settles) -- and overrides the name heuristic.

Verdict per test: FULL (every in-scope spec row measured and within tolerance) or a list of
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
SPEC_RE = re.compile(r"\(spec\s+(?P<s>.+?),\s*grade\s+(?P<g>[A-E]),\s*tol\s+(?P<t>exact|\+-\d+|range|approx)\)")
NUM_RE = re.compile(r"^-?[\d.]+(?:-[\d.]+)?(?:,-?[\d.]+)*$")
SCOPE_RE = re.compile(r"mode=(?P<mode>entry|normal|hard|all)\s+party=(?P<party>\d)")


def parse_row(detail, text_row=False):
    """{m, s, g, t} from a spec row's detail, or None. The measured value is the
    text between `measured ` and the first comma or the `(spec` group; the spec
    group is the LAST one in the detail, so free text may carry parentheses."""
    if not detail.startswith("measured "):
        return None
    hits = list(SPEC_RE.finditer(detail))
    if not hits:
        return None
    spec = hits[-1]
    head = detail[len("measured "):spec.start()].strip()
    if text_row:
        # The measured text runs to the first ';' (free text follows it); the
        # spec text runs to ', grade' and may end with the word 'text'.
        measured = head.split(";")[0].rstrip(", ").strip()
        spec_value = re.sub(r"\s+text$", "", spec.group("s").strip())
    else:
        measured = head.split(",")[0].split(";")[0].strip().split(" ")[0]
        spec_value = spec.group("s").strip().split(" ")[0]
        if not NUM_RE.match(measured) and measured != "?":
            return None
    return {"m": measured, "s": spec_value, "g": spec.group("g"), "t": spec.group("t")}


MODE_WORDS = {"hard": ("hard", "hm"), "entry": ("entry", "story"), "normal": ("normal", "regular")}
PARTY_WORDS = ("party", "scale", "_3_5", "3_5", "2_5", "duo", "trio", "4p", "5p", "team", "per_player", "per player")
SOLO_WORDS = ("solo", "_1", "1p")


SIDECAR_SCOPES = {"all", "entry", "normal", "hard", "party", "stat"}


def read_sidecar(table):
    """docs/minigames/<raid>/encounters/<room>.scope.tsv: mechanic_id <TAB> scope
    [<TAB> note]. scope is all | entry | normal | hard (the mode the row belongs
    to) | party (needs two or more players) | stat (a distribution no single room
    settles). An entry here overrides the name heuristic in row_scope."""
    path = table[:-len(".tsv")] + ".scope.tsv"
    out = {}
    if not os.path.isfile(path):
        return out
    with open(path, encoding="utf-8") as f:
        for line in f:
            if not line.strip() or line.startswith("#") or line.startswith("mechanic_id\t"):
                continue
            cells = line.rstrip("\n").split("\t")
            assert cells[1] in SIDECAR_SCOPES, "%s: bad scope %r for %s" % (path, cells[1], cells[0])
            out[cells[0]] = cells[1]
    return out


def row_scope(spec, mode, party, ids, sidecar=None):
    """None when the row is in scope, else the reason it is skipped."""
    if mode == "all":
        return None
    scope = (sidecar or {}).get(spec["mechanic_id"])
    if scope:
        if scope == "all" or scope == mode:
            return None
        if scope in ("entry", "normal", "hard"):
            return "sidecar: %s" % scope
        if scope == "party":
            return None if party >= 2 else "sidecar: party"
        return "sidecar: stat"
    text = ("%s %s" % (spec["mechanic_id"], spec["quantity"])).lower()
    tokens = re.split(r"[^a-z0-9]+", text)
    if any(w in tokens for w in MODE_WORDS[mode]):
        return None  # a row that names the test's own mode is in scope
    for other, words in MODE_WORDS.items():
        if other != mode and any(w in tokens for w in words):
            return "names %s" % other
    if mode == "entry" and not any(w in tokens for w in MODE_WORDS["entry"]):
        mid = spec["mechanic_id"]
        room, _, name = mid.partition(".")
        siblings = {"%s.entry_%s" % (room, name), "%s.%s.entry" % (room, name), "%s.%s_entry" % (room, name),
                    "%s.entry.%s" % (room, name)}
        if siblings & ids:
            return "has an entry sibling"
    if party == 1 and any(w in text for w in PARTY_WORDS) and not any(w in text for w in SOLO_WORDS):
        return "names a party size"
    return None


def spec_tables(test_id):
    raid, _, room = test_id.partition("_")
    assert raid in RAID_DOCS, "unknown raid prefix in %s" % test_id
    directory = os.path.join(ROOT, "docs", "minigames", RAID_DOCS[raid], "encounters")
    if room in FULL_RAID_ROOMS:
        return sorted(p for p in glob.glob(os.path.join(directory, "*.tsv"))
                      if os.path.basename(p)[:-4] not in FULL_RAID_ROOMS
                      and not p.endswith(".scope.tsv"))
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


def grade(test_id, ledger_path=None, mode=None, party=None):
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
    if mode is None:
        scope_rows = [r for r in by_step.get("spec.scope", []) if r["verdict"] == "PASS"]
        sm = SCOPE_RE.search(scope_rows[-1]["detail"]) if scope_rows else None
        mode = sm.group("mode") if sm else "all"
        party = int(sm.group("party")) if sm else 0
    findings = []
    skipped = []
    total = 0
    for table in tables:
        specs = read_spec(table)
        ids = {sp["mechanic_id"] for sp in specs}
        sidecar = read_sidecar(table)
        for spec in specs:
            mid = spec["mechanic_id"]
            why = row_scope(spec, mode, party or 0, ids, sidecar)
            if why:
                skipped.append("%s (%s)" % (mid, why))
                continue
            total += 1
            hits = [r for r in by_step.get("spec." + mid, []) if r["verdict"] == "PASS"]
            if not hits:
                findings.append("unmeasured %s (%s)" % (mid, spec["quantity"]))
                continue
            row = hits[-1]
            text_row = spec["unit"] == "text"
            m = parse_row(row["detail"], text_row)
            if not m:
                findings.append("malformed %s: %r" % (mid, row["detail"][:120]))
                continue
            if m["g"] != spec["grade"]:
                findings.append("grade mismatch %s: row says %s, table says %s" % (mid, m["g"], spec["grade"]))
            if m["t"] != spec["tolerance"]:
                findings.append("tolerance mismatch %s: row says %s, table says %s" % (mid, m["t"], spec["tolerance"]))
            if text_row:
                if m["s"].strip().lower() != spec["spec_value"].strip().lower():
                    findings.append("spec value mismatch %s: row says %r, table says %r" % (mid, m["s"], spec["spec_value"]))
                if m["m"].strip().lower() != spec["spec_value"].strip().lower():
                    findings.append("text mismatch %s: measured %r vs spec %r" % (mid, m["m"], spec["spec_value"]))
                continue
            if m["s"] != spec["spec_value"]:
                findings.append("spec value mismatch %s: row says %s, table says %s" % (mid, m["s"], spec["spec_value"]))
            if spec["tolerance"] == "approx" and not re.search(r"approximation, M\d+", row["detail"]):
                findings.append("grade E row %s does not say 'approximation, Mn'" % mid)
            if not within(m["m"], spec["spec_value"], spec["tolerance"]):
                findings.append("out of tolerance %s: measured %s vs spec %s (tol %s)" % (
                    mid, m["m"], spec["spec_value"], spec["tolerance"]))
    if skipped:
        print("%s: scope mode=%s party=%s; %d row(s) out of scope skipped: %s" % (
            test_id, mode, party, len(skipped), ", ".join(skipped)))
    return findings, total


def main(argv):
    ledger_path = None
    mode = None
    party = None
    ids = []
    it = iter(argv[1:])
    for a in it:
        if a == "--ledger":
            ledger_path = next(it)
        elif a == "--mode":
            mode = next(it)
        elif a == "--party":
            party = int(next(it))
        else:
            ids.append(a)
    if not ids:
        print(__doc__)
        return 2
    bad = 0
    for test_id in ids:
        result = grade(test_id, ledger_path, mode, party)
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
            print("%s: FULL (%d in-scope spec rows measured within tolerance)" % (test_id, total))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
