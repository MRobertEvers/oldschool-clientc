# Twilight's Promise (wiki-pinned brief)

Pinned 2026-10-03 for parity b56. Not a LostCity quest (2024). Quest dir
`OSRS-Content/osrs239-content/server/scripts/quests/quest_twilightspromise/`, progress var
`varb9649_vmq2` (carrier `varp4076_vmq2_primary`, bits 0-6), complete = 50.

## Sources (wiki oldids, fetched through api.php)

- Twilight's_Promise 15356498 (walkthrough, rewards, requirements)
- Transcript:Twilight's_Promise 15319500 (every line the port speaks)
- Knight_of_Varlamore_(Twilight's_Promise) 15200510 (Mezan: lvl 81, 100 hp, 70/70/40, mage 20, slash, speed 5, max 8)
- Cultist 15326762 (lvl 34, 25 hp, 30/35/30, stab, speed 4, max 4, no drops)
- Citizen_(Twilight's_Promise) 15031710 (Thieving 1, 8 xp, 3 coins, the stolen amulet) and its transcript 15031704
- Transcript:Incriminating_letter 14599682, Crate 14615444, Chest 14615453
- Quest Helper `helpers/quests/twilightspromise/TwilightsPromise.java` (steps.put 0..48)

## Stage ladder

| stage | meaning | writer |
|---|---|---|
| 0, 2 | Regulus (Varrock) flies the player to Fortis; the twins offer the quest | Regulus, twins |
| 4 | offer taken; Metzli waits in the temple | twins |
| 6, 8 | Metzli met (6); asked for the prince (8) which opens the crypt gate | Metzli |
| 10, 12 | the prince's talk begun (10); done (12) | Itzla |
| 14-20 | crest given (14), then +2 per knight group finished (bazaar, cothon, pub, colosseum) | twins, knights |
| 22 | all four found; the twins send the player to the Kualti HQ (24) | twins |
| 24, 26 | search the HQ; the letter is in the backpack (26) | chest |
| 28 | the letter read | letter |
| 30, 32, 34 | the twins judge Velam (30 begun, 32 killed, 34 sent to Regulus) | twins |
| 36 | Regulus has given the feed | Regulus |
| 38 | Renu fed; the Quetzal Transport System opens; fly to the Teomat | Renu |
| 40, 42 | the prince's report (40 begun, 42 done); Metzli next | Itzla |
| 44 | Metzli's talk ends with the attack; the eight cultists | Metzli, battle |
| 46, 48 | the eighth cultist falls (46); the aftermath scene (48); the prince's thanks completes | Itzla |
| 50 | 1 quest point, 3,000 Thieving XP, Civitas illa Fortis Teleport, the Quetzal Transport System | Itzla |

Knight-group varbits (0 untouched, 1 spoken to, 2 task done, 3 finished): `varb9829`
bazaar (2 = amulet pickpocketed), `varb9830` cothon (2 = crate found), `varb9831` pub
(2 = Azali sober), `varb9832` colosseum (2 = Mezan beaten). `varb9834` is Azali's form
(1 pub, 2 following, 3 at the fountain). `varb9833` crest given, `varb9651` letter read.

## Legs the guide asks for

Pickpocket the citizen in blue (bazaar, 1686,3109) for the stolen amulet; search the one
Cothon crate at 1778,3149 (beside two fish barrels) for "The Fortis Spark"; lead the drunk
knight (Azali) east to the fountain at 1757,3069, talking to her again when she stops;
beat Mezan in the Colosseum with two combat styles (he prays against a style hit four
times running); climb the Kualti HQ stairs (1638,3155 and 1650,3155) to the top floor and
search the chest in the south-east room (equal chance of either of two chests) and READ
the letter; feed Renu and fly to the Teomat; fight eight cultists (one attacks, the rest
only when hit).

## Not ported (cutscenes, specced elsewhere)

Three scenes (TRIAGE.tsv rows 1-3): Azali's dunk at the fountain, Velam's judgement,
the Teomat aftermath. Their dialogue is in the port as plain chat.
