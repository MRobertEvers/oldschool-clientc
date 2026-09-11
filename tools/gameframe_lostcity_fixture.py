#!/usr/bin/env python3
"""Prepare and serve an isolated, source-built rs289 fixture for gameframe_matrix.

No cache, player store or node_modules directory is borrowed from a running
server. RSA keys are explicit local inputs and are never added to the repo.
"""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
ENGINE_COMMIT = "55e89e60209a958f0499b35f26cade1b17cdab94"
CONTENT_COMMIT = "b6e11d98c1542221286af3ceb63c635b6469ba0e"
PATCH = ROOT / "tools/testdata/gameframe/lc289-node-workers.patch"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(argv, cwd, log):
    with log.open("a") as stream:
        stream.write(f"cwd={cwd}\n{shlex.join(map(str,argv))}\n")
        stream.flush()
        subprocess.run(list(map(str,argv)), cwd=cwd, stdout=stream, stderr=subprocess.STDOUT, check=True)


def source_hashes(repository, extra=()):
    files = subprocess.check_output(["git", "ls-files", "-z"], cwd=repository).decode().split("\0")[:-1]
    return {name: digest(repository / name) for name in sorted(set(files) | set(extra))
            if (repository / name).is_file()}


def server_public_key(private, public):
    """The server decrypts with private.pem; derive its actual public half."""
    return json.loads(subprocess.check_output(["node", "--input-type=module", "-e",
        "import fs from 'node:fs'; import crypto from 'node:crypto'; "
        "const key=crypto.createPublicKey(fs.readFileSync(process.argv[1])); "
        "fs.writeFileSync(process.argv[2],key.export({type:'spki',format:'pem'})); "
        "console.log(JSON.stringify(key.export({format:'jwk'})));",
        str(private), str(public)], text=True))


def validate(out, report):
    if report["build_exit"] != 0:
        raise ValueError("fixture preparation did not pass")
    for relative, expected in report["inputs"].items():
        if not (out / relative).is_file() or digest(out / relative) != expected:
            raise ValueError(f"fixture input changed after preparation: {relative}")
    pack = out / "engine/data/pack"
    current = {str(path.relative_to(pack)): digest(path) for path in pack.rglob("*") if path.is_file()}
    if current != report["pack_files"]:
        raise ValueError("fixture pack differs from its successful build")


