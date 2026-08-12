# Familiar "Interact" (npc op1) — source references and port

Status: fixed 2026-08-12. Implementation lives in
`OSRS-Content/osrs239-content/server/scripts/ported_scape2009_summoning/scripts/summoning_interact.rs2`;
acceptance is `make -C src test-summoning-interact`.

## The defect

Every one of the 78 admitted familiars carries `op1=Interact` in its npc record —
this is the rev-530 record verbatim, and `summoning_roster_530.npc` and the
per-familiar `summoning_cohort_*.npc` files agree on it.

Two of those 78 were bound, and both bindings ran the wrong operation:

```
[opnpc1,summoning_spirit_wolf]                  -> ~summoning_call_familiar
[opnpc1,summoning_cohort_dreadfowl_dreadfowl]   -> ~summoning_call_familiar
```

`~summoning_call_familiar` is *Call Follower*: it re-teleports the familiar to
the owner's feet, replays the arrival spotanim and synth, and prints "You call
your familiar." So "Interact" recalled the familiar, and the other 76 familiars
had a client-visible menu row that reached no script at all.

Call Follower already had — and keeps — its three real homes, all in
`summoning_tab.rs2`: `wornitems:call_follower`, `orbs:summoning_orb_button`, and
`summoning_familiar:call`. Nothing in 2009scape reaches Call through the npc
menu.

## What 2009scape does

Read at `../2009scape`, under
`Server/src/main/content/global/skill/summoning/familiar/` unless noted.

| Where | What |
| --- | --- |
| `FamiliarNPCOptionPlugin.java:21-25` | one global `OptionHandler` for the npc option strings `pick-up`, `interact-with`, `interact`, `store`, `withdraw` |
| `FamiliarNPCOptionPlugin.java:30-38` | node is not a `Familiar` → not handled; `familiar.getOwner() != player` → **"This is not your familiar."** |
| `FamiliarNPCOptionPlugin.java:47-48` | `interact` → `player.getDialogueInterpreter().open(node.getId(), node)` |
| `core/game/dialogue/DialogueInterpreter.java:97-114` | `open` resets the npc's walking queue and clears its pulses, **then** looks the plugin up, and returns `false` when none is registered for that id |
| `SpiritKyattOptionPlugin.java:25` | npc 7365 → `SpiritKyattDialogue` |
| `SpiritGraahkOptionPlugin.kt:19` | npc 7363 → `SpiritGraahkDialogue` (registered on 7364) |
| `LavaTitanOptionPlugin.java:18` | npc 7341 → `LavaTitanDialogue` (registered on 8700) |
| `FamiliarDialoguePlugin.java:136-138` | the dialogue's terminal stage calls `getFamiliar().startFollowing()` |

### The three familiars with a dialogue

All three open the same box — header "Select an Option", rows "Chat" and
"Teleport" — and all three refuse Chat with a "does not feel like talking now"
line when no conversation exists. The port keeps the box and the teleports and
routes Chat into the shared conversation path below, so the refusal line is no
longer what any of them answers with.

| Familiar | Type id here | Chat | Teleport |
| --- | --- | --- | --- |
| Spirit kyatt (`SpiritKyattDialogue.java:42-72`) | 38 | "The Kyatt does not feel like talking now." | (2326, 3636) |
| Spirit graahk (`SpiritGraahkDialogue.kt:31-62`) | 40 | conversation if static Summoning ≥ 67, else "The Graahk does not feel like talking now." | (2786, 3002) |
| Lava titan (`LavaTitanDialogue.java:34-64`) | 64 | "The lava titan does not feel like talking now." | (3048, 3820), row reads "Teleport to Lava Maze" |

`SpiritGraahkDialogueFile` (`SpiritGraahkDialogue.kt:71-113`) is the only real
familiar conversation in the source: four labelled exchanges picked with
`RandomFunction.random(4)`. Its level gate is `getStaticLevel(SUMMONING) >= 67`,
which is the graahk's own summon level 57 plus 10 — the same `+10` the
commented-out pet gate at `FamiliarDialoguePlugin.java:46-70` uses, so 67 is a
rule rather than a one-off constant.

### `FamiliarDialoguePlugin` is pet-only

