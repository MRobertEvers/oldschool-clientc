# Icthlarin's Little Helper, from RSProx

Rebuilt 2026-11-08 from **index 4481** (start, part 0), **4486** (start, part 1), **4487** (end), all rev 242.1, world 386/369, the owner's recordings. Cites are `[tick]` in 4487 unless a session is named. The old content was an invented wiki/Quest Helper ladder (stage numbers 8..23 did not match, cat gated via inventory, ceremony "dialogue only", the jar and memory flow simplified). Every page, stage and op below replaces it.

Files: `quests/quest_icthlarin/` (`icthlarin.rs2` Wanderer/start/hypnosis, `icthlarin_town.rs2` Sophanem's people, `icthlarin_pyramid.rs2` door/copy/pit/doors/tile game/chest/hazards, `icthlarin_jar.rs2`, `icthlarin_embalm.rs2`, `icthlarin_ceremony.rs2` incl. journal), `configs/icthlarin.constant`, `configs/icthlarin.npc`. Targeted edits: `quest_contact/scripts/contact_jex.rs2` (Jex at ILH stages, wiki standard menu), `minigame_pyramidplunder/scripts/plunder.rs2` (Tarik), `quest_beneathcursedsands/scripts/beneathcursedsands.rs2` (the invented post-quest line), `shop/sophanem/scripts/the_spice_is_right.rs2` (Trade gated on Contact!), `quests/scripts/questpoints.rs2` (jingle 152), `_explicit/configs/server_fields.generated.npc` (12 lines), new `areas/area_desert/scripts/wingstone.rs2`. Test: `test/quests/icthlarin.lua` follows the recorded flow; `SCRIPTRUN PASS`, 187 rows, private pack (shared pack currently fails on Death on the Isle's uncommitted `deathontheisle.rs2`/`di_bmp.rs2`; compiled from a mirror with HEAD's copy of that quest).

## Stages (ics_little_var 418, transmit via varp445 main_ics_var)

| value | when | cite |
|---|---|---|
| 1 | "Yes." at the start prompt | 4486 [291] |
| 2, 3 | hypnosis lands, same tick; `ics_specvis` 1 | 4486 [300] |
| 4 | first memory (door) | [896], `ics_stateofmind` 1 |
| 5 | tile puzzle solved | [1262]; 13882 met_sphinx [857] |
| 6 | token handed over | [1814]; 13883 cat_safe [1801], 13884 given_token [1815], 13885 sphinx_amascut [1842] |
| 7 | High Priest: return the jar | [2024] |
| 8 | pyramid entered to return it | [2338], `ics_liverpot_vis` 1 |
| 9 | second memory (pit) | [2451] |
| 10 | Apparition rises | [2602] |
| 11 | Apparition dead | [2708] |
| 12 | second memory over | [2765] |
| 13 | puzzle solved again | [2816] |
| 14 | jar back in pot | [2832] |
| 15 | ceremony prep | [3095] |
| 16 | holy symbol | [3342] (after embalmer multi 7, carpenter multi 1) |
| 17 | third memory | [3633] |
| 18 | symbol hidden | [3682] |
| 19 | third memory over | [3690] |
| 20, 21 | ceremony begins | [3971] |
| 22 | Possessed Priest | [4027], stage set on the page after Amascut leaves |
| 23 | priest dead | [4322], door returned [4320] |
| 24 | talk finished | [4388] |
| 25 | fourth memory over | [4429] |
| 26 | complete | [4519] |

Transmit: `varp_transmit_audit.py` over 4486 + 4487 lists no `ics_` varbit we do not send (base varps `transmit=yes`, `perm`).

## Action and teleport inventory (every row has a verdict)

### Session 4481: actions

| first tick x n | action | verdict |
|---|---|---|
| 3 x1 | `[if_buttonx] com=welcome_screen:play (378:72), op=1` | client UI / recorder chrome: engine-generic |
| 19 x3 | `[if_buttonx] com=questlist:list (399:7), op=2` | client UI / recorder chrome: engine-generic |
| 21 x2 | `[if_buttonx] com=questjournal_overview:close (782:9), op=1` | client UI / recorder chrome: engine-generic |
| 80 x6 | `[opnpc3_v2] npc=(id=banker1_east (1634), )` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 84 x2 | `[if_buttonx] com=bankmain:depositinv (12:47), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 89 x1 | `[if_buttonx] com=bankmain:items (12:12), obj=tinderbox (590), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 91 x3 | `[if_buttonx] com=bankmain:tabs (12:10), obj=poh_tablet_kourendteleport (28790), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 94 x1 | `[if_buttonx] com=bankmain:tabs (12:10), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 96 x1 | `[if_buttonx] com=bankmain:items (12:12), obj=coins (995), op=5` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 100 x4 | `[if_buttonx] com=bankmain:search (12:42), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 103 x1 | `[if_buttonx] com=bankmain:items (12:12), obj=water_skin3 (1825), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 105 x2 | `[if_buttonx] com=bankmain:items (12:12), obj=placeholder_water_skin4 (17204), op=8` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 106 x1 | `[if_buttonx] com=bankmain:items (12:12), obj=water_skin1 (1829), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 111 x1 | `[if_buttonx] com=bankmain:tabs (12:10), obj=adamant_scimitar (1331), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 118 x3 | `[opnpc3_v2] npc=(id=ge_clerk_1 (2148), )` | generic Grand Exchange: recorder's preparation |
| 120 x4 | `[if_buttonx] com=ge_offers:index_7 (465:14), op=1` | generic Grand Exchange: recorder's preparation |
| 130 x1 | `[resume_p_objdialog] id=willow_logs (1519)` | generic Grand Exchange: recorder's preparation |
| 147 x2 | `[if_buttonx] com=ge_offers:setup (465:26), op=1` | generic Grand Exchange: recorder's preparation |
| 149 x1 | `[if_buttonx] com=ge_offers:setup_confirm (465:30), op=1` | generic Grand Exchange: recorder's preparation |
| 152 x1 | `[if_buttonx] com=ge_offers:details_collect (465:24), obj=willow_logs (1519), op=1` | generic Grand Exchange: recorder's preparation |
| 152 x1 | `[if_buttonx] com=ge_offers:details_collect (465:24), obj=coins (995), op=2` | generic Grand Exchange: recorder's preparation |
| 183 x1 | `[if_buttonx] com=bankmain:items (12:12), obj=growncatobject (1561), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 187 x1 | `[if_buttonx] com=inventory:items (149:0), obj=growncatobject (1561), op=7` | generic consumables / the recorder's own items; cat drop is the kitten_pet follower |
| 191 x1 | `[if_buttonx] com=bankmain:tabs (12:10), obj=4doseprayerrestore (2434), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 193 x3 | `[if_buttonx] com=bankmain:items (12:12), obj=4dosestamina (12625), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 203 x1 | `[if_buttonx] com=bankmain:items (12:12), obj=desert_shirt (1833), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 203 x1 | `[if_buttonx] com=bankmain:items (12:12), obj=desert_robe (1835), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 204 x1 | `[if_buttonx] com=bankmain:items (12:12), obj=desert_boots (1837), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 206 x1 | `[if_buttonx] com=inventory:items (149:0), obj=desert_shirt (1833), op=3` | the recorder's worn kit and jewellery teleports (generic; ring/glory routes covered by jewellery tests) |
| 207 x1 | `[if_buttonx] com=inventory:items (149:0), obj=desert_robe (1835), op=3` | the recorder's worn kit and jewellery teleports (generic; ring/glory routes covered by jewellery tests) |
| 208 x1 | `[if_buttonx] com=inventory:items (149:0), obj=desert_boots (1837), op=3` | the recorder's worn kit and jewellery teleports (generic; ring/glory routes covered by jewellery tests) |
| 213 x1 | `[if_buttonx] com=bankmain:quantityall (12:37), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 217 x1 | `[if_buttonx] com=bankmain:items (12:12), obj=airrune (556), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 218 x1 | `[if_buttonx] com=bankmain:items (12:12), obj=mindrune (558), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 219 x1 | `[if_buttonx] com=bankmain:items (12:12), obj=firerune (554), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 221 x1 | `[if_buttonx] com=bankmain:quantity1 (12:29), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 223 x1 | `[if_buttonx] com=bankmain:items (12:12), obj=earth_battlestaff (1399), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 226 x1 | `[if_buttonx] com=inventory:items (149:0), obj=earth_battlestaff (1399), op=3` | the recorder's worn kit and jewellery teleports (generic; ring/glory routes covered by jewellery tests) |
| 245 x1 | `[if_buttonx] com=stats:woodcutting (320:22), op=2` | client UI / recorder chrome: engine-generic |
| 247 x2 | `[if_buttonx] com=skill_guide_v2:close (860:4), op=1` | client UI / recorder chrome: engine-generic |
| 257 x1 | `[if_buttonx] com=bankside:items (15:3), obj=cert_willow_logs (1520), op=6` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 262 x1 | `[if_buttonx] com=bankmain:tabs (12:10), obj=iron_ore (440), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 266 x1 | `[if_buttonx] com=bankmain:items (12:12), obj=willow_logs (1519), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 284 x1 | `[if_buttonx] com=bankmain:items (12:12), obj=bucket_empty (1925), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 289 x1 | `[if_buttonx] com=bankmain:items (12:12), obj=knife (946), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 299 x1 | `[if_buttonx] com=bankmain:items (12:12), obj=amulet_of_glory_4 (1712), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 305 x1 | `[if_buttonx] com=bankmain:items (12:12), obj=ring_of_dueling_1 (2566), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 306 x1 | `[if_buttonx] com=bankmain:items (12:12), obj=ring_of_dueling_8 (2552), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 309 x1 | `[if_buttonx] com=inventory:items (149:0), obj=ring_of_dueling_1 (2566), op=3` | the recorder's worn kit and jewellery teleports (generic; ring/glory routes covered by jewellery tests) |
| 310 x1 | `[if_buttonx] com=inventory:items (149:0), obj=amulet_of_glory_4 (1712), op=3` | the recorder's worn kit and jewellery teleports (generic; ring/glory routes covered by jewellery tests) |
| 317 x1 | `[if_buttonx] com=wornitems:slot2 (387:17), op=5` | the recorder's worn kit and jewellery teleports (generic; ring/glory routes covered by jewellery tests) |
| 323 x1 | `[oploc1_v3] id=bankdoor_r (1511)` | door / crate / combat-achievements UI: generic, not re-audited (open); the plunder door [466] is the minigame's |
| 333 x2 | `[if_buttonx] com=wornitems:call_follower (387:7), op=1` | the recorder's worn kit and jewellery teleports (generic; ring/glory routes covered by jewellery tests) |
| 337 x2 | `[opnpc1_v2] npc=(id=growncat (1619), )` | this content: pick up follower (kitten_pet), Wanderer flow |
| 373 x1 | `[opnpc4_v2] npc=(id=shantay (4642), )` | generic shop/desert: not re-audited in this port (open) |
| 377 x1 | `[opnpc3_v2] npc=(id=shantay (4642), )` | generic shop/desert: not re-audited in this port (open) |
| 382 x1 | `[if_buttonx] com=shopmain:items (300:16), obj=water_skin4 (1823), op=2` | generic shop/desert: not re-audited in this port (open) |
| 384 x1 | `[oploc1_v3] id=shantay_pass_henge_doorway (4031)` | generic shop/desert: not re-audited in this port (open) |
| 394 x1 | `[opnpc3_v2] npc=(id=magic_carpet_seller1 (17), )` | magic carpet: bound (carpet_travel); route and fare not re-audited (open) |
| 493 x1 | `[opnpc3_v2] npc=(id=magic_carpet_seller3 (19), )` | magic carpet: bound (carpet_travel); route and fare not re-audited (open) |
| 542 x2 | `[if_buttonx] com=inventory:items (149:0), obj=4dosestamina (12625), op=2` | generic consumables / the recorder's own items; cat drop is the kitten_pet follower |
| 559 x1 | `[if_buttonx] com=stats:ranged (320:4), op=2` | client UI / recorder chrome: engine-generic |
| 587 x8 | `[opnpc1_v2] npc=(id=ics_little_multi_wanderer (6187)), )` | this content: Wanderer, icthlarin.rs2 |
| 588 x4 | `[oploc1_v3] id=icthalarins_tent_door (6581)` | this content: tent line |
| 611 x1 | `[oploc1_v3] id=crate (366)` | generic world interaction: not re-audited in this port (open) |
| 617 x3 | `[oploc1_v3] id=sophanem_gate_left (44036)` | this content: gate refusal in icthlarin_town.rs2 |
| 621 x2 | `[opnpc1_v2] npc=(id=ics_little_priestdoorman (4208), )` | this content: icthlarin_town/embalm/ceremony, contact_jex and plunder Tarik |
| 803 x1 | `[oploc2_v3] id=elid_bankbooth (10517)` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 809 x1 | `[if_buttonx] com=bankside:items (15:3), obj=growncatobject (1561), op=2` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 834 x1 | `[if_buttont] selectcom=inventory:items (149:0), selectedselectedobj=3dosestamina (12627), ` | generic consumables / the recorder's own items; cat drop is the kitten_pet follower |
| 996 x1 | `[if_buttonx] com=logout:logout (182:8), op=1` | client UI / recorder chrome: engine-generic |

### Session 4481: teleports

| tick | from -> to | caused by | verdict |
|---|---|---|---|
| 320 | (3166, 3487, 0) -> (3293, 3163, 0) | `[317] [if_buttonx] com=wornitems:slot2 (387:17), op=5` | recorder jewellery/tablet: generic, not re-audited here (open) |
| 405 | (3308, 3110, 0) -> (3308, 3108, 0) | `[394] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 406 | (3308, 3108, 0) -> (3308, 3106, 0) | `[394] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 407 | (3308, 3106, 0) -> (3311, 3106, 0) | `[394] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 414 | (3319, 3097, 0) -> (3319, 3094, 0) | `[394] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 417 | (3322, 3091, 0) -> (3322, 3088, 0) | `[394] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 418 | (3322, 3088, 0) -> (3322, 3085, 0) | `[394] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 421 | (3325, 3082, 0) -> (3325, 3079, 0) | `[394] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 422 | (3325, 3079, 0) -> (3325, 3076, 0) | `[394] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 423 | (3325, 3076, 0) -> (3325, 3073, 0) | `[394] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 435 | (3343, 3055, 0) -> (3343, 3052, 0) | `[394] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 436 | (3343, 3052, 0) -> (3343, 3049, 0) | `[394] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 439 | (3346, 3046, 0) -> (3346, 3043, 0) | `[394] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 440 | (3346, 3043, 0) -> (3346, 3040, 0) | `[394] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 441 | (3346, 3040, 0) -> (3346, 3037, 0) | `[394] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 442 | (3346, 3037, 0) -> (3346, 3034, 0) | `[394] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 443 | (3346, 3034, 0) -> (3346, 3031, 0) | `[394] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 444 | (3346, 3031, 0) -> (3346, 3028, 0) | `[394] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 445 | (3346, 3028, 0) -> (3346, 3025, 0) | `[394] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 446 | (3346, 3025, 0) -> (3346, 3022, 0) | `[394] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 447 | (3346, 3022, 0) -> (3346, 3019, 0) | `[394] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 448 | (3346, 3019, 0) -> (3346, 3016, 0) | `[394] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 449 | (3346, 3016, 0) -> (3346, 3013, 0) | `[394] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 450 | (3346, 3013, 0) -> (3346, 3010, 0) | `[394] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 451 | (3346, 3010, 0) -> (3346, 3007, 0) | `[394] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 455 | (3350, 3004, 0) -> (3349, 3003, 0) | `[394] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 508 | (3351, 2942, 0) -> (3351, 2939, 0) | `[493] [opnpc3_v2] npc=(index=28406, id=magic_carpet_seller3 (19), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 511 | (3353, 2937, 0) -> (3356, 2937, 0) | `[493] [opnpc3_v2] npc=(index=28406, id=magic_carpet_seller3 (19), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 512 | (3356, 2937, 0) -> (3359, 2937, 0) | `[493] [opnpc3_v2] npc=(index=28406, id=magic_carpet_seller3 (19), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 513 | (3359, 2937, 0) -> (3363, 2937, 0) | `[493] [opnpc3_v2] npc=(index=28406, id=magic_carpet_seller3 (19), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 528 | (3384, 2916, 0) -> (3387, 2916, 0) | `[493] [opnpc3_v2] npc=(index=28406, id=magic_carpet_seller3 (19), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 529 | (3387, 2916, 0) -> (3390, 2916, 0) | `[493] [opnpc3_v2] npc=(index=28406, id=magic_carpet_seller3 (19), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 530 | (3390, 2916, 0) -> (3393, 2915, 0) | `[493] [opnpc3_v2] npc=(index=28406, id=magic_carpet_seller3 (19), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 531 | (3393, 2915, 0) -> (3396, 2915, 0) | `[493] [opnpc3_v2] npc=(index=28406, id=magic_carpet_seller3 (19), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 532 | (3396, 2915, 0) -> (3399, 2915, 0) | `[493] [opnpc3_v2] npc=(index=28406, id=magic_carpet_seller3 (19), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 533 | (3399, 2915, 0) -> (3401, 2916, 0) | `[493] [opnpc3_v2] npc=(index=28406, id=magic_carpet_seller3 (19), coor` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |

### Session 4486: actions

| first tick x n | action | verdict |
|---|---|---|
| 25 x1 | `[if_buttonx] com=welcome_screen:play (378:72), op=1` | client UI / recorder chrome: engine-generic |
| 28 x2 | `[opnpc1_v2] npc=(id=ics_little_multi_wanderer (6187)), )` | this content: Wanderer, icthlarin.rs2 |
| 41 x1 | `[if_buttonx] com=inventory:items (149:0), obj=2dosestamina (12629), op=2` | generic consumables / the recorder's own items; cat drop is the kitten_pet follower |
| 161 x2 | `[oploc2_v3] id=elid_bankbooth (10517)` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 175 x1 | `[if_buttonx] com=bankmain:items (12:12), obj=growncatobject (1561), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 245 x2 | `[if_buttonx] com=inventory:items (149:0), obj=growncatobject (1561), op=7` | generic consumables / the recorder's own items; cat drop is the kitten_pet follower |
| 249 x1 | `[oploc1_v3] id=icthalarins_tent_door (6581)` | this content: tent line |
| 362 x2 | `[if_buttonx] com=questlist:list (399:7), op=2` | client UI / recorder chrome: engine-generic |
| 369 x2 | `[if_buttonx] com=questjournal:close (119:8), op=1` | client UI / recorder chrome: engine-generic |
| 372 x1 | `[oploc1_v3] id=contact_temple_trapdoor_open (20275)` | town loc: trapdoor refusal, ladders generic maplinks, crates and wheel bound here; spice market default (Nothing interesting, recorded); altar generic pray |
| 384 x3 | `[opnpc1_v2] npc=(id=ics_little_linen1 (4204)), )` | this content: icthlarin_town/embalm/ceremony, contact_jex and plunder Tarik |
| 387 x1 | `[opnpc1_v2] npc=(id=contact_jex (3875), )` | this content: icthlarin_town/embalm/ceremony, contact_jex and plunder Tarik |
| 404 x1 | `[opnpc1_v2] npc=(id=ics_little_linen2 (4205), )` | this content: icthlarin_town/embalm/ceremony, contact_jex and plunder Tarik |
| 411 x1 | `[opnpc1_v2] npc=(id=ics_little_cat (4211)), )` | this content: icthlarin_town/embalm/ceremony, contact_jex and plunder Tarik |
| 428 x2 | `[opnpc1_v2] npc=(id=ics_little_worker1 (11592), )` | this content: icthlarin_town/embalm/ceremony, contact_jex and plunder Tarik |
| 456 x1 | `[opnpc1_v2] npc=(id=ics_little_worker2 (11594), )` | this content: icthlarin_town/embalm/ceremony, contact_jex and plunder Tarik |
| 466 x1 | `[oploc1_v3] id=ntk_pyramid_door_north_multi (26622)` | door / crate / combat-achievements UI: generic, not re-audited (open); the plunder door [466] is the minigame's |
| 480 x4 | `[opnpc2_v2] npc=(id=ics_little_plague_frog (4188)), )` | frog/mummy fights: recorder's attacks (mummy/frog npc configs from cache); pirate lock: RANDOM EVENT, no binding (open) |
| 513 x1 | `[opnpc1_v2] npc=(id=ntk_tarik (1781), )` | this content: icthlarin_town/embalm/ceremony, contact_jex and plunder Tarik |
| 520 x1 | `[if_buttonx] com=inventory:items (149:0), obj=1dosestamina (12631), op=2` | generic consumables / the recorder's own items; cat drop is the kitten_pet follower |
| 524 x8 | `[opnpc1_v2] npc=(id=ics_little_priest (4207), )` | this content: icthlarin_town/embalm/ceremony, contact_jex and plunder Tarik |
| 547 x1 | `[opnpc1_v2] npc=(id=ics_little_hipriest_town (6192)), )` | this content: icthlarin_town/embalm/ceremony, contact_jex and plunder Tarik |
| 557 x5 | `[oploc1_v3] id=contact_ladder (20353)` | town loc: trapdoor refusal, ladders generic maplinks, crates and wheel bound here; spice market default (Nothing interesting, recorded); altar generic pray |
| 571 x1 | `[oploc1_v3] id=contact_ladder_twofloors (20356)` | town loc: trapdoor refusal, ladders generic maplinks, crates and wheel bound here; spice market default (Nothing interesting, recorded); altar generic pray |
| 576 x1 | `[oploc1_v3] id=contact_ladder_top_twofloors (20357)` | town loc: trapdoor refusal, ladders generic maplinks, crates and wheel bound here; spice market default (Nothing interesting, recorded); altar generic pray |
| 582 x1 | `[if_buttonx] com=inventory:items (149:0), obj=ics_little_canopic_jar_liver (4678), op=7` | this content: icthlarin_pyramid/jar/embalm |
| 593 x4 | `[oploc1_v3] id=contact_ladder_top (20354)` | town loc: trapdoor refusal, ladders generic maplinks, crates and wheel bound here; spice market default (Nothing interesting, recorded); altar generic pray |
| 614 x1 | `[opnpc1_v2] npc=(id=ics_little_embalmer (4202), )` | this content: icthlarin_town/embalm/ceremony, contact_jex and plunder Tarik |
| 626 x1 | `[opobj3_v2] id=ics_little_bookofembalming (4686)` | this content: icthlarin_pyramid/jar/embalm |
| 632 x1 | `[if_buttonx] com=inventory:items (149:0), obj=ics_little_bookofembalming (4686), op=2` | this content: icthlarin_pyramid/jar/embalm |
| 742 x1 | `[if_buttonx] com=indexed_book:close_button (680:7), op=1` | this content: icthlarin_pyramid/jar/embalm |
| 759 x3 | `[oploc1_v3] id=icthalarins_suntrap_centre (6606)` | this content: icthlarin_pyramid/jar/embalm |
| 778 x1 | `[oploct_v2] id=contact_sink (20358), , com=inventory:items (149:0), obj=bucket_empty (1925` | town loc: trapdoor refusal, ladders generic maplinks, crates and wheel bound here; spice market default (Nothing interesting, recorded); altar generic pray |
| 789 x1 | `[oploct_v2] id=icthalarins_suntrap_centre (6606), , com=inventory:items (149:0), obj=bucke` | this content: icthlarin_pyramid/jar/embalm |
| 793 x1 | `[if_buttonx] com=inventory:items (149:0), obj=bucket_water (1929), op=6` | this content: fight/rewards; Wingstone (new wingstone.rs2 from recording and wiki); generic inventory ops |
| 802 x2 | `[opnpc1_v2] npc=(id=ics_little_carpenter (4203), )` | this content: icthlarin_town/embalm/ceremony, contact_jex and plunder Tarik |
| 819 x2 | `[oploc1_v3] id=contact_crate_many (20369)` | town loc: trapdoor refusal, ladders generic maplinks, crates and wheel bound here; spice market default (Nothing interesting, recorded); altar generic pray |
| 854 x1 | `[oploc1_v3] id=contact_potterywheel (20375)` | town loc: trapdoor refusal, ladders generic maplinks, crates and wheel bound here; spice market default (Nothing interesting, recorded); altar generic pray |
| 872 x1 | `[if_buttonx] com=wornitems:slot12 (387:24), op=3` | the recorder's worn kit and jewellery teleports (generic; ring/glory routes covered by jewellery tests) |
| 880 x1 | `[if_buttonx] com=inventory:items (149:0), obj=ring_of_dueling_8 (2552), op=3` | the recorder's worn kit and jewellery teleports (generic; ring/glory routes covered by jewellery tests) |
| 882 x2 | `[oploc1_v3] id=castlewars_bankchest (4483)` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 900 x2 | `[if_buttonx] com=bankmain:search (12:42), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 905 x2 | `[if_buttonx] com=bankmain:items (12:12), obj=softclay (1761), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 915 x1 | `[if_buttonx] com=bankmain:tabs (12:10), obj=poh_tablet_kourendteleport (28790), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 917 x1 | `[if_buttonx] com=bankmain:items (12:12), obj=lunar_tablet_catherby_teleport (24961), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 928 x1 | `[if_buttonx] com=bankmain:tabs (12:10), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 929 x1 | `[if_buttonx] com=bankmain:items (12:12), obj=tinderbox (590), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 937 x1 | `[if_buttonx] com=bankmain:items (12:12), obj=amulet_of_ghostspeak (552), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 945 x1 | `[if_buttonx] com=inventory:items (149:0), obj=lunar_tablet_catherby_teleport (24961), op=2` | tablet break (generic) / sap: the sap is this content (evergreen use-on, recorded [985..988]) |
| 971 x1 | `[oploc1_v3] id=tree2 (1278)` | tablet break (generic) / sap: the sap is this content (evergreen use-on, recorded [985..988]) |
| 985 x1 | `[oploct_v2] id=evergreen (2091), , com=inventory:items (149:0), obj=knife (946)` | tablet break (generic) / sap: the sap is this content (evergreen use-on, recorded [985..988]) |
| 1002 x2 | `[if_buttonx] com=inventory:items (149:0), obj=amulet_of_ghostspeak (552), op=3` | the recorder's worn kit and jewellery teleports (generic; ring/glory routes covered by jewellery tests) |
| 1009 x1 | `[if_subop] com=inventory:items (149:0), op=6, subop=3, obj=amulet_of_glory_3 (1710)` | the recorder's worn kit and jewellery teleports (generic; ring/glory routes covered by jewellery tests) |
| 1031 x1 | `[if_buttonx] com=inventory:items (149:0), obj=amulet_of_glory_2 (1708), op=3` | the recorder's worn kit and jewellery teleports (generic; ring/glory routes covered by jewellery tests) |
| 1042 x1 | `[if_buttonx] com=inventory:items (149:0), obj=4dosestamina (12625), op=2` | generic consumables / the recorder's own items; cat drop is the kitten_pet follower |
| 1050 x1 | `[if_buttonx] com=inventory:items (149:0), obj=water_skin0 (1831), op=7` | generic consumables / the recorder's own items; cat drop is the kitten_pet follower |
| 1051 x1 | `[if_buttonx] com=inventory:items (149:0), obj=water_skin1 (1829), op=7` | generic consumables / the recorder's own items; cat drop is the kitten_pet follower |
| 1062 x1 | `[opnpc3_v2] npc=(id=shantay (4642), )` | generic shop/desert: not re-audited in this port (open) |
| 1068 x1 | `[if_buttonx] com=shopmain:items (300:16), obj=water_skin4 (1823), op=1` | generic shop/desert: not re-audited in this port (open) |
| 1069 x3 | `[if_buttonx] com=shopmain:items (300:16), obj=water_skin4 (1823), op=2` | generic shop/desert: not re-audited in this port (open) |
| 1073 x1 | `[if_buttonx] com=shopmain:items (300:16), obj=shantay_pass (1854), op=2` | generic shop/desert: not re-audited in this port (open) |
| 1076 x1 | `[oploc1_v3] id=shantay_pass_henge_doorway (4031)` | generic shop/desert: not re-audited in this port (open) |
| 1081 x1 | `[opnpc3_v2] npc=(id=magic_carpet_seller1 (17), )` | magic carpet: bound (carpet_travel); route and fare not re-audited (open) |
| 1162 x1 | `[if_buttonx] com=logout:logout (182:8), op=1` | client UI / recorder chrome: engine-generic |

### Session 4486: teleports

| tick | from -> to | caused by | verdict |
|---|---|---|---|
| 300 | (3315, 2849, 0) -> (3294, 2782, 0) | `[256] [opnpc1_v2] npc=(index=27562, id=ics_little_multi_wanderer (6187` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 561 | (3285, 2768, 0) -> (3283, 2768, 1) | `[557] [oploc1_v3] id=contact_ladder (20353), coord=(3284, 2768, 0)` | generic ladder maplink: landing not diffed against ours (open) |
| 574 | (3283, 2772, 1) -> (3283, 2770, 3) | `[571] [oploc1_v3] id=contact_ladder_twofloors (20356), coord=(3283, 27` | generic ladder maplink: landing not diffed against ours (open) |
| 577 | (3283, 2770, 3) -> (3283, 2772, 1) | `[576] [oploc1_v3] id=contact_ladder_top_twofloors (20357), coord=(3283` | generic ladder maplink: landing not diffed against ours (open) |
| 596 | (3283, 2768, 1) -> (3285, 2768, 0) | `[593] [oploc1_v3] id=contact_ladder_top (20354), coord=(3284, 2768, 1)` | generic ladder maplink: landing not diffed against ours (open) |
| 622 | (3283, 2756, 0) -> (3283, 2754, 1) | `[618] [oploc1_v3] id=contact_ladder (20353), coord=(3283, 2755, 0)` | generic ladder maplink: landing not diffed against ours (open) |
| 748 | (3283, 2754, 1) -> (3283, 2756, 0) | `[744] [oploc1_v3] id=contact_ladder_top (20354), coord=(3283, 2755, 1)` | generic ladder maplink: landing not diffed against ours (open) |
| 816 | (3316, 2768, 0) -> (3316, 2766, 1) | `[806] [oploc1_v3] id=contact_ladder (20353), coord=(3316, 2767, 0)` | generic ladder maplink: landing not diffed against ours (open) |
| 832 | (3315, 2777, 1) -> (3315, 2775, 2) | `[826] [oploc1_v3] id=contact_ladder (20353), coord=(3315, 2776, 1)` | generic ladder maplink: landing not diffed against ours (open) |
| 836 | (3315, 2775, 2) -> (3315, 2777, 1) | `[835] [oploc1_v3] id=contact_ladder_top (20354), coord=(3315, 2776, 2)` | generic ladder maplink: landing not diffed against ours (open) |
| 848 | (3316, 2766, 1) -> (3316, 2768, 0) | `[842] [oploc1_v3] id=contact_ladder_top (20354), coord=(3316, 2767, 1)` | generic ladder maplink: landing not diffed against ours (open) |
| 875 | (3309, 2775, 0) -> (2441, 3090, 0) | `[872] [if_buttonx] com=wornitems:slot12 (387:24), op=3` | recorder jewellery/tablet: generic, not re-audited here (open) |
| 949 | (2443, 3083, 0) -> (2801, 3449, 0) | `[945] [if_buttonx] com=inventory:items (149:0), sub=14, obj=lunar_tabl` | recorder jewellery/tablet: generic, not re-audited here (open) |
| 1012 | (2794, 3470, 0) -> (3293, 3163, 0) | `[1009] [if_subop] com=inventory:items (149:0), op=6, subop=3, sub=16, ` | recorder jewellery/tablet: generic, not re-audited here (open) |
| 1095 | (3308, 3110, 0) -> (3308, 3108, 0) | `[1081] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coo` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 1096 | (3308, 3108, 0) -> (3308, 3106, 0) | `[1081] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coo` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 1097 | (3308, 3106, 0) -> (3311, 3106, 0) | `[1081] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coo` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 1104 | (3319, 3097, 0) -> (3319, 3094, 0) | `[1081] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coo` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 1107 | (3322, 3091, 0) -> (3322, 3088, 0) | `[1081] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coo` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 1108 | (3322, 3088, 0) -> (3322, 3085, 0) | `[1081] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coo` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 1111 | (3325, 3082, 0) -> (3325, 3079, 0) | `[1081] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coo` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 1112 | (3325, 3079, 0) -> (3325, 3076, 0) | `[1081] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coo` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 1113 | (3325, 3076, 0) -> (3325, 3073, 0) | `[1081] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coo` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 1125 | (3343, 3055, 0) -> (3343, 3052, 0) | `[1081] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coo` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 1126 | (3343, 3052, 0) -> (3343, 3049, 0) | `[1081] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coo` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 1129 | (3346, 3046, 0) -> (3346, 3043, 0) | `[1081] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coo` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 1130 | (3346, 3043, 0) -> (3346, 3040, 0) | `[1081] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coo` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 1131 | (3346, 3040, 0) -> (3346, 3037, 0) | `[1081] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coo` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 1132 | (3346, 3037, 0) -> (3346, 3034, 0) | `[1081] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coo` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 1133 | (3346, 3034, 0) -> (3346, 3031, 0) | `[1081] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coo` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 1134 | (3346, 3031, 0) -> (3346, 3028, 0) | `[1081] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coo` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 1135 | (3346, 3028, 0) -> (3346, 3025, 0) | `[1081] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coo` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 1136 | (3346, 3025, 0) -> (3346, 3022, 0) | `[1081] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coo` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 1137 | (3346, 3022, 0) -> (3346, 3019, 0) | `[1081] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coo` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 1138 | (3346, 3019, 0) -> (3346, 3016, 0) | `[1081] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coo` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 1139 | (3346, 3016, 0) -> (3346, 3013, 0) | `[1081] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coo` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 1140 | (3346, 3013, 0) -> (3346, 3010, 0) | `[1081] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coo` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 1141 | (3346, 3010, 0) -> (3346, 3007, 0) | `[1081] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coo` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 1145 | (3350, 3004, 0) -> (3349, 3003, 0) | `[1081] [opnpc3_v2] npc=(index=27676, id=magic_carpet_seller1 (17), coo` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |

### Session 4487: actions

| first tick x n | action | verdict |
|---|---|---|
| 4 x1 | `[if_buttonx] com=welcome_screen:play (378:72), op=1` | client UI / recorder chrome: engine-generic |
| 51 x2 | `[if_buttonx] com=orbs:worldmap (160:55), op=2` | client UI / recorder chrome: engine-generic |
| 64 x1 | `[if_buttonx] com=worldmap:close (595:38), op=1` | client UI / recorder chrome: engine-generic |
| 94 x1 | `[opnpc3_v2] npc=(id=magic_carpet_seller3 (19), )` | magic carpet: bound (carpet_travel); route and fare not re-audited (open) |
| 155 x1 | `[opnpc3_v2] npc=(id=elid_choc_ice_seller (4761), )` | generic shop/desert: not re-audited in this port (open) |
| 159 x1 | `[if_buttonx] com=shopmain:items (300:16), obj=chocolate_bar (1973), op=1` | generic shop/desert: not re-audited in this port (open) |
| 168 x2 | `[oploc2_v3] id=elid_bankbooth (10517)` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 179 x1 | `[if_buttonx] com=bankmain:tabs (12:10), obj=4doseprayerrestore (2434), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 182 x2 | `[if_buttonx] com=bankmain:items (12:12), obj=4doseprayerrestore (2434), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 184 x1 | `[if_buttonx] com=bankmain:tabs (12:10), obj=lobster (379), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 186 x1 | `[if_buttonx] com=bankmain:items (12:12), obj=lobster (379), op=4` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 192 x3 | `[if_buttonx] com=inventory:items (149:0), obj=amulet_of_glory_2 (1708), op=3` | the recorder's worn kit and jewellery teleports (generic; ring/glory routes covered by jewellery tests) |
| 195 x3 | `[if_buttonx] com=inventory:items (149:0), obj=amulet_of_ghostspeak (552), op=3` | the recorder's worn kit and jewellery teleports (generic; ring/glory routes covered by jewellery tests) |
| 198 x1 | `[if_buttonx] com=inventory:items (149:0), obj=4doseprayerrestore (2434), op=2` | generic consumables / the recorder's own items; cat drop is the kitten_pet follower |
| 217 x3 | `[opnpc1_v2] npc=(id=aluft_gnome_explorer_safari (2552), )` | this content: fight/rewards; Wingstone (new wingstone.rs2 from recording and wiki); generic inventory ops |
| 252 x2 | `[if_buttonx] com=inventory:items (149:0), obj=3dosestamina (12627), op=2` | generic consumables / the recorder's own items; cat drop is the kitten_pet follower |
| 286 x1 | `[oploc1_v3] id=icthalarins_tent_door (6581)` | this content: tent line |
| 305 x11 | `[if_buttonx] com=questlist:list (399:7), op=2` | client UI / recorder chrome: engine-generic |
| 310 x1 | `[if_buttonx] com=questjournal_overview:close (782:9), op=1` | client UI / recorder chrome: engine-generic |
| 328 x1 | `[opnpc1_v2] npc=(id=magic_carpet_seller6 (1321)), )` | Rug Merchant talk: pages checked present (magic_carpet.rs2) |
| 367 x1 | `[opnpc1_v2] npc=(id=ics_little_priestdoorman (4208), )` | this content: icthlarin_town/embalm/ceremony, contact_jex and plunder Tarik |
| 368 x2 | `[oploc1_v3] id=sophanem_gate_left (44036)` | this content: gate refusal in icthlarin_town.rs2 |
| 389 x3 | `[oploc1_v3] id=icthalarins_waters_edge (6605)` | this content: icthlarin_pyramid/jar/embalm |
| 399 x4 | `[if_buttonx] com=inventory:items (149:0), obj=lobster (379), op=2` | generic consumables / the recorder's own items; cat drop is the kitten_pet follower |
| 511 x1 | `[if_buttonx] com=bankmain:tabs (12:10), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 513 x1 | `[if_buttonx] com=bankmain:items (12:12), obj=bucket_empty (1925), op=1` | generic bank: the recorder's preparation (verdict: bank content, not this quest; no binding change) |
| 518 x2 | `[if_buttonx] com=inventory:items (149:0), obj=2dosestamina (12629), op=2` | generic consumables / the recorder's own items; cat drop is the kitten_pet follower |
| 519 x5 | `[if_buttonx] com=orbs:runbutton (160:28), op=1` | client UI / recorder chrome: engine-generic |
| 610 x4 | `[oploc1_v3] id=ics_little_entrance_multi (6621)` | this content: icthlarin_pyramid/jar/embalm |
| 632 x8 | `[opnpc1_v2] npc=(id=ics_little_spectre (1994)), )` | this content: icthlarin_town/embalm/ceremony, contact_jex and plunder Tarik |
| 758 x8 | `[opnpc1_v2] npc=(id=ics_little_sphinx (4209), )` | this content: icthlarin_town/embalm/ceremony, contact_jex and plunder Tarik |
| 762 x1 | `[opnpc1_v2] npc=(id=ntk_tarik (1781), )` | this content: icthlarin_town/embalm/ceremony, contact_jex and plunder Tarik |
| 884 x8 | `[if_buttonx] com=questjournal:close (119:8), op=1` | client UI / recorder chrome: engine-generic |
| 887 x6 | `[oploc1_v3] id=icthalarins_temple_door (6614)` | this content: icthlarin_pyramid/jar/embalm |
| 937 x8 | `[if_buttonx] com=prayerbook:prayer15 (541:23), op=1` | generic prayer: recorder's kit |
| 948 x2 | `[if_buttonx] com=inventory:items (149:0), obj=1dosestamina (12631), op=2` | generic consumables / the recorder's own items; cat drop is the kitten_pet follower |
| 990 x12 | `[oploc2_v3] id=ics_little_pit_to (6632)` | this content: icthlarin_pyramid/jar/embalm |
| 1012 x3 | `[oploc1_v3] id=ics_chestclosed (6646)` | this content: icthlarin_pyramid/jar/embalm |
| 1049 x5 | `[if_buttonx] com=inventory:items (149:0), obj=growncatobject (1561), op=7` | generic consumables / the recorder's own items; cat drop is the kitten_pet follower |
| 1064 x1 | `[oploc2_v3] id=ics_chestopen (6647)` | this content: icthlarin_pyramid/jar/embalm |
| 1202 x1 | `[if_buttonx] com=inventory:items (149:0), obj=4dosestamina (12625), op=2` | generic consumables / the recorder's own items; cat drop is the kitten_pet follower |
| 1205 x8 | `[oploc1_v3] id=icthalarins_ancient_temple_door_2 (44060)` | this content: icthlarin_pyramid/jar/embalm |
| 1224 x10 | `[oploc1_v3] id=icthalarins_ancient_temple_door_1 (44059)` | this content: icthlarin_pyramid/jar/embalm |
| 1506 x1 | `[opnpc1_v2] npc=(id=macro_combilock_pirate (5426), )` | frog/mummy fights: recorder's attacks (mummy/frog npc configs from cache); pirate lock: RANDOM EVENT, no binding (open) |
| 1521 x3 | `[if_buttonx] com=pirate_combilock:centre_down (26:9), op=1` | frog/mummy fights: recorder's attacks (mummy/frog npc configs from cache); pirate lock: RANDOM EVENT, no binding (open) |
| 1525 x1 | `[if_buttonx] com=pirate_combilock:left_up (26:5), op=1` | frog/mummy fights: recorder's attacks (mummy/frog npc configs from cache); pirate lock: RANDOM EVENT, no binding (open) |
| 1529 x3 | `[if_buttonx] com=pirate_combilock:right_up (26:11), op=1` | frog/mummy fights: recorder's attacks (mummy/frog npc configs from cache); pirate lock: RANDOM EVENT, no binding (open) |
| 1537 x1 | `[if_buttonx] com=pirate_combilock:confirm (26:25), op=1` | frog/mummy fights: recorder's attacks (mummy/frog npc configs from cache); pirate lock: RANDOM EVENT, no binding (open) |
| 1542 x1 | `[if_buttonx] com=inventory:items (149:0), obj=gold_ring (1635), op=7` | frog/mummy fights: recorder's attacks (mummy/frog npc configs from cache); pirate lock: RANDOM EVENT, no binding (open) |
| 1993 x6 | `[opnpc1_v2] npc=(id=ics_little_hipriest_town (6192)), )` | this content: icthlarin_town/embalm/ceremony, contact_jex and plunder Tarik |
| 2219 x7 | `[opnpc1_v2] npc=(id=ics_little_priest (4207), )` | this content: icthlarin_town/embalm/ceremony, contact_jex and plunder Tarik |
| 2226 x2 | `[oploc1_v3] id=contact_high_priest_temple_altar (20377)` | town loc: trapdoor refusal, ladders generic maplinks, crates and wheel bound here; spice market default (Nothing interesting, recorded); altar generic pray |
| 2236 x4 | `[oploc1_v3] id=contact_spicemarket (20348)` | town loc: trapdoor refusal, ladders generic maplinks, crates and wheel bound here; spice market default (Nothing interesting, recorded); altar generic pray |
| 2245 x4 | `[opnpc3_v2] npc=(id=ics_little_embalmer (4202), )` | this content: icthlarin_town/embalm/ceremony, contact_jex and plunder Tarik |
| 2247 x4 | `[opnpc1_v2] npc=(id=ics_little_embalmer (4202), )` | this content: icthlarin_town/embalm/ceremony, contact_jex and plunder Tarik |
| 2263 x1 | `[oploc1_v3] id=icthalarins_suntrap_centre (6606)` | this content: icthlarin_pyramid/jar/embalm |
| 2312 x4 | `[opnpc1_v2] npc=(id=ics_little_linen1 (4204)), )` | this content: icthlarin_town/embalm/ceremony, contact_jex and plunder Tarik |
| 2468 x5 | `[oploc2_v3] id=ics_little_pit_from (6633)` | this content: icthlarin_pyramid/jar/embalm |
| 2549 x2 | `[oploc1_v3] id=ics_little_pot_lungs_multi (6636)` | this content: icthlarin_pyramid/jar/embalm |
| 2557 x2 | `[oploc1_v3] id=ics_little_pot_stomach_multi (6638)` | this content: icthlarin_pyramid/jar/embalm |
| 2561 x4 | `[oploc1_v3] id=ics_little_pot_intestines_multi (6640)` | this content: icthlarin_pyramid/jar/embalm |
| 2565 x14 | `[oploc1_v3] id=ics_sarcophigi_door_2 (44600)` | this content: icthlarin_pyramid/jar/embalm |
| 2601 x3 | `[oploc1_v3] id=ics_little_pot_liver_multi (6634)` | this content: icthlarin_pyramid/jar/embalm |
| 2605 x11 | `[opnpc2_v2] npc=(id=ics_little_het (4195), )` | this content: fight/rewards; Wingstone (new wingstone.rs2 from recording and wiki); generic inventory ops |
| 2725 x2 | `[if_buttonx] com=inventory:items (149:0), obj=ics_little_canopic_jar_liver (4678), op=7` | this content: icthlarin_pyramid/jar/embalm |
| 2755 x1 | `[if_buttonx] com=orbs:cr_button (160:4), op=1` | client UI / recorder chrome: engine-generic |
| 2761 x1 | `[if_buttonx] com=cr_ui:btn_close (372:4), op=1` | door / crate / combat-achievements UI: generic, not re-audited (open); the plunder door [466] is the minigame's |
| 2943 x2 | `[oploc1_v3] id=ics_ladder (6645)` | this content: icthlarin_pyramid/jar/embalm |
| 3211 x1 | `[opnpc1_v2] npc=(id=ics_little_worker1 (11592), )` | this content: icthlarin_town/embalm/ceremony, contact_jex and plunder Tarik |
| 3260 x5 | `[opnpc1_v2] npc=(id=ics_little_carpenter (4203), )` | this content: icthlarin_town/embalm/ceremony, contact_jex and plunder Tarik |
| 3353 x2 | `[opnpc3_v2] npc=(id=ics_little_linen1 (4204)), )` | this content: icthlarin_town/embalm/ceremony, contact_jex and plunder Tarik |
| 3372 x1 | `[opnpc1_v2] npc=(id=ics_little_linen2 (4205), )` | this content: icthlarin_town/embalm/ceremony, contact_jex and plunder Tarik |
| 3395 x1 | `[if_buttonx] com=inventory:items (149:0), obj=ics_little_holy_symbol (4682), op=7` | this content: icthlarin_pyramid/jar/embalm |
| 3541 x1 | `[opnpct] npc=(id=ics_little_spectre (1994)), ), com=inventory:items (149:0), obj=ics_littl` | this content: icthlarin_town/embalm/ceremony, contact_jex and plunder Tarik |
| 3548 x1 | `[opnpct] npc=(id=ics_little_sphinx (4209), ), com=inventory:items (149:0), obj=ics_little_` | this content: icthlarin_town/embalm/ceremony, contact_jex and plunder Tarik |
| 3554 x1 | `[oploct_v2] id=icthalarins_temple_door (6614), , com=inventory:items (149:0), obj=ics_litt` | this content: icthlarin_pyramid/jar/embalm |
| 3848 x1 | `[opnpc2_v2] npc=(id=ics_little_mummy_4 (952), )` | frog/mummy fights: recorder's attacks (mummy/frog npc configs from cache); pirate lock: RANDOM EVENT, no binding (open) |
| 4031 x3 | `[opnpc2_v2] npc=(id=ics_little_possessedpriest (4210), )` | this content: fight/rewards; Wingstone (new wingstone.rs2 from recording and wiki); generic inventory ops |
| 4038 x2 | `[if_buttonx] com=prayerbook:prayer13 (541:21), op=1` | generic prayer: recorder's kit |
| 4126 x1 | `[if_buttonx] com=inventory:items (149:0), obj=3doseprayerrestore (139), op=2` | generic consumables / the recorder's own items; cat drop is the kitten_pet follower |
| 4190 x1 | `[if_buttonx] com=inventory:items (149:0), obj=2doseprayerrestore (141), op=2` | generic consumables / the recorder's own items; cat drop is the kitten_pet follower |
| 4330 x4 | `[opnpc1_v2] npc=(id=ics_little_hipriest_ceremony (11649)), )` | this content: icthlarin_town/embalm/ceremony, contact_jex and plunder Tarik |
| 4340 x2 | `[opobj3_v2] id=4dose1defense (2432)` | generic consumables / the recorder's own items; cat drop is the kitten_pet follower |
| 4348 x1 | `[opobj3_v2] id=bones (526)` | generic consumables / the recorder's own items; cat drop is the kitten_pet follower |
| 4351 x1 | `[if_buttonx] com=inventory:items (149:0), obj=bones (526), op=2` | generic consumables / the recorder's own items; cat drop is the kitten_pet follower |
| 4525 x1 | `[if_buttonx] com=questscroll:close_button (153:16), op=1` | this content: fight/rewards; Wingstone (new wingstone.rs2 from recording and wiki); generic inventory ops |
| 4556 x1 | `[if_buttonx] com=quest_rating:5 (983:10), op=1` | this content: fight/rewards; Wingstone (new wingstone.rs2 from recording and wiki); generic inventory ops |
| 4570 x1 | `[if_buttonx] com=questjournal_overview:content_inner (782:7), op=1` | client UI / recorder chrome: engine-generic |
| 4601 x2 | `[if_buttonx] com=logout:logout (182:8), op=1` | client UI / recorder chrome: engine-generic |

### Session 4487: teleports

| tick | from -> to | caused by | verdict |
|---|---|---|---|
| 108 | (3351, 2942, 0) -> (3351, 2939, 0) | `[94] [opnpc3_v2] npc=(index=28406, id=magic_carpet_seller3 (19), coord` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 111 | (3353, 2937, 0) -> (3356, 2937, 0) | `[94] [opnpc3_v2] npc=(index=28406, id=magic_carpet_seller3 (19), coord` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 112 | (3356, 2937, 0) -> (3359, 2937, 0) | `[94] [opnpc3_v2] npc=(index=28406, id=magic_carpet_seller3 (19), coord` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 113 | (3359, 2937, 0) -> (3363, 2937, 0) | `[94] [opnpc3_v2] npc=(index=28406, id=magic_carpet_seller3 (19), coord` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 128 | (3384, 2916, 0) -> (3387, 2916, 0) | `[94] [opnpc3_v2] npc=(index=28406, id=magic_carpet_seller3 (19), coord` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 129 | (3387, 2916, 0) -> (3390, 2916, 0) | `[94] [opnpc3_v2] npc=(index=28406, id=magic_carpet_seller3 (19), coord` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 130 | (3390, 2916, 0) -> (3393, 2915, 0) | `[94] [opnpc3_v2] npc=(index=28406, id=magic_carpet_seller3 (19), coord` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 131 | (3393, 2915, 0) -> (3396, 2915, 0) | `[94] [opnpc3_v2] npc=(index=28406, id=magic_carpet_seller3 (19), coord` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 132 | (3396, 2915, 0) -> (3399, 2915, 0) | `[94] [opnpc3_v2] npc=(index=28406, id=magic_carpet_seller3 (19), coord` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 133 | (3399, 2915, 0) -> (3401, 2916, 0) | `[94] [opnpc3_v2] npc=(index=28406, id=magic_carpet_seller3 (19), coord` | magic carpet flight hop (magic_carpet.rs2; route not re-audited, open) |
| 619 | (3322, 2858, 0) -> (3320, 2796, 0) | `[614] [oploc1_v3] id=ics_little_entrance_multi (6621), coord=(3323, 28` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 896 | (3295, 2781, 0) -> (3277, 9171, 0) | `[887] [oploc1_v3] id=icthalarins_temple_door (6614), coord=(3294, 2778` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 925 | (3295, 9170, 0) -> (3295, 9171, 0) | `[887] [oploc1_v3] id=icthalarins_temple_door (6614), coord=(3294, 2778` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 994 | (3295, 9191, 0) -> (3295, 9193, 0) | `[990] [oploc2_v3] id=ics_little_pit_to (6632), coord=(3295, 9194, 0)` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 996 | (3295, 9193, 0) -> (3295, 9196, 0) | `[990] [oploc2_v3] id=ics_little_pit_to (6632), coord=(3295, 9194, 0)` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 1002 | (3295, 9196, 0) -> (3312, 9180, 0) | `[990] [oploc2_v3] id=ics_little_pit_to (6632), coord=(3295, 9194, 0)` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 1004 | (3312, 9180, 0) -> (3311, 9180, 0) | `[990] [oploc2_v3] id=ics_little_pit_to (6632), coord=(3295, 9194, 0)` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 1184 | (3292, 9191, 0) -> (3292, 9193, 0) | `[1181] [oploc2_v3] id=ics_little_pit_to (6632), coord=(3292, 9194, 0)` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 1186 | (3292, 9193, 0) -> (3292, 9196, 0) | `[1181] [oploc2_v3] id=ics_little_pit_to (6632), coord=(3292, 9194, 0)` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 1188 | (3292, 9196, 0) -> (3292, 9197, 0) | `[1181] [oploc2_v3] id=ics_little_pit_to (6632), coord=(3292, 9194, 0)` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 1265 | (10320, 11056, 0) -> (3294, 2781, 0) | `[1225] [oploc1_v3] id=icthalarins_ancient_temple_door_1 (44059), coord` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 2338 | (3295, 2781, 0) -> (3277, 9171, 0) | `[2329] [oploc1_v3] id=icthalarins_temple_door (6614), coord=(3294, 277` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 2376 | (3307, 9171, 0) -> (3274, 9172, 0) | `[2364] [if_buttonx] com=prayerbook:prayer15 (541:23), op=1` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 2378 | (3274, 9172, 0) -> (3274, 9171, 0) | `[2364] [if_buttonx] com=prayerbook:prayer15 (541:23), op=1` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 2451 | (10332, 13737, 0) -> (3280, 9200, 0) | `[2444] [oploc2_v3] id=ics_little_pit_to (6632), coord=(3291, 9194, 0)` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 2765 | (11104, 13937, 0) -> (3292, 9192, 0) | `[2763] [oploc2_v3] id=ics_little_pit_from (6633), coord=(3295, 9196, 0` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 2779 | (3292, 9191, 0) -> (3292, 9193, 0) | `[2776] [oploc2_v3] id=ics_little_pit_to (6632), coord=(3292, 9194, 0)` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 2781 | (3292, 9193, 0) -> (3292, 9196, 0) | `[2776] [oploc2_v3] id=ics_little_pit_to (6632), coord=(3292, 9194, 0)` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 2783 | (3292, 9196, 0) -> (3292, 9197, 0) | `[2776] [oploc2_v3] id=ics_little_pit_to (6632), coord=(3292, 9194, 0)` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 2868 | (3291, 9199, 0) -> (3291, 9197, 0) | `[2864] [oploc2_v3] id=ics_little_pit_from (6633), coord=(3291, 9196, 0` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 2870 | (3291, 9197, 0) -> (3291, 9194, 0) | `[2864] [oploc2_v3] id=ics_little_pit_from (6633), coord=(3291, 9196, 0` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 2872 | (3291, 9194, 0) -> (3291, 9193, 0) | `[2864] [oploc2_v3] id=ics_little_pit_from (6633), coord=(3291, 9196, 0` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 2948 | (12813, 14483, 0) -> (3294, 2781, 0) | `[2945] [oploc1_v3] id=ics_ladder (6645), coord=(3277, 9172, 0)` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 3561 | (3295, 2781, 0) -> (3277, 9171, 0) | `[3556] [oploc1_v3] id=icthalarins_temple_door (6614), coord=(3294, 277` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 3633 | (13403, 295, 0) -> (3295, 9198, 0) | `[3628] [oploc2_v3] id=ics_little_pit_to (6632), coord=(3292, 9194, 0)` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 3690 | (10351, 683, 0) -> (3292, 9192, 0) | `[3689] [oploc1_v3] id=ics_sarcophigi_door_2 (44600), coord=(3312, 9197` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 3749 | (3292, 9191, 0) -> (3292, 9193, 0) | `[3746] [oploc2_v3] id=ics_little_pit_to (6632), coord=(3292, 9194, 0)` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 3751 | (3292, 9193, 0) -> (3292, 9196, 0) | `[3746] [oploc2_v3] id=ics_little_pit_to (6632), coord=(3292, 9194, 0)` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 3757 | (3292, 9196, 0) -> (3312, 9180, 0) | `[3746] [oploc2_v3] id=ics_little_pit_to (6632), coord=(3292, 9194, 0)` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 3759 | (3312, 9180, 0) -> (3311, 9180, 0) | `[3746] [oploc2_v3] id=ics_little_pit_to (6632), coord=(3292, 9194, 0)` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 3917 | (3292, 9191, 0) -> (3292, 9193, 0) | `[3917] [oploc2_v3] id=ics_little_pit_to (6632), coord=(3292, 9194, 0)` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 3919 | (3292, 9193, 0) -> (3292, 9196, 0) | `[3917] [oploc2_v3] id=ics_little_pit_to (6632), coord=(3292, 9194, 0)` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 3921 | (3292, 9196, 0) -> (3292, 9197, 0) | `[3917] [oploc2_v3] id=ics_little_pit_to (6632), coord=(3292, 9194, 0)` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 4005 | (12650, 685, 0) -> (3306, 9200, 0) | `[3964] [oploc1_v3] id=icthalarins_ancient_temple_door_2 (44060), coord` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 4414 | (13418, 1263, 0) -> (3282, 9171, 0) | `[4405] [oploc1_v3] id=icthalarins_ancient_temple_door_2 (44060), coord` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |
| 4431 | (11663, 2195, 0) -> (3294, 2781, 0) | `[4405] [oploc1_v3] id=icthalarins_ancient_temple_door_2 (44060), coord` | quest-script move (pit run-up/jump/fall, memory teleports, copies, door/ladder), built in icthlarin_pyramid/jar/ceremony |

## Dialogue sources (every page or branch)

Source key: **recording** (the page is on the wire), **wiki** (`Transcript:Icthlarin's Little Helper`, `Transcript:<npc>` raw; moods unverified), **neither** (removed). Mechanical check over all `.rs2`: `check_pages.py` (scratchpad): every recorded page's text with `<br>` as space, and every wiki `'''Speaker:'''`/tbox/topt/tselect line.

- Recording: **732 pages** checked, **3 not found**:   - MISSING (4481, '396', 'MENU', 'Carpet rides cost 100 coins.', 'Carpet rides cost 100 coins.')
  - MISSING (4486, '446', 'npc', 'chatneu2 Worker', 'I would take a dermatological irritation over back- breaking, mind-numbing labour any day!')
  - MISSING (4487, '1506', 'npc', "chatneu2 Capt' Arnav", "Ah, hello there, laddie! I've just dug up an old treasure chest of mine.")
  (Carpet title and the Worker's "back-<br>breaking" are false misses: the carpet title is composed from `carpet_fare`; the hyphenated wrap keeps the source word whole. The Capt' Arnav rows are the random event, open.)
- Wiki: **1072 lines** checked. Not in our scripts other than the pet transcript's 55 `Transcript:Cat` lines (pet system, open):

| source | section | speaker | text | verdict |
|---|---|---|---|---|
| ('wiki_transcript.txt', 175, 'Talking to the Embalmer', 'say:Embalmer', "Clear off you evil body snatcher! You won't get any of my wares.") |
| ('wiki_transcript.txt', 861, 'Talking to the Carpenter after aiding bo', 'say:Carpenter', "Well you should as soon as possible. He'll be down in the pyramid by now.") |
| ('Capt_Arnav.txt', 4, 'Standard dialogue', "say:Capt' Arnav", "[Ah, hello there, laddie/Ah, hello there, lassie/Ah, hello there]! I've just dug up an old treasure chest of mine.") |
| ('Capt_Arnav.txt', 41, 'Overhead dialogue', "say:Capt' Arnav", 'Avast there, [player name]!') |
| ('Capt_Arnav.txt', 45, 'Overhead dialogue', "say:Capt' Arnav", 'Ahoy, [player name]!') |
| ('Capt_Arnav.txt', 49, 'Overhead dialogue', "say:Capt' Arnav", 'I needs yer help, [player name]!') |
| ('Capt_Arnav.txt', 51, 'Overhead dialogue', "say:Capt' Arnav", 'I got treasure here, [player name]!') |
| ('Capt_Arnav.txt', 55, 'Overhead dialogue', "say:Capt' Arnav", 'Will ye help an old pirate, [player name]?') |
| ('Capt_Arnav.txt', 57, 'Overhead dialogue', "say:Capt' Arnav", "I knows somethin' you should knows, [player name].") |
| ('Kitten.txt', 5, 'Without a catspeak amulet', 'say:Player', "Hey kitty. What's new?") |
| ('Kitten.txt', 14, 'With a catspeak amulet', 'say:Kitten', "Meeow I'm happy.") |
| ('Kitten.txt', 16, 'With a catspeak amulet', 'say:Kitten', "I'm really hungry, do you have any fish, meeaow?") |
| ('Kitten.txt', 18, 'With a catspeak amulet', 'say:Kitten', "You don't think I'm cute and cuddly any more, do you? Is there another cat you like?") |
| ('Kitten.txt', 23, 'With a catspeak amulet', 'say:Kitten', "I'm less than a month old.") |
| ('Kitten.txt', 26, 'With a catspeak amulet', 'say:Kitten', "I'm just a month old.") |
| ('Kitten.txt', 29, 'With a catspeak amulet', 'say:Kitten', "I'm [x] months old. ''(Month count differs based on kitten's current growth.)''") |
| ('Kitten.txt', 32, 'With a catspeak amulet', 'say:Kitten', "I'm almost fully grown!") |
| ('Kitten.txt', 34, 'With a catspeak amulet', 'box', 'Age of your kitten: [x] hour(s) [y] minutes. Approximate time until fully adult: [z] hour(s) [a] minutes, assuming you look after ') |
| ('Kitten.txt', 40, 'With a catspeak amulet', 'say:Kitten', "Can we go visit Gertrude, I'd like to see her again, she's very nice.") |
| ('Kitten.txt', 42, 'With a catspeak amulet', 'say:Kitten', "Let's go to the ratpits in Ardougne. I think I fancy a fight!") |
| ('Kitten.txt', 44, 'With a catspeak amulet', 'say:Kitten', "Let's stay here. I think I fancy a fight.") |
| ('Kitten.txt', 45, 'With a catspeak amulet', 'say:Player', "I'll think about it, cat.") |
| ('Kitten.txt', 47, 'With a catspeak amulet', 'say:Kitten', 'Lets go to the sewers of Varrock.') |
| ('Kitten.txt', 49, 'With a catspeak amulet', 'say:Kitten', 'Can we stay here in the sewers and hunt rats? That would be fun.') |
| ('Kitten.txt', 55, 'With a catspeak amulet', 'say:Kitten', 'I want to chase things.') |
| ('Kitten.txt', 57, 'With a catspeak amulet', 'say:Kitten', "I'm tired, I'd like to have a nap.") |
| ('Kitten.txt', 89, 'Without a catspeak amulet', 'box', 'After taking a good look at your kitten you guess that its age is: [x] hours [y] minutes. And approximate time until fully adult: ') |
| ('Kitten.txt', 106, 'With a catspeak amulet', 'say:Player', "Who's Gielinor's most ferocious kitten?") |
| ('Kitten.txt', 107, 'With a catspeak amulet', 'say:Kitten', 'Me! Me! {{color/blue/~ Roar!}}') |
| ('Kitten.txt', 108, 'With a catspeak amulet', 'say:Player', 'Aww! So squeaky!') |
| ('Kitten.txt', 124, 'With a catspeak amulet', 'say:Player', "I'm just fed up with you, you're so needy! Can't you just look after yourself for a bit?") |
| ('Kitten.txt', 125, 'With a catspeak amulet', 'say:Kitten', "Meeeeow, don't leave me master, I'm sorry, I'll be a good kitty from now on meoooww.") |
| ('Kitten.txt', 126, 'With a catspeak amulet', 'say:Player', "No I've really had it with you, now scat!") |
| ('Kitten.txt', 146, 'Chasing rats or hellrats', 'say:Player', 'Hey well done puss, you got it!') |
| ('Kitten.txt', 152, 'Chasing crypt rats', 'say:Player', 'Do you fancy a bit of hunting?') |
| ('Kitten.txt', 153, 'Chasing crypt rats', 'say:Kitten', 'Meoowww? The nasty rat is too big for me, I might be able to take it when I grow up a little more.') |
| ('Kitten.txt', 169, 'Kitten becomes very hungry', 'say:Player', "I think it's very hungry!") |
| ('Kitten.txt', 197, 'Bucket of milk', 'say:Kitten', 'Purpurr') |
| ('Kitten.txt', 208, 'Rat Pits== <!--same across all cat types', 'say:Player', 'Hey cat, are you up for some competitive rat hunting?') |
| ('Kitten.txt', 209, 'Rat Pits== <!--same across all cat types', 'say:Kitten', 'Got any last minute advice?') |
| ('Kitten.txt', 211, 'Rat Pits== <!--same across all cat types', 'opt', 'Just take care of yourself cat.') |
| ('Kitten.txt', 212, 'Rat Pits== <!--same across all cat types', 'say:Player', 'Just take care of yourself cat.') |
| ('Kitten.txt', 214, 'Rat Pits== <!--same across all cat types', 'opt', 'Stay in for as long as you can.') |
| ('Kitten.txt', 215, 'Rat Pits== <!--same across all cat types', 'say:Player', 'Stay in for as long as you can.') |
| ('Kitten.txt', 217, 'Rat Pits== <!--same across all cat types', 'opt', 'No guts, no glory!') |
| ('Kitten.txt', 218, 'Rat Pits== <!--same across all cat types', 'say:Player', 'No guts, no glory!') |
| ('Kitten.txt', 220, 'Rat Pits== <!--same across all cat types', 'opt', 'Go berserk!') |
| ('Kitten.txt', 221, 'Rat Pits== <!--same across all cat types', 'say:Player', 'Go berserk!') |
| ('Kitten.txt', 225, 'Victory', 'say:Player', '{{Overhead/Come here kitty.}}') |
| ('Kitten.txt', 226, 'Victory', 'say:Player', 'Well done cat, you won! Those [X] coins should come in handy.') |
| ('Kitten.txt', 231, 'Loss', 'say:Player', "Agh! I can't believe I lost! Stupid cat cost me [X] coins!") |
| ('Kitten.txt', 238, 'After catching 10 rats', 'say:Player', 'Well done puss! 10 horrible rodents caught!') |
| ('Kitten.txt', 242, 'After catching 50 rats', 'say:Player', "Great puss, that's 50 rodents caught!") |
| ('Kitten.txt', 246, 'After catching 100 rats', 'say:Player', "Wow, incredible puss! That's 100 rodents caught!") |
| ('Kitten.txt', 257, 'Overhead dialogue', 'say:Kitten', 'Purrrrr!') |
| ('Kitten.txt', 270, 'When being growled at by a dog', 'say:Kitten', 'Sss!') |
| ('Kitten.txt', 276, 'Historical dialogue', 'say:Player', "Who's Gielinor's most ferocious kitten?") |
| ('Kitten.txt', 277, 'Historical dialogue', 'say:Kitten', 'Me me {{color/blue/~ Roar.}} (It sounds like more of a squeak)') |
| ('Tarik.txt', 23, 'Initial dialogue', 'say:Player', "What's this thing guarding one of them.punctuation") |
| ('Tarik.txt', 39, 'Initial dialogue', 'say:Tarik', "Hey! if you find it, I deserve a share of the profits! You wouldn't have known it existed without my help.") |
| ('Wingstone.txt', 32, 'Master', 'say:Wingstone', 'Here, solve this.') |
| ('Wingstone.txt', 33, 'Master', 'box', 'Wingstone hands you a [light box/puzzlebox]!') |
| ('Wingstone.txt', 38, 'While holding the light or puzzle box', 'say:Wingstone', "That doesn't look solved to me.") |
| ('Wingstone.txt', 41, 'While holding the light or puzzle box', 'say:Wingstone', 'Well done [player name]. Here you go.') |
| ('Wingstone.txt', 42, 'While holding the light or puzzle box', 'box', "You've obtained a [new clue/casket]!") |
| ('Wingstone.txt', 47, 'Gnome Restaurant dialogue', 'box', "Although you have found your customer you don't appear to have brought the correct food with you! You need [a/some] [ordered gnome") |
| ('Wingstone.txt', 50, 'Gnome Restaurant dialogue', 'box', "Premade dishes will not do I'm afraid. Only hand made Gnome Cuisine is good enough for customers of Aluft Aloft.") |
| ('Wingstone.txt', 53, 'Gnome Restaurant dialogue', 'box', 'You hand over your delivery of [a/some] [ordered gnome food item] to Wingstone.') |
| ('Wingstone.txt', 54, 'Gnome Restaurant dialogue', 'say:Wingstone', 'This will hit the spot. Oh yes. Thanks.') |
| ('Wingstone.txt', 56, 'Gnome Restaurant dialogue', 'say:Wingstone', "Now that I have some proper food, I won't need this any more.") |
| ('Wingstone.txt', 57, 'Gnome Restaurant dialogue', 'box', 'You are given some mintcake as a tip.') |
| ('Wingstone.txt', 59, 'Gnome Restaurant dialogue', 'say:Wingstone', 'Here, take these Grand Seed pods. Very useful in emergencies!') |
| ('Wingstone.txt', 60, 'Gnome Restaurant dialogue', 'box', 'You are given some seed pods as a tip.') |
| ('Wingstone.txt', 62, 'Gnome Restaurant dialogue', 'say:Wingstone', 'I was given this odd device, maybe you can do something with it.') |
| ('Wingstone.txt', 63, 'Gnome Restaurant dialogue', 'box', 'You are given a snake flute as a tip.') |
| ('Wingstone.txt', 65, 'Gnome Restaurant dialogue', 'say:Wingstone', "You must have had to travel a long way to get this to me. Here's a little something extra as a tip.") |
| ('Wingstone.txt', 66, 'Gnome Restaurant dialogue', 'box', 'You are given [a/some] [reward item(s)] as a tip.') |
| ('Wingstone.txt', 69, 'Gnome Restaurant dialogue', 'box', "Your delivery is too late! Go back to Aluft jnr. to get another order. You discard your delivery box as you don't need it any more") |

Verdicts on those: "Dialogue N" are the wiki's transcript labels, not menus; Embalmer "!" / Carpenter "do so" lines: the recording wins; potion lines for the High Priest sharing the Priest page; **Tarik "played the minigame" branch and Tarik's items** (no played-flag in plunder.rs2), **Eldritch ashes**, **the south-eastern gate priest menu** (no npc of ours stands there), **Wingstone seed pod / clue / restaurant lines** (other content: open).

**Neither, removed:** "Maybe I should go and see what that wanderer wants first."; "I have no interest in speaking with you." at stage < 3; "Thanks for nothing."; "Not yet." / "Well you should as soon as possible." (replaced by the wiki's jar reminder); "Icthlarin watch over you."; "Hello. I'm sure the ceremony went well."; the Sophanem town High Priest at stages 16..24 (no source: he says nothing, he is down in the pyramid); and the whole old invented file bodies (ladder texts, "Thank you. Now... I need a tinderbox", the invented Sphinx/High Priest/Embalmer/Carpenter pages, the ceremony's invented mesboxes).

Branch sources by npc: Wanderer no cat [4481 889..938] recording; cat [4486 256..328] recording; stage-1 re-talk, "No." at the start and no-supplies path wiki. Klenter: recorded random lines + wiki list and amulet branch + wiki post-quest. Sphinx: stage 3 recording, re-talk wiki, riddle recording + wiki wrong-answer, token menu recording, retalk with lost token wiki, mocking page recording, post-quest menu wiki. High Priest: stage 3 and token recording; no-token wiki; jar reminder wiki; ceremony talk recording; Icthlarin topic wiki; post-quest recording plus Contact offer recording; post-Contact, post-BCS wiki. Priests: stage 3, 7, 16 recording; 6, post-ceremony wiki; gate priest recording and wiki. Raetul, Siamun, Workers, Embalmer, Carpenter: recorded stages + wiki branches and post-quest transcript pages. Jex: recording stage 3, wiki standard menu. Moods on wiki-only lines are unverified.

## Journal

`icthlarin_journal` uses the recorded text verbatim: stage 3 ([4486 362], [4486 950], [874]), 15 ([3237], [3264], [3299]), 16 ([3458]), 23/24 ([4370], [4393]), 25 ([4460]), struck paragraphs from the same recordings. Stages 1, 2, 4..7, 8..14, 17..22 and the completed journal have no recording: their current-step text is unrecorded (`Transcript:Icthlarin's_Little_Helper/Journal` does not exist on the wiki; kept minimal, in the recorded voice, UNVERIFIED). Mechanical check: 306 of 311 recorded rows are in the script text verbatim; the 5 others are the stage-3 paragraph, built by concatenating literals (identical when joined); open.

## Start and end
Combat 50 warning [4486 287] + "Start the Icthlarin's Little Helper quest?" [289] Yes/No; stage 1 and page; completion on the High Priest's page [4518..4519]: amulet, jingle `quest_complete_1` (152), scroll lines "2 Quest Points", XP 4,500/4,000/4,000, catspeak, Sophanem, carpet; the scroll blocks with `if_addresumebutton`, pages resume after close [4526]; Contact! offer [4543..4544] Yes/No (Contact! proper is that port's).

## Fights (13)
Mummies, frogs, locusts: the recorder's own swings, cache stats. Possessed Priest: 60 shots endtime 90: fire 22, water 21, wind 17, earth 0 (`random(3)`); the fourth spell removed, wiki notwithstanding. Damage on the recorder (`rsprox_player_hits.py --summary`): priest strikes prayed magic 43 of 43 zero (fire 17, water 14, wind 12); mummy swings prayed melee 29 zeros of 30 (one 5), unprayed 2 of 4 hit (max 4); no unprayed sample of the priest, so **max hits (wiki 1/2/6) stay wiki, unverified**. Apparition Het recorded: `human_spear_lunge` 4, `human_spear_block` 20, `human_death`, `staff_double` attack sound, death to `[del]` 2 ticks; config set. The other three Apparitions: anims unrecorded (old wiki-based configs).
No bosses in this quest (`boss_fights_unaudited` empty).

## Wire audit (sections; R / ours / verdict)

Field by field (`fieldiff.py`): every synth and area sound with delay, seq with delay, spotanim height/delay, projectile starttime/endtime, loc anim. M = matched (R/ours), R = recording only, O = ours only, NEAR = same name, other values. REC-only rows in the first memory and the third memory are the recorder's stumbling (wall crusher, pitfall, chest, door refusals, pit falls) that the scripted test path does not do, or are recorder kit: built, but not exercised on our tick log. `jingle 0` is the tool's column (our rows carry id 192 and 152 in the following fields).

```
=== wanderer+hypnosis rec 4486[256..328] ours[56..113] matched 3 rec-only 0 ours-only 0
  M: pick2 0(1/1), hypnotise 0(1/1), viking_drop_to_knee_and_backtoready 0(1/1)
  R: 
  O: 
=== first memory rec 4487[887..1276] ours[133..198] matched 9 rec-only 25 ours-only 0
  M: hypnotise 0(2/2), ic_running 30(2/1), pit_jump 10(2/1), ic_pit_jump 0(2/1), ic_up_ons 0(1/1), pit_land 0(1/1), tile_reset 0(1/1), tile_move 0(24/10), unlock 0(1/1)
  R: jingle:tile win  x1; locanim:ics_pit_secret_door  x1; map:deserttreasure_cracks_floor_spotanim 0/0 x1; nseq:mummy_update_sword_attack 0 x4; nseq:scarab_spiral 0 x1; pseq:human_eat 0 x3; pseq:human_openchest 0 x1; pseq:human_stafforb_block 0 x4; pseq:ic_pit_fall 0 x1; pseq:ics_stumble_back 10 x1; synth:cancel_prayer 0 x1; synth:chest_open 0 x1; synth:clock_tick_1 0 x1; synth:eat 0 x1; synth:human_block_1 20 x4; synth:human_hit_2 20 x2; synth:liquid 0 x2; synth:locked 0 x1; synth:mummy_attack 0 x4; synth:pit_fall 10 x1; synth:pit_shoot 0 x1; synth:protect_from_melee 0 x1; synth:scarabs_appear 10 x1; synth:tol_homonculus_crying 0 x1; synth:wall_crushed 0 x1
  O: 
=== High Priest token rec 4487[2004..2217] ours[245..374] matched 0 rec-only 0 ours-only 5
  M: 
  R: 
  O: area:smokepuff 0 x1; area:smokepuff 30 x1; map:smokepuff 124/0 x1; map:smokepuff 124/30 x1; synth:piratechest_coins 0 x1
=== entry and second memory rec 4487[2329..2767] ours[375..513] matched 16 rec-only 35 ours-only 7
  M: happy_meeoow 0(1/1), human_block_1 20(7/4), human_hit_3 20(1/1), scarab_spiral 0(1/1), deserttreasure_cracks_floor_spotanim 0/0(1/1), scarabs_appear 10(1/1), hypnotise 0(2/2), icthalarins_door_opens (2/2), scrape 0(2/2), icthalarins_door_closes (2/2), abyssal_demon_teleport 0/0(1/1), quick_teleport 0(1/1), human_hit_1 20(1/1), human_death 0(1/1), human_death 0(1/1), pick2 0(1/1)
  R: area:firestrike_hit 66 x17; area:spellfail 56 x1; area:spellfail 66 x2; locanim:deserttreasure_pitfall_open  x1; locanim:ics_pit_secret_door  x1; map:comedy_smoke_fall 0/10 x1; nseq:human_spear_block 56 x1; nseq:human_spear_block 66 x19; nseq:human_spear_lunge 0 x4; nseq:mummy_update_sword_attack 0 x4; nspot:failedspell_impact 124/56 x1; nspot:failedspell_impact 124/66 x2; nspot:firestrike_impact 124/66 x17; proj:firestrike_travel 51/56 x1; proj:firestrike_travel 51/66 x19; pseq:human_caststrike_staff_walkmerge 0 x20; pseq:human_eat 0 x2; pseq:human_stafforb_block 0 x8; pseq:ics_comedy_fall 0 x1; pspot:firestrike_casting 92/0 x20; synth:cancel_prayer 0 x1; synth:clock_tick_1 0 x1; synth:eat 0 x1; synth:equip_fun 0 x1; synth:firestrike_cast_and_fire 0 x20; synth:human_hit 0 x17; synth:liquid 0 x1; synth:locked 0 x2; synth:mummy_attack 0 x4; synth:pit_shoot 0 x1; synth:pitfall_fall 10 x1; synth:pitfall_open 0 x1; synth:protect_from_melee 0 x1; synth:staff_double 0 x4; synth:tol_homonculus_crying 0 x1
  O: nseq:human_caststrike 0 x8; nseq:human_unarmedblock 0 x9; pseq:human_sword_def 64 x8; pseq:human_sword_slash 0 x9; synth:hacksword_slash 0 x9; synth:human_hit_2 20 x1; synth:human_hit_4 20 x1
=== pit and jar returned rec 4487[2776..2850] ours[543..622] matched 12 rec-only 1 ours-only 0
  M: ic_running 30(1/2), pit_jump 10(1/2), ic_pit_jump 0(1/2), ic_up_ons 0(1/2), pit_land 0(1/2), tile_reset 0(1/1), tile_move 0(10/12), icthalarins_door_opens (1/2), unlock 0(1/1), scrape 0(1/2), icthalarins_door_closes (1/2), put_down 0(1/1)
  R: jingle:tile win  x1
  O: 
=== ceremony prep rec 4487[3051..3429] ours[633..729] matched 0 rec-only 3 ours-only 0
  M: 
  R: synth:clock_tick_1 0 x1; synth:put_down 0 x1; synth:tol_homonculus_crying 0 x1
  O: 
=== third memory rec 4487[3556..3692] ours[729..788] matched 8 rec-only 8 ours-only 0
  M: happy_meeoow 0(1/1), scarab_spiral 0(1/2), deserttreasure_cracks_floor_spotanim 0/0(1/2), scarabs_appear 10(1/2), hypnotise 0(2/2), icthalarins_door_opens (1/1), scrape 0(1/1), icthalarins_door_closes (1/1)
  R: nseq:mummy_update_sword_attack 0 x3; pseq:human_eat 0 x1; pseq:human_stafforb_block 0 x3; synth:equip_fun 0 x1; synth:human_block_1 20 x1; synth:human_hit_2 20 x2; synth:liquid 0 x1; synth:mummy_attack 0 x3
  O: 
=== ceremony (both passes) rec 4487[3970..4030] ours[814..897] matched 10 rec-only 4 ours-only 4
  M: icthalarins_door_opens (2/2), scrape 0(2/2), icthalarins_door_closes (2/2), abyssal_demon_teleport 0/0(2/2), quick_teleport 0(2/2), put_down 0(1/1), abyssal_demon_teleport 0/20(1/1), quick_teleport 20(1/1), human_caststrike_walkmerge 0(1/1), spellfail 90(1/1)
  R: nspot:firestrike_casting 92/0 x1; proj:firestrike_travel 51/90 x1; pseq:human_stafforb_block 90 x1; synth:firestrike_cast_and_fire 0 x1
  O: nspot:windstrike_casting 92/0 x1; proj:windstrike_travel 51/90 x1; pseq:human_sword_def 90 x1; synth:windstrike_cast_and_fire 0 x1
=== priest fight rec 4487[4030..4325] ours[897..965] matched 18 rec-only 19 ours-only 6
  M: firestrike_casting 92/0(19/3), human_caststrike_walkmerge 0(60/7), firestrike_travel 51/90(22/3), firestrike_cast_and_fire 0(80/3), spellfail 90(17/4), waterstrike_casting 92/0(18/3), waterstrike_travel 51/90(21/3), waterstrike_cast_and_fire 0(21/3), firestrike_hit 90(17/1), windstrike_travel 51/90(17/1), windstrike_cast_and_fire 0(17/1), waterstrike_hit 90(14/2), zap 100/0(11/6), human_castzap 0(13/6), zap 0(13/6), windstrike_casting 92/0(12/1), human_death 0(1/1), human_death 0(1/1)
  R: area:burst 25 x1; area:firestrike_hit 56 x24; area:firework 0 x1; area:spellfail 56 x34; jingle:advance magic2  x1; nseq:human_spear_block 56 x45 NEAR ours 0; nspot:failedspell_impact 124/56 x34; nspot:firestrike_impact 124/56 x24; proj:firestrike_travel 51/56 x58 NEAR ours 51/90; pseq:human_caststrike_staff_walkmerge 0 x58; pseq:human_eat 0 x1; pseq:human_stafforb_block 90 x49; pspot:firestrike_casting 92/0 x58; pspot:levelup_anim 124/0 x1; synth:human_block_1 20 x43; synth:human_hit 0 x24; synth:liquid 0 x2; synth:protect_from_magic 0 x1; synth:windstrike_hit 90 x12
  O: nseq:human_spear_block 0 x14; pseq:human_sword_def 90 x7; pseq:human_sword_slash 0 x14; synth:hacksword_slash 0 x14; synth:human_hit_1 20 x2; synth:human_hit_4 20 x1
=== ceremony talk rec 4487[4331..4405] ours[965..982] matched 0 rec-only 3 ours-only 0
  M: 
  R: pseq:human_pickupfloor 0 x1; synth:bones_down 0 x1; synth:pick2 0 x2
  O: 
=== fourth memory rec 4487[4405..4434] ours[982..1014] matched 1 rec-only 0 ours-only 0
  M: hypnotise 0(2/2)
  R: 
  O: 
=== completion rec 4487[4517..4600] ours[1014..1048] matched 0 rec-only 6 ours-only 0
  M: 
  R: area:burst 25 x2; area:firework 0 x2; jingle:advance agility  x1; jingle:advance woodcutting2  x1; jingle:quest complete 1  x1; pspot:levelup_anim 124/0 x2
  O: 
TOTAL [77, 104, 22]

```

## Audit results (second pass)

- `npc_block_inventory.py`, `multinpc_stance_audit.py` (`icth22`): every MISSING row is a type this run did not reach (locust, frog, three of the four Apparitions: the jar rolls one of them per run; mummies 1, 2, 4; the priests the test does not talk to). No block kind differs on a type the test drove. One stance pair differs: `ics_little_multi_wanderer` over `ics_little_redheadlady` (walk `walk_walkingstick` against the shell's `human_walk_*`); no client photograph was taken, so what our client draws for her is not verified.
- `npc_movement_audit.py defaults`: 0 WANDERS. `paths`: the mummy gap histograms match in shape (1-tile steps, diagonals 13..35%). Our lives are shorter than the recording's (19.7 against 62.9 steps per life) because the test spends less time there.
- `varp_transmit_audit.py` (4486 + 4487): no `ics_` or kitten row is a var we do not send.
- Field-by-field section diff: table below. R rows are the recorder's kit (staff, autocast, food, prayer, level-ups) or paths the test does not walk (wall crusher, pitfall, chest, door refusal).
- Per-tick facing, ceremony cast: all 5 cast npcs match in order (High Priest `42,45`, `44,45`, `41,43`; Amascut, the priest, the possessed priest). Only the ceremony was diffed this way.
- Cutscene frames against `TORIRSSERVER_TRACE_OUT=1`, recorded/ours: `CAM_MOVETO` 1/1, `CAM_LOOKAT` 1/1, `CAM_RESET` 1/1, `CAM_UNLOCK` 1/1, `CAM_SHAKE` 5/5, hide ops 2/2, minimap toggle 2/2. `IF_OPENSUB` 38/33, `IF_CLOSESUB` 12/13, `IF_SETTEXT` 52/42 and `RUNCLIENTSCRIPT` 59/39 still differ on count. Trace level 1 has no bodies, so those four are counts only.
- Camera, mechanical: pit camera fixed (the first aim is always x 29, 4 of 4 recorded jumps, plus the north one).

## Closed in this pass

- South-eastern gate: two `ics_little_priestdoorman` spawned at 3321,2761 and 3322,2761 (`configs/icthlarin.spawn`; recorded adds in 4486 and 4487). Their post-quest menu is the wiki's. No recording plays it.
- Tarik: played branch (`ntk_mummy_spoken` = 2 set in `ntk_join`), artefact and sceptre use-on.
- Wingstone seed pod. Eldritch ashes (`toa_akkha_ashes`, the wiki's pages; the chain before it is not built).
- `Transcript:Cat`: Talk-to (`icthlarin_cat.rs2`), with catspeak and without, cat ears, Stroke and Shoo with catspeak, fish on an adult, Barrows rats, "Miaooow!" on a grown cat's drop (4487 [4010]).
- Capt' Arnav's chest (`macro_combilock_pirate/`, new; `::arnav`): the pages, 3 words, wheels, fail pages, reward table, overheads; varp 9516 added to `pack/varp.alloc`.
- First ceremony's end: built as recorded. With the cat in the pack the scene stops 7 ticks into "You! Priest..." with no click (blank box and fade [4003], out to the door [4005], fade in [4008]). Reopening the door at stage 21 replays the scene. The cause (cat in the pack) is inferred from one pair of passes.
- Pit fall's `ics_pit_secret_door` loc anim [1003]. Blank chatbox on the 12 memory fades, closed on the 7 recorded ends.
- Mummies: `huntmode=aggressive`, `huntrange 3`. Of 18 unprovoked faces, 15 came from 0..3 tiles; 3 came from 4..6 on the first tick the npc was seen. Mummy swings now appear on the tick log (13 `npc_anim`).
- Possessed Priest: casts every 4 ticks (72 of 72 gaps), one of four: wind 17, water 21, fire 22, Zap 13 (no projectile, no splat). Strike projectile 51->90, 60 of 60, all at 1 tile, so no distance fit is possible. Landing synth at 90, spellfail 17 of 60, no impact spotanim, `human_caststrike_walkmerge`, `human_spear_block`, death synth.
- Hazards pooled: `ics_scarab_swarm` also appears in index 2999 and 3, at other coordinates; `ics_little_pit_to` only in 4487. 4487 is the only source, so its counts stand.
- Generic rows: 72 of 91 distinct ops have a binding. Of the 19 flagged, these were real defects and are fixed: `banker1_east` Bank (4481 [80]), Empty on `bucket_water` (4486 [793]), the temple altar `contact_high_priest_temple_altar` (4487 [2226]), Read on the book of embalming (4486 [632], interface 680; spread 0 verbatim, later spreads wiki text wrapped by word count, UNVERIFIED). `contact_spicemarket` matches the recorded "Nothing interesting happens." The rest are the engine's generic wear, attack, eat and bury paths or a multiloc rung that is bound.
- Journal: `search interface 119/782 varbit 418` finds 4486, 4487, 1139, 1694. 1139 and 1694 never open this quest's journal (0 titles), and the wiki has no `/Journal` page.
- `CUTSCENES.tsv` row: `ported` = yes.
- Kitten note: `Transcript:Kitten` was fetched but not ported.

## Open (why no available source settles it)

1. Priest max hits: 43 of 43 splats are prayed zeros. There is no unprayed sample in any session, so the wiki's 1/2/6 stay UNVERIFIED.
2. Journal stages 1, 2, 4..7, 8..14, 17..22 and the completed journal: no recording opens them.
3. The no-cat scene end: one occurrence. The frame is recorded; the cause is inferred.
4. Arnav's wheels: only COINS 0, BOWL 1 and BAR 2 are seen, in 3 solved chests. Other cells are not recorded, and the fourth word is unknown.
5. Gate priest menu, Tarik's items and played branch, ashes, Cat talk: wiki only, moods unverified.
6. Book of embalming spreads past the first: wrap by word count, no recording turns the page.
7. Wingstone's clue and restaurant lines: the trail table has no Wingstone step, and the gnome restaurant has no delivery jobs. Porting them means building those systems.
8. Cat lines needing systems we lack: rat milestones (no counter), scratching post, dog growl, menagerie arena. `Transcript:Cat`'s Historical section (47 lines) is no longer in the game.
9. Interface-packet counts in cutscene frames (`IF_OPENSUB` 38/33 and so on) are diffed by count only. Rerun at `TORIRSSERVER_TRACE_OUT=2` to diff bodies.
10. Facing was diffed per tick only for the ceremony cast. The other scenes were compared on `fieldiff.py` kinds.
11. Generic rows diffed for a binding only, not packet by packet: carpets (route not walked here), ladders (`maplink.dbrow` rows exist, landings not run), jewellery landings, the bank UI, shops.


## NPC info: recorded and ours per block kind

Counts are npc rows only. Recorded is 4486 + 4487 together, ours is one scripted run (`icth24`), so equal counts are not expected. `growncat` (the follower) is left out.

| block kind | recorded (4486+4487, npc rows) | ours (icth24, one run) | never sent by ours (types) |
|---|---|---|---|
| facing | 405 | 83 | ics_little_het, ics_little_locust, ics_little_mummy_2, ics_little_mummy_4, ics_little_plague_frog, ics_little_worker1, ics_little_worker2 |
| say | 3 | 4 | ics_little_cat, ics_little_het |
| sequence | 215 | 45 | ics_little_croc, ics_little_het, ics_little_locust, ics_little_mummy_1, ics_little_mummy_2, ics_little_mummy_4, ics_little_plague_frog |
| spotanim | 158 | 17 | ics_little_het, ics_little_mummy_4, ics_little_plague_frog |
| enabled_ops | 11 | 8 | ics_little_het, ics_little_mummy_4, ics_little_redheadlady_ceremony |
| headbar | 57 | 14 | ics_little_het, ics_little_mummy_4, ics_little_plague_frog |
| hit | 57 | 0 |  |
| level_change | 8 | 6 | ics_little_redheadlady_ceremony |
| transformation | 8 | 9 |  |

Facing per type, recorded/ours: contact_jex 2/2, ics_little_carpenter 6/2, ics_little_croc 9/2, ics_little_devourer_ceremony 2/2, ics_little_embalmer 8/4, ics_little_het 10/0, ics_little_hipriest_ceremony 15/10, ics_little_hipriest_town 12/6, ics_little_ic 2/5, ics_little_linen1 9/2, ics_little_linen2 4/2, ics_little_locust 3/0, ics_little_multi_wanderer 4/1, ics_little_mummy_1 38/1, ics_little_mummy_2 21/0, ics_little_mummy_3 27/2, ics_little_mummy_4 29/0, ics_little_mummy_5 40/4, ics_little_plague_frog 24/0, ics_little_possessedpriest 3/3, ics_little_priest 21/2, ics_little_priest_ceremony 11/11, ics_little_redheadlady_ceremony 4/4, ics_little_spectre 24/1, ics_little_sphinx 11/8, ics_little_worker1 3/0, ics_little_worker2 2/0, ics_scarab_swarm 57/6, macro_combilock_pirate 2/1, ntk_tarik 2/2

Written in script in this pass: the town High Priest's turn back to angle 0 the tick after his reset, Klenter's face at the wake tile, a per-tick player face for chasing mummies and the scarab swarm, the swarm's owner and timer, `npc_setops` on the Devourer's change.

Not written on recorded ticks, with the reason:
- `facing` on talks (`[facing] player` at the op, `reset` at the end): this comes from the engine's dialogue mode, not from script. Per-talk counts differ only where the test talks fewer times (carpenter 6/2, embalmer 8/4, the priests 21/2).
- `ics_little_spectre` 23 of 24 rows are `type=reset` on `[add]`, sent as he comes into view. No script sends them and ours does not.
- Types with 0 ours: the test never reaches them. Het, the jar rolls one Apparition per run; workers and `ics_little_cat`, not talked to; `plague_frog`, `locust` and `croc`, the recorder's own attacks; mummies 2 and 4, the test stands near 3 and 5 only.
- `level_change` on the redhead's change: ours does not send it, reason not found.
- Swarm per-tick facing: ours 2 of 57, one short swarm life on this run. Not shown equal.


## Third pass (rsprox-content step 12)

Test: 235 rows, 0 fail (`icth30`), private packs. Per-block counts (npc rows; recorded is 4486 + 4487, ours one run; `growncat` left out):

| block kind | recorded | ours before (icth24) | ours after | never sent after (types) |
|---|---|---|---|---|
| facing | 405 | 83 | 121 | ics_little_locust, ics_little_plague_frog |
| say | 3 | 4 | 6 |  |
| sequence | 215 | 45 | 74 | ics_little_croc, ics_little_locust, ics_little_plague_frog |
| spotanim | 158 | 17 | 18 | ics_little_mummy_4, ics_little_plague_frog |
| enabled_ops | 11 | 8 | 9 | ics_little_mummy_4, ics_little_redheadlady_ceremony |
| headbar | 57 | 14 | 26 | ics_little_plague_frog |
| hit | 57 | 0 | 26 | ics_little_plague_frog |
| level_change | 8 | 6 | 6 | ics_little_redheadlady_ceremony |
| transformation | 8 | 9 | 9 |  |

- `hit 57/0` was the tool: `npc_block_inventory.py` skipped our `hit_npc` rows; it now counts them.
- Redhead `level_change` and `enabled_ops`: ours sends them on the change, logged under the new type (`ics_little_devourer_ceremony`), the recording prints the old type.
- Frog, locust, croc: the recorder attacked them (`opnpc2`, 4486 [480] x4 and neighbours); not quest content.
- Swarm per-tick facing: the engine drops an unchanged target; the script now resets and re-faces every tick.
- Dialogue faces (`dlg.py`): engine face at op+1, reset at last click +3 (carpenter, embalmer, Raetul, Sphinx, 3 of 3 where the test stays put); 1 where the test teleports away right after.
- Cutscene bodies (`bodies.py`, `TORIRSSERVER_TRACE_OUT=2`): ceremony 144 matched, 11 recording-only, 6 ours-only (combat tab text, xp drops, combat-level value: the recorder's kit). Fixed: the fade frame (hp hide, 2249), the flashback tint, blank box and "0" beats, side-tab frames.
- Wanderer stance: `t.npc.pose` answers "unsupported" on the scriptrun driver.
- Klenter: 22 adds carry `type=reset`, as do the Feud npcs in other sessions; our add re-sends only a latched entity. Not built (engine change).
- Carpets: same last tile; recorded per-tick hops, ours every 3 ticks; the route table was left as it is.
