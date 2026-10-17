import json, os, sys, time, urllib.request
BASE = "https://blert.io/api/v1"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "raw")  # not pinned: ~200 room streams
UA = "3draster-tob-research/1.0"


def get(url):
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    for a in range(3):
        try:
            with urllib.request.urlopen(req, timeout=90) as r:
                d = json.loads(r.read().decode())
                time.sleep(1.5)
                return d
        except Exception as e:
            print("retry", url, e, flush=True)
            time.sleep(10 * (a + 1))
    return None


os.makedirs(OUT, exist_ok=True)
want = int(sys.argv[1]) if len(sys.argv) > 1 else 10
for scale in (2, 3, 4, 5):
    ch = get(f"{BASE}/challenges?limit=50&type=1&stage=ge15&mode=11&scale=eq{scale}&status=eq1") or []
    print("scale", scale, len(ch), flush=True)
    for c in ch[:want]:
        p = f"{OUT}/11_{scale}_{c['uuid']}.json"
        if os.path.exists(p):
            continue
        d = get(f"{BASE}/raids/tob/{c['uuid']}/events?stage=14")
        if d is not None:
            json.dump(d, open(p, "w"))
        print("  ", c["uuid"][:8], "ok" if d is not None else "FAIL", flush=True)
