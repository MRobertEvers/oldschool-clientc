#!/usr/bin/env python3
"""Persistent real-client content acceptance runner. See docs/CONTENT_SELFTEST.md."""
import argparse
import re
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]

class Session:
    def __init__(self, directory):
        self.directory = Path(directory).resolve()

    def call(self, command, timeout=60):
        if "\n" in command or "\r" in command or len(command.encode()) >= 2048:
            raise ValueError("Mailbox commands must fit one line of less than 2048 bytes")
        response = self.directory / 'response'
        response.unlink(missing_ok=True)
        temp = self.directory / 'request.tmp'
        temp.write_text(command + '\n')
        temp.replace(self.directory / 'request')
        until = time.monotonic() + timeout
        while not response.exists():
            if getattr(self, "process", None) is not None and self.process.poll() is not None:
                raise RuntimeError(f"Client exited; see {self.directory / 'client.log'}")
            if time.monotonic() > until:
                raise TimeoutError(f'{command}: no response; see {self.directory / "client.log"}')
            time.sleep(.001)
        result = json.loads(response.read_text())
        if not result.get('ok'):
            raise AssertionError(f'{command}: {result}')
        return result

    def step(self, frames=30):
        return self.call(f'step {frames}')

    def until(self, predicate, label, ticks=40):
        for _ in range(ticks):
            state = self.call('state')
            if predicate(state):
                return state
            self.step()
        raise AssertionError(f'{label}: deadline exceeded: {state}')

    def bit(self, name, value):
        state = self.call(f'varbit {name}')
        assert state['client'] == state['server'] == value, (name, value, state)

    def shot(self, name):
        path = self.directory / f'{name}.png'
        self.call(f'shot {path}')
        assert path.exists() and path.stat().st_size > 1000, path
        return str(path)


def start(directory, binary, scripts=None, content_root=None):
    directory = Path(directory).resolve()
    directory.mkdir(parents=True, exist_ok=True)
    if (directory / 'pid').exists():
        raise RuntimeError('Session directory already has a pid; use a new directory or stop it.')
    config = (ROOT / 'manifests/manifest_osrs239.ini').read_text()
    for section, key, value in [('cache:boot', 'dir', str(ROOT / 'cache.osrs239')),
                                ('net:boot', 'transport', 'embed')]:
        pattern = rf'(\[{re.escape(section)}\][\s\S]*?^{key}=)[^\n]*'
        config, count = re.subn(pattern, lambda m: m[1] + value, config, count=1, flags=re.M)
        assert count == 1, (section, key)
    # Keep the manifest alongside its source so all other relative paths retain meaning.
    fd, manifest = tempfile.mkstemp(prefix='.content-test-', suffix='.ini', dir=ROOT / 'manifests')
    with os.fdopen(fd, 'w') as f:
        f.write(config)
    (directory / 'manifest').write_text(manifest)
    env = os.environ.copy()
    env.update(TORIRS_CONTENT_TEST=str(directory), TORIRS_CONTENT_TEST_CHECKPOINTS='1', TORIRS_CONTENT_TEST_TICK_ONLY='1', SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               TORIRSSERVER_SAVES=str(directory / 'saves'), TORIRS_PREFS='',
               TORIRSSERVER_CONTENT=str(Path(content_root).resolve() if content_root else ROOT / 'OSRS-Content/osrs239-content'),
               TORIRS_PLUGIN_PREFS=str(directory / 'plugin_prefs.ini'),
               TORIRSSERVER_HOME='3243,3237', TORIRSSERVER_TUTORIAL_HOME='3243,3237',
               TORIRSSERVER_SCRIPTS=str(Path(scripts).resolve() if scripts else ROOT / 'OSRS-Content/osrs239-content/server/scripts/build'),
               TORIRSSERVER_CACHE=str(ROOT / 'cache.osrs239'), TORIRSSERVER_STAFF_LEVEL='2')
    with (directory / 'client.log').open('w') as log:
        process = subprocess.Popen([str(Path(binary).resolve()), '--manifest', manifest,
                                    '--user', 'canoetest', '--pass', 'test', '--uncapped'],
                                   cwd=ROOT, env=env, stdout=log, stderr=log, start_new_session=True)
    (directory / 'pid').write_text(str(process.pid))
    session = Session(directory)
    session.process = process
    started = time.monotonic()
    session.until(lambda s: s['online'] and s['world_ready'], 'login', ticks=100)
    print(json.dumps({'session': str(directory), 'pid': process.pid, 'boot_seconds': time.monotonic()-started}))


