#!/usr/bin/env python3
"""Capture real GPU model chains once; replay them with hardware counters only."""
import argparse
import gzip
import hashlib
import json
import re
import shlex
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PKG = "com.torirs.client"
DATA = f"/sdcard/Android/data/{PKG}/files"
EVENTS = ("cpu-cycles", "instructions", "branch-misses", "L1-dcache-load-misses")


class Adb:
    def __init__(self, serial):
        self.prefix = ["adb", "-s", serial]

    def call(self, *args, check=True, timeout=60):
        return subprocess.run(self.prefix + list(args), check=check, text=True,
                              capture_output=True, timeout=timeout)

    def shell(self, *args, **kwargs):
        return self.shell_script(shlex.join(args), **kwargs)

    def shell_script(self, script, check=True, **kwargs):
        # Android 5's shell protocol doesn't propagate the remote exit code.
        # Without this, a failed `test -f` looks successful and skips uploads.
        script += '; mc_status=$?; echo; echo "__MODEL_CHAIN_STATUS__=$mc_status"'
        result = self.call("shell", script, check=False, **kwargs)
        match = re.search(r"(?:^|\n)__MODEL_CHAIN_STATUS__=(\d+)\s*$", result.stdout)
        if match:
            result.returncode = int(match[1])
            result.stdout = result.stdout[:match.start()]
        elif not result.returncode:
            raise RuntimeError("device command did not return a completion marker")
        if check and result.returncode:
            raise subprocess.CalledProcessError(result.returncode, result.args,
                                                output=result.stdout, stderr=result.stderr)
        return result

    def put_text(self, path, content, tmp):
        local = tmp / Path(path).name
        local.write_text(content)
        self.call("push", str(local), path)


def capture(args, adb):
    sys.path.insert(0, str(REPO / "tools"))
    from launcher.profiles import Manifest
    from launcher.bench import load_suite
    suite = load_suite(Manifest.load(str(REPO / "manifests/manifest_osrs239_bench.ini")))
    scenes = {scene.name: scene for scene in suite.scenes}
    if args.scene not in scenes:
        raise ValueError("Unknown scene; available: " + ", ".join(scenes))
    scene = scenes[args.scene]
    settings = {
        "TORIRS_PLUGINS": "0", "TORIRS_PERF": "0", "TORIRS_GLES2_DUALCORE": "0",
        "TORIRS_WORLD_MAP": scene.world_map_env(), "TORIRS_WEDGE_CAM": scene.wedge_cam_env(),
        "TORIRS_MODEL_CHAIN_CAPTURE": f"{DATA}/model-chain.bin",
        "TORIRS_MODEL_CHAIN_FIRST_PASS": str(args.first_pass),
        "TORIRS_MODEL_CHAIN_PASSES": str(args.passes),
    }
    motion = scene.wedge_cam_path_env()
    if motion:
        settings["TORIRS_WEDGE_CAM_PATH"] = motion
    if args.camera:
        if not re.fullmatch(r"-?\d+(,-?\d+){4}", args.camera):
            raise ValueError("camera must be x,y,z,pitch,yaw")
        settings["TORIRS_WEDGE_CAM"] = args.camera
        settings.pop("TORIRS_WEDGE_CAM_PATH", None)
    originals = {}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="model-chain-") as directory:
        tmp = Path(directory)
        for name in ("env.txt", "extra_args.txt"):
            result = adb.shell("cat", f"{DATA}/{name}", check=False)
            originals[name] = result.stdout if result.returncode == 0 else None
        try:
            adb.shell("am", "force-stop", PKG)
            adb.put_text(f"{DATA}/env.txt", "".join(f"{k}={v}\n" for k, v in settings.items()), tmp)
            adb.put_text(f"{DATA}/extra_args.txt", "--gles2\n--offline\n", tmp)
            adb.shell("rm", "-f", f"{DATA}/model-chain.bin")
            adb.shell("run-as", PKG, "am", "start", "--user", "0", "-n", f"{PKG}/.ClientActivity",
                      "--es", f"{PKG}.MANIFEST", f"{DATA}/manifests/manifest_osrs239_bench.ini")
            deadline, pid = time.monotonic() + 180, None
            while True:
                if time.monotonic() > deadline:
                    raise RuntimeError("capture did not finish; install a capture-enabled build")
                if pid is None:
                    for line in adb.shell("ps").stdout.splitlines():
                        fields = line.split()
                        if fields and fields[-1] == PKG:
                            pid = fields[1]
                lines = adb.call("logcat", "-d", "-s", "torirs").stdout.splitlines()
                done = next((line for line in lines if pid and
                             re.search(r"\(\s*" + pid + r"\)", line) and
                             "model chain capture complete:" in line), None)
                if done:
                    print(done.strip(), flush=True)
                    break
                time.sleep(1)
            adb.call("pull", f"{DATA}/model-chain.bin", str(tmp / "capture.bin"))
            raw = (tmp / "capture.bin").read_bytes()
            if args.out.suffix == ".gz":
                with args.out.open("wb") as output, gzip.GzipFile(fileobj=output, mode="wb", mtime=0) as stream:
                    stream.write(raw)
            else:
                args.out.write_bytes(raw)
            metadata = dict(scene=args.scene, serial=args.serial, environment=settings,
                            bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())
            args.out.with_name(args.out.name + ".json").write_text(json.dumps(metadata, indent=2) + "\n")
        finally:
            adb.shell("am", "force-stop", PKG, check=False)
            for name, content in originals.items():
                if content is None:
                    adb.shell("rm", "-f", f"{DATA}/{name}", check=False)
                else:
                    adb.put_text(f"{DATA}/{name}", content, tmp)
    print(f"saved {args.out}")


