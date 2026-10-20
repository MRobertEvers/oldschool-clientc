# Theatre of Blood, Entry Mode, SOLO: the solvers and the relay

Started 2026-10-20. The raid is the "carry the Stranger" leg of the quest A
Night at the Theatre: one player, Entry Mode, lobby to lobby, no death. The
quest test calls `QD.raid.tob_entry_solo(t, opts)`
(script/plugins/quest_driver/tob_relay.lua); the tests are
`test/raids/tob_entry_<room>.lua` (one room) and `test/raids/tob_entry_relay.lua`.

## 1. The approach

No fork. Entry is a third mode of the v2 relay (`QD.raid.tob_relay_test("entry",
...)`, SEATS = 1) and of every v2 room solver (`tob2_<room>.lua`). Each solver
takes Entry on its Normal tables with what one seat needs changed, in a
mode table built at its `init` (the Maiden: `QD.MAIDEN_V2.mode_table(hard,
entry)`, `QD.MAIDEN_V2.apply_mode(entry)`).

- `QD.raid.tob_npc_name` (raid.lua) maps a Normal name to its `_story` record
  where the content spawns one (`QD.raid._TOB_STORY`, from
  tob_raid.rs2 `~tob_mode_npc_story`, tob_verzik.rs2 and the rooms' own type
  procs); Verzik's pillars and rubble stay Normal records (index 3 shows
  8377-8379 at the Entry Verzik).
- The kit is the Normal relay's seat 1 (the Maiden's freezer kit): void melee
  with the tentacle, the blowpipe and the ranged three, the toxic trident and
  the mage three, the rune pouch of barrage runes, burning claws, brews,
  restores, combats, a ranging potion.
- The Entry supply chest (after Bloat and Sotetseg) gives bandages, not a
  points store: `take_bandages` (op 2 on the chest), and `tob_bandages` is the
  last food in `QD.raid._tob_common_ids`.

## 2. The rooms: what Entry changes, and the plan for one seat

Entry numbers from the content (tob.constant and the room constants), checked
against the recordings where they show it (section 4).

### 2.1 Maiden (500 hp solo; 1 crab of 16 hp a threshold; spawns of 10 hp)

- Storm: Normal's roll halved (`^tob_maiden_entry_divisor`), then halved by
  Protect from Magic: 0-9 prayed. Pools: 2-5 a tick (fixed, 4.1). Trail 2-5.
- Plan: the one seat is the Normal FREEZER (barrage on the crab, blowpipe on
  her, the claws at the start). Changed for solo: no PREWAVE (with nobody else
  to bring her to the threshold, waiting for the wave is waiting forever: the
  first run stood 378 ticks in F-prewave and died); the threat constants at
  Entry's numbers (`QD.MAIDEN_V2.ENTRY`), else the plan prices every storm at
  Normal's 26 and drinks brews against damage that never comes; a synthesised
  wave handler for any one-crab pattern (`tob_maiden.lua` `_maiden_wave`).

### 2.2 Bloat (320 hp solo; flies max 8, stomp max 40, hands 20-25)

- Plan: the v2 machine has no roles (every seat the same); run as is.

### 2.3 Nylocas (nylos 2/3 hp; pillars 155; collapse 30+; Vasilias 360)

- Every nylocas is style-locked (tob_damage.rs2 NULLING) in every mode, so one
  seat must kill all three styles. Plan: the Normal ranger seat's machine,
  with every style its own, Vasilias in her colour.
- As built: the Normal ranger's own two styles (ranged, the melee help), NOT
  all three. Measured on the same 8 seeds (supports lost / bites a room):
  ranged + melee 0-2 lost, ~460 bites; all three 1-3 lost, ~700; ranged +
  magic 0-3, ~800 (the trident's 4-tick swings cost more than the magic
  nylocas they save). `QD.NYLO_V2.SOLO_STYLES` holds the choice (nil = the
  two). One real fix: `NV.candidates` handed each style's best nylocas to the
  seats of that style (2 melee, 3 magic) whether or not the party has them; a
  solo seat was always one short (`r <= S.size`).
