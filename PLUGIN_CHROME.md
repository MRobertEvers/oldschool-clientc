# Plugin chrome: dressing one part of a frame somebody else arranged

## Purpose

Split the one verb the plugin contract calls "owning the gameframe" into two,
so that a plugin which replaces the **report button** can coexist with a plugin
which replaces the **whole frame**, in a defined order, and can ask the frame
what it drew there instead of guessing.

The central rule is:

> Arranging the frame is exclusive per FRAME. Dressing a control is exclusive
> per PART. The arranger declares its parts; it does not blit them. Everything
> a dresser needs to know is therefore something the host holds, not something
> a plugin kept private.

## The problem, in the code as it stands

Three facts, all load-bearing:

1. **`EV_LAYOUT` already goes to exactly one plugin.**
   `plugin_dispatch_one(host, owner, TORIRS_PLUGIN_EV_LAYOUT, &ev)` at
   `src/plugin/torirs_plugin_host.c:4577`, fenced by `engine.layout_begin` /
   `engine.layout_end`. Half the ordering this plan needs already exists.

2. **`EV_DRAW_FRAME` is owner-only.**
   `src/plugin/torirs_plugin_host.c:4478`, deliberately: *"chrome drawn under
   the interfaces of a frame somebody else is arranging is chrome in the wrong
   place."* A report-button plugin therefore cannot draw at the right z at all.
   It is pushed onto `EV_DRAW_CANVAS`, which paints over dialogues.

3. **The sprite is private.**
   `src/plugin/plugins/gameframe.c:691-710` fills a plugin-local
   `FrameChatButton{x, y, w, filter, idle, hover, active, active_hover}` and
   blits it in `frame_on_draw` (`gameframe.c:1675`). The host never sees those
   handles; `api_image_pixels` refuses *"a handle this plugin does not own"*.

So today a chrome plugin must either **become** the frame owner — mutually
exclusive with `gameframe-layout` — or paint over the label's role box and
guess at the plate. That guess is wrong on every lane:

| Lane | Report button | Trap |
| --- | --- | --- |
| osrs239 | cache interface 162 component 31, 79x22 | the cache draws its own red button in that exact box |
| dat1 / `gameframe-layout` | `slot(chat_buttons, 3)` is 100x25 | the composed plate is 100x23, drawn `FRAME_O_CHAT_BUTTON_LIFT` rows lower, so painting the role box overhangs by ~3 rows |
| 2004 Classic Fixed | 100x32 | there is no plate at all, just white `Report abuse` on `backbase1` |

`role_replace` (ABI 17, `torirs_plugin.h:2943`) already solved the *cache-native*
half of this: display-none on a native subtree, exclusive per role, persistent
across rebuilds, released when the plugin stops. This plan is the same idea for
the *plugin-arranged* frame, and then unifies the two so a plugin writes one
sentence for all three lanes.

## The two tiers

**Tier 1 — arrange.** `layout_claim`, exclusive per frame, one plugin.
Unchanged.

**Tier 2 — dress.** `chrome_claim(slot, member)`, exclusive per part, granted to
any plugin, requires no frame ownership.

A chrome claim is modelled on the `role_replace` claim in every respect: owned
by the calling plugin, exclusive, persistent across tree rebuilds, valid while
its target is temporarily absent, released automatically at teardown.

## The pass order

One layout pass, in this order, from the function that already raises
`EV_LAYOUT` (`PluginHost_Layout`, `torirs_plugin_host.c:4523`):

```
engine.layout_begin()        slot table emptied
  EV_LAYOUT   -> the arranger, alone.  Places slots AND declares part art.
engine.layout_end()          applied; every box is now final
  EV_CHROME   -> each chrome claimant, in claim order.  Reads the final boxes
                 and the arranger's parts; declares its own.
  EV_LAYOUT_CHANGED -> everyone else, as today.
```

`EV_CHROME` is the whole of the answer to "gameframe first, then the report
button". Because it is raised after `layout_end`, a dresser can never read a
stale box; because the arranger cannot subscribe to it as a way of running
last, the tiers cannot invert.

Same declaration semantics as `EV_LAYOUT`: the dispatch **is** the declaration,
the claimant's part table is emptied before the call and applied after, a part
not mentioned is a part hidden. A table rebuilt from nothing cannot disagree
with itself.

