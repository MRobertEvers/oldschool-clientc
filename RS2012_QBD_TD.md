# RS2012 Queen Black Dragon and Tormented Demons

Implementation, cache-port, and evidence record for the 2012 combat profile in
`OSRS-Content` and `mock230`.

Last updated: 10 August 2026.

## 1. Scope and historical boundary

This port deliberately creates new `rs2012_*` content. It does not reuse or
reinterpret later Old School RuneScape QBD, tormented demon, burning-claw, or
demonic-gorilla content that happens to have a similar name.

The two historical targets are:

1. Queen Black Dragon as released on **29 May 2012**, with an optional explicit
   `7 August 2012` stabilisation profile for the four fixes Jagex documented.
2. Tormented demons as introduced with **While Guthix Sleeps** on 26 November
   2008, using the pre-Evolution of Combat profile still visible during 2012.
   Dragon limbs are excluded because they were added on 20 November 2012 with
   the Evolution of Combat.

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
- [Giant worm, revision 5795384, 21 June 2012](https://runescape.fandom.com/wiki/Giant_worm?oldid=5795384) (B)
- [Jagex 7 August 2012 QBD changes](https://runescape.fandom.com/wiki/Update:Some_Like_it_Cold) (A)
- [Later Jagex QBD unique-drop disclosure](https://www.runescape.com/drop-rates?set_lang=0) (A, later ruleset)
- [Jagex While Guthix Sleeps launch post mirror, 26 November 2008](https://wiki.darkan.org/Update%3AWhile_Guthix_Sleeps) (A)
- [Tormented demon, revision 54792, 14 November 2012](https://wiki.darkan.org/index.php?title=Tormented_demon&oldid=54792) (B)
- [Tormented demon strategy, revision 66515, 17 October 2012](https://wiki.darkan.org/index.php?title=Tormented_demon/Strategies&oldid=66515) (B)
- [Ancient Guthix Temple, revision 54809, 9 November 2012](https://wiki.darkan.org/index.php?title=Ancient_Guthix_Temple&oldid=54809) (B)
- [Tormented-demon charm log, revision 65249, 23 October 2012](https://wiki.darkan.org/index.php?title=Charm:Tormented_demon&oldid=65249) (B)
- [Dragon claw, revision 49824, 11 November 2012](https://wiki.darkan.org/index.php?title=Dragon_claw&oldid=49824) (B)
- [Jagex/Mod Ash recovery of the original TD levels, 23 January 2024](https://x.com/JagexAsh/status/1749777281675907450) (A, later primary-source recovery)
- [Royal crossbow, revision 5952638, 18 July 2012](https://runescape.fandom.com/wiki/Royal_crossbow?oldid=5952638) (B)
- [Royal crossbow mechanics, revision 145907, 5 October 2012](https://wiki.darkan.org/index.php?title=Royal_crossbow&oldid=145907) (B)
- [Royal bolts, revision 6000529, 27 July 2012](https://runescape.fandom.com/wiki/Royal_bolts?oldid=6000529) (B)
- [Dragon kiteshield, revision 6025214, 2 August 2012](https://runescape.fandom.com/wiki/Dragon_kiteshield?oldid=6025214) (B)
- [Dragonbone upgrade kit, revision 6036646, 4 August 2012](https://runescape.fandom.com/wiki/Dragonbone_upgrade_kit?oldid=6036646) (B)
- [Royal dragonhide, revision 5967117, 20 July 2012](https://runescape.fandom.com/wiki/Royal_dragonhide?oldid=5967117) (B)
- [Awoken music track](https://runescape.wiki/w/Awoken) and [Queen Black Dragon music track](https://runescape.wiki/w/Queen_Black_Dragon_(music_track)) (E; titles/order corroboration)
- [Grotworm surface cave entrance and object-map coordinate](https://runescape.wiki/w/Cave_entrance_(Grotworm_Lair)?oldid=36479017) (E)
- [Grotworm cave exit and surface pairing](https://runescape.wiki/w/Cave_exit_(Grotworm_Lair)?oldid=36642689) (E)
- [RuneScape map transform data for the three Grotworm levels](https://runescape.wiki/w/Module:Map_coordinates/transform-data.json) (E)
- [OpenRS2 revision-727 verified XTEA set](https://archive.openrs2.org/caches/runescape/309/keys.json) (C)

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
  before detection; the port uses the 16.72-second wake sequence after a short
  presentation delay (B/C).
- QBD is never killed. The player drains four life-force pools and restores the
  four dragonkin artefacts, forcing her back to sleep (A/B).
- Historical total: **75,000 LP**, four pools of **18,750 LP**. A player hit is
  capped at **1,000 LP** (B). open727's 7,500-per-pool implementation is rejected.
- Every empty pool makes QBD untargetable and starts a worm-spawning
  intermission. The player must activate the current artefact. Adds persist
  through the first three restorations; the fourth clears them and exposes the
  reward stairs (B/C).

The arena and reward room share a dedicated, player-owned QBD instance handle.
A one-tick lifecycle watchdog detects any ordinary teleport out of either
scene, preserves that teleport's destination, and idempotently clears the
action lock, encounter queues/timers, walk gate, HUD/coffer transmission,
music, and all QBD-owned NPCs before releasing the allocator slot. Death holds
the slot through the corpse delay; logout and the explicit exit use the same
cleanup path. An unclaimed ten-slot reward container and its permanent
`reward_ready` flag survive all of those paths, so returning mounts a fresh
reward instance without rerolling or losing the coffer. The normal portal and
ordinary debug entry retain the 60 Summoning gate; only the composed-cache
manifest's QA command bypasses that one gate, without changing the account's
stats or quest state.

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
53 ranged, 70–90 unprotected fire, and 10–23 protected fire. The exact retail
accuracy formulas were not published. The cache level 2,100 is therefore an
explicit outgoing-accuracy proxy, while the exact open727 attack/defence bonus
rows remain visible data. All QBD, soul, and worm hits use mock230's ordinary
two-dice accuracy and style-specific equipment reduction before prayer or the
time-stop accumulator; they are no longer unconditional random damage.

### 3.4 Moving fire wall

Warning: `The Queen Black Dragon takes a huge breath.` (orange)

The wall spans the full 19-square platform and travels north to south at one
tile per game tick. A stationary player typically receives two hits; running
through a gap normally receives one and may avoid it completely (B). Contact
"is the same as being hit by her Dragonfire attack" (B, Tip.It): the
implementation rolls the dragonfire-protection ladder per contact tick —
low-200s LP with a shield or antifire, up to mid-700s unprotected.

The three wall types have their safe gap at squares **5, 9 (one west of the
centre artefact), and 15** of the 19-square platform — arena-local columns
28, 32, and **38** (B; open727's x 37 for the east gap contradicts the
period text and is rejected). **"She will cycle between these waves so a
player may predict where the next gap will be"** (B) — the gap types cycle
deterministically; the cycle position persists across casts and phases.
Consecutive waves are seven server ticks apart in open727; retained as a
cache-era approximation rather than claimed retail proof (D).

The visual is the authentic wall spot-animation family delivered exactly as
retail delivered it: the **official MAP_PROJANIM packet** with flat-glide
parameters (heights 0, peak 0, arc 0, duration 570 cycles = one row per
tick), so the projectile math degenerates to a ground-hugging, unrotated
glide in lockstep with the two-tile-deep damage front. Vertex measurement of
the rev-727 wall models (76.25 tiles of face geometry, gap holes at
model-local −20/+16/−4 tiles for 3158/3159/3160) fixes each pattern's spawn
anchor at columns 48/22/36 respectively; a shared anchor puts every visual
gap in the wrong column (C). Wave count is 1/2/3/3, not the early,
subsequently corrected claim of four waves in phase 4.

The full tick-level contract lives in
`docs/rs2012_qbd_encounter/ENCOUNTER.md` §6, with its research provenance in
`docs/rs2012_qbd_encounter/RESEARCH.md`; the mock230 selftest byte-decodes
the waves off the wire (count, cycle order, 7-tick spacing, duration, zero
arc).

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

Souls and worms live in the same 2012-LP domain as the Queen but are ordinary
mortal adds. Every successful player roll is multiplied by ten before reaching
their 500/650-LP pools, while XP retains the original old-HP roll; a miss remains
zero in both domains. They deliberately bypass QBD's 1,000-LP hit cap,
intermission immunity, and leave-one-LP phase transition. Ordinary attacks,
Royal-crossbow delayed splats, and all Dragon-claw splats share this preparation
path, including the remaining-HP XP clamp on a killing hit.

The 7 August profile prevents a summon and a shadow cast from overlapping
within approximately ten seconds and keeps a soul from wandering immediately
after teleport. The strict launch profile intentionally permits the documented
release behaviour (A).

### 3.6 Crystal armour and hardened carapace

Crystal form resists Magic and is vulnerable to physical attacks. Hardened
carapace resists Melee/Ranged and is vulnerable to Magic (A/B).

No authoritative 2012 formula survived. open727 uses defence-table swaps:
default `100/100/100/100/100`, crystal `10/10/10/200/10`, and hardened
`200/200/200/10/200` for stab/slash/crush/Magic/Ranged. The port preserves
those exact bonus rows for 40 ticks with an 80-tick cooldown and uses a labelled
base-Defence adapter of 100. Against the deterministic representative attack
roll this produces about 81%/57%/36% hit chance for weak/default/resistant
styles. It deliberately does **not** add the later guide's unverified ±25%
damage transform on top of the sourced accuracy change (D/E).

### 3.7 Soul siphon

Warning: `The Queen Black Dragon starts to siphon the energy of her mages.`

QBD drains living tortured souls and heals. Its launch existence is certain:
Jagex added an approximately 30-second cooldown on 7 August (A). The period
Tortured-soul page and Tip.It describe a **continuous** drain — "continuous
40s"/"continuous 20s", "this attack continues until all Tortured souls have
been killed", a full 500-LP soul worth 1,000 LP to her (B). The
implementation is therefore a channel: every 2 ticks each living idle soul
loses 20 LP and heals her 40, until no soul remains or a full soul has been
drained (25 drains), behind named constants/cooldowns. It starts in phase 3.

### 3.8 Time stop

Phase 4 only. A soul teleports to an east or west corner and channels for
about ten seconds — the implementation speaks the four lines at 3-tick
intervals from the teleport and completes the spell 15 ticks (9 s) after it,
matching the period "10 seconds" within a tick. The following revision-727
strings are retained (B/C):

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
compatible carrier for that foreign global config, so the server now drives
the ported interface-1285 overlay directly while the action lock and delayed
damage remain authoritative server state.

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

Giant worm: level 123, size 2, **650 LP**. Its dated definition records both
Melee and Magic, an approximate 200-LP maximum, and bones as the always-drop;
Protect/Deflect Magic fully protects against the accurate magic attack (B/C).
Contemporary prose says worms become larger in later phases, while the supplied
open727 encounter always uses NPC 15464. The current port retains 15464 and
marks later-size variants for visual QA.

Era sources conflict on the eruption cadence — RuneHQ "every second" (B),
Tip.It "every three seconds" (B), open727 every ten ticks (D); the
implementation adopts the era-guide midpoint of one worm per five ticks and
delivers it as the period visual: the queen coughs (16747) and **lobs the
worm projectile (GFX 3141) to a random mid-field tile**, where the landing
splash (3142) plays and the worm hatches three ticks after the cough,
already aggressive (B/C for the visuals, the exact interval remains a
labelled reconstruction). Killed souls and worms yield once after their
drop/state trigger so generic post-kill credit still sees the dead NPC, then
explicitly call `npc_del`; mock230 therefore cannot re-arm an ordinary NPC
respawn clock. Final-restoration cleanup removes surviving adds without rolling
their death drops.

The authentic dynamic locs reveal each new path, while a per-candidate movement
hook enforces open727's exact raw-platform masks below local z 28. Phase 2
unlocks the 47-tile west mask, phase 3 the 87-tile west/east union, and phase 4
the 99-tile west/east/centre union. Because `walkstep_coord` is checked for
every candidate tile, a two-tile run cannot skip a locked square. Standing on
raw magic deals 200 LP every fourth tick in the D profile; victory, death,
logout, and the reward-room transition clear both the hazard and movement hook.

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
claws/platform machinery on plane 0. In the destination, the hand-rock/platform
geometry remains on plane 0, while the right claw 70818 at local `(39,35)` /
source `(1447,6371)` and left claw 70822 at `(21,35)` / source `(1429,6371)`
are deliberately placed on presentation plane 1 with QBD and the player.
Central scenery 70788 at `(1439,6365)` and platform locs
70830/32/34/36/39/42 remain on plane 0. The map-port test records this narrow
destination composition override; the whole square is otherwise ported rather
than reconstructing only these visible records.

Dynamic platform stages used by the controller:

- first artefact: 70843;
- phase-2 descent 70844, second path 70845;
- phase-3 descent 70846, third path 70847;
- phase-4 descent 70848, fourth path 70849;
- completion: 70837 and 70840, then staircase 70790 + stairwell 70775.

Each arena half is one loc in three states — intact (70836/70839), completion
(70837/70840), collapsed (70838/70841) — so completion is a REPLACEMENT on the
map slab's own `(tile, level, shape)` slot, not a second loc laid over it.
The slabs are map-placed on **plane 0** at (21,24) and (33,24), one tile west of
the rev-727 anchors (LARGE_LOCS_PAINTER.md §16), and all six carry the matching
footprint correction from `[footprint:loc]` in the manifest. Adding a state on
the arena level, or on the rev-727 anchor, gives it its own slot and leaves the
arena wearing two floors.

**The completion state is 70837/70840, not 70838/70841.** This section said the
opposite until 2026-08-15 and the port followed it, which ended every fight with
the arena floor gone. open727's `QueenBlackDragon.switchPhase` case 5 spawns
70837 at (22,24,-1) and 70840 at (34,24,-1) and never places 70838/70841 at all,
and the models agree: 70837 is `models=110139,110140` — the 8,211-face floor half
plus a 79-face lit-seam overlay — while 70838's lone 110141 is a 601-face rim
spread across the whole 12×18 footprint with none of the plates in it. Rendered
by itself (`rs2012_model_view --pitch 512`) 110141 is a dark outline of the slab
and nothing else; 110144 (400 faces) is the same for the east half. They are the
half AFTER it falls away, not a floor with a stairwell in it.

The stairwell itself is **70775** — model 110109, 9,379 faces standing 3,131
units over a 5×5 footprint, `anim=rs2012_seq_16766` (12 frames, ~90 cycles,
sound 15636), `randomanimstart=no`. open727 places it at (31,29,-1) two ticks
after the floor state, on the same beat as clickable staircase 70790 at
(31,29,0). The `-1` is the floor plane, which is where this port puts it too:
on that plane it replaces the map's own central plate 70788 — same tile, level,
shape and 5×5 `blockwalk`, so the collision map does not move — while 70790
stays on the arena level with the player. An earlier note claimed 70775 could
not be placed because the wire carries one loc per (tile, layer) and it would
take 70790's slot; that is only true if both sit on the arena level.

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

### 4.3 Authentic Grotworm approach and portal

The production route begins at surface cave entrance 70792 at
`(2988,3236,p0)`, map square `(46,50)`, local `(44,36)`. This is the 22 May
2012 Grotworm entrance east of Rimmington mine, not the visually similar Song
from the Depths cave farther south. The page's contemporary object-map record
supplies the coordinate and source ID. Revision 727 contains loc 70792 and its
models, but neither the local key set nor OpenRS2's verified revision-727 key
set contains an `l46_50` group. A direct cache lookup also finds no named loc
archive. It was therefore a server-dynamic placement, not missing scenery that
can honestly be claimed as decoded map data. `BASE_PLACEMENTS.tsv` records it;
the sparse stager retains OSRS239 `m46_50` and appends only ledger-remapped loc
70792 to a disposable copy of member 1. Shape 10 comes from the loc config;
angle 0 retains its unrotated one-by-three footprint. Because the retail server
placement stream is unavailable, that angle is a documented reconstruction,
not a value decoded from `l46_50`.

The underground map is not merely the six squares containing clickable route
objects. Boundary terrain and models occupy thirteen complete source squares,
including the continuous middle-level corridor at `20_100` and its eastern
edge squares. All thirteen are imported so walking between the openings does
not cross an absent archive:

| Square | Region | m archive | l archive | XTEA | Placements |
|---|---:|---:|---:|---|---:|
| `16_99` | 4195 | 7117 | 7118 | `[477331269,-1261056419,-1221718129,1502185528]` | 1,088 |
| `17_99` | 4451 | 7168 | 7169 | `[-1909749566,258386662,8275989,315008195]` | 3,448 |
| `18_99` | 4707 | 7137 | 7138 | `[-435342694,-1956791799,-570358413,-1529989129]` | 3,707 |
| `20_99` | 5219 | 7139 | 7140 | `[1817994005,-1939081852,1483364481,986296098]` | 4,015 |
| `21_99` | 5475 | 7125 | 7126 | `[1431497013,549444996,-1138684586,-2048667815]` | 1,115 |
| `20_100` | 5220 | 7141 | 7142 | `[241390015,-1595154578,639556659,-1137712732]` | 3,471 |
| `21_100` | 5476 | 7121 | 7122 | `[-1223211157,362322168,2034488860,165451212]` | 4 |
| `20_101` | 5221 | 7127 | 7128 | `[-517702445,-1565676718,-1788593958,-1812459785]` | 3,946 |
| `21_101` | 5477 | 7119 | 7120 | `[737530024,1576892563,564068280,-1179796355]` | 970 |
| `16_101` | 4197 | 7143 | 7144 | `[-1426155942,-838747053,96059630,-1889001428]` | 1,327 |
| `17_100` | 4452 | 7123 | 7124 | `[1905932215,-1526576869,-1403936829,1608882513]` | 1,088 |
| `17_101` | 4453 | 7149 | 7150 | `[-1632718105,2121579019,1093321834,1255552106]` | 3,087 |
| `18_101` | 4709 | 7155 | 7156 | `[928791872,1258826681,-1528400880,1334217208]` | 3,231 |

The exact bidirectional topology is:

1. Surface entrance 70792 `(2988,3236)` connects to cave exit 70793 at
   `(1207,6370)`, square `18_99`, local `(55,34)`, shape 10, angle 1.
2. The first/young-grotworm level runs across `16_99`, `17_99`, and `18_99`.
   Its deeper opening 70794 is at `(1088,6359)`, local `(0,23)`, angle 3.
3. 70794 connects to middle-level opening 70796 at `(1341,6487)`, square
   `20_101`, local `(61,23)`, angle 1. The adjacent historic arrival cited for
   this floor is `(1340,6488)`.
4. The ordinary middle route traverses the six-square `20/21_99..101` area to
   opening 70797 at `(1341,6379)`, square `20_99`, local `(61,43)`, angle 1.
5. 70797 connects to the mature-grotworm opening 70798 at `(1088,6496)`,
   square `17_101`, local `(0,32)`, angle 3; the adjacent arrival is
   `(1090,6497)`.
6. Shortcut 70795 at `(1179,6355)`, local `(27,19)`, angle 0 links the first
   level directly to shortcut 70799 at `(1207,6506)`, local `(55,42)`, angle
   0, bypassing the ordinary middle-level walk.
7. Portal 70812 at `(1200,6498)`, square `18_101`, local `(48,34)`, shape 10,
   angle 0 enters the private QBD arena and retains the level-60 Summoning gate.

The ordinary cave links have isolated production handlers in
`rs2012_grotworm_route.rs2`; the existing 70812 QBD handler remains in the
session lifecycle and was not changed. Since the clickable centrepieces are
multi-tile, each traversal lands exactly one tile outside the destination
footprint:

| Operated loc | Destination loc | Safe arrival | Relationship to footprint |
|---:|---:|---:|---|
| 70792 | 70793 | `(1206,6371,p0)` | west of the 4x4 cave exit |
| 70793 | 70792 | `(2988,3235,p0)` | south of the 1x3 surface entrance |
| 70794 | 70796 | `(1340,6488,p0)` | west of the rotated 2x3 opening; period-map arrival |
| 70796 | 70794 | `(1090,6360,p0)` | east of the rotated 2x3 opening |
| 70797 | 70798 | `(1090,6497,p0)` | east of the rotated 2x3 opening; period-map arrival |
| 70798 | 70797 | `(1340,6380,p0)` | west of the rotated 2x3 opening |
| 70795 op2 | 70799 | `(1206,6506,p0)` | west of the 2x2 climb-up |
| 70799 | 70795 | `(1178,6355,p0)` | west of the 1x2 slide entrance |

70795 op1 remains a non-traversing `Investigate` response and op2 performs the
slide, matching its two imported cache options; 70799 op1 performs the return
climb. `tools/test_rs2012_grotworm_routes.py` verifies every trigger, source
placement, rotated footprint, exact destination, and one-tile separation.

Across QBD, reward, Grotworm, and TD route maps the resulting contract is 20
decoded source squares, 55,187 exact static placements, 1,006 map-referenced
source loc IDs, 12 underlays, and 12 overlays, plus the single dynamic surface
placement. Every static tuple retains source plane/local coordinate/shape/angle
and changes only its loc ID through the import ledger.

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

Durations are cache frame duration totals in 20 ms units (this engine's
animation cycle — see `APP_LOGIC_TICK_MS` in `src/app.c`). An earlier revision
of this table used 30 ms units, which is not what the engine actually plays
sequences at; that mismatch is what made `^rs2012_qbd_wake_anim_ticks` fire
the wake->fight NPC swap ~14 ticks after sequence 16714 had already finished
and the queen visibly reverted to her sleeping idle pose. The wake constant is
now derived from the corrected 16.72 s figure below.

| Sequence | Meaning | Duration |
|---:|---|---:|
| 16714 | wake | 16.72 s |
| 16715 | active idle | 7.68 s |
| 16716 | sleeping idle | 7.68 s |
| 16717 | centre melee | 1.80 s |
| 16718 | ranged sweep | 2.40 s |
| 16721 | ordinary breath | 1.80 s |
| 16742 | return to sleep | 9.12 s |
| 16743 / 16744 | west/east melee | 1.80 s each |
| 16745 | extreme breath | 6.00 s |
| 16746 | wall cast | 3.00 s |
| 16747 / 16748 | cough worm / stop cough | 0.80 / 1.20 s |
| 16758 / 16759 | left/right claw | 9.84 / 2.84 s |
| 16766 / 16768 / 16771 / 16774 | restoration / activate / complete / exit | 1.80 / 1.20 / 1.20 / 0.60 s |
| 16778 / 16779 / 16782 | worm death / defence / attack | 1.80 / 1.20 / 1.80 s |
| 16786 / 16787 | worm move / turn-ambient | 2.22 / 0.96 s |
| 16859 / 16861 / 16864 | soul death / teleport / cast | 1.52 / 1.20 / 1.94 s |
| 16883 / 16884 | soul run / walk | 1.20 s each |

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
not simultaneous sounds, and **all of them are now imported**: the 226+
destination record writes an explicit frame per entry, so a frame's
alternatives are repeated entries on that frame and the client's weighted run
rolls between them. The codec used to keep only the primary and discard the
rest — see `docs/rs2012_qbd_encounter/AUDIO.md` §4.1, which is also where the
per-attack table now lives.

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
- Worm crawl 16787: 14921/15000/15014/14966/14922. Bound to nothing on the
  destination npc — see AUDIO.md §4.3.
- Soul sequences carry no embedded audio.

Revision-727 sequence opcode 18 selects recorded index-14 audio; it is not an
ordinary animation flag. The full lane carries 184 frame-sound events — 177
recorded plus seven synthesized — of which 129 are the alternatives on the 19
frames that declare a random set. Every ID resolves in the destination packs.

What the destination still cannot carry is opcodes 19 and 20: the per-sound
volume percentage and random playback-rate range, 32 and 22 records over the
encounter's sequences. Her ready breath is authored at 40% and the fire-wall
wind-up at 150%, and the bite, both dragonfires, the sweep and the wall all
carry a ±4% pitch wobble; the osrs239 frame-sound record has no field for
either, so they stay source evidence.

The bridge preserves 106 recorded samples plus the source Vorbis setup at
index 14 archive 16000. Runtime sound loading first tries native index 4, then
uses the foreign setup for the high imported sample namespace. Source samples
below 14000 — Sir Rebrum's 13 alternative footsteps and the ambiguous 6249 —
are remapped into 17000+ in both the sequences and the ledger. All 106 samples
decode to signed 16-bit PCM from the composed cache.

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
sprites 10959–10968, hidden model 70127/sequence 9390, and a foreign hook graph
through source client scripts 6236, 6238, 6240–6242, and 6245. Those hooks read
globals:

- 1923: LP lost within the current pool;
- 1924: progress states 0 start, 1 pool 1 down, 2 artefact 1, through 8 complete;
- 1925: time-stop overlay on/off.

Interface 1284 is the 47-component Dragonkin coffer. Its source ten-slot
container is 100; the isolated destination record is cache-backed inventory
2000, `rs2012_qbd_rewardinv`. It offers Take/Bank/Discard/Examine plus Bank
all, Abandon all, and Take all. Dependencies include sprites 7920–22, 8278,
8384–98, 8444–46 and client scripts 5399, 5400, 5409–11, 5415.

Both complete visual trees are now packed at destination interfaces 1284/1285:
82 components in total. Their 32 source sprite archives are allocated at
13000–13031, HUD model 70127 maps to 110657, and sequence 9390 maps to 22075.
The source `on*` hooks cannot execute in the OSRS239 dialect and are removed
deliberately, not silently ignored. Server code replaces their authoritative
encounter semantics: all dormant/active/restored artefact states, time-stop
visibility, persistent coffer transmission, and every item/button action. Two
native OSRS239 clientscripts, 13000 `rs2012_qbd_hud_pool` and 13001
`rs2012_qbd_hud_pool_fade`, reproduce source scripts 6241/6242. The first clamps
the real lost-LP value to 0–18,750, normalises it to the source script's 7,500
scale, keeps at least one percent of green bar visible, and resizes the
271-pixel bar in one-percent steps. It places a red strip over precisely the
width lost since the previous update. The second adds 11 transparency per
timer cycle and hides/detaches that strip at 243, matching the recovered source
fade. The source full-screen `noclickthrough` sentinel remains hidden so it
cannot consume arena clicks; the rootless status and time components still
render.

The source time-stop hook used a model-backed/fading presentation whose global
carrier does not exist in OSRS239. The port currently shows and hides the
authentic overlay component with a server-coloured green fill. Action lock,
incoming damage, and one-hit release are exact server mechanics, but the
source-client model/fade animation and foreign hover-only cosmetics remain
manual visual follow-up rather than claimed fidelity.

Music client IDs are 1119 `Awoken` and 1118 `Queen Black Dragon`, established
through source script maps 1345/1351 and open727's explicit controller calls.
Both packed tracks use source patch 1157 and the foreign index-14 setup; the
closure contains 106 recorded samples in total. Entry sends 1119, the first
artefact restoration changes to 1118, and shared leave/death/logout cleanup
sends `midi_song(-1)` so the boss track cannot leak into an unmapped exterior
region. On the revision-239 wire, each positive scripted `midi_song` is carried
in the client's V2 envelope `(0,60,60,0)`; that is destination transport
behaviour, not a recovered 2012 encounter-timing claim. Numeric IDs in source
index 14 are a different namespace and must not be confused with music-track
IDs.

## 6. QBD reward contract

### 6.1 Coffer behaviour

The mature 29 June revision is the strongest complete pre-7-August table. A
successful completion gives always-drops plus two configurable resource rolls,
unique rolls, and an eligible journal. The port never rolls the generic rare
drop table and never adds charms in the strict release profile (B).

Rewards are rolled exactly once into a private, persistent ten-slot
`rs2012_qbd_rewardinv`. A full backpack does not destroy a kill, and logout or
leaving the room does not reroll it. Re-entry routes the player directly to the
unclaimed coffer. The ported interface supports per-entry Take, Bank, Discard
with confirmation, and Examine, plus Take all, Bank all, Abandon all with
confirmation, and Close. The coffer changes to its empty loc only after the
last entry is transferred or deliberately discarded.

The same inventory definition is carried in the sparse client-cache lane as
well as the server allocation. mock230 therefore creates the ten-slot custom
container on first use, transmits it to the authentic coffer component, and
persists its owned contents through save/logout; it is not a script-only name
that disappears when the composed cache boots.

The coffer is a compact reward ledger, so one source cell may legally display
`dragon bones x5` even though bones are not stackable in a backpack. Both
per-slot Take and Take all use one transfer primitive: backpack capacity is
preflighted for five spaces and the claim materialises five separate one-bone
cells, while banks and genuinely stackable items keep their full-count move.
The runtime regression constructs this exact five-bone case and restores the
original backpack/coffer state after proving the result.

Source loc 70815 chooses its closed/open child through a revision-727 varbit
whose numeric ID means an unrelated quest state in OSRS239. On every unclaimed
open the server replaces that controller with explicit closed loc 70816; after
the final take/discard it changes only to explicit open/empty loc 70817. Modern
account state can therefore neither hide nor pre-open this coffer.

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

Journals are sequential and one-time: first guaranteed, then the second at
1/10, third at 1/25, and fourth at 1/40 in the executable reconstruction. The
period pages establish only common/uncommon/rare labels for those later books,
so the numeric chances remain configuration rather than claimed retail source.
The port stores journal progression separately from inventory so destroying a
physical book cannot reset unlock state.

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

The isolated cache lane also imports the six source note partners 14473,
14475, 14477, 14485, 15273, and 20269. They link back to the imported ruined
armour parts, Dragon claws, rocktail, and infernal ashes rather than resolving
their source numbers to unrelated OSRS239 placeholder items. A packed-cache
round-trip and the reward-item regression cover every pair.

Royal armour crafting is 87 Crafting for vambraces (1 leather, 94 XP), 89 for
chaps (2, 188 XP), and 93 for body (3, 282 XP); it requires 80 Ranged and 40
Defence to wear the body, while vambraces/chaps require 80 Ranged (B). Both
standard tanners accept Royal hide: Ellis charges the period 20 coins and
Sbott retains his existing 45-coin dragonhide premium. The ordinary
needle/leather menu produces all three pieces with those exact levels,
quantities, and XP.
Royal crossbow/bolts require 85 Ranged, the Dragon kiteshield and converted
dragon armour retain Defence 60, and converted Infinity pieces retain Defence
25/Magic 50.

The dragonbone kit has an Info action and converts exactly five Infinity plus
six dragon items. Every converted item has a reversible Split action that
returns the original and kit without loss. All four journals have readable
period-adapted transcripts and confirmed Destroy actions; unlock state remains
permanent, and any missing unlocked journal can be reclaimed from all three POH
bookcase tiers without duplicating a copy already in backpack, bank, or an
unclaimed coffer.

### 6.3 Royal crossbow lifecycle

- 70 Smithing to assemble/forge; 85 Ranged to wield (A/B).
- Song from the Depths itself is not recreated. A permanent completion handoff
  gates imported Sir Rebrum (source NPC 15460, destination 25009) at his
  authentic encampment spawn `(2991,3237)`. He supplies the coral crossbow only
  after completion, only with free inventory space, and only when no coral,
  unforged, complete, or degraded form exists in backpack, bank, or equipment.
- Thurgo combines the coral crossbow and four tradeable QBD components into the
  unforged crossbow.
- During phase 4 the player chooses `Brandish`; an extreme-fire pulse turns the
  unforged weapon into the bound, untradeable Royal crossbow.
- It fires only Royal bolts.
- Ten hours of combat degrade it. mock230 records 60,000 effective attack ticks
  and subtracts cadence five on Accurate/Longrange or four on Rapid, so Rapid
  does not accidentally degrade the weapon after eight hours twenty minutes.
- Re-brandishing the degraded crossbow in extreme fire repairs it. Thurgo also
  consumes another complete four-component set to repair a degraded crossbow;
  both historical paths are implemented.
- The 5 October 2012 revision records three equal hits for a successful shot:
  the first immediately, then two more about eight and sixteen seconds later.
  Each hit rolls from 70–100% of a cap initially equal to 20% of the ordinary
  maximum; after nine attacks at the same living target inside the bleed window
  that cap becomes 25%. Trains stack. Each delayed splat resolves the target's
  live QBD/TD protection and LP/XP domains exactly once at landing, retains the
  launch combat style for XP, and terminates if the target is gone. These are B
  community reverse-engineering results, not an official engine disclosure.

## 7. Tormented demons

### 7.1 Access and encounter layout

Tormented demons are unlocked only after While Guthix Sleeps. The complete WGS
quest and sapphire-lantern puzzle are outside this encounter slice; production
entry checks permanent server flag `%rs2012_wgs_complete` (varp 6245). Source
loc 48248 is placed at `(2526,5828,p2)`, morphs through source varbit 7190, and
its post-puzzle child 40260 is the unambiguous `Cave opening` / `Climb-through`
route bound by the port. Debug entry bypasses the gate without setting quest
progress. Puzzle tunnel locs 40273–40275 are intentionally not rebound.

Five authentic temple squares are imported. No cache evidence was found for
`41_89`, so it is not fabricated:

| Square | Region / world range | m/l archives | XTEA | Placements |
|---|---|---|---|---:|
| `40_89` | 10329; x 2560–2623, z 5696–5759 | 4159 / 4160 | `[769370878,-1231940040,-395526277,-1563597554]` | 4,194 |
| `39_89` | 10073; x 2496–2559, z 5696–5759 | 2648 / 2649 | `[1714440207,1297605064,683265918,1501779755]` | 4,206 |
| `39_90` | 10074; x 2496–2559, z 5760–5823 | 2318 / 2319 | `[274761362,1679929293,-903581645,-999712194]` | 4,279 |
| `39_91` | 10075; x 2496–2559, z 5824–5887 | 4141 / 4142 | `[1551484478,1577386038,-378705686,547571118]` | 3,530 |
| `40_90` | 10330; x 2560–2623, z 5760–5823 | 1167 / 1168 | `[-907752098,1554817455,-41175839,-2043434626]` | 4,308 |

The private fight is an identity copy of `40_89`, plane 0, entered at local
`(22,30)`. Its six demons are the source positions `(2599,5736)`,
`(2609,5729)`, `(2607,5717)`, `(2592,5723)`, `(2588,5728)`, and
`(2576,5732)`, or local `(39,40)`, `(49,33)`, `(47,21)`, `(32,27)`,
`(28,32)`, and `(16,36)`. The route-boundary spawn `(2560,5742)` is not
silently counted as a seventh arena demon.

The six-demon encounter also owns a dedicated instance handle and a one-tick
departure watchdog. Source loc 40260 is installed as the normal interior exit
at local `(23,29)` and returns the player to `(2527,5827,p2)`. An arbitrary
teleport, logout, or that exit removes exactly the six owned demons and frees
the slot without moving an already-departed player. Death stops combat and
despawns the demons immediately, retains the reservation through the corpse
delay, then releases it; the grave/bones coordinate is redirected outside the
reusable template first. Re-entry therefore receives a clean slot instead of
stale demons from a prior occupant.

### 7.2 Definition and combat state

- NPC variants: 8349 melee prayer, 8350 magic prayer, 8351 ranged prayer (C).
- Combat level 450, 3,260 LP, size 3, attack rate six ticks (B/C).
- Later Jagex recovery gives the original base levels as Attack 255, Strength
  166, Defence 300, Magic 255, and Ranged 255. These are configured and tested,
  but labelled later primary evidence rather than a value published in 2012.
- The separate open727 bonus rows are offence 450/0/0/450/450 and defence
  200/350/350/200/250; they are not confused with base levels.
- Maximum melee 189 LP; Magic/Ranged and rage splash approximately 269–270 LP
  (B). At the OSRS boundary these are 19 and 27 hitpoints.
- Poison immune historically (B). mock230 currently has no player-to-NPC poison
  path, so this needs no encounter-specific suppression hook yet.
- Player protection prayers fully negate the matching demon attack in this
  pre-EoC profile (B).
- Demon attacks are distance-aware. A selected melee style is not silently
  wasted while the target is out of reach; the controller selects a usable
  attack and retains the historical style cadence.

### 7.3 Independent attack-style timer

The release encounter changes offensive style on an approximately 16-second
timer, independent of its six-tick ordinary attacks (B). It is not a block of
exactly five attacks. The implementation uses a 27-game-tick timer. Its Rage
cast uses cache/open727-supported sequence 10918 and the Magic cast/projectile;
10642 is retained in the cache lane but has no verified semantic label and is
not assigned. Sequence 10924 is likewise imported but unassigned.

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

| Asset | Source → destination | Role |
|---|---|---|
| NPCs | 8349/8350/8351 → 25006/25007/25008 | Protect Melee/Magic/Ranged forms |
| Model | 44733 → 110006 | shared TD body |
| BAS | 910 flattened | ready 10921→22017; walk 10920→22016 |
| Core sequences | 10917–10924 → 22013–22020 | death, Magic/Rage, Ranged, walk, ready, melee, defend, unassigned |
| Extra sequence | 10642 → 22012 | imported, semantic role unverified |
| Rig | frame archive 2682→22002; framemap 2401→9002 | core animation data |
| Spot animations | 1883–1888 → 10025–10030 | Magic cast/projectile/impact, melee impact, Ranged projectile/cast |
| Prayer icons | legacy indices 0/2/1 → sprite archive 440 indices 0/2/1 | visible current protection |

Spot models are 3082→110104, 44666→110105, 44637→110106,
44629→110107, and 44621→110108. Ambient sound 5602 maps to synth 16000;
sequence sounds 5609, 5562, 3835, 5622, and 5606 map to 16001–16005.
Opcode-134 idle/crawl/walk/run ambience uses 16000 at radius 15. BAS 910 is
flattened because OSRS239 has no compatible foreign BAS record, not because its
movement information was discarded.

### 7.7 TD drops

Guaranteed drop is imported infernal ashes 20268 x1. The 23 October charm log
records exact observed populations over 1,587 kills, each successful outcome a
stack of three: gold 225, green 143, crimson 254, blue 651, and none 314. The
implementation uses those observed counts directly while not claiming they are
recovered engine probabilities.

Independent tertiary rolls are Dragon claws 1/299 (contemporary Adventurer's
Log estimate), a ruined-armour aggregate 1/384 followed by equal lump/slice/
shard selection (1/1,152 each, the explicit midpoint of the observed period
range), and a hard clue at an inferred 1/128. The clue uses a real packed hard
clue item, but its reader/step system remains a wider-content dependency.

The archive gives quantities and qualitative rarity for ordinary drops, not
denominators. The executable table is therefore a visible 128-slot
reconstruction with one ordinary roll per kill. It includes the period weapons
and rune armour; super defence, prayer potion and sharks; the published seed
family; assorted herbs x8–11; diamonds x3–5; adamant bars x3–7; death/lava/law/
blood runes; fire talismans; coins x3,000–15,000; and the functional local
random-jewel slot. Spin tickets, ancient effigies, elite clues and court
summons are documentation-only because their wider systems/assets are absent.
Dragon limbs are excluded because they arrived with EoC on 20 November 2012.
No current OSRS synapse or burning-claw rewards are reused.

### 7.8 Imported Dragon claws

Source object 14484 is imported as destination 45010 / `rs2012_obj_14484`,
separate from modern OSRS `dragon_claws`. Its inventory/male/female models are
44590/43660/43651 → 110019/110020/110021. The special uses sequence
10961→22072, spot 1950→10031, spot model 44811→110458, dependent sequence
10965→22073, and synths 4138/4140/4141→16026/16027/16028. Server projection
retains 60 Attack, +56 Strength, 50% special energy and doubled slash accuracy.

Slice and Dice implements the five period branches, with `H` the successful
base roll:

| First successful roll | Four hits |
|---|---|
| first | `H, H/2, H/4, H/2−H/4` |
| second | `0, H, H/2, H−H/2` |
| third | `0, 0, H, H`, with H capped to 75% ordinary maximum |
| fourth | `0, 0, 0, 1.5H` |
| none | `0, 0, 0, 0–7 LP` historical tail, scaled to the smallest visible mock hit while preserving zero |

The hits are presented as two pairs. Every splat passes through the shared hit
preparation hook exactly once, so TD prayer counters/shield, QBD caps/forms,
target HP ceilings, and XP-domain scaling remain correct for the special.

## 8. Cache-port architecture

### 8.1 Isolated lane and allocation

Manifest: `ports/rs2012_qbd_td.ini`.

Lane: `OSRS-Content/osrs239-content/ported/rs2012_qbd_td` plus matching
`models/`, `animsets/`, `framemaps/`, `synth/`, `samples/`, `patches/`, and
`songs/` ported subtrees.

#### Revision-239 NPC_INFO high-definition rule

The per-client NPC index is 16 bits; it is separate from the definition
type. An initial NPC add carries only a 14-bit type field. Therefore the
25,000–25,009 QBD/TD allocation is valid only when the add's update flag is set
and the same `NPC_INFO` packet includes update-mask bit `0x1`. That block
replaces the type with a transformed unsigned 16-bit `p2Alt3` / `UShortLEAdd`
value. This transformation path—not a 16-bit add type—carries high definition
IDs, so the 14-bit add field is not a hard global definition-ID cap.

Allocation bases:

| Namespace | Destination allocation |
|---|---:|
| NPC | 25,000 |
| obj | 45,000 |
| loc | 63,000 |
| spotanim | 10,000 |
| model | 110,000 |
| sequence / animset | 22,000 |
| framemap | 9,000 |
| synth | 16,000 |
| recorded-sound setup | 16,000 |
| recorded-sound collision spill | 17,000+ |
| music track / patch | fixed source IDs 1,118–1,119 / 1,157 |
| interface | fixed 1,284–1,285 |
| native clientscript | 13,000–13,001 |
| material texture | 211–466 |
| material sprite | 8,535–8,790 |
| QBD UI sprite | 13,000–13,031 |
| QBD reward inventory | 2,000 |

The loc base was deliberately moved from 60,000 to 63,000 after validation
found live OSRS239 locs through 62,200. The collision had also made coffer
multiloc references resolve to unrelated spiral-stair symbols; reallocation
corrected them to `rs2012_loc_70816/70817`.

The definitive append-stable import ledger contains **one inventory, 10 NPCs,
69 objects, 1,074 locations, 32 spot animations, 660 models, 81 sequences, 31
frame archives, 29 framemaps, and 29 index-4 synths**. Client closure adds two
interfaces, two native clientscripts, 256 baked materials, and 288 total
sprites: 256 material sprites at 8535–8790 plus 32 UI sprites at 13000–13031.
Interfaces 1284/1285 remain at their source IDs; material texture IDs occupy
211–466. Sprite 13000 and clientscript 13000 do not collide because cache
indices are separate namespaces. These are final recursive totals, not the
earlier seed set.

Audio closure adds 29 index-4 synth archives; 106 index-14 recorded samples
plus the foreign setup; music tracks 1118/1119; and patch 1157 — 139 archives.
The index-14 source/destination ledger is append-stable and records every
sub-14000 collision remap into 17000+ explicitly.

### 8.2 Codec work required by revision 727

- Profile-specific spotanim grammar with BigSmart model/sequence references.
- Profile-specific late-RS2 sequence grammar, including nested opcode-13 sound
  alternatives and opcodes 15/16/18/19/20/249.
- Opcode 18's recorded-audio selector, foreign index-14 Vorbis setup/sample
  closure, packed music-song/patch/sample codecs, and a setup-selectable audio
  probe; treating those IDs as index-4 synths produced silence or false assets.
- BAS type/codec for movement and render-animation dependencies.
- Late-RS2 loc codec with BigSmart model/sequence/transform references and
  opcode-42 palettes; 73,893/73,893 exact-consumption sweep.
- RS727 map terrain uses one-byte overlay/attribute fields even though revision
  727 is numerically above OSRS209. Width is selected by cache branch/profile,
  not revision number.
- V2 frame files resolve their embedded framemap ID rather than assuming the
  archive high bits identify the rig.
- RS2 interface model/sequence/font operands use the late BigSmart branch when
  `(interface_id > 1144)`; using the OSRS operand width corrupted the QBD HUD
  model and coffer font dependencies.
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
- hook-scoped `walkstep_coord` plus per-candidate walk-trigger evaluation, so
  content can veto either tile of a running route without global collision;
- hit-preparation hook returning both effective NPC-pool damage and XP damage.

Compiler metadata, wire protocol, NPC-state isolation/respawn, action-lock,
projectile, walk-step, and mock-server selftests cover these paths. QBD's
script-created adds retire themselves on death; TD respawns deliberately remain
live and their `[ai_spawn]` initializer resets prayer counters, shield clock,
offensive style, and independent 27-tick timer for every new life.

### 8.4 Known media conversion boundary

Revision 727 materials are procedural: index 26 provides the texture-definition
table and index 9 contains property graphs, which can recursively sample index-8
sprites or other materials. OSRS239 expects sprite-backed texture records.

The imported OB3 models preserve every texture ID and mapping array—QBD models
70260/70267/70268 have 233/233/241 textured faces, TD model 44733 has 39, and
crossbow 70257 has 47. The completed material bridge contains 256 rows: 235
direct face materials and 22 retexture materials (overlapping sets), plus 36
transitive programs and six source sprites. It successfully bakes and remaps all
660 models. This is a deterministic OSRS-compatible approximation, not an HD
shader claim: each graph becomes a 128x128, 6x7x6-palette sprite with alpha
thresholded at 128. Of the rows, 204 are marked `isGroundMesh`, 25 animate, five
animate both axes, and 126 contain transparency; repeat/clamp, mipmap, shader,
float and unverified program-tail differences still require source-client
visual QA.

The historical cache-decoder field name `valid` is misleading. The supplied
727 `ImageIndexLoader` decodes the same byte as
`isGroundMesh = readUnsignedByte() == 0`; it is not an `isSd` or global
HD-only flag. The 727 mesh renderer conditionally removes those selectors when
its model-render flags include `0x40`. OSRS239 has no equivalent combination of
procedural material and model flag, so the lane's OB3 compatibility fallback
clears texture ID, UV coordinate, and textured-face-info state together while
retaining the original face HSL for normal lighting. It applies that fallback
to 274,715 faces across the 660-model closure and retains all 256 baked assets
for inspection or a future fuller renderer. Clearing only the texture ID was
the concrete cause of the mouth-only QBD intermediate render.

QBD recorded audio is bridged rather than copied into the wrong cache table.
The foreign setup remains at index 14 archive 16000, all 106 samples retain
exact source bytes (apart from the ledger-level relocations of sub-14000 ids
into 17000+), and runtime converts them through the exact decoder/16-bit PCM
path. All 184 sequence events and all 103 loc-audio references resolve to one
of the 29 index-4 synths or 107 index-14 archives. Tracks 1118/1119 and patch
1157 are also packed and decoded. The remaining boundaries are perceptual —
no live SDL listening A/B against the 2012 client was performed — and the
per-sound volume/rate modifiers of opcodes 19/20, which the OSRS239 sequence
record has no field for.

The original 1284/1285 visual trees are structurally complete and packed: 82
components, 32 UI sprites, model 70127→110657 and sequence 9390→22075. Foreign
hooks/client scripts 5399/5400/5409–11/5415 and 6236/6238/6240–42/6245 are
intentionally not executed. Server IF writes, the generic OSRS239 inventory
client, and native pool-bar scripts 13000/13001 replace the required behavior
while server state remains authoritative. The structurally present source
time-stop model/fade and hover-only effects are not represented by those two
native scripts; the implemented overlay is the explicit server-driven fallback
described in §5.6.

## 9. Implementation file index

QBD:

- `OSRS-Content/osrs239-content/server/scripts/minigames/minigame_rs2012_qbd/configs/rs2012_grotworm_route.constant`
- `OSRS-Content/osrs239-content/server/scripts/minigames/minigame_rs2012_qbd/configs/rs2012_qbd.constant`
- `.../rs2012_qbd.npc`, `rs2012_qbd.obj`, `rs2012_qbd.varp`, `rs2012_qbd.dbrow`, `rs2012_qbd.inv`
- `.../scripts/rs2012_qbd_session.rs2`
- `.../scripts/rs2012_qbd_combat.rs2`
- `.../scripts/rs2012_qbd_adds.rs2`
- `.../scripts/rs2012_qbd_rewards.rs2`
- `.../scripts/rs2012_qbd_reward_items.rs2`
- `.../scripts/rs2012_qbd_ui.rs2`
- `.../scripts/rs2012_qbd_selftest.rs2`
- `.../scripts/rs2012_royal_crossbow.rs2`
- `.../scripts/rs2012_grotworm_route.rs2`
- `OSRS-Content/osrs239-content/ported/rs2012_qbd_td/interfaces/rs2012_qbd_hud.if`
- `.../interfaces/rs2012_qbd_coffer.if` and their `.compack` files
- `.../scripts/rs2012_qbd_hud_pool.cs2`
- `.../scripts/rs2012_qbd_hud_pool_fade.cs2`
- `.../pack/3_interfaces.pack`, `8_sprites.pack`, and `12_clientscripts.pack`
- `.../pack/6_musictracks.pack`, `14_musicsamples.pack`, and `15_musicpatches.pack`
- `.../configs/rs2012.inv` and `.../pack/inv.alloc` / `inv.client`

TD:

- `OSRS-Content/osrs239-content/server/scripts/areas/area_rs2012_tormented_demons/configs/`
- `.../scripts/rs2012_td_encounter.rs2`
- `.../scripts/rs2012_td_combat.rs2`
- `.../scripts/rs2012_td_player_hit.rs2`
- `.../scripts/rs2012_td_drops.rs2`
- `.../scripts/rs2012_td_selftest.rs2`
- `.../configs/rs2012_dragon_claws.obj`
- `.../scripts/rs2012_dragon_claws.rs2`

Map/cache port:

- `3rd/rscache/tools/map_port/main.c`
- `ports/rs2012_qbd_td.ini`
- `OSRS-Content/osrs239-content/ported/rs2012_qbd_td/maps/`
- `OSRS-Content/osrs239-content/ported/rs2012_qbd_td/BASE_PLACEMENTS.tsv`
- `tools/stage_rs2012_overlay.py`
- `tools/test_rs2012_map_port.py`
- `tools/test_rs2012_grotworm_routes.py`
- `tools/test_stage_rs2012_overlay.py`
- `tools/port_rs2012_qbd_ui.py`
- `tools/test_rs2012_qbd_ui_port.py`
- `tools/test_rs2012_qbd_combat_contract.py`
- `tools/test_rs2012_qbd_lifecycle.py`
- `tools/test_rs2012_qbd_reward_items.py`
- `tools/test_rs2012_td_lifecycle.py`
- `tools/test_rs2012_audio_bridge.py`
- `tools/test_rs2012_server_overlay.py`
- `3rd/rscache/tools/audioprobe/main.c`
- `src/engine/proctex/test/rs2012_material_bake.c`
- `OSRS-Content/osrs239-content/port/rs2012_qbd_td.materials.tsv`
- `OSRS-Content/osrs239-content/ported/rs2012_qbd_td/PROVENANCE.md`
- `manifest_osrs239_rs2012.ini`, `manifest_osrs239_rs2012_td.ini`, and the
  `mock230-cache-rs2012` make target

Global integration is limited to the shared player-hit preparation point,
ranged Royal-bolt/crossbow validation, QBD/TD death, logout, and arbitrary
departure cleanup, and Thurgo's existing conversation entry, plus the
dedicated Dragon-claws special dispatcher. Ordinary and reachable special
attacks all enter the shared hit hook once. All named encounter content remains
in the `rs2012` namespace.

## 10. Build, staging, and verification

The feature lane is included as both a compiler pack and component root for
`make -C src mock230-scripts`. The cache half is baked through a disposable
staged overlay, not by flattening foreign configs into the base OSRS239 source
tree. The source-to-destination ledger is checked before every apply so adding a
dependency cannot silently renumber an existing symbol.

### 10.1 Automated checks completed

The following are completed machine checks, not a list of aspirations:

- `cachepack import` and the append-stable ledgers resolve every recursive
  NPC/object/location/spotanim/model/sequence/frame/framemap/synth dependency
  without an unknown key, source-ID leak, or allocation overlap with base
  OSRS239 or the separate Summoning lane.
- The material bridge decodes, rewrites, encodes, and decodes all 660 destination
  models. It preserves mapping arrays for ordinary materials, applies the
  complete OB3 HSL fallback to 274,715 `isGroundMesh` face selectors, and
  resolves all 256 material rows. This is a structural/codec assertion, not
  visual proof of a procedural-shader match.
- `tools/test_rs2012_map_port.py` proves 20 decoded source squares, 55,187 exact
  source placement tuples, 1,006 map-referenced loc configs, 12 underlays, and
  12 overlays; the surface placement is audited separately because it was
  server-dynamic in revision 727.
- `tools/test_stage_rs2012_overlay.py` proves hermetic staging, including
  retention of base map members 2–4 and unchanged source-tree hashes.
- `tools/test_rs2012_grotworm_routes.py` proves all four bidirectional cave
  links, nine option triggers (including the non-traversing `Investigate`), and
  eight arrivals exactly one tile outside the rotated destination footprints.
- `tools/test_rs2012_qbd_ui_port.py` proves interfaces 1284/1285, all 82 named
  components, all 32 UI sprite groups, the 70127→110657 model and
  9390→22075 sequence references, clientscripts 13000/13001, the hidden
  click-through sentinel, and the destructive coffer-button bindings.
- `tools/test_rs2012_qbd_combat_contract.py` proves the 600-tick antifire
  lifecycle, sourced QBD defence rows and typed hit routing, historical
  650-LP/melee-and-Magic Giant Worm, soul/worm ten-to-one LP and XP domains,
  their exclusion from Queen-only phase rules, one-life post-kill cleanup, and
  outgoing maxima. The host fixture kills a 500-LP time-stop caster and a
  650-LP worm through the shared player-hit queues.
- `tools/test_rs2012_qbd_lifecycle.py` plus the host fixture prove gated and
  manifest-only entry, per-step departure detection in arena and reward room,
  complete NPC/queue/UI/music/lock teardown, unclaimed-coffer preservation,
  clean re-entry, slot reuse, death, and logout.
- `tools/test_rs2012_qbd_reward_items.py` proves all 11 reversible kit maps,
  Royal tanning at both Ellis and Sbott, exact leather levels/quantities/XP,
  four journals and reclaim guards, equipment gates, kiteshield, bolts, and
  all six imported note-link pairs. Its host-backed claim test also proves that
  a compact five-bone reward becomes five legal one-item backpack cells through
  both claim paths without losing or duplicating container state.
- `tools/test_rs2012_td_lifecycle.py` plus the host fixture prove the authentic
  interior exit, arbitrary-teleport and logout cleanup, death reservation and
  safe grave placement, six-NPC teardown, clean re-entry, and allocator reuse.
- `tools/test_rs2012_server_overlay.py` proves imported allocation bands are
  accepted by the server pack and that a colliding imported band fails hard.
- `tools/test_rs2012_audio_bridge.py` proves the source and staged closure,
  fixed payload hashes, all 55 sequence events, 103 loc references, the
  6249→17000 remap, and 1119→1118→stop dispatch. `cachepack verify` compares
  synth/song/sample/patch bytes after composition; `audioprobe` decoded all
  83/83 foreign samples, both songs, and patch 1157 from the result.
- The final sparse overlay contains **1,593 physical files**. `cachepack pack`
  accepts **1,291 config records and 1,150 asset archives**: one ten-slot
  inventory, 31 animation-frame archives, 29 framemaps, two interfaces, two
  native clientscripts, 29 synths, two songs, 84 index-14 sample/setup
  archives, one patch, 21 maps, 660 models, 288 sprites, and one merged texture
  archive. The pack has zero failed configs, unknown keys, unresolved names,
  or indexed-missing assets; its inventory group round-trips 1,027/1,027
  records exactly.
- The five floor overlays whose material operand is restricted to `u8` resolve
  to permanently reserved destinations 211–215. Model-only materials may
  safely occupy the remainder through 466.
- `make -C src mock230-scripts` completes with **12,963 compiled scripts**;
  `make -C src mock230` and `make -C src test-ss-meta` pass. The server-only
  pack writes **8,340 records with zero unresolved names**.
- Both ordinary- and composed-cache host runs load **12,899 runtime scripts**.
  Their QBD and TD lifecycle/combat blocks, the exhaustive equipment gate, and
  the live composed coffer assertions pass; the latter also has zero
  `unknown container 2000`, post-kill Slayer-hook, or VM-abort diagnostics.
  After the final add-death and patrol-fixture corrections, the complete
  composed-cache host suite reports `mock230 selftest: all checks passed`.

These checks establish reproducibility, dependency closure, map tuple fidelity,
UI structure, script compilation, and the new engine primitives. They do not
turn a compiled encounter into a visually and aurally certified retail-client
capture.

### 10.2 Compiled encounter diagnostics

Two deterministic debug procedures are included and compile with the content
tree, but were not executed through a connected game client during this work:

- `::rs2012qbdtest` allocates the arena and performs 75 checks covering the
  four 18,750-LP pools, 75,000 total LP, 1/2/3/3 walls, 0/1/2/4 phase soul
  targets, launch-versus-7-August cooldown switches, time-stop durations,
  1,000-LP cap, phase-3 form changes and hit cap, intermission invulnerability,
  Royal-crossbow cadence/damage bands/three-splat domain, Sir Rebrum's quest and
  duplicate gates, all exact platform masks and phase unions, terminal queue
  cleanup, ten coffer slots, and persistence of an unclaimed reward through
  encounter cleanup.
- `::rs2012tdtest` allocates the temple and performs 64 checks covering all six
  spawns and independent state, original combat stats, distance-aware style
  transitions, 310-LP pre-shield prayer accounting including misses, the 75%
  shield and Darklight deadline, WGS-debug-gate isolation, the exact observed
  charm populations, imported claw equipment/special parameters, and every
  deterministic Slice and Dice branch.

Their existence and successful compilation are recorded separately from an
actual `OK` result. A release operator should run both commands on a staged
debug account before deployment.

### 10.3 Live-client acceptance still required

A first destination-client visual pass is complete and recorded in
`docs/rs2012_qbd_arena/README.md`. It proves the same-packet NPC replacement
installs type 25003 and the arena/platform/claw scene loads. A 1200×800 live
capture shows the complete QBD head and both independent foreclaw location
models simultaneously after the OB3 texture/UV/face-info fallback repair. This
proves render presence and geometry, not pixel-identical 727 procedural
shading. Source TD
8349/10921 and destination TD 25006/22017 also produce byte-identical four-yaw
renders. The dylib-backed macOS ASan build exposed and verified the fix for a
real renderer overflow: sleeping QBD has 6,223 vertices/9,012 faces and exceeded
the former 4,096 projection buffers and 2,000-face priority stride. Full-scene
limits are now 8,192 vertices, 16,384 faces, with a 16,384 priority stride and a
pre-projection bounds check. Captures are kept under
`docs/rs2012_qbd_arena/images/`.

The following is the remaining explicit manual/integration plan. These bullets
are not claimed complete merely because the scripts compile or a static frame
renders.

QBD run-through:

- drain exactly four pools without ordinary NPC death; verify the 1,000-LP cap,
  intermission invulnerability, worm persistence, artefact order, final add
  clear, completion stairs, death cleanup, exterior grave, and logout/re-entry;
- exercise 1/2/3/3 wall waves and all three safe columns under walking and
  running collision, then intercept shadows with souls and worms;
- exercise both phase-3 armour forms, siphon, a cancelled time stop and a
  completed time stop, including rejected input and accumulated one-hit damage;
- compare centre/corner extreme fire and both forge and repair paths for the
  brandished Royal crossbow;
- inspect the 35-component HUD in each phase: green proportional pool bar, red
  recent-damage strip and fade, every artefact state, and server-driven green
  time overlay;
- inspect the 47-component coffer and test per-slot Take/Bank/Discard/Examine,
  Take all, Bank all, confirmed Abandon all, Close, full inventory/bank cases,
  empty-loc transition, persistence across logout, and sequential journals;
- run both strict-launch and 7-August profiles and confirm that only the
  documented fire/soul/siphon scheduling switches differ;
- statistically sample resource/unique/journal rewards. There is no seeded RNG
  reward-distribution test in the current debug procedure.

Tormented-demon run-through:

- fight all six spawns long enough to verify per-NPC timer/style/prayer/shield
  isolation, full player-prayer protection, distance-aware melee selection,
  Rage collision, and Darklight disable/refresh/block behavior;
- inspect all three prayer-headicon forms and every ready/walk/attack/defend/
  death animation, projectile, impact, and movement sound in the packed client;
- exercise all Dragon-claw success/failure branches in combat, checking the two
  displayed hit pairs, 50% energy, doubled slash accuracy, shared hit-hook
  accounting, target-HP ceilings, and XP;
- sample ordinary, charm, claw, ruined-armour, and clue rolls; confirm no dragon
  limbs, modern synapses, or burning claws appear; then verify death, logout,
  route, and instance ownership cleanup.

Media/world pass:

- compare default/crystal/hardened QBD, soul, worm, all three TD forms, claws,
  coffer, Royal crossbow, maps, animated textures, alpha edges, and interfaces
  side-by-side with a revision-727 source client in equivalent camera/light
  conditions;
- walk every imported map seam and verify clipping, roofs, height transitions,
  dynamic platform stages, portal footprints, projectile heights, and the
  reconstructed surface-entrance angle;
- listen to every synthesized/recorded family and both music transitions against
  the source client. Payload identity, codec decode, PCM transport and dispatch
  are automated; mix, loudness, spatial falloff and perceptual A/B are not.

## 11. Uncertainties and explicit boundaries

### 11.1 Historical values not recoverable exactly

- No official source establishes exact 29 May QBD unique denominators. The
  configured later-Jagex rates are a labelled server reconstruction, and the
  ordinary-resource weighting remains the documented period-table
  interpretation.
- Song from the Depths certainly reduced incoming QBD damage, but the inspected
  sources do not establish a percentage. The gate does not invent one; this
  benefit remains unimplemented pending defensible evidence.
- The exact launch soul-shadow player formula and siphon heal formula are not
  recoverable from the inspected snapshots. Their ranges/amounts are exposed as
  constants rather than disguised as source truth.
- Three extreme-fire pulses are strong cache/later corroboration, not explicit
  in the June strategy text. QBD armour duration/modifiers, some projectile
  delays, and the Royal-crossbow delayed-hit edge cases are reconstructions.
- Contemporary TD sources establish timed switching, Rage, and the qualitative
  ordinary-drop rarities, but not a retail Rage radius or most denominators.
  Radius 2, the 128-slot ordinary table, the 1/128 hard-clue roll, and the
  1/1,152-per-piece ruined-armour midpoint remain named corrections points.
- The 2012 TD page says the generic rare-drop table is reachable but does not
  disclose its access denominator. The executable 128-slot reconstruction has
  one explicit local random-jewel rung; it does not claim the page's entire
  generic table or ring-of-wealth rate was recovered.
- Extra IDs in an RS727 sequence opcode-13 sound set are random alternatives.
  OSRS239 has no equivalent selector in this lane, so the destination sequence
  retains only the primary event; alternatives remain recorded in §5.4 rather
  than being played simultaneously or silently relabelled as imported assets.

### 11.2 Deliberate implementation limits

- While Guthix Sleeps progression, its sapphire-lantern/light-creature puzzle,
  and quest dialogue are not recreated. Production entry consumes the explicit
  permanent completion flag and binds only the post-puzzle cave opening; the
  debug bypass never grants quest completion.
- The six-demon room is currently a one-player identity instance for safe
  ownership and NPC-local state. Retail Ancient Guthix Temple was a shared
  world space, so multiplayer aggression, competition, and loot ownership are
  not reproduced.
- mock230 has no player-to-NPC poison route, so the demon's poison immunity has
  no active suppression hook. Period interactions with Verac's set effect,
  holy water, dwarf cannons, combat familiars, and dreadnips have not received
  encounter-specific compatibility or live QA.
- The hard-clue tertiary roll produces a real packed hard-clue item, but the
  clue reader/step/reward progression is a wider-content dependency and is not
  supplied here. Likewise, royal armour assets and requirements are present,
  while a full pre-EoC Crafting-interface recreation is outside this slice.
- Materials use a deterministic 128x128 palette/alpha bridge, while selectors
  marked `isGroundMesh` use the documented OB3 lit-HSL compatibility fallback.
  This is not the full RS727 procedural/model-flag renderer. Animated axes,
  alpha, mipmaps, shader parameters, repeat/clamp behavior, and unverified
  program tails require further side-by-side visual QA.
- All primary QBD sequence events, loc ambience references, and music payloads
  are bridged and decode from the composed cache. Random alternative IDs beyond
  the primary per-frame event are not representable in the destination sequence
  format, and a live listening comparison remains outstanding.
- The authentic HUD/coffer trees, sprites, pool-bar behavior, and coffer actions
  are ported. The source-client time-stop model/fade and hover-only cosmetics
  are not; the server-driven green fill is the visible fallback. Later-phase
  giant-worm size variation also remains a visual-QA item.
- Static map tuples are exact, but revision 727 did not contain the surface
  entrance's dynamic placement stream. Its angle is reconstructed and remains
  called out in the placement ledger.
- The in-game `::rs2012qbdtest` and `::rs2012tdtest` diagnostics compile but
  still require an attached-client run. Full fight timing, clipping, visual,
  audio, reward-distribution, and persistence acceptance remains §10.3 work.

open727 itself contains demonstrable encounter bugs: a duplicate melee branch
where Magic should be, post-shield prayer accounting, odd reset/switch
behaviour, random wasted melee at range, an unverified hard-coded AoE,
five-attack style blocks, and QBD phase/HP/reward divergences. None is silently
preserved. These are configuration and evidence boundaries, not invitations to
replace 2012 mechanics with current OSRS behaviour.
