# Porting interfaces across eras: LostCity → Kronos → OpenRune/xrsps

> **What this is for.** `OSRS-Content/osrs239-content` is a LostCity content tree
> driving a rev-230 client. LostCity's interface vocabulary is the 2004 one and
> it does not describe what a rev-230 client needs, so every ported interface
> stalls in the same place. This document says exactly where the two models
> differ, which of the three reference servers answers each question, and what
> the C side has to grow for a LostCity script to drive a modern panel.
>
> Read `docs/LOSTCITY_PORT_TRIAGE.md` §7.3 first for the *size* of the interface
> gap. This is the *shape* of it.

## 0. The one-paragraph version

A 2004 interface is a **tree of components the cache fully describes**: the
client draws it, decides what is clickable from the component's own fields, and
the server's whole job is `if_openmain(iface)` plus `if_settext(component,…)`.
A modern interface is **a program**: the cache ships clientscripts that build the
panel's children at run time (`cc_create`), read player state out of varps,
varbits, enums and obj params, and paint from that. The server's job moved from
*drawing* to *stating facts and granting permission* — set the varbit, mount the
sub-interface into a named slot, and arm the ops you are willing to hear about.
Porting a LostCity script is therefore rarely "translate the commands"; it is
"work out which of those three things the panel is waiting for".

---

## 1. Three references, three eras, one question each

| | LostCity_Server | Kronos (`kronos-osrs-184`) | OpenRune-Server / xrsps-typescript |
|---|---|---|---|
| protocol era | rev 254 (2004) | OSRS 184 | OSRS ~220-230+ (rsprot) |
| language | RuneScript (`.rs2`) | Java, imperative | Kotlin plugins / TypeScript |
| **ask it about** | what a *content script* should look like | the **minimum** a server must send to make a modern gameframe work | what the *current* protocol actually is, and named ids |

Their value is not equal and it is not interchangeable:

- **LostCity** is the only one that answers *"how should this be written as
  content?"* — triggers, procs, queues, the `[if_button,x]` shape. It is the
  model for the tree, and useless as a model for the wire.
- **Kronos** is the most useful one and the least obvious. It is a *bare*
  server driving a real OSRS gameframe from Java, so its login burst is a
  stripped list of exactly what a client will not start without. When something
  is blank, `DisplayHandler.java` is the fastest place to find out what the
  server forgot to send.
- **OpenRune** is the authority on the modern shape, because it is built on
  **rsprot** — the same protocol library `docs/osrs230_mockserver.md` calls the
  reference decoder. Its `PlayerInterfaceExtensions.kt` is effectively a typed
  index of every interface packet at this revision.
- **xrsps** is a fourth thing and worth keeping separate: it is a *custom*
  protocol over WebSocket (its `varp_transmit` client→server message does not
  exist in real OSRS). Read it for **behaviour and ids**, never for wire shape.

---

## 2. The four axes that actually change

Everything below is one of these four. When a ported panel is blank or dead,
work down this list.

### 2.1 Addressing: flat id → `(interface << 16) | child`

```
LostCity   inventory:com_3            one flat id space shared with interfaces
Kronos     sendString(701, childId,…) interface and child passed separately
OpenRune   "components.side_journal:tab_container".asRSCM()   packed uid
```

This tree already speaks the packed form (`interfaces/<name>.compack`), so the
port is mechanical — **except** for one place it leaks into a format:

> **Trap.** A compiled ServerScript trigger key is an i32 with the subject at
> bit 10, so a trigger subject must be under 2²¹. `bankmain:deposit_line` is
> interface 12 and fits; `orbs:runbutton` is `160 << 16` and does not. Those
> scripts compile **name-addressed** and the engine resolves them through
> `mock230_scripts_run_if_button_named`. Do not widen the on-disk key — it is
> LostCity's format and `test-ss-roundtrip` proves this compiler reproduces it
> byte for byte.

### 2.2 Permission: the cache decides → the server decides

This is the big one and it is the cause of most "clicking does nothing".

| era | mechanism |
|---|---|
| 2004 | the component's own `buttonType` / ops. Live as soon as it is drawn. |
| OSRS 184 | `sendAccessMask(iface, child, minSub, maxSub, mask)` |
| OSRS 230 | `IfSetEvents(component, start, end, events:i32)` |
| OSRS 239+ | `IfSetEventsV2(component, start, end, events1, events2)` — 64-bit |

