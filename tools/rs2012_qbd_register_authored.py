#!/usr/bin/env python3
"""Register the priority-authored QBD as its own npc, beside the original.

The point is A/B: the lane keeps `rs2012_qbd_default` exactly as it is, and
this adds `rs2012_qbd_prioritized_authored` ("QBD_Prioritized_Authored") built
from the same geometry with the bands `rs2012_face_priorities` solved. Same
model, same animations, same size — only the face render priorities differ, so
anything that looks different between the two is the sort and nothing else.

Run the solve first; this only copies what it produced:

    tools/rs2012_qbd_prio.sh
    python3 tools/rs2012_qbd_register_authored.py
    make -C src torirsserver-cache-rs2012

Idempotent: re-running replaces the block and the lines rather than doubling
them, so re-solving the models is a two-command refresh.

Five files have to agree or the npc is half-present in a way nothing reports:

    models/ported/<lane>/*.ob3          the geometry
    ported/<lane>/pack/7_models.pack    id -> path, how the packer finds it
    ported/<lane>/configs/rs2012.npc    the record itself
    ported/<lane>/pack/npc.alloc        symbol -> id
    ported/<lane>/pack/npc.client       symbols the client is allowed to see
    port/<lane>.map                     the provenance ledger
"""

import pathlib
import re
import shutil
import sys

REPO = pathlib.Path(__file__).resolve().parents[1]
TREE = REPO / "OSRS-Content" / "osrs239-content"
LANE = TREE / "ported" / "rs2012_qbd_td"
MODELS = TREE / "models" / "ported" / "rs2012_qbd_td"
SOLVED = REPO / "docs" / "rs2012_qbd_priorities" / "run"

NPC_SYMBOL = "rs2012_qbd_prioritized_authored"
NPC_NAME = "QBD_Prioritized_Authored"
NPC_ID = 25010
CLONE_OF = "rs2012_qbd_default"
# (source model id, destination id) — the default form's model1 and model2.
# Ids continue the lane's model band, which ends at 110659.
PAIRS = [(70260, 110660), (69766, 110661)]


def main() -> int:
    for src_id, _ in PAIRS:
        src = SOLVED / f"rs2012_model_{src_id}.ob3"
        if not src.exists():
            print(f"missing {src} — run tools/rs2012_qbd_prio.sh first", file=sys.stderr)
            return 1
        shutil.copyfile(src, MODELS / f"rs2012_model_{src_id}_authored.ob3")

    pack = LANE / "pack" / "7_models.pack"
    prefixes = tuple(f"{dest}=" for _, dest in PAIRS)
    lines = [l for l in pack.read_text().splitlines() if not l.startswith(prefixes)]
    for src_id, dest in PAIRS:
        lines.append(f"{dest}=ported/rs2012_qbd_td/rs2012_model_{src_id}_authored")
    pack.write_text("\n".join(lines) + "\n")

    # Cloned field by field from the form it is being compared against, so the
    # only difference between the two npcs is the models they name.
    cfg = LANE / "configs" / "rs2012.npc"
    text = re.sub(rf"\n\[{NPC_SYMBOL}\].*?(?=\n\[|\Z)", "", cfg.read_text(), flags=re.S)
    donor = re.search(rf"\[{CLONE_OF}\]\n(.*?)(?=\n\[)", text, re.S)
    if not donor:
        print(f"cannot find [{CLONE_OF}] to clone", file=sys.stderr)
        return 1
    block = [f"[{NPC_SYMBOL}]", f"name={NPC_NAME}"]
    for line in donor.group(1).strip().splitlines():
        if line.startswith("name="):
            continue
        if line.startswith("model1="):
            line = f"model1={PAIRS[0][1]}"
        elif line.startswith("model2="):
            line = f"model2={PAIRS[1][1]}"
        block.append(line)
    cfg.write_text(text.rstrip("\n") + "\n\n" + "\n".join(block) + "\n")

    alloc = LANE / "pack" / "npc.alloc"
    rows = [l for l in alloc.read_text().splitlines() if not l.startswith(f"{NPC_ID}=")]
    rows.append(f"{NPC_ID}={NPC_SYMBOL}")
    alloc.write_text("\n".join(rows) + "\n")

    client = LANE / "pack" / "npc.client"
    names = sorted({l for l in client.read_text().split() if l} | {NPC_SYMBOL})
    client.write_text("\n".join(names) + "\n")

    ledger = TREE / "port" / "rs2012_qbd_td.map"
    rows = [
        l
        for l in ledger.read_text().splitlines()
        if "_authored" not in l and f"\t{NPC_ID}\t" not in l
    ]
    for src_id, dest in PAIRS:
        rows.append(
            f"model\t{src_id}\tmodel_{src_id}_authored\t{dest}\t"
            f"rs2012_model_{src_id}_authored\tminted\tunreviewed"
        )
    rows.append(
        f"npc\t15454\tqbd_default_authored\t{NPC_ID}\t{NPC_SYMBOL}\tminted\tunreviewed"
    )
    ledger.write_text("\n".join(rows) + "\n")

    print(f"registered {NPC_SYMBOL} as npc {NPC_ID}, models {[d for _, d in PAIRS]}")
    print("re-pack with: make -C src torirsserver-cache-rs2012")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
