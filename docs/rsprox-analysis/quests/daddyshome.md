# Daddy's Home, from RSProx

Written 2026-10-20. Content: `OSRS-Content/osrs-content/server/scripts/quests/quest_daddyshome/`,
the sawmill's Buy-plank and Trade in `skill_construction/scripts/poh_sawmill.rs2`, and the
sawmill's stock in `shop/sawmill/configs/construction_supplies.inv`. Test: `test/quests/daddyshome.lua`.

## Sessions

| session | rev | world | what it holds |
|---|---|---|---|
| index 4291 | 241.1 | 478 (normal) | **The whole miniquest**, Marlo to the opened crate, ticks [269..763]. The recorder owns a house in Taverley and skips the lecture. |
| index 1686 (rsprox.net 1702) | 226.1 | 367 (normal) | The whole miniquest again [1143..1562]; Yarlo's "You still need to remove" list [1283..1284]; the house is in Rellekka. |
| index 1707 (rsprox.net 1723) | 227.1 | Leagues V | No house, so the lecture plays (a cutscene) [11536..11695]; the crate with no room [11752]; the sawmill's "more logs" [11970]. |
| index 211 (rsprox.net 73) | 227.1 | Leagues V | The end: bed, carpet, Yarlo's thanks, Marlo's reward [5142..5241]. A Mahogany Homes contractor: Marlo's second row is "I'd like a construction contract." |
| index 3201 (rsprox.net 3218) | 232.1 | 367 (normal) | Yarlo after the miniquest [628..634], and the lecture replayed from there [794..852]. |
| index 4350 | 241.1 | 337 (normal) | **The whole miniquest as a beginner** (no house, Construction 1), [173..784]: Marlo's other greeting, the lecture's cutscene with no question [280..450], the build row clicked without materials [517..519] [644..646], the no-house reward [769..777]. |

Found with `rsprox_index.py search varbit 10570`, `search npc 10425` and `search message_game "Old Man Yarlo"`.
Pages were listed with `tools/rsprox/rsprox_pages.py <transcript> [first] [last]`, which now reads the
index's named format (rev 238+) as well as the old bare ids; before, it printed nothing for 4291.

## State

`%varb10570_daddyshome_status` (bits 19..22 of varp 393). Values written, with the tick in 4291:

| value | tick | written with |
|---|---|---|
| 1 | [311] | Marlo's "Fabulous! Dad lives in the south-east of Varrock..." |
| 2 | [386] | Yarlo's "My boy Marlo sent you?", after the seven furniture bits go 0 -> 1 |
| 3 | [448] | the player's "I've finished removing all the old furniture." |
| 4 | [459] | "Now, I was just getting round to telling you about the furniture..." |
| 5 | [476] | the "take three logs from the crate to the sawmill" box |
| 6 | [631] | the sawmill's planks |
| 10 | [680] | Yarlo's "That's everything!" |
| 11 | [734] | Marlo's "That's wonderful news!" |
| 13 | [742] | the reward box (the dbrow's `endstate=13`) |
| 14 | [760] | the crate opened |

Each piece has its own varbit (10563 stool_1, 10564 stool_2, 10565 chair, 10566 table_1, 10567 table_2,
10568 bed, 10569 carpet): 0 untouched, 1 broken (Remove), 2 hotspot (Build), 3 built. They drive the
multilocs (`multiloc1..4` = `*_broken_0op`, `*_broken_1op`, `*_hotspot`, `*_fixed`).

Varp 393 was `scope=temp, transmit=no` in the generated layer. The recording transmits every bit and the
state has to persist, so `quest_daddyshome/configs/daddyshome.varp` states it `perm` and `transmit`. The
same varp also carries the Karamja dungeon, God Wars entrance and Kalphite rope bits.

## Timelines

**Remove** (4291 [390..438]; 1686 and 1707 the same). L+1: face (`sizex=2, sizez=2` for the bed); L+2:
`human_throw_away` delay 10, synth delay 20, `busy`=1; L+3: the varbit goes 1 -> 2. From a standing
start the throw comes on the op tick ([397], [403]). The bed and the stools sound `demolish_1` (2097); the
carpet, the tables and the chair sound `demolish_2` (2098). Ours: `p_arrivedelay; anim; sound_synth; p_delay(0); varbit`.

**Build** (4291 [544..593], [671..674]). The `poh_furniture_creation` panel opens on the op tick (L+1, no
arrive delay) with one row, clientscript 7988 `(1, icon obj, level 1, "name|materials", available)`.
On the answer tick R: `human_poh_build`, `poh_build_wood` (938), the panel closes. Then one nail per
tick (`mes_spam "You use a nail."`), the build anim on every nail except the last, and the sound on
every second tick. The planks go with the last nail; the cloth (carpet) or planks and cloth (bed) go on R.
On the next tick the varbit goes 2 -> 3 and the xp lands.

| piece | row text | icon (239 name) | nails | xp (wire) |
|---|---|---|---|---|
| carpet | `Carpet\|Bolt of cloth: 3` | 20603 placeholder_beta_elder_maul | 0 | 45 |
| stool | `Wooden stool\|Plank: 1<br>Nails: 2` | 20604 beta_elder_wand | 2 | 29 |
| table | `Wooden table\|Plank: 3<br>Nails: 4` | 20601 beta_elder_maul | 4 | 86 |
| chair | `Wooden chair\|Plank: 2<br>Nails: 2` | 20605 cert_beta_elder_wand | 2 | 57 |
| bed | `Waxwood bed\|Waxwood plank: 3<br>Bolt of cloth: 2` | 20602 cert_beta_elder_maul | 0 | 207 |

The wiki prints 87 for the table and 58 for the chair. Both tables in 4291 add 86 and the chair adds 57.

**Crate** (4291 [424..443], [604..607]). Before the bed talk: `"Old Man Yarlo may not appreciate you poking around in there."`.
After it: three logs and the objectbox; with no room, the objectbox_double "...so you'll need some more inventory space." (1707 [11752]).

**Sawmill** (4291 [623..631], 1686 [1436..1439], 1707 [11968..11974]). Trade opens "Construction supplies"
on the op tick: saw, cloth, bronze, iron and steel nails. Buy-plank opens skillmulti mode 0 with one cell per log
the mill takes, priced in the names (`Wood - 100gp` ... `Rosewood - 7,500gp` in 241), and `Waxwood - free` while
waxwood logs are held. The planks and status 6 arrive on the answer tick. Asking for more than are held
converts what there is and says "You'll need to bring me some more logs." (1707). There is no
dialogue about Yarlo's name on this path.

**Reward** (4291 [742..745]). In one tick: status 13, +1,000 coins, Marlo's crate, +400 Construction xp, and
objectbox_double (crate, coins_1000). Continuing the box moves `miniquests_completed_count` (+1). No quest
scroll and no jingle. The crate's Open [760]: status 14, +5 house tablets, 25 noted planks, 50 mithril nails,
5 noted steel bars, 10 noted oak planks, 8 noted cloth, 1 Falador tablet, `found_gem` synth, objectbox. Its
Destroy [751] uses confirmdestroy 814 with "Marlo, in Varrock, can replace it if you need another one."

## Dialogue

Every recorded page is in the script, verbatim, with the chathead's expression (`chathap4` -> `^chat_happy`,
the row count is added by `~chat_anim_rows`). The menus come back without the row just chosen
(4291 [290] -> [296] -> [301], [486] -> [507] -> [513]). Marlo's third row is "Tell me more about the job."
until it has been asked, then "Tell me where he lives, and I'll do the job.". The player speaks first when
talking to Yarlo during the build ([483]).

## Unverified (wiki transcript, or no source)

Each is marked `unverified` in the script:

- Marlo: "Maybe another time.", "I'd rather not." / "Okay, suit yourself.", the mid-quest "How are you getting on...",
  "Can I do some other kind of building work for you?", the paused-payment greeting, and the beginner's company
  pages ("What does this Mahogany Homes company do?" is recorded as a menu row only, 4350 [174]).
- Marlo after the miniquest, and the crate replacement.
- Yarlo: before Marlo (only his greeting), "What am I supposed to be doing again?", the paused lecture menu,
  "How do I build furniture?", "Where do I get planks, nails and cloth?", the saw-only offer, "No thanks.",
  "I'll get on with it.", and the "before claiming reward" pages.
- The crate with three already held, the crate opened with a full pack, and any tool given with a full pack.
- The sawmill's coin refusal, the Woodcutting Guild menu without waxwood, and the max/start quantity (4291
  and 1686: both equal to the logs held; 1707 opened at 10).