**Nothing is clickable until the server says so.** The cache still says the
component *has* an op — the client will hover it, highlight it, and put the verb
in the menu — and then the click runs the component's local clientscript and
tells the server nothing. That is exactly what "the orbs aren't clickable" was:
`orbs:runbutton` carries `op1=Toggle Run`, the client showed it, clientscript
7557 flipped its own copy of `option_run`, and no packet ever left.

The bit layout moved, and this bit us at exactly our revision:

```
rev 230  (IfSetEvents,   i32)   bit 0  = op-less click ("pause button")
                                bits 1-10  = ops 1..10
                                bits 11-16 = target kinds (obj/npc/loc/player/inv/com)
                                bits 17-19 = drag depth
                                bit 20 = drag target, bit 21 = target

rev 239+ (IfSetEventsV2, i64)   bits 1-10 are DEPRECATED ops
                                bits 32-63 are the real ops 1..32
```

We speak the rev-230 wire (`manifest_osrs230.ini`) against a rev-239 *cache*, so
the **v1 layout is the correct one** even though the cache is newer. rsprot's
`protocol/osrs-230/.../IfSetEventsEncoder.kt` vs `osrs-239/.../IfSetEventsV2Encoder.kt`
is where to check this, and it is the single easiest thing to get silently wrong:
a v2 mask sent over a v1 packet arms nothing and reports no error.

**Corollary the reference states and is easy to miss:** the client *purges* a
component's events when its interface unmounts. Anything armed on a panel that
gets closed and reopened — every journal sub-tab, every dialogue — must be
re-armed on every open. xrsps writes this down in `sideJournal.ts`; OpenRune
models it as `ui.events.clear(interf)` inside `closeModal`/`closeOverlay`.

### 2.3 Mounting: named slots → an arbitrary component

```
LostCity   if_openmain(iface)  if_openside(iface)  if_openoverlay(iface)
           — the slot is baked into the command name; there are three
Kronos     sendInterface(childIface, parentIface, parentChild, type)
OpenRune   ifOpenSub(iface, "components.<any>:<any>", Modal|Overlay)
```

Panels **nest** now, and that is why three fixed slots are not enough vocabulary:
the gameframe alone has 24 slots, and the side journal's five tabs all mount into
one component of an interface that is itself mounted into the gameframe. The
target has to be an argument.

Kronos is the clearest illustration, and it also shows the slot *moving between
revisions* — which is the reason to check rather than copy:

```java
// Kronos, OSRS 184
ps.sendInterface(399, 629, 2, 1);     // questlist into side_journal:2
```
```kotlin
// OpenRune, OSRS 230
player.ifOpenOverlay("interfaces.questlist", "components.side_journal:tab_container")
//                                            ^ 629:43 at this revision
```

### 2.4 Population: the server paints → the panel paints itself

| era | how a list gets its rows |
|---|---|
| 2004 | server `if_settext` per component; the components exist in the cache |
| OSRS 184 | server `sendString` per row + `sendClientScript(135,…)` to restyle |
| OSRS 230 | the panel's own clientscript `cc_create`s the rows, reading enums, obj params, varbits and stats |

The rev-230 prayer book is the cleanest example and it is worth internalising,
because *nothing about it is a packet*:

```
prayerbook:universe onload -> script 458 -> script 547
    enum 4956                      -> 29 obj ids, one per prayer
    oc_param(<obj>, param_1751)    -> the component to build the button in
    oc_param(<obj>, param_1752/4)   -> its name and description
    stat_base(stat_5), map_members  -> which enum, i.e. which prayer set
    varbit prayer_<name>            -> whether to draw it lit
```

The server never sends a prayer button. It sends a **stat** and a **varbit**,
and the cache does the rest. So "the prayer tab is empty" was not a missing
packet at all — it was `oc_param` returning the ParamType default because the
client's obj decoder had dropped every record's params.

**The rule this produces:** before adding a packet, find the clientscript and
read what it consumes. Three quarters of the time the panel wants a var, not a
paint.

---

## 3. Converting one LostCity interface script — the procedure

Given a LostCity `.rs2` that drives an IF1 interface:

1. **Find the modern interface by name, not by id.**
   `grep -n "=<name>" OSRS-Content/osrs239-content/pack/3_interfaces.pack`.
   Ids are not stable across eras; names very nearly are.

