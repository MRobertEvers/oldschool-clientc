# The modal that would not centre

> **What this is.** In resizable mode every main modal — the skill guide, the
> bank, anything the server opens into `toplevel_osrs_stretch:mainmodal` — drew
> down and to the right of where it belongs, its right edge under the sidebar
> and its bottom edge under the chatbox. The layout engine was correct. So was
> the cache. The client was simply booting with a setting at a value no real
> account has, and one branch of the cache's own interface-window helper is only
> correct at the other value.
>
> Verified against `Deobfuscator/src_osrs239_rl1_12_33` and the decompiled
> clientscript corpus (`3rd/rscache/tools/cs2/cs2 decompile --rev osrs239`).

---

## 0. The one-paragraph version

`settings_interface_resizing` (varbit **17772**, `settings_varp_5` bit 27) is a
client setting nobody transmits, so an unseeded client came up with it at 0.
Clientscript `~script7925` reads it, and the cache's interface-window helper
(`script1898`, and `script1904` for the skill guide) picks one of two ways to
place a modal's panel from the answer. At 1 it clamps the panel's saved default
box to the host, which collapses to (0,0) — the panel lands exactly on the
512x334 `mainmodal` slot, which is already centred. At 0 it instead writes
`if_setposition(if_getx(mainmodal), if_gety(mainmodal), 0, 0, panel)` — and
`if_getx` returns a **parent-relative** origin. In resizable mode the slot is
centred inside `hud_container_front`, so that adds the centring offset a second
time, *inside the slot*, and the panel walks off by half the chrome insets
(+374, +219 on a 1511x938 canvas).

The fix is one seeded varbit. Nothing in the layout code changed.

---

## 1. What the reference actually specifies

Four things, each read out rather than assumed.

### 1.1 The layout math (deob)

`Statics.method6166` is `alignWidgetPosition`. The six x/y modes it implements —
absolute, centred, far-edge, and the three `>> 14` proportional variants —
match [`src/ui/ui_if3_layout.h`](../src/ui/ui_if3_layout.h) case for case,
including that mode 1 is `parentDim - selfDim` divided by **Java integer
division**, not an arithmetic shift.

### 1.2 Where a mounted sub-interface goes (deob)

`Statics.method3275` → `method6179` → `method6192`. A layer that hosts a mounted
group lays that group's roots (`parentId == -1`) out against the **host's**
scroll-size-or-size, and `class163.java:421` draws them at the host's *own*
absolute origin — unscrolled, unlike its ordinary children. That is exactly
what `layout_parent_box` plus the mount sweep in
[`src/ui/uitree_layout.c`](../src/ui/uitree_layout.c) already did.

### 1.3 Where the slot is (clientscript 909 + the cache)

`toplevel_resize` (clientscript 909) sets `hud_container_front` (161:15) to the
canvas minus the toplevel's four chrome insets — 0 / 0 / 250 / 165 at rev 239,
so 1261x773 on a 1511x938 canvas — and forces `mainmodal` (161:16) to
**512x334**. The slot's own x/y modes come from the cache and are 1/1 with base
0, i.e. **centred in 161:15**.

Clientscript 910/911 confirms this independently and from the other side: the
modal dimmer is four panels painted around a 512x334 hole *centred in 161:15*.
Wherever the panel ends up, that hole does not move.

### 1.4 What `if_getx` returns (deob)

CS2 opcode **2500** pushes `class308.field4070`, and `field4070` is the field
`method6166` writes as `(parentWidth - width) / 2 + rawX`. So `if_getx` is
parent-relative — the same thing `UITree_GetRelativeX` returns. That is the
whole hinge of this bug.

---

## 2. The two branches

`~script7925` is the gate:

```
[proc,script7925]()(int)
if (getwindowmode = ^windowmode_fixed) return(0);
if (~script1972 = 1) return(0);          // mobile
if (%varbit17772 = 0) return(0);         // settings_interface_resizing
return(1);
```

