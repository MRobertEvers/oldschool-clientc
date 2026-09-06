#!/usr/bin/env python3
"""Break temporary production sources; reuse the existing UI/CS2 test targets."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess


def run_cs2_controls(src, out, make_args, selected):
    make = ["make", "--no-print-directory", *make_args]
    with (out / "positive.log").open("w") as log:
        subprocess.run([*make, "test-cs2-transmit-pump"], cwd=src,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    recipe = subprocess.check_output([*make, "-n", "test-cs2-transmit-pump"],
                                     cwd=src, text=True).replace("\\\n", " ")
    link = next(shlex.split(line) for line in recipe.splitlines()
                if "game/test/rs_cs2_transmit_pump_test.c " in line and " -o " in line)
    compile_commands = {}
    snapshot = "this same pass. */\n        if( UITree_ResolveRef(self->host->tree, hook->ref) < 0 )"
    registry = ("if( UITree_ResolveRef(self->host->tree, hook->ref) < 0 )\n"
                "        {\n            hook->last_seen_serial = self->host->inv_change_serial;")
    controls = {
        "resume_context": ("if( self->has_resume_context )", "if( false )",
                           "resumed callback respects widget incarnation dot=0 stale=1"),
        "dispatch_compaction": (
            "if( hint < count && task_cs2_refs_equal(ref, task_cs2_hook_ref(hooks, stride, hint)) )",
            "if( hint < count )", "compaction cannot skip original listener channel=0"),
        "queued_origin": ("(!self->started &&", "(false &&",
                          "queued CS2 callback cannot write recycled native ID"),
        "snapshot_identity": (snapshot, snapshot.replace(
            "UITree_ResolveRef(self->host->tree, hook->ref)",
            "UITree_FindByComponentId(self->host->tree, hook->component_id)"),
            "snapshot callback 0 cannot reach replacement"),
        "registry_identity": (registry, registry.replace(
            "UITree_ResolveRef(self->host->tree, hook->ref)",
            "UITree_FindByComponentId(self->host->tree, hook->component_id)"),
            "registry callback 0 cannot reach replacement"),
    }
    controls = {name: ("game/task_cs2_run.c", *control) for name, control in controls.items()}
    controls.update({
        "widget_pose": ("engine/uitree_anim.c", "if( !cache ) return source;", "if( true ) return source;",
                        "model widgets own independent animation poses"),
        "model_resource_revision": ("engine/uitree_anim.c",
            "if( !pose || pose->source_revision != revision )", "if( !pose )",
            "resource replacement refreshes the private pose"),
        "skeletal_widget": ("engine/uitree_anim.c",
            "!anim || (!anim->base && !anim->skeletal) || anim->frame_count <= 0",
            "!anim || !anim->base || anim->frame_count <= 0", "skeletal widget clock advances"),
        "active_animation": ("ui/uitree_emit.c",
            "out->model_anim_seq = active ? component->u.rs_model.active_anim_seq_id : component->u.rs_model.anim_seq_id;",
            "out->model_anim_seq = component->u.rs_model.anim_seq_id;",
            "cache active animation reaches native rendering dat2=0"),
    })
    if selected and set(selected) - controls.keys():
        raise ValueError("unknown CS2 control")
    for name, (source, before, after, expected) in controls.items():
        if selected and name not in selected:
            continue
        if source not in compile_commands:
            obj = next(arg for arg in link if arg.endswith("/" + Path(source).stem + ".o"))
            recipe = subprocess.check_output([*make, "-n", "-W", source, obj],
                                             cwd=src, text=True).replace("\\\n", " ")
            compile_cmd = next(shlex.split(line) for line in recipe.splitlines()
                               if f" -c {source} " in line)
            compile_commands[source] = (obj, compile_cmd)
        obj, compile_cmd = compile_commands[source]
        original = (src / source).read_text()
        if original.count(before) != 1:
            raise RuntimeError(f"{name}: mechanism changed; update explicit mutation")
        mutant = out / f"{name}.c"
        mutant.write_text(original.replace(before, after))
        mutant_obj = out / f"{name}.o"
        binary = out / name
        compile_mutant = [str(mutant) if arg == source else arg for arg in compile_cmd]
        compile_mutant[compile_mutant.index("-o") + 1] = str(mutant_obj)
        compile_mutant.extend(["-I", str((src / source).parent)])
        link_mutant = [str(mutant_obj) if arg == obj else arg for arg in link]
        link_mutant[link_mutant.index("-o") + 1] = str(binary)
        with (out / f"{name}-build.log").open("w") as log:
            for command in (compile_mutant, link_mutant):
                subprocess.run(command, cwd=src, stdout=log, stderr=subprocess.STDOUT, check=True)
        run = subprocess.run([str(binary)], cwd=src, text=True, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, timeout=30)
        (out / f"{name}.log").write_text(run.stdout)
        if run.returncode != 1 or "FAIL " + expected not in run.stdout:
            raise RuntimeError(f"{name}: expected assertion was not observed")
        print(f"{name}: observed expected failing assertion", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    parser.add_argument("--only", action="append", help="run only this named mechanism (repeatable)")
    parser.add_argument("--suite", choices=("ui", "cs2"), default="ui")
    parser.add_argument("--make-arg", action="append", default=[], help="make assignment, e.g. OPT=1")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    out = args.out.resolve()
    src = Path(__file__).resolve().parents[1] / "src"
    if args.suite == "cs2":
        run_cs2_controls(src, out, args.make_arg, args.only)
        return
    recipe = subprocess.check_output(
        ["make", "--no-print-directory", *args.make_arg, "-n", "test-uitree"], cwd=src, text=True
    ).replace("\\\n", " ")
    command = next(shlex.split(line) for line in recipe.splitlines()
                   if "ui/uitree.c " in line and " -o " in line)
    original = (src / "ui/uitree.c").read_text()
    controls = {
        "operation_freshness": (
            "(!pick->action_signature || pick->action_signature == UITree_ActionSignatureAt(tree, pick->node_index))",
            "true", "retained menu rejects changed operation labels"),
        "operation_text": (
            "hash = action_hash_text(hash, c->u.rs_text.text);",
            "(void)hash;", "retained menu rejects changed native text target"),
        "geometry_audit": ("if( current[i] != record->values[i] )", "if( false )",
                           "audit catches unclassified native position"),
        "global_identity": ("component->incarnation = atomic_fetch_add(&next_incarnation, 1);",
                            "component->incarnation = (uint64_t)idx + 1;",
                            "retained incarnation cannot affect another tree"),
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
        if run.returncode != 1 or "FAIL: " + expected not in run.stdout:
            raise RuntimeError(f"{name}: did not produce its expected assertion: {run.stdout}")
        print(f"{name}: observed expected failing assertion", flush=True)


if __name__ == "__main__":
    main()
