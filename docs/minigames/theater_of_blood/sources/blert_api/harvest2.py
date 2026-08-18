import json, os, time, urllib.request, sys
BASE="https://blert.io/api/v1"; CACHE="/tmp/blertdata"
def get(url):
    req=urllib.request.Request(url, headers={"User-Agent":"tob-research/1.0 (mechanics study)"})
    with urllib.request.urlopen(req, timeout=90) as r: return json.loads(r.read().decode())
def collect(mode, want):
    out=[]; before=None
    while len(out)<want:
        u=f"{BASE}/challenges?limit=100&type=1&mode={mode}"
        if before: u+=f"&startTime=lt{before}"
        page=get(u)
        if not page: break
        out += [c for c in page if c["status"]==1 and c["scale"]>=1]
        before=int(__import__('datetime').datetime.fromisoformat(page[-1]["startTime"].replace("Z","+00:00")).timestamp()*1000)
        time.sleep(0.3)
    return out[:want]
def events(uuid, stage):
    p=f"{CACHE}/{uuid}_{stage}.json"
    if os.path.exists(p): return json.load(open(p))
    d=get(f"{BASE}/raids/tob/{uuid}/events?stage={stage}")
    json.dump(d, open(p,"w")); time.sleep(0.3); return d
if __name__=="__main__":
    mode=int(sys.argv[1]); n=int(sys.argv[2]); stages=[int(x) for x in sys.argv[3].split(",")]
    ch=collect(mode,n); print(f"mode {mode}: {len(ch)} completed raids", flush=True)
    json.dump(ch, open(f"{CACHE}/challenges_{mode}.json","w"))
    ok=0
    for i,c in enumerate(ch):
        for s in stages:
            try: events(c["uuid"], s); ok+=1
            except Exception as ex: print(f"  {c['uuid'][:8]}/{s}: {ex}", flush=True)
        if i%10==0: print(f"  {i}/{len(ch)} raids, {ok} files", flush=True)
    print("done", ok, flush=True)
