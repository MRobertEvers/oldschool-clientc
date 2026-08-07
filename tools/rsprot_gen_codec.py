#!/usr/bin/env python3
"""
Generate C message structs + encode/decode functions from RSProt's Kotlin
Encoder/Decoder classes, for the 3rd/rsprot library.

    tools/rsprot_gen_codec.py 239

This is NOT a general Kotlin compiler. RSProt's codec bodies are written in one
very regular style (ktlint-formatted, one call per line, no exotic control
flow) because they exist only to read/write a fixed wire format in order. This
generator recognizes exactly that style: a small expression grammar (field
access, casts, arithmetic, elvis, inline if/else, comparisons) and a small
statement grammar (val bindings, buffer.pX/gX calls, for loops over a range or
a collection, if/else blocks, and the encoder's implicit "no return" / the
decoder's trailing `return Type(args...)`).

**A file the parser cannot fully understand is SKIPPED, not guessed.** This
project's own porting history says why: three of RSProt's first six
hand-transcribed writers were wrong with nothing to catch it
(docs/RSPROT_OSRS239_PORT.md §5a), and "refusing is visible; a wrong layout is
not" is stated there as the operating rule for exactly this kind of table.
A generator that emits code for a construct it does not model would be the
same failure at a larger scale. Every skip is logged with the reason, and the
skip list is the actual TODO for hand-porting the remainder — see
3rd/rsprot/gen/codec_status_<rev>.txt after a run.

**Self-verification, not an external oracle.** There is no RSProt production
binary to capture real traffic from, so generated encoders/decoders cannot be
checked against a live wire trace the way the hand-transcribed osrs239 tables
were (docs/RSPROT_OSRS239_PORT.md §5b). What IS generated alongside every
encoder is a "shadow decoder" -- literally the same field list read back with
the exact inverse of each write call -- and alongside every decoder a "shadow
encoder". 3rd/rsprot/test/test_codec_roundtrip.c fills a struct with random
values, encodes, shadow-decodes, and asserts equality for every generated
message type. This catches the majority-case transcription bugs (transposed
fields, wrong Alt-order, wrong width) without needing a live client -- the
residual risk (both the parser and a hand reader mistranscribing the SAME
Kotlin the same wrong way) is no worse than the manual process already in use
for osrs230/osrs239, and measurably better than no check at all.
"""

import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
RSPROT = os.path.expanduser("~/Documents/git_repos/rsprot")
OUT_DIR = os.path.join(REPO_ROOT, "3rd", "rsprot", "gen", "codec")

# --- directories excluded from the generic pass -----------------------------
#
# These are not MessageEncoder/MessageDecoder classes with a flat field list;
# they are stateful multi-packet subsystems (bit-packed avatar streams with
# their own per-observer state machines) that need a hand port, not codegen.
EXCLUDE_DIR_PARTS = (
    "playerinfo",
    "npcinfo",
    "worldentity",
)

# Encoder/decoder classes with no encode()/decode() body at all (NoOp, or a
# body this generator does not need to touch because the packet carries no
# payload). Recognized structurally (see parse_class) rather than listed here.


# --- tokenizer ---------------------------------------------------------------

TOKEN_RE = re.compile(
    r"""
    (?P<ws>\s+)
  | (?P<comment>//[^\n]*)
  | (?P<num>0[xX][0-9A-Fa-f]+|\d+\.\d+[fF]?|\d+[uUlL]?)
  | (?P<str>"(?:[^"\\]|\\.)*")
  | (?P<char>'(?:[^'\\]|\\.)')
  | (?P<ident>[A-Za-z_][A-Za-z0-9_]*)
  | (?P<elvis>\?:)
  | (?P<safedot>\?\.)
  | (?P<range3>\.\.<|until)
  | (?P<range>\.\.)
  | (?P<op>==|!=|<=|>=|&&|\|\||[-+*/%()\[\]{},.<>=!&|^~:])
    """,
    re.VERBOSE,
)


def tokenize(src):
    toks = []
    pos = 0
    while pos < len(src):
        m = TOKEN_RE.match(src, pos)
        if not m:
            raise SyntaxError(f"tokenizer stuck at: {src[pos:pos+40]!r}")
        pos = m.end()
        kind = m.lastgroup
        if kind in ("ws", "comment"):
            continue
        toks.append((kind, m.group()))
    return toks


# --- expression AST -----------------------------------------------------------
#
# Deliberately tiny: everything downstream (the C emitter) switches on `kind`.

def E(kind, **kw):
    kw["kind"] = kind
    return kw


class ExprParser:
    """Small precedence-climbing parser over a flat token list."""

    def __init__(self, toks):
        self.toks = toks
        self.i = 0

    def peek(self):
        return self.toks[self.i] if self.i < len(self.toks) else (None, None)

    def eat(self, val=None):
        k, v = self.peek()
        if val is not None and v != val:
            raise SyntaxError(f"expected {val!r}, got {v!r}")
        self.i += 1
        return v

    def at_end(self):
        return self.i >= len(self.toks)

    def parse(self):
        e = self.expr()
        if not self.at_end():
            raise SyntaxError(f"trailing tokens: {self.toks[self.i:]}")
        return e

    def expr(self):
        return self.elvis()

    def elvis(self):
        left = self.ternary_or_or()
        while self.peek()[0] == "elvis":
            self.eat()
            right = self.ternary_or_or()
            left = E("elvis", left=left, right=right)
        return left

    def ternary_or_or(self):
        # Kotlin `if (cond) A else B` as an expression.
        if self.peek()[1] == "if":
            self.eat("if")
            self.eat("(")
            cond = self.expr()
            self.eat(")")
            then = self.expr()
            self.eat("else")
            els = self.expr()
            return E("ternary", cond=cond, then=then, els=els)
        return self.logic_or()

    def logic_or(self):
        left = self.logic_and()
        while self.peek()[1] in ("||", "or"):
            op = self.eat()
            left = E("bin", op="||", left=left, right=self.logic_and())
        return left

    def logic_and(self):
        left = self.bitwise()
        while self.peek()[1] in ("&&", "and"):
            self.eat()
            left = E("bin", op="&&", left=left, right=self.bitwise())
        return left

    def bitwise(self):
        left = self.compare()
        while self.peek()[1] in ("or", "and", "xor", "shl", "shr", "ushr") and self.peek()[0] == "ident":
            op = self.eat()
            left = E("bin", op=op, left=left, right=self.compare())
        return left

    def compare(self):
        left = self.range_expr()
        while self.peek()[1] in ("==", "!=", "<", ">", "<=", ">=", "in") and self.peek()[0] in ("op", "ident"):
            if self.peek()[1] == "in":
                self.eat()
                lo = self.additive()
                self.eat(".." if self.peek()[1] == ".." else self.peek()[1])
                hi = self.additive()
                left = E("in_range", val=left, lo=lo, hi=hi)
                continue
            op = self.eat()
            left = E("bin", op=op, left=left, right=self.range_expr())
        return left

    def range_expr(self):
        return self.additive()

    def additive(self):
        left = self.term()
        while self.peek()[1] in ("+", "-"):
            op = self.eat()
            left = E("bin", op=op, left=left, right=self.term())
        return left

    def term(self):
        left = self.unary()
        while self.peek()[1] in ("*", "/", "%"):
            op = self.eat()
            left = E("bin", op=op, left=left, right=self.unary())
        return left

    def unary(self):
        if self.peek()[1] == "-":
            self.eat()
            return E("neg", val=self.unary())
        if self.peek()[1] == "!":
            self.eat()
            return E("not", val=self.unary())
        return self.postfix()

    def postfix(self):
        e = self.primary()
        while True:
            k, v = self.peek()
            if v in (".", "?."):
                self.eat()
                name = self.eat()
                if self.peek()[1] == "(":
                    args = self.arglist()
                    e = E("call", recv=e, name=name, args=args, safe=(v == "?."))
                else:
                    e = E("member", recv=e, name=name, safe=(v == "?."))
            elif v == "[":
                self.eat()
                idx = self.expr()
                self.eat("]")
                e = E("index", recv=e, idx=idx)
            elif v == "(" and e["kind"] == "ident":
                args = self.arglist()
                e = E("call", recv=None, name=e["name"], args=args, safe=False)
            else:
                break
        return e

    def arglist(self):
        self.eat("(")
        args = []
        if self.peek()[1] != ")":
            args.append(self.named_arg())
            while self.peek()[1] == ",":
                self.eat()
                args.append(self.named_arg())
        self.eat(")")
        return args

    def named_arg(self):
        # Trailing-lambda-free named args: `name = expr`. We only see this
        # rarely; treat `ident =` at arg position as a named arg wrapper.
        save = self.i
        if self.peek()[0] == "ident":
            nm = self.peek()[1]
            if self.i + 1 < len(self.toks) and self.toks[self.i + 1][1] == "=" and (
                self.i + 2 >= len(self.toks) or self.toks[self.i + 2][1] != "="
            ):
                self.i += 2
                return E("namedarg", name=nm, val=self.expr())
        self.i = save
        return self.expr()

    def primary(self):
        k, v = self.peek()
        if v == "(":
            self.eat()
            e = self.expr()
            self.eat(")")
            return e
        if k == "num":
            self.eat()
            return E("num", text=v)
        if k == "str":
            self.eat()
            return E("str", text=v[1:-1])
        if k == "char":
            self.eat()
            return E("char", text=v[1:-1])
        if v == "true" or v == "false":
            self.eat()
            return E("bool", val=(v == "true"))
        if v == "null":
            self.eat()
            return E("null")
        if k == "ident":
            self.eat()
            return E("ident", name=v)
        raise SyntaxError(f"unexpected token {v!r}")


def parse_expr(text):
    return ExprParser(tokenize(text)).parse()


# --- statement-level splitting -----------------------------------------------
#
# We do not build a full statement grammar; instead we split a method body
# into balanced-brace/paren top-level chunks (kotlin here is one logical
# statement per top-level chunk, ktlint-formatted) and classify each chunk by
# its head token(s). This mirrors how a human skims this code.

def split_top_level(body):
    """Split `body` into a list of raw statement strings at top-level ';' or
    newline-terminated points, respecting (), {}, []."""
    stmts = []
    depth = 0
    cur = []
    i = 0
    n = len(body)
    while i < n:
        c = body[i]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        cur.append(c)
        if c == "\n" and depth == 0:
            s = "".join(cur).strip()
            if s:
                stmts.append(s)
            cur = []
        i += 1
    s = "".join(cur).strip()
    if s:
        stmts.append(s)
    # Re-merge continuation lines: a statement is "complete" when parens/
    # braces balance AND it doesn't end with a binary/assignment continuation
    # token. Simplest robust approach: re-join adjacent raw lines using a
    # bracket-depth scan over the WHOLE body instead of per-line, which is
    # what the loop above already does via `depth` -- so `stmts` here are
    # already top-level-balanced chunks. But a single statement can still
    # span multiple of these chunks if it has no brackets at all and Kotlin
    # wrapped it across lines (e.g. `foo +\n    bar`). Merge a chunk into the
    # previous one if it starts with an operator/`.`/`?:`.
    merged = []
    for s in stmts:
        if merged and re.match(r"^(\.|\?:|\?\.|\+|-|\*|/|and\b|or\b|xor\b|shl\b|shr\b|ushr\b|==|!=)", s):
            merged[-1] += " " + s
        else:
            merged.append(s)
    return merged


