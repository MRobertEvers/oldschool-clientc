# OSRS ranged weapons: Wiki inventory and implementation audit

Audit date: 2026-08-16. The canonical inventory is the OSRS Wiki's
[Ranged weapons](https://oldschool.runescape.wiki/w/Ranged_weapons) page
(revision `15264153`, 2026-07-15). The category cross-check is
[Weapons/Categories](https://oldschool.runescape.wiki/w/Weapons/Categories)
(revision `15301239`, 2026-08-14). Ammunition rules were checked against the
Wiki's [Ammunition](https://oldschool.runescape.wiki/w/Ammunition) and
[Crossbow](https://oldschool.runescape.wiki/w/Crossbow_(weapon)) pages.

This audit treats a weapon as implemented when its cache object reaches the
normal PvM Ranged combat path with an attack style, compatible ammunition (or
self-ammunition), Ranged Strength, consumption/recovery semantics, and a cache
projectile where one exists. LMS, Deadman, Corrupted Gauntlet, ornamented,
charged and degraded forms share their base family's combat path. Exact parity
for every bespoke passive, PvP rule, or multi-target effect is a separate
feature from making every listed weapon usable; notably, chinchompa splash
targets remain deferred by the current single-target PvM combat system.

## Canonical weapon list

Parenthetical `p`, `p+`, and `p++` entries below are distinct poisoned item
variants in the cache and are all included in the audit.

### Bows and bow-like weapons

- [3rd age bow](https://oldschool.runescape.wiki/w/3rd_age_bow), [Bone shortbow](https://oldschool.runescape.wiki/w/Bone_shortbow), [Bow of faerdhinen](https://oldschool.runescape.wiki/w/Bow_of_faerdhinen), [Bow of faerdhinen (c)](https://oldschool.runescape.wiki/w/Bow_of_faerdhinen_(c)).
- Bow of faerdhinen (c) variants: [Amlodd](https://oldschool.runescape.wiki/w/Bow_of_faerdhinen_(c)_(Amlodd)), [Cadarn](https://oldschool.runescape.wiki/w/Bow_of_faerdhinen_(c)_(Cadarn)), [Crwys](https://oldschool.runescape.wiki/w/Bow_of_faerdhinen_(c)_(Crwys)), [Deadman](https://oldschool.runescape.wiki/w/Bow_of_faerdhinen_(c)_(Deadman)), [Iorwerth](https://oldschool.runescape.wiki/w/Bow_of_faerdhinen_(c)_(Iorwerth)), [Ithell](https://oldschool.runescape.wiki/w/Bow_of_faerdhinen_(c)_(Ithell)), [LMS](https://oldschool.runescape.wiki/w/Bow_of_faerdhinen_(c)_(LMS)), [Meilyr](https://oldschool.runescape.wiki/w/Bow_of_faerdhinen_(c)_(Meilyr)), and [Trahaearn](https://oldschool.runescape.wiki/w/Bow_of_faerdhinen_(c)_(Trahaearn)).
- [Comp ogre bow](https://oldschool.runescape.wiki/w/Comp_ogre_bow), [Corrupted bow (attuned)](https://oldschool.runescape.wiki/w/Corrupted_bow_(attuned)), [Corrupted bow (basic)](https://oldschool.runescape.wiki/w/Corrupted_bow_(basic)), [Corrupted bow (perfected)](https://oldschool.runescape.wiki/w/Corrupted_bow_(perfected)), [Corrupted dark bow](https://oldschool.runescape.wiki/w/Corrupted_dark_bow), [Corrupted twisted bow](https://oldschool.runescape.wiki/w/Corrupted_twisted_bow).
- [Craw's bow](https://oldschool.runescape.wiki/w/Craw%27s_bow), [Crystal bow](https://oldschool.runescape.wiki/w/Crystal_bow), [Crystal bow (attuned)](https://oldschool.runescape.wiki/w/Crystal_bow_(attuned)), [Crystal bow (basic)](https://oldschool.runescape.wiki/w/Crystal_bow_(basic)), [Crystal bow (perfected)](https://oldschool.runescape.wiki/w/Crystal_bow_(perfected)), [Cursed goblin bow](https://oldschool.runescape.wiki/w/Cursed_goblin_bow).
- [Dark bow](https://oldschool.runescape.wiki/w/Dark_bow), [Dark bow (bh)](https://oldschool.runescape.wiki/w/Dark_bow_(bh)), [Dark bow (Deadman)](https://oldschool.runescape.wiki/w/Dark_bow_(Deadman)), [Dark bow (LMS)](https://oldschool.runescape.wiki/w/Dark_bow_(LMS)), [Echo venator bow](https://oldschool.runescape.wiki/w/Echo_venator_bow).
- [Eclipse atlatl](https://oldschool.runescape.wiki/w/Eclipse_atlatl), [Eclipse atlatl (LMS)](https://oldschool.runescape.wiki/w/Eclipse_atlatl_(LMS)), [Longbow](https://oldschool.runescape.wiki/w/Longbow), [Magic comp bow](https://oldschool.runescape.wiki/w/Magic_comp_bow), [Magic longbow](https://oldschool.runescape.wiki/w/Magic_longbow), [Magic shortbow](https://oldschool.runescape.wiki/w/Magic_shortbow), [Magic shortbow (i)](https://oldschool.runescape.wiki/w/Magic_shortbow_(i)).
- [Maple longbow](https://oldschool.runescape.wiki/w/Maple_longbow), [Maple shortbow](https://oldschool.runescape.wiki/w/Maple_shortbow), [Nature's recurve](https://oldschool.runescape.wiki/w/Nature%27s_recurve), [Oak longbow](https://oldschool.runescape.wiki/w/Oak_longbow), [Oak shortbow](https://oldschool.runescape.wiki/w/Oak_shortbow), [Ogre bow](https://oldschool.runescape.wiki/w/Ogre_bow).
- [Rain bow](https://oldschool.runescape.wiki/w/Rain_bow), [Scorching bow](https://oldschool.runescape.wiki/w/Scorching_bow), [Seercull](https://oldschool.runescape.wiki/w/Seercull), [Shortbow](https://oldschool.runescape.wiki/w/Shortbow), [Signed oak bow](https://oldschool.runescape.wiki/w/Signed_oak_bow), [Starter bow](https://oldschool.runescape.wiki/w/Starter_bow), [Training bow](https://oldschool.runescape.wiki/w/Training_bow).
- [Twisted bow](https://oldschool.runescape.wiki/w/Twisted_bow), [Venator bow](https://oldschool.runescape.wiki/w/Venator_bow), [Webweaver bow](https://oldschool.runescape.wiki/w/Webweaver_bow), [Willow comp bow](https://oldschool.runescape.wiki/w/Willow_comp_bow), [Willow longbow](https://oldschool.runescape.wiki/w/Willow_longbow), [Willow shortbow](https://oldschool.runescape.wiki/w/Willow_shortbow), [Yew comp bow](https://oldschool.runescape.wiki/w/Yew_comp_bow), [Yew longbow](https://oldschool.runescape.wiki/w/Yew_longbow), [Yew shortbow](https://oldschool.runescape.wiki/w/Yew_shortbow).

### Salamanders

- [Black salamander](https://oldschool.runescape.wiki/w/Black_salamander), [Orange salamander](https://oldschool.runescape.wiki/w/Orange_salamander), [Red salamander](https://oldschool.runescape.wiki/w/Red_salamander), [Swamp lizard](https://oldschool.runescape.wiki/w/Swamp_lizard), [Tecu salamander](https://oldschool.runescape.wiki/w/Tecu_salamander).

The audit covers the Ranged/Flare style and its exact tar pairing. Salamander
Melee and Magic styles are outside this ranged-weapon audit.

### Chinchompas

- [Black chinchompa](https://oldschool.runescape.wiki/w/Black_chinchompa), [Chinchompa](https://oldschool.runescape.wiki/w/Chinchompa), [Red chinchompa](https://oldschool.runescape.wiki/w/Red_chinchompa).

### Crossbows and ballistae

- [Adamant crossbow](https://oldschool.runescape.wiki/w/Adamant_crossbow), [Armadyl crossbow](https://oldschool.runescape.wiki/w/Armadyl_crossbow), [Armadyl crossbow (LMS)](https://oldschool.runescape.wiki/w/Armadyl_crossbow_(LMS)), [Blurite crossbow](https://oldschool.runescape.wiki/w/Blurite_crossbow), [Bronze crossbow](https://oldschool.runescape.wiki/w/Bronze_crossbow), [Crossbow](https://oldschool.runescape.wiki/w/Crossbow).
- [Dorgeshuun crossbow (Bone crossbow)](https://oldschool.runescape.wiki/w/Dorgeshuun_crossbow), [Dragon crossbow](https://oldschool.runescape.wiki/w/Dragon_crossbow), [Dragon crossbow (cr)](https://oldschool.runescape.wiki/w/Dragon_crossbow_(cr)), [Dragon crossbow (LMS)](https://oldschool.runescape.wiki/w/Dragon_crossbow_(LMS)), [Dragon hunter crossbow](https://oldschool.runescape.wiki/w/Dragon_hunter_crossbow), [Dragon hunter crossbow (b)](https://oldschool.runescape.wiki/w/Dragon_hunter_crossbow_(b)), [Dragon hunter crossbow (t)](https://oldschool.runescape.wiki/w/Dragon_hunter_crossbow_(t)).
- [Heavy ballista](https://oldschool.runescape.wiki/w/Heavy_ballista), [Heavy ballista (LMS)](https://oldschool.runescape.wiki/w/Heavy_ballista_(LMS)), [Heavy ballista (or)](https://oldschool.runescape.wiki/w/Heavy_ballista_(or)), [Hunters' crossbow](https://oldschool.runescape.wiki/w/Hunters%27_crossbow), [Hunters' sunlight crossbow](https://oldschool.runescape.wiki/w/Hunters%27_sunlight_crossbow), [Iron crossbow](https://oldschool.runescape.wiki/w/Iron_crossbow), [Karil's crossbow](https://oldschool.runescape.wiki/w/Karil%27s_crossbow), [King's barrage](https://oldschool.runescape.wiki/w/King%27s_barrage).
- [Light ballista](https://oldschool.runescape.wiki/w/Light_ballista), [Light ballista (LMS)](https://oldschool.runescape.wiki/w/Light_ballista_(LMS)), [Mithril crossbow](https://oldschool.runescape.wiki/w/Mithril_crossbow), [Phoenix crossbow](https://oldschool.runescape.wiki/w/Phoenix_crossbow), [Rune crossbow](https://oldschool.runescape.wiki/w/Rune_crossbow), [Rune crossbow (LMS)](https://oldschool.runescape.wiki/w/Rune_crossbow_(LMS)), [Rune crossbow (or)](https://oldschool.runescape.wiki/w/Rune_crossbow_(or)), [Silvthrill ballista](https://oldschool.runescape.wiki/w/Silvthrill_ballista), [Steel crossbow](https://oldschool.runescape.wiki/w/Steel_crossbow), [Zaryte crossbow](https://oldschool.runescape.wiki/w/Zaryte_crossbow), [Zaryte crossbow (LMS)](https://oldschool.runescape.wiki/w/Zaryte_crossbow_(LMS)).

### Thrown weapons and blowpipes

- [Adamant dart](https://oldschool.runescape.wiki/w/Adamant_dart) (`p`, `p+`, `p++`), [Adamant knife](https://oldschool.runescape.wiki/w/Adamant_knife) (`p`, `p+`, `p++`), [Adamant thrownaxe](https://oldschool.runescape.wiki/w/Adamant_thrownaxe), [Amethyst dart](https://oldschool.runescape.wiki/w/Amethyst_dart) (`p`, `p+`, `p++`), [Black dart](https://oldschool.runescape.wiki/w/Black_dart) (`p`, `p+`, `p++`), [Black knife](https://oldschool.runescape.wiki/w/Black_knife) (`p`, `p+`, `p++`).
- [Blazing blowpipe](https://oldschool.runescape.wiki/w/Blazing_blowpipe), [Blisterwood stake](https://oldschool.runescape.wiki/w/Blisterwood_stake), [Bronze dart](https://oldschool.runescape.wiki/w/Bronze_dart) (`p`, `p+`, `p++`), [Bronze knife](https://oldschool.runescape.wiki/w/Bronze_knife) (`p`, `p+`, `p++`), [Bronze thrownaxe](https://oldschool.runescape.wiki/w/Bronze_thrownaxe), [Camphor blowpipe](https://oldschool.runescape.wiki/w/Camphor_blowpipe).
- [Dragon dart](https://oldschool.runescape.wiki/w/Dragon_dart) (`p`, `p+`, `p++`), [Dragon knife](https://oldschool.runescape.wiki/w/Dragon_knife) (`p`, `p+`, `p++`), [Dragon knife (LMS)](https://oldschool.runescape.wiki/w/Dragon_knife_(LMS)), [Dragon thrownaxe](https://oldschool.runescape.wiki/w/Dragon_thrownaxe), [Drygore blowpipe](https://oldschool.runescape.wiki/w/Drygore_blowpipe), [Holy water](https://oldschool.runescape.wiki/w/Holy_water), [Hunter's spear](https://oldschool.runescape.wiki/w/Hunter%27s_spear).
- [Iron dart](https://oldschool.runescape.wiki/w/Iron_dart) (`p`, `p+`, `p++`), [Iron knife](https://oldschool.runescape.wiki/w/Iron_knife) (`p`, `p+`, `p++`), [Iron thrownaxe](https://oldschool.runescape.wiki/w/Iron_thrownaxe), [Ironwood blowpipe](https://oldschool.runescape.wiki/w/Ironwood_blowpipe), [Mithril dart](https://oldschool.runescape.wiki/w/Mithril_dart) (`p`, `p+`, `p++`), [Mithril knife](https://oldschool.runescape.wiki/w/Mithril_knife) (`p`, `p+`, `p++`), [Mithril thrownaxe](https://oldschool.runescape.wiki/w/Mithril_thrownaxe).
- [Morrigan's javelin (bh)](https://oldschool.runescape.wiki/w/Morrigan%27s_javelin_(bh)), [Morrigan's javelin (Deadman)](https://oldschool.runescape.wiki/w/Morrigan%27s_javelin_(Deadman)), [Morrigan's javelin (LMS)](https://oldschool.runescape.wiki/w/Morrigan%27s_javelin_(LMS)), [Morrigan's throwing axe (bh)](https://oldschool.runescape.wiki/w/Morrigan%27s_throwing_axe_(bh)), [Morrigan's throwing axe (Deadman)](https://oldschool.runescape.wiki/w/Morrigan%27s_throwing_axe_(Deadman)), [Mud pie](https://oldschool.runescape.wiki/w/Mud_pie), [Rosewood blowpipe](https://oldschool.runescape.wiki/w/Rosewood_blowpipe).
- [Rune dart](https://oldschool.runescape.wiki/w/Rune_dart) (`p`, `p+`, `p++`), [Rune knife](https://oldschool.runescape.wiki/w/Rune_knife) (`p`, `p+`, `p++`), [Rune thrownaxe](https://oldschool.runescape.wiki/w/Rune_thrownaxe), [Sage's axe](https://oldschool.runescape.wiki/w/Sage%27s_axe), [Spine](https://oldschool.runescape.wiki/w/Spine), [Steel dart](https://oldschool.runescape.wiki/w/Steel_dart) (`p`, `p+`, `p++`), [Steel knife](https://oldschool.runescape.wiki/w/Steel_knife) (`p`, `p+`, `p++`), [Steel thrownaxe](https://oldschool.runescape.wiki/w/Steel_thrownaxe).
- [Toktz-xil-ul](https://oldschool.runescape.wiki/w/Toktz-xil-ul), [Tonalztics of ralos](https://oldschool.runescape.wiki/w/Tonalztics_of_ralos), [Toxic blowpipe](https://oldschool.runescape.wiki/w/Toxic_blowpipe).

## Code audit result

All weapons above have an osrs239 cache object and now reach a usable normal
PvM Ranged attack path. The audit fixed these family-level gaps:

| Area | Before | Audited implementation |
| --- | --- | --- |
| Bone/Dorgeshuun crossbow | Bone bolts had no projectile row; the generic gate wrongly allowed them in every crossbow | Bone bolts are exclusive to the Dorgeshuun crossbow; it also accepts only plain bronze/iron bolts and their poisoned forms, matching the [Wiki](https://oldschool.runescape.wiki/w/Dorgeshuun_crossbow) |
| Special launchers | Ballista javelins, bolt racks, kebbit/antler bolts, atlatl darts and salamander tar were rejected or lost their strength bonus | One shared compatibility predicate now drives both firing and Ranged Strength |
| Ogre bows | No complete ogre/brutal-arrow rule | Ogre bow and comp ogre bow use their [Wiki ammunition sets](https://oldschool.runescape.wiki/w/Brutal_arrows) |
| Self-ammunition bows | Gauntlet and Deadman starter bows incorrectly demanded quiver arrows and had no projectile | All six Gauntlet bows and both cached Starter bow forms use their generated/charge ammunition and cache projectile |
| Thrown families | Most thrownaxes, black/dragon variants, chinchompas, holy water, hunter spear, Morrigan items, Toktz-xil-ul, Spine and Blisterwood stake lacked projectile data | Projectile rows and poisoned-variant canonicalisation cover the full list |
| Chinchompas / holy water | Could be Ava-saved or dropped intact after use | Always consumed; normal single-target impact is functional |
| Mud pie | Cache category incorrectly routed it through melee | Explicitly routed to the thrown Ranged style and consumed |
| Tonalztics | Normal attacks could delete the equipped non-stackable weapon | Treated as returning/self-ammunition |
| Blowpipes | Loaded dart strength was omitted; Drygore could not load/unload darts | Loaded dart strength is included and Drygore shares the unpoisoned-dart loader without scales |
| Salamanders | Combat style resolver fell back to unarmed | Dedicated Accurate/Flare/Scorch style table; Flare consumes the matching tar |

The permanent `::gearrun` regression suite includes positive and negative
checks for Bone, conventional and Hunter crossbows, ballistae, Karil's
crossbow, Eclipse atlatl and salamanders, plus representative projectile rows.

`Blaster` and `Fixed device` appear on the broad Weapons/Categories table, but
not in the canonical Ranged weapons inventory: they do not perform ordinary
Ranged weapon damage. They are therefore deliberately not counted above.
