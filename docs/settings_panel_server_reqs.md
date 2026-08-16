# Settings (interface 134): what the server owes

> **NOT BUILT — triaged 2026-08-02: BLOCKED, and the real blocker is not the
> one §0 names.** `CS2VM2_Op_PopVar` and `CS2VM2_Op_PopVarbit` are still
> stack-balanced no-ops (re-measured today, `cs2vm2.c:5589` and `:5605`), so a
> toggle does not change even locally — ~30 lines, and fixing it unblocks every
> client-authored varp in the cache. Behind that sits the unresolved question:
> `osrs230/packetout.h` has **no client→server varp opcode of any kind**, and
> the rows are `cc_setonop` (client-handled), so no `IF_BUTTON` reaches the
> server either — how a CS2-written varp propagates is not established anywhere
> in this repo, and `Client-TS/` is an uninitialised submodule here. Do not
> start until that is answered; §3's "declare two `.varp` overlays" does not
> survive it. **§0's "single load-bearing corpus gap" is not a gap**:
> `[proc,script6716]` (659 lines — the toggle-id → varp/varbit map) and its
> setter `[clientscript,script3965]` (651 lines; `settings_clicked_toggle` is
> not the symbol) both decompile, and Accept Aid is `case 59` in both →
> `%varbit4180`. §2's `PKT_NAME_CHAT_FILTER_SETTINGS` "confirmed absent from
> `osrs230/packetin.h`" is now stale — it landed with friends/PM. §1's
> Shift-click row is unsupported: no script in the 9,433-file corpus references
> `%var1055` at all.

> Companion to `docs/chrome_panels_server_reqs.md`'s xptracker section, same
> discovery pass. **Inverts that section's finding.** xptracker's chrome was
> pure client varc with one real varp buried underneath. Here, **even the
> panel's own navigation chrome (selected tab, search-active flags) is a
> real named varbit**, and confirmed corpus-wide: `{varcN}` never appears in
> a single `if_setonvartransmit`/`cc_setonvartransmit` list anywhere in this
> cache (zero hits vs. 902 `{varN}` hits) — every redraw dependency in this
> panel rides a real varp/varbit. The load-bearing gap isn't "which varp
> exists," it's **two missing procs that are the only place any toggle's
> actual read/write happens.**

## 0. Status at a glance

The panel is a generic, data-driven settings framework (enum-driven
categories → struct-driven rows), same shape as real OSRS's modern settings
screen, not per-toggle static components. Every toggle funnels through
`~script6716` (getter) and `settings_clicked_toggle` (setter) —
**confirmed both missing from this decompile** (`find`/`grep` both empty).
This is the single load-bearing corpus gap, same class as loottools'
`script7166`/`script7133`.

## 1. What's real vs. what's cosmetic — findings, not assumptions

