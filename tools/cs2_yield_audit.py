#!/usr/bin/env python3
"""The CS2 interpreter's yield rule, checked against its source.

Nothing is saved for a yield (src/cs2vm2/cs2vm2.h, struct CS2VM2_Took). An
implementation that can reach the host counts what it pops in a local
(CS2VM2_TakeInt / _TakeStr / _TakeLong) and hands the count to CS2VM2_Ask,
which puts it back when the host answers "not ready". A debug build asserts
the count at every ask and stops at a yield that left a stack moved; a release
build checks nothing. This catches the same mistakes without running anything:

  1. `host_exec` is called from exactly one place, CS2VM2_Ask, which takes
     the count and puts it back on a yield (calls CS2VM2_YieldRestore).
  2. A function that calls CS2VM2_Ask has a count in scope at the call: its
     own (`struct CS2VM2_Took took = CS2VM2_TOOK_NOTHING;`, in the function
     or in the case block that asks) or its caller's (a `struct CS2VM2_Took
     took` parameter).
  3. Where a count is in scope nothing pops behind its back before an ask: no
     CS2VM2_PopInt / _PopStr / _PopLong, no write to a stack top, and no call
     to a function of this file that pops unless that function is given the
     count.
  4. A function given its caller's count does not also start one.
  5. The game side takes nothing off the stacks: no CS2VM2_Take* / _Pop* /
     CS2VM2_YieldRestore in the files that answer requests. The VM pops every
     request's arguments into its arm and CS2VM2_Ask puts them back; a host
     that popped would have to count and restore them itself (DB_* did, until
     its arguments moved into the request).

    tools/cs2_yield_audit.py          (make -C src check-cs2-yield)
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, "src", "cs2vm2", "cs2vm2.c")
# The files that answer the client's requests (RS_CS2Host_Exec and the
# handlers it hands signature-popped requests to).
HOST_SOURCES = [
    os.path.join(ROOT, "src", "game", name)
    for name in ("rs_cs2_host.c", "rs_cs2_client_ops.c", "rs_cs2_social_ops.c", "rs_cs2_market_ops.c")
]
HOST_TAKES = re.compile(r"\bCS2VM2_(?:Take(?:Int|Str|Long)|Pop(?:Int|Str|Long)|YieldRestore)\s*\(")

FUNC_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\(([^;{]*?)\)\s*\n\{\n", re.M)
CALL_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")
OWN = "struct CS2VM2_Took took = CS2VM2_TOOK_NOTHING;"
BY_VALUE = re.compile(r"struct CS2VM2_Took took\b")
BY_POINTER = re.compile(r"struct CS2VM2_Took\* took\b")
POP = re.compile(r"\bCS2VM2_Pop(?:Int|Str|Long)\s*\(")
TAKE = re.compile(r"\bCS2VM2_Take(?:Int|Str|Long)\s*\(")
TOP_WRITE = re.compile(
    r"(?:--|\+\+)\s*[A-Za-z_>.\-]*\b(?:ints|strs|longs)_stack_top\b"
    r"|\b(?:ints|strs|longs)_stack_top\s*(?:--|\+\+|-=|\+=|=(?!=))"
)
# The primitives themselves, and the loop, which holds the top in a register.
MAY_MOVE_TOPS = {
    "CS2VM2_PopInt", "CS2VM2_PopStr", "CS2VM2_PopLong", "CS2VM2_PushInt", "CS2VM2_PushStr",
    "CS2VM2_PushLong", "CS2VM2_PushArray", "cs2vm2_run_script_body",
}


def blank(match):
    return re.sub(r"[^\n]", " ", match.group(0))


def main():
    with open(SOURCE, encoding="utf-8") as handle:
        source = handle.read()
    code = re.sub(r"/\*.*?\*/|//[^\n]*|\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'", blank, source, flags=re.S)

    functions = {}
    for match in FUNC_RE.finditer(code):
        start = match.end() - 2
        depth = 0
        end = start
        while True:
            char = code[end]
            if char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    break
            end += 1
        functions[match.group(1)] = (match.group(2), start, end + 1)

    def line_of(offset):
        return source.count("\n", 0, offset) + 1

    problems = []

    # 1. host_exec only in CS2VM2_Ask, which takes the count and puts it back
    #    on a yield
    for match in re.finditer(r"->host_exec\s*\(", code):
        owner = next((n for n, (_, a, b) in functions.items() if a <= match.start() < b), None)
        if owner != "CS2VM2_Ask":
            problems.append(f"{line_of(match.start())}: host_exec is called outside CS2VM2_Ask (in {owner})")
    if "CS2VM2_Ask" not in functions:
        problems.append("0: CS2VM2_Ask is missing")
    else:
        params, a, b = functions["CS2VM2_Ask"]
        if not BY_VALUE.search(params):
            problems.append(f"{line_of(a)}: CS2VM2_Ask does not take the instruction's count")
        if not re.search(r"\bCS2VM2_YieldRestore\s*\(", code[a:b]):
            problems.append(f"{line_of(a)}: CS2VM2_Ask does not put the count back on a yield")

    # which functions pop, directly
    pops = set()
    for name, (_, a, b) in functions.items():
        body = code[a:b]
        if POP.search(body) or TAKE.search(body) or TOP_WRITE.search(body):
            pops.add(name)

    asking = given = own_counts = 0
    for name, (params, a, b) in functions.items():
        if name == "CS2VM2_Ask":
            continue
        body = code[a:b]
        by_value = bool(BY_VALUE.search(params))
        by_pointer = bool(BY_POINTER.search(params))
        owns = [m.start() for m in re.finditer(re.escape(OWN), body)]
        asks = [m.start() for m in re.finditer(r"\bCS2VM2_Ask\s*\(", body)]
        if not (asks or owns or by_value):
            continue

        # the open blocks at each offset of interest
        stacks = {}
        stack = []
        marks = set(owns) | set(asks)
        marks |= {m.start() for m in POP.finditer(body)} | {m.start() for m in TOP_WRITE.finditer(body)}
        marks |= {m.start() for m in CALL_RE.finditer(body)}
        for index, char in enumerate(body):
            if index in marks:
                stacks[index] = tuple(stack)
            if char == "{":
                stack.append(index)
            elif char == "}":
                stack.pop()

        def counted(offset):
            if by_value or by_pointer:
                return True
            here = stacks[offset]
            return any(own < offset and stacks[own] == here[: len(stacks[own])] for own in owns)

        # A pop after the function's last ask has no yield left to answer for
        # (GOSUB_WITH_PARAMS moves its arguments once the host pushed the frame).
        last_ask = max(asks) if asks else len(body)

        def before_an_ask(offset):
            return by_pointer or offset < last_ask

        # 4.
        if (by_value or by_pointer) and owns:
            problems.append(f"{line_of(a)}: {name} is given its caller's count and starts another")
        if by_value:
            given += 1
        own_counts += len(owns)

        # 2.
        for ask in asks:
            asking += 1
            if not counted(ask):
                problems.append(f"{line_of(a + ask)}: {name} asks the host with no count in scope")

        # 3.
        for match in POP.finditer(body):
            if counted(match.start()) and before_an_ask(match.start()):
                problems.append(
                    f"{line_of(a + match.start())}: {name} pops with {match.group(0)[:-1].strip()} "
                    "where a count is in scope; use CS2VM2_Take*")
        if name not in MAY_MOVE_TOPS:
            for match in TOP_WRITE.finditer(body):
                if counted(match.start()) and before_an_ask(match.start()):
                    problems.append(
                        f"{line_of(a + match.start())}: {name} writes a stack top where a count is in scope")
        for match in CALL_RE.finditer(body):
            callee = match.group(1)
            if callee == name or callee not in functions or callee not in pops:
                continue
            if callee in MAY_MOVE_TOPS or not counted(match.start()) or not before_an_ask(match.start()):
                continue
            callee_params = functions[callee][0]
            if BY_VALUE.search(callee_params) or BY_POINTER.search(callee_params):
                continue
            problems.append(
                f"{line_of(a + match.start())}: {name} calls {callee}, which pops, without giving it the count")

    # 5. the game side takes nothing off the stacks
    host_problems = []
    for path in HOST_SOURCES:
        with open(path, encoding="utf-8") as handle:
            host_source = handle.read()
        host_code = re.sub(r"/\*.*?\*/|//[^\n]*|\"(?:\\.|[^\"\\])*\"", blank, host_source, flags=re.S)
        for match in HOST_TAKES.finditer(host_code):
            host_problems.append(
                f"{os.path.relpath(path, ROOT)}:{host_source.count(chr(10), 0, match.start()) + 1}: "
                f"the host takes from the stacks ({match.group(0)[:-1].strip()}); "
                "the VM pops a request's arguments into its arm")

    if problems or host_problems:
        print("check-cs2-yield: FAIL")
        for problem in problems:
            print("  src/cs2vm2/cs2vm2.c:" + problem)
        for problem in host_problems:
            print("  " + problem)
        return 1
    print(
        f"check-cs2-yield: PASS ({asking} asks, each with a count in scope: {own_counts} counts started, "
        f"{given} helpers given their caller's; no pop or stack-top write behind a count; "
        f"no pop on the game side)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
