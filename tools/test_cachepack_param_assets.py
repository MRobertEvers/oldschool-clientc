#!/usr/bin/env python3
"""Exercise typed asset params through the real cachepack encoder and decoder."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PACKER = ROOT / "3rd/rscache/tools/cachepack/cachepack"


class ParamAssetTest(unittest.TestCase):
    def test_categories_resolve_for_items_and_npcs(self):
        with tempfile.TemporaryDirectory(prefix="cachepack-categories-") as temporary:
            base = Path(temporary)
            source = base / "source"
            for path, text in {
                "pack/category.pack": "42=tone\n",
                "server/scripts/.keep": "",
                "configs/all.obj.compack": "0=instrument\n1=tone\n",
                "configs/all.obj": "[instrument]\nname=Instrument\ncategory=tone\n[tone]\ncategory=19\n",
                "configs/all.npc.compack": "0=performer\n1=tone\n",
                "configs/all.npc": "[performer]\nname=Performer\ncategory=tone\n[tone]\ncategory=19\n",
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
                "configs/all.obj": "[instrument]\nname=Instrument\nparam=sound,tone\nparam=number,42\nparam=silent,null\nparam=literal,19\n[tone]\nname=Item also named tone\n",
                "configs/all.param": "[sound]\ntype=int\n[number]\ntype=int\n[silent]\ntype=synth\n[literal]\ntype=synth\n",
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
            self.assertIn("param=param_0,int,7", text)  # sound 7, never item 1
            self.assertIn("param=param_1,int,42", text)
            self.assertIn("param=param_2,int,-1", text)
            self.assertIn("param=param_3,int,19", text)


if __name__ == "__main__":
    unittest.main()
