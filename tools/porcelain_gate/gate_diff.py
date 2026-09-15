"""Judge a plugin port: the captures it produces must say the same thing the
pre-port captures did.

Three keys, in order of how much they mean:
  BOUNDS        every live component's resolved box. Node indices do not
                appear in the line, so an identical multiset means the tree
                and every box in it are the same.
  ROLE_WIDGET   the roles that resolved and where; the node index is
                normalised out because a port may create its controls in a
                different order.
  OWNED_WIDGET  the plugin's own controls, by the key it created them under.
                This is the port's real subject: same keys, same boxes, same
                hidden state. The TEXT of a control is compared too, but a
                readout of something measured -- a frame time, a memory
                figure -- differs every run by construction, so those keys are
                named as volatile and their text is reported rather than
                judged. Naming them is the point: a key nobody declared
                volatile that changes its text still fails.
Plus: no PORCELAIN_FINDING the port did not declare.

A port may DECLARE a difference it intends, with --expect <file>. Some ports
are supposed to change what the client shows: the ledger row a port exists to
satisfy sometimes says today's output is the defect. Without a way to say so,
such a port can never pass, and the verdict becomes an argument in prose --
which is exactly the thing this comparator was built to replace.

A declaration is not a suppression. Three rules keep it honest:
  - every declaration carries a reason, and an empty one is refused;
  - every declaration is printed with the number of times it fired;
  - a declaration that never fires FAILS the gate, because a stale claim about
    what a port changes is as wrong as an undeclared change. This is the same
    bidirectional rule the layer applies to a declared absence.

The file is ini-ish, one declaration per line, `kind target = reason`:

  only-before BOUNDS com=0x1234 ... = the empty control was the defect; a
                                      skill with no reading gets no orb
  owned-drop  orb_hitpoints        = same row, said about the owned control
  normalise   scene                = the image slot is an internal handle, and
                                      releasing the source art renumbers it

`only-before` matches a whole capture line by substring and `only-after` is
its mirror, for a line the port ADDS; a port that moves a native box states
one of each, because the box left one place and arrived at another.
`role-move` names a ROLE whose resolved box changed. `owned-drop` names an
owned key that must disappear, `owned-add` one that must appear, and
`owned-move` one whose box or state the port changes on purpose -- the same
mirror, said about the plugin's own controls rather than about the lane's.
`owned-move` is the twin of `role-move`, and it exists because without it a
port that moves a NATIVE surface could not be declared at all: another
plugin's control that follows that surface changes its box, and every other
kind here is about capture lines or roles. Say in the reason WHICH it is: a control that moved because its
target did is one claim, and a control that moved for its own reasons is a
different one. `normalise` strips
`<name>=<value>` from every tail before comparing. Blank lines and lines
beginning with # are ignored.
"""
import re, sys, collections

