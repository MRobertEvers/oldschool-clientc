#!/usr/bin/env python3
"""Did a cs1live shot actually reach the game, with a sidebar on it?

Three states look different in the sidebar tab strip and only one is the shot
we wanted, which is why this is a script and not an eyeball:

    ~12 distinct colours   flat stone -- logged in, but the tutorial never got
                           skipped, so there are NO tabs and every
                           inventory-shaped plugin has nothing to draw on.
    ~40-500                the tab icons. This is the one we want.
    >1000                  the LOGIN SCREEN. The title art is busy and scores
                           far higher than the tabs do, so a naive "more
                           colours = more tabs" test calls a failed login the
                           best shot in the set. It did.
"""
import os, sys
from PIL import Image

TAB_STRIP = (545, 172, 760, 198)

def classify(path):
    colours = len(set(Image.open(path).convert('RGB').crop(TAB_STRIP).getdata()))
    if colours > 1000:
        return 'LOGIN-SCREEN', colours
    if colours < 30:
        return 'NO-TABS', colours
    return 'ok', colours

def main(paths):
    bad = 0
    for path in sorted(paths):
        state, colours = classify(path)
        if state != 'ok':
            bad += 1
        print(f"{os.path.basename(path):28} {state:14} colours={colours}")
    print(f"\n{len(paths) - bad}/{len(paths)} reached the game with a sidebar")
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