Per-frame draw: `EV_DRAW_FRAME` to the owner (whole frame), then to each
claimant **clipped to its part**, ordered by `draw_order` then claim order.
The clip is what makes the frame surface safe to open to a non-owner.

## The part descriptor

```c
enum ToriRS_PluginChromeState
{
    TORIRS_PLUGIN_CHROME_IDLE = 0,
    TORIRS_PLUGIN_CHROME_HOVER,
    TORIRS_PLUGIN_CHROME_ACTIVE,
    TORIRS_PLUGIN_CHROME_ACTIVE_HOVER,
    TORIRS_PLUGIN_CHROME_DISABLED,
    TORIRS_PLUGIN_CHROME_STATE_COUNT
};

enum ToriRS_PluginChromeSource
{
    /** Nothing is there. */
    TORIRS_PLUGIN_CHROME_SOURCE_NONE = 0,
    /** A plugin arranger declared it. Art handles are real. */
    TORIRS_PLUGIN_CHROME_SOURCE_FRAME,
    /** The cache's or revconfig's own. Real box, art[] all -1. */
    TORIRS_PLUGIN_CHROME_SOURCE_LANE
};

struct ToriRS_PluginChromePart
{
    /*
     * The ART's box, which is NOT the role's box -- and that difference is
     * the whole reason this struct exists. slot_member_rect answers where
     * the LABEL mounts; this answers where the plate is.
     */
    int x, y, w, h;

    /*
     * One handle per state, -1 for a state this part does not have. Report
     * abuse is momentary, so ACTIVE is -1, and that is an ANSWER: it says
     * the button does not select, it opens something.
     */
    int art[TORIRS_PLUGIN_CHROME_STATE_COUNT];

    /* Where a caption or icon centres inside the art, for a replacement
     * that keeps the plate and changes only what is on it. */
    int label_x, label_y;

    /** enum ToriRS_PluginChromeSource. Written by chrome_part; ignored on
     *  the declaring calls. */
    int source;
};
```

## API additions (ABI 18)

Appended below the ABI 17 block in `src/plugin/torirs_plugin.h`, and
`TORIRS_PLUGIN_ABI` bumped to 18.

```c
/* -- ABI 18 append: chrome ------------------------------------------- */

/** Declare the art of one member of a placed role. Legal only inside
 *  EV_LAYOUT (asserted), and only for the frame's owner.
 *  @return 1 when the frame has that member, 0 otherwise. */
int (*layout_slot_art)(
    struct ToriRS_PluginCtx* ctx, int slot, int member,
    struct ToriRS_PluginChromePart const* part);

/** Claim or release exclusive dressing of one part. `enabled` nonzero
 *  claims (or idempotently restates), zero releases.
 *  @return 1 accepted, 0 for a part another plugin holds or a full table. */
int (*chrome_claim)(struct ToriRS_PluginCtx* ctx, int slot, int member,
                    int enabled);

/** Is this part claimed by ANY plugin other than the caller? The one line
 *  an arranger that still blits imperatively has to add. */
int (*chrome_claimed)(struct ToriRS_PluginCtx* ctx, int slot, int member);

/** Read the part as whatever authority currently owns it. Art handles come
 *  back already borrowed into the caller's own namespace.
 *  @return 1 and fills `out`, or 0 for a part this frame does not have. */
int (*chrome_part)(struct ToriRS_PluginCtx* ctx, int slot, int member,
                   struct ToriRS_PluginChromePart* out);

/** Declare the caller's replacement art for a part it holds. Legal only
 *  inside EV_CHROME (asserted).
 *  @return 1 recorded, 0 for an unheld part or a region not placed. */
int (*chrome_paint)(struct ToriRS_PluginCtx* ctx, int slot, int member,
                    struct ToriRS_PluginChromePart const* part);

/** Ops for the part's hit region, installed by the host with the
 *  declaration. `tag` comes back in EV_CANVAS_CLICK. Legal inside EV_CHROME.
 *  Called with op_count 0 to claim the pointer and offer nothing. */
int (*chrome_ops)(struct ToriRS_PluginCtx* ctx, int slot, int member,
                  char const* const* ops, int op_count, uint32_t tag);
```

