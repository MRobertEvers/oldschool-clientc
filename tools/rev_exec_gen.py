#!/usr/bin/env python3
"""Generate each supported revision's executor: src/net/rev/<rev>/<rev>_exec.c.

    tools/rev_exec_gen.py            write them
    tools/rev_exec_gen.py --check    exit 1 if any is stale

One file per section of src/net/rev/revisions.ini. A revision's executor is the
client's whole protocol at that revision, stated as calls:

  - server -> client: one arm per opcode in RSProt's table for the revision
    (3rd/rsprot/spec/tables/<rev>.ini). The arm runs the codec the revision
    carries -- packet_<file>_<rev>_out, the vX its packet header names for it --
    then the message's adapter (src/net/proto/proto_adapt.h);
  - zone sub-packets: the same, by the ordinal the revision's zone order gives;
  - client -> server: one arm per client prot, filling the message from
    struct ProtoOutArgs (w_<file>, src/net/proto/proto_out_writers.h) and
    running packet_<file>_<rev>_in;
  - the revision's GameProtoRevTable, GameProtoRev_<REV>(): its opcode tables,
    the arms, its info-stream entry points, its appearance shape and login.

Nothing is looked up at run time: which codec a prot calls at a revision is
decided here, once, by the rule RSProt's registry states (the row's version,
else its name, else the name with _V1 added or removed).

Inputs: RSProt's tables and packet headers, src/net/rev/prot_names.ini (prot ->
canonical name), src/net/rev/revisions.ini, and the declarations in
src/net/proto/proto_adapt.h (which files have an adapter, and what it takes).
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RSPROT = os.path.join(ROOT, "3rd", "rsprot")
sys.path.insert(0, os.path.join(RSPROT, "tools"))
import rsprot_spec  # noqa: E402

PACKETS = os.path.join(RSPROT, "packets")
STREAMS = os.path.join(RSPROT, "streams")
TABLES = os.path.join(RSPROT, "spec", "tables")
REV_DIR = os.path.join(ROOT, "src", "net", "rev")
NAMES_INI = os.path.join(REV_DIR, "prot_names.ini")
REVISIONS_INI = os.path.join(REV_DIR, "revisions.ini")
ADAPT_H = os.path.join(ROOT, "src", "net", "proto", "proto_adapt.h")
WRITERS_H = os.path.join(ROOT, "src", "net", "proto", "proto_out_writers.h")

GENERATOR = "tools/rev_exec_gen.py"

TABLE_RE = re.compile(r"extern const RsprotVersionRange rsprot_(\w+)_(out|in)\[\];")
BIND_RE = re.compile(r"^/\* rsprot-bind: (\w+) \*/$", re.M)
MSG_RE = re.compile(r"void packet_(\w+)_v\d+_(out|in)\(RsprotExec \*x, (\w+) \*m\);")
STRUCT_RE = re.compile(r"typedef struct (\w+) \{(.*?)\} (\w+);", re.S)
FIELD_RE = re.compile(r"^\s*(?:const\s+)?[\w ]+?\s*\**\s*(\w+)(?:\[\w+\])*;", re.M)
DECL_RE = re.compile(r"^(?:int|void)\n((?:adapt|prep)_\w+)\((.*?)\);", re.M | re.S)
WRITER_RE = re.compile(r"^static (?:inline )?void\n(w_\w+)\(", re.M)

APPEARANCE = {"v2": "APPEARANCE_ENC_V2", "v3": "APPEARANCE_ENC_V3", "v4": "APPEARANCE_ENC_V4", "v5": "APPEARANCE_ENC_V5", "v6": "APPEARANCE_ENC_V6",
              "classic": "APPEARANCE_ENC_CLASSIC", "classic_lc289": "APPEARANCE_ENC_CLASSIC_LC289"}
# A login driver's vtable; `lc` is the classic handshake net.c drives itself
# (net/login/login_lc.c), which a table states with no vtable.
LOGIN = {"osrs": "&g_login_osrs_vtable", "lc": "NULL"}


# --- inputs -----------------------------------------------------------------

class Packets:
    """Every packet file: the prots it answers for, its message per direction,
    whether that message has fields, and which revisions it has an entry
    point for."""

    def __init__(self):
        self.by_prot = {"out": {}, "in": {}}
        self.msg = {}
        self.empty = {}
        self.entries = {}
        for name in sorted(os.listdir(PACKETS)):
            if not name.endswith(".h"):
                continue
            text = open(os.path.join(PACKETS, name)).read()
            binds = BIND_RE.findall(text)
            structs = {m.group(3): m.group(2) for m in STRUCT_RE.finditer(text)}
            for m in TABLE_RE.finditer(text):
                f, d = m.group(1), m.group(2)
                for prot in binds or [f.upper()]:
                    self.by_prot[d][prot] = f
            for m in MSG_RE.finditer(text):
                f, d, t = m.group(1), m.group(2), m.group(3)
                self.msg[(f, d)] = t
                body = re.sub(r"/\*.*?\*/", "", structs.get(t, ""), flags=re.S)
                fields = FIELD_RE.findall(body)
                self.empty[(f, d)] = t in structs and all(x.startswith("_unused") for x in fields)
            for m in re.finditer(r"^#define packet_(\w+)_((?:osrs|rs)\w+?)_(out|in) ", text, re.M):
                self.entries.setdefault((m.group(1), m.group(3)), set()).add(m.group(2))

    def resolve(self, prot, version, d, rev):
        """The packet file `prot` (a table row naming `version`, or None) calls
        at `rev`, by RSProt's registry rule; None when it has no codec there."""
        def has(key):
            f = self.by_prot[d].get(key)
            return f if f and rev in self.entries.get((f, d), ()) else None

        if version:
            return has(version)
        f = has(prot)
        if f:
            return f
        alt = prot[:-3] if prot.endswith("_V1") else prot + "_V1"
        return has(alt)


