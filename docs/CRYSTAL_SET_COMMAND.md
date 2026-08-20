# `::~crystal_set`: pristine-cache escape, Cry collision, and permanent guards

This is the canonical incident record for the command that made the player
perform the Cry emote while the server logged nothing. Read this before changing
typed emotes, revision-239 chat submission, `CLIENT_CHEAT`, debugproc lookup, or
the crystal equipment debug command.

The short answer is that **two independent bugs existed on opposite sides of a
packet boundary**:

1. The revision-239 client ran a local typed-emote parser before its server-cheat
   path. That parser used prefix matching, so `crystal_set` matched `cry`, played
   animation 860, returned “handled,” and prevented `CLIENT_CHEAT` from being
   sent. The server could not report an error because it received no packet.
2. The content tree also had two global `[debugproc,crystal_set]` declarations.
   The ServerScript compiler did not reject the collision; both bodies resolved
   to one name slot and compilation order silently selected the effective body.
   This defect became observable only after the client was allowed to send the
   command.

Both had to be fixed. Fixing only the server could never stop the Cry animation;
fixing only the client would send the command into an ambiguous server binding.

## The command to use: `::~crystal_set`

`::~name` is the canonical spelling for every server-side debugproc and built-in
cheat. It works with the **pristine** revision-239 cache and does not require a
clientscript rebuild:

1. Script 7304 removes `::` and examines `~crystal_set`.
2. `~crystal_set` cannot prefix-match the local `cry` alias, even in the
   original broken prefix-matching clientscripts, so the local handler returns 0.
3. Script 73 calls `docheat`, which sends `~crystal_set` in `CLIENT_CHEAT`.
4. The server strips the single namespace marker and dispatches
   `[debugproc,crystal_set]`.

The same escape works for local-name collisions such as `::~run 1` and for the
server's built-in diagnostic ladder. Plain `::name` remains accepted when the
client does not consume it locally, and a baked cache with exact alias matching
continues to make `::crystal_set` work. Use `::~name` in automation because it
does not depend on which cache the client booted.

## What the client actually did

`OSRS-Content/osrs239-content/scripts/script_73.cs2` is the chatbox Enter
handler. On Enter it first calls `~script7304(%varcstring335)`. Only if that
local handler returns 0 does text beginning with `::` reach `docheat(...)`.

`script_7304.cs2` removes the `::` or `!`, lowercases the remainder, and maps
typed local aliases to emote components. The broken Cry test was equivalent to:

```text
string_indexof_string("crystal_set", "cry", 0) = 0
```

That expression is true. Script 7304 selected emote sub-id 16, called
`cc_triggerop(1)`, and returned 1. Script 73 then cleared the input and never
called `docheat`. Prefix matching was invalid for every alias, not merely Cry:
these are complete zero-argument local commands, so `runanything`,
`dance_command`, and every other alias-prefixed name could also be stolen.

The correct rule is exact equality for every local alias:

```text
compare($string0, "cry") = 0
```

Exact `::cry` therefore remains local, while `::crystal_set` falls through to
`docheat` and leaves the client as revision-239 `CLIENT_CHEAT` opcode 34.

## Why the server was silent

The packet trace for the failure contained keyboard telemetry, an interface
button packet, and local animation 860, but no opcode 34:

```text
packet_out op=54 len=-2    EVENT_KEYBOARD
packet_out op=47 len=9     IF_BUTTONX
actor ... anim=860         Cry
```

The decisive missing line was:

```text
packet_out op=34 len=-1    CLIENT_CHEAT
```

No server subsystem can log, reject, or execute a packet that the client never
sends. “No server error” did not mean the script ran successfully. It meant the
investigation was observing one boundary too late.

## The duplicate server command

After the client fix, two `[debugproc,crystal_set]` bodies still existed:

- an obsolete Gauntlet helper that attempted to put charged pieces in the
  backpack and could refuse when space was low;
- the combat-owned command intended to equip an immediately usable set and bow.

Debugprocs are global `::` commands. Two declarations cannot sensibly compose.
The old compiler declaration pass nevertheless assigned both headers entries;
the emit pass used a first-name lookup and wrote both bodies to the same script
slot. The final behavior depended on traversal order, with no collision error.

The obsolete Gauntlet declaration was removed. The sole owner is now:

```text
OSRS-Content/osrs239-content/server/scripts/
  skill_combat/scripts/player/crystal_set.rs2
```

That command raises its own Ranged and Agility requirements, equips the Crystal
helm/body/legs and infinite Bow of faerdhinen directly into `worn`, clears an
incompatible shield, refreshes equipment, and reports the computed set bonus.
It does not merely place four items in a backpack and leave a fresh account
unable to wear them.

