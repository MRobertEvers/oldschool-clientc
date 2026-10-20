# Mount Quidamortem (the surface outside the Chambers of Xeric)

What Jagex's server sends on the summit, from RSProx, and how the content
follows it. Content: `areas/area_quidamortem/`, `minigames/minigame_cox/scripts/cox_party.rs2`,
`transport_lovakengj_minecart/`. Tests: `test/raids/cox_surface.lua` (one raider),
`test/raids/cox_party_board.lua` (two).

## Sources

| Session | Rev | World | What it shows |
|---|---|---|---|
| local 4284 (index id 4284, "rsprox local", 2026-10-19) | 241 | normal | Score board, Recruiting board (no channel), steps, the rev-241 entry |
| 820 | 234 | Grid Master | Make party with a channel member, Refresh, Scaling 50, layout Small, steps, the completion count |
| 843 | 234 | Grid Master | Scaling 0, layout, steps, completion count |
| 2528 | 229 | normal | Make party refused without a chat-channel, steps |
| 3282 | 234 | Grid Master | board, steps, completion count |
| 3285 | 234 | Grid Master | Captain Rimor's first line, the once-only entry warning, board, completion count |
| 3389 | 235 | Grid Master | Challenge Mode toggle, the Challenge Mode count |
| 1441, 1703 | 224, 226 | normal | the south Mountain Guide's tile and walk |

Sessions on the square: `POST /api/search {"conditions":[{"type":3,"query":"4919"}]}` (m19_55).
4284 is transcribed at `~/.rsprox/binary/archive/local/4284.txt`.

## Locs with an op (maps m18_55 m19_55 m19_54 m20_55)

| Loc | Where | Ops | Answer |
|---|---|---|---|
| `tox_bank_chest` 30087 | 1254,3572 | Use, Collect | the bank (RSProx: no anim, no sound); Collect = collection box |
| `bank_deposit_box` 10529 | 1252,3571 | Deposit | the deposit box (bound for every copy in the world) |
| `raids_storage_lobby` 30107 | 1256,3572 | Private | the raid's private store, drawn for outside (`varb5432 = 0`: "Withdraw all" / "Bank all") |
| `raids_challenge_scores` 32544 | 1224,3565; 1243,3563 | Read | interface 21 (below) |
| `raids_party_recruitment` 29776 | 1224,3562; 1240,3549; 1246,3562 | Read | the party list (below) |
| `raids_entrance_steps` 29777 | 1232,3573 | Enter | below |
| `lovakengj_minecart` 28835 | 1254,3548 | Travel | the minecart menu |
| `deadtree1` / `deadtree2_dark` | | Chop down | woodcutting (category `tree`) |

## Npcs

| Npc | RSProx | Content |
|---|---|---|
| Captain Rimor `raids_temple_captain` | 16 adds, all inside 1233..1235 x 3567..3569 | `wanderrange=1`; first line "Beware the Chambers of Xeric, adventurer." on `chatang1` (614, same seq in 239) |
| Stuliette `lovakengj_minecart_quidamortem` | 18 adds at 1256,3548, angle 512, never moves | `facing=west` |
| Mountain Guide (south) `raids_mountainguide_south_multi` | 17 adds in 1270..1272 x 3476..3478 | spawned at 1271,3477 (absent from the map dump), `wanderrange=1` |
| Mountain Guide (summit) | 1275,3560 once | spawn table's 1276,3559 kept |
| Gnosi, Duffy, Natural Historian | not recorded | visible only while Tale of the Righteous < 12 |

## The Recruiting board

Every press is a `RESUME_PAUSEBUTTON` on the interface's `contents` with the
cache's enum value as the sub id: `enum_1538` (list: 0 Refresh, 1 Make party /
My party) and `enum_1539` (details: 0 Join/Leave, 1 Advertise, 2 Disband,
3 Refresh, 4 Back, 5-7 preferred size / combat / total, 8 Scaling,
9 Challenge mode, 10 Map Layout). A list row is a resume on `list` with the row.

- Open (4284 [20], 3389 [2977]): `toplevel_mainmodal_open(-1,-1)`, `if_opensub 499`,
  40 x `raids_partylist_addline(row, "")`, then `contents` 0..1 and `list` 0..39
  armed `PAUSEBUTTON`.
