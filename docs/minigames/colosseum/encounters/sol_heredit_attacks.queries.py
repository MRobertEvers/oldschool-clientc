#!/usr/bin/env python3
"""Queries behind encounters/sol_heredit_attacks.tsv: OBSERVED Blert events only, cached sample, no network.
    python3 docs/minigames/colosseum/encounters/sol_heredit_attacks.queries.py > build/logs/sol_q.txt
Reads sources/blert_api/*.json wave '12' events: 10 NPC_ATTACK of npc 12821 (attack 110 thrust = seq 10883,
111 slam = 10885, 112 break/grapple = 10884, 113 combo = 10887; the long triple 10886 is not in the plugin's
lookup, M41), 204 dust (graphics 2669-2671 created; pattern/direction LABELS derived), 206 pools (graphic 2698),
205 grapple, 7 NPC_SPAWN. Phase = number of transition pool events (a pools event listing >= 5 tiles) seen so far.
Questions: M20 pool and gates, M21 alternation, M19 attack-to-dust tick, M25 grapple window, D24 first attack."""
import collections, glob, json, os
API = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sources", "blert_api")
NAME = {110: "thrust", 111: "slam", 112: "grapple", 113: "triple"}
PAT = {0: "spear1", 1: "spear2", 2: "shield1", 3: "shield2"}
streams = []
for f in sorted(glob.glob(os.path.join(API, "*.json"))):
    d = json.load(open(f)); ev = (d.get("waves") or {}).get("12")
    if ev:
        streams.append((os.path.basename(f)[:8], ev))


def dist(c):
    return " ".join("%s:%d" % (k, c[k]) for k in sorted(c, key=str))


def timeline(ev):
    """phase-annotated list of (tick, kind, detail) for one stream."""
    out = []; phase = 0
    pools = sorted((e["tick"], len(e["colosseumSolPools"]["pools"])) for e in ev if e["type"] == 206)
    trans = []; last = -99
    for t, n in pools:
        if n >= 5 and t - last > 8:
            trans.append(t)
        last = t if n >= 5 else last
    items = []
    for e in ev:
        if e["type"] == 10 and e["npc"]["id"] == 12821:
            items.append((e["tick"], "atk", e["npcAttack"]["attack"]))
        elif e["type"] == 204:
            items.append((e["tick"], "dust", e["colosseumSolDust"]["pattern"]))
        elif e["type"] == 205:
            g = e["colosseumSolGrapple"]; items.append((e["tick"], "grap", (g["attackTick"], g["target"], g["outcome"])))
    items.sort(key=lambda x: (x[0], x[1]))
    for t, k, v in items:
        ph = sum(1 for x in trans if x <= t)
        out.append((t, k, v, ph))
    return out, trans


def main():
    print("streams", len(streams))
    spawn = collections.Counter(); first = collections.Counter(); firstkind = collections.Counter()
    gaps = collections.defaultdict(collections.Counter); counts = collections.defaultdict(collections.Counter)
    seqlens = []; atk2dust = collections.defaultdict(collections.Counter); first_phase = collections.defaultdict(collections.Counter)
    after_trans = collections.Counter(); trans_n = collections.Counter(); grap = collections.Counter(); gwin = collections.Counter()
    alt = collections.Counter(); sincespecial = collections.Counter(); specgap = collections.defaultdict(collections.Counter)
    for run, ev in streams:
        tl, trans = timeline(ev); trans_n[len(trans)] += 1
        sp = [e["tick"] for e in ev if e["type"] == 7 and e["npc"]["id"] == 12821]
        atk = [(t, v, ph) for t, k, v, ph in tl if k == "atk"]
        if sp and atk:
            first[atk[0][0] - sp[0]] += 1; firstkind[NAME.get(atk[0][1])] += 1
        for (t0, a0, p0), (t1, a1, p1) in zip(atk, atk[1:]):
            gaps[(NAME[a0], p0)][t1 - t0] += 1
        for t, a, p in atk:
            counts[p][NAME[a]] += 1
        for k in ("thrust", "slam", "grapple", "triple"):
            fp = [p for t, a, p in atk if NAME[a] == k]
            if fp: first_phase[k][fp[0]] += 1
        # attack tick -> next dust tick
        dust = [(t, v) for t, k, v, ph in tl if k == "dust"]
        for t, a, p in atk:
            if a in (110, 111):
                nd = [dt for dt, _ in dust if dt >= t]
                if nd: atk2dust[NAME[a]][nd[0] - t] += 1
        # first attack after each transition
        for tt in trans:
            nxt = [(t, a) for t, a, p in atk if t > tt]
            if nxt: after_trans[NAME[nxt[0][1]]] += 1
        # pattern alternation: dust patterns paired with the attack sequence
        prev = None; seq = []
        for t, k, v, ph in tl:
            if k == "dust": seq.append((t, PAT[v]))
        for (t0, p0), (t1, p1) in zip(seq, seq[1:]):
            s0, s1 = p0[:5], p1[:5]
            alt[(p0, p1)] += 1
        for t, k, v, ph in tl:
            if k == "grap":
                grap[(v[2])] += 1; gwin[t - v[0]] += 1
        # specials: attacks since last special
        n = 0
        for t, a, p in atk:
            if a in (112, 113):
                specgap[NAME[a]][n] += 1; n = 0
            else:
                n += 1
    print("transition pool events per stream:", dist(trans_n))
    print("Sol spawn tick to first attack:", dist(first), " first attack kind:", dict(firstkind))
    for ph in sorted(counts): print("phase", ph, "attack counts:", dict(counts[ph]))
    print("first phase each kind is seen:", {k: dict(v) for k, v in first_phase.items()})
    print("first attack after a transition:", dict(after_trans))
    for k in sorted(gaps): print("gap prev=%s phase=%d n=%d %s" % (k[0], k[1], sum(gaps[k].values()), dist(gaps[k])))
    for k in atk2dust: print("attack tick -> next dust tick,", k, dist(atk2dust[k]))
    print("dust pattern pairs (consecutive dust):", {"%s>%s" % k: v for k, v in sorted(alt.items())})
    print("grapple outcome (0 hit 1 defend 2 parry):", dist(grap), "event tick minus attackTick:", dist(gwin))
    for k in specgap: print("regular attacks before each", k, dist(specgap[k]))


