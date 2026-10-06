# seam22 tob_normal_trio_findings (2): Blert Xarpus room events (stage 14), Normal (mode 11),
# scales 3 (the trio), 2 and 4; completed raids. UA carries no personal address.
import json, os, time, urllib.request
BASE = "https://blert.io/api/v1"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "blert_xarpus")
UA = "3draster-tob-research/1.0"
def get(url):
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    for a in range(3):
        try:
            with urllib.request.urlopen(req, timeout=90) as r:
                d = json.loads(r.read().decode()); time.sleep(3); return d
        except Exception as e:
            print("retry", e, flush=True); time.sleep(15 * (a + 1))
os.makedirs(OUT, exist_ok=True)
for mode, scale, n in [(11, 3, 12), (11, 2, 4), (11, 4, 4), (11, 1, 5), (10, 1, 5), (11, 2, 6)]:
    ch = get(f"{BASE}/challenges?limit=25&type=1&stage=ge15&mode={mode}&scale=eq{scale}&status=eq1") or []
    print(mode, scale, len(ch), flush=True)
    for c in ch[3:3 + n]:
        p = f"{OUT}/{mode}_{scale}_{c['uuid']}.json"
        if os.path.exists(p): continue
        d = get(f"{BASE}/raids/tob/{c['uuid']}/events?stage=14")
        if d is not None: json.dump(d, open(p, "w"))
        print("  ", c['uuid'][:8], "ok" if d is not None else "FAIL", flush=True)
