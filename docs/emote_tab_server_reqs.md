# The Emotes tab: what's already landed

> Companion to `docs/questlist_chatmenu_levelup.md`, same discovery pass.
> **Inverted result, worth stating up front: this one is already landed and
> selftested.** Unlike every other interface in this survey series so far,
> the only real gap is a content one (nothing sets the unlock bits yet — a
> deliberate non-goal, not an oversight), and LostCity has a genuine
> precedent here (rev 254, 2004, already has player emotes) rather than the
> "no precedent" finding common to the modern chrome panels.

## 0. Status at a glance

| piece | status |
|---|---|
| grid layout / icon rendering / tooltips | client-only, needs nothing |
| click → animation → broadcast to nearby players | **landed + selftested** |
| unlock-gating icon swap | client-only, reads a real bitset varp |
| unlock bitset transmit + who sets the bits | **content gap** — declared but unconfigured, nothing writes it |

---

## 1. `emote.if` (216) and the click path

6 components: `universe`, `scrollable`, `contents`, `overlay`, `scrollbar`,
`tooltip`. `[contents]` is a scrollable grid the onload script
(`script_699.cs2`, `[clientscript,emote_init]`) fills with `cc_create` cells
for indices 0..55, 4 columns wide, 48×48px at 49px pitch. Cells are dynamic
children with the **emote index as sub-id** — the same wire shape
`docs/questlist_chatmenu_levelup.md` §2.3 found for `chatmenu`: one
`IF_BUTTON1` on `emote:contents`, sub-id = clicked index, not `last_com`.

Per-cell configuration (`script_701.cs2`, `[proc,emote_update]`) sets the
icon, tooltip text, and op1/op2 labels ("Perform"/"Loop") — **unconditionally**;
`emote_checkunlocked` (`script_702.cs2`) only swaps which icon is drawn
(unlocked vs. greyed), it does not gate the click itself. A real refusal has
to happen server-side, same as any RuneScape server — and it does (§3).

## 2. Unlock gating — a real bitset, confirmed

`script_702.cs2` is a per-index `switch_int` over **one basevar shared by
every gated emote**: `varp313` = `emote_access`
(`configs/all.varp.compack:356`), with 24 named varbits packed into it
(`basevar=emote_access` appears 24 times in `configs/all.varbit`) —
`emote_glassbox`, `emote_lean`, `emote_maxcape`, `emote_musiccape`,
`emote_party`, etc. Indices 0-21 have no case and fall through to
`return(1)` — always unlocked (the base emotes). This is exactly the same
varp name and shape LostCity uses (§4) — the cache carries LostCity's bitset
design forward, just renamed per-bit.

No dbtable backs any of this — icon/label/unlock-check data is enum-driven
(`enum_1000`/`1001`/`1002`/`4998`/`4999`/`681`), and the animation id itself
appears **nowhere in the cache**, as expected: it's server knowledge only.

## 3. Already landed, end to end

`OSRS-Content/osrs239-content/server/scripts/interface_emote/` exists and is
wired:

```
[if_button,emote:contents]
~emote_perform(last_slot);                          -- emote.rs2:50-51

[proc,emote_perform](int $emote)
  looks up the seq via ~emote_anim
  if null: mes("You haven't unlocked that emote yet.")
  else:    p_stopaction; anim($anim, ^emote_anim_delay);   -- emote.rs2:53-64

[proc,emote_login]
  if_setevents(emote:contents, 0, 55, ^if_event_op1)        -- emote.rs2:133-134
  called from player/login.rs2:48
```

`[proc,emote_anim]` (`emote.rs2:72-121`) is the index→seq table, 40 of 56
indices mapped. `anim()` compiles to `SS_OP_ANIM`
(`src/net/mock/mock230_scripts.c:3180-3194`), which calls
**`mock230_anim_play_player`** — confirmed the identical function combat
calls (`mock230_combat.c:156,180`) — under the same priority gate and
setting the same `MOCK230_PMASK_SEQUENCE` broadcast mask
(`mock230.h:290`, confirmed) the player-info encoder already streams to
nearby observers. **This is the direct answer to "is this cheap because it
reuses combat's plumbing" — yes, confirmed**: no new wire opcode, no new
host op, nothing new in the engine at all.

The dynamic-child `IF_BUTTON1` sub-id addressing this depends on was fixed
once, generically: `app_if_button_target` (`src/app.c:282`, confirmed,
called from the click path at `:328` and `:8403`) — documented as covering
both `chatmenu`'s rows and `emote`'s grid.

**Selftested**: `mock230_world.c:5738` onward (confirmed present) drives
`IF_BUTTON1` with real sub-ids for 7 named emotes and asserts
`player->anim_id` matches the cache's real seq id for each, plus a negative
case (an unmodelled index) asserting nothing plays rather than silently
succeeding.

## 4. LostCity precedent (real, unlike the modern chrome panels)

`content/scripts/interface_controls/scripts/player_controls.rs2`:

- 12 fixed `[if_button,controls:com_N]` bindings (one per component, since
  LostCity's tab isn't client-built) plus 4 gated mime emotes.
- **`%emote_access`** — the same varp name, `transmit=yes scope=perm`
  (`_unpack/254/all.varp:5-7`) — tested via `testbit` before each mime
  emote's perform label runs.
- **Set by content, not engine**: the Mime random event
  (`content/scripts/macro events/scripts/general/macro_event_mime.rs2:163-220`)
  calls `setbit(%emote_access, ^macro_mime_climb_rope)` etc. after the
  player completes each mime instruction.
- Shared `[label,controls_emote](seq $anim)` does `p_finduid` busy-guard,
  a Fishing Trawler swim-check, then the identical `p_stopaction;
  anim($anim, 20);` shape osrs239's port already uses
  (`emote_anim_delay = 20`, `emote.constant:66`).

## 5. Server obligations (small, all content-side)

| item | status |
|---|---|
| index → seq, click dispatch, animate, broadcast | **done** |
| `emote_access` (varp313) `.varp` overlay, `transmit=yes scope=perm` | **not declared** — exists in `all.varp` but no overlay anywhere in `server/scripts` sets the transmit keys |
| Something that actually sets the 24 unlock bits (Mime event, Lost Tribe quest, Achievement Diaries, Skill/Max capes) | **not modelled** — no quest/diary/random-event system currently touches any bit; `emote.rs2:33-38` states this is deliberate, not a bug |
| `last_verb`-based "Loop" op variant | **content gap only** — `last_verb` already exists and is populated (`mock230.h:1201`); `emote_perform` just doesn't branch on it yet |
| `p_finduid` busy-guard / Fishing Trawler swim-check | **not ported** — no Trawler content exists yet, consistent with `emote.rs2`'s own note |

## 6. What this doc does not cover

- Which of the 24 unlock-gated emotes should be prioritized once a
  quest/diary system exists — that's a content-slice decision, not a
  server-interaction question.
- The remaining 16 of 56 indices with no `[proc,emote_anim]` case — not
  investigated individually; likely correspond to seqs this world's cache
  doesn't carry or hasn't been named yet.
