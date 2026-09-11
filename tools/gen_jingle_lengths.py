#!/usr/bin/env python3
"""Generate the mock server's jingle id -> duration table.

    tools/gen_jingle_lengths.py > src/torirsserver/torirs_server_jingle_lengths.gen.h

`SS_OP_MIDI_LENGTH` and the rev-239 MIDI_JINGLE packet's `length_in_millis`
field both need a jingle's duration, and ToriRSServer has no live cache access at
script-command time (see `AMBIENTSOUND`'s soundscape id for the same
constraint, worked around the same way `torirs_server_music_regions.gen.h` works
around the region table having no cache source: compute it once, offline, and
commit the answer).

The client itself does not act on the wire value once it decodes it
(`rs_audio.c`'s `RS_Audio_Jingle`: "trusting it would cut a jingle short on a
slow load"), so a wrong length here is not audible -- only the wire-layout
tests catch it. That is precisely why this has to be a real decode and not a
placeholder: the one place that would ever notice is the one place nobody
would think to listen.

Each jingle is decoded with `3rd/rscache/tools/audioprobe --jingle <id> --out
-`, which unpacks the column-packed index-11 archive into a real Standard MIDI
File (see `3rd/rscache/src/datatypes/music_song.h`). This script then walks
every track's events in tick order, applies Set Tempo meta-events (`FF 51 03`)
as they occur, and integrates ticks-to-milliseconds under the running tempo --
the standard SMF duration algorithm, nothing cache-specific. The default tempo
before the first Set Tempo event is 500000 us/quarter (120 BPM), the same
default General MIDI assumes.

Regenerate when `cache.osrs239` changes. Requires `audioprobe` to be built:

    make -C 3rd/rscache/tools audioprobe
"""

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
AUDIOPROBE = ROOT / "3rd/rscache/tools/audioprobe/audioprobe"
CACHE = ROOT / "cache.osrs239"
OUT = ROOT / "src/torirsserver/torirs_server_jingle_lengths.gen.h"
JINGLE_COUNT = 315


def read_varint(data, pos):
    value = 0
    while True:
        byte = data[pos]
        pos += 1
        value = (value << 7) | (byte & 0x7F)
        if not byte & 0x80:
            return value, pos


def track_events(data, pos, end):
    """Yield (absolute_tick, kind, a, b) for one MTrk body."""
    tick = 0
    running_status = None
    while pos < end:
        delta, pos = read_varint(data, pos)
        tick += delta
        status = data[pos]
        if status & 0x80:
            pos += 1
            running_status = status
        else:
            status = running_status
        high = status & 0xF0
        if high in (0x80, 0x90, 0xA0, 0xB0, 0xE0):
            a, b = data[pos], data[pos + 1]
            pos += 2
            yield tick, "voice", a, b
        elif high in (0xC0, 0xD0):
            pos += 1
        elif status == 0xFF:
            meta_type = data[pos]
            pos += 1
            length, pos = read_varint(data, pos)
            payload = data[pos : pos + length]
            pos += length
            if meta_type == 0x51 and length == 3:
                tempo = (payload[0] << 16) | (payload[1] << 8) | payload[2]
                yield tick, "tempo", tempo, None
            if meta_type == 0x2F:
                yield tick, "end", None, None
                return
        elif status in (0xF0, 0xF7):
            length, pos = read_varint(data, pos)
            pos += length
        else:
            return
    yield tick, "end", None, None


def midi_duration_ms(data):
    """Standard SMF duration: merge every track's tempo events on a shared
    timeline, then integrate ticks-to-ms under the tempo in force at each
    point. Division and track count live in the last 3 header bytes -- the
    cache's own footer, per music_song.h."""
    assert data[0:4] == b"MThd"
    division = int.from_bytes(data[12:14], "big")
    ntrk = int.from_bytes(data[10:12], "big")

    pos = 14
    all_events = []
    last_tick = [0] * ntrk
    for t in range(ntrk):
        assert data[pos : pos + 4] == b"MTrk"
        length = int.from_bytes(data[pos + 4 : pos + 8], "big")
        body_start = pos + 8
        body_end = body_start + length
        for tick, kind, a, b in track_events(data, body_start, body_end):
            all_events.append((tick, kind, a, b))
            last_tick[t] = max(last_tick[t], tick)
        pos = body_end

    all_events.sort(key=lambda e: e[0])
    last_tick_overall = max(last_tick) if last_tick else 0

    tempo = 500000  # default 120 BPM, same as General MIDI
    ms = 0.0
    prev_tick = 0
    for tick, kind, a, b in all_events:
        if tick > last_tick_overall:
            break
        ms += (tick - prev_tick) * tempo / division / 1000.0
        prev_tick = tick
        if kind == "tempo":
            tempo = a
    ms += (last_tick_overall - prev_tick) * tempo / division / 1000.0
    return round(ms)


def decode_length(jingle_id):
    with tempfile.NamedTemporaryFile(suffix=".mid") as tmp:
        result = subprocess.run(
            [str(AUDIOPROBE), str(CACHE), "osrs239", "--jingle", str(jingle_id), "--out", tmp.name],
            capture_output=True,
        )
        if result.returncode != 0:
            return None
        data = Path(tmp.name).read_bytes()
    if not data.startswith(b"MThd"):
        return None
    return midi_duration_ms(data)


def main():
    if not AUDIOPROBE.exists():
        print(f"error: {AUDIOPROBE} not built -- run: make -C 3rd/rscache/tools audioprobe", file=sys.stderr)
        return 1

    lengths = []
    for jingle_id in range(JINGLE_COUNT):
        length = decode_length(jingle_id)
        lengths.append(length if length is not None else 0)

    lines = []
    lines.append("/*")
    lines.append(" * GENERATED by tools/gen_jingle_lengths.py -- do not edit.")
    lines.append(" *")
    lines.append(" * index-11 archive id -> duration in milliseconds, from a real Standard")
    lines.append(" * MIDI File decode of the archive (see the generator for the algorithm).")
    lines.append(" * `SS_OP_MIDI_LENGTH` and MIDI_JINGLE's own length field both read this.")
    lines.append(" */")
    lines.append("")
    lines.append("#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_JINGLE_LENGTHS_GEN_H")
    lines.append("#define SRC_TORIRSSERVER_TORIRS_SERVER_JINGLE_LENGTHS_GEN_H")
    lines.append("")
    lines.append(f"#define TORIRSSERVER_JINGLE_LENGTH_COUNT {JINGLE_COUNT}")
    lines.append("")
    lines.append("static const int k_ToriRSServer_JingleLengthMs[TORIRSSERVER_JINGLE_LENGTH_COUNT] = {")
    for i in range(0, len(lengths), 10):
        row = ", ".join(str(v) for v in lengths[i : i + 10])
        lines.append(f"    {row},")
    lines.append("};")
    lines.append("")
    lines.append("#endif")
    lines.append("")

    OUT.write_text("\n".join(lines))
    print(f"wrote {OUT} ({sum(1 for v in lengths if v)} / {JINGLE_COUNT} decoded)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