def run(args, adb):
    binary = args.binary or REPO / "build/model-chain/android/model_chain_replay"
    if not binary.is_file():
        raise ValueError("Build first: make -f tools/perf/model_chain.mk -j3")
    raw = gzip.open(args.corpus, "rb").read() if args.corpus.suffix == ".gz" else args.corpus.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    remote = f"/data/local/tmp/model-chain-{digest}.bin"
    adb.shell("am", "force-stop", PKG)
    adb.call("push", str(binary), "/data/local/tmp/model_chain_replay")
    if adb.shell("test", "-f", remote, check=False).returncode:
        with tempfile.TemporaryDirectory(prefix="model-chain-") as directory:
            path = Path(directory) / "capture.bin"
            path.write_bytes(raw)
            adb.call("push", str(path), remote)
    settings = []
    for value in args.env:
        if not re.fullmatch(r"(?:TORIDRAW|TORIRS)_[A-Z0-9_]+=[a-zA-Z0-9_,.-]+", value):
            raise ValueError("benchmark overrides must be TORIDRAW_NAME=value")
        settings.append(value)
    result = dict(corpus=str(args.corpus), corpus_sha256=digest, serial=args.serial,
                  cpu=0, pinned=True, exclude_kernel=True,
                  binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                  mode=args.mode, env=settings, events={})
    for event in args.event or ["cpu-cycles"]:
        command = ["/data/local/tmp/model_chain_replay", remote, event, args.mode, str(args.repetitions)]
        # Assignment words are shell syntax, but both names and values above
        # are constrained to a small literal alphabet.
        command_text = " ".join(settings + [shlex.join(command)])
        completed = adb.shell_script(command_text, timeout=180)
        rows = []
        for line in completed.stdout.splitlines():
            if line.startswith("placement:"):
                result["placement"] = line
            if line.startswith("verified:"):
                result["workload"] = line
            if line.startswith("pmu,"):
                parts = line.split(",")
                _, observed, mode, sample, calls, count, per_call = parts[:7]
                if observed != event or mode != args.mode:
                    raise RuntimeError("unexpected PMU sample")
                rows.append(dict(sample=int(sample), calls=int(calls),
                                 count=int(count), per_call=float(per_call)))
                if len(parts) == 8:
                    rows[-1]["arm"] = int(parts[7].removeprefix("arm="))
        if event != "verify" and len(rows) != (12 if args.mode in ("acquire", "publish", "chain-ab") else 9):
            raise RuntimeError("missing hardware-counter samples; no timing fallback\n" + completed.stdout[-2000:])
        values = [row["per_call"] for row in rows]
        if values:
            median = statistics.median(values)
            mad = statistics.median(abs(value - median) for value in values)
            result["events"][event] = dict(median=median, mad=mad, samples=rows)
            if args.mode in ("acquire", "publish", "chain-ab"):
                arms = {str(arm): statistics.median(row["per_call"] for row in rows if row["arm"] == arm) for arm in (0, 1)}
                result["events"][event]["arms"] = arms
                print(f"{event}: {args.mode} A={arms['0']:.3f} B={arms['1']:.3f}; {((arms['1']/arms['0']-1)*100) if arms['0'] else float('nan'):+.2f}%", flush=True)
            else:
                print(f"{event}: {median:.3f} per {args.mode} call; MAD {mad:.3f}", flush=True)
        else:
            print(result.get("workload", completed.stdout).strip(), flush=True)
        if args.out:
            args.out.parent.mkdir(parents=True, exist_ok=True)
            args.out.write_text(json.dumps(result, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", default="T062809L3Z")
    commands = parser.add_subparsers(dest="command", required=True)
    cap = commands.add_parser("capture", help="requires an installed capture-enabled APK")
    cap.add_argument("--scene", default="lumbridge")
    cap.add_argument("--camera")
    cap.add_argument("--first-pass", type=int, default=120)
    cap.add_argument("--passes", type=int, default=4)
    cap.add_argument("--out", required=True, type=Path)
    replay = commands.add_parser("run")
    replay.add_argument("corpus", type=Path)
    replay.add_argument("--binary", type=Path)
    replay.add_argument("--event", action="append", choices=EVENTS + ("verify",))
    replay.add_argument("--mode", choices=("chain", "sort", "acquire", "publish", "chain-ab"), default="chain")
    replay.add_argument("--repetitions", type=int, default=0,
                        help="whole-corpus passes per sample; 0 targets at least 30k calls")
    replay.add_argument("--env", action="append", default=[])
    replay.add_argument("--out", type=Path)
    args = parser.parse_args()
    if args.command == "capture":
        if not 1 <= args.passes <= 300 or args.first_pass < 1:
            parser.error("passes must be 1..300 and first-pass positive")
        capture(args, Adb(args.serial))
    else:
        if not 0 <= args.repetitions <= 10000:
            parser.error("repetitions must be 0..10000")
        run(args, Adb(args.serial))


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as exc:
        print((exc.stdout or "") + (exc.stderr or ""), file=sys.stderr)
        sys.exit(exc.returncode if exc.returncode > 0 else 1)
    except (ValueError, RuntimeError, OSError, subprocess.TimeoutExpired) as exc:
        print(str(exc), file=sys.stderr)
        sys.exit(1)
