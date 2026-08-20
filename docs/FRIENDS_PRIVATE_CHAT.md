# Friends, ignore, and private chat

> **Status: the service, the wire and the CS2 host ops have all landed. A player
> can now add a friend and send a private message, in the real client, with the
> real interface.**
>
> `src/torirsserver/ToriRSServer_Friends.{c,h}` is the service (§5.3), §3.5 is the wire,
> and §4.4 is this stage: eighteen host ops with measured stack shapes, the
> friend-transmit repaint channel, and the outbound send seam. Proved in the
> headless client, end to end, with no cheat and no second process — §8.2b.
>
> **The content half is written up in
> [`FRIENDS_PRIVATE_CHAT_CONTENT.md`](FRIENDS_PRIVATE_CHAT_CONTENT.md)** —
> specified, compiled and run against a scratch copy of the content tree, and
> still **not applied**, because `OSRS-Content/` belongs to another lane. That
> file supersedes §2.3 here and corrects three things in it.
>
> With that diff applied, **the ignore panel (432) mounts and draws for the
> first time in this client's history** — proved headless, both swap directions,
> §8.2c — the caps come from content, and a follower is told "bob has logged
> in." in content's words. Without it, 429 works and 432 is still unreachable:
> the two buttons that swap the panels are server buttons that need
> `IF_SETEVENTS`, and the caps fall back to the storage ceilings.
>
> The paragraphs below are the original design note, kept because §§1–4 are
> still the plan and were measured. Where implementation contradicted the
> design, the design text is **corrected in place and the correction says so** —
> §2.3(b), §3.5 and §4.4 are the ones that changed.
>
> **Verified independently — §8.5.** Every stage's claim was re-run rather than
> read. Three things came back different and are corrected below: the
> reproduction command in §8.2b **silently produces a client with no embedded
> server** unless one object file is deleted first (§8.5.1, and it is a trap for
> every lane, not just this feature); the host-op stage's run left the whole
> friends panel `hidden=1`, so it proved the *tree* and not the *pixels*
> (§8.5.2); and the client half had **no permanent check at all** until this
> pass added `make -C src test-social` (§8.5.3).

> The paragraphs below are the original design note, kept because §§1–4 are
> still the plan and were measured. Where implementation contradicted the
> design, the design text is **corrected in place and the correction says so** —
> §2.3(b) and §3.5's list of corrections are the ones that changed.

> Written 2026-08-01 as the first
> sections of this feature's topic doc, per `PORTING_GUIDE.md` §5.3 ("write the
> doc *with* the implementation"). §§1–7 are the plan; §8 is how it gets proven;
> §9 is what is deliberately left out. Every id, opcode, signature and line
> number below was re-measured in this worktree — nothing here is quoted from
> another doc's prose (§7, "distrust prose counts").
>
> **This lane cannot write to the content tree.** `OSRS-Content/` is a symlink
> to another lane's checkout. §2.3 is a content diff **specified but not
> applied**; the owning lane applies it. Nothing in this document was written
> to `OSRS-Content/` or `cache.osrs239/`.

---

## 0. Where the reference puts this (PORTING_GUIDE §2.2)

**LostCity puts the entire friends / ignore / private-message feature in the
ENGINE** — `engine/src/server/friend/{FriendServer,FriendServerRepository,
FriendThread}.ts` (a separate-process WebSocket service reached from the world
through a worker thread), `World.ts:1523-1631` + `1946-1998` for the call sites,
six client handlers and five server encoders under `engine/src/network/game/`.
`content/scripts/engine.rs2` — the reference's entire 511-command engine
surface — declares **zero** friend/ignore/privatechat/pm commands, and no `.rs2`
anywhere on the path. Content owns only the two interface layouts
(`player/interfaces/{friends,ignore}.if`) and the tutorial dialogue that
explains the lists.

So the engine keeping the mechanism here *is* the faithful port
(`CONTENT_ARCHITECTURE.md` §8.1). What this tree adds on top of the reference is
its own, stricter rule about strings and config-shaped constants — §2 says
exactly where those go, and it is a shorter list than usual precisely because
the reference emits **no player-facing strings at all** on this path.

---

## 1. The client half — measured, not guessed

Everything in this section was produced by

```
./3rd/rscache/tools/cs2/cs2 decompile --rev osrs239 --cache cache.osrs239 \
    --names OSRS-Content/osrs239-content/pack --out <scratch> 123 125 126 127 129 681 926 89
```

and by reading `OSRS-Content/osrs239-content/interfaces/{friends,ignore}.{if,compack}`
and `pack/3_interfaces.pack`. Ids are resolved through the pack, never copied.

### 1.1 Interfaces and mounts

| pack name | id | mounted where | by what |
|---|---|---|---|
| `friends` | **429** | `toplevel_osrs_stretch:side9` | `player/configs/gameframe.enum:43` |
| `ignore` | **432** | **nowhere** | — |
| `pm_chat` | **163** | `toplevel_osrs_stretch:pm_container` | `gameframe.enum:26` |
| `chatbox` | **162** | `toplevel_osrs_stretch:chat_container` | `gameframe.enum:23` |

`ignore` (432) is in no enum and in no clientscript. The swap between the two
panels is server-driven and is content's, not the engine's: `friends:ignore`
(429:1) carries `op1=View Ignore List` **with no `onop=`**, and `ignore:friends`
(432:1) carries `op1=View Friends List`, also with no `onop=`
(`interfaces/friends.if` `[ignore]` block, `interfaces/ignore.if` `[friends]`
block). Both are therefore server `IF_BUTTON`s.

### 1.2 The rows are `cc_create`d — the server can address none of them

`clientscript 125` (friends list body, run from `123`, the `friends:universe`
onload) builds **three dynamics per friend** into `429:11`, and
`clientscript 129` **two per entry** into `432:9`. `clientscript 926` (`163:0`
onload) `cc_create`s four dynamics per PM line and uses the static `pm1..pm5`
only as op carriers.

The consequence is the chatmenu precedent again
(`rev230-p-choice-and-string-caps`): **the server cannot `IF_SETTEXT` a single
row.** The rows come from the client's own friend store, read through CS2 host
ops. That is the whole reason §4 exists.

### 1.3 What drives a repaint — a friend-transmit listener, not a varp

`clientscript 123`, last two lines, verbatim:

```
~script125($int3, $int4, $int5, $int6, $int7, $int8, $component9, $int10, $int11);
if_setonfriendtransmit("script631(...)", $component0);
if_setonvartransmit ("script631(...){var1737}", $component0);
```

`script127` (ignore onload) is the same with `script630`. So the reactive path
is `packet → client store → FRIEND transmit → script631/630 → script125/129 →
friend_count / friend_getname / friend_getworld`. There is no varp, no varbit
and no `RUNCLIENTSCRIPT` on it. `varp 1737` is `tob_temp_transmit_2`, whose bit
31 is `has_displayname_transmitter` (varbit 8119) — already written by
`player/login.rs2:12`; that second registration is a *cheaper* repaint trigger
that already works today, and §8.2 uses it as a fallback probe.

### 1.4 What `friend_count` returning 0 looks like (measured)

Booting `manifest_osrs230_embed.ini` headless with `TORIRS_DUMP_TREE_EXIT=1`:
429 mounts, `script123` runs, `429:3` reads `"Friends List - World 0"`, the sort
tabs and scrollbar exist, `429:11` has **zero children**, and `429:13` reads the
`$count17 = 0` branch string. That is the correct empty state, not a blank
panel — so `REV230_UI_BLANK_PANELS.md` §1's ladder has already been walked and
does not apply. `"World 0"` is `map_world` (3318) returning its stub.

`script125`'s branches are worth stating because the host op must be able to
produce each one: `friend_count <= -2` and `= -1` both render *"Loading friends
list"* (or *"You must set a name before using this."* when varbit 8119 is 0);
`= 0` renders the empty-state help text; `>= 1` builds rows. `script129` uses
`ignore_count < 0` for the same loading state, and `script681` refuses with
*"Unable to update ignore list - system busy."* when `ignore_count < 0`.
**The host must therefore never answer negative once the lists are loaded.**

### 1.5 Every click except the two panel-swap buttons is a CS2 host op

| click | script | effect |
|---|---|---|
| `friends:addfriend` (429:14) op1 | cache `onop=i:103` | opens the mode-2 name prompt. No packet. |
| `friends:delfriend` (429:16) op1 | `onop=i:104` | mode 3 |
| `ignore:addignore` (432:12) op1 | `onop=i:105` | mode 4 |
| `ignore:delignore` (432:14) op1 | `onop=i:106` | mode 5 |
| Enter in that prompt | `script112` → **`script681`** | 2 `friend_add`, 3 `friend_del`, 4 `ignore_add`, 5 `ignore_del`, 6 `chat_sendprivate(%varcstring360, %varcstring359)` |
| friend row op1 "Message" | `script126` case 1 | opens the mode-6 prompt |
| friend row op2 (offline) | `script126` case 2 | `mes("That player is currently offline.")`, client-only |
| friend row op3 "Delete" | `script126` case 3 | `friend_del` |
| ignore row op1 "Delete" | `script130` case 1 | `ignore_del` |
| chat-line ops 7/8/9 | `script88` | `friend_add` / `ignore_add`, gated on `%varbit8119` |

**The cache `onop=` hooks fire without any arming.** `src/engine/uitree_from_component.c:80`
bakes a component's static `onop` into `runtime_hooks.on_op`, and
`src/ui/uitree_interact.c:32-61` dispatches it. The server-bound branch in
`src/app.c:8387-8409` is separately gated on the `IF_SETEVENTS` mask, so a
component the server never armed produces a good menu row and sends nothing.

**Therefore only two components in this whole feature need `IF_SETEVENTS`:
`friends:ignore` and `ignore:friends`.** (This corrects the discovery pass,
which listed `429:14/16` and `432:12/14` as needing arming — they do not; they
are pure CS2.)

---

## 2. Engine / content split

### 2.1 Engine gets the mechanism

- The friend service: the roster keyed by base-37 name, presence, the
  visibility rule, the caps *check*, PM relay (§5).
- The wire: five inbound decoders' table rows + six outbound rows, and the
  server-side encoders/handlers (§3).
- The CS2 host ops and the friend-transmit hook channel (§4).
- Base-37 codec, wordpack, the one-social-packet-per-tick throttle.

### 2.2 Content gets policy, the panel swap, and every string

The reference emits **zero** player-facing strings here; every one the player
sees in 429/432/163 is already in the cache's own CS2 (`"That player is
currently offline."`, `"Unable to complete action - system busy."`, `"You must
set a name before you can chat."`). So content's share is small but real:

1. **Arming + the friends↔ignore swap.** Two `if_setevents` and two
   `[if_button]` triggers.
2. **The list caps.** LostCity's are `members ? 200 : 100` friends
   (`FriendServerRepository.ts:231`) and 100 ignores (`:268`). Those are
   config-shaped constants and must not be C literals (§2.4 item 3). They go in
   a content `.enum` read by the engine through `ToriRSServer_ContentEnum()` — the
   same seam `torirs_server_bank.c:555` uses for `bank_tabs` and
   `torirs_server_equipment.c:112` for `worn_slots`.
3. **The one string this era adds that LostCity never had.** At rev 230 the
   *"X has logged in."* notification is a `MESSAGE_GAME` with chattype 5, sent
   by the server. It is a player-facing string, so it is content's, reached the
   way `torirs_server_world.c:2047` already reaches `[proc,nothing_interesting_message]`
   — `ToriRSServer_Say`, documented in `torirs_server.h:2308` as being for exactly this
   ("a message content should word but only the engine knows a name for").

