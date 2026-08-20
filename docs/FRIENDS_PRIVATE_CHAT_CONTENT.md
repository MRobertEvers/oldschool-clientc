# Friends, ignore and private chat — the content half

> **Status: specified and verified, NOT applied.** `OSRS-Content/` is a symlink
> into another lane's checkout and that lane owns it. Everything below is
> copy-pasteable and has been compiled and run — against a *copy* of the content
> tree in a scratch directory, with `TORIRSSERVER_CONTENT` pointed at it. Nothing was
> written to `OSRS-Content/` or `cache.osrs239/`.
>
> The engine half is already in the tree. Friend presence notifications fire
> `SS_TRIGGER_FRIENDLOGIN` / `SS_TRIGGER_FRIENDLOGOUT`; content binds
> `[friendlogin,_]` / `[friendlogout,_]`. Cap constants still warn at boot if
> missing:
>
> ```
> torirsserver: 3 friend/ignore cap(s) are not in any .constant — the lists are limited
> only by this server's array sizes. Content owns these: see
> docs/FRIENDS_PRIVATE_CHAT.md §2.3.
> ```
>
> With the diff applied, **all five of those lines disappear** — that is the
> cheapest check that it landed correctly, and §5 below is the full one.

The engine side is `docs/FRIENDS_PRIVATE_CHAT.md`. This file is only the part a
content author owns, and it is short on purpose: §1 says why.

---

## 1. Where the reference puts this, and why content's share is so small

**LostCity puts the entire friends / ignore / private-message feature in the
ENGINE** — `engine/src/server/friend/{FriendServer,FriendServerRepository,
FriendThread}.ts`, six client handlers and five server encoders,
`World.ts:1523-1631` and `1946-1998` for the call sites. Its
`content/scripts/engine.rs2` — the reference's *whole* 511-command engine
surface — declares **zero** friend/ignore/privatechat/pm commands, and no `.rs2`
anywhere touches the path. The reference emits **no player-facing string at
all** here: `FriendServerRepository.addFriend` returns bare on every failure, and
the reference even commented out its own `console.error`s.

So an engine service, an engine wire and engine host ops *are* the faithful port
(`CONTENT_ARCHITECTURE.md` §8.1), and content's share is only what this tree's
stricter rules add on top:

| content owns | why |
|---|---|
| the two list caps and the PM length cap | config-shaped constants (§8.2(e)) |
| the friends↔ignore panel swap, and its arming | a policy about which panel a click reaches, expressed as `if_opensub` |
| "X has logged in." / "has logged out." | a player-facing string (§8.2(a)) — and the *only* one, for the reason in §3 |

Everything else a player reads on interfaces 429, 432 and 163 is already in the
cache's own clientscripts (`"That player is currently offline."`, `"Unable to
complete action - system busy."`, `"You must set a name before you can chat."`),
which is why there is no `[proc,friend_add_failed_message]` here and must not be
one: inventing a refusal message would be inventing a behaviour the reference
does not have.

---

## 2. The four files

### 2.1 New — `server/scripts/interface_friends/scripts/friends.rs2`

```
// The friends and ignore side panels (429 `friends`, 432 `ignore`).
//
// Almost nothing here is the server's, and that is the point of the file. Both
// panels build every row themselves: clientscripts 123/125 and 127/129 cc_create
// three dynamics per friend and two per ignore, off the client's *own* store,
// which the server feeds with UPDATE_FRIENDLIST and UPDATE_IGNORELIST. So there
// is no row the server can address and no if_settext it could usefully send --
// see docs/FRIENDS_PRIVATE_CHAT.md §1.2.
//
// Two things are the server's, and both are here:
//
//   1. `ignore` is mounted by nobody. player/configs/gameframe.enum gives side9
//      to `friends` and never names 432, and no clientscript in the cache
//      references it either. The two panels swap through a pair of *server*
//      buttons: `friends:ignore` carries op1 "View Ignore List" with no onop=,
//      and `ignore:friends` carries op1 "View Friends List" the same way, which
//      at rev 230 is how a component says "ask the server".
//
//   2. Those two ops are inert until if_setevents says otherwise. Every other
//      button on both panels -- add friend, delete friend, add ignore, delete
//      ignore, and every op on every row -- carries its own cache onop= and is
//      pure clientscript. Nothing else on this feature needs arming, which is
//      why this file is four statements long.

[proc,friends_login]
// Both, once. The client keys its events table by component id and never clears
// it, so arming survives a mount -- `ignore` may be armed here even though it is
// mounted by nobody yet, and neither op has to be re-armed after a swap.
if_setevents(friends:ignore, 0, 0, ^if_event_op1);
if_setevents(ignore:friends, 0, 0, ^if_event_op1);

// Swap what side9 holds. if_opensub over a slot that already has an interface in
// it is the shape the journal tab strip uses (interface_journal/scripts/
// journal.rs2); an if_closesub here would hide the *slot* rather than the group
// and leave the sidebar tab dark -- see the if-closesub-slot-poison note.
[if_button,friends:ignore]
if_opensub(toplevel_osrs_stretch:side9, ignore, 1);

[if_button,ignore:friends]
if_opensub(toplevel_osrs_stretch:side9, friends, 1);
```

