# The port gate

Six captures, one comparison. A plugin ported to Porcelain must do exactly what
it did before on every lane, and this is what decides whether it did.

```
zsh tools/porcelain_gate/capture_set.sh <outdir> <binary> [worktree]
python3 tools/porcelain_gate/gate_diff.py <baseline>/<lane>:<outdir>/<lane> ...
```

Pass all six pairs to one `gate_diff.py` invocation. It prints a verdict per
lane and one for the set, and exits non-zero if any lane failed.

## The six lanes, and why these six

| tag | layout mode | frame | what it covers |
|---|---|---|---|
| `native548` | 0 | auto | the lane's own frame, no provider. The control. |
| `classic548` | 0 | classic-fixed | the desktop provider on the default toplevel |
| `classic161` | 1 | classic-fixed | the same provider on a different toplevel |
| `modern164` | 2 | modern-resizable | the resizable provider, where staleness shows |
| `stone601` | 0 | stone-drawer | the touch provider, `TORIRS_CLIENTTYPE=7` |
| `dat1_254` | 0 | auto | the CS1/RevConfig lane, offline |

Five are CS2 and one is CS1, which is the split that matters: a change that
works only because it read a dat2 fact fails `dat1_254` and nothing else.

## What is compared

- **BOUNDS** as a multiset: every component's resolved box, keyed by component.
  Boxes only-before or only-after are a failure.
- **ROLE_WIDGET**: which node answers each role.
- **OWNED_WIDGET** by the key the plugin created it under, not by node index —
  so a control that legitimately moves in the tree is not a failure but a
  control that moves on screen is.
- **PORCELAIN_FINDING** lines the plugin did not declare expected.

Text that is a measurement is compared as measurement: a key beginning
`performance_` is allowed to differ in its string but not in its box, because
those readouts print frame times and a comparator that failed on them would
fail every port for ever. A key nobody declared volatile that changes its text
still fails.

## Declaring a difference the port is supposed to make

Some ports are meant to change what the client shows. The ledger row a port
exists to satisfy sometimes says today's output is the defect: a skill with no
reading used to get an empty zero-by-zero orb, and "no reading, no orb" is the
behaviour the row asks for. Without a way to say that, such a port can never
pass this gate, and its verdict becomes an argument in prose -- which is the
thing the comparator was built to replace.

So a port may declare what it changes:

```
python3 tools/porcelain_gate/gate_diff.py --expect port.expect <pairs...>
```

One declaration per line, `kind target = reason`:

```
owned-drop  orb_hitpoints = a skill with no reading gets no orb; the empty
                            control was the defect the ledger names
only-before BOUNDS com=0x02240015 = the same row, said about the capture line
normalise   scene = the image slot is an internal handle, and handing back the
                    write-once source art renumbers what comes after it
```

`only-before` matches a whole capture line by substring. `owned-drop` names an
owned control that must disappear. `normalise` strips `<name>=<value>` from
every tail before comparing.

A declaration is not a suppression, and three rules keep it from becoming one:

- **Every declaration carries a reason**, and one with an empty reason is
  refused outright rather than ignored.
- **Every declaration is printed with the number of times it fired**, so a
  reader sees what was excused and why.
- **A declaration that never fires fails the gate.** A stale claim about what a
  port changes is as wrong as an undeclared change, and this is the same
  bidirectional rule the layer applies to a declared absence: a declaration has
  to be true in both directions or it is not evidence.

Write the reason for someone deciding whether to believe you. "internal handle"
is a claim a reader can disagree with; "expected" is not.

## The baseline

There is no baseline checked in. It is 22 MB of captures and it is a function
of a commit, so it is regenerated rather than stored:

```
git checkout <the commit the port branched from>
make -C src -j8 OPT=1 EMBED_SERVER=1 PLATFORM_OBJ_BASE=build_gate PLATFORM_TARGET=torirs_gate torirs_gate
zsh tools/porcelain_gate/capture_set.sh /tmp/gate_baseline src/torirs_gate
```

Capture the baseline with **your own** binary, not one somebody else built.
A baseline from a different build of a different tree turns every unrelated
difference into your port's failure.

## Two traps that cost real time

**Absolute paths.** Both scripts resolve every path they are given to an
absolute one before the client runs, because the client runs from the data
checkout and the shell opens the log redirect after the `cd`. This is now
handled; the symptom when it was not was six lanes exiting 1 with no log file.

**Run whole sets.** A single-lane rerun under load is not deterministic. An
isolated `stone601` rerun of an *unchanged* plugin has been observed to drop a
control entirely and shift 58 scene ids. Six lanes run sequentially are stable;
one lane run on a busy machine is not. Judge a set, never a lane.

## Data versus code

The caches and content come from `TORIRS_GATE_REPO` (default: this checkout).
The `revconfig` under test comes from the worktree argument. That split is why
`capture_set.sh` writes its own manifests rather than passing the repo's: the
checked-in manifests use paths relative to the checkout root, which would
resolve against the data tree and silently leave a worktree's revconfig edits
untested.

Set `TORIRS_GATE_SCRIPT_DIR` to test a worktree's Lua plugins the same way.
