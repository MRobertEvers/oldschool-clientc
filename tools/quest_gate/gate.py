#!/usr/bin/env python3
"""Turn a quest run's artefacts into a verdict.

Red on, and only on, things that mean the test did not actually happen:

  * any row in ledger.tsv whose verdict is FAIL (a BLOCKED row is its own
    bucket -- see below, not a silent pass and not folded into FAIL),
  * a missing ledger, or one whose summary row disagrees with its rows,
  * a listed quest with no ledger at all -- run.py always creates
    build/quest_gate/<quest>/ before it launches a client, so this also
    catches a process that never got that far,
  * a PASS row whose detail is empty (EMPTY DETAIL below): t.exec already
    grades a bare `ok` FAIL `hollow`; t.expect/t.check/t.step did not,
  * a step that claims a shot (its `shots` column) that is not on disk,
  * a shot that is on disk but undersized (a capture that raced the
    renderer -- less than 1000 bytes),
  * two shots with the same MD5, anywhere in one quest's shots/ directory --
    the failure that looks most like a pass: a screenshot per interaction,
    all of them identical, is a driver that never drove anything (the driver
    no longer WRITES a second copy of a frame it has already photographed --
    see UNCHANGED FRAMES below -- so this rule can no longer fire on a quest
    whose verbs simply did not move the screen, and what is left of it is the
    case it was written for),
  * a shot whose own top-left 64x64 corner matches the Character-Creator or
    pre-login fingerprint (tools/quest_gate/fingerprints/) -- a run that
    never actually got past boot/login can still write a ledger full of
    PASS rows if every verb it calls answers `refused`/`timeout` in a way
    the quest's own asserts do not catch; this catches it at the pixel
    level instead of trusting the ledger to have noticed,
  * (a run that would otherwise be green) its Quest Helper guide coverage:
    tools/quest_gate/helper_coverage.py must read FULL, or every step it
    cannot grade DRIVEN must be a CONTENT_GAP that the file itself declares
    (a t.blocked()/content_bug line, or a `-- GUIDE-GAP: <step> <reason>`
    marker whose reason cites the .rs2 line). A step that is not a gap -- a
    sibling test's branch, a two-player partner cheat, a plugin sync step, an
    obsolete step, one done another way -- grades EQUIVALENT (neutral) under a
    BRANCH-IN/PARTNER/NOT-A-STEP/OBSOLETE/ANY-OF marker whose evidence
    checks out; one that does not check out is RED. A CHEAT or UNMATCHED guide step
    is RED with the step named -- the guide is the spec, and before this a
    test that ::goto'd past a gated door or ::gave an item the guide has you
    gather passed every gate (docs/QUEST_HELPER_COVERAGE_2026-09-23.md).
    `--no-coverage` skips it, for the conformance/cheats harness files, which
    are not quests. A file with no QUEUE row or no guide (hans) is not graded;
  * (once a quest's source uses the scaffold that makes these meaningful --
    see MINIMUM SHAPE below) too few step rows or PNGs for how the run ended
    (the rule table below), no quest.* row when the source calls
    quest.bind, no quest.varp_complete row and no trailing BLOCKED row, or a
    non-setup row with no shot at all.

BLOCKED is counted separately from PASS/FAIL (phase 1: torirs_plugin_drive.c
writes `SUMMARY ... pass=N fail=M blocked=K`, the `blocked=K` token present
only when K>0). A quest whose ledger has fail==0 and at least one BLOCKED
row is reported "blocked", not "green" and not "RED": it is a known,
declared stub (`t.blocked(...)`, or `quest.expect_complete`'s own
`unsupported` row for a verb that has not landed yet), not a regression.
`gate.py` still exits non-zero for it BY DEFAULT -- a stub is not nothing,
CI should see it -- unless `--allow-blocked` is given, in which case a
blocked-but-not-failing quest exits 0.

UNCHANGED FRAMES. A screenshot request whose frame is byte-identical to the
last shot the run actually wrote is answered, not written: the file is
deleted again, the row's `shots` column stays EMPTY and its detail gains
`[frame unchanged]` (core.lua / torirs_plugin_drive_ui.c, 2026-09-19). That
is why the "every auto-shooting row carries a shot" rule below exempts a row
carrying that marker -- `t.exec` shoots after every verb, including the many
(`npc.await_present` on an npc already there, `inv.count`, a walk to the tile
the player is on) that change nothing on screen, and Sheep Herder was
rejected for four byte-identical consecutive PNGs its author could do
nothing about. The exemption is narrow on purpose: the marker is written by
the driver, from the bytes of a picture it really did take and really did
compare, so a row that carries it is a row whose capture SUCCEEDED -- the
rule's own target, a capture that silently failed, still has an empty shots
column and no marker, and still fails. A `<name>-FAIL` capture is never
suppressed, so a FAIL row always keeps its picture.

MINIMUM SHAPE. The rules about row/PNG counts, `quest.*` rows,
`quest.varp_complete`/BLOCKED, and "every non-setup row has a shot" describe
the NEW scaffold (`t.quest.bind{...}`, `t.exec(...)`) from
docs/QUEST_SUITE_KIT.md phase 2, which auto-shoots every row it writes
(core.lua's own `record_with_shot`) -- by construction, a well-formed
t.exec/t.check row can never trip the shot rule, so a shotless non-setup row
is a genuine capture failure, not a false positive waiting to happen. A
quest file that does not yet call `t.quest.bind`/`t.exec` (the two real
quests, cooks_assistant/hans, as of this pass -- hand-written against the
OLDER t.step/t.expect idiom, which never auto-shoots) has nothing these
rules describe, and enforcing them against that older idiom would flag
manually-written rows that were never wrong -- so each rule is gated on the
quest's OWN source file already using the verb it is checking, read once
per quest from test/quests/<quest>.lua (not from the ledger, which carries
no marker for which verb wrote a row).

The row/PNG minimum is smaller for a run that ends BLOCKED than for one
that ends green, because an honest `t.blocked()` reached three or four
steps into a quest cannot manufacture a fifth without padding (trap 15),
and the ledger's own trailing verdict already says the run stopped on
purpose rather than ran out of rows. Rule table (also in
docs/QUEST_AUTHORING.md section 7 -- re-read that file if this drifts):

    ledger's last row      | min step rows | min PNGs
    ----------------------- | -------------: | -------:
    BLOCKED                 |             4 |        2
    anything else (green)   |             8 |        4

  and, independent of which row of the table applies (unconditional on how
  the run ended, conditional only on the source calling `quest.bind` at
  all): at least one `quest.*` row.

Reads build/quest_gate/<quest>/ for every quest `run.py` would run (see
tools/quest_gate/quest_list.py) unless told to check only specific ones.

Usage:
  tools/quest_gate/gate.py [--all] [--allow-blocked] [--no-coverage]
  tools/quest_gate/gate.py <quest> [<quest> ...] [--allow-blocked] [--no-coverage]
  tools/quest_gate/gate.py --cutscene-as <test_id> <artefact dir> [...]   (cutscene rule only)
"""

