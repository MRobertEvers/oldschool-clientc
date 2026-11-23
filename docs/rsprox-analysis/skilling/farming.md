# Farming against the recordings

Sessions (all normal worlds unless marked): index **4419**, **4422** (the owner's
farm runs, rev 241), **4438** (fruit trees, spirit trees), **4529** (tree and
fruit-tree run, rev 242), **4614** / **4630** / **4644** (papaya picks), **4427**
(Garden of Death, a clear), **4635** (Garden of Tranquility). Leagues sessions
(31, 548, 1095, 1819, 3265, ...) only cross-check shapes: their XP is boosted and
some loops are faster.

Method: `tools/rsprox/rsprox_interact.py` and a per-tick signature of what the
recorder was told (seqs, faces, synths, messages, varbits decoded from varps,
inventory, stats, interfaces), set against the same action replayed on our
server with `torirsserver --scriptrun ... --record` and transcribed by the same
RSProx. Replicas: Catherby rake/compost/plant/note (4419 [396..516]); Falador
grow/harvest/clear and an oak's plant/check (4422); a Gnome Stronghold papaya
picked clean (4614 [976..999]).

Ticks below are relative to **A**, the tick the op fires (the click's arrival).

## The rules

| verb | A | after | n |
|---|---|---|---|
| **Rake** | the engine's loc face (footprint) only | A+1: rake anim + rake sound + face to the nearest patch tile, never a weed; then a sweep every 4 ticks, each rolling one weed (weed, state +1, 4 xp on the sweep's tick). The sweep that takes the last weed sends `seq -1` and **no sound**. No busy. | 24 rakes, 4419/4422/4438/4529 |
| **Compost** (bucket / bottomless) | nearest-tile face, `farming_pour_water` (2283) / `farming_pour_water_bottomlessbucket` (8197), compost sound (2427) | busy A+1; A+2: `farming_compost_varbit_<family>_<n>_transmit` = tier, 18/26/36 xp, bucket emptied, "You treat the <patch> with <compost>." (gamemessage) | 28 bottomless; bucket: 1819 [3571] (Leagues) |
| **Plant seed** | seeds leave the pack, nearest-tile face, `farming_seed_dibbing`, dibbing sound | busy A+1..A+2; A+3: state, "You plant ..." (**spam**). **No xp.** | 19, 4419/4422/4427 |
| **Plant sapling** | nearest-tile face, `human_dig` + `digspade` | A+1: the sapling's slot becomes `plantpot_empty`, state, "You plant the <sapling> in the <patch>." (spam). No busy, no xp. | 13, 4422/4438/4529 |
| **Harvest** (herb, allotment) | the engine's loc face | A+1: "You begin to harvest the <patch>." (spam, first pass only), nearest-tile face, `picking_low` / `human_dig`+`digspade`, busy; A+2: the crop + xp (herbs: `pick` sound here), busy off; again every 3 ticks. Art thins at 2 lives left (grown+1) and 1 (grown+2). The emptying pick: state 3, compost varbit 0, "The <patch> is now empty." (gamemessage), xp = harvest + **planting**. Full pack: "You have run out of inventory space." (spam) on the tick the last slot fills. | 4422 [183..216], [1109..1125] and 30 more |
| **Clear** (spade / dead crop's Clear) | the engine's loc face | A+1: "You start digging the farming patch..." (spam), nearest-tile face, dig; a dig every 4 ticks, each after the first finishing it (2 of 3); the finishing dig is busy and 2 ticks later "You have successfully cleared this patch for new crops." (spam), state 3, compost 0, the crop's **planting xp** | 4422 [806..818], [830..838], 4427 [304..311] |
| **Check health** (tree, fruit tree) | the engine's loc face | A+1: nearest-tile face, state, "You examine the tree ..." (spam), xp = check + **planting** (maple 3448 = 3403.4 + 45). No anim. | 4529 [594], [706], [759] |
| **Pick fruit** | the engine's loc face | A+1: nearest-tile face, `picking_high`; busy A+2; A+3: fruit, `farming_pick` (2437), xp, state -1, "You pick a papaya fruit." (spam; "an onion" 548); every 3 ticks; no anim reset at the end | 24 papayas, 4614; 4630, 4644 |
| **Leaving the area** | | entering a square with no patches zeroes `farming_transmit_a..d`; a square with patches overwrites them directly | 4422 [797], [950], [1153], [1407], [1458], [1569], [1704] |

Planting xp is never paid on the planting tick in these revisions (0 stat
updates over 25 normal-world plantings); Leagues V (rev 227) still paid it there
(1819 [3569]).

Jagex never sends our permanent patch carriers: varps 501..509, 511, 512, 515,
516, 830, 1536 are in no session of the 4653 indexed (`rsprox_index.py search
varp`), and no loc or npc morphs on them. Only the area temps
(`farming_transmit_*`, varp 529 and 2077), the per-patch compost transmits
(varps 5746..5751) and the settings (`options_varp` 3552, `spirittree` 5478) go
to the client (`tools/rsprox/varp_transmit_audit.py`).

Every objbox in 324 cached transcripts (index 4300..4699) sends
`if_setobject` zoom **400** (1315/1315) and arms `objectbox:universe` 0..1
(1180) or -1..-1 (135).

## What changed (2026-11-23)

- `farming_patch.varp`: the carriers above `transmit=no`; compost transmits,
  `options_varp`, `spirittree` `transmit=yes scope=perm` (and those two keys
  dropped from `server_fields.generated.varp`).
- `farming_rake.rs2` and every family's rake: `~farming_face_patch`,
  `~farming_rake_sweep_start/_anim`; first sweep at A+1; silent last sweep.
- `farming_items.rs2`: `~farming_treat_patch(_lands)` for both buckets; the
  bottomless bucket's tier and uses in `varp9848/9849` (server-only), with a
  one-time migration out of the tool store's varbits; the spade's dig loop on a
  weak queue (`[queue,farming_clear_dig]`, `^farming_clear_chance` 50, fitted
  from 3 clears).
- `farming_state.rs2`: `~farming_compost_transmit` (70 patches, generated from
  the cache's names; every recorded one checked) on tier set and reset.
- `farming_plant.rs2` (`~farming_dib_seed`), `farming_tree.rs2` /
  `farming_fruit.rs2` (`~farming_plant_sapling`, `~farming_check_health_start`),
  `farming_harvest.rs2` (`~farming_harvest_start/_next`, the thinning art),
  `farming_patch.rs2` (`~farming_planted_xp`), fruit picking.
- `farming_transmit.rs2`: `[mapzoneexit]` on every farming square.
- `interface_chat/configs/chat.constant`: `^objbox_zoom` 400.
- Engine: `p_oploc` that needs no walk does not re-arm the loc face (b65082090);
  scriptrun gained `api_drive.varbit_content` (the server's copy of an unsent
  varbit), so tests read the now-unsent carriers through `t.var.server`.

## Second pass (2026-11-23)

- **Dialogue events.** Jagex arms each page with its own range (index
  4300-4699 and every cached transcript): chat_left/right, messagebox,
  objectbox_double continue `-1..-1`; chatmenu options `1..N`; objectbox,
  confirmdestroy, graphicbox `0..1`; xpreward `0..23` (rev 238+); the list menus
  `0..512`; skillmulti rows `0..$max`; poh_furniture_creation `1..N`;
  poh_add_room:list `1..29`; poh_jewellery_box `0..26`; the portal nexus's five
  (`rows1`/`key_listeners` 0..40, `rows2`/`extra_key_listeners` 0..2,
  `options_layer` 2..3); sailing boats `1..5` with OP1; tfa_partylist `0..2`;
  tutorial experience `0..3`; and the level-up box nothing at all (0 of 581).
  `if_addresumebutton` now arms `-1..-1`; the new `if_resumebutton` registers
  without arming, beside the page's own `if_setevents`. Pages no recording opens
  (books, quest scroll, garden quiz, death screens) keep the old `0..28`.
- **VARP packets.** Jagex does not merge: one packet per varbit write, an
  area's transmits in the order d, c, b, a (index 4422 with the varp roots
  shown, [174], [797]), and the clock as `date_minutes` (on the minute),
  `date_vars` milliseconds, `date_vars` seconds, `map_clock` (1200 of 1835
  ticks carry both `date_vars` packets). Ours matched neither order; both fixed.
- **Leaving an area** zeroes exactly the transmits the next square's sync does
  not write (all of them off the farms), in d, c, b, a order.
- **Harvest is a queued loop**: each click queues one, a click while one runs
  starts a second beside it (the spam-click double loop, 4422 [193..207]), and a
  loop stops when the player leaves its tile.
- **Arrival delay**: compost, seeds and saplings walked to act two ticks after the
  last step (7 of 7), on the op's own tick standing (81).
- **Flowers** (recorded after all: 4635 [936..941], [5545..5548]): the pick's
  tick faces and plays `farming_pick_mushroom` with the pick sound, busy, and
  three ticks on the flower, the weeded patch and the xp with planting in it; no
  message. **Hops** planting says "You plant 4 seeds in the hops patch." (4637
  [2217]). Bushes, cacti, mushrooms, hops harvest and compost bins follow the
  recorded patterns (compost bins from Leagues 1842: `human_push` with
  `compost_close`/`compost_open`, a bucket every 4 ticks with `farming_fillpot`,
  "The compost bin is now empty."), UNVERIFIED on a normal world.
- **Planting xp already paid**: a one-time login migration marks every crop in
  the ground with `^farming_flag_xp_paid`; planting clears it, a health check
  sets it.

## Open

- The live bot cannot answer the xp lamp in X Marks the Spot or the level-up
  box's continue in Wanted! (both fail the same way on HEAD).
- Garden of Tranquility's Lyra/Kragen "stale reopen" choices fail on HEAD too.
