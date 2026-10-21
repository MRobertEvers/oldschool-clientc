# Scythe of vitur: what Jagex's server sends for a swing

These findings come from **RSProx recordings** of real OSRS sessions. Each recording holds every packet between the client and Jagex's server. Every scythe swing the recording player made was read out, together with the sweep graphic the server sent for it and the target's footprint at that moment.

`scythe_of_vitur.rs2` now follows these numbers. Before this pass it used Near-Reality's direction rule and NR's height of 96.

## The answer

One swing is two packets in the same tick:

| Packet | Content |
|---|---|
| `player_info` sequence | `scythe_of_vitur_attack` (8056), with no delay |
| `map_anim` | one sweep copy, `delay=20`, `height=92`, on the tile **one step** from the player in the copy's direction |

All 4,627 single-sweep swings show exactly that pair. No other delay or height appears.

**Which copy is sent:**

1. Find the target's **centre row**: its south-west tile's row plus `size / 2`. A **2×2 is the exception**: its centre row is its south-west row, as if it were a 1×1.
2. If the player is **not on the centre row**, the sweep is **north or south**, toward that row. This holds however far east or west the target is.
3. If the player **is on the centre row**, the sweep is **east or west**, toward the target.

The copy named D always stands on the tile one step D from the player: north is (0,+1), east (+1,0), south (0,−1), west (−1,0).

| Rule | Right | Wrong |
|---|---|---|
| **Measured rule** (above) | **3,523** | **6** |
| Centre row = `size / 2` with no 2×2 exception | 3,387 | 142 |
| Near-Reality: nearest cardinal to the centre, +22.5° for even sizes | 2,441 | 1,088 |

The six misses are unexplained. Five are against Scurrius, a 3×3 boss that moves often, and one is against a 1×1 Fight Cave swarm standing two tiles from the player. All six are listed in [rsprox_scythe.out](rsprox_scythe.out).

### Seen on the ground

Each grid is one target size. Each cell is a tile the player stood on, relative to the footprint (the dots inside are the footprint). It shows the copy sent and how many swings were seen there. North is up.