| item | mechanism | kind |
|---|---|---|
| **Accept Aid** | redraw bound to **var427 = `option_aid`** (confirmed), specific bit **varbit 4180 = `option_acceptaid`** (confirmed) | real varbit, gameplay-affecting |
| **Shift-click to drop items** | redraw bound to **var1055 = `chat_filter_assist`** (confirmed real varp) | real varp, gameplay-affecting; exact bit unconfirmed (getter missing) |
| Most other individual toggles (profanity filter, loot notifications, run-invert, etc.) | not individually cased in the redraw-dependency switch; fall to `default` → **var2855 = `settings_tracking`**, which is itself a *real, meaningful* base varp (it also hosts the panel's own selected-tab varbit) | ambiguous without the missing getter, but the "no `{varcN}` anywhere" fact is evidence most toggle *values* are varp/varbit-backed too — the reverse of the usual assumption for a settings screen |
| Panel chrome: selected category (`%varbit9656` = `settings_category`), search-active flags (`%varbit16073/16074`), tooltip declutter, "hide locked settings" | all real, confirmed varbits, packed into shared base varps | real, but purely cosmetic UI state |
| Search keystroke buffer, layout-height cache, modal-open flag, one feature gate | pure client varc — **the only 4 varc references in the entire traced call graph** | pure client, no server touch |
| Music/sound volume, minimap/viewport/UI zoom | CS2-host round-trip fields, already landed client-side | pure client, already done |

## 2. Two real scope corrections

- **"Run-mode default" is not part of this interface at all.** The actual
  run toggle lives on the minimap orb, a different feature, and is
  **already landed**: content varp `option_run`
  (`server/scripts/player/configs/player_controls.varp:39-41`, confirmed),
  written from `interface_orbs/scripts/orbs.rs2`, wired server-side at
  `src/net/mock/mock230_world.c:966,1896,2711` (confirmed present). Nothing
  to do here.
- **"Private/public chat filter mode" is also not part of this interface.**
  The Chat-category structs under that label are chatbox text-*colour*
  customization, not the privacy mode. The real privacy-mode mechanism has
  genuine dead client-side plumbing already in this repo: `struct
  PktChatFilterSettings` (`src/net/rev/revpacket.h:193-198`), decoded in
  `rs_gameproto_exec.c`, and a real wire opcode —
  **`PKT_NAME_CHAT_FILTER_SETTINGS` is routed in `lc254/packetin.h`
  (confirmed, opcode 24) but has zero presence in `osrs230/packetin.h`
  (confirmed absent)**. This is a real, separate gap — but it belongs to
  the chat-privacy feature, not to interface 134, and is flagged here only
  because a prior research pass's premise conflated the two.

## 3. Server obligations

| state | mock230 status |
|---|---|
| `option_acceptaid` (varbit 4180 / varp `option_aid`, 427) | **not declared anywhere** — clean gap, no conflicting content; needs a `.varp` overlay (`transmit=yes`, likely `scope=perm`) on the already-working generic varp wire |
| `chat_filter_assist` (var1055) | **not declared anywhere** |
| Privacy chat-filter mode (`PktChatFilterSettings`) | **not this panel's problem** — the client-side decode exists but the packet is unrouted for `osrs230` specifically; a separate, adjacent gap |
| Run toggle, auto-retaliate, combat style | **already landed** — not this panel, cited only as the "real varp, LostCity-precedented" comparison shape |
| Music/sound/zoom settings | **already landed** as client-host state, nothing server-side expected |
| Panel chrome (tab, search, declutter) | undeclared but low-priority — cosmetic session UI, not gameplay |
| `script6716`/`settings_clicked_toggle` bodies | **corpus gap** — re-decompile before implementing any specific toggle; this is what actually determines each toggle id's real varp/varbit |

`grep -rniE "accept.?aid|settings" src/net/mock/ src/game/` finds only the
bank's own unrelated settings (varbit-packed withdrawal mode) and the
already-landed CS2-host audio/zoom fields — **zero existing wiring for any
of interface 134's own toggle values**.

## 4. LostCity precedent

- **Accept Aid: none** — zero hits anywhere in LostCity, consistent with
  the feature postdating the rev-254 (Sept 2004) snapshot.
- **Chat filter mode: a real precedent, and it matches this repo's dead
  packet exactly.** LostCity's `Player.ts` declares `publicChat`/
  `privateChat`/`tradeDuel` as real per-player fields, packed into one byte
  and persisted on save, set from an incoming client packet
  (`ChatSetModeHandler.ts`), pushed via `ChatFilterSettingsEncoder`. This is
  the identical 3-field shape already sitting client-side in this repo's
  `PktChatFilterSettings` — confirming that shape is a real mechanism, just
  currently unwired for the wire table this client actually speaks.

## 5. What this doc does not cover

- `script6716`/`settings_clicked_toggle`'s real bodies — genuinely missing
  from this decompile; every specific toggle-to-varp mapping beyond the two
  confirmed above (Accept Aid, Shift-click) depends on re-decompiling these.
- The numeric input-type rows (e.g. "Energy threshold to re-enable
  running") — a separate handler (`settings_create_input_setting`), not
  traced.
- 306 files match `settings_` broadly — this doc covers only the toggle
  mechanism and the two real gameplay settings found; not exhaustive.