and `script1904` (the skill guide's layout script; `script1898` is the generic
version used by every other window) is where it lands:

```
def_component $component15 = enum(component, component, ~script900, interface_161:16);
def_int $int16 = 512;
def_int $int17 = 334;
def_int $int18 = if_getx($component15);          // <- parent-relative!
def_int $int19 = if_gety($component15);
if ($int13 = 0) {                                 // $int13 = 0 means 7925 said 1
    $int16 = max(512, min(%varcint1168, $width10));
    $int17 = max(334, min(%varcint1169, $int12));
    $int18 = max(0, min(%varcint1170, calc($width10 - %varcint1168)));
    $int19 = max($int11, min(%varcint1171, calc($int11 + $int12 - %varcint1169)));
}
if (~script1972 = 0) {
    if_setposition($int18, $int19, 0, 0, $component3);   // 860:1, the panel
    if_setsize($int16, $int17, 0, 0, $component3);
}
```

`%varcint1168..1171` are the slot's width/height/x/y, cached once when the
interface opened (`script1902`). `$width10` is `if_getwidth(860:0)` — the
modal's own root, which fills the slot.

**Setting on.** `$width10 - %varcint1168` is `512 - 512 = 0`, so
`max(0, min(374, 0))` is **0**. The panel is placed at its root's top-left,
which *is* the slot, which is already centred. The clamp is also what makes the
window draggable without escaping: it is a "keep the saved box inside the host"
expression that happens to be a no-op while the host and the panel are the same
size.

**Setting off.** `$int18` keeps `if_getx(mainmodal)` = **374**, `$int19` keeps
219, and those are offsets *within 161:15* being applied *within the slot*. The
panel lands at 353+374, 219+219 = **(727, 438)** — down and right by exactly
half the chrome insets, out from under the dimmer hole.

That branch is only self-consistent where the slot's relative origin is (0,0),
and the other two clauses of `~script7925` are exactly the cases where it is:
fixed window mode, and mobile. Resizable is not one of them.

---

## 3. How it was pinned down

The whole chain is observable headlessly. `TORIRS_DUMP_BOUNDS=<group>` prints
each component's resolved box **and its live position modes**, which is what
separates "the cache says centred" from "a script overwrote it with absolute
coordinates":

```bash
MOCK230_SAVES=$SCRATCH/saves \
MOCK230_SCRIPTS=OSRS-Content/osrs239-content/server/scripts/build_summoning \
MOCK230_CACHE=$PWD/cache.osrs239.summoning \
SDL_VIDEODRIVER=dummy TORIRS_NET_CHEAT="summoning_unlock;summoning_demo" \
TORIRS_MAX_FRAMES=420 \
TORIRS_SIM_CLICK_AT="200,1282,621;240,1285,896" \
TORIRS_EXIT_BMP=$SCRATCH/guide.bmp TORIRS_DUMP_BOUNDS=860 \
  ./src/torirs cache.osrs239.summoning --manifest manifest_osrs239.ini \
  --soft3d --windowmode resizable --window 1511x938
```

Before:

```
BOUNDS com=0x035c0000 (860|0) abs=353,219 512x334 modes=w1,h1,x1,y1   <- the slot, correct
BOUNDS com=0x035c0001 (860|1) abs=727,438 512x334 modes=w0,h0,x0,y0   <- the panel, 374/219 off
```

`860:1`'s modes being `x0,y0` when the cache says `x1,y1` is the tell: a script
had run `if_setposition(..., 0, 0)` over it. After the fix, `860:1` is
`abs=353,219` and sits exactly on `860:0`.

Two confirmations that the *layout* was never at fault:

- flipping only `%varbit17772` at runtime
  (`TORIRS_SIM_RUNSCRIPT="150,3965,442"` — case 442 of the settings toggle)
  centred the panel with no code change at all;
- `dump_interface_layout --iface 860` at two different root sizes shows
  `860:1` tracking the centre, i.e. cache modes 1/1 base 0, exactly as decoded.