And one event, appended to `enum ToriRS_PluginEvent` before `EV_COUNT`:
`TORIRS_PLUGIN_EV_CHROME`, payload `EvLayout` (the same canvas the arranger
was handed).

### Why the arranger declares instead of blitting

Once the part is a declaration, replacement needs no cooperation: the host
simply stops painting the arranger's version when someone else owns it. This is
the argument `layout_slot_overlay` already makes at `torirs_plugin.h:2894` —
*"the host never guesses that an ordinary draw intended to replace or decorate
some component. This declaration is explicit."*

For `gameframe.c` the cost is small: `frame_chat_buttons_across` already
computes every field of `ChromePart`, so it gains ~4 lines beside its existing
`layout_slot_at` and loses the blit loop at `gameframe.c:1675`.

An arranger with a part it genuinely cannot express declaratively — an animated
stone, a live gradient — keeps blitting in `EV_DRAW_FRAME`, but must then ask
`chrome_claimed(slot, member)` and skip. One line, and the host tells it the
truth instead of it guessing.

### Borrowed art handles

`PluginImage.plugin` is a single owner (`torirs_plugin_host.c:259`), and
`api_image_pixels` / `api_image_release` gate on `plugin_image_owned`. So the
arranger's handles are meaningless to a dresser as they stand.

`chrome_part` therefore hands back handles **already borrowed into the caller's
namespace** — a second reference onto the same resident scene entry, drawable
and measurable, never composable and never releasable by the borrower.
Idempotent: the same foreign image yields the same borrowed handle for the life
of the claim, so calling `chrome_part` every `EV_CHROME` does not leak.

Borrow rather than copy because Lua has neither `image_compose` nor
`image_pixels` today, and copying 100x23 ARGB per state per plugin per reload
is silly when the host already has the pixels resident.

## Edge cases

### A. Ownership and arbitration

**A1. Two dressers want the same part.** The second `chrome_claim` returns 0
and changes nothing. First-come and exclusive, the same rule and the same
reason as `layout_claim`: *"the loser must be able to carry on drawing whatever
it drew before, and a claim that half-succeeded would leave two plugins each
believing they own the stones."*

**A2. A dresser that is also the frame owner.** Allowed, and deliberately not
special-cased. It means "I state this part in `EV_CHROME` rather than
`EV_LAYOUT`", which is exactly what a plugin looks like the day before it is
split into two. The `EV_CHROME` declaration wins over the `EV_LAYOUT` one for
that part, on the same "last writer in the pass" rule that already governs
`layout_slot_at` over `layout_slot`.

**A3. The frame owner changes mid-session.** Claims survive: they key on
`(slot, member)`, never on the owner. The next pass's `EV_CHROME` hands the
dresser the new arranger's part, with new borrowed handles. A dresser that
re-reads every pass — which the declaration semantics require anyway — needs no
code for this.

**A4. The dresser is the frame owner and calls `layout_release`.** Its chrome
claims stand and now refer to the LANE part. Same as A3 with the lane as the
incoming arranger.

**A5. Claim table full.** Return 0 and log with the table size, exactly as
`api_layout_reserve` does at `torirs_plugin_host.c:1442`. `TORIRS_PLUGIN_CHROME_CLAIMS_MAX`
is 64, matching `TORIRS_PLUGIN_ROLE_REPLACEMENTS_MAX`.

**A6. A LANE part whose role is already `role_replace`d by another plugin.**
`chrome_claim` on a LANE part routes through `role_replace` internally (see
E2), so it must inherit that call's refusal: return 0. Conversely a standing
chrome claim makes a later `role_replace` on the same role name fail. One
exclusion set, two spellings into it.

### B. Lifetime and teardown

**B1. A dresser stops.** `plugin_teardown` (`torirs_plugin_host.c:3889`) drops
its chrome claims beside `plugin_reserves_drop_plugin` and
`role_replacements_drop_plugin`, and bumps the layout revision.

Critically: **the arranger's declared part is retained in the host table while
it is suppressed, not discarded.** Restoring it is then a flag flip and needs no
re-dispatch of `EV_LAYOUT`. Discarding it would mean a dresser being switched
off leaves a hole in the frame until the next resize.

