#!/usr/bin/env python3
"""verify_blert -- measure wave minigames from Blert recordings and from our own tick log.

    tools/waves_gate/verify_blert.py [--game inferno|colosseum] list --status completed --limit 12
    tools/waves_gate/verify_blert.py sample                      # 12 completed + 6 failed late
    tools/waves_gate/verify_blert.py fetch   <uuid> [--waves 1-69]
    tools/waves_gate/verify_blert.py overviews                   # per-wave record of each cached challenge
    tools/waves_gate/verify_blert.py export                      # observed npc rows + wave records as TSV
    tools/waves_gate/verify_blert.py summary [--md out.md]       # per-wave spawns, per-npc attack gaps
    tools/waves_gate/verify_blert.py ticklog <run>/ticklog.tsv   # the same summary of OUR server

--game picks the minigame (default inferno, so every earlier invocation is unchanged). The
Colosseum is challenge type 4; wave n is stage 99 + n (stages 100-111, 12 waves; the plugin's
core/Stage.java); GET /api/v1/challenges/colosseum/<uuid>/events?stage=<99 + wave>; its cache
is docs/minigames/colosseum/sources/blert_api/. See
docs/minigames/colosseum/sources/blert/PROVENANCE.md before quoting any number: a Manticore
burst's 2nd and 3rd NPC_ATTACK are ASSERTED by the plugin (one per tick), so the Colosseum
summary groups a burst and measures only the gap between burst starts.

Pattern: tools/verify_tob_timings.py (list, fetch into a cache, analyse the cache). Differences:

* The Inferno is challenge type 5. A challenge's events are served one WAVE (stage) at a
  time: GET /api/v1/challenges/inferno/<uuid>/events?stage=<199 + wave>. A completed run
  is therefore 69 requests; the cache holds one file per challenge and a wave is never
  fetched twice.
* Blert is a volunteer service. MIN_INTERVAL_SECONDS is a constant, not a flag, and it is
  enforced across processes through a lock file, so two invocations still make one request
  per three seconds between them.
* Only OBSERVED events are summarised (see
  docs/minigames/inferno/sources/blert/PROVENANCE.md): NPC_SPAWN, NPC_ATTACK, NPC_DEATH. An
  NPC_ATTACK is emitted by the plugin from an NPC animation id; an NPC_SPAWN is a spawn the
  client saw (except those inside the wave's first tick, see below); an NPC_DEATH is a
  DESPAWN, not the tick hitpoints reached zero. INFERNO_WAVE_START's start tick rests on
  an asserted 10-tick offset for wave 1 and is never printed as a measurement.

Tick 0 of a wave is the tick the chat message "Wave: N" arrived. Every NPC the client
already held at that message is reported as spawned on tick 0 whatever tick it truly
appeared on (DataTracker.getTick() returns 0 while the tracker is NOT_STARTED), so the
tick-0 batch is "the wave's set", not a spawn-time measurement. Spawns after tick 0 are
real spawn ticks.
"""

from __future__ import annotations

import argparse
import collections
import datetime
import fcntl
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
API = "https://blert.io/api/v1"
UA = "3draster-toa-research/1.0 (mrobertevers@gmail.com)"

# A hard constant. Never read from the command line or the environment.
MIN_INTERVAL_SECONDS = 3.0
_THROTTLE_SLACK = 0.05          # request starts are spaced by at least 3.05 s

CHALLENGE_TYPE = 5
MAX_WAVES = 69
API_SLUG = "inferno"
GAME = "inferno"
PILLAR_NPC = 7709               # the Inferno pillars are excluded from the tick-0 set
BURST_ATTACKS: frozenset = frozenset()   # attacks whose follow-ups are asserted (Colosseum manticore)
STATUS = {"in_progress": 0, "completed": 1, "reset": 2, "wiped": 3, "abandoned": 4}
STAGE_WAVE_1 = 200      # wave n is stage 199 + n (plugin core/Stage.java); Colosseum: 99 + n

DOCS_CACHE = os.path.join(REPO, "docs", "minigames", "inferno", "sources", "blert_api")
BIG_CACHE_ROOT = os.path.join(REPO, "build", "corpus_tmp", "blert_api")
BIG_CACHE = BIG_CACHE_ROOT
BIG_FILE_BYTES = 2 * 1024 * 1024
FETCH_LOG = os.path.join(DOCS_CACHE, "FETCH_LOG.tsv")
THROTTLE_LOCK = os.path.join(BIG_CACHE_ROOT, ".last_request")

# Event type numbers: plugin events/EventType.java, protos/event.proto.
EV_PLAYER_UPDATE, EV_PLAYER_ATTACK, EV_PLAYER_DEATH = 4, 5, 6
EV_NPC_SPAWN, EV_NPC_UPDATE, EV_NPC_DEATH, EV_NPC_ATTACK = 7, 8, 9, 10
EV_INFERNO_WAVE_START = 300
OBSERVED_NPC_EVENTS = (EV_NPC_SPAWN, EV_NPC_ATTACK, EV_NPC_DEATH)

# plugin challenges/inferno/InfernoNpc.java (id, name).
NPC_NAME = {
    7691: "nibbler", 7692: "bat", 7693: "blob", 7694: "bloblet_mage", 7695: "bloblet_range",
    7696: "bloblet_melee", 7697: "meleer", 7698: "ranger", 7699: "mager", 7700: "jad",
    7701: "jad_healer", 7702: "zuk_ranger", 7703: "zuk_mager", 7704: "zuk_jad",
    7705: "zuk_jad_healer", 7706: "zuk", 7707: "zuk_shield", 7708: "zuk_healer",
    7709: "pillar",
}
# plugin core/NpcAttack.java
ATTACK_NAME = {
    70: "bat_auto", 71: "blob_ranged", 72: "blob_mage", 73: "blob_melee",
    74: "bloblet_ranged", 75: "bloblet_mage", 76: "bloblet_melee", 77: "meleer_auto",
    78: "meleer_dig", 79: "ranger_auto", 80: "ranger_melee", 81: "mager_auto",
    82: "mager_melee", 83: "mager_resurrect", 84: "jad_ranged", 85: "jad_mage",
    86: "jad_melee", 87: "jad_healer_auto", 88: "zuk_auto",
}
# plugin InfernoNpc.java: (npc id, animation id) -> NpcAttack number. Used to read OUR tick
# log (which carries sequence ids) with the same names Blert's events carry.
ANIM_BY_NPC = {
    (7692, 7578): 70,
    (7693, 7581): 72, (7693, 7582): 73, (7693, 7583): 71,
    (7694, 7581): 75, (7695, 7583): 74, (7696, 7582): 76,
    (7697, 7597): 77, (7697, 7600): 78,
    (7698, 7604): 80, (7698, 7605): 79, (7702, 7604): 80, (7702, 7605): 79,
    (7699, 7610): 81, (7699, 7611): 83, (7699, 7612): 82,
    (7703, 7610): 81, (7703, 7611): 83, (7703, 7612): 82,
    (7700, 7590): 86, (7700, 7592): 85, (7700, 7593): 84,
    (7704, 7590): 86, (7704, 7592): 85, (7704, 7593): 84,
    (7701, 2637): 87, (7705, 2637): 87, (7706, 7566): 88,
}


