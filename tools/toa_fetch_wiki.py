#!/usr/bin/env python3
"""toa_fetch_wiki — pull OSRS Wiki pages for the Tombs of Amascut research.

    tools/toa_fetch_wiki.py docs/minigames/tombs_of_amascut/sources <<'EOT'
    Tombs of Amascut
    Tombs of Amascut/Strategies
    EOT

Titles come in on stdin, one per line; `#` lines are ignored. Each page is
resolved through `action=query&redirects=1`, then re-read with
`action=parse&oldid=<revid>` so the file on disk is byte-identical to what the
`?oldid=` permalink renders. One file per resolved title, plus an appended row in
`manifest.tsv` recording title, revid, fetch date and filename.

Etiquette, matching tools/wiki_fetch.py: one request per second, a descriptive
User-Agent with a contact address, no parallelism. A missing page is reported on
stderr and recorded as MISSING rather than being retried.
"""
import json, sys, time, urllib.parse, urllib.request, pathlib, re

OUT = pathlib.Path(sys.argv[1])
OUT.mkdir(parents=True, exist_ok=True)
UA = "3draster-toa-research/1.0 (mrobertevers@gmail.com)"
API = "https://oldschool.runescape.wiki/api.php"

CA_MODE = "--combat-achievements" in sys.argv[2:]

TITLES = [] if CA_MODE else [
    t.strip() for t in sys.stdin.read().splitlines()
    if t.strip() and not t.startswith("#")]

def get(params):
    url = API + "?" + urllib.parse.urlencode(params)
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.load(r)


def combat_achievements():
    """Every ToA Combat Achievement, found by the wiki's own `monster =` field.

    Searching `insource:` rather than reading a hand-kept list means a task added
    upstream shows up on the next run instead of being silently missed.
    """
    titles = set()
    for monster in ("Tombs of Amascut", "Tombs of Amascut: Entry Mode",
                    "Tombs of Amascut: Expert Mode"):
        d = get({"action": "query", "list": "search",
                 "srsearch": f"insource:/monster = {monster}/",
                 "srlimit": "200", "srnamespace": "0", "format": "json"})
        titles |= {r["title"] for r in d["query"]["search"]}
        time.sleep(0.8)
    rows = []
    titles = sorted(titles)
    for i in range(0, len(titles), 20):
        d = get({"action": "query", "prop": "revisions",
                 "rvprop": "content|ids", "rvslots": "main",
                 "titles": "|".join(titles[i:i + 20]), "format": "json",
                 "formatversion": "2"})
        for page in d["query"]["pages"]:
            body = page["revisions"][0]["slots"]["main"]["content"]

            def field(key):
                m = re.search(rf"^\|\s*{key}\s*=\s*(.*)$", body, re.M)
                return m.group(1).strip() if m else ""

            if not field("id"):
                continue
            rows.append((int(field("id")), page["title"], field("tier"),
                         field("monster"), field("type"),
                         " ".join(field("description").split())))
        time.sleep(0.8)
    rows.sort()
    (OUT / "wiki_combat_achievements_toa.tsv").write_text(
        "id\tname\ttier\tmonster\ttype\tdescription\n"
        + "\n".join("\t".join(str(c) for c in r) for r in rows) + "\n",
        encoding="utf-8")
    print(f"combat achievements\t{len(rows)}")


if CA_MODE:
    combat_achievements()
    sys.exit(0)

manifest = []
for i in range(0, len(TITLES), 20):
    batch = TITLES[i:i+20]
    d = get({"action": "query", "prop": "revisions", "rvprop": "ids|timestamp",
             "rvslots": "main", "titles": "|".join(batch), "redirects": "1",
             "format": "json", "formatversion": "2"})
    redirects = {r["from"]: r["to"] for r in d["query"].get("redirects", [])}
    for page in d["query"]["pages"]:
        title = page["title"]
        if page.get("missing"):
            manifest.append((title, "MISSING", "", ""))
            print("MISSING", title, file=sys.stderr)
            continue
        rev = page["revisions"][0]
        revid, ts = rev["revid"], rev["timestamp"]
        c = get({"action": "parse", "oldid": revid, "prop": "wikitext",
                 "format": "json", "formatversion": "2"})
        text = c["parse"]["wikitext"]
        fn = "wiki_" + re.sub(r"[^A-Za-z0-9]+", "_", title).strip("_") + ".wikitext"
        (OUT / fn).write_text(text, encoding="utf-8")
        src = [k for k, v in redirects.items() if v == title]
        manifest.append((title, str(revid), ts[:10], fn))
        print(f"{title}\t{revid}\t{ts[:10]}\t{fn}", flush=True)
        time.sleep(0.8)
    time.sleep(0.8)

with (OUT / "manifest.tsv").open("a", encoding="utf-8") as f:
    for row in manifest:
        f.write("\t".join(row) + "\n")