---

## 4. The fix

`settings_interface_resizing` is a *client* setting — clientscript 3965 case 442
toggles it with `setvarbit`, and no server transmits it — so the client owns its
default, and the varp table's implicit 0 is a value rather than an absence. This
is the same situation, and the same remedy, as the four audio volume varps
seeded a few lines above it in [`src/app.c`](../src/app.c): a settings varp left
at 0 makes an interface behave as though the player had chosen 0.

The id is a lineage fact rather than a universal one, so it lives in the era
table ([`src/features/features.h`](../src/features/features.h),
[`features.c`](../src/features/features.c)) as
`varbit_interface_resizing`, zero for LostCity (no resizable mode exists there)
and 17772 for the OldSchool tables. Rev 230's varbit table stops at 17425, so
the seed is a no-op on that cache rather than a write to somebody else's bits.

`app.c` then seeds it optimistically, before the tree is built, and a server
`VARP_SMALL`/`VARP_LARGE` still overrides it — the same precedence the volumes
document.

```c
if( app->features->varbit_interface_resizing > 0 )
    VarPManager_SetVarbitOptimistic(
        &app->varps, app->features->varbit_interface_resizing, 1);
```

Seeding it *at boot* rather than after login matters: the toplevel's onload and
`toplevel_resize` both read the setting, and the value has to be in place before
they first run.

---

## 5. The viewport rect, 21px to the left

The first pass left this flagged and unchecked: `161:92`/`161:94` are sized from
`viewport_geteffectivesize`, this client answered the full canvas, and centring
a canvas-wide child inside `161:34` — 42px narrower, to leave room for the
right-hand icon strip — resolved them to **x = -21**. Every descendant inherited
it, the modal slot included.

It is not reference-correct. Opcode **6203** (`Statics.method6341` case 6203)
reads:

```java
if (field6847.field6268 == null) { push(-1); push(-1); }
else {
    class159.method5357(0, 0, field6847.field6268.field4051,
                              field6847.field6268.field4052, false, …);
    push(client.field813); push(client.field837);
}
```

`field6268` is **the viewport widget**, latched by `method3791` (alignWidgetSize)
with `if (widget.clientCode == 1337) widgetManager.viewportWidget = widget` —
the same clientCode-1337 layer this tree already caches as `tree->world_index`.
So the answer is that widget's box, not the canvas, and `method5357` then
letterboxes it against the CLAMPFOV range before it is reported.

At 1511x938 the widget is 1469x938 (canvas minus the 42px strip), so
`161:92`/`161:94` now resolve to **x = 0** and the viewport rect
(`hud_container_front`) is 1219x773 instead of 1261x773 offset by -21. Its right
edge stops 9px clear of the sidebar block instead of running 12px under it.

The modal does not move: the slot is *centred* in that rect, and a container
42px wider shifted 21px left has the same centre. What changes is the rect's
extent — which is what clipping, the dimmer hole and anything else that measures
the viewport reads.

Three neighbouring opcodes had to be corrected to get there, all in the same
family and all checked against `Statics.method6341`:

| opcode | reference | what this client did |
| --- | --- | --- |
| 6202 `viewport_clampfov` | stores **four** bounds — a FOV min/max (`field804`/`field805`) and an aspect min/max (`field1040`/`field810`) — each defaulting when `<= 0`, each max raised to its own min, and does **not** touch the FOV | read the four as value/min/max, clamped the FOV with them and dropped the fourth, so `method5357` had no bounds to letterbox against and GETFOV answered the clamp |
| 6200 `viewport_setfov` | stores only the **decoded** endpoints (`method5659`, `2^(arg/256+7)`, defaulting to 256) | also kept the raw arguments |
| 6205 `viewport_getfov` | re-encodes with `method9013`, so the round trip is lossy — `setfov(512, 220)` reads back `512, 219` | answered the raw arguments |

The pre-SETFOV default moved with them, from a 128/896 placeholder to the
reference's 256/256.