INFERNO_NAMES, INFERNO_ATTACKS, INFERNO_ANIMS = NPC_NAME, ATTACK_NAME, ANIM_BY_NPC

# Fortis Colosseum. plugin challenges/colosseum/ColosseumNpc.java (ids), core/NpcAttack.java
# (attack numbers 100-115), core/Stage.java (COLOSSEUM_WAVE_1 = 100 .. WAVE_12 = 111).
COLOSSEUM_NAMES = {
    12810: "jaguar_warrior", 12811: "serpent_shaman", 12812: "minotaur", 12813: "minotaur_12813",
    12814: "fremennik_archer", 12815: "fremennik_seer", 12816: "fremennik_berserker",
    12817: "javelin_colossus", 12818: "manticore", 12819: "shockwave_colossus",
    12821: "sol_heredit", 12823: "bee_swarm", 12824: "laser_prism", 12825: "healing_totem",
    12826: "solarflare",
}
COLOSSEUM_ATTACKS = {
    100: "berserker_auto", 101: "seer_auto", 102: "archer_auto", 103: "shaman_auto",
    104: "jaguar_auto", 105: "javelin_auto", 106: "javelin_toss", 107: "manticore_mage",
    108: "shockwave_auto", 109: "minotaur_auto", 110: "sol_thrust", 111: "sol_slam",
    112: "sol_break", 113: "sol_combo", 114: "manticore_range", 115: "manticore_melee",
    -1: "manticore_burst",      # synthetic: the gap between burst STARTS (summarise)
}
# ColosseumNpc.java: (npc id, animation id) -> NpcAttack number. The manticore has none: its
# attacks are read from the animation 10869 plus the style spotanim (Manticore.java), so a
# ticklog can only be compared on its animation ticks (see BURST_ATTACKS).
COLOSSEUM_ANIMS = {
    (12810, 10847): 104, (12811, 10859): 103, (12812, 10843): 109, (12813, 10843): 109,
    (12814, 10850): 102, (12815, 10853): 101, (12816, 10856): 100,
    (12817, 10892): 105, (12817, 10893): 106, (12819, 10903): 108,
    (12821, 10883): 110, (12821, 10884): 112, (12821, 10885): 111, (12821, 10887): 113,
}
COLOSSEUM_BURST_ATTACKS = frozenset({107, 114, 115})
MANTICORE_ANIMATION = 10869     # Manticore.ATTACK_ANIMATION (not an NpcAttack)


def configure(game: str) -> None:
    """Point every module-level constant at one minigame (called once, by main)."""
    global GAME, CHALLENGE_TYPE, MAX_WAVES, API_SLUG, STAGE_WAVE_1, PILLAR_NPC, BURST_ATTACKS
    global NPC_NAME, ATTACK_NAME, ANIM_BY_NPC, DOCS_CACHE, FETCH_LOG, BIG_CACHE
    GAME = game
    if game == "colosseum":
        CHALLENGE_TYPE, MAX_WAVES, API_SLUG, STAGE_WAVE_1, PILLAR_NPC = 4, 12, "colosseum", 100, -1
        BURST_ATTACKS = COLOSSEUM_BURST_ATTACKS
        NPC_NAME, ATTACK_NAME, ANIM_BY_NPC = COLOSSEUM_NAMES, COLOSSEUM_ATTACKS, COLOSSEUM_ANIMS
    elif game == "inferno":
        CHALLENGE_TYPE, MAX_WAVES, API_SLUG, STAGE_WAVE_1, PILLAR_NPC = 5, 69, "inferno", 200, 7709
        BURST_ATTACKS = frozenset()
        NPC_NAME, ATTACK_NAME, ANIM_BY_NPC = INFERNO_NAMES, INFERNO_ATTACKS, INFERNO_ANIMS
    else:
        raise SystemExit("unknown --game %r" % game)
    DOCS_CACHE = os.path.join(REPO, "docs", "minigames", game, "sources", "blert_api")
    # Checkpoints and files over 2 MB: one directory per game (the Inferno's stays where it was).
    # THROTTLE_LOCK stays in the Inferno's directory so every game shares one request clock.
    BIG_CACHE = BIG_CACHE_ROOT if game == "inferno" else os.path.join(BIG_CACHE_ROOT, game)
    FETCH_LOG = os.path.join(DOCS_CACHE, "FETCH_LOG.tsv")


# ---------------------------------------------------------------------------------------
# HTTP: the only place a request is made
# ---------------------------------------------------------------------------------------

def _today() -> str:
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _throttle() -> None:
    """Block until MIN_INTERVAL_SECONDS (+ slack) have passed since ANY process's last
    request. The timestamp lives in a lock file so parallel invocations queue."""
    os.makedirs(os.path.dirname(THROTTLE_LOCK), exist_ok=True)
    with open(THROTTLE_LOCK, "a+") as fh:
        fcntl.flock(fh, fcntl.LOCK_EX)
        fh.seek(0)
        raw = fh.read().strip()
        last = float(raw) if raw else 0.0
        wait = last + MIN_INTERVAL_SECONDS + _THROTTLE_SLACK - time.time()
        if wait > 0:
            time.sleep(wait)
        fh.seek(0)
        fh.truncate()
        fh.write(repr(time.time()))
        fh.flush()
        fcntl.flock(fh, fcntl.LOCK_UN)


