# Fairytale II - Cure a Queen, and the fairy ring network

Recordings (rev 242.1, 2026-11-22, the owner's): index 4606 (Fairytale I's
end: Martin's stage 5 and the crops' timer), 4654 (the start to the star
flower, 3,363 ticks), 4653 (the Gorak plane to the quest's end, 451 ticks).
4651 is Recipe for Disaster's Evil Dave and 4652 a login with no clicks (both
labelled "Fairy Tale 2"): no Fairytale II content. The ring network also uses
every cached session with a fairy ring arrival (92 sessions, 227 arrivals,
`human_fairy_appear` on a teleport).

Content: `quests/quest_fairytale2/` (martin, zanaris, hideout, planes,
journal), `general/scripts/misc/fairy_rings.rs2` +
`fairy_rings_codes.rs2` (generated, `tools/gen_fairy_rings.py`),
`skill_construction/scripts/poh_fairy_ring.rs2` (the house ring, now on the
same procs). Tests: `test/magic/fairy_rings.lua` (the network, every code),
`test/quests/_fairytale2_recorded.lua` (the quest in the recorded order).

## The fairy ring network

### Destinations

The cache carries the whole table: dbtable `fairyring` (88), 64 rows, one per
code in code order (`multiloc_state` 0..63), with `dest_coord`, `code`,
`desc` (the travel log's text), `apparition_npc`, `show_apparition`,
`no_staff_return`, and an `id` that is exactly what Jagex writes to varp 817
`fairyring_destination1` (AIR 2, DLR 112, DJQ 133, AJS 31, DIR 102, CKP 220,
ALQ 13, BLP 310). Ours reads the table at run time (`db_listall` +
`db_findbyindex`); the hand-written switch is gone.

| check | result |
|---|---|
| recorded codes (34) landing on the table's tile | 34/34, every arrival of each (e.g. DJR 45/45, CKS 14/14, AIS 13/13, BKR 12/12) |
| the table's tiles vs the ring locs' map placements (56 placements) | every code with a destination sits on a `fairyring_minorhub` placement, except BLQ (Yu'biusk is built at run time) |
| our old switch vs the table | wrong for AIS (1430,3323; 13/13 recorded 1429,3324), AKP (3284,2705; 10/10 3284,2706), AKR (1827,3538; 2/2 1826,3540), DLP (2923,10454; table and placement 2926,10455), BLQ (3573,4371; table 3572,4372); DJQ, DIQ, CJQ unhandled |
| the dial's encoding | the wheel varbits hold the code letter index directly (wheels 1,0,2 dialled DIR at 4653 [41..47]; 1,3,3 DJQ at 4654 [1700..1706]). Ours swapped values 1 and 3 on every wheel, so every code with a D, B, L, J, S or Q went to the wrong ring. Fixed |

The house ring (index 1025 [5239..5244], 2863) departs with the same frame and
lands on the exact tile (13/13 house departures); ours scattered over radius 2.
Fixed: the house's leave is two hooks (`~poh_fairy_leave_begin/end`) round the
shared frame.

### The frame

| step | recorded | ours (test/magic/fairy_rings.lua tick log) |
|---|---|---|
| Configure, at L+1 (C when adjacent) | wheels back to A I P; every log row's `if_settext` (a found code's `desc`, else ""), in Jagex's order; the hideout row once found; the ten favourite rows; `toplevel_mainmodal_background(4212288, 50)`; `fairyrings` in mainmodal and `fairyrings_log` in sidemodal (4653 [38], [297]; 4654 [1620]) | same, row order generated from 4653 [38] |
| a wheel turn | the varbit on the click, no sound (4653 [41..46]) | same (ours played `lotg_fairyring_adjust` on every turn: removed) |
| a log row "Use code" | the wheels and synth `ft_fairyrings` (4653 [299], [309], [319], [330]) | same |
| Confirm C | dial and log closed, `meslayer_close(28)`, varp 817, `fairyring_lastloc`, `lastloc_opvis` (64 + code), `lastloc_set`, the code's log flag (and `gorack_plane` on DIR), the step onto the ring, `cam_reset`, `minimap_toggle 0` (4653 [48], 4654 [1631]) | same on C [134] |
| C+1 | `human_fairy_vanish` delay 30, spotanim `fairy_flower_ring`, synth `ft_fairy_teleport` | [135] same |
| C+4 | teleport onto the far ring, `human_fairy_appear` | [138] same |
| C+5 | seq -1, busy 0 | [139] same |
| Zanaris op (minorhub op1) | the frame, no state written (4653 [88..93], 4654 [1647..1651]) | same |
| Last-destination at the ring itself | the frame with no move and no line (4654 [1638..1643]) | same |
| a code with no ring (DJQ) | the frame, landing back on the tile the step left, then the player's page "Wow, fairy magic sure is useful, I hardly moved at all!" chatneu1 (4654 [1706..1712]) | same |
| no staff | "The fairy ring only works for those who wield fairy magic." on the op (4653 [82]); ours said "rings only work" | fixed |
| no permission | "You don't have permission to use that fairy ring." (4404) | same |

`busy` is the engine's. The confirm's HUD re-send (pvp icons, combat tab,
side tabs reopened) is Jagex's generic modal close, not the ring's: not built.

### Who may use which ring

The wiki (Fairy_ring, Combinations, each destination's page). The wiki has no
refusal line for any locked code (the only dialling line it gives is the "I
hardly moved" one), so a locked code answers like a code with no ring.

| codes | gate | verdict |
|---|---|---|
| all | members, `%varb2329` (the Godfather's "Yes, okay.", 4654 [1518] = 2), dramen or lunar staff wielded (`lunar_moonclan_liminal_staff`) unless the Lumbridge & Draynor elite diary | built; recorded for permission and staff |
| CKP, DIR | none: both recorded with `%varb2330` / `%varb2331` at 0 (4654 [2429], 4653 [47]); DIR's arrival WRITES 2331 | our old gates on both removed |
| AIS, AJP, CKQ | Children of the Sun and Varlamore visited (`%varb9650`) | wiki |
| AKP | the Necropolis entered (`%varb13841` >= 14) | wiki |
| AJQ | Death to the Dorgeshuun | wiki |
| DLP | Grimstone visited (`%varb19564`; nothing in our tree sets it) | wiki |
| DLS | In Search of the Myreque | wiki |
| DJR, CIS, CIR | Great Kourend visited (`%varb4897`, the Veos ferry) | wiki |
| CIP | The Fremennik Trials | wiki |
| CLR | Monkey Madness I | wiki |
| CJQ | Troubled Tortugans, the bandage given to Floopa (`%varb18321` >= 7) | wiki (new) |
| BLQ | Land of the Goblins far enough | wiki |
| BJS | Regicide | wiki; tested locked then open |
| BJR | Holy Grail far enough | wiki |
| DIQ | the player's house fairy ring | NOT BUILT: answers like a dead code |

### Fairy Tale II's hideout sequence

AIR, DLR, DJQ, then AJS lands on the hideout ring (2328,4426) when the three
destinations before it were AIR, DLR, DJQ in that order (varp 817..819) and
Nuff's certificate is held; it writes `%varb4026` (the log's hideout row).
Recorded twice in 4654 ([1631..1728], [2211..2272]) and in 4653
([301..337]). The log's hideout row puts the next code in white by varp 817
(4653 [297], [307], [317], [329]); its "Use code" turns the wheels to it.
Before the plaque is read the wiki's "Maybe you need to investigate further
first.." is given with the dead-code frame (shape UNVERIFIED).

## The quest

### Actions (rsprox_actions.py, 4654 and 4653), with verdicts

| action | verdict |
|---|---|
| Martin opnpc1, menu "Ask about the quest." | this content (ft2_martin) |
| zanarisdoor, bank booths, spiral stairs, glory / dueling / fangs equips and the glory Rub > Edgeville | generic, not this content |
| fairy_coordinator, fairy_godfather_multi, henchmen 1 and 2 | this content (FT1's triggers dispatch to FT2 once FT1 is done) |
| the six vial locs, the broken certificate | this content |
| certificate Read / Study / Destroy (op 2, 3, 7) | this content |
| fairy_sc_juttingwall, trees, fai_varrock_wheat, oaktree, metalgateclosedr | generic Zanaris, not this content |
| fairy_mindslayer_guard_multi talk | Fairytale I's (FT1 file) |
| fairy2_runetemple_sign | this content |
| fairy2_repair (Fairy Fixit) talk | this content |
| sheep / cows / chef shown the certificate; the Gate Keeper, bankers, shopkeepers, slayer master, imp and Aeryka given it | this content where the recording answers; the rest "Nothing interesting happens." (engine) or their own default lines |
| fairy_fat_cow (loc) use | generic ("The cow doesn't want that.") |
| ii_lost_impling, ii_fairy (Aeryka), handsand_blaec, slayer_master_4, macro_sandwich_lady | other content |
| fairy ring ops and the dial / log buttons | the network (above) |
| snakeboss telescope / spectator | other content |
| fairyresistance_multi1..5 talks, the blue crates | this content |
| fairy_nuff_multi2 talk and use (certificate, secateurs) | this content |
| fairyliquid_multi (Fairy Very Wise) talk and use | this content |
| fairy_queen_sleeping_multi2 use (certificate: "Nothing interesting happens."; magic essence) | this content |
| Godfather pickpocket (op3) | this content |
| fairy_godfatherarmy_multi2 (the orks) talk | this content |
| fairy2_starflower_fullgrown pick | this content |
| fairytale2_gorak attack, drops picked | this content (attack = the God Wars Gorak's) |
| star flower on vial, pestle on claws, claws on vial | this content (herblore tables) |
| fairy_queen_multi2 talk | this content |
| quest_rating, the lamp's Agility | generic |

### Teleports

| ticks | cause | verdict |
|---|---|---|
| 4654 [54] [163] [455] [2095] | spiral stairs, glory, the shed door | generic |
| 4654 [1635..1728], [2215..2272], [2750..2792]; 4653 [52..337] | fairy rings | the network, every landing tested |
| 4654 [2041] | the Godfather's henchman: knocked out to 3139,3228 | built (ft2_zanaris) |

### Stages (varbit 2326 on varp 810, sent, saved)

| value | when | ours |
|---|---|---|
| 5 | Martin's "Great! I'll see you soon then." click (4606 [114]), with `%varb2333` | same; softtimer 573 |
| 10 | 573 ticks later on its own (4606 [687]) | the softtimer, restarted at login (UNVERIFIED) |
| 20 | "Yes." (4654 [220]) with 1807 = 2, 2332 = 1, 2327 = 1 and the started line | same |
| 30 | the grotto's pan ends (4654 [578]) with 2339 | same |
| 40 | "Yes, okay." [1518] with 2328 = 1, 2329 = 2 | same |
| 45 | "So what should we do about it do you think?" click [1873] + "You see a fairy approaching ..." | same |
| 50 | Very Wise's "...back from the Godfather!" click [1885] with 1808 = 2 | same |
| 60 | the pickpocket [2149] | same |
| 65 | the secateurs box's click [2331] with 1808 = 1 | same |
| 70 | the lecture's last click [2350] | same |
| 72 / 73 | the two brews (4653 [281], [291]) | same (hook in `~attempt_brew_potion`) |
| 75 / 80 | the essence poured (4653 [394]) / four ticks on [398] | same |
| 90 | the lamp's box [433], with 2335 and the points | same; the scroll on its click |

The quest's own varps (810, 812, 816, 817, 1337, 1423, 3354, 4855, 4856) were
stated in `fairy_rings.varp` AND in the generated block; the generated copies
are deleted, so each is stated once (perm, sent).

### Scenes

| scene | recorded | ours |
|---|---|---|
| the trashed grotto | area trigger at 4654 [550] (the tile before the step, 2391,4466; not at 2391,4465 [549]); `cam_moveto` 2386,4471 h300 rate 4/10 at T+9, eight `cam_lookat`s every two ticks, then 2339, stage 30, `cam_reset` and the player's scared page at T+28 | a timer armed on entering Zanaris (FT1's mapzone label) watches the fitted box; `queue(9)` then the pan line for line. The recording's `busy` from T is not built (the player walks on in both) |
| the knockout | pickpocket fail: line + anim + synth on C, the Godfather's say on C+3, "You're beaten unconscious." + defend anim + cudgel + fade out on C+5, the move to 3139,3228 on C+11, the fade in + message box on C+12, the overlay closed on its click (4654 [2030..2045]) | same |
| the Godfather's pockets | only from behind him (the owner, 2026-11-22). He stands on 2446,4427 and every 20 ticks faces the next way, north, east, west, south (24 timed turns in 4654, the same cycle in 4605 and 4606); while someone talks to him he holds them until they step away. [2030] the recorder north of him, still held from the talk at [2019]: caught. [2149] the recorder east of him as he faced west: the secateurs | `[ai_timer,fairy_godfather_multi]` turns him on the clock's 20-tick beat; the op succeeds only when the player is strictly behind his way, and never while he holds a talker. The quest test is caught three times before his back turns |
| the pickpocket jingle | success: silence + `stealing_from_the_godfather` with the box on C ([2149]) | same |
| the Queen wakes | kneel-and-pour + bed `loc_anim fairy2_queenwake` + silence + jingle on the use, stage 80 four ticks on (4653 [394..398]) | same |

CUTSCENES.tsv row: `fairytale2_recorded`.

### Journal

Recorded 14 times (4654 [225], [646], [1186], [1435], [1572], [1927], [2128],
[2173], [2312], [2372], [2704], [2719]; 4653 [349], [365], [409]); the
overview at 10 ([29]). `ft2_journal.rs2` is those rows verbatim; the
unrecorded states are listed in its header.

### Test results (2026-11-22, scriptrun under tools/quest_gate/run.py)

- `test/magic/fairy_rings.lua`: 70/70. Every code dialled with the dial's
  buttons from Zanaris's ring: 53 landings on the table's tile, the 9 dead
  codes and DIQ back on the stepped-from tile with the line, BKS from its own
  ring with no move and no page, the permission and staff refusals, BJS
  locked then open after Regicide, the Zanaris op and Last-destination.
- `test/quests/_fairytale2_recorded.lua`: 186/186, the quest in the recorded
  order to stage 90 and the lamp (the gorak fight under `::god`: several hunt
  at once and drain stats, Herblore among them, as the recording's do).

### Wire checks done

The ring frame on our tick log against 4654 [1631..1636] (table above). The
quest test compares every recorded page of Martin (4606, 4654), the
Godfather, the Co-ordinator, Nuff and Fairy Very Wise, the secateurs and the
finale raw, `<br>`s included (a page naming the recorder, and the rest of
that speech, only by kind: the name moves the wraps).

## Open

- The departure apparition (`apparition_npc`, 1/6 per the wiki): appears at
  the ring the traveller has just left, so no recording of a traveller can
  show it (303 departures, none seen). A second player watching a ring would.
- DIQ (the house's ring): needs the house's garden ring found in the
  instance; answers like a dead code.
- Favourites: no recording sets or uses one; the rows' text is UNVERIFIED.
- `no_staff_return`: the table marks 14 rings, but DIR (marked) refused the
  Zanaris op without a staff (4653 [82]); its meaning is not settled.
- The thieving / farming refusal lines (wiki gates, our wording).
- Martin at stages 20..80 and after the quest; the Godfather at 60+; the
  Co-ordinator without the certificate; the chef, sheep and cows outside
  stage 30: no recording. Fairy Fixit's shop (post-quest) not built.
- The star flowers' spawn rate is one session's (31 in 256 ticks); the first
  seed came 46 ticks after arrival.
- The gorak's drop table beyond the big bones and the quest claws (the coins
  132 at 4653 [254]).
- The Cosmic Being (`fairytale2_cosmic_entity`) on CKP: not talked to in any
  recording; not spawned in our tree.
- Fairy Very Wise's spawn row (2353,4446, generated) is seven tiles from where
  every recording shows her (2353..2355,4453..4454).
