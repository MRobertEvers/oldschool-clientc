"""Read RSProt's spec: the INI files under spec/.

The dialect, which every file under spec/ follows:

    ; a comment -- only at the start of a line, since a value may be C
    [section]           sections may repeat; their order is kept
    key = value         keys may repeat; their order is kept. The value is
                        everything after `= `, leading spaces included, so a
                        `code =` line keeps its indentation.

A file is a list of (section, [(key, value)]). The loaders below give each
family's file the shape tools/rsprot_gen.py builds from:

    spec/packets/<file>.ini
        [packet]            prot, dir, summary, doc*, source, source_path,
                            include*, bind*
        [struct:<name>]     doc*; then per field: field_doc*, field, note?
        [define:<name>]     doc*, value
        [layout]            one per version, in version order: revs, then
                            the version's reads in wire order -- `<ENC> = field`
                            for RSPROT_<ENC>(x, m->field), `code = <C>` for a
                            line the reads cannot say (indented under the
                            function's four spaces)

    spec/tables/osrs<N>.ini
        [table]             source
        [server] [client]   <NAME> = <opcode> <size> [<version it calls>]
        [zone_order]        <NAME> = <ordinal>

    spec/streams/<stream>.ini
        [version:<K>]                  revs, small_delta_bits | extended
        [version:<K>:fields]           <field> = <encoding type>
        [osrs<N>:bits]                 <NAME> = <flag bit>
        [osrs<N>:v<K>:function:<name>] params, clang_off?, code*
        [osrs<N>:v<K>:blocks]          <BIT> = <reader> [<constants field>]
        [osrs<N>:v<K>:constants]       <field>[.<field>...] = <C initializer>
"""
import os
import re

SECTION_RE = re.compile(r"^\[([^\]]+)\]$")


def read_ini(path):
    sections = []
    with open(path) as f:
        for number, raw in enumerate(f, 1):
            line = raw.rstrip("\n")
            stripped = line.strip()
            if not stripped or stripped.startswith(";"):
                continue
            m = SECTION_RE.match(stripped)
            if m:
                sections.append((m.group(1), []))
                continue
            key, eq, value = line.partition("=")
            if not eq or not sections:
                raise ValueError("%s:%d: not `key = value` inside a section: %r" % (path, number, line))
            if value.startswith(" "):
                value = value[1:]
            sections[-1][1].append((key.strip(), value))
    return sections


def parse_runs(text):
    """"223-228, 230" -> [[223, 228], [230, 230]]."""
    out = []
    for part in text.split(","):
        lo, _, hi = part.strip().partition("-")
        out.append([int(lo), int(hi or lo)])
    return out


def parse_bool(text):
    if text in ("yes", "true", "1"):
        return True
    if text in ("no", "false", "0"):
        return False
    raise ValueError("not a boolean: %r" % text)


def code_line(value):
    """A `code =` value back to its C line: the function body's four spaces."""
    return "    " + value if value else ""


def stem(path):
    return os.path.splitext(os.path.basename(path))[0]


def fail(path, section, why):
    raise ValueError("%s [%s]: %s" % (path, section, why))


# --- spec/packets -----------------------------------------------------------

def load_packet(path):
    p = {"file": stem(path), "structs": [], "layouts": []}
    for name, rows in read_ini(path):
        if name == "packet":
            for key, value in rows:
                if key in ("prot", "dir", "summary"):
                    p[key] = value
                elif key == "doc":
                    p.setdefault("doc", []).append(value)
                elif key == "source":
                    p.setdefault("source", {})["project"] = value
                elif key == "source_path":
                    p.setdefault("source", {})["path"] = value
                elif key == "include":
                    p.setdefault("includes", []).append(value)
                elif key == "bind":
                    p.setdefault("binds", []).append(value)
                else:
                    fail(path, name, "unknown key %r" % key)
        elif name.startswith("struct:"):
            s = {"name": name[len("struct:"):], "fields": []}
            pending = []
            for key, value in rows:
                if key == "doc":
                    s.setdefault("doc", []).append(value)
                elif key == "field_doc":
                    pending.append(value)
                elif key == "field":
                    field = {"decl": value}
                    if pending:
                        field["doc"] = pending
                        pending = []
                    s["fields"].append(field)
                elif key == "note":
                    s["fields"][-1]["note"] = value
                else:
                    fail(path, name, "unknown key %r" % key)
            if pending:
                fail(path, name, "field_doc with no field after it")
            p["structs"].append(s)
        elif name.startswith("define:"):
            d = {"name": name[len("define:"):]}
            for key, value in rows:
                if key == "doc":
                    d.setdefault("doc", []).append(value)
                elif key == "value":
                    d["value"] = value
                else:
                    fail(path, name, "unknown key %r" % key)
            p.setdefault("defines", []).append(d)
        elif name == "layout":
            layout = {"revs": None, "body": []}
            for key, value in rows:
                if key == "revs":
                    layout["revs"] = parse_runs(value)
                elif key == "code":
                    layout["body"].append(code_line(value))
                else:
                    layout["body"].append([key, value])
            if layout["revs"] is None:
                fail(path, name, "no revs")
            p["layouts"].append(layout)
        else:
            fail(path, name, "unknown section")
    for key in ("prot", "dir"):
        if key not in p:
            fail(path, "packet", "no %s" % key)
    return p


