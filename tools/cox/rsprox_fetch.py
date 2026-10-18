#!/usr/bin/env python3
"""Fetch RSProx session recordings from the public archive (rsprox.net/database),
cached: a recording already on disk is never downloaded again.

    python3 tools/cox/rsprox_fetch.py search npc 7569      # submissions holding npc 7569
    python3 tools/cox/rsprox_fetch.py get 820 3389          # download into the cache

A recording comes from the NAS mirror (tools/rsprox_mirror.py) when it holds it,
else the archive. Recordings land in ~/.rsprox/binary/archive/<id>.bin -- RSProx's own binary
directory, so its `transcribe -name archive/<id>` finds them -- and every search
answer is kept beside them in search/<type>_<query>.json (a repeated search
reads the file). The archive's search is the page's own POST /api/search.
"""
import gzip
import json
import os
import sys
import urllib.request

ARCHIVE = "https://rsprox.net"
CACHE = os.path.join(os.path.expanduser("~"), ".rsprox", "binary", "archive")
MIRROR = "/Volumes/MatthewLLM_Shared/rsprox_archive"
TYPES = {"npc": 0, "obj": 1, "loc": 2, "mapsquare": 3, "message_game": 4, "varp": 5, "varbit": 6,
         "interface": 7, "seq": 8, "spotanim": 9, "text": 14}


def search(kind, query):
    assert kind in TYPES, "type must be one of " + ", ".join(TYPES)
    path = os.path.join(CACHE, "search", "%s_%s.json" % (kind, query))
    if os.path.isfile(path):
        with open(path, "r", encoding="utf-8") as handle:
            return json.load(handle)
    body = json.dumps({"conditions": [{"type": TYPES[kind], "query": str(query)}], "mode": "ALL",
                       "page": 0, "limit": 100, "sort": "NEWEST"}).encode()
    request = urllib.request.Request(ARCHIVE + "/api/search", data=body,
                                     headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=60) as response:
        answer = json.load(response)
    assert not answer.get("hasNext"), "more than 100 results: page the search"
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(answer, handle, indent=1)
    return answer


def get(submission):
    path = os.path.join(CACHE, "%d.bin" % submission)
    if os.path.isfile(path) and os.path.getsize(path) > 0:
        return path, False
    os.makedirs(CACHE, exist_ok=True)
    temporary = path + ".part"
    # the NAS mirror (tools/rsprox_mirror.py) first: never fetch a recording twice
    mirrored = os.path.join(MIRROR, "bin", "%d.bin.gz" % submission)
    if os.path.isfile(mirrored):
        with open(mirrored, "rb") as source, open(temporary, "wb") as handle:
            handle.write(source.read())
    else:
        request = urllib.request.Request("%s/download/%d" % (ARCHIVE, submission),
                                         headers={"User-Agent": "rsprox-archive-mirror/1.0"})
        with urllib.request.urlopen(request, timeout=300) as response:
            with open(temporary, "wb") as handle:
                while True:
                    chunk = response.read(1 << 20)
                    if not chunk:
                        break
                    handle.write(chunk)
    # the archive serves the recording gzipped; RSProx reads it raw
    with open(temporary, "rb") as handle:
        packed = handle.read(2) == b"\x1f\x8b"
    if packed:
        with gzip.open(temporary, "rb") as source, open(path, "wb") as target:
            while True:
                chunk = source.read(1 << 20)
                if not chunk:
                    break
                target.write(chunk)
        os.remove(temporary)
    else:
        os.replace(temporary, path)
    return path, True


def main(argv):
    assert len(argv) >= 2, __doc__
    if argv[0] == "search":
        assert len(argv) == 3, "search <type> <query>"
        answer = search(argv[1], argv[2])
        for row in answer.get("results", []):
            s = row["submission"]
            print("%5d  rev %s  %-10s  %-24s  matches %s" % (s["id"], s["revision"], s["fileSize"],
                                                             s.get("worldActivity", "-"), row["matches"]))
        return 0
    if argv[0] == "get":
        for item in argv[1:]:
            path, fetched = get(int(item))
            print("%s %s (%d bytes)" % ("fetched" if fetched else "cached ", path, os.path.getsize(path)))
        return 0
    raise SystemExit(__doc__)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
