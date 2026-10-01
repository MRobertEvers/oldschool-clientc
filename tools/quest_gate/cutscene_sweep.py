#!/usr/bin/env python3
"""Which quests script a camera cutscene, and whether our port kept it.

The guide is the spec, and Quest Helper has no "watch the cutscene" step,
so helper_coverage never asks for one: a port can drop a cutscene and the
test stays green (Fight Arena's ogre-pen camera, 2026-09-30). This sweep
is the check the grader cannot make. For every quest folder it counts the
camera ops (cam_moveto, cam_lookat, cam_shake, cam_reset, cam_coord) in
LostCity and in OSRS-Content, and grades the pair.

LostCity is TWO trees: LostCity_Content2/scripts/quests/<quest_dir> and
LostCity_Server/content/scripts/quests/<quest_dir>. A quest in either is a
LostCity quest (the parity rule in content_parity.workflow.js says the same).
Ten quests live only in LostCity_Server (eadgar, horror, misc, mm, mortton,
regicide, routequest, tbwt, troll_love, viking); reading Content2 alone graded
them WIKI_* and hid a dropped LostCity cutscene (seam34). Where a quest is in
both, Content2 is read (it is the newer tree; the framing counts of all 59
shared quests agree, 2026-10-01); the `lostcity_tree` column names which.
The grading counts neither tree's `[debugproc,...]` blocks: a developer's
camera test is not a cutscene (script_lines).

  MATCH      both script a cutscene, same number of moveto/lookat ops
  PARTIAL    both do, but ours has fewer moveto/lookat ops
  DROPPED    LostCity scripts a cutscene, ours has no camera op at all
  OURS_ONLY  ours scripts one and LostCity has none (a later-era quest
             constructed from the wiki, or a LostCity quest LostCity
             itself left without one)
  NONE       neither does (a LostCity quest whose CUTSCENES.tsv row says
             the wiki describes one reads WIKI_MISSING instead)
  NO_SOURCE  the quest is not in LostCity and ours has no camera op --
             the OSRS wiki decides whether it should; a parity worker
             records that decision in docs/quests/CUTSCENES.tsv, and a
             recorded row turns this into
  WIKI_NONE      the wiki describes no cutscene (cited)
  WIKI_PORTED    the wiki describes one and ours scripts it (cited)
  WIKI_MISSING   the wiki describes one and ours does not -- open

docs/quests/CUTSCENES.tsv columns: test_id, source (a wiki or Quest
Helper URL or file:line), has_cutscene (yes/no), where (the step or
dialogue that starts it), ported (yes/no), note. One row per quest.

gate.py's cutscene rule (cutscene_row_required) reads
`quests_with_cutscene(repo)`: a quest whose OWN scripts carry
cam_moveto/cam_lookat must drive at least one PASS `cutscene:` row, and
`cutscene_sites(repo)` -- one entry per cam_moveto/cam_lookat call, a SITE
being one script file:line -- is what the union of its keyframes must cover.

Usage: cutscene_sweep.py [--repo .] [--lostcity PATH] [--lostcity-server PATH]
[--tsv OUT] [--only-open] [--sites] [--fail-on-dropped] -- prints the table
(--sites adds, under every DROPPED/PARTIAL row, each LostCity framing call
file:line with no same-op same-coord call in ours: the next content pass's
work list); exit 0, or with
--fail-on-dropped exit 1 when any quest is DROPPED or PARTIAL (`make -C src
check-quest-cutscenes`). WIKI_MISSING never fails it: that is the backlog of
post-LostCity quests being specced from the wiki in a separate session
(docs/quests/cutscenes/), not a port that lost something it had.
"""
import argparse
import csv
import os
import re
import sys

CAM_OP = re.compile(r"\bcam_(moveto|lookat|shake|reset|coord)\b")
# A framing call and its first argument (the coord): `cam_moveto(0_38_154_44_13, ...`
# or `cam_lookat(movecoord(coord, -1, 0, -1), ...` -- the argument runs to the
# first comma at paren depth 0 (framing_first_arg).
FRAMING_CALL = re.compile(r"\bcam_(moveto|lookat)\s*\(")
COORD_LITERAL = re.compile(r"^\s*(\d+)_(\d+)_(\d+)_(\d+)_(\d+)\s*$")
FRAMING_OPS = ("moveto", "lookat")
DEFAULT_LOSTCITY = os.path.expanduser("~/Documents/git_repos/LostCity_Content2")
DEFAULT_LOSTCITY_SERVER = os.path.expanduser("~/Documents/git_repos/LostCity_Server")