It looks like the generic familiar small-talk and is not. All 173 ids in its
`getIds()` are rows of `pet/Pets.java` — cats 761-779, hellcats 3503-3505,
clockwork cat 3598, dogs 6958-6969 / 7237-7260, baby dragons 6900-6907, monkeys
7210-7227, vultures 7319-7328, and so on. No combat-familiar id appears in it:
spirit wolf 6829, dreadfowl 6825, and spirit terrorbird 6794 are all absent.
I swept every `getIds()` in the server tree against all 258 familiar npc ids in
the port map to confirm it; the only other dialogue-plugin hits are kyatt 7365,
graahk 7363/7364 and lava titan 8700.

So in 2009scape, Interact on a familiar that is not one of those three finds no
plugin, `open` returns `false`, and the click produces nothing visible.

Clockwork cat — the one pet in this lane — carries `op1=Pick-up`, not Interact,
and is handled by `summoning_pet_clockwork_cat.rs2`. That plugin therefore has
no port here.

## The conversations come from the wiki, not from 2009scape

The reference has one familiar conversation (spirit graahk) and a placeholder
for the rest, which its own author flags:

> `player.sendMessage("The Graahk does not feel like talking now.")` —
> *"This message is likely inauthentic, but I cannot source the correct one so
> I'm keeping the default here -Bishop"*

Two things say the conversations exist in the real game:

* **Every one of the 78 npc records imports a `head1` chathead.** A chathead is
  drawn nowhere but a dialogue box. Jagex would not have shipped 78 of them for
  familiars that never speak.
* **The option box is a choice between Chat and a teleport.** It exists for the
  three familiars that have somewhere to send you. A familiar with no teleport
  has nothing to choose, so Chat is simply what Interact does.

`SUMMONING_SPECIALS.md`'s porting policy allows "a cited historical reference or
another known-good implementation only when the local 2009scape method is
absent" — and it is absent. The RuneScape Wiki's `Category:Familiar dialogue`
transcripts are that reference.

| | |
| --- | --- |
| Corpus | `docs/summoning_port/familiar_dialogue.json` (checked in) |
| Built by | `tools/build_familiar_dialogue_corpus.py` — a **one-off** wiki pull; nothing in the build reaches the network |
| Compiled by | `tools/generate_familiar_dialogue_script.py` → `summoning_dialogue.rs2`, via `make -C src summoning-dialogue` |
| Coverage | 78 familiars, 408 conversations, 1,647 dialogue pages |
| Licence | CC BY-NC-SA 3.0 — attribute the wiki when reusing the transcripts |

The wiki's spirit graahk transcript is the same conversation 2009scape has, a
few words closer to the original ("I got **you** a present"), so all 78 are
generated from the one source rather than two.

### The level gate

Chat is gated on the **static** Summoning level at **the familiar's own level +
10**. Three independent sources agree, which is why it is applied to all 78
rather than hardcoded where it was found:

* `SpiritGraahkDialogue.kt:48` gates a level-57 familiar at 67;
* `FamiliarDialoguePlugin.java:46-70`'s commented-out pet gate reads
  `getSummoningLevel() + 10`;
* `Transcript:Spirit wolf` splits a level-1 familiar at 11.

Below the gate the familiar **still speaks** — the player just cannot make sense
of it. That is the wiki's own reading of the split rather than a substitute for
it: `Transcript:Spirit wolf` is the only page that records both sides, and it
shows the same utterance with the translation withheld — "Whurf?" below level
11, "Whurf? (What are you doing?)" at 11+. Every other familiar has that rule
applied to its own first line. Karamthulhu overlord is the single exception: it
is telepathic, every line it speaks is a parenthetical thought, so it has no
untranslated sound and falls back.

### What the corpus implements vs. holds out

Every conditional conversation the wiki records is checked against a curated
table, `CURATED_GUARDS` in `build_familiar_dialogue_corpus.py`: a
`(type_id, condition_text)` pair mapped to a real ServerScript check, built
only from an obj/category token independently confirmed to exist in THIS
cache (`configs/all.obj`, `pack/category.pack`) — not assumed from the wiki's
English. A condition with no safe token stays in `conditional`, unimplemented.

**Implemented — 18 guarded conversations, checked before the random pool,
matching the wiki's own shape (the special line when the condition holds, the
ordinary pool otherwise):**

