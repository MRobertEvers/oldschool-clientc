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


# --- wire function -> C function/type table -----------------------------------

def _build_fn_table():
    t = {}
    for w in (1, 2, 3, 4):
        for alt, asuf in (("", ""), ("Alt1", "_alt1"), ("Alt2", "_alt2"), ("Alt3", "_alt3")):
            t[f"p{w}{alt}"] = (f"rsprot_p{w}{asuf}", "int32_t")
            t[f"g{w}{alt}"] = (f"rsprot_g{w}{asuf}", "int32_t")
            if w <= 3:
                t[f"g{w}s{alt}"] = (f"rsprot_g{w}s{asuf}", "int32_t")
    t["p8"] = ("rsprot_p8", "int64_t")
    t["g8"] = ("rsprot_g8", "int64_t")
    t["p4f"] = ("rsprot_p4f", "float")
    t["g4f"] = ("rsprot_g4f", "float")
    t["p8d"] = ("rsprot_p8d", "double")
    t["g8d"] = ("rsprot_g8d", "double")
    t["pboolean"] = ("rsprot_pboolean", "bool")
    t["gboolean"] = ("rsprot_gboolean", "bool")
    for kt in ("pjstr", "gjstr", "pjstrnull", "gjstrnull", "pjstr2", "gjstr2"):
        t[kt] = (f"rsprot_{kt}", "const char *")
    for kt, c in (
        ("pSmart1or2", "psmart1or2"), ("gSmart1or2", "gsmart1or2"),
        ("pSmart1or2s", "psmart1or2s"), ("gSmart1or2s", "gsmart1or2s"),
        ("pSmart1or2null", "psmart1or2null"), ("gSmart1or2null", "gsmart1or2null"),
        ("pSmart1or2extended", "psmart1or2extended"), ("gSmart1or2extended", "gsmart1or2extended"),
        ("pSmart2or4", "psmart2or4"), ("gSmart2or4", "gsmart2or4"),
        ("pSmart2or4null", "psmart2or4null"), ("gSmart2or4null", "gsmart2or4null"),
        ("pMidiVarLen", "pmidivarlen"), ("gMidiVarLen", "gmidivarlen"),
        ("pVarInt2", "pvarint2"), ("gVarInt2", "gvarint2"),
        ("pVarInt2s", "pvarint2s"), ("gVarInt2s", "gvarint2s"),
        ("pType", "ptype"), ("gType", "gtype"),
    ):
        t[kt] = (f"rsprot_{c}", "int32_t")
    for alt, asuf in (("", ""), ("Alt1", "_alt1"), ("Alt2", "_alt2"), ("Alt3", "_alt3")):
        t[f"pCombinedId{alt}"] = (f"rsprot_pcombined_id{asuf}", "int32_t")
        t[f"gCombinedId{alt}"] = (f"rsprot_gcombined_id{asuf}", "int32_t")
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


def c_escape_ident(name):
    return re.sub(r"[^A-Za-z0-9_]", "_", name)


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
        # loop index / plain local?
        s = scope
        while s is not None:
            if name in s.locals:
                kind = s.locals[name]
                if kind[0] == "loopvar":
                    return c_escape_ident(name)
                if kind[0] == "local":
                    return c_escape_ident(name)
                if kind[0] == "field":
                    owner, cname = kind[1], kind[2]
                    return f"{owner}->{cname}"
                if kind[0] == "count_alias":
                    owner, field = kind[1], kind[2]
                    return f"{owner}->{field}_count"
            s = s.parent
        raise Unsupported(f"unresolved identifier: {name}")

    if k == "member":
        recv = ast["recv"]
        name = ast["name"]
        target_scope = resolve_recv_scope(recv, scope)
        if target_scope is not None:
            ptr = struct_ptr_expr(target_scope, ctx)
            cname = c_escape_ident(name)
            target_scope.add_field(cname, "int32_t")
            return f"{ptr}->{cname}"
        # `foo.size` on a local array-ish value we track as a count alias
        if recv["kind"] == "ident":
            s = scope
            while s is not None:
                if recv["name"] in s.locals and s.locals[recv["name"]][0] == "count_alias" and name == "size":
                    owner, field = s.locals[recv["name"]][1], s.locals[recv["name"]][2]
                    return f"{struct_ptr_expr(scope_for_owner(scope, owner), ctx)}->{field}_count"
                s = s.parent
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
    if target_scope.parent is None:
        return "out" if ctx == "decode" else "msg"
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


