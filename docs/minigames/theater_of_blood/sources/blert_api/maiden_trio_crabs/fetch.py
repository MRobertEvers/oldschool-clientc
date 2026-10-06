import json,os,time,glob,shutil,urllib.request,datetime
BASE="https://blert.io/api/v1";OUT=os.path.dirname(os.path.abspath(__file__))
OLD="build/spec_state/matthew-mbp-m4-raid-b1-spec-tob/blert_maiden_raw"
UA="3draster-tob-research/1.0 (mrobertevers@gmail.com)"
def get(u):
    for a in range(4):
        try:
            r=urllib.request.urlopen(urllib.request.Request(u,headers={"User-Agent":UA}),timeout=90)
            d=json.loads(r.read().decode());time.sleep(3);return d
        except urllib.error.HTTPError as e:
            if e.code==404: time.sleep(3);return None
            time.sleep(30*(a+1))
        except Exception: time.sleep(30*(a+1))
WANT=26;got=0;cur=None
while got<WANT:
    q=f"{BASE}/challenges?limit=50&type=1&mode=11&scale=eq3&status=eq1"+(f"&startTime=lt{cur}" if cur else "")
    L=get(q)
    if not L: break
    cur=int(datetime.datetime.fromisoformat(L[-1]["startTime"].replace("Z","+00:00")).timestamp()*1000)
    for c in L:
        if got>=WANT: break
        p=f"{OUT}/{c['uuid']}.json"
        if not os.path.exists(p):
            o=f"{OLD}/{c['uuid']}_10.json"
            if os.path.exists(o): shutil.copy(o,p)
            else:
                s=get(f"{BASE}/raids/tob/{c['uuid']}/events?stage=10")
                if s is None: continue
                json.dump(s,open(p,"w"))
        json.dump(c,open(p+".meta","w"));got+=1;print(got,c['uuid'][:8],flush=True)
