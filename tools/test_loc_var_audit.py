#!/usr/bin/env python3
"""Hermetic test for loc_var_audit.py — the loc-transform variable audit.

Two things are being defended, and only the second is about the tool:

  1. `--check-carriers` must go RED when a script writes a loc-transform varbit
     whose carrier varp is not declared `transmit=yes`. That is the failure
     mode the whole file exists for — it is invisible at compile time (the
     varbit resolves), invisible at run time (`ToriRSServer_WorldMarkVarp` drops
     the packet with no message), and invisible on screen (the loc simply never
     changes). 204 varbits were in that state when the check was written.

  2. The classifier must not call working content broken. A multiloc whose
     state is produced by `loc_change` on the variants, or by a whole-varp
     write to the carrier, is implemented — differently, but implemented — and
     a queue that lists those is a queue nobody trusts.

Every case builds a throwaway content tree, so nothing here depends on
OSRS-Content being checked out.
"""

from __future__ import annotations

import importlib.util
import io
import os
import tempfile
from contextlib import redirect_stdout
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
TOOL = REPO / "tools/loc_var_audit.py"


def load_tool():
    spec = importlib.util.spec_from_file_location("loc_var_audit", TOOL)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def build_tree(root, *, script, varp_decl, loc_extra="", varbit_extra="",
               loc_text=None):
    """A content tree with one multiloc on one varbit in one carrier.

    loc 100 `gate_multi` transforms on varbit 7 `gate_open` (carrier varp 3
    `gate_carrier`, bit 0) into `gate_shut` at value 0 and `gate_ajar` at 1,
    and is placed once on square 50_50. `loc_text` replaces the all.loc
    records (FULL_KEY_LOC states the same three locs in the full-key format).
    """
    cfg = root / "configs"
    cfg.mkdir(parents=True)
    (cfg / "all.loc.compack").write_text(
        "100=gate_multi\n101=gate_shut\n102=gate_ajar\n", encoding="utf8")
    (cfg / "all.varbit.compack").write_text("7=gate_open\n", encoding="utf8")
    (cfg / "all.varp.compack").write_text("3=gate_carrier\n", encoding="utf8")
    (cfg / "all.varbit").write_text(
        "[gate_open]\nbasevar=gate_carrier\nstartbit=0\nendbit=0\n" + varbit_extra,
        encoding="utf8")
    if loc_text is None:
        loc_text = (
            "[gate_multi]\n"
            "multivarbit=gate_open\n"
            "multiloc1=gate_shut\n"
            "multiloc2=gate_ajar\n"
            "\n[gate_shut]\nname=Gate\nop1=Open\n"
            "\n[gate_ajar]\nname=Gate\nop1=Close\n")
    (cfg / "all.loc").write_text(loc_text + loc_extra, encoding="utf8")

    maps = root / "maps"
    maps.mkdir()
    (maps / "m50_50.jl2").write_text("==== LOC ====\n0 10 10: 100 0 0\n", encoding="utf8")

    pkg = root / "server" / "scripts" / "gates"
    (pkg / "scripts").mkdir(parents=True)
    (pkg / "configs").mkdir()
    (pkg / "scripts" / "gates.rs2").write_text(script, encoding="utf8")
    if varp_decl:
        (pkg / "configs" / "gates.varp").write_text(varp_decl, encoding="utf8")


BOUND_AND_WRITTEN = "[oploc1,gate_shut]\n%gate_open = 1;\n"
BOUND_ONLY = "[oploc1,gate_shut]\nmes(\"It will not budge.\");\n"
TRANSMIT = "[gate_carrier]\nprotect=no\ntransmit=yes\nscope=perm\n"

# The same three locs in the full-key config format: every key a record does
# not set is stated `key=default` (absent) or `key=empty` (an empty list), and
# `\default` is a string that happens to read "default". A reader that took the
# markers as values would find a multiloc on a varbit named "default" in every
# plain loc, and a base named "\default". The switch a multiloc does not use is
# written `-1` (cachepack's own unpack does that): no varp either.
FULL_KEY_LOC = (
    "[gate_multi]\n"
    "multivarbit=gate_open\n"
    "multivarp=-1\n"
    "multiloc1=gate_shut\n"
    "multiloc2=gate_ajar\n"
    "name=\\default\n"
    "op=default\n"
    "retex=empty\n"
    "\n[gate_shut]\nname=Gate\nop1=Open\n"
    "multivarp=default\nmultiloc=empty\nmultivarbit=default\nretex=empty\n"
    "\n[gate_ajar]\nname=Gate\nop1=Close\n"
    "multivarp=default\nmultiloc=empty\nmultivarbit=default\nretex=empty\n")
# A full-key varp record: the keys it does not set are stated as markers.
TRANSMIT_FULL_KEY = ("[gate_carrier]\nclientcode=default\nprotect=no\n"
                     "transmit=yes\nscope=perm\ntype=default\n")