# This directory holds queue.py (the QUEUE.tsv tool). Python puts a script's
# own directory FIRST on sys.path, so from here `import queue` -- which the
# standard library's concurrent.futures does -- found ours, and run.py
# --jobs N died on ThreadPoolExecutor ("module 'queue' has no attribute
# 'SimpleQueue'", 2026-09-20; the full-suite re-run never ran a quest). The
# scrub below must run before ANY other import; the directory goes back on
# the END of the path so quest_list/build_support/ledger still resolve.
import os as _os
import sys as _sys
_HERE = _os.path.dirname(_os.path.abspath(__file__))
_sys.path[:] = [_p for _p in _sys.path if _os.path.abspath(_p or ".") != _HERE]
_sys.path.append(_HERE)

import argparse
import hashlib
import json
import os
import re
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))

import ledger  # noqa: E402
import quest_list  # noqa: E402

MIN_SHOT_BYTES = 1000
FINGERPRINTS_DIR = os.path.join(HERE, "fingerprints")
FINGERPRINT_NAMES = ("character_creator", "pre_login")
# Measured separation (fingerprints/README.md): an ordinary in-world corner
# is the engine's flat clear colour, tens of levels away from either
# reference in every channel. A software-rendered headless frame does not
# introduce that much run-to-run noise, so this threshold has real headroom
# on both sides rather than being tuned to just barely pass today's shots.
FINGERPRINT_MATCH_THRESHOLD = 12.0
# See the rule table in the MINIMUM SHAPE banner section above: a run whose
# ledger ends on a BLOCKED row is held to the smaller pair, everything else
# (a green run) to the larger pair.
MIN_ROWS_GREEN = 8
MIN_ROWS_BLOCKED = 4
MIN_SHOTS_GREEN = 4
MIN_SHOTS_BLOCKED = 2
# core.lua's flush() folds this into the detail of a row whose capture was
# suppressed as an unchanged frame (see UNCHANGED FRAMES in the banner).
UNCHANGED_MARKER = "[frame unchanged]"


FROM_LEG_REFUSAL = "a checkpoint run is for authoring; grade the full run"
FROM_LEG_RUN_RE = re.compile(r"^.+\.leg\d+$")


def artefact_dir(name):
    assert name
    return os.path.join(REPO_ROOT, "build", "quest_gate", name)


def parse_summary_counts(summary):
    """("pass=<n> fail=<m>[ blocked=<k>]" -> (n, m, k_or_None), or
    (None, None, None) if it cannot be read -- treated as its own
    disagreement, never as a pass by default. `blocked` is only present in
    the tuple when the SUMMARY row itself carried a `blocked=` token
    (torirs_plugin_drive.c only writes one when K>0)."""
    if summary is None or len(summary) < 6:
        return None, None, None
    text = summary[5]
    counts = {}
    for token in text.split():
        key, _, value = token.partition("=")
        if key in ("pass", "fail", "blocked"):
            try:
                counts[key] = int(value)
            except ValueError:
                return None, None, None
    return counts.get("pass"), counts.get("fail"), counts.get("blocked")


def md5_of(path):
    digest = hashlib.md5()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


# --------------------------------------------------------------- PNG corner
#
# Enough of the PNG format to read the top-left NxN pixels of the RGB,
# 8-bit, non-interlaced files this project's own screenshot writer produces
# (app_plugin_assets.c: tdefl_write_image_to_png_file_in_memory_ex(...,
# num_channels=3, ...)) -- stdlib + zlib only, no Pillow, matching
# fingerprints/capture_fingerprints.py's OWN copy of this reader (kept in
# sync by hand; verified byte-identical against an independent decode --
# sips -- of the same capture, 2026-09-19).

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
            raise ValueError("unknown PNG filter type %d" % ftype)
        recon[i] = v & 0xFF
    return bytes(recon)


def read_png_corner(path, size=64):
    """{"width","height","channels","block_w","block_h","rows"} for the
    top-left size x size pixels of `path` -- None on anything this reader
    does not understand (an unsupported colour type/bit depth/interlace),
    which check_quest treats as "cannot compare", not as a match or a
    finding on its own; the ordinary shot-integrity checks already cover a
    file that is not a real PNG at all."""
    assert path
    try:
        with open(path, "rb") as handle:
            data = handle.read()
    except OSError:
        return None
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    width = height = bitdepth = colortype = None
    idat = bytearray()
    for ctype, cdata in _png_chunks(data):
        if ctype == b"IHDR":
            if len(cdata) < 13:
                return None
            width, height, bitdepth, colortype, compression, filt, interlace = \
                struct.unpack(">IIBBBBB", cdata[:13])
            if compression != 0 or filt != 0 or interlace != 0:
                return None
        elif ctype == b"IDAT":
            idat += cdata
    if not width or not height:
        return None
    channels = {0: 1, 2: 3, 4: 2, 6: 4}.get(colortype)
    if not channels or bitdepth != 8:
        return None
    try:
        raw = zlib.decompress(bytes(idat))
    except zlib.error:
        return None
    stride = 1 + width * channels
    rows_needed = min(size, height)
    needed_bytes = min(size, width) * channels
    if len(raw) < rows_needed * stride:
        return None
    prev = None
    rows = []
    for r in range(rows_needed):
        start = r * stride
        row = _unfilter_row(raw[start], raw[start + 1:start + 1 + needed_bytes], prev, channels)
        rows.append(row)
        prev = row
    return {"width": width, "height": height, "channels": channels,
            "block_w": min(size, width), "block_h": rows_needed, "rows": rows}