class Expectations:
    """What a port says it changes on purpose, and whether it was telling the
    truth. Every declaration must fire at least once."""

    def __init__(self, path=None):
        self.only_before, self.only_after = {}, {}
        self.role_move, self.owned_drop, self.normalise = {}, {}, {}
        self.owned_add, self.owned_move = {}, {}
        self.fired = collections.Counter()
        if not path:
            return
        for lineno, raw in enumerate(open(path), 1):
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            # The separator is a SPACED equals, and that is load-bearing. A
            # capture line is full of `key=value` with no spaces, so splitting
            # on the first bare `=` truncates the target at the first field --
            # `only-before BOUNDS com=0x...` becomes the target `BOUNDS com`,
            # which matches every bounds line there is. A declaration that
            # excuses everything is worse than no declaration at all.
            if " = " not in line:
                raise SystemExit(f"{path}:{lineno}: a declaration needs ` = <reason>`, "
                                 f"with spaces around the equals: {line}")
            decl, reason = (p.strip() for p in line.split(" = ", 1))
            if not reason:
                raise SystemExit(f"{path}:{lineno}: a declaration with no reason is a suppression")
            kind, _, target = decl.partition(" ")
            target = target.strip()
            if not target:
                raise SystemExit(f"{path}:{lineno}: `{kind}` names nothing")
            table = {"only-before": self.only_before, "only-after": self.only_after,
                     "role-move": self.role_move, "owned-drop": self.owned_drop,
                     "owned-add": self.owned_add, "owned-move": self.owned_move,
                     "normalise": self.normalise}.get(kind)
            if table is None:
                raise SystemExit(f"{path}:{lineno}: unknown declaration `{kind}`")
            table[target] = reason

    def strip(self, text):
        for name in self.normalise:
            if re.search(rf"\b{re.escape(name)}=\S+", text):
                self.fired[("normalise", name)] += 1
                text = re.sub(rf"\s*\b{re.escape(name)}=\S+", "", text)
        return text

    def excuses_line(self, line, kind="only-before"):
        """A capture line a port is declared to add or remove.

        `only-after` is the mirror of `only-before` and it is not a symmetry
        for its own sake: a port that moves a NATIVE box states one of each,
        because the box left one place and arrived at another. Without it a
        port whose whole job is to place a surface the old one silently failed
        to place could never pass -- it can say the old box is gone and not
        that the new one is right, which is half an argument.
        """
        table = self.only_before if kind == "only-before" else self.only_after
        for target, _ in table.items():
            if target in line:
                self.fired[(kind, target)] += 1
                return True
        return False

    def excuses_role(self, role):
        """A ROLE whose resolved box the port is declared to move."""
        if role in self.role_move:
            self.fired[("role-move", role)] += 1
            return True
        return False

    def excuses_key(self, key):
        if key in self.owned_drop:
            self.fired[("owned-drop", key)] += 1
            return True
        return False

    def excuses_added_key(self, key):
        """An owned control the port is declared to ADD.

        The mirror of owned-drop, and here for the reason only-after is here.
        A port whose whole job is to put back a picture the frame was missing
        -- the Stone Drawer's chat bar, which the lane holds at full
        transparency so the re-skin that was supposed to carry it never
        reached the screen -- can say nothing under the old vocabulary: the
        drop half excuses a control that goes and there was no half for one
        that arrives. Such a change could never pass, and its verdict became
        an argument in prose, which is the thing this comparator replaces.
        """
        if key in self.owned_add:
            self.fired[("owned-add", key)] += 1
            return True
        return False

    def excuses_moved_key(self, key):
        """An owned control the port is declared to MOVE.

        Same argument again, said about a box rather than about existence. A
        fix that moves the frame's own furniture on purpose -- a chat block
        that was hung flush to the window's last row and now sits on the
        margin every other bottom-anchored piece sits on -- moves every
        control in that block, and the declaration is what makes "on purpose"
        checkable instead of assertable.

        Not the same claim as owned-drop and not interchangeable with it: a
        control that moved is still there, and a port that said `drop` about
        one that merely moved would be excusing the wrong thing.
        """
        if key in self.owned_move:
            self.fired[("owned-move", key)] += 1
            return True
        return False

    def report(self):
        """Print what each declaration did. A declaration nobody needed is a
        failure: it claims the port changes something it does not."""
        stale = []
        for kind, table in (("only-before", self.only_before), ("only-after", self.only_after),
                            ("role-move", self.role_move), ("owned-drop", self.owned_drop),
                            ("owned-add", self.owned_add), ("owned-move", self.owned_move),
                            ("normalise", self.normalise)):
            for target, reason in table.items():
                n = self.fired[(kind, target)]
                print(f"  declared {kind} {target}: fired {n}x -- {reason}")
                if not n:
                    stale.append(f"{kind} {target}")
        for d in stale:
            print(f"  STALE DECLARATION, nothing matched it: {d}")
        return stale


def lines(path, prefix):
    try:
        return [l.rstrip("\n") for l in open(path, errors="replace") if l.startswith(prefix)]
    except FileNotFoundError:
        return None

def roles(path, expect):
    out = {}
    for l in (lines(path, "ROLE_WIDGET") or []):
        l = expect.strip(l)
        m = re.search(r"role=(\S+).*?box=(-?\d+),(-?\d+),(-?\d+),(-?\d+) hidden=(\d)", l)
        if m:
            out[m.group(1)] = (tuple(int(x) for x in m.groups()[1:5]), m.group(6))
    return out