**Measured, not assumed:** the arming really does survive a mount.
`App_IfEventsSet` (`src/app.c:203-243`) keys a flat table by component id and
nothing ever clears an entry, so arming `ignore:friends` at login — while 432 is
mounted by nobody — is enough, and the swap handlers do **not** need to re-arm.
The earlier draft of this diff re-armed on every swap; that was belt-and-braces
against a client that does not behave that way. Removing one of the two
`if_setevents` lines and recompiling makes the embed test go red (§5), which is
what makes this an assertion rather than a claim.

### 2.2 New — `server/scripts/interface_friends/configs/social.constant`

```
// How much of each list a player may have, and how long a private message may
// be.
//
// All three are engine literals in the reference -- FriendServerRepository.ts:231
// and :268, MessagePrivateHandler.ts:13 -- and all three are config-shaped
// constants, which is the thing CONTENT_ARCHITECTURE.md §8.2(e) says may not
// live in C. The engine reads them by name, the way ^lootdrop_duration is read.
//
// A missing constant here is not an error: the engine says so once at boot and
// falls back to the size of its own arrays. That is deliberate -- "as many as
// fit" is an honest answer, a number invented in C is not.
^friend_max = 100
^ignore_max = 100

// The reference's own 200-friend members tier is deliberately absent. There is
// no account model in this server and nothing carries a members bit, so a second
// cap would be a number no code path could select. Add it with the account
// model, not before.

// Bytes of *packed* text, not characters. The wire carries a private message
// wordpacked, the reference tests the packed slice (`input.length > 100`) and
// so does this server -- roughly two typed characters per byte. The name says
// bytes because the number counts bytes.
^private_message_max_bytes = 100
```

Two things about this file are corrections to the design note, both re-measured:

- **`.constant`, not `.enum`.** `struct ToriRSServerEnumValue` has an `int key` and no
  key text (`torirs_server_content.h:412-420`) and a `val=` key runs through
  `enum_operand`, which for a non-pack input kind is `atoi(text)`
  (`torirs_server_content.c:1438`). So the design's "`inputtype=string`" alternative
  does not fail — it silently maps every key to 0.
- **`^private_message_max_bytes`, not `..._max_chars`.** The cap counts *packed*
  wire bytes, which is what `MessagePrivateHandler.ts:13` tests
  (`input.length > 100`, on the raw slice, before `WordPack.unpack`) and what
  `handle_message_private` tests. A wordpack byte carries roughly two typed
  characters, so a constant named for characters would have lied by a factor of
  two. **The engine was renamed to match in this stage** — `cap_pm_chars` →
  `cap_pm_bytes` — so do not use the older name from
  `FRIENDS_PRIVATE_CHAT.md` §2.3(b); it will read as absent and fall back to 255.

### 2.3 Append to `server/scripts/player/messages.rs2`

```
// ------------------------------------------------------------------
// Friend presence
// ------------------------------------------------------------------

// The one pair of sentences on the friends/ignore/private-chat path that the
// server has to word, and the reason is a change of era rather than a change of
// opinion. LostCity's 2004 client watched the world byte in UPDATE_FRIENDLIST go
// from 0 to non-zero and wrote its own line, so no LostCity server ever said
// this and there is nothing in its content tree to port. The rev-230 client
// dropped that derivation: the notification arrives as an ordinary server game
// message, which makes it a sentence the server says, which makes it content's.
//
// $name is the display name, handed over by the engine for the same reason
// ~equip_message is handed an obj name: only the engine knows it. It cannot be
// asked for here -- `displayname` answers for the *active* player, and the
// active player during these is the person being told, not the person who
// logged in.
//
// Whether anything is said at all is stated by this file too. The engine calls
// these by name and a name it cannot resolve is a no-op it reports once at boot,
// so a tree that deletes these two procs is a tree whose players are not
// notified. Nothing in C decides that.
//
// Who hears it is not content's and is not here: the engine sends to exactly the
// followers the world update goes to, and only where isVisibleTo says the world
// byte would have been non-zero, so a notification can never announce a presence
// the panel itself would have hidden.
[proc,friend_login_notification](string $name)
mes("<$name> has logged in.");

[proc,friend_logout_notification](string $name)
mes("<$name> has logged out.");
```

