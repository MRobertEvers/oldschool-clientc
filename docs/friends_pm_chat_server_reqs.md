# Friends (429) and PM chat (163): what the server owes each

> **DONE — this is the worked example for the series, and it is superseded by
> [`FRIENDS_PRIVATE_CHAT.md`](FRIENDS_PRIVATE_CHAT.md) and
> [`FRIENDS_PRIVATE_CHAT_CONTENT.md`](FRIENDS_PRIVATE_CHAT_CONTENT.md).**
> `mock230_friends.{c,h}`, interfaces 429/432/163, packets 15/21/29/56, 18 CS2
> host ops, and content under `server/scripts/interface_friends/` with
> `~friends_login` at `player/login.rs2:49`; `make -C src test-social` green as
> of 2026-08-02. §5's "zero rows in `packetout.h` for any of the five names"
> and "the host ops are stubs" were true when written and are now stale, as is
> §1.1's `~friend_update` corpus gap — it is clientscript 125, recovered by a
> full re-decompile. Its roster / `isVisibleTo` / base-37 store is what clan
> chat tier 1 is meant to reuse
> (`docs/clan_chat_server_reqs.md`, still blocked).

> Companion to `docs/questlist_chatmenu_levelup.md`, same discovery pass
> (`docs/PORTING_GUIDE.md` §5.3) applied to the two interfaces §5.2 names as
> the thing to do **before** clan chat: *"Do friends/PM first; clan chat
> reuses its plumbing."* `OSRS-Content/osrs239-content` is abbreviated
> `content/` below.

## 0. Status at a glance

| interface | id | status | what's missing |
|---|---|---|---|
| `friends` | 429 (19 components) | **greenfield** | row-builder proc (`~friend_update`) missing from the decompiled corpus; zero server state; wire packets frame to `PKT_NAME_NONE`; CS2 host ops silently no-op |
| `pm_chat` | 163 (`container` + 5 static rows) | **greenfield** | log is the client's native chat-history ring, fed only by a `MESSAGE_PRIVATE` packet the server never sends; `chat_sendprivate` is a stub |

This is a different flavour of gap from questlist/levelup: there is real,
working **client-side plumbing already built for a different, older wire**
(§4) — it just never got connected to the CS2/rev-230 path.

---

## 1. Population mechanism

### 1.1 `friends` (429)

`interfaces/friends.if:14` onload → **script_123**
(`[clientscript,friend_init]`):

```
cc_deleteall($component9);
~scrollbar_vertical(...);
~friend_update($int3..$int11, $component9, ...);
if_setonfriendtransmit("friend_update(...)", $component0);
if_setonvartransmit("friend_update(...){var1737}", $component0);
```

- **`~friend_update` — the actual row-building loop — is not present anywhere
  in this decompiled corpus.** Only its three callers exist
  (`script_123.cs2:12-14`, a wrapper `script_631.cs2`, and the sort-button
  handler `script_1670.cs2:8`); no `[proc,friend_update]` definition. Same
  corpus-gap class as `~questlist_draw`/`~chatbox_multi_addoption`
  (`docs/questlist_chatmenu_levelup.md` §1.3/§2.1) — **re-decompile the live
  script id before porting this**, don't guess the body.
- What's confirmed without that body: the rows come from the client's native
  friend-list buffer via `friend_getname`/`friend_getworld`/`friend_getrank`
  (§2 table) — a genuinely dynamic, server-fed list, not a dbtable/config.
- `if_setonfriendtransmit` (opcode 2420) is the repaint trigger: it re-runs
  `~friend_update` whenever an incoming packet writes into that native buffer
  — i.e. row rebuild is packet-driven, not polled.
- **Add/Delete Friend** (`friends.if:196-242`, `onop=i:103`/`i:104`) open a
  shared text-input overlay (`script_103`/`script_104`,
  `[clientscript,meslayer_mode2/3]`); Enter is handled centrally by
  `[proc,meslayer_enter]` (`script_681.cs2:11-29`): `case 2: friend_add(...);
  case 3: friend_del(...);`, gated on `friend_count < 0` ("system busy").
