# Holy Grail — RSProx index 4435

Recorded 2026-10-30, rev 241.1, world 369, one session end to end (3,495 ticks), by the
same recorder as Merlin's Crystal (index 4433) and Thrantax (4434), at combat level 39.
The session covers:

- the start [58..96] and every knight [101..224];
- Merlin [355..417] and Galahad at stage 3 [643..791];
- a farming run [814..1232];
- Entrana: the monks, Fritz, the High Priest and the crone [1285..1601];
- the napkin [1730..1755] and Draynor's whistles [1835..1884];
- the corrupted realm: the Titan [2086..2636], the fisherman, the bell, the castle and the Fisher King [2641..2884];
- the feather and Percival [2956..3170];
- the restored realm, King Percival and the Grail [3267..3427];
- the hand-in [3470..3475].

The journal was opened at every stage from 2 to 9 (16 times).

Before this port the quest was a LostCity port (2004):

- forced `|` breaks in most pages and several wrong moods; "RuneScape world";
- a start menu instead of the confirm;
- no High Priest menu;
- whistles that teleported with an invented mesbox, from a radius-6 circle that is not the tower;
- the bell landing on plane 1 beside the Fisher King;
- the sack spawning Percival on the player's tile with a game message;
- the completion queued, with an invented "Access to the Fisher Realm" scroll row;
- no stairs in either castle going anywhere but one plane on the player's own tile; the corrupted castle's Walk-up stairs had no binding;
- a silent Titan with a 2004 drop table;
- a 2004 journal.

**Ours:**

- `quests/quest_grail/`:
  - `quest_grail.rs2`: the doors, whistles, bell, feather, Grail and completion;
  - `grail_realm_npcs.rs2`, `fisher_king.rs2`, `sir_percival.rs2`, `grail_crone.rs2`, `black_knight_titan.rs2`;
  - `grail_journal.rs2`;
  - configs `quest_grail.constant`, `quest_grail.npc`, `quest_grail.spawn`, `black_knight_titan.npc`.
