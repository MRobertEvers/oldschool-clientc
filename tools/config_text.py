"""Reading cachepack config text: `[name]` blocks of `key=value` lines.

The content tree's config text (rank 0 `configs/all.<type>`, and the rank-1
`server/scripts/**/configs/*.<type>` / `ported/**/configs/*.<type>` overlays)
states every key of a record. A key the record does not set is written with a
MARKER instead of being left out:

    name=default      the field is ABSENT -- exactly as if the line were missing
    recol=empty       a list-valued key that is PRESENT with zero entries
    name=\\default    an ordinary string that happens to read "default"

The marker test is on the raw text after `=` (trailing CR/LF removed, nothing
else stripped), before any unescaping -- the same test the packer makes
(`cp_value_is_default` / `cp_value_is_empty` in 3rd/rscache/tools/cachepack).
A marker is the only line for its key, and it sits on the bare stem of an
indexed family (`recol=default`, never `recol1s=default`).

Most readers here do not care about the difference between "absent" and
"present, empty": they iterate lines, and both mean "no entries". For them the
whole change is one call where the file is read:

    text = config_text.read_text(path)          # instead of path.read_text()
    for line in config_text.read_lines(path):   # instead of open(path)

which drops every marker line and turns `key=\\default` back into
`key=default`, so the text a reader sees is exactly what the old format (keys
omitted) said. Nothing else in a line is touched, so old-format text reads
byte-for-byte as before.

A reader that MERGES rank-1 overlays onto rank-0 records must not drop the
markers -- an overlay's `key=default` clears the inherited field, and
`key=empty` replaces an inherited list with nothing. Use `parse_records()`
(markers kept as `DEFAULT` / `EMPTY`) and `apply_overlay()` for that.
"""

import os
import re
import subprocess

DEFAULT_TEXT = "default"
EMPTY_TEXT = "empty"


class _Marker:
    __slots__ = ("text",)

    def __init__(self, text):
        self.text = text

    def __repr__(self):
        return "config_text." + self.text.upper()


# The parsed value of `key=default` / `key=empty` in parse_records(). Distinct
# objects, never equal to any string, so a consumer cannot mistake one for a
# value spelled "default".
DEFAULT = _Marker(DEFAULT_TEXT)
EMPTY = _Marker(EMPTY_TEXT)

_ESCAPED_MARKERS = {"\\" + DEFAULT_TEXT: DEFAULT_TEXT, "\\" + EMPTY_TEXT: EMPTY_TEXT}


def _chomp(text):
    """Drop a trailing LF/CR run and nothing else (the packer's own trim)."""
    return text.rstrip("\r\n")


def marker(raw):
    """`DEFAULT`, `EMPTY`, or None for the raw text after `=`."""
    assert raw is not None
    raw = _chomp(raw)
    if raw == DEFAULT_TEXT:
        return DEFAULT
    if raw == EMPTY_TEXT:
        return EMPTY
    return None


def is_default(raw):
    return marker(raw) is DEFAULT


def is_empty(raw):
    return marker(raw) is EMPTY


def unmark(raw):
    """Undo only the marker escape: `\\default` -> `default`, `\\empty` -> `empty`.

    For a reader that does not unescape anything else -- every other escape is
    left exactly as it was, so old-format text reads as before. The caller has
    already tested `marker(raw)`; a bare marker passed here is a caller bug.
    """
    assert raw is not None
    assert marker(raw) is None, "unmark() of a marker: test marker() first"
    return _ESCAPED_MARKERS.get(raw, raw)


def unescape(raw):
    """Full unescape, as cp_unescape: `\\n`, `\\r`, and `\\X` -> X (`\\\\`, `\\[`)."""
    assert raw is not None
    assert marker(raw) is None, "unescape() of a marker: test marker() first"
    out = []
    i = 0
    n = len(raw)
    while i < n:
        c = raw[i]
        if c == "\\" and i + 1 < n:
            i += 1
            c = raw[i]
            out.append("\n" if c == "n" else "\r" if c == "r" else c)
        else:
            out.append(c)
        i += 1
    return "".join(out)


