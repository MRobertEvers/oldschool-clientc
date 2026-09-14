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
"""
import re, sys, collections

def lines(path, prefix):
    try:
        return [l.rstrip("\n") for l in open(path, errors="replace") if l.startswith(prefix)]
    except FileNotFoundError:
        return None

def roles(path):
    out = {}
    for l in lines(path, "ROLE_WIDGET") or []:
        m = re.search(r"role=(\S+).*?box=(-?\d+),(-?\d+),(-?\d+),(-?\d+) hidden=(\d)", l)
        if m:
            out[m.group(1)] = (tuple(int(x) for x in m.groups()[1:5]), m.group(6))
    return out

# Keys whose text is a live measurement. Their box and hidden state are
# judged like any other control; only the string differs run to run.
VOLATILE_TEXT = ("performance_",)

def is_volatile(key):
    return key.startswith(VOLATILE_TEXT)

def owned(path):
    out = {}
    for l in lines(path, "OWNED_WIDGET") or []:
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

def compare(before, after, label):
    bad = []
    b, a = lines(before + "/log.txt", "BOUNDS"), lines(after + "/log.txt", "BOUNDS")
    if b is None or a is None:
        print(f"{label}: MISSING capture ({'before' if b is None else 'after'})"); return ["missing"]
    cb, ca = collections.Counter(b), collections.Counter(a)
    only_b, only_a = sum((cb - ca).values()), sum((ca - cb).values())
    print(f"{label}: bounds {len(b)} -> {len(a)}, only-before {only_b}, only-after {only_a}")
    if only_b or only_a:
        bad.append("bounds")
        for l in list((cb - ca))[:4]: print("    -", l[:150])
        for l in list((ca - cb))[:4]: print("    +", l[:150])
    rb, ra = roles(before + "/log.txt"), roles(after + "/log.txt")
    moved = sorted(k for k in set(rb) | set(ra) if rb.get(k) != ra.get(k))
    print(f"{label}: roles {len(rb)} -> {len(ra)}, differing {len(moved)}{': ' + ', '.join(moved[:6]) if moved else ''}")
    if moved: bad.append("roles")
    ob, oa = owned(before + "/log.txt"), owned(after + "/log.txt")
    gone = sorted(set(ob) - set(oa)); new = sorted(set(oa) - set(ob))
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
    fail = 0
    for pair in sys.argv[1:]:
        before, after = pair.split(":")
        label = after.rstrip("/").split("/")[-1]
        bad = compare(before, after, label)
        print(f"{label}: {'PASS' if not bad else 'FAIL (' + ', '.join(bad) + ')'}\n")
        fail += bool(bad)
    print(f"port gate: {'PASS' if not fail else str(fail) + ' lane(s) FAILED'}")
    sys.exit(1 if fail else 0)
