"""Shared plumbing for the XP benchmark queue.

The XP box is one machine. Two timed runs overlapping produce plausible garbage,
not obvious garbage -- the box is bimodal by ~7.5% and a collision reads as
noise. So every timed run goes through a queue with exactly one drainer, and
the drainer is the only thing that launches a benchmark.

Non-timed traffic (uploads, /fs/get, script puts, log reads) is NOT serialized;
any agent may call the box API directly for those.

Host-side only. Python 3.7+. The box-side runner is generated separately and
must stay Python 3.2 compatible (see xp_runner.py.tmpl).
"""

import errno
import hashlib
import json
import os
import socket
import subprocess
import time
import urllib.error
import urllib.parse
import urllib.request

# --------------------------------------------------------------------------
# locations
# --------------------------------------------------------------------------

BOX = os.environ.get('BQ_BOX', 'http://10.10.10.2:8088')
BOX_ROOT = r'C:\dev\oldschool-clientc'
BOX_BQ = BOX_ROOT + r'\bq'

# One machine-global queue root so every agent, worktree and session agrees
# on where jobs live without being told.
QUEUE_ROOT = os.environ.get(
    'BQ_ROOT', r'C:\Users\mrobe\AppData\Local\Temp\claude\xpbq')

DIR_PENDING = os.path.join(QUEUE_ROOT, 'pending')
DIR_RUNNING = os.path.join(QUEUE_ROOT, 'running')
DIR_DONE = os.path.join(QUEUE_ROOT, 'done')
DIR_LOGS = os.path.join(QUEUE_ROOT, 'logs')
LOCK_PATH = os.path.join(QUEUE_ROOT, 'drainer.lock')
RUNLOG = os.path.join(QUEUE_ROOT, 'runlog.jsonl')
BOXSTATE = os.path.join(QUEUE_ROOT, 'boxstate.json')

# A drainer refreshes its lease every loop; a lease older than this is dead.
LEASE_SECONDS = 90
MAX_ATTEMPTS = 2

# The four handrolled kernels. TORIDRAW_ABLATE / TORIDRAW_SPAN_CENSUS /
# TORIDRAW_SPAN_TRACE in TORIDRAW_PROBE_CFLAGS trip a makefile gate that drops
# all four, and a binary missing them is not comparable to one that has them.
REQUIRED_SYMS = (
    '_toridraw_texspan_opaque_lerp8_v3_asm',
    '_toridraw_gouraud_tri_opaque_s4_asm',
    '_toridraw_textri_opaque_lerp8_v3_asm',
    '_toridraw_fb_clear32_nt_asm',
)

NM = os.environ.get(
    'BQ_NM',
    r'C:\Users\mrobe\Documents\git_repos\oldschool-clientc\toolchains\mingw32\bin\nm.exe')


def ensure_dirs():
    for d in (QUEUE_ROOT, DIR_PENDING, DIR_RUNNING, DIR_DONE, DIR_LOGS):
        os.makedirs(d, exist_ok=True)


# --------------------------------------------------------------------------
# box API (thin; every caller may use it concurrently for non-timed traffic)
# --------------------------------------------------------------------------

def _q(s):
    return urllib.parse.quote(s, safe='')


def box_get(path, timeout=120):
    """Read a file off the box. Returns None when it does not exist yet."""
    url = BOX + '/fs/get?path=' + _q(path)
    try:
        return urllib.request.urlopen(url, timeout=timeout).read()
    except urllib.error.HTTPError as e:
        if e.code == 404:
            return None
        raise


def box_list(path, timeout=60):
    """Directory listing. Returns None when the directory does not exist."""
    url = BOX + '/fs/list?path=' + _q(path)
    try:
        raw = urllib.request.urlopen(url, timeout=timeout).read()
    except urllib.error.HTTPError as e:
        if e.code == 404:
            return None
        raise
    return json.loads(raw.decode('utf-8', 'replace'))


def box_put(local_path, remote_path, timeout=1800):
    """Upload. The JSON content_b64 envelope drops the connection; the server
    wants the raw bytes as the body, with Expect: suppressed."""
    assert os.path.isfile(local_path)
    with open(local_path, 'rb') as f:
        blob = f.read()
    url = BOX + '/fs/put?path=' + _q(remote_path)
    req = urllib.request.Request(
        url, data=blob,
        headers={'Content-Type': 'application/octet-stream', 'Expect': ''})
    urllib.request.urlopen(req, timeout=timeout).read()
    return len(blob)


def box_put_script(text, name, timeout=300):
    assert name.endswith('.py')
    url = BOX + '/scripts/put?name=' + _q(name)
    req = urllib.request.Request(
        url, data=text.encode('utf-8'),
        headers={'Content-Type': 'text/plain', 'Expect': ''})
    return urllib.request.urlopen(req, timeout=timeout).read()


def box_run_script(name, timeout=60):
    """POST, not GET. A script that relaunches itself detached resets the
    connection before responding -- that is the expected path, so a transport
    error here is not a failure. Poll the output file instead."""
    url = BOX + '/scripts/run?name=' + _q(name)
    req = urllib.request.Request(url, data=b'', headers={'Expect': ''})
    try:
        return ('ok', urllib.request.urlopen(req, timeout=timeout).read()[:400])
    except Exception as e:
        return ('detached', repr(e)[:200])


