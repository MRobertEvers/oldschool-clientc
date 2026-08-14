# Script name collisions — closed

**Status: the queue is empty and `sscompile` now rejects a duplicate.** This
file is kept as the explanation of what the rule is and why, because the
failure it describes is invisible at run time and the fix is not obvious from
the error alone.

## The rule

A `[trigger,subject]` may be declared **once** in the whole tree.

Two declarations do not compose and there is no precedence rule. Both take a
script id, but `finish_script` resolves every body back to the **first**
matching name, so whichever file the compiler reaches **last** silently
replaces the other's body — and the loser's id is left permanently empty.

Nothing reported this. The pack loaded, the ids resolved, and the npc or loc
just ran a script that was not its own. It is how these became dead content:

| Quest | What was dead | Beaten by |
|---|---|---|
| Sheep Shearer | Fred the Farmer's entire dialogue | `quest_coldwar` |
| Rune Mysteries | Sedridor's entire dialogue | `quest_templeoftheeye` |
| A Tail of Two Cats | Unferth's quest start; the fireplace chore | its own later stanzas |
| Romeo & Juliet, One Small Favour | the Varrock Apothecary | `quest_atailoftwocats` |
| Giant Mole | mud never extinguished a light | a `return;` stub in `giantmole_ai.rs2` |
| The Great Brain Robbery | every bookcase in the game searched for its book | vs `general_use/scripts/bookcases.rs2` |
| Fletching, leather craft, cooking | knife / needle / leather / cooked_meat uses | assorted quest `[opheldu]`s |

67 groups in all, spanning `opnpc1`, `oploc1`, `oplocu`, `opheldu`, `opnpcu`,
`ai_queue3` and `proc`.

## Sharing an npc or loc between files

One trigger, in the file that owns the subject — normally `areas/`, or the
base quest for a quest's own npc. Every other file keeps its body in a
`[label,...]` of its own, and the canonical trigger branches into it on that
quest's varp:

```
// areas/lumbridge/scripts/fred_the_farmer.rs2 — the ONE trigger
[opnpc1,fred_the_farmer]
if (%peng_quest = ^coldwar_fred) {
    @coldwar_fred_talk;          // body lives in quest_coldwar
    return;
}
...Sheep Shearer's own dialogue follows
```

Put the branch for the *narrower* condition first, and never give a shared
trigger a catch-all `else` that answers for states it does not own — a
fallback like `~chatnpc("Afternoon.")` is what swallowed Fred's quest.

For an `[opheldu]` shared by two skills, branch on `last_useitem` (the item
dragged onto this one) rather than declaring the subject twice; where the
canonical trigger's fallback is itself an action — `~attempt_brew_potion`, say
— list the other file's targets explicitly instead of falling through.

## Stacked headers are not duplicates

```
[oploc1,hunting_sapling_full_green]
[oploc1,hunting_sapling_full_orange]
[oploc1,hunting_sapling_full_red]
~hunter_net_take;
```

Distinct names sharing one body. That is supported, and is the right way to
give every variant of a thing one handler. (It was silently broken too — only
the last name got the code, 1,287 times — see `alias_script` in
`src/serverscript/ssc_compile.c`.)

## Not covered by this rule

`SSVM_Provider` still reports `duplicate_keys=2`: two *different* script names
whose subjects resolve to the same cache id, so they collide in the trigger
index rather than the name table.

    [oploc1,dragonshipladdertop]  vs  [oploc1,myq3_hideout_trapdoor]
    [oploc1,boardgames_runesquares_door_experienced]  vs  [oploc1,grim_pendant]

That is a symbol-allocation problem, not a script one, and is untouched here.