_FINGERPRINT_CACHE = {}


def load_fingerprint(name):
    """The stored reference block, decoded from its rows_hex once and
    cached -- gate.py may check dozens of shots across a suite run."""
    if name in _FINGERPRINT_CACHE:
        return _FINGERPRINT_CACHE[name]
    path = os.path.join(FINGERPRINTS_DIR, "%s.json" % name)
    with open(path, "r", encoding="utf-8") as handle:
        payload = json.load(handle)
    payload["rows"] = [bytes.fromhex(row) for row in payload["rows_hex"]]
    _FINGERPRINT_CACHE[name] = payload
    return payload


def fingerprint_distance(corner, fingerprint):
    """Mean absolute byte difference over the region both blocks actually
    have in common -- a window/shot size that differs slightly from the
    fingerprint's own capture still compares meaningfully."""
    rows_n = min(corner["block_h"], fingerprint["block_h"])
    if rows_n == 0:
        return None
    total = 0
    count = 0
    for r in range(rows_n):
        a = corner["rows"][r]
        b = fingerprint["rows"][r]
        n = min(len(a), len(b))
        if n == 0:
            continue
        total += sum(abs(a[i] - b[i]) for i in range(n))
        count += n
    if count == 0:
        return None
    return total / count


def matches_boot_fingerprint(shot_path):
    """The name of the fingerprint `shot_path` matches within
    FINGERPRINT_MATCH_THRESHOLD, or None. Silently None (not a finding on
    its own) when the shot cannot be decoded at all -- the shot-integrity
    checks in check_quest already flag a missing/undersized/corrupt file;
    this function's job is only the pixel comparison."""
    corner = read_png_corner(shot_path)
    if corner is None:
        return None
    for name in FINGERPRINT_NAMES:
        fingerprint = load_fingerprint(name)
        distance = fingerprint_distance(corner, fingerprint)
        if distance is not None and distance <= FINGERPRINT_MATCH_THRESHOLD:
            return name
    return None


# ---------------------------------------------------------- minimum shape
#
# Whether a quest's OWN SOURCE FILE uses the verb each rule is about -- see
# the module banner ("MINIMUM SHAPE") for why this is read from the source
# and not inferred from the ledger.

def _quest_source_text(name):
    path = quest_list.quest_path(REPO_ROOT, name)
    if not os.path.isfile(path):
        return ""
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        return handle.read()


def _long_bracket_level(text, index):
    """The level of the Lua long bracket opening at `text[index]` (`[[` is 0,
    `[==[` is 2), or None when nothing opens there."""
    if index >= len(text) or text[index] != "[":
        return None
    cursor = index + 1
    level = 0
    while cursor < len(text) and text[cursor] == "=":
        level += 1
        cursor += 1
    if cursor < len(text) and text[cursor] == "[":
        return level
    return None


def _long_bracket_end(text, index, level):
    """The index just past the matching `]]`/`]==]`, or len(text) when the
    bracket is never closed (a truncated file, not this function's problem)."""
    closer = "]" + "=" * level + "]"
    found = text.find(closer, index + 2 + level)
    if found == -1:
        return len(text)
    return found + len(closer)


def strip_lua_comments(source_text):
    """`source_text` with every Lua comment replaced by a space, and every
    string literal left alone.

    Why this exists: the two rules below switch on whether a quest file USES
    `quest.bind`/`t.exec`, and a raw substring search answers yes to a file
    that only NAMES the verb in a comment. That is not a hypothetical --
    test/quests/cooks_assistant.lua's own banner, explaining which idiom it
    was written in, flipped both rules on and drove the file off the idiom to
    get out from under them (2026-09-19, phase 3 review). A comment is prose;
    only code counts."""
    out = []
    index = 0
    length = len(source_text)
    while index < length:
        char = source_text[index]
        if char in ("'", '"'):
            quote = char
            out.append(char)
            index += 1
            while index < length:
                current = source_text[index]
                out.append(current)
                index += 1
                if current == "\\" and index < length:
                    out.append(source_text[index])
                    index += 1
                elif current == quote:
                    break
            continue
        if char == "[":
            level = _long_bracket_level(source_text, index)
            if level is not None:
                # A long STRING -- kept verbatim, comments only start at `--`.
                end = _long_bracket_end(source_text, index, level)
                out.append(source_text[index:end])
                index = end
                continue
        if char == "-" and source_text.startswith("--", index):
            level = _long_bracket_level(source_text, index + 2)
            if level is not None:
                index = _long_bracket_end(source_text, index + 2, level)
            else:
                while index < length and source_text[index] != "\n":
                    index += 1
            out.append(" ")
            continue
        out.append(char)
        index += 1
    return "".join(out)


# A relay (legs) file binds through its top-level `bind = {...}` field, which
# the harness hands to t.quest.bind before the first leg it runs
# (docs/quest_authoring/relay.md "Checkpoints"): the same bind, so the same
# minimum-shape rules.
def _uses_quest_bind(source_text):
    if "quest.bind" in strip_lua_comments(source_text):
        return True
    import lint_quest  # lazy: the one reader of a relay file's layout
    layout = lint_quest.legs_layout(source_text)
    return bool(layout and layout["bind"])