**B2. The arranger stops while a dresser holds a claim.** `plugin_teardown`
already gives the frame back to the lane. The arranger's part table goes with
its declaration; every handle the dresser borrowed from it goes inert (B4). The
dresser receives an `EV_CHROME` on the next pass with `source = LANE`, or
`chrome_part` returning 0 on a lane that has no such member. Because a dresser
re-reads and re-declares every pass, this is not a state it has to detect.

**B3. Plugin reload.** Teardown drops the claim, the re-run start handler
retakes it. `(slot, member)` is stable across a reload, so unlike an image name
there is nothing to re-resolve.

**B4. A borrowed handle whose lender image is dropped.** This is the one place
a dangling reference could get into the scene, so it is handled by
construction: `plugin_image_drop` bumps a per-slot `generation`, and every
borrow row carries the generation it was taken at. A borrow whose generation no
longer matches is **stale**, and stale is defined to behave exactly like
*pending*:

- `image_size` answers 0 and returns 0
- `draw_image` draws nothing
- `chrome_part` re-borrows on the next call, picking up the new incarnation if
  there is one

Reusing the pending semantics rather than inventing a third state is the point:
every caller already has code for "the pixels are not here yet", written for
the IO queue, and that code is correct for this too.

**B5. Borrow table exhaustion.** Made impossible rather than handled. Borrows
are bounded by claims times states, so the table is a fixed
`TORIRS_PLUGIN_CHROME_CLAIMS_MAX * TORIRS_PLUGIN_CHROME_STATE_COUNT` array
(64 x 5 = 320 rows, 16 bytes each). There is no failure path to get wrong.

### C. Absence and timing

**C1. A claim taken before any frame owner exists**, or before
`APP_STATE_READY`. The claim stands, `chrome_part` returns 0, and no
`EV_CHROME` is raised until there is a declaration to read. This mirrors
`role_replace`'s *"remains valid while its target is temporarily absent and
rebinds to the next incarnation that resolves"*, and it avoids the known trap
that a declaration made before `APP_STATE_READY` finds no roles and stands for
ever.

**C2. A claim on a member this frame does not have.** Recorded; `chrome_part`
returns 0; the dresser paints nothing. An answer, not a fault — the same idiom
`layout_slot_at` uses so a layout can tell a frame with no Trade/duel button
from one that has it somewhere unexpected.

**C3. A dresser that claims and paints nothing.** The part is hidden. This is
declaration semantics ("a slot left unplaced is a slot HIDDEN") and it is also a
legitimate feature: a plugin whose whole purpose is to remove the report button
claims it and declares nothing. A dresser that wants the arranger's part *back*
releases the claim; those are two different sentences and the API says both.

**C4. A borrowed image still pending when the dresser wants to composite.**
The sharp one. `EV_CHROME` fires only on layout passes, but an image becomes
resident asynchronously off the IO queue, with no layout pass to follow — so a
dresser that skipped a pass because `image_size` answered 0 would never be asked
again.

Handled with a re-declaration flag: `plugin_image_publish` marks every claimant
holding a borrow on that image as needing re-declaration, and the frame loop
drains those by raising `EV_CHROME` for them alone on the next frame. This is
the same shape as `layout_slot_overlay`'s promise that a handle *"may still be
loading: its stable scene identity is retained and it begins drawing as soon as
its pixels land, without waiting for another EV_LAYOUT"* — except that a
composite has to be recomputed, so the plugin has to be re-asked rather than the
pixels merely appearing.

**C5. Canvas resize, gameframe rebuild.** Ordinary path: `EV_LAYOUT` then
`EV_CHROME`. Nothing special.

### D. Coordinates and clipping

**D1. A replacement bigger than the role box.** Legitimate and must work — a
wider button, a plate with a shadow. So the clip is **not** the role's member
rect. It is the claimant's own declared part box, further clipped to the *parent
clip of the role's node* — the rule `layout_slot_overlay` already states and
which already solved this exact question: *"That permits a housing to overlap
the live surface while preventing it from painting over unrelated chrome outside
the containing panel."*

**D2. A part declared with `w <= 0` or `h <= 0`.** Refused with a log, not
silently accepted. This is arithmetic gone wrong, and the lesson is already in
the tree: a claim laid out against a zero canvas *"does not produce a small
frame -- it produces one at negative coordinates, declared and drawn and
invisible"* (`torirs_plugin_host.c:4540`). Saying so is the difference between a
caller that fixes its call and one that spends an afternoon looking at the
plugin.

