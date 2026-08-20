"""Rebuilding what a world is derived from, driven by the manifest.

A world is not just a manifest: it is a composed cache and a compiled server
script pack, both derived from the content tree, and both invisible when stale
— an edited config that never reached the cache looks exactly like a config
that was never written.

`run-live.sh` already knows how to decide this, but it knows it as code: a
table of which lane implies which bake target, which lineage is pristine-rooted
and must refuse rather than rebuild. That table can only recognise worlds it
has been taught. A manifest that declares its own derivations needs no such
table, which is the same move `[content:lanes]` already made when it stopped
the launcher from guessing lanes out of a directory name.

    [derived:cache]
    out=../cache.osrs239.summoning
    check=tools/cache_overlay_stale.py --cache {out} --lane scape2009_summoning
    target=torirsserver-cache-summoning
    make_args=TORIRSSERVER_CACHE_DIR={out}

`{out}` expands to the absolute path of `out=` (manifest-relative, like every
other path key). The launcher stays generic: it runs `check`, and rebuilds with
`target` when told to. The POLICY stays in the checker, where it already lives.

THE EXIT-CODE CONTRACT, which is the checker's and not this module's invention:

    0  rebuild — missing, incomplete, or an input is newer
    1  skip    — every input is older than the output
    2  error   — usage, or a required input is absent

Anything that is not exactly 1 means rebuild. A predicate that cannot answer
must never be read as "up to date": a needless bake costs two minutes, and a
skipped one costs a session spent debugging content that was never there.
"""

import os
import shlex
import subprocess


class DerivedResult:
    def __init__(self, name, action, detail=""):
        self.name = name
        self.action = action  # "fresh" | "rebuilt" | "failed" | "skipped"
        self.detail = detail


def _expand(template, mapping):
    out = template
    for key, value in mapping.items():
        out = out.replace("{%s}" % key, value)
    return out


def check_derived(manifest, block_name, fields, repo_root, verbose=False):
    """Run one block's checker. Returns (should_rebuild, detail)."""
    check = fields.get("check")
    out = fields.get("out")
    if not check:
        # No checker: the block declares an artifact but no way to test it, so
        # the honest answer is "rebuild", not "assume fresh".
        return True, "no check= declared"

    out_abs = manifest.resolve_path(out) if out else ""
    argv = shlex.split(_expand(check, {"out": out_abs}))
    if argv and argv[0].endswith(".py"):
        argv = ["python3"] + argv

    proc = subprocess.run(
        argv, cwd=repo_root,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    text = proc.stdout.decode("utf-8", "replace").strip()
    if verbose and text:
        print(text)
    if proc.returncode == 1:
        return False, text or "up to date"
    if proc.returncode == 0:
        return True, text or "stale"
    return True, "checker exited %d: %s" % (proc.returncode, text or "(no output)")


def rebuild_derived(manifest, block_name, fields, repo_root, verbose=True):
    """Run one block's make target. Returns (ok, detail)."""
    target = fields.get("target")
    if not target:
        return False, "block declares no target= to rebuild with"

    out_abs = manifest.resolve_path(fields.get("out") or "")
    argv = ["make", "-C", "src", target]
    make_args = fields.get("make_args")
    if make_args:
        argv += shlex.split(_expand(make_args, {"out": out_abs}))

    print("launch: %s is stale — %s" % (block_name, " ".join(argv)))
    proc = subprocess.run(argv, cwd=repo_root)
    if proc.returncode != 0:
        return False, "make exited %d" % proc.returncode
    return True, " ".join(argv)


def prepare_derived(manifest, repo_root, force=False, verbose=False):
    """Check and rebuild every `[derived:*]` block the manifest declares.

    Returns (results, ok). Blocks run in file order, so a manifest can state a
    base bake before the overlay stacked on it.
    """
    results = []
    for name, fields in manifest.derived():
        if force:
            should, detail = True, "forced"
        else:
            should, detail = check_derived(
                manifest, name, fields, repo_root, verbose=verbose)
        if not should:
            results.append(DerivedResult(name, "fresh", detail))
            continue
        ok, build_detail = rebuild_derived(
            manifest, name, fields, repo_root, verbose=verbose)
        if not ok:
            results.append(DerivedResult(name, "failed", build_detail))
            return results, False
        results.append(DerivedResult(name, "rebuilt", detail))
    return results, True


def coverage_gaps(manifest):
    """Artifacts this manifest names but declares no `[derived:*]` block for.

    Declaring even one block opts the world out of run-live.sh's preparation
    entirely — the launcher cannot run both without rebuilding things twice and
    in the wrong order. That makes a PARTIAL declaration the dangerous state:
    the covered artifact stays fresh, the uncovered one silently stops being
    checked at all, and the symptom is content that is present in the tree and
    absent in the game. So a partial declaration is reported rather than
    quietly honoured.
    """
    declared = {name for name, _ in manifest.derived()}
    if not declared:
        return []
    gaps = []
    if manifest.cache_dir and "cache" not in declared:
        gaps.append(
            ("cache", "[cache:boot] dir=%s" % manifest.ini.get("cache:boot", "dir")))
    if manifest.server_scripts and "scripts" not in declared:
        gaps.append(
            ("scripts", "[net:boot] scripts=%s" % manifest.ini.get("net:boot", "scripts")))
    return gaps


def prepare_via_run_live(manifest_path, repo_root, env=None):
    """Fall back to run-live.sh's own preparation for a manifest with no
    `[derived:*]` blocks.

    TORIRS_PREPARE_ONLY=1 is an existing seam, not one invented here:
    run-runelite.sh already borrows this file's bake policy through it rather
    than copying the policy. Using the same door means a world that has not yet
    been migrated to `[derived:*]` still gets exactly the preparation it always
    got, with no second implementation to drift.
    """
    script = os.path.join(repo_root, "run-live.sh")
    if not os.path.isfile(script):
        return True, "run-live.sh not present — nothing prepared"
    process_env = dict(os.environ)
    process_env["TORIRS_PREPARE_ONLY"] = "1"
    if env:
        process_env.update({str(k): str(v) for k, v in env.items()})
    proc = subprocess.run(
        ["./run-live.sh", manifest_path], cwd=repo_root, env=process_env)
    if proc.returncode != 0:
        return False, "run-live.sh preparation exited %d" % proc.returncode
    return True, "prepared via run-live.sh"