# The two verbs that shoot a row by themselves (core.lua's record_with_shot):
# `t.exec(name, verb, ...)` and `t.check(name, ...)`. Both are TOKEN matches:
# the root table is `t` and the verb is an ordinary Lua Name, so the pattern
# bounds BOTH ends -- `(?<![\w.])t` refuses `quest_t.exec` and a preceding
# `foo.t`, and `exec(`/`check(` must be the whole name before the call's own
# paren. The text they are matched against has already had its comments
# stripped (strip_lua_comments, above), so naming the verb in prose switches
# nothing on. Only a row NAME that is a plain string literal is collected: a
# name built by concatenation is not one this gate can attribute to a row, and
# a rule that cannot name its row does not get to fail one.
_DO_CALL_RE = re.compile(r"""(?<![\w.])t\s*\.\s*exec\s*\(\s*(["'])([^"']*)\1""")
_CHECK_CALL_RE = re.compile(r"""(?<![\w.])t\s*\.\s*check\s*\(\s*(["'])([^"']*)\1""")
# core.lua appends `-2`, `-3`, ... to a row name repeated within one run.
_REPEAT_SUFFIX_RE = re.compile(r"-\d+$")


def shooting_row_names(source_text):
    """Every ledger row name this source writes through an auto-shooting verb.

    PER ROW, not per file: the old rule was "this file mentions t.exec
    somewhere, so EVERY non-setup/non-quest row must carry a shot", which
    fails rows written with `t.expect` -- a verb that has never shot
    anything and was never supposed to. A row only owes a shot when the
    source actually wrote it with a verb that takes one."""
    code = strip_lua_comments(source_text)
    names = set()
    for match in _DO_CALL_RE.finditer(code):
        names.add(match.group(2))
    for match in _CHECK_CALL_RE.finditer(code):
        names.add(match.group(2))
    return names


def minimum_shape_findings(name, rows, shots_dir):
    findings = []
    source_text = _quest_source_text(name)
    checks_stage = _uses_quest_bind(source_text)
    shooting_rows = shooting_row_names(source_text)

    # The rule table (module banner, MINIMUM SHAPE): a run whose ledger ends
    # on a BLOCKED row is held to the smaller pair -- an honest t.blocked()
    # three or four steps in cannot reach the green minimum without padding.
    trailing_blocked = bool(rows) and rows[-1]["verdict"] == "BLOCKED"
    min_rows = MIN_ROWS_BLOCKED if trailing_blocked else MIN_ROWS_GREEN
    min_shots = MIN_SHOTS_BLOCKED if trailing_blocked else MIN_SHOTS_GREEN

    if len(rows) < min_rows:
        findings.append("only %d step row(s), need >= %d (minimum shape, %s run)" % (
            len(rows), min_rows, "BLOCKED" if trailing_blocked else "green"))

    if checks_stage:
        if not any(row["step"].startswith("quest.") for row in rows):
            findings.append("uses quest.bind but has no quest.* row (e.g. "
                             "quest.expect_stage/quest.varp_complete) -- minimum shape "
                             "requires one")
        has_varp_complete = any(row["step"] == "quest.varp_complete" and row["verdict"] == "PASS"
                                 for row in rows)
        if not (has_varp_complete or trailing_blocked):
            findings.append("uses quest.bind but has no passing quest.varp_complete row, "
                             "and the last row is not BLOCKED (a tier-4 stub must end on "
                             "a BLOCKED row)")
        # The completion modal must be in every green run's evidence (owner's
        # rule, 2026-09-20): quest.expect_complete photographs the reward
        # scroll into its quest.scroll_title row, so a green run whose
        # quest.scroll_title row passed without a shot means the picture was
        # lost, and a green run without that row never completed on screen.
        if not trailing_blocked:
            scroll_rows = [row for row in rows if row["step"] == "quest.scroll_title"]
            if not scroll_rows:
                findings.append("green run has no quest.scroll_title row -- the quest "
                                 "completion scroll was never read (quest.expect_complete)")
            elif not any(row["verdict"] == "PASS" and (row["shots"] or
                                                       "[scroll already photographed:" in row["detail"])
                         for row in scroll_rows):
                findings.append("quest.scroll_title row carries no screenshot and does not "
                                 "name an earlier shot of the scroll -- the completion scroll "
                                 "must be photographed in every green run")

    png_count = 0
    if os.path.isdir(shots_dir):
        png_count = sum(1 for entry in os.listdir(shots_dir) if entry.endswith(".png"))
    if png_count < min_shots:
        findings.append("only %d PNG(s) under shots/, need >= %d (%s run)" % (
            png_count, min_shots, "BLOCKED" if trailing_blocked else "green"))

    for row in rows:
        step = row["step"]
        if step not in shooting_rows and _REPEAT_SUFFIX_RE.sub("", step) not in shooting_rows:
            continue
        if row["shots"]:
            continue
        # The capture happened and was identical to the previous one, so the
        # driver kept the picture it already had (UNCHANGED FRAMES, above).
        # That is an empty shots column with a reason attached, and the reason
        # is written by the driver rather than by the quest file.
        if UNCHANGED_MARKER in row["detail"]:
            continue
        findings.append("step %r is written with t.exec/t.check, which always "
                         "shoots, but has no shot recorded and no %s in its "
                         "detail -- an empty shots column there means the "
                         "capture itself failed" % (step, UNCHANGED_MARKER))

    return findings


# EMPTY DETAIL (seam27). A PASS row whose detail is empty proves nothing to
# whoever reads the ledger later: t.exec already grades a verb's bare `ok`
# FAIL `hollow` (core.lua), but t.expect / t.check / t.step write whatever
# they are handed, so `t.expect("x.present", t.npc.await_present(...))` or
# `t.check("x", t.await{...})` wrote PASS with nothing behind it, and the
# sampler sent two files back for it before any gate did. The unchanged-frame
# marker is the driver's note about the SHOT, not about the step, so a row
# that carries only that marker is still empty.
EMPTY_DETAIL_FINDING = (
    "step %r PASSed with an empty detail -- a row that proves nothing; pass the "
    "verb's own detail through (`t.expect(name, verb(...))` keeps both returns, "
    "`local r = verb(...)` drops the second) or write one naming what was read "
    "(docs/QUEST_AUTHORING.md trap 12)")