- Make party (820 [1306]): `varp1427` set, `varb5428 = 1`, `varb5408` set, then
  507 with one `raids_partydetails_addline` per member
  (`name|combat|total|kc|levels...|`, the viewer's own name in `<col=ffffff>`)
  and `raids_party_panel("Raiding Party of <leader> (<n>)", role, listed, adverts)`
  (role 2 leader, 1 member, 0 other; adverts 15 for a leader, 0 otherwise).
- Without a chat-channel Jagex refuses: "You must be in a chat-channel to create
  a party." (2528 [342], 4284). **This world has no chat-channels**, so Make
  party always succeeds and the panel's Join adds the viewer directly.
- Scaling (820 [1311]): `meslayer_mode7("Set the number of players to scale the
  raid by (or 0 to clear it):")`, the answer into `varb9539` and `varb9541`. The
  raid scales to at least it (`^cox_var_party_n`). The prompts for the three
  preferences are not recorded; theirs are this tree's wording.
- Map Layout (820 [1314], 3285 [14421]): "How large of a layout to generate?"
  Small|Large|Full -> `varb5408` 0/1/2 and `varb3288` alike. Stored and drawn;
  the raid's layout generator does not take a size yet.
- Challenge mode (3389 [2992]): `varb6385 = 1`, `varb3288 = 3`, `varb5408 = 2`.
  Stored and drawn; the raid has no Challenge Mode yet.

## The steps

- The warning is once per account (3285 [14432]-[14435]): the mesbox
  "WARNING!<br>Items left in a dungeon <col=7f0000>cannot</col> be retrieved.<br>..."
  then "Accept warning and proceed." / "Cancel."; accepting sets `varb27`
  (on varp 1428, never transmitted). No later entry in any session shows it.
- Entry (4284 [79]-[81], all six others alike): T `fade_overlay` + `948(0,255,0,0,15)`
  and `hpbar_hud:hp` hidden; T+1 "<col=ef20ff>Inviting party...</col>", the
  teleport, synth 1952 (`down_stone_stairs`); T+2 `raids_overlay`, `raids_fadein`
  (1513), the side panel.
- A member is called when the leader goes down (820 [2324]): "<col=ef20ff>Your
  party has entered the dungeons! Come and join them now.</col>". A member's
  steps take them into the leader's raid until it is started. What Jagex says
  to a member whose leader is not down yet is not recorded ("Your party has not
  entered the Chambers of Xeric yet." is this tree's).
- The private store must be empty to enter: the store's own outside text
  (`raids_storage_private_prompt_1605.cs2`).

## The Score board (4284 [13])

`toplevel_mainmodal_open(-1,-1)`, `if_opensub 21`, `list` 1..13 armed `OP1`,
then `raids_score_display(2263, 7000, 2605, 2684, 2762, 2786, 2810, "names...")`:
the global best (22:37.80, the wiki's solo record), the bracket's target
(1:10:00), and the world's five best teams. The content sends the wiki's record
table and target times; this world has no Challenge Mode completions, so its
five rows are empty.

## Completion (3285 [16945]; 820, 843, 3282; 3389 for Challenge Mode)

A tick after "As the Great Olm collapses...": "Congratulations - your raid is
complete!<br>Team size: ... Duration: ..." then "Your completed Chambers of Xeric
count is: <col=ff0000>N</col>." with `varp1532` (Challenge Mode: varp 1735 and
its own line). The "(new personal best)" tail is not sent (no best is kept).

## Not done, and why

- **Captain Rimor's Set-difficulty**: no recording, no transcript, no cache
  varbit names it. Left unbound.
- **Claim-cape refusal** (fewer than 100 Challenge Mode raids): not transcribed;
  the line is this tree's.
- **The minecart's travel interface**: no session rides one; the chat menu
  stays (now shared by Lassin and Stuliette, and it says "You're already here,
  human." at the station you are at).
- **Mountain Guide travel**: no session takes a trip; it is a teleport to the
  wiki's destination pins.
- **Unclaimed chest loot to the storage unit** (wiki Storage unit): not wired.