**D3. A part declared for a slot the arranger did not place this pass.**
`chrome_paint` returns 0. A part on a hidden region is a part nobody can see,
and letting it record would leave a button floating where its chatbox is not.

**D4. Every coordinate is canvas coordinates**, matching every other layout
call, so a dresser can mix `chrome_part`, `slot_member_rect` and `mouse_pos`
without converting.

### E. Input

**E1. The arranger's hit region for a claimed member.** The host installs
regions from the declaration, so a suppressed part's region is suppressed with
it — automatic for a declarative arranger. An arranger still calling
`hit_region` imperatively gates that on `chrome_claimed` too, the same one line
as its blit.

**E2. A LANE part.** `chrome_claim` takes an internal `role_replace` on the
canonical role name for the part, so the native widget's ops leave the menu
along with its pixels. The canonical spelling is `<slot>.<member>` —
`chat_buttons.report` — parsed with the functions that already exist,
`UITree_RoleSlotFromName` and `UITree_RoleSlotMemberFromName`
(`src/ui/uitree_role.c:38` and `:50`), which already accept both the name and the
number for a member. No second table to keep in step.

This is the payoff of the whole plan: a plugin writes *"I own the report
button"* once and it holds whether that button is the 2004 lane's
`UIELEM_BUILTIN_CHAT_BUTTON`, `gameframe-layout`'s composed plate, or osrs239's
interface 162 component 31.

**E3. A click that arrives after ownership changed.** `hit_region`'s contract
already warns that *"a region outlives the frame it was declared in by one --
the menu is built from the previous frame's list"*. So the click is routed by
**who holds the claim at dispatch time**, not by who declared the region. A
plugin that lost or released a part in between hears nothing, exactly as a
disabled plugin does today.

**E4. `chrome_ops` with `op_count` 0.** Claims the pointer and offers nothing —
a part that must stop a click falling through to the widget behind it. Same
semantics as `hit_region`'s NULL ops.

### F. Ordering determinism

**F1. Two dressers on different parts.** Claim order. Ties are impossible,
because claims are unique per part.

**F2. Draw order within `EV_DRAW_FRAME`.** Owner first — it is the backdrop and
`gameframe-layout` already declares `draw_order = -100` — then claimants by
`draw_order`, then claim order. `plugin_sub_key`
(`torirs_plugin_host.c:499`) already implements exactly this for the three draw
events and needs only to admit the claimants.

**F3. Wrong-phase calls.** `chrome_paint` and `chrome_ops` outside `EV_CHROME`,
`layout_slot_art` outside `EV_LAYOUT`, `layout_claim` from inside `EV_CHROME` —
all assert, matching the existing *"layout_slot is legal only inside
EV_LAYOUT"* assertion at `torirs_plugin_host.c:856`. These are contract
violations by the plugin author, and the project convention is that a contract
violation stops at the frame that caused it.

**F4. Re-entrancy.** `chrome_claim` called from inside `EV_CHROME` is refused
(not asserted): the claim set is being iterated. A plugin legitimately learns
mid-pass that it wants a part; it takes the claim from `EV_LAYOUT_CHANGED` or
its tick, and gets it on the next pass.

## Host data structures

```c
#define TORIRS_PLUGIN_CHROME_CLAIMS_MAX 64

struct PluginChromeClaim
{
    int plugin;          /* -1 for a free row */
    uint8_t slot;
    int32_t member;
    /** Set by plugin_image_publish when a borrowed image lands; drained by
     *  the frame loop as a targeted EV_CHROME. @see C4. */
    uint8_t needs_declare;
};

struct PluginChromeBorrow
{
    int borrower;        /* -1 for a free row */
    int lender_image;    /* index into host->images */
    uint32_t generation; /* the lender slot's generation at borrow time */
};
```

Plus, on the existing `struct PluginImage`, one `uint32_t generation` bumped by
`plugin_image_drop`.

Part storage goes where the rest of the declaration lives:
`struct UITreeFrameSlotRect` (`src/ui/uitree_frame.h:134`) already carries
`skin` and `overlay`; it gains `struct UITreeFrameChromePart at_art[UITREE_FRAME_SLOT_NODES_MAX]`.
The header there already anticipates this — *"A role with several independently
placed members needs a future member-specific declaration rather than
duplicating one image over all of them."*

