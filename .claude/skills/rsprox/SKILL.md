---
name: rsprox
description: Use RSProx recordings (real Jagex-server packet captures) from the RSProx index server on this network to find, transcribe and analyse what OSRS actually sends — npc anims, projectiles, sounds, locs, varbits, messages, timings — and to audit or rebuild our content against it. Trigger on "RSProx", "the index server", "use the recording(s)", "the most recent recording", "what does Jagex send", or any mechanic/room/visual audit where recorded data could decide the answer.
---

# RSProx data from the index server

RSProx recordings are captures of real Jagex sessions. **They are authoritative.**
They outrank the wiki, Near-Reality, our spec docs and our code wherever they show
something. A behaviour that no recording shows falls back to those sources and is
labelled *unverified*.

## 1. Find sessions

The index server is `rsprox-index` at `http://127.0.0.1:8370` (LAN:
`http://192.168.1.148:8370`; set `RSPROX_INDEX_URL` when working from another
machine). It holds every rsprox.net submission plus the owner's own recordings.
Everything goes through one tool:

```sh
T=tools/rsprox/rsprox_index.py
python3 $T stats                         # is it up, anything queued/failed?
python3 $T latest 5                      # newest uploads = "the most recent recording"
python3 $T search npc 7569               # sessions holding an id
python3 $T search seq 8056 npc 8360      # ALL conditions (add --any for ANY)
python3 $T search message_game "shunts you"   # MESSAGE_GAME / TEXT: case-insensitive substring
python3 $T search mapsquare 12889        # every session that entered a map square
python3 $T counts 4286 npc               # one session's index rows
```

The search types are `npc obj loc mapsquare message_game varp varbit interface seq
spotanim midi synth jingle clientscripts text ambience`.

Search rules:
- **Enumerate a content area by `mapsquare`** over all of its squares, not by npc or obj. Content search missed sessions that the square search found (the CoX skilling survey).
- Search finds only `indexed` sessions. A `queued` or `failed` one is invisible to search but still transcribes, so check `latest` and `stats` before concluding "0 results".
- A result row shows `<index id>  net <rsprox.net id>  rev  world  date  status  activity  description  [matches]`. The **activity** tells you whether a session is seasonal: "Leagues V", "Grid Master:…", or `-` / a place name for a normal world.

## 2. Transcribe

```sh
python3 $T get 4286                      # -> path of the transcript (cached after the first time)
python3 $T get net:820                   # by rsprox.net submission number (what the docs cite)
python3 $T get 820 3389 --all-players    # every raider's movement/seqs (party rules)
python3 $T get net:820 --raw             # untranslated instance tiles (player<->npc distances)
python3 $T get 4286 --bas                # player BAS + worn equipment (stances, weapons)
```

Transcripts persist in `~/.rsprox-index-cache/home-<variant>/.rsprox/binary/archive/idx<id>.txt`,
shared across sessions. **Never transcribe into `~/.rsprox`**, and never re-make
a scratchpad home: the cache already has every variant. `--force` re-transcribes.
The transcriber is the index's own RSProx clone (osrs-223..241, JDK 21). A transcription
takes seconds.

**IDs:** the index numbers its sessions itself. A session imported from rsprox.net
has `source rsprox.net:N`, and N ≠ its index id (index 819 = rsprox.net 820). The
older shared transcripts `~/.rsprox/binary/archive/<N>.txt` use **rsprox.net**
numbers, so `archive/4288.txt` is a public rev-239 recording, while index 4288 is
the owner's rev-241 Tutorial Island. In write-ups, cite **"index 4286"** or
**"rsprox.net 820"**, never a bare number.

## 3. Read what was on the wire

Generic tools (all take transcript paths):

