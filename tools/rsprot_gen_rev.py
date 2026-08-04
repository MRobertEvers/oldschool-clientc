#!/usr/bin/env python3
"""
Generate a revision module's wire tables from RSProt.

    tools/rsprot_gen_rev.py 239 --out src/net/rev/osrs239

Writes `packetin.h` (server->client), `packetout.h` (client->server) and
`zoneprot.h` (the UPDATE_ZONE_PARTIAL_ENCLOSED sub-packet index). Every prot the
revision defines gets a row, including the ones this engine has no decoder for:
a row it cannot decode still frames to the right length, and a length this table
does not state is a stream desync rather than a dropped packet.

The two maps below are the only hand-written part, and they are the only part
that can be wrong in a way the compiler will not catch. `CANON_IN` / `CANON_OUT`
say which RSProt prot answers to which of this engine's canonical packet names
(src/net/rev/pktnames.h). A name mapped here is a claim that the *payload* this
engine reads matches what the revision writes — mapping a prot whose layout
changed is how you get a packet that frames cleanly and decodes to garbage. When
a revision renames a prot with a `_V<n>` suffix it is telling you the layout
moved; check the encoder before carrying the mapping forward.

Prots RSProt gives opcode -1 exist only inside UPDATE_ZONE_PARTIAL_ENCLOSED.
They keep their row (the size is still the truth for the sub-packet) and are
emitted with code -1, which no wire byte can match.
"""

import argparse
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# --- RSProt server prot -> canonical PKT_NAME_*, plus why ------------------
#
# Unmapped prots are emitted as PKT_NAME_NONE: framed and dropped.
CANON_IN = {
    "IF_OPENTOP": "IF_OPENTOP",
    "IF_OPENSUB": "IF_OPENSUB",
    "IF_CLOSESUB": "IF_CLOSESUB",
    "IF_MOVESUB": "IF_MOVESUB",
    # V2 widened the events bitmask from 12 to 16 bytes (two extra shorts for
    # the sub-op range). The reader is per-rev; see osrs239_parse.c.
    "IF_SETEVENTS_V2": "IF_SETEVENTS",
    "IF_SETPOSITION": "IF_SETPOSITION",
    "IF_SETSCROLLPOS": "IF_SETSCROLLPOS",
    "IF_SETTEXT": "IF_SETTEXT",
    "IF_SETHIDE": "IF_SETHIDE",
    "IF_SETOBJECT": "IF_SETOBJECT",
    "IF_SETCOLOUR": "IF_SETCOLOUR",
    "IF_SETANIM": "IF_SETANIM",
    "IF_SETNPCHEAD": "IF_SETNPCHEAD",
    "IF_SETPLAYERHEAD": "IF_SETPLAYERHEAD",
    # V2 carries a 4-byte combined component uid where the old one carried a
    # 2-byte flat id.
    "IF_SETMODEL_V2": "IF_SETMODEL",
    "MIDI_SONG_V2": "MIDI_SONG",
    "MIDI_JINGLE": "MIDI_JINGLE",
    "SYNTH_SOUND": "SYNTH_SOUND",
    "UPDATE_ZONE_FULL_FOLLOWS": "UPDATE_ZONE_FULL_FOLLOWS",
    "UPDATE_ZONE_PARTIAL_FOLLOWS": "UPDATE_ZONE_PARTIAL_FOLLOWS",
    "UPDATE_ZONE_PARTIAL_ENCLOSED": "UPDATE_ZONE_PARTIAL_ENCLOSED",
    "LOC_ADD_CHANGE_V2": "LOC_ADD_CHANGE",
    "LOC_DEL": "LOC_DEL",
    "LOC_ANIM": "LOC_ANIM",
    "LOC_MERGE": "LOC_MERGE",
    "OBJ_ADD": "OBJ_ADD",
    "OBJ_DEL": "OBJ_DEL",
    "OBJ_COUNT": "OBJ_COUNT",
    "OBJ_ENABLED_OPS": "OBJ_REVEAL",
    "MAP_ANIM": "MAP_ANIM",
    "MAP_PROJANIM_V2": "MAP_PROJANIM",
    "PLAYER_INFO": "PLAYER_INFO",
    "NPC_INFO_SMALL_V5": "NPC_INFO",
    "REBUILD_NORMAL_V2": "REBUILD_NORMAL",
    "REBUILD_REGION_V2": "REBUILD_REGION",
    "VARP_SMALL": "VARP_SMALL",
    "VARP_LARGE": "VARP_LARGE",
    "VARP_RESET": "VARP_RESET",
    "VARP_SYNC": "VARP_SYNC",
    "CAM_SHAKE": "CAM_SHAKE",
    "CAM_RESET": "CAM_RESET",
    # V2 moved to absolute-coordinate camera targets (8 bytes, not the classic
    # 6-byte scene-local pair).
    "CAM_MOVETO_V2": "CAM_MOVETO",
    "CAM_LOOKAT_V2": "CAM_LOOKAT",
    "UPDATE_INV_FULL": "UPDATE_INV_FULL",
    "UPDATE_INV_PARTIAL": "UPDATE_INV_PARTIAL",
    "UPDATE_INV_STOPTRANSMIT": "UPDATE_INV_STOP_TRANSMIT",
    "MESSAGE_PRIVATE": "MESSAGE_PRIVATE",
    "FRIENDLIST_LOADED": "FRIENDLIST_LOADED",
    "UPDATE_FRIENDLIST": "UPDATE_FRIENDLIST",
    "UPDATE_IGNORELIST": "UPDATE_IGNORELIST",
    "LOGOUT": "LOGOUT",
    "UPDATE_RUNWEIGHT": "UPDATE_RUNWEIGHT",
    "UPDATE_RUNENERGY": "UPDATE_RUNENERGY",
    # One prot for both the set and the clear; the payload says which.
    "SET_MAP_FLAG_V2": "UNSET_MAP_FLAG",
    "SET_PLAYER_OP": "SET_PLAYER_OP",
    "UPDATE_STAT_V2": "UPDATE_STAT",
    "RUNCLIENTSCRIPT": "RUNCLIENTSCRIPT",
    "MESSAGE_GAME": "MESSAGE_GAME",
    "CHAT_FILTER_SETTINGS": "CHAT_FILTER_SETTINGS",
    "HINT_ARROW": "HINT_ARROW",
    "RESET_ANIMS": "RESET_ANIMS",
    "UPDATE_REBOOT_TIMER_V2": "UPDATE_REBOOT_TIMER",
    "SERVER_TICK_END": "SERVER_TICK_END",
}