- **Row right-click** (`script_126.cs2`, `[clientscript,friend_op]`): op1 →
  PM prompt, op2 → "That player is currently offline.", op3 → `friend_del`,
  op4 → world-hop (out of scope here).

### 1.2 `pm_chat` (163)

`interfaces/pm_chat.if:12` onload=`i:926` → **script_926**
(`[clientscript,pm_init]`): pre-creates 4 hidden sub-components per row slot
under `interface_163:0`, then calls `~rebuildpmbox(~script553)`.

- `~script553` walks all 118 native chat types via
  `chat_gethistorylength`/`chat_gethistoryex_bytypeandline` — a **native
  client-side chat-history ring buffer**, not server content.
- `~rebuildpmbox` (`script_89.cs2`) walks that ring backward
  (`chat_getprevuid`/`chat_gethistoryex_byuid`), filters to
  `chattype_privatechat*` rows, and for each one arms right-click ops
  ("Message"/"Add friend", "Add ignore", "Report", "Crown Info") via
  `if_setonop`.
- **The repaint trigger is packet-driven, confirmed**: `script_925.cs2`
  (`[clientscript,chatbox_init]`) registers
  `if_setonchattransmit`/`if_setonfriendtransmit` → `chat_onchattransmit`
  (`script_663.cs2`), which calls `~rebuildpmbox` on every fire. **No
  content/server code populates the ring; it is fed only by the wire.**
- **Sending a PM**: `~meslayer_mode6` opens "Enter message to send to
  `<name>`"; `[proc,meslayer_enter]` case 6 calls
  **`chat_sendprivate(username, message)`** (opcode 5009).

---

## 2. The fuller CS2 op surface

Decoded from `3rd/rscache/src/cs2/cs2_command.gen.h`:

| op | opcode | shape | confirmed callers |
|---|---|---|---|
| `friend_add` | 3605 | `(username) -> ()` | `meslayer_enter` case 2, `friend_op`'s message path, `friendschat_op`, `clan_sidepanel_op`, 6 more |
| `friend_del` | 3606 | `(username) -> ()` | `meslayer_enter` case 3 and the block-path in case 6, `friend_op` |
| `ignore_add` | 3607 | `(username) -> ()` | `meslayer_enter` case 4, `private_op` |
| `ignore_del` | 3608 | `(username) -> ()` | `meslayer_enter` case 5 |
| `friend_test` / `ignore_test` | 3609 / 3623 | `(username) -> bool` | gate most add/del handlers before offering the opposite verb |
| `friend_count` / `ignore_count` | 3600 / 3621 | `() -> count` | busy-gate in `meslayer_enter` |
| `friend_getname` / `friend_getworld` / `friend_getrank` | 3601/3602/3603 | `(index) -> ...` | not called directly in this corpus — presumed inside the missing `~friend_update` |
| `chat_sendprivate` | 5009 | `(username, mes) -> ()` | `meslayer_enter` case 6 — the send |
| `if_setonfriendtransmit` / `if_setonchattransmit` | 2420 / 2418 | clientscript listener | `script_123`, `script_925` |
| `clan_*` | 3611-3627 | `clan_getchatusername(i)`, `clan_kickuser`, `clan_joinchat`, `clan_isfriend/isignore` | confirms `docs/PORTING_GUIDE.md` §5.2's claim exactly; not exercised by friends/PM |
| `push_varclan(setting)` | 74 / 76 | var-push (same kind as `push_varc_int`) | not called anywhere in the friends/PM call graph — clan-chat-only, out of scope here |

---

## 3. Wire packets

All four packets named in `docs/PORTING_GUIDE.md` §5.2 are confirmed by row
and by name (the file cites RSProt naming in its own header comment):

| id | RSProt name | row | mapped to |
|---|---|---|---|
| 15 | `FRIENDLIST_LOADED` | `packetin.h:88` | `PKT_NAME_NONE` |
| 56 | `UPDATE_FRIENDLIST` | `packetin.h:89` | `PKT_NAME_NONE` |
| 21 | `UPDATE_IGNORELIST` | `packetin.h:90` | `PKT_NAME_NONE` |
| 29 | `MESSAGE_PRIVATE` | `packetin.h:77` | `PKT_NAME_NONE` |

