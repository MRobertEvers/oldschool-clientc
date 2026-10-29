# Alfred Grimhand's Barcrawl (miniquest) — RSProx index 4420

Recorded 2026-10-29, rev 241.1, world 369, one session end to end (1,372 ticks):
the start at the Barbarian Outpost, all ten bars, the card read ten times, the
journal opened at seven stages, the hand-in and the vial smashing. Index 4421 (the
same recorder the next day, Scorpion Catcher) supplies the outpost gate.

Before this port the miniquest did not exist in our tree: every bartender said
"Barcrawl arm deferred", `varp77_barcrawl` was `temp, transmit=no`, the guard had
no Talk-to and the outpost gate no binding.

Ours: `quests/quest_barcrawl/` (configs `barcrawl.constant`, `barcrawl.varp`;
scripts `barcrawl.rs2` = guard, gate, card, journal; `barcrawl_bars.rs2` = the ten
drinks). Each bar's own Talk-to script carries the menu option. Test:
`test/quests/barcrawl.lua` (88 rows, all PASS on a private pack, 2026-10-29).

## 1. Every action in the recording

| tick | action | verdict |
|---|---|---|
| 2 | welcome screen play | engine |
| 8, 338 | Varrock teleport tablet (Break) | generic: `teleport_tablet.rs2`, bound |
| 50-130 | GE clerk, offers, bank booths, bank tabs, placeholders, deposit/withdraw | generic: `interface_grandexchange`, bank, bound; not diffed here |
| 156, 323, ... | quest list op 2 (journal) x16, close x13 | **this content** (§6) and the overview of other quests |
| 226-246 | bank search, withdraw games necklace, ring of dueling, glory | generic, bound |
| 256 | games necklace rub -> Barbarian Outpost | generic: `necklace_of_minigames`, bound |
| 274, 277 | oploc1 barbariangater / gatel | **this content** (§5): neither op landed before the guard click |
| 278..304, 1348..1369 | opnpc1 barbguard1 x5 | **this content** (§2) |
| 306 ... 1309 | opheld1 barcrawl_card x10 | **this content** (§4) |
| 363, 503, 550, 628, 711, 831, 926, 1015, 1173, 1284 | the ten bartenders | **this content** (§3) |
| 413-472 | Varrock bank booths, withdraw tablets, kitten, sardine, wool | generic, bound |
| 453 | drop kitten | the recorder's pet (`kitten_pet.rs2`) |
| 467, 671, 1011, 1215 | stamina potion sips, vial drop | generic consumption, bound |
| 472 | lumberyard teleport scroll | generic: `teleport_scroll.rs2`, bound |
| 530 | Falador tablet | generic, bound |
| 584 | glory (worn op 4) -> Draynor | generic, bound |
| 686, 689 | Seaman Lorris Talk-to, Pay-fare -> Karamja | generic: `sailors.rs2`, bound |
| 808 | membergater (Karamja/Brimhaven gate) | generic: `gates.rs2`, bound |
| 870, 969 | Ardougne, Camelot tablets | generic, bound |
| 923 | poshdoor (Flying Horse) x3 | generic door, bound |
| 989, 997 | Kourend bank, withdraw necklace of passage | generic, bound |
| 1062 | necklace of passage rub -> The Outpost | generic: `necklace_of_passage.rs2`, bound |
| 1071, 1078 | world map open/close | engine |
| 1092 | gnome_areagate | generic: `gnome_gate.rs2`, bound |
| 1134 | stronghold_ent op2 "The tree doesn't feel like talking." | generic: `spirit_tree.rs2`, bound |
| 1157, 1162 | treedoorr, grandtree_ladderbottom | generic, bound |
| 1209 | ring of dueling rub -> Castle Wars | generic, bound |
| 1263, 1271 | xbowsdoubledoor (Yanille) | generic double door, bound |
| 1328 | games necklace rub -> Barbarian Outpost | generic, bound |
| 1372 | logout | engine |

Generic interactions were checked for a binding only; none was diffed packet by
packet in this port (Open 6).

