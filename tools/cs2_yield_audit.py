#!/usr/bin/env python3
"""The CS2 interpreter's yield rule, checked against its source.

The loop saves nothing for a yield (src/cs2vm2/cs2vm2.h, struct
CS2VM2_YieldMark). An opcode's implementation that can reach the host takes a
mark before it touches anything, and the host is asked only through
CS2VM2_Ask, which takes the mark. A debug build asserts a late mark the moment
the instruction runs and every build aborts on a yield that was not unwound;
this catches the same mistakes without running anything:

  1. `host_exec` is called from exactly one place, CS2VM2_Ask.
  2. A function CS2VM2_RunOp dispatches to that reaches CS2VM2_Ask takes its
     mark as its FIRST statement, before any pop.
  3. A function that reaches CS2VM2_Ask and is not dispatched to (a helper)
     takes the mark as a parameter and never makes its own: by the time a
     helper runs, its caller may have popped.
  4. Inside CS2VM2_RunOp, a case block that asks takes its mark as the
     block's first statement.

    tools/cs2_yield_audit.py          (make -C src check-cs2-yield)
"""
import collections
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, "src", "cs2vm2", "cs2vm2.c")

FUNC_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\(([^;{]*?)\)\s*\n\{\n(.*?)\n\}\n", re.S | re.M)
CALL_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")
COMMENT_RE = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
PREPARE = "struct CS2VM2_YieldMark const mark = CS2VM2_PrepareYield(vm);"
MARK_PARAM = "struct CS2VM2_YieldMark mark"
# The loop and what drives it: they reach CS2VM2_Ask only by running opcodes.
LOOP_CHAIN = {
    "CS2VM2_RunOp", "cs2vm2_run_script_body", "cs2vm2_run_script_body_timed", "CS2VM2_RunScript",
    "CS2VM2_Run", "CS2VM2_ThreadRun", "CS2VM2_ThreadResume",
}


def first_statement(body):
    for line in body.split("\n"):
        if line.strip():
            return line.strip()
    return ""


def main():
    with open(SOURCE, encoding="utf-8") as handle:
        source = handle.read()
    functions = {}
    for match in FUNC_RE.finditer(source):
        functions[match.group(1)] = (match.group(2), COMMENT_RE.sub("", match.group(3)))
    assert "CS2VM2_RunOp" in functions and "CS2VM2_Ask" in functions, "cs2vm2.c: loop functions not found"

    problems = []
    for name, (_, body) in functions.items():
        if "host_exec(" in body and name != "CS2VM2_Ask":
            problems.append("%s calls host_exec itself: the host is asked only through CS2VM2_Ask" % name)

    calls = {name: set(CALL_RE.findall(body)) & set(functions) for name, (_, body) in functions.items()}
    asks = {name for name, callees in calls.items() if "CS2VM2_Ask" in callees}
    changed = True
    while changed:
        changed = False
        for name, callees in calls.items():
            if name not in asks and callees & asks:
                asks.add(name)
                changed = True
    asks -= LOOP_CHAIN
    callers = collections.defaultdict(set)
    for name, callees in calls.items():
        for callee in callees:
            callers[callee].add(name)

    entries = 0
    helpers = 0
    for name in sorted(asks):
        params, body = functions[name]
        dispatched = callers[name] == {"CS2VM2_RunOp"}
        makes_mark = "CS2VM2_PrepareYield(" in body
        if dispatched:
            entries += 1
            if first_statement(body) != PREPARE:
                problems.append("%s is dispatched to and reaches the host: its first statement must be `%s`"
                                % (name, PREPARE))
            if body.count("CS2VM2_PrepareYield(") != 1:
                problems.append("%s takes more than one mark" % name)
        else:
            helpers += 1
            if MARK_PARAM not in re.sub(r"\s+", " ", params):
                problems.append("%s reaches the host from a caller's instruction: it must take `%s`"
                                % (name, MARK_PARAM))
            if makes_mark:
                problems.append("%s is a helper and makes its own mark: by then its caller may have popped" % name)
            if "CS2VM2_RunOp" in callers[name] and len(callers[name]) > 1:
                problems.append("%s is both dispatched to and called as a helper" % name)

    # Case blocks of the dispatch that ask the host themselves.
    blocks = 0
    runop = functions["CS2VM2_RunOp"][1]
    for match in re.finditer(r"CS2VM2_Ask\(vm, &request, ([^)]*(?:\([^)]*\))?)\)", runop):
        blocks += 1
        argument = match.group(1)
        if argument == "CS2VM2_PrepareYield(vm)":
            continue
        # The dispatch's own case label (four spaces in), not one of a switch
        # nested inside the block.
        head = runop.rfind("\n    case CS2_OP_", 0, match.start()) + 1
        block = runop[head:match.start()]
        opened = block.find("{")
        if argument != "mark" or opened < 0 or first_statement(block[opened + 1:]) != PREPARE:
            problems.append("CS2VM2_RunOp, %s: a case that asks the host takes its mark as the block's "
                            "first statement" % runop[head:runop.find(":", head)].strip())

    if problems:
        for problem in problems:
            print("check-cs2-yield: " + problem)
        print("check-cs2-yield: FAIL (%d)" % len(problems))
        return 1
    print("check-cs2-yield: PASS (%d dispatched implementations take a mark first, %d helpers take "
          "their caller's, %d dispatch cases ask in place)" % (entries, helpers, blocks))
    return 0


if __name__ == "__main__":
    sys.exit(main())