def strip_comments(src):
    # Remove // line comments (not inside strings -- codec bodies don't put
    # `//` inside string literals for wire values, and if they ever do this
    # generator will simply skip the file when the parser then fails).
    out = []
    for line in src.split("\n"):
        # crude but sufficient: find `//` not preceded by `:` (URLs aren't in
        # this code) and not inside a string -- check quote parity.
        idx = line.find("//")
        if idx != -1:
            before = line[:idx]
            if before.count('"') % 2 == 0:
                line = before
        out.append(line)
    return "\n".join(out)


# --- Kotlin class extraction --------------------------------------------------

CLASS_RE = re.compile(
    r"public\s+class\s+(\w+)(?:\([^)]*\))?\s*:\s*"
    r"(MessageEncoder|MessageDecoder|ZoneProtEncoder|NoOpMessageEncoder)\s*<\s*([\w.]+)\s*>",
)

PROT_RE = re.compile(r"override val prot:\s*\w+\s*=\s*([\w.]+)")


def find_method_body(src, method_name, has_buffer_first_param):
    """Locate `override fun encode(...)  { ... }` or `decode(...) { ... }` and
    return its raw body text (between the outermost matching braces)."""
    m = re.search(rf"override fun {method_name}\s*\(", src)
    if not m:
        return None
    # Find the matching close-paren for the parameter list, then the `{`
    # after it, then the matching `}`.
    i = m.end() - 1  # at '('
    depth = 0
    j = i
    while j < len(src):
        if src[j] == "(":
            depth += 1
        elif src[j] == ")":
            depth -= 1
            if depth == 0:
                break
        j += 1
    # An expression-body function (`fun decode(...): T = EXPR`, no `{ }` at
    # all) has no brace to find here -- return None so the caller's
    # expression-body fallback in parse_file() can handle it, rather than
    # crashing on the next unrelated `{` later in the file (the next class,
    # or nothing at all).
    nl = src.find("\n", j)
    line_tail = src[j:nl if nl != -1 else len(src)]
    if "{" not in line_tail and "=" in line_tail:
        return None
    if "{" not in src[j:]:
        return None
    k = src.index("{", j)
    depth = 0
    p = k
    while p < len(src):
        if src[p] == "{":
            depth += 1
        elif src[p] == "}":
            depth -= 1
            if depth == 0:
                break
        p += 1
    return src[k + 1 : p]


def parse_file(path):
    src = open(path).read()
    src = strip_comments(src)
    m = CLASS_RE.search(src)
    if not m:
        return None
    cls_name, base, msg_type = m.groups()
    pm = PROT_RE.search(src)
    prot = pm.group(1).split(".")[-1] if pm else None

    is_decoder = base == "MessageDecoder"
    if base == "NoOpMessageEncoder":
        return {
            "class": cls_name,
            "base": base,
            "msg_type": msg_type.split(".")[-1],
            "prot": prot,
            "is_decoder": False,
            "no_body": True,
            "fields": [],
            "ops": [],
        }

    method = "decode" if is_decoder else "encode"
    body = find_method_body(src, method, not is_decoder)
    if body is None:
        # `override fun decode(buffer: JagByteBuf): Idle = Idle` -- Kotlin's
        # expression-body syntax, used only for zero-payload singleton
        # messages (no `{ }` block to find). A bare identifier or a
        # no-arg-constructor RHS on this shape carries no wire fields.
        em = re.search(rf"override fun {method}\s*\([^)]*\)\s*(?::\s*[\w.<>]+)?\s*=\s*([^\n{{]+)", src)
        if em and "(" not in em.group(1).strip().rstrip():
            return {
                "class": cls_name,
                "base": base,
                "msg_type": msg_type.split(".")[-1],
                "prot": prot,
                "is_decoder": is_decoder,
                "no_body": True,
            }
        return None
    return {
        "class": cls_name,
        "base": base,
        "msg_type": msg_type.split(".")[-1],
        "prot": prot,
        "is_decoder": is_decoder,
        "no_body": False,
        "body": body,
        "path": path,
    }


# --- statement -> IR -----------------------------------------------------------
#
# IR ops:
#   {"op": "write", "fn": "p2Alt1", "expr": <AST>}
#   {"op": "local", "name": "count", "expr": <AST>}
#   {"op": "read",  "name": "x", "fn": "g2Alt1"}
#   {"op": "readbool", "name": "controlKey", "fn": "g1Alt1"}
#   {"op": "for_range", "var": "i", "lo": <AST>, "hi": <AST>, "body": [...]}
#   {"op": "for_each",  "var": "friend", "iter": <AST>, "body": [...]}
#   {"op": "if", "cond": <AST>, "then": [...], "else": [...]}
#   {"op": "return", "args": [<AST>...]}

WRITE_FN_RE = re.compile(r"^buffer\.(p[\w0-9]*)\s*\((.*)\)$", re.S)
READ_FN_RE = re.compile(r"^buffer\.(g[\w0-9]*)\s*\(\s*\)$")


class Unsupported(Exception):
    pass


def parse_statements(chunks, is_decoder):
    ops = []
    i = 0
    n = len(chunks)
    while i < n:
        s = chunks[i]
        i += 1
        ops.append(parse_one(s, is_decoder))
    return ops


def parse_one(s, is_decoder):
    s = s.strip()
    if s.startswith("val ") or s.startswith("var "):
        return parse_val(s, is_decoder)
    if s.startswith("buffer."):
        m = WRITE_FN_RE.match(s)
        if not m:
            raise Unsupported(f"unrecognized buffer call: {s[:60]}")
        fn, argtext = m.groups()
        return {"op": "write", "fn": fn, "expr": parse_expr(argtext)}
    if s.startswith("for ("):
        return parse_for(s, is_decoder)
    if s.startswith("if (") or s.startswith("if("):
        return parse_if_stmt(s, is_decoder)
    if s.startswith("return "):
        return parse_return(s)
    if s == "continue" or s == "break":
        return {"op": s}
    if "=" in s.split("(")[0] and not s.startswith("buffer") and re.match(r"^\w+(\.\w+)*\s*=[^=]", s):
        # plain assignment to an already-declared local, e.g. `count = 5`
        name, expr = s.split("=", 1)
        return {"op": "assign", "name": name.strip(), "expr": parse_expr(expr.strip())}
    # Bare expression statement with side effects we don't model (e.g.
    # `message.returnInventory()`) -- safe to drop if it doesn't touch the
    # buffer. Anything touching the buffer we don't recognize is fatal.
    if "buffer." in s:
        raise Unsupported(f"buffer expression not understood: {s[:60]}")
    return {"op": "ignore", "text": s}


def split_val_type_and_init(rhs):
    return rhs


def parse_val(s, is_decoder):
    # `val NAME = EXPR` or `val NAME =\n    EXPR` (already joined by
    # split_top_level's continuation merge) or `val NAME: Type = EXPR`.
    m = re.match(r"^(?:val|var)\s+(\w+)(?:\s*:\s*[\w<>.,? ]+)?\s*=\s*(.*)$", s, re.S)
    if not m:
        raise Unsupported(f"val without '=': {s[:60]}")
    name, rhs = m.groups()
    rhs = rhs.strip()

    if is_decoder:
        gm = READ_FN_RE.match(rhs)
        if gm:
            return {"op": "read", "name": name, "fn": gm.group(1)}
        # `TypeName(buffer.gN())` -- a value-class/coord wrapper constructed
        # directly around a single read (e.g. `CoordGrid(buffer.g4())`). The
        # wrapper's only content IS that read; unwrap it to a plain read
        # under the local's own name, same as the bare form above.
        wm = re.match(r"^\w+\(\s*(buffer\.g[\w0-9]*\(\))\s*\)$", rhs)
        if wm:
            gm2 = READ_FN_RE.match(wm.group(1))
            if gm2:
                return {"op": "read", "name": name, "fn": gm2.group(1)}
        # `buffer.gN() == 1` boolean derivation
        m2 = re.match(r"^buffer\.(g[\w0-9]*)\(\)\s*==\s*(\d+)$", rhs)
        if m2:
            return {"op": "readbool", "name": name, "fn": m2.group(1), "true_val": int(m2.group(2))}
        # a val whose RHS is a bare `if (...) A else B` where each branch is
        # itself a read -- decode a conditional read.
        if rhs.startswith("if ") or rhs.startswith("if("):
            try:
                expr = parse_expr(rhs)
            except SyntaxError as e:
                raise Unsupported(f"unparseable conditional read for {name}: {e}")
            return {"op": "local", "name": name, "expr": expr}
    # plain local, general expression (used by both directions)
    try:
        expr = parse_expr(rhs)
    except SyntaxError as e:
        raise Unsupported(f"unparseable val {name}: {e}")
    return {"op": "local", "name": name, "expr": expr}


def find_matching(s, open_ch, close_ch, start):
    depth = 0
    for idx in range(start, len(s)):
        if s[idx] == open_ch:
            depth += 1
        elif s[idx] == close_ch:
            depth -= 1
            if depth == 0:
                return idx
    raise Unsupported("unbalanced brackets")


def parse_block(body_text, is_decoder):
    return parse_statements(split_top_level(body_text), is_decoder)


def parse_for(s, is_decoder):
    # for ( HEADER ) { BODY }
    popen = s.index("(")
    pclose = find_matching(s, "(", ")", popen)
    header = s[popen + 1 : pclose].strip()
    rest = s[pclose + 1 :].strip()
    if not rest.startswith("{"):
        raise Unsupported(f"for-loop without braces: {s[:60]}")
    bopen = 0
    bclose = find_matching(rest, "{", "}", bopen)
    body_text = rest[bopen + 1 : bclose]

    # `x in HI downTo LO` (descending, inclusive on both ends)
    m = re.match(r"^(\w+)\s+in\s+(.*?)\s+downTo\s+(.*)$", header)
    if m:
        var, hi, lo = m.groups()
        return {
            "op": "for_range",
            "var": var,
            "lo": parse_expr(lo.strip()),
            "hi": parse_expr(hi.strip()),
            "inclusive": True,
            "descending": True,
            "body": parse_block(body_text, is_decoder),
        }
    # `x in A..<B` / `x in A until B` / `x in A..B`
    m = re.match(r"^(\w+)\s+in\s+(.*?)\s*(?:\.\.<|until)\s*(.*)$", header)
    if m:
        var, lo, hi = m.groups()
        return {
            "op": "for_range",
            "var": var,
            "lo": parse_expr(lo.strip()),
            "hi": parse_expr(hi.strip()),
            "inclusive": False,
            "body": parse_block(body_text, is_decoder),
        }
    m = re.match(r"^(\w+)\s+in\s+(.*?)\.\.(.*)$", header)
    if m:
        var, lo, hi = m.groups()
        return {
            "op": "for_range",
            "var": var,
            "lo": parse_expr(lo.strip()),
            "hi": parse_expr(hi.strip()),
            "inclusive": True,
            "body": parse_block(body_text, is_decoder),
        }
    # destructuring `(key, value) in map`
    m = re.match(r"^\(\s*(\w+)\s*,\s*(\w+)\s*\)\s+in\s+(.*)$", header)
    if m:
        k, v, it = m.groups()
        return {
            "op": "for_each_pair",
            "key": k,
            "val": v,
            "iter": parse_expr(it.strip()),
            "body": parse_block(body_text, is_decoder),
        }
    # `x in someList`
    m = re.match(r"^(\w+)\s+in\s+(.*)$", header)
    if m:
        var, it = m.groups()
        return {
            "op": "for_each",
            "var": var,
            "iter": parse_expr(it.strip()),
            "body": parse_block(body_text, is_decoder),
        }
    raise Unsupported(f"unrecognized for-header: {header[:60]}")


