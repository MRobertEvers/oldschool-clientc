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
line when no conversation exists.

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

So in 2009scape, Interact on a familiar that is not kyatt/graahk/lava titan
finds no plugin, `open` returns `false`, and the click produces nothing visible.

Clockwork cat — the one pet in this lane — carries `op1=Pick-up`, not Interact,
and is handled by `summoning_pet_clockwork_cat.rs2`. That plugin therefore has
no port here.

## The port

`~summoning_familiar_interact`, bound from all 78 `[opnpc1,…]` triggers in
`summoning_bindings.rs2`:

1. account gate;
2. the clicked npc must be the player's own familiar, else
   "This is not your familiar.";
3. dispatch on the persisted familiar type — 38 kyatt, 40 graahk, 64 lava titan,
   everything else the no-conversation path;
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

### Two deliberate deviations

1. **The no-conversation familiars answer.** The source is silent for those 75;
   a silent op1 is indistinguishable from the unbound one this change is fixing.
   The port prints the source's own generic no-conversation line, the one all
   three implemented familiars use verbatim: "The \<name\> does not feel like
   talking now." No conversation is invented for a familiar the source does not
   write one for.
2. **No wilderness gate on the teleports.**
   `WildernessZone.checkTeleport(player, 20)` guards all three, refusing above
   wilderness level 20. This world has no wilderness zone and no skull manager.
   The guard is omitted rather than approximated; when a wilderness zone lands it
   belongs in `~summoning_familiar_interact_teleport` and nowhere else.

Two smaller notes:

* The graahk's chathead expressions (`GHRAAK_SHAKE_VIGOROUS`, `GHRAAK_SHAKE_MILD`,
  `GRAAHK_NOD`) are rev-530 head sequences this cache has no counterpart for.
  Those lines use the neutral head and the source expression survives as a
  comment. The player-side expressions map one-to-one.
* Source lines split across arguments are joined with a space, per
  `interface_chat/scripts/chat.rs2`'s rule for this era's single wrapping body
  component.

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
