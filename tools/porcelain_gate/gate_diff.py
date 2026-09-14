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

`only-before` matches a whole capture line by substring, `owned-drop` names an
owned key that must disappear, `normalise` strips `<name>=<value>` from every
tail before comparing. Blank lines and lines beginning with # are ignored.
"""
import re, sys, collections

class Expectations:
    """What a port says it changes on purpose, and whether it was telling the
    truth. Every declaration must fire at least once."""

    def __init__(self, path=None):
        self.only_before, self.owned_drop, self.normalise = {}, {}, {}
        self.fired = collections.Counter()
        if not path:
            return
        for lineno, raw in enumerate(open(path), 1):
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            if "=" not in line:
                raise SystemExit(f"{path}:{lineno}: a declaration needs `= <reason>`: {line}")
            decl, reason = (p.strip() for p in line.split("=", 1))
            if not reason:
                raise SystemExit(f"{path}:{lineno}: a declaration with no reason is a suppression")
            kind, _, target = decl.partition(" ")
            target = target.strip()
            if not target:
                raise SystemExit(f"{path}:{lineno}: `{kind}` names nothing")
            table = {"only-before": self.only_before, "owned-drop": self.owned_drop,
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

    def excuses_line(self, line):
        for target, _ in self.only_before.items():
            if target in line:
                self.fired[("only-before", target)] += 1
                return True
        return False

    def excuses_key(self, key):
        if key in self.owned_drop:
            self.fired[("owned-drop", key)] += 1
            return True
        return False

    def report(self):
        """Print what each declaration did. A declaration nobody needed is a
        failure: it claims the port changes something it does not."""
        stale = []
        for kind, table in (("only-before", self.only_before), ("owned-drop", self.owned_drop),
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
            tail = re.sub(r"\bnode=\d+\b|\bowner=\d+\b", "", m.group(6)).strip()
            out[m.group(1)] = (tuple(int(x) for x in m.groups()[1:5]), tail)
    return out

def hidden_of(tail):
    m = re.search(r"hidden=(\d)", tail)
    return m.group(1) if m else "?"

def findings(path):
    return [l for l in lines(path, "PORCELAIN_FINDING") or [] if "expected=1" not in l]

def compare(before, after, label, expect):
    bad = []
    b, a = lines(before + "/log.txt", "BOUNDS"), lines(after + "/log.txt", "BOUNDS")
    if b is None or a is None:
        print(f"{label}: MISSING capture ({'before' if b is None else 'after'})"); return ["missing"]
    b = [expect.strip(l) for l in b]
    a = [expect.strip(l) for l in a]
    cb, ca = collections.Counter(b), collections.Counter(a)
    gone_lines = [l for l in (cb - ca).elements() if not expect.excuses_line(l)]
    new_lines = list((ca - cb).elements())
    only_b, only_a = len(gone_lines), len(new_lines)
    print(f"{label}: bounds {len(b)} -> {len(a)}, only-before {only_b}, only-after {only_a}")
    if only_b or only_a:
        bad.append("bounds")
        for l in gone_lines[:4]: print("    -", l[:150])
        for l in new_lines[:4]: print("    +", l[:150])
    rb, ra = roles(before + "/log.txt", expect), roles(after + "/log.txt", expect)
    moved = sorted(k for k in set(rb) | set(ra) if rb.get(k) != ra.get(k))
    print(f"{label}: roles {len(rb)} -> {len(ra)}, differing {len(moved)}{': ' + ', '.join(moved[:6]) if moved else ''}")
    if moved: bad.append("roles")
    ob, oa = owned(before + "/log.txt", expect), owned(after + "/log.txt", expect)
    gone = sorted(k for k in set(ob) - set(oa) if not expect.excuses_key(k))
    new = sorted(set(oa) - set(ob))
    diff = [k for k in sorted(set(ob) & set(oa)) if ob[k] != oa[k]]
    # A volatile readout may differ ONLY in its text; its box and hidden state
    # are judged like everything else.
    measured = [k for k in diff if is_volatile(k) and ob[k][0] == oa[k][0]
                and hidden_of(ob[k][1]) == hidden_of(oa[k][1])]
    movedc = [k for k in diff if k not in measured]
    print(f"{label}: owned controls {len(ob)} -> {len(oa)}, missing {len(gone)}, "
          f"added {len(new)}, moved {len(movedc)}, measured-text {len(measured)}")
    for k in gone[:6]: print("    - ", k, ob[k])
    for k in new[:6]:  print("    + ", k, oa[k])
    for k in movedc[:6]: print("    ~ ", k, ob[k], "->", oa[k])
    for k in measured[:6]: print("    = ", k, "same box, measured text differs")
    if gone or new or movedc: bad.append("owned")
    f = findings(after + "/log.txt")
    print(f"{label}: unexpected findings {len(f)}")
    for l in f[:4]: print("    !", l[:150])
    if f: bad.append("findings")
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
