#!/usr/bin/env python3
"""Pin RS2012 NPC ids and the revision-239 high-id transformation contract."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ALLOC = ROOT / "OSRS-Content/osrs239-content/ported/rs2012_qbd_td/pack/npc.alloc"
LEDGER = ROOT / "OSRS-Content/osrs239-content/port/rs2012_qbd_td.map"
MANIFEST = ROOT / "ports/rs2012_qbd_td.ini"
ENCODER = ROOT / "src/net/mock/mock230_encode.c"
EXPECTED = tuple(range(25000, 25010))


def main() -> int:
    rows = [line.split("=", 1) for line in ALLOC.read_text().splitlines() if "=" in line]
    ids = tuple(int(ident) for ident, _ in rows)
    assert ids == EXPECTED, f"RS2012 NPC allocation drifted: {ids}"
    assert "npc_base=25000" in MANIFEST.read_text()

    ledger_ids = tuple(
        int(fields[3])
        for line in LEDGER.read_text().splitlines()
        if (fields := line.split("\t")) and fields[0] == "npc"
    )
    assert ledger_ids == EXPECTED, f"NPC ledger disagrees with allocation: {ledger_ids}"
    encoder = ENCODER.read_text()
    assert "npc_add_requires_transformation(npc)" in encoder
    assert "force_type_latch ? npc->type : npc->change_type" in encoder
    assert "rsab_p2_alt3" in encoder
    print("rs2012 NPC wire ids: PASS (25000..25009 use rev239 transformation update)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