def read_names():
    out = {"server": {}, "client": {}}
    for section, rows in rsprot_spec.read_ini(NAMES_INI):
        for key, value in rows:
            out[section][key] = value
    return out


def read_revisions():
    return [(name, dict(rows)) for name, rows in rsprot_spec.read_ini(REVISIONS_INI)]


def read_adapters():
    """name -> its parameter names, from proto_adapt.h."""
    text = open(ADAPT_H).read()
    out = {}
    for m in DECL_RE.finditer(text):
        params = [p.strip() for p in m.group(2).replace("\n", " ").split(",")]
        out[m.group(1)] = [re.split(r"[\s*]+", p)[-1] for p in params]
    return out


def stream_entries(rev):
    """Every stream entry point `rev` has, from the headers that declare them:
    generated per-revision files and hand-written readers alike."""
    text = ""
    for name in sorted(os.listdir(STREAMS)):
        if name.endswith(".h"):
            text += open(os.path.join(STREAMS, name)).read()
    return set(re.findall(r"\b(\w+_%s)\(" % rev, text))


# --- one revision -----------------------------------------------------------

class Gen:
    def __init__(self, rev, conf, packets, names, adapters, writers):
        self.rev = rev
        self.conf = conf
        self.p = packets
        self.names = names
        self.adapters = adapters
        self.writers = writers
        self.table = rsprot_spec.load_table(os.path.join(TABLES, rev + ".ini"))
        self.family = self.table["revision"].family
        self.streams = stream_entries(rev)
        self.files = set()
        self.notes = []
        self.errors = []

    def size(self, s):
        return {"VARU8": "PKTIN_LENGTH_VARU8", "VARU16": "PKTIN_LENGTH_VARU16"}.get(s, str(s))

    def call(self, name, f):
        """The adapter call, its arguments by the declaration's parameter names."""
        args = {"m": "&m", "out": "out", "revision": str(self.table["revision"].major),
                "data": "data", "len": "len", "codec": "(RsprotCodecFn)packet_%s_%s_out" % (f, self.rev),
                "zone": "zone_sub"}
        return "%s(%s)" % (name, ", ".join(args[p] for p in self.adapters[name]))

    def decode(self, f, cursor, extra=()):
        """Lines declaring `m` (and `extra`) and running the revision's codec
        for `f` over `cursor` (an RsprotBuf*), which the caller has wrapped."""
        self.files.add(f)
        lines = ["    %s m;" % self.p.msg[(f, "out")], "    RsprotExec x;"] + list(extra)
        lines += ["", "    memset(&m, 0, sizeof(m));"]
        if "prep_" + f in self.adapters:
            lines.append("    prep_%s(&m);" % f)
        return lines, ["    rsprot_exec_decode(&x, %s);" % cursor,
                       "    packet_%s_%s_out(&x, &m);" % (f, self.rev)]

    def info_arm(self, prot, key, canon):
        if canon == "PLAYER_INFO":
            return ["    return adapt_player_info(data, len, out);"]
        if self.family == "rs" and canon == "NPC_INFO":
            # One npc stream, read by the revision's own reader (npc_stream).
            return ["    return adapt_npc_info(data, len, out);"]
        if canon in ("NPC_INFO", "NPC_INFO_LARGE"):
            npc = self.conf["npc_info"].upper()
            want = "NPC_INFO_%s_%s" % ("SMALL" if canon == "NPC_INFO" else "LARGE", npc)
            if key != want:
                return None
            return ["    return adapt_npc_info(data, len, out);"]
        if canon == "WORLDENTITY_INFO":
            fn = "worldentity_info_v%s_%s" % (key.rsplit("_V", 1)[1], self.rev)
            if fn not in self.streams:
                self.errors.append("%s: no stream entry %s" % (prot, fn))
                return None
            return ["    return adapt_worldentity_info(%s, data, len, out);" % fn]
        return False

    def server_arm(self, row):
        prot, code, size = row[0], row[1], row[2]
        version = row[3] if len(row) > 3 else None
        key = version or prot
        # A row names the prot, and where its message is filed under another
        # name, the version: 223's NPC_INFO_SMALL is NPC_INFO_SMALL_V4.
        canon = self.names["server"].get(prot) or (version and self.names["server"].get(version))
        if canon is None:
            return None, None
        info = self.info_arm(prot, key, canon)
        if info is None:
            return None, None
        if info:
            return canon, info
        f = self.p.resolve(prot, version, "out", self.rev)
        if f is None:
            self.notes.append("%s (%s): no codec at this revision" % (prot, canon))
            return None, None
        decls, run = self.decode(f, "&b", ["    RsprotBuf b;"])
        lines = decls + ["    rsprot_buf_wrap_read(&b, data, len);"] + run
        lines += ["    if( !rev_exec_whole(&x, &b, len) )", "        return 0;"]
        if "adapt_" + f in self.adapters:
            lines.append("    return %s;" % self.call("adapt_" + f, f))
        elif "adapt_zone_" + f in self.adapters:
            lines.insert(1, "    struct PktZoneSubPacket sub;")
            lines += ["    memset(&sub, 0, sizeof(sub));",
                      "    if( %s != 1 )" % self.call("adapt_zone_" + f, f).replace("out)", "&sub)"),
                      "        return 0;",
                      "    proto_adapt_zone_to_packet(&sub, out);",
                      "    return 1;"]
        elif self.p.empty.get((f, "out")):
            lines += ["    (void)out;", "    return 1;"]
        else:
            self.errors.append("%s -> %s (%s): a canonical name and no adapter_%s in proto_adapt.h"
                               % (prot, f, canon, f))
            return None, None
        return canon, lines

    def zone_case(self, ordinal, prot, version=None):
        f = self.p.resolve(prot, version, "out", self.rev)
        if f is None:
            return ["    case %d: /* %s: no codec at this revision */" % (ordinal, prot),
                    "        return 0;"]
        decls, run = self.decode(f, "c")
        body = ["    case %d: /* %s */" % (ordinal, prot), "    {"]
        body += ["    " + l if l else "" for l in decls + run]
        body += ["        if( !rsprot_exec_ok(&x) || c->err )", "            return 0;"]
        if "adapt_zone_" + f in self.adapters:
            body.append("        return %s;" % self.call("adapt_zone_" + f, f))
        else:
            body += ["        (void)m;", "        return 2; /* read; no canonical arm */"]
        body.append("    }")
        return body

    def client_arm(self, row):
        prot = row[0]
        version = row[3] if len(row) > 3 else None
        canon = self.names["client"].get(prot)
        if canon is None:
            return None, None
        f = self.p.resolve(prot, version, "in", self.rev)
        if f is None:
            self.notes.append("%s (%s): no client codec at this revision" % (prot, canon))
            return None, None
        if "w_" + f not in self.writers:
            self.notes.append("%s -> %s: no writer (proto_out_writers.h)" % (prot, f))
            return None, None
        self.files.add(f)
        return canon, ["    %s m;" % self.p.msg[(f, "in")], "    RsprotExec x;", "",
                       "    memset(&m, 0, sizeof(m));",
                       "    w_%s(&m, a);" % f,
                       "    rsprot_exec_encode(&x, b);",
                       "    packet_%s_%s_in(&x, &m);" % (f, self.rev),
                       "    if( b->err )",
                       "        return 0; /* it does not fit the caller's buffer */",
                       "    /* Arguments the layout cannot carry are the caller's bug. */",
                       "    assert(rsprot_exec_ok(&x));",
                       "    return 1;"]

    def generate(self):
        rev = self.rev
        R = rev.upper()
        n = self.table["revision"].major
        server = [r for r in self.table["server"] if r[1] >= 0]
        client = [r for r in self.table["client"] if r[1] >= 0]
        top = max([r[1] for r in server] + [r[1] for r in client]) + 1

        in_arms = []
        in_rows = []
        name_wire = {}
        for row in server:
            canon, lines = self.server_arm(row)
            fn = None
            if lines:
                fn = "in_" + row[0].lower()
                in_arms += ["/* %s, opcode %d */" % (row[0], row[1]), "static int",
                            "%s(uint8_t const* data, int len, struct RevPacket* out)" % fn, "{"]
                in_arms += lines + ["}", ""]
                name_wire.setdefault(canon, row[1])
            in_rows.append((row, canon if lines else None, fn))

        by_opcode = self.table["zone_index"] == "opcode"
        zone = ["/* The revision's zone sub-packets, by the %s that leads each. */"
                % ("top-level opcode" if by_opcode else "ordinal"),
                "static int", "zone_sub(RsprotBuf* c, struct PktZoneSubPacket* out)", "{",
                "    int ordinal;", "",
                "    if( rsprot_buf_readable(c) <= 0 || c->err )", "        return -1;",
                "    ordinal = rsprot_g1(c);", "    memset(out, 0, sizeof(*out));",
                "    switch( ordinal )", "    {"]
        if by_opcode:
            # A zone sub-packet is a top-level prot whose message has a zone
            # adapter; LostCity leads each with that prot's own opcode.
            for row in server:
                version = row[3] if len(row) > 3 else None
                f = self.p.resolve(row[0], version, "out", self.rev)
                if f and "adapt_zone_" + f in self.adapters:
                    zone += self.zone_case(row[1], row[0], version)
        else:
            for i, prot in enumerate(self.table["zone_order"]):
                zone += self.zone_case(i, prot)
        zone += ["    default:", "        return 0;", "    }", "}", ""]

        out_arms = []
        out_rows = {}
        for row in client:
            canon, lines = self.client_arm(row)
            if not lines or canon in out_rows:
                continue
            fn = "out_" + row[0].lower()
            out_arms += ["/* %s, opcode %d */" % (row[0], row[1]), "static int",
                         "%s(struct ProtoOutArgs const* a, RsprotBuf* b)" % fn, "{"]
            out_arms += lines + ["}", ""]
            out_rows[canon] = (row[1], fn)
        client_names = {}
        for row in client:
            canon = self.names["client"].get(row[0])
            if canon is not None:
                client_names.setdefault(canon, row[1])

        if self.errors:
            raise SystemExit("%s:\n  %s" % (rev, "\n  ".join(self.errors)))

        if self.family == "osrs":
            npc = self.conf["npc_info"]
            for fn in ("player_info_" + rev, "npc_info_small_%s_%s" % (npc, rev), "npc_info_large_%s_%s" % (npc, rev)):
                if fn not in self.streams:
                    raise SystemExit("%s: no stream entry %s" % (rev, fn))

        o = ["/*",
             " * Revision %s: the client's protocol at this revision, as calls." % rev,
             " *",
             " * GENERATED by %s from 3rd/rsprot/spec/tables/%s.ini, 3rd/rsprot/packets/," % (GENERATOR, rev),
             " * src/net/rev/prot_names.ini, src/net/rev/revisions.ini and",
             " * src/net/proto/proto_adapt.h -- do not edit.",
             " *",
             " * Each server opcode's arm runs the codec this revision carries for its prot",
             " * (packet_<file>_%s_out) and hands the message to its adapter; each client" % rev,
             " * packet's arm fills its message and runs packet_<file>_%s_in." % rev]
        if self.notes:
            o += [" *", " * Canonical prots with no arm at this revision:"]
            o += [" *   " + s for s in self.notes]
        o += [" */", '#include "net/rev/rev_exec.h"', ""]
        o += ['#include "packets/%s.h"' % f for f in sorted(self.files)]
        o += ["", "#include <assert.h>", "#include <string.h>", "", "/* clang-format off */", "",
              "static int", "zone_sub(RsprotBuf* c, struct PktZoneSubPacket* out);", ""]
        o += in_arms + zone + out_arms

        o += ["#define TOP %d" % top, "#define NO_PROT (-100)", "",
              "/* Server opcode -> its arm, its canonical name, its size. */",
              "static int (*const k_in[TOP])(uint8_t const* data, int len, struct RevPacket* out) = {"]
        o += ["    [%d] = %s," % (r[1], fn) for r, c, fn in in_rows if fn]
        o += ["};", "", "static int16_t const k_in_name[TOP] = {"]
        o += ["    [%d] = PKT_NAME_%s," % (r[1], c) for r, c, fn in in_rows if c]
        in_sizes = {r[1]: self.size(r[2]) for r in server}
        o += ["};", "", "/* Every opcode's size; NO_PROT where the revision has none. */",
              "static int16_t const k_in_size[TOP] = {"]
        o += ["    [%d] = %s," % (w, in_sizes.get(w, "NO_PROT")) for w in range(top)]
        o += ["};", "", "/* A canonical name's first opcode, plus one. */",
              "static int16_t const k_name_wire1[PKT_NAME_COUNT] = {"]
        o += ["    [PKT_NAME_%s] = %d," % (c, w + 1) for c, w in sorted(name_wire.items(), key=lambda x: x[1])]
        o += ["};", "", "/* Client: canonical name -> opcode plus one; opcode -> size plus one. */",
              "static int16_t const k_out_wire1[PKTOUT_NAME_COUNT] = {"]
        o += ["    [PKTOUT_NAME_%s] = %d," % (c, w + 1) for c, w in sorted(client_names.items(), key=lambda x: x[1])]
        out_sizes = {r[1]: self.size(r[2]) for r in client}
        o += ["};", "", "static int16_t const k_out_size[TOP] = {"]
        o += ["    [%d] = %s," % (w, out_sizes.get(w, "NO_PROT")) for w in range(top)]
        o += ["};", "", "static int (*const k_out[PKTOUT_NAME_COUNT])(struct ProtoOutArgs const* a, RsprotBuf* b) = {"]
        o += ["    [PKTOUT_NAME_%s] = %s," % (c, fn) for c, (w, fn) in sorted(out_rows.items(), key=lambda x: x[1][0])]
        o += ["};", "", "/* clang-format on */", ""]

        o += ["static int", "in_size(int wire)", "{",
              "    return wire < 0 || wire >= TOP || k_in_size[wire] == NO_PROT ? 0 : k_in_size[wire];", "}", "",
              "static int", "in_code(int wire)", "{",
              "    return wire < 0 || wire >= TOP ? PKT_NAME_NONE : k_in_name[wire];", "}", "",
              "static int", "in_wire(int name)", "{",
              "    return name <= 0 || name >= PKT_NAME_COUNT ? -1 : k_name_wire1[name] - 1;", "}", "",
              "static int", "out_code(int name)", "{",
              "    return name <= 0 || name >= PKTOUT_NAME_COUNT ? -1 : k_out_wire1[name] - 1;", "}", "",
              "static int", "out_size(int wire)", "{",
              "    return wire < 0 || wire >= TOP || k_out_size[wire] == NO_PROT ? 0 : k_out_size[wire];", "}", "",
              "static int",
              "exec_in(struct GameProtoRevTable const* rev, int wire, uint8_t const* data, int len, struct RevPacket* out)",
              "{",
              "    (void)rev;",
              "    if( wire < 0 || wire >= TOP || !k_in[wire] )",
              "        return 0;",
              "    out->packet_type = (enum GameProtoPktName)k_in_name[wire];",
              "    return k_in[wire](data, len, out);", "}", "",
              "static int",
              "exec_out(int out_name, struct ProtoOutArgs const* args, uint8_t* buf, int cap)",
              "{",
              "    RsprotBuf b;", "",
              "    if( out_name <= 0 || out_name >= PKTOUT_NAME_COUNT || !k_out[out_name] )",
              "        return -1;",
              "    rsprot_buf_wrap(&b, buf, cap);",
              "    return k_out[out_name](args, &b) ? b.wpos : -1;", "}", "",
              "static int",
              "exec_has(int out_name)",
              "{",
              "    return out_name > 0 && out_name < PKTOUT_NAME_COUNT && k_out[out_name] != NULL;", "}", "",
              "static int", "appear(uint8_t const* d, int l, struct PktPlayerAppearance* o)", "{",
              "    return PktPlayerAppearance_DecodeAs(o, %s, d, l);" % APPEARANCE[self.conf["appearance"]], "}", ""]
        if self.family == "osrs":
            o += ["static int", "pinfo(uint8_t const* d, int l, struct RsprotInfoList* o)", "{",
                  "    return proto_info_player_read(player_info_%s, d, l, o);" % rev, "}", "",
                  "static int", "ninfo(uint8_t const* d, int l, struct RsprotInfoList* o)", "{",
                  "    return proto_info_npc_read(npc_info_small_%s_%s, d, l, o);" % (npc, rev), "}", "",
                  "static int", "ninfo_large(uint8_t const* d, int l, struct RsprotInfoList* o)", "{",
                  "    return proto_info_npc_read(npc_info_large_%s_%s, d, l, o);" % (npc, rev), "}", ""]
            family = ["    .revision = GAMEPROTO_REVISION_OSRS,",
                      "    /* pSmart1Or2: two bytes once a server opcode passes 0x7F. */",
                      "    .opcode_smart2 = %d," % (1 if max(r[1] for r in server) >= 0x80 else 0),
                      "    .component_id_bytes = 4,",
                      "    .reconnect_kind = NET_RECONNECT_SEED,",
                      "    .player_info_read = pinfo,",
                      "    .npc_info_read = ninfo,",
                      "    .npc_info_read_large = ninfo_large,",
                      "    .player_info_set_local = proto_info_set_local,",
                      "    .player_info_init = proto_info_init,",
                      "    .player_info_state_size = proto_info_state_size,",
                      "    .player_info_state_save = proto_info_state_save,",
                      "    .player_info_state_load = proto_info_state_load,"]
        else:
            family = ["    .revision = GAMEPROTO_REVISION_RS,",
                      "    /* One opcode byte, a 2-byte component id, and LostCity's re-login on",
                      "     * opcode 18 with an otherwise identical block. */",
                      "    .opcode_smart2 = 0,",
                      "    .component_id_bytes = 0,",
                      "    .reconnect_kind = NET_RECONNECT_CREDS,",
                      "    .player_info_read = %s," % self.conf["player_stream"],
                      "    .npc_info_read = %s," % self.conf["npc_stream"]]
        jag = self.conf.get("jag_checksum")
        o += ["static struct GameProtoRevTable k_rev = {",
              '    .name = "%s",' % rev,
              "    .number = %d," % n,
              "    .client_version = %d," % n]
        if jag:
            o += ["    .jag_checksum = { %s }," % jag]
        o += ["    .packetin_size = in_size,",
              "    .packetin_code = in_code,",
              "    .packetin_wire = in_wire,",
              "    .packetout_code = out_code,",
              "    .packetout_size = out_size,",
              "    .transport_kind = NET_TRANSPORT_TCP,",
              "    .opcode_plaintext = 0,",
              "    .server_tick_ms = 600,",
              "    .login = %s," % LOGIN[self.conf["login"]],
              "    .parse_wire = exec_in,",
              "    .encode = exec_out,",
              "    .encodes = exec_has,",
              "    .appearance_decode = appear,"] + family + ["};", ""]
        o += ["struct GameProtoRevTable const*", "GameProtoRev_%s(void)" % R, "{"]
        if self.conf.get("jag_env"):
            # The server instance's CRCs, when the boot gives them (the env
            # is read once; the table is the process's).
            o += ["    static int loaded;", "",
                  "    if( !loaded )", "    {", "        loaded = 1;",
                  '        GameProtoRev_JagChecksumsFromEnv(&k_rev, "%s");' % self.conf["jag_env"], "    }"]
        o += ["    return &k_rev;", "}", ""]
        return "\n".join(o)


