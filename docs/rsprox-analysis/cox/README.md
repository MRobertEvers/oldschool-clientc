# Chambers of Xeric: what Jagex's server actually sends

Findings read from **RSProx recordings** of real OSRS sessions. RSProx is a proxy that records every packet between the client and Jagex's server. These recordings answer what the wiki and the guides leave open: exact max hits and damage shapes, projectile timing and heights, sound ids, animation order, and message text.

Each room's file states the recorded behaviour, the recording ticks it was read from, and how our content compares (with the fix, where one was made).

| File | Room |
|---|---|
| [olm.md](olm.md) | The Great Olm |
| [guardians.md](guardians.md) | Guardians |
| [tekton.md](tekton.md) | Tekton |
| [vespula.md](vespula.md) | Vespula |
| [vasa.md](vasa.md) | Vasa Nistirio |
| [vanguards.md](vanguards.md) | Vanguards |

## Sources

All recordings are submissions to the public archive, [rsprox.net/database](https://rsprox.net/database).

| Submission | Revision | World | Raid | Used for |
|---|---|---|---|---|
| 820 | 234 | Grid Master | a trio, then a four-man raid | Guardians (party), Olm (party), Tekton and Vespula (the whole fights), Vasa (party), teleport swaps |
| 3282 | 234 | Grid Master | solo, normal | Guardians (solo), Olm, Vasa |
| 843 | 234 | Grid Master | solo, normal | Olm, Vasa, Vespula (the start, the clear) |
| 3285 | 234 | Grid Master | solo, normal | Olm (the fullest solo kill), Vasa |
| 3389 | 235 | Grid Master | solo, **Challenge Mode** | excluded from damage numbers; Vespula's CW room |

**Caveats:**
- **Grid Master** is a seasonal world type. Its perks can change what the raider deals and takes. Monster damage read here is consistent across the recordings and with the wiki's ordering, but treat a single outlier with suspicion.
- **Party size** is the `raids_client_partysize` varbit (5424), and `raids_client_highestcombat` (12285) gives the scaling target. Challenge Mode shows as "Your completed Chambers of Xeric Challenge Mode count is" at the end.
- **Coordinates inside instances:** in the Guardians room, NPC coordinates and the player's sit in different frames. Classify hits there by timing, not by tile. The Olm chamber is consistent: the raider, the claws and the graphics share one frame.
- **Heights:** the recordings use the v2 projectile packet, which carries raw heights (e.g. 355). Our server sends the older packet, whose height byte the client multiplies by 4, so recorded heights are divided by 4 for our scripts.

## Tools

| Tool | Purpose |
|---|---|
| `tools/cox/rsprox_fetch.py` | Search the archive by content and fetch recordings, cached (the NAS mirror first) |
| `tools/rsprox_mirror.py` | A polite mirror of the whole archive to the NAS (`/Volumes/MatthewLLM_Shared/rsprox_archive/`) |
| `tools/cox/rsprox_guardians.py` | Guardians: every hit on the raider, classed stomp or swing |
| `tools/cox/rsprox_room.py` | One room in TEMPLATE tiles: undoes the transcriber's mirrored chunk offsets in a quarter-turned room, and a turned npc's footprint corner |
| `tools/cox/rsprox_vespula.py` | Vespula: layout, her stings and enrage, the grubs, the drains, the clear |
| `tools/cox/rsprox_tekton.py` | Tekton: the session clock, the strike by style and held facing, the anvil, sparks, death (corrects a turned room's npc coordinates) |
| `tools/cox/rsprox_hits.py` | Every hit on the raider, matched to the tells landing with it |
| `tools/cox/rsprox_vanguards.py` | Vanguards: the rise, every volley's projectiles against the raider, the shuffles (reset or timer, walk, heal, emergence), the clear |
| `tools/cox/rsprox_vasa.py` | Vasa: the special tick by tick, boulders against the raider's tile, the crystal cycle, the walk, the stomp, the death |
| `tools/cox/rsprox_olm.py` | Olm's area hits (bombs, falling crystals) against distance |
| `tools/cox/rsprox_olm_tells.py` | Inventory of the Olm fight's animations, projectiles, graphics, sounds and messages |
| `tools/cox/rsprox_sound_pairs.py` | Which event each area sound belongs to (same tick, same tile) |

Transcribing a recording needs an RSProx checkout built with JDK 21 (Gradle 8.7 does not run under 24):

```sh
JAVA_HOME=/Library/Java/JavaVirtualMachines/temurin-21.jdk/Contents/Home ./gradlew :proxy:classes   # in ~/Documents/git_repos/rsprox
python3 tools/cox/rsprox_fetch.py get 3285          # -> ~/.rsprox/binary/archive/3285.bin
java -cp "$(cat proxy.classpath)" net.rsprox.proxy.cli.TranscribeCommandKt -name archive/3285   # -> 3285.txt
```

A searchable index of every recording, with content search like rsprox.net's, is [MRobertEvers/rsprox-index](https://github.com/MRobertEvers/rsprox-index). It runs locally on `127.0.0.1:8370` and stores its recordings on the NAS.

## Sound ids

Recordings give numeric sound ids. Our names are in `OSRS-Content/osrs239-content/pack/4_soundeffects.pack` (`id=name`).