def split_line(line):
    """`(key, raw_value)` for a property line; None for a blank, comment or header.

    The raw value keeps everything after the first `=` except the line ending.
    """
    assert line is not None
    body = _chomp(line)
    if not body or body.startswith("//") or body.startswith("["):
        return None
    eq = body.find("=")
    if eq < 0:
        return None
    return body[:eq], body[eq + 1:]


def filter_line(line):
    """The line as old-format text would have had it, or None to drop it.

    `key=default` / `key=empty` -> None (the old text omitted the key);
    `key=\\default` -> `key=default`; anything else unchanged (line ending kept).
    """
    assert line is not None
    stripped = line.lstrip()
    if not stripped or stripped.startswith("//") or stripped.startswith("["):
        return line
    eq = line.find("=")
    if eq < 0:
        return line
    body = line[eq + 1:]
    raw = _chomp(body)
    if raw == DEFAULT_TEXT or raw == EMPTY_TEXT:
        return None
    plain = _ESCAPED_MARKERS.get(raw)
    if plain is None:
        return line
    return line[:eq + 1] + plain + body[len(raw):]


def filter_lines(lines):
    """filter_line() over an iterable, dropping the marker lines."""
    for line in lines:
        kept = filter_line(line)
        if kept is not None:
            yield kept


def filter_text(text):
    """filter_line() over a whole file's text; line endings are preserved."""
    assert text is not None
    return "".join(filter_lines(text.splitlines(keepends=True)))


def read_text(path, encoding="utf-8", errors=None):
    """A config file's text with the markers filtered out (see filter_line)."""
    with open(path, encoding=encoding, errors=errors) as f:
        return filter_text(f.read())


def read_lines(path, encoding="utf-8", errors=None, keepends=False):
    """read_text() split into lines."""
    return read_text(path, encoding=encoding, errors=errors).splitlines(keepends)


# ---- marker-aware parsing, for readers that merge overlays -----------------

_INDEX_RE = re.compile(r"[0-9]")


def key_matches(stem, key):
    """Does a line spelled `key` belong to the key `stem`?

    A marker sits on the bare stem of an indexed family, so `recol` covers
    `recol1s`, `recol2d`, ...: an exact match, or the stem followed by a digit
    (cp_keys.c spec_matches).
    """
    assert stem
    assert key
    if key == stem:
        return True
    return key.startswith(stem) and len(key) > len(stem) and bool(
        _INDEX_RE.match(key[len(stem)]))


def parse_value(raw, unescape_strings=False):
    """`DEFAULT`, `EMPTY`, or the value as a string.

    With `unescape_strings` the string is fully unescaped; otherwise only the
    marker escape is undone (unmark), so a reader that never unescaped keeps
    its old output.
    """
    m = marker(raw)
    if m is not None:
        return m
    return unescape(raw) if unescape_strings else unmark(raw)


def parse_records(text, unescape_strings=False):
    """`{name: [(key, value), ...]}` in file order; markers kept as DEFAULT/EMPTY.

    Lines before the first header are ignored, as the old ad-hoc readers did.
    A repeated header appends to the same record.
    """
    assert text is not None
    records = {}
    current = None
    for line in text.splitlines():
        body = _chomp(line)
        if body.startswith("[") and body.endswith("]") and len(body) > 2:
            current = records.setdefault(body[1:-1], [])
            continue
        kv = split_line(line)
        if kv is None or current is None:
            continue
        key, raw = kv
        current.append((key, parse_value(raw, unescape_strings)))
    return records


def apply_overlay(base, overlay):
    """A rank-1 overlay block applied to a rank-0 record's lines.

    Both are `[(key, value), ...]` from parse_records(). A key the overlay
    does not mention is inherited. A key it does mention replaces every base
    line of that key (an indexed family by its stem when the overlay line is a
    marker). `DEFAULT` clears the field; `EMPTY` leaves it present with zero
    entries -- kept as one `(key, EMPTY)` line so a consumer can tell.
    """
    assert base is not None
    assert overlay is not None
    overlay_keys = [k for k, _ in overlay]
    marker_stems = [k for k, v in overlay if isinstance(v, _Marker)]

    def overridden(key):
        if key in overlay_keys:
            return True
        return any(key_matches(stem, key) for stem in marker_stems)

    merged = [(k, v) for k, v in base if not overridden(k)]
    merged.extend((k, v) for k, v in overlay if v is not DEFAULT)
    return merged


