#!/usr/bin/env python3
"""Complete config records that must state every key and do not.

Every rank-0 record and every NEW record (one no rank-0 block and no imported
lane defines) states every key of its type, client and server, `key=default`
when it does not set the field (3rd/rscache/tools/cachepack/cp_text.h). This
asks cachepack which keys are missing -- `cachepack missing` merges the tree
exactly as `pack` does -- and appends `<key>=default` to the record's block in
the first file that declares it, leaving every other byte of the file alone.

    python3 tools/config_complete.py --src OSRS-Content/osrs239-content [--check]

`--check` writes nothing and exits 1 if anything is missing (a gate).
"""
import argparse
import collections
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_CACHEPACK = os.path.join(HERE, "..", "3rd", "rscache", "tools", "cachepack", "cachepack")


def missing(cachepack, src, rev, lanes=()):
    argv = [cachepack, "missing", "--src", src, "--rev", rev]
    for lane in lanes:
        argv += ["--lane", lane]
    out = subprocess.run(argv,
                         check=True, capture_output=True, text=True, encoding="latin-1").stdout
    by_file = collections.defaultdict(lambda: collections.defaultdict(list))
    for line in out.splitlines():
        kind, record, path, key, marker = line.split("\t")
        if path == "-":
            # A block with no lines carries no origin; find it by its header.
            path = find_block(src, lanes, kind, record)
        by_file[path][record].append((key, marker))
    return by_file


def find_block(src, lanes, kind, record):
    """The file holding `[record]` among the walked `.<kind>` files."""
    roots = ["configs", "server/scripts"] + ["ported/%s/configs" % lane for lane in lanes]
    header = "[%s]" % record
    for root in roots:
        for dirpath, _dirs, files in os.walk(os.path.join(src, root)):
            for name in sorted(files):
                if not name.endswith("." + kind):
                    continue
                path = os.path.join(dirpath, name)
                with open(path, encoding="latin-1") as handle:
                    if any(line.strip() == header for line in handle):
                        return path
    raise AssertionError("no file holds %s (%s)" % (header, kind))


def complete_file(path, records):
    """Append each record's missing keys after the last line of its first block."""
    with open(path, encoding="latin-1", newline="") as f:
        lines = f.read().split("\n")
    out = []
    current = None
    pending = None

    def flush():
        # Insert before any trailing blank lines/comments of the block.
        if pending is None:
            return
        keys = records.pop(pending, None)
        if not keys:
            return
        at = len(out)
        while at > 0 and (not out[at - 1].strip() or out[at - 1].lstrip().startswith("//")):
            at -= 1
        out[at:at] = ["%s=%s" % (k, m) for k, m in keys]

    for line in lines:
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            flush()
            current = stripped[1:-1]
            pending = current if current in records else None
        out.append(line)
    flush()
    assert not records, "blocks not found in %s: %s" % (path, sorted(records)[:5])
    with open(path, "w", encoding="latin-1", newline="") as f:
        f.write("\n".join(out))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--src", required=True)
    parser.add_argument("--rev", default="osrs239")
    parser.add_argument("--cachepack", default=DEFAULT_CACHEPACK)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--lane", action="append", default=[],
                        help="also complete an imported lane's own records "
                             "(ported/<lane>/configs), as a pack including it reads them")
    args = parser.parse_args()

    by_file = missing(args.cachepack, args.src, args.rev, args.lane)
    total = sum(len(k) for r in by_file.values() for k in r.values())
    records = sum(len(r) for r in by_file.values())
    if args.check:
        for path, recs in sorted(by_file.items()):
            for record, keys in sorted(recs.items()):
                print("%s [%s]: missing %s" % (path, record, ", ".join(k for k, _ in keys)))
        print("%d record(s) in %d file(s) miss %d key(s)" % (records, len(by_file), total))
        return 1 if total else 0
    for path, recs in sorted(by_file.items()):
        complete_file(path, dict(recs))
    print("completed %d record(s) in %d file(s) with %d `key=default` line(s)"
          % (records, len(by_file), total))
    return 0


if __name__ == "__main__":
    sys.exit(main())
