"""Each Bork fight's first ticks from the first raider over the wall (A): faces, steps, gong, sound, song.
    python3 bk_engage.py <all-players transcript>...
"""
import re,sys
for path in sys.argv[1:]:
    t=0; rows={}; land=[]; cur=None; seen=set()
    for line in open(path,errors="replace"):
        m=re.match(r"^\[(\d+)\]",line)
        if m: t=int(m.group(1)); cur=None; continue
        m=re.match(r"^ {8}\[(\w+)\] player=\(index=\d+, name=([^,]+), coord=\((\d+), (\d+), \d\)\)(?:, newcoord=\((\d+), (\d+))?",line)
        if m:
            cur=None
            nx,nz=(int(m[5]),int(m[6])) if m[5] else (int(m[3]),int(m[4]))
            ox=int(m[3])
            if ox<4182 and nx>=4184 and 5590<=nz<=5608: land.append((t,m[2]))
            continue
        m=re.match(r"^ {8}\[(\w+)\] npc=\(index=\d+, id=(tfa_bork|tfa_smork) \(\d+\), coord=\((\d+), (\d+)",line)
        if m:
            cur=m[2][4:]
            if m[1] not in("idle",): rows.setdefault(t,[]).append(f"{cur}:{m[1]}")
            continue
        if re.match(r"^ {8}\[",line): cur=None; continue
        if cur and line.startswith("            "):
            s=line.strip()
            if s.startswith("[headbar]") or s.startswith("[hit]") or "thrall" in s: continue
            s=re.sub(r"player=\(index=\d+, name=([^,]+),.*","player=\\1",s)
            rows.setdefault(t,[]).append(f"{cur}:{s[:60]}")
        m=re.search(r"\[sound_area\] id=12769",line)
        if m: rows.setdefault(t,[]).append("SOUND 12769")
        m=re.search(r"\[midi_song\w*\] id=(\d+)",line)
        if m and m[1]=="888": rows.setdefault(t,[]).append("SONG 888")
    firsts=[]
    for tt,name in land:
        if not firsts or tt-firsts[-1][0]>200: firsts.append((tt,name))
    for A,name in firsts:
        print("==",path.split('/')[-1],"A",A,name)
        for k in range(A-1,A+16):
            if k in rows: print("  +%d"%(k-A), "; ".join(rows[k]))