# --- RSProt client prot -> canonical PKTOUT_NAME_* -------------------------
#
# The `_V2` op families all widened by one byte: rev 239 addresses an npc by a
# 16-bit index plus a control byte, and a loc/obj by (x, z, id, control).
CANON_OUT = {
    "NO_TIMEOUT": "NO_TIMEOUT",
    "IDLE": "IDLE_TIMER",
    "MAP_BUILD_COMPLETE": "MAP_BUILD_COMPLETE",
    "EVENT_APPLET_FOCUS": "EVENT_APPLET_FOCUS",
    "EVENT_CAMERA_POSITION": "EVENT_CAMERA_POSITION",
    "EVENT_MOUSE_MOVE": "EVENT_MOUSE_MOVE",
    "EVENT_MOUSE_CLICK_V1": "EVENT_MOUSE_CLICK",
    "MOVE_GAMECLICK": "MOVE_GAMECLICK",
    "MOVE_MINIMAPCLICK": "MOVE_MINIMAPCLICK",
    "IF_BUTTON": "IF_BUTTON",
    "IF_BUTTOND": "INV_BUTTOND",
    "CLICKWORLDMAP": "CLICK_WORLD_MAP",
    "RESUME_PAUSEBUTTON": "RESUME_PAUSEBUTTON",
    "CLOSE_MODAL": "CLOSE_MODAL",
    "RESUME_P_COUNTDIALOG": "RESUME_P_COUNTDIALOG",
    "OPNPC1_V2": "OPNPC1",
    "OPNPC2_V2": "OPNPC2",
    "OPNPC3_V2": "OPNPC3",
    "OPNPC4_V2": "OPNPC4",
    "OPNPC5_V2": "OPNPC5",
    "OPNPCT": "OPNPCT",
    "OPNPCU": "OPNPCU",
    "OPLOC1_V2": "OPLOC1",
    "OPLOC2_V2": "OPLOC2",
    "OPLOC3_V2": "OPLOC3",
    "OPLOC4_V2": "OPLOC4",
    "OPLOC5_V2": "OPLOC5",
    "OPLOCT": "OPLOCT",
    "OPLOCU": "OPLOCU",
    "OPOBJ1_V2": "OPOBJ1",
    "OPOBJ2_V2": "OPOBJ2",
    "OPOBJ3_V2": "OPOBJ3",
    "OPOBJ4_V2": "OPOBJ4",
    "OPOBJ5_V2": "OPOBJ5",
    "OPOBJT": "OPOBJT",
    "OPOBJU": "OPOBJU",
    "OPPLAYER1": "OPPLAYER1",
    "OPPLAYER2": "OPPLAYER2",
    "OPPLAYER3": "OPPLAYER3",
    "OPPLAYER4": "OPPLAYER4",
    "OPPLAYER5": "OPPLAYER5",
    "OPPLAYERT": "OPPLAYERT",
    "OPPLAYERU": "OPPLAYERU",
    "MESSAGE_PUBLIC": "MESSAGE_PUBLIC",
    "MESSAGE_PRIVATE": "MESSAGE_PRIVATE",
    "CLIENT_CHEAT": "CLIENT_CHEAT",
    "FRIENDLIST_ADD": "FRIENDLIST_ADD",
    "FRIENDLIST_DEL": "FRIENDLIST_DEL",
    "IGNORELIST_ADD": "IGNORELIST_ADD",
    "IGNORELIST_DEL": "IGNORELIST_DEL",
    "SET_CHATFILTERSETTINGS": "CHAT_SETMODE",
    "WINDOW_STATUS": "WINDOW_STATUS",
}

