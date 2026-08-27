"""Put an XP profile on the box: the client, its manifest, and what they need.

    python docs/d3d9/deploy_xp.py rs289lc-xp
    python docs/d3d9/deploy_xp.py rs289lc-xp --exe-name torirs-abc.exe

This is the step that used to live in somebody's shell history. Everything else
about the benchmark lane was already in the repo -- build_winxp.ps1 builds,
mkjob_raster.py composes a job, drive.py submits it, runner.py.tmpl runs it on
the box -- but getting the binary and its configuration ONTO the box was a
handful of ad-hoc box.put calls, and a lane you cannot redo from a clean
checkout is a lane whose results you cannot redo either.

WHAT GOES, AND WHY EACH ONE

  src/torirs.exe                     the client, under its canonical name so a
                                     pushed build, a hand run and launch.cmd
                                     all mean the same file
  build/manifests/<profile>.ini      COMPOSED here, from profiles/<profile>.ini,
                                     because the box has no launcher to compose
                                     it with (Python 3.2 there; tools/launcher
                                     needs 3.5+)
  revconfig/**                       the UI and cache bindings the manifest
                                     names. The box had none of this, and a
                                     missing revconfig is not a clean error --
                                     it is a client that boots to a broken
                                     interface
  launch.cmd                         so `launch.cmd run <profile>` works there
                                     too; it detects the absent Python and
                                     falls back to reading build/manifests/

WHAT DOES NOT GO, AND WHY NOT

No proxy. The osrs239 lane needs one (tcp_forward_43596.py) because
torirsserver binds INADDR_LOOPBACK and the box cannot reach loopback on
another machine. The LostCity server does not have that problem, and that is
measured rather than assumed -- from the box:

    10.10.10.1:43594 CONNECT OK          the game port
    10.10.10.1:80    CONNECT OK          the web port
    GET /crc      -> 40 bytes            the login checksums
    on-demand handshake (byte 15)
                  -> 8 bytes             the cache stream, on the game port

All four wires the manifest names answer. A forwarder here would be code that
runs, does nothing, and has to be kept working. If a future server binds
loopback, tcp_forward_43596.py is the pattern to copy.

No cache directory. This world streams its cache (`source=ondemand`), which is
the whole reason the manifest states no `dir=`.
"""
import argparse
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, os.pardir, os.pardir))
sys.path.insert(0, HERE)
sys.path.insert(0, REPO)

import box  # noqa: E402

BS = chr(92)
BOX_ROOT = r'C:\dev\oldschool-clientc'
ATTEMPTS = 4


def put(local, rel):
    """Upload with retries, then read back and compare the length.

    The box's HTTP endpoint shares its one CPU with the desktop streamer that
    hosts it and drops a connection now and then. A half-shipped tree is worse
    than an unshipped one: it boots, and then misbehaves somewhere that looks
    unrelated to the thing that did not arrive.
    """
    remote = BOX_ROOT + BS + rel
    sent = None
    for attempt in range(ATTEMPTS):
        try:
            sent = box.put(local, remote)
            break
        except Exception as exc:
            if attempt == ATTEMPTS - 1:
                raise
            sys.stderr.write('  retry %d: %s (%s)\n'
                             % (attempt + 1, rel, type(exc).__name__))
            time.sleep(1.5 * (attempt + 1))
    back = None
    for _ in range(ATTEMPTS):
        try:
            back = box.get(remote)
            break
        except Exception:
            time.sleep(1.0)
    ok = back is not None and len(back) == sent
    print('  %-46s %9d %s' % (rel, sent, 'ok' if ok else 'READ-BACK MISMATCH'))
    return ok


def ensure_dirs(rels):
    """fs/put writes a file but will not create the directory above it.

    There is no mkdir endpoint, so the script endpoint makes the tree. This is
    why revconfig/ silently refused to upload the first time it was tried: the
    directory did not exist, and every file in it failed the same way.
    """
    wanted = sorted({os.path.dirname(r) for r in rels if os.path.dirname(r)})
    if not wanted:
        return
    lines = ['import os']
    for d in wanted:
        lines.append('os.makedirs(%r, exist_ok=True)' % (BOX_ROOT + BS + d))
    lines.append("print('dirs ok')")
    box.putscript('\n'.join(lines) + '\n', 'deploy_mkdirs.py')
    box.run('deploy_mkdirs.py')


def compose(profile_name):
    """Generate build/manifests/<profile>.ini from profiles/<profile>.ini."""
    from tools.launcher import profiles as profiles_mod
    profile = profiles_mod.load_profile(REPO, profile_name)
    out_dir = os.path.join(REPO, 'build', 'manifests')
    path = profiles_mod.generate_resolved_manifest(profile, out_dir)
    if os.path.abspath(path) == os.path.abspath(profile.world_path):
        raise SystemExit(
            "profile '%s' states no [override:*] block, so it composes to the\n"
            "base manifest -- which names localhost and is unreachable from the\n"
            "box. An XP profile has to redirect [net:boot]." % profile_name)
    return path


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('profile', help='an XP profile, e.g. rs289lc-xp')
    ap.add_argument('--exe', default=os.path.join('src', 'torirs.exe'),
                    help='the built client (default src/torirs.exe)')
    ap.add_argument('--exe-name', default='torirs.exe',
                    help='what to call it on the box (default torirs.exe)')
    ap.add_argument('--no-exe', action='store_true',
                    help='configuration only; leave the binary alone')
    args = ap.parse_args()

    os.chdir(REPO)

    manifest = compose(args.profile)
    manifest_rel = os.path.relpath(manifest, REPO).replace('/', BS).replace(os.sep, BS)
    print('composed %s' % manifest_rel)

    items = [(manifest_rel, manifest_rel), ('launch.cmd', 'launch.cmd')]
    for base, _sub, files in os.walk('revconfig'):
        for fn in files:
            local = os.path.join(base, fn)
            items.append((local, local.replace('/', BS).replace(os.sep, BS)))
    if not args.no_exe:
        if not os.path.exists(args.exe):
            raise SystemExit(
                '%s is not built. Run .\\build_winxp.ps1 first -- and note that\n'
                'it must be the win32 lane: src/torirs_win64.exe will not run on\n'
                'the box at all.' % args.exe)
        items.insert(0, (args.exe, args.exe_name))

    ensure_dirs([rel for _local, rel in items])

    print('to %s:' % BOX_ROOT)
    bad = 0
    for local, rel in items:
        if not put(local, rel):
            bad += 1

    print()
    if bad:
        print('%d file(s) did not verify -- do NOT trust a run off this tree' % bad)
        return 1
    print('%d files deployed and verified.' % len(items))
    print('On the box:  launch.cmd run %s --soft3d' % args.profile)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