def parse_if_stmt(s, is_decoder):
    popen = s.index("(")
    pclose = find_matching(s, "(", ")", popen)
    cond = s[popen + 1 : pclose].strip()
    rest = s[pclose + 1 :].strip()
    if not rest.startswith("{"):
        raise Unsupported(f"if-statement without braces: {s[:60]}")
    bclose = find_matching(rest, "{", "}", 0)
    then_text = rest[1:bclose]
    tail = rest[bclose + 1 :].strip()
    else_ops = []
    if tail.startswith("else"):
        tail = tail[4:].strip()
        if tail.startswith("if"):
            else_ops = [parse_if_stmt(tail, is_decoder)]
        elif tail.startswith("{"):
            eclose = find_matching(tail, "{", "}", 0)
            else_ops = parse_block(tail[1:eclose], is_decoder)
        else:
            raise Unsupported(f"unrecognized else-tail: {tail[:60]}")
    return {
        "op": "if",
        "cond": parse_expr(cond),
        "then": parse_block(then_text, is_decoder),
        "else": else_ops,
    }


def parse_return(s):
    # `return Type(\n a,\n b,\n)` or `return Type(a, b)`
    m = re.match(r"^return\s+(\w[\w.]*)\s*\((.*)\)\s*$", s, re.S)
    if not m:
        # `return` with no payload (Unit-returning path) -- fine for encoders
        # only; decoders always construct a message.
        if s.strip() == "return":
            return {"op": "return_void"}
        raise Unsupported(f"unrecognized return: {s[:60]}")
    ctor, argtext = m.groups()
    args = split_args(argtext)
    parsed = []
    for a in args:
        a = a.strip()
        if not a:
            continue
        parsed.append(parse_expr(a))
    return {"op": "return", "ctor": ctor, "args": parsed}


def split_args(text):
    parts = []
    depth = 0
    cur = []
    for c in text:
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        if c == "," and depth == 0:
            parts.append("".join(cur))
            cur = []
        else:
            cur.append(c)
    if "".join(cur).strip():
        parts.append("".join(cur))
    return parts


# --- wire function -> C function/type table -----------------------------------

def _build_fn_table():
    """Kotlin buffer call -> (RSPROT_* macro, C field type).

    The mapping is DIRECTION-FREE, which is the whole point of the codec shape
    this generator now targets: `p2Alt1` and `g2Alt1` both become
    `RSPROT_U2_ALT1`, and the executor decides at runtime whether that moves
    bytes out of the field or into it. A packet therefore has ONE transcription
    rather than a writer and a reader that can disagree -- the failure recorded
    in docs/RSPROT_OSRS239_PORT.md §5a, where three of the first six
    hand-transcribed writers were wrong.

    A Kotlin call with no row here raises Unsupported, so its file is SKIPPED
    and logged rather than guessed. The macro set is the measured one in
    include/rsprot_exec.h's RSPROT_PRIM_LIST: every p*/g* appearing in RSProt's
    complete revision-239 desktop codec set. Calls deliberately absent from it
    (pSmart1or2extended, pMidiVarLen, pType, the float/double widths) are absent
    here too -- a row nobody calls is a row nobody has checked.
    """
    t = {}
    for w, prim in ((1, "U1"), (2, "U2"), (3, "U3"), (4, "U4")):
        for alt, asuf in (("", ""), ("Alt1", "_ALT1"), ("Alt2", "_ALT2"), ("Alt3", "_ALT3")):
            t[f"p{w}{alt}"] = (f"RSPROT_{prim}{asuf}", "int32_t")
            t[f"g{w}{alt}"] = (f"RSPROT_{prim}{asuf}", "int32_t")
    # Signed reads. Only the widths rsprot_exec models: there is no I3/I4 row,
    # so g3s/g4s fall through to Unsupported rather than silently widening.
    t["g1s"] = ("RSPROT_I1", "int32_t")
    t["g2s"] = ("RSPROT_I2", "int32_t")
    t["p8"] = ("RSPROT_U8", "int64_t")
    t["g8"] = ("RSPROT_U8", "int64_t")
    # int32_t, not bool: RSPROT_BOOL resolves to rsprot_x_boolean, which takes
    # an int32_t* like every other primitive. A `bool` field would be a
    # different-width pointer and is a type error, not a style choice.
    t["pboolean"] = ("RSPROT_BOOL", "int32_t")
    t["gboolean"] = ("RSPROT_BOOL", "int32_t")
    for kt, macro in (
        ("pjstr", "RSPROT_JSTR"), ("gjstr", "RSPROT_JSTR"),
        ("pjstrnull", "RSPROT_JSTRNULL"), ("gjstrnull", "RSPROT_JSTRNULL"),
        ("pjstr2", "RSPROT_JSTR2"), ("gjstr2", "RSPROT_JSTR2"),
    ):
        t[kt] = (macro, "const char *")
    for kt, macro in (
        ("pSmart1or2", "RSPROT_SMART"), ("gSmart1or2", "RSPROT_SMART"),
        ("pSmart1or2null", "RSPROT_SMARTNULL"), ("gSmart1or2null", "RSPROT_SMARTNULL"),
        ("pSmart2or4null", "RSPROT_SMART2OR4NULL"), ("gSmart2or4null", "RSPROT_SMART2OR4NULL"),
        ("pVarInt2", "RSPROT_VARINT2"), ("gVarInt2", "RSPROT_VARINT2"),
        ("pVarInt2s", "RSPROT_VARINT2S"), ("gVarInt2s", "RSPROT_VARINT2S"),
    ):
        t[kt] = (macro, "int32_t")
    for alt, asuf in (("", ""), ("Alt1", "_ALT1"), ("Alt2", "_ALT2"), ("Alt3", "_ALT3")):
        t[f"pCombinedId{alt}"] = (f"RSPROT_COM{asuf}", "int32_t")
        t[f"gCombinedId{alt}"] = (f"RSPROT_COM{asuf}", "int32_t")
    return t


FN_TABLE = _build_fn_table()


def wire_c_fn(kt_fn):
    if kt_fn not in FN_TABLE:
        raise Unsupported(f"unmapped wire function: {kt_fn}")
    return FN_TABLE[kt_fn]


# --- scope / field registry ---------------------------------------------------
#
# A Scope owns one C struct. The top-level scope is the message struct; a
# for_each/for_each_pair loop body gets its OWN scope for its element struct.
# `fields` is an ordered dict name -> ctype (declaration order = first-seen
# order, which is also wire order for the common flat case -- readable output).

class Scope:
    def __init__(self, struct_name, parent=None, recv_name=None):
        self.struct_name = struct_name
        self.parent = parent
        self.recv_name = recv_name  # the Kotlin identifier this scope's struct is accessed through (None for top)
        self.fields = {}  # name -> ctype
        self.locals = {}  # kotlin local name -> ("field", cname) | ("local", ctype) | ("loopvar",) | ("elem_alias", child_scope)
        self.array_fields = {}  # field name -> element Scope (for for_each) or True (for indexed scalar arrays)
        self.count_field_for = {}  # array field name -> the count field name

    def add_field(self, name, ctype):
        if name not in self.fields:
            self.fields[name] = ctype
        return name


_CAMEL_BOUNDARY_1 = re.compile(r"(.)([A-Z][a-z]+)")
_CAMEL_BOUNDARY_2 = re.compile(r"([a-z0-9])([A-Z])")


def c_escape_ident(name):
    """Kotlin identifier -> C identifier, camelCase folded to snake_case.

    `interfaceId` -> `interface_id`. Cosmetic on its face, but these names are
    what DESCRIBE prints and what every hand-written call site spells, so they
    are read far more often than they are generated -- and a struct that mixes
    Kotlin casing with this tree's snake_case reads as two half-ported things.
    Run-of-capitals stay together: `objIdOrNpcID` -> `obj_id_or_npc_id`.
    """
    s = re.sub(r"[^A-Za-z0-9_]", "_", name)
    s = _CAMEL_BOUNDARY_1.sub(r"\1_\2", s)
    s = _CAMEL_BOUNDARY_2.sub(r"\1_\2", s)
    return re.sub(r"__+", "_", s).lower()


def lower_first(s):
    return s[:1].lower() + s[1:] if s else s


def deprefix_getter(name):
    """`getObject` -> `object`, `getX` -> `x`."""
    if name.startswith("get") and len(name) > 3 and name[3].isupper():
        return lower_first(name[3:])
    return name


# --- expression resolution + C emission ---------------------------------------

CAST_CALLS = {"toInt", "toByte", "toShort", "toLong", "toFloat", "toDouble", "toUInt", "toUByte", "toUShort"}


def resolve_recv_scope(recv_ast, scope):
    """If `recv_ast` is `message` (or a known nested-loop var), return the
    Scope it addresses. Otherwise None (not a field access we model)."""
    if recv_ast["kind"] != "ident":
        return None
    name = recv_ast["name"]
    if name == "message":
        s = scope
        while s.parent is not None:
            s = s.parent
        return s
    s = scope
    while s is not None:
        if s.recv_name == name:
            return s
        s = s.parent
    return None


def resolve_path(ast, scope):
    """Resolve `ast` to (target_scope, [path segments]) if it is a chain of
    plain member accesses rooted at `message`, a loop-iterated element var, or
    a local alias registered via a previous `val X = <path>` statement.
    Returns None if the chain does not root anywhere resolvable (the caller
    decides whether that is fatal).

    This is what lets `message.easing.id` (a value-class/enum property on top
    of a field) and `update.index` (a member access on top of an ALIASED
    field, from `val update = message.update`) both flatten to a single C
    struct field -- `easing_id`, `update_index` -- without special-casing each
    shape RSProt happens to use for "a field that isn't a bare Int"."""
    if ast["kind"] == "ident":
        name = ast["name"]
        if name == "message":
            s = scope
            while s.parent is not None:
                s = s.parent
            return (s, [])
        s = scope
        while s is not None:
            if s.recv_name == name:
                return (s, [])
            s = s.parent
        info = _all_local_names(scope).get(name)
        if info and info[0] == "alias_path":
            return (info[1], list(info[2]))
        return None
    if ast["kind"] == "member":
        base = resolve_path(ast["recv"], scope)
        if base is None:
            return None
        target_scope, path = base
        return (target_scope, path + [ast["name"]])
    return None


def flatten_field_name(path):
    """Path segments -> one C field name, collapsing value-class unwrapping.

    RSProt models a component address as a `CombinedId` value class whose only
    property is also called `combinedId`, and the revisions disagree about how
    much of it to spell:

        rev 221   buffer.p4(message.combinedId.combinedId)
        rev 239   buffer.pCombinedId(message.combinedId)

    Those are the same four bytes -- rsprot_exec.h states that pCombinedId IS
    p4 -- but the naive join gives `combined_id_combined_id` for one and
    `combined_id` for the other, so one packet ends up with two struct fields
    for one wire field and each version writes only its own. The wrapper is not
    always spelled the same as its property, either:

        message.destinationCombinedId.combinedId   ->  ..._combined_id_combined_id

    So a trailing segment collapses when the segment before it already ENDS
    with that name. Narrow on purpose -- it is a suffix match against the
    immediately preceding segment only, so `message.base.colour` and
    `message.player.name` are untouched, and a field genuinely named `x.x`
    would collapse but does not exist in the vendored tree.
    """
    segs = [c_escape_ident(p) for p in path]
    out = []
    for s in segs:
        if out and (out[-1] == s or out[-1].endswith("_" + s)):
            continue
        out.append(s)
    return "_".join(out)