def lostcity_quest_roots(lostcity, lostcity_server):
    """[(tree label, quests dir)] in preference order: Content2 first."""
    assert lostcity
    assert lostcity_server
    return [("content2", os.path.join(lostcity, "scripts/quests")),
            ("server", os.path.join(lostcity_server, "content/scripts/quests"))]


def lostcity_quest_dir(roots, quest_dir):
    """(tree label, path) of the first LostCity tree holding quest_dir, or
    (None, None) when neither does -- the quest is not a LostCity quest."""
    assert roots
    assert quest_dir
    for label, root in roots:
        path = os.path.join(root, quest_dir)
        if os.path.isdir(path):
            return label, path
    return None, None


def script_lines(path, skip_debugproc=False):
    """One .rs2's lines, line numbers kept. A `//` comment is cut: a
    commented-out op is not a cutscene (LostCity's winelda.rs2 keeps two
    behind //). With skip_debugproc, every line of a `[debugproc,...]` block
    (its header to the next `[` trigger header) is blanked: a developer's
    camera test is not a cutscene a player sees (LostCity's
    debug_routequest.rs2 `::cutcam`/`::camtest`/`::camlook` frame the camera
    six times and read as a PARTIAL routequest port, seam34)."""
    assert path
    with open(path, encoding="utf-8", errors="replace") as fh:
        lines = [line.split("//", 1)[0] for line in fh]
    if skip_debugproc:
        inside = False
        for i, line in enumerate(lines):
            if line.startswith("["):
                inside = line.startswith("[debugproc,")
            if inside:
                lines[i] = ""
    return lines


def count_cam_ops(quest_dir, skip_debugproc=False):
    """{op: count} over every .rs2 under quest_dir, plus {'files': {path: n}}.
    skip_debugproc: see script_lines (the sweep's grading sets it; gate.py's
    quests_with_cutscene does not)."""
    assert quest_dir
    counts = {}
    files = {}
    for root, _dirs, names in os.walk(quest_dir):
        for name in names:
            if not name.endswith(".rs2"):
                continue
            path = os.path.join(root, name)
            text = "\n".join(script_lines(path, skip_debugproc))
            n_file = 0
            for m in CAM_OP.finditer(text):
                counts[m.group(1)] = counts.get(m.group(1), 0) + 1
                n_file += 1
            if n_file:
                files[os.path.relpath(path, quest_dir)] = n_file
    return counts, files


def framing(counts):
    return sum(counts.get(op, 0) for op in FRAMING_OPS)


def quests_with_cutscene(repo):
    """test_id -> {file: n} for every QUEUE quest whose OWN scripts frame a
    camera (cam_moveto/cam_lookat). This is what gate.py's rule reads."""
    assert repo
    out = {}
    for row in read_queue(repo):
        our_dir = os.path.join(repo, "OSRS-Content/osrs239-content/server/scripts/quests", row["quest_dir"])
        if not os.path.isdir(our_dir):
            continue
        counts, files = count_cam_ops(our_dir)
        if framing(counts):
            out[row["test_id"]] = files
    return out


def framing_first_arg(text, start):
    """The first argument of the call whose '(' ends at `start`."""
    depth = 0
    for i in range(start, len(text)):
        ch = text[i]
        if ch == "(":
            depth += 1
        elif ch == ")":
            if depth == 0:
                return text[start:i].strip()
            depth -= 1
        elif ch == "," and depth == 0:
            return text[start:i].strip()
    return text[start:].strip()


def coord_tile(literal):
    """`level_mx_mz_lx_lz` -> (x, z, level) world tile, or None for an
    expression (`coord`, `movecoord(...)`, `$c`)."""
    m = COORD_LITERAL.match(literal or "")
    if not m:
        return None
    level, mx, mz, lx, lz = (int(g) for g in m.groups())
    return (mx * 64 + lx, mz * 64 + lz, level)


def framing_sites(quest_dir, skip_debugproc=False):
    """[{file, line, op, arg, tile}] for every cam_moveto/cam_lookat call
    under quest_dir (comments stripped like count_cam_ops). `file` is
    relative to quest_dir; `tile` is (x, z, level) when the coord is a
    literal, else None -- the gate then accepts any keyframe of that op.
    skip_debugproc: see script_lines (cutscene_sites does not set it)."""
    assert quest_dir
    sites = []
    for root, _dirs, names in os.walk(quest_dir):
        for name in sorted(names):
            if not name.endswith(".rs2"):
                continue
            path = os.path.join(root, name)
            lines = script_lines(path, skip_debugproc)
            for number, line in enumerate(lines, 1):
                for m in FRAMING_CALL.finditer(line):
                    arg = framing_first_arg(line, m.end())
                    sites.append({
                        "file": os.path.relpath(path, quest_dir), "line": number,
                        "op": m.group(1), "arg": arg, "tile": coord_tile(arg),
                    })
    sites.sort(key=lambda s: (s["file"], s["line"]))
    return sites