### 2.3 The content diff — SPECIFIED HERE, NOT APPLIED

> **Superseded 2026-08-01 by
> [`FRIENDS_PRIVATE_CHAT_CONTENT.md`](FRIENDS_PRIVATE_CHAT_CONTENT.md).** That
> file is the applyable diff: it has been compiled and run against a scratch
> copy of the content tree, it corrects three things below, and it carries the
> verification recipe and the opcode-surface shortfall list. Apply that one.
>
> The three corrections, so this section is not read on its own:
>
> 1. **`^private_message_max_chars` is now `^private_message_max_bytes`**, and
>    the engine accessor with it (`ToriRSServer_FriendsCapPmBytes`). The cap counts
>    *packed* wire bytes — `MessagePrivateHandler.ts:13` tests `input.length` on
>    the raw slice, before `WordPack.unpack` — which is about two typed
>    characters per byte. The old name lied by a factor of two.
> 2. **The two `[if_button]` handlers do not re-arm.** Both `if_setevents` calls
>    belong in `[proc,friends_login]` and nowhere else: `App_IfEventsSet` keys a
>    flat table by component id and never clears an entry, so arming survives a
>    mount and `ignore:friends` can be armed while 432 is mounted by nobody.
>    Measured, and the embed test goes red if either line is dropped.
> 3. **(c)'s two presence lines are reached through `FRIENDLOGIN` /
>    `FRIENDLOGOUT` triggers**, not a named-proc hook table — content binds
>    `[friendlogin,_]` / `[friendlogout,_]` (`CONTENT_ARCHITECTURE.md` §8.6).
>    Their engine call site is `social_notify_followers` in `torirs_server_world.c`,
>    which also owns the three rules content cannot express: who hears it,
>    `isVisibleTo`, and not the subject of the sentence.

This lane does not own `OSRS-Content/`. The owning lane applies the following.

**(a) New file `server/scripts/interface_friends/scripts/friends.rs2`**

```
// The friends and ignore side panels (429 `friends`, 432 `ignore`).
//
// Everything both panels *draw* is the client's: clientscripts 123/125 and
// 127/129 build every row with cc_create off the client's own friend store,
// which the server feeds with UPDATE_FRIENDLIST / UPDATE_IGNORELIST. There is
// no if_settext the server could usefully send.
//
// Two things are the server's, and both are here:
//
//   1. `ignore` is mounted by nobody. It is in no gameframe slot and no
//      clientscript references it. The two panels swap through a pair of
//      server buttons -- `friends:ignore` and `ignore:friends` each carry an
//      op1 with no onop=, which at rev 230 means "ask the server".
//   2. Those two ops are inert until if_setevents says otherwise. Every other
//      button on both panels carries its own cache `onop=` and needs no arming.

[proc,friends_login]
if_setevents(friends:ignore, 0, 0, ^if_event_op1);
if_setevents(ignore:friends, 0, 0, ^if_event_op1);

// Swap the side9 slot's contents. if_opensub over the same slot is the shape
// the journal tab strip already uses (interface_journal/scripts/journal.rs2);
// an if_closesub here would hide the slot, not the group -- see the
// if-closesub-slot-poison note.
[if_button,friends:ignore]
if_opensub(toplevel_osrs_stretch:side9, ignore, 1);
if_setevents(ignore:friends, 0, 0, ^if_event_op1);

[if_button,ignore:friends]
if_opensub(toplevel_osrs_stretch:side9, friends, 1);
if_setevents(friends:ignore, 0, 0, ^if_event_op1);
```

**(b) New file `server/scripts/interface_friends/configs/social.constant`**

> **Corrected 2026-08-01, when the service landed.** The design said `.enum`,
> with `inputtype=string` offered as an "equally readable" alternative. Both are
> wrong, and the measurement is in `torirs_server_content.c`:
> `struct ToriRSServerEnumValue` has an `int key` and **no key text**
> (`torirs_server_content.h:412-420`), and a `val=` key goes through `enum_operand`,
> which for a non-pack input kind is `atoi(text)` (`torirs_server_content.c:1438`).
> So `inputtype=string` does not fail — it silently maps *every* key to 0.
> That leaves the int-keyed form, which forces the C side to know that key 0
> means `friend_max`: an index guessed from prose, which the design itself
> named as the thing to avoid.
>
> `.constant` is name-keyed by construction and is the seam
> `CONTENT_ARCHITECTURE.md` §8.2(e) already established for exactly this — a
> tuning number the engine reads (`^lootdrop_duration`). So:

```
// List caps. The reference's are engine literals
// (FriendServerRepository.ts:231,268, MessagePrivateHandler.ts:13); here they
// are content, because a cap is a config-shaped constant. The engine reads them
// by name, the way ^lootdrop_duration is read.
^friend_max = 100
^ignore_max = 100
^private_message_max_chars = 100
```

