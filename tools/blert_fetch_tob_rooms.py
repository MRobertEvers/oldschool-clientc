#!/usr/bin/env python3
"""Fetch Blert ToB Normal trio, death-free, completed rooms' event streams.

  tools/blert_fetch_tob_rooms.py <stage> <outdir> <n>     (stage 10 Maiden, 12 Nylocas)

Pages the listing back with startTime=lt<ms>. One request every 3 s; a
neutral User-Agent (no personal data). Streams already on disk are kept."""
import datetime, json, os, sys, time, urllib.request, urllib.error
BASE = "https://blert.io/api/v1"
UA = "3draster-tob-calibration/1.0"
stage, out, n = int(sys.argv[1]), sys.argv[2], int(sys.argv[3])
os.makedirs(out, exist_ok=True)

def get(url):
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    for attempt in range(4):
        try:
            with urllib.request.urlopen(req, timeout=90) as r:
                data = r.read()
            time.sleep(3.0)
            return json.loads(data.decode())
        except urllib.error.HTTPError as e:
            print("http", e.code, url, flush=True)
            if e.code == 404:
                time.sleep(3.0)
                return None
            time.sleep(30 * (attempt + 1))
        except Exception as e:
            print("retry", url, e, flush=True)
            time.sleep(20 * (attempt + 1))
    return None

def ms(iso):
    return int(datetime.datetime.fromisoformat(iso.replace("Z", "+00:00")).timestamp() * 1000)

lst = os.path.join(out, "list_paged.json")
rooms = json.load(open(lst)) if os.path.exists(lst) else []
def good(c):
    return c.get("mode") == 11 and c.get("scale") == 3 and c.get("totalDeaths", 1) == 0 and c.get("status") == 1
pages = 0
while sum(1 for c in rooms if good(c)) < n and pages < 40:
    url = f"{BASE}/challenges?limit=100&type=1&mode=11&scale=eq3&status=eq1"
    if rooms:
        url += "&startTime=lt%d" % min(ms(c["startTime"]) for c in rooms)
    d = get(url)
    d = d if isinstance(d, list) else (d or {}).get("challenges", [])
    if not d:
        break
    seen = {c["uuid"] for c in rooms}
    new = [c for c in d if c["uuid"] not in seen]
    if not new:
        break
    rooms += new
    pages += 1
    json.dump(rooms, open(lst, "w"))
    print("listed", len(rooms), "good", sum(1 for c in rooms if good(c)), flush=True)
got = 0
for c in rooms:
    if got >= n:
        break
    if not good(c):
        continue
    p = os.path.join(out, c["uuid"] + ".json")
    if not os.path.exists(p):
        d = get(f"{BASE}/raids/tob/{c['uuid']}/events?stage={stage}")
        if not d:
            continue
        json.dump(d, open(p, "w"))
        json.dump(c, open(p + ".meta", "w"))
    got += 1
    if got % 10 == 0:
        print(got, flush=True)
print("done", got, flush=True)
