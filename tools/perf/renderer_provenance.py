"""Build provenance for renderer experiments; contains no launch credentials."""
import hashlib
from pathlib import Path
import subprocess
from model_chain import REPO


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def source_state():
    def git(*args):
        return subprocess.check_output(['git', *args], cwd=REPO)
    # Include untracked source files as well as tracked edits. A commit hash
    # alone does not describe experiments in a dirty performance checkout.
    paths = ('src', '3rd', 'tools/perf', 'android')
    changed = set(git('diff', 'HEAD', '--name-only', '-z', '--', *paths).decode().split('\0'))
    changed.update(git('ls-files', '--others', '--exclude-standard', '-z', '--', *paths).decode().split('\0'))
    suffixes = {'.c', '.h', '.m', '.mm', '.inc', '.s', '.S', '.py', '.mk', '.gradle', '.java', '.xml', '.properties'}
    files = {}
    for name in sorted(changed - {''}):
        path = REPO / name
        if path.suffix in suffixes or path.name.lower() in ('makefile', 'cmakelists.txt'):
            files[name] = sha256(path) if path.is_file() else None
    return dict(commit=git('rev-parse', 'HEAD').decode().strip(), changed_source_sha256=files,
                tracked_diff_sha256=hashlib.sha256(git('diff', 'HEAD', '--', *paths)).hexdigest())


def switch_matrix(target):
    old = ('direct_order', 'acquire_cache', 'feed_batch', 'compact4', 'static_primary',
           'pose_reuse', 'actor_world_cache', 'world_fast_shader')
    result = {}
    for arm, label in ((0, 'before'), (1, 'after')):
        enabled = target.startswith('sub10') or bool(arm)
        result[label] = {key: enabled for key in old}
        result[label].update(canvas_compact=target in ('sub10-actor', 'sub10-ui', 'sub10-ui-aa', 'sub10-words', 'sub10-words-aa') or
                            (target in ('sub10-canvas', 'sub10') and bool(arm)),
                            actor_direct=target in ('sub10-ui', 'sub10-ui-aa', 'sub10-words', 'sub10-words-aa') or (target in ('sub10-actor', 'sub10') and bool(arm)),
                            overlay_retain=target in ('sub10-words', 'sub10-words-aa') or (target in ('sub10-ui', 'sub10') and bool(arm)),
                            actor_words=target in ('sub10-words', 'sub10') and bool(arm))
    return result


def device_conditions(adb):
    # Read only; preserve governor and thermal policy. Missing vendor sysfs
    # fields remain explicit rather than inventing a steady-state guarantee.
    paths = ('/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor',
             '/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq',
             '/sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq',
             '/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq',
             '/sys/class/power_supply/battery/temp',
             '/sys/class/power_supply/battery/capacity',
             '/sys/class/power_supply/battery/status')
    result = {}
    for path in paths:
        read = adb.shell('cat', path, check=False)
        result[path] = read.stdout.strip() if read.returncode == 0 else None
    return result