`messages.rs2` and not `interface_friends/` because that file is where every
`[proc,*_message]` in the tree lives and one place for the wording is the whole
point of it. The alternative — keeping the feature in one directory so an
operator can delete it wholesale — is defensible; it was not chosen because the
caps would stay behind anyway.

### 2.4 One line in `server/scripts/player/login.rs2`

Beside the other `~*_login` arming calls at the end of `[login,_]`:

```
~emote_login;
~friends_login;          <-- add this line
```

---

## 3. What content cannot express here, reported rather than worked around

Per `PORTING_GUIDE.md` §2.4 item 4 these are reported, not routed back into C.
The full list with measurements is §6; the one that shows on a player's screen
is this:

**The login notification wants chattype 5 and there is no opcode for it.**
`LOGINLOGOUTNOTIFICATION` is what a real rev-230 server sends; this client
already models it (`RS_CHAT_TYPE_PRIVATE_SYSTEM`, `src/game/rs_chat.h:35`),
already renders it and already filters it. But `SS_OP_MES`
(`torirs_server_scripts.c:1346`) writes MESSAGE_GAME with no type, `struct
PktMessageGame` (`src/net/rev/revpacket.h:180`) has no type field, and
`osrs230_parse.c:381-393` drops the byte. So the first landing uses plain `mes`:
**correct text, black instead of cyan, no auto-expire, and the player's "Private
chat: off" filter does not suppress it.** A `mes_type`-style opcode plus the
field plus the parse fix is a small follow-up for whichever lane owns
`ToriRSServer_Ops_*.c`; this lane is forbidden from adding those files.

---

## 4. What the engine does with each piece — the seam, so it can be checked

| content | engine reads it at | what happens if it is absent |
|---|---|---|
| `~friends_login` | `[login,_]`, phase 7 | the two swap buttons are never armed; the ignore panel is unreachable and clicking the button sends nothing |
| `[if_button,friends:ignore]` / `[if_button,ignore:friends]` | `ToriRSServer_ScriptsRunIfButtonNamed`, from `handle_if_button_op` — no name in C | the click reaches the server and nothing answers it |
| `^friend_max`, `^ignore_max` | `cap_resolve` in `torirs_server_friends.c`, once per process | one stderr line, then the storage ceilings (256 / 128) |
| `^private_message_max_bytes` | same | one stderr line, then 255 — the var-u8 packet's own limit |
| `[friendlogin,_]` / `[friendlogout,_]` | `SS_TRIGGER_FRIENDLOGIN` / `SS_TRIGGER_FRIENDLOGOUT` from `social_notify_followers` (login) and `ToriRSServer_WorldRemovePlayer` (logout), with the display name as a string arg | trigger miss → silence on that event (no boot hook table) |

The engine holds **no** player-facing string on this path. `grep` the social
section of `torirs_server_world.c` and `torirs_server_friends.c` for `"` and every hit is a
comment or a `stderr` diagnostic.

Three rules stay in the engine on purpose and content must not try to restate
them, because content has no way to reach a second player (§6.2):

1. **Who hears a notification** — the followers of the name, i.e. everyone with
   it in their friend list. The same set the world update goes to.
2. **Whether they may hear it** — `isVisibleTo(follower, subject)`, so a
   notification can never announce a presence the friends panel itself would
   have drawn as "Offline".
3. **That you are not told about your own login.** Nothing refuses a player who
   lists themselves, so without this branch a self-listed player greets
   themselves.

---

## 5. How to verify it, from a tree that does not have it yet

The content tree cannot be written by every lane, and this diff was developed
against a scratch copy. The recipe is reusable and is the reason every claim
above is a measurement:

```sh
# 1. A content tree you may write to: symlink the heavy directories, copy the
#    three the compiler and the allocator touch.
SRC=OSRS-Content/osrs239-content
DST=/tmp/content-scratch
rm -rf $DST && mkdir -p $DST
for e in $(ls $SRC); do
  case "$e" in
    server|pack|configs|content.ini|meta.ini) cp -R "$SRC/$e" "$DST/$e" ;;
    *) ln -s "$PWD/$SRC/$e" "$DST/$e" ;;
  esac
done
# (copy pack/ and configs/ rather than symlinking them: ss_allocate.py appends
#  to pack files, and a symlink would write into the real tree.)

# 2. Apply the four files of §2 to $DST, then compile — never
#    `make -C src torirsserver-scripts`, which writes the shared tree.
make -C src PLATFORM_OBJ_BASE=build_lane2 sscompile
python3 tools/ss_allocate.py --tree $DST
./src/build_lane2/sscompile --src $DST/server/scripts \
    --out $DST/server/scripts/build --pack $DST/pack --pack $DST/configs

# 3. Run everything against it.
TORIRSSERVER_CONTENT=$DST ./src/build_lane2/ToriRSServer_EmbedTest
./src/build_lane2/ToriRSServer_Pack --check-only --content $DST
```