# Held-item and inventory-component ops have no prot of their own at this
# revision: OSRS routes every inventory click through IF_SUBOP / IF_BUTTONX,
# which name the component and the op number rather than the item. They are
# listed here so the omission reads as a decision instead of an oversight.
NO_PROT_OUT = {
    "OPHELD1": "IF_BUTTONX op 1 on the inventory component",
    "OPHELD2": "IF_BUTTONX op 2",
    "OPHELD3": "IF_BUTTONX op 3",
    "OPHELD4": "IF_BUTTONX op 4",
    "OPHELD5": "IF_BUTTONX op 5",
    "OPHELDT": "IF_BUTTONT",
    "OPHELDU": "IF_SUBOP / IF_BUTTONT",
    "INV_BUTTON1": "IF_BUTTONX",
    "INV_BUTTON2": "IF_BUTTONX",
    "INV_BUTTON3": "IF_BUTTONX",
    "INV_BUTTON4": "IF_BUTTONX",
    "INV_BUTTON5": "IF_BUTTONX",
    "IF_BUTTON1": "IF_BUTTONX with op=1",
    "IF_BUTTON2": "IF_BUTTONX with op=2",
    "IF_BUTTON3": "IF_BUTTONX with op=3",
    "IF_BUTTON4": "IF_BUTTONX with op=4",
    "IF_BUTTON5": "IF_BUTTONX with op=5",
    "IF_BUTTON6": "IF_BUTTONX with op=6",
    "IF_BUTTON7": "IF_BUTTONX with op=7",
    "IF_BUTTON8": "IF_BUTTONX with op=8",
    "IF_BUTTON9": "IF_BUTTONX with op=9",
    "IF_BUTTON10": "IF_BUTTONX with op=10",
}

BANNER = """/*
 * GENERATED by tools/rsprot_gen_rev.py -- do not edit.
 *
 * Source: RSProt {src}, revision {rev}.
 * Regenerate with:  tools/rsprot_gen_rev.py {rev} --out src/net/rev/osrs{rev}
 */"""


def size_literal(n, prefix):
    if n == -1:
        return f"{prefix}_LENGTH_VARU8"
    if n == -2:
        return f"{prefix}_LENGTH_VARU16"
    return str(n)