def emit_expr(ast, scope, ctx):
    """Emit a C expression string for `ast`. `ctx` carries mode ('encode' or
    'decode') for a few asymmetric idioms. Raises Unsupported for anything
    not modeled -- see module docstring for why that is the correct failure
    mode here."""
    k = ast["kind"]

    if k == "num":
        t = ast["text"]
        if t.endswith(("f", "F")):
            return t
        return t.rstrip("uUlL")

    if k == "str":
        return '"' + ast["text"] + '"'

    if k == "char":
        ch = ast["text"]
        if ch == "\\":
            raise Unsupported("bare backslash char literal")
        return f"'{ch}'" if ch != "'" else "'\\''"

    if k == "bool":
        return "true" if ast["val"] else "false"

    if k == "null":
        return "NULL"

    if k == "ident":
        name = ast["name"]
        s = scope
        while s is not None:
            if name in s.locals:
                kind = s.locals[name]
                if kind[0] == "loopvar":
                    return c_escape_ident(name)
                if kind[0] == "local":
                    return c_escape_ident(name)
                if kind[0] == "count_alias":
                    owner, field = kind[1], kind[2]
                    return f"{owner}->{field}_count"
                if kind[0] == "field":
                    owner, cname = kind[1], kind[2]
                    return f"{owner}->{cname}"
            s = s.parent
        # Anything else that resolves as a path (bare `message.X` handled via
        # the "member" case normally, but a local *alias* to a field used
        # directly as a scalar -- `val update = message.update; ...update...`
        # with no further `.member` -- lands here too.)
        path = resolve_path(ast, scope)
        if path is not None:
            target_scope, segs = path
            if not segs:
                raise Unsupported(f"bare '{name}' has no wire value")
            ptr = struct_ptr_expr(target_scope, ctx)
            cname = flatten_field_name(segs)
            target_scope.add_field(cname, "int32_t")
            return f"{ptr}->{cname}"
        raise Unsupported(f"unresolved identifier: {name}")

    if k == "member":
        recv = ast["recv"]
        name = ast["name"]
        # `X.size` (X a resolvable path, e.g. `message.subInterfaces.size`,
        # NOT preceded by a `val` -- the count-alias idiom below only fires
        # when there IS a preceding `val`) marks X as an array field and
        # reads its caller-provided count, same convention as for_each's
        # synthetic `_count` companion.
        if name == "size":
            inner = resolve_path(recv, scope)
            if inner is not None and inner[1]:
                target_scope, segs = inner
                field = flatten_field_name(segs)
                target_scope.add_field(field, "int32_t")
                target_scope.array_fields.setdefault(field, True)
                return f"{struct_ptr_expr(target_scope, ctx)}->{field}_count"
            # `foo.size` on a local already holding a count (from a `val N =
            # X?.size ?: 0` elsewhere) -- rare, kept for completeness.
            if recv["kind"] == "ident":
                s = scope
                while s is not None:
                    if recv["name"] in s.locals and s.locals[recv["name"]][0] == "count_alias":
                        owner, field = s.locals[recv["name"]][1], s.locals[recv["name"]][2]
                        return f"{owner}->{field}_count"
                    s = s.parent
        path = resolve_path(ast, scope)
        if path is not None:
            target_scope, segs = path
            ptr = struct_ptr_expr(target_scope, ctx)
            cname = flatten_field_name(segs)
            target_scope.add_field(cname, "int32_t")
            return f"{ptr}->{cname}"
        raise Unsupported(f"unmodeled member access: .{name}")

    if k == "index":
        recv_c = emit_expr(ast["recv"], scope, ctx)
        idx_c = emit_expr(ast["idx"], scope, ctx)
        return f"{recv_c}[{idx_c}]"

    if k == "call":
        return emit_call(ast, scope, ctx)

    if k == "neg":
        return f"(-{emit_expr(ast['val'], scope, ctx)})"

    if k == "not":
        return f"(!{emit_expr(ast['val'], scope, ctx)})"

    if k == "bin":
        op = ast["op"]
        cop = {
            "and": "&", "or": "|", "xor": "^", "shl": "<<", "shr": ">>", "ushr": ">>",
        }.get(op, op)
        l = emit_expr(ast["left"], scope, ctx)
        r = emit_expr(ast["right"], scope, ctx)
        if op == "ushr":
            return f"((int32_t)((uint32_t)({l}) >> ({r})))"
        return f"({l} {cop} {r})"

    if k == "ternary":
        c = emit_expr(ast["cond"], scope, ctx)
        t = emit_expr(ast["then"], scope, ctx)
        e = emit_expr(ast["els"], scope, ctx)
        return f"({c} ? {t} : {e})"

    if k == "elvis":
        # Only the "reference-or-default" shape is modeled: emit as a NULL
        # check. This is correct for pointer-typed lefts (strings) and is
        # rejected (Unsupported) for anything else -- see design note above
        # on why a numeric elvis is not safely representable without a
        # distinct null sentinel this generator does not model.
        left = ast["left"]
        if left["kind"] == "member" and left.get("safe"):
            raise Unsupported("safe-call member elvis not modeled generically")
        l = emit_expr(left, scope, ctx)
        r = emit_expr(ast["right"], scope, ctx)
        return f"({l} != NULL ? {l} : {r})"

    if k == "in_range":
        v = emit_expr(ast["val"], scope, ctx)
        lo = emit_expr(ast["lo"], scope, ctx)
        hi = emit_expr(ast["hi"], scope, ctx)
        return f"(({v}) >= ({lo}) && ({v}) <= ({hi}))"

    raise Unsupported(f"unmodeled expr kind: {k}")


def scope_for_owner(scope, owner_expr):
    # owner_expr is already a C pointer-expr string like "msg" or "elem";
    # this helper only needs to exist for the count_alias resolution path
    # above where we already have the target scope logically -- kept simple
    # by resolving through struct_ptr_expr's own naming convention.
    return scope


def struct_ptr_expr(target_scope, ctx):
    """The message pointer, which is `m` in BOTH directions.

    The old shape had `msg` for encoders and `out` for decoders because the
    parameter was const one way and not the other. A codec that runs in four
    directions cannot be const in any of them, so there is one name -- and the
    generated body for an Encoder.kt and a Decoder.kt now differ only in the
    order fields appear, never in their spelling.
    """
    if target_scope.parent is None:
        return "m"
    return "elem"


CALL_UNSUPPORTED = object()


def emit_call(ast, scope, ctx):
    name = ast["name"]
    recv = ast["recv"]
    args = ast["args"]

    if name in CAST_CALLS:
        # (Type) cast -- values are already the right C width.
        return emit_expr(recv, scope, ctx)

    if name == "coerceAtMost":
        a = emit_expr(recv, scope, ctx)
        b = emit_expr(args[0], scope, ctx)
        return f"({a} < {b} ? {a} : {b})"
    if name == "coerceAtLeast":
        a = emit_expr(recv, scope, ctx)
        b = emit_expr(args[0], scope, ctx)
        return f"({a} > {b} ? {a} : {b})"

    if name == "isNullOrEmpty":
        # only used in `if (!X.isNullOrEmpty())`; handled at the if-condition
        # level (see emit_cond), never reached as a bare expression normally.
        raise Unsupported("isNullOrEmpty outside an if-condition")

    if recv is None and name in ("message",):
        raise Unsupported("bare message() call")

    # `message.getX(i)` / `alias.getX(i)` indexed accessor -> array field.
    if recv is not None:
        target_scope = resolve_recv_scope(recv, scope)
        if target_scope is not None and len(args) == 1:
            field = c_escape_ident(deprefix_getter(name))
            idx_c = emit_expr(args[0], scope, ctx)
            target_scope.add_field(field, "int32_t")
            target_scope.array_fields.setdefault(field, True)
            ptr = struct_ptr_expr(target_scope, ctx)
            return f"{ptr}->{field}[{idx_c}]"

    raise Unsupported(f"unmodeled call: {name}(...)")


def leaf_string_fn(fn_kt):
    return fn_kt.startswith(("pjstr", "gjstr"))


# A field reference the RSPROT_* macros can take the address of: `m->field`,
# `elem->field`, either one subscripted, and nothing else. Deliberately a
# whitelist -- `m->a + 1` and `(int32_t)m->a` are both perfectly good C
# expressions that are not addressable, and admitting either would produce a
# codec that encodes correctly and reads into a temporary while decoding.
_LVALUE_RE = re.compile(r"^(?:m|elem)->[A-Za-z_][A-Za-z0-9_]*(?:\[[^\]]*\])?$")


def _is_lvalue_c(expr_c):
    return bool(_LVALUE_RE.match(expr_c))


# An integer literal, decimal or hex, optionally negated. Not a general
# constant-folder: `1 + 1` is arithmetic that happens to be constant, and
# admitting it would start down the road of evaluating Kotlin in the generator.
_INT_LITERAL_RE = re.compile(r"^-?(?:0[xX][0-9a-fA-F]+|\d+)$")


def _is_int_literal_c(expr_c):
    return bool(_INT_LITERAL_RE.match(expr_c.strip()))


# --- IR -> C statement emission ------------------------------------------------

class Emitter:
    def __init__(self, msg_name, ctx):
        self.msg_name = msg_name
        self.ctx = ctx  # "encode" | "decode"
        self.lines = []
        self.tmp_i = 0
        self.decl_locals = []  # (ctype, name) declared up front, C89-ish safety not required (C11 ok inline)
        # C field expressions already moved, in order. This is what makes the
        # branch rule checkable at generation time: a condition may only name
        # something in here. Names are the emitted C ("m->type"), not the
        # Kotlin, so an alias and its target compare equal.
        self.transferred = set()

    def emit(self, s, indent=1):
        self.lines.append(("    " * indent) + s)

    def mark_transferred(self, expr_c):
        self.transferred.add(expr_c)

    def new_scope_field(self, scope, ast_or_name, kt_fn=None):
        pass  # kept for symmetry; fields are registered where referenced.


def gen_ops(ops, scope, em, indent, ctx):
    for op in ops:
        gen_op(op, scope, em, indent, ctx)


