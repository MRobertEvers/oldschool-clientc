import json, os, time, urllib.request, sys
BASE="https://blert.io/api/v1"; CACHE="/tmp/blertdata"
DELAY=3.0
def get(url):
    req=urllib.request.Request(url, headers={"User-Agent":"tob-research/1.0 (mechanics study)"})
    for attempt in range(4):
        try:
            with urllib.request.urlopen(req, timeout=90) as r:
                time.sleep(DELAY); return json.loads(r.read().decode())
        except Exception as e:
            print("   retry", e, flush=True); time.sleep(20*(attempt+1))
    return None
def events(uuid, stage):
    p=f"{CACHE}/{uuid}_{stage}.json"
    if os.path.exists(p): return True
    d=get(f"{BASE}/raids/tob/{uuid}/events?stage={stage}")
    if d is None: return False
    json.dump(d, open(p,"w")); return True
if __name__=="__main__":
    mode, scale, n = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
    stages=[int(x) for x in sys.argv[4].split(",")]
    ch=get(f"{BASE}/challenges?limit=100&type=1&mode={mode}&scale=eq{scale}&status=eq1")
    print(f"mode={mode} scale={scale}: {len(ch or [])} completed", flush=True)
    for c in (ch or [])[:n]:
        for s in stages:
            ok=events(c["uuid"], s)
            print(f"  {c['uuid'][:8]} s{s} {'ok' if ok else 'FAIL'}", flush=True)
