#!/usr/bin/env python3
"""
gen_stronghold_questions -- the Stronghold of Security's gate questions, from
the pinned OSRS Wiki page corrected by RSProx, as RuneScript.

    tools/gen_stronghold_questions.py           # check only: parse + validate
    tools/gen_stronghold_questions.py --write   # rewrite the generated .rs2

Source: docs/areas/stronghold/sources/Stronghold_of_Security.wiki, section
"Doors" -- a wikitable of Question / Answer / Response, one row per answer,
with `rowspan` on the question (it spans its answers) and on a response two
answers share. Refresh the page with docs/areas/stronghold/sources/fetch.sh.

The output is every question, its options in the wiki's order, and the
response the door gives to EACH option. Where RSProx index 4346 shows a string
differently, docs/areas/stronghold/sources/rsprox_4346_overrides.tsv gives the
recorded text and it wins (the wiki's "huts", "varified", "WoW!" are its own;
the game's "authentiation" and "thell" are not recorded either way). An answer is correct when its
response starts "Correct!" -- or "Well done!", the one way the bank-PIN
question says it -- and the wiki's "Quite good." / "Okay!" / "Incorrect!" /
"Not a good idea." answers do not open the gate. Every question must have
exactly one; the parse refuses a page where that is not so.
"""

from __future__ import annotations

import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WIKI = os.path.join(ROOT, "docs/areas/stronghold/sources/Stronghold_of_Security.wiki")
RECORDED = os.path.join(ROOT, "docs/areas/stronghold/sources/rsprox_4346_overrides.tsv")
OUT = os.path.join(ROOT, "OSRS-Content/osrs-content/server/scripts/areas/area_stronghold/"
                   "scripts/stronghold_questions.generated.rs2")

CORRECT = ("Correct!", "Well done!")

ROWSPAN = re.compile(r'^\s*rowspan="(\d+)"\s*\|(.*)$')


def cell(text):
    """(rowspan, text) of one `|...` cell."""
    m = ROWSPAN.match(text)
    if m:
        return int(m.group(1)), m.group(2).strip()
    return 1, text.strip()


def parse(path):
    """[(question, [(answer, response), ...]), ...] in the page's order."""
    lines = open(path, encoding="utf-8").read().split("\n")
    start = next(i for i, l in enumerate(lines) if l.strip() == "==Doors==")
    table = next(i for i in range(start, len(lines)) if lines[i].startswith("{|"))
    end = next(i for i in range(table, len(lines)) if lines[i].startswith("|}"))
    rows = []
    row = []
    for line in lines[table + 1:end]:
        if line.startswith("|-"):
            if row:
                rows.append(row)
            row = []
        elif line.startswith("|+") or line.startswith("!"):
            continue
        elif line.startswith("|"):
            row.append(line[1:])
    if row:
        rows.append(row)

    questions = []
    q_left = 0          # answers the open question still spans
    r_left = 0          # answers the open response still spans
    response = None
    for row in rows:
        cells = list(row)
        if q_left == 0:
            span, text = cell(cells.pop(0))
            assert text.startswith("To pass you must answer me this:"), text
            questions.append((text, []))
            q_left = span
        assert cells, "a row with no answer under %r" % questions[-1][0]
        answer = cell(cells.pop(0))[1]
        if r_left == 0:
            assert cells, "no response for %r" % answer
            span, response = cell(cells.pop(0))
            r_left = span
        assert not cells, "stray cells %r" % cells
        questions[-1][1].append((answer, response))
        q_left -= 1
        r_left -= 1
    assert q_left == 0 and r_left == 0, "the table ends inside a rowspan"
    return questions


def load_overrides(path):
    """{wiki text: recorded text} from the RSProx override table."""
    out = {}
    for line in open(path, encoding="utf-8"):
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        wiki, recorded, _tick = line.split("\t")
        out[wiki] = recorded
    return out


def apply_overrides(questions, overrides):
    """Every wiki string the recording shows differently, replaced; every row used."""
    used = set()

    def fix(text):
        if text in overrides:
            used.add(text)
            return overrides[text]
        return text

    out = [(fix(q), [(fix(a), fix(r)) for a, r in answers]) for q, answers in questions]
    unused = set(overrides) - used
    assert not unused, "override rows not on the wiki page: %r" % sorted(unused)
    return out