def prepare(args):
    out = args.out.resolve()
    if out.exists():
        raise ValueError(f"refusing to overwrite existing fixture {out}")
    out.mkdir(parents=True)
    log = out / "prepare.log"
    run(["git", "worktree", "add", "--detach", out / "engine", ENGINE_COMMIT], args.engine_source, log)
    run(["git", "worktree", "add", "--detach", out / "content", CONTENT_COMMIT], args.content_source, log)
    engine = out / "engine"
    run(["git", "apply", PATCH], engine, log)
    config = {
        "easyStartup": False, "website": {"registration": False},
        "web": {"port": args.web_port, "managementPort": args.management_port, "allowedOrigin": ""},
        "engine": {"revision": 289},
        "node": {"id":91, "port":args.game_port, "members":True, "autoSubscribeMembers":True,
                 "xpRate":1, "production":False, "minimumWealthValueEvent":10,
                 "debug":True, "debugProfile":False, "clientRoutefinder":True,
                 "profile":"plugin-engine", "maxConnected":1000, "debugProcChar":"~",
                 "hopTime":45000, "rateLimitAddressLogin":30, "rateLimitDeviceLogin":5},
        "login":{"enabled":False,"host":"localhost","port":43500},
        "friend":{"enabled":False,"host":"localhost","port":45099},
        "logger":{"enabled":False,"host":"localhost","port":45001},
        "db":{"backend":"sqlite","host":"localhost","port":3306,"user":"root",
              "pass":"password","name":"lostcity","verbose":False},
        "build":{"verbose":False,"startup":False,"verify":True,"verifyFolder":True,
                 "verifyPack":True,"liveReload":False,"srcDir":"../content"},
    }
    (engine / "data/config").mkdir(parents=True, exist_ok=True)
    (engine / "data/config/world.json").write_text(json.dumps(config, indent=2) + "\n")
    private = engine / "data/config/private.pem"
    shutil.copyfile(args.keys / "private.pem", private)
    private.chmod(0o600)
    public = server_public_key(private, engine / "data/config/public.pem")
    (engine / "data/players/plugin-engine").mkdir(parents=True, exist_ok=True)
    run(["npm", "ci", "--ignore-scripts"], engine, log)
    run(["node", "--import", "tsx", "tools/pack/Build.ts"], engine, log)
    # Extract only the public modulus/exponent; the key can differ from the
    # developer server's without producing a misleading login reply 6.
    def as_hex(value):
        return base64.urlsafe_b64decode(value + "="*((-len(value))%4)).hex()
    manifest = f"""; Source-built rs289 fixture. Accounts are allocated by gameframe_matrix.
[cache:boot]
epoch=dat1
game=rs2
revision=289
quirks=none
source=ondemand
[net:boot]
rev=lc289
transport=tcp
host=localhost
port={args.game_port}
ws_host=localhost
ws_port={args.web_port}
client_version=289
rsa_exp={as_hex(public['e'])}
rsa_mod={as_hex(public['n'])}
[ui:boot]
logic=cs1
chrome=revconfig
revconfig_ui={ROOT}/revconfig/rs245_2lc/rs245_2lc_dat1_ui.ini
revconfig_cache={ROOT}/revconfig/rs289lc/rs289lc_dat1_cache.ini
[derived:cache]
pristine=yes
note=the source-built fixture server serves its own cache; see fixture-build.json
[derived:scripts]
pristine=yes
note=the fixture server built and loads its own scripts; see fixture-build.json
"""
    (out / "manifest.ini").write_text(manifest)
    report = {
        "engine_commit": ENGINE_COMMIT, "content_commit": CONTENT_COMMIT,
        "runtime_patch_sha256": digest(PATCH),
        "node_version": subprocess.check_output(["node", "--version"], text=True).strip(),
        "lock_sha256": digest(engine / "package-lock.json"),
        "public_key_sha256": digest(engine / "data/config/public.pem"),
        "world_config_sha256": digest(engine / "data/config/world.json"),
        "build_command": ["node", "--import", "tsx", "tools/pack/Build.ts"],
        "build_exit": 0,
        "pack_files": {str(path.relative_to(engine / "data/pack")): digest(path)
                       for path in sorted((engine / "data/pack").rglob("*")) if path.is_file()},
    }
    report["inputs"] = {"engine/"+name: value for name,value in source_hashes(engine,
        ("src/util/RuntimeWorker.ts", "src/util/RuntimeWorkerNode.mjs")).items()}
    report["inputs"].update({"content/"+name: value for name,value in source_hashes(out / "content").items()})
    for name in ("world.json", "public.pem", "private.pem"):
        report["inputs"]["engine/data/config/"+name] = digest(engine / "data/config" / name)
    (out / "fixture-build.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Prepared {out}; run this tool with serve --out {out}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("prepare", "serve"))
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--engine-source", type=Path)
    parser.add_argument("--content-source", type=Path)
    parser.add_argument("--keys", type=Path)
    parser.add_argument("--game-port", type=int, default=43694)
    parser.add_argument("--web-port", type=int, default=8980)
    parser.add_argument("--management-port", type=int, default=8998)
    args = parser.parse_args()
    if args.command == "prepare":
        if not all((args.engine_source,args.content_source,args.keys)):
            parser.error("prepare requires --engine-source, --content-source and --keys")
        prepare(args)
        return 0
    report = json.loads((args.out / "fixture-build.json").read_text())
    validate(args.out, report)
    return subprocess.call(["node", "--import", "tsx", "src/app.ts"], cwd=args.out / "engine")


if __name__ == "__main__":
    raise SystemExit(main())