def values(lines, key):
    """Every string value of `key` in a record's lines (markers contribute none)."""
    return [v for k, v in lines if k == key and not isinstance(v, _Marker)]


def value(lines, key, fallback=None):
    """The last string value of `key`, or `fallback` when absent or a marker."""
    found = values(lines, key)
    return found[-1] if found else fallback


# ---- writing: complete a NEW record -------------------------------------
#
# A record no rank-0 block defines -- a generator's dbrow, a minted enum --
# states every key of its type, client and server, `key=default` for each it
# does not set (3rd/rscache/tools/cachepack/cp_text.h). The key set is the
# packer's own (`cachepack keys --src`), never a copy kept here, so a key added
# to a type's table or to `fields/<type>.ini` reaches every generator at once.

_KEYS_CACHE = {}


def default_cachepack():
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.join(here, "..", "3rd", "rscache", "tools", "cachepack", "cachepack")


def type_keys(kind, src, rev="osrs239", cachepack=None):
    """[(key, flag, sibling)] every block of `kind` states, flag in {"-", "list",
    "indexed"}; `sibling` is a key of the same opcode or None."""
    cachepack = cachepack or default_cachepack()
    cache_key = (cachepack, os.path.abspath(src), rev)
    if cache_key not in _KEYS_CACHE:
        out = subprocess.run([cachepack, "keys", "--rev", rev, "--src", src],
                             check=True, capture_output=True, text=True).stdout
        table = {}
        for line in out.splitlines():
            parts = line.split()
            t, key, flag = parts[:3]
            sibling = parts[3] if len(parts) > 3 else None
            table.setdefault(t, []).append((key, flag, sibling))
        _KEYS_CACHE[cache_key] = table
    table = _KEYS_CACHE[cache_key]
    assert kind in table, "cachepack knows no config type %r" % kind
    return table[kind]


def complete_block(kind, lines, src, rev="osrs239", cachepack=None):
    """`lines` (one record's `key=value` lines) plus `key=default` for every key of
    `kind` they do not state, in the type's key order. Lines are not reordered."""
    stated = set()
    for line in lines:
        parsed = split_line(line)
        if parsed is not None:
            stated.add(parsed[0])
    missing = []
    for key, flag, sibling in type_keys(kind, src, rev, cachepack):
        if any(key_matches(key, k) if flag == "indexed" else k == key for k in stated):
            continue
        # A key sharing an opcode with a stated sibling is that opcode with
        # no entries (`empty`), not an absent opcode.
        marker = EMPTY_TEXT if sibling and sibling in stated else DEFAULT_TEXT
        missing.append("%s=%s" % (key, marker))
    return list(lines) + missing


def complete_new_blocks(kind, text, src, rev="osrs239", cachepack=None):
    """`text` (a whole config file of `kind`) with every NEW record completed:
    `key=default` appended for each key the block does not state. A block whose
    name the cache already holds (`configs/all.<kind>.compack`) is an overlay and
    stays partial, as an overlay may. Generators pass their output through this
    before writing it, so a regenerated file is the file the tree keeps."""
    cache_names = set()
    path = os.path.join(src, "configs", "all.%s.compack" % kind)
    if os.path.isfile(path):
        for line in read_text(path).splitlines():
            if "=" in line and not line.startswith("//"):
                cache_names.add(line.split("=", 1)[1].strip())
    out = []
    block = None
    body = []

    def flush():
        if block is None:
            return
        name = block.strip()[1:-1]
        lines = body
        if name not in cache_names and name != "default":
            # Trailing blank/comment lines stay after the completed keys.
            tail = []
            while lines and (not lines[-1].strip() or lines[-1].lstrip().startswith("//")):
                tail.insert(0, lines.pop())
            lines = complete_block(kind, lines, src, rev, cachepack) + tail
        out.append(block)
        out.extend(lines)

    for line in text.split("\n"):
        if line.startswith("[") and line.rstrip().endswith("]"):
            flush()
            block, body = line, []
        elif block is None:
            out.append(line)
        else:
            body.append(line)
    flush()
    return "\n".join(out)


