# Ancient Magicks: what Jagex's server sends for a cast

These findings come from **RSProx recordings** of real OSRS sessions. `tools/rsprox_ancients.py` read 2,520 casts made by the recording player across 155 transcripts (revisions 223–239), plus every Ancient flight, impact graphic and impact sound that other players' casts put on npcs in the same sessions.

`magic_combat_spells.dbrow` and `player_magic.rs2` now follow these numbers. Before this pass the sixteen rows came from Near-Reality's `CombatSpell` table, and no Ancient spell played a sound.

## What every Ancient spell sends

| Tell | Content | Was |
|---|---|---|
| Caster seq | rush/blitz `zaros_casting_walkmerge` (10091), burst/barrage `zaros_vertical_casting_walkmerge` (10092) | 1978 / 1979, which only rev 224–229 send |
| Cast sound | `synth_sound` to the caster, delay 0: `ice_cast` 171, `blood_cast` 106, `shadow_cast` 178, `smoke_cast` 183 | none |
| Flight | start 51, **angle 16**, progress 64, start height 43, end cycle `46 + 10 × tiles` | angle 23 |
| Impact graphic | on each npc struck, delayed to the flight's end cycle | same |
| Impact sound | `sound_area` at each struck npc's tile, range 10, delayed to the flight's end cycle; one per victim | none |
| Hitsplat | `ceil(end / 30)` ticks after the cast (end 56 → +2, 66–86 → +3, 96–116 → +4) | same |
| Splash | `failedspell_impact` (85) at height **124** on the npc + `sound_area spellfail` (227), both on the landing cycle; a missed burst/barrage secondary gets the same | height 92, plus a map graphic Jagex never sends |

The splash holds for every spell type in the recordings (fire bolts, tridents, the sanguinesti staff and others: about 2,700 cases), so the fix to `~pvm_spell_fail` covers the standard spellbook too. 31 splashes on oversized npcs were drawn higher (178, 248).

## Per spell

| Spell | Flight graphic (end height) | Impact graphic (height) | Impact sound | Evidence |
|---|---|---|---|---|
| Smoke Rush | 384 (31) | 385 (124) | `smoke_rush_impact` 185 | 2540, 2542 (an npc casting at the recorder); 1768 |
| Smoke Burst | 388 (31) | 389 (124) | `smoke_burst_impact` 182 | 1703 |
| Smoke Blitz | 386 (31) | 387 (124) | `smoke_blitz_impact` 181 | 83, 1768 (other casters) |
| Smoke Barrage | 390 (31) | 391 (124) | `smoke_barrage_impact` 180 | 4029, 3273 (other casters) |
| Shadow Rush | 378 (0) | 379, **never seen landing** | `shadow_rush_impact` 179 | 1768, one flight |
| Shadow Burst | none | 382 (0) | `shadow_burst_impact` 177 | 1703 |
| Shadow Blitz | 380 (0) | 381, **never seen landing** | `shadow_blitz_impact` 176 | 1768, one flight |
| Shadow Barrage | none | 383 (0) | `shadow_barrage_impact` 175 | 1703, 100, 1153 |
| Blood Rush | none | 373 (0) | `blood_rush_impact` 110 | 552 |
| Blood Burst | none | 376 (0) | `blood_burst_impact` 105 | 1703, 1836 |
| Blood Blitz | 374 (0) | 375 (0) | `blood_blitz_impact` 104 | 552, 1224, 1433, 3277 |
| Blood Barrage | none | 377 (0) | `blood_barrage_impact` 102 | 676 own casts |
| Ice Rush | 360 (0) | 361 (0) | `ice_rush_impact` 173 | 6 own casts |
| Ice Burst | **366** (0) | 363 (0) | `ice_burst_impact` 170 | 106 own casts, every one with the flight |
| Ice Blitz | none; 366 on the caster at 124 | 367 (0) | `ice_blitz_impact` 169 | 450 own casts |
| Ice Barrage | 368 (0) | 369 (0) | `ice_barrage_impact` 168 | 1,230 own casts |

