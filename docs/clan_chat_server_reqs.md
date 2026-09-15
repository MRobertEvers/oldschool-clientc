# Clan Chat (`clans_sidepanel` 701, `clans_members` 693): what the server owes

> **NOT BUILT — triaged 2026-08-02: split verdict.** 701/693 are **BLOCKED**,
> and §2's reading of the failure mode is wrong in a way that inverts the risk:
> of the 60 clan-family opcodes, 8 are genuinely handled (the transmit-listener
> discard group), 19 are silent zero stubs, and **33 have `known=0` and hit
> `assert(0 && "unimplemented CS2 opcode reached StackMetaStub")` — SIGABRT,
> not a zeroed default.** `activeclansettings_getaffinedcount` is in the abort
> set; it does not crash today only because `activeclansettings_find_affined`
> (3801, `known=1`) returns false and short-circuits every guard, so it starts
> crashing the moment the server says "you are in a clan". The fix is **one
> generator change**, not 33 signatures: `gen_opcode_stack.py` reads only
> `cs2_opcode_meta.c`, while `3rd/rscache/src/cs2/cs2_command.gen.h` already
> carries proto arities for all 60. Two things this doc misses: the interfaces
> that matter for tier 1 are `side_channels` **707** (the tab strip) and
> `chatchannel_current` **7** (the classic Friends-Chat panel, the real home of
> the `clan_*` ops) — 7's 14 ops are `known=1` today, so tier 1 is buildable
> now on friends' plumbing; and §1's `clan_*` range is 3611-3620 + 3624-3627,
> because 3621/3622/3623 are `ignore_count`/`ignore_getname`/`ignore_test`,
> already implemented by friends. §1.1's chattype ids are **not** a corpus gap
> either — `script84` has five cases: 41 own clan, 43 clan/system broadcast, 44
> guest/listened, 46 guest broadcast, 9 legacy friends-chat. §5's secondary ids
> are **8 of 12 wrong**; pack truth: `clans_hall` 692, `clans_board` 700,
> `clans_storage_main/_side` 696/697, `clans_permissions` 706,
> `clans_events/_create` 703/704, `clans_interests` 691, `clans_outfit` 694,
> `clans_ranktitles` 695 (689, 699, 702, 705 are right).

> Companion to `docs/friends_pm_chat_server_reqs.md`, same discovery pass —
> the feature `docs/PORTING_GUIDE.md` §5.2 names as the direct follow-on:
> *"Do friends/PM first; clan chat reuses its plumbing."* **Correction to
> that section's framing**: LostCity has **no** Friends Chat / clan-chat
> precedent at all (§5) — Friends Chat launched August 2008, after
> LostCity's rev-254 target. This is a bigger, and in one respect *worse*,
> greenfield gap than friends/PM.

## 0. Status at a glance

| interface | id | status | what's missing |
|---|---|---|---|
| `clans_sidepanel` | 701 | greenfield | roster/chat both native-buffer-driven; **no packet name constants exist at all** — worse than friends/PM, which at least had unrouted `PKT_NAME_*` |
| `clans_members` | 693 | greenfield | same op family, same gap |

---

## 1. The mechanism — three op families, not one

Prior friends/PM research named `clan_*` (3611-3627) as present but uncalled.
This trace found it's actually **three separate op families**, each serving
a different scope:

| family | opcodes | role | confirmed caller |
|---|---|---|---|
| `clan_*` | 3611-3627 | join/leave/kick the **currently-open** channel (own or a named guest channel) | `script_437.cs2`, `script_681.cs2:85`, `script_3763-3766.cs2` |
| `activeclanchannel_*` | 3850-3861 (confirmed) | who is **connected right now** to my affined clan's channel — transient | `script_4399.cs2` (sidepanel roster), `script_84.cs2` (chatbox kick op) |
| `activeclansettings_*` | 3800-3822 (confirmed) | **persistent membership roster** (online + offline), rank, join date, muted/banned | `script_4232.cs2` (`clans_members`), `script_84.cs2` (chatbox rank icon) |
| `chat_sendclan` | 5010 (confirmed, `(string,int,int)→()`) | send into channel 3 or 4 (own/guest) | `script_5517.cs2:50` |

