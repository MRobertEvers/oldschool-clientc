#!/usr/bin/env python3
"""Acceptance for the familiar "Interact" operation (npc op1).

The defect: op1 ran `~summoning_call_familiar`, so "Interact" teleported the
familiar to the owner's feet and printed "You call your familiar." — the Call
Follower operation, which belongs to the worn-items button, the summoning orb
and the familiar page. Only 2 of the 78 familiars were bound at all.

2009scape's `FamiliarNPCOptionPlugin` routes "interact" to the familiar's own
dialogue after an ownership check, and three familiars (spirit kyatt, spirit
graahk, lava titan) register one. See summoning_interact.rs2's header and
docs/summoning_port/FAMILIAR_INTERACT.md for the full citation set.

What is proven live, through the real OPNPC1 packet and the server's ordinary
trigger dispatch:

  * a familiar with no source conversation answers and is NOT recalled;
  * the three-familiar branch opens the source's two-row option box;
  * Call Follower still works, so the message split in `~summoning_call_familiar`
    did not silence the operation that legitimately calls.

What is static: the per-row bindings, the teleport destinations, the level gate,
and the conversation text. Selecting a row inside the option box needs a pixel
this harness cannot address stably (`~p_choice_open`'s rows are `cc_create`d
children), so the branches behind it are checked against the source instead.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
CONTENT = REPO / "OSRS-Content/osrs239-content"
LANE = CONTENT / "ported/scape2009_summoning"
SCRIPT_LANE = CONTENT / "server/scripts/ported_scape2009_summoning"
CLIENT = REPO / "src/torirs"
CACHE = REPO / "cache.osrs239.summoning"
SCRIPTS = CONTENT / "server/scripts/build_summoning"
MANIFEST = REPO / "manifest_osrs239.ini"

# Familiar type ids, from ~summoning_familiar_npc.
TYPE_GRAAHK = 40

# 2009scape teleport destinations, absolute (x, z).
SOURCE_TELEPORTS = {
    "^summoning_interact_kyatt_coord": (2326, 3636),
    "^summoning_interact_graahk_coord": (2786, 3002),
    "^summoning_interact_lava_titan_coord": (3048, 3820),
}


def familiar_npc_names() -> list[str]:
    """The 78 npc records `~summoning_familiar_npc` can return."""
    source = (SCRIPT_LANE / "scripts/summoning_registry.rs2").read_text(encoding="utf-8")
    body = source.split("[proc,summoning_familiar_npc](int $type)(npc)")[1].split("[proc,")[0]
    return re.findall(r"return\((summoning_[a-z0-9_]+)\);", body)


def npc_ops(names: set[str]) -> dict[str, dict[str, str]]:
    """op1..op5 for each named npc record in the client lane."""
    found: dict[str, dict[str, str]] = {}
    for path in sorted((LANE / "configs").glob("*.npc")):
        current = None
        for raw in path.read_text(encoding="utf-8").splitlines():
            line = raw.strip()
            if line.startswith("[") and line.endswith("]"):
                current = line[1:-1]
                if current in names:
                    found.setdefault(current, {})
            elif current in found and re.match(r"^op[1-5]=", line):
                key, value = line.split("=", 1)
                found[current][key] = value
    return found


def npc_alloc() -> dict[str, int]:
    rows: dict[str, int] = {}
    for raw in (LANE / "pack/npc.alloc").read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if "=" in line and not line.startswith("//"):
            npc_id, name = line.split("=", 1)
            rows[name] = int(npc_id)
    return rows


def coord_to_absolute(literal: str) -> tuple[int, int]:
    level, mx, mz, lx, lz = (int(part) for part in literal.split("_"))
    assert level == 0
    return mx * 64 + lx, mz * 64 + lz


def run_client(saves: Path, env_extra: dict[str, str], frames: int) -> str:
    env = os.environ.copy()
    for key in ("TORIRS_NET_CHEAT", "TORIRS_SIM_CLICK_AT", "TORIRS_SIM_OPNPC", "TORIRS_MAX_FRAMES"):
        env.pop(key, None)
    env.update(
        {
            "MOCK230_SAVES": str(saves),
            "MOCK230_SCRIPTS": str(SCRIPTS),
            "MOCK230_CACHE": str(CACHE),
            "SDL_VIDEODRIVER": "dummy",
            "TORIRS_MAX_FRAMES": str(frames),
            "TORIRS_NET_DEBUG": "1",
        }
    )
    env.update(env_extra)
    result = subprocess.run(
        [str(CLIENT), str(CACHE), "--manifest", str(MANIFEST), "--soft3d"],
        cwd=REPO,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=300,
        check=False,
    )
    if result.returncode:
        raise RuntimeError(f"client exited {result.returncode}\n{result.stdout[-4000:]}")
    return result.stdout


def after_op(log: str) -> str:
    """The tail of a log from the frame the OPNPC packet actually went out.

    Everything before it is login and provisioning, which say "You summon ..."
    and would otherwise satisfy assertions about what Interact did.
    """
    match = re.search(r"^sim_opnpc: .*slot=\d+$", log, re.MULTILINE)
    return log[match.end() :] if match else ""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=REPO / "build/summoning-interact")
    parser.add_argument("--static-only", action="store_true")
    args = parser.parse_args()

    errors: list[str] = []
    checks = 0

    def expect(ok: bool, message: str) -> None:
        nonlocal checks
        checks += 1
        if not ok:
            errors.append(message)

    bindings = (SCRIPT_LANE / "scripts/summoning_bindings.rs2").read_text(encoding="utf-8")
    interact = (SCRIPT_LANE / "scripts/summoning_interact.rs2").read_text(encoding="utf-8")
    core = (SCRIPT_LANE / "scripts/summoning_core.rs2").read_text(encoding="utf-8")
    dialogue = (SCRIPT_LANE / "scripts/summoning_dialogue.rs2").read_text(encoding="utf-8")
    tab = (SCRIPT_LANE / "scripts/summoning_tab.rs2").read_text(encoding="utf-8")
    constants = (SCRIPT_LANE / "configs/summoning.constant").read_text(encoding="utf-8")

    names = familiar_npc_names()
    expect(len(names) == 78, f"the roster should resolve 78 familiar npcs, got {len(names)}")

    # 1. Every familiar carries op1=Interact, so every binding below is reachable
    #    and no familiar is left with a menu row that reaches nothing.
    ops = npc_ops(set(names))
    missing_op1 = [name for name in names if ops.get(name, {}).get("op1") != "Interact"]
    expect(not missing_op1, f"familiar records without op1=Interact: {missing_op1[:5]}")

    # 2. op1 is bound for all of them, and to interact rather than to call.
    unbound = [name for name in names if f"[opnpc1,{name}]\n" not in bindings]
    expect(not unbound, f"familiars with no opnpc1 binding: {unbound[:5]}")
    for name in names:
        block = bindings.split(f"[opnpc1,{name}]\n", 1)
        body = block[1].split("\n\n", 1)[0] if len(block) > 1 else ""
        expect(
            "~summoning_familiar_interact;" in body,
            f"[opnpc1,{name}] does not run ~summoning_familiar_interact",
        )
        expect(
            "~summoning_call_familiar" not in body,
            f"[opnpc1,{name}] still runs the Call Follower operation",
        )

    # 3. Call Follower keeps exactly the three interface homes it had, plus the
    #    debug hook and the silent recall the teleport issues.
    expect(
        "[if_button1,wornitems:call_follower]" in tab
        and "[if_button,orbs:summoning_orb_button]" in tab
        and "[if_button,summoning_familiar:call]" in tab,
        "Call Follower lost one of its three interface bindings",
    )
    expect(
        "[proc,summoning_call_familiar_ex](boolean $message)" in core
        and 'if ($message = true) mes("You call your familiar.");' in core,
        "the callable-without-a-message split is absent from ~summoning_call_familiar",
    )
    expect(
        "~summoning_call_familiar_ex(false);" in interact,
        "the Interact teleport does not bring the familiar along",
    )

    # 4. The proc itself: ownership guard, source dispatch, follow restore.
    expect("[proc,summoning_familiar_interact]" in interact, "~summoning_familiar_interact is absent")
    expect(
        interact.count('mes("This is not your familiar.");') == 2,
        "FamiliarNPCOptionPlugin's ownership refusal is not on both reject paths",
    )
    expect(
        "npc_findowned = false" in interact and "npc_uid != $clicked" in interact,
        "Interact does not require the clicked npc to be the player's own familiar",
    )
    expect(
        "[proc,summoning_familiar_interact_end](npc_uid $familiar)" in interact
        and "npc_setmode(playerfollow);" in interact,
        "the dialogue does not put the familiar back on its follow mode",
    )
    for type_id, proc in ((38, "kyatt"), (40, "graahk"), (64, "lava_titan")):
        expect(
            f"if ($type = {type_id})" in interact or f"$type = {type_id})" in interact,
            f"familiar type {type_id} has no Interact branch",
        )
        expect(
            f"[proc,summoning_familiar_interact_{proc}](npc_uid $familiar)" in interact,
            f"~summoning_familiar_interact_{proc} is absent",
        )

    # 5. Source values that a reader cannot check by eye.
    for constant, absolute in SOURCE_TELEPORTS.items():
        match = re.search(rf"^{re.escape(constant)} = (\S+)$", constants, re.MULTILINE)
        expect(match is not None, f"{constant} is not defined")
        if match:
            expect(
                coord_to_absolute(match.group(1)) == absolute,
                f"{constant} is {match.group(1)}, which is not the source's {absolute}",
            )
    expect(
        "if (stat_base(summoning) < add(~summoning_familiar_level($type), 10)) {" in interact,
        "the chat gate is not the STATIC Summoning level at the familiar's level + 10",
    )

    # 6. The conversations. They are wiki transcripts compiled from a checked-in
    #    corpus, so the checks are that the corpus covers the roster, that the
    #    generated script is not stale against it, and that no conversation was
    #    hand-edited into the generated file.
    corpus = json.loads((REPO / "docs/summoning_port/familiar_dialogue.json").read_text())
    familiars = corpus["familiars"]
    expect(len(familiars) == 78, f"corpus covers {len(familiars)} familiars, not 78")
    expect(
        all(str(t) in familiars for t in range(1, 79)),
        "the corpus is not keyed by the registry's 1..78 type ids",
    )
    empty = [f["name"] for f in familiars.values() if not f["conversations"]]
    expect(not empty, f"familiars with no conversation at all: {empty[:5]}")
    for type_id, entry in familiars.items():
        expect(
            f"if ($type = {type_id}) return(~summoning_familiar_chat_{type_id}($familiar));"
            in dialogue,
            f"type {type_id} ({entry['name']}) is not in the chat dispatcher",
        )
    expect(
        "GENERATED FILE" in dialogue.split("\n", 1)[0],
        "summoning_dialogue.rs2 lost its generated-file banner",
    )
    stale = subprocess.run(
        [sys.executable, str(REPO / "tools/generate_familiar_dialogue_script.py"), "--check"],
        cwd=REPO, text=True, capture_output=True, check=False,
    )
    expect(
        stale.returncode == 0,
        "summoning_dialogue.rs2 is stale against the corpus: " + stale.stderr.strip(),
    )
    # Every spoken string must survive this era's chat interface: `<` opens a
    # colour tag, and mock230_send_if_settext builds its packet in 512 bytes.
    spoken = re.findall(r'"([^"]*)"', dialogue)
    expect(spoken, "the generated dialogue has no strings")
    bad = [s for s in spoken if "<" in s or ">" in s]
    expect(not bad, f"dialogue strings carry colour-tag brackets: {bad[:3]}")
    overlong = [s for s in spoken if len(s) > 220]
    expect(not overlong, f"dialogue strings exceed one chat page: {[len(s) for s in overlong][:3]}")

    # 7. The three option boxes still route Chat into the shared path rather than
    #    keeping 2009scape's placeholder line.
    for type_id, proc in ((38, "kyatt"), (40, "graahk"), (64, "lava_titan")):
        expect(
            f"~summoning_familiar_interact_chat($familiar, {type_id});" in interact,
            f"the {proc} option box does not route Chat into the shared conversation path",
        )
    expect(
        interact.count('" does not feel like talking now."') == 1,
        "the no-conversation fallback is stated more than once",
    )

    if args.static_only or errors:
        return finish(checks, errors, args.out)

    alloc = npc_alloc()
    wolf = alloc["summoning_spirit_wolf"]
    graahk = alloc["summoning_cohort_spirit_graahk_spirit_graahk"]
    args.out.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="summoning_interact_") as root:
        # A. A plain familiar, BELOW the chat gate. It must answer with its own
        #    untranslated sound and must NOT be recalled: a recall re-sends the
        #    arrival spotanim. Spirit wolf is level 1, so a fresh account at
        #    Summoning 1 is below its level-11 gate.
        saves = Path(root) / "plain"
        saves.mkdir()
        plain = run_client(
            saves,
            {
                "TORIRS_NET_CHEAT": "summoning_unlock;summoning_demo",
                "TORIRS_SIM_OPNPC": f"200,1,{wolf}",
            },
            420,
        )
        (args.out / "plain.log").write_text(plain)
        tail = after_op(plain)
        expect(tail != "", "the Interact packet never went out for the Spirit wolf")
        expect(
            "text='Whurf?'" in tail,
            "below the gate the familiar did not make its own untranslated sound",
        )
        expect(
            "(What are you doing?)" not in tail,
            "below the gate the player understood the familiar anyway",
        )
        expect(
            "You call your familiar." not in tail,
            "Interact still runs the Call Follower operation",
        )
        expect(
            "spotanim" not in tail.lower(),
            "Interact moved the familiar — an arrival graphic followed the op",
        )

        # B. The same familiar ABOVE the gate: a real transcript conversation,
        #    in the chathead dialogue box rather than a chatbox line.
        saves = Path(root) / "gated"
        saves.mkdir()
        gated = run_client(
            saves,
            {
                "TORIRS_NET_CHEAT": "summoning_unlock;setlevel 24 20;summoning_demo",
                "TORIRS_SIM_OPNPC": f"200,1,{wolf}",
            },
            420,
        )
        (args.out / "gated.log").write_text(gated)
        tail = after_op(gated)
        expect(tail != "", "the Interact packet never went out above the gate")
        expect(
            "text='Spirit wolf'" in tail,
            "the dialogue box was not labelled with the familiar's name",
        )
        expect(
            "does not feel like talking now" not in tail,
            "above the gate the familiar still refused to talk",
        )
        wolf_lines = [
            line["text"]
            for conversation in familiars["1"]["conversations"]
            for line in conversation["lines"]
            if line["who"] == "npc"
        ]
        expect(
            any(f"text='{text}'" in tail for text in wolf_lines),
            "no Spirit wolf transcript line reached the dialogue box",
        )

        # C. The branch that has a teleport. Selecting a row is out of reach, so
        #    this proves the option box opens: interface 219 armed for resume and
        #    clientscript 58 (chatbox_multi_init) handed the two rows.
        saves = Path(root) / "graahk"
        saves.mkdir()
        graahk_log = run_client(
            saves,
            {
                "TORIRS_NET_CHEAT": f"summoning_unlock;setlevel 24 70;summoning_summon {TYPE_GRAAHK}",
                "TORIRS_SIM_OPNPC": f"250,1,{graahk}",
            },
            460,
        )
        (args.out / "graahk.log").write_text(graahk_log)
        tail = after_op(graahk_log)
        expect(tail != "", "the Interact packet never went out for the Spirit graahk")
        expect(
            "runclientscript: script=58" in tail,
            "the graahk Interact did not open the source's option box",
        )
        expect("(219:1)" in tail, "chatmenu:options was not armed for the graahk option box")
        expect(
            "You call your familiar." not in tail,
            "the graahk Interact fell through to Call Follower",
        )

        # C. Call Follower still calls. `~summoning_call_familiar` grew a message
        #    parameter for the teleport recall; this is the operation that must
        #    keep printing.
        saves = Path(root) / "call"
        saves.mkdir()
        call = run_client(
            saves,
            {"TORIRS_NET_CHEAT": "summoning_unlock;summoning_demo;summoning_call"},
            380,
        )
        (args.out / "call.log").write_text(call)
        expect("You call your familiar." in call, "Call Follower stopped calling")

    return finish(checks, errors, args.out)


def finish(checks: int, errors: list[str], out: Path) -> int:
    for error in errors:
        print(f"test_summoning_interact: {error}", file=sys.stderr)
    print(f"test_summoning_interact: {checks} checks, {len(errors)} errors (logs in {out})")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