| Familiar | Condition | Check |
| --- | --- | --- |
| Spirit wolf | any bone in inventory | sum of the 38 real bones-category objs |
| Thorny snail | wearing a snelm | sum of the 9 snelm colour/style objs, worn |
| Desert wyrm | wielding a pickaxe | `inv_totalcat(worn, weapon_pickaxe)` |
| Spirit tz-kih | a prayer potion dose | sum of the 4 dose objs |
| Vampire bat, Unicorn stallion | missing/not max life points | `stat(hitpoints) < stat_base(hitpoints)` |
| Beaver | logs in inventory | `inv_totalcat(inv, firemaking_logs)` |
| Bull ant | 0% run energy | `runenergy = 0` |
| Ibis | 2+ raw sharks | `inv_total(inv, raw_shark) >= 2` |
| Spirit kyatt | a ball of wool | `inv_total(inv, ball_of_wool)` |
| Spirit cobra | ring of charos(a) worn | `inv_total(worn, ring_of_charos_unlocked)` |
| Barker toad | holding a swamp toad | `inv_total(inv, swamp_toad)` |
| Fruit bat | 5+ papayas | `inv_total(inv, papaya) >= 5` |
| Praying mantis | butterfly net worn or carried | sum, worn + inv |
| Swamp titan | swamp tar or paste | sum of the 3 tar/paste objs |
| Spirit terrorbird ×3 | beast-of-burden load | `~summoning_familiar_bob_items`, pre-existing |

Verified live for two guard kinds (category and multi-obj sum), through a real
OPNPC1 packet and the mounted dialogue box — see `test_summoning_interact.py`
cases C, and the `summoning_dialogue_guard_kit` debugproc that provisions them.

**The `bones`/`inv_totalcat` trap.** The first attempt used
`inv_totalcat(inv, bones)`, matching the working precedent for
`firemaking_logs` and `weapon_pickaxe`. It compiled clean — no diagnostic —
and measured **0** live with 5 `[bones]` in the inventory, while
`inv_total(inv, bones)` measured 5 at the same instant. `bones` is both
`category.pack`'s name for category 6 AND a real obj's own name, and the
symbol resolved to the obj, not the category — the exact collision shape
`category.pack`'s own header warns about for `arrows_dragon` vs.
`dragon_arrows`, except silent here instead of diagnosed. The fix enumerates
the category's 38 real objs and sums them instead of naming the category;
`test_summoning_interact.py` asserts `inv_totalcat(inv, bones)` never
reappears in the generated file, and that the two category-kind guards left
(`firemaking_logs`, `weapon_pickaxe`) don't collide with any obj name.

**Held out — 22 conditions, no safe token found in this cache:**

* No matching item exists under any plausible name: flies (Spirit spider),
  purple sweets (Void spinner), a standard/red chinchompa (Giant chinchompa),
  a plain mirror shield (Sp. cockatrice ×7 — only `slayer_mirror_shield`
  exists, a different reward item), a real keris (Spirit kalphite — only
  `contact_keris*`, a "Contact!" quest prop, exists), a standard cannonball
  (Barker toad's "loaded with a cannonball" — only Dwarf-cannon-quest and
  Barrows/Fever-cannon variants exist), a fire cape (Obsidian golem — only
  `skillcape_max_firecape`, the combined skill cape, exists).
* No category exists for "any raw fish" (Bunyip) or "any weapon" (Ravenous
  locust, Steel titan) — building one would mean minting a new
  `category.pack` entry or OR-ing ~30 weapon categories, both out of scope for
  a dialogue port.
* No area/region system exists to check Karamja (Fruit bat), the Rellekka
  Hunter area (Arctic bear), the Kharidian Desert (Ice titan), or "a dark
  area" (Vampire bat).
* No familiar-carried state is tracked for Compost mound's bucket or Barker
  toad's cannonball load — the special-move system that could track it
  (`summoning_special_targeted.rs2`) drives Compost mound's special into a
  real farming bin, not something the familiar visibly holds.

* **Ordering conditions** ("always first", "after conversation 1") stay in the
  pool with the condition recorded. The source unlocks them in sequence; this
  picks at random.
* **Overhead dialogue** is the familiar's ambient chat, not the Interact result.
  Recorded in the corpus, unimplemented.
* **Fire titan** keeps its "Dialogue options prior to 6 December 2011" set. The
  page's other section is a post-2011 rewrite, and a later revision cannot
  override the period value.

## The port