def cutscene_sites(repo):
    """test_id -> framing_sites(...) for every quest quests_with_cutscene
    names, with `file` spelled from the repo root so a gate message is a
    path a reader can open."""
    assert repo
    out = {}
    queue = {row["test_id"]: row for row in read_queue(repo)}
    for test_id in quests_with_cutscene(repo):
        rel = os.path.join("OSRS-Content/osrs239-content/server/scripts/quests", queue[test_id]["quest_dir"])
        sites = framing_sites(os.path.join(repo, rel))
        for site in sites:
            site["file"] = os.path.join(rel, site["file"])
        out[test_id] = sites
    return out


def unmatched_lostcity_sites(lc_dir, our_dir):
    """LostCity framing sites with no counterpart in ours, as framing_sites
    entries (file relative to lc_dir). A counterpart is a call of the same op
    on the same tile (a coord literal) or, for an expression argument, the
    same argument text with whitespace removed; each of ours answers one
    LostCity site. This names the sites a DROPPED/PARTIAL port lost, for the
    next content pass -- a renamed variable reads as lost, so check by eye."""
    assert lc_dir
    assert our_dir
    def key(site):
        if site["tile"] is not None:
            return (site["op"], site["tile"])
        return (site["op"], re.sub(r"\s+", "", site["arg"]))
    pool = {}
    if os.path.isdir(our_dir):
        for site in framing_sites(our_dir, skip_debugproc=True):
            pool[key(site)] = pool.get(key(site), 0) + 1
    lost = []
    for site in framing_sites(lc_dir, skip_debugproc=True):
        if pool.get(key(site), 0) > 0:
            pool[key(site)] -= 1
        else:
            lost.append(site)
    return lost


def read_cutscene_ledger(repo):
    """test_id -> row of docs/quests/CUTSCENES.tsv (the wiki decisions)."""
    assert repo
    path = os.path.join(repo, "docs/quests/CUTSCENES.tsv")
    if not os.path.isfile(path):
        return {}
    with open(path, encoding="utf-8") as fh:
        rows = list(csv.DictReader(fh, delimiter="\t"))
    out = {}
    for r in rows:
        assert r["has_cutscene"] in ("yes", "no"), r
        assert r["ported"] in ("yes", "no", ""), r
        assert r["source"], r
        out[r["test_id"]] = r
    return out


def read_queue(repo):
    with open(os.path.join(repo, "test/quests/QUEUE.tsv"), encoding="utf-8") as fh:
        return list(csv.DictReader(fh, delimiter="\t"))


