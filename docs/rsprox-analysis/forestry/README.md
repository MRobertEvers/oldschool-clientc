# Forestry (2023) — port plan and recorded facts

Started 2026-11-16 after the woodcutting/farming pass (OSRS-Content b96de28a98).
Nothing of Forestry existed in our scripts: no kit, no bark, no Forester, no
events. The cache has all of it (`content_asset_inventory.py --names
'gathering_event|forestry'`: 61 npcs, most UNUSED).

## Recordings

Almost every Forestry session is Leagues V (rev 227) or Leagues VI (rev 237);
event mechanics are the world's, but anything rate-like (rolls, spawn odds) may
carry a relic and is marked as such.

| event | sessions (npc/loc hits) |
|---|---|
| Rising Roots (loc 47482) | 1834 [23], 1102 [11], 1741 [11], 231 [10], 1095 [10], 551 [9] |
| Struggling Sapling (12164) | 1702, 1095, 1139, 1741, 1834 |
| Flowering Tree (12120) | 1834, 1105, 222, 1101, 231, 1012 (normal world, [2]) |
| Leprechaun (12163) | 1095, 1707, 1101, 1105, 1834, 1839 |
| Beehive (12514) | 1095, 1839, 1105, 1139, 1702, 213 |
| Enchantment Ritual (12519) | 1707 [14], 1834, 1105, 1102, 1098, 1099 |
| Pheasant Control (12536) | 1101 [72], 1839 [49], 222 [36], 1095, 1707 |
| Friendly Ent (12543) | 1101 [64], 1707 [54], 1095 [41], 1102, 558, 1104 |
| Poachers (12553) | 1105, 1839, 1099, 1702, 222, 235 |
| Friendly Forester talk | 1702 only (low-level branch) |

## Shared frame seen in index 1101

- varbits `group_gathering_buff_num_players`, `group_gathering_buff_value`
  (the +1-per-chopper invisible boost, max +10 [wiki Forestry]).
- bark: `You've been awarded <col=3366ff>N Anima-infused bark</col>.` (spam),
  178 times; obj `forestry_currency`.
- leaves into the kit: "Some maple leaves fall to the ground and you place
  them into your Forestry kit." (29).
- not eligible: "As you weren't chopping near the event when it started, you
  aren't eligible for full rewards." (wiki; red).

## Friendly Forester (index 1702 [7999..8017])

Draynor 3096,3238, `forestry_forester` multinpc -> `forestry_forester_nostorage`
(11427). Talk (low level, below Woodcutting 15): pages at 8000..8012, moods
588/567/554/605/568/576. Op3 (Trade) opens the omnishop:

- varps 3870/3869 = -1 (selected main/side), 3874 = dbrow 3284 `forestry_shop`;
  clientscripts 3478(-1,-1), 917(-1,-3); `if_opensub 164:16 819 modal` (main),
  `164:71 806 modal` (side); clientscripts 7140 `omnishop_main_bind_actions`
  (3284, inv -1), 7246 `omnishop_side_init`(3284); setevents 819:33 0..288
  OP1-6,10; 806:0 0..28 OP1-6,10,DEPTH1,DRAGTARGET; 806:1 0..0 OP1; 819:36 0..8 OP1.
- select row 0: `if_button1 819:33 sub=0` -> varp 3869 = 0, clientscript 7146
  (3284, -1, 0, -1, 0, 1, stock dbrow 3285, description, false).
- buy: `if_button1 819:25` (rev 227 numbering) -> obj 28136 (the kit) into slot 3.

## The omnishop (Jagex's dbrow-driven point shop; 88 shops use it)

Not implemented anywhere in our server before this. All data is in the cache:
`omnishop_shop_data` (name, inv, currency, stock rows, cost mods, op texts,
buy quantities q1..q4) and `omnishop_stock_data` (obj, filter, cost rows
(dbrow currency, amount; several rows per item), max purchase, ...). Forestry:
`forestry_shop` (3284), 26 stock rows; currencies `currency_free`,
`forestry_shop_currency_forestry_currency`, `forestry_shop_currency_log_*`.

## Plan (phases, each committed when its test passes)

1. Omnishop: open/select/buy for any `omnishop_shop_data` row, currencies by
   the cache's currency rows. Rev 242 component ids from our interface pack,
   not the rev 227 recording.
2. Forestry core: Friendly Forester (talk, kit), kit storage (inv
   `forestry_kit`), bark, leaves into the kit, group buff, event scheduler on
   chop (eligible trees, kit, 30 tiles / 3 minutes eligibility), XP scaling.
3. Events, one at a time from their recordings: Rising Roots, Struggling
   Sapling, Friendly Ent, Flowering Tree, Leprechaun, Beehive, Pheasant
   Control, Poachers, Enchantment Ritual.
4. Campfire (forestry_fire / campfire_smoke), rations, Forestry basket.

