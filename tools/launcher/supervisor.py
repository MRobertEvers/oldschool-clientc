"""Starting, watching, and stopping the processes a run owns.

Run state lives in `build/run/<profile>/`: one pidfile and one log per service,
plus the resolved plan. That directory is what makes `status`, `stop` and
`logs` possible at all — without it, "which servers are up?" can only be
answered by pattern-matching the process table, and stopping a run means
`pkill` on a name fragment. This tree has already learned what that costs: the
server binaries are deliberately named dev_torirsserver / alt_torirsserver so
that a `pkill -f torirsserver` aimed at one of them does not take the others
with it. A launcher that kills by pattern would reintroduce exactly that bug,
so this one only ever signals pids it started.
"""

import contextlib
import errno
import json
import os
import signal
import socket
import subprocess
import time
import urllib.error
import urllib.request


def run_dir(repo_root, profile_name):
    return os.path.join(repo_root, "build", "run", profile_name)


def pidfile_path(repo_root, profile_name, service_name):
    return os.path.join(run_dir(repo_root, profile_name), service_name + ".pid")


def logfile_path(repo_root, profile_name, service_name):
    return os.path.join(run_dir(repo_root, profile_name), service_name + ".log")


def planfile_path(repo_root, profile_name):
    return os.path.join(run_dir(repo_root, profile_name), "plan.json")


@contextlib.contextmanager
def signals_ignored():
    """Ignore Ctrl-C (and SIGTERM/SIGHUP) for the duration of a block.

    For work that must not be abandoned half-done. Ignored rather than
    deferred: a second Ctrl-C during a shutdown means "yes, stop", which is
    already what is happening, so replaying it afterwards would only produce a
    traceback over a run that is already down.
    """
    previous = {}
    for name in ("SIGINT", "SIGTERM", "SIGHUP"):
        signum = getattr(signal, name, None)
        if signum is None:
            continue
        try:
            previous[signum] = signal.signal(signum, signal.SIG_IGN)
        except (OSError, ValueError):
            pass
    try:
        yield
    finally:
        for signum, handler in previous.items():
            try:
                signal.signal(signum, handler)
            except (OSError, ValueError):
                pass


IS_WINDOWS = os.name == "nt"

# Windows has none of the POSIX process model this file is written against:
# no session to reap, no process group to signal, no SIGTERM. What it has is a
# handle you can ask whether a process is still running, and a tool that walks
# a process tree. These two functions are that, and they are kept beside their
# POSIX counterparts rather than in a platform module so the two readings of
# "is it alive" and "make it stop" stay legible as one pair.
_STILL_ACTIVE = 259
_PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
_ERROR_ACCESS_DENIED = 5