def empty_detail_text(detail):
    """The part of a ledger detail that says something about the step: the
    detail with the driver's `[frame unchanged]` shot marker removed."""
    return (detail or "").replace(UNCHANGED_MARKER, "").strip()


def check_quest(name, allow_blocked):
    """(findings, blocked) for one quest -- findings is every RED-level
    problem (as plain strings); blocked is every BLOCKED row's own
    (step, detail) description, kept separate so a declared stub is never
    silently graded the same as a real defect (see the module banner)."""
    findings = []
    blocked = []
    directory = artefact_dir(name)
    ledger_path = os.path.join(directory, "ledger.tsv")
    shots_dir = os.path.join(directory, "shots")
    rows, summary = ledger.read(ledger_path)

    if rows is None:
        findings.append("no ledger.tsv at %s" % ledger_path)
        return findings, blocked

    # A checkpoint run (run.py --from-leg K / --only-leg K) started from a
    # saved player, skipped the setup list and legs 1..K-1: it proves the
    # legs it ran from a state it did not reach itself. It is for authoring
    # (docs/quest_authoring/relay.md "Checkpoints"), never a verdict.
    if FROM_LEG_RUN_RE.match(name) or (summary is not None and len(summary) > 5 and any(
            token.startswith("from_leg=") for token in summary[5].split())):
        findings.append(FROM_LEG_REFUSAL)
        return findings, blocked

    if not rows:
        findings.append("ledger.tsv has no step rows")

    for row in rows:
        if row["verdict"] == "BLOCKED":
            blocked.append("%s: %s" % (row["step"], row["detail"]))
        elif row["verdict"] != "PASS":
            findings.append("step %r: verdict %s -- %s" % (
                row["step"], row["verdict"], row["detail"]))
        elif not empty_detail_text(row["detail"]):
            findings.append(EMPTY_DETAIL_FINDING % row["step"])

    if summary is None:
        findings.append("ledger.tsv has no trailing SUMMARY row")
    else:
        counted_pass = sum(1 for row in rows if row["verdict"] == "PASS")
        counted_fail = sum(1 for row in rows if row["verdict"] == "FAIL")
        counted_blocked = sum(1 for row in rows if row["verdict"] == "BLOCKED")
        summary_pass, summary_fail, summary_blocked = parse_summary_counts(summary)
        if summary_pass is None or summary_fail is None:
            findings.append("SUMMARY row's pass/fail counts could not be read: %s"
                             % "\t".join(summary))
        else:
            if (summary_pass, summary_fail) != (counted_pass, counted_fail):
                findings.append(
                    "SUMMARY row disagrees with its own rows: SUMMARY says "
                    "pass=%d fail=%d, the rows say pass=%d fail=%d"
                    % (summary_pass, summary_fail, counted_pass, counted_fail))
            # `blocked=K` is only ever written when K>0 (phase 1) -- so a
            # missing token is only a disagreement if the rows actually
            # counted a BLOCKED one; a present token must match exactly.
            effective_summary_blocked = summary_blocked if summary_blocked is not None else 0
            if effective_summary_blocked != counted_blocked:
                findings.append(
                    "SUMMARY row disagrees on blocked count: SUMMARY says blocked=%s, "
                    "the rows say blocked=%d" % (summary_blocked, counted_blocked))
            elif summary_blocked == 0:
                findings.append("SUMMARY row carries \"blocked=0\" -- only write the "
                                 "token when K>0")
        summary_verdict = summary[2] if len(summary) > 2 else None
        expected_verdict = "PASS" if counted_fail == 0 else "FAIL"
        if summary_verdict != expected_verdict:
            findings.append(
                "SUMMARY row's own verdict is %s but its rows say %s"
                % (summary_verdict, expected_verdict))

    for row in rows:
        for shot_name in ledger.shot_names(row):
            shot_path = os.path.join(shots_dir, "%s.png" % shot_name)
            if not os.path.isfile(shot_path):
                findings.append("step %r claims shot %r, which is not on disk (%s)"
                                 % (row["step"], shot_name, shot_path))
                continue
            size = os.path.getsize(shot_path)
            if size < MIN_SHOT_BYTES:
                findings.append("step %r's shot %r is only %d byte(s) (< %d)"
                                 % (row["step"], shot_name, size, MIN_SHOT_BYTES))

    if os.path.isdir(shots_dir):
        by_digest = {}
        for entry in sorted(os.listdir(shots_dir)):
            if not entry.endswith(".png"):
                continue
            path = os.path.join(shots_dir, entry)
            by_digest.setdefault(md5_of(path), []).append(entry)
            matched = matches_boot_fingerprint(path)
            if matched:
                findings.append("shot %r matches the %s fingerprint -- this run never "
                                 "actually got past boot/login" % (entry, matched))
        for digest, names in sorted(by_digest.items()):
            if len(names) > 1:
                findings.append("%d shots share one MD5 (%s), a screenshot that never "
                                 "changed: %s" % (len(names), digest, ", ".join(names)))

    findings.extend(minimum_shape_findings(name, rows, shots_dir))

    return findings, blocked


# cutscene_row_required (seam32 cutscene_verb_and_camera_read). The guide has
# no "watch the cutscene" step, so a port could drop a camera cutscene and its
# test stayed green (Fight Arena's ogre pen). A quest whose OWN content scripts
# cam_moveto/cam_lookat (cutscene_sweep.quests_with_cutscene) must hold a PASS
# row whose detail begins `cutscene:` -- t.cutscene.await's row -- and the
# union of those rows' keyframes must cover every framing SITE (one script
# file:line, cutscene_sweep.cutscene_sites): a site whose coord is a literal
# needs a keyframe of its op on that exact tile, one whose coord is an
# expression (`coord`, `movecoord(...)`) any keyframe of its op. Graded only on
# a run that is otherwise green, like the guide coverage: a blocked run that
# stopped before the cutscene is blocked, not red twice.
CUTSCENE_ROW_PREFIX = "cutscene:"
CUTSCENE_KEYFRAME_RE = re.compile(r"#\d+ t=-?\d+ (moveto|lookat) (-?\d+),(-?\d+)")
_CUTSCENE_SITES = None