# --- spec/tables ------------------------------------------------------------

def table_value(text):
    return int(text) if re.match(r"^-?\d+$", text) else text


def load_table(path):
    m = re.match(r"^osrs(\d+)$", stem(path))
    if not m:
        raise ValueError("%s: a table is spec/tables/osrs<N>.ini" % path)
    t = {"revision": int(m.group(1)), "server": [], "client": [], "zone_order": []}
    zones = []
    for name, rows in read_ini(path):
        if name == "table":
            for key, value in rows:
                if key != "source":
                    fail(path, name, "unknown key %r" % key)
                t["source"] = value
        elif name in ("server", "client"):
            for key, value in rows:
                parts = value.split()
                if len(parts) not in (2, 3):
                    fail(path, name, "%s: want `<opcode> <size> [<version>]`" % key)
                row = [key, int(parts[0]), table_value(parts[1])]
                if len(parts) == 3:
                    row.append(parts[2])
                t[name].append(row)
        elif name == "zone_order":
            zones += [(int(value), key) for key, value in rows]
        else:
            fail(path, name, "unknown section")
    zones.sort()
    if [n for n, _ in zones] != list(range(len(zones))):
        fail(path, "zone_order", "ordinals are not 0..%d" % (len(zones) - 1))
    t["zone_order"] = [key for _, key in zones]
    return t


# --- spec/streams -----------------------------------------------------------

CARRY_RE = re.compile(r"^osrs(\d+):v(\d+):(blocks|constants|function:(\w+))$")


def load_stream(path):
    spec = {"stream": stem(path), "versions": [], "revisions": []}
    versions = {}
    revisions = {}

    def revision(n):
        if n not in revisions:
            revisions[n] = {"revision": n, "carries": []}
            spec["revisions"].append(revisions[n])
        return revisions[n]

    def carry(n, k):
        r = revision(n)
        for c in r["carries"]:
            if c["version"] == k:
                return c
        c = {"version": k}
        r["carries"].append(c)
        return c

    for name, rows in read_ini(path):
        m = re.match(r"^version:(\d+)(:fields)?$", name)
        if m:
            k = int(m.group(1))
            if m.group(2):
                versions[k]["fields"] = [[key, value] for key, value in rows]
                continue
            v = {"version": k}
            for key, value in rows:
                if key == "revs":
                    v["revs"] = parse_runs(value)
                elif key == "small_delta_bits":
                    v[key] = int(value)
                elif key == "extended":
                    v[key] = parse_bool(value)
                else:
                    fail(path, name, "unknown key %r" % key)
            versions[k] = v
            spec["versions"].append(v)
            continue
        m = re.match(r"^osrs(\d+):bits$", name)
        if m:
            revision(int(m.group(1)))["bits"] = [[key, value] for key, value in rows]
            continue
        m = CARRY_RE.match(name)
        if not m:
            fail(path, name, "unknown section")
        c = carry(int(m.group(1)), int(m.group(2)))
        if m.group(3) == "blocks":
            c["blocks"] = []
            for key, value in rows:
                parts = value.split()
                if len(parts) not in (1, 2):
                    fail(path, name, "%s: want `<reader> [<field>]`" % key)
                c["blocks"].append([key, parts[0], parts[1] if len(parts) == 2 else None])
        elif m.group(3) == "constants":
            c["constants"] = {}
            for key, value in rows:
                at = c["constants"]
                path_keys = key.split(".")
                for part in path_keys[:-1]:
                    at = at.setdefault(part, {})
                at[path_keys[-1]] = value
        else:
            f = {"name": m.group(4), "body": []}
            for key, value in rows:
                if key == "params":
                    f["params"] = value
                elif key == "clang_off":
                    f["clang_off"] = parse_bool(value)
                elif key == "code":
                    f["body"].append(code_line(value))
                else:
                    fail(path, name, "unknown key %r" % key)
            c.setdefault("functions", []).append(f)
    for r in spec["revisions"]:
        for c in r["carries"]:
            if "constants" not in c:
                fail(path, "osrs%d:v%d:constants" % (r["revision"], c["version"]), "missing")
    return spec
