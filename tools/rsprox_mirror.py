#!/usr/bin/env python3
"""Mirror the public RSProx recording archive (rsprox.net/database) to a cache,
politely: one request at a time, a pause between requests, backing off on 429 and
5xx (Retry-After honoured), resumable (a file already in the cache is never
fetched again).

    python3 tools/rsprox_mirror.py                       # index, then download
    python3 tools/rsprox_mirror.py --index-only
    python3 tools/rsprox_mirror.py --reuse-index         # download from the saved index
    python3 tools/rsprox_mirror.py --dest /some/other/dir

Layout under --dest (default the NAS share):
    index.tsv            id, date, revision, world activity, listed size
    bin/<id>.bin.gz      the recording exactly as the archive serves it (gzip)
    mirror.log           what was fetched, skipped, failed and why
A recording is unpacked for RSProx (raw .bin under ~/.rsprox/binary) by
tools/cox/rsprox_fetch.py, which reads this cache before the network.
"""
import argparse
import html
import os
import re
import sys
import time
import urllib.error
import urllib.request

ARCHIVE = "https://rsprox.net"
DEFAULT_DEST = "/Volumes/MatthewLLM_Shared/rsprox_archive"
USER_AGENT = "rsprox-archive-mirror/1.0 (one request at a time; research cache)"
PAGE_SIZE = 100
ROW = re.compile(r"<tr>\s*<td>([^<]*)</td>\s*<td>([^<]*)</td>\s*<td>([^<]*)</td>\s*<td>([^<]*)</td>\s*<td>\s*"
                 r"<a href=\"/download/(\d+)\"", re.S)
TOTAL = re.compile(r"Showing [\d,]+.[\d,]+ of ([\d,]+)")


def log(dest, text):
    line = time.strftime("%Y-%m-%d %H:%M:%S ") + text
    print(line, flush=True)
    with open(os.path.join(dest, "mirror.log"), "a", encoding="utf-8") as handle:
        handle.write(line + "\n")


def fetch(dest, url, pause):
    """GET url -> bytes, after `pause` seconds; backs off on 429/5xx and on
    connection errors (60 s doubling to 15 min, or the server's Retry-After)."""
    wait = 60
    for attempt in range(1, 9):
        time.sleep(pause)
        request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
        try:
            with urllib.request.urlopen(request, timeout=300) as response:
                return response.read()
        except urllib.error.HTTPError as error:
            if error.code == 404:
                raise
            if error.code != 429 and error.code < 500:
                raise
            retry = error.headers.get("Retry-After")
            delay = int(retry) if retry and retry.isdigit() else wait
            log(dest, "%s -> HTTP %d, attempt %d, waiting %d s" % (url, error.code, attempt, delay))
            time.sleep(delay)
        except (urllib.error.URLError, TimeoutError, ConnectionError) as error:
            log(dest, "%s -> %s, attempt %d, waiting %d s" % (url, error, attempt, wait))
            time.sleep(wait)
        wait = min(wait * 2, 900)
    raise RuntimeError("gave up on " + url)


def index(dest, pause):
    rows = {}
    page = 0
    total = None
    while True:
        url = "%s/database?view=all&sort=oldest&size=%d&page=%d" % (ARCHIVE, PAGE_SIZE, page)
        text = fetch(dest, url, pause).decode("utf-8", "replace")
        if total is None:
            m = TOTAL.search(text)
            total = int(m.group(1).replace(",", "")) if m else None
        found = ROW.findall(text)
        for date, revision, activity, size, ident in found:
            rows[int(ident)] = (html.unescape(date.strip()), revision.strip(), html.unescape(activity.strip()),
                                size.strip())
        log(dest, "index page %d: %d rows (%d of %s)" % (page, len(found), len(rows), total))
        if len(found) < PAGE_SIZE:
            break
        page += 1
    path = os.path.join(dest, "index.tsv")
    with open(path + ".part", "w", encoding="utf-8") as handle:
        handle.write("id\tdate\trevision\tactivity\tsize\n")
        for ident in sorted(rows):
            handle.write("%d\t%s\n" % (ident, "\t".join(rows[ident])))
    os.replace(path + ".part", path)
    assert total is None or len(rows) == total, "indexed %d of %s submissions" % (len(rows), total)
    return sorted(rows)


def download(dest, idents, pause):
    folder = os.path.join(dest, "bin")
    os.makedirs(folder, exist_ok=True)
    fetched = skipped = failed = 0
    for n, ident in enumerate(idents, 1):
        path = os.path.join(folder, "%d.bin.gz" % ident)
        if os.path.isfile(path) and os.path.getsize(path) > 0:
            skipped += 1
            continue
        assert os.path.isdir(dest), "the cache went away (NAS unmounted?): " + dest
        try:
            data = fetch(dest, "%s/download/%d" % (ARCHIVE, ident), pause)
        except Exception as error:  # one bad submission must not stop the mirror
            failed += 1
            log(dest, "%d FAILED: %s" % (ident, error))
            continue
        if data[:2] != b"\x1f\x8b":
            path = os.path.join(folder, "%d.bin" % ident)
        with open(path + ".part", "wb") as handle:
            handle.write(data)
        os.replace(path + ".part", path)
        fetched += 1
        if fetched % 25 == 0 or n == len(idents):
            log(dest, "%d/%d: fetched %d, already cached %d, failed %d" % (n, len(idents), fetched, skipped, failed))
    log(dest, "done: fetched %d, already cached %d, failed %d" % (fetched, skipped, failed))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dest", default=DEFAULT_DEST)
    parser.add_argument("--pause", type=float, default=3.0, help="seconds before every request")
    parser.add_argument("--index-only", action="store_true")
    parser.add_argument("--reuse-index", action="store_true", help="download from the saved index.tsv")
    args = parser.parse_args()
    assert os.path.isdir(os.path.dirname(args.dest.rstrip("/"))), "no such parent (NAS mounted?): " + args.dest
    os.makedirs(args.dest, exist_ok=True)
    if args.reuse_index:
        with open(os.path.join(args.dest, "index.tsv"), "r", encoding="utf-8") as handle:
            idents = [int(line.split("\t", 1)[0]) for line in handle.readlines()[1:]]
    else:
        idents = index(args.dest, args.pause)
    if not args.index_only:
        download(args.dest, idents, args.pause)
    return 0


if __name__ == "__main__":
    sys.exit(main())