Ambience: the clock-tick synth 3120 (x3) and 3424 are the recorder's kitten
(hunger/attention), not barcrawl.

## 2. Stages and vars

| var | value | when (4420) | ours |
|---|---|---|---|
| varp 77 `barcrawl` | 1 (bit 0) | the click on "I have the perfect challenge" page [302] | same, sent (`transmit=yes`, was `temp/no`) |
| varbit 13714 `barcrawl_progress` (varp 3403) | 1 | [302] | same, sent |
| varp 77 | +bit 3 Blue Moon 9 [381], +9 Jolly 521 [521], +11 Rising Sun 2569 [566], +12 Rusty 6665 [641], +10 Karamja 7689 [730], +5 Dead Man 7721 [847], +7 Flying Horse 7849 [944], +8 Forester's 8105 [1039], +4 Blurberry 8121 [1187], +6 Dragon 8185 [1304] | per drink | every value matched in order (trace run) |
| varp 77 | +bit 2 "drunk card" 8189 | the card read with all signed [1309] | same |
| varp 77 | 2 | hand-in [1350] | same |
| varbit 13714 | 2 | [1350] | same |
| varbit 14162 `miniquests_completed_count` | +1 | [1350] | same |
| varbit 5668 `auto_smash_vials` (varp 3190) | 1 | "Yes please, I want to smash my vials." [1360] | same, sent (varp 3190 was `temp/no`) |

Index 4421 [2145]: Scorpion Catcher's completion writes varp 77 to **0**. So
completion is read from `barcrawl_progress`, never varp 77 (`barcrawl.constant`).

Vial smashing used a server-only `varp8023_barbarian_vial_smash`; it is now the
cache's `auto_smash_vials` everywhere (`consume_shared.rs2`, CoX trainer, the
gear-up cheat), and varp 8023 is retired in `pack/varp.alloc`.

## 3. The ten drinks

B = the click on the bartender's price page: the coins go, the page closes and
the first line goes out on B. Every later line is 3 or 4 ticks on. `g` = game
message, `s` = spam. "Ours" is the TRACE_OUT run's tick relative to its own B.

