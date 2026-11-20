"""One recipe line of a test target, under `make check-test-builds`: run the
part that builds, drop the part that runs.

A recipe line is cut at its top-level `&&`, `||`, `;` and `|` into commands,
and each command is one of:

  build    a compiler or linker, ar, ranlib, libtool, install_name_tool,
           codesign, lipo, dsymutil, strip, objcopy, xcrun; a sub-make whose
           goals are files (or an allowed tool goal); a generator, i.e. a
           program run with its stdout sent to a .c/.h/.inc/.def file
  neutral  mkdir, cd, echo, printf, true, `:`, exit
  run      everything else: the test binary, node, a python oracle, a content
           compile or pack, `test`/`[` (a check that something the RUN needs
           is present), and the fixture staging of a run (cp, mv, ln, rm,
           touch: they copy what the test reads, not what the build needs)

The longest leading run of build and neutral commands is executed and the
first run command ends the line; it and everything after it are dropped. A
kept part with no build command in it (`cd x && ./test`) is dropped too. A
line that opens with shell syntax (for, if, a brace group) is dropped whole;
if it compiles inside, that is logged as COMPOUND so the sweep can say which
compiles it could not reach.

Every decision is appended to $TORIRS_TB_LOG (one line each) when it is set.
"""

import os
import re
import shlex
import sys

BUILD = {
    "cc", "gcc", "clang", "c++", "g++", "clang++", "cpp", "ar", "ranlib", "libtool",
    "install_name_tool", "codesign", "lipo", "dsymutil", "strip", "objcopy", "xcrun",
}
NEUTRAL = {"mkdir", "cd", "echo", "printf", "true", ":", "exit"}
# A sub-make goal that is a file is a build. These named goals are builds too.
# Any other named goal is not built here: a test-* or check-* goal is swept on
# its own, and the rest (torirsserver-servpack, torirsserver-scripts-lanes,
# torirsserver-cache-check: content compiles and packs; `all`, `torirsserver`:
# the whole client or server, built by their own targets) must not run.
MAKE_GOALS_ALLOWED = {"cachepack"}
PIPE_FILTERS = {"tee", "grep", "sed", "head", "tail", "cat"}
COMPOUND_HEADS = {"for", "while", "until", "if", "case", "{", "(", "function", "select"}
MINGW_TOOL = re.compile(r"-(gcc|g\+\+|ar|ranlib|windres|strip|objcopy)$")
GENERATED_SOURCE = re.compile(r">\s*\S+\.(c|h|inc|def)\s*$")


def log(kind, text):
    path = os.environ.get("TORIRS_TB_LOG")
    if not path:
        return
    with open(path, "a") as f:
        f.write("%s\t%s\n" % (kind, " ".join(text.split())[:400]))


def split_top(line):
    """[(op_before, raw_segment)] at top-level && || ; | (quotes and $( ) aware)."""
    out = []
    start = 0
    op = None
    i = 0
    depth = 0
    quote = None
    n = len(line)
    while i < n:
        c = line[i]
        if quote:
            if c == "\\" and quote == '"':
                i += 2
                continue
            if c == quote:
                quote = None
            i += 1
            continue
        if c in "'\"`":
            quote = c
        elif c == "\\":
            i += 2
            continue
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        elif depth == 0 and line.startswith(("&&", "||"), i):
            out.append((op, line[start:i]))
            op = line[i:i + 2]
            i += 2
            start = i
            continue
        elif depth == 0 and c in ";|" and not (c == "|" and i > 0 and line[i - 1] == ">"):
            out.append((op, line[start:i]))
            op = c
            start = i + 1
        i += 1
    out.append((op, line[start:]))
    return [(o, s) for o, s in out if s.strip()]


def program(segment):
    try:
        toks = shlex.split(segment, posix=True)
    except ValueError:
        return None, []
    i = 0
    while i < len(toks) and re.match(r"^[A-Za-z_][A-Za-z0-9_]*=", toks[i]):
        i += 1
    while i < len(toks) and re.match(r"^\d*[<>]", toks[i]):
        i += 1
    if i >= len(toks):
        return ":", []
    return toks[i], toks[i + 1:]


def make_kind(args):
    goals = []
    skip = False
    for a in args:
        if skip:
            skip = False
            continue
        if a in ("-C", "-f", "-I", "-j", "-o", "-W"):
            skip = True
            continue
        if not a.startswith("-") and "=" not in a:
            goals.append(a)
    if not goals:
        return "run"  # the default goal: not a test build
    for g in goals:
        if "/" in g or g.endswith((".o", ".a", ".dylib")):
            continue
        if g in MAKE_GOALS_ALLOWED:
            continue
        return "run"
    return "build"


def classify(segment):
    """'build', 'neutral', 'run' or 'compound' for one command."""
    s = segment.strip()
    words = s.split()
    head = words[0] if words else ""
    if head in COMPOUND_HEADS or s.startswith(("{", "(")):
        return "compound"
    prog, args = program(s)
    if prog is None:
        return "compound"
    base = os.path.basename(prog)
    if base in ("make", "gmake"):
        return make_kind(args)
    if base in BUILD or MINGW_TOOL.search(base):
        return "build"
    if base in NEUTRAL:
        return "neutral"
    if GENERATED_SOURCE.search(s) and "/" in prog:
        return "build"  # a built generator writing a source the build compiles
    return "run"


def main():
    line = sys.argv[1].replace("\\\n", " ")
    segs = split_top(line)
    keep_end = len(segs)
    verdict = "build"
    for idx, (op, seg) in enumerate(segs):
        kind = classify(seg)
        if kind in ("build", "neutral"):
            continue
        if op == "|" and program(seg)[0] in PIPE_FILTERS:
            continue  # a build's output piped into a filter (`cc ... 2>&1 | tee log`)
        verdict = kind
        keep_end = idx
        break
    if verdict == "build":
        log("RUN", line)
        os.execv("/bin/sh", ["/bin/sh", "-c", line])
    # A pipe whose head we keep but whose tail we would drop changes what the
    # head writes to; drop the whole pipeline instead.
    while keep_end and segs[keep_end][0] == "|":
        keep_end -= 1
    kept = segs[:keep_end]
    compiles = re.search(r"(^|[\s/;&|(])(cc|clang|gcc)\s", line) is not None
    if not any(classify(s) == "build" for _, s in kept):
        log("COMPOUND" if (verdict == "compound" and compiles) else "SKIP", line)
        return 0
    # The kept prefix is the raw line up to the operator that introduced the
    # first dropped command.
    pos = 0
    cut = len(line)
    for idx, (op, seg) in enumerate(segs):
        at = line.find(seg, pos)
        if idx == keep_end:
            cut = at
            break
        pos = at + len(seg)
    prefix = re.sub(r"(&&|\|\||;|\|)\s*$", "", line[:cut].rstrip()).rstrip()
    log("PREFIX", prefix)
    log("SKIP", line[cut:])
    os.execv("/bin/sh", ["/bin/sh", "-c", prefix])


if __name__ == "__main__":
    sys.exit(main())