`~summoning_familiar_interact`, bound from all 78 `[opnpc1,…]` triggers in
`summoning_bindings.rs2`:

1. account gate;
2. the clicked npc must be the player's own familiar, else
   "This is not your familiar.";
3. dispatch on the persisted familiar type — 38 kyatt, 40 graahk and 64 lava
   titan open the option box, everything else goes straight to Chat;
4. `npc_setmode(playerfollow)` on the way out, which is
   `FamiliarDialoguePlugin`'s stage-99 `startFollowing()`. `~chatnpc` and
   `~p_choice_open` hand the npc `playerfaceclose`, a standing mode with no
   mover, so without this the familiar stays where the conversation left it.

Every npc touch after a yield re-finds by uid (`npc_finduid`): the lifetime timer
can vanish the familiar while the option box is open.

### The subject cannot be a wildcard

Trigger dispatch resolves type → category → `_`
(`SSVM_ProviderGetByTrigger`). `[opnpc1,_]` would take op1 away from every npc in
the world, and the familiar records carry no `category=`, so the 78 rows are
written out per record. That matches the 78 `[opheld4,…]` pouch rows already in
the same file.

### The deliberate deviation

**No wilderness gate on the teleports.**
`WildernessZone.checkTeleport(player, 20)` guards all three, refusing above
wilderness level 20. This world has no wilderness zone and no skull manager. The
guard is omitted rather than approximated; when a wilderness zone lands it
belongs in `~summoning_familiar_interact_teleport` and nowhere else.

2009scape's "does not feel like talking now." survives only as the fallback for
a familiar whose every recorded conversation is state-conditional. With the
corpus in place, no familiar on the roster actually reaches it.

Two smaller notes:

* Every familiar keeps the neutral chathead. The rev-530 head sequences the
  source expressions name (`GHRAAK_SHAKE_VIGOROUS`, `GRAAHK_NOD`, ...) have no
  counterpart in this cache.
* **Pagination.** The wiki records a familiar's whole speech as one bullet; this
  era's dialogue body is a single 479x67 component that wraps and then CLIPS at
  about four lines, and `mock230_send_if_settext` builds its packet in a
  512-byte buffer. Pack yak's longest line is 675 characters. Long lines are
  split at sentence boundaries into successive dialogue pages — which is what
  the player clicks through in the real game anyway.
* **Character folding.** Curly quotes and ellipses fold to ASCII, and angle
  brackets to parentheses, because `<` opens a colour tag in this client's text
  renderer.

### Supporting changes

* `~summoning_call_familiar` became a wrapper over
  `~summoning_call_familiar_ex(boolean $message)`. Nothing about Call Follower
  changed; the teleport branch needs the same relocation without the
  "You call your familiar." line. That relocation is
  `Familiar.update` (`Familiar.java:271-274`), which re-calls a familiar left
  more than 12 tiles behind its owner — this lane's tick has no distance check,
  so the recall is issued at the teleport instead of waiting for one.
* `App_SimulateNpcOp` / `TORIRS_SIM_OPNPC="frame,op,npc"` (`src/app.c`,
  `src/main.c`) — the npc counterpart of the existing `TORIRS_SIM_OPLOC` hook.
  It sends a real OPNPC through `net_out_opnpc`, addressed by cache npc type
  because neither the npc's pixels nor its server slot are stable enough for a
  test to name.
* `[debugproc,summoning_summon](int $type)` — provisioning for acceptance runs
  that need a familiar with no pouch debugproc. The level and point gates are the
  ordinary ones.

## Known-unbound and out of scope

* `[opnpc2,summoning_spirit_wolf]` and `[opnpc2,…dreadfowl]` run
  `~summoning_dismiss_familiar`. No familiar record declares `op2`, in this lane
  or in rev 530, so the client cannot send it and neither binding is reachable.
  2009scape has no npc dismiss option either. They are left alone: removing them
  is a separate decision from fixing op1.
* `interact-with` (`FamiliarNPCOptionPlugin.java:44-45`, → `KittenInteractDialogue`)
  and `pick-up` are pet operations. 24 records in the client lane carry
  `op4=Interact-with`; none of them is a row of `~summoning_familiar_npc`.
* Selecting a row inside the option box is not covered end-to-end. `~p_choice_open`
  builds its rows with `cc_create`, so there is no component id to address and the
  pixel moves with the chatbox. The branches behind the box are checked against
  the source statically.