# --- IR -> C statement emission ------------------------------------------------

class Emitter:
    def __init__(self, msg_name, ctx):
        self.msg_name = msg_name
        self.ctx = ctx  # "encode" | "decode"
        self.lines = []
        self.tmp_i = 0
        self.decl_locals = []  # (ctype, name) declared up front, C89-ish safety not required (C11 ok inline)

    def emit(self, s, indent=1):
        self.lines.append(("    " * indent) + s)

    def new_scope_field(self, scope, ast_or_name, kt_fn=None):
        pass  # kept for symmetry; fields are registered where referenced.


def gen_ops(ops, scope, em, indent, ctx):
    for op in ops:
        gen_op(op, scope, em, indent, ctx)


def gen_op(op, scope, em, indent, ctx):
    kind = op["op"]

    if kind == "write":
        fn_kt = op["fn"]
        c_fn, ctype = wire_c_fn(fn_kt)
        expr_ast = op["expr"]
        # register the field if the expr is a direct field/member reference
        _pre_register(expr_ast, scope, ctype, ctx)
        expr_c = emit_expr(expr_ast, scope, ctx)
        buf = "buf"
        if leaf_string_fn(fn_kt):
            em.emit(f"{c_fn}({buf}, {expr_c});", indent)
        else:
            em.emit(f"{c_fn}({buf}, {expr_c});", indent)
        return

    if kind == "read":
        fn_kt = op["fn"]
        c_fn, ctype = wire_c_fn(fn_kt)
        name = c_escape_ident(op["name"])
        scope.add_field(name, ctype)
        scope.locals[op["name"]] = ("field", struct_ptr_expr(scope, ctx), name)
        buf = "buf"
        if leaf_string_fn(fn_kt):
            em.emit(f"{struct_ptr_expr(scope, ctx)}->{name} = {c_fn}({buf}, NULL);", indent)
        else:
            cast = f"({ctype})" if ctype != "int32_t" else ""
            em.emit(f"{struct_ptr_expr(scope, ctx)}->{name} = {cast}{c_fn}({buf});", indent)
        return

    if kind == "readbool":
        fn_kt = op["fn"]
        c_fn, _ = wire_c_fn(fn_kt)
        name = c_escape_ident(op["name"])
        scope.add_field(name, "bool")
        scope.locals[op["name"]] = ("field", struct_ptr_expr(scope, ctx), name)
        tv = op["true_val"]
        em.emit(f"{struct_ptr_expr(scope, ctx)}->{name} = ({c_fn}(buf) == {tv});", indent)
        return

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
        # pure passthrough alias: `val N = message.FIELD` (no array use) or
        # `val N = someLocal`
        if expr_ast["kind"] in ("ident", "member"):
            try:
                target_scope = None
                if expr_ast["kind"] == "member":
                    target_scope = resolve_recv_scope(expr_ast["recv"], scope)
                if target_scope is not None:
                    cname = c_escape_ident(expr_ast["name"])
                    target_scope.add_field(cname, "int32_t")
                    scope.locals[name] = ("field", struct_ptr_expr(target_scope, ctx), cname)
                    return
                if expr_ast["kind"] == "ident" and expr_ast["name"] in _all_local_names(scope):
                    scope.locals[name] = scope_lookup(scope, expr_ast["name"])
                    return
            except Unsupported:
                pass
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
        cmp = "<=" if op["inclusive"] else "<"
        em.emit(f"for (int32_t {var} = {lo_c}; {var} {cmp} {hi_c}; {var}++) {{", indent)
        scope.locals[op["var"]] = ("loopvar",)
        gen_ops(op["body"], scope, em, indent + 1, ctx)
        del scope.locals[op["var"]]
        em.emit("}", indent)
        return

    if kind == "for_each":
        iter_scope = resolve_recv_scope(op["iter"], scope) if op["iter"]["kind"] == "ident" else None
        field = None
        if op["iter"]["kind"] == "member":
            target_scope = resolve_recv_scope(op["iter"]["recv"], scope)
            if target_scope is not None:
                field = c_escape_ident(op["iter"]["name"])
                owner = target_scope
        elif op["iter"]["kind"] == "ident":
            owner = scope
            field = None
        if field is None:
            raise Unsupported("for-each over an unresolved iterable")
        elem_struct = f"Rsprot_{c_escape_ident(scope.struct_name)}_{c_escape_ident(field)}Elem"
        child = Scope(elem_struct, parent=scope, recv_name=op["var"])
        owner.array_fields[field] = child
        owner.count_field_for[field] = f"{field}_count"
        elem_var = c_escape_ident(op["var"])
        ptr = struct_ptr_expr(owner, ctx)
        em.emit(
            f"for (int32_t rs_i_{elem_var} = 0; rs_i_{elem_var} < {ptr}->{field}_count; rs_i_{elem_var}++) {{",
            indent,
        )
        decl_kw = "const " if ctx == "encode" else ""
        em.emit(f"{decl_kw}{elem_struct} *elem = ({decl_kw}{elem_struct} *)&{ptr}->{field}[rs_i_{elem_var}];", indent + 1)
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
        # return/return_void/ignore: nothing to emit -- for decoders the
        # struct is filled field-by-field already; the "return TYPE(args)"
        # constructor call's argument LIST is what tells us which locals are
        # part of the wire struct (locals not referenced there are scratch),
        # so we validate it, not emit it.
        if kind == "return":
            for a in op["args"]:
                _ = emit_expr(a, scope, ctx)  # validates every arg resolves
        return

    raise Unsupported(f"unmodeled IR op: {kind}")


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
                target_scope = resolve_recv_scope(a["recv"], scope)
                if target_scope is not None:
                    fname = None
                    # bare `message.isNullOrEmpty()`-style not expected; skip
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
    while a["kind"] == "call" and a["name"] in CAST_CALLS:
        a = a["recv"]
    if a["kind"] == "member":
        target_scope = resolve_recv_scope(a["recv"], scope)
        if target_scope is not None:
            target_scope.fields[c_escape_ident(a["name"])] = ctype
    elif a["kind"] == "ident":
        info = _all_local_names(scope).get(a["name"])
        if info and info[0] == "field":
            owner_ptr, cname = info[1], info[2]
            # find owning scope object matching owner_ptr; simplest: walk up
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
    """Recognize `FIELD?.size ?: 0`, `FIELD.size`, `FIELD?.size`. Returns
    (owner_scope, field_name) or None."""
    a = expr_ast
    default_ok = True
    if a["kind"] == "elvis":
        # right side must be 0
        r = a["right"]
        if not (r["kind"] == "num" and r["text"] in ("0",)):
            return None
        a = a["left"]
    if a["kind"] != "member" or a["name"] != "size":
        return None
    target_scope = resolve_recv_scope(a["recv"], scope)
    if target_scope is None:
        return None
    field = c_escape_ident(a["recv"]["name"]) if a["recv"]["kind"] == "member" else None
    if a["recv"]["kind"] == "member":
        field = c_escape_ident(a["recv"]["name"])
    elif a["recv"]["kind"] == "ident":
        # `X.size` where X is itself a local alias to a field
        info = _all_local_names(scope).get(a["recv"]["name"])
        if info and info[0] == "field":
            field = info[2]
        else:
            return None
    else:
        return None
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
            lines.append(f"\tconst {elem_name} *{name};")
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