def cutscene_sites_by_quest():
    """test_id -> sites, read once per gate run (lazy: the sweep walks every
    quest's scripts)."""
    global _CUTSCENE_SITES
    if _CUTSCENE_SITES is None:
        import cutscene_sweep  # lazy: only a would-be-green run is graded
        _CUTSCENE_SITES = cutscene_sweep.cutscene_sites(REPO_ROOT)
    return _CUTSCENE_SITES


def cutscene_keyframes(rows):
    """[(op, x, z)] from every PASS `cutscene:` row, and those rows' names."""
    keyframes = []
    names = []
    for row in rows:
        detail = row["detail"] or ""
        if row["verdict"] != "PASS" or not detail.startswith(CUTSCENE_ROW_PREFIX):
            continue
        names.append(row["step"])
        # Only the keyframe list: the text after ";;" is the verb's notes.
        listing = detail.split(";;", 1)[0]
        for m in CUTSCENE_KEYFRAME_RE.finditer(listing):
            keyframes.append((m.group(1), int(m.group(2)), int(m.group(3))))
    return keyframes, names


def site_text(site):
    tile = site["tile"]
    where = " -> %d,%d" % (tile[0], tile[1]) if tile else " (an expression: any %s)" % site["op"]
    return "%s:%d cam_%s(%s)%s" % (site["file"], site["line"], site["op"], site["arg"], where)


# cutscene_site_on_an_optional_route (seam34). A site the guide's route never
# reaches -- Shilo Village's table raft (quest_zombiequeen.rs2:738/739), one
# of three ways out of the caverns, none of them a guide step -- could only be
# covered by driving a route the guide does not take. t.cutscene.exempt(site,
# reason) writes a PASS row whose detail is `cutscene-exempt: <site> ;;
# <reason>` (script/plugins/quest_driver/cutscene.lua), and this gate accepts
# it ONLY when all three hold:
#   1. <site> names exactly one of the quest's camera sites (a path suffix:
#      `quest_zombiequeen.rs2:738`);
#   2. <reason> names a guide step (a Quest Helper step variable, not a
#      panel) that this ledger PASSed -- the step the test drove instead;
#   3. the site is OFF the guide's route: walking up from the site's script
#      block (proc <- ~caller, label <- @caller, queue <- queue(...), timer <-
#      settimer(...)) reaches only player-op triggers ([op*,X]/[ap*,X]) whose
#      subject X no guide step targets or carries. A site any guide step's
#      target or item reaches is ON the route and is never exempt; a site
#      whose callers cannot all be named (a mapzone, a debugproc, an
#      interface button, no caller at all) is refused too -- strict, since a
#      wrong "off route" is a dropped cutscene passing.
# A refused exemption is a finding (RED), and the site is graded as uncovered.
CUTSCENE_EXEMPT_PREFIX = "cutscene-exempt:"
CUTSCENE_EXEMPT_STEP_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
_RS2_BLOCKS = None
_CALLER_PATTERNS = {
    "proc": r"~%s\b",
    "label": r"@%s\b",
    "queue": r"\b(?:weak|strong|long)?queue\(\s*%s\b",
    "weakqueue": r"\b(?:weak|strong|long)?queue\(\s*%s\b",
    "strongqueue": r"\b(?:weak|strong|long)?queue\(\s*%s\b",
    "longqueue": r"\b(?:weak|strong|long)?queue\(\s*%s\b",
    "timer": r"\b\w*timer\(\s*%s\b",
    "softtimer": r"\b\w*timer\(\s*%s\b",
}
PLAYER_OP_TRIGGER_RE = re.compile(r"^(op|ap)(loc|npc|obj|held)")


def rs2_blocks():
    """[(content-relative path, [(start line, kind, subject)], [lines])] for
    every .rs2 under the content scripts, comments stripped like the sweep
    (read once per gate run)."""
    global _RS2_BLOCKS
    if _RS2_BLOCKS is not None:
        return _RS2_BLOCKS
    import helper_coverage as hc
    header = re.compile(r"^\[(\w+),([\w:]+)\]")
    out = []
    for directory, dirnames, filenames in os.walk(hc.CONTENT_ROOT):
        dirnames[:] = [d for d in dirnames if d != "selftest"]
        for filename in sorted(filenames):
            if not filename.endswith(".rs2"):
                continue
            path = os.path.join(directory, filename)
            with open(path, "r", encoding="utf-8", errors="replace") as handle:
                lines = [line.split("//", 1)[0] for line in handle.read().split("\n")]
            blocks = []
            for number, line in enumerate(lines, 1):
                match = header.match(line)
                if match:
                    blocks.append((number, match.group(1), match.group(2)))
            out.append((os.path.relpath(path, hc.CONTENT_ROOT), blocks, lines))
    _RS2_BLOCKS = out
    return out


def enclosing_block(rel, line):
    """(kind, subject) of the script block `rel`:`line` sits in, or None."""
    for path, blocks, _lines in rs2_blocks():
        if path != rel:
            continue
        found = None
        for start, kind, subject in blocks:
            if start <= line:
                found = (kind, subject)
        return found
    return None