- `areas/area_camelot/scripts/king_arthur.rs2` and `sir_*.rs2` (the Grail arms only; Merlin's Crystal owns the rest), and `merlin.rs2`.
- `areas/area_seers/scripts/brother_galahad.rs2`.
- `areas/entrana/scripts/{high_priest_of_entrana,entrana_monk,entrana_glassblower}.rs2`.
- `areas/port_sarim/scripts/monk_of_entrana.rs2`.
- `ladders_stairs/configs/maplink_landings.{dbrow,loc}`.
- `skill_prayer/scripts/altar.rs2`.
- `sailing/{scripts/port_masters.rs2,configs/port_masters.spawn}`.

**Tests** (scriptrun on private packs, `src/build_hg_opt/torirsserver`):

- `test/quests/grail.lua`: 186 of 194 rows pass. Eight fail, none of them Grail content:
  - seven `*.camelotTeleport.cast` / `faladorTeleport.cast` / `lumbridgeTeleport.cast` rows. The driver's cast check settles before the spell's teleport lands, and each following `.landed` row passes. This is shared driver behaviour (Open).
  - `quest.journal`, which no scriptrun can pass (no quest list).
- `test/quests/grail_titan.lua`: 12 of 12 (the Black Knight Titan, below).
- `test/quests/_journals.lua`: `journal.holygrail.{2,3,4,8,8.feather,9,9.grail}`, the rows of [125], [420], [1588], [2888], [2989], [3146], [3447]. These need the live client. Headless, all seven states were opened through `::grailjournal` on a `TORIRSSERVER_TRACE_OUT=2` run, and every row's byte length matches the recorded row, in order (7/7).

## 1. Every action in the recording

| tick | action | verdict |
|---|---|---|
| 2, 12.., 43, 47, 62, 128.. | welcome screen, quest list, overview, world map, journal close | engine/UI. The overview's start text **added** (§7) |
| 57.., 3470 | King Arthur x5 | **this content** (§2, §8) |
| 101..224, 2929..2946, 3451.. | the eight knights | **this content**. Every Grail page matched except Tristram's mood (chathap1) and Lancelot's "Frankly, friend," and his angry reply: **fixed** |
| 118.., 167, 329.. | Camelot doors, stairs, courtyard gate | generic. Audited by the Merlin's Crystal port (4433, `merlins_crystal.md`) |
| 228.., 324.., 1028, 1186, 1597, 1989, 2900.. | Falador, Camelot, Varrock, Lumbridge, Ardougne tablets | generic: audited 2026-10-29 (`teleport_tablets.lua`) |
| 293, 299 | goblin Talk-to "I kill you human!" | generic, page matched |
| 304, 309, 3060 | goblin_outpost_poordoor | generic door, bound; the test passes it |
| 312, 317, 3065.., 3149, 3153 | percy_sacks Prod/Open | **this content** (§5) |
| 344.., 605, 1127, 1641, 2198.., 3262 | stamina, prayer restore, antipoison | generic consumption, bound |
| 355.. | merlinworkshop x4 | **this content**: **fixed** to a walk-through (§3) |
| 366.. | Merlin | **this content**: pages matched; one forced break **removed** |
| 460..530, 814..916, 1917..1981 | Seers' oak, kr_ladder, banks, bank tabs, withdraw/deposit, Castle Wars chest | generic, bound; not diffed |
| 537, 808, 1167, 1902 | ring of dueling: wear, worn Castle Wars | generic: audited 2026-10-29 (`jewellery_teleports.lua`) |
| 616..638, 1715, 1726 | Galahad's house: double door, bookcase x5, chest, poordoor, ladder up and down | generic, bound; the test passes the door. The ladder climbs on the player's tile (2612,3476), our default |
| 643.., 1730 | Brother Galahad x6 | **this content** (§4) |
| 880..1225 | tree patches (rake, compost, plant), gardeners' Pay x8, skill guide | generic farming. Patches are bound by rung (`farming_tree.rs2`). **The gardeners' Pay (op3) has no binding for any gardener** (Heskel's "I'll want 200 Coins to chop down your tree." and the protection payment): Open |
| 993 | membergatel | generic, bound |
| 1108 | spirit tree (Varrock → Gnome Stronghold) | generic, bound; its objbox page matched |
| 1232, 1763 | glory Rub → Draynor Village | generic: audited 2026-10-29 |
| 1285, 1318 | shipmonk to Entrana, gangplank | generic: "All is satisfactory. You may board the boat now." was missing (LostCity's, noted "deferred"), and an unrecorded board line was sent: **fixed**. The 14-tick hold at 3064,3208,1 [1299] and the deck 2834,3331,1 [1313] matched |
| 1330 | Entrana church door (poshdoor) | generic door, bound |
| 1348.. | Entrana monk x3 | generic: "out of the way" (the menu says "out the way") **fixed**; the heal's seq (`human_castentangle`) and `heal` area sound **fixed**, Entrana only |
| 1364 | devious_altar (Entrana church) | generic: **no binding; added** (`~pray_at_altar`: the recorded spam line, `human_pray`, synth 2674) |
| 1382 | Fritz the Glassblower | generic: **no binding; ported** (`entrana_glassblower.rs2`), the bucket on the recorded click. His later visits are Open |
| 1485.., 1500.. | High Priest, crone | **this content** (§4) |
| 1774, 1847, 1868 | napkin examine, drop, take | generic: drop and take bound (private drop, despawn 300) |
| 1818..1897 | Draynor Manor doors and stairs | generic (Ernest/Haunted ports); the test passes each with the recorded landings |
| 1835.. | whistledoor x8 | **this content**: **fixed** (§3) |
| 1851.., 2722, 2775, 3415 | Take whistles, bell, Grail | engine pickup (`human_pickuptable_walkmerge`, `pick2`) matched. Grail: §6 |
| 1874.., 2086, 3267, 3427 | magic whistle Blow | **this content**: **fixed** (§3) |
| 2014, 2024, 3198, 3204 | Captain Barnaby to Brimhaven, gangplank | generic, bound; not diffed |
| 2097, 2207, 2477, 3163 | wield rune scimitar, glory, Excalibur | the recorder's kit |
| 2100.., 2154.. | Black Knight Titan | **this content**: one boss agent, below |
| 2103, 2378 | prayers | the recorder's |
| 2556..2565 | Take marrentill seed and bones, bury | generic; the seed is the Titan's drop (fixed, below) |
| 2567.., 3275.. | unhappy/happy peasants | **this content**: pages matched, forced breaks removed |
| 2597 | potato (realm) Pick | generic, bound |
| 2641 | fisherman | **this content** (§6) |
| 2719.., 3348 | Grail Maiden | **this content**: page matched |
| 2728, 3011 | Ring the bell | **this content**: **fixed** (§6) |
| 2763..2825, 3363..3426 | castle stairs, ladders, spookystairs | **this content**: **fixed** (§6) |
| 2833.. | Fisher King | **this content**: pages matched, forced breaks removed |
| 2978.. | feather Blow x12 | **this content** (§5) |
| 3101.. | Sir Percival | **this content** (§5) |
| 3155 | attack goblin | the recorder's |
| 3195 | port_task_master_ardougne | generic: neither spawned nor bound in our tree. **Placed** at the recorded tile and bound like the other two port masters. The recorded click was overtaken two ticks later (no answer recorded) |
| 3339 | restored castle door | generic door, bound; the test passes it |
| 3377 | King Percival | **this content**: pages matched |
| 3480, 3495 | quest scroll close, logout | engine |

## 1b. Every teleport

| tick | cause | landing | verdict |
|---|---|---|---|
| 171, 349 | kr_cam_woodenstairs | 2751,3513,1 | Merlin's Crystal port |
| 233.., 328.., 448, 866, 1032, 1190, 1601, 1993, 2904.., 3179, 3437 | tablets | — | audited 2026-10-29 |
| 812, 1170, 1905 | worn ring of dueling | Castle Wars | audited 2026-10-29 |
| 1115 | spirit tree | 2461,3444 | generic, not diffed |
| 1235, 1766 | glory | 3105,3251 | audited |
| 1299, 1313, 1321 | shipmonk, gangplank | 3064,3208,1 → 2834,3331,1 → 2834,3335,0 | matched (test) |
| 1719, 1727 | Galahad's ladder | same tile ±1 | default climb, matched |
| 1827..1897 | Draynor stairs | recorded tiles | matched (test) |
| 2020, 2026, 3201, 3207 | Barnaby, plank | 2775,3233,1 / 2772,3234,0 | generic, not diffed |
| 2086 | whistle under the tower, stage 4 | 2806,4715,0 | matched (`^grail_realm_entry_coord`) |
| 2733 | the bell | 2761,4692,0 (a 2-tile glide) | **fixed** (was 2763,4688,1) |
| 2767, 2790, 2809, 2822, 2825, 3363, 3367, 3374, 3390, 3408 | castle stairs | see §6 | **fixed** |
| 2772, 2786, 3413, 3426 | castle ladders | same tile ±1 | default climb, matched |
| 3267 | whistle under the tower, stage 9 | 2678,4715,0 | matched |
| 3427 | whistle in the realm | 2741,3235,0 | **fixed** (was 2740,3232) |

## 2. The start (4435 [58..96])

| R | ours | verdict |
|---|---|---|
| "Well, we recently found out that the Holy Grail has passed into Gielinor." | "...passed into RuneScape world." | fixed |
| combat warning mesbox at the quest row's 50 [86] (the recorder is 39) | none | added (`~player_combat_level < 50`) |
| `Start the Holy Grail quest?` Yes/No [89] | "I'd enjoy trying that." / "I may come back..." menu | fixed. No is the wiki's, moods unrecorded |
| on Yes: the player's "I'd enjoy trying that.", then stage 2 + "You've started a new quest: @q_mes@Holy Grail</col>" with Arthur's next page [92] | the stage before the page, no line | fixed |
| Arthur's two pages (neu3, neu2) | same | matched |

## 3. Doors and whistles

**Merlin's workshop and the whistle room are walk-throughs**, not swings. On the op tick (L+1, no
`p_arrivedelay`): an inviswall in the door's slot, its inactive leaf (`inactiveposhdoor` /
`draynorpanelleddoor_inactive`) on the far tile turned one step, `door_open` (62), and the player
stepped across. Both revert two ticks later. Workshop [359], [362], [365]; whistle room [1838],
[1842], [1849]. Ours was `~door_selfstage_open` (a 500-tick swing): **fixed** with
`~grail_walk_through`. Our tick log matches it packet for packet: 274/276 whistle room, 99/101 workshop.
Merlin is (re)added at 2767,3500 facing south with each step in [359], [365]: matched.

**Whistles:** every pass through the door, either way, with the napkin held and fewer than two
whistles held lays two private whistles on 3107,3359,2 with despawn 35 ([1838] going in, [1842]
coming out). None without the napkin [1849], or with two [1884] or four [1870] held. Ours were public,
for 250 ticks, and only when the door swung: **fixed** (`obj_add_private`, 35).

**Blowing** is a bare teleport on the click: no seq, no sound, no line. The places:

- Under the tower: [2086] stage 4 → 2806,4715; [3267] stage 9 → 2678,4715.
- In the realm: [3427] → 2741,3235.
- Anywhere else: the mesbox "The whistle makes no noise. It will not work in this location." ([1874], [1999], [2084]).

The tower is four pillars (loc 1773, m42_50) at 2740..2743 × 3234..3237. Both working blows (2742,3236; 2742,3235) are inside it, and the failed one (2741,3233) is one row south. The wiki's square (2740,3232 r4) contains 2741,3233 and is wrong.

Ours was a radius-6 circle round 2740,3232, with three invented mesboxes, a "gentle blow" line before stage 4, and an exit to 2740,3232: **fixed**.

## 4. Galahad, the High Priest and the crone

| R | ours | verdict |
|---|---|---|
| Galahad's greeting, one page | forced breaks | fixed |
| menu "Do you get lonely here on your own?" (the player then says "out here") | "out here" in the menu | fixed |
| "What are you talking about?" / "Leaving was tough..." / "I don't suppose I could borrow that?..." | forced breaks | fixed |
| tea into the pack on the click closing its box [653]; napkin likewise [1755], no tea after | same | matched |
| "Why did you leave?" at stage 3 ends after "Leaving was tough..." [707] | same | matched |
| High Priest: menu "Ask about the Holy Grail Quest." / "Make polite conversation." at stages 3 and 4 [1487], [1583] | no menu | added |
| polite: npc "Enjoy your stay here. May it be spiritually uplifting!" only [1490] | forced break | fixed |
| the crone added at 2851,3342 facing south with her first page [1500], wandering through the talk | same tile; wanders on the generated default | matched |
| crone pages; stage 4 with the first menu [1519]; the menu's "Ok, I will go searching.." | forced breaks | fixed |

The crone's lifetime is unrecorded: she is deleted from view when the player teleports at [1601].

## 5. The feather, the sacks and Percival

- **Feather:** 12 recorded readings [2978..3055], all game lines. Read from the tile the player stood on when the line went out ([2997] and [3017] answered a tick later), every one fits the LostCity rule with the sack's tile 2960,3507. The rule gives a straight point unless both offsets pass 5 (2954,3474 → northeast, dx 6; 2955,3482 → north, dx 5). 12/12. The near lines are unverified.
- **Prod:** a mesbox at stages 8 and 9 [3065], [3149]. Matched.
- **Open:**
  - at stage 9, "I have no reason to do that." [3153]: matched;
  - at stage 8, the mesbox "You hear muffled noises from the sack. You open the sack.", then on its click Percival added at 2961,3505 facing north, turned to the player, with his first page [3069..3071]. Ours was a game line, added on the player's tile: **fixed**. Facing north **fixed** (`facing=north`; the generated block said south).
- **Percival:** every page and mood matched; forced breaks removed. The whistle and stage 9 go out on the click closing "Ok, I will see you there then!" [3123]: matched. He never steps [3071..3170] (`defaultmode=none`; the generated block let him wander): **fixed**.
- **Arthur's feather:** an objectbox at zoom 400 with the feather in the pack on the same tick [2974]. Ours was a mesbox: **fixed**. Pages matched; forced breaks removed.

## 6. The realm

- **Fisherman** [2649..2677]:
  - "Not amazing..." is neutral (ours sad), and "...I didn't see any bells." is confused (ours quiz): **fixed**.
  - The bell is private: despawn 492 when the recorder's view reached it at [2684], so laid on the closing click [2677] with 500. Ours was public: **fixed**.
- **Ringing the bell** [2728..2736]:
  - `grail_bell` synth (3018) and the mesbox "Ting-a-ling-a-ling!";
  - on its click, the Grail Maiden's chatcon2 "Welcome to the Grail castle. You should come inside, it's cold out here.", with the player turned to 2762,4689 (no maiden stood near);
  - on that click, the mesbox "Somehow you are now inside the castle." and a 2-tile glide to 2761,4692,0.

  Rung in Falador [3011], the same box and then "Nothing happens." on its click. Ours had a mesbox with invented text, a maiden-within-4 test, and a landing on plane 1 beside the Fisher King: **fixed**. Where it works was recorded only at 2762,4694. "Within 10 of the bell's tile" is unverified.
- **The Grail:**
  - Before stage 9, the mesbox "You feel that the Grail shouldn't be moved..." [2776]. Matched.
  - At stage 9, Take puts it in the pack; the floor one stays; no seq and no sound [3417]. Matched.
  - The corrupted castle's Grail (2777,4684,2) and three whistles (2755,4728,0; 2761,4689,0; 2762,4677,1) were missing from the generated m43_73 spawns: **added** (`quest_grail.spawn`). The restored castle's four were present.
- **Stairs.** No maplink row reached the realm, so each staircase climbed one plane on the player's tile; the corrupted castle's Walk-up/Walk-down stairs had no binding at all. Jagex lands each on a fixed tile whichever side it is climbed from (2633,4680 from 2634,4682 [3363] and from 2633,4682 [3374]). **Fixed** with a ring of `maplink_landings.dbrow` rows per staircase, plus `climb_up`/`climb_down` on the spooky stairs (`maplink_landings.loc`):

| stairs | landing | source |
|---|---|---|
| spiralstairs 2761,4680,0 | 2762,4682,1 | [2807..2809] |
| spiralstairs 2776,4683,0 | 2777,4685,1 | [2763..2767] |
| spiralstairstop 2777,4684,1 | 2778,4684,0 | [2789..2790] |
| spiralstairstop 2762,4681,1 | 2763,4681,0 | unrecorded: the restored castle's, moved 128 east |
| spookystairs 2768,4682,1 | 2768,4685,2 | [2819..2822] (face L+1, move L+2) |
| spookystairstop 2768,4683,2 | 2768,4681,1 | [2825] (from standing, on C) |
| spiralstairs 2633,4680,0 | 2634,4682,1 | [3363], [3374] |
| spiralstairs 2648,4683,0 | 2649,4685,1 | [3406..3408] |
| spiralstairstop 2634,4681,1 | 2635,4681,0 | [3367], [3390] |
| spiralstairstop 2649,4684,1 | 2650,4684,0 | unrecorded: the corrupted castle's, moved 128 west |

- **Npc movement** (`rsprox_npcinfo`): maidens (36 lives, 158 steps), peasants, the fisherman, the Fisher King and Merlin wander, as their generated default. Galahad (`wanderrange=0`), King Percival and Sir Percival never step: King and Sir Percival **fixed** (`quest_grail.npc`). Block inventory: facing is the only kind on either side. The maidens', peasants' and King Percival's recorded faces are all the dialogue turn and its reset; matched on the npcs our test talks to.

## 7. The journal (4435 [125], [420], [1588], [2888], [2989], [3146], [3447])

Jagex's rows carry stray mid-row `<str>` tags ("...the Fisher King. <str>I") and a bare
`<col=000080>` before highlighted names. They go out row for row (`~quest_journal_rows`), not
through the split:

- stage 4's six rows are the same with and without the napkin, and in the realm;
- stage 8 adds two rows while the feather is held;
- stage 9's last row is "I should follow him to the Castle..." without the Grail and "Now I have the Grail with me..." with it.

Stages 5..7 (never written in 4435) show stage 4's rows, and the completed journal (never opened) the struck rows and `QUEST COMPLETE!`: both **unverified**. The not-started overview's start text ([36], [60]: "talking to King Arthur in Camelot Castle.") was **added** to `quest_overview_start.rs2`.

## 8. The hand-in (4435 [3470..3475])

Pages matched. On the click closing "Wow! Incredible!":

- the stage;
- the points;
- the line;
- disease −50/0;
- the Grail out of the pack;
- music 147 and jingle 153 (9,955 ms);
- the scroll with the Grail at 77,128 and the rows "2 Quest Points", "11,000 Prayer XP", "15,300 Defence XP";
- then the xp.

Ours queued the completion and showed "11000 Prayer XP|15300 Defence XP|Access to the Fisher Realm" at the default offset: **fixed** (`~grail_quest_complete`, `~quest_complete_rewards_at`). Our tick log has music 147 and jingle 153 9955 on the hand-in tick.

## 9. Vars

| varp | values | sent with | ours |
|---|---|---|---|
| grail (5) | 0→2 [92], 3 [381], 4 [1519], 8 [2878], 9 [3123], 10 [3475] | Arthur's page / Merlin's Galahad page / the crone's menu / the click after "I shall go and see..." / the click after "Ok, I will see you there then!" / the scroll | same values (`transmit=yes`), same pages; matched in the tick log |
| qp (101) | 96→98 | the scroll | `~quest_complete_rewards_at` |
| latest_quest_journal (3679) | 73 | the quest list | engine |

## Black Knight Titan (4435 [2086..2636])

One boss agent (Opus, max effort), `test/quests/grail_titan.lua` 12/12. The recorder stood on 2792,4722 with Protect from Melee from [2103]. They fought with a rune scimitar [2100..2476], then Excalibur from [2477], and killed at [2546]/[2547].

**Npc info and attacks:**

| | Recorded | Ours | Verdict |
|---|---|---|---|
| Spawn | 2791,4722, `spawnangle=east` [2086], [2553] | `facing=south` | **fixed** (`facing=east`) |
| Movement | idle, 0 steps in 3 lives | none | matched |
| Facing | `player=` on the first splat; `type=reset` on the death tick | same | matched |
| Defend seq | `giant_block` on each player swing (89) | same | matched |
| Attack | `giant_attack`, 64 swings 7 ticks apart, first at swing +4 | same | matched |
| Swing sound | `unarmed_punch` 2566, 64/64 | null (silent) | **fixed** |
| Defend sound | `blade5` 1972 | same | matched |
| Death | `giant_death` the tick after the killing splat; `human_death` 512; ops 0b00000 | — | **fixed** (`death_script`) |
| Del / respawn | death +2 / del +4 | — | **fixed** (`npc_setrespawn(4)`) |
| Kill | "Well done! You have defeated the Black Knight Titan!" + glide 2792→2790,4722 on the death tick; stage stays 4 | line on the del tick or later | **fixed** |
| Drop | bones + marrentill seed (private, 200) on the del | the 2004 table (no seeds) | **fixed** (the OSRS wiki table) |

**Damage:** 63 prayed splats, all 0, all `npc:black_knight_titan:giant_attack@0:adj` (§13b leftovers: none). Ours: 48, all 0. The levels are the cache's (91/91/100/142).

## Open

1. **The tree gardeners' Pay (op3)** has no binding for any gardener: Heskel's "I'll want 200 Coins to chop down your tree." + "Pay 200 Coins to have your tree chopped down?" [891..934], and the protection payment ("That'll do nicely, sir...", [944], [1068], [1158]). This is generic farming, outside the quest; not ported.
2. **Fritz's later visits** (buying molten glass) need a state no cache var names; every visit is the first one.
3. **The bell's area** is fitted to one tile (2762,4694). **The crone's lifetime** is unrecorded. **The two mirrored stair tops** (2762,4681,1 and 2649,4684,1) are unrecorded.
4. **The journal at stages 5..7 and 10** is unverified.
5. **The Titan:**
   - hidden healing faster than ours (about 9 hp over 447 ticks at 142 hp, no fixed period);
   - max hit and accuracy unmeasured (every recorded hit was prayed);
   - the no-Excalibur zero is unrecorded (the full heal, the line, the two pages, the stage-7 write: LostCity's, kept);
   - not checked: the player's `angle=512` at the kill, the respawn's `jump=true`/ops, clientscript 7192 on the drop.
6. **Shared, not Grail:**
   - `t.player.teleport_cast`'s cast row fails on every spell while its `.landed` row passes (the check settles before the spell's teleport lands);
   - `rsprox_hit_causes.py --ours` does not attribute our hit rows.
7. **`~quest_journal_rows`** is the Merlin's Crystal port's proc (uncommitted at the time of writing). This journal needs it.