- The quest journal's text.
- Nails never bend: no recording shows a bent one.

## Who is asked before the lecture, and greeted as a builder

| session | house | Construction | Marlo's greeting | Yarlo before the lecture |
|---|---|---|---|---|
| 4291 | Taverley | 75 | "Would you like to work for us?" + "Tell me about working for Mahogany Homes." | "Is this necessary?" + Listen / Skip |
| 1686 | Rellekka | 99 | the same | the same |
| 1707 | Brimhaven, **bought at [10894]** | 1 | (a Mahogany Homes contractor: "Hello - it's lovely to see a contractor...") | the cutscene, no question [11536] |
| 4350 | none | 1 | "I wonder... could you maybe do me a little favour?" + "What does this Mahogany Homes company do?" | the cutscene, no question [280] |

The wiki's transcript puts the question behind "If the player already has a player-owned house"; 1707
had one and was not asked, so the house alone is not the check. Ours (`~dh_experienced`): a house AND
Construction >= `^dh_experienced_level` (2). The level it turns at is **unverified**: only 1 (twice, not
asked) and 75+ (twice, asked) are recorded. Marlo's greeting uses the same test; the contractor greeting
needs Mahogany Homes contracts, which this tree does not have.

## The lecture's cutscene (index 4350 [280..450])

`quest_daddyshome/scripts/daddyshome_lecture.rs2`, drafted with `tools/rsprox/rsprox_cutscene.py` from the
**`--raw`** transcript, one origin per copy (13008,10264 / 12816,10456 / 12432,10648), and cut into per-click
segments by `build/daddyshome_private/gen_lecture.py`.

**The copies are assembled, not copied.** Pairing every coord of the raw transcript with the translated one
(`build/daddyshome_private/zone_map.py`) gives each raw zone's template zone (all unturned): the player stands
in a filler zone of the template (zone 231,712; 231,716 in the third copy) and the house's zones, 224..226,
sit in the three zone columns just east of it. So the first builder is 15 tiles from the player and the
camera 6 from the house; read through the translated transcript the same camera is 48 tiles off, because
the filler zone's template coords stand for several raw zones. A first build from template coords put the
whole stage out of our client's 104-tile scene and out of npc view, and the frames showed void.

| copy | tick | player's zone | house zones (instance zone) |
|---|---|---|---|
| 1 | [282] | filler 231,712 at (1,1) | 225,712..713 at (3,1..2); 224/226 columns filler |
| 2 | [338] | filler 231,712 at (1,1) | 224..226,712..713 at (2..4,1..2) |
| 3 | [403] | filler 231,716 at (1,1) | 224,715..717 at (2,1..3); 225,715..717 at (3,1..3); 226,716..717 at (4,2..3); (4,1) filler |

224,717 and 226,716 are never named by a recorded coord; the template holds the same locs there as at
224,713 and 226,712, so they are copied. Ours: a 6x5-zone instance per copy, filler everywhere, all four
levels (`~dh_copy_build`).

**Client:** the scene's far shots need the draw distance Jagex's override gives. Our painter took its draw
distance from the boot feature only; it now reads device option 14, which the cache's own script 5193 sets
to 90 while `draw_distance_max_override` (18141) is 1 and back to the player's choice after
(`src/app/app_world_paint.c`). Roofs: `option_hide_rooftops_override` 2 at [296] and 0 at [449] are each
followed by clientscript 7917, which applies it as game option 1; ours sends both.

- **F** [280], the click on "Let me tell you...": song 147 with fade-out speed 50, the fade to black
  (50 cycles), an empty chatbox (no continue).