def gen_op(op, scope, em, indent, ctx):
    kind = op["op"]

    if kind == "write":
        fn_kt = op["fn"]
        macro, ctype = wire_c_fn(fn_kt)
        expr_ast = op["expr"]

        # `buffer.p1(if (message.flag) 1 else 0)` IS a boolean field.
        #
        # RSProt spells a bool this way rather than calling pboolean, and the
        # naive reading is "a computed expression", which gets refused because a
        # ternary has no lvalue to decode into. But the wire form is exactly
        # what RSPROT_BOOL moves -- one byte, 0 or 1 -- and the field behind the
        # condition is a perfectly good lvalue. Recognising it turns a refusal
        # into the right codec.
        #
        # Only for plain `p1`/`g1`: rsprot_x_boolean is a plain byte, so an
        # Alt-transformed width-1 write would put a different byte on the wire
        # and this shortcut would be a lie.
        if fn_kt in ("p1", "g1") and expr_ast.get("kind") == "ternary":
            t, e = expr_ast.get("then"), expr_ast.get("els")
            if (t and e and t.get("kind") == "num" and e.get("kind") == "num"
                    and t.get("text") == "1" and e.get("text") == "0"):
                _pre_register(expr_ast["cond"], scope, "int32_t", ctx)
                cond_c = emit_expr(expr_ast["cond"], scope, ctx)
                if _is_lvalue_c(cond_c):
                    em.emit(f"RSPROT_BOOL(x, {cond_c});", indent)
                    em.mark_transferred(cond_c)
                    return
        # register the field if the expr is a direct field/member reference
        _pre_register(expr_ast, scope, ctype, ctx)
        expr_c = emit_expr(expr_ast, scope, ctx)
        # The macro takes the field as an LVALUE -- it needs its address to be
        # able to run in the decode direction, and it stringifies the same
        # expression for DESCRIBE's field name. A write whose Kotlin argument
        # is a computed expression rather than a plain field reference has no
        # lvalue to hand it, so it is refused here instead of being emitted as
        # something that would only work while encoding.
        if not _is_lvalue_c(expr_c):
            # A literal the layout pins (a format byte, a reserved zero) has no
            # field behind it, but it IS a wire field. RSPROT_CONST carries it:
            # written on encode, VERIFIED on decode. Anything else computed --
            # `m->a + 1`, a shifted pack -- still has no honest reading and is
            # refused, since only the encode direction could ever be right.
            if _is_int_literal_c(expr_c):
                if not macro.startswith("RSPROT_"):
                    raise Unsupported(f"{fn_kt} constant on a non-primitive")
                em.emit(f"RSPROT_CONST(x, {macro[len('RSPROT_'):]}, {expr_c});", indent)
                return
            raise Unsupported(
                f"{fn_kt} writes a computed expression `{expr_c}`, which has no "
                f"field to decode back into")
        em.emit(f"{macro}(x, {expr_c});", indent)
        em.mark_transferred(expr_c)
        return

    if kind == "read":
        fn_kt = op["fn"]
        macro, ctype = wire_c_fn(fn_kt)
        name = c_escape_ident(op["name"])
        scope.add_field(name, ctype)
        scope.locals[op["name"]] = ("field", struct_ptr_expr(scope, ctx), name)
        # Identical to the `write` line above. That is the property worth
        # noticing: a Decoder.kt and its matching Encoder.kt now generate the
        # same C, so a layout can no longer be right in one direction and wrong
        # in the other.
        em.emit(f"{macro}(x, {struct_ptr_expr(scope, ctx)}->{name});", indent)
        em.mark_transferred(f"{struct_ptr_expr(scope, ctx)}->{name}")
        return

    if kind == "readbool":
        # `gX(buf) == N` -- a read whose value is compared to a constant. The
        # comparison is not a wire operation, so the field stores the raw value
        # and the predicate belongs to the caller. Refused rather than emitted:
        # writing back a bool would encode 0/1 where the wire carries N.
        raise Unsupported(
            f"{op['fn']} compared to {op['true_val']} on read: the encode "
            f"direction cannot reconstruct the original value from a bool")

    if kind == "local":
        # Restrict to the patterns declared safe in the module docstring:
        # a pure alias to a field/local, or a self-contained computed value
        # with no `message.`/element field reference beyond a single leaf
        # already covered by the field registry.
        expr_ast = op["expr"]
        name = op["name"]
        # count-alias idiom: `val N = FIELD?.size ?: 0` / `FIELD.size`
        alias = _match_count_alias(expr_ast, scope)
        if alias is not None:
            owner, field = alias
            scope.locals[name] = ("count_alias", owner, field)
            return
        # pure passthrough alias: `val N = message.FIELD` (possibly a
        # compound value accessed further via N.subfield later -- deferred
        # to a path, not a scalar field, until something actually reads N as
        # a value) or `val N = someLocal`/`val N = message.FIELD.sub`.
        if expr_ast["kind"] in ("ident", "member"):
            path = resolve_path(expr_ast, scope)
            if path is not None:
                target_scope, segs = path
                scope.locals[name] = ("alias_path", target_scope, segs)
                return
            if expr_ast["kind"] == "ident":
                info = _all_local_names(scope).get(expr_ast["name"])
                if info is not None:
                    scope.locals[name] = info
                    return
        # otherwise: emit as a genuine computed C local (int32_t/bool by shape)
        ctype = "bool" if expr_ast["kind"] in ("bin",) and expr_ast.get("op") in (
            "==", "!=", "<", ">", "<=", ">=", "&&", "||") else "int32_t"
        expr_c = emit_expr(expr_ast, scope, ctx)
        cname = c_escape_ident(name)
        em.emit(f"{ctype} {cname} = {expr_c};", indent)
        scope.locals[name] = ("local", ctype)
        return

    if kind == "assign":
        name = op["name"]
        expr_c = emit_expr(op["expr"], scope, ctx)
        em.emit(f"{c_escape_ident(name)} = {expr_c};", indent)
        return

    if kind == "if":
        # The null-presence-flag idiom, rewritten into something decodable.
        #
        #   if (X != null) { p1(1); <write X> } else { p1(0) }
        #
        # is a wire layout with a flag BYTE in it, but RSProt writes that byte
        # by testing the field -- which only an encoder can do. Decoding has to
        # read the flag first and branch on THAT. So the flag becomes a real
        # transferred field and the branch moves onto it. Same bytes, and now
        # both directions can produce them.
        rewritten = _try_null_flag_idiom(op, scope, em, indent, ctx)
        if rewritten:
            return

        # THE ONE RULE (include/rsprot_exec.h): a codec may branch only on a
        # field it has already transferred. A predicate over a field that has
        # not been read yet works while encoding and reads uninitialised memory
        # while decoding -- a packet that encodes correctly and decodes garbage,
        # with nothing wrong at the frame level. Enforced here, at generation,
        # rather than left for RSPROT_BRANCH to catch under DESCRIBE, because a
        # generator that emits it has already shipped the bug.
        untransferred = _cond_untransferred_fields(op["cond"], scope, em, ctx)
        if untransferred:
            raise Unsupported(
                f"branches on {', '.join(sorted(untransferred))}, which "
                f"{'has' if len(untransferred) == 1 else 'have'} not been transferred yet")

        cond_c = emit_cond(op["cond"], scope, ctx)
        em.emit(f"if ({cond_c}) {{", indent)
        gen_ops(op["then"], scope, em, indent + 1, ctx)
        if op["else"]:
            em.emit("} else {", indent)
            gen_ops(op["else"], scope, em, indent + 1, ctx)
        em.emit("}", indent)
        return

    if kind == "for_range":
        var = c_escape_ident(op["var"])
        lo_c = emit_expr(op["lo"], scope, ctx)
        hi_c = emit_expr(op["hi"], scope, ctx)
        scope.locals[op["var"]] = ("loopvar",)
        if op.get("descending"):
            em.emit(f"for (int32_t {var} = {hi_c}; {var} >= {lo_c}; {var}--) {{", indent)
        else:
            cmp = "<=" if op["inclusive"] else "<"
            em.emit(f"for (int32_t {var} = {lo_c}; {var} {cmp} {hi_c}; {var}++) {{", indent)
        gen_ops(op["body"], scope, em, indent + 1, ctx)
        del scope.locals[op["var"]]
        em.emit("}", indent)
        return

    if kind == "for_each":
        path = resolve_path(op["iter"], scope)
        if path is None or not path[1]:
            raise Unsupported("for-each over an unresolved iterable")
        owner, segs = path
        field = flatten_field_name(segs)
        # NOT c_escape_ident'd. `scope.struct_name` is already a C identifier,
        # and running it through the snake_case fold here produced
        # `Rsprot_rsprot_msg_if_resync_v2_eventsElem` for the typedef while
        # render_struct spelled the field's type `Rsprot_RsprotMsg_IfResyncV2_
        # eventsElem` from the raw name -- two names for one struct, and a file
        # that does not compile. `field` arrives already escaped by
        # flatten_field_name.
        elem_struct = f"Rsprot_{scope.struct_name}_{field}Elem"
        child = Scope(elem_struct, parent=scope, recv_name=op["var"])
        owner.add_field(field, "int32_t")
        owner.array_fields[field] = child
        owner.count_field_for[field] = f"{field}_count"
        elem_var = c_escape_ident(op["var"])
        ptr = struct_ptr_expr(owner, ctx)

        # THE BRANCH RULE, in loop form.
        #
        # The loop is bounded by `<field>_count`, and that only means anything
        # while decoding if the count came off the wire FIRST. RSProt's trailing
        # arrays are often written with no count at all -- the reader consumes
        # to the end of a VAR_SHORT payload -- and a generated loop over an
        # untransferred count reads an uninitialised bound and then walks an
        # array that was never allocated.
        #
        # Refused rather than emitted, same as the `if` case: encoding would
        # work (the caller set the count) and decoding would be memory
        # corruption, which is the worst possible split.
        count_expr = f"{ptr}->{field}_count"
        if count_expr not in em.transferred:
            raise Unsupported(
                f"array `{field}` is bounded by {field}_count, which is never "
                f"transferred -- decoding cannot know how many to read")

        em.emit(
            f"for (int32_t rs_i_{elem_var} = 0; rs_i_{elem_var} < {count_expr}; rs_i_{elem_var}++) {{",
            indent,
        )
        # No `const`: one codec runs in both directions, so an element the
        # encoder reads is the same element the decoder writes.
        em.emit(f"{elem_struct} *elem = &{ptr}->{field}[rs_i_{elem_var}];", indent + 1)
        scope.locals[op["var"]] = ("elem_alias", child)
        gen_ops(op["body"], child, em, indent + 1, ctx)
        del scope.locals[op["var"]]
        em.emit("}", indent)
        return

    if kind in ("return", "return_void", "ignore", "continue", "break"):
        if kind == "continue":
            em.emit("continue;", indent)
        elif kind == "break":
            em.emit("break;", indent)
        # return: for decoders, a `read`/`readbool` op already wrote its
        # field straight to `out->name`. But an arg here can also be a
        # COMPUTED local (e.g. `val rightClick = packed and 0x1 != 0` --
        # EventMouseClickV1Decoder derives its actual output fields from a
        # raw `packed` short this way, not by reading them directly). Such an
        # arg has no home in the struct yet, so materialize one: add the
        # field under the local's own name and copy the already-computed C
        # local into it. A field/loopvar arg needs nothing further -- it's
        # already where it belongs.
        if kind == "return":
            for a in op["args"]:
                _ = emit_expr(a, scope, ctx)  # validates every arg resolves
                if a["kind"] != "ident":
                    continue
                info = scope.locals.get(a["name"]) or _all_local_names(scope).get(a["name"])
                if info is not None and info[0] == "local":
                    cname = c_escape_ident(a["name"])
                    ctype = info[1]
                    scope.add_field(cname, ctype)
                    ptr = struct_ptr_expr(scope, ctx)
                    em.emit(f"{ptr}->{cname} = {cname};", indent)
        return

    raise Unsupported(f"unmodeled IR op: {kind}")


def _collect_field_refs(ast, scope, ctx, out):
    """Every `m->field` / `elem->field` a condition names, as emitted C."""
    if not isinstance(ast, dict):
        return
    k = ast.get("kind")
    if k in ("member", "ident"):
        path = resolve_path(ast, scope)
        if path is not None and path[1]:
            owner, segs = path
            out.add(f"{struct_ptr_expr(owner, ctx)}->{flatten_field_name(segs)}")
            return
    for key in ("recv", "val", "left", "right", "cond", "then", "else"):
        v = ast.get(key)
        if isinstance(v, dict):
            _collect_field_refs(v, scope, ctx, out)
    for v in ast.get("args") or []:
        _collect_field_refs(v, scope, ctx, out)


