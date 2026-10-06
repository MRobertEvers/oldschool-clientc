import json, os, time, urllib.request, sys
BASE="https://blert.io/api/v1"
RAW=os.path.join(os.path.dirname(os.path.abspath(__file__)),"blert_sote_raw")
UA="3draster-tob-research/1.0 (mrobertevers@gmail.com)"
DELAY=3.0
def get(url):
    req=urllib.request.Request(url, headers={"User-Agent":UA})
    for attempt in range(3):
        try:
            with urllib.request.urlopen(req, timeout=90) as r:
                data=r.read(); time.sleep(DELAY); return json.loads(data.decode())
        except Exception as e:
            print("   retry",url,e,flush=True); time.sleep(30*(attempt+1))
    return None
plan=[(11,5,6),(11,4,4),(11,3,4),(10,1,3),(10,3,3),(10,4,2),(12,5,3),(12,4,2),(12,3,2)]
for mode,scale,n in plan:
    lp=f"{RAW}/list_m{mode}_s{scale}.json"
    if os.path.exists(lp): ch=json.load(open(lp))
    else:
        ch=get(f"{BASE}/challenges?limit=100&type=1&mode={mode}&scale=eq{scale}&status=eq1&stage=ge13")
        if ch is not None: json.dump(ch,open(lp,"w"))
    print(f"mode={mode} scale={scale}: {len(ch or [])} listed",flush=True)
    for c in (ch or [])[:n]:
        p=f"{RAW}/m{mode}_s{scale}_{c['uuid']}_13.json"
        if os.path.exists(p): continue
        d=get(f"{BASE}/raids/tob/{c['uuid']}/events?stage=13")
        if d is None: print("  FAIL",c['uuid'][:8],flush=True); continue
        json.dump(d,open(p,"w")); print("  ok",c['uuid'][:8],len(d),flush=True)
print("DONE",flush=True)