def validate(questions):
    for q, answers in questions:
        assert 2 <= len(answers) <= 3, "%r has %d answers" % (q, len(answers))
        right = [a for a, r in answers if r.startswith(CORRECT)]
        assert len(right) == 1, "%r has %d correct answers" % (q, len(right))
        for text in [q] + [x for pair in answers for x in pair]:
            assert "[[" not in text and "''" not in text and "{{" not in text, \
                "wiki markup in %r" % text
            assert "<" not in text and "\\" not in text, "a tag or escape in %r" % text


def rs2_string(text):
    return '"' + text.replace('"', '\\"') + '"'


def emit(questions):
    out = []
    w = out.append
    w("// GENERATED by tools/gen_stronghold_questions.py --write; do not hand-edit.")
    w("//")
    w("// The Stronghold of Security's gate questions, from the OSRS Wiki's")
    w("// Stronghold_of_Security page, section \"Doors\" (pinned under")
    w("// docs/areas/stronghold/sources/), with every string RSProx 4346 shows")
    w("// differently replaced by the recorded one (rsprox_4346_overrides.tsv): every")
    w("// question, its options in the recorded order, and the door's own response")
    w("// to each option. The correct option is the one whose response begins")
    w("// \"Correct!\" (\"Well done!\" for the bank PIN question).")
    w("// %d questions." % len(questions))
    w("")
    w("[proc,sos_question_text](int $question)(string)")
    w("switch_int ($question) {")
    for i, (q, _) in enumerate(questions):
        w("    case %d : return(%s);" % (i, rs2_string(q)))
    w("}")
    w('return("");')
    w("")
    w("// The option the player picked, 1-based, or 0 when the player picked none")
    w("// (the menu was closed).")
    w("[proc,sos_question_ask](int $question)(int)")
    w("switch_int ($question) {")
    for i, (_, answers) in enumerate(questions):
        args = ", ".join("%s, %d" % (rs2_string(a), n + 1) for n, (a, _) in enumerate(answers))
        w("    case %d : return(~p_choice%d(%s));" % (i, len(answers), args))
    w("}")
    w("return(0);")
    w("")
    w("[proc,sos_question_response](int $question, int $answer)(string)")
    w("switch_int ($question) {")
    for i, (_, answers) in enumerate(questions):
        w("    case %d :" % i)
        for n, (_, r) in enumerate(answers):
            w("        if ($answer = %d) return(%s);" % (n + 1, rs2_string(r)))
    w("}")
    w('return("");')
    w("")
    w("[proc,sos_answer_correct](int $question, int $answer)(boolean)")
    w("switch_int ($question) {")
    for i, (_, answers) in enumerate(questions):
        right = [n + 1 for n, (_, r) in enumerate(answers) if r.startswith(CORRECT)][0]
        w("    case %d : if ($answer = %d) return(true);" % (i, right))
    w("}")
    w("return(false);")
    w("")
    w("// How many options question $question offers.")
    w("[proc,sos_question_options](int $question)(int)")
    w("switch_int ($question) {")
    for i, (_, answers) in enumerate(questions):
        w("    case %d : return(%d);" % (i, len(answers)))
    w("}")
    w("return(0);")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--write", action="store_true", help="rewrite the generated .rs2")
    args = ap.parse_args()
    questions = apply_overrides(parse(WIKI), load_overrides(RECORDED))
    validate(questions)
    text = emit(questions)
    if args.write:
        with open(OUT, "w", encoding="utf-8") as fh:
            fh.write(text)
        print("wrote %s: %d questions" % (os.path.relpath(OUT, ROOT), len(questions)))
        return 0
    same = os.path.exists(OUT) and open(OUT, encoding="utf-8").read() == text
    print("%d questions; %s" % (len(questions), "generated file current" if same else
                                 "generated file STALE (run --write)"))
    return 0 if same else 1


if __name__ == "__main__":
    sys.exit(main())