**What that proves, measured 2026-08-01:**

| check | with the diff | without it |
|---|---|---|
| `friends:ignore is armed, so the ignore panel is reachable` | ok | SKIP |
| `and ignore:friends, so it is escapable` | ok | SKIP |
| `^friend_max is what the service caps at` | ok | SKIP |
| `alice is told bob logged out, in content's words` | ok | SKIP |
| `and alice is told so, in content's words` (login) | ok | SKIP |
| `and bob was not told about his own login` | ok | ok |
| every boot warning in this file's header | gone | printed |
| `ToriRSServer_Pack --check-only` | 0 errors, 13 warnings | identical |

The component ids in the arming check are resolved through the pack
(`ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "friends:ignore")`), never
written into the test.

**And the checks were proved able to fail** — four mutations, all reverted:

| mutation | result |
|---|---|
| delete `if_setevents(ignore:friends, …)` from `friends.rs2`, recompile | `and ignore:friends, so it is escapable  FAILED` |
| `cap_resolve` reads NULL instead of the constant | `^friend_max is what the service caps at  FAILED` |
| drop the "not the subject" branch from `social_notify_followers` | `and bob was not told about his own login  FAILED` |
| `social_notify_followers` runs the proc with the *caller's* active player | all three notification checks FAILED |

### 5.1 In the real client, headless — the ignore panel's first mount ever

432 had never been mounted by anything, in any run, so its onload (script 127)
had never executed. It does now:

```sh
make -C src PLATFORM_OBJ_BASE=build_lane2 EMBED_SERVER=1
SDL_VIDEODRIVER=dummy TORIRSSERVER_CONTENT=$DST TORIRSSERVER_VERBOSE=1 TORIRS_NET_DEBUG=1 \
TORIRS_SIM_CLICK_AT="140,536,484;220,686,215;300,686,215" \
TORIRS_MAX_FRAMES=420 TORIRS_EXIT_BMP=/tmp/swap.bmp TORIRS_DUMP_TREE_EXIT=1 \
  ./src/torirs --manifest manifest_osrs230_embed.ini
# afterwards: rm src/torirs && git checkout -- src/.last_flavor
```

`536,484` is the side9 tab stone (`toplevel_osrs_stretch:stone9`); `686,215` is
the swap button, which both panels place at the same 21×21 rect — the same click
swaps either way. Coordinates come from `TORIRS_DUMP_BOUNDS`, not from guessing.
`TORIRS_SIM_HOOK` cannot be used here: these two are *server* buttons with no
cache `onop`, so there is no CS2 hook to dispatch and only a real click reaches
the arming gate.

Observed in one run:

```
if_setevents: com=28114945 (429:1) slots=0..0 events=0x2
if_setevents: com=28311553 (432:1) slots=0..0 events=0x2
torirsserver: <- IF_BUTTON1 429:1 sub=-1
torirsserver: if_button named lookup `[if_button,friends:ignore]` -> found
if-opensub: mount iface=432 under uid=0x00a10055 (161<<16|85)
torirsserver: <- IF_BUTTON1 432:1 sub=-1
torirsserver: if_button named lookup `[if_button,ignore:friends]` -> found
if-opensub: mount iface=429 under uid=0x00a10055 (161<<16|85)
```

and in the exit tree dump, 432 **draws** rather than mounting blank — which is
the `REV230_UI_BLANK_PANELS.md` §1 question and it comes back clean:

| node | evidence |
|---|---|
| `432:3` | `text="Ignore List - World 1"` — script 127 ran and `map_world` answered |
| `432:11` | the full empty-state help text, `hidden=0` |
| `432:4` | eight `cc_create`d frame graphics (40049–40056) |
| `432:10` | six `cc_create`d scrollbar parts |
| after the second swap | `429:3` `hidden=0`, `432:3` `hidden=1` — the slot really swapped back |

`if_opensub` over a slot that already holds an interface is therefore fine, and
`if_closesub` is still the wrong tool (it would hide the slot, not the group).