def _win_pid_alive(pid):
    """Liveness by handle, because os.kill(pid, 0) is not a probe here.

    On Windows os.kill maps to TerminateProcess, and signal 0 is not a
    no-op sentinel -- it is an exit code, and the call fails with WinError 87
    before it can even mean anything. Asking the kernel for a handle is the
    real question, and refusing to open one for lack of rights still answers
    it: the process is there.
    """
    import ctypes

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    handle = kernel32.OpenProcess(_PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not handle:
        # Alive but not ours reads the same as EPERM does on POSIX: a process
        # somebody else owns is not the one we started.
        return ctypes.get_last_error() == _ERROR_ACCESS_DENIED
    try:
        code = ctypes.c_ulong()
        if not kernel32.GetExitCodeProcess(handle, ctypes.byref(code)):
            return False
        return code.value == _STILL_ACTIVE
    finally:
        kernel32.CloseHandle(handle)


def _win_stop_tree(pid, force):
    """taskkill /T, which is this platform's spelling of killpg.

    The tree, not the pid, for the reason stop_pid gives: a server that spawned
    a child and is signalled alone leaves that child holding the port. /F is
    the second pass -- without it taskkill asks a console process to close,
    which these do honour, and asking first is what makes the grace period
    mean something.
    """
    argv = ["taskkill", "/T", "/PID", str(pid)]
    if force:
        argv.append("/F")
    try:
        subprocess.run(argv, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       check=False)
        return True
    except OSError:
        return False


def pid_alive(pid):
    """Is this pid a live process we may signal?

    Reaps first, because the services are this process's own children while a
    `run` is in the foreground: once one exits, it stays a zombie until it is
    waited for, and a zombie answers `kill(pid, 0)` exactly like a live
    process. Untreated, that made every in-run shutdown sit through the full
    grace period and then report "WOULD NOT DIE" for a process that had
    already gone — and made `_supervise_until_interrupt` blind to a service
    that crashed, since the corpse still read as "running". `./launch stop`
    never saw either, because there the pids belong to a previous process and
    the waitpid below simply fails with ECHILD.
    """
    if pid <= 0:
        return False
    if IS_WINDOWS:
        return _win_pid_alive(pid)
    try:
        if os.waitpid(pid, os.WNOHANG)[0] == pid:
            return False
    except OSError:
        pass
    try:
        os.kill(pid, 0)
    except OSError as error:
        if error.errno == errno.ESRCH:
            return False
        if error.errno == errno.EPERM:
            # Alive, owned by somebody else — which means it is NOT the process
            # we started, so callers treat it as foreign rather than ours.
            return True
        raise
    return True


def read_pid(path):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            return int(handle.read().strip())
    except (OSError, ValueError):
        return None


def port_listening(port, host="127.0.0.1", timeout=0.4):
    try:
        with socket.create_connection((host, int(port)), timeout=timeout):
            return True
    except OSError:
        return False


def http_ok(url, timeout=1.0):
    try:
        with urllib.request.urlopen(url, timeout=timeout) as response:
            return 200 <= response.status < 500
    except urllib.error.HTTPError:
        # An HTTP error still proves something is listening and speaking HTTP,
        # which is all readiness asks.
        return True
    except Exception:
        return False


def log_contains(path, needle):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            return needle in handle.read()
    except OSError:
        return False


def service_ready(service, log_path=None):
    kind, target = service.ready
    if kind is None:
        return True
    if kind == "tcp":
        return port_listening(target)
    if kind == "http":
        return http_ok(target)
    if kind == "log":
        # The service announces its own readiness. Used where a port probe
        # lies: ToriRSServer binds its listen socket early, to fail fast on a
        # port conflict, and only then spends ~10s loading scripts and content
        # — during which the port already accepts connections while the server
        # cannot serve anything, and may still refuse to run at all. Connecting
        # to that socket proves nothing; the line it prints when it is actually
        # serving proves everything.
        return log_contains(log_path, target) if log_path else False
    return True


def wait_ready(service, timeout=90.0, interval=0.25, proc=None, log_path=None):
    """Wait for a service to answer, failing fast if it dies while we wait.

    Returns (ok, reason). A process that exits during startup is the common
    case — a port already in use, a stale script pack, a cache that does not
    match the manifest — and reporting that as "never became ready" would send
    the reader to the wrong question, so the exit is named separately.
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        if proc is not None and proc.poll() is not None:
            return False, "exited during startup with code %d" % proc.returncode
        if service_ready(service, log_path=log_path):
            return True, ""
        time.sleep(interval)
    if service.ready[0] is None:
        return True, ""
    return False, "never became ready (%s %s) after %.0fs" % (
        service.ready[0], service.ready[1], timeout)


def start_service(repo_root, profile_name, service, env=None, settle=1.5):
    """Spawn one service detached, record its pid, and wait for it to answer.

    Detached (its own session) for two reasons: a Ctrl-C aimed at the launcher
    must not tear down services a `--detach` run intended to outlive it, and a
    service that forks children can then be stopped as a whole process group
    rather than leaving orphans behind.
    """
    directory = run_dir(repo_root, profile_name)
    os.makedirs(directory, exist_ok=True)

    if service.port and port_listening(service.port):
        return None, "port %s already in use" % service.port

    log_path = logfile_path(repo_root, profile_name, service.name)
    process_env = dict(os.environ)
    if env:
        process_env.update({str(k): str(v) for k, v in env.items()})

    # Spawn and record as one unit: a Ctrl-C in the gap between them would
    # leave a live process with no pidfile, which is an orphan nothing can
    # find again.
    with signals_ignored():
        with open(log_path, "wb") as log_handle:
            log_handle.write(
                ("=== %s: %s\n" % (service.name, " ".join(service.argv)))
                .encode("utf-8"))
            log_handle.flush()
            proc = subprocess.Popen(
                service.argv,
                cwd=repo_root,
                stdout=log_handle,
                stderr=subprocess.STDOUT,
                stdin=subprocess.DEVNULL,
                env=process_env,
                # A group of its own on both platforms, so stop_pid can take
                # the service and its children together. start_new_session is
                # POSIX-only and silently ignored on Windows, where the
                # equivalent is a creation flag -- and without it taskkill /T
                # has no group to walk and Ctrl-C in the foreground would
                # reach these children as well as the launcher.
                **_spawn_group_kwargs(),
            )

        with open(pidfile_path(repo_root, profile_name, service.name), "w",
                  encoding="utf-8") as handle:
            handle.write(str(proc.pid))

    ok, reason = wait_ready(service, proc=proc, log_path=log_path)
    if not ok:
        stop_pid(proc.pid)
        return None, reason

    # An open port is not a healthy service. These servers bind BEFORE they
    # finish loading, and several of them refuse to run once loaded — a stale
    # script pack, a cache that does not match the manifest — so a readiness
    # probe can pass against a process that is already on its way out. Without
    # this settle window that death is reported much later, as a service the
    # user is told came "up" and then finds dead.
    settle_deadline = time.time() + settle
    while time.time() < settle_deadline:
        if proc.poll() is not None:
            return None, "started, then exited with code %d" % proc.returncode
        time.sleep(0.1)
    return proc.pid, ""


def _spawn_group_kwargs():
    if IS_WINDOWS:
        return {"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP}
    return {"start_new_session": True}


def log_tail(repo_root, profile_name, service_name, lines=12):
    """The last few log lines, for reporting a failure where it happened."""
    path = logfile_path(repo_root, profile_name, service_name)
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            content = handle.read().splitlines()
    except OSError:
        return []
    return content[-lines:]


def stop_pid(pid, grace=4.0):
    """SIGTERM the process group, then SIGKILL what is left.

    The group, not the pid: io_server and the servers can spawn children, and
    signalling only the leader leaves those children holding the port — which
    presents on the next run as "port already in use" with nothing visible to
    blame.
    """
    if not pid or not pid_alive(pid):
        return False
    if IS_WINDOWS:
        if not _win_stop_tree(pid, force=False):
            return False
    else:
        try:
            os.killpg(os.getpgid(pid), signal.SIGTERM)
        except OSError:
            try:
                os.kill(pid, signal.SIGTERM)
            except OSError:
                return False

    deadline = time.time() + grace
    while time.time() < deadline:
        if not pid_alive(pid):
            return True
        time.sleep(0.1)

    if IS_WINDOWS:
        _win_stop_tree(pid, force=True)
    else:
        try:
            os.killpg(os.getpgid(pid), signal.SIGKILL)
        except OSError:
            try:
                os.kill(pid, signal.SIGKILL)
            except OSError:
                pass
    time.sleep(0.2)
    return not pid_alive(pid)


def write_plan(repo_root, profile_name, plan):
    directory = run_dir(repo_root, profile_name)
    os.makedirs(directory, exist_ok=True)
    with open(planfile_path(repo_root, profile_name), "w",
              encoding="utf-8") as handle:
        json.dump(plan, handle, indent=2)
        handle.write("\n")


def read_plan(repo_root, profile_name):
    try:
        with open(planfile_path(repo_root, profile_name), "r",
                  encoding="utf-8") as handle:
            return json.load(handle)
    except (OSError, ValueError):
        return None


def known_runs(repo_root):
    """Every profile with run state on disk, whether or not it is still up."""
    base = os.path.join(repo_root, "build", "run")
    if not os.path.isdir(base):
        return []
    return sorted(
        entry for entry in os.listdir(base)
        if os.path.isdir(os.path.join(base, entry)))


def run_status(repo_root, profile_name):
    """What this run's services are doing right now.

    Each service is reported as one of:
      running   pidfile pid is alive
      dead      pidfile exists, process gone (crashed or killed elsewhere)
      orphan    process gone but the port is still held by something else
      stopped   no pidfile
    """
    plan = read_plan(repo_root, profile_name)
    services = (plan or {}).get("services", [])
    rows = []
    for entry in services:
        name = entry.get("name")
        pid = read_pid(pidfile_path(repo_root, profile_name, name))
        port = entry.get("port")
        alive = pid_alive(pid) if pid else False
        listening = port_listening(port) if port else None
        if alive:
            state = "running"
        elif pid and listening:
            state = "orphan"
        elif pid:
            state = "dead"
        else:
            state = "stopped"
        rows.append({
            "name": name,
            "pid": pid,
            "port": port,
            "state": state,
            "listening": listening,
            "description": entry.get("description", ""),
        })
    return {"profile": profile_name, "plan": plan, "services": rows}


def stop_run_now(repo_root, profile_name):
    """stop_run, proof against a user leaning on Ctrl-C while it works.

    Taking a run down is the one block that must finish: `stop_pid` waits out
    a grace period per service, and a Ctrl-C landing in that wait used to
    abandon the loop with the remaining services still alive. Those orphans go
    on holding their ports, so the next `run` refuses to start with nothing on
    screen to blame. The retry covers a Ctrl-C that Python had already queued
    before the handlers were swapped out; stop_run re-reads the pidfiles it
    has not removed yet, so running it again resumes rather than repeats.
    """
    with signals_ignored():
        while True:
            try:
                return stop_run(repo_root, profile_name)
            except KeyboardInterrupt:
                continue


def stop_run(repo_root, profile_name):
    """Stop every service this run started. Returns [(name, outcome)]."""
    plan = read_plan(repo_root, profile_name)
    results = []
    services = (plan or {}).get("services", [])
    # Reverse start order: a dependency outlives what depends on it.
    for entry in reversed(services):
        name = entry.get("name")
        path = pidfile_path(repo_root, profile_name, name)
        pid = read_pid(path)
        if not pid:
            results.append((name, "not running"))
            continue
        if not pid_alive(pid):
            results.append((name, "already gone"))
        elif stop_pid(pid):
            results.append((name, "stopped (pid %d)" % pid))
        else:
            results.append((name, "WOULD NOT DIE (pid %d)" % pid))
        try:
            os.remove(path)
        except OSError:
            pass
    return results
