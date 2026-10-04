#!/usr/bin/env python3
"""pass_state -- the persisted state of a raid room pass, read by a program
instead of by an agent (the eighth launch's state reader lost a sent-back room:
its stale accepted review was replayed and the room was never re-authored).

    python3 tools/raid_gate/pass_state.py build/author_state/<pass>

Writes <dir>/state.json and prints one line per room. The JSON is exactly what
the room card's State phase returns:
  reviewed            every <id>.review.json that parses, verbatim
  authored            every <id>.author.json that parses, verbatim
  sampled             sample.json exists and says pushed
  sample_considered   its "considered" ids
  sample_sent_back    its "sent_back" ids (an entry may be an id or {"id": ...})
"""
import glob
import json
import os
import sys


def load(path):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def main():
    assert len(sys.argv) == 2, __doc__
    state_dir = sys.argv[1]
    os.makedirs(state_dir, exist_ok=True)
    reviewed = [d for d in (load(p) for p in sorted(glob.glob(os.path.join(state_dir, "*.review.json")))) if d]
    authored = [d for d in (load(p) for p in sorted(glob.glob(os.path.join(state_dir, "*.author.json")))) if d]
    sample = load(os.path.join(state_dir, "sample.json")) or {}
    sent_back = [(e.get("id") if isinstance(e, dict) else e) for e in sample.get("sent_back", [])]
    state = {
        "reviewed": reviewed,
        "authored": authored,
        "sampled": bool(sample.get("pushed")),
        "sample_considered": [str(x) for x in sample.get("considered", [])],
        "sample_sent_back": [str(x) for x in sent_back if x],
    }
    with open(os.path.join(state_dir, "state.json"), "w", encoding="utf-8") as f:
        json.dump(state, f, indent=1)
    for d in authored:
        print("authored %s %s" % (d.get("test_id"), d.get("outcome")))
    for d in reviewed:
        print("reviewed %s %s" % (d.get("test_id"), d.get("verdict")))
    print("considered %s; sent back %s" % (",".join(state["sample_considered"]) or "none",
                                          ",".join(state["sample_sent_back"]) or "none"))
    print("wrote %s" % os.path.join(state_dir, "state.json"))


if __name__ == "__main__":
    sys.exit(main())
