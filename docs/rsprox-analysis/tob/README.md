# Theatre of Blood: what Jagex's server actually sends

Findings read from **RSProx recordings** of real OSRS sessions. RSProx is a proxy that records every packet between the client and Jagex's server. These recordings answer what the wiki, Blert and Near-Reality leave open: animation delays, projectile timing and heights, which sounds the server sends and when, overhead text, dialogue, and the order of a phase change tick by tick.

Each room's file states the recorded behaviour, the ticks it was read from, and how our content compares (with the fix, where one was made).

| File | Room |
|---|---|
| [bloat.md](bloat.md) | The Pestilent Bloat, Normal Mode (30 kills; [bloat.out](bloat.out) is the full tool output) |
| [maiden.md](maiden.md) | The Maiden of Sugadinti, Normal Mode (31 kills; [maiden.out](maiden.out) is the full tool output) |
| [nylocas.md](nylocas.md) | The Nylocas, Normal Mode (30 kills; [nylocas.out](nylocas.out) is the full tool output) |
| [sotetseg.md](sotetseg.md) | Sotetseg, Normal Mode (28 kills, 56 mazes; [sotetseg.out](sotetseg.out) is the full tool output) |
| [verzik.md](verzik.md) | Verzik Vitur, Normal Mode |
| [xarpus.md](xarpus.md) | Xarpus, Normal Mode (28 kills; [xarpus.out](xarpus.out) is the full tool output), including the tick a spit's target is locked |
| [doors.md](doors.md) | Every door's landing tile, the HUD transitions and the entrance, barrier and exit dialogue |

## Sources

All recordings are submissions to the public archive, [rsprox.net/database](https://rsprox.net/database). Search it by content (`python3 tools/cox/rsprox_fetch.py search npc 8374`) and combine conditions with `mode: ALL` to find a room.

| Submission | Revision | World | Verzik kills | Party |
|---|---|---|---|---|
| 3299 | 234 | Grid Master | 5 | duo |
| 3285 | 234 | Grid Master | 1 | solo |
| 788 | 234 | Grid Master | 1 | duo |
| 1833 | 227 | Leagues V | 7 | |
| 1764 | 227 | Leagues V | 4 | |
| 553, 1779, 1837 | 227 | Leagues V | 3 each | |
| 582 | 227 | Leagues V | 1 | |

28 Normal Mode kills in all. 1163 (rev 237, Leagues VI) does not transcribe with our RSProx checkout. 1852 holds Hard Mode, and 554 and 101 hold Entry Mode; none of the three is read here.

**Caveats:**
- **Seasonal worlds.** Every recording is a Leagues or Grid Master world, so relics change the raiders' damage and speed. They are not known to change what the boss sends. Grid Master and Leagues tells (`league_guardian`, `fx_khopesh_*`, `sanguinesti_staff_heal` on Verzik, `thunder` sounds) are left out.
- **Rev 227 has no names.** Its transcripts print bare ids. `tools/rsprox_tells.py` names them from OSRS-Content's compacks, and every claim here was cross-checked against the named rev-234 sessions.
- **Heights.** Rev 234 records the v2 projectile packet, which carries raw heights (400). Rev 227 and our server send the older packet, whose height byte the client multiplies by 4. Projectile heights below are therefore in **our units** (raw ÷ 4). Spotanim heights are raw in every revision, and so are ours.
- **Instance coordinates** print mapped back to the template. ToB rooms are not rotated, so tile offsets read directly.

## Tools

| Tool | Purpose |
|---|---|
| `tools/rsprox_tells.py` | Everything sent during a fight window (npc and player animations, graphics, projectiles, sounds, locs, messages, varbits), as counted signatures or a `--timeline`. With `--ours <ticklog.tsv>...` it lines our server's scriptrun tick log up against the recordings, kind by kind. |
| `tools/rsprox_verzik_throws.py` | Verzik: every throw of a raider, from a transcript with every player shown - the P2 slam and stomp (swing offset, exactmove, hit, the stun), the pillar collapse against every raider near it, the P3 web arrival, and her P3 steps onto raiders |
| `tools/rsprox_nylocas.py` | Follows each npc through its life in the Nylocas room: deaths and self-destructs, support bites, projectile flight against the thrower's footprint, Vasilias' spawn and form changes, collapses, wrong-style hits, the room's end |
| `tools/rsprox_bloat.py` | The Bloat room: her sleep cycle and the stomp (shakes, sound, reset), the falling flesh against the players and its splats, the flies, the stun, her death |
| `tools/rsprox_maiden.py` | The Maiden room: her attack clock and both projectiles' flight against distance, the blood throw's shape and timing, the trail, the crabs and blood spawns through their lives, her death |
| `tools/rsprox_sotetseg.py` | The Sotetseg room: his attacks, the ball and its bounces against distance, the death ball, the maze's start and end, the realm's drain, wrong tiles, the tornado, his death |
| `tools/cox/rsprox_fetch.py` | Search the archive and fetch recordings, cached (the NAS mirror first) |

```sh
A=~/.rsprox/binary/archive
python3 tools/rsprox_tells.py --anchor 8369-8375 $A/{3299,3285,788}.txt                  # Verzik, recorded
python3 tools/rsprox_tells.py --anchor 8369-8375 --timeline $A/3285.txt                  # one kill, tick by tick
python3 tools/rsprox_tells.py --anchor 8369-8375 $A/{3299,3285,788}.txt --ours runs/*/ticklog.tsv   # vs ours
```

`--anchor` is the npc ids that define the fight (8369-8375 is every Normal Verzik form). Our side comes from a scriptrun session (`--session DIR` writes `ticklog.tsv`). Run the room with:

```sh
./src/build_opt/torirsserver --scriptrun test/raids/tob_verzik.lua --bots 3 --name sa --ticks 8000 \
    --session $S/sa --fixture test/raids/fixtures/fresh_lumbridge.ini
```