A 5×5 (Olm's hand, Sotetseg): the middle row sweeps toward the target, and every other row sweeps north or south toward the middle row. NR's rule sweeps west from the whole east side.

```
        col -1   (footprint)   col 5
row 4   S 45                   S 35
row 3   S 74                   S 96
row 2   E 88      centre row   W 88
row 1   N 20                   N 83
row 0   N 22                   N 25
```

A 2×2 (Scurrius' giant rats, aberrant spectres, Fight Cave swarms). Beside it, the **lower** row sweeps toward it and the upper row sweeps south:

```
        col -1  col 0  col 1  col 2
row 2     .     S 38   S 39     .
row 1   S 41     .      .     S 33
row 0   E 39     .      .     W 21
row -1    .     N 65   N 47     .
```

A 4×4 and a 6×6 use `size / 2`, the **upper** of the two middle rows (4×4: Nylocas Vasilias, Dusk, Fight Cave swarms; 6×6: Maiden, Dusk's last phase):

```
4x4     col -1          col 4          6x6     col -1          col 6
row 3   S 7             S 4            row 5   S 17            S 3
row 2   .               W 2            row 4   S 4             S 13
row 1   N 9             N 4            row 3   .               W 8
row 0   N 35            N 6
```

A 1×1 on a diagonal sweeps north or south, never east or west (3 swings).

Every grid, including the swings that were set aside, is in [rsprox_scythe.out](rsprox_scythe.out). [swings.tsv](swings.tsv) has one row per swing.

### What is still open

- **The 2×2 exception.** It holds across 134 swings on three npc types in four sessions, one of them outside any instance (Slayer Tower). Projectiles aimed at a 2×2 land on sw+1, like `size / 2`, so the sweep does not use the projectile's centre. No mechanism explains it yet. The rule states it as measured.
- **6×6 below the centre row.** No swing was recorded from rows 0–2 of a 6×6, so its north side rests on the 4×4 pattern.
- **The ornaments' colours.** Every sweep the recording player made is red (the plain scythe). Other raiders sent dark red (1891–1894) and blue (1895–1898) sweeps, but the transcript's appearance blocks do not list equipment, so those colours can't be tied to the sanguine and holy scythes from this data. The mapping in the script still rests on the cache-id evidence in `docs/scythe_animation/README.md`.
- **Attack speed.** Every recording is a Leagues V or Grid Master world. Consecutive swings are mostly 4 and 2 ticks apart, which are relic speeds, not the scythe's 5. Nothing here measures the base speed.

## Sources

All recordings come from the public archive, [rsprox.net/database](https://rsprox.net/database). Only the recording player's own swings are counted.

| Submission | Revision | World | Swings | Targets |
|---|---|---|---|---|
| 1845 | 227 | Leagues V | 1,935 | Scurrius and his giant rats |
| 820 | 234 | Grid Master | 994 | CoX: Olm, Tekton, Vasa |
| 1839 | 227 | Leagues V | 519 | Scurrius and his giant rats |
| 1863 | 227 | Leagues V | 471 | Grotesque Guardians (Dusk) |
| 551 | 227 | Leagues V | 284 | Fight Cave swarms (Leagues) |
| 788 | 234 | Grid Master | 242 | ToB (Maiden, Nylocas Vasilias), Vorkath, Araxxor |
| 1858 | 227 | Leagues V | 184 | aberrant spectres (Slayer Tower, not instanced) |
| 787 | 234 | Grid Master | 100 | Vorkath |
| 3299 | 234 | Grid Master | 66 | ToB (Nylocas Vasilias, Sotetseg) |
| 3282, 843 | 234 | Grid Master | 66 | CoX Olm |
| 582 | 227 | Leagues V | 6 | ToB |

Four more ToB and Leagues sessions (1764, 1852 hard mode, 1857, 1875) were transcribed but hold no swings by the recording player. Submission 4025, the one normal-world session with scythe swings, did not transcribe: RSProx's `transcribe` exits with an error on it.

## How swings were classified

| Class | Count | Meaning |
|---|---|---|
| ok | 3,529 | stationary target, one sweep beside the player; these test the rule |
| moving | 865 | the target changed tile in the last 2 ticks, or the player this tick; npc_info's end-of-tick tile need not be where the server judged the swing |
| rotated | 233 | the copy's direction disagrees with its tile step (see Caveats) |
| sweeps=2 | 233 | another raider's sweep also landed within a step |
| sweeps=0 | 7 | no sweep within a step of the player |

## Caveats

- **Rotated rooms.** RSProx prints instance coordinates mapped back to the template. In a room built from rotated chunks (CoX's Tekton and Vasa rooms here), the real-world step comes out rotated in print while the copy's name does not. Each such room shows one fixed mismatch, for example north copy on the printed east tile, which is consistent with a chunk rotation. These swings are kept out of the rule tests. The Olm chamber is never rotated, and projectile coordinates confirm that its npc and player frames agree.
- **Rev 227 transcripts have no names.** RSProx has no symbol names for that revision, so its sequences, graphics and npcs print as ids. Sizes come from `OSRS-Content/osrs-content/configs/all.npc` by id.
- **Seasonal worlds.** Leagues and Grid Master relics change speed and damage. Nothing indicates they change the sweep packet: the copy, delay and height agree across both world types and both revisions.

## Reproducing

```sh
python3 tools/cox/rsprox_fetch.py get 1845          # the NAS mirror first, then the archive
# transcribe with RSProx (docs/rsprox-analysis/cox/README.md and the rsprox memory have the classpath recipe)
python3 tools/rsprox_scythe.py --tsv docs/rsprox-analysis/scythe/swings.tsv \
    ~/.rsprox/binary/archive/{820,3282,843,1839,788,3299,582,1764,1852,551,787,1857,1858,1875,1863,1845}.txt \
    > docs/rsprox-analysis/scythe/rsprox_scythe.out
```

To find more sessions, search the archive for the swing: `python3 tools/cox/rsprox_fetch.py search seq 8056`. To find ToB sessions with swings, combine it with a boss npc in one `mode: ALL` search.

The checks in our content are `::gearrun`'s sweep cases (`gear/gear_selftest.rs2`). Each one is a tile from the grids above with the copy the real server sent.