`viewport_setzoom`/`getzoom` (6201/6204) were already right: raw `field780` /
`field747`, defaulting to 256/320 — the follow camera's orbit-distance
endpoints, a different pair from the FOV's despite the opcode names.

---

## 6. The modal dimmer is not a bug

The first pass also called this one out, and it was wrong to.

`%varcint172` and `%varcint173` are named by the cache's own gameval table:
**`toplevel_mainmodal_bg_colour`** and **`toplevel_mainmodal_bg_trans`**. Only
clientscripts 917 and 2524 (thin wrappers of 2525) write them, neither has a CS2
caller, so the server declares them when it opens a modal — and `script901`
resets both to **-1** on every toplevel open.

`script910` bails on `%varcint172 <= -1 | %varcint173 <= -1`, so -1/-1 means
"512x334 modal, no background". That is a legitimate reference state, it is
`toplevel_resize`'s own reset value, and it is exactly what every modal in this
content gets. There is nothing to repair.

What the trans field additionally carries is a pair of **size sentinels** read
by `toplevel_resize`: `-2` makes `mainmodal` 512 wide and full height, `-3`
makes it fill the whole viewport rect. Both are also `<= -1`, so both mean "no
background" as well.

`-3` is the interesting one, because it is the only thing that makes the cache's
own window machinery do anything: with the slot fixed at 512x334,
`$width10 - %varcint1168` is 0, so `script1904`'s clamp pins the panel and the
nine `cc_create` resize grips `script1902` builds can never be dragged. So some
interfaces are surely opened with it.

It was **not** applied here, because neither ordering produces the reference's
result and there is no per-interface value to read off:

- **declare, then open** — `script908`'s sub-change runs `toplevel_resize`, the
  slot expands to the viewport rect, and `script1902` then captures *that* as
  the window's saved default box. Measured: the guide's panel lands at the
  viewport's top-left, stretched to 1219x773.
- **open, then declare** — `script1902` captures the centred 512x334 box
  correctly, but nothing re-runs `toplevel_resize` afterwards, so the slot never
  expands and the result is identical to sending nothing at all.

Picking a colour, a transparency or a sentinel per interface is content
authorship against a value this tree does not have. Left alone deliberately.

---

## 7. How to apply this

- **A modal in the wrong place is not necessarily a layout bug.** Dump the
  component's live *modes* alongside its box. Cache-authored 1/1 that reads back
  as 0/0 means a clientscript overwrote it, and the question becomes which one
  and with what inputs — not what the layout engine did with the modes.
- **`if_getx`/`if_gety` are parent-relative.** Any script that feeds one into
  `if_setposition` on a component in a *different* parent is only correct when
  the two parents share an origin. That is a real pattern in the cache, and it
  is guarded by conditions (fixed mode, mobile) rather than by arithmetic.
- **A client settings varbit at 0 is a choice the player did not make.** Grep
  `~script` gates for `%varbit` before concluding the client is misbehaving; a
  proc like `~script7925` reading three inputs is three chances for the client
  to be answering a question wrong.
- **The gameval name table names these.** `settings_interface_resizing`,
  `toplevel_mainmodal_bg_colour` and `toplevel_mainmodal_bg_trans` all came from
  `cachepack unpack --types varbit,varc`, not from guessing what 17772 and
  172/173 do. Two of the three arguments in this case file were settled by a
  name.
- **A getter that answers the canvas is a guess until you read its opcode.**
  Three of the six `viewport_*` opcodes were wrong here in the same way — a
  plausible value that nothing had checked. `GETCANVASSIZE` and
  `VIEWPORT_GETEFFECTIVESIZE` are genuinely different questions.
- **"The reference does something odd here" is a finding, not a licence to make
  it tidy.** GETFOV's lossy round trip and the -1/-1 modal background both look
  like defects and are not.

Related: [`REV230_UI_OWNERSHIP.md`](REV230_UI_OWNERSHIP.md) — same triage
question (who owns this pixel), different answer.
