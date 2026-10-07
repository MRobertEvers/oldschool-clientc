#!/usr/bin/env python3
"""Exercise typed asset params through the real cachepack encoder and decoder."""
from pathlib import Path
import subprocess
import tempfile
import unittest

import config_text

ROOT = Path(__file__).resolve().parents[1]
PACKER = ROOT / "3rd/rscache/tools/cachepack/cachepack"


def full_key(kind, text):
    """Rank-0 fixture text in the full-key format the packer requires.

    Every block states every key of its type: the keys a fixture block does not
    set are appended as `key=default`. The key list is the packer's own
    (`cachepack keys`), so the fixtures stay the minimal records they read as.
    """
    listed = subprocess.run([str(PACKER), "keys", "--rev", "osrs239", "--types", kind],
                            text=True, capture_output=True, check=True).stdout
    keys = [line.split()[1] for line in listed.splitlines()
            if line.split() and line.split()[0] == kind]
    assert keys, f"cachepack keys printed nothing for {kind}"
    out = []
    stated = None
    for line in text.splitlines():
        if line.startswith("["):
            if stated is not None:
                out.extend(f"{key}=default" for key in keys
                           if not any(config_text.key_matches(key, s) for s in stated))
            stated = []
        elif stated is not None and "=" in line:
            stated.append(line.split("=", 1)[0])
        out.append(line)
    if stated is not None:
        out.extend(f"{key}=default" for key in keys
                   if not any(config_text.key_matches(key, s) for s in stated))
    return "\n".join(out) + "\n"


class ParamAssetTest(unittest.TestCase):
    def test_categories_resolve_for_items_and_npcs(self):
        with tempfile.TemporaryDirectory(prefix="cachepack-categories-") as temporary:
            base = Path(temporary)
            source = base / "source"
            for path, text in {
                "pack/category.pack": "42=tone\n",
                "server/scripts/.keep": "",
                "configs/all.obj.compack": "0=instrument\n1=tone\n",
                "configs/all.obj": full_key("obj", "[instrument]\nname=Instrument\ncategory=tone\n[tone]\ncategory=19\n"),
                "configs/all.npc.compack": "0=performer\n1=tone\n",
                "configs/all.npc": full_key("npc", "[performer]\nname=Performer\ncategory=tone\n[tone]\ncategory=19\n"),
            }.items():
                destination = source / path
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_text(text)
            packed = subprocess.run([str(PACKER), "pack", "--src", str(source), "--out", str(base / "cache"),
                "--rev", "osrs239", "--types", "obj,npc"], text=True, capture_output=True)
            self.assertEqual(packed.returncode, 0, packed.stdout + packed.stderr)
            unpacked = subprocess.run([str(PACKER), "unpack", "--cache", str(base / "cache"),
                "--src", str(base / "decoded"), "--rev", "osrs239", "--types", "obj,npc"], text=True, capture_output=True)
            self.assertEqual(unpacked.returncode, 0, unpacked.stdout + unpacked.stderr)
            for kind in ("obj", "npc"):
                text = (base / f"decoded/configs/all.{kind}").read_text()
                self.assertIn("category=42", text)
                self.assertIn("category=19", text)

    def test_synth_names_keep_their_namespace_and_wire_integer(self):
        with tempfile.TemporaryDirectory(prefix="cachepack-param-assets-") as temporary:
            base = Path(temporary)
            source = base / "source"
            for path, text in {
                "pack/obj.pack": "0=instrument\n1=tone\n",
                "configs/all.obj.compack": "0=instrument\n1=tone\n",
                "pack/param.pack": "0=sound\n1=number\n2=silent\n3=literal\n",
                "configs/all.param.compack": "0=sound\n1=number\n2=silent\n3=literal\n",
                "pack/4_soundeffects.pack": "7=tone\n",
                "configs/all.obj": full_key("obj", "[instrument]\nname=Instrument\nparam=sound,tone\nparam=number,42\nparam=silent,null\nparam=literal,19\n[tone]\nname=Item also named tone\n"),
                "configs/all.param": full_key("param", "[sound]\ntype=int\n[number]\ntype=int\n[silent]\ntype=synth\n[literal]\ntype=synth\n"),
                "server/scripts/sound.param": "[sound]\ntype=synth\n",
            }.items():
                destination = source / path
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_text(text)
            packed = subprocess.run([str(PACKER), "pack", "--src", str(source), "--out", str(base / "cache"),
                "--rev", "osrs239", "--types", "obj,param"], text=True, capture_output=True)
            self.assertEqual(packed.returncode, 0, packed.stdout + packed.stderr)
            unpacked = subprocess.run([str(PACKER), "unpack", "--cache", str(base / "cache"),
                "--src", str(base / "decoded"), "--rev", "osrs239", "--types", "obj,param"], text=True, capture_output=True)
            self.assertEqual(unpacked.returncode, 0, unpacked.stdout + unpacked.stderr)
            text = (base / "decoded/configs/all.obj").read_text()
            # `type=synth` is ScriptVarType 14, character 'P' -- the cache's own
            # (rev 239's param_1239 states typechar 80, typeid 14) -- so the
            # packed params say synth, and the unpack spells each value as a
            # sound: its name where the sound pack names it (the decoded tree
            # has none, so a number), `null` for -1.
            self.assertIn("param=param_0,7\n", text)  # sound 7, never item 1
            self.assertIn("param=param_1,42\n", text)
            self.assertIn("param=param_2,null\n", text)
            self.assertIn("param=param_3,19\n", text)
            params = config_text.param_types(base / "decoded/configs/all.param")
            self.assertEqual([params[f"param_{i}"] for i in range(4)],
                             ["synth", "int", "synth", "synth"])


if __name__ == "__main__":
    unittest.main()