`friend_max_members` (the reference's `account.members ? 200 : 100`) is **not**
in the list and the engine does not read one. There is no account model here and
nothing carries a members bit, so a second cap would be a number no code path
could ever select. When an account model exists, that is the moment to add it.

Until this file lands the engine says so, once, at boot:

```
torirsserver: 3 friend/ignore cap(s) are not in any .constant — the lists are limited
only by this server's array sizes. Content owns these: see
docs/FRIENDS_PRIVATE_CHAT.md §2.3.
```

and falls back to its storage ceilings (256 friends, 128 ignores, 255 PM bytes —
the last being the var-u8 packet's own limit). That is deliberate: a missing cap
degrades to "as many as the array holds", never to a policy number invented in
C. It is also why the caps are **not** in `ToriRSServer_Ids`'s resolve table, which
reports through the content error count and would fail `ToriRSServer_Pack
--check-only` for every other lane until this file exists. Moving them there is
the right follow-up **on the day the constants land**.

**(c) Append to `server/scripts/player/messages.rs2`**

```
// ------------------------------------------------------------------
// Friend presence
// ------------------------------------------------------------------

// The reference never sends these -- its 2004 client derived them itself from
// UPDATE_FRIENDLIST world-id transitions. At rev 230 the notification is a
// server MESSAGE_GAME, so the sentence is the server's to word, so it is
// content's. The engine calls these by name (ToriRSServer_Say), the way
// [proc,nothing_interesting_message] is already called.
[proc,friend_login_notification](string $name)
mes("<$name> has logged in.");

[proc,friend_logout_notification](string $name)
mes("<$name> has logged out.");
```

**(d) One line in `server/scripts/player/login.rs2`**, beside the other
`~*_login` arming calls (`login.rs2:42-48`):

```
~friends_login;
```

**Nothing else.** In particular: no `[proc,friend_add_failed_message]` and no
"you are already friends with" text. The reference is silent on every failure
path (`FriendServerRepository.addFriend` returns bare, and the reference even
*commented out* its own `console.error`s). Inventing a message here would be
inventing a behaviour the reference does not have.

### 2.4 One thing content cannot express, reported rather than worked around

The login notification wants chattype **5** (`LOGINLOGOUTNOTIFICATION`), which
this client already models as `RS_CHAT_TYPE_PRIVATE_SYSTEM`
(`src/game/rs_chat.h:35`) and already renders and filters
(`rs_chat_widgets.c:68`, `rs_chat.c:102`). ServerScript's `mes` (`SS_OP_MES`,
`torirs_server_scripts.c:1346`) writes type 0 and there is no typed variant.

Per §2.4 item 4 this is reported, not routed back into C: **the opcode surface
is the bug.** The first landing uses plain `mes` (type 0 — correct text, black
instead of cyan, no auto-expire); a `mes_type`-style opcode is a one-line
follow-up for whichever lane owns `ToriRSServer_Ops_*.c`. This lane is explicitly
forbidden from adding those files, so it does not.

---

## 3. The wire

### 3.1 Doctrine

`src/net/rev/osrs230/packetin.h:5-25` and `packetout.h:5-24` both state it:
opcode *numbers* are RSProt's where a real one is known; **payload layouts are
the lc254 ones**, because `src/torirsserver` is the only producer and what matters
is that the two ends agree. This feature follows that, and every layout below is
the one **this repo's own decoder already implements** — which is also, checked
row by row, the one `LostCity_Server/engine/src/network/game/server/codec/*Encoder.ts`
writes.

### 3.2 Server → client

| op | table today | change | payload | authority |
|---|---|---|---|---|
| **56** UPDATE_FRIENDLIST | `{56, VARU16, PKT_NAME_NONE}` (`packetin.h:89`) | name it | `p8 name37`, `p1 world` (exactly 9) | `gameproto_parse.c:641-647` (asserts full consumption); `UpdateFriendListEncoder.ts` |
| **21** UPDATE_IGNORELIST | `{21, VARU16, NONE}` (`:90`) | name it | `p8 name37` × `len/8` | `gameproto_parse.c:632-640`; `UpdateIgnoreListEncoder.ts` |
| **15** FRIENDLIST_LOADED | `{15, 0, NONE}` (`:88`) | **length 0 → 1** and name it | `p1 status` (0 loading, 1 connecting, 2 online) | `gameproto_parse.c:648-653` reads `g1` then asserts — leaving the length at 0 aborts on the first packet in a debug build |
| **29** MESSAGE_PRIVATE | `{29, VARU16, NONE}` (`:77`) | name it | `p8 from37`, `p4 messageId`, `p1 staffMod`, wordpack over `len-13` | `gameproto_parse.c:370-380`; `MessagePrivateEncoder.ts` |
| **3** CHAT_FILTER_SETTINGS | **absent** | **new row**, fixed 3 | `p1 public`, `p1 private`, `p1 trade` | `gameproto_parse.c:382-388`; `ChatFilterSettingsEncoder.ts` |

Opcode 3 is free: the inbound table's used set, computed from `packetin.h`, is
`{0,2,6,7,10,12,15,20,21,22,23,27,29,35,36,37,38,41,47,52,53,55,56,57,59,60,65,68,70,71,72,73,75,76,77,80,82,84,88,90,94..98,102..104,106,108,114,120..128}`.
Staying under 128 avoids the 2-byte pSmart form the framer does not need.

### 3.3 Client → server

All six builders already exist and work; **none has a rev-230 opcode**, so
`packetout_code_osrs230()` returns −1 and `out_begin` refuses. That is why
`src/app.c`'s existing friend-add/del sends have always written nothing at rev
230.

| canonical name | assigned op | length | body the existing builder writes |
|---|---|---|---|
| `PKTOUT_NAME_FRIENDLIST_ADD` | **3** | 8 | `p8 name37` (`net_out.c:917` → `out_name37` `:900`) |
| `PKTOUT_NAME_FRIENDLIST_DEL` | **4** | 8 | ditto (`:928`) |
| `PKTOUT_NAME_IGNORELIST_ADD` | **5** | 8 | ditto (`:939`) |
| `PKTOUT_NAME_IGNORELIST_DEL` | **6** | 8 | ditto (`:950`) |
| `PKTOUT_NAME_CHAT_SETMODE` | **7** | 3 | `p1 public`, `p1 private`, `p1 trade` (`:882`) |
| `PKTOUT_NAME_MESSAGE_PRIVATE` | **8** | VARU8 | `p8 to37` + wordpack (`:832`) |

3–8 are free: the outbound used set is
`{0,9,10,11,13,15..22,24,25,26,30..37,40..49,54,55,57..59,61..64,71,75,76,86,90..100}`.
14 and 16 are the login driver's and are avoided (`packetout.h:9-14`).

### 3.4 Two decode consequences the implementer must not skip

- **`MESSAGE_GAME` currently throws its type byte away.** `torirs_server_encode.c:662`
  writes `rsab_p1(&buf, 0)` and the rev-230 override at
  `src/net/rev/osrs230/osrs230_parse.c:381-393` does `memcpy(..., data + 1, ...)`,
  so `rs_gameproto_exec.c:505` files everything as `RS_CHAT_TYPE_GAME`. The
  login notification cannot exist until `struct PktMessageGame` carries the type
  and exec passes it through. The rest of the chat model already handles types
  3/5/6/7 (`rs_chat_widgets.c:46-80`, `rs_chat.c:82-107`).
- **`ToriRSServer` does not link wordpack.** `TORIRSSERVER_CORE_SRCS`
  (`src/makefile:251-264`) has `net/jbase37.c` only, and only via `TORIRSSERVER_SRCS`
  (`:859`). The fix is one line — add `net/wordpack.c` beside `net/jbase37.c` in
  `TORIRSSERVER_SRCS`, for the same reason the comment at `:855-858` gives (the embed
  test and `EMBED_SERVER=1` already link it out of `NET_CORE_OBJS`
  (`:672`) / the client's own `SRCS` (`:220`), so putting it in `TORIRSSERVER_CORE_SRCS`
  would be a duplicate symbol). Rejected alternative: a plaintext override in
  `osrs230_parse.c` — the client's *outbound* builder wordpacks, so a plaintext
  inbound would make the two halves disagree, and the reference filters and
  length-caps the decoded text anyway.

### 3.5 As landed

Everything in §3.1–3.4 landed as written. What follows is what the tables above
could not say, plus the two things the plan got wrong.

**Files.**

| file | what changed |
|---|---|
| `src/net/rev/osrs230/packetin.h` | 29/56/21 named; **15's length 0 → 1**; new `{ 3, 3, CHAT_FILTER_SETTINGS }` row |
| `src/net/rev/osrs230/packetout.h` | the six social rows at opcodes 3–8 |
| `src/torirsserver/torirs_server_encode.c` | five encoders + five opcode-enum/name rows |
| `src/torirsserver/torirs_server.h` | the five `ToriRSServer_Send_*` declarations |
| `src/torirsserver/torirs_server_world.c` | the social section (~350 lines): three send helpers, the login dump, three handlers, six `k_packet_routes` rows, and the logout broadcast |
| `src/makefile` | `net/wordpack.c` into `TORIRSSERVER_SRCS` |
| `src/torirsserver/test/embed_test.c` | `absorb_social` + three send helpers + 23 two-player assertions |

**Nothing in `src/net/rev/gameproto_parse.c` or `src/game/rs_gameproto_exec.c`
had to change.** Both already decoded and filed all five packets; they had
simply never received one, because no rev-230 opcode mapped to them. That is why
the client half of §3 is a table edit and not code.

**The three parse asserts are the test.** `FRIENDLIST_LOADED`,
`UPDATE_FRIENDLIST` and `CHAT_FILTER_SETTINGS` all end with
`assert(buffer.position == data_size)`, and `src/makefile` sets no `-DNDEBUG`.
A field out of place is a client abort, not a drawing bug — which is also the
positive evidence that the layouts are right: the headless client run in §8.2
received all three and did not abort.

**Two corrections to §3.**

1. **§3.2's "leaving 15's length at 0 aborts on the first packet" is not what
   happens** — measured by putting the 0 back. `ToriRSServer_Send`'s own
   `check_frame_length` catches it first and prints
   `op 15 (FRIENDLIST_LOADED) wrote 1 bytes, client frames it as 0`, and the
   client then frames a 0-length packet and takes the status byte as the next
   opcode: a stream desync, which showed up as 8 unrelated `embed_test`
   failures several packets later. The fix and the reason for it are unchanged;
   the *symptom* is a desync, not an assert.
2. **`socialProtect` is taken before the name is validated**, where the
   reference validates first. The service owns the base-37 round-trip, so
   asking it "is this name valid" before asking it to act would mean a second
   public entry point whose only purpose is to be asked first. The consequence
   is that eight bytes of garbage spend the sender's one social packet for that
   tick — stricter than the reference, never looser. Recorded in
   `handle_social_list`'s comment.

**Three judgement calls.**

- **`CHAT_FILTER_SETTINGS` is sent at login as well as on echo**, which the
  reference does not do (its 2004 client kept the modes locally). At rev 230 the
  CS2 side asks through `chat_getfilter_*` and the server is the only source, so
  a client that was never told starts out disagreeing with the server about its
  own filters.
- **The login dump is step 5b of `ToriRSServer_WorldLogin`, after the panels are
  mounted**, not before. The friends panel builds its rows from the store on its
  onload; a store already populated when 429 mounts draws on the first paint,
  where a store filled afterwards needs the repaint channel of §4.3, which does
  not exist yet.
- **The logout broadcast is at the *end* of `ToriRSServer_WorldRemovePlayer`**,
  after `player->active = 0`. Broadcasting earlier asks "is bob online" while
  bob's slot is still active and tells every follower the world of a player who
  has just left.

**One structural limit, stated because it will bite.** The friend roster is
*process*-scoped (it has to outlive a session — §5.3 decision 1) but *delivery*
is world-scoped: `social_player_by_name37` scans this `ToriRSServer`'s player
pool. So a name can be online to the roster with no player slot to send to.
Every send tolerates that and none treats it as an error. In the socket server,
which accepts one connection at a time, this means cross-player delivery is
only exercised by the embed harness — which is where §8.3's assertions live.

---

## 4. CS2 host ops

**Host implementations existing today: zero.** `src/cs2vm2/cs2vm2_host.h` has
179 request kinds and not one is social or chat; `src/game/rs_cs2_host.c` has no
friend/ignore/chat case.

### 4.1 The two failure regimes

`CS2VM2_Op_StackMetaStub` (`cs2vm2.c:6751`) is the only fallback these families
reach, and it branches on `known` in `cs2vm2_opcode_stack.gen.h`:

- `known == 0` → `assert(0 && "unimplemented CS2 opcode reached StackMetaStub")`
  at `cs2vm2.c:6838`. `src/makefile:58` sets no `-DNDEBUG`, so this is a live
  SIGABRT (exit 134) that reads exactly like a hang
  (`rev230-xp-drops-stat-transmit`).
- `known == 1` → silent no-op with a *recorded* shape. If the shape is wrong,
  the stack desyncs and the script dies at an unrelated later opcode.

**`cs2vm2_opcode_stack.gen.h` is not trustworthy for this family.** It is
derived heuristically by `gen_opcode_stack.py:414` (`if name.startswith("FRIEND_")
… return (1,0,0,1) if "NAME" in name else (1,0,1,0)`) from
`cs2_opcode_meta.c`, which carries only name + operand kind. The authority is
`3rd/rscache/src/cs2/cs2_command.gen.h`'s proto pool, which is what the
decompiler reads — hence `script125` correctly showing
`$string0, $string1 = friend_getname(...)`.

### 4.2 The op table

Signatures below are read out of `cs2_command.gen.h:592-614, 788-802` and
resolved through `cs2_types.c` (`username`/`mes` → STRING; `index`, `count`,
`world`, `rank`, `boolean`, `chatfilter` → INT). Notation
`(int_in, str_in, int_out, str_out)`.

| op | name | true shape | gen.h today | today's behaviour |
|---|---|---|---|---|
| 3600 | `friend_count() -> count` | (0,0,1,0) | `{0,0,1,0,1}` ✓ | pushes 0 → empty state |
| 3601 | `friend_getname(index) -> username, username` | (1,0,**0,2**) | `{1,0,0,1,1}` | **one string short per row** |
| 3602 | `friend_getworld(index) -> world` | (1,0,1,0) | ✓ | 0 = offline |
| 3603 | `friend_getrank(index) -> rank` | (1,0,1,0) | ✓ | 0 |
| 3605 | `friend_add(username)` | (**0,1**,0,0) | `{1,0,1,0,1}` | pops the wrong stack, pushes a phantom int |
| 3606 | `friend_del(username)` | (**0,1**,0,0) | `{1,0,1,0,1}` | ditto |
| 3607 | `ignore_add(username)` | (0,1,0,0) | `{0,0,0,0,0}` | **ASSERT** |
| 3608 | `ignore_del(username)` | (0,1,0,0) | `{0,0,0,0,0}` | **ASSERT** |
| 3609 | `friend_test(username) -> boolean` | (**0,1**,1,0) | `{1,0,1,0,1}` | transposed, 10 call sites |
| 3621 | `ignore_count() -> count` | (0,0,1,0) | ✓ | 0 |
| 3622 | `ignore_getname(index) -> username, username` | (1,0,**0,2**) | `{0,0,0,0,0}` | **ASSERT** |
| 3623 | `ignore_test(username) -> boolean` | (0,1,1,0) | ✓ | false |
| 3318 | `map_world() -> world` | (0,0,1,0) | ✓ | 0 → header reads "World 0" |
| 5000/5005/5016 | `chat_getfilter_{public,private,trade}() -> chatfilter` | (0,0,1,0) | ✓ | 0 |
| 5001 | `chat_setfilter(3× chatfilter)` | (**3,0**,0,0) | `{0,0,1,0,1}` | pushes a phantom int, drops 3 |
| 5009 | `chat_sendprivate(username, mes)` | (**0,2**,0,0) | `{0,0,1,0,1}` | **the PM send path** — leaves both strings |
| 2420 | `if_setonfriendtransmit` | listener | `known=0` | parsed then **discarded** (`cs2vm2.c:8429`, the `IF_SetOnEventDiscard` group) |
| 1420 | `cc_setonfriendtransmit` | listener | `known=0` | **ASSERT** — the CC discard group at `cs2vm2.c:8384-8415` lists `CC_SETONCLANTRANSMIT` and omits this one. A one-line omission; script 4376 reaches it. |
| 3628-3639, 3640-3643 | friends/ignore sort spec | balanced | ✓ | no-ops; order ends up as insertion order |

Ten of those shapes are wrong or unknown. The fix is `MANUAL_STACK` entries in
`gen_opcode_stack.py` (which already carries the hand-fixed 3600/3621/3623) plus
real handlers with their own pops — not one or the other.

`5015 chat_playername` (39 call sites) has a correct shape and stubs to `""`;
it is not on this path and is left alone. Likewise `5017/5019/5030/5031` chat
history: the rev-230 chatbox scrollback is filled from C
(`src/game/rs_chat_widgets.c`), bypassing scripts 84/89 entirely, and script
89's loop is gated behind `chat_gethistorylength(14) > 0`, which the stub answers
0. Latent, not live — **not part of this feature**, and 5030/5031's real
8-value returns are recorded here so the next lane does not rediscover them.

### 4.3 The friend-transmit channel

`if_setonfriendtransmit` takes no trigger arguments, so it is structurally
identical to the **misc-transmit** channel that already exists — one dirty flag,
every registered hook re-runs. That is the template, end to end:

| misc (exists) | friend (to add) |
|---|---|
| `CS2VM_HOST_REQUEST_IF_SETONMISCTRANSMIT` (`cs2vm2_host.h:116`) | `..._IF_SETONFRIENDTRANSMIT` |
| `CS2VM2_Op_IF_SetOnMiscTransmit` (`cs2vm2.c:4413`) | `CS2VM2_Op_IF_SetOnFriendTransmit` |
| `runtime_hooks.on_misc_transmit` (`uitree.h:202`) | `on_friend_transmit` |
| slot resolver `rs_cs2_host.c:2962` | one more case |
| registration `rs_cs2_host.c:4563` | one more case |
| `host->misc_transmit_dirty` (`rs_cs2_host.h:215`) | `friend_transmit_dirty` |
| `CreateTask_CS2MiscTransmitDispatch` (`task_cs2_run.c:1865`) | `...FriendTransmitDispatch` |
| pumped at `rs_cs2_dispatch.c:249` | one more branch |

Without this, the list paints once at mount and never again, however correct
everything else is.

### 4.4 As landed

**Everything in §4.1–4.3 landed, and §4.2's table was wrong in more places than
it said.** The corrections below were all produced the same way, and the method
matters more than any individual row: `3rd/rscache/src/cs2/cs2_command.gen.h`
carries an `(args_offset, args_count, rets_offset, rets_count)` per opcode
indexing `cs2_proto_pool[]`, and every proto resolves through `cs2_types.c` to a
base type. STRING vs not-STRING on each side **is** the stack shape. That is the
table the decompiler reads, which is why `cs2 decompile` prints
`$string0, $string1 = friend_getname($int)` while `cs2vm2_opcode_stack.gen.h`
believed one string came back.

Running that comparison across the whole FRIEND_/IGNORE_/CLAN_/CHAT_ family
found **23** opcodes whose generated shape disagreed with the authority, not the
ten §4.2 listed:

| opcode | true (int_in, str_in, int_out, str_out) | was |
|---|---|---|
| 3601 `friend_getname` | (1,0,0,2) | (1,0,0,1) |
| 3604 `friend_setrank` | (1,1,0,0) | (1,0,1,0) |
| 3605 `friend_add` | (0,1,0,0) | (1,0,1,0) |
| 3606 `friend_del` | (0,1,0,0) | (1,0,1,0) |
| 3607 `ignore_add` | (0,1,0,0) | **unknown → assert** |
| 3608 `ignore_del` | (0,1,0,0) | **unknown → assert** |
| 3609 `friend_test` | (0,1,1,0) | (1,0,1,0) |
| 3617 `clan_kickuser` | (0,1,0,0) | (1,0,1,0) |
| 3619 `clan_joinchat` | (0,1,0,0) | (1,0,1,0) |
| 3622 `ignore_getname` | (1,0,0,2) | **unknown → assert** |
| 5001 `chat_setfilter` | (3,0,0,0) | (0,0,1,0) |
| 5002 `chat_sendabusereport` | (2,1,0,0) | (0,0,1,0) |
| 5003 `chat_gethistory_bytypeandline` | (2,0,3,3) | (2,0,0,1) |
| 5004 `chat_gethistory_byuid` | (1,0,3,3) | (2,0,0,1) |
| 5008 `chat_sendpublic` | (1,1,0,0) | (0,0,1,0) |
| 5009 `chat_sendprivate` | (0,2,0,0) | (0,0,1,0) |
| 5010 `chat_sendclan` | (2,1,0,0) | (0,0,1,0) |
| 5018 `chat_getnextuid` | (1,0,1,0) | (0,0,1,0) |
| 5019 `chat_getprevuid` | (1,0,1,0) | (0,0,1,0) |
| 5021 `chat_setmessagefilter` | (0,1,0,0) | (0,0,1,0) |
| 5024 `chat_settimestamps` | (1,0,0,0) | (0,0,1,0) |
| 5030 `chat_gethistoryex_bytypeandline` | (2,0,4,4) | (2,0,0,1) |
| 5031 `chat_gethistoryex_byuid` | (1,0,4,4) | (2,0,0,1) |

All 23 are `MANUAL_STACK` entries in `src/cs2vm2/gen_opcode_stack.py`, and the
regenerated header changes exactly 23 rows. **The real fix is for that generator
to read the proto pool instead of guessing from opcode names** — the single
heuristic line `if name.startswith("FRIEND_") or name.startswith("CLAN_")`
produced most of this — but that would re-shape several hundred opcodes at once
and is deliberately not done here. Recorded as the follow-up.

**The op table, as implemented** (`CS2VM2_Op_Social` / `CS2VM2_Op_Chat` in
`cs2vm2.c`, `exec_social` / `exec_chat` in `rs_cs2_host.c`). Pops are written
out per opcode rather than driven off the generated table: three shapes live in
this family, two of them push two strings, and spelling the shape at the call
site is what makes a future mismatch a visible edit instead of a silent desync.

| op | behaviour |
|---|---|
| 3600 `friend_count` | −1 until `FRIENDLIST_LOADED` says 2, then the real count |
| 3601 `friend_getname` | display name, then `""` (no rename model — see §10.3) |
| 3602 `friend_getworld` | 0 = offline |
| 3603 `friend_getrank` | 0; no rank model, and 429 never reads it |
| 3605/3606/3607/3608 | local store mutation **and** a queued packet — both |
| 3609/3623 `*_test` | base-37 keyed, so "bob"/"Bob"/"B o b" are one player |
| 3621 `ignore_count` | never negative |
| 3622 `ignore_getname` | display name, then `""` |
| 3318 `map_world` | `RS_Social.node_id` (1). Header now reads "Friends List - World 1" |
| 5000/5005/5016 | read `RS_UISlots.chat_filter_mode` **by pointer**, not a copy |
| 5001 `chat_setfilter` | writes those three and queues CHAT_SETMODE |
| 5009 `chat_sendprivate` | queues MESSAGE_PRIVATE + echoes "To X:" locally |
| 2420 `if_setonfriendtransmit` | registered for real; out of the discard group |
| 1420 `cc_setonfriendtransmit` | added **to** the CC discard group; no longer asserts |

**The three judgement calls.**

1. **The mutators do both halves — local store *and* packet.** This is the
   reference client's own behaviour (`Client.ts` `addFriend`/`delFriend`/
   `addIgnore`/`delIgnore`), and neither half is redundant: the server echoes
   `UPDATE_FRIENDLIST` for an add, but it sends **nothing at all** for a delete
   or an ignore-add (§3.5's handler table), so a client that waited for the
   server would show a deleted friend forever.

2. **No message on any failure path.** Not "your friend list is full", not "X is
   already on your friend list", not "you cannot add yourself" — all of which
   the 2004 client emits from Java. Those are game-facing strings, so they are
   content's (§2.4 item 2), and there is no content proc for them. The refusals
   are silent, which is what the *server* reference does on the same paths.
   **This is a deliberate gap, not an oversight**; it is the one place where
   this port is quieter than the client it is copying.

3. **A send queue, not a send.** `RS_CS2Host` has no network pointer and gains
   none. `friend_add` parks a `struct RS_CS2SocialSend` and `App_Tick` drains it
   into `net_out_*`, which is the shape `close_modal_requested` and
   `logout_requested` already use. It is a queue rather than a flag because
   script 681 can issue a `chat_setfilter` and a `chat_sendprivate` in one run.

**Two structural changes beyond the op table.**

- `struct RS_Social` now carries a **base-37 hash per entry**, as the reference
  does (`friendUserhash`). Names reach this store in three spellings — from a
  packet ("bob"), from a chat line ("Bob"), from a typed prompt ("B o b") — and
  `strcasecmp` on the raw text made those three different players. `friend_del`
  from the panel is keyed by the display name the panel itself drew, so this is
  on the live path, not a hypothetical. `RS_Social_DisplayName` (the reference's
  `toScreenName`: underscores to spaces, words capitalised) is applied on the
  way out, so the store stays raw and only what a player reads is formatted.
- **`RS_Social_SeedDefaults` is deleted.** Four hardcoded player names in C
  (`Durial321`, `Zezima`, …) were content in C, and with the host ops live they
  stopped being invisible: they would have been the first four rows of a real
  friends panel.

### 4.5 A CS2 bug this feature surfaced, and fixed: `event_opbase`

`event_opbase` is the one CS2 event local that is a **string**, so it travels in
a hook's argument list as its own literal name rather than as one of the
`CS2VM_SCRIPT_ARG_*` int sentinels `task_cs2_set_int_local` substitutes. Nothing
substituted it.

Every friends and ignore row is built as
`cc_setopbase("<col=ff9040><name></col>")` followed by
`cc_setonop("script126(event_opindex, event_opbase, ...)")`, and script 126
opens the private-message prompt on `removetags(` that `)` — the `removetags`
is there *because* the value arrives with the colour tags on, which is the
strongest possible confirmation of what the substitution should be. Left
unsubstituted, clicking "Message" on a friend addressed a player literally
called `event_opbase`; the mock server logged
`<- MESSAGE_PRIVATE to=event_opbase (offline; dropped)`.

Fixed in `task_cs2_run.c` beside the int substitutions. It is not friends-
specific — every `cc_setopbase` + `cc_setonop` pair in the cache was affected.

---

## 5. The friend service (engine)

### 5.1 What is ported

`FriendServer`'s *message vocabulary*, as ordinary in-process function calls in
one new module. The nine `FriendsClientOpcodes` and three `FriendsServerOpcodes`
become ~12 functions; the reference's call-site list is the real contract and is
kept: login, reconnect, logout, chat-setmode, the four list mutations, and PM.

The service is keyed by **base-37 name**, not by player slot, and it must be:

- `friends[]` and `ignores[]` per name, for names that are **not logged in** —
  the reference's `getFollowers()` walks every entry to answer "who has alice in
  their list", and a per-`ToriRSServerPlayer` array structurally cannot answer that.
- `private_mode` per name (volatile, pushed up at login), and `world` (0 =
  offline / not visible, non-zero = online).

Rules ported unchanged, as mechanism:

- **`isVisibleTo`** (`FriendServerRepository.ts:332-355`), the whole
  social-visibility policy: staff bypass → ignored-by-target → `OFF` invisible →
  `FRIENDS` visible iff mutual → else visible; unknown player defaults OFF.
- **`socialProtect`** — one social packet per tick, cleared in `resetEntity`.
  The modern client can spam these harder than the 2004 one.
- **`UPDATE_FRIENDLIST`'s dual use** — the full dump at login, single-entry
  deltas afterwards — followed by `FriendlistLoaded(2)`.
- **`pmId` must be non-zero.** `World.pmCount` starts at 1 with the comment
  *"can't be 0 as clients will ignore the pm, their array is filled with 0 as
  default"* (`World.ts:167`), and this client's dedupe ring
  (`app->pm_message_ids[100]`, `rs_gameproto_exec.c:731-738`) is zero-filled and
  would swallow it.
- **The caps as a check**, with the numbers from content (§2.3b), not literals.
- **PM text length cap** (`MessagePrivateHandler.ts:13`), likewise from content.

Three things the reference deliberately does *not* do, which this port also does
not do: incoming PMs are **not** filtered server-side by the recipient's ignore
list (the client drops them — and this client does not yet, see §7); `privateChat`
mode gates online *visibility*, not PM delivery; `publicChat`/`tradeDuel` are
stored and echoed and never read.

### 5.2 What is deliberately NOT ported

`ToriRSServer` is single-process and single-world (`grep -rln pthread_create
src/torirsserver/` returns nothing; `torirs_server_main.c:252-262` runs one session to
completion before accepting the next). Everything below exists in the reference
only to survive a multi-process 2004 deployment, and porting its *shape* is
exactly the over-port §5.1 step 4 warns against:

1. The separate process, `WebSocketServer`, `InternalClient`, JSON-over-WS
   framing, `Environment.FRIEND_HOST/PORT/SERVER`, and the `FRIEND_SERVER=false`
   degenerate path.
2. `FriendThread.ts` and `postMessage` — it exists to keep DB I/O off the tick.
3. Multi-world lookup: `socketByWorld`, `playersByWorld[worldId][2000]`,
   `WORLD_CONNECT`, `getPlayerWorldSocket`, `WORLD_PLAYER_LIMIT = 2000`, the
   linear slot scan, `NODE_ID << 24` in `pmId`. **But the wire semantics stay**:
   `UPDATE_FRIENDLIST`'s world byte is 0 for offline and non-zero for online,
   and the client draws "World N" from it — in a one-world server that is a
   constant vs 0, not a lookup. The constant is `RS_Social.node_id`, which
   `rs_social.c:13` already initialises to 1, and which `map_world` (3318) will
   return.
4. The nine `RELAY_*` opcodes. The reference's own source calls them a temporary
   squat on the friend socket (`FriendServer.ts:25`); they are an admin bus, and
   `RELAY_QUEUESCRIPT` spells a script name over the wire, which §2.4 item 5
   forbids here.
5. `profile` scoping and the kysely `DB_BACKEND` sqlite/mysql branch.
6. `PUBLIC_CHAT_LOG` / the `private_chat` and `public_chat` moderation tables.
   They live in the friend server only because it holds the DB handle.
7. `fromBase37(name) === 'invalid_name'` ⇒ 48-hour automated ban. The
   *rejection* is ported; the ban and `172800000` are moderation policy and a
   config-shaped constant.

### 5.3 As landed

`src/torirsserver/ToriRSServer_Friends.{c,h}`, ~560 lines including the prose. What it
holds and what it deliberately does not:

| in the module | not in the module |
|---|---|
| the roster, keyed by name37 | any encoder, any opcode, any packet |
| presence (login / logout / world) | the login list dump and the follower broadcast — those are *packets*, so they belong with the wire |
| the three chat modes, single copy | a varp mirror of them |
| the four mutations + the caps | any message to the player on a failure path |
| `isVisibleTo`, ported line for line | multi-world lookup |
| `getFollowers` | the ignore filter on *incoming* PMs (the reference does not do it either — the client does) |
| non-zero pm ids | the wordpack, the length gate's caller |
| `socialProtect`, as a gate function | the six handlers that will call it |

Four decisions worth stating, because each could reasonably have gone the other
way:

1. **The store is a file-static, not a field on `struct ToriRSServer`.**
   `serve()` (`torirs_server_main.c:115`) `memset`s the world at the top of every
   connection, so a roster on the world would be erased between two sessions of
   one process — which is precisely the case this service exists for (alice
   adds bob while bob is offline). The reference gets process scope for free by
   being a different process. `ToriRSServer_FriendsReset()` is the only thing that
   clears it, and it is **not** called from `ToriRSServer_WorldReset`.

2. **Entries are created by being *mentioned*, and never recycled.** Adding bob
   creates bob's entry even though nothing is written to it, because
   `getFollowers` walks entries and a name with no entry has no followers. The
   roster ceiling (512) exceeds the friend ceiling (256) for the same reason:
   one player with a full list occupies that many slots plus their own.

3. **The lists are grown on demand**, not sized at the ceiling. Most entries in
   a live roster are followers with empty lists of their own, and this module is
   linked into the *client* under `EMBED_SERVER=1`; a flat `int64_t[256]` per
   entry would be 1.6 MB of zeroes.

4. **Chat modes live only in the service.** The reference keeps them on `Player`
   *and* mirrors them into the repository; two copies of a rule is the
   `CONTENT_ARCHITECTURE.md` §8.2(e) smell, and `isVisibleTo` — the thing that
   reads them — has to answer for names whose player slot is long gone.

Wired into the world in exactly three places, none of them a packet:

- `ToriRSServer_WorldSetDisplayName` derives `player->name37` (one place packs a
  name, so the key and the name cannot drift);
- `ToriRSServer_WorldLogin` registers presence, with the reference's default chat
  modes (`Player.ts:307-309`, all three ON) since nothing persists them;
- `ToriRSServer_WorldRemovePlayer` drops presence *before* the slot goes, keeping
  the lists — that is what lets a follower still see the name with "Offline"
  beside it.

Plus `phase_cleanup_player` clears `social_protect`, where the reference clears
it in `resetEntity`.

**Verified** by a new `ToriRSServer --selftest` section ("the friend service"), which
covers: invalid names refused (including 0, which is what an unnamed player and
an empty wire field both decode to); add / duplicate-add / delete / delete-absent
results; followers answered for a name that has never logged in; every branch of
`isVisibleTo` in order (staff, ignored-by-target beating friendship, OFF,
FRIENDS-iff-mutual, ON); the world byte going to 0 for "not visible" as well as
for "offline"; a logout keeping the list; deletion closing the gap rather than
swapping the tail in; the cap refusing at the boundary; pm ids never 0 and never
repeating; and the social gate opening once per tick.

**And the tests were proved able to fail** (`verify-blocker-and-failing-test`):
deleting the ignored-by-target branch from `isVisibleTo` and the cap check from
`list_add` produces four `FAIL` lines, two per mutation, and both mutations were
reverted.

What the selftest **cannot** cover, and nobody should read as covered: the
content-cap path. `ToriRSServer_ContentConstant` reads the loaded tree and there is
no injection seam, so the cap assertion runs against the fallback ceiling. The
day `social.constant` lands, re-run it and check the number in the failure
message changes.

---

## 6. Persistence — the decision, made explicitly

**Decision: in-memory only for the first landing. No save/load code is written,
and no assertion in this feature may depend on relogin.**

> **Landed as decided.** `torirs_server_friends.h` carries this decision as a boxed
> comment at the top of the file, in the same words, because a reader who does
> not know the reasoning will "fix" it by wiring the player save and be wrong.
> Under `TORIRSSERVER_VERBOSE` the server also says it once per process, at the first
> registration: *"friend and ignore lists are in-memory only; a restart loses
> them"*. There is no `ToriRSServer_FriendsSave`, no file format and no dead
> function — which is the §3.15 failure this section was written to avoid.

The facts this rests on, re-measured:

- `ToriRSServer_SavePlayer` / `ToriRSServer_LoadPlayer` have **no callers anywhere** —
  `grep -rn` over `src/` and `tools/` returns the prototypes at
  `torirs_server_save.h:50,63` and the definitions at `torirs_server_save.c:101,227`, and
  nothing else. Persistence has been dead code since it was written, so "a
  returning player" is not a case anything here can be tested against
  (PORTING_GUIDE §2.5).
- The reference does **not** persist friend lists into the player save. It uses
  its own `friendlist` / `ignorelist` tables keyed by account, precisely because
  followers must be enumerable while offline. Chat modes are the exception —
  those *do* ride the save byte (`Player.ts:270`).
- So even wiring `ToriRSServer_SavePlayer`'s callers would not give this feature
  persistence. It needs a second, name-keyed store either way.

Why not do that store now: it is a separable ~120-line change with its own
failure modes, and shipping it inside this feature would couple "does a PM
arrive" to "does an ini round-trip". The in-memory service already covers the
case that matters within one server run — *alice adds bob while bob is
offline* — because the service is keyed by name and outlives any player slot.
What a restart loses, it loses.

**No dead code.** There will be no `ToriRSServer_FriendsSave()` with no callers.
That is the exact failure `osrs230_mockserver.md` §3.15 records — intent written
down and later read as fact — and this section exists so the next reader knows
the absence is a decision.

The follow-up, when it lands, is the reference's shape: `saves/social.ini` keyed
by name37, owned and written by the friend module, reusing the write-then-rename
and name-sanitisation already in `torirs_server_save.c:45-78,113-118`. Wiring
`ToriRSServer_SavePlayer`'s two call sites is a **separate** work item with its own
commit and its own test — it turns on persistence for bank, stats and every
quest varp on the same day, and the first thing it will expose is whichever of
those has a load-order bug.

Chat modes are the one piece that could ride a varp: they are 3 × 2-bit bounded
enums and `[namespace:varp] ids = server` (`content.ini:56-59`) lets content
declare one, `transmit=no`, `scope=perm`. That is the right server-side home and
is compatible with everything above — but it is a *mirror*: the rev-230 client
reads its modes through `chat_getfilter_*` (5000/5005/5016), not from a varp, so
the wire (`CHAT_FILTER_SETTINGS`, §3.2) and the host ops (§4.2) are unavoidable
regardless. A varbit is not available: `[namespace:varbit] ids = cache`
(`content.ini:61-64`), so content cannot mint one.

---

## 7. File list

### Client — wire

| file | reason |
|---|---|
| `src/net/rev/osrs230/packetin.h` | **LANDED** — named ops 29/56/21, changed 15's length 0 → 1, added the `CHAT_FILTER_SETTINGS` row at op 3 |
| `src/net/rev/osrs230/packetout.h` | **LANDED** — six new rows at opcodes 3–8 (§3.3); `packetout_code_osrs230` no longer returns −1 for a social send |
| `src/net/rev/revpacket.h` | `struct PktMessageGame` gains `int type` — without it the login notification cannot exist. **Not done**, and deliberately: no packet in the wire stage is `MESSAGE_GAME`, the notification's text is content that has not landed, and `SS_OP_MES` has no typed variant to reach it with (§2.4). Doing it now would be an unread field |
| `src/net/rev/osrs230/osrs230_parse.c` | keep `MESSAGE_GAME`'s type byte instead of `memcpy(data + 1, …)`. **Not done**, same reason |

### Client — store, chat, host ops

| file | reason |
|---|---|
| `src/game/rs_social.{c,h}` | **LANDED** — `RS_Social_SeedDefaults` deleted; base-37 hash per entry; `RS_Social_DisplayName`; the count/name/world/is-friend accessors the host ops read. No change serial: the friend channel is a single dirty flag, like misc (§4.3) |
| `src/app.c` | **LANDED** — the seed call is gone, `RS_CS2Host_SetSocial` sits beside `SetStats`, and `App_Tick` drains the host's social send queue into `net_out_*` |
| `src/game/rs_gameproto_exec.c` | **PARTLY LANDED** — `RS_CS2Host_NotifyFriendChanged` on all four social packets. **Not** the `MESSAGE_GAME` type pass-through (still needs the `PktMessageGame.type` field, §2.4) and **not** the incoming-PM ignore filter (still the named gap in §9) |
| `src/cs2vm2/cs2vm2_host.h` | **LANDED** — four kinds, not eighteen: `SOCIAL` and `CHAT` each carry their opcode (the world-map family's shape), plus `MAP_WORLD` and `IF_SETONFRIENDTRANSMIT` |
| `src/cs2vm2/cs2vm2.c` | **LANDED** — `CS2VM2_Op_Social`, `CS2VM2_Op_Chat`, `CS2VM2_Op_IF_SetOnFriendTransmit`, the `MAP_WORLD` case, and both discard-group moves |
| `src/cs2vm2/gen_opcode_stack.py` + regenerated `cs2vm2_opcode_stack.gen.h` | **LANDED** — 23 `MANUAL_STACK` entries, not ten (§4.4); exactly 23 rows change in the generated header |
| `src/game/rs_cs2_host.{c,h}` | **LANDED** — `social` + `chat_filter_mode` (a *pointer* into `RS_UISlots`, not a copy) + `map_world`; `RS_CS2Host_SetSocial`; `friend_transmit_dirty` + `NotifyFriendChanged`; `exec_social` / `exec_chat`; the seton-slot case; and the outbound `RS_CS2SocialSend` queue |
| `src/ui/uitree.h` | **LANDED** |
| `src/game/task_cs2_run.{c,h}` | **LANDED** — not cloned: the misc walker was generalised to take the hook slot, and both channels call it. Also the `event_opbase` string substitution (§4.5) |
| `src/game/rs_cs2_dispatch.c` | **LANDED** |
| `src/main.c` | **LANDED** — the hook-name table (and its hard-coded loop bound 16, which would have hidden the new slot), plus two new in-loop harness hooks, `TORIRS_SIM_HOOK` and `TORIRS_SIM_TYPE` (§8.2b) |

### Server

| file | reason |
|---|---|
| `src/torirsserver/ToriRSServer_Friends.{c,h}` | **LANDED** — the service: name37-keyed roster, presence, `isVisibleTo`, cap check against the content `.constant`s, pm ids. No PM *relay*: relaying is writing a packet to another player, which is the wire's |
| `src/torirsserver/torirs_server.h` | **LANDED** — per-player `name37` and the `socialProtect` tick latch. **Not** the three chat modes: they live in the service, single copy (§5.3 decision 4) |
| `src/torirsserver/torirs_server_world.c` (service half) | **LANDED** — `name37` derived in `set_display_name`, register in `world_login`, unregister in `remove_player`, latch cleared in `phase_cleanup_player`, and the selftest section |
| `src/torirsserver/torirs_server_encode.c` | **LANDED** — five new encoders + their opcode enum/name rows |
| `src/torirsserver/torirs_server_world.c` | **LANDED** — six `k_packet_routes` rows + three handlers, the login dump, the follower broadcasts and the logout broadcast. **Does not touch** the trigger dispatch or the nine `run_trigger` call sites |
| `src/makefile` | **LANDED** — `net/wordpack.c` into `TORIRSSERVER_SRCS` (§3.4) and `torirs_server_friends.c` into `TORIRSSERVER_CORE_SRCS` |
| `src/torirsserver/test/embed_test.c` | **LANDED** — `absorb_social` + the two-player routing assertions (§8.3), then the six content-seam assertions of §8.2c |
| `src/torirsserver/torirs_server.h` (triggers) | **LANDED** — `FRIENDLOGIN` / `FRIENDLOGOUT` in the trigger table; no `struct ToriRSServerHooks` (`CONTENT_ARCHITECTURE.md` §8.6) |
| `src/torirsserver/torirs_server_scripts.c` | **LANDED** — `run_trigger_sv` for string-arg friend notifications. Hook table rows removed |
| `src/torirsserver/torirs_server_world.c` (`social_notify_followers`) | **LANDED** — the engine's three rules on this path (who hears it, `isVisibleTo`, not the subject) and the active-player switch that makes `mes` reach the right chatbox. Called from the end of `ToriRSServer_WorldSocialLogin` and the end of `ToriRSServer_WorldRemovePlayer` |
| `src/torirsserver/ToriRSServer_Embed.{c,h}` | **LANDED, content stage** — `ToriRSServer_EmbedDisconnect`, the socket server's close sequence in-process. There was no way to log a player out in-process before, so nothing that happens on a logout could be asserted |

Not touched, by lane rule: `src/torirsserver/ToriRSServer_Ops_*.c` (per-domain opcode
files, another lane's), and `torirs_server_scripts.c`'s trigger dispatch.

### Content

`docs/FRIENDS_PRIVATE_CHAT_CONTENT.md` — four files, **specified and verified
against a scratch copy of the tree, not applied.** Its §6 is the list of what the
opcode surface would have to grow for more of this to be content, which is the
part worth reading even if the diff is applied unchanged. Re-verified
independently in §8.5: its §5 recipe reproduces from a clean scratch tree, and
all four content-gated embed checks go SKIP → ok with the diff in place.

### Tests

| file | reason |
|---|---|
| `src/game/test/rs_social_test.c` | **LANDED in the verification pass** — the client half's only permanent check; see §8.5.3 for what it asserts and the five mutations that prove it can fail |
| `src/makefile` | **LANDED** — the `test-social` target and its `.PHONY` entry |
| `src/torirsserver/test/embed_test.c` | **LANDED** — 23 social checks from the wire stage plus six content-gated ones |
| `src/torirsserver/torirs_server_world.c` (selftest section) | **LANDED** — the "the friend service" stanza |

---

## 8. Verification

### 8.1 What can be proven, and what cannot — stated up front

**Exactly one harness in this tree can host two players: `src/torirsserver/test/embed_test.c`**
(`make -C src test-torirsserver-embed`). It calls `ToriRSServer_EmbedConnect`
(`embed_test.c:484`; `TORIRSSERVER_EMBED_CLIENT_MAX = 4`), logs in alice and bob into
one world, and asserts on each peer's stream decoded with the *client's own*
readers. It is the only caller of `ToriRSServer_EmbedConnect` in the tree.

Everything else is single-player, and that is structural, not incidental:

- the socket server accepts one connection at a time and `memset`s the world per
  session (`torirs_server_main.c:252-262`, `serve()` at `:115`/`:179`);
- `--selftest` adds exactly one player;
- `EMBED_SERVER=1` in the real client calls `ToriRSServer_EmbedStart()` only
  (`src/platform/net_transport_embed.c:91`) — client 0. Two `torirs` processes
  are two separate worlds.

So: **the renderer-level verification and the two-player verification cannot be
the same run.** This plan does both, separately, and claims neither covers the
other.

A second honest limit: the mock's packet capture (`torirs_server_encode.c:225`)
records `(opcode, len, data)` with **no addressee**, so "the PM reached bob and
not alice" is not expressible against it. §8.3 asserts on each peer's decoded
byte stream instead, which is what the existing PLAYER_INFO assertions already
do and is the stronger form.

### 8.1b What the wire stage actually proved — and what it could not

**Ran and green** (`PLATFORM_OBJ_BASE=build_lane2` throughout):

- `test-torirsserver-embed` — 23 new social assertions, listed in §8.3 as landed.
- `ToriRSServer --selftest`, `test-torirsserver-coverage`, `test-servercodec`,
  `ToriRSServer_Pack --check-only` (0 errors, 13 pre-existing warnings).
- The client links and runs: `make -C src EMBED_SERVER=1` clean, then
  `SDL_VIDEODRIVER=dummy TORIRSSERVER_VERBOSE=1 ./src/torirs --manifest
  manifest_osrs230_embed.ini` — the login burst emits `FRIENDLIST_LOADED`
  (op 15, 1 byte), `UPDATE_IGNORELIST` (op 21, 0 bytes) and
  `CHAT_FILTER_SETTINGS` (op 3, 3 bytes), the real client decodes all three,
  and **nothing asserts, desyncs or warns** — no `frames it as` line, no
  `no decoder for wire opcode` line other than the pre-existing 108. That is
  the whole of what a single client can prove at this stage.
- `MallocScribble=1 MallocPreScribble=1` on both the embed test and the
  selftest — clean. (Not ASAN; it hangs on this machine.)

**Proved able to fail**, three mutations, all reverted:

| mutation | result |
|---|---|
| `social_send_world_update` drops the `isVisibleTo` factor | 3 FAIL (private-chat-off, ignored-by-target, and the ignored-PM control) — **re-measured in §8.5 as 2 FAIL**: the two panel checks go red, the ignored-PM control stays green because the server delivers an ignored sender's PM by design |
| `handle_message_private` sends to the sender instead of the target | 5 FAIL |
| op 15's length back to 0 | 8 FAIL + the encoder's own `check_frame_length` line |

**Not proved, and nobody should read it as proved:**

- **Anything the player can see.** The friends panel still draws its empty
  state, and will until §4 lands — `friend_count` (3600) stubs to 0 regardless
  of what is in the store. §8.2 below is the *next* stage's proof, not this one's.
- **The content caps.** Still absent, still falling back to the storage
  ceilings, still printing the one-line warning at boot.
- **`test-db`** fails on a `RSCache_ProfileIsIdentified` assertion in
  `cache_provider.h`. Pre-existing and unrelated — reproduced by the previous
  stage against the main checkout's own binary.
- **The socket server's cross-player path.** Only the embed harness hosts two
  players (§8.1), so two-player social is proved there and nowhere else.

### 8.2 Single client, real renderer, headless — the end-to-end proof

The key realisation: **a self-addressed PM exercises every piece of the feature
in one client.** Alice types a PM to alice; the client's CS2 `chat_sendprivate`
emits `PKTOUT_NAME_MESSAGE_PRIVATE`; the server's friend service relays it back;
`MESSAGE_PRIVATE` (op 29) decodes into `RS_Chat` as `PRIVATE_FROM`; the rev-230
chatbox renders `From alice: hello` through
`RS_ChatWidgets_ComposeLine` (`rs_chat_widgets.c:57-62`) into interface 162's
line components. No cheat, no new ServerScript op, no second player.

```
cd <worktree>
SDL_VIDEODRIVER=dummy TORIRSSERVER_VERBOSE=1 \
TORIRS_SIM_TICKS=... \
TORIRS_SIM_CLICK_AT=<friends tab>  \
TORIRS_SIM_KEYS="alice\n" ...      \
TORIRS_EXIT_BMP=/tmp/pm.bmp TORIRS_DUMP_TREE_EXIT=1 \
  ./src/build_lane2/torirs --manifest manifest_osrs230_embed.ini
```

Assertions, in order of what they isolate:

1. **`TORIRS_DUMP_TREE_EXIT=1`** — `429:11` has ≥ 1 child, and `429:13` no longer
   holds the empty-state string. That alone proves the packet → store →
   host op → `cc_create` chain.
2. **`429:3` reads `"Friends List - World 1"`**, not `World 0` — proves
   `map_world` (3318) has a real handler.
3. The chatbox line component holds `From alice: …` — proves op 29 end to end.
4. **`TORIRS_EXIT_BMP`** for the visual record.
5. **Negative control**: with the friend service disabled, run 1 again and
   confirm the empty state returns. A test that only ever sees the good state
   proves nothing (`serverscript-guard-testing-confounds`).

Ladder if the panel draws nothing (`REV230_UI_BLANK_PANELS.md` §1, in order):
`TORIRS_DUMP_TREE_EXIT=1` → `TORIRS_DUMP_BOUNDS` → `TORIRS_DUMP_SETSIZE` → only
then suspect a packet. And a specific probe available here and nowhere else:
429 also registers `if_setonvartransmit(…){var1737}`, which **already works**.
Writing varp 1737 forces a repaint without the friend-transmit channel existing
— so if the rows appear on a var write and not on a friend update, the bug is in
§4.3 and nowhere else.

Watch for exit code **134**: an unimplemented-opcode abort reads exactly like a
hang (`rev230-xp-drops-stat-transmit`). `TORIRS_CS2_SURVEY=1` downgrades it to
one stderr line per opcode — survey only; the run after it is untrustworthy.

### 8.2b What the host-op stage actually proved — the run, verbatim

Two harness hooks had to be added to get here, and the reason is worth stating
because it will recur for every rev-230 side panel: **the pre-loop
`TORIRS_SIM_CLICK` / `TORIRS_SIM_KEYS` blocks run before login completes**, so
they cannot reach anything the *server* mounted, and at rev 230 that is every
side panel. `TORIRS_SIM_CLICK_AT` already existed as the in-loop twin of
`SIM_MOUSE_CLICK` for exactly this reason; the two new ones complete the set:

- `TORIRS_SIM_HOOK="frame,com[;frame,com...]"` — dispatch a component's onop
  hook at a main-loop frame. No coordinates and no visibility needed, so it can
  drive a button on a panel whose tab is not selected. It latches
  `event_op_index = 1` first, because every list row's onop switches on
  `event_opindex` and is a no-op without it.
- `TORIRS_SIM_TYPE="frame,c97,c108,k84[;frame,...]"` — key events at
  consecutive frames, same grammar as `TORIRS_SIM_KEYS` (`c`=character,
  `k`=OSRS key code), `;` starting a new burst at a new frame.

The run (the embed server names the player `guest`, so this is the design's
self-addressed case):

```
SDL_VIDEODRIVER=dummy TORIRSSERVER_VERBOSE=1 \
TORIRS_SIM_HOOK="120,0x01AD000E;220,0x01AD9C7A" \
TORIRS_SIM_TYPE="140,c103,c117,c101,c115,c116,k84;240,c104,c105,c32,c116,c104,c101,c114,c101,k84" \
TORIRS_MAX_FRAMES=600 TORIRS_EXIT_BMP=/tmp/friends.bmp TORIRS_DUMP_TREE_EXIT=1 \
  ./src/torirs --manifest manifest_osrs230_embed.ini
```

`0x01AD000E` is `friends:addfriend` (429:14); `0x01AD9C7A` is the friend row
`cc_create`d by the first half of the run. Nothing is hardcoded about the
second id beyond it being the first dynamic child of 429:11.

What that chain exercises, in order: the cache `onop` on 429:14 (script 103,
**no `IF_SETEVENTS` involved**, confirming §1.5) → the mode-2 name prompt →
script 112 → script 681 → `friend_add("guest")` → the local store + the queued
`FRIENDLIST_ADD` → `App_Tick` → the packet → the service → `UPDATE_FRIENDLIST`
back → the store → `friend_transmit_dirty` → script 631 → script 125 →
`friend_count` / `friend_getname` / `friend_getworld` / `map_world` → three
`cc_create`d rows. Then the row's own onop (script 126, op 1) →
`event_opbase` → the mode-6 prompt → script 681 case 6 → `chat_sendprivate` →
`MESSAGE_PRIVATE` → the service → op 29 back → the chatbox.

**Observed, all in one run:**

| assertion | evidence |
|---|---|
| `map_world` is real | `429:3` reads `text="Friends List - World 1"` |
| the rows exist | `429:11` has three dynamic children where it had none |
| the name is right | `text="Guest"` — capitalised by `RS_Social_DisplayName` |
| the *world* is right | `text="World 1"`, `color=0xdc10d` — script 125's green same-world branch, which only runs when `friend_getworld == map_world` |
| the empty state is gone | `429:13` reads `text=""` |
| the previous-name icon is correctly hidden | `429:40059 ownhide=1` (`friend_getname`'s second return is `""`) |
| the send path reaches the server | `torirsserver: <- social name=89 target=guest result=0` |
| the repaint really re-ran | the row component ids advance 40056→40058 between the local add and the server echo, i.e. `cc_deleteall` + rebuild happened twice |
| `chat_sendprivate` end to end | `torirsserver: <- MESSAGE_PRIVATE to=guest "Hi there"` |
| the PM reaches the chatbox both ways | `162:65` = `To Guest: hi there` (local echo on send), `162:66` = `From guest: Hi there` (op 29 decoded; `wordpack_unpack` sentence-cases, §3.5) |

> **Two corrections to this run, from §8.5.** (1) Its build command silently
> produces a client with **no embedded server** unless
> `src/build_lane2/net_transport_embed.o` is deleted first — §8.5.1. (2) It never
> selects the friends sidebar tab, so every `429:*` node above is `hidden=1`: the
> rows are built and are not on screen, and the `TORIRS_EXIT_BMP` it took shows
> the inventory tab. The chatbox lines *are* visible (`162:65`/`162:66` are
> `hidden=0`), so the private message was genuinely proved at the renderer and
> the friends list was not. §8.5.2 has the one extra click that fixes it and the
> screenshot in which the panel actually draws.

**Proved able to fail**, two mutations, both reverted:

| mutation | result |
|---|---|
| `IF_SETONFRIENDTRANSMIT` back into the discard group | the add still reaches the server, but `429:11` has **no** children and `429:13` is back to the empty-state help text — i.e. the repaint channel is the load-bearing piece, exactly as §4.3 claimed |
| `chat_sendprivate`'s two `PopStr` calls swapped | `<- MESSAGE_PRIVATE to=hi_there (offline; dropped) "Guest"` |

A third measurement worth recording because it looks like a bug and is not:
adding a friend who is **not** online (`alice`) draws `Offline` in red, and the
server's `UPDATE_FRIENDLIST` carries world 0. That is `isVisibleTo` answering
for a name it has never seen — the reference's "unknown player defaults OFF"
line — and it is correct.

**Not proved by this stage, and nobody should read it as proved:**

- **The ignore panel (432).** It has still never been mounted by anything, so
  `ignore_count` / `ignore_getname` have real handlers that no run has executed.
  The panel swap is content (§2.3a) and this lane cannot apply it. Expect §10.4
  to bite whoever mounts it first.
- **The caps.** Still absent, still falling back to the storage ceilings.
- **`friend_getrank`, `friend_setrank`, the sort ops.** Real shapes, stub
  answers; the list is still in insertion order (§10.2).
- **The clan half of the `MANUAL_STACK` fix.** The shapes are now right; no clan
  script was run.
- **`test-db`** still fails on the pre-existing `RSCache_ProfileIsIdentified`
  assertion, as it did for both earlier stages.

### 8.2c What the content stage proved — the ignore panel's first mount

The content diff is in `FRIENDS_PRIVATE_CHAT_CONTENT.md`; the commands and the
scratch-tree recipe that make it testable from a lane that cannot write
`OSRS-Content/` are its §5. What that run settled, and could not have been
settled any other way:

- **432 mounts and draws.** §10.4 warned that the panel had never been mounted
  by anything, ever, so script 127 had never run and the first mount would
  surface its own bugs. It does not: `432:3` reads `"Ignore List - World 1"`
  (so `map_world` reached script 127), `432:11` carries the full empty-state
  help text unhidden, `432:4` gains eight `cc_create`d frame graphics and
  `432:10` six scrollbar parts. `REV230_UI_BLANK_PANELS.md` §1's ladder was
  walked and came back clean.
- **`if_opensub` over an occupied slot is the right tool.** Swapping there and
  back leaves `429:3` `hidden=0` and `432:3` `hidden=1`; `if_closesub` would
  have hidden the slot, not the group.
- **Only two components in this feature need arming, and neither needs
  re-arming.** `if_setevents: com=28114945 (429:1)` and `com=28311553 (432:1)`,
  both `events=0x2`, both from `[proc,friends_login]` and never repeated — which
  is §1.5 confirmed a second way, and the correction to §2.3(a)'s draft.
- **`TORIRS_SIM_HOOK` cannot drive a server button.** These two carry no cache
  `onop`, so there is no CS2 hook to dispatch; the run needs a real
  `TORIRS_SIM_CLICK_AT`, which in turn needs the side9 tab stone
  (`toplevel_osrs_stretch:stone9`) clicked first so the panel is visible. Worth
  knowing before the next server button is tested.

Two-player, no renderer: `test-torirsserver-embed` gains six checks — the arming of
both swap buttons, `^friend_max` reaching the service, the logout notification,
the login notification, and that a self-listed player is not told about their own
login. Each is a SKIP against a content tree without the diff and `ok` with it,
and four mutations were used to prove they can go red (the content doc's §5).

`ToriRSServer_EmbedDisconnect` is new and is why the logout half is testable at all:
there was no in-process way to log a player *out*, so nothing that happens on a
logout had a test it could be asserted in. It is the socket server's own close
sequence minus the parts that assume the world goes away with the last
connection.

### 8.3 Two players, no renderer — the routing proof

**Landed, 23 assertions, all green.** What is in the tree differs from the plan
below in three places, each measured:

- **Step 1 is two assertions, not one.** The service state
  (`ToriRSServer_FriendsIsFriend`) and the packet alice's client decoded are
  checked separately, because a handler that mutated the roster and forgot to
  send would pass a check that only looked at one of them.
- **Step 3's PM text is asserted as `"Hi there"`, not `"hi there"`.**
  `wordpack_unpack` sentence-cases, exactly as the reference's `WordPack.unpack`
  does, so the text alice typed is not the text bob is shown. The message is
  also chosen to be an **even** number of single-nibble characters: an odd
  nibble count pads the last byte with a zero nibble, which decodes as a
  trailing space, and a message picked without noticing that reads as a
  round-trip bug. Both facts are in the test's comment so the next reader does
  not spend the afternoon on it.
- **Step 5, the cap, is not there.** It cannot be: `ToriRSServer_FriendsCapFriends`
  falls back to the 256-entry storage ceiling until `social.constant` lands, so
  the test would need 257 sends at one social packet per tick, and the "mutate
  the cap and re-run" half — the half that makes it a test — has nothing to
  mutate. The selftest already covers the cap at whatever value is resolved.
  **Add this assertion the day the content lands.**

Two more that the plan did not list and that turned out to be the sharper ones:
the login burst is asserted (`FRIENDLIST_LOADED` = 2, an ignore list that is
empty *but stated*, and the filter modes), and the chat-mode round trip is
asserted in both directions — private-off makes alice read offline in bob's
panel, private-on brings her back — which is the only check that exercises
`isVisibleTo`'s `FRIENDS`/`OFF` branches over the wire.

The plan, for the record:

In `embed_test.c`, after the existing alice/bob login:

1. alice sends `PKTOUT_NAME_FRIENDLIST_ADD(bob37)`; assert alice's stream
   receives `UPDATE_FRIENDLIST` with `name37 == bob37` and `world == 1`.
2. bob logs out; assert alice receives a second `UPDATE_FRIENDLIST` for bob with
   `world == 0` (the delta form, §5.1).
3. alice sends `PKTOUT_NAME_MESSAGE_PRIVATE(bob37, "hi")`; assert **bob's**
   stream carries op 29 with `from == alice37`, a non-zero `messageId`, and the
   wordpack round-tripping to `"hi"` — and assert **alice's** stream does not.
4. bob `IGNORELIST_ADD(alice37)`; alice PMs bob again; assert delivery per the
   reference (delivered — the reference does *not* filter incoming PMs
   server-side) and that alice's next `UPDATE_FRIENDLIST` for bob reads world 0
   (`isVisibleTo`: ignored-by-target ⇒ invisible).
5. The cap: add `friend_max + 1` names; assert the last is silently refused and
   the count stops at the content-declared cap. **Then mutate the cap in the
   content enum and re-run** — a test that cannot fail is not a test
   (`verify-blocker-and-failing-test`).

### 8.4 The standing gates

- `make -C src PLATFORM_OBJ_BASE=build_lane2` (restore `src/.last_flavor`
  afterwards), `test-torirsserver-embed`, `test-torirsserver-coverage`,
  `ToriRSServer_ServerCodecTest`, `test-db`.
- **`make -C src test-social`** — the client half (§8.5.3). New in the
  verification pass; it is the only gate that covers `rs_social.c`, the CS2
  host ops, the friend-transmit registration and the generated stack shapes.
- `ToriRSServer_Pack --check-only` at **0 errors**.
- The content diff in §2.3 applied by its owning lane and re-verified — the
  content tree may change underneath this lane mid-run, so a content-smelling
  failure gets re-run before it is believed.
- Memory: `MallocScribble` with the SIM harness. **Not ASAN** — it hangs on this
  machine.

---

### 8.5 The verification pass — what re-running everything changed

Every claim in §§8.1–8.4 was re-run from the worktree rather than read. Most
reproduced exactly, including the ones most worth doubting: the §8.2b headless
chain (`429:3` = `"Friends List - World 1"`, three dynamic children on `429:11`,
`162:65`/`162:66` carrying the two PM lines), the §8.2c panel swap against a
scratch content tree (`432:3` = `"Ignore List - World 1"`, eight `cc_create`d
frames on `432:4`, six scrollbar parts on `432:10`, `429:3 hidden=0` after
swapping back), and all four content-gated embed checks going **SKIP → ok** with
the §2.3 diff applied, with every boot warning gone. Three things did not.

#### 8.5.1 The reproduction command silently builds a client with no server

`make -C src PLATFORM_OBJ_BASE=build_lane2 EMBED_SERVER=1` **links a `torirs`
with no embedded server** whenever `build_lane2/net_transport_embed.o` was last
compiled without the define. Make tracks the source, not `CFLAGS`, and
`src/.last_flavor` tracks the objdir, not `EMBED_SERVER` — so nothing notices.

The previous stage documented the *opposite* direction (EMBED → plain **fails to
link**, which is loud and self-announcing). This direction is silent in the way
that matters: the build succeeds, the link line still pulls in every
`ToriRSServer_*.o`, and the only symptom is one line at boot —

```
net: this build has no embedded server — rebuild with `make -C src torirs EMBED_SERVER=1`
```

— after which the client boots to a world with no login, no panels and no
packets. Run §8.2b's command in that state and the exit-tree dump contains
interface 161 and nothing else, which reads exactly like "the feature does not
work".

**Always `rm -f src/build_lane2/net_transport_embed.o` before an
`EMBED_SERVER=1` build**, in either direction. This is not friends-specific; it
applies to every headless verification in this tree that uses the embed
manifest.

#### 8.5.2 §8.2b proved the tree, not the pixels

Its run never selects the friends sidebar tab, so every node it cites is
`hidden=1`: the rows were built correctly and were never on screen, and the
`TORIRS_EXIT_BMP` it took shows the inventory tab. The claim "a player can add a
friend … in the real client" was true of the component tree and unproven at the
renderer.

Adding one click fixes it — `TORIRS_SIM_CLICK_AT="100,536,484"` (the side9 tab
stone) in front of the existing hooks — and then the panel really does draw:
`429:3` `hidden=0`, the `"Guest"` / `"World 1"` row visible in green, `Add
Friend` / `Del Friend` beneath it, and `To Guest: hi there` / `From guest: Hi
there` in the chatbox. Use that command, not §8.2b's:

```sh
rm -f src/build_lane2/net_transport_embed.o
make -C src PLATFORM_OBJ_BASE=build_lane2 EMBED_SERVER=1
SDL_VIDEODRIVER=dummy TORIRSSERVER_VERBOSE=1 \
TORIRS_SIM_CLICK_AT="100,536,484" \
TORIRS_SIM_HOOK="120,0x01AD000E;220,0x01AD9C7A" \
TORIRS_SIM_TYPE="140,c103,c117,c101,c115,c116,k84;240,c104,c105,c32,c116,c104,c101,c114,c101,k84" \
TORIRS_MAX_FRAMES=600 TORIRS_EXIT_BMP=/tmp/friends.bmp TORIRS_DUMP_TREE_EXIT=1 \
  ./src/torirs --manifest manifest_osrs230_embed.ini
```

#### 8.5.3 The client half had no permanent check

The server half shipped with two gates (`ToriRSServer --selftest`'s "the friend
service" section and `test-torirsserver-embed`'s social block). The client half —
`rs_social.c`, the CS2 host ops, the friend-transmit registration, the
`MANUAL_STACK` shapes — shipped with **none**: it was proved once, by hand, in a
headless run, and nothing would have gone red if any of it regressed. That is
the §8.2b asymmetry stated plainly, and it is the wrong way round, because every
bug this feature actually hit was on the client side.

`src/game/test/rs_social_test.c` + `make -C src test-social` closes it. No cache
and no server, on purpose: a gate that can SKIP is a gate that eventually only
ever skips. It asserts the store's rules (base-37 keying across spellings, the
gap closing on delete, `friend_count` negative *only* while loading), the VM
dispatch shape through the **real** dispatch table (so a routing regression is
visible, which a direct handler call would not be), the real
`RS_CS2Host_Exec` answers over a real store including the mutators' *two*
halves, and the seven generated stack shapes this family depends on.

**Proved able to fail — five mutations, all reverted:**

| mutation | result |
|---|---|
| `IF_SETONFRIENDTRANSMIT` (2420) back in the IF discard group | `test-social` 1 FAIL; and headless, `429:11` has **no** children and `429:13` is back to the empty-state help text while the add still reaches the server — the §4.3 claim, reproduced |
| `chat_sendprivate`'s two `PopStr` calls swapped | 2 FAIL (`addressed to the first argument, got "hi there"`) |
| `friend_getname`'s second `PushStr` dropped | 3 FAIL |
| `friend_count` returns the count instead of −1 while loading | 2 FAIL |
| `gen_opcode_stack.py`'s `3601` set to the old name-guess shape, regenerated | 1 FAIL — so a regeneration that loses `MANUAL_STACK` is now caught |

And on the server side, for symmetry: dropping `isVisibleTo` from
`social_send_world_update` reddens `test-torirsserver-embed` — **2** checks, not the
3 the wire stage recorded.

#### 8.5.4 Two smaller corrections

- **`ToriRSServer --selftest` is intermittently red, and it is not this feature.**
  One failure in 67 runs of the same binary (the content stage saw one in six).
  The failing check was not in the friend-service section and could not be
  re-provoked in 66 consecutive passes. The likely cause stands: the selftest
  loads `script.dat` from a hardcoded path in `OSRS-Content/` that another lane
  rewrites, and it does **not** honour `TORIRSSERVER_CONTENT`. Re-run before
  believing a content-smelling failure.
- **`test-db`, `test-uitree` and `test-ui-slots` are red and all three
  predate this work** — confirmed by building each from the merge-base tree
  (`git archive HEAD | tar -x`, plus the gitignored `src/cache/rscache_io.h`,
  which `.gitignore`'s `cache/` rule excludes from the archive). `test-db`
  aborts in `CacheProvider_Profile` because `db_cache_test.c` calls
  `RSCache_Dat2DiskSetProfile` and never `CacheProvider_SetProfile`;
  `test-uitree` fails to open its emit-snapshot file for write; `test-ui-slots`
  wants a `../cache254` that does not exist. Identical failures in both trees.

---

## 9. Deliberately not doing

- **Clan chat.** Out of scope; it comes next. The plumbing is shaped for it on
  purpose, and here is exactly where it reuses this work: the friend-transmit
  channel of §4.3 is one `runtime_hooks` slot + one dirty flag + one dispatch
  task, and `IF_SETONCLANTRANSMIT` / `CC_SETONCLANTRANSMIT` are already sitting
  in the same discard groups (`cs2vm2.c:8402, 8431`) waiting for the identical
  treatment; the `3611-3627 clan_*` ops carry the *same* string/int
  transposition as 3605/3606, so the `MANUAL_STACK` fix in §4.2 should be
  written to cover both families in one pass even though only the friend half is
  wired; and the service in §5 is keyed by name, not by player slot, which is
  what a channel roster needs too. `74/76 push_varclan*` are the only genuinely
  new mechanism.
- **A second persistence path.** §6, decided and argued.
- **Chat-history host ops (5017/5019/5030/5031) and the CS2 scrollback.** The
  rev-230 chatbox is filled from C today and script 89's history loop is gated
  off by `chat_gethistorylength` returning 0. Their real shapes are recorded in
  §4.2 so the next lane does not re-derive them.
- **`friend_setrank` (3604) and the rank column.** Called only from script 1667
  (clan ranks). The reference's `setChatMode`/rank plumbing comes with clan chat.
- **The friends-chat channel panels** (`chatchannel_current` 7,
  `chatchannel_setup` 94, `side_channels` 707). Separate feature, same follow-on.
- **Report-abuse.** `PKTOUT_NAME_REPORT_ABUSE` exists and is socially adjacent,
  but the reference's one player-facing string on that path
  (`"Thank-you, your abuse report has been received"`,
  `ReportAbuseHandler.ts:26`) plus its moderation policy is its own slice.
- **Making the ignore list filter incoming PMs client-side.** Recorded as a
  parity gap (`rs_gameproto_exec.c:725-750` dedupes by id but does not check the
  ignore list, where Client-TS does when `staffModLevel <= 1`); it is in §7's
  file list as a one-line fix, but if it slips it is a known, named gap and not
  a silent one.
- **Fixing `SS_OP_UID` / `SS_OP_FINDUID`.** They return the constant 1 and
  resolve only `srv->active_player` (`torirs_server_scripts.c:1991-2020, 3294-3297`),
  which is wrong-by-construction now the pool holds 8 and is the single
  prerequisite for content ever addressing a second player. It is independently
  right, it is not this feature's, and this feature does not need it: nothing in
  §2.3 addresses a second player.

---

## 10. Open risks

1. **The assigned opcodes are this repo's, not RSProt's.** Whether ops 3–8
   outbound and 3 inbound match a real OldSchool 230 server is not verifiable
   here — RSProt is not vendored and no table records it. Under the doctrine in
   `packetin.h:5-25` that does not matter while `src/torirsserver` is the only
   producer, and the headers already say so. It would matter the day this client
   is pointed at a real server.
2. **The sort ops are no-ops.** 3628-3643 are balanced and do nothing, so the
   list renders in insertion order regardless of `%varcint183`. Correct-looking
   and not correct; recorded rather than fixed.
3. **`friend_getname`'s second return is the "previous name".** This server has
   no rename model, so it must return `""` — `script125` branches on
   `string_length($string1) > 0` and would otherwise show a "Reveal previous
   name" op with nothing behind it.
4. ~~**`ignore` (432) has never been mounted, by anything, ever.**~~
   **Discharged 2026-08-01, §8.2c.** It mounts, script 127 runs, and the panel
   draws its title, its help text, its frame and its scrollbar — no bug
   surfaced. Note that this was measured with the content diff applied from a
   scratch tree; on a tree without `~friends_login` the panel is still
   unreachable and the risk is only dormant, not gone.
5. **This tree's base-37 decoder rejects a range the reference accepts.**
   Found while porting the `invalid_name` guard, which is asked through
   `base37tostr`. `src/net/jbase37.c:5` sets `MAX_BASE37 = 0x1000000000000000`
   (1.15e18) and calls anything at or above it invalid;
   the reference's bound is 37^12 = 6 582 952 005 840 035 281 (6.58e18)
   (`JString.ts:38`). So a legitimate 12-character name whose packed value lands
   between the two decodes as `invalid_name` here and as itself there — the
   friend service would refuse to add it, silently, exactly as it refuses
   garbage. Not fixed by this lane: `jbase37.c` is on the client's own link line
   and a change to it wants its own round-trip test. Recorded so the next reader
   does not spend the afternoon on "why can't I add this one player".

7. **`gen_opcode_stack.py` guesses stack shapes from opcode *names*.** That is
   the root cause of every wrong shape in §4.4, and it is not confined to this
   family — the same `heuristic()` covers `OC_`, `STAT`, `IF_GET`, `CC_GET`,
   `INVOTHER_`, `STOCKMARKET_` and a catch-all `SET*`. The authority
   (`cs2_command.gen.h`'s proto pool, resolved through `cs2_types.c`) is in the
   tree and is what the decompiler reads. Making the generator read it is the
   right fix and would re-shape several hundred opcodes in one commit; it wants
   its own change, its own diff review and its own boot comparison. Until then,
   **every new opcode family needs the §4.4 comparison run by hand.**

8. **`event_opbase` was one of a class.** §4.5 fixed the string-valued event
   local because this feature needed it. Nothing has audited whether the cache
   spells any *other* event local as a string literal the same way; if one
   exists it is silently passing its own name today.

6. **A fresh worktree cannot build.** `src/cache/rscache_io.h` is covered by
   `.gitignore:5 (cache/)` and is absent, so eight `engine/dat1/*` TUs fail with
   `fatal error: 'cache/rscache_io.h' file not found`. Copy it from the main
   checkout. Also: `make -C src PLATFORM_OBJ_BASE=build_lane2 torirs` fails if
   `build_lane2/` does not exist (the `torirs` target has no `$(OBJ_DIR)`
   prerequisite) — use the default `all` target first.