All four are server→client. **The client-side decode/apply already exists,
fully, and is simply unreached**: `src/game/rs_gameproto_exec.c:686-745` has
complete cases for all four `PKT_NAME_*` (confirmed present at those lines) —
they're dead code today only because `osrs230/packetin.h` never routes wire
opcodes 15/56/21/29 to them. The `lc254` revision table *does* route its
equivalent (`lc254/packetin.h:54,137`), proving the decode path works.

**Outbound is worse — it doesn't even frame.** `pktnames.h` declares
`PKTOUT_NAME_FRIENDLIST_ADD/DEL`, `IGNORELIST_ADD/DEL`, `MESSAGE_PRIVATE`, and
`net_out.c` has working, revision-generic builders — confirmed present:
`net_out_message_private` (`net_out.c:832`), `net_out_friendlist_add`
(`:917`), `net_out_ignorelist_add` (`:939`) — but
**`src/net/rev/osrs230/packetout.h` has zero rows for any of the five names**
(confirmed empty grep), so `out_begin` has no opcode to write against, unlike
`lc254/packetout.h` and `lc245_2/packetout.h` which both assign real opcodes.

**CS2 host ops are declared but stub to no-ops**, confirmed at the dispatch
level: `src/cs2vm2/cs2_opcode_meta.c` tags `FRIEND_*`/`IGNORE_*`/
`CHAT_SENDPRIVATE` as `CS2_HANDLER_HOST`, but **`cs2vm2.c`'s dispatch switch
has no `case` for any of them** (confirmed empty grep for
`CS2_OP_FRIEND_`/`CS2_OP_IGNORE_`/`CS2_OP_CHAT_SENDPRIVATE`) — they fall to
`CS2VM2_Op_StackMetaStub`, which (since their arity is statically known) pops
the right argument count and pushes zeroed defaults rather than asserting:
`friend_add("bob")` runs and does nothing, `friend_test` always returns
`false`, `friend_count` always returns `0`, `chat_sendprivate` discards both
arguments. `IF_SETONFRIENDTRANSMIT`/`CC_SETONFRIENDTRANSMIT` fall into the
same documented discard-stub family as other unmodelled transmit events
(`cs2vm2.c` comment: "No model for these events yet... MUST still be
parsed") — parsed so later opcodes don't desync, never fired.

---

## 4. Existing scaffolding — not zero, but the wrong wire

There is a substantial, working social system already built client-side —
just wired to the legacy `lc254`/`lc245_2` numbered-interface path, not CS2:

- `src/game/rs_social.h`/`rs_social.c` — an `RS_Social` struct
  (`friend_name[]`/`friend_world[]`/`friend_count`, `ignore_name[]`/`_count`)
  with `RS_Social_AddFriend`/`RemoveFriend`/`AddIgnore`/`RemoveIgnore`.
- `src/game/rs_gameproto_exec.c:686-745` — full decode-and-apply for all four
  inbound packets, reachable only if a revision's `packetin.h` routes to
  these names (§3).
- `src/game/rs_chat.c` — a separate, legacy social-input overlay
  (`RS_CHAT_SOCIAL_ADD_FRIEND` etc.), distinct from the CS2 `meslayer_*`
  system in §1.
- `src/app.c:9150-9172` — wires that legacy overlay's submit to
  `net_out_friendlist_add/del`, `net_out_ignorelist_add/del`.
- `src/game/rs_minimenu_build.c` — a right-click minimenu keyed on a
  different reference client's numbered-component convention.

**None of this reaches interface 429/163, the CS2 host ops, or the osrs230
wire tables.** It's directly reusable groundwork (payload shapes, `RS_Social`
state, name codec) but dead weight for the CS2 target until (a)
`osrs230/packetin.h` routes 15/56/21/29 to real names and (b)
`osrs230/packetout.h` gets rows for the five outbound names.

---

## 5. Server obligations

