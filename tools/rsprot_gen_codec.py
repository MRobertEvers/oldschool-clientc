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
