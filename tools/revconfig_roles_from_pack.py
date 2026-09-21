#!/usr/bin/env python3
"""Generate and check revconfig role data from the content packs.

Owner: core-revconfig (docs/ARCHITECT.md). Two jobs, one file, because they
must never disagree:

  --write   regenerate the [iface:] and [role:] sections for the osrs239 dat2
            lane from OSRS-Content/osrs239-content/pack/3_interfaces.pack and
            each interface's own .compack (falling back to counting an .if
            file's `[name]` section headers, loudly, when a .compack is
            missing), into revconfig/osrs239/osrs239_dat2_roles.gen.ini -- a
            SEPARATE file chained by the existing loader
            (src/engine/uitree_role_load.c:206-221, and
            src/revconfig/revconfig_refs.c beside it), never into the
            hand-edited osrs239_dat2_cache.ini. A generator that rewrites a
            file a person also edits loses that person's work the first time
            it runs.
  --check   regenerate into memory, diff against what is committed (a missing
            committed file is a FAIL, not a free pass), and statically check
            that every `derive=` fact named in the generated file is one
            App_RoleDeriveFallback (src/app/app_role_derive.c) knows, and that
            every `iface(<name>` a generated match= line references resolves
            to an `[iface:<name>]` section somewhere in this file or in the
            hand-edited osrs239_dat2_cache.ini it sits beside.

`make -C src check-revconfig-roles` runs `--check` with no arguments.

Bootstrap it the way CLAUDE.md requires: introduce the drift in a THROWAWAY
worktree and confirm PASS flips to FAIL there. Never in this tree.

Byte-stable: every list below is written in a fixed, hand-authored order, so
running --write twice with unchanged content produces an unchanged file.
"""

import argparse
import os
import re
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
CONTENT_ROOT = os.path.join(REPO_ROOT, "OSRS-Content", "osrs239-content")
PACK_INTERFACES = os.path.join(CONTENT_ROOT, "pack", "3_interfaces.pack")
INTERFACES_DIR = os.path.join(CONTENT_ROOT, "interfaces")

OUT_PATH = os.path.join(REPO_ROOT, "revconfig", "osrs239", "osrs239_dat2_roles.gen.ini")
HAND_EDITED_INI = os.path.join(REPO_ROOT, "revconfig", "osrs239", "osrs239_dat2_cache.ini")

# Every fact App_RoleDeriveFallback answers for on this lane today. Mirror
# this list by hand when that file's vocabulary grows -- keeping the two in
# step by eye is exactly what --check exists to stop trusting.
KNOWN_DERIVE_FACTS = {"dialog_continue", "pause_pending"}

# --- section 1b: new [iface:] sections --------------------------------------
# Role-level iface name == pack interface name for every one of these.
NEW_IFACE_NAMES = [
    "chat_left",
    "chat_right",
    "messagebox",
    "messagebox_titled",
    "messagebox_url",
    "objectbox",
    "objectbox_double",
    "chatmenu",
    "levelup_display",
    "questscroll",
]

# A [role:…]'s iface() anchor is usually spelled the same as the pack
# interface name it resolves against; "chat" is the one exception -- the
# hand-edited ini already declares [iface:chat] id=162, bound to the pack's
# "chatbox", and this file must reference that role name, never restate the
# id (QUEST_DRIVER_PLAN.md section 1a).
ROLE_IFACE_PACK_NAME = {"chat": "chatbox"}