def block_callers(kind, subject):
    """[(kind, subject)] of every block whose body calls this proc/label/
    queue/timer; None when `kind` is not a callable block."""
    pattern = _CALLER_PATTERNS.get(kind)
    if pattern is None:
        return None
    call = re.compile(pattern % re.escape(subject))
    callers = []
    for _path, blocks, lines in rs2_blocks():
        for index, (start, block_kind, block_subject) in enumerate(blocks):
            end = blocks[index + 1][0] - 1 if index + 1 < len(blocks) else len(lines)
            if (block_kind, block_subject) == (kind, subject):
                continue
            if any(call.search(lines[n - 1]) for n in range(start + 1, end + 1)):
                if (block_kind, block_subject) not in callers:
                    callers.append((block_kind, block_subject))
    return callers


def site_entry_triggers(site_rel, line):
    """({(op trigger kind, subject)} the site is reached from, [unresolved
    block names]) -- the walk the module note's rule 3 describes."""
    start = enclosing_block(site_rel, line)
    if start is None:
        return set(), ["%s:%d is in no script block" % (site_rel, line)]
    entries, unresolved = set(), []
    seen = {start}
    frontier = [start]
    while frontier:
        kind, subject = frontier.pop()
        if PLAYER_OP_TRIGGER_RE.match(kind):
            entries.add((kind, subject))
            continue
        callers = block_callers(kind, subject)
        if callers is None:
            unresolved.append("[%s,%s] (no player op names it)" % (kind, subject))
            continue
        if not callers:
            unresolved.append("[%s,%s] (no caller found)" % (kind, subject))
            continue
        for caller in callers:
            if caller not in seen:
                seen.add(caller)
                frontier.append(caller)
    return entries, unresolved


def guide_route(name):
    """(grader guide, {symbol: [guide step names]}) -- every symbol a
    non-panel guide step targets or carries as an item, widened over
    multinpc/multiloc families and categories; None when no guide."""
    import helper_coverage as hc
    if not hc.has_guide(name):
        return None
    grader = hc.Grader(name, test_text="")
    grader.grade()
    guide = grader.guide
    _triggers, categories, _ = hc.content_index()
    route = {}

    def add(symbol, step_name):
        names = set(hc.family(symbol))
        frontier = [symbol]
        while frontier:
            for child in hc.CHILDREN.get(frontier.pop(), ()):
                if child not in names:
                    names.add(child)
                    frontier.append(child)
        for each in names:
            route.setdefault(each, []).append(step_name)
            if categories.get(each):
                route.setdefault("_" + categories[each], []).append(step_name)

    for step_name, step in guide.steps.items():
        if step.is_composite():
            continue
        for _kind, symbol in step.targets:
            add(symbol, step_name)
        for var in step.req_vars:
            for symbol in guide.items.get(var, {}).get("ids", []):
                add(symbol, step_name)
    return guide, route


def cutscene_exemptions(name, rows, sites):
    """({site index: exempt row name} accepted, [refusal findings], [notes])."""
    exempt_rows = [row for row in rows if row["verdict"] == "PASS"
                   and (row["detail"] or "").startswith(CUTSCENE_EXEMPT_PREFIX)]
    if not exempt_rows:
        return {}, [], []
    route_info = guide_route(name)
    passed = [row["step"] for row in rows if row["verdict"] == "PASS"]
    accepted, refusals, notes = {}, [], []
    for row in exempt_rows:
        body = row["detail"][len(CUTSCENE_EXEMPT_PREFIX):]
        ref, _, reason = body.partition(";;")
        ref, reason = ref.strip(), reason.strip()
        where = "cutscene_exempt_refused: %s: row %s exempts %r" % (name, row["step"], ref)
        ref_path, _, ref_line = ref.rpartition(":")
        matches = [index for index, site in enumerate(sites)
                   if ref_line.isdigit() and site["line"] == int(ref_line) and ref_path
                   and (site["file"] == ref_path or site["file"].endswith("/" + ref_path))]
        if len(matches) != 1:
            refusals.append("%s, which names %s of this quest's camera sites (sites: %s)"
                            % (where, "none" if not matches else "%d" % len(matches),
                               "; ".join(site_text(s) for s in sites)))
            continue
        site = sites[matches[0]]
        if route_info is None:
            refusals.append("%s: %s has no Quest Helper guide, so no step can be named "
                            "and no route told apart" % (where, name))
            continue
        guide, route = route_info
        guide_steps = {step_name for step_name, step in guide.steps.items() if not step.is_composite()}
        named = [token for token in CUTSCENE_EXEMPT_STEP_RE.findall(reason) if token in guide_steps]
        driven = [step for step in named
                  if any(p == step or p.startswith(step + "-") or p.startswith(step + ".")
                         for p in passed)]
        if not driven:
            refusals.append("%s: the reason %r names %s -- it must name the guide step the test "
                            "drove instead, one this ledger PASSed (guide steps: %s)"
                            % (where, reason, ("guide step(s) %s with no PASS row" % ", ".join(named))
                               if named else "no guide step", ", ".join(sorted(guide_steps))))
            continue
        content_prefix = "OSRS-Content/osrs239-content/server/scripts/"
        site_rel = site["file"][len(content_prefix):] if site["file"].startswith(content_prefix) else site["file"]
        entries, unresolved = site_entry_triggers(site_rel, site["line"])
        on_route = ["[%s,%s] <- guide step %s" % (kind, subject, "/".join(sorted(set(route[subject]))))
                    for kind, subject in sorted(entries) if subject in route]
        if on_route:
            refusals.append("%s: %s is ON the guide's route (%s) -- a site a guide step reaches "
                            "is covered by t.cutscene.await, never exempt"
                            % (where, site_text(site), "; ".join(on_route)))
            continue
        if unresolved or not entries:
            refusals.append("%s: cannot show %s is off the guide's route: the walk up from it "
                            "reaches %s -- strict: only a site entered from player-op triggers "
                            "no guide step names may be exempt"
                            % (where, site_text(site), "; ".join(unresolved) or "no trigger"))
            continue
        accepted[matches[0]] = row["step"]
        notes.append("cutscene: %s exempt by row %s (entered from %s, no guide step's; drove %s instead)"
                     % (site_text(site), row["step"],
                        ", ".join("[%s,%s]" % e for e in sorted(entries)), ", ".join(driven)))
    return accepted, refusals, notes


