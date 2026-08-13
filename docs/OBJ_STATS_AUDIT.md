# Object stats audit — tradeable, members, alch, weapon stats, attack speed

> Measured 2026-08-13, against `OSRS-Content/osrs239-content/configs/all.obj`
> (33,747+ obj records) and the mock230 engine. Every claim below is a literal
> command; re-run it rather than believing the prose (same contract as
> `WEAPON_FX.md`). This is the "audit all objects" answer for the fields that
> turned out to need no wiki work at all — see `WEAPON_FX_PORT_QUEUE.md` slice
> 12 for the one field group (special attack behaviour) that genuinely does.

## 0. Headline

Five of the fields named in the request — tradeable, members, high/low alch,
weapon attack speed, weapon combat bonuses — are **already fully decoded from
the OSRS cache for every object and already wired into the engine and
content**. Nothing here needed scraping; the audit is a proof, not a port.

| field | status | evidence |
|---|---|---|
| tradeable | done — native | `struct Mock230ObjInfo.tradeable` (opcode 15), `oc_tradeable` |
| members | done — native, consumed | `struct Mock230ObjInfo.members` (opcode 16), `oc_members` used at 10+ real call sites |
| weight | done — native | opcode 75, `weight=` in every record |
| equipable | done — native (`wearpos != -1`) | opcodes for wearpos/wearpos_2/wearpos_3 |
| weapon combat bonuses | done — native params | `strengthbonus`/`prayerbonus`/stab-slash-crush-range-magic attack+defence, params 0-11 |
| weapon attack speed | done — native param, read | `attackrate` param (id 14, default 4); `combat.rs2:112,227` reads `oc_param($weapon, attackrate)` |
| high/low alchemy | done — computed, not stored | `oc_cost * 60/100` / `*40/100`; `alchemy.rs2:47,86` implements exactly that |
| **special attack behaviour** | **the real gap** | 135 weapons have `specwep`/`sa_energy`; ~10-12 had a behaviour script before this pass |

## 1. Tradeable

```sh
grep -c "^tradeable=no" OSRS-Content/osrs239-content/configs/all.obj
grep -n "tradeable" src/net/mock/mock230.h src/net/mock/mock230_ops_obj.c
```

`Mock230ObjInfo.tradeable` (`mock230.h:857`) is set from cache opcode 15
(`getradeable=`/`tradeable=` in the text export; the cache clears a bit for
non-tradeable, default is tradeable). `[command,oc_tradeable]`
(`mock230_ops_obj.c:319`) reads it directly. Distinct from GE listing by
design (an item can be tradeable player-to-player without a GE line).

## 2. Members

```sh
grep -c "^members=yes" OSRS-Content/osrs239-content/configs/all.obj
grep -rn "oc_members(" OSRS-Content/osrs239-content/server/scripts | wc -l
```

`Mock230ObjInfo.members` (`mock230.h:859`) is opcode 16. `oc_members` is
called from real content, not just declared: crafting (jewellery members
gate), woodcutting, runecraft, cooking (dough/pizza), magic (staff/enchant),
and `npc/scripts/dragon.rs2`'s dragonfire-weapon check — see
`combat.rs2:332`'s own comment: "a members' item cannot be used on a free
world."

## 3. High / low alchemy

```sh
grep -c "^highalch=\|^lowalch=" OSRS-Content/osrs239-content/configs/all.obj   # 0 — not a stored field, anywhere
sed -n '40,55p;80,90p' OSRS-Content/osrs239-content/server/scripts/skill_magic/scripts/spells/alchemy.rs2
```

There is no `highalch`/`lowalch` opcode in the OSRS cache format at all —
confirmed by extracting every unique `key=` across the 33,747-record file and
finding none. The value is `content`'s to compute, per
`mock230.h:850-851`'s own comment: `"oc_cost returns this; high alch is
content's calc(oc_cost($obj) * 60 / 100)."`
`skill_magic/scripts/spells/alchemy.rs2` already does exactly that:

```
def_int $profit = max(scale(6, 10, oc_cost($item)), 1);   // high alch, 60%
def_int $profit = max(scale(4, 10, oc_cost($item)), 1);   // low alch, 40%
```

Nothing to port.

## 4. Weapon attack speed

```sh
grep -n "attackrate" OSRS-Content/osrs239-content/server/scripts/skill_combat/combat.rs2
```

`param=attackrate` is native to every weapon record (e.g. `bronze_scimitar`
carries `param=attackrate,int,4`). `combat.rs2:112` (`%action_delay =
add(map_clock, oc_param($weapon, attackrate))`) and `:227` (the unarmed
fallback path) both read it live — this is not a default being silently
substituted, it is the actual per-weapon value driving the player's swing
timer.

## 5. Weapon combat stats (accuracy/strength/defence bonuses)

Native params 0-11 (`strengthbonus`, `prayerbonus`, `stabattack`,
`slashattack`, `crushattack`, `rangeattack`, `magicattack`, `stabdefence`,
`slashdefence`, `crushdefence`, `rangedefence`, `magicdefence`), present on
every equipable record. `EQUIP_BAS_PORT_QUEUE.md` slice 8 already audited
this exact question ("Stats audit — cache bonuses remain authority (bronze
scimitar selftest); no RL dump") and closed it — this pass re-confirmed the
same conclusion by reading `configs/all.obj` directly rather than trusting
the prior close.

## 6. What is genuinely not native, and needed the wiki

**Special attack behaviour.** `sa_kind`, `specwep`, `sa_energy` are
server-allocated params (the cache has no special-attack concept at all).
`special_attack.obj` had 135 weapons with `specwep`/`sa_energy` (arm/drain
data, wiki-sourced in an earlier pass) but only ~10-12 with a real `sa_kind`
dispatch + behaviour script — every other special-attack weapon drained the
orb and then just swung normally. This is the one place in the whole audit
where "scrape the wiki, then implement the behaviour" is the correct and
necessary shape of the work, and it is what the rest of this pass (tracked in
`WEAPON_FX_PORT_QUEUE.md` slice 12) implements, one weapon at a time, each
with its own `.rs2` file and a cited wiki source.

Related, not re-litigated here: weapon *animation/sound* overlays (a
separate, largely-complete effort — see `WEAPON_FX.md` /
`WEAPON_FX_PORT_QUEUE.md`), and the "products and creation" (recipe) ledger,
which is mostly already covered piecemeal across `CONTENT_PORT_QUEUE.md`'s
skill_* slices rather than missing outright.