def q_clean_gaps():
    """Gap to the next Sol attack, split by whether a transition pool event falls inside the gap (a transition
    adds its own 7+ ticks) and by phase at the first attack (p<2: above 75 %; p>=2: below 75 %)."""
    clean = collections.defaultdict(collections.Counter); cross = collections.defaultdict(collections.Counter)
    for run, ev in streams:
        tl, trans = timeline(ev)
        atk = [(t, v, p) for t, k, v, p in tl if k == "atk"]
        for (t0, a0, p0), (t1, a1, p1) in zip(atk, atk[1:]):
            key = (NAME[a0], "p>=2" if p0 >= 2 else "p<2")
            (cross if any(t0 < x <= t1 for x in trans) else clean)[key][t1 - t0] += 1
    for k in sorted(clean): print("CLEAN gap prev=%s %s: %s" % (k[0], k[1], dist(clean[k])))
    for k in sorted(cross): print("CROSSES TRANSITION gap prev=%s %s: %s" % (k[0], k[1], dist(cross[k])))


def q_damage():
    """Player hitpoints (type 4 event, high 16 bits = current) change on the dust tick (attack tick + 3)."""
    am = collections.defaultdict(collections.Counter)
    for run, ev in streams:
        cur = {e["tick"]: e["player"]["hitpoints"] >> 16 for e in ev if e["type"] == 4 and "hitpoints" in e["player"]}
        for e in ev:
            if e["type"] == 10 and e["npc"]["id"] == 12821 and e["npcAttack"]["attack"] in (110, 111):
                t = e["tick"] + 3
                if t in cur and t - 1 in cur:
                    d = cur[t - 1] - cur[t]
                    am[NAME[e["npcAttack"]["attack"]]]["hit" if d >= 15 else ("none" if d == 0 else "small")] += 1
                    if d >= 15: am[NAME[e["npcAttack"]["attack"]] + "_amounts"][d] += 1
    for k in sorted(am): print("hp drop at attack+3:", k, dist(am[k]))


def q_pattern_rule():
    """Consecutive dust pairs, split by what happened between them: nothing, a special attack (grapple/triple),
    or a phase transition. Same style = both spear or both shield. Rule under test (wiki): a same-style repeat
    uses the other pattern; a style change gives pattern 1; a special resets to pattern 1; a transition keeps memory."""
    res = collections.defaultdict(collections.Counter)
    for run, ev in streams:
        tl, trans = timeline(ev)
        dust = [(t, v) for t, k, v, ph in tl if k == "dust"]
        spec = [t for t, k, v, ph in tl if k == "atk" and v in (112, 113)]
        for (t0, p0), (t1, p1) in zip(dust, dust[1:]):
            between = "special" if any(t0 < x < t1 for x in spec) else ("transition" if any(t0 < x <= t1 for x in trans) else "none")
            same = (p0 < 2) == (p1 < 2)
            kind = ("same style " + ("alternated" if p1 != p0 else "SAME pattern")) if same else ("style change -> pattern " + ("1" if p1 in (0, 2) else "2"))
            res[between][kind] += 1
    for b in sorted(res): print("between=%s: %s" % (b, dist(res[b])))
    first = collections.Counter()
    for run, ev in streams:
        tl, trans = timeline(ev)
        dust = [(t, v) for t, k, v, ph in tl if k == "dust"]
        for tt in trans:
            prev = [v for t, v in dust if t < tt]; nxt = [v for t, v in dust if t > tt]
            if prev and nxt: first[(PAT[prev[-1]], PAT[nxt[0]])] += 1
    print("pattern before transition -> first pattern after:", {"%s>%s" % k: v for k, v in sorted(first.items())})


def q_first_pattern():
    """pattern of the first dust of each wave-12 stream (0 = spear 1) and its direction label."""
    c = collections.Counter(); d = collections.Counter()
    for run, ev in streams:
        ds = sorted((e["tick"], e["colosseumSolDust"]["pattern"], e["colosseumSolDust"]["direction"]) for e in ev if e["type"] == 204)
        c[PAT[ds[0][1]]] += 1; d[ds[0][2]] += 1
    print("first dust pattern:", dist(c), " direction label (0 N 1 E 2 S 3 W):", dist(d))


main()
q_clean_gaps()
q_damage()
q_pattern_rule()
q_first_pattern()
