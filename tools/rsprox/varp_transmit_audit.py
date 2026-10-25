#!/usr/bin/env python3
"""Every varp the recordings send (a varbit through its base varp), against our
effective `transmit`: the content's own .varp blocks over the generated ones.

The tick log writes every varp write, sent or not, so a var written on the recorded
tick can still never reach the client. The generated blocks
(_explicit/configs/server_fields.generated.varp) say `transmit=no` for every varp: the
Colosseum's wave clock, glory and modifier ranks were written right and sent never
(2026-10-24). Every row printed is a recorded var we do not send: give it a
`transmit=yes` block in the content's .varp (and its real scope).

    python3 tools/rsprox/varp_transmit_audit.py <transcript|@list>... [--after 30]
"""
import collections, os, re, sys
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CT = os.path.join(ROOT, "OSRS-Content", "osrs-content")
vb = {}
for blk in open(os.path.join(CT, "configs", "all.varbit"), encoding="latin-1").read().split("\n\n"):
    h = re.match(r"\[varb(\d+)_", blk.strip()); b = re.search(r"basevar=varp(\d+)_(\w+)", blk)
    if h and b: vb[int(h.group(1))] = int(b.group(1))
transmit = {}; gen = {}
for dp, _, fs in os.walk(os.path.join(CT, "server", "scripts")):
    for f in fs:
        if not f.endswith(".varp"): continue
        p = os.path.join(dp, f)
        for m in re.finditer(r"^\[varp(\d+)_\w*\]\n(.*?)(?=^\[|\Z)", open(p, encoding="latin-1").read(), re.M | re.S):
            t = re.search(r"^transmit=(\w+)", m.group(2), re.M)
            if not t: continue
            d = gen if "generated" in f else transmit
            d[int(m.group(1))] = t.group(1)
args = sys.argv[1:]; after = 30
if "--after" in args: i = args.index("--after"); after = int(args[i + 1]); del args[i:i + 2]
paths = []
for a in args:
    paths += [l.strip() for l in open(a[1:])] if a.startswith("@") else [a]
R = collections.Counter(); S = collections.defaultdict(set); name = {}
for p in paths:
    if not p or not os.path.exists(p): continue
    t = 0
    for l in open(p, errors="replace"):
        mm = re.match(r"^\[(\d+)\]", l)
        if mm: t = int(mm.group(1)); continue
        if t < after: continue
        m = re.search(r"\[(varp_small|varp_large|varbit)\] id=(\S+) \((\d+)\)", l)
        if not m: continue
        i = int(m.group(3)); vp = vb.get(i, -1) if m.group(1) == "varbit" else i
        R[vp] += 1; S[vp].add(p); name.setdefault(vp, set()).add(m.group(2))
for vp, n in sorted(R.items(), key=lambda x: (-len(S[x[0]]), -x[1])):
    eff = transmit.get(vp, gen.get(vp, "?"))
    if eff != "yes":
        print(f"{vp:6d} transmit={eff:3s} R={n:6d} ({len(S[vp])} sess)  {', '.join(sorted(name[vp]))[:110]}")
