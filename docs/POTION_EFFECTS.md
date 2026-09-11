# Potion effects — the complete inventory

Every drinkable potion the osrs239 cache ships, what it does, and where its
Drink handler lives. Built by sweeping `configs/all.obj` for every dose family
and diffing against the `[opheld1,...]` bindings in
`OSRS-Content/osrs239-content/server/scripts`, so it is a mechanical list, not a
remembered one. Re-run the sweep rather than trusting this file to stay current:

```sh
cd OSRS-Content/osrs239-content/server/scripts
grep -rhoE "^\[opheld[0-9],[a-z0-9_+]+\]" . | sed -E 's/^\[opheld[0-9],//;s/\]$//' | sort -u
```

Effect numbers are the OSRS wiki's, one link per family in the implementing
file's header. Where this tree deliberately differs — 2012 antifire durations,
un-implemented party sharing — the implementing file says so at the point of
difference; the deviations are collected at the bottom of this page.

## A. Standard vial potions

| Potion | Effect | Handler |
|---|---|---|
| Attack / Super attack | Attack +3+10% / +5+15% | `attack_potion.rs2` |
| Strength / Super strength | Strength +3+10% / +5+15% | `strength_potion.rs2` |
| Defence / Super defence | Defence +3+10% / +5+15% | `defence_potion.rs2` |
| Ranging | Ranged +3+10% | `ranging_potion.rs2` |
| Magic | Magic +4 | `magic_potion.rs2` |
| Combat | Attack and Strength +3+10% | `combat_potion.rs2` |
| Super combat | Attack, Strength, Defence +5+15% | `combat_potion.rs2` |
| Prayer | Prayer +7+25% | `prayer_potion.rs2` |
| Restore | five combat stats +10+30% | `restore_potion.rs2` |
| Super restore | every stat +8+25% | `prayer_potion.rs2` |
| Sanfew serum | five combat stats +4+30%, poison immunity, disease cure | `restore_potion.rs2` |
| Menaphite remedy | restore over time | `restore_potion.rs2` |
| Energy / Super energy / Extreme energy | run energy 10% / 20% / more | `energy_potion.rs2` |
| Stamina / Extended stamina | energy 20%/40% + the 70% drain cut | `inferno_potions.rs2`, `energy_potion.rs2` |
| Agility / Fishing / Super fishing / Hunter / Super hunter | the named skill +3 (supers higher) | `skill_potion.rs2` |
| Antipoison / Superantipoison | cure + immunity | `anti_poison.rs2` |
| Antidote+ / Antidote++ | cure + longer immunity | `venom_cure.rs2` |
| Anti-venom / + / Extended + | venom cure + immunity | `venom_cure.rs2` |
| Relicym's balm | cure disease | `misc_potion.rs2` |
| Serum 207 | shade/afflicted cure | `misc_potion.rs2` |
| Guthix rest | heal + poison | `misc_potion.rs2` |
| Goading / Surge / Magic essence / Battlemage | see file | `misc_potion.rs2` |
| Bastion / Divine bastion / Divine ranging / Prayer regeneration | see file | `inferno_potions.rs2` |
| Divine super attack/strength/defence/magic/battlemage/combat | boost + the divine hold | `divine_potion.rs2` |
| Saradomin brew | Hitpoints +2+15%, four combat stats drained | `sara_brew.rs2` |
| Zamorak brew / Ancient brew / Forgotten brew / Armadyl brew | see file | `god_brew.rs2` |
| Antifire / Super antifire | dragonfire countdown | `antifire_potion.rs2` |
| **Extended antifire** | 1200 ticks of the same countdown | `antifire_potion.rs2` |
| **Extended super antifire** | 1200 ticks of the stronger type | `antifire_potion.rs2` |
| **Moonlight potion** | Attack/Strength/Defence and Prayer, scaled by Herblore | `varlamore_potion.rs2` |
| **Haemostatic dressing** | bleed cure — see deviations | `varlamore_potion.rs2` |
| **Castlewars brew** | super combat + ranging + magic + stamina + super restore | `castlewars_brew.rs2` |

## B. Barbarian mixes

All 29 families, both the 2-dose and the 1-dose rung, in
`barbarian_mix.rs2`: attack, super attack, strength, super strength, defence,
super defence, magic, ranging, combat, prayer, restore, super restore, energy,
super energy, stamina, agility, fishing, hunting, magic essence, antipoison,
anti-poison supermix, antidote+, relicym's, antifire, extended antifire, super
antifire, extended super antifire, zamorak, ancient.

Each keeps its base potion's effect exactly and adds the wiki's flat heal —
**3 HP** for the roe-eligible mixes, **6 HP** for the caviar ones.

## C. Hunter's mixes

`hunter_mix.rs2`: ruby harvest (Attack +4+15%), black warlock (Strength),
sapphire glacialis (Defence), snowy knight (8 HP), sunlight moth (restore
+6+20% except Prayer, plus 8 HP), moonlight moth (22 Prayer).

## D. Chambers of Xeric

`cox_potion.rs2` — seven families times three tiers: elder, twisted, kodai,
overload, xeric's aid, revitalisation, prayer enhance, plus **antipoison**
(90 seconds / six minutes / nine minutes of immunity by tier).

## E. Tombs of Amascut supplies

`toa_supplies.rs2`: **nectar**, **tears of Elidinis**, **ambrosia**,
**silk dressing**, **blessed crystal scarab**, **liquid adrenaline**,
**smelling salts**.

`br_potion.rs2` covers the eight `br_` PvP-supply families the raid's own loot
table hands out — Saradomin brew, super restore, super combat, ranging,
stamina, sanfew serum, prayer, super energy.

## F. Nightmare Zone

`nightmarezone_potion.rs2`: **overload**, **absorption**, **super ranging**,
**super magic**. All four gated on `%nzone_active`.

## G. Deadman

`deadman_potion.rs2`: **starter combat potion**, **blighted overload**.

## H. Not drinkable — potion-shaped items with a different verb

These are potions by name and by Herblore recipe but have no Drink option, so
they are out of scope for this page and are handled (or not) elsewhere:

Guthix balance (used on frozen vampyres), Serum 208 (used on afflicted
villagers), compost potion (used on a compost bin), weapon poison / + / ++
(applied to a weapon), olive oil and sacred oil (pyre logs), holy water
(thrown), unfinished potions, empty and water-filled vials.

## Deviations from the wiki, all deliberate

* **Extended super antifire runs 1200 ticks, not OSRS's 600.** Inherited, not
  new: this tree deliberately runs the 2012 six-minute super antifire rather
  than OSRS's three-minute one, so OSRS's number would make an extended super
  antifire exactly as long as a plain one. The wiki's actual relationship —
  extended is twice the base it upgrades — is what is kept. See
  `player/configs/consumption/antifire.constant`.
* **Party sharing is not implemented** for the hunter's mixes or for tears of
  Elidinis. Nothing in this tree shares a consumable across a party.
* **Haemostatic dressing does not heal**, because no bleed status exists to
  cure; the wiki's own rule is that it heals 5 only when a bleed was stopped.
  The dose ladder is wired and the TODO marks the one line to add.
* **Moonlight potion is not restricted to Neypotzli.** That is an area rule
  belonging to the Neypotzli lane, and the potion is untradeable and made only
  from Neypotzli's own grub paste.
* **Nectar's drain is computed from the base level**, not the current one —
  `stat_drain`'s percentage is always of the base.
* **Sanfew serum has no timed disease immunity**, because `%disease` is a
  binary flag with no immunity window. Pre-existing, noted in
  `restore_potion.rs2`.