def build_message(info, msg_type):
    ctx = "decode" if info["is_decoder"] else "encode"
    struct_c_name = f"RsprotMsg_{msg_type}"
    scope = Scope(struct_c_name)
    em = Emitter(msg_type, ctx)

    if info.get("no_body"):
        fn_name = f"rsprot_decode_{msg_type}" if info["is_decoder"] else f"rsprot_encode_{msg_type}"
        struct_text = f"typedef struct {struct_c_name} {{\n\tint32_t _unused;\n}} {struct_c_name};"
        if info["is_decoder"]:
            fn_text = (
                f"static inline void {fn_name}(RsprotBuf *buf, {struct_c_name} *out)\n"
                f"{{\n\t(void)buf;\n\tout->_unused = 0;\n}}"
            )
        else:
            fn_text = (
                f"static inline void {fn_name}(RsprotBuf *buf, const {struct_c_name} *msg)\n"
                f"{{\n\t(void)buf;\n\t(void)msg;\n}}"
            )
        return struct_text, fn_text, scope

    chunks = split_top_level(info["body"])
    ops = parse_statements(chunks, info["is_decoder"])
    gen_ops(ops, scope, em, 1, ctx)

    struct_text = render_all_structs(scope, msg_type)

    if info["is_decoder"]:
        fn_name = f"rsprot_decode_{msg_type}"
        sig = f"static inline void {fn_name}(RsprotBuf *buf, {struct_c_name} *out)"
    else:
        fn_name = f"rsprot_encode_{msg_type}"
        sig = f"static inline void {fn_name}(RsprotBuf *buf, const {struct_c_name} *msg)"
    body_text = "\n".join(em.lines) if em.lines else ""
    fn_text = f"{sig}\n{{\n{body_text}\n}}"
    return struct_text, fn_text, scope


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


