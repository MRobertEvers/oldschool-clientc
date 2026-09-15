#!/usr/bin/env python3
"""Did a cs1live shot reach the game, with a sidebar AND a chat pane on it?

Two questions, because they have two answers and this script used to ask only
the first.

THE SIDEBAR, from the tab strip.  Three states look different there and only
one is the shot we wanted:

    ~12 distinct colours   flat stone -- logged in, but no tabs are mounted, so
                           every inventory-shaped plugin has nothing to draw on.
    ~40-500                the tab icons. This is the one we want.
    >1000                  the LOGIN SCREEN. The title art is busy and scores
                           far higher than the tabs do, so a naive "more
                           colours = more tabs" test calls a failed login the
                           best shot in the set. It did.

THE CHAT PANE, from the run's own log.  This half is new, and the docstring it
replaces is why: it called the flat-stone state "the tutorial never got
skipped", which is a claim about the TUTORIAL made by looking at the TABS.  The
two come apart, and on this lane they come apart in the direction that hides a
defect.  `~skiptutorial` mounts the tabs with `~tutorial_set_active_tabs` and
closes interfaces with `if_close` -- and `if_close` cannot close the tutorial's
own box, because that box lives in the engine's TUT modal slot and only
`tut_close` empties it (LostCity `Player.closeModal` clears modalMain,
modalChat and modalSide and never modalTutorial; `Player.closeTutorial` is the
only thing that writes `TutOpen(-1)`).  So a first login on a fresh account
ends with a full sidebar and the "Getting started" parchment still owning the
whole chat region -- 'ok' by the tab strip, and useless for any plugin that
speaks in the chat.

That is not hypothetical.  Thirty-seven of the forty-seven cs1live captures
taken on this lane's own 2004 frame were photographed in that state, including
both notification plugins' shots and their own control, and it was found by a
person cropping one of them by hand.  (The remaining nineteen live PNGs use a
frame provider or have no run directory left, so the same measurement does not
apply to them; the ten clear ones are exactly the shots whose LostCity account
predates the sweep.)  The message log is not merely COVERED there: the chat
builtin does not draw at all while an interface is mounted in the region
(`RS_UISlots_ChatRegionIface`), so a `Porcelain_Notify` line, a `mes`, and a
plugin that never fired are one picture.

The client now states the answer in every capture, beside the BMP write:

    CHAT_REGION iface=<id> chat_com=<id> tut_com=<id> log_visible=<0|1>

so this reads it rather than guessing at pixels.  A shot whose log predates
that line is reported UNKNOWN-CHAT, which is a real state and not a pass.

    python3 live_check.py plugins/*live*.png
    python3 live_check.py --runs runs plugins/cannon-live.png

WHY A FRESH ACCOUNT AND NOT A RACE.  Whether the box is there at all is decided
before the client starts: `login.rs2` runs `@start_tutorial` only when
`%tutorial < ^tutorial_complete` AND the player is standing on tutorial island,
which is true on an account's FIRST login and false ever after, because
`~varrock` moves them off it and the save keeps the coord.  `shot.sh` derives
the account name from the SHOT name, so a shot's first-ever run is the one that
gets the box and every later run of that same name does not.  Two shots in the
same sweep therefore disagree with no race anywhere: `nxthl-live`'s account was
born at 21:07 and photographed at 23:34 with a clear pane; `cannon-live`'s was
born at 23:37, in the capture itself, and photographed covered.
`lc_prime.sh` is the supported way to warm one.
"""
import argparse
import os
import re
import sys

from PIL import Image

TAB_STRIP = (545, 172, 760, 198)

# The client's own answer, written next to the frame it describes.
CHAT_REGION = re.compile(
    r"^CHAT_REGION iface=(-?\d+) chat_com=(-?\d+) tut_com=(-?\d+) log_visible=(\d)",
    re.M,
)


def classify_sidebar(path):
    colours = len(set(Image.open(path).convert("RGB").crop(TAB_STRIP).getdata()))
    if colours > 1000:
        return "LOGIN-SCREEN", colours
    if colours < 30:
        return "NO-TABS", colours
    return "ok", colours


def classify_chat(log_path):
    """('ok'|'CHAT-COVERED'|'UNKNOWN-CHAT', detail) from the run's own log.

    A missing log is UNKNOWN and never 'ok': a check that treats "I could not
    look" as "I looked and it was fine" is how this file got its last bug.
    """
    if not log_path or not os.path.exists(log_path):
        return "UNKNOWN-CHAT", "no log.txt"
    with open(log_path, errors="ignore") as handle:
        body = handle.read()
    match = None
    for match in CHAT_REGION.finditer(body):
        pass  # the LAST one: the frame the picture was taken on.
    if match is None:
        return "UNKNOWN-CHAT", "log has no CHAT_REGION line (pre-fix binary)"
    iface, chat_com, tut_com, visible = (int(g) for g in match.groups())
    if visible:
        return "ok", "message log"
    owner = "chat dialogue" if chat_com != -1 else "tutorial box"
    return "CHAT-COVERED", "%s iface=%d owns the region" % (owner, iface)


def log_for(shot_path, runs_dir):
    stem = os.path.splitext(os.path.basename(shot_path))[0]
    return os.path.join(runs_dir, stem, "log.txt")


def main(argv):
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("shots", nargs="+")
    parser.add_argument(
        "--runs",
        default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "runs"),
        help="where runs/<shot>/log.txt lives",
    )
    args = parser.parse_args(argv)

    bad = 0
    for path in sorted(args.shots):
        sidebar, colours = classify_sidebar(path)
        chat, detail = classify_chat(log_for(path, args.runs))
        if sidebar != "ok" or chat != "ok":
            bad += 1
        state = sidebar if sidebar != "ok" else chat
        print(
            "%-28s %-14s colours=%-5d chat: %s"
            % (os.path.basename(path), state, colours, detail)
        )
    print("\n%d/%d reached the game with a sidebar and a chat pane"
          % (len(args.shots) - bad, len(args.shots)))
    if bad:
        print("A CHAT-COVERED shot cannot photograph a chat line, whatever the")
        print("plugin did. Warm the account with lc_prime.sh, or re-run the shot")
        print("a second time under the same name -- the second login has no box.")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