Ice Burst was the one row with a wrong graphic: Near-Reality throws nothing, and the recordings always show 366 flying. The compack's names for 363 and 367 are swapped against the casts: Ice Burst lands 363 (`ice_blitz_impact`) and Ice Blitz lands 367 (`ice_burst_impact`). The rows already had that right.

### Gaps

- **Shadow Rush / Shadow Blitz impact height.** Their only flights (1768) struck npcs outside the recorder's view, and no session in the archive shows graphic 381 on an npc. The rows keep 0, like the other shadow spells. This is unverified.
- **Smoke Rush / Smoke Blitz caster seq.** These were cast only by others, whose seqs a local-only transcript hides. The tier rule above (rush/blitz on 10091) is inferred from the other twelve spells.
- **Content specials are not the generic spell.** On Desert Treasure II's totems and the Leviathan, the impact draws at 124 and its sound comes as a delayed synth to the caster (100, 1153, 1703). That is boss content, not the spellbook.

## Stance (BAS) of sceptres and wands

Re-transcribing with `PLAYER_APPEARANCE_BAS` and `PLAYER_APPEARANCE_EQUIPMENT` on (they are off by default) prints each appearance's worn weapon and base animation set. The full table is in [weapon_bas.txt](weapon_bas.txt).

The **ancient sceptre** (529 appearances; the blood sceptre 11,439), the **kodai wand** (19), the drygore blowpipe, the eye of ayak, the twinflame staff, the purging staff and the rat bone staff all stand on the staff set: ready `human_staffready`, turn `human_halberdturnonspot`, walk `human_halberdwalk_f/b/l/r`, run `human_halberdrunning`. The generated layer (`server_fields.generated.obj`) had all of them on the unarmed set. The authored overlay is now `skill_combat/configs/bas/sceptres_wands.obj`. It also covers the unrecorded members of each family: the other sceptres, the Mage Training Arena wands, the 3rd age and dragon hunter wands, which follow the kodai wand.

**Not every staff takes the staff stance.** The Guthix, Saradomin and Zamorak staves are recorded on the unarmed set (3344), and the generated layer is right for them.

The recordings also show weapons outside this pass whose stance we get wrong. These are not fixed:

| Weapon | Recorded | Ours |
|---|---|---|
| adamant / mithril 2h sword | `human_dh_weapon_ready` / `_walk` / `_run` | unarmed set |
| steel pickaxe | unarmed set | staff set |
| hunting butterfly net | staff set | unarmed set |
| midsummer fan | `human_ready_fan` set | unarmed set |
| thunder khopesh (Leagues) | `league_5_khopesh_*` | unarmed set |

## Reproducing

Saved outputs: [rsprox_ancients.txt](rsprox_ancients.txt) (per-spell summary) and [rsprox_ancients_all_casters.txt](rsprox_ancients_all_casters.txt).

```sh
python3 tools/rsprox_ancients.py ~/.rsprox/binary/archive/*.txt           # per-spell summary
python3 tools/rsprox_ancients.py --casts <txt>...                         # one line per cast
python3 tools/rsprox_ancients.py --all-casters ~/.rsprox/binary/archive/*.txt
```

Our side is `test/magic/ancient_tells.lua`. It casts all sixteen in a multiway zone, one fresh npc each. Read its tick log with `tools/rsprox/ticklog_tells.py`.

The BAS transcription uses a private RSProx home, so the shared transcripts stay untouched. That home has `~/.rsprox/caches` symlinked, a copy of the settings, and `filters/filters-1.txt`:

```
version=0
creationtime=1
name=bas
active=true
PLAYER_APPEARANCE_BAS=true
PLAYER_APPEARANCE_EQUIPMENT=true
```

Then run `java -Duser.home=<home> -cp <rsprox classpath> net.rsprox.proxy.cli.TranscribeCommandKt -name archive/<id>`, with the `.bin` symlinked into `<home>/.rsprox/binary/archive/`.