def process_dir(base_dir, label):
    generated = []
    skipped = []
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
        try:
            struct_text, fn_text, scope = build_message(info, msg_type)
        except Unsupported as e:
            skipped.append((rel, str(e)))
            continue
        except Exception as e:  # noqa: BLE001
            skipped.append((rel, f"internal error: {type(e).__name__}: {e}"))
            continue
        generated.append(
            {
                "rel": rel,
                "class": info["class"],
                "msg_type": msg_type,
                "prot": info["prot"],
                "is_decoder": info["is_decoder"],
                "struct": struct_text,
                "fn": fn_text,
                "scope": scope,
                "flat": len(scope.array_fields) == 0,
            }
        )
    return generated, skipped


BANNER = """/*
 * GENERATED by tools/rsprot_gen_codec.py -- do not edit.
 * Source: RSProt revision {rev}, {count} message types.
 * See 3rd/rsprot/gen/codec_status_{rev}.txt for what was skipped and why.
 */"""


def emit_header(path, guard, banner, includes, body_chunks):
    lines = [f"#ifndef {guard}", f"#define {guard}", "", banner, ""]
    for inc in includes:
        lines.append(f'#include "{inc}"')
    lines.append("")
    for c in body_chunks:
        lines.append(c)
        lines.append("")
    lines.append("#endif")
    lines.append("")
    with open(path, "w") as fh:
        fh.write("\n".join(lines))


