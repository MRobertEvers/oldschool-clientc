#!/usr/bin/env python3
"""Patch the seven prayer-tab clientscripts to know about a third book.

The osrs239 client's prayer tab is already generic over the book: every script
below branches on `%varbit14826` (`prayerbook`, bits 0..4 of `prayer_general2`,
so values 0..31 fit and no new selector var is needed). Ruinous Powers is book 1.
Ancient Curses becomes book **2**.

Each patch adds one arm and changes nothing else. The originals are copied from
the base tree verbatim and the arm is inserted by anchored replacement, so a
drift in the base script fails loudly here rather than silently shipping a stale
copy.

Note the asymmetry that makes this cheap: `prayer_updatebutton` (463) reads a
curse's level, name, tooltip and both icons out of *obj params*, and
`prayer_op`/`prayer_isavailable` are already book-agnostic. Only the
book-selection plumbing needs to learn the new value.
"""

from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TREE = ROOT / "OSRS-Content/osrs239-content"
LANE = TREE / "ported/rs558_ancient_curses"

BOOK = 2
# Numeric, not named: every var and enum reference in the surrounding cache
# scripts is numeric (`%varbit14827`, `enum_4959`), and these ids are minted by
# tools/gen_curses_lane_configs.py, which is the one place they are decided.
# Keep the two files in step.
ENUM_BOOK = 6035               # enum curses_book
VAR_ACTIVE = 20412             # varbit prayer_allactive_curses
VAR_QUICK = 20413              # varbit quickprayer_selected_curses

# (script id, anchor that must appear exactly once, replacement)
PATCHES = [
    # 7823 — which enum backs the book. Curses arm goes first so it short-circuits
    # before the standard book's quest/members ladder is evaluated.
    (7823,
     "[proc,script7823]()(int)\n",
     f"[proc,script7823]()(int)\n"
     f"if (%varbit14826 = {BOOK}) {{\n\treturn(enum_{ENUM_BOOK});\n}}\n"),

    # 6723 / 7054 — read the active and quick masks.
    (6723,
     "if (%varbit14826 = 1) {\n\treturn(%varbit14827);\n}\n",
     "if (%varbit14826 = 1) {\n\treturn(%varbit14827);\n}\n"
     f"if (%varbit14826 = {BOOK}) {{\n\treturn(%varbit{VAR_ACTIVE});\n}}\n"),
    (7054,
     "if (%varbit14826 = 1) {\n\treturn(%varbit14828);\n}\n",
     "if (%varbit14826 = 1) {\n\treturn(%varbit14828);\n}\n"
     f"if (%varbit14826 = {BOOK}) {{\n\treturn(%varbit{VAR_QUICK});\n}}\n"),

    # 7053 / 7055 — write them back. These are the optimistic client-side writes;
    # the server's corrective varp still has the last word.
    (7053,
     "if (%varbit14826 = 1) {\n\t%varbit14827 = $int0;\n}\n",
     "if (%varbit14826 = 1) {\n\t%varbit14827 = $int0;\n}\n"
     f"if (%varbit14826 = {BOOK}) {{\n\t%varbit{VAR_ACTIVE} = $int0;\n}}\n"),
    (7055,
     "if (%varbit14826 = 1) {\n\t%varbit14828 = $int0;\n}\n",
     "if (%varbit14826 = 1) {\n\t%varbit14828 = $int0;\n}\n"
     f"if (%varbit14826 = {BOOK}) {{\n\t%varbit{VAR_QUICK} = $int0;\n}}\n"),

    # 547 — the 34x34 backing plate behind each icon. 4893 is the ancient/Zarosian
    # plate; curses are an ancient book, so they take it too. Written as
    # "not the standard book" rather than "= 1" so a fourth book inherits it.
    (547,
     "\t\tif (%varbit14826 = 1) {\n\t\t\tcc_setgraphic(\"graphic_4893\");\n"
     "\t\t} else {\n\t\t\tcc_setgraphic(\"graphic_4892\");\n\t\t}\n",
     "\t\tif (%varbit14826 = 0) {\n\t\t\tcc_setgraphic(\"graphic_4892\");\n"
     "\t\t} else {\n\t\t\tcc_setgraphic(\"graphic_4893\");\n\t\t}\n"),

    # 463 — the "Protect Item is inert here" overlay on high-risk and deadman
    # worlds. Protect Item is index 8 in the standard book and 23 in Ruinous;
    # in the curses book it is bit 0.
    #
    # This was withheld for a while on the belief that the base script could not
    # round-trip, because compiling it failed with
    #
    #   FAIL script_463.cs2: line 49: cannot determine the type of a callback
    #   argument; a hook's descriptor needs one letter per argument
    #
    # That was an artefact of the *invocation*, not the script. The hook passes
    # `~prayer_gettooltiptext($obj1)`, and typing a `~proc` argument means
    # loading the callee — which needs a script store. `--raw <cachedir>` does
    # not provide one; `--cache <cachedir>` does. With `--cache` the untouched
    # base script compiles, so the arm below is safe to ship. See the
    # `test-curses-cs2` recipe, which uses `--cache` for exactly this reason.
    (463,
     "if ((%varbit14826 = 0 & $index5 = 8 | %varbit14826 = 1 & $index5 = 23) & "
     "(~high_risk_world = 1 | ~deadman_world ! 0 | %varbit5314 = 1)) {\n",
     "if ((%varbit14826 = 0 & $index5 = 8 | %varbit14826 = 1 & $index5 = 23 | "
     f"%varbit14826 = {BOOK} & $index5 = 0) & "
     "(~high_risk_world = 1 | ~deadman_world ! 0 | %varbit5314 = 1)) {\n"),
]


def main() -> int:
    scripts = LANE / "scripts"
    scripts.mkdir(parents=True, exist_ok=True)

    pack = []
    for sid, anchor, replacement in PATCHES:
        src = TREE / f"scripts/script_{sid}.cs2"
        text = src.read_text()
        hits = text.count(anchor)
        if hits != 1:
            raise SystemExit(
                f"script_{sid}: anchor matched {hits} times, expected 1 — the base "
                f"script has drifted and this patch must be re-read, not forced")
        out = text.replace(anchor, replacement)
        (scripts / f"script_{sid}.cs2").write_text(out)
        pack.append(f"{sid}=script_{sid}")
        print(f"patched script_{sid}.cs2")

    (LANE / "pack/12_clientscripts.pack").write_text("\n".join(sorted(pack, key=lambda s: int(s.split('=')[0]))) + "\n")
    print(f"wrote {LANE.relative_to(ROOT)}/pack/12_clientscripts.pack ({len(pack)} scripts)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
