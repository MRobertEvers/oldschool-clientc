import json, os, time, urllib.request, urllib.error, sys
BASE="https://blert.io/api/v1"
RAW=os.path.join(os.path.dirname(os.path.abspath(__file__)),"blert_maiden_raw")
UA="3draster-tob-research/1.0 (mrobertevers@gmail.com)"
DELAY=3.0
def get(url):
    req=urllib.request.Request(url, headers={"User-Agent":UA})
    for attempt in range(3):
        try:
            with urllib.request.urlopen(req, timeout=90) as r:
                data=r.read(); time.sleep(DELAY); return json.loads(data.decode())
        except urllib.error.HTTPError as e:
            print('   http',e.code,url,flush=True)
            if e.code==404: time.sleep(DELAY); return None
            time.sleep(30*(attempt+1))
        except Exception as e:
            print("   retry",url,e,flush=True); time.sleep(30*(attempt+1))
    return None
def listing(mode,scale):
    p=f"{RAW}/list_m{mode}_s{scale}.json"
    if os.path.exists(p): return json.load(open(p))
    d=get(f"{BASE}/challenges?limit=100&type=1&mode={mode}&scale=eq{scale}&status=eq1")
    if d is not None: json.dump(d,open(p,"w"))
    return d
def events(uuid):
    p=f"{RAW}/{uuid}_10.json"
    if os.path.exists(p): return True
    d=get(f"{BASE}/raids/tob/{uuid}/events?stage=10")
    if d is None: return False
    json.dump(d,open(p,"w")); return True
if __name__=="__main__":
    mode=int(sys.argv[1]); n=int(sys.argv[2]); scales=[int(x) for x in sys.argv[3].split(",")]
    for sc in scales:
        ch=listing(mode,sc)
        print(f"mode={mode} scale={sc}: {len(ch or [])} listed",flush=True)
        got=0
        for c in (ch or []):
            if got>=n: break
            ok=events(c["uuid"]); print("  ",c["uuid"][:8],ok,flush=True); got+=bool(ok)