CONFIG_KINDS = ("underlay", "overlay", "idk", "inv", "loc", "enum", "npc", "obj", "param",
                "seq", "spotanim", "varbit", "varp", "varc", "hitsplat", "healthbar",
                "struct", "mapelement", "dbrow", "dbtable")


def completed(path, text, src=None):
    """What a generator writes to `path`: `text` with every new record completed
    (complete_new_blocks), the kind taken from the extension; any other file is
    returned unchanged. `src` is the content tree, found from `path` when None.
    Generators call this on the text they write AND on the text their --check
    compares, so a regenerated file is byte-for-byte the file the tree keeps."""
    kind = os.path.basename(str(path)).rsplit(".", 1)[-1]
    if kind not in CONFIG_KINDS:
        return text
    if src is None:
        here = os.path.abspath(str(path))
        while here != os.path.dirname(here):
            here = os.path.dirname(here)
            if os.path.isdir(os.path.join(here, "configs")) and \
                    os.path.isdir(os.path.join(here, "server")):
                src = here
                break
        assert src, "no content tree above %s" % path
    return complete_new_blocks(kind, text, src)


def write_config(path, text, encoding="utf-8", src=None, **_):
    """`pathlib.Path.write_text` for a generator's config output: writes
    completed(path, text) (every new record states every key)."""
    with open(str(path), "w", encoding=encoding) as handle:
        handle.write(completed(path, text, src))


# ---- typed values: the one ScriptVarType table ---------------------------
#
# A value's type is a ScriptVarType: a dbtable column states its id, a param or
# an enum its character, the text its word. cachepack's table
# (3rd/rscache/tools/cachepack/cp_value.c) is the source; this is the same table,
# row for row, and tools/test_config_text.py holds the two equal. `kind` is the
# Names namespace a name resolves in, None for a type whose values are numbers
# (or text, for `string`). Every value is spelled through it: a reference by name
# (`null` for -1), a coord `level_mx_mz_lx_lz`, a boolean `yes`/`no` (a param or
# an enum) or `true`/`false` (a dbrow) -- either pair reads -- and a number for
# the rest. A bare number reads for every int type.

#              word            id    char     kind
VALUE_TYPES = (
    ("int",          0,    "i",    None),
    ("boolean",      1,    "1",    None),
    ("seq",          6,    "A",    "seq"),
    ("colour",       7,    "C",    None),
    ("locshape",     8,    "H",    None),
    ("component",    9,    "I",    "component"),
    ("idkit",        10,   "K",    None),
    ("midi",         11,   "M",    None),
    ("namedobj",     13,   "O",    "obj"),
    ("synth",        14,   "P",    "synth"),
    ("stat",         17,   "S",    "stat"),
    ("coord",        22,   "c",    None),
    ("graphic",      23,   "d",    None),
    ("fontmetrics",  25,   "f",    None),
    ("enum",         26,   "g",    "enum"),
    ("jingle",       28,   "j",    None),
    ("loc",          30,   "l",    "loc"),
    ("model",        31,   "m",    None),
    ("npc",          32,   "n",    "npc"),
    ("obj",          33,   "o",    "obj"),
    ("string",       36,   "s",    None),
    ("spotanim",     37,   "t",    "spotanim"),
    ("inv",          39,   "v",    "inv"),
    ("texture",      40,   "x",    None),
    ("category",     41,   "y",    "category"),
    ("char",         42,   "z",    None),
    ("mapsceneicon", 55,   "\xa3", None),
    ("mapelement",   59,   "\xb5", "mapelement"),
    ("hitmark",      62,   "\xd7", "hitsplat"),
    ("struct",       73,   "J",    "struct"),
    ("dbrow",        74,   "\xd0", "dbrow"),
    ("dbtable",      118,  "\xd8", "dbtable"),
    ("varp",         209,  "7",    "varp"),
    ("area",         None, "R",    None),
    ("maparea",      None, "`",    None),
    ("interface",    None, "a",    "interface"),
    ("varbit",       None, "\x81", "varbit"),
)

# word -> the Names namespace its values resolve in (reference types only).
REF_KINDS = {word: kind for word, _, _, kind in VALUE_TYPES if kind is not None}
# Both names are the one table; kept for the callers that import them.
PARAM_REF_KINDS = REF_KINDS
DB_REF_KINDS = REF_KINDS
_TYPE_BY_CHAR = {ch: word for word, _, ch, _ in VALUE_TYPES}
_TYPE_BY_ID = {i: word for word, i, _, _ in VALUE_TYPES if i is not None}