def gen_packetin(rev, rows, src):
    lines = [
        f"#ifndef SRC_NET_REV_OSRS{rev}_PACKETIN_H",
        f"#define SRC_NET_REV_OSRS{rev}_PACKETIN_H",
        "",
        BANNER.format(rev=rev, src=src),
        "",
        "/*",
        f" * OSRS revision {rev} server->client wire opcodes and payload sizes.",
        " *",
        " * Every prot the revision defines has a row, whether or not this engine",
        " * decodes it: an undecoded row still frames to the right length, and a length",
        " * this table omits desynchronises the stream rather than dropping one packet.",
        " * `name` is PKT_NAME_NONE for the rows with no decoder.",
        " *",
        " * Opcodes >= 0x80 are written by the server as TWO stream-cipher bytes",
        " * (`(op >>> 8) | 0x80`, then `op & 0xFF`) -- RSProt's pSmart1Or2Enc. The framer",
        " * reads that form when the revision sets `opcode_smart2`.",
        " *",
        " * A code of -1 means the prot has no top-level opcode: it exists only as a",
        " * sub-packet inside UPDATE_ZONE_PARTIAL_ENCLOSED (see zoneprot.h). Its size is",
        " * still the truth for that sub-packet.",
        " */",
        "",
        '#include "net/rev/pktnames.h"',
        "",
        "#ifndef PKTIN_LENGTH_VARU8",
        "#define PKTIN_LENGTH_VARU8 (-1)",
        "#define PKTIN_LENGTH_VARU16 (-2)",
        "#endif",
        "",
        f"struct Osrs{rev}PacketInDef",
        "{",
        "    int code;   /* wire opcode, or -1 for enclosed-only */",
        "    int length;",
        "    int name;   /* canonical GameProtoPktName, PKT_NAME_NONE if undecoded */",
        "    char const* prot; /* RSProt's own name, for logs and for grepping back */",
        "};",
        "",
        "/* clang-format off */",
        f"static const struct Osrs{rev}PacketInDef g_packet_in_definitions_osrs{rev}[] = {{",
    ]
    for r in rows:
        canon = CANON_IN.get(r["name"])
        nm = f"PKT_NAME_{canon}" if canon else "PKT_NAME_NONE"
        lines.append(
            "    { %4d, %-22s %-36s \"%s\" },"
            % (r["code"], size_literal(r["size"], "PKTIN") + ",", nm + ",", r["name"])
        )
    lines += [
        "};",
        "/* clang-format on */",
        "",
        f"#define OSRS{rev}_PACKET_IN_COUNT                                                                    \\",
        f"    ((int)(sizeof(g_packet_in_definitions_osrs{rev}) /                                               \\",
        f"           sizeof(g_packet_in_definitions_osrs{rev}[0])))",
        "",
        "static inline int",
        f"packetin_size_osrs{rev}(int wire_opcode)",
        "{",
        f"    for( int i = 0; i < OSRS{rev}_PACKET_IN_COUNT; i++ )",
        f"        if( g_packet_in_definitions_osrs{rev}[i].code == wire_opcode )",
        f"            return g_packet_in_definitions_osrs{rev}[i].length;",
        "    return 0; /* unknown -> zero-length; the framer completes and resets */",
        "}",
        "",
        "static inline int",
        f"packetin_code_osrs{rev}(int wire_opcode)",
        "{",
        f"    for( int i = 0; i < OSRS{rev}_PACKET_IN_COUNT; i++ )",
        f"        if( g_packet_in_definitions_osrs{rev}[i].code == wire_opcode )",
        f"            return g_packet_in_definitions_osrs{rev}[i].name;",
        "    return PKT_NAME_NONE;",
        "}",
        "",
        "static inline int",
        f"packetin_wire_osrs{rev}(int pkt_name)",
        "{",
        f"    for( int i = 0; i < OSRS{rev}_PACKET_IN_COUNT; i++ )",
        f"        if( g_packet_in_definitions_osrs{rev}[i].name == pkt_name &&",
        f"            g_packet_in_definitions_osrs{rev}[i].name != PKT_NAME_NONE )",
        f"            return g_packet_in_definitions_osrs{rev}[i].code;",
        "    return -1;",
        "}",
        "",
        "/** RSProt's own prot name for a wire opcode; NULL when unknown. Logs read as",
        " *  the revision spells them, so a trace can be grepped straight into RSProt. */",
        "static inline char const*",
        f"packetin_protname_osrs{rev}(int wire_opcode)",
        "{",
        f"    for( int i = 0; i < OSRS{rev}_PACKET_IN_COUNT; i++ )",
        f"        if( g_packet_in_definitions_osrs{rev}[i].code == wire_opcode )",
        f"            return g_packet_in_definitions_osrs{rev}[i].prot;",
        "    return 0;",
        "}",
        "",
        "#endif",
        "",
    ]
    return "\n".join(lines)