def generated():
    packets = Packets()
    names = read_names()
    adapters = read_adapters()
    writers = set(WRITER_RE.findall(open(WRITERS_H).read()))
    files = {}
    revs = read_revisions()
    for rev, conf in revs:
        files[os.path.join(REV_DIR, rev, rev + "_exec.c")] = Gen(rev, conf, packets, names, adapters, writers).generate()
    files[os.path.join(REV_DIR, "rev_list.h")] = rev_list([r for r, _ in revs])
    return files


def rev_list(revs):
    o = ["/*", " * The client's supported revisions: GENERATED by %s from" % GENERATOR,
         " * src/net/rev/revisions.ini -- do not edit. Each is src/net/rev/<rev>/<rev>_exec.c.",
         " */", "#ifndef SRC_NET_REV_REV_LIST_H", "#define SRC_NET_REV_REV_LIST_H", "",
         "struct GameProtoRevTable;", ""]
    for r in revs:
        o += ["struct GameProtoRevTable const*", "GameProtoRev_%s(void);" % r.upper()]
    o += ["", "/* X(ID, number) for every supported revision: GameProtoRev_##ID. */", "#define REV_LIST(X) \\"]
    o += ["    X(%s, %d) \\" % (r.upper(), int(re.sub(r"\D", "", r))) for r in revs]
    o += ["", "#endif", ""]
    return "\n".join(o)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    stale = 0
    for path, text in sorted(generated().items()):
        old = open(path).read() if os.path.exists(path) else None
        if old == text:
            continue
        stale += 1
        if args.check:
            print("stale: " + os.path.relpath(path, ROOT))
            continue
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            f.write(text)
        print("wrote " + os.path.relpath(path, ROOT))
    if args.check and stale:
        sys.exit(1)


if __name__ == "__main__":
    main()
