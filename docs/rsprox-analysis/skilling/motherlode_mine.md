# Motherlode Mine: rebuilt from RSProx

Recordings: **index 4658, 4661, 4664..4669, 4671, 4674** (rev 242.1, w337,
2026-11-23/24, Mining 41..54, Smithing 37). Index 4665 alone is 9,600 ticks of
mining. Wiki pages pinned in `docs/skills/mining/sources/`, plus the OSRS wiki
Pay-dirt and Ore vein pages (2026-11-25).

Ours: `OSRS-Content/osrs-content/server/scripts/minigames/minigame_motherlode/`
(`motherlode_mine.rs2` player side, `motherlode_hopper.rs2` hopper, channel and
sack, `motherlode_world.rs2` Percy's world tick, struts, vein clocks,
rockfalls and the dwarves; `motherlode_slots.rs2` generated accessors;
`configs/motherlode.vars` world state, `motherlode_veins.enum` generated from
the map). Test: `test/skills/motherlode_mine.lua` (scriptrun, 15/15 PASS,
2026-11-25).

## What was wrong before

The tree held a Kronos port ("First slice ... Water NPC path ... deferred").
None of it could be played:

- **No vein could be mined.** The map ships all 266 veins depleted
  (26665..26668 in `maps/m58_88.jl2`); Jagex's server lays the ore on top, and
  every recorded login re-sends a `motherlode_ore_*` over each one. Ours never
  laid them, so `[oploc1,motherlode_ore_*]` had nothing to bind to.
- The sack's op bound the multiloc's children (`motherlode_sack_empty/_full`);
  the click is on the parent `motherlode_sack` (26688), so the sack never
  answered.
- The crate binding was `crate`; the mine's crates are `crate2_old` (357),
  already bound by the Ikov dungeon to "You search the crate but find nothing."
- The hopper wrote straight into the sack: no channel, no pay-dirt npc, no
  arrival line, no bonus xp, no water check.
- Struts were per-player (`varp6479`, a softtimer), broke on a fixed 100 ticks
  and were fixed with `human_smithing` in 3 ticks with a "You fix the strut."
  line the recording never sends.
- Pay-dirt success 1 in 4 (a Kronos guess), xp 6 (60 tenths) instead of 60, the
  ore rolled when the sack was searched, the sack total kept only on a temp varp
  (lost at logout).
- Veins depleted on a 1-in-3 roll per pay-dirt with a 20-tick respawn and no
  overlay timer.
- Rockfalls became an `inviswall` for 25 ticks; no falling rock, no xp of 10.
- No HUD, no Percy shouts, every dwarf wandered on the generated
  `wanderrange=5`, and most stood on tiles the recording never shows.
- Invented lines: "The sack cannot hold any more ore.", "The sack is getting
  full.", "You'll need a hammer to fix it.", "You fix the strut.", "The sack is
  empty.", Percy's "Keep diggin' that pay-dirt...".

## The veins

| rule | recorded | ours |
|---|---|---|
| frame | 4665 [9..15]: the wall swing (`human_mining_rune_pickaxe_wall`) on the op tick, "You swing your pick at the rock." the tick after, the first attempt 3 ticks after the swing, then one every 3 ticks; a find is pay-dirt + `mine_ore` + the line + seq -1 on one tick | the same (new `mining_animation_wall` param on every pickaxe) |
| success | 0.29..0.41 per attempt at Mining 44..54, 0.34 pooled (1,150 of 3,380) | the wiki chart, low 60 / high 105: 0.32..0.33 |
| xp | +60 a pay-dirt | 600 tenths, through `~mining_advance` (prospector kit) |
| gems | "You just found a Sapphire!" etc. while mining pay-dirt | the ordinary rocks' 1/256 roll (1/86 with a glory) |
| life | 36..45 ticks from the vein's first pay-dirt (22 veins) | the same; the raised deck 60..67 (wiki, unrecorded) |
| depletion | the next pay-dirt after the life ends mines it out, every time (DEP always on a PD tick) | the same |
| respawn | 175 ticks after the life ends, whenever it was mined out: the depleted overlay = expiry overlay minus the wait (33 inner veins: 176/176-k); 100 for the outer ring and the deck (31 veins: 101) | the same, inner box 3727..3760 x 5645..5669 on the lower floor, the deck by the map's level |
| overlay | `add_overlaytimer_loc(vein, ore, 0, 1, 176/101, 0xffd000, 0)` to everyone near at expiry; `(vein, depleted, ..., remaining+1, ...)` at depletion | the same, from Percy's tick (bystanders get the depleted one a tick late: the miner's script cannot hunt players) |
| full pack | the box "Your inventory is too full to hold any more pay-dirt." with `pillory_wrong`, on the find's own tick | the same |

Vein clocks are world state (16 slots in `motherlode.vars`). Since the engine
has no per-loc storage, a vein waits for a slot when 16 are running (no
recorded world needed that many).

## Hopper, channel, sack

