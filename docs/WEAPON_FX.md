# Weapon animations, sounds and specials — the findings catalog

> Written 2026-08-04, from measurement. Every number here comes from a command
> that is printed beside it; re-run it rather than believing the prose
> (PORTING_GUIDE §7). The port itself is
> [`WEAPON_FX_PORT_QUEUE.md`](WEAPON_FX_PORT_QUEUE.md) — this file is what that
> queue was written from.

The complaint that started this: *many weapons do not have the right animations
and sounds — whip, scythe, twisted bow, rapier, dragon weapons and their
specials — and they should already be in the cache.*

Both halves of that are right, and the second half is the useful one. **The
animations are in this cache, under names that resolve, and nothing is asking
for them.** `scythe_of_vitur_attack`, `ghrazi_rapier_attack`,
`slayer_abyssal_whip_attack`, `human_dragon_claws_spec`, `human_elder_maul_attack`
are all present in `configs/all.seq` today. What is missing is one table saying
which obj plays which of them.

---

## 1. Why nothing looks broken

`[proc,combat_attack_anim]` (`skill_combat/combat_stats.rs2:383`) resolves the
swing as `oc_param($weapon, <style>attack_anim)`, and those params carry a
`default=` (`skill_combat/configs/combat.param:101-125`):

| param | default |
|---|---|
| `stabattack_anim` | `human_unarmedpunch` |
| `slashattack_anim` | `human_sword_slash` |
| `crushattack_anim` | `human_unarmedkick` |
| `rangeattack_anim` | `human_bow` |
| `defend_anim` | `human_unarmedblock` |

So a weapon with no overlay does not fail, log, or fall back visibly. It swings
a bronze longsword. An abyssal whip plays `human_sword_slash`; so does a scythe
of vitur and a ghrazi rapier. A twisted bow plays `human_bow`, which is nearly
right and therefore worse — nobody looks twice at it.

That is the whole bug class, and it is why it had never been counted. **A wrong
answer that is shaped like a right one produces no signal.** The counting is now
a command:

```sh
tools/port_weapon_fx.py --summary
```

---

## 2. The size of it

```
osrs239 obj records                      33747
  worn in the right hand and fights       1083
  already carry an FX overlay              170
  swing the param default today            913
```

"Worn in the right hand and fights" is `wearpos=3` **plus** a combat param
(`attackrate` or one of the four attack bonuses). `wearpos=3` alone is 1,964
records and includes sailing cargo crates; the combat-param test is what makes
the denominator honest.

The 170 that are overlaid are the LostCity port
(`skill_combat/configs/bas/*.obj`, `EQUIP_BAS_PORT_QUEUE.md` slices 3–7). They
are correct and they stop where LostCity stops: **September 2004**. Every weapon
released since — whip aside, which LostCity does carry — is in the 913.

---

## 3. Three sources, and they answer different questions

Every row this port writes cites three. Do not substitute one for another: two
of the three are id-shaped and ids never cross trees (PORTING_GUIDE §4.1 rule 4).

### 3.1 Cache — what exists *here*

`OSRS-Content/osrs239-content/`. The authority for which objs are weapons, what
`category=` (config opcode 94) they carry, what `attackrate` the cache states,
and — the load-bearing part — **which seq names are spellable**.

```sh
grep -c '^\[' OSRS-Content/osrs239-content/configs/all.seq       # 14413 seq records
grep -n '^\[scythe_of_vitur_attack\]' …/configs/all.seq          # it is there
```

The port emits **names, never numbers**. If a name does not resolve in
`all.seq`, the row cannot be written and `--check` says so. That is the entire
id-safety story.

### 3.2 Wiki — what the weapon *does*