# Keys whose text is a live measurement. Their box and hidden state are
# judged like any other control; only the string differs run to run.
VOLATILE_TEXT = ("performance_",)

def is_volatile(key):
    return key.startswith(VOLATILE_TEXT)

def owned(path, expect):
    out = {}
    for l in (lines(path, "OWNED_WIDGET") or []):
        l = expect.strip(l)
        m = re.search(r"key=(\S+) node=\d+ box=(-?\d+),(-?\d+),(-?\d+),(-?\d+)(.*)$", l)
        if m:
            # node, owner and scene are ALLOCATION ORDER, not description.
            #
            # Two runs that create the same controls in a different order get
            # different numbers for all three, and a port that frees one image
            # and composes another renumbers scene for every control after it.
            # Leaving scene in the identity made stone601 report three owned
            # controls "moved" whose boxes were byte-identical -- motion where
            # nothing moved, which is the most expensive kind of gate output
            # because somebody has to go and prove it means nothing.
            #
            # There is a `normalise scene` declaration for this, and it is the
            # wrong instrument: a per-port declaration for a field that is
            # never a fact about the port makes every port declare it, and a
            # declaration that everyone writes is a field that should have
            # been stripped here.
            tail = re.sub(r"\bnode=\d+\b|\bowner=\d+\b|\bscene=\d+\b", "", m.group(6))
            tail = re.sub(r"\s+", " ", tail).strip()
            out[m.group(1)] = (tuple(int(x) for x in m.groups()[1:5]), tail)
    return out

def hidden_of(tail):
    m = re.search(r"hidden=(\d)", tail)
    return m.group(1) if m else "?"

# A finding line carries two fields that are a function of WHEN it happened,
# not of what happened: the frame it was first raised on and how many times it
# coalesced. Two runs of the same binary disagree about both.
FINDING_VOLATILE = re.compile(r"\s*\b(?:first_frame|count)=\S+")
# The subject a coalescing finding happens to name first. Stripped only as a
# SECOND attempt at matching, never as part of the primary key: two different
# refusals of the same verb on the same element are still two findings.
FINDING_DETAIL = re.compile(r"\s*\bdetail=.*?(?=\s+\w+=|$)")

def finding_key(line):
    return FINDING_VOLATILE.sub("", line)

def findings(path):
    """Undeclared findings, keyed so two runs of the same tree agree.

    Compared BEFORE against AFTER, like everything else here. A finding the
    port introduced is a failure; one that was already there is the tree's
    problem and not this port's, and failing a port for it makes the gate
    unusable on that lane for ever -- which is exactly what happened: a
    readout's one-second timer racing a layout switch raised an undeclared
    refusal on some runs and not others, and two ports in a row had to argue
    their way past a red lane they had not caused.
    """
    out = {}
    for l in lines(path, "PORCELAIN_FINDING") or []:
        if "expected=1" in l:
            continue
        out.setdefault(finding_key(l), l)
    return out

