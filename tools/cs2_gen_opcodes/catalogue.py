"""The rev-239 client's own command declarations -- the authority for CS2 names and signatures.

vendor/commands (provenance in vendor/commands/VERSION) is the client's own
declaration of every clientscript command:

    100 [command,cc_create](component $component, iftype $type, int $subid)
    100 [command,cc_create](component $component, iftype $type, int $subid, boolean $assert_empty) 230

A row may end in a REVISION number: that declaration applies from that revision
on, and an unnumbered row is the original. Rows are not in revision order within
a file. The highest revision present is 231, so at rev 239 the answer for an id
is always its highest-numbered row.

Three generators read this module, and they read it rather than each keeping
their own copy of the answer, because keeping their own copy is how they came to
disagree:

    tools/cs2_gen_opcodes/gen_opcodes.py     names    -> src/cs2vm2/cs2_opcode.h, cs2_opcode_meta.c
    src/cs2vm2/gen_opcode_stack.py           stacks   -> src/cs2vm2/cs2vm2_opcode_stack.gen.h
    3rd/rscache/tools/cs2/gen_cs2_tables.py  both     -> 3rd/rscache/src/cs2/cs2_command.gen.h (decoder/compiler)

Before this, the VM's names came from a 2021 RuneStar table plus hand overlays.
200 of them disagreed with the client, and not only in spelling: the whole
cc_input block at 1133..1146 was shifted by one or more slots, so a script
setting an input box's focus set its cursor colour instead.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path

CATALOGUE_DIR = Path(__file__).resolve().parent / "vendor" / "commands"

# The revision this client implements. Every catalogue row is at or below it.
CLIENT_REVISION = 239

# Catalogue types whose values travel on the STRING stack at this revision.
# Array handles are pool pointers carried as strings (see ARRAY_LENGTH, 8003).
STRING_TYPES = frozenset({"string", "unknownarray", "intarray", "stringarray"})

# Types that make a command's stack effect depend on a runtime value (a typed
# `unknown` selected by a basevartype, a hook's argument_list) or that live on
# the long stack, which the VM's stack table has no column for. A command naming
# any of them has no fixed signature to record.
UNFIXED_TYPES = frozenset({"unknown", "argument_list", "clientscript", "long", "unknown_long"})

_ROW = re.compile(
    r"^(\d+) \[command,([a-z0-9_]+)\]"
    r"(?:\(([^)]*)\)(?:\(([^)]*)\))?)?"
    r"(?: (\d+))?$"
)


@dataclass(frozen=True)
class Command:
    opcode: int
    name: str  # lowercase, as the catalogue spells it
    revision: int  # the revision its governing row applies from; 0 = original
    args: tuple[str, ...] | None  # None: declared by name only (core ops)
    returns: tuple[str, ...] | None

    @property
    def signature(self) -> tuple[int, int, int, int] | None:
        """(int_in, str_in, int_out, str_out), or None when not fixed."""
        if self.args is None:
            return None
        if any(t in UNFIXED_TYPES for t in self.args + self.returns):
            return None
        si = sum(1 for t in self.args if t in STRING_TYPES)
        so = sum(1 for t in self.returns if t in STRING_TYPES)
        return (len(self.args) - si, si, len(self.returns) - so, so)


def _rows() -> dict[int, Command]:
    best: dict[int, Command] = {}
    for path in sorted(CATALOGUE_DIR.glob("*.txt")):
        for line in path.read_text(encoding="utf-8").splitlines():
            m = _ROW.match(line.strip())
            if not m:
                continue  # a `(gap)` row
            opcode, revision = int(m.group(1)), int(m.group(5) or 0)
            if revision > CLIENT_REVISION:
                raise ValueError(f"catalogue row {line!r} is newer than rev {CLIENT_REVISION}")
            if opcode in best and best[opcode].revision > revision:
                continue
            if m.group(3) is None:
                args = returns = None
            else:
                args = tuple(a.strip().split(" ")[0] for a in m.group(3).split(",") if a.strip())
                returns = tuple(r.strip() for r in (m.group(4) or "").split(",") if r.strip())
            best[opcode] = Command(opcode, m.group(2), revision, args, returns)
    return best


def commands() -> dict[int, Command]:
    """opcode -> its governing declaration at CLIENT_REVISION, with names made unique.

    A name can be claimed by two ids when it MOVED at some revision: at 228
    `db_find` went from 7508 to 7500, and at 195 `chat_gethistory_byuid` went
    from 5004 to 5031. The claim from the higher revision owns the name; the id
    it moved away from keeps its old declaration under `<name>_pre<revision>`,
    so a generated `#define` never collides and the spelling still says what
    happened. (7508..7510 are absent from the rev-239 dispatch outright; 5003
    and 5004 are still dispatched.)
    """
    rows = _rows()
    claimants: dict[str, list[Command]] = {}
    for command in rows.values():
        claimants.setdefault(command.name, []).append(command)
    out: dict[int, Command] = {}
    for name, group in claimants.items():
        owner = max(group, key=lambda c: (c.revision, c.opcode))
        for command in group:
            if command is owner:
                out[command.opcode] = command
            else:
                renamed = f"{name}_pre{owner.revision}"
                out[command.opcode] = Command(
                    command.opcode, renamed, command.revision, command.args, command.returns
                )
    return out


def names() -> dict[int, str]:
    """opcode -> canonical UPPERCASE name."""
    return {op: c.name.upper() for op, c in commands().items()}


# Where the rev-239 CLIENT and its command catalogue disagree, the client wins,
# because the client is what a script runs against. Each entry must say which
# code proves it; signatures() refuses an entry that no longer disagrees.
SIGNATURE_EXCEPTIONS: dict[int, tuple[tuple[int, int, int, int], str]] = {
    6618: (
        (1, 0, 1, 0),
        "worldmap_getsourcecoord: Statics 6618 pushes ONE packed coord (or -1) on "
        "success; only its no-current-map branch, copied from 6617, pushes two. "
        "The catalogue declares (int, int). The handler reproduces both paths.",
    ),
    3148: (
        (0, 0, 0, 0),
        "settermsandprivacy: Statics.java:46057 pops nothing and returns; native "
        "216 0xc4c (AcceptTermsAndPrivacy) pops nothing too. The catalogue "
        "declares a boolean argument.",
    ),
    3166: (
        (2, 0, 1, 0),
        "shop_isproductavailable: Statics.java:46170 and native 216 0xc5e both "
        "pop two ints. The catalogue declares one.",
    ),
    3331: (
        (0, 0, 1, 0),
        "runenergy: Statics.java:48602 pushes the raw run energy (3321 pushes "
        "it / 100). The catalogue declares no return.",
    ),
}


def signatures() -> dict[int, tuple[int, int, int, int]]:
    """opcode -> fixed stack signature, for every command that has one.

    The declared signature, except where SIGNATURE_EXCEPTIONS records that the
    rev-239 client's own dispatch does something else.
    """
    out = {op: sig for op, c in commands().items() if (sig := c.signature) is not None}
    for op, (sig, _reason) in SIGNATURE_EXCEPTIONS.items():
        if out.get(op) == sig:
            raise ValueError(f"signature exception for {op} matches the catalogue; drop it")
        out[op] = sig
    return out