def canoes(session):
    began = time.monotonic()
    (session.directory / 'canoes.json').unlink(missing_ok=True)
    session.call('pointer -100 -100')
    session.call('close')
    session.call('cheat god 1')
    for stat in ['attack', 'strength', 'defence', 'hitpoints']:
        session.call(f'cheat setlevel {stat} 99')
    session.call('cheat canoe 2')
    session.step(780)  # expire scenery from previous runs before resetting the station
    session.call('cheat canoe 1')
    session.until(lambda s: abs(s['x']-3243) <= 6 and abs(s['z']-3237) <= 6, 'Lumbridge station')
    session.bit('canoestation_state_lumbridge', 0)
    station = 'loc 1 3241 3235 canoeing_canoestation_lumbridge'
    session.call(station)
    for _ in range(40):
        session.step()
        bit = session.call('varbit canoestation_state_lumbridge')
        if bit['client'] == bit['server'] == 10:
            break
    else:
        raise AssertionError(f'chop did not produce a fallen tree: {bit}')
    session.call(station)
    session.step(60)
    assert session.call('widget canoeing:log 0')['exists'], 'CS2 did not create Make Log'
    images = [session.shot('01-shaping')]
    session.call('button canoeing:log 0 1')
    session.step(180)
    session.bit('canoestation_state_lumbridge', 1)
    session.bit('canoe_type', 1)
    session.call(station)
    session.step(180)
    session.bit('canoestation_state_lumbridge', 11)
    session.call(station)
    session.step(120)
    assert session.call('widget canoe_map_lum:destination_2 -1')['exists'], 'travel map absent'
    images.append(session.shot('02-destinations'))
    session.call('button canoe_map_lum:destination_2 -1 1')
    seat = session.until(lambda s: (s['x'], s['z']) == (1817, 4515) and s['camera'] == 1, 'river cutscene')
    session.until(lambda st: not session.call('widget fade_overlay:fader -1')['exists'], 'river fade complete')
    seat=session.call('state')
    assert seat['animation'] == 3302 and seat['player_yaw'] == 512, seat
    images.append(session.shot('03-river-start'))
    # NPC_INFO must produce the moving backdrop in the client's actual world.
    scenery = session.call('npc canoeing_bullrush')
    assert scenery['count'] > 0, scenery
    session.step(60)
    moved = session.call('npc canoeing_bullrush')
    assert moved['count'] > 0 and (moved['x'], moved['z']) != (scenery['x'], scenery['z']), (scenery, moved)
    rowing = session.call('state')
    assert rowing['animation'] == 3302 and rowing['anim_frame'] != seat['anim_frame'], (seat, rowing)
    images.append(session.shot('04-river-motion'))
    arrival = session.until(lambda s: (s['x'], s['z']) == (3199, 3344), 'Champions Guild arrival')
    session.until(lambda st: session.call('widget messagebox:continue -1')['exists'], 'arrival dialogue')
    arrival=session.call('state')
    assert arrival['camera'] == 0 and arrival['animation'] == 65535, arrival
    assert arrival['chat_blocked'] == 0, arrival
    session.bit('canoestation_state_lumbridge', 0)
    images.append(session.shot('05-arrival'))
    session.call('resume messagebox:continue')
    session.step()
    # Cave scene has its own camera and scenery; all four boat models use the real multiloc.
    for boat in range(1, 5):
        session.call(f'cheat canoecave {boat}')
        seat = session.until(lambda s: (s['x'], s['z']) == (1845, 4492) and s['camera'] == 1, 'cave cutscene')
        session.until(lambda st: not session.call('widget fade_overlay:fader -1')['exists'], 'cave fade complete')
        seat=session.call('state')
        session.bit('canoe_type', boat)
        assert seat['animation'] == 3302 and seat['player_yaw'] == 512, seat
        images.append(session.shot(f'06-cave-{boat}'))
        arrival = session.until(lambda s: (s['x'], s['z']) == (3141, 3796), 'Wilderness arrival')
        assert arrival['camera'] == 0, arrival
        session.until(lambda st: session.call('widget messagebox:continue -1')['exists'], 'cave arrival dialogue')
        session.call('resume messagebox:continue')
        session.step(780)  # expire the previous cave's scenery before another rider
    pixels = visual_checks(session)
    report = {'ok': True, 'seconds': time.monotonic()-began, 'images': images, 'visual_mean_errors': pixels}
    (session.directory / 'canoes.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


def visual_checks(session, record=False):
    from PIL import Image, ImageDraw, ImageFilter, ImageChops, ImageStat
    fixtures = ROOT / 'tools/testdata/canoes'
    spec = json.loads((fixtures / 'regions.json').read_text())
    errors = {}
    for name, region in spec['regions'].items():
        source = session.directory / f'{name}.png'
        frame = Image.open(source).convert('RGB')
        if list(frame.size) != spec['size']:
            raise AssertionError(f'{name}: framebuffer size {frame.size}, expected {spec["size"]}')
        for mask in spec['masks'].get(name, []):
            ImageDraw.Draw(frame).rectangle(mask, fill=(0,0,0))
        crop = frame.crop(region).filter(ImageFilter.GaussianBlur(1.5)).resize((160,100))
        golden = fixtures / f'{name}.png'
        if record:
            crop.save(golden)
        else:
            expected = Image.open(golden).convert('RGB')
            diff = ImageChops.difference(expected, crop)
            score = sum(ImageStat.Stat(diff).mean)/3
            errors[name] = round(score,4)
            if score > spec['mean_error_limit']:
                diff.save(session.directory / f'{name}-diff.png')
                raise AssertionError(f'{name}: rendered pixels differ from reviewed baseline: {score:.3f} > {spec["mean_error_limit"]}; see {session.directory / (name+"-diff.png")}')
    return errors


def dependency_fingerprint(content=None):
    """Inputs loaded once by the compiler; body edits are handled separately."""
    import hashlib
    digest = hashlib.sha256()
    content = Path(content) if content else ROOT / 'OSRS-Content/osrs239-content'
    skip = {'models', 'maps', 'sprites', 'synth', 'songs', 'textures', '.git', 'build', 'build_rs2012'}
    suffixes = {'.pack', '.constant', '.dbtable', '.varp', '.varbit', '.com', '.compack', '.ini'}
    for base, dirs, files in os.walk(content):
        dirs[:] = sorted(d for d in dirs if d not in skip and not d.startswith('build'))
        for name in sorted(files):
            path = Path(base) / name
            if path.suffix in suffixes:
                digest.update(str(path.relative_to(content)).encode())
                digest.update(path.read_bytes())
    return digest.hexdigest()


def compiler_start(session, restart=False, content_root=None):
    folder = session.directory
    output = folder / 'scripts'
    output.mkdir(parents=True, exist_ok=True)
    if (folder / 'compiler.pid').exists():
        if not restart:
            raise RuntimeError('Compiler already started; use compiler --restart for configuration changes.')
        import signal
        try:
            os.kill(int((folder / 'compiler.pid').read_text()), signal.SIGTERM)
        except ProcessLookupError:
            pass  # failed cold compile: the recorded process has already exited
        (folder / 'compiler.pid').unlink()
    (folder / 'compile.response').unlink(missing_ok=True)
    content = Path(content_root).resolve() if content_root else ROOT / 'OSRS-Content/osrs239-content'
    (folder / 'compiler.content_root').write_text(str(content))
    (folder / 'compiler.dependencies').write_text(dependency_fingerprint(content))
    started = time.monotonic()
    with (folder / 'compiler.log').open('w') as log:
        process = subprocess.Popen([str(ROOT / 'src/build_opt/sscompile'),
                                    '--src', str((content / 'server/scripts').relative_to(ROOT)),
                                    '--content-root', str(content.relative_to(ROOT)),
                                    '--out', str(output), '--serve', str(folder)],
                                   cwd=ROOT, stdout=log, stderr=log, start_new_session=True)
    (folder / 'compiler.pid').write_text(str(process.pid))
    until = time.monotonic() + 300
    while not (folder / 'compile.response').exists():
        if process.poll() is not None or time.monotonic() > until:
            raise RuntimeError(f'Compiler did not become ready; see {folder / "compiler.log"}')
        time.sleep(.05)
    print(json.dumps({'compiler_pid': process.pid, 'cold_compile_seconds': time.monotonic()-started}))


def recompile(session, source):
    whole_started = time.monotonic()
    folder = session.directory
    compiler_root = (folder / 'compiler.content_root').read_text() if (folder / 'compiler.content_root').exists() else None
    if (folder / 'compiler.dependencies').read_text() != dependency_fingerprint(compiler_root):
        raise RuntimeError('Compiler configuration/symbol inputs changed. Restart the compiler for a full build.')
    source = str(Path(source).resolve().relative_to(ROOT))
    if not source.endswith('.rs2'):
        raise ValueError('Warm compile accepts one .rs2 body edit.')
    response = folder / 'compile.response'
    response.unlink(missing_ok=True)
    temp = folder / 'compile.request.tmp'
    began = time.monotonic()
    temp.write_text(source+'\n')
    temp.replace(folder / 'compile.request')
    until = time.monotonic()+60
    while not response.exists():
        if time.monotonic() > until:
            raise TimeoutError('Compiler did not respond; see compiler.log')
        time.sleep(.001)
    status = response.read_text().strip()
    if not status.startswith('OK '):
        raise AssertionError(status)
    compiled = time.monotonic()-began
    session.call(f'reload {folder / "scripts"}')
    print(json.dumps({'compile_seconds': compiled, 'compile_reload_seconds': time.monotonic()-began, 'total_seconds': time.monotonic()-whole_started}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', default='build/content-test')
    sub = parser.add_subparsers(dest='action', required=True)
    boot = sub.add_parser('start')
    boot.add_argument('--binary', default=str(ROOT / 'src/torirs_contenttest'))
    boot.add_argument('--scripts', help='compiled script pack directory')
    boot.add_argument('--content-root', help='server content/definitions root')
    sub.add_parser('canoes')
    compiler = sub.add_parser('compiler')
    compiler.add_argument('--restart', action='store_true')
    compiler.add_argument('--content-root', help='compiler content root')
    visuals = sub.add_parser('visuals')
    visuals.add_argument('--record', action='store_true')
    changed = sub.add_parser('recompile')
    changed.add_argument('source')
    command = sub.add_parser('command')
    command.add_argument('text')
    sub.add_parser('stop')
    args = parser.parse_args()
    if args.action == 'start':
        start(args.session, args.binary, args.scripts, args.content_root)
    elif args.action == 'visuals':
        print(json.dumps(visual_checks(Session(args.session), args.record), indent=2))
    elif args.action == 'compiler':
        compiler_start(Session(args.session), args.restart, args.content_root)
    elif args.action == 'recompile':
        recompile(Session(args.session), args.source)
    elif args.action == 'canoes':
        session = Session(args.session)
        try:
            canoes(session)
        except Exception as exc:
            (session.directory / 'canoes.json').write_text(json.dumps({'ok': False, 'error': str(exc)}, indent=2)+'\n')
            raise
    elif args.action == 'command':
        print(json.dumps(Session(args.session).call(args.text), indent=2))
    elif args.action == 'stop':
        directory = Path(args.session)
        import signal
        os.kill(int((directory / 'pid').read_text()), signal.SIGTERM)
        Path((directory / 'manifest').read_text()).unlink(missing_ok=True)
        (directory / 'pid').unlink()
        if (directory / 'compiler.pid').exists():
            os.kill(int((directory / 'compiler.pid').read_text()), signal.SIGTERM)
            (directory / 'compiler.pid').unlink()

if __name__ == '__main__':
    if not __debug__:
        raise SystemExit('Acceptance assertions require Python without -O.')
    main()