def compare(before, after, label, expect):
    bad = []
    b, a = lines(before + "/log.txt", "BOUNDS"), lines(after + "/log.txt", "BOUNDS")
    if b is None or a is None:
        print(f"{label}: MISSING capture ({'before' if b is None else 'after'})"); return ["missing"]
    b = [expect.strip(l) for l in b]
    a = [expect.strip(l) for l in a]
    cb, ca = collections.Counter(b), collections.Counter(a)
    gone_lines = [l for l in (cb - ca).elements() if not expect.excuses_line(l)]
    new_lines = [l for l in (ca - cb).elements()
                 if not expect.excuses_line(l, "only-after")]
    only_b, only_a = len(gone_lines), len(new_lines)
    print(f"{label}: bounds {len(b)} -> {len(a)}, only-before {only_b}, only-after {only_a}")
    if only_b or only_a:
        bad.append("bounds")
        for l in gone_lines[:4]: print("    -", l[:150])
        for l in new_lines[:4]: print("    +", l[:150])
    rb, ra = roles(before + "/log.txt", expect), roles(after + "/log.txt", expect)
    moved = sorted(k for k in set(rb) | set(ra)
                   if rb.get(k) != ra.get(k) and not expect.excuses_role(k))
    print(f"{label}: roles {len(rb)} -> {len(ra)}, differing {len(moved)}{': ' + ', '.join(moved[:6]) if moved else ''}")
    if moved: bad.append("roles")
    ob, oa = owned(before + "/log.txt", expect), owned(after + "/log.txt", expect)
    gone = sorted(k for k in set(ob) - set(oa) if not expect.excuses_key(k))
    new = sorted(k for k in set(oa) - set(ob) if not expect.excuses_added_key(k))
    diff = [k for k in sorted(set(ob) & set(oa))
            if ob[k] != oa[k] and not expect.excuses_moved_key(k)]
    # A volatile readout may differ ONLY in its text; its box and hidden state
    # are judged like everything else.
    measured = [k for k in diff if is_volatile(k) and ob[k][0] == oa[k][0]
                and hidden_of(ob[k][1]) == hidden_of(oa[k][1])]
    movedc = [k for k in diff if k not in measured and not expect.excuses_moved_key(k)]
    print(f"{label}: owned controls {len(ob)} -> {len(oa)}, missing {len(gone)}, "
          f"added {len(new)}, moved {len(movedc)}, measured-text {len(measured)}")
    for k in gone[:6]: print("    - ", k, ob[k])
    for k in new[:6]:  print("    + ", k, oa[k])
    for k in movedc[:6]: print("    ~ ", k, ob[k], "->", oa[k])
    for k in measured[:6]: print("    = ", k, "same box, measured text differs")
    if gone or new or movedc: bad.append("owned")
    fb, fa = findings(before + "/log.txt"), findings(after + "/log.txt")
    # A finding whose exact line is in both sides is plainly the same finding.
    # One that is not may still be: the readout refusal on the remount lane
    # names whichever of three sibling readouts lost the race that run, so the
    # same defect reports detail=performance_frame on one capture and
    # detail=performance_effective on the next. Failing a port for that is the
    # same mistake as failing it for the frame number, one field along.
    #
    # So a line missing from `before` is checked again without its detail, and
    # counts as carried only if a finding with the same plugin, verb, element
    # and result was already there. A genuinely new KIND of refusal still has
    # no match and still fails.
    kb = {FINDING_DETAIL.sub("", k) for k in fb}
    introduced = [fa[k] for k in fa
                  if k not in fb and FINDING_DETAIL.sub("", k) not in kb]
    carried = [fa[k] for k in fa
               if k in fb or FINDING_DETAIL.sub("", k) in kb]
    print(f"{label}: unexpected findings {len(introduced)} introduced, "
          f"{len(carried)} already there")
    for l in introduced[:4]: print("    !", l[:150])
    for l in carried[:4]: print("    ~ already before this port:", l[:130])
    if introduced: bad.append("findings")
    return bad

if __name__ == "__main__":
    args = sys.argv[1:]
    expect_path = None
    if "--expect" in args:
        i = args.index("--expect")
        if i + 1 >= len(args):
            raise SystemExit("--expect needs a file")
        expect_path = args[i + 1]
        del args[i:i + 2]
    expect = Expectations(expect_path)
    fail = 0
    for pair in args:
        before, after = pair.split(":")
        label = after.rstrip("/").split("/")[-1]
        bad = compare(before, after, label, expect)
        print(f"{label}: {'PASS' if not bad else 'FAIL (' + ', '.join(bad) + ')'}\n")
        fail += bool(bad)
    stale = []
    if expect_path:
        print("declarations:")
        stale = expect.report()
        print()
    verdict = "PASS" if not fail and not stale else (
        (f"{fail} lane(s) FAILED" if fail else "") +
        (" and " if fail and stale else "") +
        (f"{len(stale)} STALE declaration(s)" if stale else ""))
    print(f"port gate: {verdict}")
    sys.exit(1 if (fail or stale) else 0)
