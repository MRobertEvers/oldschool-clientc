#!/usr/bin/env python3
"""Regenerate tools/quest_gate/fingerprints/{character_creator,pre_login}.json
from two REAL, deliberately-broken client runs -- not written by hand, and
not meant to run often: the two references only need to change if the boot
screen or the character creator's own layout changes.

Both captures reuse run.py's own client_env/launch_client/write_manifest --
same binary, same env, same window size a quest run actually uses -- so the
64x64 corner block stored here is comparable, pixel-for-pixel, against a
real quest's own shots/*.png without any format translation at gate.py's
end (gate.py's PNG reader is the ONLY reader it needs at runtime).

1. Character Creator: a session whose saves/ directory holds no fixture at
   all for the login name used. A fresh account boots into the Character
   Creator modal instead of the world (confirmed visually, 2026-09-19: the
   captured shot IS the "Character Creator" panel, not a plain login race).
   Captured through the ordinary t.t.shot path, so it is a real PNG, exactly
   like every quest shot gate.py ever reads.

2. Pre-login: TORIRS_PRESENT_BMP dumps the SDL-presented frame once, after a
   fixed number of real render presents, independent of login/plugin state
   entirely (platform_sdl2.c) -- frame 2 here, which lands on the client's
   own "Checking for updates - 0%" loading screen, well before any server
   round trip. The process is then SIGKILLed (this is the "killed before
   login" case) -- it was never going to reach `run(t)` at all, so there is
   no t.t.shot to ask for one. That frame is a BMP (present_bmp's own
   format, not the driver's PNG writer), decoded once, here, by this
   script's own small BMP reader -- gate.py itself never reads a BMP.

Run from the repo root: python3 tools/quest_gate/fingerprints/capture_fingerprints.py
Requires a built src/torirs_questtest (QUEST_BINARY overrides it, same as run.py).
"""

import json
import os
import shutil
import signal
import struct
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
GATE_DIR = os.path.dirname(HERE)
REPO_ROOT = os.path.dirname(os.path.dirname(GATE_DIR))
sys.path.insert(0, GATE_DIR)

import run as run_mod  # noqa: E402

BLOCK_SIZE = 64
BINARY = os.environ.get("QUEST_BINARY") or os.path.join(REPO_ROOT, "src", "torirs_questtest")


def _write_boot_script(path):
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(
            "return {\n"
            "    run = function(t)\n"
            "        t.t.ticks(60)\n"
            "        t.t.shot(\"boot\")\n"
            "        t.t.finish(0)\n"
            "    end,\n"
            "}\n"
        )


def _session_dir(name):
    directory = os.path.join(REPO_ROOT, "build", "quest_gate", name)
    if os.path.isdir(directory):
        shutil.rmtree(directory)
    os.makedirs(directory)
    return directory


def capture_character_creator(manifest_path):
    """No fixture copied into saves/ at all -- a brand new account, which
    boots into the Character Creator instead of the world."""
    directory = _session_dir("_fp_creator")
    saves = os.path.join(directory, "saves")
    os.makedirs(saves, exist_ok=True)
    script_path = os.path.join(directory, "boot.lua")
    _write_boot_script(script_path)
    log_path = os.path.join(directory, "client.log")
    code, timed_out = run_mod.launch_client(
        BINARY, manifest_path, "_fp_creator", directory, saves, script_path, log_path, 60)
    shot_path = os.path.join(directory, "shots", "01-boot.png")
    assert not timed_out, "character-creator capture timed out -- see %s" % log_path
    assert code == 0, "character-creator capture exited %s -- see %s" % (code, log_path)
    assert os.path.isfile(shot_path), "no shot at %s -- see %s" % (shot_path, log_path)
    return read_png_corner(shot_path, BLOCK_SIZE)


def capture_pre_login(manifest_path):
    """A VALID fixture (login would eventually succeed) but the process is
    killed at a wall-clock deadline chosen well before it could reach
    torirsserver's own login reply -- see the module banner for why the
    frame itself comes from TORIRS_PRESENT_BMP, not a driver shot."""
    directory = _session_dir("_fp_prelogin")
    saves = os.path.join(directory, "saves")
    run_mod.write_session_fixture("fresh_lumbridge.ini", saves, "_fp_prelogin")
    script_path = os.path.join(directory, "boot.lua")
    with open(script_path, "w", encoding="utf-8") as handle:
        handle.write("return { run = function(t) t.t.ticks(200) t.t.finish(0) end }\n")
    bmp_path = os.path.join(directory, "prelogin.bmp")
    environment = run_mod.client_env(directory, saves, script_path)
    environment["TORIRS_PRESENT_BMP"] = bmp_path
    environment["TORIRS_PRESENT_BMP_FRAME"] = "2"
    command = [BINARY, "--manifest", manifest_path, "--user", "_fp_prelogin", "--pass", "test",
               "--soft3d", "--window", "765x503"]
    log_path = os.path.join(directory, "client.log")
    print("+ " + " ".join(command), flush=True)
    with open(log_path, "wb") as log:
        process = subprocess.Popen(command, env=environment, cwd=REPO_ROOT, stdout=log,
                                    stderr=subprocess.STDOUT, start_new_session=True)
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except OSError:
                pass
            process.wait()
    assert os.path.isfile(bmp_path), "no BMP at %s -- see %s" % (bmp_path, log_path)
    return read_bmp_corner(bmp_path, BLOCK_SIZE)


# --------------------------------------------------------------- PNG corner
# Kept byte-identical to gate.py's own reader (this script is the only place
# that ALSO needs a BMP reader, so it is not worth sharing a module for one
# function -- verified equal, 2026-09-19, against sips's own PNG conversion
# of the same BMP capture, pixel for pixel).

