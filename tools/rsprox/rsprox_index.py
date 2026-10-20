#!/usr/bin/env python3
"""The local RSProx index server (rsprox-index, ~/Documents/git_repos/rsprox-index):
find recordings by content, and turn one into a transcript in one step.

    python3 tools/rsprox/rsprox_index.py stats
    python3 tools/rsprox/rsprox_index.py latest [N]                 # newest uploads (the owner's own recordings)
    python3 tools/rsprox/rsprox_index.py search npc 7569 [seq 8056 ...] [--any] [--sort NEWEST]
    python3 tools/rsprox/rsprox_index.py counts <idx> [npc|seq|...]  # one session's index rows
    python3 tools/rsprox/rsprox_index.py get <idx|net:N> ... [--all-players] [--raw] [--bas] [--force]

`get` prints the transcript path of each session, downloading the recording from
the server and transcribing it on first use. Transcripts are kept for every later
session in ~/.rsprox-index-cache/<variant>/.rsprox/binary/archive/idx<idx>.txt.

IDS. The index numbers its sessions itself. A session imported from the public
archive has `source` "rsprox.net:<N>", and N is NOT its index id (index 819 is
rsprox.net 820). The docs cite rsprox.net ids; `net:820` names one that way.
Files are named idx<idx> because the shared ~/.rsprox/binary/archive/<N>.txt
uses rsprox.net numbering: archive/4288.txt is a rev-239 public recording,
index 4288 the owner's rev-241 Tutorial Island.

VARIANTS (each its own RSProx home, so the shared transcripts are untouched):
    (none)         as ~/.rsprox/settings: the recorder only, instance tiles translated to the template
    --all-players  every player's movement, seqs and graphics (party rules)
    --raw          TRANSLATE_INSTANCED_COORDS=false: raw instance tiles, one frame for raider and npc
                   (anything measured between a player and an npc in a turned room)
    --bas          the player's base animation set and worn equipment (filtered out by default)

SERVER: $RSPROX_INDEX_URL, default http://127.0.0.1:8370 (from another machine on
the LAN: http://192.168.1.148:8370). The transcriber is the build the index server
runs ($RSPROX_CLASSPATH, default ~/rsprox-index-data/rsprox.classpath: RSProx
osrs-223..241 plus the local-indexer-fixes branch, see rsprox-index/PATCHES.md)
under JDK 21 ($RSPROX_JAVA).
"""
import argparse
import fcntl
import gzip
import json
import os
import shutil
import subprocess
import sys
import urllib.request

SERVER = os.environ.get("RSPROX_INDEX_URL", "http://127.0.0.1:8370").rstrip("/")
HOME = os.path.expanduser("~")
CACHE = os.path.join(HOME, ".rsprox-index-cache")
SHARED = os.path.join(HOME, ".rsprox")
CLASSPATH = os.environ.get("RSPROX_CLASSPATH", os.path.join(HOME, "rsprox-index-data", "rsprox.classpath"))
JAVA = os.environ.get("RSPROX_JAVA", "/Library/Java/JavaVirtualMachines/temurin-21.jdk/Contents/Home/bin/java")
TYPES = ("NPC", "OBJ", "LOC", "MAPSQUARE", "MESSAGE_GAME", "VARP", "VARBIT", "INTERFACE", "SEQ", "SPOTANIM",
         "MIDI", "SYNTH", "JINGLE", "CLIENTSCRIPTS", "TEXT", "AMBIENCE")
BAS_FILTER = "version=0\ncreationtime=1\nname=bas\nactive=true\nPLAYER_APPEARANCE_BAS=true\nPLAYER_APPEARANCE_EQUIPMENT=true\n"