# --- sections 1c + 1e: static roles, resolved to a child index by NAME in
# the target interface's own .compack -- never a number written here.
STATIC_ROLES = [
    ("chat_modal_host", "chat", "chatmodal"),
    ("dialog_npc_universe", "chat_left", "universe"),
    ("dialog_npc_head", "chat_left", "head"),
    ("dialog_npc_name", "chat_left", "name"),
    ("dialog_npc_continue", "chat_left", "continue"),
    ("dialog_npc_text", "chat_left", "text"),
    ("dialog_player_universe", "chat_right", "universe"),
    ("dialog_player_head", "chat_right", "head"),
    ("dialog_player_name", "chat_right", "name"),
    ("dialog_player_continue", "chat_right", "continue"),
    ("dialog_player_text", "chat_right", "text"),
    ("dialog_mesbox_universe", "messagebox", "universe"),
    ("dialog_mesbox_text", "messagebox", "text"),
    ("dialog_mesbox_continue", "messagebox", "continue"),
    ("dialog_mesbox_titled_universe", "messagebox_titled", "universe"),
    ("dialog_mesbox_titled_title", "messagebox_titled", "title"),
    ("dialog_mesbox_titled_continue", "messagebox_titled", "continue"),
    ("dialog_mesbox_titled_text", "messagebox_titled", "text"),
    ("dialog_mesbox_url_universe", "messagebox_url", "universe"),
    ("dialog_mesbox_url_text", "messagebox_url", "text"),
    ("dialog_mesbox_url_continue", "messagebox_url", "continue"),
    ("dialog_objbox_universe", "objectbox", "universe"),
    ("dialog_objbox_item", "objectbox", "item"),
    ("dialog_objbox_text", "objectbox", "text"),
    ("dialog_objbox_double_universe", "objectbox_double", "universe"),
    ("dialog_objbox_double_item1", "objectbox_double", "model1"),
    ("dialog_objbox_double_text", "objectbox_double", "text"),
    ("dialog_objbox_double_item2", "objectbox_double", "model2"),
    ("dialog_objbox_double_continue", "objectbox_double", "pausebutton"),
    ("dialog_options_universe", "chatmenu", "universe"),
    ("dialog_options", "chatmenu", "options"),
    ("dialog_quest_scroll_universe", "questscroll", "universe"),
    ("dialog_quest_scroll_title", "questscroll", "quest_title"),
    ("dialog_quest_scroll_icon", "questscroll", "quest_model"),
    ("dialog_quest_scroll_points", "questscroll", "quest_points"),
    ("dialog_quest_scroll_award_text", "questscroll", "award_text"),
    ("dialog_quest_scroll_close", "questscroll", "close_button"),
    ("dialog_levelup_universe", "levelup_display", "universe"),
    ("dialog_levelup_text1", "levelup_display", "text1"),
    ("dialog_levelup_text2", "levelup_display", "text2"),
    ("dialog_levelup_continue", "levelup_display", "continue"),
]

# section 1e: the numbered reward family -- resolved by NAME pattern
# ("quest_reward<n>"), not a hardcoded index list.
QUEST_REWARD_COUNT = 7

# section 1c: the numbered skill family -- every non-"universe/text1/text2/
# continue", non-"com_N" entry in levelup_display.compack IS the family; no
# id is written here at all.
LEVELUP_FIXED_MEMBERS = {"universe", "text1", "text2", "continue"}

# section 1d: chatmenu's title and 2-5 rows are cc_create'd at runtime
# (chatbox_multi_init / chatbox_multi_addoption under chatmenu:options) and
# have NO .if section -- the only roles in this file whose child position
# cannot be read from a pack. Anchor and sub id both come straight from
# QUEST_DRIVER_PLAN.md section 1d, never from a regenerated pack fact. The
# type filter ("text") is the cc(parent, subid, <type>) grammar guarding
# against a reused dynamic sub id landing on a non-text node after a
# CC_DELETEALL rebuild (both the title and every row are UIELEM_RS_TEXT,
# QUEST_DRIVER_PLAN.md section 5.5).
CC_OPTIONS_ANCHOR_ROLE = ("chatmenu", "options")  # -> iface(chatmenu, <its index>)
CC_OPTIONS_ROW_COUNT = 5
CC_OPTIONS_TYPE = "text"

# section 1f: derived roles this pass owns (dialog_continue, pause_pending).
# chat_modal_host (dat1) and button_type(N) are the rs289lc lane's, out of
# scope here -- @see src/app/app_role_derive.c.
DERIVE_ROLES = ["dialog_continue", "pause_pending"]


class GeneratorError(Exception):
    pass


def load_pack_ids(path):
    """{name: id} from a `pack/N_x.pack` file's `id=name` lines."""
    ids = {}
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or "=" not in line:
                continue
            id_str, name = line.split("=", 1)
            if not id_str.isdigit():
                continue
            ids[name] = int(id_str)
    return ids