- A collapse hits the one seat for 30-50; the relay eats through it.

### 2.4 Sotetseg (560 hp solo; solo death ball 15; maze)

- Plan: the v2 solver as is; the one seat is always the maze runner.
- As built: Entry's own death ball (`QD.SOTE_V2.MODE.entry`, 15 solo) and
  hits (`QD.SOTE_V2.ENTRY`: melee 20, balls 22). On Normal's solo 121 the pair
  floor sat at HEAL_CAP 115 and the seat drank 6-11 brew doses a room for ~30
  taken; now 0.

### 2.5 Xarpus (520 hp; screech at 22.5%; exhumeds heal 6)

- Plan: the v2 solver as is; the one seat takes the Dawnbringer.

### 2.6 Verzik (P1 300, P2 400, P3 600 solo; pillars collapse for 50)

- Plan: the v2 P1/P2/P3 solvers as is.
- As built, P2: the one seat is the Athanatos' killer wherever it lands and
  every red is its red in turn (`S.p2m.solo`). The first run never touched the
  Athanatos: it healed her every 4 ticks, her bar sat at 10% and P2 timed out
  at 900.
- As built, P3: nobody to bounce the green ball to, so it always lands for 74%
  of the hitpoints LEVEL (73 at 99; content `~tob_verzik_ball_damage`). The hp
  floor (`VZ.hp_floor`) had no floor for an un-bounced first hop ("no floor
  survives a miss": Hard's instant kill); Entry now floors the holder at the
  73 plus the autos landing (2 of 8 seeds died at 71 under a 73 before).

## 3. Results (scriptrun, 2026-10-20, private objdir build_natt_entry and a
private script pack of the shared tree with the NATT quest at HEAD)

| test | seeds | green |
|---|---|---|
| tob_entry_maiden | 10 | 10 (26-64 taken) |
| tob_entry_bloat | 10 | 10 (0 taken) |
| tob_entry_nylocas | 10 | 10 (0-2 supports lost, 28-113 taken) |
| tob_entry_sotetseg | 10 | 10 (20-43 taken, no brews) |
| tob_entry_xarpus | 8 (after the 4.4 fix) | 8 (0 taken) |
| tob_entry_verzik | 10 | 10 (P3 7-172 taken) |
| tob_entry_relay | 32 (e1-e8, f1-f16 before the Xarpus fix; g1-g8 after) | 32 |
| tob_entry_solo (QD.raid.tob_entry_solo) | 12 | 12 |

The relay is ~2000 ticks (4 s); it reaches Verzik on 6-16 brew doses and the
second chest's bandages.

## 4. Content fixed from the recordings

The two Entry recordings: index 3 (rsprox.net 101, Leagues V, SOLO: "Is your
party ready? Members: 1. Mode: Entry." at [25675]; Entry run [25676-27274],
during the quest: the Mysterious Stranger walks every room) and index 553
(rsprox.net 554, Leagues V, a DUO with Unsupposed). Both seasonal: the
recorder hits every tick and its numbers are not ours; what the SERVER sends
(counts, adds, heals, messages) is read.

### 4.1 Maiden