def type_word(text):
    """A type's word from any of its spellings: the word itself, the type
    character (`O`), or a ScriptVarType id (`13`). None when it names nothing."""
    if text is None:
        return None
    if any(text == word for word, _, _, _ in VALUE_TYPES):
        return text
    if _INT_RE.match(text):
        return _TYPE_BY_ID.get(int(text))
    if len(text) == 1:
        return _TYPE_BY_CHAR.get(text)
    return None


def value_int(type_word_, text, names=None):
    """One int value of type `type_word_` (a word; None or an unknown word: a
    plain int) as the number the cache stores: `null` -> -1, `yes`/`true` -> 1,
    `no`/`false` -> 0, a coord packed, a name resolved through `names` (a
    Names), else a decimal. A bare number passes through for every type, as
    cachepack writes one when it has no name."""
    assert text is not None
    assert type_word_ != "string", "value_int() of a string type"
    if text == "null":
        return -1
    if _INT_RE.match(text):
        return int(text)
    if type_word_ == "boolean":
        if text in ("true", "yes"):
            return 1
        if text in ("false", "no"):
            return 0
    elif type_word_ == "coord":
        return coord_pack(text)
    elif type_word_ in REF_KINDS:
        assert names is not None, "value_int(%s): a name needs a Names" % type_word_
        return names.id(REF_KINDS[type_word_], text)
    raise ValueError("%s value %r is not a number" % (type_word_, text))


# ---- params: `param=<name>,<value>` ------------------------------------
#
# Every param line, rank 0 and rank 1 alike, is LostCity's two-field form. The
# value's kind is the PARAM's declared type (`type=` in `configs/all.param`, a
# word), not a column of the line: a string param's value is the text after the
# first comma, commas and all; every other value is spelled by its type
# (value_int).


def split_param(raw):
    """`(name, value)` for the raw text after `param=`: value is everything after
    the first comma. A marker (`param=default` / `param=empty`) is the caller's
    to test first."""
    assert raw is not None
    assert marker(raw) is None, "split_param() of a marker: test marker() first"
    name, comma, value = _chomp(raw).partition(",")
    assert comma, "param line without a value: %r" % raw
    return name, value


def param_types(path, encoding="utf-8", errors="replace"):
    """`{param name: type word}` from a `configs/all.param`. A param whose
    `type=` is absent maps to None (the cache's default, an int)."""
    with open(path, encoding=encoding, errors=errors) as f:
        records = parse_records(f.read())
    return {name: type_word(value(lines, "type")) for name, lines in records.items()}


def param_is_string(type_word_):
    """A param declared `string`."""
    return type_word_ == "string"


def param_int(type_word_, text, names):
    """An int-kind param value as its id (value_int through the param's type)."""
    assert text is not None
    assert not param_is_string(type_word_), "param_int() of a string param"
    return value_int(type_word_, text, names)


def param_values(raws, types, names):
    """`{param name: value}` for a record's raw `param=` texts (markers skipped):
    a string param's value as text, every other kind as its int id (param_int).
    `types` is param_types()' answer, `names` a Names."""
    out = {}
    for raw in raws:
        if marker(raw) is not None:
            continue
        name, text = split_param(raw)
        word = types.get(name)
        out[name] = text if param_is_string(word) else param_int(word, text, names)
    return out


# ---- record names -> ids ------------------------------------------------

_INT_RE = re.compile(r"^-?[0-9]+$")