# --------------------------------------------------------------------------
# hashing / provenance
# --------------------------------------------------------------------------

def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        while True:
            b = f.read(1 << 20)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def missing_asm_syms(exe_path):
    """Which of the four handrolled kernels are absent from this binary.

    Returns a list; empty means all four linked. Returns None when nm could not
    be run at all -- the caller must treat that as unverified, not as pass.
    """
    assert os.path.isfile(exe_path)
    if not os.path.isfile(NM):
        return None
    try:
        out = subprocess.run([NM, exe_path], capture_output=True, timeout=300)
    except Exception:
        return None
    if out.returncode != 0:
        return None
    text = out.stdout.decode('utf-8', 'replace')
    return [s for s in REQUIRED_SYMS if s not in text]


# --------------------------------------------------------------------------
# job / result IO
# --------------------------------------------------------------------------

def _atomic_write(path, text):
    tmp = path + '.tmp%d' % os.getpid()
    with open(tmp, 'w', encoding='utf-8') as f:
        f.write(text)
        f.flush()
        os.fsync(f.fileno())
    # Windows fails os.replace with WinError 5 when ANY other process holds the
    # destination open -- Python's open() for reading does not pass
    # FILE_SHARE_DELETE. With a dozen agents polling the lock and the job files
    # at once that collision is routine, not exceptional, and it killed a
    # completed 5-arm job (0824-231902-aa108c) by propagating out of the
    # drainer's heartbeat. Retry for ~2 s, then give up honestly.
    for attempt in range(40):
        try:
            os.replace(tmp, path)
            return
        except PermissionError:
            if attempt == 39:
                raise
            time.sleep(0.05)


def write_json(path, obj):
    _atomic_write(path, json.dumps(obj, indent=2, sort_keys=True))


def read_json(path):
    # A reader can also lose the race against a writer's replace; a transient
    # sharing violation is not "the file does not exist".
    for attempt in range(20):
        try:
            with open(path, 'r', encoding='utf-8') as f:
                return json.load(f)
        except (FileNotFoundError, ValueError):
            return None
        except PermissionError:
            if attempt == 19:
                raise
            time.sleep(0.05)
    return None


def append_runlog(record):
    ensure_dirs()
    line = json.dumps(record, sort_keys=True)
    # Append-only; O_APPEND writes under the pipe-buffer size are atomic enough
    # for a single drainer plus occasional client notes.
    with open(RUNLOG, 'a', encoding='utf-8') as f:
        f.write(line + '\n')


def result_path(job_id):
    return os.path.join(DIR_DONE, job_id + '.result.json')


def pending_path(job_id):
    return os.path.join(DIR_PENDING, job_id + '.job.json')


def running_path(job_id):
    return os.path.join(DIR_RUNNING, job_id + '.job.json')


# --------------------------------------------------------------------------
# drainer lease
# --------------------------------------------------------------------------

def read_lease():
    return read_json(LOCK_PATH)


def _pid_alive(pid):
    """Does this pid exist right now?

    Not os.kill(pid, 0): on Windows os.kill *terminates* the process instead
    of probing it, so the obvious portable idiom would kill the drainer it is
    asking about.
    """
    if os.name != 'nt':
        return True
    import ctypes
    PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
    ERROR_INVALID_PARAMETER = 87        # "no such pid"
    STILL_ACTIVE = 259
    k = ctypes.windll.kernel32
    h = k.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, int(pid))
    if not h:
        return k.GetLastError() != ERROR_INVALID_PARAMETER
    code = ctypes.c_ulong()
    ok = k.GetExitCodeProcess(h, ctypes.byref(code))
    k.CloseHandle(h)
    return (not ok) or code.value == STILL_ACTIVE


def lease_is_live(lease, now=None):
    """A fresh heartbeat is necessary but not sufficient.

    A drainer killed outright -- taskkill, a wedged box, a closed terminal --
    leaves behind a heartbeat that is seconds old and a pid that no longer
    exists. Waiting LEASE_SECONDS for that to age out parks the whole queue
    for a minute and a half over a process we can simply look up. So when the
    lease was written on this host, ask the kernel whether its drainer is
    still there; a lease from another host has only the heartbeat to go on.

    Pid reuse can make a dead lease look live for the rest of its heartbeat
    window. That errs toward waiting, never toward two drainers.
    """
    if not lease:
        return False
    now = time.time() if now is None else now
    if (now - lease.get('heartbeat', 0)) >= LEASE_SECONDS:
        return False
    if lease.get('host') == socket.gethostname() and lease.get('pid'):
        return _pid_alive(lease['pid'])
    return True


def write_lease(owner):
    write_json(LOCK_PATH, {'owner': owner, 'pid': os.getpid(),
                           'host': socket.gethostname(),
                           'heartbeat': time.time()})


def clear_lease(owner):
    lease = read_lease()
    if lease and lease.get('owner') == owner:
        try:
            os.remove(LOCK_PATH)
        except OSError as e:
            if e.errno != errno.ENOENT:
                raise
