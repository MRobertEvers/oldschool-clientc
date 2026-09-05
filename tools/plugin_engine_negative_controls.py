#!/usr/bin/env python3
"""Break production mechanisms in temporary sources; reuse the UITree harness."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    parser.add_argument("--only", action="append", help="run only this named mechanism (repeatable)")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    out = args.out.resolve()
    src = Path(__file__).resolve().parents[1] / "src"
    recipe = subprocess.check_output(
        ["make", "--no-print-directory", "-n", "test-uitree"], cwd=src, text=True
    ).replace("\\\n", " ")
    command = next(shlex.split(line) for line in recipe.splitlines()
                   if "ui/uitree.c " in line and " -o " in line)
    original = (src / "ui/uitree.c").read_text()
    controls = {
        "copy_state": ("i < src.params_count", "i < 0", "copy preserves integer parameters"),
        "tree_identity": ("ref.tree_instance != tree->instance_id", "false",
                          "reference cannot cross tree instances"),
        "identity": ("!c->freed && c->incarnation == ref.incarnation",
                     "!c->freed", "old focus cannot move"),
        "geometry": ("if( pos->layout_resolved )\n        return pos->abs_w",
                     "if( pos->layout_resolved && pos->abs_w > 0 )\n        return pos->abs_w",
                     "computed zero is not replaced"),
        "content_hide": ("c->item_id = obj_id;",
                         "UITree_SetHideAt(tree, idx, 0); c->item_id = obj_id;",
                         "content update cannot clear native hiding"),
        "mount_hide": ("c->mount_hidden = (uint8_t)hidden;",
                       "c->mount_hidden = (uint8_t)hidden; if( !hidden ) UITree_SetHideAt(tree, idx, 0);",
                       "mount cannot clear later server hide"),
        "topology": ("ancestor == child_index ||", "false ||", "reject descendant cycle"),
    }
    if args.only and set(args.only) - controls.keys():
        parser.error("unknown control: " + ", ".join(set(args.only) - controls.keys()))
    for name, (before, after, expected) in controls.items():
        if args.only and name not in args.only:
            continue
        if original.count(before) != 1:
            raise RuntimeError(f"{name}: mechanism changed; update the explicit mutation")
        mutant = out / f"{name}.c"
        mutant.write_text(original.replace(before, after))
        binary = out / name
        build = [str(mutant) if arg == "ui/uitree.c" else arg for arg in command]
        build[build.index("-o") + 1] = str(binary)
        with (out / f"{name}-build.log").open("w") as log:
            subprocess.run(build, cwd=src, stdout=log, stderr=subprocess.STDOUT, check=True)
        run = subprocess.run([str(binary)], cwd=src,
                             env={**os.environ, "TORIRS_TEST_CONTRACT_V3": "1"},
                             text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             timeout=30)
        (out / f"{name}.log").write_text(run.stdout)
        if run.returncode != 1 or expected not in run.stdout:
            raise RuntimeError(f"{name}: did not produce its expected assertion: {run.stdout}")
        print(f"{name}: observed expected failing assertion", flush=True)


if __name__ == "__main__":
    main()
