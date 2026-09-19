#!/usr/bin/env python3
"""
Pixel diff of two frame strips captured by TORIRS_BMP_SERIES.

Only useful because the lane is deterministic: with the client's logic tick
frame-locked (TORIRS_MAX_FRAMES) and the embedded server's tick frame-locked
too (TORIRS_EMBED_CLOCK_MS), two runs of the same command line paint the same
world, so anything that differs between two arms is the arm and nothing else.

The left 180 columns are ignored: the debug overlay prints this frame's cost in
milliseconds there, and that is the one thing two runs are not allowed to agree
on.
"""
import os
import sys

IGNORE_LEFT = 180


def load(path):
    from PIL import Image
    import numpy as np

    return np.asarray(Image.open(path).convert("RGB")).astype(int)


def main():
    if len(sys.argv) != 3:
        print("usage: diff_frames.py <dir-a> <dir-b>", file=sys.stderr)
        return 2
    import numpy as np

    left, right = sys.argv[1], sys.argv[2]
    names = sorted(f for f in os.listdir(left) if f.endswith(".bmp"))
    if not names:
        print("no frames in %s" % left, file=sys.stderr)
        return 2

    differing = 0
    for name in names:
        other = os.path.join(right, name)
        if not os.path.exists(other):
            print("%s: missing in %s" % (name, right))
            differing += 1
            continue
        mask = np.abs(load(os.path.join(left, name)) - load(other)).sum(2) > 8
        mask[:, :IGNORE_LEFT] = False
        count = int(mask.sum())
        if count:
            ys, xs = np.nonzero(mask)
            print(
                "%s: %d px differ, x %d..%d y %d..%d"
                % (name, count, xs.min(), xs.max(), ys.min(), ys.max())
            )
            differing += 1
    print("%d of %d frames differ between the two arms" % (differing, len(names)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