| step | recorded (4665 [228..247], 4671 [31]) | ours |
|---|---|---|
| deposit | L+1: `human_pickuptable`, all pay-dirt out, synth `watersplash` | same |
| pay-dirt npc | add on 3748,5671 that tick, idle a tick, a tile south each tick (teleport kind, the client glides) to 3748,5660, gone the next tick | same (`npc_tele`) |
| broken machine | deposit still taken, then the objbox "The machine will need to be repaired before your pay-dirt can be cleaned." (pay-dirt model, zoom 400) (4666 [765]) | same; the pay-dirt waits in the channel while both wheels are jammed |
| arrival | T+12: varbit 5558 up by the load, "Some ore is ready to be collected from the sack." (game), "... The sack is now full." at 108 (4665 [1045]), bonus xp (405 for 27 at Mining 45: coal and gold 15 each), synth `put_down` delay 30 | same; ores rolled on the wiki's cascade chart (nugget 8/256 flat, then rune/addy/mith/gold by level, coal), nuggets capped at 3 a batch |
| collect | L+1: nuggets first, then the ores best first, one 5558 step per kind; objbox (pay-dirt model) "You collect your ore from the sack." / "...<br>The sack is now empty." | same; tallies in perm varps 7977..7984, 5558 rewritten at login |

## Struts, Percy, the dwarves

| thing | recorded | ours |
|---|---|---|
| strut break | each strut on its own, 24..445 ticks after a fix (34 distinct intervals, 8 sessions on one world); the strut and its wheel on one tick, wheel at angle 2 broken / 0 fixed; the map ships them broken | uniform 20..450, world vars |
| repair | 4666 [781..796]: L+1 face; from L+2 a roll every tick, `swan_hammer` d10 + `swan_repair_wall` d10 every 4 ticks; on the fix seq -1, strut + wheel, 1.5 x Smithing xp; no busy | same (Mod Ash's 12%..27% as stat_random 30..68) |
| Percy | a line every 6 ticks while a wheel is jammed: one wheel "We got us a jammed wheel!" / "Ye'd better fix that wheel!" / "Git yer hammer an' fix that wheel!"; both, with `emote_angry`, "That water ain't flowing!" / "Both them wheels be jammed!" / "Git yer hammer an' fix them wheels!" (4666 [9..27], [756..780]); wanders 3755..3757 x 5665..5667 | same; `wanderrange=1`, his `[ai_timer]` is the mine's world tick |
| dwarves | miner_4 / miner_10 (and 8, 9 seen briefly) swing `royal_dwarf_mining` every 3 ticks at a vein, seq -1 + "Pay-dirt!" the tick after a swing (809 of 3,994), next swing 2 ticks later; the other seven stand still at one tile each | same at the recorded posts (spawns relocated in `tools/gen_spawns.py` and `m58_88.spawn`); `defaultmode=none` |

## Rockfalls, the way in, crates, HUD

| thing | recorded | ours |
|---|---|---|
| rockfall clear | 4658 [5082..5088]: L+1 face, L+2 busy + the no-reach-forward swing d15, L+4 busy off, seq -1, `loc_del`, +10 xp; `mine_ore` area sound range 3 | same |
| falls back | 74..101 ticks later (26 observed): two `motherlode_rockfall` projectiles from 2 tiles behind (heights 1000/900 raw -> 25, 0..30 and 10..30), `castlewars_catapult_splash` + `chinchompa_explode` r2 at delay 30, the rockfall the next tick | same, 75..101 |
| entrance | 4658 [5076..5080]: L+1 face (3x2), L+2 busy, `human_longcrawl`, synth `climbing_loop` x2 d4, L+3 teleport to 3728,5692 + face north, L+4 busy off | same; the exit mirrored (unrecorded) |
| HUD | `motherlode_hud` (382) on overlay_hud the tick after arriving, closed on the tick of leaving (4667 [285]) | `[mapzone,0_58_88]` / exit |
| crates | L+1 objbox "You've found a hammer. How handy." at 3752,5664 (4666 [770]); "You've found a bronze pickaxe. How handy." at 3753,5659 (4665 [4985]) | same; the other four crates' finds UNVERIFIED (wiki: three of each) |
| Dark tunnel | objbox (agility_contortion) "It gets quite narrow ... Agility level of 54 before entering." at Agility 52 (4658 [5110]) | same |
| Percy talk | 4671 [4807..4831]: greeting (angry), menu "What would you like to say?" x5, the unlocks branch (upper level offer, sack offer) | that branch verbatim |

## Bridged locs (a trap for tests)

The sack, both struts and 61 veins are authored on the map's level 1 over
bridged tiles; the client and the recording show them on plane 0, and the
engine's scene loads them on the walked plane (`record_loc_at`). Content
addresses them on plane 0. The scriptrun selector reads the raw map: the test
names the sack and struts on level 1.

## Open

- Mining or depositing at a full sack: the wiki says a chatbox warning stops
  both; its words are in no recording. Ours stops silently.
- An empty sack's click and a full pack at the sack: unrecorded, ours says
  nothing.
- Percy's other menu branches ("How do I mine here?", "Tell me about
  yourself.", the purchases, "I'll leave you alone." and his reply): only the
  wiki transcript has them, and only paraphrased here. The player's line is
  said; Percy's answer is not invented.
- Percy's Trade (4671 [4801]): "Prospector Percy's Nugget Shop" on inv 187,
  stock recorded (prospector kit x100, coal bag, gem bag, soft clay pack
  x1,000, bag full of gems x100). `shop.rs2` takes only coins, so the op is not
  wired until the shop has a currency.
- The Dark tunnel's crossing at Agility 54 and the upper level's ladder and
  hopper: unrecorded.
- `put_down` on arrival: 24 of 35 recorded arrivals carry it, with no
  distance rule found. Ours always sends it.
- The dwarves walk between several posts in the recording (miner_4: six);
  ours keep one each. "Ores are determined when the pay-dirt is mined"
  [wiki]: ours rolls on arrival (differs only for a boost that ends in
  between).