def call(path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    request = urllib.request.Request(SERVER + path, data=data, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=120) as response:
        return json.load(response)


def row(session, matches=None):
    source = session["source"] or ""
    net = source[len("rsprox.net:"):] if source.startswith("rsprox.net:") else "-"
    revision = "%s.%s" % (session["revision"], session["sub_revision"]) if session["revision"] else "?"
    text = "%5d  net %-5s rev %-5s  w%-3s %-10s %-9s %-28s %s" % (
        session["id"], net, revision, session["world"] or "?",
        (session["recorded_on"] or "")[:10], session["status"], (session["activity"] or "-")[:28],
        session["description"] or ("" if net != "-" else source))
    return text if matches is None else "%s  [%d]" % (text, matches)


def search(pairs, mode, sort):
    assert len(pairs) % 2 == 0, "conditions come in pairs: <type> <query>"
    conditions = []
    for kind, query in zip(pairs[0::2], pairs[1::2]):
        assert kind.upper() in TYPES, "type must be one of " + ", ".join(TYPES)
        conditions.append({"type": kind.upper(), "query": query})
    page, out = 0, []
    while True:  # pages are 0-based; page 1 of a one-page answer is empty
        answer = call("/api/search", {"conditions": conditions, "mode": mode, "page": page, "limit": 500, "sort": sort})
        out.extend(answer["results"])
        page += 1
        if page >= answer["totalPages"]:
            return out


def resolve(name):
    """An index id, or `net:N` for the session imported from rsprox.net submission N."""
    if not name.startswith("net:"):
        return int(name)
    wanted = "rsprox.net:" + name[4:]
    table_path = os.path.join(CACHE, "net_ids.json")
    table = {}
    if os.path.isfile(table_path):
        with open(table_path, "r", encoding="utf-8") as handle:
            table = json.load(handle)
    if wanted not in table:  # the server has no lookup by source: page the session list once
        page = 0
        while True:
            answer = call("/api/sessions?page=%d&limit=500" % page)
            for session in answer["results"]:
                if session["source"]:
                    table[session["source"]] = session["id"]
            page += 1
            if page * 500 >= answer["total"]:
                break
        os.makedirs(CACHE, exist_ok=True)
        with open(table_path, "w", encoding="utf-8") as handle:
            json.dump(table, handle)
    assert wanted in table, "the index holds no session from " + wanted
    return table[wanted]


def variant_home(all_players, raw, bas):
    parts = [p for p, on in (("all", all_players), ("raw", raw), ("bas", bas)) if on]
    home = os.path.join(CACHE, "home-" + ("-".join(parts) or "default"))
    rsprox = os.path.join(home, ".rsprox")
    if os.path.isdir(os.path.join(rsprox, "settings")):
        return home
    os.makedirs(os.path.join(rsprox, "binary", "archive"), exist_ok=True)
    if not os.path.lexists(os.path.join(rsprox, "caches")):
        os.symlink(os.path.join(SHARED, "caches"), os.path.join(rsprox, "caches"))  # historic caches, shared
    settings = [f for f in os.listdir(os.path.join(SHARED, "settings")) if f.startswith("settings-")]
    assert settings, "no ~/.rsprox/settings/settings-*.txt to copy"
    with open(os.path.join(SHARED, "settings", sorted(settings)[-1]), "r", encoding="utf-8") as handle:
        text = handle.read()
    if all_players:
        text = text.replace("PLAYER_INFO_LOCAL_PLAYER_ONLY=true", "PLAYER_INFO_LOCAL_PLAYER_ONLY=false")
    if raw:
        text = text.replace("TRANSLATE_INSTANCED_COORDS=true", "TRANSLATE_INSTANCED_COORDS=false")
    if bas:
        os.makedirs(os.path.join(rsprox, "filters"), exist_ok=True)
        with open(os.path.join(rsprox, "filters", "filters-1.txt"), "w", encoding="utf-8") as handle:
            handle.write(BAS_FILTER)
    staging = os.path.join(rsprox, "settings.part")
    os.makedirs(staging, exist_ok=True)
    with open(os.path.join(staging, "settings-1.txt"), "w", encoding="utf-8") as handle:
        handle.write(text)
    os.replace(staging, os.path.join(rsprox, "settings"))
    return home


def fetch_bin(session):
    path = os.path.join(CACHE, "bin", "idx%d.bin" % session["id"])
    if os.path.isfile(path):
        return path
    os.makedirs(os.path.dirname(path), exist_ok=True)
    temporary = path + ".%d.part" % os.getpid()
    with urllib.request.urlopen(SERVER + "/api/sessions/%d/download" % session["id"], timeout=600) as response:
        with gzip.open(response, "rb") as source, open(temporary, "wb") as target:
            shutil.copyfileobj(source, target, 1 << 20)  # RSProx reads the recording raw
    os.replace(temporary, path)
    return path


def get(name, all_players, raw, bas, force):
    index_id = resolve(name)
    session = call("/api/sessions/%d" % index_id)
    if session["status"] != "indexed":
        # a failed or queued session is invisible to search, but its recording transcribes
        print("note: session %d is %s, so search cannot find it%s" % (
            index_id, session["status"], ": " + session["error"][:200] if session["error"] else ""), file=sys.stderr)
    home = variant_home(all_players, raw, bas)
    archive = os.path.join(home, ".rsprox", "binary", "archive")
    transcript = os.path.join(archive, "idx%d.txt" % index_id)
    with open(os.path.join(archive, "idx%d.lock" % index_id), "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)  # another session may be transcribing the same one
        if os.path.isfile(transcript) and os.path.getsize(transcript) > 0 and not force:
            return transcript
        binary = fetch_bin(session)
        link = os.path.join(archive, "idx%d.bin" % index_id)
        if not os.path.lexists(link):
            os.symlink(binary, link)
        assert os.path.isfile(CLASSPATH), "no RSProx classpath at " + CLASSPATH
        with open(CLASSPATH, "r", encoding="utf-8") as handle:
            classpath = handle.read().strip()
        log = os.path.join(archive, "idx%d.log" % index_id)
        with open(log, "w") as handle:
            done = subprocess.run([JAVA, "-Duser.home=" + home, "-cp", classpath,
                                   "net.rsprox.proxy.cli.TranscribeCommandKt", "-name", "archive/idx%d" % index_id],
                                  stdout=handle, stderr=subprocess.STDOUT)
        if done.returncode != 0 or not os.path.isfile(transcript) or os.path.getsize(transcript) == 0:
            with open(log, "r", encoding="utf-8", errors="replace") as handle:
                tail = handle.readlines()[-15:]
            sys.exit("transcribing session %d failed (exit %d), log %s:\n%s" % (
                index_id, done.returncode, log, "".join(tail)))
    return transcript


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("stats")
    latest = sub.add_parser("latest")
    latest.add_argument("n", nargs="?", type=int, default=10)
    find = sub.add_parser("search")
    find.add_argument("pairs", nargs="+", help="<type> <query> ...")
    find.add_argument("--any", action="store_true", help="sessions meeting any condition (default: all)")
    find.add_argument("--sort", default="MATCHES")
    counts = sub.add_parser("counts")
    counts.add_argument("session")
    counts.add_argument("type", nargs="?")
    fetch = sub.add_parser("get")
    fetch.add_argument("sessions", nargs="+", help="index ids, or net:N for rsprox.net submission N")
    fetch.add_argument("--all-players", action="store_true")
    fetch.add_argument("--raw", action="store_true")
    fetch.add_argument("--bas", action="store_true")
    fetch.add_argument("--force", action="store_true", help="transcribe again")
    args = parser.parse_args()

    if args.command == "stats":
        print(json.dumps(call("/api/stats"), indent=1))
    elif args.command == "latest":
        for session in call("/api/sessions?page=0&limit=%d" % args.n)["results"]:
            print(row(session))
    elif args.command == "search":
        results = search(args.pairs, "ANY" if args.any else "ALL", args.sort.upper())
        stats = call("/api/stats")["sessions"]
        print("%d sessions (index: %s)" % (len(results), ", ".join("%s %d" % kv for kv in sorted(stats.items()))))
        for result in results:
            print(row(result["session"], result["matches"]))
    elif args.command == "counts":
        query = "?type=" + args.type.upper() if args.type else ""
        for count in call("/api/sessions/%d/counts%s" % (resolve(args.session), query)):
            print("%-13s %-8s %6d" % (count["type"], count["ikey"] if count["skey"] is None else count["skey"],
                                      count["count"]))
    else:
        for name in args.sessions:
            print(get(name, args.all_players, args.raw, args.bas, args.force))


if __name__ == "__main__":
    main()