def _cond_untransferred_fields(cond_ast, scope, em, ctx):
    refs = set()
    _collect_field_refs(cond_ast, scope, ctx, refs)

    def satisfied(r):
        if r in em.transferred or r.endswith("_count"):
            return True
        # A guard over an ARRAY whose count is already on the wire.
        #
        #     buffer.p1Alt1(opCount)              <- count transferred
        #     if (!ops.isNullOrEmpty()) { for (...) }
        #
        # The predicate names `ops`, which is never itself transferred, so the
        # naive check calls this a branch-rule violation. It is not: the guard
        # is redundant with the loop bound, and the bound came off the wire.
        # Decoding reads the count and loops that many times, which is exactly
        # what the guard was protecting.
        return f"{r}_count" in em.transferred

    return {r for r in refs if not satisfied(r)}


def _try_null_flag_idiom(op, scope, em, indent, ctx):
    """`if (X != null) { pN(1); ... } else { pN(0) }` -> a transferred flag.

    Returns True when it rewrote the branch. Deliberately narrow: the flag
    constants must be exactly 1 and 0, and the then-branch must open with the
    write. Anything else is left to the branch-rule check, which refuses it --
    a looser match here would be this generator guessing at a layout.
    """
    cond = op.get("cond") or {}
    if cond.get("kind") != "bin" or cond.get("op") != "!=":
        return False
    lhs, rhs = cond.get("left"), cond.get("right")
    if not (isinstance(rhs, dict) and rhs.get("kind") == "null"):
        return False
    path = resolve_path(lhs, scope)
    if path is None or not path[1]:
        return False
    owner, segs = path
    field = flatten_field_name(segs)

    then_ops, else_ops = op.get("then") or [], op.get("else") or []
    if not then_ops or then_ops[0].get("op") != "write":
        return False
    w = then_ops[0]
    if not (len(else_ops) == 1 and else_ops[0].get("op") == "write"):
        return False
    e = else_ops[0]
    if w["fn"] != e["fn"]:
        return False
    if not (w["expr"].get("kind") == "num" and e["expr"].get("kind") == "num"):
        return False
    if w["expr"].get("text") != "1" or e["expr"].get("text") != "0":
        return False

    macro, _ctype = wire_c_fn(w["fn"])
    flag = f"{field}_present"
    owner.add_field(flag, "int32_t")
    ptr = struct_ptr_expr(owner, ctx)
    em.emit(f"{macro}(x, {ptr}->{flag});", indent)
    em.mark_transferred(f"{ptr}->{flag}")
    em.emit(f"if (RSPROT_BRANCH(x, {ptr}->{flag})) {{", indent)
    gen_ops(then_ops[1:], scope, em, indent + 1, ctx)
    em.emit("}", indent)
    return True


def emit_cond(ast, scope, ctx):
    # special-case `!X.isNullOrEmpty()` / `X.isNullOrEmpty()` at condition
    # position, since isNullOrEmpty() cannot be modeled as a value expr.
    def scan(a, negate):
        if a["kind"] == "not":
            return scan(a["val"], not negate)
        if a["kind"] == "call" and a["name"] == "isNullOrEmpty" and a["recv"] is not None:
            alias = _match_count_alias({"kind": "member", "recv": a["recv"], "name": "size"}, scope) \
                if a["recv"]["kind"] == "ident" else None
            cnt = None
            if a["recv"]["kind"] == "ident" and a["recv"]["name"] in _all_local_names(scope):
                info = scope_lookup(scope, a["recv"]["name"])
                if info[0] == "count_alias":
                    owner, field = info[1], info[2]
                    cnt = f"{struct_ptr_expr(scope, ctx)}->{field}_count"
            if cnt is None:
                # `val ops = message.ops` binds an alias_path, not a count
                # alias, so the lookup above misses it -- but the array's own
                # `<field>_count` is exactly what "is it empty" asks about.
                # Resolving through the path is what lets
                # `if (!ops.isNullOrEmpty())` compile to a count test.
                path = resolve_path(a["recv"], scope)
                if path is not None and path[1]:
                    owner, segs = path
                    fname = flatten_field_name(segs)
                    if fname in owner.array_fields:
                        cnt = f"{struct_ptr_expr(owner, ctx)}->{fname}_count"
                if cnt is None:
                    raise Unsupported("isNullOrEmpty on unresolved receiver")
            expr = f"({cnt} > 0)"
            return f"(!{expr})" if negate else expr
        return None

    top = scan(ast, False)
    if top is not None:
        return top
    return emit_expr(ast, scope, ctx)


def _pre_register(ast, scope, ctype, ctx):
    """Best-effort: if `ast` is directly (optionally cast-wrapped) a field or
    local reference, register/override its declared ctype from the wire call
    that touches it -- gives strings/booleans/int64 the right struct type
    instead of the emit_expr default of int32_t."""
    a = ast
    while True:
        if a["kind"] == "call" and a["name"] in CAST_CALLS:
            a = a["recv"]
        elif a["kind"] == "elvis":
            a = a["left"]
        else:
            break
    if a["kind"] == "member" and a["name"] == "size":
        return  # array-count field: emit_expr's own `.size` handling owns it
    if a["kind"] in ("member", "ident"):
        path = resolve_path(a, scope)
        if path is not None and path[1]:
            target_scope, segs = path
            target_scope.fields[flatten_field_name(segs)] = ctype
            return
    if a["kind"] == "ident":
        info = _all_local_names(scope).get(a["name"])
        if info and info[0] == "field":
            owner_ptr, cname = info[1], info[2]
            s = scope
            while s is not None:
                if struct_ptr_expr(s, ctx) == owner_ptr and cname in s.fields:
                    s.fields[cname] = ctype
                    break
                s = s.parent


def _all_local_names(scope):
    out = {}
    s = scope
    while s is not None:
        for k, v in s.locals.items():
            out.setdefault(k, v)
        s = s.parent
    return out


def scope_lookup(scope, name):
    s = scope
    while s is not None:
        if name in s.locals:
            return s.locals[name]
        s = s.parent
    raise Unsupported(f"unresolved local: {name}")


def _match_count_alias(expr_ast, scope):
    """Recognize `FIELD?.size ?: 0`, `FIELD.size`, `FIELD?.size` (FIELD may
    itself be a dotted path or an alias to one). Returns (owner_ptr_expr,
    field_name) or None."""
    a = expr_ast
    if a["kind"] == "elvis":
        r = a["right"]
        if not (r["kind"] == "num" and r["text"] in ("0",)):
            return None
        a = a["left"]
    if a["kind"] != "member" or a["name"] != "size":
        return None
    path = resolve_path(a["recv"], scope)
    if path is None or not path[1]:
        return None
    target_scope, segs = path
    field = flatten_field_name(segs)
    target_scope.array_fields.setdefault(field, True)
    return (struct_ptr_expr(target_scope, "encode"), field)


# --- struct + function rendering -----------------------------------------------

def collect_nested_scopes(scope, out):
    for name, val in scope.array_fields.items():
        if isinstance(val, Scope):
            collect_nested_scopes(val, out)
            out.append(val)


def render_struct(scope, struct_c_name):
    lines = [f"typedef struct {struct_c_name} {{"]
    for name, ctype in scope.fields.items():
        av = scope.array_fields.get(name)
        if av is True:
            lines.append(f"\t{ctype} *{name};")
            lines.append(f"\tint32_t {name}_count;")
        elif isinstance(av, Scope):
            elem_name = f"Rsprot_{struct_c_name}_{name}Elem"
            # Not const: one codec runs both directions, so the decoder writes
            # through this pointer.
            lines.append(f"\t{elem_name} *{name};")
            lines.append(f"\tint32_t {name}_count;")
        else:
            lines.append(f"\t{ctype} {name};")
    lines.append(f"}} {struct_c_name};")
    return "\n".join(lines)


def render_all_structs(scope, msg_type):
    """Nested element structs first (dependency order), then the top struct."""
    nested = []
    collect_nested_scopes(scope, nested)
    out = []
    for child in nested:
        # child.struct_name was set to the placeholder element-struct name
        # already at construction time in gen_op's for_each handling.
        out.append(render_struct(child, child.struct_name))
    out.append(render_struct(scope, f"RsprotMsg_{msg_type}"))
    return "\n\n".join(out)


def build_message(info, msg_type, fn_suffix):
    """`fn_suffix` names the generated FUNCTION (derived from the Kotlin class
    name, e.g. "OpLoc2V2") -- distinct from `msg_type`, which names the
    STRUCT. Several sibling classes (OpLoc1V2Decoder..OpLoc5V2Decoder) target
    the SAME message type but read its fields in DIFFERENT wire orders/Alt-
    modes per op slot -- confirmed by diffing RSProt's own sources, not
    assumed -- so the struct is shared while every class still gets its own
    function."""
    ctx = "decode" if info["is_decoder"] else "encode"
    struct_c_name = f"RsprotMsg_{msg_type}"
    scope = Scope(struct_c_name)
    em = Emitter(msg_type, ctx)

    if info.get("no_body"):
        # A zero-payload packet. Still a real codec -- the framing layer writes
        # its opcode and a zero length -- so it gets a function like any other.
        struct_text = (
            f"typedef struct {struct_c_name} {{\n"
            f"\t/* No payload at any version. The member exists because C has no\n"
            f"\t * empty struct; nothing reads it. */\n"
            f"\tint32_t _unused;\n"
            f"}} {struct_c_name};"
        )
        return struct_text, "", scope

    chunks = split_top_level(info["body"])
    ops = parse_statements(chunks, info["is_decoder"])
    gen_ops(ops, scope, em, 1, ctx)

    struct_text = render_all_structs(scope, msg_type)
    # The body only; the caller wraps it in a per-version signature, because
    # one message type has several layouts and each needs its own function.
    body_text = "\n".join(em.lines) if em.lines else ""
    return struct_text, body_text, scope


# --- driver --------------------------------------------------------------------

def iter_kt_files(base_dir):
    for root, dirs, files in os.walk(base_dir):
        rel = os.path.relpath(root, base_dir)
        parts = rel.split(os.sep)
        if any(p in EXCLUDE_DIR_PARTS for p in parts):
            dirs[:] = []
            continue
        for f in sorted(files):
            if f.endswith(".kt") and not f.endswith("Test.kt"):
                yield os.path.join(root, f)


def class_fn_suffix(class_name):
    for suf in ("Encoder", "Decoder"):
        if class_name.endswith(suf):
            return class_name[: -len(suf)]
    return class_name