| | Recorded | Ours before | Fix |
|---|---|---|---|
| Crabs a threshold | solo: 1 at 70/50/30% (index 3 [25720] [25726] [25732]); duo: 2 (index 553 [3997] [4001] [4013]) | 2 a player (Normal's rule) | `^tob_maiden_crabs_per_player_entry` 1 (tob_maiden.constant, `~tob_maiden_crab_count`) |
| Pool damage | 5, 2, 5 on three pool ticks, her heal the same (index 3 [25726-25728]) | Normal's 5..10 + 2c | `^tob_maiden_pool_entry_divisor` 2 (2..5) |
| Her hp | 500 solo (headbar 148/152 after 35) | 500 | match |

### 4.2 Bloat

| | Recorded | Ours | |
|---|---|---|---|
| Her hp | 320 dealt in 19 hits to her death (index 3 [25804-25870]) | 320 | match |
| The room-clear line | "The Vampyres are impressed by your prowess. Check the chest to see what they've sent you." [25870], and after Sotetseg [26789] | nothing in Entry | **fixed**, tob_chest.rs2 `~tob_chest_award_on_clear` |

### 4.3 Nylocas

| | Recorded | Ours | |
|---|---|---|---|
| Vasilias hp | 359 dealt (index 3) | 360 | match |
| Vasilias colour | first change +14 after her combat add, then every 15 (index 3 [26551] -> [26565] [26580] [26595] [26610]; index 553 [4615] -> [4629] [4644]) | 14 / 15 (^tob_vasilias_entry_window_ticks) | match |
| Nylocas added | 153 small, 37 big | 163 small, 43 big (one seed) | comparable |

### 4.4 Xarpus

| | Recorded | Ours before | Fix |
|---|---|---|---|
| Phase 1 start | his bar at 100 on the first exhumed heal [26847] and every one after (15 heals of 6 to [26919]) | 75% (390 of 520) | Entry starts full (`~tob_xarpus_open_health`) |
| The heals | phase 2's first hits of 30, 31, 34, 11 read 95, 90, 84, 82: a pool of ~610 = 520 + the 90 healed; 625 dealt to his death | capped at the 520 pool | Entry: each orb raises his maximum with his hitpoints (`~tob_xarpus_heal_land`); `~tob_xarpus_pool_live` carries it through the stand-up, the HUD and the 22.5% screech |

### 4.5 Sotetseg

| | Recorded | Ours | |
|---|---|---|---|
| Death ball, solo | 15, 14 (index 3 [26703] [26751]) | flat 15 | match |

### 4.6 Verzik

| | Recorded | Ours before | Fix |
|---|---|---|---|
| P2 lightning, solo, no insulated boots (feet: aranea boots, `get 3 --bas`) | 6, 6, 6 (index 3 [27155] [27175] [27195]) | 1..48 (46-48 seen) | `^tob_verzik_p2_zap_entry` 6, `~tob_verzik_zap_hit` |
| Athanatos heal | 6 x6, 7 x3 (index 3 [27148]-[27180]) | 9-10 | `^tob_verzik_p2_purple_heal_min/max_entry` 6-7 |
| Pillars | Normal records 8377-8379 | the same | match |

### 4.7 Bandages

| | Recorded | Ours before | Fix |
|---|---|---|---|
| The heal's line | "You use a bandage." (index 3 [27001]) | "You use the bandages." | tob_spectate.rs2 `[opheld1,tob_bandages]` |

## 5. Open

- The Entry supply chest is an interface in the recording: op 2 opens
  `tob_chests` (interface 23) over inv 612 with 10 `tob_bandages` and a
  `tob_vampyre_message`, the ops Take / Take-5 / Take-All / Take-X
  (clientscript 149), components 23:6 and 23:7 hidden, 23:8-11 moved (index 3
  [25887]). Ours puts as many bandages as fit straight into the pack. Not
  changed (the relay drops what the later rooms need room for).
- Xarpus in NORMAL shows the same full bar through phase 1 with its heals
  (index 3268 = rsprox.net 3285, solo Grid Master: heals of 20 at a bar of 100
  from [414099]); only Entry was changed (4.4).
- The lobby door on scriptrun: `opts.enter = "door"` answers "You need to be in
  a party to enter the Theatre" there, because the notice board (forming the
  party) is clientscripts and scriptrun runs none (test/raids/tob_lobby.lua is
  live only). On scriptrun a caller enters with `opts.enter = "cheat"` or its
  own landing.
- The P3 webs: the solo Entry recording's P3 is 48 ticks and shows none; ours
  has them. Not judged.