| obligation | detail | precedent |
|---|---|---|
| Decode 4 inbound packets | `osrs230/packetin.h` — route 15/29/21/56 to their real `PKT_NAME_*`; client-side apply already exists | LostCity `World.ts:1946-1997` writes these same 4 events |
| Frame 5 outbound packets | `osrs230/packetout.h` — add opcode rows for the 5 `PKTOUT_NAME_*`; builders already exist in `net_out.c` | LostCity `FriendsClientOpcodes` |
| A social-state service | friend lists, ignore lists, per-player online world, private-chat mode, visibility policy — must be **shared across players**, not per-connection (a friend's online status can't be answered from one connection's state) | `LostCity_Server/engine/src/server/friend/FriendServer.ts`, §6 |
| CS2 host op implementations | `FRIEND_ADD/DEL/TEST/COUNT/GETNAME/GETWORLD/GETRANK/SETRANK`, `IGNORE_ADD/DEL/TEST/COUNT/GETNAME`, `CHAT_SENDPRIVATE` need real `case`s in `cs2vm2.c` instead of falling to `StackMetaStub` | CS2-era-specific; no LostCity equivalent (rev 254 used numbered components, no host-op VM) |
| Transmit-listener firing | `IF_SETONFRIENDTRANSMIT` etc. need an actual "fire this clientscript when the buffer changes" mechanism, replacing the current parse-only discard stub | client-side VM work, not server, but gates §1's repaint path |
| `~friend_update` proc body | missing from this decompiled corpus — re-decompile the live script id before implementing the row loop | — |

---

## 6. LostCity `FriendServer` precedent — scoping detail

- **Process shape, confirmed literally**: `engine/src/friend.ts` is its own
  OS process (`engine/package.json:17`, `"friend": "bun run
  src/friend.ts"`), separate from `login`/`logger`/`app`(world). Each world
  talks to it via a `worker_threads.Worker` (`World.ts:113`,
  `FriendThread.ts`) that proxies requests out and pushes back in — i.e. one
  shared service process + one proxy thread per world, not a single-layer
  design.
- **State** (`FriendServerRepository.ts`): `playersByWorld[world][slot]`,
  `worldByPlayer[name]`, `playerStaff` (visibility override),
  `privateChatByPlayer[name]` (off/friends-only/on), `playerFriends[name][]`,
  `playerIgnores[name][]` — in-memory, DB-backed, lazily loaded per-player on
  login.
- **Visibility policy is one function**: `isVisibleTo(viewer, other)` — staff
  always visible; being on the target's ignore list hides you from them; the
  target's own private-chat mode gates who sees them online at all. This is
  exactly `docs/PORTING_GUIDE.md` §2's "port the proc, not the field" — a
  policy, not a flag.
- **Push model**: any state change for player X triggers
  `broadcastWorldToFollowers` — every player who has X as a friend gets a
  fresh row via `sendPlayerWorldUpdate`. This is what makes "friend just
  logged in" appear live without polling, and is the piece a mock230 port
  most needs to replicate *structurally* (a single-connection server today
  has no way to express "notify everyone who has X as a friend").
- **Minimal mock230 equivalent, scoped from this**: not per-connection state
  — but also **not** LostCity's process/worker-thread split, since neither
  pressure it solves (keeping DB I/O and cross-world visibility off a tick
  loop, serving many world processes from one service) exists yet in mock230
  (single active connection per `docs/PORTING_GUIDE.md` §6.1, no live
  persistence per §2.5's note that `mock230_save.c` has no callers). What's
  worth porting now is the **data model and the policy shape** — a
  name→(world, friends, ignores, chat-mode) table, an `is_visible_to`
  equivalent, and add/del mutators that decide who needs a repaint — as a
  plain in-process service module, with the process/thread split deferred
  until multi-world is a real requirement.

---

## 7. What this doc does not cover

- Clan chat itself (`clan_*` 3611-3627, `push_varclan*`) — confirmed present
  and unused by friends/PM, explicitly the *next* interface per
  `docs/PORTING_GUIDE.md` §5.2, reusing this doc's packet/service plumbing.
- `~friend_update`'s and `friend_op`'s exact row-layout bodies — missing from
  this decompiled corpus; re-decompile before implementing.
- The legacy `lc254`/`rs_social.c` path's own correctness — out of scope,
  noted only as reusable groundwork.
