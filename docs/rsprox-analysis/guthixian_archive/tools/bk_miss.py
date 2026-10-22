"""Bork's seqs, lines and dust after each missed bomb (12250).
    python3 bk_miss.py <all-players transcript>...
"""
import re,sys
for p in sys.argv[1:]:
    t=0; cur=None; ev={}; misses=[]; dust={}
    for line in open(p,errors="replace"):
        m=re.match(r"^\[(\d+)\]",line)
        if m: t=int(m.group(1)); cur=None; continue
        m=re.match(r"^ {8}\[(\w+)\] npc=\(index=\d+, id=(\w+) ",line)
        if m: cur=m[2]; continue
        if re.match(r"^ {8}\[",line): cur=None
        if "[sound_area] id=12250," in line: misses.append(t)
        if cur=="tfa_bork":
            mm=re.search(r"\[sequence\] id=(\w+)",line)
            if mm: ev.setdefault(t,[]).append(mm[1].replace("npc_bork_",""))
            mm=re.search(r'\[say\] text="([^"]+)"',line)
            if mm: ev.setdefault(t,[]).append("say:"+mm[1][:20])
        if "tfa_wave_dust_short" in line: dust[t]=dust.get(t,0)+1
    for L in misses:
        row=[]
        for k in range(L+16,L+34):
            s=ev.get(k,[]); d=dust.get(k,0)
            if s or d: row.append(f"{k-L:+d}:{','.join(s)}{'' if not d else ' d'+str(d)}")
        print(p.split('/')[-1],L," ".join(row))