def sweep(repo, lostcity, lostcity_server):
    rows = []
    ledger = read_cutscene_ledger(repo)
    roots = lostcity_quest_roots(lostcity, lostcity_server)
    for q in read_queue(repo):
        our_dir = os.path.join(repo, "OSRS-Content/osrs239-content/server/scripts/quests", q["quest_dir"])
        lc_tree, lc_dir = lostcity_quest_dir(roots, q["quest_dir"])
        # Graded with debugprocs blanked on both sides (script_lines).
        ours, our_files = count_cam_ops(our_dir, skip_debugproc=True) if os.path.isdir(our_dir) else ({}, {})
        in_lc = lc_dir is not None
        lc, lc_files = count_cam_ops(lc_dir, skip_debugproc=True) if in_lc else ({}, {})
        f_ours, f_lc = framing(ours), framing(lc)
        if f_lc and not f_ours:
            verdict = "DROPPED"
        elif f_lc and f_ours < f_lc:
            verdict = "PARTIAL"
        elif f_lc and f_ours:
            verdict = "MATCH"
        elif f_ours:
            verdict = "OURS_ONLY"
        elif in_lc:
            verdict = "NONE"
        elif q["test_id"] in ledger:
            decided = ledger[q["test_id"]]
            if decided["has_cutscene"] == "no":
                verdict = "WIKI_NONE"
            elif f_ours:
                verdict = "WIKI_PORTED"
            else:
                verdict = "WIKI_MISSING"
        else:
            verdict = "NO_SOURCE"
        if verdict == "OURS_ONLY" and q["test_id"] in ledger and ledger[q["test_id"]]["has_cutscene"] == "yes":
            verdict = "WIKI_PORTED"
        if verdict == "NONE" and q["test_id"] in ledger and ledger[q["test_id"]]["has_cutscene"] == "yes":
            # LostCity frames no camera but the wiki describes a cutscene
            # (Regicide's catapult: LostCity_Server scripts the catapult
            # with no cam op). Not DROPPED -- LostCity alone decides those --
            # but it stays on the wiki backlog instead of reading NONE.
            verdict = "WIKI_MISSING"
        rows.append({
            "quest_dir": q["quest_dir"], "test_id": q["test_id"], "tier": q["tier"],
            "status": q["status"], "in_lostcity": "yes" if in_lc else "no",
            "lostcity_tree": lc_tree or "", "lostcity_dir": lc_dir or "", "our_dir": our_dir,
            "lostcity_framing_ops": f_lc, "ours_framing_ops": f_ours,
            "lostcity_files": ";".join(f"{k}:{v}" for k, v in sorted(lc_files.items())),
            "ours_files": ";".join(f"{k}:{v}" for k, v in sorted(our_files.items())),
            "verdict": verdict,
        })
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo", default=".")
    ap.add_argument("--lostcity", default=DEFAULT_LOSTCITY, help="LostCity_Content2 (scripts/quests/...)")
    ap.add_argument("--lostcity-server", default=DEFAULT_LOSTCITY_SERVER,
                    help="LostCity_Server (content/scripts/quests/...)")
    ap.add_argument("--tsv", help="write the full table here")
    ap.add_argument("--only-open", action="store_true", help="print only DROPPED/PARTIAL/WIKI_MISSING rows")
    ap.add_argument("--sites", action="store_true",
                    help="under each DROPPED/PARTIAL row, list the LostCity framing calls ours lacks")
    ap.add_argument("--fail-on-dropped", action="store_true",
                    help="exit 1 when any quest is DROPPED or PARTIAL (WIKI_MISSING never fails)")
    args = ap.parse_args()
    for _label, root in lostcity_quest_roots(args.lostcity, args.lostcity_server):
        # A missing tree would grade its quests WIKI_* silently -- the very
        # hole this sweep exists to close.
        assert os.path.isdir(root), root
    rows = sweep(args.repo, args.lostcity, args.lostcity_server)
    cols = ["quest_dir", "test_id", "tier", "status", "in_lostcity", "lostcity_tree", "lostcity_framing_ops",
            "ours_framing_ops", "lostcity_files", "ours_files", "verdict"]
    if args.tsv:
        with open(args.tsv, "w", encoding="utf-8", newline="") as fh:
            w = csv.DictWriter(fh, fieldnames=cols, delimiter="\t", extrasaction="ignore")
            w.writeheader()
            w.writerows(rows)
    shown = [r for r in rows if not args.only_open or r["verdict"] in ("DROPPED", "PARTIAL", "WIKI_MISSING")]
    for r in shown:
        print("%-28s %-18s t%s %-8s %-9s %-8s lc=%-3s ours=%-3s %s" % (
            r["quest_dir"], r["test_id"], r["tier"], r["status"], r["verdict"], r["lostcity_tree"] or "-",
            r["lostcity_framing_ops"], r["ours_framing_ops"], r["lostcity_files"] or r["ours_files"]))
        if args.sites and r["verdict"] in ("DROPPED", "PARTIAL"):
            for site in unmatched_lostcity_sites(r["lostcity_dir"], r["our_dir"]):
                print("    lost: %s:%d cam_%s(%s)" % (
                    os.path.join(r["lostcity_dir"], site["file"]), site["line"], site["op"], site["arg"]))
    tally = {}
    for r in rows:
        tally[r["verdict"]] = tally.get(r["verdict"], 0) + 1
    print("verdicts:", " ".join(f"{k}={v}" for k, v in sorted(tally.items())))
    if args.fail_on_dropped:
        lost = [r for r in rows if r["verdict"] in ("DROPPED", "PARTIAL")]
        for r in lost:
            print("cutscene_sweep: %s %s -- LostCity (%s) frames the camera %s time(s) (%s), the port %s"
                  % (r["verdict"], r["test_id"], r["lostcity_tree"], r["lostcity_framing_ops"],
                     r["lostcity_files"], r["ours_framing_ops"]), file=sys.stderr)
        if lost:
            print("cutscene_sweep: FAIL -- %d quest(s) dropped a LostCity cutscene" % len(lost), file=sys.stderr)
            return 1
        print("cutscene_sweep: PASS -- no quest dropped a LostCity cutscene "
              "(WIKI_MISSING is the wiki backlog, not counted)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