| bar | lines (R ticks from B) | stats (R) | hit | ours |
|---|---|---|---|---|
| Blue Moon (Gutrot, 50) | g buy B, g drink +3, g insides +6, g sign +9, say "Blearrgh!" + hit +12 | atk/def/str/smith | 0 at 26 hp (block, synth 511) | identical |
| Jolly Boar (Olde Suspiciouse, 10) | s buy, s gulp +3, s spinning +6, g sign +9, player "Thanksh very mush..." +12 | atk/def/mag/craft/mine | — | identical |
| Rising Sun (Hand of Death, 70) | s buy, s drink +3 (+drain), s stumble +6 + cam_shake(0,0,15,2), g "Kaylee giggles and signs your card." +10 + cam_reset + hit | atk/def/rng/fish | 0 at 26 hp | identical |
| Rusty Anchor (Black Skull, 8) | **sign on B**, s buy, s drink +3, s blurs +6, g sign +9, say "Hiccup!" | none | — | identical |
| Karamja (Ape Bite, 7) | s buy, s swirl +3, g "Zembo signs your card." +6, player "Mmmmm, dat was luverly..." +9 | none ([725]'s +1s are the natural restore) | — | identical |
| Dead Man's Chest (Supergrog, 15) | g serve, s wince +3, s stagger +7 (+drain), g "2 bartenders" +10 | atk/def/pray/cook/herb | — | identical |
| Flying Horse (Heart Stopper, 8) | g hands, s grimace +3, s clutch +7, g tears + g signing +10 + hit | none | 6 of 26 = 25% | identical (hit sound 521 vs R 519: the engine's random human_hit) |
| Forester's (Liverbane, 18) | g gives, s gulp +3, g swaying +7 (+drain), g scrawls +10 | atk/def/wc/fletch/fm | — | identical |
| Blurberry (Fire Toad, 10) | s buy, s burn +3, g sign +6, player "Danks bery mush..." opened and closed on +9 with the hit | none | 1 of 22 = 5% | identical |
| Dragon Inn (Fire Brandy, 12) | g hands, s blow +3, s blurs +7 (+drain), g sign +10 | atk/def | — | identical |

**The drain is 5 + 5% of the CURRENT level**, floored, never below 0. 24 of the 25 recorded
drains fit it (Jolly Boar attack 35 -> 29 with base 41 rules out the base level;
magic 19 -> 14 rules out rounding). The one exception is Fire Brandy's attack, 19
-> 13 [1301]: 6 + 5% (one sample). Our `stat_sub`/`stat_drain` both take the
percentage of the base level, so `~barcrawl_drain` computes the amount itself.

**Hits**: a percentage of current hitpoints: Gutrot 3% (0 at 26), Hand of Death
0 at 26 (written 3%; LostCity's 5% would have been 1), Heart Stopper 25%, Fire
Toad 5%. A 0 goes out as a block splat (12) with `human_block_1`, as recorded.

Menus and pages (all recorded, 4420): the option is offered while on the crawl,
the bar unsigned and the card carried. Jolly Boar's menu says "Grimhands" (no
apostrophe); Zembo's and Kaylee's say "barcrawl"; Blurberry skips greeting and
menu. Fixed on the way: Jolly Boar's greeting mood (quiz), Rusty Anchor's
"Have you heard any rumours here?", the Dragon Inn's "oh and some cheap beer."
and its fifth option.

## 4. The card

`messagescroll` (220): a blank row, `<col=ffffff>The Official Alfred Grimhand
Barcrawl!`, a spacer, the ten bars `<col=7f0000>Bar (Place)` or `<col=00ff00>Bar -
Completed!`, a spacer, a blank row; `toplevel_mainmodal_open(-1, -1)` and the
mount. With every bar signed it is not opened: bit 2 and "You are too drunk to be
able to read the barcrawl card." [1309]. Ours: identical rows ([307] and [1199]
asserted by the test).

## 5. The guard and the gate

Pages: every recorded page verbatim with its mood; the head is always
`barbguard1_postcrawl` (7724), whatever the multinpc shows. The guard is a multinpc
on varp 77 (precrawl / postcrawl) standing in the shell's `barrow_dharok_ready`:
the client read shows 7285 before and 7724 after, both in 2065.

The hand-in's menu came a tick after the page's click ([1355] -> [1356]); copied
with `p_delay(0)` (one sample).

Gate (4421 [6..11], [27..29]): the op at L+1 from the gate tile on either side;
both slots an inviswall, `inacmetalgateopenl` (1573) rot 1 at 2546,3570 and
`inacmetalgateopenr` (1574) rot 3 at 2546,3569 for 2 ticks, `iron_door_open`
(71), the player stepped across on their own row. Ours: identical (test crosses
both ways).

## 6. The journal

Recorded at [315] (stage 1), [574], [659], [743], [860], [958], [1050] and [1313]
(all signed + card read): ours matches [315] and [1313] row for row (test), and
the rows between are the same per-bar switch. The overview (not started) and the
COMPLETED journal are UNVERIFIED: no recording opens either.

## 7. Unverified (no recording)

Every branch below is the wiki transcript's text (moods LostCity's or a guess):
the full-pack refusal, lost card / card in the bank / not finished, the "stop
smashing" yes, the smashing-off prompt, every "not enough coins" line, the
Blurberry barman's pointer, the Dragon Inn's cheap beer, and the gate clicked
before completion (the nearest guard answers).

## Open

1. The completed journal and the not-started overview (no recording).
2. Fire Brandy's attack constant 6 and Hand of Death's 3% are fitted to one
   sample each.
3. The hand-in menu's one-tick delay is one sample.
4. Scorpion Catcher (Thormac) and Horror from the Deep still do not require the
   barcrawl (their own ports; 4421 is the Scorpion Catcher recording).
5. `stat_sub`/`stat_drain` both use the base level; RuneScript's `stat_drain` is
   the current level (LostCity's PlayerOps). 106 call sites rely on ours; not
   changed here.
6. The generic interactions in §1 are bound but not diffed against 4420.