# `transmit=default` means the key is absent -- the varp does not transmit.
TRANSMIT_DEFAULT = "[gate_carrier]\nprotect=no\ntransmit=default\nscope=perm\n"
NO_TRANSMIT = "[gate_carrier]\nprotect=no\ntransmit=no\nscope=perm\n"


def rows_for(mod, **kw):
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp) / "tree"
        build_tree(root, **kw)
        rows = mod.build_rows(str(root))
        for r in rows:
            r["bucket"] = mod.classify(r)
        return rows


def one(rows, name="gate_open"):
    hits = [r for r in rows if r["name"] == name]
    assert len(hits) == 1, f"expected one row for {name}, got {len(hits)}"
    return hits[0]


def check(cond, what):
    if not cond:
        raise AssertionError(what)
    print(f"  ok  {what}")


def main():
    mod = load_tool()
    failures = 0

    print("the join itself")
    r = one(rows_for(mod, script=BOUND_AND_WRITTEN, varp_decl=TRANSMIT))
    check(r["id"] == 7 and r["basevar"] == "gate_carrier", "varbit id and carrier resolve")
    check(r["placements"] == 1, "a placed base is counted from maps/*.jl2")
    check(r["writes"] == 1 and r["reads"] == 1, "the script's write is seen")
    check(r["op_bound"] == ["gates/scripts/gates.rs2"],
          "the variant's op binding is attributed to the base's variable")
    check(r["bucket"] == "implemented", "written + transmitting carrier is implemented")

    print("\nthe gap the queue is for")
    r = one(rows_for(mod, script=BOUND_ONLY, varp_decl=TRANSMIT))
    check(r["bucket"] == "op_bound_gap",
          "a bound loc whose variable nothing writes is an op_bound_gap")

    print("\nworking content the classifier must not call broken")
    r = one(rows_for(mod, script="[oploc1,gate_shut]\nloc_change(gate_ajar, 100);\n",
                     varp_decl=TRANSMIT))
    check(r["bucket"] == "loc_change_driven",
          "loc_change on a variant is an implementation, not a gap")
    r = one(rows_for(mod, script="[oploc1,gate_shut]\n%gate_carrier = 1;\n",
                     varp_decl=TRANSMIT))
    check(r["bucket"] == "carrier_written",
          "a whole-varp write to the carrier sets the bit and counts")
    r = one(rows_for(mod, script=BOUND_AND_WRITTEN, varp_decl=TRANSMIT,
                     loc_extra="\n[unplaced_multi]\nmultivarbit=gate_open\nmultiloc1=-1\n"))
    check(r["placements"] == 1,
          "an unplaced second base adds no placements to the same variable")

    print("\nthe carrier check")
    for decl, want, why in (
        (TRANSMIT, 0, "a declared transmitting carrier passes"),
        (NO_TRANSMIT, 1, "transmit=no is caught"),
        ("", 1, "an undeclared carrier is caught"),
        (TRANSMIT_FULL_KEY, 0, "a full-key transmitting carrier passes"),
        (TRANSMIT_DEFAULT, 1, "transmit=default (absent) is caught"),
    ):
        rows = rows_for(mod, script=BOUND_AND_WRITTEN, varp_decl=decl)
        buf = io.StringIO()
        with redirect_stdout(buf):
            rc = mod.cmd_check_carriers(rows)
        try:
            check(rc == want, why)
        except AssertionError as e:
            print(f"  FAIL {e}\n{buf.getvalue()}")
            failures += 1

    # A varbit nothing writes cannot break this way, and reporting it here would
    # bury the real ones under the 1,516 the tree has not implemented at all.
    rows = rows_for(mod, script=BOUND_ONLY, varp_decl="")
    buf = io.StringIO()
    with redirect_stdout(buf):
        rc = mod.cmd_check_carriers(rows)
    check(rc == 0, "an unwritten varbit is not a carrier failure")

    print("\nthe full-key config format")
    rows = rows_for(mod, script=BOUND_AND_WRITTEN, varp_decl=TRANSMIT_FULL_KEY,
                    loc_text=FULL_KEY_LOC)
    check([r["name"] for r in rows] == ["gate_open"],
          "a `multivarbit=default` / `multivarp=default` / `multivarp=-1` names no variable")
    r = one(rows)
    check(r["placements"] == 1 and r["bucket"] == "implemented",
          "the full-key tree classifies as the old one does")
    check([l["variants"] for l in r["locs"]] == [["gate_shut", "gate_ajar"]],
          "the multiloc variants still resolve")
    check([l["display"] for l in r["locs"]] == ["default"],
          "`name=\\default` is the string \"default\", not a marker")

    print("\nall checks passed" if not failures else f"\n{failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
