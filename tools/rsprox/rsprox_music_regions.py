#!/usr/bin/env python3
"""Which song Jagex plays in each map square, from RSProx transcripts.

docs/audio/music_regions.tsv came from Kronos (an OSRS rev-184 era server); a
square it has wrong plays the wrong track and unlocks the wrong one (2026-11-17:
Musa Point, square 46,49, was "Attention"; Jagex plays Sea Shanty, song 92, in
index 4378 [98] and 4420 [696]). This reads what the recordings say instead.

A sample is a tick in which `music_current_track` (varp 3883) changed and a
MIDI_SONG went out, within two ticks of the recorder ENTERING the square it
stands in at the end of that tick (a song picked in the music tab, a quest's
override, a cutscene's silence (147) all happen without one). Instances
(x >= 6400) are skipped. Per square, the songs and how many
samples (sessions) each had:

    python3 tools/rsprox/rsprox_music_regions.py <transcript|@list>... [--tsv]

--tsv prints `region<TAB>song_id<TAB>samples<TAB>sessions<TAB>first_session[tick]`
for each square's majority song.
"""
import collections
import re
import sys

TICK = re.compile(r"^\[(\d+)\]")
POS = re.compile(r"\[(?:run|walk|crawl|teleport|idle|stationary)\] player=\(index=\d+, name=[^,]*, coord=\((\d+), (\d+), (\d+)\)\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?")
TRACK = re.compile(r"\[varp_(?:small|large)\] id=(?:music_current_track \()?3883\)?, old=(-?[\d,]+), new=(-?[\d,]+)")
SONG = re.compile(r"\[midi_song_v\d\] id=(?:[^(,]*\()?(\d+)\)?,")


def scan(path, samples):
    pos = None
    tick = -1
    changed = False
    song = None
    region_now = None
    entered = -100  # the tick the recorder entered region_now


    def flush():
        if changed and song is not None and song != 147 and pos and pos[0] < 6400 and tick - entered <= 2:
            samples[region_now][song].append((path, tick))

    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            m = TICK.match(line)
            if m:
                flush()
                tick, changed, song = int(m.group(1)), False, None
                continue
            m = POS.search(line)
            if m and line.startswith("        ["):  # the player's own row, not a block inside one
                g = m.groups()
                pos = (int(g[3]), int(g[4]), int(g[5])) if g[3] else (int(g[0]), int(g[1]), int(g[2]))
                region = (pos[0] >> 6) << 8 | (pos[1] >> 6)
                if region != region_now:
                    region_now, entered = region, tick
                continue
            if TRACK.search(line):
                changed = True
                continue
            m = SONG.search(line)
            if m:
                song = int(m.group(1))
        flush()


def main():
    args = sys.argv[1:]
    tsv = "--tsv" in args
    args = [a for a in args if a != "--tsv"]
    paths = []
    for a in args:
        paths += [l.strip() for l in open(a[1:])] if a.startswith("@") else [a]
    samples = collections.defaultdict(lambda: collections.defaultdict(list))
    for p in paths:
        if p:
            scan(p, samples)
    for region in sorted(samples):
        songs = samples[region]
        ranked = sorted(songs.items(), key=lambda kv: -len({s for s, _ in kv[1]}))
        if tsv:
            song, hits = ranked[0]
            sessions = {s for s, _ in hits}
            first = hits[0]
            print("%d\t%d\t%d\t%d\t%s[%d]" % (region, song, len(hits), len(sessions),
                                              first[0].rsplit("/", 1)[-1].replace(".txt", ""), first[1]))
        else:
            print("%d (%d,%d): %s" % (region, region >> 8, region & 255, ", ".join(
                "%d x%d/%dsess" % (s, len(h), len({p for p, _ in h})) for s, h in ranked)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