| tool | use |
|---|---|
| `tools/rsprox_tells.py --anchor <npc ids> <txt>... [--kind proj] [--timeline]` | full inventory of a fight window: npc/player seqs, spotanims, projectiles (all params), map anims, area/synth sounds, locs, messages, varbits, cam shakes, hits |
| `tools/rsprox_tells.py --anchor <ids> <txt>... --ours <ticklog.tsv>` | R-only / O-only diff against our scriptrun tick log |
| `tools/rsprox/rsprox_tells.py <txt> --from T --to T [--timeline]` | the same, without an anchor npc (skilling, quests, minigames) |
| `tools/rsprox/ticklog_tells.py <ticklog.tsv>` | our tick log as the same named timeline |
| `tools/rsprox/rsprox_attack_tells.py` | what each npc type's attack sends (tick-attributed) |
| `tools/cox/rsprox_sound_pairs.py <txt>` | which event each area sound belongs to |
| `tools/rsprox/rsprox_timeline.py <txt> <t0> <t1>`, `rsprox_pages.py`, `rsprox_annotate.py` | quest/dialogue: ops, pages, choices, varbits; names rev-227 ids |
| `tools/rsprox_frame_check.py <translated> <raw>` | which room cells a translated transcript measures right |
| `tools/cox/rsprox_room.py` | one CoX room in template tiles (fixes quarter-turn mirroring) |

Per-content parsers are in `tools/rsprox_*.py`, `tools/cox/rsprox_*.py` and `tools/rsprox/`
(Nylocas/Maiden/Bloat/Sotetseg/Xarpus/Verzik, ToA rooms, Wintertodt, scythe, ancients, TD, ...).
**Read the existing write-up first**: `docs/rsprox-analysis/<topic>/` (cox, toa, tob, inferno,
wintertodt, canoes, quests, combat, ancients, scythe, ...). It names the sessions, the tool,
and what is already settled.

## 4. Reading traps (each one has cost a wrong fix)

- **Recorded ids can be OLDER revisions** (223–241; our cache is 241 since 2026-10-20, `osrs-content/meta.ini`). Jagex reuses freed slots. Look up the 241 name in `OSRS-Content/osrs-content/configs/all.<type>.compack` before using any id. seq 579 is a chathead in the old recordings but an axe special in our cache. Rev-227 transcripts print bare ids. Sound names are in `OSRS-Content/osrs-content/pack/4_soundeffects.pack`.
- **Transcript format differs by transcriber.** The index's RSProx (all `get` output) prints npc adds as `coord=(...)), spawnangle=east` and faces as `[facing] coord=`. Older shared transcripts printed `angle=1536`. `rsprox_tells.py` handles both. If an older per-room tool returns nothing on a `get` transcript, check its regex against both formats.
- **Only the recorder's player is shown** by default (seqs, graphics, movement). Never conclude something is private from that. Use `--all-players` for party behaviour.
- **Instance tiles are translated to the template**, and in a quarter-turned room that mirrors offsets inside each 8x8 chunk. A large npc in a 180° room prints the wrong corner. Measure anything between a player and an npc on `--raw`.
- **Seasonal recorders** (Leagues V, Grid Master) attack every 2 ticks, take about 10% less damage, and have perks (Last Stand). An "every 2 ticks" npc loop may be the player's attack cadence. Line it up with the recorder's attack ticks, and confirm on a normal-world session. Party size: varbit 5424 `raids_client_partysize`. rsprox.net 820 is scaled as 54 players.
- **A shared projectile id** fired by two npc types: attribute it by the shooter's attack seq on that tick (ticks where only one type attacked) plus the `[source] coord=` tile.
- **Click ticks jitter by one** (the proxy files a click when it saw it). Time a mechanic from the server's first answer, never from the click.
- **Projectile timing:** `starttime`/`endtime` are client cycles (30 per tick). Projectile heights: a `map_projanim_v1` line (rev up to ~233) prints our script units already; a v2 / `projanim_specific_v3` line (rev 234+) prints raw heights, 4x ours (divide by 4). Spotanim heights are raw everywhere, ours too. A landing read on the arrival row is a tick early (`F.tick + (cycles_left + 45) // 30`).
- Rev 227 sends no varp 83. Prayers are varbit 4101 `prayer_allactive` (cache bit order).

