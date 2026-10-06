import json, os, time, urllib.request, sys
BASE="https://blert.io/api/v1"; OUT=os.path.join(os.path.dirname(os.path.abspath(__file__)),"blert_verzik")
UA="3draster-tob-research/1.0 (mrobertevers@gmail.com)"
def get(url):
    req=urllib.request.Request(url, headers={"User-Agent":UA})
    for a in range(4):
        try:
            with urllib.request.urlopen(req, timeout=90) as r:
                d=json.loads(r.read().decode()); time.sleep(3); return d
        except Exception as e:
            print("retry",e,flush=True); time.sleep(20*(a+1))
combos=[(10,1,3),(10,2,3),(10,3,3),(10,4,3),(10,5,3),(11,1,2),(11,2,2),(11,3,3),(11,4,3),(11,5,3),(12,3,2),(12,4,2),(12,5,3)]
for mode,scale,n in combos:
    ch=get(f"{BASE}/challenges?limit=20&type=1&stage=ge15&mode={mode}&scale=eq{scale}&status=eq1") or []
    print(mode,scale,len(ch),flush=True)
    for c in ch[:n]:
        p=f"{OUT}/{mode}_{scale}_{c['uuid']}.json"
        if os.path.exists(p): continue
        d=get(f"{BASE}/raids/tob/{c['uuid']}/events?stage=15")
        if d is not None: json.dump(d,open(p,"w"))
        print("  ",c['uuid'][:8],"ok" if d is not None else "FAIL",flush=True)
