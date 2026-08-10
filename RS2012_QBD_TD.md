# RS2012 Queen Black Dragon and Tormented Demons

Implementation, cache-port, and evidence record for the 2012 combat profile in
`OSRS-Content` and `mock230`.

Last updated: 9 August 2026.

## 1. Scope and historical boundary

This port deliberately creates new `rs2012_*` content. It does not reuse or
reinterpret later Old School RuneScape QBD, tormented demon, burning-claw, or
demonic-gorilla content that happens to have a similar name.

The two historical targets are:

1. Queen Black Dragon as released on **29 May 2012**, with an optional explicit
   `7 August 2012` stabilisation profile for the four fixes Jagex documented.
2. Tormented demons after **While Guthix Sleeps**, using the pre-Evolution of
   Combat state visible during 2012. Dragon limbs are excluded because they were
   added on 20 November 2012 with the Evolution of Combat.

The commonly repeated 22 May date for QBD is not the release date used here.
Jagex's launch post is dated 29 May 2012 and says that the Queen had arrived.
The 22 May date belongs to the preceding Song from the Depths update and is
attached to some later infoboxes.

The port uses a ten-to-one boundary between 2012 life points and mock230 player
hitpoints. QBD and tormented demon NPC pools retain their real 2012 values
(18,750 per QBD pool and 3,260 per demon), incoming player damage is multiplied
by ten before it reaches those pools, and outgoing 2012 damage is divided by ten
for a 99-hitpoint OSRS player. XP remains based on the unscaled player roll.

## 2. Evidence policy

Every important statement in this document should be read with one of these
evidence classes:

| Class | Meaning |
|---|---|
| A | Dated Jagex news/update or later Jagex drop-rate disclosure |
| B | Timestamped 2012 RuneScape Wiki revision, i.e. contemporary community observation |
| C | Definition, map, or media relationship decoded directly from the supplied revision-727 cache |
| D | The supplied open727 server/client implementation; valuable implementation archaeology, but not proof of retail behaviour |
| E | Later wiki, guide, or empirical corroboration |

When A/B and D disagree, the port follows A/B. In particular, open727's QBD
pool size, phase placement of armour changes, timing, and reward weights are not
treated as canonical.

### Primary research links