def gen_packetout(rev, rows, src):
    lines = [
        f"#ifndef SRC_NET_REV_OSRS{rev}_PACKETOUT_H",
        f"#define SRC_NET_REV_OSRS{rev}_PACKETOUT_H",
        "",
        BANNER.format(rev=rev, src=src),
        "",
        "/*",
        f" * OSRS revision {rev} client->server wire opcodes and payload sizes.",
        " *",
        " * Same rule as packetin.h: every prot gets a row so the receiving side (the",
        " * mock server) frames the whole stream, not only the packets it acts on.",
        " * `name` is PKTOUT_NAME_NONE for prots this engine never builds.",
        " *",
        " * Canonical names with NO row in this revision, and what replaced them:",
    ]
    for k, v in NO_PROT_OUT.items():
        lines.append(f" *   {k:<16} -> {v}")
    lines += [
        " *",
        " * The login prots (INIT_GAME_CONNECTION, GAMELOGIN, POW_REPLY) are not here:",
        " * they live in a different prot space and are built by the login driver.",
        " */",
        "",
        '#include "net/rev/pktnames.h"',
        "",
        "#ifndef PKTOUT_LENGTH_VARU8",
        "#define PKTOUT_LENGTH_VARU8 (-1)",
        "#define PKTOUT_LENGTH_VARU16 (-2)",
        "#endif",
        "",
        f"struct Osrs{rev}PacketOutDef",
        "{",
        "    int name;   /* canonical GameProtoPktOutName */",
        "    int code;",
        "    int length;",
        "    char const* prot;",
        "};",
        "",
        "/* clang-format off */",
        f"static const struct Osrs{rev}PacketOutDef g_packet_out_definitions_osrs{rev}[] = {{",
    ]
    for r in rows:
        canon = CANON_OUT.get(r["name"])
        nm = f"PKTOUT_NAME_{canon}" if canon else "PKTOUT_NAME_NONE"
        lines.append(
            "    { %-36s %4d, %-24s \"%s\" },"
            % (nm + ",", r["code"], size_literal(r["size"], "PKTOUT") + ",", r["name"])
        )
    lines += [
        "};",
        "/* clang-format on */",
        "",
        f"#define OSRS{rev}_PACKET_OUT_COUNT                                                                   \\",
        f"    ((int)(sizeof(g_packet_out_definitions_osrs{rev}) /                                              \\",
        f"           sizeof(g_packet_out_definitions_osrs{rev}[0])))",
        "",
        "/** Canonical PKTOUT_NAME_* -> wire opcode; -1 = absent from this revision. */",
        "static inline int",
        f"packetout_code_osrs{rev}(int pkt_out_name)",
        "{",
        "    if( pkt_out_name == PKTOUT_NAME_NONE )",
        "        return -1;",
        f"    for( int i = 0; i < OSRS{rev}_PACKET_OUT_COUNT; i++ )",
        f"        if( g_packet_out_definitions_osrs{rev}[i].name == pkt_out_name )",
        f"            return g_packet_out_definitions_osrs{rev}[i].code;",
        "    return -1;",
        "}",
        "",
        "/** Wire opcode -> payload size for the receiving side. 0 for an unknown",
        " *  opcode, which frames as empty and lets the stream resynchronise rather",
        " *  than desyncing on a guessed length. */",
        "static inline int",
        f"osrs{rev}_packetout_size(int wire_opcode)",
        "{",
        f"    for( int i = 0; i < OSRS{rev}_PACKET_OUT_COUNT; i++ )",
        f"        if( g_packet_out_definitions_osrs{rev}[i].code == wire_opcode )",
        f"            return g_packet_out_definitions_osrs{rev}[i].length;",
        "    return 0;",
        "}",
        "",
        "/** Wire opcode -> canonical PKTOUT_NAME_*, or PKTOUT_NAME_NONE. */",
        "static inline int",
        f"osrs{rev}_packetout_name(int wire_opcode)",
        "{",
        f"    for( int i = 0; i < OSRS{rev}_PACKET_OUT_COUNT; i++ )",
        f"        if( g_packet_out_definitions_osrs{rev}[i].code == wire_opcode )",
        f"            return g_packet_out_definitions_osrs{rev}[i].name;",
        "    return PKTOUT_NAME_NONE;",
        "}",
        "",
        "static inline char const*",
        f"osrs{rev}_packetout_protname(int wire_opcode)",
        "{",
        f"    for( int i = 0; i < OSRS{rev}_PACKET_OUT_COUNT; i++ )",
        f"        if( g_packet_out_definitions_osrs{rev}[i].code == wire_opcode )",
        f"            return g_packet_out_definitions_osrs{rev}[i].prot;",
        "    return 0;",
        "}",
        "",
        "#endif",
        "",
    ]
    return "\n".join(lines)