## 5. Auditing our content against it

To IMPLEMENT or rebuild content from a recording (ops, maplinks, locs, projectiles, sounds, interfaces, npc info, dialogues, movement), use the `rsprox-content` skill: `tools/rsprox/rsprox_interact.py` traces one interaction tick by tick, and `.claude/skills/rsprox-content/PACKETS.md` explains every packet.


1. Run the full `rsprox_tells.py --anchor … --ours …` inventory, then walk **every** section: loc_add shape/rotation, varp/varbit (incl. `hpbar_hud_*`), synth and area sounds, npc seq/spotanim, obj despawn, messages. Explain every R-only and O-only row as a fix, the recorder's kit, or rate. Do not extract only the events you went looking for.
2. `--ours` blind spots: it never compares `proj`, `sound_area` or `synth_sound` (compare `sound`/`proj_core`). Its window is "anchor npc seen", so grep the tick log for room start/clear events. Our tick log has no message, mesbox or interface rows.
3. Also check, per room: entry `loc_add_change` at E+0..3 against our `loc_set`; each npc's recorded death seq and defend sound against ours; whether the content is wired into shared hooks (`[playerdeath,_]`); party movement lines; every recorded message line. Delete invented messages: no recording, no line.
4. Fix the side that is wrong (a server bug in the server). Diagnose visual complaints from the transcript and tick log, not by running the client.
5. Write it up in `docs/rsprox-analysis/<topic>/README.md`. Cite the session ids (with the "index"/"rsprox.net" prefix), the tick numbers `[n]`, the tool, and the counts behind each rule ("37/37"). Keep a ledger row per defect (e.g. `toa/AUDIT.tsv`).

## 6. When the server misbehaves

- Source `~/Documents/git_repos/rsprox-index`, data `~/rsprox-index-data`, blobs on the NAS (`/Volumes/MatthewLLM_Shared/rsprox_index_blobs`), log `~/rsprox-index-data/server.log`. There is no LaunchAgent, so a reboot leaves it down. Restart from the rsprox-index checkout with 3draster's `.venv` python:
  `python -m rsprox_index serve --storage ~/rsprox-index-data --blobs /Volumes/MatthewLLM_Shared/rsprox_index_blobs --classpath ~/rsprox-index-data/rsprox.classpath --host 0.0.0.0 --port 8370 >> ~/rsprox-index-data/server.log 2>&1`
- If `stats` shows sessions stuck `queued` and `failed` growing with FileNotFoundError, run `mkdir -p ~/rsprox-index-data/indexer/.rsprox/binary/q`.
- `~/rsprox-index-data/rsprox-upstream` carries **local fixes** on its branch `local-indexer-fixes` (rev 237+ hit/headbar/face blocks, rev 241 clientscript array args, a decode-error hook). Never `git pull`/`checkout` there; rebase per `rsprox-index/PATCHES.md`. The server and this client both use `~/rsprox-index-data/rsprox.classpath`, a build of that branch made by `rsprox-index/scripts/build_rsprox.sh` in `rsprox-upstream-builds/<sha>`.
- A session that cannot be indexed is `failed` with a one-line `error` and its header (revision/world) still stored: an RS3 recording is refused by name; a brand-new revision says `RSProx has no decoder for revision N`. Then upstream RSProx needs updating (rebase + rebuild with JDK 21), so say so rather than guessing.
- Packets that fail to decode or index are skipped, and the rest of the session is still indexed. They show as `packet_failures` on `/api/sessions/<id>` and across sessions at `/api/failures`. A non-empty list there means RSProx cannot read part of that revision yet.
- The public archive is rsprox.net/database (`tools/cox/rsprox_fetch.py`, NAS mirror `/Volumes/MatthewLLM_Shared/rsprox_archive`). The index already imports it, so prefer the index.
