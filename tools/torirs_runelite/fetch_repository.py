#!/usr/bin/env python3
"""Make one RuneLite release's jar repository, pinned, in a directory of its own.

    tools/torirs_runelite/fetch_repository.py 1.13.6 build/runelite-1.13.6/repository2

RuneLite's launcher keeps every release it ever ran in ~/.runelite/repository2,
side by side, so that directory holds several guava, several client-*.jar and
several runelite-api-*.jar at once. run-runelite.sh sweeps a repository's
libraries onto the classpath, and two copies of one library there silently run
whichever sorts first. So each pinned release gets a repository with exactly
its own artifacts.

What a release is made of is tools/torirs_runelite/bootstrap-<version>.json: RuneLite's
https://static.runelite.net/bootstrap.json as it stood when that release was
current, kept here because the live file always names the newest release. Every
artifact is checked against the sha256 recorded there; a mismatch is fatal, and
a file already present with the right hash is not downloaded again.
"""

import hashlib
import json
import os
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    if len(sys.argv) != 3:
        print(__doc__.split("\n\n")[1], file=sys.stderr)
        return 2
    version, out = sys.argv[1], sys.argv[2]
    manifest = os.path.join(HERE, "bootstrap-%s.json" % version)
    if not os.path.exists(manifest):
        print("fetch_repository: no %s -- pin a release by saving its bootstrap.json there"
              % manifest, file=sys.stderr)
        return 1
    with open(manifest, encoding="utf-8") as f:
        boot = json.load(f)
    want = "client-%s.jar" % version
    if not any(a["name"] == want for a in boot["artifacts"]):
        print("fetch_repository: %s does not list %s" % (manifest, want), file=sys.stderr)
        return 1
    os.makedirs(out, exist_ok=True)
    fetched = 0
    for a in boot["artifacts"]:
        dest = os.path.join(out, a["name"])
        if os.path.exists(dest) and sha256(dest) == a["hash"]:
            continue
        print("fetch_repository: %s" % a["name"], file=sys.stderr)
        part = dest + ".part"
        with urllib.request.urlopen(a["path"], timeout=60) as r, open(part, "wb") as f:
            f.write(r.read())
        got = sha256(part)
        if got != a["hash"]:
            os.unlink(part)
            print("fetch_repository: %s sha256 %s, bootstrap says %s" % (a["name"], got, a["hash"]),
                  file=sys.stderr)
            return 1
        os.replace(part, dest)
        fetched += 1
    print("fetch_repository: RuneLite %s in %s (%d artifacts, %d fetched)"
          % (version, out, len(boot["artifacts"]), fetched), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
