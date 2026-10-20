# The Inferno: what Jagex's server actually sends

Findings read from **RSProx recordings** of real OSRS sessions. The method and caveats are the same as for [Chambers of Xeric](../cox/README.md). Each file gives the recorded behaviour and the ticks it was read from. It then compares our content against it, with the fix where one was made.

| File | Fight |
|---|---|
| [zuk.md](zuk.md) | TzKal-Zuk (wave 69): the cutscene, Zuk, the Ancestral Glyph, the Zuk-phase JalTok-Jad and Yt-HurKot, Jal-MejJak, the kill and the exit. It also covers the wave-end message and jingle shared by every wave. |
| [mobs.md](mobs.md) | Waves 1–68: every mob's attack (projectile, sounds, graphics), hit/death/defend, the meleer's dig, the mager's resurrection, falling supports, and the jump-in |

## Sources

The public archive ([rsprox.net/database](https://rsprox.net/database)) holds four recordings with Zuk in them (NPC 7706). All four are boosted accounts, and none lasts long enough to see a JalZek/JalXil set spawn. Another 39 sessions in the local archive hold Inferno waves without Zuk (Leagues V and Grid Master: entries, deaths, pauses, logouts, pillar falls); [mobs.md](mobs.md) lists them in its 2026-10-19 section.

| Submission | Revision | World | What it holds |
|---|---|---|---|
| 3299 | 234 | Grid Master | Waves 1–69 in one run, with names. A 75-tick Zuk kill. The only recording with the whole wave 68 → 69 cutscene, the kill and the exit dialogue. |
| 1754 | 227 | Leagues V | The longest Zuk fight: 388 ticks, with Jad, the Jad healers, the Mejjak healers (heals, and volleys after being attacked) and 99 Zuk shots. It carries numeric ids only. |
| 199 | 227 | Leagues V | Logs in at wave 69. A 143-tick kill, including a Zuk shot that splashed. |
| 198 | 227 | Leagues V | Waves 66–68; it logs out 10 ticks into wave 69. |

**Caveats:**
- **Boosted damage.** Leagues relics and Grid Master perks kill Zuk 3–15× faster than normal. So these recordings say nothing about how long a fight lasts or when the set spawns. They do record Zuk's own cadence, shots, graphics, sounds and messages, and those agree across all three.
- **Projectile packets.** Rev 227 sends `map_projanim_v1`, whose heights are already in our wire units (128). Rev 234's `map_projanim_v2` sends raw heights (512), so divide by 4.
- **Coordinates.** The arena's recorded coordinates are the template's (x 2240–2303, z 5312–5375), so local = world − (2240, 5312).
- **Names.** Rev 227 transcripts show ids only. `tools/rsprox/rsprox_tells.py` names them per type from our own `configs/all.<type>.compack`.

## Tools

| Tool | Purpose |
|---|---|
| `tools/rsprox/rsprox_tells.py` | Any RSProx transcript, as an inventory or an ordered `--timeline` of NPC sequences, spotanims, hits, projectiles (all parameters), map graphics, sounds, loc changes and animations, and messages |
| `tools/rsprox/rsprox_attack_tells.py` | Everything sent on one mob's attack tick and the ticks after, attributed per mob type, with projectile source offsets and distances |
| `tools/rsprox/ticklog_tells.py` | The same tells, read from our scriptrun tick log (`<session>/ticklog.tsv`), for side-by-side comparison |
| `test/waves/inferno_mob_tells.lua` | The jump-in (`::infernojump`), waves 1/9/35/62, a forced resurrection (`::infernorevive`) and a dig, for the tick log; `::infernodig` forces a dig |
| `test/waves/inferno_zuk_tells.lua` | A scriptrun that plays wave 68 → Zuk for the tick log: it kills the wave with `::infernowavekill`, plays the cutscene, walks Zuk over the thresholds with `::zukhp`, then kills him and runs the exit |

```sh
python3 tools/rsprox/rsprox_tells.py ~/.rsprox/binary/archive/3299.txt --from 13630 --to 13760 --npc inferno_ --timeline
./src/build_<objdir>/torirsserver --scriptrun test/waves/inferno_zuk_tells.lua --bots 1 --name zuk --ticks 1500 \
    --session $S/zuk --fixture test/raids/fixtures/fresh_lumbridge.ini
python3 tools/rsprox/ticklog_tells.py $S/zuk/ticklog.tsv --from 40 --to 80
```
