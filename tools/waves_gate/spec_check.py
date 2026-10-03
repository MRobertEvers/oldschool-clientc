#!/usr/bin/env python3
"""spec_check -- validate a wave minigame's encounter spec table
(docs/WAVES_ORCHESTRATOR.md section 6; copied from the raid loop's
tools/raid_gate/spec_check.py at 94f55b306, docs/minigames/waves_loop/FORKED_FROM.md).

    python3 tools/waves_gate/spec_check.py docs/minigames/inferno/encounters/nibblers.tsv ...
    python3 tools/waves_gate/spec_check.py --all   # every encounters/*.tsv under
                                                   # docs/minigames/inferno/ and colosseum/

A spec table is a TSV with exactly these columns:

    mechanic_id  quantity  spec_value  unit  tags  grade  source_ref  closes  tolerance

* mechanic_id: `<unit>.<name>`, unique in the file, [a-z0-9_.]+
* spec_value: a number, a range `a-b`, a list `a,b,c`, or `?` (grade E only)
* unit: ticks | cycles | hp | tiles | count | percent | permille | ratio | text
* tags: one or more `[tag]` provenance tags ([jagex] [cache] [blert] [plugin] [wiki]
  [guide] [video] [Mn], docs/WAVES_ORCHESTRATOR.md section 6; any [word] is accepted)
* grade: A-E per section 2; E requires an [Mn] tag; A requires [jagex] or [cache];
  B requires [blert]; D or better forbids `?`
* source_ref: `<file>[:<line or anchor>][; ...]` where each <file> exists under
  docs/ (relative to the repo root or to the game's docs directory,
  docs/minigames/<game>/, or its sources/) -- a quote
  that cannot be opened is not a source
* closes: an Mn id, or `-`
* tolerance: `exact`, `+-N`, `range`, or `approx` (grade E only)

Exit 1 with one line per finding; exit 0 and `ok <file>: <rows> rows` otherwise.
"""
import glob
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
COLUMNS = ["mechanic_id", "quantity", "spec_value", "unit", "tags", "grade", "source_ref", "closes", "tolerance"]
UNITS = {"ticks", "cycles", "hp", "tiles", "count", "percent", "permille", "ratio", "text"}
TAG_RE = re.compile(r"^(\[[A-Za-z0-9]+\])+$")
ID_RE = re.compile(r"^[a-z0-9_]+\.[a-z0-9_.]+$")
VALUE_RE = re.compile(r"^(\?|-?\d+(\.\d+)?(-\d+(\.\d+)?)?|(-?\d+(\.\d+)?)(,-?\d+(\.\d+)?)+)$")
TOL_RE = re.compile(r"^(exact|\+-\d+|range|approx)$")
# docs/minigames/<game>/ for every wave minigame the loop owns (section 1).
WAVE_GAMES = ("inferno", "colosseum")


def check(path):
    findings = []
    with open(path, encoding="utf-8") as f:
        lines = [l.rstrip("\n") for l in f if l.strip() and not l.startswith("#")]
    if not lines:
        return ["%s: empty" % path]
    header = lines[0].split("\t")
    if header != COLUMNS:
        return ["%s: header is %r, expected %r" % (path, header, COLUMNS)]
    seen = set()
    game_docs = os.path.dirname(os.path.dirname(os.path.abspath(path)))
    for n, line in enumerate(lines[1:], start=2):
        cells = line.split("\t")
        if len(cells) != len(COLUMNS):
            findings.append("%s:%d: %d cells, expected %d" % (path, n, len(cells), len(COLUMNS)))
            continue
        row = dict(zip(COLUMNS, cells))
        mid = row["mechanic_id"]
        if not ID_RE.match(mid):
            findings.append("%s:%d: bad mechanic_id %r" % (path, n, mid))
        if mid in seen:
            findings.append("%s:%d: duplicate mechanic_id %r" % (path, n, mid))
        seen.add(mid)
        if not row["quantity"].strip():
            findings.append("%s:%d: empty quantity" % (path, n))
        if row["unit"] != "text" and not VALUE_RE.match(row["spec_value"]):
            findings.append("%s:%d: bad spec_value %r" % (path, n, row["spec_value"]))
        if row["unit"] not in UNITS:
            findings.append("%s:%d: bad unit %r" % (path, n, row["unit"]))
        if not TAG_RE.match(row["tags"]):
            findings.append("%s:%d: tags must be [tag][tag]..., got %r" % (path, n, row["tags"]))
        grade = row["grade"]
        if grade not in "ABCDE" or len(grade) != 1:
            findings.append("%s:%d: bad grade %r" % (path, n, grade))
        tags = row["tags"]
        if grade == "E" and not re.search(r"\[M\d+\]", tags):
            findings.append("%s:%d: grade E needs an [Mn] tag" % (path, n))
        if grade == "A" and "[jagex]" not in tags and "[cache]" not in tags:
            findings.append("%s:%d: grade A needs [jagex] or [cache]" % (path, n))
        if grade == "B" and "[blert]" not in tags:
            findings.append("%s:%d: grade B needs [blert]" % (path, n))
        if grade != "E" and row["spec_value"] == "?":
            findings.append("%s:%d: only grade E may have spec_value ?" % (path, n))
        if grade != "E" and row["tolerance"] == "approx":
            findings.append("%s:%d: tolerance approx is grade E only" % (path, n))
        if not TOL_RE.match(row["tolerance"]):
            findings.append("%s:%d: bad tolerance %r" % (path, n, row["tolerance"]))
        if not re.match(r"^(M\d+|-)$", row["closes"]):
            findings.append("%s:%d: closes must be Mn or -, got %r" % (path, n, row["closes"]))
        for ref in row["source_ref"].split(";"):
            ref = ref.strip()
            if not ref:
                findings.append("%s:%d: empty source_ref" % (path, n))
                continue
            file_part = ref.split(":")[0].strip()
            candidates = [os.path.join(ROOT, file_part), os.path.join(game_docs, file_part),
                          os.path.join(game_docs, "sources", file_part), os.path.join(ROOT, "docs", file_part)]
            if not any(os.path.exists(c) for c in candidates):
                findings.append("%s:%d: source_ref %r names no file under docs/" % (path, n, ref))
    if not findings:
        print("ok %s: %d rows" % (os.path.relpath(path, ROOT), len(lines) - 1))
    return findings


def main(argv):
    paths = argv[1:]
    if paths == ["--all"] or not paths:
        # <unit>.scope.tsv is waves_coverage's scope sidecar, not a spec table.
        # The two wave minigames only: docs/minigames/ also holds the raid
        # loop's documents once both branches reach v3.
        paths = sorted(p for game in WAVE_GAMES
                       for p in glob.glob(os.path.join(ROOT, "docs", "minigames", game, "encounters", "*.tsv"))
                       if not p.endswith(".scope.tsv"))
    findings = []
    for p in paths:
        findings += check(p)
    for f in findings:
        print(f)
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