## Implementation phases

Each phase is independently verifiable; nothing before phase 4 changes what is
on screen.

1. **Descriptor and storage.** `ChromePart`/`ChromeState`/`ChromeSource` in the
   public header; `at_art[]` on `UITreeFrameSlotRect`; `generation` on
   `PluginImage`. ABI to 18.
2. **`layout_slot_art`** and host-side painting of declared parts, in the frame
   surface at the member's box. Port `gameframe.c`'s four chat buttons to it and
   delete the blit loop. Screen output must be pixel-identical — this is the
   A/B checkpoint for the whole plan.
3. **Borrows.** Borrow table, `generation` staleness, `chrome_part` reading
   FRAME and LANE sources. Still no claims: read-only, and testable on its own.
4. **Claims and `EV_CHROME`.** `chrome_claim` / `chrome_claimed` /
   `chrome_paint` / `chrome_ops`, the event, the pass order, suppression of the
   arranger's part, teardown, and the `role_replace` routing for LANE parts.
5. **`EV_DRAW_FRAME` for claimants**, clipped per D1, ordered per F2.
6. **Lua binding.** `api.layout.<region>.part(member)` / `.claim(member)` /
   `.paint(member, {...})` / `.ops(...)` on the existing region sub-tables in
   `torirs_plugin_lua.c:2085`, taking a member argument exactly as `rect(member)`
   does. Bind `image_compose` and `image_pixels` in the same pass — a Lua dresser
   that can borrow a plate but cannot composite an icon onto it has half an API.
7. **Port `screenshot.lua`** off its `PLATE_RAMP` primitives and onto the
   borrowed plate, on all three lanes. That plugin is the reason this exists and
   is the acceptance test.

## Verification

Adding an engine callback means updating the fake engines in
`src/plugin/test/{gameframe,xp_orbs,torirs_plugin_host,nxt_activities}_test.c`
— `PluginHost_Init` asserts each one is present.

- `make -C src test-gameframe` for the declaration itself.
- Headless, per lane:

```
SDL_VIDEODRIVER=dummy TORIRS_MAX_FRAMES=1200 \
TORIRS_PLUGIN_PREFS=<scratch>/plugin_prefs.ini \
TORIRS_SIM_CLICK_AT="1100,567,19" TORIRS_EXIT_BMP=/tmp/x.bmp \
./src/torirs --manifest manifests/manifest_osrs239.ini
```

  This needs the `EMBED_SERVER=1` binary: the dat1 manifests have no embedded
  server, so a headless run never logs in and any plugin gated on
  `local_player()` draws nothing. `TORIRS_FRAME_DEBUG=1` prints the
  item/hidden/role counts.

- Phase 2 A/B: same manifest, same seed, `TORIRS_EXIT_BMP` before and after,
  byte-compare. A non-identical frame there means the declarative path is not
  reproducing the blit, and every later phase is built on it.

New tests worth writing, one per class above: two claimants on one part (A1),
arranger torn down under a live borrow (B2/B4), a claim on a member the lane
lacks (C2), a pending borrow that lands with no layout pass in flight (C4), and
a click arriving one frame after a release (E3).

## Deliberately not in scope

- **Rasterising a LANE part's native sprite into a borrowable image.** It would
  make all three lanes uniform, and it is feasible — the sprite is already
  decoded in the tree. It is left out because it needs a sprite-to-ARGB path
  host-side and the answer goes stale whenever a CS2 script recolours the
  widget. `SOURCE_LANE` therefore reports a real box with `art[]` of -1, and a
  dresser on that lane ships its own plate. Revisit if the lane case turns out
  to matter more than that.
- **Parts on roles other than `chat_buttons` and `sidebar`.** The mechanism is
  general — it is keyed on `(slot, member)` for any slot — but nothing else has
  a member today, so nothing else is ported.
- **A dresser reordering or moving a part.** `chrome_paint` states a box, so it
  can move the art; it cannot move the *role's node*, because where a region
  goes is the arranger's sentence and two plugins arguing about it is the state
  tier 1 exists to prevent.