2. **Decompile its onload before writing anything.**
   ```sh
   3rd/rscache/tools/cs2/cs2 decompile --rev osrs239 --cache cache.osrs239 --out /tmp/cs2 <id>
   ```
   The `onload=` field of the interface's `[universe]` block in
   `interfaces/<name>.if` names the script and its arguments. Read it. It tells
   you which varps, varbits, enums, params and stats the panel reads — that list
   *is* the server's work item.

3. **Classify every LostCity command in the script:**

   | LostCity | rev 230 |
   |---|---|
   | `if_openmain` / `if_openside` / `if_openoverlay` | `if_opensub(<component>, <interface>, <type>)` |
   | `if_settext(com, s)` | usually **delete it** — the panel reads a var. Keep only for genuinely free-text components. |
   | `if_setobject` / `if_setmodel` / `if_setanim` | same commands, packed uid |
   | `if_settab` / `if_settabactive` | gone. The tab strip reads a varbit. |
   | `split_init` / `split_get` / … | delete for modern multiline chat; retain for fixed single-line server-painted rows such as `questjournal` (triage §7.4) |
   | *(nothing)* | **add `if_setevents`** for every op the script's `[if_button]` expects |

4. **Add the arming, and put it where the panel opens** — not only at login.
   OpenRune models this as `onIfOpen("interfaces.side_journal") { … }`. This
   tree has no if-open trigger yet, so arming lives in `[proc,<x>_login]` called
   from `[login,_]`; a panel that closes and reopens will need the trigger.

5. **Write the button triggers against component names**, e.g.
   `[if_button,orbs:runbutton]`. `last_verb` carries which op, `last_slot` which
   sub-id — one trigger answers a component's whole op list.

6. **Verify with the client, not with the log.** A `IF_SETEVENTS` in the server
   log proves only that a packet left.
   ```sh
   SDL_VIDEODRIVER=dummy TORIRS_NO_MOCK=1 \
     TORIRS_SIM_CLICK_AT="200,545,128" TORIRS_EXIT_BMP=/tmp/shot.bmp \
     TORIRS_MAX_FRAMES=300 ./src/torirs --manifest manifest_osrs230.ini --user testc --pass test
   ```
   and check `mock230: <- IF_BUTTON1 160:28` came back.

---

## 4. What the C side still owes this

Landed (see `docs/osrs230_mockserver.md`):

- `if_setevents(component, from, to, events)` — opcode 11000
- `if_opensub(component, interface, type)` — opcode 11001
- `p_countdialog_noprompt` — opcode 11004. `p_countdialog` writes the chatbox
  "Enter amount" prompt *and* parks the script; this is the park on its own, for
  a rev-230 interface that produces the number itself and answers with the CS2
  `resume_countdialog`. See [`bank_pin_server_reqs.md`](bank_pin_server_reqs.md)
  §4 — and note that the CS2 opcode on the other end of it (3104) had no handler
  at all until that feature needed one.
- name-addressed `[if_button,<iface>:<com>]` dispatch for components above
  interface 31

Not landed, in the order they block things:

1. **An if-open trigger.** OpenRune's `onIfOpen`. Without it every arming has to
   happen at login and cannot survive a panel being closed and reopened — which
   the client's event purge (§2.2) makes mandatory, not cosmetic.
2. ~~**`runclientscript` with string arguments.**~~ **Landed**, and the entry
   was wrong about what it was waiting on. The sender always took a per-argument
   type string (`mock230_send_run_clientscript_mixed`); what was fixed at one
   int and two strings was the *opcode*. `runclientscript*`
   (`SS_OP_RUNCLIENTSCRIPTVARARG`, 11003) sends any mix, with the arity decided
   at the call site, and content uses it. `~p_choice*` was never blocked by it
   either — it ships on `runclientscript_ss` and is bounded by three caps
   instead. See [`runclientscript.md`](runclientscript.md).
3. **`if_closesub(component)`.** Switching a journal tab must close the previous
   panel; leaving it mounted stacks two overlays in one slot.
4. **`if_setevents` on grids.** The `from`/`to` range is plumbed but nothing in
   content uses it yet; the bank and inventory still arm from C.

---

## 4b. When the panel is the client's fault, not the server's

§2.4 says "before adding a packet, find the clientscript and read what it
consumes", and that is still the first move. But a rev-230 panel is a *program*,
and a program the client runs slightly wrong fails in a way that looks exactly
like a missing packet: the panel mounts, its onload runs, and it draws nothing.
Six panels were blank for six different reasons and only one of them was a
packet. The pattern that separates them:

> **A blank panel whose interface IS mounted is a client bug until proven
> otherwise.** Check `TORIRS_DUMP_BOUNDS=<group>` first — if the geometry is
> absurd (a 190x261 sidebar panel resolving to 500x1) the server did its job and
> the client mis-ran the script.

The six, and what each one actually was:

| symptom | cause | where |
|---|---|---|
| quest tab blank | runtime hook args capped at 32; the journal's hooks carry **44**, and the dropped tail held the component the hook writes to | `CS2VM_SETON_INT_ARG_MAX` |
| combat tab has no attack styles | `db_find*` takes **three** stack arguments (column, value, type-tag), not two, and the tuple nibble is **1-based** | `exec_db`, `rs_cs2_host.c` |
| magic tab empty but for "Filters" | the spellbook sort recurses to depth **70**; the frame cap was 50 | `CS2VM_MAX_FRAMES` |
| orbs are black circles | the "empty" overlay is a zero-height clipping layer; a degenerate clip was treated as *no* clip instead of *clip everything* | `UITree_LayerCullsChildren` |
| no stack numbers on items | rev 230 has no TYPE_INV grid — items are `cc_create`d widgets, and only the grid path drew counts | `emit_obj_stack_count` |
| music tab dies | `define_array` carries an element **type**; an `s` array lives on the string stack | `CS2VM2_Array.is_string` |
| summary cells left-aligned, icons off the panel | `parawidth` measured the bytes of `<col=…>` markup the renderer never draws, so a 1-glyph value measured 104px | `rs_cs2_font_wrap` |

Three of those (hook args, DB stack shape, array types) share one shape worth
naming: **the client silently dropped part of a script's data and the script
carried on with a shifted stack.** The abort then surfaces at an unrelated
opcode — script 2621 "failed at opcode 40", which reads like a bad gosub and is
really a blown call stack. When a CS2 failure names an opcode that looks
innocent, suspect the stack, not the opcode.

The last row is a different lesson and just as general: at rev 230 **the colour
is in the text**, so most strings a panel measures carry markup. Anything that
walks a string to work out how wide it will be is a second implementation of the
renderer and will drift from it — make it call the renderer's tokeniser
(`ToriDraw_FontMarkupTokenLength`) rather than its own byte loop.

And one server-side trap, which is §2.1 biting in a new place:

> **A gameval symbol from a neighbouring revision is not evidence.** The world
> map orb was armed on 160:53 because OpenRune's rev-235 table calls 160:55
> `orbs:worldmap` and 53 `wiki_icon`, so it looked like components had been
> inserted and the rev-230 orb must be lower. The cache disagrees twice:
> `interfaces/orbs.compack` names 53 `wiki_icon_graphic` and 55 `worldmap`, and
> the orb's own onload (1492 → 1700) installs "Floating World Map" on the
> argument it passes as 10485815 = 160:55. **Read the compack and the onload;
> they are the revision you are actually running.**

Each of these is written up in full — symptom, dump output, the arithmetic that
proves it, and the fix — in
[`docs/REV230_UI_BLANK_PANELS.md`](REV230_UI_BLANK_PANELS.md).

---

## 5. Reading list, by question

| question | file |
|---|---|
| what does a modern login burst have to send? | `kronos-osrs-184/…/network/incoming/handlers/DisplayHandler.java` |
| what is the exact packet at rev N? | `rsprot/protocol/osrs-<N>/…/game/outgoing/interfaces/` |
| what is the typed API over those packets? | `OpenRune-Server/content/src/main/kotlin/org/alter/interfaces/PlayerInterfaceExtensions.kt` |
| how is a real panel driven end to end? | `OpenRune-Server/…/org/alter/interfaces/journal/` |
| what are the event mask bits? | `rsmod/engine/game/src/main/kotlin/org/rsmod/game/type/interf/IfEvent.kt` |
| which ids does a panel use? | `xrsps-typescript/src/shared/ui/` and `server/src/widgets/` |
| how should the *content* read? | `LostCity_Server/content/scripts/interface_*/` |
| what does the client actually do with it? | `cs2 decompile`, then `TORIRS_CS2_TRACE=1` |

`LostCity_Server/content/scripts/engine.rs2` deserves one warning: it carries
**commented-out** declarations for commands the reference never implemented,
including a nine-argument `if_setevents` from a later RuneScript dialect. A
commented-out declaration is not a specification — it does not match the rev-230
packet, which has four fields.