## Permanent guards

The incident is now protected at every boundary that failed:

| Boundary | Enforced invariant | Guard |
|---|---|---|
| pristine-cache escape | `::~name` arrives in `CLIENT_CHEAT` as `~name`; server strips `~` before all command routing | `handle_cheat` + semantic self-test |
| local client command | Baked caches use exact emote aliases; exact `cry` still exists | `tools/check_crystal_set_contract.py` |
| chat control flow | script 7304 runs before `docheat`, making the interception order explicit | same checker |
| wire mapping | rev-239 `CLIENT_CHEAT` remains opcode 34 | same checker + packet table |
| command ownership | exactly one `crystal_set` debugproc, at the combat-owned path | same checker |
| all debug commands | duplicate global `[debugproc,name]` declarations fail compilation | `SSC_Declare` + `test-ssc` negative test |
| command semantics | required stats, four worn objects, and equipment refresh remain in the canonical proc | contract checker |
| runtime dispatch | every received command logs `ran`, `FAILED`, or `not found`; aborts notify the player | `handle_cheat` |
| actual result | self-test asserts worn helm/body/legs/bow and Ranged/Agility 99, not merely a successful return | `ToriRSServer --selftest` |
| documentation | this file is required by the contract checker | contract checker |

The contract gate is a prerequisite of **both** `torirsserver-scripts` and
`torirsserver-cache`. This is deliberate: one builds the server half while the other
packs the clientscript half. Either workflow must stop before publishing an
incomplete fix.

Run the focused gates from the repository root:

```sh
make -C src check-crystal-set-contract
make -C src test-ssc
make -C src torirsserver-scripts
make -C src test-ToriRSServer
```

The checker includes negative controls proving that a restored Cry prefix test
and a second crystal debugproc make the check fail. `test-ssc` separately creates
two temporary `[debugproc,same_command]` files and requires the compiler to
reject them with the command name in its diagnostic.

## How to diagnose any future silent `::` command

Follow the boundaries in order. Do not begin by editing the server script.

| Observation after Enter | Meaning | Inspect next |
|---|---|---|
| no relevant outbound packet | the input was not submitted | focus, live chat buffer, JCTL invocation |
| opcode 69 `MESSAGE_PUBLIC` | submitted as speech | missing or mangled `::` |
| opcode 47 `IF_BUTTONX` | a local clientscript consumed the text | typed emotes and other chat-local shortcuts |
| opcode 34 `CLIENT_CHEAT`, no server line | wire decode/dispatch gap | rev table and inbound parser |
| server logs `not found` | no matching command in the loaded pack | spelling, source ownership, stale `script.dat` |
| server logs `FAILED` | the matching debugproc aborted | VM backtrace and server log |
| server logs `ran`, wrong state | script semantics or stale artifacts | assert worn/inventory/stats, not return value |

For JCTL, quote the whole positional command:

```sh
python3 tools/runelite239_ctl.py 'type ::~crystal_set'
```

Writing `python3 tools/runelite239_ctl.py type ::~crystal_set` supplies two JCTL
commands, not one command with an argument, and creates a different failure.

## Rebuild rule that is easy to miss

`::~crystal_set` needs no client cache rebuild. Rebuild `ToriRSServer` and the server
script pack, then keep serving the pristine cache if desired:

```sh
make -C src ToriRSServer torirsserver-scripts
```

By contrast, making the unescaped historical spelling `::crystal_set` work
changes client content. `make -C src torirsserver-scripts` cannot do that; repack the
clientscript into the exact cache served by JS5:

```sh
make -C src torirsserver-cache
```

Both the world and JS5 must use that baked cache. A modified `.cs2` source next
to an unchanged `cache.osrs239` does nothing at runtime because RuneLite executes
the clientscripts in the served cache, not source files from the repository.

## Expected end-to-end proof

The success trace is:

```text
JCTL input: ::~crystal_set
packet_out op=34 len=-1
torirsserver: [debugproc,crystal_set] with 0 int and 0 string args
torirsserver: cheat 'crystal_set' -> debugproc ran
Crystal set equipped at Ranged 99: +30% accuracy, +15% damage.
```

Container 94 (`worn`) must then contain Crystal helm in head, Bow of faerdhinen
(c) in weapon, Crystal body in torso, and Crystal legs in legs. Neighboring
controls must also hold: exact `::cry` stays local and plays Cry, while a
definitely missing command sends opcode 34 and produces a visible
unknown-command message.

The longer integration narrative and original real-client evidence remain in
`GPTSOL56_RUNELITE_INTEGRATION.md`, but this file owns the incident and the
invariants that prevent its return.
