#!/usr/bin/env python3
"""Static contract checks for the Stronghold of Security implementation.

Run from the repository root:

    python3 tools/check_stronghold_contract.py

The checks intentionally read the authored ServerScript and maplink data as
well as the rev-239 map placements.  They require no cache binary, compiler,
or third-party Python package.
"""

from __future__ import annotations

from collections import Counter
from dataclasses import dataclass
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
CONTENT = ROOT / "OSRS-Content" / "osrs239-content"
STRONGHOLD_SCRIPT = (
    CONTENT
    / "server"
    / "scripts"
    / "areas"
    / "area_stronghold"
    / "scripts"
    / "stronghold.rs2"
)
STRONGHOLD_MAPLINKS = (
    CONTENT
    / "server"
    / "scripts"
    / "areas"
    / "area_stronghold"
    / "configs"
    / "stronghold_maplinks.dbrow"
)
GENERATED_MAPLINKS = (
    CONTENT
    / "server"
    / "scripts"
    / "ladders_stairs"
    / "configs"
    / "maplink.dbrow"
)
LOC_COMPACK = CONTENT / "configs" / "all.loc.compack"
MAPS = CONTENT / "maps"


GATE_HANDLERS = {
    "sos_war_door_face": 1,
    "sos_war_door_face_mirr": 1,
    "sos_fam_door_face": 2,
    "sos_fam_door_face_mirr": 2,
    "sos_pest_door_face": 3,
    "sos_pest_door_face_mirr": 3,
    "sos_death_door_face": 4,
    "sos_death_door_face_mirr": 4,
}

GATE_FAMILIES = {
    "war": ("sos_war_door_face", "sos_war_door_face_mirr"),
    "famine": ("sos_fam_door_face", "sos_fam_door_face_mirr"),
    "pestilence": ("sos_pest_door_face", "sos_pest_door_face_mirr"),
    "death": ("sos_death_door_face", "sos_death_door_face_mirr"),
}
EXPECTED_GATE_COUNTS = {
    "war": 72,
    "famine": 76,
    "pestilence": 60,
    "death": 72,
}

# (player source, destination, clicked loc, direction)
SUPPLEMENTAL_MAPLINKS = {
    # Vault of War: Spikey chain -> floor-one start.
    ("0_29_81_24_48", "0_29_81_3_59", "sos_war_chainbottom", 1),
    ("0_29_81_26_48", "0_29_81_3_59", "sos_war_chainbottom", 1),
    ("0_29_81_25_49", "0_29_81_3_59", "sos_war_chainbottom", 1),
    # Catacomb of Famine: four ropes -> floor-two start.
    ("0_31_81_26_8", "0_31_81_58_61", "sos_fam_rope_up", 1),
    ("0_31_81_28_8", "0_31_81_58_61", "sos_fam_rope_up", 1),
    ("0_31_81_27_7", "0_31_81_58_61", "sos_fam_rope_up", 1),
    ("0_31_81_32_26", "0_31_81_58_61", "sos_fam_rope_up", 1),
    ("0_31_81_34_26", "0_31_81_58_61", "sos_fam_rope_up", 1),
    ("0_31_81_33_25", "0_31_81_58_61", "sos_fam_rope_up", 1),
    ("0_31_81_33_27", "0_31_81_58_61", "sos_fam_rope_up", 1),
    ("0_31_81_46_5", "0_31_81_58_61", "sos_fam_rope_up", 1),
    ("0_31_81_48_5", "0_31_81_58_61", "sos_fam_rope_up", 1),
    ("0_31_81_47_4", "0_31_81_58_61", "sos_fam_rope_up", 1),
    ("0_31_81_47_6", "0_31_81_58_61", "sos_fam_rope_up", 1),
    ("0_31_81_57_24", "0_31_81_58_61", "sos_fam_rope_up", 1),
    # Pit of Pestilence: missing west approach -> floor-three start.
    ("0_33_82_37_30", "0_33_82_11_4", "sos_pest_rope_up", 1),
    # Western Death chain -> floor-four start (not the surface).
    ("0_36_81_6_56", "0_36_81_54_31", "sos_death_rope_up", 1),
    ("0_36_81_5_55", "0_36_81_54_31", "sos_death_rope_up", 1),
    ("0_36_81_5_57", "0_36_81_54_31", "sos_death_rope_up", 1),
}

CENTRAL_DEATH_SOURCES = {
    "0_36_81_45_31",
    "0_36_81_46_30",
    "0_36_81_46_32",
    "0_36_81_47_31",
}
CENTRAL_DEATH_DEST = "0_48_53_9_29"


@dataclass(frozen=True)
class MaplinkRow:
    name: str
    src: str
    dest: str
    loc: str
    direction: int

    def contract_tuple(self) -> tuple[str, str, str, int]:
        return self.src, self.dest, self.loc, self.direction