def read_zone_order(rev):
    """The enclosed sub-packet index is the ORDINAL of RSProt's IndexedZoneProtEncoder,
    not the prot id and not the top-level opcode. Read the enum in declaration order."""
    import re

    path = os.path.expanduser(
        f"~/Documents/git_repos/rsprot/protocol/osrs-{rev}/osrs-{rev}-desktop/src/main/"
        "kotlin/net/rsprot/protocol/game/outgoing/codec/zone/header/"
        "DesktopUpdateZonePartialEnclosedEncoder.kt"
    )
    src = open(path).read()
    body = src[src.index("private enum class IndexedZoneProtEncoder") :]
    body = body[: body.index("\n            ;")]
    return re.findall(r"^\s{12}(\w+)\(OldSchoolZoneProt\.", body, re.M)


def gen_zoneprot(rev, order, src):
    lines = [
        f"#ifndef SRC_NET_REV_OSRS{rev}_ZONEPROT_H",
        f"#define SRC_NET_REV_OSRS{rev}_ZONEPROT_H",
        "",
        BANNER.format(rev=rev, src=src),
        "",
        "/*",
        " * UPDATE_ZONE_PARTIAL_ENCLOSED sub-packet index.",
        " *",
        " * A sub-packet inside the enclosed zone payload is NOT addressed by its",
        " * top-level opcode, and not by RSProt's OldSchoolZoneProt id either. It is",
        " * addressed by the ORDINAL of RSProt's IndexedZoneProtEncoder enum, which is",
        " * declaration order and matches a table inside the client. Using either of the",
        " * other two numbers writes a well-formed packet the client reads as a",
        " * different sub-packet.",
        " */",
        "",
        "enum Osrs%sZoneProt" % rev,
        "{",
    ]
    for i, name in enumerate(order):
        lines.append(f"    OSRS{rev}_ZONE_{name} = {i},")
    lines += [
        f"    OSRS{rev}_ZONE_COUNT = {len(order)}",
        "};",
        "",
        "#endif",
        "",
    ]
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rev")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    dump = json.loads(
        subprocess.check_output(
            [sys.executable, os.path.join(HERE, "rsprot_dump_prot.py"), args.rev]
        )
    )
    src = subprocess.check_output(
        ["git", "-C", os.path.expanduser("~/Documents/git_repos/rsprot"),
         "rev-parse", "--short", "HEAD"]
    ).decode().strip()

    os.makedirs(args.out, exist_ok=True)
    written = []
    for fname, text in (
        ("packetin.h", gen_packetin(args.rev, dump["server"], src)),
        ("packetout.h", gen_packetout(args.rev, dump["client"], src)),
        ("zoneprot.h", gen_zoneprot(args.rev, read_zone_order(args.rev), src)),
    ):
        path = os.path.join(args.out, fname)
        with open(path, "w") as fh:
            fh.write(text)
        written.append(path)

    # Report what did not get a canonical name, so a missing decoder is a number
    # someone read rather than a silence.
    unmapped_in = [r["name"] for r in dump["server"] if r["name"] not in CANON_IN]
    unmapped_out = [r["name"] for r in dump["client"] if r["name"] not in CANON_OUT]
    for p in written:
        print(f"wrote {p}", file=sys.stderr)
    print(
        f"rev {args.rev}: {len(dump['server'])} server prots "
        f"({len(dump['server']) - len(unmapped_in)} decoded), "
        f"{len(dump['client'])} client prots "
        f"({len(dump['client']) - len(unmapped_out)} built)",
        file=sys.stderr,
    )


if __name__ == "__main__":
    main()