- **F+2** [282]: the lock (Misthalin Mystery's frame plus `option_acceptaid` 0), the player into the first
  copy (raw 13022,10272), three `village_wall_window` walls, the hud lock.
- **F+3** [283]: the first builder, song 147 (no ramp), jingle 20 `basic_wood_entrance_poh`, song 454
  `home_sweet_home`, the camera jumped (232/100). **F+4** [284] the black lifts and the first box.
- **Twelve boxes, each shown twice:** first with no continue (`if_setevents` none, text handed to 1508)
  while the scene plays, then the same text with its continue; the scene goes on from the click. Gaps
  from a click to the box's continue: 5, 19, 16, 7, 5, 4, 5, 12, 6, 6, (blank chatbox, 5,) 16, 2 ticks.
- The builders (`daddyshome_cutscene_builder_plain`, `_hammer` while working) fit a parlour and a garden
  ([291..334]: 243 `loc_add`s in all), a fresh copy at [338] shows the study's lectern, the nexus, the
  altar and the pool; another at [403] the pets and the throne room's lever, cage and pitfall.
  `option_hide_rooftops_override` goes 2 at [296] (with clientscript 7917).
- Four fitters ([301..313]) are added holding the hammer, plain four ticks later, gone on the fifth (one
  while a box waits for its click): `npc_add` duration 5 and an ai_queue2. Walks are queued (ai_queue1)
  so they step on the recorded tick; the study builder's route around the furniture [363..368] is four
  waypoints.
- **E** [446] (click + 1): an empty chatbox, the fade to black. **E+2** [448]: back on the tile the player
  left, the copy freed. **E+3** [449]: the unlock, `cam_unlock false`, one `cam_reset`, the panels,
  song 125 with fades 60/60, the black lifts. **E+4** [450]: status 4 and Yarlo's next page.
- **Jagex's chathead quirk, copied:** Yarlo's three pages after the scene ([450], [456], [460]) carry
  `daddyshome_cutscene_builder_plain` (10426) as the chathead; his own (10425) again from the next talk.
- No copy to be had: everything is put back and the twelve boxes play where the player stands.

### Wire audit (4350 [280..450] against `test/quests/daddyshome_nohouse.lua`'s tick log)

Every event placed by (box clicks since F, ticks since that click): the recording's F = [280] and clicks
[291 318 335 357 375 381 388 402 412 419 441 445]; ours F = 111 and the driver's twelve clicks.
`build/daddyshome_private/audit_scene.py`.

| kind | recording | ours | verdict |
|---|---|---|---|
| synths (id, delay) | 52 | 50 | matched; the two left are the recorder's stamina running out ([379] 3120 x3 with "about to expire", [399] 3424 with "has expired") |
| builder seqs, spotanims, map anims | 31 + 2 + 2 | 31 + 2 + 2 | matched |
| loc adds (243) and loc anims (6), the cage's delete | all | all | matched |
| builder adds/dels, transformations (20) | all | all | matched; the fourth fitter's plain form and delete ([312], [313]) fall after our driver's quicker click, at the same ticks from their segment's start |
| builder `[facing]` (coord and reset) | 30 | 30 | matched tick for tick |
| music, jingle | 147 (fade-out 50), 147, jingle 20, 454, 125 (60/60) | the same | matched |
| the end: copy freed, unlock + music, status 4 + page | click +3, +4, +5 | click +3, +4, +5 | matched |

The bather's 2-tile `teleport` at [395] is `npc_tele` (a glide on our wire). Camera lines are not in the tick
log; they are the recorded numbers in the copy's frame. Screens checked: the house exterior with its portal,
the furnished parlour with its roofs off, the study and nexus, the altar, the pets' garden.

## The build row without its materials (index 4350 [517..519], [644..646])

The panel opens as usual (the row's `available` false); its click closes the panel and opens a messagebox
"You don't have the right materials:<br>" and each material as "3 x Waxwood plank", red
(`<col=ef1020>`) while fewer are held ([646]: the planks held, plain). [519] answers on the click's tick,
[646] a tick later; ours answers on the click. With the tools or nails short: unverified (the panel just
closes, as before).

## The no-house reward (index 4350 [773..777])

Marlo's pages `chathap4`, `chatshifty3`, `chathap4`; on the box tick the house (`poh_house_style` 0 -> 1,
`poh_house_location` 0 -> 1), status 13, the crate and `poh_guide_book`, +400 xp, and objectbox_double
(crate, book) "Marlo rewards you with a crate of supplies, a book about how to use them, and 400
Construction XP. You also have a new house in Rimmington, where you can start building." No coins. Jagex's
starter style is 1 on the wire; this tree's `^poh_style_basic_wood` is 0 (a construction-wide encoding,
not changed here).

## Not ported

**The sawmill operator's waxwood talk** (wiki: "I need some waxwood planks for Old Man Yarlo."). The recorded
path is Buy-plank. Talk-to is still Bone Voyage's.
