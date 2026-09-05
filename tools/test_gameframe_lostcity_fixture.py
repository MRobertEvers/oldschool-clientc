#!/usr/bin/env python3
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from gameframe_lostcity_fixture import digest, server_public_key, validate


class LostCityFixtureTest(unittest.TestCase):
    def test_public_key_comes_from_the_server_private_key(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            expected = json.loads(subprocess.check_output(["node", "--input-type=module", "-e",
                "import crypto from 'node:crypto'; import fs from 'node:fs'; "
                "const key=crypto.generateKeyPairSync('rsa',{modulusLength:1024}); "
                "fs.writeFileSync(process.argv[1],key.privateKey.export({type:'pkcs1',format:'pem'})); "
                "console.log(JSON.stringify(key.publicKey.export({format:'jwk'})));", str(root / "private.pem")], text=True))
            (root / "public.pem").write_text("wrong public file from a previous setup")
            actual = server_public_key(root / "private.pem", root / "public.pem")
            self.assertEqual((actual["e"],actual["n"]), (expected["e"],expected["n"]))
            self.assertIn("BEGIN PUBLIC KEY", (root / "public.pem").read_text())

    def test_modified_inputs_or_pack_cannot_reuse_a_success_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "engine/data/pack").mkdir(parents=True)
            source = root / "engine/input.ts"
            packed = root / "engine/data/pack/script.dat"
            source.write_bytes(b"source")
            packed.write_bytes(b"pack")
            report = {"build_exit":0, "inputs":{"engine/input.ts":digest(source)},
                      "pack_files":{"script.dat":digest(packed)}}
            validate(root,report)
            source.write_bytes(b"changed source")
            with self.assertRaisesRegex(ValueError,"input changed"):
                validate(root,report)
            source.write_bytes(b"source")
            packed.write_bytes(b"changed pack")
            with self.assertRaisesRegex(ValueError,"pack differs"):
                validate(root,report)


if __name__ == "__main__":
    unittest.main()