def read_text(path: Path, errors: list[str]) -> str | None:
    try:
        return path.read_text(encoding="utf-8")
    except OSError as exc:
        errors.append(f"cannot read {path.relative_to(ROOT)}: {exc}")
        return None


def trigger_blocks(text: str) -> list[tuple[str, str]]:
    """Return ``(header, body)`` pairs for ServerScript-style triggers."""
    matches = list(re.finditer(r"(?m)^[ \t]*\[([^\]\r\n]+)\]", text))
    blocks: list[tuple[str, str]] = []
    for index, match in enumerate(matches):
        end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
        blocks.append((match.group(1).strip(), text[match.end() : end]))
    return blocks


def strip_line_comments(text: str) -> str:
    return "\n".join(line.split("//", 1)[0] for line in text.splitlines())


def check_gate_script(errors: list[str]) -> None:
    text = read_text(STRONGHOLD_SCRIPT, errors)
    if text is None:
        return
    blocks = trigger_blocks(text)

    for loc_name, floor in GATE_HANDLERS.items():
        header = f"oploc1,{loc_name}"
        bodies = [body for found_header, body in blocks if found_header == header]
        if len(bodies) != 1:
            errors.append(
                f"{STRONGHOLD_SCRIPT.relative_to(ROOT)} must contain exactly one "
                f"[{header}] handler (found {len(bodies)})"
            )
            continue
        code = strip_line_comments(bodies[0]).strip()
        if re.fullmatch(rf"~\s*sos_door\s*\(\s*{floor}\s*\)\s*;", code) is None:
            errors.append(
                f"[{header}] must delegate only to ~sos_door({floor}); "
                f"(found {code!r})"
            )

    force_bodies = [body for header, body in blocks if header == "proc,sos_force_pass"]
    if len(force_bodies) != 1:
        errors.append(
            f"{STRONGHOLD_SCRIPT.relative_to(ROOT)} must contain exactly one "
            f"[proc,sos_force_pass] (found {len(force_bodies)})"
        )
        return

    force_code = strip_line_comments(force_bodies[0])
    if re.search(r"~\s*agility_exactmove\s*\(", force_code) is None:
        errors.append("[proc,sos_force_pass] must use ~agility_exactmove(...)")

    forbidden = {
        "double-door proc": r"\b(?:open|close)_double_door_(?:left|right)\b",
        "generic active-door proc": r"\bdoor_(?:open|close)_active\b",
        "doubledoor helper": r"\bdoubledoor\w*\b",
        "loc mutation": r"\bloc_(?:add|del|change|replace)\s*\(",
    }
    for description, pattern in forbidden.items():
        if re.search(pattern, force_code, flags=re.IGNORECASE):
            errors.append(
                f"[proc,sos_force_pass] must not use {description}; Stronghold "
                "gates stay closed and use forced exact movement"
            )

    second_bodies = [body for header, body in blocks if header.startswith("proc,sos_second_gate")]
    if len(second_bodies) != 1:
        errors.append(
            f"{STRONGHOLD_SCRIPT.relative_to(ROOT)} must contain exactly one "
            f"[proc,sos_second_gate] (found {len(second_bodies)})"
        )
        return

    second_code = strip_line_comments(second_bodies[0])
    for helper in ("door_open", "check_axis"):
        if re.search(rf"~\s*{helper}\s*\(", second_code) is None:
            errors.append(
                f"[proc,sos_second_gate] must derive both traversal directions "
                f"with ~{helper}(...)"
            )
    for distance in (2, 3, 4):
        if re.search(rf"\$step_x\s*\*\s*{distance}", second_code) is None:
            errors.append(
                f"[proc,sos_second_gate] must inspect the {distance}-tile gate spacing"
            )


def read_loc_ids(errors: list[str]) -> dict[str, int]:
    text = read_text(LOC_COMPACK, errors)
    if text is None:
        return {}
    wanted = {name for names in GATE_FAMILIES.values() for name in names}
    found: dict[str, int] = {}
    for raw_line in text.splitlines():
        key, separator, name = raw_line.partition("=")
        if not separator or name not in wanted:
            continue
        try:
            loc_id = int(key)
        except ValueError:
            errors.append(f"invalid loc id for {name!r} in {LOC_COMPACK.relative_to(ROOT)}")
            continue
        if name in found:
            errors.append(f"duplicate compack entry for Stronghold gate loc {name}")
        found[name] = loc_id
    for name in sorted(wanted - found.keys()):
        errors.append(f"missing Stronghold gate loc {name} in {LOC_COMPACK.relative_to(ROOT)}")
    return found