def read_id_map(path, encoding="utf-8"):
    """`{name: id}` from an `<id>=<name>` file (`*.compack`, `pack/*.pack`)."""
    by_name = {}
    with open(path, encoding=encoding, errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("//"):
                continue
            raw_id, eq, name = line.partition("=")
            if eq and _INT_RE.match(raw_id):
                by_name.setdefault(name, int(raw_id))
    return by_name


class Names:
    """Record names -> ids for one content tree (`.../osrs239-content`).

    A config type resolves through `configs/all.<kind>.compack`; `stat` through
    `pack/stat.pack`; `category` through `pack/category.pack`; a component
    `interface:child` through `pack/3_interfaces.pack` and
    `interfaces/<interface>.compack` -- the same sources cachepack spells them
    from. Each file is read once, on first use.
    """

    def __init__(self, content):
        assert content
        self.content = str(content)
        self._maps = {}

    def _map(self, key, path):
        if key not in self._maps:
            self._maps[key] = read_id_map(path) if os.path.exists(path) else {}
        return self._maps[key]

    def table(self, kind):
        """`{name: id}` for one namespace."""
        assert kind
        if kind == "stat":
            return self._map(kind, os.path.join(self.content, "pack", "stat.pack"))
        if kind == "category":
            return self._map(kind, os.path.join(self.content, "pack", "category.pack"))
        if kind == "interface":
            return self._map(kind, os.path.join(self.content, "pack", "3_interfaces.pack"))
        if kind == "synth":
            return self._map(kind, os.path.join(self.content, "pack", "4_soundeffects.pack"))
        return self._map(kind, os.path.join(
            self.content, "configs", "all.%s.compack" % kind))

    def id(self, kind, name):
        """The id `name` has in namespace `kind`; KeyError when it has none."""
        assert name is not None
        if kind == "component":
            iface, colon, child = name.partition(":")
            if not colon:
                raise KeyError("component %r is not interface:child" % name)
            children = self._map("com:" + iface, os.path.join(
                self.content, "interfaces", "%s.compack" % iface))
            return (self.id("interface", iface) << 16) | _lookup(children, "component", name, child)
        return _lookup(self.table(kind), kind, name, name)


def _lookup(table, kind, full, name):
    if name in table:
        return table[name]
    raise KeyError("%s %r has no id in this content tree" % (kind, full))


# ---- dbtable / dbrow: LostCity's authored grammar ------------------------
#
#     [quest]                            [quest_animalmagnetism]
#     column=id,int                      table=quest
#     column=version,ABSENT              data=id,123
#     column=startcoord,coord            data=startcoord,0_48_52_22_31
#     default=startcoord,0_0_0_0_0       data=requirement_stats,woodcutting,35
#
# A column's id is its POSITION among the table's `column=` lines; a hole in the
# numbering is a column with the property ABSENT (no types, no values). An
# upper-case word after the name is a property (INDEXED, REQUIRED, LIST,
# ABSENT), anything else a type. `default=` / `data=` carry one tuple per line:
# the column name, then exactly as many fields as the column has types; the last
# field takes the rest of the line, an earlier one escapes a comma `\,` (also
# `\\`, `\ `, `/\/`, `\^`, `\n`, `\r`) -- see cp_db.c. `column=default` /
# `default=default` / `data=default` say the opcode is absent, `=empty` that it is
# present with nothing in it: both read as "no columns" / "no tuples" here.

DB_ABSENT = "ABSENT"

def _db_escaped(s, at):
    run = 0
    while at > run and s[at - 1 - run] == "\\":
        run += 1
    return run & 1


def db_clean(raw):
    """The server's line cleaner, escape-aware (cp_db.c db_clean_value): cut at
    the first unescaped `//`, trim unescaped trailing blanks and leading blanks."""
    assert raw is not None
    s = _chomp(raw)
    i = 0
    while i < len(s):
        if s[i] == "\\" and i + 1 < len(s):
            i += 2
            continue
        if s[i] == "/" and s[i + 1:i + 2] == "/":
            s = s[:i]
            break
        i += 1
    end = len(s)
    while end and s[end - 1] <= " " and not _db_escaped(s, end - 1):
        end -= 1
    return s[:end].lstrip(" \t")


def db_unescape(text):
    """Undo a field's escapes: `\\n`, `\\r`, and `\\X` -> X (cp_db.c db_unescape)."""
    assert text is not None
    if "\\" not in text:
        return text
    out = []
    i = 0
    while i < len(text):
        c = text[i]
        if c == "\\" and i + 1 < len(text):
            i += 1
            c = text[i]
            c = "\n" if c == "n" else "\r" if c == "r" else c
        out.append(c)
        i += 1
    return "".join(out)


def db_field(text, last):
    """One db field as config text, the inverse of `db_unescape` after the
    line cleaner (cp_db.c db_append_field). `last` is the tuple's last field,
    which takes the rest of the line and so keeps its commas bare. Escaped: a
    backslash, a comma (unless last), a leading `^` (else a constant), the
    second slash of `//` (else a comment), trailing blanks of the last field
    (else trimmed), and newlines."""
    assert text is not None
    trailing = 0
    if last:
        while trailing < len(text) and text[len(text) - 1 - trailing] <= " ":
            trailing += 1
    out = []
    for i, c in enumerate(text):
        if c == "\n":
            out.append("\\n")
            continue
        if c == "\r":
            out.append("\\r")
            continue
        if (c == "\\" or (c == "," and not last) or (c == "^" and i == 0)
                or (c == "/" and i > 0 and text[i - 1] == "/")
                or i >= len(text) - trailing):
            out.append("\\")
        out.append(c)
    return "".join(out)


def _db_comma(s, start):
    """Index of the first unescaped comma at or after `start`, or -1."""
    i = start
    while i < len(s):
        if s[i] == "\\" and i + 1 < len(s):
            i += 2
            continue
        if s[i] == ",":
            return i
        i += 1
    return -1


def db_head(raw):
    """The column name a `default=` / `data=` value starts with."""
    s = db_clean(raw)
    comma = _db_comma(s, 0)
    return s if comma < 0 else s[:comma]


def db_split(raw, count=None):
    """`(head, [field, ...])` for a `default=` / `data=` value: the column name
    and `count` unescaped fields, the last taking the rest of the line. With
    `count` None (a column of unknown types) every unescaped comma splits."""
    assert count is None or count > 0
    s = db_clean(raw)
    comma = _db_comma(s, 0)
    if comma < 0:
        return s, []
    head = s[:comma]
    fields = []
    start = comma + 1
    while count is None or len(fields) < count - 1:
        comma = _db_comma(s, start)
        if comma < 0:
            break
        fields.append(s[start:comma])
        start = comma + 1
    fields.append(s[start:])
    return head, [db_unescape(f) for f in fields]


class DbColumn:
    __slots__ = ("id", "name", "types", "properties", "defaults")

    def __init__(self, column_id, name, types, properties):
        self.id = column_id
        self.name = name
        self.types = tuple(types)
        self.properties = tuple(properties)
        self.defaults = []  # [tuple of field strings], one per `default=` line

    @property
    def absent(self):
        return DB_ABSENT in self.properties

    def __repr__(self):
        return "DbColumn(%d, %r, %r, %r)" % (self.id, self.name, self.types, self.properties)


class DbTable:
    __slots__ = ("name", "columns", "_by_name")

    def __init__(self, name):
        self.name = name
        self.columns = []  # by position: columns[i].id == i, holes included
        self._by_name = {}

    def add(self, name, types, properties):
        col = DbColumn(len(self.columns), name, types, properties)
        self.columns.append(col)
        self._by_name.setdefault(name, col)
        return col

    def column(self, name):
        """The column called `name`, or None."""
        return self._by_name.get(name)

    def schema(self):
        """`{id: (name, types)}` for every column that is not a hole."""
        return {c.id: (c.name, c.types) for c in self.columns if not c.absent}


class DbRow:
    __slots__ = ("name", "table", "schema", "data")

    def __init__(self, name):
        self.name = name
        self.table = None   # the table's name; None when `table=default`
        self.schema = None  # that table's DbTable, when the caller had it
        self.data = []      # [(column name, tuple of field strings)] in file order

    def tuples(self, column):
        """Every tuple the row states for `column` (a name), in order."""
        return [t for c, t in self.data if c == column]

    def fields(self, column):
        """tuples() flattened."""
        return [f for t in self.tuples(column) for f in t]

    def typed(self, column, names=None):
        """tuples() with every non-string field as the number the cache stores
        (db_tuple_ints against the column's types)."""
        assert self.schema is not None, "[%s] typed() needs the row's table" % self.name
        col = self.schema.column(column)
        assert col is not None, "[%s] table %s has no column %r" % (
            self.name, self.table, column)
        return [db_tuple_ints(col.types, t, names) for t in self.tuples(column)]

    def typed_fields(self, column, names=None):
        """typed() flattened."""
        return [f for t in self.typed(column, names) for f in t]

    def columns(self):
        """The column names the row states, in first-seen order."""
        seen = []
        for c, _ in self.data:
            if c not in seen:
                seen.append(c)
        return seen


def _db_blocks(text, unique):
    """`[(name, [(key, raw)])]` in file order; markers left in `raw`. With
    `unique` a repeated header is a ValueError."""
    blocks = []
    seen = set()
    current = None
    for line in text.splitlines():
        body = _chomp(line)
        if body.startswith("[") and body.endswith("]") and len(body) > 2:
            if unique and body[1:-1] in seen:
                raise ValueError("duplicate block [%s]" % body[1:-1])
            seen.add(body[1:-1])
            current = (body[1:-1], [])
            blocks.append(current)
            continue
        kv = split_line(line)
        if kv is None or current is None:
            continue
        current[1].append(kv)
    return blocks


def parse_dbtables(text, unique=False):
    """`{name: DbTable}` for an `all.dbtable` (or authored `.dbtable`) text. A
    repeated header adds to the same table, or with `unique` is a ValueError."""
    assert text is not None
    tables = {}
    for name, lines in _db_blocks(text, unique):
        table = tables.get(name) or DbTable(name)
        tables[name] = table
        for key, raw in lines:
            if key != "column" or marker(raw) is not None:
                continue
            words = [w for w in db_clean(raw).split(",")]
            col_name = words[0]
            types = [w for w in words[1:] if not (w.isupper() and w.isalpha())]
            props = [w for w in words[1:] if w.isupper() and w.isalpha()]
            table.add(col_name, types, props)
        for key, raw in lines:
            if key != "default" or marker(raw) is not None:
                continue
            head = db_head(raw)
            col = table.column(head)
            assert col is not None, "[%s] default= for no column %r" % (name, head)
            col.defaults.append(tuple(db_split(raw, len(col.types) or None)[1]))
    return tables


def parse_dbrows(text, tables, unique=False):
    """`{name: DbRow}` for an `all.dbrow` text, in file order. `tables` is
    parse_dbtables()'s answer: it says how many fields each column's tuple has.
    A row of an unknown table (or column) splits at every unescaped comma. A
    repeated header adds to the same row, or with `unique` is a ValueError."""
    assert text is not None
    assert tables is not None
    rows = {}
    for name, lines in _db_blocks(text, unique):
        row = rows.get(name) or DbRow(name)
        rows[name] = row
        for key, raw in lines:
            if key == "table" and marker(raw) is None:
                row.table = unmark(raw)
        table = tables.get(row.table) if row.table else None
        row.schema = table
        for key, raw in lines:
            if key != "data" or marker(raw) is not None:
                continue
            head = db_head(raw)
            col = table.column(head) if table else None
            count = len(col.types) if col is not None and col.types else None
            row.data.append((head, tuple(db_split(raw, count)[1])))
    return rows


def read_db(configs, encoding="utf-8", errors="replace"):
    """`(tables, rows)` from a `configs/` directory's `all.dbtable` + `all.dbrow`."""
    configs = str(configs)
    with open(os.path.join(configs, "all.dbtable"), encoding=encoding, errors=errors) as f:
        tables = parse_dbtables(f.read())
    with open(os.path.join(configs, "all.dbrow"), encoding=encoding, errors=errors) as f:
        rows = parse_dbrows(f.read(), tables)
    return tables, rows


def coord_pack(text):
    """`level_mx_mz_lx_lz` -> the packed coord int (level << 28 | x << 14 | z)."""
    parts = text.split("_")
    if len(parts) != 5 or not all(_INT_RE.match(p) for p in parts):
        raise ValueError("coord %r is not level_mx_mz_lx_lz" % text)
    level, mx, mz, lx, lz = (int(p) for p in parts)
    return (level << 28) | ((mx * 64 + lx) << 14) | (mz * 64 + lz)


def db_int(type_word_, text, names=None):
    """One field of an int-valued column type as the number the cache stores
    (value_int: the one table)."""
    assert type_word_
    return value_int(type_word_, text, names)


def db_tuple_ints(types, fields, names=None):
    """db_int() over a tuple, string types left as text."""
    assert len(types) == len(fields), "tuple %r does not match types %r" % (fields, types)
    return tuple(f if t == "string" else db_int(t, f, names) for t, f in zip(types, fields))
