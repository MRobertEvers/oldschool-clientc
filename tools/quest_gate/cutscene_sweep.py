#!/usr/bin/env python3
"""Which quests script a camera cutscene, and whether our port kept it.

The guide is the spec, and Quest Helper has no "watch the cutscene" step,
so helper_coverage never asks for one: a port can drop a cutscene and the
test stays green (Fight Arena's ogre-pen camera, 2026-09-30). This sweep
is the check the grader cannot make. For every quest folder it counts the
camera ops (cam_moveto, cam_lookat, cam_shake, cam_reset, cam_coord) in
LostCity_Content2 and in OSRS-Content, and grades the pair:

  MATCH      both script a cutscene, same number of moveto/lookat ops
  PARTIAL    both do, but ours has fewer moveto/lookat ops
  DROPPED    LostCity scripts a cutscene, ours has no camera op at all
  OURS_ONLY  ours scripts one and LostCity has none (a later-era quest
             constructed from the wiki, or a LostCity quest LostCity
             itself left without one)
  NONE       neither does
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

gate.py's cutscene rule reads `quests_with_cutscene(repo)`: a quest whose
OWN scripts carry cam_moveto/cam_lookat must drive at least one cutscene
row. Usage: cutscene_sweep.py [--repo .] [--lostcity PATH] [--tsv OUT]
[--only-open] -- prints the table, exit 0.
"""
import argparse
import csv
import os
import re
import sys

CAM_OP = re.compile(r"\bcam_(moveto|lookat|shake|reset|coord)\b")
FRAMING_OPS = ("moveto", "lookat")
DEFAULT_LOSTCITY = os.path.expanduser("~/Documents/git_repos/LostCity_Content2")


def count_cam_ops(quest_dir):
    """{op: count} over every .rs2 under quest_dir, plus {'files': {path: n}}."""
    assert quest_dir
    counts = {}
    files = {}
    for root, _dirs, names in os.walk(quest_dir):
        for name in names:
            if not name.endswith(".rs2"):
                continue
            path = os.path.join(root, name)
            with open(path, encoding="utf-8", errors="replace") as fh:
                # A commented-out op is not a cutscene (LostCity's winelda.rs2
                # keeps two behind //).
                text = "\n".join(line.split("//", 1)[0] for line in fh)
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


def sweep(repo, lostcity):
    rows = []
    ledger = read_cutscene_ledger(repo)
    for q in read_queue(repo):
        our_dir = os.path.join(repo, "OSRS-Content/osrs239-content/server/scripts/quests", q["quest_dir"])
        lc_dir = os.path.join(lostcity, "scripts/quests", q["quest_dir"])
        ours, our_files = count_cam_ops(our_dir) if os.path.isdir(our_dir) else ({}, {})
        in_lc = os.path.isdir(lc_dir)
        lc, lc_files = count_cam_ops(lc_dir) if in_lc else ({}, {})
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
        rows.append({
            "quest_dir": q["quest_dir"], "test_id": q["test_id"], "tier": q["tier"],
            "status": q["status"], "in_lostcity": "yes" if in_lc else "no",
            "lostcity_framing_ops": f_lc, "ours_framing_ops": f_ours,
            "lostcity_files": ";".join(f"{k}:{v}" for k, v in sorted(lc_files.items())),
            "ours_files": ";".join(f"{k}:{v}" for k, v in sorted(our_files.items())),
            "verdict": verdict,
        })
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo", default=".")
    ap.add_argument("--lostcity", default=DEFAULT_LOSTCITY)
    ap.add_argument("--tsv", help="write the full table here")
    ap.add_argument("--only-open", action="store_true", help="print only DROPPED/PARTIAL/WIKI_MISSING rows")
    args = ap.parse_args()
    assert os.path.isdir(os.path.join(args.lostcity, "scripts/quests")), args.lostcity
    rows = sweep(args.repo, args.lostcity)
    cols = ["quest_dir", "test_id", "tier", "status", "in_lostcity", "lostcity_framing_ops",
            "ours_framing_ops", "lostcity_files", "ours_files", "verdict"]
    if args.tsv:
        with open(args.tsv, "w", encoding="utf-8", newline="") as fh:
            w = csv.DictWriter(fh, fieldnames=cols, delimiter="\t")
            w.writeheader()
            w.writerows(rows)
    shown = [r for r in rows if not args.only_open or r["verdict"] in ("DROPPED", "PARTIAL", "WIKI_MISSING")]
    for r in shown:
        print("%-28s %-18s t%s %-8s %-9s lc=%-3s ours=%-3s %s" % (
            r["quest_dir"], r["test_id"], r["tier"], r["status"], r["verdict"],
            r["lostcity_framing_ops"], r["ours_framing_ops"], r["lostcity_files"] or r["ours_files"]))
    tally = {}
    for r in rows:
        tally[r["verdict"]] = tally.get(r["verdict"], 0) + 1
    print("verdicts:", " ".join(f"{k}={v}" for k, v in sorted(tally.items())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