`https://oldschool.runescape.wiki/w/<Display_Name>` (spaces → `_`; the display
name is the obj record's `name=`). Checked 2026-08-04: **all 76 weapons the
wiki's [Special attack](https://oldschool.runescape.wiki/w/Special_attack) page
lists resolve to an osrs239 obj by display name, with zero misses.** That makes
the wiki a usable index into this cache, not just prose.

It is the authority for:

- **attack speed in ticks** ([Attack speed](https://oldschool.runescape.wiki/w/Attack_speed)) — cross-check against the cache's `attackrate`
- **which styles a weapon offers** and what each trains
- **whether a weapon has a special attack**, its energy cost, and what it does

It is **not** an authority for animation or sound ids. It does not state them.
Anyone reading one out of a wiki page has read it out of a screenshot caption.

One correction worth carrying: the wiki's
[Attack style](https://oldschool.runescape.wiki/w/Attack_style) page does **not**
enumerate per-weapon-type button layouts — it gives the generic four styles and
says "some present 3 options … others might have four". The per-type layout
authority is in this cache: DBTable 78 `combat_interface_weapon_category`, 36
rows, `columndef=1:button,int,string,string,graphic`
(`configs/all.dbtable:701`). §5 has how that is already wired.

### 3.3 RuneLite — id ↔ gameval name

`~/Documents/git_repos/runelite`, `runelite-api/src/main/java/net/runelite/api/gameval/AnimationID.java`
(14,468 constants, read off a current live cache, HEAD `954f990b` 2026-08-03).
This is what turns *another server's integer* into a name this tree can resolve:

```
public static final int SLAYER_ABYSSAL_WHIP_ATTACK = 1658;
public static final int SCYTHE_OF_VITUR_ATTACK     = 8056;
public static final int HUMAN_DRAGON_CLAWS_SPEC    = 7514;
```

lowercased, those are exactly this cache's seq names. RuneLite's sibling
`gameval/` classes (`ItemID`, `NpcID`, `ObjectID`, `SpotanimID`, `VarbitID`,
`InterfaceID`, `DBTableID`) serve the same role for the other namespaces.

**RuneLite has no full sound-effect table.** `runelite-api/.../SoundEffectID.java`
is a hand-curated 101-line list of UI blips. §6 has what stands in.

---

## 4. The mapping source: rsmod

`~/Documents/git_repos/rsmod` (HEAD `39403bbc`, 2025-08-08) — a modern OSRS
server in Kotlin. It is the closest structural match of any reference in this
project's stack, and the reason is not incidental: **rsmod states weapon FX as
obj params**, which is exactly where this tree already reads them from.

`api/cache-enricher/src/main/resources/org/rsmod/api/cache/enricher/obj/objs.toml`
— 35,139 lines, 4,686 obj rows, keyed by *the same gameval obj names this tree
uses*:

```toml
[[config]]
obj = 'abyssal_whip'
walk_anim = 1660
run_anim = 1661
anim_stance1 = 1658
sound_stance1 = 2720
anim_stance2 = 1658
sound_stance2 = 2720
anim_stance4 = 1658
sound_stance4 = 2720
block_anim = 1659
equipment_sound = 2249
weapon_category = 'Whip'
```

Field coverage across those 4,686 rows:

| key | rows | this tree's param |
|---|---:|---|
| `anim_stance1..4` | 969 | `attack_anim_stance1..4` *(new, §7)* |
| `sound_stance1..4` | 962 | `attack_sound_stance1..4` *(new, §7)* |
| `block_anim` | 933 | `defend_anim` *(exists)* |
| `equipment_sound` | 2,307 | `equipment_sound` *(new)* |
| `ready_anim` / `turn_anim` / `walk_anim*` / `run_anim` | 506 / 368 / 473 / 444 | the `*_baseanim` family *(exists)* |
| `weapon_category` | 1,168 | cross-check against `oc_category` |
| `speed` | 647 | cross-check against cache `attackrate` |
| `range`, `proj_anim`, `projectile_launch`, `projectile_travel`, `ammo_recovery_rate` | 301 / 225 / 104 / 175 / 10 | `weapon_attackrange`, `proj_launch`, `proj_travel` *(exist)* |

`.data/symbols/seq.sym` (12,205 rows, `id\tname`) is the id→name table used
first; RuneLite is the independent second opinion on every id.

### 4.1 It matches

```
seq names: osrs239 14413, rsmod seq.sym 12205, runelite gameval 14468
  rsmod names that resolve here          12156 / 12205    (99.6%)
rows with an unwritable field                0
```

Zero. Every seq id `objs.toml` cites for a weapon in this cache resolves to a
name that exists here, and **rsmod's seq.sym and RuneLite's AnimationID never
once disagree on an id this port cites**. That is the cross-check, and it is the
reason this can be a mechanical table copy rather than a judgement call.

### 4.2 Coverage of the 913

```
rsmod objs.toml rows                      4686
  bare weapons it states a swing for       667
  bare weapons with no row                 246
    of which inherit from a sibling        236
    of which have no source at all          10
cross-check set (overlaid AND in rsmod)    169
```

The 236 share a cache `category=` with a weapon rsmod does cover (a `_trouver`
variant, an inactive/uncharged form, a barrows ornament kit, a league reskin),
so a per-category default table answers them without inventing anything. **Ten
weapons in this cache have no source at any tier** — those get named in the
queue and left bare rather than guessed.

The 169 overlap is not waste, it is the validation set: 169 weapons where
LostCity's 2004 answer and rsmod's live answer can be compared. Spot-checked,
they agree — `adamant_claws` slash is `human_axe_chop` in the tree and 393 in
rsmod, and 393 *is* `human_axe_chop`. The port must diff all 169 and treat a
disagreement as a finding, not as a merge conflict.

### 4.3 The whole port is small

```sh
tools/port_weapon_fx.py --report | grep -o 'synth_[0-9]*' | sort -u | wc -l   #  85
```

**154 distinct seqs and 85 distinct sound effects** across all 913 weapons. Two
checkable lists, not a thousand decisions.

---

## 5. Weapon type is already content, and it is the right key

`[proc,combat_weapon_type]`
(`interface_combat/scripts/weapon_type.rs2:23`) already maps `oc_category` →
varbit 357's weapon type over 23 categories, with
`interface_combat/configs/weapon_type.constant` naming the 36 rows of DBTable
78. It exists because writing the category straight into the varbit drew a
plausible wrong panel (see the `weapon-category-vs-weapon-type` note and
`combat_hud.md`).

That rung is the natural home for the per-category defaults the 236 inheritors
need. It is already ported, already named, already selftested — the FX port
extends it rather than building beside it.

---

## 6. Sound is a three-link chain and every link is measurable

This is separate from animation and is the reason the port is two lanes, not one.

### 6.1 Content dropped every call

The LostCity combat port deleted `sound_synth` wholesale, and said so in each
file header:

```sh
grep -rn "drop sound_synth" OSRS-Content/osrs239-content/server/scripts/skill_combat/
```

— `player_ranged.rs2`, `player_magic.rs2`, `npc_combat_magic.rs2`,
`npc_combat_ranged.rs2`, `poison.rs2`, `specwep.rs2`,
`player_special_attack.rs2`, the spec scripts, the spell scripts. The stated
reason was "soundeffects pack is synth_N only", which is true and turns out not
to matter (§6.4).

Upstream, `[proc,combat_swing_anim_and_synth]`
(`LostCity_Server/content/scripts/skill_combat/scripts/combat.rs2:64`) returns
`(seq, synth)` — **one proc, both effects**. Splitting them is this tree's
divergence, not the reference's.

### 6.2 The opcode is a stub

`SS_OP_SOUND_SYNTH` is 2110 (`src/serverscript/ss_opcode.h:184`) and *is* in the
coverage header, so nothing flags it. Its host implementation
(`src/torirsserver/torirs_server_scripts.c:6406`) pops three ints, prints them under
`TORIRSSERVER_VERBOSE`, and returns. It sends nothing. The comment says so honestly —
"the encoder lands with the rest of the dialogue packets" — which means the
coverage number counts an opcode that does not do its job.

### 6.3 The packet is unrouted

`src/net/rev/osrs230/packetin.h:132`:

```c
{ 102, 5, PKT_NAME_NONE }, /* SYNTH_SOUND */
```

It frames correctly and drops cleanly, which is the pattern PORTING_GUIDE §5.1
step 3 describes. `lc254` and `lc245_2` both route it
(`PACKET_DEFINITION(PKT_NAME_SYNTH_SOUND, …, 5)`); rev230 does not.

### 6.4 The client half is finished and tested

`src/game/rs_gameproto_exec.c:907` decodes `PKT_NAME_SYNTH_SOUND` into
`RS_Audio_Synth(id, loops, delay)`. `src/game/test/rs_audio_test.c` asserts a
SYNTH_SOUND packet reaches the platform as **audible PCM from a real cache**.

And the naming problem the content headers cite is not one: this tree's
`pack/4_soundeffects.pack` is `N=synth_N` for 12,010 ids, so **`synth_2720` is
already a resolvable name**. Zero naming work is required to write the table;
minting readable names is optional polish.

So the sound gap is: one `packetin.h` line, one encoder, one opcode body, and
then a data table. Not an era limitation.

### 6.5 Separately: 1,588 seqs carry frame sounds that nothing plays *(superseded)*

> **Superseded 2026-08-09.** Frame sounds are wired and played; the radius and
> the weighted pick among a frame's alternatives are implemented too. See
> `docs/AUDIO_ACCURACY.md` §3.3. The grep below now finds plenty. The section is
> kept because the *measurement* in it is still correct and is the basis of the
> newer one.

`configs/all.seq` has 4,329 `sound=` lines across 1,588 records — the cache's
own per-frame sound effects. `3rd/rscache/src/datatypes/dat2_config_sequence.c`
decodes all four era shapes of them (`handle_frame_sounds_pre_220` /
`_220_226` / `_framed` / `_226_plus`) into `def->frame_sounds`.

```sh
grep -rn "frame_sounds" src/ | grep -v /build     # empty
```

**Nothing in `src/` reads that field.** These are mostly skilling
(`human_mining_*`, `human_woodcutting_*`, `human_fishing_casting`,
`human_smithing`) plus newer combat (`wild_cave_chainmace_crush`), so closing it
is worth doing on its own merits — but note the weapons this complaint is about
do *not* have frame sounds: `slayer_abyssal_whip_attack` and
`human_scythe_slash` carry none. **Weapon swing sound is server-driven
(§6.1–6.4); skilling sound is cache-driven (this section).** Fixing one does not
fix the other, and confusing them would send someone to the wrong lane.

---
### 6.6 The swing sound was wrong for every weapon, and why *(resolved)*

Recorded 2026-08-09, after a report that "the bow of faerdhinen sounds like a
regular bow". It did not: **every weapon in the game played sound effect 0.**

Three faults in series, each invisible on its own.

**1. The "nothing" sentinel was a real sound.** `attack_sound_stance1..4`
declared `default=0`, and `[proc,combat_attack_sound]` returned that default
verbatim. Sound effect 0 exists in the cache (1,275 bytes, a real synth
program), and the client's queue only refuses a *negative* id — so the default
was not silence, it was a specific noise, played identically by every weapon.
LostCity gets this right by construction: its equivalents are `type=synth,
default=null`, and its engine refuses a null (`check(synth, NumberNotNull)`,
PlayerOps.ts). Ours are now `default=-1`, guarded at both swing sites and
dropped by `SS_OP_SOUND_SYNTH`.

This is the [[weapon-fx-and-rsmod-reference]] trap in its purest form — *a param
with a default turns a missing port into a plausible wrong answer* — and it beat
the audit that memory came from, because that audit counted weapons falling
through to the **animation** default and never asked the same question of the
sound.

**2. No weapon stated a sound, and none could.** The port writer refused to emit
sound params at all: `pack/4_soundeffects.pack` named every sound `synth_<id>`,
the id spelled back at itself, so there was nothing for a value to resolve
against and `torirs_server_content.c` had no `synth` pack kind to resolve it with.
Both are fixed — `TORIRSSERVER_PACK_SYNTH` exists, and `tools/gen_sound_names.py`
fills the pack with **Jagex's own config names** (see §6.7). 902 weapons now
state their swing sounds, emitted by `tools/port_weapon_fx.py --write-sounds`.

**3. The keying disagreed with the animation's.** LostCity returns anim and
sound from *one* proc keyed by damage type
(`[proc,combat_swing_anim_and_synth]`, combat.rs2:64), so the two can never
disagree. This tree keyed the anim by stance-then-damage-type and the sound by
stance only, with no fallback. `[proc,combat_attack_sound]` now has the same
two-stage shape the anim side has, and `stab_sound` / `slash_sound` /
`crush_sound` / `rangeattack_sound` are declared — which also makes LostCity's
own 444 rows usable here without re-keying them.

### 6.7 Sound effects have real names now *(resolved)*

`pack/4_soundeffects.pack` was 12,010 rows of `synth_<id>`. It is now 10,113
rows of Jagex's own config names, from the OSRS Wiki's
[[List of sound IDs]] — which the wiki says became knowable because Jagex
"accidentally transmitted their names" in the February 2025 *Game Jam: POH
Improvements* update. `tools/gen_sound_names.py` regenerates it from
`docs/audio/osrs_wiki_sound_ids.wikitext`, committed so the generator needs no
network.

That they are Jagex's own names is checkable, and it checks three ways:
2500 is `hacksword_slash` and 2501 `hacksword_stab`, the exact spellings
LostCity wrote from the 2004 cache years earlier; 2247/2248 are
`equip_staff`/`equip_sword`, matching what rsmod assigns as `equipment_sound` on
staves and swords; and 1352 is `crystal_bow2`, which is what rsmod assigns to the
bow of faerdhinen — a crystal bow.

**That last line is what makes the port safe.** rsmod targets revision 231 and
this tree is 239, rsmod's `synth.sym` names only 187 sounds (none of the ones
weapons use), so before the names existed a sound could only cross as a raw
integer — exactly what §4.1 rule 4 forbids. Two things now stand behind it: the
names mediate the crossing the way they do for animations, and the id space was
checked directly — all 97 distinct sound ids rsmod cites are present and
**byte-identical** in `cache.osrs230` and `cache.osrs239`, which bracket 231.

The same wiki leak covered **sounds only**. `List of sound IDs` is the only page
on the wiki that cites it, and a full-text search for the phrase returns that one
page — so there is no equivalent windfall waiting for varps, scripts, npcs or
structs.


## 7. The keying decision: stance, not damage type

This is the one design choice the port has to make, and both answers are already
in the tree.

- **This tree keys by damage type.** `combat_attack_anim` switches on
  `%damagetype` and reads `stabattack_anim` / `slashattack_anim` /
  `crushattack_anim` / `rangeattack_anim`. That is LostCity's shape.
- **rsmod and the live game key by stance** — the 1..4 slot of the attack-style
  button, which this server already tracks as `%com_mode` and which the combat
  tab already draws from DBTable 78.

They agree wherever the style's damage type decides the swing, which is most
weapons. They cannot agree where two styles share a damage type and differ in
animation — unarmed punch vs kick is exactly that case, and
`combat_stats.rs2:395` already special-cases it in the proc:

```
if ($damagestyle = ^style_melee_aggressive) { return(human_unarmedkick); }
return(human_unarmedpunch);
```

That `if` is the damage-type model failing once and being patched by hand. The
port should **add** `attack_anim_stance1..4` / `attack_sound_stance1..4`,
consult them first, and keep the damage-type params as the fallback — which
makes the 170 existing overlays keep working untouched and lets the special case
above be deleted rather than multiplied.

`combat_stats.rs2:369` already anticipates this in a comment: *"The reference
resolves it from the weapon style table (`~combat_swing_anim_and_synth`), which
also picks the sound; until that table lands this is the weapon's own param."*
This is that table landing.

### 7.1 One correction to `combat.param`

`skill_combat/configs/combat.param:116` says the general `attack_anim` is *"the
cache's own `attack_anim` (param 2635)"*. Measured:

```sh
grep -c "param_2635" OSRS-Content/osrs239-content/configs/all.obj   # 0
```

There is no param 2635 on any obj record in this cache, and no obj param of any
number carries an animation. The cache states weapon **bonuses** as params
(`slashattack`, `strengthbonus`, `attackrate`, …) and states nothing about
swings. `attack_anim` here is a server-allocated param like the rest. The
comment should be corrected when the file is next touched — as written it tells
the next reader the data is already in the cache under a number, and they will
go look for it.

---

## 8. Special attacks

Two different gaps wearing one name.

**What exists.** `skill_combat/configs/special_attack.obj` — 12 obj rows
(`specwep` / `sa_energy` / `sa_kind`) covering dragon dagger (+p/+p+/+p++),
dragon longsword, dragon mace, dragon battleaxe, Excalibur, rune claws,
Saradomin godsword, magic shortbow, magic longbow. Seven scripts in
`skill_combat/scripts/player/specs/`. Energy, the toggle, the orb and the regen
timer are all ported and working (`specwep.rs2`). This is LostCity's set, and
again it stops in 2004.

**What the game has.** The wiki's [Special attack](https://oldschool.runescape.wiki/w/Special_attack)
page lists ~63 melee, 20 ranged and 5 magic weapons, plus 14 skilling specials
and 4 cooldown-based semi-specials. All 76 sampled resolve to an osrs239 obj by
display name (§3.2).

**Where the implementation shape comes from.** Not rsmod — its
`content/other/special-attacks/` is early (29 registrations, mostly the skilling
axes/pickaxes/harpoons). **Kronos** is the reference here:
`Kronos184-Fixed_2/.../io/ruin/model/combat/special/` — 30 melee, 12 ranged, 1
magic, 4 skilling, each a small class carrying exactly the three things this
port needs beside the damage rule:

```java
player.animate(1658);        // seq   → name via seq.sym / AnimationID
player.publicSound(2713);    // synth → synth_2713
target.graphics(341, 96, 0); // spotanim + height + delay
```

**The dependency that orders the work.** Specials need §6's sound chain
(`publicSound` is `sound_synth`) and they need the anim table, because a spec
that plays the right special animation and then returns to a bronze-longsword
swing is still wrong. **Specials are last.** The queue says so.

The behavioural half is not mechanical and must not be treated as such — dragon
claws' four-hit damage cascade is 40 lines of rules the wiki states in prose and
Kronos states in code, and both need reading. That is a per-weapon content
slice, priced accordingly in the queue.

---

## 9. What was checked and found *correct*

Recorded so nobody re-derives it:

- The 170 LostCity overlays cross-check clean against rsmod on the 169 rows both
  cover (spot-checked; the full diff is queue slice 2).
- `~combat_weapon_type`'s 23-category mapping and the 36 `weapon_type.constant`
  rows agree with DBTable 78.
- `TORIRSSERVER_STYLE_*` ordering (0 accurate, 1 aggressive, 2 controlled, 3
  defensive) matches the cache.
- The client's animation path is fine end to end — this is not a rendering bug,
  and `TORIRS_ANIM_DEBUG` will show the *correct* wrong seq playing.

## 10. Commands

```sh
tools/port_weapon_fx.py --summary            # the counts in §2 and §4.2
tools/port_weapon_fx.py --report             # TSV, every combat weapon
tools/port_weapon_fx.py --report --gap       # only the 667 portable rows
tools/port_weapon_fx.py --sources abyssal_whip   # the three-source card
tools/port_weapon_fx.py --check              # the build bar; exit 1 on a bad row
```

`--check` is the bar because it is the one that cannot pass by accident: it
fails on any cited id that has no name, whose two sources disagree, or whose
name is absent from this cache.
