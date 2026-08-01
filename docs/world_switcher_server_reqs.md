# World Switcher (interface 69): what this actually needs from a game-world server

> Companion to `docs/chrome_panels_server_reqs.md`'s hiscores section, same
> discovery pass. **Same shape of finding**: the CS2 client half is fully
> present and traceable, but the actual mechanism is login-server-tier, not
> a game-world packet — mock230 is a single world and structurally cannot
> (and shouldn't) implement world-switching as a game packet.

## 0. Status at a glance

**The target world is structurally discarded by the client itself** —
confirmed by reading the full 4-line body of `logout_op`. World switching
is disconnect-and-reconnect-elsewhere, handled entirely outside the CS2 VM
and outside any single world's protocol. The only real mock230-relevant
work is a client-VM stub for 5 host ops so the panel doesn't hang forever
on "Loading...", plus trivially answering "what world am I on" with
mock230's own fixed number.

## 1. The mechanism

`worldswitcher_init` (`script_747.cs2`) waits on **`worldlist_fetch`**
(opcode 6500, confirmed, no args → bool) — a client-native "has my locally
cached world list arrived" gate — then builds the grid from a confirmed,
distinct op family:

```
[6500] worldlist_fetch     () -> bool
[6501] worldlist_start     () -> (world, flags, country?, activityId, players, activityName)
[6502] worldlist_next      () -> same 6-tuple, iterator
[6506] worldlist_specific  (world) -> same 6-tuple for one world
[6507] worldlist_sort      (key, desc, key2, desc2)
```

These are read-only iteration over **a cache the client already owns** —
never written to, and confirmed distinct from the unrelated `worldmap_*`
family (map browsing) and from `map_world`/opcode 3318 (get *my own*
current world, singular, used only to bold the current row).

**Sort and filter are entirely local** over the already-fetched cache — no
per-keystroke round trip. **Favourites** are two slots
(`%varbit4597`/`%varbit4598`), each resolved via `worldlist_specific`; the
write side ("Favourite"/"Clear" actually setting these) is **missing from
this decompile** — `logout_op` only branches on Switch (index 1), Favourite
(index 2) falls through to nothing but an optional sound. Flag as a corpus
gap, don't guess the body.

### 1.1 "Switch worlds" — a bare disconnect, confirmed three ways

```
[clientscript,logout_op](int $index0, int $flags1, int $int2)
if ($index0 = 1) {
	logout;
}
```
(`script_970.cs2`, verbatim, read in full — 4 lines total.) **The target
world (`$int2`) is received but never referenced anywhere in this proc.**
`logout` is opcode 5630, confirmed zero operands and zero returns — the
identical bare disconnect any other logout path uses. Three independent
confirmations this is login-server-tier, not game-world-tier:

1. The target world is dead on arrival in the only handler that exists.
2. `logout` is generic, not switcher-specific — no `WORLDLIST_JOIN`-shaped
   opcode exists anywhere in the op table.
3. The worldlist tuple itself never carries a connectable address (host/IP/
   port) — just world number, flags, name, activity, players, string.

This is exactly the hiscores pattern (`docs/chrome_panels_server_reqs.md`
§2): a feature whose CS2 half lives in the client cache but whose real
mechanism is out-of-band relative to the game-world wire — hiscores via
HTTP, world-switching via a login-server worldlist + a fresh socket.
Confirmed mock230 is genuinely single-world: `mock230_embed.c:9`, "One
world, N connections" — every "world" reference in `src/net/mock/` means
the one `Mock230Server` struct, never a list of worlds.

## 2. Server obligations

| requirement | needed in mock230? | why |
|---|---|---|
| A "switch to world N" game packet | **No** | the target-world argument is read by nothing |
| Live player counts across other worlds | **No, out of scope** | mock230 has no concept of other worlds at all |
| CS2 host ops for `worldlist_fetch/start/next/specific/sort` | **Yes, client-VM-side only** | declared `CS2_HANDLER_HOST` but confirmed zero dispatch cases in `cs2vm2.c` and zero `CS2VM_HOST_REQUEST_WORLDLIST*` in `rs_cs2_host.c` — a client VM gap, same class as hiscores' ops |
| Favourite-world persistence (2 slots) | **Client-local concern, possibly nothing for mock230** | write side is a corpus gap; historically a client-side preference, not account state |
| "Am I on world X" highlight (opcode 3318) | **Trivial** — answer with mock230's fixed world number, a constant | the one piece of this whole panel legitimately answerable by mock230 |
| Sort/filter | **No packet** — pure local logic once the host ops exist | operates entirely on the already-fetched cache |
| Friends-list "world-hop" (adjacent) | **Same answer, already flagged** in `docs/friends_pm_chat_server_reqs.md` |

## 3. Landed vs. gap

- **Landed: nothing** — zero references anywhere prior to this doc.
- **Gap 1 (client VM)**: the 5 worldlist ops have declared metadata but no
  implementation — without them the panel gets stuck on "Loading..."
  forever, since `worldlist_fetch` never flips true.
- **Gap 2 (data source, if implemented)**: even with the host ops built,
  mock230 has no other worlds to report. Options mirror hiscores' triage:
  (1) report a single fabricated/self world so the panel isn't empty, (2)
  stub `worldlist_fetch` permanently false so the panel stays inert, or (3)
  treat the whole feature as out of scope for a single-instance mock.
- **Gap 3 (corpus)**: the favourite-world write handler is missing;
  re-decompile before implementing that specific piece.
- `logout` itself is generic and already necessary for every logout path —
  nothing switcher-specific needed there.

## 4. LostCity precedent

No worldlist/world-switcher reference anywhere in the engine or content
tree — rev 254's client has no in-game world-picker UI; world selection
happened on the public website before ever opening a socket. LostCity does
carry a real login/world-tier split structurally
(`engine/src/server/login/LoginServer.ts`, its own process, tracking which
world node a connecting player routes to) — but that's for *routing a
connection to the right running world process*, not for rendering a
world-picker list. The closer precedent, if mock230 ever becomes genuinely
multi-world, is the `FriendServer` shape already scoped in
`docs/friends_pm_chat_server_reqs.md` §6 (a service tracking which world
each player is in, pushed to followers) — world-switching itself would
still terminate at a login-server tier outside any individual world's
engine, exactly as in real OSRS and in LostCity's own process split.

## 5. What this doc does not cover

- The favourite-world write handler's real body — missing from this
  decompile, not guessed.
- `worldswitcher_filter`'s full filter-picker UI — confirmed to be a
  settings editor over the same varbit family, not traced further.
- Whether mock230 should ever answer the worldlist ops with fabricated
  data — a product decision, not a research finding.
