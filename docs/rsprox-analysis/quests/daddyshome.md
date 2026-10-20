# Daddy's Home, from RSProx

Written 2026-10-20. Content: `OSRS-Content/osrs239-content/server/scripts/quests/quest_daddyshome/`,
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
  "Can I do some other kind of building work for you?", the paused-payment greeting, the no-house company pages and
  the no-house reward (a Rimmington starter house instead of the coins; its box text is not recorded anywhere).
- Marlo after the miniquest, and the crate replacement.
- Yarlo: before Marlo (only his greeting), "What am I supposed to be doing again?", the paused lecture menu,
  "How do I build furniture?", "Where do I get planks, nails and cloth?", the saw-only offer, "No thanks.",
  "I'll get on with it.", and the "before claiming reward" pages.
- The crate with three already held, the crate opened with a full pack, and any tool given with a full pack.
- The sawmill's coin refusal, the Woodcutting Guild menu without waxwood, and the max/start quantity (4291
  and 1686: both equal to the logs held; 1707 opened at 10).
- The quest journal's text.
- Nails never bend: no recording shows a bent one.

## Not ported

**The lecture's cutscene.** 1707 [11538..11695] and 3201 [798..]: a fade, a teleport to a POH-style
instance at 1854,5696 with `hidenpcops/hidelocops/hideobjops`, minimap off, camera moves, the builder npcs
(`daddyshome_cutscene_builder_*`) and "Home Sweet Home". Ours shows its twelve message boxes, verbatim and
in order, where the player stands.

**The sawmill operator's waxwood talk** (wiki: "I need some waxwood planks for Old Man Yarlo."). The recorded
path is Buy-plank. Talk-to is still Bone Voyage's.
