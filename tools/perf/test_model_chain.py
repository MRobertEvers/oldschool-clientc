"""Format/verification regression checks; run after building the host harness."""
import gzip
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BINARY = ROOT / "build/model-chain/host/model_chain_replay"
CORPUS = ROOT / "benchmarks/krait_model_chains/lumbridge.chain.gz"


@unittest.skipUnless(BINARY.exists() and CORPUS.exists(), "build host harness and capture corpus first")
class ModelChainTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = gzip.open(CORPUS, "rb").read()

    def run_bytes(self, data):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "case.bin"
            path.write_bytes(data)
            return subprocess.run([str(BINARY), str(path), "verify"],
                                  capture_output=True, text=True, timeout=30)

    def test_real_chain(self):
        result = self.run_bytes(self.data)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("verified:", result.stdout)

    def test_bad_magic(self):
        result = self.run_bytes(b"\0\0\0\0" + self.data[4:])
        self.assertEqual(result.returncode, 1)
        self.assertIn("invalid chain header", result.stderr)

    def test_partial_footer(self):
        result = self.run_bytes(self.data[:-1])
        self.assertEqual(result.returncode, 1)
        self.assertIn("invalid chain header", result.stderr)

    def test_missing_footer(self):
        result = self.run_bytes(self.data[:-256])
        self.assertEqual(result.returncode, 1)
        self.assertIn("incomplete capture", result.stderr)

    def test_wrong_face_order(self):
        data = bytearray(self.data)
        offset = 0
        while offset < len(data):
            h = struct.unpack_from("<64i", data, offset)
            vertices, faces, options, count = h[4], h[5], h[9], h[20]
            arrays = 6 * vertices + 6 * faces
            arrays += (faces + 1) // 2 if options & 1 else 0
            arrays += 2 * faces if options & 2 else 0
            if count > 0 and faces > 1:
                order = offset + 256 + arrays + 12 * vertices
                old, = struct.unpack_from("<i", data, order)
                struct.pack_into("<i", data, order, (old + 1) % faces)
                break
            # Visible is 0 in the serialized ToriDraw_Cull enum.
            offset += 256 + arrays + (12 * vertices + 4 * count if h[19] == 0 else 0)
        else:
            self.fail("corpus contains no sorted model")
        result = self.run_bytes(data)
        self.assertEqual(result.returncode, 1)
        self.assertIn("face order mismatch", result.stderr)


if __name__ == "__main__":
    unittest.main()