`clans_sidepanel.if`'s onload (`script_4395`) arms **four** transmit
listeners on the same draw proc — a var write, a clan-settings event, a
clan-channel event, or a friend-list event all trigger repaint
(`script_4395.cs2:2-8`). `clans_guest_sidepanel` (702) reuses the identical
script with one flipped argument (own-clan vs guest-clan branch).

### 1.1 Chat log — the same native ring as `pm_chat`, confirmed

No message-log component exists in the interface itself — clan messages
render in the **main chatbox**, via the same `chat_gethistoryex_byuid` ring
walk `docs/friends_pm_chat_server_reqs.md` §1.2 found for `pm_chat`
(`script_84.cs2`, `rebuildchatbox`). Four clan-related chattype cases exist
in the dispatch (`script_84.cs2:135-215`) — legacy-style, own-clan,
guest/"listened", and broadcast/notice variants — each pulling rank icons
via `activeclansettings_getaffinedrank`. **The numeric chattype ids
themselves aren't in this decompiled corpus** — same missing-enum class as
elsewhere in this series; re-decompile before hardcoding them.

Repaint trigger, confirmed unchanged from the `pm_chat` finding: fed only by
the wire (`if_setonchattransmit`/`if_setonfriendtransmit` →
`chat_onchattransmit` → `rebuildchatbox`), no content code populates it.

### 1.2 Roster — two scopes, same as the two op families above

`clans_sidepanel`'s roster (`clan_sidepanel_drawchannel`, `script_4399.cs2`)
walks `activeclanchannel_getsorteduserslot` → `getuserdisplayname`/
`getuserworld`/`getuserrank` — **who's connected right now**.
`clans_members`'s roster (`clan_members_draw`, `script_4232.cs2`) walks
`activeclansettings_getsortedaffinedslot` → `getaffineddisplayname`/
`getaffinedrank` — **the full persistent membership list**, offline members
included. Both reuse the friend/ignore ops directly for right-click
Add/Remove Friend/Ignore (`script_4399.cs2:113-156`) — confirmed same ops
`docs/friends_pm_chat_server_reqs.md` §2 catalogued.

---

## 2. Landed vs. gap — confirmed fully greenfield, and worse than friends/PM

- `grep -rniE "\bclan\b|varclan" src/torirsserver/ src/game/` — zero hits.
- **`grep -in "clan" src/net/pktnames.h` — zero hits, confirmed.** Unlike
  friends/PM, which at least has `PKTOUT_NAME_FRIENDLIST_ADD` etc.
  declared-but-unframed, **there are no `PKT_NAME_CLAN*`/`PKTOUT_NAME_CLAN*`
  constants of any kind.** Clan chat has no packet-name skeleton at all.
- `grep -niE "\bclan\b" src/game/rs_gameproto_exec.c src/game/rs_social.* src/game/rs_chat.*` —
  zero hits. Unlike friends/PM (which has full, unreached decode/apply for
  its four packets), **there is no dead client-side decode path to
  reconnect for clan chat** — it has to be built from nothing.
- `src/cs2vm2/cs2vm2.c` — confirmed **no case** for any `CLAN_*`,
  `ACTIVECLANSETTINGS_*`, `ACTIVECLANCHANNEL_*`, `CLANPROFILE_FIND`, or
  `CHAT_SENDCLAN`. All fall to `StackMetaStub` — arity popped correctly,
  zeroed defaults pushed, so e.g. `activeclansettings_getaffinedcount`
  always returns 0, `chat_sendclan` silently discards its args.