def process_dir(base_dir, label):
    generated = []
    skipped = []
    seen_structs = {}  # msg_type -> (field_name_set, struct_text, rel) of the first occurrence
    for path in iter_kt_files(base_dir):
        rel = os.path.relpath(path, base_dir)
        try:
            info = parse_file(path)
        except Exception as e:  # noqa: BLE001 - report and move on
            skipped.append((rel, f"file parse error: {e}"))
            continue
        if info is None:
            continue  # not a recognizable Encoder/Decoder class -- e.g. a helper file
        msg_type = info["msg_type"]
        fn_suffix = class_fn_suffix(info["class"])
        try:
            struct_text, fn_text, scope = build_message(info, msg_type, fn_suffix)
        except Unsupported as e:
            skipped.append((rel, str(e)))
            continue
        except Exception as e:  # noqa: BLE001
            skipped.append((rel, f"internal error: {type(e).__name__}: {e}"))
            continue

        # Several classes can share one message TYPE -- e.g. OpLoc1V2Decoder
        # .. OpLoc5V2Decoder all decode `OpLocV2`, but a diff against RSProt's
        # own sources shows each op-slot reads the same FIELDS in a DIFFERENT
        # wire order/Alt-mode. So: one struct per message type (its field set
        # only, not the read order, has to agree across siblings), but every
        # class still gets its own function. A field-set mismatch is a real
        # conflict -- two classes claiming the same type but disagreeing on
        # what it holds -- and is reported rather than silently resolved.
        emit_struct = True
        field_set = frozenset(scope.fields.keys())
        if msg_type in seen_structs:
            prev_fields, prev_struct, prev_rel = seen_structs[msg_type]
            if prev_fields != field_set:
                skipped.append(
                    (rel, f"message type {msg_type!r} field set disagrees with {prev_rel}")
                )
                continue
            emit_struct = False
        else:
            seen_structs[msg_type] = (field_set, struct_text, rel)

        generated.append(
            {
                "rel": rel,
                "class": info["class"],
                "msg_type": msg_type,
                "fn_suffix": fn_suffix,
                "prot": info["prot"],
                "is_decoder": info["is_decoder"],
                "struct": struct_text if emit_struct else None,
                "fn": fn_text,
                "scope": scope,
                "flat": len(scope.array_fields) == 0,
            }
        )
    return generated, skipped




# --- the layout-version ledger -------------------------------------------------
#
# gen/packet_versions.txt is the AUTHORITY for version numbers. This generator
# reads it and never allocates one: the ledger is append-only (see
# tools/rsprot_version_ledger.py) precisely so that generated C, hand-written
# call sites and tests can hard-code these numbers, and a generator that minted
# its own would defeat that the first time a revision was vendored.
#
# Regenerate the ledger BEFORE running this tool if RSProt's source moved:
#
#     python3 tools/rsprot_version_ledger.py
#     python3 tools/rsprot_gen_codec.py

LEDGER_PATH = os.path.join(REPO_ROOT, "3rd", "rsprot", "gen", "packet_versions.txt")
PACKETS_DIR = os.path.join(REPO_ROOT, "3rd", "rsprot", "packets")

REV_LO, REV_HI = 221, 239

# Packets whose files are HAND-WRITTEN and must not be overwritten.
#
# IF_OPENTOP is the documented pattern every generated file is shaped against
# (see 3rd/rsprot/packets/if_opentop.h), so it carries teaching comments a
# generator cannot produce and would erase. Keeping it hand-written costs
# nothing in confidence: generating it and running src/net/exec/test/
# pkt_packets_test.c against the OUTPUT passes the same assertions the
# hand-written file does -- the four layouts, the thirteen rows, the per-byte
# patterns and the alias/table agreement. Re-check that equivalence with:
#
#     python3 tools/rsprot_gen_codec.py --only IF_OPENTOP --out /tmp/pkts
#     diff <(sed 's/[[:space:]]*$//' /tmp/pkts/if_opentop.c) ...
#
# A name here that stops matching a hand-written file is reported, not ignored:
# silently skipping a packet nobody wrote is how a gap becomes permanent.
HAND_WRITTEN_PROTS = {"IF_OPENTOP"}


def load_ledger(path=LEDGER_PATH):
    """(class, dir) -> {revision: version}.

    Columns are CLASS, DIR, MODULE, VER, HASH, REVS -- tab separated, '#'
    comments, one row per distinct layout with REVS as comma-separated ranges.
    """
    if not os.path.exists(path):
        sys.exit(
            f"missing {path}\n"
            f"run: python3 tools/rsprot_version_ledger.py")
    table = {}
    with open(path) as fh:
        for line in fh:
            line = line.rstrip("\n")
            if not line or line.startswith("#") or line.startswith("CLASS\t"):
                continue
            parts = line.split("\t")
            if len(parts) != 6:
                continue
            cls, direction, _module, ver, _hash, revs = parts
            if revs.strip() == "RETIRED":
                continue
            per_rev = table.setdefault((cls, direction), {})
            for chunk in revs.split(","):
                chunk = chunk.strip()
                if not chunk:
                    continue
                if "-" in chunk:
                    lo, hi = chunk.split("-", 1)
                    rng = range(int(lo), int(hi) + 1)
                else:
                    rng = (int(chunk),)
                for r in rng:
                    if r in per_rev and per_rev[r] != int(ver):
                        sys.exit(
                            f"ledger conflict: {cls}/{direction} rev {r} claimed by "
                            f"versions {per_rev[r]} and {ver}")
                    per_rev[r] = int(ver)
    return table


# --- naming --------------------------------------------------------------------


def prot_to_snake(prot):
    """Prot name -> the packet file's basename. IF_OPENTOP -> if_opentop."""
    return re.sub(r"[^a-z0-9]+", "_", prot.lower()).strip("_")


def snake_to_pascal(snake):
    return "".join(p[:1].upper() + p[1:] for p in snake.split("_") if p)


def compress_ranges(revs):
    """[221,222,224] -> [(221,222),(224,224)]."""
    out = []
    for r in sorted(revs):
        if out and r == out[-1][1] + 1:
            out[-1][1] = r
        else:
            out.append([r, r])
    return [tuple(x) for x in out]


def fmt_ranges(ranges):
    return ", ".join(f"{lo}" if lo == hi else f"{lo}-{hi}" for lo, hi in ranges)


# --- per-packet file emission --------------------------------------------------

PACKET_H = '''#ifndef RSPROT_PACKET_{guard}_H
#define RSPROT_PACKET_{guard}_H

/*
 * {prot} -- {dir_prose}.
 *
 * GENERATED by tools/rsprot_gen_codec.py -- do not edit. Hand edits are lost on
 * the next run; change the generator, or the RSProt source it reads.
 *
 * Shape and rationale: 3rd/rsprot/packets/if_opentop.h, which is hand-written
 * and is the pattern this file is generated into. Read that one first.
 *
 * RSProt class : {cls}
 * Layouts      : {nver} distinct, over {nrun} contiguous revision run(s)
 * Version nos. : from gen/packet_versions.txt (append-only; never allocated
 *                here). Cross-check with:
 *                    grep '^{cls}\\t{ledger_dir}\\t' 3rd/rsprot/gen/packet_versions.txt
 */

#include "../include/rsprot_exec.h"

/**
 * One struct for every version -- the union of all their fields.
 *
 * A field a version does not carry is simply not transferred, which is what
 * keeps the layer above revision-free: fill the struct, and the wire picks the
 * codec.
 */
{struct}

{decls}
/* Per-revision aliases: `packet_{snake}_rev239_{dir}` is greppable, which is the
 * point -- "what does 239 do for {prot}" without reading a table. */
{aliases}

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_{snake}_{dir}[];
extern const int rsprot_{snake}_{dir}_count;

#endif
'''

PACKET_C = '''/* {prot}. GENERATED by tools/rsprot_gen_codec.py -- do not edit. */
#include "{snake}.h"

/*
 * Transcribed from RSProt:
 *   protocol/osrs-<N>/osrs-<N>-desktop/src/main/kotlin/net/rsprot/protocol/
 *       game/{sub}/codec/{rel}
 *
 * One function per layout, running in whichever direction the executor is in --
 * so a server's writer and a client's reader are the same transcription and
 * cannot drift apart.
 */

{bodies}
/*
 * {nrun} row(s) for {nver} layout(s), complete over revisions {revlo}-{revhi}.
 * A complete table makes rsprot_version_pick's NULL mean exactly one thing: a
 * revision outside that span. An incomplete one would conflate "not
 * transcribed" with "no such packet", and those are different facts.
 */
const RsprotVersionRange rsprot_{snake}_{dir}[] = {{
{rows}
}};

const int rsprot_{snake}_{dir}_count =
    (int)(sizeof(rsprot_{snake}_{dir}) / sizeof(rsprot_{snake}_{dir}[0]));
'''


def emit_packet_files(pkt, out_dir):
    snake = pkt["snake"]
    direction = pkt["dir"]          # "out" | "in"
    msg_struct = pkt["msg_struct"]
    versions = pkt["versions"]      # {ver: {"body": str, "revs": [..]}}
    rev_to_ver = pkt["rev_to_ver"]

    decls = []
    bodies = []
    for ver in sorted(versions):
        info = versions[ver]
        rngs = fmt_ranges(compress_ranges(info["revs"]))
        fn = f"packet_{snake}_v{ver}_{direction}"
        decls.append(f"/* v{ver} -- revs {rngs}. */")
        decls.append(f"void {fn}(RsprotExec *x, {msg_struct} *m);")
        body = info["body"] or f"    (void)x;\n    (void)m;"
        bodies.append(f"/* v{ver} -- revs {rngs}. */\nvoid\n{fn}(RsprotExec *x, {msg_struct} *m)\n{{\n{body}\n}}\n")

    aliases = []
    for rev in range(REV_LO, REV_HI + 1):
        ver = rev_to_ver.get(rev)
        if ver is None:
            continue
        aliases.append(
            f"#define packet_{snake}_rev{rev}_{direction} packet_{snake}_v{ver}_{direction}")

    rows = []
    for lo, hi in compress_ranges(rev_to_ver.keys()):
        # a compressed run may still span two versions; split on version change
        cur = rev_to_ver[lo]
        start = lo
        for r in range(lo, hi + 1):
            if rev_to_ver[r] != cur:
                rows.append((start, r - 1, cur))
                cur = rev_to_ver[r]
                start = r
        rows.append((start, hi, cur))

    row_text = "\n".join(
        f"    {{ {lo}, {hi}, (RsprotCodecFn)packet_{snake}_v{ver}_{direction} }},"
        for lo, hi, ver in rows)

    h = PACKET_H.format(
        guard=snake.upper(),
        prot=pkt["prot"],
        dir_prose="server -> client" if direction == "out" else "client -> server",
        cls=pkt["cls"],
        ledger_dir=direction,
        nver=len(versions),
        nrun=len(rows),
        struct=pkt["struct"],
        decls="\n".join(decls) + "\n",
        aliases="\n".join(aliases),
        snake=snake,
        dir=direction,
    )
    c = PACKET_C.format(
        prot=pkt["prot"],
        snake=snake,
        sub="outgoing" if direction == "out" else "incoming",
        rel=pkt["rel"],
        bodies="\n".join(bodies),
        nrun=len(rows),
        nver=len(versions),
        revlo=REV_LO,
        revhi=REV_HI,
        rows=row_text,
        dir=direction,
    )
    with open(os.path.join(out_dir, f"{snake}.h"), "w") as fh:
        fh.write(h)
    with open(os.path.join(out_dir, f"{snake}.c"), "w") as fh:
        fh.write(c)


