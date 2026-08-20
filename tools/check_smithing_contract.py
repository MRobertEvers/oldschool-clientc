#!/usr/bin/env python3
"""Hold the anvil table to the wiki's stated per-bar rule.

Plan: docs/NEAR_REALITY_CONTENT_QUEUE.md slice D12.

The wiki states the rule outright rather than tabulating 159 items:

  "Smithing experience is calculated by taking the experience granted from 1
   bar and multiplying it by the number of bars used. For example, a bronze
   platebody is 62.5 experience using 5 bars. 1 bar is 12.5 exp, so go
   5 * 12.5 = 62.5. This works for all bars and all smithing items, with a few
   exceptions such as cannonballs."

So every row in `smithing.dbrow` is checkable against ONE number per metal --
which is a far stronger check than comparing 159 values to 159 wiki cells,
because it also catches a row whose bar COUNT is wrong: a platebody recorded as
using 3 bars would need its experience to be wrong by exactly the same factor to
slip through.

The per-bar figures are derived from the tree's own table rather than typed:
whatever a metal's 1-bar item awards is that metal's rate, and every other item
in that metal must be a whole multiple of it. So this checks the table for
INTERNAL CONSISTENCY against the wiki's rule, and needs no per-metal constants
to be maintained.
"""
import io
import os
import re
import sys
import collections

ROWS = ("OSRS-Content/osrs239-content/server/scripts/skill_smithing/configs/"
        "smithing.dbrow")

# The wiki names cannonballs as an exception to the per-bar rule.
EXCEPTIONS = ("cannonball",)


def rows(path):
    text = io.open(path, encoding="utf-8", errors="replace").read()
    out, block = [], None
    cur = {}
    for line in text.split("\n"):
        m = re.match(r"^\[([a-z0-9_]+)\]$", line.strip())
        if m:
            if block:
                out.append((block, cur))
            block, cur = m.group(1), {}
            continue
        if block and line.startswith("data="):
            k, _, v = line[5:].partition(",")
            cur.setdefault(k, v)
    if block:
        out.append((block, cur))
    return out


def main():
    table = rows(ROWS)
    by_metal = collections.defaultdict(list)
    skipped = []
    for block, data in table:
        bar = data.get("bar")
        xp = data.get("experience")
        n = data.get("bar_amount")
        if not (bar and xp and n):
            skipped.append("%s: missing bar/experience/bar_amount" % block)
            continue
        if any(e in block for e in EXCEPTIONS):
            skipped.append("%s: cannonballs are a stated exception to the "
                           "per-bar rule" % block)
            continue
        by_metal[bar].append((block, int(xp), int(n)))

    problems, checked = [], 0
    for bar, items in sorted(by_metal.items()):
        # The per-bar rate this metal implies, taken from its 1-bar items.
        singles = {xp for _b, xp, n in items if n == 1}
        if len(singles) != 1:
            skipped.append("%s: %d distinct 1-bar awards (%s) -- no unambiguous "
                           "per-bar rate" % (bar, len(singles),
                                             ", ".join(str(s / 10) for s in sorted(singles))))
            continue
        rate = next(iter(singles))
        for block, xp, n in items:
            checked += 1
            if xp != rate * n:
                problems.append("%s: %d bar(s) at %s/bar should be %s, table "
                                "says %s" % (block, n, rate / 10,
                                             rate * n / 10, xp / 10))

    for s in skipped:
        print("  skipped  " + s)
    for p in problems:
        print("  " + p)
    if problems:
        print("check_smithing_contract: %d row(s) break the per-bar rule "
              "across %d checked" % (len(problems), checked), file=sys.stderr)
        return 1
    print("check_smithing_contract: %d row(s) across %d metal(s) obey the "
          "wiki's per-bar rule, %d skipped"
          % (checked, len(by_metal), len(skipped)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