def cutscene_findings(name, rows, notes=None):
    """cutscene_row_required: [] for a quest whose content frames no camera,
    or whose PASS cutscene rows cover every site not exempted (above); else
    one finding per gap and one per refused exemption. `notes`, when a list,
    collects the accepted exemptions for the report."""
    sites = cutscene_sites_by_quest().get(name)
    if not sites:
        return []
    keyframes, row_names = cutscene_keyframes(rows)
    accepted, findings, accepted_notes = cutscene_exemptions(name, rows, sites)
    if notes is not None:
        notes.extend(accepted_notes)
    uncovered = []
    for index, site in enumerate(sites):
        if index in accepted:
            continue
        tile = site["tile"]
        covered = any(op == site["op"] and (tile is None or (x, z) == (tile[0], tile[1]))
                      for op, x, z in keyframes)
        if not covered:
            uncovered.append(site)
    if uncovered and not row_names:
        findings.append("cutscene_row_required: %s's content scripts %d camera site(s) and the ledger "
                        "has no PASS row whose detail begins `cutscene:` (t.cutscene.await, "
                        "docs/quest_authoring/verbs-cutscene.md) -- sites: %s"
                        % (name, len(sites), "; ".join(site_text(s) for s in uncovered)))
        return findings
    for site in uncovered:
        findings.append("cutscene_row_required: %s: no keyframe in its cutscene row(s) (%s) "
                        "covers %s" % (name, ", ".join(row_names), site_text(site)))
    return findings


def coverage_findings(name):
    """The guide-coverage findings for a run that is otherwise green; [] when
    it reads FULL or declares every content gap, or when the file has no guide
    to be graded against."""
    import helper_coverage  # lazy: it imports this module for the Lua comment stripper
    if not helper_coverage.has_guide(name):
        print("    (coverage: %s has no QUEUE row or Quest Helper guide -- not graded)" % name)
        return []
    return helper_coverage.gate_findings(name)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("quests", nargs="*", help="check only these quests (default: --all)")
    parser.add_argument("--all", action="store_true", help="check every discovered quest")
    parser.add_argument("--allow-blocked", action="store_true",
                        help="accept a quest whose ledger has BLOCKED row(s) and no FAIL "
                             "row (fail==0) -- without this flag such a quest still exits "
                             "non-zero, reported \"blocked\" rather than \"RED\"")
    parser.add_argument("--no-coverage", action="store_true",
                        help="skip the Quest Helper guide coverage check (helper_coverage.py) -- "
                             "for the conformance/cheats harness files, which are not quests")
    parser.add_argument("--cutscene-as", metavar="TEST_ID", default=None,
                        help="grade ONLY cutscene_row_required (and its exemptions) of the named "
                             "artefact directories -- scratch runs, run.py --script --name -- "
                             "against TEST_ID's camera sites and guide; no shape, no coverage")
    arguments = parser.parse_args()

    if arguments.cutscene_as:
        if not arguments.quests:
            parser.error("--cutscene-as grades named artefact directories: give at least one")
        bad = 0
        for directory_name in arguments.quests:
            rows, _summary = ledger.read(os.path.join(artefact_dir(directory_name), "ledger.tsv"))
            notes = []
            findings = cutscene_findings(arguments.cutscene_as, rows or [], notes)
            print("%-24s %s (cutscene rule as %s)" % (directory_name, "RED" if findings else "green",
                                                     arguments.cutscene_as))
            for note in notes:
                print("    (%s)" % note)
            for finding in findings:
                print("    - %s" % finding)
            bad += len(findings)
        return 1 if bad else 0

    if arguments.quests:
        names = arguments.quests
    else:
        names = quest_list.discover(REPO_ROOT)
        if not names:
            # An empty suite is a discovery FAILURE, not an empty pass: it is
            # indistinguishable, from here, from test/quests/ having been
            # wiped or misconfigured, and CI must not read that as green.
            print("gate: no quest files under test/quests/ -- nothing to check", file=sys.stderr)
            return 1

    total_findings = 0
    any_unaccepted_blocked = False
    any_accepted_blocked = False
    for name in names:
        findings, blocked = check_quest(name, arguments.allow_blocked)
        if not findings and not blocked:
            # A would-be-green run whose content frames the camera must have
            # asserted it (cutscene_row_required, above).
            rows, _summary = ledger.read(os.path.join(artefact_dir(name), "ledger.tsv"))
            cutscene_notes = []
            findings = cutscene_findings(name, rows or [], cutscene_notes)
            for note in cutscene_notes:
                print("    (%s: %s)" % (name, note))
        if not findings and not blocked and not arguments.no_coverage:
            # Only a run that would be GREEN is graded against its guide: a
            # green that skips a guide step is not green.
            findings = coverage_findings(name)
        if findings:
            status = "RED"
        elif blocked:
            status = "blocked" if arguments.allow_blocked else "RED"
        else:
            status = "green"
        print("%-24s %s" % (name, status))
        for finding in findings:
            print("    - %s" % finding)
        for row in blocked:
            print("    ~ blocked: %s" % row)
        total_findings += len(findings)
        if blocked and not findings:
            if arguments.allow_blocked:
                any_accepted_blocked = True
            else:
                any_unaccepted_blocked = True

    print("")
    if total_findings or any_unaccepted_blocked:
        if total_findings:
            print("gate: %d finding(s) across %d quest(s)" % (total_findings, len(names)),
                  file=sys.stderr)
        if any_unaccepted_blocked:
            print("gate: blocked row(s) present without --allow-blocked", file=sys.stderr)
        return 1
    if any_accepted_blocked:
        print("gate: %d quest(s) accepted (--allow-blocked covers a BLOCKED row in at "
              "least one)" % len(names))
    else:
        print("gate: %d quest(s) green" % len(names))
    return 0


if __name__ == "__main__":
    sys.exit(main())
