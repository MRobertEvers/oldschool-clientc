import re
TOK = re.compile(r"\$([A-Za-z_][A-Za-z0-9_]*)")
LOSSLESS = "// @rscache-lossless-v1 "
def strip_stamp(t):
    i = t.find(LOSSLESS)
    return t if i < 0 else t[:i]
def local_map(curated, raw):
    """raw local -> curated local, where curated == '<meaning>_' + raw."""
    raw_set = set(TOK.findall(raw)); m = {}
    for name in set(TOK.findall(curated)):
        if name in raw_set: continue
        for r in raw_set:
            if name.endswith("_" + r) and (r not in m or len(m[r]) < len(name)):
                m[r] = name
    return m
def apply(text, m):
    return TOK.sub(lambda x: "$" + m.get(x.group(1), x.group(1)), text)

def align_map(curated, raw):
    """Positional raw->curated local map when the texts agree outside `$locals`.

    Returns None when the non-local text differs (a real edit, not a rename) or
    the pairing is inconsistent (one raw local named two ways)."""
    cl = TOK.split(strip_stamp(curated)); rl = TOK.split(strip_stamp(raw))
    if len(cl) != len(rl): return None
    m = {}
    for i in range(len(rl)):
        if i % 2 == 0:
            if cl[i] != rl[i]: return None
        else:
            if m.setdefault(rl[i], cl[i]) != cl[i]: return None
    if len(set(m.values())) != len(m): return None
    return {k: v for k, v in m.items() if k != v}