def _log_fetch(url: str, status: int, size: int, target: str, started: str = "") -> None:
    os.makedirs(os.path.dirname(FETCH_LOG), exist_ok=True)
    new = not os.path.exists(FETCH_LOG)
    with open(FETCH_LOG, "a", encoding="utf-8") as fh:
        if new:
            fh.write("date_utc\tstatus\tbytes\turl\tfile\n")
        # date_utc is when the request STARTED (after the throttle released it). Rows written
        # before 2026-10-03T18:00Z used the time the response finished instead.
        fh.write("%s\t%d\t%d\t%s\t%s\n" % (started or _today(), status, size, url, target))


def http_get(url: str, target: str = "", retries: int = 5):
    """GET url as JSON. Returns (status, body); body is None on 404. Every attempt is
    throttled and logged. 429 backs off 60 s."""
    for attempt in range(retries):
        _throttle()
        started = _today()
        req = urllib.request.Request(url, headers={"User-Agent": UA, "Accept": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=90) as resp:
                raw = resp.read()
                _log_fetch(url, resp.status, len(raw), target, started)
                return resp.status, json.loads(raw)
        except urllib.error.HTTPError as exc:
            _log_fetch(url, exc.code, 0, target, started)
            if exc.code == 404:
                return 404, None
            if exc.code == 429 or exc.code >= 500:
                time.sleep(60 if exc.code == 429 else 10 * (attempt + 1))
                continue
            raise
        except (urllib.error.URLError, TimeoutError):
            _log_fetch(url, -1, 0, target, started)
            time.sleep(10 * (attempt + 1))
    raise RuntimeError("gave up on %s" % url)


# ---------------------------------------------------------------------------------------
# Listing and the cache
# ---------------------------------------------------------------------------------------

def list_challenges(status: int | None, limit: int, stage_ge: int | None = None) -> list[dict]:
    q: dict = {"type": CHALLENGE_TYPE, "limit": limit}
    if status is not None:
        q["status"] = status
    if stage_ge is not None:
        q["stage"] = "ge%d" % stage_ge
    _, body = http_get("%s/challenges?%s" % (API, urllib.parse.urlencode(q)), "(listing)")
    return body or []


def _cache_paths(uuid: str) -> list[str]:
    return [os.path.join(DOCS_CACHE, uuid + ".json"), os.path.join(BIG_CACHE, uuid + ".json")]


def load_cached(uuid: str) -> dict | None:
    for path in _cache_paths(uuid):
        if os.path.exists(path):
            with open(path, encoding="utf-8") as fh:
                return json.load(fh)
    return None


def _save(uuid: str, doc: dict) -> str:
    """Checkpoint into BIG_CACHE (not in git) while fetching; `place` moves a finished
    file of at most 2 MB into the docs tree."""
    os.makedirs(BIG_CACHE, exist_ok=True)
    path = os.path.join(BIG_CACHE, uuid + ".json")
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump(doc, fh, separators=(",", ":"))
    os.replace(tmp, path)
    return path


def place(uuid: str) -> str:
    big = os.path.join(BIG_CACHE, uuid + ".json")
    if not os.path.exists(big):
        return ""
    if os.path.getsize(big) > BIG_FILE_BYTES:
        return big
    os.makedirs(DOCS_CACHE, exist_ok=True)
    dst = os.path.join(DOCS_CACHE, uuid + ".json")
    os.replace(big, dst)
    return dst


def parse_waves(spec: str | None, upto: int) -> list[int]:
    if not spec:
        return list(range(1, upto + 1))
    out: list[int] = []
    for part in spec.split(","):
        a, _, b = part.partition("-")
        out.extend(range(int(a), int(b or a) + 1))
    return sorted({w for w in out if 1 <= w <= MAX_WAVES})


def fetch_overview(doc: dict) -> bool:
    """One request per challenge: GET /challenges/inferno/<uuid>. Its `inferno.waves[]` is the
    server's per-wave record: ticks (the wave's length), startTick, and every npc with its
    spawnTick, spawnPoint, deathTick. startTick of wave 1 rests on the plugin's asserted
    10-tick offset; the DIFFERENCES between waves do not."""
    if "overview" in doc:
        return False
    _, body = http_get("%s/challenges/%s/%s" % (API, API_SLUG, doc["uuid"]), doc["uuid"] + ".json")
    doc["overview"] = body
    _save(doc["uuid"], doc)
    return True


def fetch_challenge(row: dict, waves_spec: str | None = None) -> str:
    uuid = row["uuid"]
    doc = load_cached(uuid) or {"uuid": uuid, "challenge": row, "waves": {}, "missing": []}
    fetch_overview(doc)
    reached = max(1, min(MAX_WAVES, int(row.get("stage") or STAGE_WAVE_1) - STAGE_WAVE_1 + 1))
    for wave in parse_waves(waves_spec, reached):
        if str(wave) in doc["waves"] or wave in doc["missing"]:
            continue                                  # never fetched twice
        stage = STAGE_WAVE_1 + wave - 1
        url = "%s/challenges/%s/%s/events?stage=%d" % (API, API_SLUG, uuid, stage)
        _, events = http_get(url, uuid + ".json")
        if events is None:
            doc["missing"].append(wave)
        else:
            doc["waves"][str(wave)] = events
        doc["fetched_utc"] = _today()
        _save(uuid, doc)
        sys.stderr.write("  %s wave %d: %s\n" % (uuid[:8], wave, "missing" if events is None else "%d events" % len(events)))
    _save(uuid, doc)
    return place(uuid)


def all_cached() -> list[dict]:
    seen: dict[str, dict] = {}
    for d in (DOCS_CACHE, BIG_CACHE):
        if not os.path.isdir(d):
            continue
        for fname in sorted(os.listdir(d)):
            if fname.endswith(".json") and len(fname) == 41 and fname[:-5] not in seen:
                with open(os.path.join(d, fname), encoding="utf-8") as fh:
                    seen[fname[:-5]] = json.load(fh)
    return list(seen.values())


# ---------------------------------------------------------------------------------------
# Normalised observations: one shape for a recording and for our server
# ---------------------------------------------------------------------------------------
# Observation = dict(wave, tick, kind in spawn|attack|death, npc, room, x, y, attack)

def observations_from_blert(doc: dict) -> list[dict]:
    out = []
    for wave_key, events in doc["waves"].items():
        wave = int(wave_key)
        for e in events:
            t = e.get("type")
            if t not in OBSERVED_NPC_EVENTS:
                continue
            npc = e.get("npc") or {}
            if t == EV_NPC_ATTACK:
                na = e.get("npcAttack") or {}
                out.append(dict(wave=wave, tick=e["tick"], kind="attack", npc=npc.get("id", 0),
                                room=npc.get("roomId", -1), x=e.get("xCoord"), y=e.get("yCoord"),
                                attack=na.get("attack")))
            else:
                out.append(dict(wave=wave, tick=e["tick"], kind="spawn" if t == EV_NPC_SPAWN else "death",
                                npc=npc.get("id", 0), room=npc.get("roomId", -1),
                                x=e.get("xCoord"), y=e.get("yCoord"), attack=None))
    return out


def read_ticklog(path: str) -> list[dict]:
    """TODO(waves driver seam, docs/minigames/waves_loop/DRIVER_NOTES.md): the waves tick
    log's columns are not fixed. This reader accepts a TSV whose first line is a header
    and whose rows carry at least `tick` and `kind`, with kind in {npc_spawn, npc_anim,
    npc_death} and the columns `npc` (npc type id), `slot` (the npc's index), `x`, `z`
    (or `y`), `seq` (npc_anim), and optionally `wave`. Everything else is ignored. When
    the driver seam fixes the real columns, change ONLY this function; the summaries take
    the normalised observations it returns (see observations_from_blert).

    A tick log's tick is the server's; Blert's tick 0 is the `Wave: N` message. Until the
    log marks the wave start, `wave` is 0 and ticks are absolute: do not compare the two
    tick axes directly, compare gaps."""
    out: list[dict] = []
    with open(path, encoding="utf-8", errors="replace") as fh:
        header = fh.readline().rstrip("\n").split("\t")
        col = {n: i for i, n in enumerate(header)}
        if "tick" not in col or "kind" not in col:
            raise SystemExit("%s: header %r has no tick/kind columns; see read_ticklog TODO" % (path, header))

        def cell(cols, key, default=0):
            i = col.get(key)
            if i is None or i >= len(cols) or cols[i] == "":
                return default
            try:
                return int(cols[i])
            except ValueError:
                return default

        for line in fh:
            cols = line.rstrip("\n").split("\t")
            kind = cols[col["kind"]] if col["kind"] < len(cols) else ""
            if kind not in ("npc_spawn", "npc_anim", "npc_death"):
                continue
            npc = cell(cols, "npc")
            obs = dict(wave=cell(cols, "wave", 0), tick=cell(cols, "tick"), npc=npc,
                       room=cell(cols, "slot", -1), x=cell(cols, "x"),
                       y=cell(cols, "y", cell(cols, "z")), attack=None)
            if kind == "npc_anim":
                attack = ANIM_BY_NPC.get((npc, cell(cols, "seq", -1)))
                if attack is None and GAME == "colosseum" and npc == 12818 and cell(cols, "seq", -1) == MANTICORE_ANIMATION:
                    attack = 107        # a burst START; the style needs the spotanim, which the log may not carry
                if attack is None:
                    continue
                obs.update(kind="attack", attack=attack)
            else:
                obs["kind"] = "spawn" if kind == "npc_spawn" else "death"
            out.append(obs)
    return out


# ---------------------------------------------------------------------------------------
# Summaries (observed events only)
# ---------------------------------------------------------------------------------------

def name(npc: int) -> str:
    return NPC_NAME.get(npc, "npc%d" % npc)


def attack_name(a) -> str:
    return ATTACK_NAME.get(a, "attack%s" % a)


def summarise(runs: list[tuple[str, list[dict]]]) -> dict:
    """runs: [(label, observations)]. Returns the aggregates the report prints."""
    tick0 = collections.defaultdict(collections.Counter)      # wave -> (npc, x, y) on tick 0
    later = collections.defaultdict(collections.Counter)      # wave -> (npc, x, y) after tick 0
    wave_runs: dict[int, int] = collections.defaultdict(int)
    gaps = collections.defaultdict(collections.Counter)       # (npc, attack) -> gap -> n
    firsts = collections.defaultdict(collections.Counter)     # (npc, attack) -> ticks spawn->first attack
    deaths = collections.defaultdict(collections.Counter)     # npc -> despawn lifetime
    ends = collections.defaultdict(list)                      # wave -> per-run last event tick
    comp = collections.defaultdict(collections.Counter)       # wave -> tick-0 set (npc name x count) -> runs
    later_ticks = collections.defaultdict(collections.Counter)  # (wave, npc) -> spawn tick (after 0) -> n
    bursts: collections.Counter = collections.Counter()       # style tuple of one manticore burst -> n
    for _, obs in runs:
        sets: dict[int, collections.Counter] = collections.defaultdict(collections.Counter)
        for o in obs:
            if o["kind"] == "spawn" and o["tick"] == 0 and o["npc"] != PILLAR_NPC:
                sets[o["wave"]][o["npc"]] += 1
        for w, c in sets.items():
            comp[w][tuple(sorted((name(n), k) for n, k in c.items()))] += 1
        for w in {o["wave"] for o in obs}:
            wave_runs[w] += 1
        per_end: dict[int, int] = {}
        rooms: dict[tuple[int, int], dict] = {}
        for o in sorted(obs, key=lambda o: (o["wave"], o["tick"])):
            key = (o["wave"], o["room"])
            per_end[o["wave"]] = max(per_end.get(o["wave"], 0), o["tick"])
            if o["kind"] == "spawn":
                (tick0 if o["tick"] == 0 else later)[o["wave"]][(o["npc"], o["x"], o["y"])] += 1
                if o["tick"] > 0:
                    later_ticks[(o["wave"], o["npc"])][o["tick"]] += 1
                rooms[key] = dict(spawn=o["tick"], last=None)
            elif o["kind"] == "attack" and o["attack"] in BURST_ATTACKS:
                # A manticore burst: the plugin emits ONE event per tick for three ticks from one
                # animation (ASSERTED, PROVENANCE.md). Count the burst, not its inner events.
                rec = rooms.setdefault(key, dict(spawn=None, last=None))
                b = rec.get("burst")
                if b is not None and o["tick"] - b["start"] <= 2 and len(b["styles"]) < 3:
                    b["styles"].append(o["attack"])
                    continue
                if b is not None:
                    bursts[tuple(b["styles"])] += 1
                if rec.get("burst_prev") is not None:
                    gaps[(o["npc"], -1)][o["tick"] - rec["burst_prev"]] += 1
                elif rec["spawn"] not in (None, 0):
                    firsts[(o["npc"], -1)][o["tick"] - rec["spawn"]] += 1
                rec["burst"] = dict(start=o["tick"], styles=[o["attack"]])
                rec["burst_prev"] = o["tick"]
            elif o["kind"] == "attack":
                rec = rooms.setdefault(key, dict(spawn=None, last=None))
                if rec["last"] is not None:
                    gaps[(o["npc"], o["attack"])][o["tick"] - rec["last"]] += 1
                elif rec["spawn"] not in (None, 0):
                    firsts[(o["npc"], o["attack"])][o["tick"] - rec["spawn"]] += 1
                rec["last"] = o["tick"]
            else:
                rec = rooms.get(key)
                if rec and rec["spawn"] not in (None, 0):
                    deaths[o["npc"]][o["tick"] - rec["spawn"]] += 1
        for rec in rooms.values():
            if rec.get("burst") is not None:
                bursts[tuple(rec["burst"]["styles"])] += 1
        for w, t in per_end.items():
            ends[w].append(t)
    return dict(tick0=tick0, later=later, wave_runs=wave_runs, gaps=gaps, firsts=firsts,
                deaths=deaths, ends=ends, comp=comp, later_ticks=later_ticks, bursts=bursts)


def _dist(counter: collections.Counter) -> str:
    n = sum(counter.values())
    return "n=%d  %s" % (n, "  ".join("%d:%d" % (g, c) for g, c in sorted(counter.items())))


def render(agg: dict, label: str) -> str:
    L: list[str] = ["# %s" % label, "", "## Tick-0 set per wave (pillars excluded)",
                    "Event: NPC_SPAWN(7) on tick 0 (the wave's set; spawn TIME is clamped, see above).",
                    "Each line: how many runs showed that exact multiset.", ""]
    for wave in sorted(agg["comp"]):
        for key, c in sorted(agg["comp"][wave].items(), key=lambda kv: -kv[1]):
            L.append("wave %-2d runs %-2d  %s" % (wave, c, " ".join("%sx%d" % kv for kv in key) or "(none)"))
    L += ["", "## Spawn tiles per wave",
                    "Event: NPC_SPAWN(7). 'first tick' = tick 0 = the tick the chat message `Wave: N`",
                    "arrived; every npc the client already held then is stamped tick 0 (a set, not a",
                    "spawn time). 'later' spawns carry a real spawn tick. 'runs' = runs that recorded",
                    "the wave; 'seen k of n' counts npc spawns at that tile over those runs.", ""]
    for wave in sorted(agg["wave_runs"]):
        n = agg["wave_runs"][wave]
        L.append("wave %d  (runs %d)" % (wave, n))
        for (npc, x, y), c in sorted(agg["tick0"][wave].items(), key=lambda kv: (name(kv[0][0]), -kv[1])):
            L.append("    first tick  %-13s (%s,%s)  seen %d of %d runs" % (name(npc), x, y, c, n))
        for (npc, x, y), c in sorted(agg["later"][wave].items(), key=lambda kv: (name(kv[0][0]), -kv[1]))[:12]:
            L.append("    later       %-13s (%s,%s)  seen %d" % (name(npc), x, y, c))
    L += ["", "## Attack gaps per npc type and attack",
          "Event: NPC_ATTACK(10), anchored to the npc's animation id. Gap = ticks between",
          "consecutive NPC_ATTACK events of the SAME npc (room id) in one wave. A gap that is a",
          "multiple of the cadence can be an attack the recorder did not see (an animation that",
          "restarted without changing), a pause (freeze, dig) or a retarget.", ""]
    for (npc, attack), c in sorted(agg["gaps"].items(), key=lambda kv: (name(kv[0][0]), kv[0][1] or 0)):
        L.append("%-14s %-16s %s" % (name(npc), attack_name(attack), _dist(c)))
    L += ["", "## First attack after a real (post tick 0) spawn",
          "Events: NPC_SPAWN(7) then NPC_ATTACK(10), same npc. Ticks from spawn to first attack.", ""]
    for (npc, attack), c in sorted(agg["firsts"].items(), key=lambda kv: (name(kv[0][0]), kv[0][1] or 0)):
        L.append("%-14s %-16s %s" % (name(npc), attack_name(attack), _dist(c)))
    L += ["", "## Despawn after a real spawn",
          "Event: NPC_DEATH(9), which the plugin emits on NpcDespawned, NOT when hitpoints reach 0.", ""]
    for npc, c in sorted(agg["deaths"].items(), key=lambda kv: name(kv[0])):
        L.append("%-14s lifetime %s" % (name(npc), _dist(c)))
    if GAME == "colosseum":
        L += ["", "## Spawn tick of every npc spawned AFTER tick 0 (reinforcements and adds)",
              "Event: NPC_SPAWN(7), real spawn ticks (the tick 0 batch is excluded). wave, npc: tick:n.", ""]
        for (wave, npc), c in sorted(agg["later_ticks"].items(), key=lambda kv: (kv[0][0], name(kv[0][1]))):
            L.append("wave %-2d %-18s %s" % (wave, name(npc), _dist(c)))
        L += ["", "## Manticore bursts (one animation 10869 = up to three NPC_ATTACK events, one per tick)",
              "Events: NPC_ATTACK(10) 107/114/115. The 2nd and 3rd event of a burst are ASSERTED by the",
              "plugin (PROVENANCE.md); the burst gap above is the gap between burst STARTS. Style order",
              "of each burst (mage=107 range=114 melee=115); a burst shorter than 3 lost a style read.", ""]
        for styles, n in sorted(agg["bursts"].items(), key=lambda kv: -kv[1]):
            L.append("n=%-4d %s" % (n, ",".join(attack_name(a).replace("manticore_", "") for a in styles) or "(none)"))
    L += ["", "## Wave end per run",
          "Events: the last NPC_SPAWN/ATTACK/DEATH tick of the wave in each run (a lower bound on the",
          "wave's end tick; the stage-end tick is a STAGE_UPDATE the API does not serve).", ""]
    for wave in sorted(agg["ends"]):
        v = sorted(agg["ends"][wave])
        L.append("wave %-2d n=%-2d min %-4d median %-4d max %d" % (wave, len(v), v[0], v[len(v) // 2], v[-1]))
    return "\n".join(L) + "\n"


def overview_report(docs: list[dict]) -> str:
    """Per-wave length, inter-wave gap and pillar collapse from the server's per-wave record.
    ticks = the wave's length from its start message to its end message; the gap is
    start[n+1] - (start[n] + ticks[n]); both are anchored to chat messages the client saw."""
    length = collections.defaultdict(list)
    gapc: collections.Counter = collections.Counter()
    spawn_sets = collections.defaultdict(collections.Counter)
    first_ticks = collections.defaultdict(lambda: collections.defaultdict(list))
    n = 0
    for d in docs:
        ov = d.get("overview")
        if not ov or not ov.get("inferno"):
            continue
        n += 1
        waves = ov["inferno"]["waves"]
        for i, w in enumerate(waves):
            wave = w["stage"] - STAGE_WAVE_1 + 1
            length[wave].append(w["ticks"])
            if i + 1 < len(waves) and w.get("ticksLost", 0) == 0:
                gapc[waves[i + 1]["startTick"] - (w["startTick"] + w["ticks"])] += 1
            for npc in w["npcs"].values():
                if npc["spawnTick"] > 0:
                    first_ticks[wave][npc["spawnNpcId"]].append(npc["spawnTick"])
        for stage, sp in (ov.get("spawns") or {}).items():
            key = tuple(sorted((x["npcId"], x["x"], x["y"]) for x in sp["npcs"]))
            spawn_sets[int(stage) - STAGE_WAVE_1 + 1][key] += 1
    per_run = []
    for d in docs:
        ov = d.get("overview")
        if not ov or not ov.get("inferno"):
            continue
        w = ov["inferno"]["waves"]
        g = collections.Counter(w[i + 1]["startTick"] - (w[i]["startTick"] + w[i]["ticks"]) for i in range(len(w) - 1))
        per_run.append("%s status=%s waves=%-2d last wave end %-5d game Duration %-5s gaps %s" % (
            d["uuid"][:8], d["challenge"]["status"], len(w), w[-1]["startTick"] + w[-1]["ticks"],
            d["challenge"].get("challengeTicks"), dict(g)))
    L = ["# Per-wave record (GET /challenges/inferno/<uuid>), %d challenges" % n, "",
         "CAUTION: a run whose plugin sent no INFERNO_WAVE_START (a logout mid-run sets `hasLogged`;",
         "an older plugin) has every wave start RECONSTRUCTED by the server: start of wave 1 = 10 and",
         "+6 between waves (asserted constants, `PROVENANCE.md`), so its gaps are 6 by construction.",
         "The API has no flag for it. A run with a gap other than 6, or whose last wave end equals the",
         "game's own `Duration`, carries real start events. Per run:", ""] + per_run + [""] + [
         "## Wave length in ticks (start message to end message), per wave", ""]
    for wave in sorted(length):
        v = sorted(length[wave])
        L.append("wave %-2d n=%-2d min %-4d median %-4d max %d" % (wave, len(v), v[0], v[len(v) // 2], v[-1]))
    L += ["", "## Gap between a wave's end and the next wave's start message (ticks), all waves",
          "(waves with ticksLost > 0 excluded; ALL runs, so runs with reconstructed starts count as 6)", "", _dist(gapc), "",
          "## Spawn sets (the server's spawn index: npc id, x, y of the tick-0 batch), per wave", ""]
    for wave in sorted(spawn_sets):
        L.append("wave %d" % wave)
        for key, c in sorted(spawn_sets[wave].items(), key=lambda kv: -kv[1]):
            L.append("    seen %d  %s" % (c, "  ".join("%s@%d,%d" % (name(a), b, c2) for a, b, c2 in key)))
    return "\n".join(L) + "\n"


def colosseum_overview_report(docs: list[dict]) -> str:
    """GET /challenges/colosseum/<uuid>: `splits` 152-163 are the waves' lengths in ticks (the
    game's own `Wave duration` timer, OBSERVED), 164-173 the cumulative start of waves 3-12 (a
    sum of lengths: the Minimus intermission is NOT counted), `colosseum.waves[]` the per-wave
    record (handicap options and choice, every npc with spawnTick/spawnPoint/deathTick),
    `spawns` the server's spawn index of the four indexed types (shaman, javelin, manticore,
    shockwave: blert challenge-harder colosseum.rs). A handicap id is handicap + 30 * level."""
    length = collections.defaultdict(list)
    spawn_sets = collections.defaultdict(collections.Counter)
    options = collections.defaultdict(collections.Counter)
    chosen = collections.defaultdict(collections.Counter)
    per_run = []
    n = 0
    for d in docs:
        ov = d.get("overview")
        if not ov or not ov.get("colosseum"):
            continue
        n += 1
        sp = ov.get("splits") or {}
        lens = [sp.get(str(152 + i)) for i in range(12)]
        reached = d["challenge"]["stage"] - STAGE_WAVE_1 + 1
        for i, v in enumerate(lens):
            if v is not None and not (d["challenge"]["status"] != 1 and i + 1 >= reached):
                length[i + 1].append(v)         # a wave a failed run ended in is partial: not a length
        for stage, rec in (ov.get("spawns") or {}).items():
            key = tuple(sorted((x["npcId"], x["x"], x["y"]) for x in rec["npcs"]))
            spawn_sets[int(stage) - STAGE_WAVE_1 + 1][key] += 1
        for w in ov["colosseum"]["waves"]:
            wave = w["stage"] - STAGE_WAVE_1 + 1
            for o in w.get("options") or []:
                options[wave][o] += 1
            if w.get("options"):
                chosen[wave][w.get("handicap")] += 1
        per_run.append("%s status=%s stage=%s game ticks %-5s waves %s" % (
            d["uuid"][:8], d["challenge"]["status"], d["challenge"]["stage"],
            ov.get("challengeTicks"), ",".join("-" if v is None else str(v) for v in lens)))
    L = ["# Per-wave record (GET /challenges/colosseum/<uuid>), %d challenges" % n, "",
         "Wave length = splits 152-163 = the game's `Wave duration` timer (PROVENANCE.md, STAGE_UPDATE).",
         "Per run (status 1 completed, 3 wiped):", ""] + per_run + ["",
         "## Wave length in ticks, per wave", ""]
    for wave in sorted(length):
        v = sorted(length[wave])
        L.append("wave %-2d n=%-2d min %-4d median %-4d max %d" % (wave, len(v), v[0], v[len(v) // 2], v[-1]))
    L += ["", "## Spawn index (npc id, x, y of the four indexed types), per wave", ""]
    for wave in sorted(spawn_sets):
        L.append("wave %d" % wave)
        for key, c in sorted(spawn_sets[wave].items(), key=lambda kv: -kv[1]):
            L.append("    seen %d  %s" % (c, "  ".join("%s@%d,%d" % (name(a), b, c2) for a, b, c2 in key)))
    L += ["", "## Handicap options offered per wave (id = handicap + 30 * level), count over runs", ""]
    for wave in sorted(options):
        L.append("wave %-2d %s" % (wave, "  ".join("%d:%d" % kv for kv in sorted(options[wave].items()))))
    return "\n".join(L) + "\n"


def sol_effects_report(docs: list[dict]) -> str:
    """Events 201 (doom hitsplat), 203 (reentry pools), 204-207 (Sol) of every cached wave.
    All OBSERVED except the dust pattern/direction LABELS (derived) and a grapple HIT (its
    tick is the announcement + 5 by construction): PROVENANCE.md."""
    pattern = collections.Counter(); dust_gap = collections.Counter(); first = collections.defaultdict(list)
    laser = collections.Counter(); scan_shot = collections.Counter(); pool_gap = collections.Counter()
    grapple = collections.Counter(); doom = collections.Counter(); reentry = collections.Counter()
    sol_runs = 0
    for d in docs:
        for wave_key, evs in d["waves"].items():
            for e in evs:
                if e["type"] == 201:
                    doom[int(wave_key)] += 1
                elif e["type"] == 203:
                    reentry[int(wave_key)] += 1
        evs = d["waves"].get(str(MAX_WAVES))
        if not evs:
            continue
        sol_runs += 1
        dust = [e for e in evs if e["type"] == 204]
        las = [e for e in evs if e["type"] == 207]
        pools = [e for e in evs if e["type"] == 206]
        for a, b in zip(dust, dust[1:]):
            dust_gap[b["tick"] - a["tick"]] += 1
        for e in dust:
            pattern[(e["colosseumSolDust"]["pattern"], e["colosseumSolDust"].get("direction"))] += 1
        for key, seq in (("dust", dust), ("laser", las), ("pool", pools)):
            if seq:
                first[key].append(seq[0]["tick"])
        for e in las:
            laser[e["colosseumSolLasers"]["phase"]] += 1
        for a, b in zip(las, las[1:]):
            if a["colosseumSolLasers"]["phase"] == 0 and b["colosseumSolLasers"]["phase"] == 1:
                scan_shot[b["tick"] - a["tick"]] += 1
        for a, b in zip(pools, pools[1:]):
            pool_gap[b["tick"] - a["tick"]] += 1
        for e in evs:
            if e["type"] == 205:
                g = e["colosseumSolGrapple"]
                grapple[(g["target"], g["outcome"], e["tick"] - g["attackTick"])] += 1
    L = ["# Sol Heredit and the modifier events (%d wave-12 streams)" % sol_runs, "",
         "Events 204-207 (graphics objects), 205 (chat), 201 (DOOM hitsplat), 203 (pool objects).", "",
         "dust (pattern 0 trident1 / 1 trident2 / 2 shield1 / 3 shield2, direction 0 N 1 E 2 S 3 W; LABELS derived): "
         + "  ".join("%s:%d" % (k, v) for k, v in sorted(pattern.items(), key=lambda kv: str(kv[0]))),
         "dust gap between consecutive dust events: " + _dist(dust_gap),
         "first tick: dust %s | laser %s | pools %s" % tuple(sorted(first[k]) for k in ("dust", "laser", "pool")),
         "laser events (0 scan, 1 shot): %s ; scan -> next shot gap: %s" % (dict(laser), _dist(scan_shot)),
         "pool event gaps: " + _dist(pool_gap),
         "grapple (equipment slot, outcome 0 hit 1 defend 2 parry, event tick - announcement tick): "
         + "  ".join("%s:%d" % (k, v) for k, v in sorted(grapple.items())),
         "doom hitsplats per wave (all runs): " + "  ".join("%d:%d" % kv for kv in sorted(doom.items())),
         "reentry pool events per wave (all runs): " + "  ".join("%d:%d" % kv for kv in sorted(reentry.items())),
         "totem heal events (202): " + str(sum(1 for d in docs for evs in d["waves"].values() for e in evs if e["type"] == 202))]
    return "\n".join(L) + "\n"


def cmd_overviews(args) -> int:
    got = 0
    for doc in all_cached():
        if fetch_overview(doc):
            got += 1
            sys.stderr.write("  overview %s\n" % doc["uuid"][:8])
            place(doc["uuid"])
    print("fetched %d overviews" % got)
    return 0


def cmd_summary(args) -> int:
    docs = all_cached()
    runs = [(d["uuid"], observations_from_blert(d)) for d in docs]
    if not runs:
        raise SystemExit("the cache holds no challenge; run `sample` or `fetch` first")
    waves = sum(len(d["waves"]) for d in docs)
    text = render(summarise(runs), "Blert %s sample: %d challenges, %d wave streams" % (GAME.capitalize(), len(runs), waves))
    text += "\n" + (colosseum_overview_report(docs) + "\n" + sol_effects_report(docs) if GAME == "colosseum" else overview_report(docs))
    if args.md:
        with open(args.md, "w", encoding="utf-8") as fh:
            fh.write(text)
    sys.stdout.write(text)
    return 0


def cmd_export(args) -> int:
    """Write the OBSERVED npc rows and the per-wave records of every cached challenge as two
    TSVs under sources/blert_api/, so a spec worker can query them without the (git-ignored)
    large cache files."""
    os.makedirs(DOCS_CACHE, exist_ok=True)
    docs = all_cached()
    n_ev = 0
    with open(os.path.join(DOCS_CACHE, "observed_npc_events.tsv"), "w", encoding="utf-8") as fh:
        fh.write("run\twave\ttick\tkind\tnpc_id\tnpc\troom_id\tx\ty\tattack_id\tattack\n")
        for d in sorted(docs, key=lambda d: d["uuid"]):
            for o in sorted(observations_from_blert(d), key=lambda o: (o["wave"], o["tick"], o["room"])):
                fh.write("%s\t%d\t%d\t%s\t%d\t%s\t%d\t%s\t%s\t%s\t%s\n" % (
                    d["uuid"][:8], o["wave"], o["tick"], o["kind"], o["npc"], name(o["npc"]), o["room"],
                    o["x"], o["y"], "" if o["attack"] is None else o["attack"],
                    "" if o["attack"] is None else attack_name(o["attack"])))
                n_ev += 1
    n_w = 0
    if GAME == "colosseum":
        with open(os.path.join(DOCS_CACHE, "wave_records.tsv"), "w", encoding="utf-8") as fh:
            fh.write("run\tstatus\twave\tticks\tticks_lost\toffset\thandicap_id\toptions\tgame_duration_ticks\n")
            for d in sorted(docs, key=lambda d: d["uuid"]):
                ov = d.get("overview") or {}
                sp = ov.get("splits") or {}
                for w in (ov.get("colosseum") or {}).get("waves", []):
                    wave = w["stage"] - STAGE_WAVE_1 + 1
                    fh.write("%s\t%s\t%d\t%s\t%s\t%s\t%s\t%s\t%s\n" % (
                        d["uuid"][:8], d["challenge"]["status"], wave, sp.get(str(151 + wave), ""),
                        w.get("ticksLost", ""), w.get("offset", ""), w.get("handicap", ""),
                        ",".join(str(o) for o in w.get("options") or []), d["challenge"].get("challengeTicks")))
                    n_w += 1
        print("wrote %d npc rows, %d wave rows" % (n_ev, n_w))
        return 0
    with open(os.path.join(DOCS_CACHE, "wave_records.tsv"), "w", encoding="utf-8") as fh:
        fh.write("run\tstatus\twave\tticks\tstart_tick\tticks_lost\tgame_duration_ticks\n")
        for d in sorted(docs, key=lambda d: d["uuid"]):
            ov = d.get("overview") or {}
            for w in (ov.get("inferno") or {}).get("waves", []):
                fh.write("%s\t%s\t%d\t%d\t%d\t%d\t%s\n" % (
                    d["uuid"][:8], d["challenge"]["status"], w["stage"] - STAGE_WAVE_1 + 1, w["ticks"],
                    w["startTick"], w["ticksLost"], d["challenge"].get("challengeTicks")))
                n_w += 1
    print("wrote %d npc rows, %d wave rows" % (n_ev, n_w))
    return 0


def cmd_ticklog(args) -> int:
    sys.stdout.write(render(summarise([(args.path, read_ticklog(args.path))]), "Our server: %s" % args.path))
    return 0


def cmd_list(args) -> int:
    status = int(args.status) if args.status.isdigit() else STATUS[args.status]
    for r in list_challenges(status, args.limit, args.stage_ge):
        print("%s status=%s stage=%s(wave %d) ticks=%s start=%s deaths=%s" % (
            r["uuid"], r["status"], r["stage"], r["stage"] - STAGE_WAVE_1 + 1,
            r.get("challengeTicks"), r.get("startTime"), r.get("totalDeaths")))
    return 0


def cmd_fetch(args) -> int:
    cached = load_cached(args.uuid)
    row = cached["challenge"] if cached else {"uuid": args.uuid, "stage": STAGE_WAVE_1 + MAX_WAVES - 1}
    print(fetch_challenge(row, args.waves))
    return 0


def cmd_sample(args) -> int:
    if args.late_wave is None:
        args.late_wave = 31 if GAME == "inferno" else 9
    done = list_challenges(STATUS["completed"], args.completed)
    late = list_challenges(STATUS["wiped"], args.failed, stage_ge=STAGE_WAVE_1 + args.late_wave - 1)
    chosen = {r["uuid"]: r for r in done + late}
    print("sample: %d completed + %d failed at wave >= %d" % (len(done), len(late), args.late_wave))
    for r in chosen.values():
        print("  %s status=%s stage=%s" % (r["uuid"], r["status"], r["stage"]))
    for r in chosen.values():
        print("fetched", r["uuid"], "->", fetch_challenge(r))
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", choices=("inferno", "colosseum"), default="inferno")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("list")
    p.add_argument("--status", default="completed")
    p.add_argument("--limit", type=int, default=12)
    p.add_argument("--stage-ge", type=int, default=None, dest="stage_ge")
    p.set_defaults(fn=cmd_list)
    p = sub.add_parser("fetch")
    p.add_argument("uuid")
    p.add_argument("--waves", default=None, help="e.g. 1-9,25")
    p.set_defaults(fn=cmd_fetch)
    p = sub.add_parser("sample")
    p.add_argument("--completed", type=int, default=12)
    p.add_argument("--failed", type=int, default=6)
    p.add_argument("--late-wave", type=int, default=None, dest="late_wave",
                   help="a failed run is 'late' when it reached at least this wave")
    p.set_defaults(fn=cmd_sample)
    p = sub.add_parser("export")
    p.set_defaults(fn=cmd_export)
    p = sub.add_parser("overviews")
    p.set_defaults(fn=cmd_overviews)
    p = sub.add_parser("summary")
    p.add_argument("--md", default=None)
    p.set_defaults(fn=cmd_summary)
    p = sub.add_parser("ticklog")
    p.add_argument("path")
    p.set_defaults(fn=cmd_ticklog)
    args = ap.parse_args()
    configure(args.game)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
