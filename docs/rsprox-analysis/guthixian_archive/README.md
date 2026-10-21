# The Guthixian Archive (rev 241, OSRS 2026-10-20) against RSProx

Sessions (both rev 241, world 329, recorder "Das Idle", not seasonal):

| session | what it holds |
|---|---|
| index 4293 | the Guthixian Temple teleport scroll [35], the temple's climbs, crawls and jump-downs [45..137], two skull noses [146], [152], the crawl into the Archive [161], the intro cutscene [173..324], the lobby [324..] |
| index 4294 | the lobby, spectating, two Archive Guardian fights (~[545..1043], ~[1099..1491]), Bork/Smork from ~[1491] |

Content: `OSRS-Content/osrs-content/server/scripts/areas/area_guthixian_archive/`
(`gt_temple.rs2` items 0 and 0b, `tfa_cutscene.rs2` item 2).
Test: `test/guthixian_archive/gt_entry.lua` (scriptrun; replays 4293's route and
its cutscene continues on the recorded ticks).

## How it was audited

Three timelines, aligned per section on an event both sides have (the
section's first player seq), never on the click:

- entity tells: `tools/rsprox/rsprox_tells.py <txt> --from --to --timeline`
  against `tools/rsprox/ticklog_tells.py <ticklog.tsv>`;
- everything the tick log does not carry (interfaces, clientscripts, messages,
  vars, camera, minimap, synths, music, the player's facing and exactmove):
  `TORIRS_SCRIPTRUN_WIRELOG=<file>` on the scriptrun (new, `src/scriptrun/
  scriptrun_core.c` `core_wirelog`) diffed by `tools/rsprox/wirelog_tells.py`
  (new). The wire log's tick is the tick log's minus one.
- the cutscene plays in an instance; ours is diffed with `--xlate 2304,-5440`
  (that run's instance origin against the template square 64,86).

Ignored on both sides as not this content (`--ignore` in the diff): the
side-panel and stone `if_setevents` re-sends, `account_info_update` (2498),
the orbs' regen syncs (4716/4717/4722), and the clocks (varbits 8354, 12391,
12392, varps 3078, 3079, 6405, 102, 456, 3883).

## Item 0: the teleport scroll (4293 [35..38])

| recorded | ours | verdict |
|---|---|---|
| C: `busy` 1, seq `teleport_scroll_open` 3864, `map_anim telescroll_teleport` 1039 on the tile, `sound_area` 200 `teleport_all` r10, the scroll deleted | same tick, same | matched |
| C+3: teleport to (4063,4557,0), seq -1, `sanity` 100, `busy` 0 | same | matched (`busy`/`sanity` were never transmitted before: varps 3077, 3939 now `transmit=yes`) |
| C+3: music 3605 The Sound of Guthix as `midi_song_withsecondary` 791+796 | `midi_song` 791 on the same tick (music table row 16199 added) | **approximate**: no secondary track in our engine |
| C+3: side panels re-sent, `toa_lobby` overlay closed, `cam_reset`, `minimap_toggle 0`, script 390 | none | the recorder's: leaving the ToA lobby, where the scroll was read |

## Item 0b: the temple to the Archive entrance (4293 [45..173])

| section | recorded | ours | verdict |
|---|---|---|---|
| wall climbs [45], [53] | L+1 face; L+2 `busy` 1 + `wgs_heroic_climb_success_one`; L+6 teleport up a plane onto the wall tile + `..._two` + exactmove (start, wall, 20, 45, angle 0); L+7 `busy` 0 | identical ticks; exactmove 20/45/0 on the same tiles | matched; Jagex's `[facing] angle=0` beside the exactmove is not sent (engine has no face-angle mask) |
| main temple door [62] | L+1 face (SW corner + size 3x2); L+2 `busy`, `luc2_crawl_into_skull`, fade out (948 0,255,0,0,50 + hp bar hidden); L+3 minimap 2; L+5 teleport (4061,4466,0) facing (4061,4462), seq -1; L+6 minimap 0, `busy` 0, fade in, music 792; L+8 overlay closed | identical ticks | matched; face sent as the loc's centre (engine); music one tick early (engine plays on the arrival tick; after the scroll Jagex does too) |
| wall-kit hole [72] (standing) | C+1 the same crawl | identical | matched |
| jump-downs [80], [134] | L+1 face; L+2 `busy`, `agility_shortcut_wall_jumpdown` delay 15 + synth `jump2` 2462 delay 15; L+3 down a plane on the trigger's tile, `..._jumpdown2`, `busy` 0 | identical | matched |
| skull noses [146], [152] | on arrival: mesbox "You don't have any reason to go crawling through there again." | same text, tick | matched (see the shared mesbox row below) |
| raid entry [161..173] | L+1 face; L+2 `busy`, `human_longcrawl`, synth `climbing_loop` 2454 x4 delay 4, fade out; L+3 minimap 2; L+4 the cutscene | identical | matched; needed `wgs_tfa_shortcut_unlocked` (varbit 16354) = 1 or the multiloc is the blocked cavity |

`wgs_tfa_shortcut_unlocked`: both sessions log in with it at 1 beside `wgs` 900;
neither shows it being set. **Unverified**: ours sets it from While Guthix
Sleeps' completion (login, the quest's end, `::complete`). Its varp (4234) is
now `perm`/`transmit=yes`, as the login dump shows.

## Item 2: the intro cutscene (4293 [171..327])

Continues are pressed on the recorded answer ticks (the transcript's own
resume ticks jitter by one). After the fixes, 19 lines differ, all listed:

| tick | recorded | ours | verdict |
|---|---|---|---|
| [173] | lock vars, ops hidden, cam shakes cleared, orbs/friends/account/music re-opened, side 0-6/11/12 closed, xp drops closed, `highlighting_off`, music 891 Into the Archive | same | matched (music: row 16470 added; the generator now reads the 241 `music` dbrows) |
| [173] | the three actors added facing the player's mark, spawnangle NW/SW/SW | same, `npc_setmode(none)` | matched (the draft's actors wandered) |
| [173] | `musicmulti_27` 4194308 | 4194304 + "You have unlocked a new music track" | the recorder already had bit 2 / the track |
| [173], [324] | - | varbit 4605 `multiway_indicator` 1, then 0 | **open (engine)**: every map instance counts as multi-way (`world_sync_multiway_indicator`); this one is not |
| [174..205] | four camera shots: `cam_moveto`/`cam_rotateto_coordinate`/`cam_moveto_cycles` (ease-in sine 500, ease-out sine 555), `cam_lookat` | same values, same ticks | matched |
| [205..212] | the player walks N4 then NE4, one tile a tick, run orb on | same, stepped a tile a tick | matched (the draft ran) |
| [214], [310] | cut pages: no continue, events -1..-1 none, continue text "" | same | matched |
| every page | echo `message_game` 114 "Name\|text" (cut at 200), `toplevel_chatbox_resetbackground` 2379, open, head, seq by rows, name, text wrapped with `<br>`, `if_settextalign` 16/28/20/16 by rows, events, "Click here to continue" | same | matched (shared `chat.rs2`, see below) except the continue's events: **open (engine)** ours 0..28, Jagex -1..-1 |
| [292] | Armacus `emote_laugh` with Eva's page | same tick | matched |
| [320..323] | Ivy SW,S,SW,W; Armacus bows [321] (the player turns to him, `[facing] angle=1688`); Eva SW,W; Armacus W; fade out [322]; minimap 2 [323] | same tiles and ticks; the player's turn is a `facesquare` | matched; the angle is **approximate** (no face-angle mask) |
| [324] | unlock, `tfa_miniquest` 2, teleport to (4135,5548), actors gone, orbs/xp drops/side 0-13 re-opened, `cam_unlock false`, `cam_reset`, `highlighting_on`, minimap 0 | same, but xp drops closed (our fixture has them off) and the journal's sub-tab differs | matched; `cam_unlock` **open (engine)**: no command/packet name for it |
| [325], [327] | fade in; `busy` 0 and the overlay closed | same | matched |
| [327] | music 891 re-sent; `tfa_lobby` overlay (975); pvp_icons layout | - | music: engine (same track, not re-sent); the overlay is item 1 |