def gen_shadow_test(gen, direction):
    """For flat (no array/nested) message types only: a mirror function that
    reads back what the real function wrote (or vice versa), purely for
    test_codec_roundtrip.c. Not part of the public API."""
    scope = gen["scope"]
    msg_type = gen["msg_type"]
    struct_c_name = f"RsprotMsg_{msg_type}"
    lines = []
    if direction == "encode":
        fn_name = f"rsprot_shadow_decode_{msg_type}"
        lines.append(f"static inline void {fn_name}(RsprotBuf *buf, {struct_c_name} *out)")
        lines.append("{")
        for name, ctype in scope.fields.items():
            fn_kt = scope.__dict__.get("field_fn", {}).get(name)
            if not fn_kt:
                return None
            c_fn, _ = wire_c_fn(fn_kt)
            g_fn = c_fn.replace("rsprot_p", "rsprot_g", 1) if c_fn.startswith("rsprot_p") else None
            if g_fn is None:
                return None
            if leaf_string_fn(fn_kt.replace("p", "g", 1)) or fn_kt.startswith("pjstr"):
                lines.append(f"\tout->{name} = {g_fn}(buf, NULL);")
            else:
                cast = f"({ctype})" if ctype != "int32_t" else ""
                lines.append(f"\tout->{name} = {cast}{g_fn}(buf);")
        lines.append("}")
        return "\n".join(lines)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rev")
    args = ap.parse_args()
    rev = args.rev

    desktop = os.path.join(RSPROT, f"protocol/osrs-{rev}/osrs-{rev}-desktop/src/main/kotlin/net/rsprot/protocol/game")

    enc_gen, enc_skip = process_dir(os.path.join(desktop, "outgoing/codec"), "encoders")
    dec_gen, dec_skip = process_dir(os.path.join(desktop, "incoming/codec"), "decoders")

    os.makedirs(OUT_DIR, exist_ok=True)

    enc_chunks = []
    for g in enc_gen:
        enc_chunks.append(f"/* {g['class']} ({g['rel']}) -- prot {g['prot']} */")
        enc_chunks.append(g["struct"])
        enc_chunks.append(g["fn"])
    emit_header(
        os.path.join(OUT_DIR, f"encoders_{rev}.h"),
        f"RSPROT_GEN_CODEC_ENCODERS_{rev}_H",
        BANNER.format(rev=rev, count=len(enc_gen)),
        ["../../src/rsprot_buf.h"],
        enc_chunks,
    )

    dec_chunks = []
    for g in dec_gen:
        dec_chunks.append(f"/* {g['class']} ({g['rel']}) -- prot {g['prot']} */")
        dec_chunks.append(g["struct"])
        dec_chunks.append(g["fn"])
    emit_header(
        os.path.join(OUT_DIR, f"decoders_{rev}.h"),
        f"RSPROT_GEN_CODEC_DECODERS_{rev}_H",
        BANNER.format(rev=rev, count=len(dec_gen)),
        ["../../src/rsprot_buf.h"],
        dec_chunks,
    )

    status_path = os.path.join(os.path.dirname(OUT_DIR), f"codec_status_{rev}.txt")
    with open(status_path, "w") as fh:
        fh.write(f"rsprot codec generation status -- revision {rev}\n")
        fh.write(f"encoders: {len(enc_gen)} generated, {len(enc_skip)} skipped (of {len(enc_gen)+len(enc_skip)})\n")
        fh.write(f"decoders: {len(dec_gen)} generated, {len(dec_skip)} skipped (of {len(dec_gen)+len(dec_skip)})\n\n")
        fh.write("--- generated encoders ---\n")
        for g in enc_gen:
            fh.write(f"  OK   {g['rel']:<55} {g['msg_type']}{' [flat]' if g['flat'] else ' [array/nested]'}\n")
        fh.write("\n--- skipped encoders ---\n")
        for rel, reason in enc_skip:
            fh.write(f"  SKIP {rel:<55} {reason}\n")
        fh.write("\n--- generated decoders ---\n")
        for g in dec_gen:
            fh.write(f"  OK   {g['rel']:<55} {g['msg_type']}{' [flat]' if g['flat'] else ' [array/nested]'}\n")
        fh.write("\n--- skipped decoders ---\n")
        for rel, reason in dec_skip:
            fh.write(f"  SKIP {rel:<55} {reason}\n")

    print(
        f"rev {rev}: encoders {len(enc_gen)}/{len(enc_gen)+len(enc_skip)}, "
        f"decoders {len(dec_gen)}/{len(dec_gen)+len(dec_skip)} -- see {status_path}",
        file=sys.stderr,
    )


if __name__ == "__main__":
    main()