def load_compack(pack_name, compack_cache):
    """{member_name: child_index} for one interface, memoised.

    Reads interfaces/<pack_name>.compack when it exists (the normal case: it
    is written by the same content build that produces the .if). Falls back
    to counting `[name]` section headers, in file order, in
    interfaces/<pack_name>.if -- loudly, since a fallback that resolves
    silently is a fallback nobody notices stops matching the day the .compack
    reappears with a different order.
    """
    if pack_name in compack_cache:
        return compack_cache[pack_name]

    compack_path = os.path.join(INTERFACES_DIR, pack_name + ".compack")
    members = {}
    if os.path.isfile(compack_path):
        with open(compack_path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line or "=" not in line:
                    continue
                idx_str, name = line.split("=", 1)
                if not idx_str.isdigit():
                    continue
                members[name] = int(idx_str)
    else:
        if_path = os.path.join(INTERFACES_DIR, pack_name + ".if")
        print(
            "tools/revconfig_roles_from_pack.py: no {}.compack -- falling back to "
            "counting [name] headers in {}.if\n".format(pack_name, pack_name),
            file=sys.stderr,
        )
        if not os.path.isfile(if_path):
            raise GeneratorError(
                "neither {0}.compack nor {0}.if exists under {1}".format(
                    pack_name, INTERFACES_DIR
                )
            )
        idx = 0
        header_re = re.compile(r"^\[([A-Za-z0-9_]+)\]\s*$")
        with open(if_path, "r", encoding="utf-8") as f:
            for line in f:
                m = header_re.match(line)
                if not m:
                    continue
                members[m.group(1)] = idx
                idx += 1

    compack_cache[pack_name] = members
    return members


def child_index(role_iface_name, member_name, compack_cache):
    """The numeric child index `member_name` names in `role_iface_name`."""
    pack_name = ROLE_IFACE_PACK_NAME.get(role_iface_name, role_iface_name)
    members = load_compack(pack_name, compack_cache)
    if member_name not in members:
        raise GeneratorError(
            "{}.compack (role iface '{}') has no member '{}'".format(
                pack_name, role_iface_name, member_name
            )
        )
    return members[member_name]


def emit_role(lines, role_name, source_comment, match_expr):
    lines.append("; {}".format(source_comment))
    lines.append("[role:{}]".format(role_name))
    lines.append("match={}".format(match_expr))
    lines.append("")


def generate():
    pack_ids = load_pack_ids(PACK_INTERFACES)
    compack_cache = {}
    lines = []

    lines.append(
        "; GENERATED by tools/revconfig_roles_from_pack.py --write. Do not hand-edit --"
    )
    lines.append("; `make -C src check-revconfig-roles` fails if this drifts from the pack.")
    lines.append(";")
    lines.append(
        "; Chained beside osrs239_dat2_cache.ini (src/engine/uitree_role_load.c:206-221,"
    )
    lines.append(
        "; src/revconfig/revconfig_refs.c), never into it. Source of truth: OSRS-Content/"
    )
    lines.append(
        "; osrs239-content/pack/3_interfaces.pack (ids) and each interface's own .compack"
    )
    lines.append("; (child indices) -- @see docs/QUEST_DRIVER_PLAN.md sections 1b-1f.")
    lines.append("")

    # --- 1b: new [iface:] sections ---
    for name in NEW_IFACE_NAMES:
        if name not in pack_ids:
            raise GeneratorError(
                "pack/3_interfaces.pack has no interface named '{}'".format(name)
            )
        lines.append("; source: pack/3_interfaces.pack")
        lines.append("[iface:{}]".format(name))
        lines.append("id={}".format(pack_ids[name]))
        lines.append("")

    # --- 1c + 1e: static roles, resolved by member name ---
    for role_name, iface_role_name, member_name in STATIC_ROLES:
        idx = child_index(iface_role_name, member_name, compack_cache)
        pack_name = ROLE_IFACE_PACK_NAME.get(iface_role_name, iface_role_name)
        emit_role(
            lines,
            role_name,
            "{}:{} ({}.compack)".format(iface_role_name, member_name, pack_name),
            "iface({}, {})".format(iface_role_name, idx),
        )

    # --- 1e: the questscroll reward family ---
    for n in range(1, QUEST_REWARD_COUNT + 1):
        member = "quest_reward{}".format(n)
        idx = child_index("questscroll", member, compack_cache)
        emit_role(
            lines,
            "dialog_quest_scroll_reward_{}".format(n),
            "questscroll:{} (questscroll.compack)".format(member),
            "iface(questscroll, {})".format(idx),
        )

    # --- 1c: the levelup skill family, discovered rather than hardcoded ---
    levelup_members = load_compack("levelup_display", compack_cache)
    for member_name, idx in sorted(levelup_members.items(), key=lambda kv: kv[1]):
        if member_name in LEVELUP_FIXED_MEMBERS or member_name.startswith("com_"):
            continue
        emit_role(
            lines,
            "dialog_levelup_skill_{}".format(member_name),
            "levelup_display:{} (levelup_display.compack)".format(member_name),
            "iface(levelup_display, {})".format(idx),
        )

    # --- 1d: chatmenu's cc_create'd title and rows -- the one family this
    # generator cannot read from a pack; anchor and sub ids are design-doc
    # facts, held here as data rather than in C. ---
    anchor_iface, anchor_member = CC_OPTIONS_ANCHOR_ROLE
    anchor_idx = child_index(anchor_iface, anchor_member, compack_cache)
    anchor_expr = "iface({}, {})".format(anchor_iface, anchor_idx)

    emit_role(
        lines,
        "dialog_options_title",
        "chatmenu:options sub 0 (cc_create'd by chatbox_multi_init -- no .if section)",
        "cc({}, 0, {})".format(anchor_expr, CC_OPTIONS_TYPE),
    )
    for n in range(1, CC_OPTIONS_ROW_COUNT + 1):
        emit_role(
            lines,
            "dialog_options_row_{}".format(n),
            "chatmenu:options sub {} (cc_create'd by chatbox_multi_addoption -- no .if section)".format(
                n
            ),
            "cc({}, {}, {})".format(anchor_expr, n, CC_OPTIONS_TYPE),
        )

    # --- 1f: derived roles ---
    for fact in DERIVE_ROLES:
        lines.append("; derive= -- answered by App_RoleDeriveFallback, no match expression")
        lines.append("[role:{}]".format(fact))
        lines.append("derive={}".format(fact))
        lines.append("")

    return "\n".join(lines).rstrip("\n") + "\n"


def write(content):
    out_dir = os.path.dirname(OUT_PATH)
    os.makedirs(out_dir, exist_ok=True)
    with open(OUT_PATH, "w", encoding="utf-8") as f:
        f.write(content)
    print("tools/revconfig_roles_from_pack.py: wrote {}".format(OUT_PATH))


IFACE_SECTION_RE = re.compile(r"^\[iface:([A-Za-z0-9_]+)\]\s*$", re.MULTILINE)
DERIVE_LINE_RE = re.compile(r"^derive=([A-Za-z0-9_]+)", re.MULTILINE)
IFACE_REF_RE = re.compile(r"iface\(\s*([A-Za-z0-9_]+)")


def known_iface_names():
    names = set()
    for path in (OUT_PATH, HAND_EDITED_INI):
        if not os.path.isfile(path):
            continue
        with open(path, "r", encoding="utf-8") as f:
            text = f.read()
        names.update(IFACE_SECTION_RE.findall(text))
    return names


def check():
    try:
        generated = generate()
    except GeneratorError as e:
        print("tools/revconfig_roles_from_pack.py: FAIL -- {}".format(e), file=sys.stderr)
        return 1

    ok = True

    if not os.path.isfile(OUT_PATH):
        print(
            "tools/revconfig_roles_from_pack.py: FAIL -- {} does not exist; run "
            "--write first".format(OUT_PATH),
            file=sys.stderr,
        )
        return 1

    with open(OUT_PATH, "r", encoding="utf-8") as f:
        committed = f.read()

    if committed != generated:
        print(
            "tools/revconfig_roles_from_pack.py: FAIL -- {} does not match the pack; "
            "run --write and commit the result".format(OUT_PATH),
            file=sys.stderr,
        )
        ok = False

    for fact in DERIVE_LINE_RE.findall(generated):
        if fact not in KNOWN_DERIVE_FACTS:
            print(
                "tools/revconfig_roles_from_pack.py: FAIL -- derive={} is not a fact "
                "App_RoleDeriveFallback knows (src/app/app_role_derive.c)".format(fact),
                file=sys.stderr,
            )
            ok = False

    declared = known_iface_names()
    for name in sorted(set(IFACE_REF_RE.findall(generated))):
        if name not in declared:
            print(
                "tools/revconfig_roles_from_pack.py: FAIL -- iface({}) is not declared by "
                "any [iface:{}] section in {} or {}".format(
                    name, name, os.path.basename(OUT_PATH), os.path.basename(HAND_EDITED_INI)
                ),
                file=sys.stderr,
            )
            ok = False

    if ok:
        print("check-revconfig-roles: PASS")
        return 0
    return 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--write", action="store_true", help="regenerate the .gen.ini")
    group.add_argument(
        "--check", action="store_true", help="regenerate in memory and diff (the default)"
    )
    args = parser.parse_args()

    if args.write:
        try:
            write(generate())
        except GeneratorError as e:
            print("tools/revconfig_roles_from_pack.py: FAIL -- {}".format(e), file=sys.stderr)
            return 1
        return 0

    return check()


if __name__ == "__main__":
    sys.exit(main())