---

## 6. What the opcode surface would have to grow for more of this to be content

This list is worth more than the workarounds above, so it is stated in the order
of how much it would move.

### 6.1 A typed `mes`

Stated in §3. Smallest change on the list, and the only one a player can see
today: text is right, colour and filtering are not. Needs one ServerScript
opcode, one field on `struct PktMessageGame`, and the byte `osrs230_parse.c`
currently drops.

### 6.2 Any way at all for content to address a second player — **the big one**

`SS_OP_UID` returns the constant 1 and `SS_OP_FINDUID` resolves only
`srv->active_player` (`torirs_server_scripts.c:1991-2020, 3294-3297`). So a script
cannot say *"for each follower of $name, tell them"*. That is the single reason
the three rules in §4 — who hears it, whether they may, and not the subject —
are C and not a `[proc]`, even though all three are policy by any reading of
§8.1. With a working `finduid`/`p_finduid` the entire notification, visibility
gate included, would be content and the engine would keep only "a login
happened".

It is not this feature's to fix (`FRIENDS_PRIVATE_CHAT.md` §9 already names it),
but this is the concrete cost of not having it, and the cost is a policy rule
living in C.

### 6.3 A trigger for "a friend's presence changed"

**Landed.** `SS_TRIGGER_FRIENDLOGIN` / `SS_TRIGGER_FRIENDLOGOUT` fire from
`social_notify_followers` with the display name as a string arg; content binds
`[friendlogin,_]` / `[friendlogout,_]` and `mes`es. No named-proc hook table.

### 6.4 Chat-mode opcodes, or a varp behind the chat modes

`ToriRSServer_WorldLogin` states the opening privacy settings in C (`public 0,
TORIRSSERVER_CHAT_PRIVATE_ON, trade 0` — the reference's own defaults from
`Player.ts:307-309`). Content cannot state them: the modes deliberately live
only in the friend service (`FRIENDS_PRIVATE_CHAT.md` §5.3(4), because
`isVisibleTo` must answer for names whose player slot is gone), and there is no
`chat_setmode`/`chat_getmode` ServerScript op. A pair of ops would let the
default move into `[login,_]` beside `%com_mode`, which is exactly where the
combat tab's opening state already lives.

### 6.5 `.enum`s with string keys

`struct ToriRSServerEnumValue` has an `int key` and no key text, and a `val=` key
`atoi()`s for a non-pack input kind — so `inputtype=string` compiles, loads, and
silently maps every key to 0. Any name→value table content wants therefore has
nowhere to go but `.constant`, which is fine for three caps and not fine in
general. This is a hole in the config surface, not a preference.

### 6.6 "Optional, but valid if present" for a content constant

`ToriRSServer_ContentConstant` reads raw text; `ToriRSServer_ContentConstantInt`
reports a miss through the content error count, which `ToriRSServer_Pack
--check-only` fails on. There is no way to say *this constant is optional, but if
it is there it must be a positive integer*, so `^friend_max = onehundred`
degrades to the storage ceiling with one stderr line and a clean `--check-only`.
The caps use the raw-text form for exactly this reason: requiring them before
the file exists would have broken every other lane's content gate.

### 6.7 Not a gap, recorded so nobody chases it

There is **no** `uppercase` / `capitalize` / `toDisplayName` string op, and the
reference has none either — `engine.rs2` declares `lowercase` and nothing else.
Display names carry their own capitalisation at rev 230, so `<$name>` is right;
the lowercase names in the tests are an artifact of the mock server's login
strings, not of the notification.

Likewise the client's `"From %s:"` / `"To %s:"` chat prefixes
(`src/game/rs_chat.c:195,208`) are **not** content debt: the reference client
spells them in Java too, and this tree's content pipeline has no client-string
surface to move them to.

---

## 7. Checklist for the lane that applies this

1. Create the two new files of §2.1 and §2.2, append §2.3, add the one line of
   §2.4.
2. `make -C src torirsserver-scripts` (that lane owns it; this one may not run it).
3. `make -C src test-torirsserver-embed` — six checks that were SKIPs become `ok`.
4. `ToriRSServer_Pack --check-only` — still 0 errors.
5. Boot anything and confirm the five warning lines in this file's header are
   gone. If `^friend_max` is still reported missing, check the spelling against
   §2.2 — a typo degrades silently (§6.6).
6. Then delete the `--- corrections ---` note in `FRIENDS_PRIVATE_CHAT.md`
   §2.3(b) and move the three caps into `ToriRSServer_Ids`'s resolve table, which is
   the right home for them **on the day the constants land** and not before.