Player facing on every page is a `facesquare` of the speaker's tile, as
recorded.

## Shared changes these sections needed

| change | evidence | scope |
|---|---|---|
| `~chat_layout` breaks pages with `<br>` at the wraps; line height then follows the rows (28 for 2, 20 for 3, 16 for 1/4) | 4293 [217] "...join the ranks`<br>`of the Alliance?" at 28; every page of [214..318] | every dialogue (`chat.rs2`) |
| each dialogue page: the echo (114), script 2379, then the page mounted; mesbox: echo (115), script 1508 "0" | 4293 [214..318], [147] | every dialogue |
| `mes_typed(int, string)` (opcode 11222) | the echoes above | engine |
| varps 3077 (`busy`), 3939 (`sanity`) transmit | 4293 [35], [38] and every `busy` edge | every content that sets `busy` |
| varps 4234, 5920 perm + transmit | 4293/4294 login dumps | this area |
| music squares 16199, 16197, 16470 | 4293 [38], [70], [173] | engine table |

## Open (named, not fixed)

- The continue/mesbox events range: our `if_addresumebutton` arms 0..28;
  Jagex arms a plain continue -1..-1. 101 call sites use it; changing the
  engine is a separate job.
- `[facing] type=reset` after an interaction, the face angle beside an
  exactmove, and the face coordinate for sized locs (Jagex: the SW corner with
  `sizex/sizez`, or a far point for the hole and the cavity): engine.
- `cam_unlock`, `midi_song_withsecondary`, `music_current_track`: engine.
- Instances are always multi-way (varbit 4605).
- The mesbox's close on a walk sends script 1508 in Jagex's: engine close path.
- `wgs_tfa_shortcut_unlocked`'s real trigger (unverified).
