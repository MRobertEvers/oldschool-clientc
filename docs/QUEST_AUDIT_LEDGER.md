# Quest audit ledger

> Generated 2026-08-19 by cross-referencing the OSRS wiki's
> [Quests/Release dates](https://oldschool.runescape.wiki/w/Quests/Release_dates)
> page (raw wikitext) against `OSRS-Content/osrs239-content/server/scripts/quests/quest_*`.
> One row per quest/miniquest, in release-date order (oldest first). This is the
> durable state for the `/loop` quest-audit task in `docs/QUEST_AUDIT_PROMPT.md` --
> pick the first `unaudited` row each iteration, update status at start and end.
>
> Many `quest_*` directory names are LostCity-internal codenames that do not match
> the wiki title (e.g. `quest_zombiequeen` = Shilo Village, `quest_haunted` = Ernest
> the Chicken, `quest_misc` = Throne of Miscellania). Where the mapping was inferred
> from file/NPC/comment fingerprints rather than a direct name match, the note column
> says so -- re-confirm against the wiki page during that quest's audit iteration.
>
> Status values: `unaudited | in-progress | audited-clean | fixed | blocked:<reason>`.
> `blocked:not-ported` rows have no content dir; do not implement them in this loop,
> only audit what already exists.

| Release date | Quest | Content dir | Status | Date | Note |
|---|---|---|---|---|---|
| 04 Jan 2001 | Cook's Assistant | `quest_cook` | in-progress | 2026-08-19 |  |
| 04 Jan 2001 | Demon Slayer | `quest_demon` | unaudited | |  |
| 04 Jan 2001 | The Restless Ghost | `quest_priest` | unaudited | |  |
| 04 Jan 2001 | Romeo & Juliet | `quest_romeojuliet` | unaudited | |  |
| 04 Jan 2001 | Sheep Shearer | `quest_sheep` | unaudited | |  |
| 04 Jan 2001 | Shield of Arrav | `quest_blackarmgang` | unaudited | |  |
| 21 Jan 2001 | Ernest the Chicken | `quest_haunted` | unaudited | | dir=quest_haunted; matched to Ernest the Chicken via piranha/fountain/pressure-gauge puzzle fingerprint, not a direct name match -- confirm at audit time |
| 28 Jan 2001 | Vampyre Slayer | `quest_vampire` | unaudited | |  |
| 16 Feb 2001 | Imp Catcher | `quest_imp` | unaudited | |  |
| 28 Feb 2001 | Prince Ali Rescue | `quest_prince` | unaudited | |  |
| 06 Apr 2001 | Doric's Quest | `quest_doric` | unaudited | |  |
| 06 Apr 2001 | Black Knights' Fortress | `quest_blackknight` | unaudited | |  |
| 06 Apr 2001 | Witch's Potion | `quest_hetty` | unaudited | |  |
| 06 Apr 2001 | The Knight's Sword | `quest_squire` | unaudited | |  |
| 08 May 2001 | Goblin Diplomacy | `quest_gobdip` | unaudited | |  |
| 11 Jun 2001 | Pirate's Treasure | `quest_hunt` | unaudited | | dir=quest_hunt; matched to Pirate's Treasure via Redbeard Frank + constant comment |
| 23 Sep 2001 | Dragon Slayer I | `quest_dragon` | unaudited | |  |
| 27 Feb 2002 | Druidic Ritual | `quest_druid` | unaudited | |  |
| 27 Feb 2002 | Lost City | `quest_zanaris` | unaudited | |  |
| 27 Feb 2002 | Witch's House | `quest_ball` | unaudited | |  |
| 27 Feb 2002 | Merlin's Crystal | `quest_arthur` | unaudited | |  |
| 27 Feb 2002 | Heroes' Quest | `quest_hero` | unaudited | |  |
| 25 Mar 2002 | Scorpion Catcher | `quest_scorpcatcher` | unaudited | |  |
| 09 Apr 2002 | Family Crest | `quest_crest` | unaudited | |  |
| 30 Apr 2002 | Tribal Totem | `quest_totem` | unaudited | |  |
| 28 May 2002 | Fishing Contest | `quest_fishingcompo` | unaudited | |  |
| 28 May 2002 | Monk's Friend | `quest_drunkmonk` | unaudited | |  |
| 17 Jun 2002 | Temple of Ikov | `quest_ikov` | unaudited | |  |
| 17 Jun 2002 | Clock Tower | `quest_cog` | unaudited | |  |
| 23 Jul 2002 | Holy Grail | `quest_grail` | unaudited | |  |
| 23 Jul 2002 | Tree Gnome Village | `quest_tree` | unaudited | |  |
| 23 Jul 2002 | Fight Arena | `quest_arena` | unaudited | |  |
| 15 Aug 2002 | Hazeel Cult | `quest_hazeelcult` | unaudited | |  |
| 15 Aug 2002 | Sheep Herder | `quest_sheepherder` | unaudited | |  |
| 27 Aug 2002 | Plague City | `quest_elena` | unaudited | | dir=quest_elena; matched to Plague City via Elena/mourner/plaguehouse fingerprint (Elena also appears in Biohazard/Mourning's End I/Song of the Elves) -- confirm scope at audit time |
| 09 Sep 2002 | Sea Slug | `quest_seaslug` | unaudited | |  |
| 24 Sep 2002 | Waterfall Quest | `quest_waterfall` | unaudited | |  |
| 23 Oct 2002 | Biohazard | `quest_biohazard` | unaudited | |  |
| 23 Oct 2002 | Jungle Potion | `quest_junglepotion` | unaudited | |  |
| 12 Dec 2002 | The Grand Tree | `quest_grandtree` | unaudited | |  |
| 27 Jan 2003 | Shilo Village | `quest_zombiequeen` | unaudited | | dir=quest_zombiequeen; matched to Shilo Village (Rashiliyia/Ah Za Rhoon), NOT Shades of Mort'ton (that's quest_mortton, separate dir/quest) |
| 03 Mar 2003 | Underground Pass | `quest_upass` | unaudited | |  |
| 17 Mar 2003 | Observatory Quest | `quest_itgronigen` | unaudited | | dir=quest_itgronigen; matched to Observatory Quest (Gronigen redirects to Observatory professor) |
| 14 Apr 2003 | The Tourist Trap | `quest_desertrescue` | unaudited | |  |
| 07 May 2003 | Watchtower | `quest_itwatchtower` | unaudited | |  |
| 27 May 2003 | Dwarf Cannon | `quest_mcannon` | unaudited | |  |
| 09 Jun 2003 | Murder Mystery | `quest_murder` | unaudited | |  |
| 09 Jul 2003 | The Dig Site | `quest_itexam` | unaudited | |  |
| 28 Jul 2003 | Gertrude's Cat | `quest_fluffs` | unaudited | |  |
| 20 Aug 2003 | Legends' Quest | `quest_legends` | unaudited | |  |
| 01 Dec 2003 | Rune Mysteries | `quest_runemysteries` | unaudited | |  |
| 18 May 2004 | Big Chompy Bird Hunting | `quest_chompybird` | unaudited | |  |
| 02 Jun 2004 | Elemental Workshop I | `quest_elemental_workshop` | unaudited | |  |
| 29 Jun 2004 | Priest in Peril | `quest_priestperil` | unaudited | |  |
| 13 Jul 2004 | Nature Spirit | `quest_druidspirit` | unaudited | |  |
| 09 Aug 2004 | Death Plateau | `quest_death` | unaudited | |  |
| 24 Aug 2004 | Troll Stronghold | `quest_troll` | unaudited | |  |
| 14 Sep 2004 | Tai Bwo Wannai Trio | `quest_tbwt` | unaudited | |  |
| 20 Sep 2004 | Regicide | `quest_regicide` | unaudited | |  |
| 05 Oct 2004 | Eadgar's Ruse | `quest_eadgar` | unaudited | |  |
| 18 Oct 2004 | Shades of Mort'ton | `quest_mortton` | unaudited | |  |
| 02 Nov 2004 | The Fremennik Trials | `quest_viking` | unaudited | | dir=quest_viking; matched to The Fremennik Trials via constant comment |
| 17 Nov 2004 | Horror from the Deep | `quest_horror` | unaudited | |  |
| 29 Nov 2004 | Throne of Miscellania | `quest_misc` | unaudited | |  |
| 06 Dec 2004 | Monkey Madness I | `quest_mm` | unaudited | |  |
| 21 Dec 2004 | Haunted Mine | `quest_hauntedmine` | unaudited | |  |
| 05 Jan 2005 | Troll Romance | `quest_troll_love` | unaudited | |  |
| 10 Jan 2005 | In Search of the Myreque | `quest_routequest` | unaudited | | dir=quest_routequest; matched to In Search of the Myreque via Cyreg Paddlehorn NPC |
| 31 Jan 2005 | Creature of Fenkenstrain | `quest_fenkenstrain` | unaudited | |  |
| 07 Feb 2005 | Roving Elves | `quest_rovingelves` | unaudited | |  |
| 15 Feb 2005 | Ghosts Ahoy | `quest_ghostsahoy` | unaudited | |  |
| 28 Feb 2005 | One Small Favour | `quest_onesmallfavour` | unaudited | |  |
| 07 Mar 2005 | Mountain Daughter | `quest_mountaindaughter` | unaudited | |  |
| 21 Mar 2005 | Between a Rock... | `quest_betweenarock` | unaudited | |  |
| 04 Apr 2005 | The Feud | `quest_thefeud` | unaudited | |  |
| 11 Apr 2005 | The Golem | `quest_golem` | unaudited | |  |
| 18 Apr 2005 | Desert Treasure I | `quest_deserttreasure` | unaudited | |  |
| 26 Apr 2005 | Icthlarin's Little Helper | `quest_icthlarin` | unaudited | |  |
| 04 May 2005 | Tears of Guthix | `quest_tearsofguthix` | unaudited | |  |
| 17 May 2005 | Zogre Flesh Eaters | `quest_zogreflesheaters` | unaudited | |  |
| 31 May 2005 | The Lost Tribe | `quest_losttribe` | unaudited | |  |
| 31 May 2005 | The Giant Dwarf | `quest_giantdwarf` | unaudited | |  |
| 13 Jun 2005 | Enter the Abyss | `quest_entertheabyss` | unaudited | |  |
| 27 Jun 2005 | Recruitment Drive | `quest_recruitmentdrive` | unaudited | |  |
| 19 Jul 2005 | Mourning's End Part I | `quest_mourningsendparti` | unaudited | |  |
| 26 Jul 2005 | Forgettable Tale... | `quest_forgettabletale` | unaudited | |  |
| 30 Aug 2005 | Garden of Tranquillity | `quest_gardenoftranquility` | unaudited | |  |
| 26 Sep 2005 | A Tail of Two Cats | `quest_atailoftwocats` | unaudited | |  |
| 17 Oct 2005 | Wanted! | `quest_wanted` | unaudited | |  |
| 17 Oct 2005 | Mourning's End Part II | `quest_mourningsendpartii` | unaudited | |  |
| 31 Oct 2005 | Rum Deal | `quest_rumdeal` | unaudited | |  |
| 14 Nov 2005 | Shadow of the Storm | `quest_shadowstorm` | unaudited | |  |
| 22 Nov 2005 | Making History | `quest_makinghistory` | unaudited | |  |
| 28 Nov 2005 | Ratcatchers | `quest_ratcatchers` | unaudited | |  |
| 05 Dec 2005 | Spirits of the Elid | `quest_spiritsoftheelid` | unaudited | |  |
| 19 Dec 2005 | Devious Minds | `quest_deviousminds` | unaudited | |  |
| 10 Jan 2006 | The Hand in the Sand | `quest_handinthesand` | unaudited | |  |
| 23 Jan 2006 | Enakhra's Lament | `quest_enakhraslament` | unaudited | |  |
| 07 Feb 2006 | Cabin Fever | `quest_cabinfever` | unaudited | |  |
| 27 Feb 2006 | Fairytale I - Growing Pains | -- | blocked:not-ported | 2026-08-19 | no `quest_*` dir found |
| 15 Mar 2006 | Recipe for Disaster | `quest_recipefordisaster` | unaudited | |  |
| 22 Mar 2006 | In Aid of the Myreque | `quest_inaidofthemyreque` | unaudited | |  |
| 03 Apr 2006 | A Soul's Bane | `quest_soulsbane` | unaudited | |  |
| 10 Apr 2006 | Rag and Bone Man I | `quest_ragandboneman` | unaudited | |  |
| 02 May 2006 | Swan Song | `quest_swansong` | unaudited | |  |
| 22 May 2006 | Royal Trouble | `quest_royaltrouble` | unaudited | |  |
| 21 Jun 2006 | Death to the Dorgeshuun | `quest_deathtothedorgeshuun` | unaudited | |  |
| 11 Jul 2006 | Fairytale II - Cure a Queen | -- | blocked:not-ported | 2026-08-19 | no `quest_*` dir found |
| 24 Jul 2006 | Lunar Diplomacy | `quest_lunardiplomacy` | unaudited | |  |
| 15 Aug 2006 | The Eyes of Glouphrie | `quest_theeyesofglouphrie` | unaudited | |  |
| 04 Sep 2006 | Darkness of Hallowvale | `quest_darknessofhallowvale` | unaudited | |  |
| 20 Sep 2006 | The Slug Menace | `quest_theslugmenace` | unaudited | |  |
| 02 Oct 2006 | Elemental Workshop II | `quest_elementalworkshopii` | unaudited | |  |
| 23 Oct 2006 | My Arm's Big Adventure | `quest_myarmsbigadventure` | unaudited | |  |
| 06 Nov 2006 | Enlightened Journey | `quest_enlightenedjourney` | unaudited | |  |
| 28 Nov 2006 | Eagles' Peak | `quest_eaglepeak` | unaudited | |  |
| 12 Dec 2006 | Animal Magnetism | `quest_animalmagnetism` | unaudited | |  |
| 10 Jan 2007 | Contact! | `quest_contact` | unaudited | |  |
| 29 Jan 2007 | Cold War | `quest_coldwar` | unaudited | |  |
| 06 Feb 2007 | The Fremennik Isles | `quest_thefremennikisles` | unaudited | |  |
| 19 Feb 2007 | Tower of Life | `quest_toweroflife` | unaudited | |  |
| 06 Mar 2007 | The Great Brain Robbery | `quest_thegreatbrainrobbery` | unaudited | |  |
| 27 Mar 2007 | What Lies Below | `quest_whatliesbelow` | unaudited | |  |
| 10 Apr 2007 | Olaf's Quest | `quest_olafsquest` | unaudited | |  |
| 24 Apr 2007 | Another Slice of H.A.M. | `quest_anothersliceofham` | unaudited | |  |
| 15 May 2007 | Dream Mentor | `quest_dreammentor` | unaudited | |  |
| 04 Jun 2007 | Grim Tales | `quest_grimtales` | unaudited | |  |
| 24 Jul 2007 | King's Ransom | `quest_kingsransom` | unaudited | |  |
| 06 May 2016 | Monkey Madness II | `quest_monkeymadnessii` | unaudited | |  |
| 19 May 2016 | Bear Your Soul | `quest_bearyoursoul` | unaudited | |  |
| 26 Jan 2017 | Misthalin Mystery | `quest_misthalinmystery` | unaudited | |  |
| 20 Apr 2017 | Client of Kourend | `quest_clientofkourend` | unaudited | |  |
| 17 Aug 2017 | Rag and Bone Man II | -- | blocked:not-ported | 2026-08-19 | no `quest_*` dir found |
| 07 Sep 2017 | Bone Voyage | `quest_bonevoyage` | unaudited | |  |
| 09 Nov 2017 | The Queen of Thieves | `quest_queenofthieves` | unaudited | |  |
| 09 Nov 2017 | The Depths of Despair | `quest_depthsofdespair` | unaudited | |  |
| 07 Dec 2017 | The Corsair Curse | `quest_corsaircurse` | unaudited | |  |
| 04 Jan 2018 | Dragon Slayer II | `quest_dragonslayer2` | unaudited | |  |
| 19 Apr 2018 | Tale of the Righteous | `quest_taleoftherighteous` | unaudited | |  |
| 24 May 2018 | A Taste of Hope | `quest_tasteofhope` | unaudited | |  |
| 06 Sep 2018 | Making Friends with My Arm | `quest_makingfriendswithmyarm` | unaudited | |  |
| 10 Jan 2019 | The Forsaken Tower | `quest_forsakentower` | unaudited | |  |
| 10 Jan 2019 | The Ascent of Arceuus | `quest_ascentofarceuus` | unaudited | |  |
| 07 Feb 2019 | X Marks the Spot | `quest_xmarksthespot` | unaudited | |  |
| 04 Jul 2019 | In Search of Knowledge | `quest_insearchofknowledge` | unaudited | |  |
| 25 Jul 2019 | Song of the Elves | `quest_songoftheelves` | unaudited | |  |
| 26 Sep 2019 | The Fremennik Exiles | `quest_fremennikexiles` | unaudited | |  |
| 04 Jun 2020 | Sins of the Father | `quest_sinsofthefather` | unaudited | |  |
| 10 Sep 2020 | A Porcine of Interest | `quest_porcineofinterest` | unaudited | |  |
| 25 Nov 2020 | Getting Ahead | `quest_gettingahead` | unaudited | |  |
| 14 Apr 2021 | Below Ice Mountain | `quest_belowicemountain` | unaudited | |  |
| 03 Jun 2021 | A Night at the Theatre | `quest_nightatthetheatre` | unaudited | |  |
| 16 Jun 2021 | A Kingdom Divided | `quest_kingdomdivided` | unaudited | |  |
| 09 Feb 2022 | Land of the Goblins | `quest_landofthegoblins` | unaudited | |  |
| 23 Mar 2022 | Temple of the Eye | `quest_templeoftheeye` | unaudited | |  |
| 27 Apr 2022 | Beneath Cursed Sands | `quest_beneathcursedsands` | unaudited | |  |
| 08 Jun 2022 | Sleeping Giants | `quest_sleepinggiants` | unaudited | |  |
| 30 Nov 2022 | The Garden of Death | `quest_gardenofdeath` | unaudited | |  |
| 11 Jan 2023 | Secrets of the North | `quest_secretsofthenorth` | unaudited | |  |
| 26 Jul 2023 | Desert Treasure II - The Fallen Empire | `quest_deserttreasureii` | unaudited | |  |
| 13 Sep 2023 | The Path of Glouphrie | `quest_pathofglouphrie` | unaudited | |  |
| 10 Jan 2024 | Children of the Sun | `quest_childrenofthesun` | unaudited | |  |
| 21 Feb 2024 | Defender of Varrock | `quest_defenderofvarrock` | unaudited | |  |
| 20 Mar 2024 | Twilight's Promise | `quest_twilightspromise` | unaudited | |  |
| 20 Mar 2024 | Perilous Moons | `quest_perilousmoons` | unaudited | |  |
| 20 Mar 2024 | At First Light | `quest_atfirstlight` | unaudited | |  |
| 20 Mar 2024 | The Ribbiting Tale of a Lily Pad Labour Dispute | `quest_ribbitingtale` | unaudited | |  |
| 10 Jul 2024 | While Guthix Sleeps | `quest_whileguthixsleeps` | unaudited | |  |
| 25 Sep 2024 | The Heart of Darkness | `quest_heartofdarkness` | unaudited | |  |
| 25 Sep 2024 | Death on the Isle | `quest_deathontheisle` | unaudited | |  |
| 25 Sep 2024 | Meat and Greet | `quest_meatandgreet` | unaudited | |  |
| 25 Sep 2024 | Ethically Acquired Antiquities | `quest_ethicallyacquiredantiquities` | unaudited | |  |
| 06 Nov 2024 | The Curse of Arrav | `quest_curseofarrav` | unaudited | |  |
| 23 Jul 2025 | The Final Dawn | `quest_finaldawn` | unaudited | |  |
| 23 Jul 2025 | Scrambled! | `quest_scrambled` | unaudited | |  |
| 23 Jul 2025 | Shadows of Custodia | `quest_shadowsofcustodia` | unaudited | |  |
| 22 Oct 2025 | Learning the Ropes | -- | blocked:not-ported | 2026-08-19 | no `quest_*` dir found |
| 19 Nov 2025 | Pandemonium | `quest_pandemonium` | unaudited | |  |
| 19 Nov 2025 | Prying Times | `quest_pryingtimes` | unaudited | |  |
| 19 Nov 2025 | Current Affairs | `quest_currentaffairs` | unaudited | |  |
| 19 Nov 2025 | Troubled Tortugans | `quest_troubledtortugans` | unaudited | |  |
| 25 Feb 2026 | The Ides of Milk | `quest_idesofmilk` | unaudited | |  |
| 20 May 2026 | The Red Reef | `quest_redreef` | unaudited | |  |
| 30 Jun 2026 | The Blood Moon Rises | -- | blocked:not-ported | 2026-08-19 | no `quest_*` dir found |
| 29 Jul 2026 | Fallen From Grace | -- | blocked:not-ported | 2026-08-19 | no `quest_*` dir found |
