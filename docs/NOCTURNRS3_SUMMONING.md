# nocturne-rs3 as a cross-check for the Summoning port

What `~/Documents/git_repos/nocturne-rs3` can and cannot corroborate about our
familiars' attack / defend / death animations, their projectiles, and their
landing graphics. Generated 2026-08-14.

## Sources inside nocturne-rs3

Two independent stores, and they do not always agree with each other:

| store | what it holds | coverage |
|---|---|---|
| `data/npcs/packedCombatDefinitions.ncd` | per-npc `hitpoints, attackAnim, defenceAnim, deathAnim, respawnDelay, attackGfx, attackProjectile, xp` + flags | 13,002 records, **4,343 unique npc ids** (later rows overwrite, matching the loader's own `put`) |
| `src/net/nocturne/game/npc/combat/impl/*Combat.java` | hand-written per-npc combat scripts: animations, projectile parameters, landing graphics | **12** familiar scripts only |
| `src/net/nocturne/game/npc/familiar/impl/*.java` | 146 familiar classes — special name/cost/description/BOB size | no auto-attack visuals at all |

`.ncd` is a flat array of 45-byte big-endian records, no header
(585,090 bytes / 45 = 13,002 exactly): `8 x int32`, then `double xp`, then 5 bytes of flags.

## The join key (this was the trap)

A summoning familiar owns **two** npc ids — the idle/spawn form and the combat
form (2009scape's `SteelTitanNPC.getIds()` returns `{7343, 7344}`). Our
`roster_assets_530.ini` keys the **first**; nocturne's combat definitions are
keyed on the **second**. Joining on the raw id makes every single familiar
look wrong, because each record then lines up against the previous familiar in
npc-id order — steel titan 7343 appears to say `7980/7979`, which is actually
lava titan's pair. Joining on `npc + 1` resolves it: nocturne 7344 holds
`8183 / 8184`, exactly our steel titan values.

**All figures below use the `npc + 1` join.**

## Agreement across 77 familiars

| role | match | differ | only ours | only nocturne | both absent |
|---|---|---|---|---|---|
| attack | 46 | 10 | 0 | 15 | 0 |
| defend | 0 | 0 | 19 | 0 | 52 |
| death | 45 | 9 | 0 | 17 | 0 |

Six familiars have no record at all: beaver, fruit_bat, ibis, macaw, magpie, spirit_cobra.

### Defence animations are not corroborable

`defenceAnim` is `-1` for **every** familiar in the packed store. nocturne
simply does not carry familiar defence animations, so our 19 `defend_anim`
params have no second source here — neither confirmed nor contradicted.

### Where we disagree

Most disagreements are not conflicts of fact. rev-530 sequence ids top out
around 8700; a nocturne value well above that is an RS3-era
re-animation of the same familiar, so **ours is the correct rev-530 value** and
nocturne is simply from a later game.

**attack (10)**

| familiar | npc | ours | nocturne | reading |
|---|---|---|---|---|
| dreadfowl | 6825 | 7810 | 5387 | **both rev-530 range - needs review** |
| pack_yak | 6873 | 5782 | 853 | **both rev-530 range - needs review** |
| pyrelord | 7377 | 8080 | 17492 | later re-animation, keep ours |
| smoke_devil | 6865 | 7816 | 24033 | later re-animation, keep ours |
| spirit_jelly | 6992 | 8569 | 24982 | later re-animation, keep ours |
| spirit_kalphite | 6994 | 6223 | 19487 | later re-animation, keep ours |
| spirit_scorpion | 6837 | 6254 | 23627 | later re-animation, keep ours |
| vampire_bat | 6835 | 8275 | 23639 | later re-animation, keep ours |
| war_tortoise | 6815 | 8286 | 25582 | later re-animation, keep ours |
| wolpertinger | 6869 | 8304 | 8303 | **both rev-530 range - needs review** |

**death (9)**

| familiar | npc | ours | nocturne | reading |
|---|---|---|---|---|
| dreadfowl | 6825 | 5389 | 7813 | **both rev-530 range - needs review** |
| pack_yak | 6873 | 5784 | 852 | **both rev-530 range - needs review** |
| pyrelord | 7377 | 8078 | 17491 | later re-animation, keep ours |
| smoke_devil | 6865 | 7818 | 24030 | later re-animation, keep ours |
| spirit_jelly | 6992 | 8570 | 24983 | later re-animation, keep ours |
| spirit_kalphite | 6994 | 6228 | 19486 | later re-animation, keep ours |
| spirit_scorpion | 6837 | 6256 | 23628 | later re-animation, keep ours |
| vampire_bat | 6835 | 8276 | 23640 | later re-animation, keep ours |
| war_tortoise | 6815 | 8285 | 25583 | later re-animation, keep ours |

So the genuine rev-530-range conflicts are only: **dreadfowl**, **pack_yak**
and **wolpertinger** (attack 8304 vs 8303 — adjacent ids, could be either).

Note dreadfowl is a case where nocturne contradicts *itself*: the packed store
says attack `5387`, but `DreadFowlCombat.java` sets `Animation(7810)` — which
is our value. Where the two nocturne stores disagree, the hand-written combat
script is the better witness.

## Familiar combat scripts: projectiles and landing graphics

Only 12 of 146 familiars have a combat script. Projectile parameter order is
`World.sendProjectileNew(from, to, gfx, startHeight, endHeight, startTime, speed, angle, slope)`.

| class | npc ids | attack anims | projectile(s) `gfx,sH,eH,start,speed,angle,slope` | landing gfx |
|---|---|---|---|---|
| `AbbysalTitanCombat` | 7349, 7350 | 7980 | - | - |
| `DreadFowlCombat` | 6824, 6825 | 7810, 7810, 7810 | `1376,34,16,35,2,10,0`<br>`1376,34,16,35,2,10,0` | - |
| `GeyserTitanCombat` | 7339, 7340 | 7883, 7883, 7879 | `1376,34,16,35,2,10,0`<br>`1374,34,16,35,3,10,0` | - |
| `IronTitanCombat` | 7375, 7376 | 7954, 7694, 7946 | `1452,34,16,36,2,16,0` | - |
| `LavaTitanCombat` | 7341, 7342 | 7883, 7980 | - | - |
| `MinotaurCombat` | 6853, 6854, 6855, 6856, 6857, 6858, 6859, 6860, 6861, 6862, 6863, 6864 | 8026, 8024 | `1497,34,16,35,2,16,0` | - |
| `MossTitanCombat` | 7329, 7330 | 8223, 8222 | `1462,34,16,35,2,10,0` | - |
| `SpiritKalphiteCombat` | 6994, 6995 | 8519, 8519 | - | - |
| `SpiritWolfCombat` | 6828, 6829 | 8293, 6829 | `1333,34,16,35,2,10,0` | - |
| `SteelTitanCombat` | 7343, 7344 | 8190, 8183, 8190, 7694 | `1445,34,16,35,2,10,0`<br>`1451,34,16,35,2,10,0` | 1449 |
| `ThornySnailCombat` | 6806, 6807 | 8148, 8143 | `1386,34,16,30,35,16,0` | - |
| `TzKihCombat` | 7361, 7362 | - | - | - |

The remaining 134 familiars have no projectile or landing data in nocturne at all.

## Steel titan, specifically

`SteelTitanCombat.java` is the most detailed familiar script nocturne has, and
it independently corroborates part of the roster we are working from:

| style | nocturne anim | nocturne projectile | our lane |
|---|---|---|---|
| melee | 8183 | - | 8183 (`iron_titan_swing`, shared) — **agrees** |
| ranged | 8190 | **1445** `(34,16,35,2,10,0)` | 8190 + spotanim 1445 — **agrees** |
| magic | **7694** | **1451** | 8190 + spotanim 1446 — **conflicts** |
| special | 8190 | target graphic **1449** | 1449 — **agrees** |

Two things follow:

1. **1445 is the ranged flying projectile.** nocturne says so independently of
   the observed roster, which settles it.
2. **The magic projectile is contested.** The roster says 1446 (with 1447 as
   its recoloured impact); nocturne says 1451 with attack animation 7694. In
   the rev-530 cache 1451/1452 are a matching pair on model 31476 + seq 7695,
   recoloured `127->2619` — structurally identical to how 1446/1447 pair on
   model 31438 + seq 8193. Both are plausible magic projectile/impact pairs;
   the cache alone cannot choose between them.

nocturne also gives the special's shape: four hits, ranged if the target is
out of melee range, melee otherwise — matching our four-hit implementation.

## Limitations

- nocturne is RS3-era. Ids that drifted between rev 530 and RS3 will show as
  differences that are not errors; the `>8700` heuristic above catches the
  obvious ones but is a heuristic, not proof.
- The packed store's last-row-wins duplication is reproduced faithfully here,
  but it means a familiar's record may come from whichever line happened to be
  last in the source text file.
- Coverage is thin where it matters most: no defence animations at all, and
  projectile/landing data for only 12 familiars.
