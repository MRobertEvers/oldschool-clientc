# Stronghold of Security implementation plan

## Goal

Finish the existing Stronghold of Security content slice without replacing the
cache-authored dungeon. The implementation will make all eight named maze-door
records pass the player with visible forced movement, add the security-question
flow, wire the four rewards and their progression, enforce the correct portal
rules, and complete the non-local rope/chain maplinks.

## Sources and revision policy

The gameplay contract comes from the Old School RuneScape Wiki:

- [Stronghold of Security](https://oldschool.runescape.wiki/w/Stronghold_of_Security)
- [Portal (Stronghold of Security)](https://oldschool.runescape.wiki/w/Portal_(Stronghold_of_Security))
- [Gift of Peace](https://oldschool.runescape.wiki/w/Gift_of_Peace)
- [Grain of Plenty](https://oldschool.runescape.wiki/w/Grain_of_Plenty)
- [Box of Health](https://oldschool.runescape.wiki/w/Box_of_Health)
- [Cradle of Life](https://oldschool.runescape.wiki/w/Cradle_of_Life)
- [Gate of War](https://oldschool.runescape.wiki/w/Gate_of_War)
- [Rickety door](https://oldschool.runescape.wiki/w/Rickety_door)
- [Oozing barrier](https://oldschool.runescape.wiki/w/Oozing_barrier)
- [Portal of Death](https://oldschool.runescape.wiki/w/Portal_of_Death)

The repository pins the main page at
`docs/areas/stronghold/sources/Stronghold_of_Security.wiki`. It establishes:

- four maze floors with a paired security gate between rooms;
- a security question on the second gate, with a chance for the prompt to be
  skipped; the detailed gate pages clarify that claiming a floor's reward
  stops questions on that floor (so full completion stops all questions);
- one-time coin rewards of 2,000, 3,000, and 5,000, plus the floor emotes;
- Hitpoints and Prayer restoration at each reward, with every stat restored by
  the Box of Health;
- repeatable replacement boots from the Cradle of Life;
- portal access after claiming that floor's reward **or** at combat levels
  26, 51, and 76 on floors 1, 2, and 3 respectively.

Coordinates are checked against the rev-239 cache maps and the repository's
vendored RuneLite shortest-path transport data in
`tools/data/shortest_path/transports/`. Cache identity and placement win over
names copied from another revision.

## Existing assets to preserve

- The four maps, their collision, monster placements, reward locs, portals,
  gate chatheads, animations, sound effect, coins, and reward boots already
  exist in the cache/content tree.
- Generated `maps/*.jl2` and `areas/world/configs/*.spawn` files are not edited.
- Generic double-door and generic climb/maplink behavior stays intact for all
  non-Stronghold content. Exact-name Stronghold triggers override it.
- `%sos_claimed` remains a permanent four-bit floor-completion mask.

## Implementation contract

### 1. Maze gates

Own op 1 for every closed Stronghold leaf:

- `sos_war_door_face`, `sos_war_door_face_mirr`
- `sos_fam_door_face`, `sos_fam_door_face_mirr`
- `sos_pest_door_face`, `sos_pest_door_face_mirr`
- `sos_death_door_face`, `sos_death_door_face_mirr`

All 280 placed leaves form 140 two-leaf barriers, arranged as 70 pairs with a
safe vestibule between them. Every leaf uses the same geometry-driven pass:

1. Snapshot the loc coordinate, angle, and shape.
2. Determine the player's side and the crossing vector with `~check_axis` and
   `~door_open`.
3. Align to the near tile if necessary.
4. Play the Stronghold door sound/effect and force-move across the closed wall.
5. Leave both cache locs closed; do not invoke the generic double-door swing.

When the clicked leaf is the second gate in a pair, ask a randomly selected
wiki security question unless the skip roll succeeds or that floor's reward bit
is set. A wrong answer leaves the player in place; a correct answer runs the
same force-pass path. The first gate always passes without a question.

Second-gate detection must be geometric, not a hard-coded list of 140 barrier
coordinates: use the crossing vector and player side to inspect for the
matching barrier two to four tiles behind the player. This keeps both
directions, the Famine floor's two-tile spacing, one four-tile Vault exception,
and all orientations correct.

### 2. Rewards and progression

| Floor | Loc | First successful claim | Repeat interaction |
|---|---|---|---|
| Vault of War | `sos_war_chest` | 2,000 coins; mark Flap/floor 1; restore HP/Prayer | Already claimed message |
| Catacomb of Famine | `sos_fam_sack` | 3,000 coins; mark Slap Head/floor 2; restore HP/Prayer | Already claimed message |
| Pit of Pestilence | `sos_pest_chest` | 5,000 coins; mark Idea/floor 3; restore every stat | Already claimed message |
| Sepulchre of Death | `sos_death_pram` | mark Stamp/floor 4; choose boots; restore HP/Prayer | Offer replacement boots and restore HP/Prayer again |

Coin claims are atomic: if the player has neither a coin stack nor a free
inventory slot, do not mark the floor claimed. The Cradle likewise does not
claim or consume a choice when the selected boots cannot be added.

This server has no account-authenticator/Jagex-account state. Therefore it
cannot truthfully enforce the modern two-factor reward gate or distinguish
Jagex-account-only fancier boots. The implementation exposes the two original
choices, Fancy boots and Fighting boots, and records this compatibility
adaptation in dialogue/comments rather than inventing an account-security
flag.

### 3. Portals

Exact-name portal handlers gate the existing maplink transition:

| Floor | Allowed when |
|---|---|
| 1 | floor 1 claimed **or** combat level >= 26 |
| 2 | floor 2 claimed **or** combat level >= 51 |
| 3 | floor 3 claimed **or** combat level >= 76 |
| 4 | floor 4 claimed |

An allowed click delegates to the existing maplink row. A denied click keeps
the player in place and explains whether the reward or combat requirement is
missing.

### 4. Ladders, ropes, and chains

Every non-local climb must resolve through the `maplink` table. The destination
contract is:

| Origin | Destination |
|---|---|
| Surface entrance | Vault start `(1859, 5243, 0)` |
| Vault exit ladders | Surface `(3081, 3421, 0)` |
| Vault descent | Famine start `(2042, 5245, 0)` |
| Famine start ladder | Vault start `(1859, 5243, 0)` |
| Any Famine escape rope | Famine start `(2042, 5245, 0)` |
| Famine descent | Pestilence start `(2123, 5252, 0)` |
| Pestilence start vine | Famine start `(2042, 5245, 0)` |
| Pestilence escape vine | Pestilence start `(2123, 5252, 0)` |
| Pestilence descent | Death start `(2358, 5215, 0)` |
| Death start ladder | Pestilence start `(2123, 5252, 0)` |
| Western Death escape chain | Death start `(2358, 5215, 0)` |
| Central Death treasure-room chain | Surface `(3081, 3421, 0)` |

The generated maplink file already covers the harvested rows, but misses the
Vault escape chain, the four Famine escape ropes, one reachable side of the
Pestilence escape vine, and the western Death escape chain. Those omissions
will be supplied as hand-authored Stronghold dbrows, keyed by collision-checked
player approach tiles and the exact loc type, rather than by editing generated
output. The central Death chain's existing surface maplink remains unchanged.

### 5. Verification

- Extend `selftest_stronghold` to cover claim bits, one-time rewards, portal
  claim-or-combat logic, completion suppression, and question correctness.
- Add a static contract test for all eight exact-name gate handlers and all
  Stronghold rope/chain loc placements/destinations.
- Compile scripts and build the content/server packs with zero Stronghold
  errors.
- Run selftest registration, server tests, the door audit, and the broad
  content test where practical.

## Work checklist

- [x] Correct and extend progression/portal helper procs.
- [x] Implement all eight force-pass gate handlers and security questions.
- [x] Implement all four reward handlers and restoration/boots behavior.
- [x] Override and gate all four portal handlers.
- [x] Add and verify the missing rope/chain maplink rows.
- [x] Extend permanent contracts/selftests and update their registration.
- [x] Compile, pack, and run the relevant test suites.

## Verification results

- `check_stronghold_contract.py`: passed; 8 exact force-walk handlers, all 280
  placed leaves, 19 supplemental maplinks, and all four central Death exits.
- `make -C src torirsserver-scripts`: passed; 29,578 scripts compiled.
- `make -C src torirsserver-pack` and `make -C src torirsserver-servpack`:
  passed.
- `make -C src test-torirsserver`: passed; all native/server-script selftests,
  including the ten-step Stronghold contract, completed successfully.
- `door_audit.py`: passed read-only coverage audit.
- The repository-wide packer's `--check-only` mode still reports the existing
  `dragonslayer_giantrat_1_key` server-band/text mismatch (Attack, Defence, and
  Hitpoints). It reports no Stronghold error. The generated maplink import check
  is likewise already out of sync with its vendored TSV while the tracked
  generated file is clean; the Stronghold additions deliberately remain in the
  separate authored dbrow checked above.
