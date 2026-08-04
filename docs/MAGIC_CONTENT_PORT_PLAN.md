# Magic skill + spells — finish plan

Agent-loop state for **closing Magic** against the OSRS wiki, with LostCity /
2009scape / Kronos as era-shaped refs. Parent queue:
[`SKILLS_CONTENT_PORT_QUEUE.md`](SKILLS_CONTENT_PORT_QUEUE.md) rows **#30–36**.
This file owns **ordered Magic-only slices** so a dedicated loop does not wait
on unrelated Finish-queue rows (cannon, prayer, …).

**Gap authority:** [Magic](https://oldschool.runescape.wiki/w/Magic) ·
[Magic/Training](https://oldschool.runescape.wiki/w/Magic/Training) ·
[Standard spellbook](https://oldschool.runescape.wiki/w/Standard_spellbook) ·
[Ancient Magicks](https://oldschool.runescape.wiki/w/Ancient_Magicks) ·
[Lunar spells](https://oldschool.runescape.wiki/w/Lunar_spells) ·
[Arceuus spellbook](https://oldschool.runescape.wiki/w/Arceuus_spellbook).

**Shape refs:** LostCity `skill_magic/` + `skill_combat/.../player_magic.rs2`
(+ `spells/scripts/`, `auto_cast.rs2`); 2009scape
`content/global/skill/magic/{modern,ancient,lunar}/`; cache
`interfaces/magic_spellbook.compack` for component names.

**Rules:** PORTING_GUIDE §2 / §4.1 / §4.7 / §7 — grep LC first, never park
`minigame_mta/` or siblings, resolve names via pack, no C content constants.

Sentinel: `AGENT_LOOP_WAKE_magic_port` (~120s). One pending M-slice per tick.

Status: `pending` | `in_progress` | `done` | `blocked`.

## Baseline (already live)

| Area | In tree |
|---|---|
| Utility F2P+ | tele/alch/enchant1–5/telegrab/superheat/charge/orb/bones_bananas+peaches (`skill_magic/`) |
| Combat F2P | strike→wave + confuse/weaken/curse/bind (`player_magic.rs2`) |
| Tables | `magic_spells.dbrow`, `magic_combat_spells.dbrow` (strike→wave, bind/debuff, crumble, god/iban, magic_dart; surges still missing) |
| Magic def | `#9` 7:3 blend done |
| MTA | live — do not park |

## Slice order (deps-first)

| # | Slice | Era | SKILLS | Status | Notes |
|---|---|---|---|---|---|
| M1 | Members standard combat: snare/entangle/vuln/enfeeble/stun | LC | #33/#34 | done | Bindings + vuln/enfeeble/stun dbrows; snare/entangle already had rows |
| M2 | Magic potion Drink | LC+wiki | #35 | done | `magic_potion.rs2` — `4dose1magic`… +4 Magic; battlemage/divine deferred |
| M3 | Crumble Undead | LC | #33 | done | Script + dbrow + undead param host + seed npc overlays |
| M4 | God spells + Iban Blast | LC | #33 | done | `god_iban.rs2` + dbrows; `%iban_staff_charges`; Mage Arena cast unlock; npc_statsub deferred |
| M5 | Utility remainder + spellbook leftovers | LC+wiki | #34 | done | oc_cost/oc_members hosted; bones_peaches + Magic Dart; teleother/teleblock + post-LC teles deferred |
| M6 | Autocast IF + continue | LC+cache | #30 | in_progress | Modern staff autocast surface (not LC `staff_spells.if`); host `P_OPNPCT` if still unhosted; replace stubs |
| M7 | Ancient Magicks | 2009 | #31 | pending | Ice/blood/smoke/shadow + ancient teles; DT unlock; 2009scape `ancient/` |
| M8 | Lunar spellbook | 2009 | #32 | pending | Utility+Vengeance+heal; Lunar Diplomacy unlock; 2009scape `lunar/` |
| M9 | Arceuus spellbook | wiki+cache | #32 | pending | Reanimation / thralls / death charge — post-2009; no LC |
| M10 | Surges + Magic cape | wiki+cache | #36 | pending | Wind/Water/Earth/Fire Surge rows+bindings; `skillcape_magic` Boost in `skillcape_boost.rs2` |

## Engine / host gaps (log when hit)

| Gap | Blocks | Disposition |
|---|---|---|
| `P_OPNPCT` declared 2081, may be unhosted | M6 autocast continue | Implement host in same slice if content cannot delay-reclick forever |
| `oc_members` / `oc_cost` runtime | M5 alchemy/staff F2P filter | done — hosted in `mock230_ops_obj.c` |
| `%npc_stunned` / `npc_statsub` / freeze walktrigger | Bind family effects | Soft-defer effect body; casts still spend runes/XP (status quo) |
| Multi-player / PvP magic | teleblock PvP, ancient multi | blocked → skills #2 host |

## Loop prompt

Read `docs/MAGIC_CONTENT_PORT_PLAN.md` + PORTING_GUIDE §4 / §4.7 / §7. Claim
lowest pending M-slice. Grep LostCity then 2009scape. Never park siblings.
Verify `mock230_pack --check-only` + `make -C src mock230-scripts`. Mark slice
`done`, mirror SKILLS #30–36 when a row fully closes, append Log. Re-arm
`AGENT_LOOP_WAKE_magic_port` ~120s. Stop when only `blocked` / `done` remain.

## Log

- plan created: wiki Magic + Standard/Ancient/Lunar/Arceuus; LC utility+F2P combat live; gaps → M1–M10; dedicated magic loop (does not use general skills_port selection).
- M1 done: LC `player_magic.rs2` members debuff/bind family. Added `apnpct` for snare/entangle/vuln/enfeeble/stun; combat dbrows for vuln/enfeeble/stun (`magic_spellbook:*`, no synth). Scripts green; pack 0 errors. Sibling gauntlet compile blockers fixed (varp rename vs cache varbit, hunllef stub/anims, healstat→stat_heal) — not parked.
- M2 done: wiki Magic potion +4; `player/scripts/consumption/magic_potion.rs2` dose ladder. Battlemage/divine deferred. Next = M3 Crumble.
- M3 done: LC `crumble_undead.rs2` → `spells/crumble_undead.rs2` + combat dbrow; `undead` in `combat.param`; `apply_param` hosts `undead`; seed `undead.npc` overlays. Scripts green; pack 0 errors. Next = M4 god/iban.
- M4 done: LC god/iban → `spells/god_iban.rs2` + combat dbrows; `%iban_staff_charges` authored; Arena unlock via `%saramage`/`%guthmage`/`%zamomage` + `mage_arena` zone; charge cape ×1.5; npc_statsub deferred. Next = M5 utility.
- M5 done: `oc_cost`/`oc_members` already hosted (`mock230_ops_obj.c`). Bones to Peaches as `^bones_to_peaches` + `magic_spellbook:bones_peaches` (MTA unlock gate); Magic Dart (`^magic_dart=54`, Slayer 55 / slayer staff, maxhit floor(Magic/10)+10). Deferred: teleother/teleblock (PvP/#2), house/ape/kourend/fortis teles, enchant6/xbow. Scripts green; pack 0 errors. Sibling: stub `[softtimer,gauntlet_floor_tick]` (not parked). Next = M6 autocast.