- [Jagex QBD launch post mirror, 29 May 2012](https://wiki.darkan.org/Update%3AQueen_Black_Dragon%2C_Diamond_Jubilee_and_Clue_Fest) (A)
- [QBD article, revision 5685894, 30 May 2012](https://runescape.fandom.com/wiki/Queen_Black_Dragon?oldid=5685894) (B)
- [QBD article, revision 5861300, 29 June 2012](https://runescape.fandom.com/wiki/Queen_Black_Dragon?oldid=5861300) (B)
- [QBD article, last pre-rebalance revision 6053174, 6 August 2012](https://runescape.fandom.com/wiki/Queen_Black_Dragon?oldid=6053174) (B)
- [QBD strategy, revision 5818963, 24 June 2012](https://runescape.fandom.com/wiki/Queen_Black_Dragon/Strategies?oldid=5818963) (B)
- [Jagex 7 August 2012 QBD changes](https://runescape.fandom.com/wiki/Update:Some_Like_it_Cold) (A)
- [Later Jagex QBD unique-drop disclosure](https://www.runescape.com/drop-rates?set_lang=0) (A, later ruleset)
- [Jagex While Guthix Sleeps launch post mirror, 26 November 2008](https://wiki.darkan.org/Update%3AWhile_Guthix_Sleeps) (A)
- [Tormented demon article, revision 6586274, 19 November 2012](https://runescape.wiki/w/Tormented_demon?oldid=6586274) (B)
- [Tormented demon strategy, revision 6434662](https://runescape.wiki/w/Tormented_demon/Strategies?oldid=6434662) (B)
- [Ancient Guthix Temple, revision 6545327](https://runescape.wiki/w/Ancient_Guthix_Temple?oldid=6545327) (B)
- [Royal crossbow, revision 5952638, 18 July 2012](https://runescape.fandom.com/wiki/Royal_crossbow?oldid=5952638) (B)
- [Royal bolts, revision 6000529, 27 July 2012](https://runescape.fandom.com/wiki/Royal_bolts?oldid=6000529) (B)
- [Dragon kiteshield, revision 6025214, 2 August 2012](https://runescape.fandom.com/wiki/Dragon_kiteshield?oldid=6025214) (B)
- [Dragonbone upgrade kit, revision 6036646, 4 August 2012](https://runescape.fandom.com/wiki/Dragonbone_upgrade_kit?oldid=6036646) (B)
- [Royal dragonhide, revision 5967117, 20 July 2012](https://runescape.fandom.com/wiki/Royal_dragonhide?oldid=5967117) (B)

The archived wiki pages are evidence of what players had established at that
date, not leaked source code. Where a revision only supplied “rare” rather than
an exact denominator, this document does not manufacture certainty.

## 3. Queen Black Dragon encounter contract

### 3.1 Entry, life cycle, and victory

- Members-only, strictly solo instance (A/B).
- Combat level 2,100; Jagex recommended combat level 120 (A).
- Level 60 Summoning is a hard portal requirement (A/B).
- Song from the Depths is not required. It grants an unspecified incoming
  damage reduction; no percentage is invented here (A).
- On death, the grave is placed outside the Summoning-restricted door (A).
- QBD begins asleep. Contemporary strategy text describes about 30 seconds
  before detection; the port uses the 25.08-second wake sequence after a short
  presentation delay (B/C).
- QBD is never killed. The player drains four life-force pools and restores the
  four dragonkin artefacts, forcing her back to sleep (A/B).
- Historical total: **75,000 LP**, four pools of **18,750 LP**. A player hit is
  capped at **1,000 LP** (B). open727's 7,500-per-pool implementation is rejected.
- Every empty pool makes QBD untargetable and starts a worm-spawning
  intermission. The player must activate the current artefact. Adds persist
  through the first three restorations; the fourth clears them and exposes the
  reward stairs (B/C).

### 3.2 Phase matrix

| Phase | Pool | Added mechanics | Wall waves | Souls | Artefact at pool end |
|---|---:|---|---:|---:|---|
| 1 | 18,750 | melee, ranged, ordinary dragonfire, fire wall | 1 | 0 | north/central |
| 2 | 18,750 | prior set plus shadow soul | 2 | 1 | south-west/far-left |
| 3 | 18,750 | prior set plus crystal/carapace and siphon | 3 | 2 | south-east/far-right |
| 4 | 18,750 | prior set plus time stop and extreme fire | 3 | 4 | south/central, final |

Armour forms begin in phase 3. open727 calls them in phase 2, but release-era
strategy evidence consistently places them later.

### 3.3 Ordinary attacks

**Melee bite.** Used when the player is near the north-centre of the platform;
observed maximum 475 LP. Pre-EoC Protect/Deflect Melee fully blocks it (B).

**Ranged sweep.** Usable anywhere, including melee range; observed maximum 525
LP. Protect/Deflect Missiles fully blocks it (B).

**Ordinary dragonfire.** Without protection it commonly hits 700–900+ LP.
Even a full shield plus super antifire leaves low-200 damage in release-era
reports (B). The launch version had no anti-spam cooldown. Jagex added an
approximately ten-second cooldown on 7 August specifically to prevent unfair
spamming of an unblockable attack (A).

The code projects those values across the ten-to-one player boundary: 48 melee,
53 ranged, 70–90 unprotected fire, and 10–23 protected fire. The exact
accuracy formulas were not published and remain data constants.

### 3.4 Moving fire wall

Warning: `The Queen Black Dragon takes a huge breath.`

The 19-tile-wide orange wall travels north to south. Contact deals rapid
low-200s on each game tick. A stationary player typically receives two hits;
running through a gap normally receives one and may avoid it completely (B).

The three release patterns have their safe column at arena-local x 28, 37, or
32. In period prose these were described as tile 5, tile 9 (one west of the
middle artefact), and tile 15 from the platform edge. Revision-727 GFX 3158,
3159, and 3160 correspond to those patterns (B/C). Consecutive waves are seven
server ticks apart in open727; this is retained as a cache-era approximation
rather than claimed retail proof (D).

The implementation models a two-tile-deep damage front and advances it one tile
per tick. Wave count is 1/2/3/3, not the early, subsequently corrected claim of
four waves in phase 4.

### 3.5 Tortured souls and shadow projectiles

- Soul: level 147, 500 LP, slow movement, weak to slash (B/C).
- Counts by phase: 1 in phase 2, 2 in phase 3, 4 in phase 4 (B).
- A soul teleports one tile opposite the player and sends a slow purple shadow
  one square per tick toward the player's tile (B).
- The defining counterplay is preserved: step through the soul after the cast.
  The shadow continues and can strike a soul, another soul, or a giant worm.
  Its monster hit is lethal at 500 LP (B).
- Contemporary player-damage reports conflict: 152–199 in one strategy snapshot
  and 200–500 in the mature main article. The implementation exposes a 150–500
  LP range as constants and marks it uncertain.
- After its first cast a surviving soul uses weak, inaccurate melee (B).

The 7 August profile prevents a summon and a shadow cast from overlapping
within approximately ten seconds and keeps a soul from wandering immediately
after teleport. The strict launch profile intentionally permits the documented
release behaviour (A).

### 3.6 Crystal armour and hardened carapace

Crystal form resists Magic and is vulnerable to physical attacks. Hardened
carapace resists Melee/Ranged and is vulnerable to Magic (A/B).

No authoritative 2012 formula survived. open727 uses extreme defence-table
swaps; later guides describe roughly ±25% damage and about a minute of state.
The port uses a visible, configurable ±25% modifier for 40 ticks and an 80-tick
cooldown. This is a fidelity reconstruction, not a claim that those exact
numbers were retail source (D/E).

### 3.7 Soul siphon

Warning: `The Queen Black Dragon starts to siphon the energy of her mages.`

QBD drains living tortured souls and heals. Its launch existence is certain:
Jagex added an approximately 30-second cooldown on 7 August (A). Exact healing
was not established in the June snapshots. The implementation starts the move
in phase 3 and uses open727's approximation of 20 LP taken from each soul and
40 LP healed to QBD, behind named constants/cooldowns (D).

### 3.8 Time stop

Phase 4 only. A soul teleports to an east or west corner and channels for about
ten seconds. The following revision-727 strings are retained (B/C):

- `Kill me, mortal... quickly! HURRY! BEFORE THE SPELL IS COMPLETE!`
- `Time is short!`
- `She is pouring her energy into me... hurry!`
- `The spell is nearly complete!`

Killing the caster during the channel cancels the spell. Once it completes,
the screen was green, the player could not move, eat, heal, or attack for about
seven seconds, and other souls/worms froze. QBD and the caster continued to act.
Damage accumulated during the freeze and appeared in one hit when time resumed
(B/C).

mock230 now has a dedicated `player_lock()` / `player_unlock()` state rather
than abusing ordinary busy state. It clears movement and outgoing interaction,
centrally rejects movement/entity/location/item/interface action packets, but
allows queues, timers, incoming combat, and queued damage to continue. It also
clears on death, disconnect, and player reset. This is what makes the queued
one-hit release semantics deterministic.

Revision 727 used global config 1925 for the green overlay. OSRS239 has no
compatible carrier for that foreign global config; the lock and messages are
implemented, while the original overlay remains a documented UI-port item.

### 3.9 Extreme dragonfire

Warning: `The Queen Black Dragon gathers her strength to breathe extremely hot flames.`

This phase-4 attack targets the arena centre and falls off sharply toward the
east/west corners. Observed launch maximum is 970 LP. Cache/open727/later guide
evidence supports three rapid pulses; the June strategy still marked the pulse
count unknown, so three is labelled C/D/E rather than B (B/C/D/E).

The port uses three pulses and open727's distance divisor
`(distance_from_centre / 3) + 1`. The pulse also performs the Royal crossbow
forge/repair check.

### 3.10 Worm intermissions and artefacts

Giant worm: level 123, size 2. It uses accurate Magic around 200 LP and some
melee; Protect/Deflect Magic fully protects against the magic attack (B/C).
Contemporary prose says worms become larger in later phases, while the supplied
open727 encounter always uses NPC 15464. The current port retains 15464 and
marks later-size variants for visual QA.

open727 coughs one worm every ten ticks while an artefact remains unactivated;
that cadence is retained as D. Magical/raw platform tiles and the path to the
next artefact are changed by the authentic dynamic loc set. The exact raw-tile
damage cadence in open727 is not asserted as retail truth.

Artefact order and arena-local coordinates:

| Step | Dormant / active / complete locs | Arena-local coordinate |
|---|---|---|
| 1 | 70776 / 70777 / 70778 | (33,31,p1) |
| 2 | 70779 / 70780 / 70781 | (24,21,p1) |
| 3 | 70782 / 70783 / 70784 | (42,21,p1) |
| 4 | 70785 / 70786 / 70787 | (33,21,p1) |

## 4. QBD world and map assets

### 4.1 Fight arena

- Region 5731, map square `(22,99)`.
- Source world range `1408..1471, 6336..6399`, all four planes.
- XTEA: `[470121283, 1782907695, -131030477, -446458785]`.
- Revision-727 archives: `m22_99=7174` (42,073 bytes), `l22_99=7175`
  (245 decrypted bytes), `um22_99=7176`, `ul22_99=7177` (C).
- Presentation plane: 1.
- Queen: local `(31,37,p1)` / source `(1439,6373,p1)`.
- Player: local `(33,28,p1)` / source `(1441,6364,p1)`.

Static source placements include the four dormant artefacts on plane 1 and the
claws/platform machinery on plane 0. Significant plane-0 placements are right
claw 70818 `(39? source 1447,6371)`, left claw 70822 `(1429,6371)`, central
scenery 70788 `(1439,6365)`, and platform locs 70830/32/34/36/39/42. The whole
square is ported rather than reconstructing only these visible records.

Dynamic platform stages used by the controller:

- first artefact: 70843;
- phase-2 descent 70844, second path 70845;
- phase-3 descent 70846, third path 70847;
- phase-4 descent 70848, fourth path 70849;
- completion: 70837 and 70840 plus staircase 70790.

### 4.2 Reward chamber

- Region 5215, map square `(20,95)`.
- Source world range `1280..1343, 6080..6143`.
- XTEA: `[558713239, 1417714157, -302553027, -158448671]`.
- Archives `m20_95=7157` (21,488 bytes), `l20_95=7158` (8,428 decrypted
  bytes) (C).
- Player local `(31,36,p0)`; coffer core near local `(30,28,p0)`.
- The repeated loc 71039/model 69925 is genuine chamber scenery, not a decoder
  failure.

The room depends on locs 84, 25636, 70813–70815, 71039, 71429/30/33–36/38/39,
71442/43/46–48/50/52–54/59–61, 71598, 71612, 71625–30/32–39, 71764–70,
72483, and 72504. Their recursive model/sequence closure is imported with the
whole map, not substituted with OSRS scenery.

### 4.3 Authentic approach portal

- Portal loc 70812 at `(1200,6498,p0)`, region 4709 `(18,101)`.
- XTEA `[928791872, 1258826681, -1528400880, 1334217208]`.
- Archives `m18_101=7155`, `l18_101=7156`.
- Nearby shortcut 70799 at `(1207,6506)`.

The complete Grotworm route continues through 70793/95 in region 4707,
70794 in 4451, 70798 in 4453, 70797 in 5219, and 70796 in 5221. The encounter
debug command can enter directly, but production entry is bound to imported
portal 70812 and preserves the level-60 gate.

## 5. QBD definition and media manifest

All source IDs below are revision-727 IDs. Destination IDs are deliberately
allocated through `ports/rs2012_qbd_td.ini` and recorded in
`OSRS-Content/osrs239-content/port/rs2012_qbd_td.map`; source IDs must never be
assumed valid in OSRS239.

### 5.1 NPCs and models

| Source NPC | Imported symbol | Form | Models | BAS/render |
|---:|---|---|---|---:|
| 15454 | `rs2012_qbd_default` | active/default | 70260, 69766 | 2502 |
| 15506 | `rs2012_qbd_crystal` | crystal | 70267, 69766 | 2502 |
| 15507 | `rs2012_qbd_hardened` | carapace | 70268, 69766 | 2502 |
| 15509 | `rs2012_qbd_sleeping` | sleeping | 70260, 69766 | 2503 |
| 15510 | `rs2012_qbd_tortured_soul` | soul, level 147 | 70761 | 2514 |
| 15464 | `rs2012_qbd_giant_worm` | worm, level 123 | 69765 | 2500 |

BAS 2502 uses active idle 16715; 2503 uses sleeping idle 16716; 2514 uses
run/walk 16883/16884; 2500 uses movement 16786 and turn variants 16787.

### 5.2 Sequences

Durations are cache frame duration totals in 30 ms units.

| Sequence | Meaning | Duration |
|---:|---|---:|
| 16714 | wake | 25.08 s |
| 16715 | active idle | 11.52 s |
| 16716 | sleeping idle | 11.52 s |
| 16717 | centre melee | 2.70 s |
| 16718 | ranged sweep | 3.60 s |
| 16721 | ordinary breath | 2.70 s |
| 16742 | return to sleep | 13.68 s |
| 16743 / 16744 | west/east melee | 2.70 s each |
| 16745 | extreme breath | 9.00 s |
| 16746 | wall cast | 4.50 s |
| 16747 / 16748 | cough worm / stop cough | 1.20 / 1.80 s |
| 16758 / 16759 | left/right claw | 14.76 / 4.26 s |
| 16766 / 16768 / 16771 / 16774 | restoration / activate / complete / exit | 2.70 / 1.80 / 1.80 / 0.90 s |
| 16778 / 16779 / 16782 | worm death / defence / attack | 2.70 / 1.80 / 2.70 s |
| 16786 / 16787 | worm move / turn-ambient | 3.33 / 1.44 s |
| 16859 / 16861 / 16864 | soul death / teleport / cast | 2.28 / 1.80 / 2.91 s |
| 16883 / 16884 | soul run / walk | 1.80 s each |

The RS727 sequence codec is not the OSRS codec: opcode 13 contains nested sound
sets and later opcodes differ. The importer now decodes that grammar explicitly.
QBD animation archives use 4000/4024-era frame IDs; their V2 frame-file
framemap path is verified during import rather than inferred from the high bits.

### 5.3 Spot animations

The complete adjacent QBD effect cluster is imported, including variants that
open727 does not call:

| GFX | Model / sequence | Use |
|---:|---|---|
| 3141 | 69765 / 16799 | worm expel |
| 3142 | 69765 / 16800 | worm landing |
| 3143 | 69670 / 16728 | ordinary breath |
| 3144 | 69896 / 16866 | alternate soul cast |
| 3145 | 69896 / 16865 | soul cast |
| 3146 | 69897 / 16867 | moving shadow |
| 3147 | 69900 / 16856 | soul teleport |
| 3148 | 70211 / 16868 | siphon |
| 3149 | 69668 / 16869 | adjacent breath effect |
| 3150 | 70211 / 16740 | adjacent siphon effect |
| 3151 | 69900 / 16741 | adjacent teleport effect |
| 3152 | 69669 / 16749 | extreme breath |
| 3153–3154 | 69899 / 16753, 16755 | extreme variants |
| 3155–3157 | 69671 / 16762–16764 | flame variants |
| 3158–3160 | 69880/69878/69879 / 16761 | three wall gaps |
| 3161–3163 | 69780 / 16794–16796 | worm magic variants |
| 3164–3165 | 69667 / 16798, 16797 | worm projectiles |

RS727 spotanim model and sequence operands are BigSmart. Decoding them as the
OSRS width produced a plausible but false dependency graph; the profile codec
now consumes the source format exactly.

### 5.4 Animation audio

Sequence-embedded QBD sound events reference revision-727 index 14, not the
OSRS index-4 synth table. Alternative IDs at one frame are random alternatives,
not simultaneous sounds.

- Wake 16714: 14969, 14991, 15022, 14832, 14989, 14975, 14912, 14940,
  14992, 14914 across its marked frames.
- Active idle 16715: 14915/15022/14969 and 15015/14991/14972 alternative sets.
- Sleeping idle 16716: the same families across frames 5/18/28/43.
- Melee 16717 and side melees 16743/44: 14986/14930/15010.
- Ranged 16718: 14964. Ordinary breath 16721: 14908.
- Return sleep 16742: 14832, 15622, 14975, 14991, 14992, 14940, 14832,
  15613, 15621.
- Extreme 16745: 14988. Wall 16746: 14984 then 14896.
- Restoration 16766: 15636. Artefact complete 16771: 15634. Exit 16774: 15458.
- Worm attack 16782: 14844/14979/14974/14967.
- Worm turn 16787: 14921/15000/15014/14966/14922.
- Soul sequences carry no embedded audio.

The event references are retained in imported sequence configs. The audio
transcode section below distinguishes retained references from actually
renderable OSRS synth assets.

### 5.5 Core locs and models

- Restoration loc 70775: model 70271, sequence 16766.
- Artefacts 70776/79/82/85: model 70263.
- Clickable active 70777/80/83/86: model 70269, sequence 16768, `Activate`.
- Completed 70778/81/84/87: model 70270, sequence 16771.
- Staircase 70790: model 50039, `Climb down`.
- Portal 70812: models 69863/69864, `Investigate` / `Pass through`.
- Return portal 70813: models 69863/69869.
- Exit 70814: model 69673, sequence 16774.
- Coffer click/closed/open 70815/16/17: invisible, 69872, 69870.
- Right claws 70818–21: models 69887/69882/69886/69888.
- Left claws 70822–25: models 69885/69890/69883/69884.
- Platform family 70826–49: models 70285, 69617, 69905, 70195–70207,
  64941, and 69847–69850 as enumerated in the port ledger.

The late-RS2 loc codec uses BigSmart model/sequence/transform references and an
opcode-42 palette. A complete validation sweep consumes all 73,893 source loc
definitions exactly; this matters because many QBD models exceed 32,767.

### 5.6 Original interfaces and music

Revision-727 interface 1285 is the 35-component QBD pool/artefact HUD. It uses
sprites 10959–10968, hidden model 70127/sequence 9390, client scripts 6236 and
6240, and globals:

- 1923: LP lost within the current pool;
- 1924: progress states 0 start, 1 pool 1 down, 2 artefact 1, through 8 complete;
- 1925: time-stop overlay on/off.

Interface 1284 is the 47-component Dragonkin coffer. Its ten-slot container is
100 and offers Take/Bank/Discard/Examine plus Bank all, Abandon all, and Take
all. Dependencies include sprites 7920–22, 8278, 8384–98, 8444–46 and client
scripts 5399, 5400, 5409–11, 5415.

Music client IDs are 1119 `Awoken` and 1118 `Queen Black Dragon`, established
through source script maps 1345/1351. Numeric IDs in source index 14 are a
different namespace and must not be confused with music-track IDs.

## 6. QBD reward contract

### 6.1 Coffer behaviour

The mature 29 June revision is the strongest complete pre-7-August table. A
successful completion gives always-drops plus two configurable resource rolls,
unique rolls, and an eligible journal. The port never rolls the generic rare
drop table and never adds charms in the strict release profile (B).

Always:

- dragon bones x5;
- royal dragonhide x2–4;
- royal bolts x50–100;
- first dragonkin journal on first completion.

Resource entries preserved from the period table:

| Group | Entries |
|---|---|
| weapons/armour | rune battleaxe x1; dragon dagger x1; dragon med helm x1; dragon longsword x1; dragon spear x1 |
| runes | death x50 or x500; blood x500; soul x20–100; nature x300 |
| ores/seeds | coal x300–580; adamantite x50 or x172; runite x30 or x100; magic seed x1; snapdragon x5; watermelon x3; torstol x1 |
| consumables | rocktail x1–10; Saradomin brew(2) x1–10; super restore(2) x1–10 |
| resources/misc | coins x150k or x200k; magic logs x90–120; yew logs x150–500; onyx tips x30; dragonstone x1–9; shield left half x1; raw swordfish x200; grimy lantadyme x50; grimy torstol x10 |

Very rare/unique entries are Dragon kiteshield, dragonbone upgrade kit,
draconic visage, and the four Royal crossbow components.

No official 29 May denominator was published. The implementation uses the
later Jagex disclosures—kite 1/128, kit 2/128, each component 5/128, visage
1/109—as an explicitly labelled numeric reconstruction, because a server needs
a number. Those rates are configurable and are not described as proven launch
rates. A September 2012 Adventurer's Log sample also broadly supports the same
order of rarity, but it postdates the 7 August rebalance.

Journals are sequential and one-time: first guaranteed, then second common,
third uncommon, fourth rare. The port stores journal progression separately
from inventory so destroying a physical book cannot reset unlock state.

### 6.2 Reward items

| Source item | Meaning |
|---:|---|
| 24336 | Royal bolts, +125 ranged strength |
| 24337 | Royal crossbow (unforged) |
| 24338 | Royal crossbow |
| 24339 | degraded Royal crossbow |
| 24340 / 24342 / 24344 / 24346 | stabiliser / frame / sight / torsion spring |
| 24352 | dragonbone upgrade kit |
| 24354–24364 | dragonbone Infinity/dragon armour conversions |
| 24365 | Dragon kiteshield |
| 24368–24371 | four dragonkin journals |
| 24372 / 24374 | royal hide / leather |
| 24376 / 24379 / 24382 | royal vambraces / chaps / body |

Royal armour crafting is 87 Crafting for vambraces (1 leather, 94 XP), 89 for
chaps (2, 188 XP), and 93 for body (3, 282 XP); it requires 80 Ranged and 40
Defence to wear (B). Item/config/model assets are ported even where the broader
Crafting interface is outside the two encounter scripts.

### 6.3 Royal crossbow lifecycle

- 70 Smithing to assemble/forge; 85 Ranged to wield (A/B).
- Thurgo combines the coral crossbow and four tradeable QBD components into the
  unforged crossbow.
- During phase 4 the player chooses `Brandish`; an extreme-fire pulse turns the
  unforged weapon into the bound, untradeable Royal crossbow.
- It fires only Royal bolts.
- Ten hours of combat degrade it. mock230 records 60,000 game ticks and subtracts
  the actual attack cadence.
- Re-brandishing the degraded crossbow in extreme fire repairs it. The historical
  alternative—Thurgo plus another complete component set—is retained as a
  follow-up integration test.
- Contemporary reverse engineering reported two delayed hits eight seconds
  apart after a successful first hit. The compatibility implementation queues
  those non-XP bleed hits; exact accumulation/minimum behaviour remains labelled B
  community observation rather than official source.

## 7. Tormented demons

### 7.1 Access and encounter layout

Tormented demons are unlocked only after While Guthix Sleeps. The production
entrance also requires the sapphire lantern route into the Ancient Guthix
Temple. Six demons occupy the authentic temple combat area; instances preserve
the source map-square layout rather than substituting modern OSRS WGS scenery.

The source map closure contains `(40,89)`, `(39,89)`, `(39,90)`, `(39,91)`, and
`(40,90)`. Exact map placement/XTEA/static-loc tables are generated by the map
porter and appended to this section's audit table rather than guessed from
open727 spawn code.

### 7.2 Definition and combat state

- NPC variants: 8349 melee prayer, 8350 magic prayer, 8351 ranged prayer (C).
- Combat level 450, 3,260 LP, size 3, attack rate six ticks (B/C).
- Maximum melee 189 LP; Magic/Ranged and rage splash approximately 269–270 LP
  (B). At the OSRS boundary these are 19 and 27 hitpoints.
- Poison immune (B).
- Player protection prayers fully negate the matching demon attack in this
  pre-EoC profile (B).
- Demon attacks are distance-aware. A selected melee style is not silently
  wasted while the target is out of reach; the controller selects a usable
  attack and retains the historical style cadence.

### 7.3 Independent attack-style timer

The release encounter changes offensive style on an approximately 16-second
timer, independent of its six-tick ordinary attacks (B). It is not a block of
exactly five attacks. The implementation uses a 27-game-tick timer and the
10642 roar/rage sequence when transitioning.

The transition launches a small through-scenery area attack. Surviving period
descriptions establish the attack and its approximate maximum, but not a
source-code radius. The port uses a conservative radius 2 and maximum 269 LP,
both named constants for correction if stronger evidence is recovered.

### 7.4 Protection-prayer adaptation

The demon measures **310 LP of pre-shield incoming damage** in the current
player combat style, then changes its protection prayer to that style (B/D).
Misses and successful hits below 20 LP count as 20 toward the threshold. This
counter uses the original pre-shield roll; open727 incorrectly counts the
already-reduced result. Each demon keeps its own counter/state in the 16 new
mock230 per-NPC integer slots, so six demons cannot leak adaptation state into
one another.

Once the matching prayer is active, that combat style is fully blocked. The
player must switch weapons/styles; the implementation's global hit-preparation
hook performs this before NPC damage while returning a separate XP value.

### 7.5 Fire shield and Darklight

- The visible fire shield reduces ordinary damage by 75% (B/E).
- A successful non-zero Darklight hit disables the shield for about 60 seconds
  and refreshes that duration on subsequent valid hits (B).
- Darklight cannot lower the shield while the demon is protecting from Melee.
- Shield damage reduction is applied after the 310-LP prayer counter records the
  original hit, preserving the historical tactic.

This is intentionally not the modern OSRS tormented-demon 20% reduction.

### 7.6 TD media

| Asset | Source IDs |
|---|---|
| NPCs | 8349, 8350, 8351 |
| Core sequences | 10917–10924; transition roar 10642 |
| Spot animations | 1883 magic cast, 1884 projectile, 1885 impact, 1886 melee impact, 1887 ranged projectile, 1888 ranged cast |
| Rig | framemap 2401 for sequence 10917 family |

The source-to-destination symbols are
`rs2012_tormented_demon_melee/magic/ranged` and
`rs2012_td_magic_cast/projectile/impact`, `rs2012_td_melee_impact`, and
`rs2012_td_ranged_projectile/cast`. Full model/BAS/sound details are recorded in
the generated port ledger and final cache audit.

### 7.7 TD drops

The port uses the pre-EoC-era reward family:

- dragon claws;
- ruined dragon armour lump, slice, and shard for the dragon platebody;
- charms and the period common/rare resource table;
- no dragon limbs before the 20 November 2012 EoC addition.

Dragon claws are configured near the contemporary roughly 1/300 estimate. Exact
period denominators for every ruined piece/common entry are not all official;
they remain data constants with evidence labels. The code does not copy current
OSRS tormented synapse or burning-claw drops.

## 8. Cache-port architecture

### 8.1 Isolated lane and allocation

Manifest: `ports/rs2012_qbd_td.ini`.

Lane: `OSRS-Content/osrs239-content/ported/rs2012_qbd_td` plus matching
`models/`, `animsets/`, `framemaps/`, and `synth/` ported subtrees.

Allocation bases:

| Namespace | Destination base |
|---|---:|
| NPC | 25,000 |
| obj | 45,000 |
| loc | 63,000 |
| spotanim | 10,000 |
| model | 110,000 |
| sequence / animset | 22,000 |
| framemap | 9,000 |
| synth | 16,000 |

The loc base was deliberately moved from 60,000 to 63,000 after validation
found live OSRS239 locs through 62,200. The collision had also made coffer
multiloc references resolve to unrelated spiral-stair symbols; reallocation
corrected them to `rs2012_loc_70816/70817`.

The first full generic closure contained 9 NPCs, 62 objs, 54 dynamic locs, 31
spotanims, 150 models, 61 sequences, 22 animation sets, 21 framemaps, and six
index-4 TD synths. The complete explicit QBD sequence list adds six sequences
without renumbering any established destination ID. Static map locs and their
recursive model dependencies extend those totals in the map pass.

### 8.2 Codec work required by revision 727

- Profile-specific spotanim grammar with BigSmart model/sequence references.
- Profile-specific late-RS2 sequence grammar, including nested opcode-13 sound
  alternatives and opcodes 15/16/18/19/20/249.
- BAS type/codec for movement and render-animation dependencies.
- Late-RS2 loc codec with BigSmart model/sequence/transform references and
  opcode-42 palettes; 73,893/73,893 exact-consumption sweep.
- RS727 map terrain uses one-byte overlay/attribute fields even though revision
  727 is numerically above OSRS209. Width is selected by cache branch/profile,
  not revision number.
- V2 frame files resolve their embedded framemap ID rather than assuming the
  archive high bits identify the rig.
- Model transcode preserves textured-face IDs, coordinate arrays, and opaque
  tails instead of stripping them to make an encoder succeed.

### 8.3 Engine extensions

The encounter needed small general engine primitives rather than QBD-specific C:

- coordinate-to-coordinate projectile command
  `projanim_map(from,to,spot,fromHeight,toHeight,delay,duration,peak,arc)`;
- 16 persistent integer slots per NPC, reset on spawn/respawn and preserved over
  `npc_changetype`;
- corrected `npc_changetype(type,duration)` stack handling;
- player action lock/unlock with central packet gating and live delayed damage;
- hit-preparation hook returning both effective NPC-pool damage and XP damage.

Compiler metadata, wire protocol, NPC-state isolation/respawn, action-lock,
projectile, and mock-server selftests cover these paths.

### 8.4 Known media conversion boundary

Revision 727 materials are procedural: index 26 provides the texture-definition
table and index 9 contains property graphs, which can recursively sample index-8
sprites or other materials. OSRS239 expects sprite-backed texture records.

The imported OB3 models preserve every texture ID and mapping array—QBD models
70260/70267/70268 have 233/233/241 textured faces, TD model 44733 has 39,
crossbow 70257 has 47—but preserving references is not the same as providing
pixels. The material pass therefore rasterises every referenced RS727 graph to
an OSRS-compatible sprite-backed material and remaps the model faces. Until that
pass and visual comparison succeed, a textured model may decode correctly but
render with a missing/wrong material; validation must report this honestly.

Likewise, QBD sequence sounds are index-14 MIDI/Vorbis event IDs. Six TD
index-4 synth dependencies are directly portable. QBD audio requires decoding
the index-14 sample/patch chain or rendering compatible samples; retaining event
numbers alone is not called complete audio fidelity.

The original 1284/1285 interfaces also require component operand remapping,
sprite allocation, CS2 dependency closure, and replacements for foreign global
configs 1923–1925. Gameplay is not made dependent on those UI assets: server
messages and encounter state remain authoritative while the UI port is tested.

## 9. Implementation file index

QBD:

- `OSRS-Content/osrs239-content/server/scripts/minigames/minigame_rs2012_qbd/configs/rs2012_qbd.constant`
- `.../rs2012_qbd.npc`, `rs2012_qbd.obj`, `rs2012_qbd.varp`, `rs2012_qbd.dbrow`
- `.../scripts/rs2012_qbd_session.rs2`
- `.../scripts/rs2012_qbd_combat.rs2`
- `.../scripts/rs2012_qbd_adds.rs2`
- `.../scripts/rs2012_qbd_rewards.rs2`
- `.../scripts/rs2012_royal_crossbow.rs2`

TD:

- `OSRS-Content/osrs239-content/server/scripts/areas/area_rs2012_tormented_demons/configs/`
- `.../scripts/rs2012_td_encounter.rs2`
- `.../scripts/rs2012_td_combat.rs2`
- `.../scripts/rs2012_td_player_hit.rs2`
- `.../scripts/rs2012_td_drops.rs2`
- `.../scripts/rs2012_td_selftest.rs2`

Global integration is limited to the shared player-hit preparation point,
ranged Royal-bolt/crossbow validation, QBD/TD death and logout cleanup, and
Thurgo's existing conversation entry. All named encounter content remains in
the `rs2012` namespace.

## 10. Build, staging, and verification

The feature lane is included as both a compiler pack and component root for
`make -C src mock230-scripts`. The cache half is baked through a disposable
staged overlay, not by flattening foreign configs into the base OSRS239 source
tree. The source-to-destination ledger is checked before every apply so adding a
dependency cannot silently renumber an existing symbol.

Required acceptance tests:

### Cache and media

- manifest dry run and apply produce the same ledger;
- no allocation overlaps base OSRS239 or the Summoning lane;
- every NPC/obj/loc/spotanim config compiles with no unresolved source ID;
- each model decodes after destination pack; texture face counts and coordinate
  arrays match source;
- every sequence resolves at least one real animset/framemap and frame duration;
- whole arena/reward/temple maps round-trip from RS727 decode through OSRS239
  LostCity text and packed cache;
- every scenery loc in those maps resolves to an imported destination loc;
- procedural material screenshots match source-client renders for default,
  crystal, hardened, soul, worm, all three TD forms, claws, coffer, and crossbow;
- every embedded audio event resolves to an actual destination sound asset;
- original HUD/coffer component trees load without stale model/sprite/script IDs.

### QBD gameplay

- four transitions require exactly 18,750 effective LP each;
- no hit exceeds 1,000 LP and QBD never reaches ordinary NPC death;
- 1/2/3/3 walls and all three gap columns are reachable;
- soul counts are 1/2/4 and shadows can kill souls/worms;
- siphon damages living souls and heals QBD;
- both armour forms apply the correct opposing style response only in phase 3+;
- killing a time caster before completion cancels; after completion it does not;
- locked player input is rejected while queued QBD damage continues and lands as
  one release hit;
- extreme fire scales with centre distance and forges/repairs a brandished
  crossbow;
- worm spawning and artefact order are exact; only the fourth clears adds;
- death frees the instance and puts the grave outside the portal;
- coffer always-drops, resource rolls, bank/take actions, and sequential journals
  are deterministic under seeded RNG tests;
- strict-launch and 7-Aug profiles differ only in documented switches.

### TD gameplay

- six NPCs retain independent style/prayer/shield counters;
- attack style changes by time, not a five-attack block;
- matching player prayer fully blocks the demon attack;
- 310 pre-shield LP triggers the correct protection, including miss/minimum-20
  accounting;
- active fire shield reduces damage by 75%; valid Darklight suppresses for 60
  seconds and matching Protect Melee prevents suppression;
- melee is not selected/wasted outside reach;
- rage splash respects configured radius/maximum and collision intent;
- all three forms use correct animation/GFX/projectile assets;
- drops include claws/three ruined pieces, exclude dragon limbs and modern OSRS
  uniques, and cleanly handle death/logout/instance ownership.

## 11. Uncertainties that must remain visible

- No official source establishes exact 29 May QBD unique denominators.
- The exact launch soul-shadow player formula and siphon heal formula are not
  recoverable from the inspected snapshots.
- Three extreme-fire pulses are strong cache/later corroboration, not explicit
  in the June strategy text.
- QBD armour duration/modifiers and several projectile delays are reconstructed.
- The precise TD rage radius and several ordinary-drop denominators remain
  contemporary estimates.
- open727 contains demonstrable encounter bugs: a duplicate melee branch where
  Magic should be, post-shield prayer accounting, odd reset/switch behaviour,
  random wasted melee at range, an unverified hard-coded AoE, five-attack style
  blocks, and QBD phase/HP/reward divergences. None is silently preserved.

These are configuration and evidence boundaries, not invitations to replace
2012 mechanics with current OSRS behaviour.
