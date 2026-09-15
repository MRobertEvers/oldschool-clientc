#!/usr/bin/env python3
"""What live_check.py must say about a run's chat pane.

The check it replaces answered "was the tutorial skipped" by counting colours
in the SIDEBAR, and so called a run 'ok' whose chat region was owned by the
tutorial box for its whole length -- which is how thirty-seven of the forty-seven
live captures taken on that lane's own 2004 frame shipped with no chat pane in
them and nothing in the harness said so.

So the three things that must hold are asserted here rather than left to the
next sweep:

  * a run whose log says the message log was visible is 'ok';
  * a run whose log says an interface owned the region is CHAT-COVERED, and it
    names WHICH interface, because the tutorial box and a dialogue are
    different problems with different fixes;
  * a run with no CHAT_REGION line, and a run with no log at all, are
    UNKNOWN-CHAT -- never 'ok'. "I could not look" and "I looked and it was
    fine" have to be different answers or this file grows its last bug back.

    python3 live_check_test.py
"""
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import live_check


def check(condition, what):
    if not condition:
        print("FAIL: %s" % what)
        sys.exit(1)
    print("PASS: %s" % what)


def log_with(body):
    handle = tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False)
    handle.write(body)
    handle.close()
    return handle.name


def main():
    # The line the client writes beside the BMP, in each of its three shapes.
    clear = log_with(
        "SIM_READY elapsed_ms=291 tree_generation=18577\n"
        "CHAT_REGION iface=-1 chat_com=-1 tut_com=-1 log_visible=1\n"
        "wrote /tmp/out.bmp\n"
    )
    tutorial = log_with(
        "CHAT_REGION iface=3559 chat_com=-1 tut_com=3559 log_visible=0\n"
    )
    dialogue = log_with(
        "CHAT_REGION iface=2459 chat_com=2459 tut_com=-1 log_visible=0\n"
    )
    silent = log_with("SIM_READY elapsed_ms=740 tree_generation=19083\n")

    state, detail = live_check.classify_chat(clear)
    check(state == "ok", "a visible message log is ok (%s)" % detail)

    state, detail = live_check.classify_chat(tutorial)
    check(state == "CHAT-COVERED", "the tutorial box is CHAT-COVERED")
    check("tutorial box" in detail, "and it is named as the tutorial box")

    state, detail = live_check.classify_chat(dialogue)
    check(state == "CHAT-COVERED", "an IF_OPENCHAT dialogue is CHAT-COVERED")
    check("chat dialogue" in detail, "and it is named as a dialogue")

    state, _ = live_check.classify_chat(silent)
    check(state == "UNKNOWN-CHAT", "a log with no CHAT_REGION line is UNKNOWN-CHAT")

    state, _ = live_check.classify_chat(os.path.join(tempfile.gettempdir(), "nope.txt"))
    check(state == "UNKNOWN-CHAT", "a missing log is UNKNOWN-CHAT, not ok")

    # The LAST line wins: a run that opens a dialogue half way through and
    # closes it again was photographed with the log on screen, and the frame
    # the picture was taken on is the one beside the BMP write.
    cycled = log_with(
        "CHAT_REGION iface=2459 chat_com=2459 tut_com=-1 log_visible=0\n"
        "CHAT_REGION iface=-1 chat_com=-1 tut_com=-1 log_visible=1\n"
    )
    state, _ = live_check.classify_chat(cycled)
    check(state == "ok", "the last CHAT_REGION line is the one that counts")

    for path in (clear, tutorial, dialogue, silent, cycled):
        os.unlink(path)
    print("\nlive_check chat classifier: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