- `PUSH_VARCLANSETTING`/`PUSH_VARCLAN` (74/76) are tagged as VM-local
  var-pushes (like `PUSH_VARBIT`) but have no case in the VM's opcode
  switch either — same `StackMetaStub` fallthrough, so every
  `%varclansettingN`/`%varclanN` read currently evaluates to a stub default.

**Net: strictly larger gap than friends/PM** — no packet names, no client
decode scaffolding, no host-op cases, no VM var-push cases.

---

## 3. Server obligations — building directly on `docs/friends_pm_chat_server_reqs.md`

| obligation | shared vs. clan-specific |
|---|---|
| Social-state service, the 4 friend/ignore packets, `CHAT_SENDPRIVATE`/`FRIEND_*`/`IGNORE_*` host ops | **shared** — already scoped in the friends/PM doc; clan right-click ops call directly into it |
| **A clan/channel-membership service**: name→(clan affiliation, rank, join date, muted, banned) + a separate transient "who is connected right now" set | **clan-specific, new** — no LostCity precedent to port (§4) |
| **A "currently open channel" pointer per player** | **clan-specific** — the `clan_*` (3611-3627) family's state, distinct from persistent membership |
| New packets, inbound and outbound | **clan-specific, strictly new** — must declare `PKT_NAME_CLAN*`/`PKTOUT_NAME_CLAN*` from scratch, no dead path to reconnect |
| CS2 host ops (14 `ACTIVECLANSETTINGS_*`, 12 `ACTIVECLANCHANNEL_*`, the `CLAN_*` dozen, `CHAT_SENDCLAN`, `CLANPROFILE_FIND`) | **clan-specific**, CS2-era-only, no LostCity equivalent |
| `PUSH_VARCLANSETTING`/`PUSH_VARCLAN` VM cases | **clan-specific** VM work, mirroring `PushVarbit`/`PushVarcInt` |
| Transmit-listener firing (`SETONCLANTRANSMIT` etc.) | **shared mechanism**, clan-specific trigger conditions |
| Chattype/enum re-decompile | corpus gap — the numeric ids behind the 4 clan chattype cases aren't in this corpus |

---

## 4. LostCity precedent — none found, correcting the porting guide's framing

`grep -ril "friendschat\|clanchat" LostCity_Server/` — **zero hits,
confirmed** (only one unrelated prose match, a quest line using "clan" as an
ordinary word). This is a verifiable historical fact, not a corpus gap:
**LostCity targets rev 254 (~2005-2006); Friends Chat is an August 2008
feature** — it postdates the revision LostCity implements. There is no
rank/kick model, no channel service, nothing to port structurally. The only
adjacent precedent remains the plain friend-list `FriendServer.ts`
(`docs/friends_pm_chat_server_reqs.md` §6) — a simple social graph with no
channel/membership/rank concept at all. **Clan chat's server design has to
be designed fresh**, not ported, even more so than friends/PM.

---

## 5. Other clan interfaces — inventory only, not deep-traced

Confirmed to exist, sharing the same `activeclansettings_*` plumbing, but
out of scope for this pass: `clans_hall` (700, storage/decoration hub),
`clans_storage_main`/`_side` (695/696), `clans_permissions` (703, rank
matrix via a content-side proc, not a native op), `clans_applicants` (699,
join queue), `clans_banned` (689, via `getbannedcount`/`getbanneddisplayname`),
`clans_board` (692, message board), `clans_events`/`_create` (694, calendar),
`clans_interests` (697), `clans_outfit` (691, cape designer), `clans_ranktitles`
(706), `clans_creation_sidepanel` (705, new-clan wizard), and the
`clanwars_*` family (a separate PvP-tournament subsystem).

## 6. What this doc does not cover

- Any of the "other clan interfaces" listed in §5 beyond confirming they
  exist and roughly what they do.
- The numeric chattype ids and `enum_4070` (chat-mode names) — both
  confirmed missing from this decompile; re-decompile before implementing.
- `clanprofile_find` (opcode 3890) — declared, not traced.
