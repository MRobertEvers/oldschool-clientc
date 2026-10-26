#!/usr/bin/env python3
"""Export RSProx recordings for the protocol diff (src/net/test/proto_diff.c).

    tools/proto_diff_corpus.py --index ~/Documents/git_repos/rsprox-index \\
        --storage ~/rsprox-index-data --blobs /Volumes/MatthewLLM_Shared/rsprox_index_blobs \\
        --out build/proto_diff_corpus [--per-revision 40] [--revisions 239 241]

Writes <revision>_<session>.bin, newest sessions first, skipping a blob the
store cannot read. Then:

    make -C src test-proto-diff CORPUS="$(ls build/proto_diff_corpus/*.bin)"

The recordings are Jagex's traffic and are not committed; this is how the
corpus the ledger (src/net/test/proto_diff_expected.txt) was measured on is
rebuilt.
"""
import argparse
import os
import sqlite3
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--index", required=True, help="an rsprox-index checkout (for its Store)")
    ap.add_argument("--storage", required=True)
    ap.add_argument("--blobs")
    ap.add_argument("--out", required=True)
    ap.add_argument("--per-revision", type=int, default=40)
    ap.add_argument("--revisions", type=int, nargs="+", default=[239, 241])
    args = ap.parse_args()

    sys.path.insert(0, os.path.expanduser(args.index))
    from rsprox_index.store import Store  # noqa: E402

    store = Store(os.path.expanduser(args.storage), args.blobs)
    db = sqlite3.connect(os.path.join(os.path.expanduser(args.storage), "index.sqlite3"))
    db.row_factory = sqlite3.Row
    os.makedirs(args.out, exist_ok=True)
    written = 0
    for revision in args.revisions:
        rows = db.execute("select id, sha256 from sessions where revision = ? order by id desc limit ?",
                          (revision, args.per_revision))
        for row in rows:
            try:
                data = store.raw_bytes(row["sha256"])
            except OSError as e:
                print("skip %d: %s" % (row["id"], e))
                continue
            with open(os.path.join(args.out, "%d_%d.bin" % (revision, row["id"])), "wb") as f:
                f.write(data)
            written += 1
    print("%d recordings in %s" % (written, args.out))


if __name__ == "__main__":
    main()