def check_gate_placements(errors: list[str]) -> None:
    loc_ids = read_loc_ids(errors)
    if not loc_ids:
        return

    id_to_family: dict[int, str] = {}
    for family, names in GATE_FAMILIES.items():
        for name in names:
            if name in loc_ids:
                loc_id = loc_ids[name]
                if loc_id in id_to_family:
                    errors.append(
                        f"gate loc id {loc_id} is shared by {id_to_family[loc_id]} and {family}"
                    )
                id_to_family[loc_id] = family

    map_files = sorted(MAPS.glob("m*.jl2"))
    if not map_files:
        errors.append(f"no rev-239 .jl2 maps found under {MAPS.relative_to(ROOT)}")
        return

    counts: Counter[str] = Counter()
    placement = re.compile(r"^\s*\d+\s+\d+\s+\d+:\s+(\d+)(?:\s|$)")
    for path in map_files:
        try:
            lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
        except OSError as exc:
            errors.append(f"cannot read map {path.relative_to(ROOT)}: {exc}")
            continue
        for line in lines:
            match = placement.match(line)
            if match is None:
                continue
            family = id_to_family.get(int(match.group(1)))
            if family is not None:
                counts[family] += 1

    for family, expected in EXPECTED_GATE_COUNTS.items():
        actual = counts[family]
        if actual != expected:
            errors.append(
                f"rev-239 {family} closed gate leaves: expected {expected}, found {actual}"
            )
    total = sum(counts.values())
    if total != 280:
        errors.append(f"rev-239 closed gate leaf total: expected 280, found {total}")


def parse_maplinks(path: Path, errors: list[str]) -> list[MaplinkRow]:
    text = read_text(path, errors)
    if text is None:
        return []

    rows: list[MaplinkRow] = []
    for name, body in trigger_blocks(text):
        table = re.search(r"(?m)^\s*table\s*=\s*([^\s/]+)", body)
        if table is None or table.group(1) != "maplink":
            continue
        fields: dict[str, str] = {}
        for match in re.finditer(r"(?m)^\s*data\s*=\s*(src|dest|loc|dir)\s*,\s*([^\s/]+)", body):
            field, value = match.groups()
            if field in fields:
                errors.append(
                    f"duplicate data={field} in [{name}] of {path.relative_to(ROOT)}"
                )
            fields[field] = value
        missing = {"src", "dest", "loc", "dir"} - fields.keys()
        if missing:
            errors.append(
                f"incomplete maplink [{name}] in {path.relative_to(ROOT)}: "
                f"missing {', '.join(sorted(missing))}"
            )
            continue
        try:
            direction = int(fields["dir"])
        except ValueError:
            errors.append(
                f"maplink [{name}] in {path.relative_to(ROOT)} has invalid dir "
                f"{fields['dir']!r}"
            )
            continue
        rows.append(
            MaplinkRow(name, fields["src"], fields["dest"], fields["loc"], direction)
        )
    return rows


def format_maplink(link: tuple[str, str, str, int]) -> str:
    src, dest, loc, direction = link
    return f"src={src}, dest={dest}, loc={loc}, dir={direction}"


def check_maplinks(errors: list[str]) -> None:
    supplemental = parse_maplinks(STRONGHOLD_MAPLINKS, errors)
    actual = {row.contract_tuple() for row in supplemental}
    for missing in sorted(SUPPLEMENTAL_MAPLINKS - actual):
        errors.append(
            f"missing supplemental Stronghold maplink in "
            f"{STRONGHOLD_MAPLINKS.relative_to(ROOT)}: {format_maplink(missing)}"
        )

    generated = parse_maplinks(GENERATED_MAPLINKS, errors)
    for src in sorted(CENTRAL_DEATH_SOURCES):
        matches = [
            row
            for row in generated
            if row.src == src and row.loc == "sos_death_rope_up" and row.direction == 1
        ]
        if len(matches) != 1:
            errors.append(
                f"generated central Death-chain source {src} must have exactly one "
                f"sos_death_rope_up dir=1 row (found {len(matches)})"
            )
            continue
        if matches[0].dest != CENTRAL_DEATH_DEST:
            errors.append(
                f"generated central Death-chain source {src} must target surface "
                f"{CENTRAL_DEATH_DEST}, found {matches[0].dest}"
            )


def main() -> int:
    errors: list[str] = []
    check_gate_script(errors)
    check_gate_placements(errors)
    check_maplinks(errors)

    if errors:
        print("check_stronghold_contract: FAILED", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    counts = "/".join(str(EXPECTED_GATE_COUNTS[name]) for name in GATE_FAMILIES)
    print(
        "check_stronghold_contract: OK — "
        f"8 force-walk gate handlers; 280 leaves ({counts}); "
        f"{len(SUPPLEMENTAL_MAPLINKS)} supplemental maplinks; "
        f"{len(CENTRAL_DEATH_SOURCES)} central Death exits"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
