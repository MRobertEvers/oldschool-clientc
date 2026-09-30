# Devious Minds -- what the ladder cannot know
(paths: S = OSRS-Content/osrs239-content/server/scripts, DM = S/quests/quest_deviousminds/scripts)

Positions
- Hooded monk 3406,3492 (ladder says 3494); dead monk 3405,3492. Multinpc: %devious_monk 0 hooded, 1 dead.
- Whetstone loc at 2953,3451 is INSIDE Doric's hut: stand on 2952,3451 first, else "I can't reach that".
- Altar 2853,3349 in the Entrana church; stand at 2854,3347. High Priest 2851,3349.
- Sir Tiffy = rd_teleporter_guy 2997,3373 (Falador Park).

Gating
- Offer dialogue has Yes/No; only Yes starts the quest. A skill warning shows when Smithing 65 / Runecraft 50 / Fletching 50 are missing (boosts count except Runecraft, stat_base).
- Whetstone use asks "Grind down the blade...?" -> choose Yes. Sword must be mithril_2h_sword.
- Orb goes only into a large or colossal pouch; small/medium/giant give the transcript refusals.
- Bare orb on the altar only warns; the pouch starts the heist at stage 30.
- Tiffy talks Devious Minds only at stage 70 and only after Recruitment Drive ("::complete quest_recruitmentdrive" in setup, before bind).
- Priest hub: greeting first, then the stage branch (40 surprise, 50 "Not yet.", 60 report).

Cutscene / fight
- Heist is a scripted cutscene, no fight. DM/deviousminds_items.rs2:172-. Temp npcs: relic monk, 2 monks, assassin; deleted at the end.
- Large pouch is destroyed, colossal survives.
- No wanderers on the route. Abyss/Entrana travel is not quest-gated.

Implemented differently from the guide
- Ladder steps 5-8 (Abyss trip) are not required: the pouch can be used on the altar after taking the boat; travel is not enforced.
- Stage 40 (cutscene done) and the priest "surprise" talk are not ladder steps; S/areas/entrana/scripts/high_priest_of_entrana.rs2:37-64.
- Dead monk click: DM/deviousminds_monk.rs2:135; stage 50 -> 60.
- Tiffy splice: S/quests/quest_recruitmentdrive/scripts/recruitmentdrive.rs2:99.
- Camera and spawn offsets are approximate; the black-screen fade is not done.
- Rewards: DM/deviousminds_tiffy.rs2:38 (6500 Smithing, 5000 Runecraft, 5000 Fletching, 1 QP).
- Debug reset: ::deviousminds (DM/deviousminds_debug.rs2:4).