## Done so far (2026-11-16)

- Omnishop (shop/scripts/omnishop.rs2): open per index 4474 [231]; the
  rev 242 triggers trigger_buy (shop, slot, qty), trigger_examine (obj),
  trigger_request_info (shop, slot) routed in torirs_server_world.c
  IF_SCRIPT_TRIGGER_ROUTES; buy pays every cost row (wiki Forestry Shop);
  varps 3869/3870/3874/4271 now transmit (were generated transmit=no).
- Friendly Forester (forestry/): 4 spawns from every recorded add, standing,
  talk from the current wiki transcript with the recording's moods, Trade.
- Forestry kit (forestry/scripts/forestry_kit.rs2): View/Wear/Fill, deposit
  and withdraw with the client's op shuffle (varbit 14985), ~fkit_add.

- Rising Roots (forestry/scripts/rising_roots.rs2), index 1834 [7914..8045].
- Friendly Ent (forestry/scripts/friendly_ent.rs2), index 1101 [6184..6388]:
  five entlings, requests said every few ticks, Prune chatmenu and op3/4/5,
  the wrong-cut stun, the pruned form, the warning at +155, the despawn seq
  at +199 and the end's rewards. Needs npc vars 16/17; the npc var cap went
  64 -> 96 for the events (torirs_server.h).
- Struggling Sapling (forestry/scripts/struggling_sapling.rs2), below.

## Struggling Sapling (index 1095 [31918..32318], index 1707 [5127..5346])

| | recorded | ours |
|---|---|---|
| spawn E | the tree's/stump's tile becomes `gathering_event_sapling_NxN` at the tree's angle; hp-bar npc (2x2 on the SW tile; a 3x3 tree's sapling gets the 1x1 npc on its centre, 1095 [~31000]); spotanim 2369; headbar 6 60->0 over 12000 cycles + headbar 17 fill 1; 5 piles beside it (mushrooms one of 4a/4b/4c); sound_area 7006, 7000 range 10 | same; the head-bar npc's pool is 900 x players within 8 tiles (wiki: x choppers, cap 9000) |
| Collect | C+1 seq 827, synth 2581, stage obj, "You create some mulch using X as the first ingredient." / "You add some X ... as the second/third ingredient." (spam); full: "The mulch would be too sloppy..." | same |
| Add-mulch | C+1 seq 827, "You add the mulch to the sapling..." (spam), "It seems to love it!/like it./accept it. You estimate ... N%" (game message until the whole combination is known, then spam), one "seems to love X as the Nth ingredient" per newly right slot + synth 7012, synths 2427 + 7004 (50%+) / 7009 (accept), heal splat of the hp restored; first 100%: objbox (stage-3 mulch, zoom 400) "The sapling seems to love this combination of ingredients!"; C+2 bark (spam) | same, the objbox at C+2 behind the bark |
| part-made mulch | "You think this mulch could do with some more ingredients first." | same, also for an empty hand (unrecorded) |
| Inspect | woodplaque (749) modal: ms2 "Sapling Nutrition: <col=00ffff>N%</col>", best/last combination rows (1707 [5157]) | same |
| saved | C+2 bark, "<col=3366ff>Well done, you've saved the sapling...</col>", npc del, loc saved_NxN 14 ticks then the tree, piles gone, sound_area 7015; C+3 bonus bark + leaves (game messages), XP | same, to every player within 16 tiles who fed it |
| withered | E+399 piles gone, tree back; E+400 "<col=ff3045>The sapling withers due to lack of nutrients.</col>", npc del, synth 7018 | same (the trigger player's queue) |

Rewards are the wiki's table (nutrition 10 + 30 per O + 10 per P; per-mulch
XP/bark; bonus XP level x avg / 2.5; bonus bark by average band; 15-20 leaves
of a random kind). The recordings' leaves (48..160) carry League multipliers.
A solo player needs 15 perfect mulches in 400 ticks; 1095's solo recorder got
to 40/60 and the sapling withered, as ours does in the scriptrun.

Scheduler fixes found by the test: starting a tree now counts as chopping it
(an event landing before the first roll had made its own trigger player
ineligible), and an event waits at least 4 ticks.

## Event spawn rate (APPROXIMATE)

The wiki gives no number ("odds based on the type of tree and the number of
participants", doubling at 10+). Index 1101 (Leagues VI, busy world): ~13
event starts over the recorder's 30 eligible-tree chops (22 Prifddinas maple,
8 teak), but other players' trees spawn events there too. Modelled: 1 in 4
per eligible tree a kit-holder starts, 1 in 2 with 10+ choppers.

## Open

Group buff (+1 per chopper; needs per-tree player counts), log storage,
the members' events (Flowering Tree, Leprechaun, Beehive, Pheasant Control,
Poachers, Enchantment Ritual), the campfire and rations.
