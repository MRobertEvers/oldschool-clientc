# Content gaps reviewers named (section 8)

Declare a content gap with a `-- GUIDE-GAP:` marker citing the `.rs2` line (trap 32); never drive
around one.

## Content gaps reviewers named in sonnet-b31

*Origin: section 8 ("Gaps reported by authors").*

CONTENT GAPS REVIEWERS NAMED IN sonnet-b31 (declare them, never drive around them): The Feud's
`buyDisguiseGear`/`createDisguise` (`feud_recruitment.rs2:218` hands over a FINISHED
`feud_desert_disguise` at the heist briefing; no headpiece-plus-beard combine exists),
`blackjackVillager` (Lure/Knock-out narrated, `feud_recruitment.rs2:292`) and `givenDungToHag`
(`feud_traitor.rs2:166`). Shadow of the Storm's Evil Dave clothing check, exit portal, recruit legs,
golem implement and second ritual were ported in seam26 (`shadowstorm_ritual.rs2`,
`golem_portal.rs2`).

## Ported in seam27, and the One Small Favour gaps named before it

*Origin: section 8 ("Gaps reported by authors").*

PORTED IN seam27 (The Feud's disguise combine, blackjack lesson and three Hag talks; One Small
Favour's eight landing lights, three-step vane, Horvik's bars/medicine/pigeon cages, Tassie's clay
and Aggie's stage; Mountain Daughter's lake crossings and dead trees; Ghosts Ahoy's stepping stones
and Energy Barrier), so the gaps below are history.

### Content gaps reviewers named (One Small Favour)

CONTENT GAPS REVIEWERS NAMED (One Small Favour, `quest_onesmallfavour/scripts/`; declare them, never
drive around them): `fixAllLamps` narrated by a mesbox that sets the eight light vars itself
(`onesmallfavour_relay.rs2:333`); `useHammerOnVane`/`searchVaneAgain` collapsed into one hammer
mesbox (`onesmallfavour_puzzles.rs2:114`); `talkToAggie` (`onesmallfavour_relay.rs2:20`); Tassie
hands an empty pot with no soft clay (`onesmallfavour_relay.rs2:52-59`); Horvik never takes the
pigeon cages and hands chicken cages for the salts instead
(`areas/varrock/scripts/horvik.rs2:20-33`, `talkToHorvikFinal`), and pigeons spawn only three at a
time.

## Dragon Slayer: the guild master's questions and the Oracle's magic door (sonnet-b36; FIXED seam31)

*Origin: the Dragon Slayer author and reviewer (batch sonnet-b36).*

- `guild_master.rs2:4` has no quest questions after stage 0. `returnToGuildmaster` (the guide asks
  him where the map pieces are) can only reach "What is this place?". Grade it as a GUIDE-GAP that
  cites the line, not as a PASS on a generic talk.
- `magic_door.rs2` has only `oploc1`. The door takes all four items (silk, lobster pot, unfired
  bowl, wizard's mind bomb) in one click, so the guide's four use-on steps (`useSilkOnDoor`,
  `usePotOnDoor`, `useUnfiredBowlOnDoor`, `useMindBombOnDoor`) are four GUIDE-GAPs.
  `helper_coverage` stays at CONTENT_GAP=4 until the door gets one `[oplocu]` per item.

FIXED seam31 (both bullets; sources Quest Helper DragonSlayer.java, 2009scape
GuildmasterDialogue.java / DSMagicDoorPlugin.java and the OSRS wiki brief
`docs/quests/dragon_slayer_i.md` s5/s8 -- LostCity has neither):

- The Guildmaster at stage 1 asks "Have you gone to talk to Oziach yet?". From stage 2 until the
  quest is done he offers "What is this place?" / "About my quest to kill the dragon...". Drive it as
  `chat.play{"npc:Greetings!", "options", "choose:About my quest to kill the dragon...",
  "player:About my quest"}`, then `chat.drain{stop_at="options"}` + `chat.choose` per topic. The
  route topic opens a second menu (Melzar / Thalzar / Lozar); Melzar's answer hands over a maze key
  only when you own none. Each topic raises its knowledge flag, never lowers it.
- The magic door takes one item per use, in any order: `t.player.use_on` each of `silk`,
  `lobster_pot`, `bowl_unfired`, `wizards_mind_bomb` ("You put <item> into the opening in the
  door."). The fourth opens it ("The door opens...", `%dragon_oracle` = 3) and walks you through.
  A plain Open answers "The door is locked." until then. Server varp `%dragon_door_items` holds the
  four bits.