def _png_chunks(data):
    i = 8
    while i < len(data):
        length = struct.unpack(">I", data[i:i + 4])[0]
        ctype = data[i + 4:i + 8]
        cdata = data[i + 8:i + 8 + length]
        i += 12 + length
        yield ctype, cdata
        if ctype == b"IEND":
            break


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def _unfilter_row(ftype, raw, prev, bpp):
    n = len(raw)
    recon = bytearray(n)
    for i in range(n):
        x = raw[i]
        a = recon[i - bpp] if i >= bpp else 0
        b = prev[i] if prev is not None else 0
        c = prev[i - bpp] if (prev is not None and i >= bpp) else 0
        if ftype == 0:
            v = x
        elif ftype == 1:
            v = x + a
        elif ftype == 2:
            v = x + b
        elif ftype == 3:
            v = x + (a + b) // 2
        elif ftype == 4:
            v = x + _paeth(a, b, c)
        else:
            raise ValueError("%s: unknown PNG filter type %d" % (ftype,))
        recon[i] = v & 0xFF
    return bytes(recon)


def read_png_corner(path, size=BLOCK_SIZE):
    with open(path, "rb") as handle:
        data = handle.read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "%s: not a PNG" % path
    width = height = bitdepth = colortype = None
    idat = bytearray()
    for ctype, cdata in _png_chunks(data):
        if ctype == b"IHDR":
            width, height, bitdepth, colortype, compression, filt, interlace = \
                struct.unpack(">IIBBBBB", cdata)
            assert compression == 0 and filt == 0, "%s: unsupported PNG encoding" % path
            assert interlace == 0, "%s: interlaced PNG not supported" % path
        elif ctype == b"IDAT":
            idat += cdata
    assert width and height, "%s: no IHDR" % path
    channels = {0: 1, 2: 3, 4: 2, 6: 4}.get(colortype)
    assert channels, "%s: unsupported PNG color type %d" % (path, colortype)
    assert bitdepth == 8, "%s: unsupported PNG bit depth %d" % (path, bitdepth)
    raw = zlib.decompress(bytes(idat))
    stride = 1 + width * channels
    rows_needed = min(size, height)
    needed_bytes = min(size, width) * channels
    prev = None
    rows = []
    for r in range(rows_needed):
        start = r * stride
        row = _unfilter_row(raw[start], raw[start + 1:start + 1 + needed_bytes], prev, channels)
        rows.append(row)
        prev = row
    return {"width": width, "height": height, "channels": channels,
            "block_w": min(size, width), "block_h": rows_needed, "rows": rows}


def read_bmp_corner(path, size=BLOCK_SIZE):
    """Uncompressed 32bpp BGRA, bottom-up rows (the only shape
    platform_sdl2.c's present_bmp writer produces) -- reordered to RGB to
    match read_png_corner's own output shape."""
    with open(path, "rb") as handle:
        data = handle.read()
    assert data[:2] == b"BM", "%s: not a BMP" % path
    offset = struct.unpack("<I", data[10:14])[0]
    hdr_size, width, height, planes, bpp, compression = struct.unpack("<IiiHHI", data[14:14 + 20])
    assert compression == 0, "%s: compressed BMP not supported" % path
    assert bpp == 32, "%s: expected 32bpp, got %d" % (path, bpp)
    bottom_up = height > 0
    h = abs(height)
    row_stride = width * 4
    rows_needed = min(size, h)
    needed_pixels = min(size, width)
    rows = []
    for y in range(rows_needed):
        file_row = (h - 1 - y) if bottom_up else y
        start = offset + file_row * row_stride
        raw = data[start:start + needed_pixels * 4]
        rgb = bytearray()
        for i in range(0, len(raw), 4):
            b, g, r = raw[i], raw[i + 1], raw[i + 2]
            rgb += bytes((r, g, b))
        rows.append(bytes(rgb))
    return {"width": width, "height": h, "channels": 3,
            "block_w": needed_pixels, "block_h": rows_needed, "rows": rows}


def save_fingerprint(name, corner, source):
    path = os.path.join(HERE, "%s.json" % name)
    payload = {
        "name": name,
        "source": source,
        "width": corner["width"], "height": corner["height"], "channels": corner["channels"],
        "block_w": corner["block_w"], "block_h": corner["block_h"],
        "rows_hex": [row.hex() for row in corner["rows"]],
    }
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, indent=1)
        handle.write("\n")
    print("wrote %s (%dx%d block, %d bytes/row)"
          % (path, corner["block_w"], corner["block_h"], len(corner["rows"][0])))


def main():
    assert os.path.isfile(BINARY), "no binary at %s -- build src/torirs_questtest first" % BINARY
    manifest_path = run_mod.write_manifest()
    creator = capture_character_creator(manifest_path)
    save_fingerprint("character_creator", creator,
                      "build/quest_gate/_fp_creator/shots/01-boot.png, captured 2026-09-19: "
                      "a login with no fixture copied into saves/, boots into the Character "
                      "Creator modal (confirmed visually) instead of the world")
    prelogin = capture_pre_login(manifest_path)
    save_fingerprint("pre_login", prelogin,
                      "build/quest_gate/_fp_prelogin/prelogin.bmp, captured 2026-09-19: "
                      "TORIRS_PRESENT_BMP_FRAME=2 against a valid fixture, process SIGKILLed "
                      "at a 3s wall-clock deadline -- the client's own \"Checking for "
                      "updates - 0%\" loading screen, well before any login round trip")
    return 0


if __name__ == "__main__":
    sys.exit(main())
