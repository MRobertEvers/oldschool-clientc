#!/usr/bin/env python3
"""Build the screenshot sheets for one quest batch: per quest, thumbnail
sheets (96 shots each) and full-size sheets (24 shots each) as WebP, plus a
JSON index render_page.py turns into the page. WebP and chunking keep every
file near 1 MB however long the quest (a 300-shot quest is 13 full sheets).

Usage: build_sheet.py <repo> <out_dir> <test_id> [<test_id> ...] [--quality N]

--quality N sets the FULL-sheet WebP quality (default 70, same as before this
flag existed). Thumb-sheet quality is unchanged. Lower it when a batch's
sheets don't fit the artifact size limit (64 MB/version): each full sheet is
already the bulk of a batch's bytes.

Run book (docs/QUEST_SUITE_KIT.md, phase 5): after every batch,
  .venv/bin/python tools/quest_gate/batch_sheet/build_sheet.py . build/batch_sheet/<batch> <ids...>
  .venv/bin/python tools/quest_gate/batch_sheet/render_page.py build/batch_sheet/<batch> <batch> "Quest Batch <batch>"
then publish build/batch_sheet/<batch>/index.html with every *.webp there as
supporting files, and record the link in test/quests/BATCHES.tsv.

Reads build/quest_gate/<id>/ledger.tsv and shots/*.png (the reviewer's last
run of each quest) and test/quests/QUEUE.tsv for the row's status/owner/note.
"""
import argparse, csv, json, os, sys
from PIL import Image

THUMB_W, THUMB_H, COLS = 380, 250, 4
FULL_W, FULL_H = 765, 503
FULL_PER_SHEET, THUMB_PER_SHEET = 24, 96   # 6 rows / 24 rows of 4: a 300-shot quest is 13 full sheets of ~1.3 MB
Q_FULL, Q_THUMB = 70, 70


def read_ledger(path):
    rows, summary = [], None
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if not line or line.startswith("quest-ledger") or line.startswith("index\t"):
                continue
            cols = line.split("\t")
            if cols[0] == "SUMMARY":
                summary = cols
                continue
            while len(cols) < 6:
                cols.append("")
            rows.append({"index": cols[0], "step": cols[1], "verdict": cols[2],
                         "ticks": cols[3], "shots": [s for s in cols[4].split(",") if s],
                         "detail": cols[5]})
    return rows, summary


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("repo")
    parser.add_argument("out_dir")
    parser.add_argument("ids", nargs="+")
    parser.add_argument("--quality", type=int, default=Q_FULL,
                         help="FULL-sheet WebP quality (default %d)" % Q_FULL)
    args = parser.parse_args()
    repo, out_dir, ids, quality_full = args.repo, args.out_dir, args.ids, args.quality
    os.makedirs(out_dir, exist_ok=True)
    queue = {}
    with open(os.path.join(repo, "test/quests/QUEUE.tsv"), encoding="utf-8") as fh:
        for r in csv.DictReader(fh, delimiter="\t"):
            queue[r["test_id"]] = r
    index = {"quests": []}
    for tid in ids:
        base = os.path.join(repo, "build/quest_gate", tid)
        ledger = os.path.join(base, "ledger.tsv")
        shots_dir = os.path.join(base, "shots")
        if not os.path.isfile(ledger):
            index["quests"].append({"id": tid, "missing": True, "queue": queue.get(tid, {})})
            continue
        rows, summary = read_ledger(ledger)
        shot_files = sorted(f for f in os.listdir(shots_dir) if f.endswith(".png")) if os.path.isdir(shots_dir) else []
        owner_of = {}
        for r in rows:
            for s in r["shots"]:
                owner_of[s] = r
        n = len(shot_files)
        def sheets(per, w, h, suffix, quality):
            out = []
            for s0 in range(0, max(n, 1), per):
                chunk = shot_files[s0:s0 + per]
                rows_n = (len(chunk) + COLS - 1) // COLS or 1
                sheet = Image.new("RGB", (w * COLS, h * rows_n), (18, 16, 14))
                for j, f in enumerate(chunk):
                    im = Image.open(os.path.join(shots_dir, f)).convert("RGB")
                    x, y = j % COLS, j // COLS
                    sheet.paste(im.resize((w, h), Image.LANCZOS), (x * w, y * h))
                name = f"{tid}-{suffix}-{s0 // per}.webp"
                sheet.save(os.path.join(out_dir, name), "WEBP", quality=quality, method=4)
                out.append({"name": name, "rows": rows_n, "count": len(chunk)})
            return out
        full_sheets = sheets(FULL_PER_SHEET, FULL_W, FULL_H, "full", quality_full)
        thumb_sheets = sheets(THUMB_PER_SHEET, THUMB_W, THUMB_H, "thumb", Q_THUMB)
        shots = []
        for i, f in enumerate(shot_files):
            stem = f[:-4]
            r = owner_of.get(stem)
            fs, fi = i // FULL_PER_SHEET, i % FULL_PER_SHEET
            ts, ti = i // THUMB_PER_SHEET, i % THUMB_PER_SHEET
            shots.append({"file": stem,
                          "full": {"sheet": fs, "col": fi % COLS, "row": fi // COLS, "rows": full_sheets[fs]["rows"]},
                          "thumb": {"sheet": ts, "col": ti % COLS, "row": ti // COLS, "rows": thumb_sheets[ts]["rows"]},
                          "step": r["step"] if r else "", "verdict": r["verdict"] if r else "",
                          "detail": (r["detail"] if r else "")[:220]})
        q = queue.get(tid, {})
        passes = sum(1 for r in rows if r["verdict"] == "PASS")
        fails = sum(1 for r in rows if r["verdict"] == "FAIL")
        blocked = sum(1 for r in rows if r["verdict"] == "BLOCKED")
        index["quests"].append({
            "id": tid, "quest_dir": q.get("quest_dir", ""), "status": q.get("status", ""),
            "owner": q.get("owner", ""), "note": (q.get("last_failure", "") or "")[:600],
            "rows": len(rows), "pass": passes, "fail": fails, "blocked": blocked,
            "ticks": summary[3] if summary and len(summary) > 3 else "",
            "shots": shots, "full_sheets": full_sheets, "thumb_sheets": thumb_sheets,
            "steps": [{"index": r["index"], "step": r["step"], "verdict": r["verdict"],
                       "ticks": r["ticks"], "shots": r["shots"], "detail": r["detail"][:220]} for r in rows],
        })
    with open(os.path.join(out_dir, "index.json"), "w", encoding="utf-8") as fh:
        json.dump(index, fh)
    total = sum(os.path.getsize(os.path.join(out_dir, f)) for f in os.listdir(out_dir))
    print("built", len(index["quests"]), "quests;", sum(len(q.get("shots", [])) for q in index["quests"]), "shots;",
          "%.1f MB" % (total / 1e6))


if __name__ == "__main__":
    main()
