# Yama: the way in, the dialogues, the rewards

Sources: RSProx index 4498 (first contract [297..404], "In any case" [7143..7155]), 3268
[3995..4019], 798 [3384..3400], 3329 [1173..1190], 3371 [52]. A line or rate no recording
reaches comes from `docs/minigames/yama/sources/` (Transcript_Yama, Yama, Dossier,
Forgotten_lockbox) and is marked wiki below. Test: `test/bosses/yama_entry.lua`.

| item | R | ours | verdict |
|---|---|---|---|
| Voice op1, first time: 7 pages, menu Enter/Stay | 4498 [297..313], 3268 [3962..3973] | `yama_access.rs2`; test row voice.first | matched (text, order, mood family) |
| `chasm_voice_progress` 0 -> 2 on the menu | 4498 [308] | same | matched |
| Voice op1, repeat: "Proceed or begone" | 3371 [52] | same | matched |
| Voice op3 Travel, op4 Join + name prompt | 798 [1977], 4498 [516..531] | `~yama_domain_enter` / `_join` | written; not driven by a test |
| "Stay." | no recording | ends the talk | unverified |
| Throne first contract, option 1 | 4498 [343..404] | test row throne.first (42 pages) | matched |
| Throne options 2, 3 | no recording | wiki | moods unverified |
| Challenge before signing: short contract | 3268 [3997..4006] | same | matched |
| Backing out of the contract | no recording | wiki | moods unverified |
| Signed op1: greeting, "In any case", menu | 4498 [7143..7155] | test row throne.welcome (no-outcome branch) | matched; defeated branch's "Welcome" is wiki |
| Questions branch | no recording | wiki | moods unverified; hiscores interface NOT built |
| Contract on the throne | 798, 3329 [1174..1189] | owner 2026-11-09: contracts deferred; Yama refuses with the wiki's pre-launch line ("my scribes are a tad overwhelmed"); test row contract.refused | DEFERRED: the recorded flow is the spec when they are built |
| Greeting pools | 4498, 3268, 3322, 3329, 3368, 798, 799 | counts in `yama_access.rs2` | pool per outcome matches; the kill-count split (<=15 / more) is fitted from 49 win greetings |
| All 159 recorded Yama/Voice npc pages present verbatim | 10 sessions | mechanical check | 159/159 |
| Loot tile / despawn | 115/115 at 1503,10088, despawn 18000 | `yama_rewards.rs2` | matched |
| Share scaling | 46 worms of 90 at a 51% share, 4498 [4751] | `scale(damage, 2500, amount)` | one example |
| Drop rates, dossier, lockbox | too few recorded rolls | wiki tables | rates are the wiki's |
| Chain slide 83 Agility | 4498 [6614], every use the same | `chasm_of_fire.rs2` | matched; level gate and its line are wiki/generic |
| Chasm npc floors | Voice 0, black demons 0, greater 1, lesser 2 (900 adds) | m22_157.spawn was all +1 | FIXED, `gen_spawns.py` carries it |

Open:
- Contract fights and the contract dialogue (deferred by the owner, 2026-11-09).
- Yama's pizza dialogues (wiki only, need his eat animation).
- His hiscores interface on the record question.
- The Ironman wording in the contract questions.
- A Kingdom Divided is not enforced at the Voice: no recording shows a player without it.