UNITY_HEADER = '''/*
 * rsprot packet-codec unity anchor -- every file under packets/, one TU.
 *
 * GENERATED by tools/rsprot_gen_codec.py from the contents of packets/.
 * Do not hand-edit: add a packet by generating it, not by adding a line here.
 *
 * Separate from rsprot_unity.c on purpose. That anchor is the *library*:
 * buffer, crypto, compression, all self-contained and linkable with no host,
 * which is what lets 3rd/rsprot/makefile build and test it standalone. The
 * codecs here are not self-contained -- they speak the vocabulary declared in
 * include/rsprot_exec.h and implemented host-side in src/net/exec/pkt_exec.c,
 * so a TU holding them only links inside a project that supplies an executor.
 * Folding them into rsprot_unity.c would make the library's own test build
 * depend on the host, which is the dependency this split exists to prevent.
 *
 * One TU rather than one object per packet because there will be ~200 of these
 * and they are small; the compile is dominated by the header, not the bodies.
 */

// clang-format off
'''


def write_packets_unity(pkt_dir):
    names = sorted(
        f[:-2] for f in os.listdir(pkt_dir)
        if f.endswith(".c") and os.path.exists(os.path.join(pkt_dir, f[:-2] + ".h")))
    body = "".join(f'#include "packets/{n}.c"\n' for n in names)
    path = os.path.join(os.path.dirname(pkt_dir.rstrip("/")), "rsprot_packets_unity.c")
    with open(path, "w") as fh:
        fh.write(UNITY_HEADER + body + "// clang-format on\n")
    return path


# --- driver --------------------------------------------------------------------


def collect_revision(rev, ledger):
    """Parse one revision's codec tree. Returns {(cls, dir): record}."""
    desktop = os.path.join(
        RSPROT,
        f"protocol/osrs-{rev}/osrs-{rev}-desktop/src/main/kotlin/net/rsprot/protocol/game")
    out = {}
    skipped = []
    for sub, direction in (("outgoing/codec", "out"), ("incoming/codec", "in")):
        base = os.path.join(desktop, sub)
        if not os.path.isdir(base):
            continue
        gen, skip = process_dir(base, direction)
        for g in gen:
            out[(g["class"], direction)] = g
        skipped.extend((direction, rel, why) for rel, why in skip)
    return out, skipped


def main():
    ap = argparse.ArgumentParser(
        description="Generate 3rd/rsprot/packets/*.{h,c} from RSProt's Kotlin.")
    ap.add_argument(
        "--only", default=None,
        help="comma-separated prot names to emit (e.g. IF_OPENSUB,MESSAGE_GAME). "
             "Omit to emit every packet that generates cleanly.")
    ap.add_argument("--out", default=PACKETS_DIR)
    ap.add_argument(
        "--status", default=os.path.join(REPO_ROOT, "3rd", "rsprot", "gen", "codec_status.txt"))
    args = ap.parse_args()

    only = set(s.strip().upper() for s in args.only.split(",")) if args.only else None
    ledger = load_ledger()

    # Parse every vendored revision once.
    per_rev = {}
    all_skips = []
    for rev in range(REV_LO, REV_HI + 1):
        recs, skips = collect_revision(rev, ledger)
        per_rev[rev] = recs
        all_skips.extend((rev, d, rel, why) for d, rel, why in skips)
        print(f"  rev {rev}: {len(recs)} codecs parsed, {len(skips)} skipped", file=sys.stderr)

    # Group by (class, direction) across revisions.
    classes = {}
    for rev, recs in per_rev.items():
        for key, g in recs.items():
            classes.setdefault(key, {})[rev] = g

    os.makedirs(args.out, exist_ok=True)
    emitted, refused = [], []

    for (cls, direction), by_rev in sorted(classes.items()):
        prot = next((g["prot"] for g in by_rev.values() if g["prot"]), None)
        if not prot:
            refused.append((cls, direction, "no prot name in any revision"))
            continue
        if only is not None and prot.upper() not in only:
            continue
        if prot.upper() in HAND_WRITTEN_PROTS:
            snake = prot_to_snake(prot)
            existing = os.path.join(args.out, f"{snake}.c")
            if os.path.exists(existing):
                refused.append((cls, direction, "hand-written -- see HAND_WRITTEN_PROTS"))
                continue
            refused.append(
                (cls, direction,
                 f"listed hand-written but {existing} does not exist -- either write it "
                 f"or drop it from HAND_WRITTEN_PROTS"))
            continue

        led = ledger.get((cls, direction))
        if led is None:
            refused.append((cls, direction, "no ledger rows -- rerun rsprot_version_ledger.py"))
            continue

        # Every revision that generated cleanly AND has a ledger version.
        rev_to_ver, versions = {}, {}
        conflict = None
        for rev in sorted(by_rev):
            ver = led.get(rev)
            if ver is None:
                continue
            body = by_rev[rev]["fn"]
            slot = versions.setdefault(ver, {"body": body, "revs": []})
            # The cross-check worth having: the ledger derives versions by
            # hashing RSProt's KOTLIN; this generator produces C. Two revisions
            # the ledger calls one layout must generate identical C. When they
            # do not, one of the two is wrong and guessing which would be the
            # exact failure this tool refuses to commit.
            if slot["body"] != body:
                conflict = (
                    f"ledger v{ver} spans revs {slot['revs']} and {rev}, but they "
                    f"generate different C")
                break
            slot["revs"].append(rev)
            rev_to_ver[rev] = ver
        if conflict:
            refused.append((cls, direction, conflict))
            continue
        if not rev_to_ver:
            refused.append((cls, direction, "no revision both parsed and had a ledger version"))
            continue

        # One struct per packet, holding the UNION of every version's fields --
        # which is the point of the design, not a compromise: the layer above
        # fills a message without knowing the revision, and a field a version
        # does not carry is simply not transferred. Requiring the versions to
        # agree instead would refuse every packet that ever gained a field,
        # which at 239 is most of the interesting ones (IF_OPENSUB has 7
        # distinct field sets across the vendored revisions).
        #
        # Ordering: ascending revision, first-seen wins. A field's position in
        # the struct has no wire meaning -- the codec names fields explicitly --
        # so this only has to be stable, and ascending revision makes a diff
        # between two generator runs track the source rather than dict order.
        flat = all(not by_rev[r]["scope"].array_fields for r in rev_to_ver)
        if flat:
            union = {}
            for rev in sorted(rev_to_ver):
                for fname, ctype in by_rev[rev]["scope"].fields.items():
                    if fname in union and union[fname] != ctype:
                        # Same name, two C types across revisions. Not
                        # mergeable, and picking one would silently truncate
                        # the other -- exactly the class of bug this refuses.
                        union[fname] = None
                    else:
                        union.setdefault(fname, ctype)
            bad = [f for f, t in union.items() if t is None]
            if bad:
                refused.append(
                    (cls, direction, f"field(s) {', '.join(bad)} change C type across revisions"))
                continue
            lines = [f"typedef struct {{"]
            if not union:
                # A zero-payload packet (SERVER_TICK_END, RESET_ANIMS). C has no
                # empty struct, so it gets a member nothing reads -- rather than
                # the packet being refused, because "this packet carries no
                # bytes" is a layout, not a gap.
                lines.append("\t/* No payload at any version. C has no empty")
                lines.append("\t * struct; nothing reads this. */")
                lines.append("\tint32_t _unused;")
            for fname, ctype in union.items():
                sep = "" if ctype.endswith("*") else " "
                lines.append(f"\t{ctype}{sep}{fname};")
            struct_text = "\n".join(lines) + "\n}} PLACEHOLDER;"
        else:
            # Arrays and nested element structs are REFUSED, not emitted.
            #
            # The union rule above would have to merge element structs too, and
            # the per-revision fallback does not survive the message-struct
            # rename: the element typedef keeps a `Rsprot_RsprotMsg_<X>_<f>Elem`
            # name that no longer matches its declaration, so the file does not
            # compile. Emitting that is worse than not emitting it — this
            # generator's whole contract is that a construct it cannot model is
            # skipped visibly rather than guessed at, and "does not compile" is
            # only a louder version of the same failure.
            #
            # Arrays/nested element structs. The union rule above is not modeled
            # for element structs, so these require every version's struct text
            # to agree; a disagreement is refused rather than resolved.
            structs = {by_rev[r]["struct"] for r in rev_to_ver if by_rev[r]["struct"]}
            if len(structs) > 1:
                refused.append(
                    (cls, direction,
                     f"{len(structs)} disagreeing struct shapes across versions "
                     f"(array/nested -- union not modeled)"))
                continue
            struct_text = structs.pop() if structs else None
        if struct_text is None:
            refused.append((cls, direction, "no struct produced"))
            continue

        snake = prot_to_snake(prot)
        # From the RSProt CLASS, not from the prot name: `IfOpenTopEncoder`
        # gives `MsgIfOpenTop`, where re-casing the snake would give
        # `MsgIfOpentop` and lose a word boundary the source already knows.
        msg_struct = f"Msg{class_fn_suffix(cls)}"
        msg_type = by_rev[max(rev_to_ver)]["msg_type"]
        struct_text = struct_text.replace("}} PLACEHOLDER;", f"}} {msg_struct};")
        struct_text = struct_text.replace(
            "typedef struct {", f"typedef struct {msg_struct} {{", 1)
        # Exact substring, NOT a \b-anchored regex: the element structs embed
        # the message name mid-identifier (`Rsprot_RsprotMsg_X_fElem`), where a
        # word boundary does not match and half the file would keep the old
        # name while the other half got the new one.
        struct_text = struct_text.replace(f"RsprotMsg_{msg_type}", msg_struct)
        struct_text = re.sub(r"\bRsprotMsg_\w+\b", msg_struct, struct_text)
        # The bodies name the element structs too (`Rsprot_RsprotMsg_X_fElem
        # *elem = ...`), so they take the same rename. Renaming only the struct
        # text is what left the two halves spelling one type differently.
        for slot in versions.values():
            slot["body"] = slot["body"].replace(f"RsprotMsg_{msg_type}", msg_struct)

        emit_packet_files({
            "snake": snake, "prot": prot, "cls": cls, "dir": direction,
            "rel": by_rev[max(rev_to_ver)]["rel"],
            "msg_struct": msg_struct, "struct": struct_text,
            "versions": versions, "rev_to_ver": rev_to_ver,
        }, args.out)
        emitted.append((prot, snake, direction, len(versions), len(rev_to_ver)))

    # Rewrite the unity anchor from what is actually on disk -- every .c in
    # packets/, not just what this run emitted, so a --only run does not drop
    # the others. A codec that exists but is missing from the anchor does not
    # fail to build; it silently is not linked, and the first binary to
    # reference its version table is where that surfaces.
    write_packets_unity(args.out)

    with open(args.status, "w") as fh:
        fh.write("rsprot packet-codec generation status\n")
        fh.write(f"revisions {REV_LO}-{REV_HI}; {len(emitted)} packets emitted, "
                 f"{len(refused)} refused\n\n")
        fh.write("--- emitted (prot, file, dir, layouts, revisions covered) ---\n")
        for prot, snake, d, nv, nr in sorted(emitted):
            fh.write(f"  OK   {prot:<32} {snake}.c {d:<4} {nv:>2} layout(s) {nr:>2} rev(s)\n")
        fh.write("\n--- refused (class, dir, reason) ---\n")
        for cls, d, why in sorted(refused):
            fh.write(f"  SKIP {cls:<48} {d:<4} {why}\n")
        fh.write("\n--- per-file parse skips ---\n")
        for rev, d, rel, why in sorted(all_skips):
            fh.write(f"  rev{rev} {d:<4} {rel:<60} {why}\n")

    print(f"emitted {len(emitted)} packets, refused {len(refused)} -- see {args.status}",
          file=sys.stderr)


if __name__ == "__main__":
    main()
