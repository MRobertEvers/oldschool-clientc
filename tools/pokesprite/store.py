"""The PokeSprite store: .pss sheets on disk, indexed by sqlite.

The generator's half of src/pokesprite/pokesprite_format.h and
pokesprite_store.h -- those are the contract; this writes to it.

    <root>/index.db      sheet(key PRIMARY KEY, path, kind, bytes, generator)
    <root>/sheets/<key>.pss
    <root>/missing.db    written by io_server, read here (not checked in)
"""

import io
import os
import sqlite3
import struct

import numpy as np
from PIL import Image

MAGIC = b"PSS1"
KIND_RGBA = 1
KIND_LAYER = 2
MAT_HEAD = 1
MAT_SKIN = 2
GENERATOR = "tools/pokesprite v1"


def npc_key(npc, seq):
    return "npc/%d/%d" % (npc, seq)


def layer_key(kind, ident, seq, gender="m"):
    assert kind in ("kit", "obj")
    return "layer/%s/%s/%d/%d" % (gender, kind, ident, seq)


class Store:
    def __init__(self, root):
        self.root = root
        os.makedirs(os.path.join(root, "sheets"), exist_ok=True)
        # Several generator jobs may write one store at once: wait for the
        # lock rather than fail on it.
        self.db = sqlite3.connect(os.path.join(root, "index.db"), timeout=120)
        # WAL: io_server keeps reading while a generator writes.
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.execute("CREATE TABLE IF NOT EXISTS sheet (key TEXT PRIMARY KEY, path TEXT NOT NULL,"
                        " kind TEXT NOT NULL, bytes INTEGER NOT NULL, generator TEXT NOT NULL)")

    def close(self):
        self.db.commit()
        # Fold the write-ahead log back in, so the checked-in index.db is
        # the whole index on its own.
        self.db.execute("PRAGMA wal_checkpoint(TRUNCATE)")
        self.db.close()

    def has(self, key):
        return self.db.execute("SELECT 1 FROM sheet WHERE key = ?", (key,)).fetchone() is not None

    def missing(self):
        """Keys io_server was asked for and did not have, most-requested first."""
        p = os.path.join(self.root, "missing.db")
        if not os.path.exists(p):
            return []
        db = sqlite3.connect(p)
        rows = db.execute("SELECT key, count FROM missing ORDER BY count DESC").fetchall()
        db.close()
        have = {k for (k,) in self.db.execute("SELECT key FROM sheet")}
        return [(k, n) for k, n in rows if k not in have]

    def _write(self, key, kind_name, blob):
        rel = "sheets/%s.pss" % key
        path = os.path.join(self.root, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(blob)
        self.db.execute("INSERT OR REPLACE INTO sheet (key, path, kind, bytes, generator)"
                        " VALUES (?, ?, ?, ?, ?)", (key, rel, kind_name, len(blob), GENERATOR))

    def write_rgba(self, key, rows, durations, origin, ground):
        """rows: one list of RGBA frames (PIL images or HxWx4 arrays) per facing."""
        self._write(key, "rgba", encode(KIND_RGBA, rows, durations, origin, ground, []))

    def write_empty(self, key):
        """An answer of "nothing to draw": a 1x1 transparent cell. For a subject
        with no visible faces (a minimap-icon loc, an invisible npc), so the
        client stops asking and the generator stops retrying."""
        import numpy as np
        blank = np.zeros((1, 1, 4), np.uint8)
        self._write(key, "empty", encode(KIND_RGBA, [[blank]], [20], (0, 0), (0, 0), []))

    def write_layer(self, key, rows, durations, table):
        """rows: per facing, encoded layer frames (R material, G band, B depth,
        A coverage); table: layers.py material dicts. Origin and ground are the
        humanoid cell's, the same for every layer."""
        from layers import GROUND, ORIGIN
        self._write(key, "layer", encode(KIND_LAYER, rows, durations, ORIGIN, GROUND, table))


def encode(kind, rows, durations, origin, ground, table):
    facings = len(rows)
    assert facings in (1, 4), facings
    frames = len(rows[0])
    assert all(len(r) == frames for r in rows), "every facing needs the same frame count"
    assert len(durations) >= frames
    ims = [[np.asarray(im if not isinstance(im, Image.Image) else im.convert("RGBA"), np.uint8)
            for im in r] for r in rows]
    h, w = ims[0][0].shape[:2]
    sheet = np.zeros((h * facings, w * frames, 4), np.uint8)
    for fi, r in enumerate(ims):
        for i, im in enumerate(r):
            assert im.shape == (h, w, 4), (im.shape, (h, w))
            sheet[fi * h:(fi + 1) * h, i * w:(i + 1) * w] = im
    png = io.BytesIO()
    Image.fromarray(sheet, "RGBA").save(png, "PNG", optimize=True)
    png = png.getvalue()

    out = bytearray(MAGIC)
    out += struct.pack("<BBHHHhhhh", kind, facings, frames, w, h, int(origin[0]), int(origin[1]),
                       int(ground[0]), int(ground[1]))
    out += struct.pack("<%dH" % frames, *[min(65535, int(d)) for d in durations[:frames]])
    out += struct.pack("<B", len(table))
    for m in table:
        flags = (MAT_HEAD if m.get("head") else 0) | (MAT_SKIN if m.get("skin") else 0)
        hsl = list(m["hsl16"])[:32]
        out += struct.pack("<3BBBB", *m["base_rgb"], m["steps"], flags, len(hsl))
        out += struct.pack("<%dH" % len(hsl), *hsl)
    out += struct.pack("<I", len(png)) + png
    return bytes(out)
