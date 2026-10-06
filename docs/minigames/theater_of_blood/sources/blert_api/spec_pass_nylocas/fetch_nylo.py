import json,time,urllib.request,os,sys
API="https://blert.io/api/v1"
UA={"User-Agent":"3draster-tob-research/1.0 (mrobertevers@gmail.com)"}
OUT=os.path.join(os.path.dirname(os.path.abspath(__file__)),"blert_nylo_raw")
def get(u):
    req=urllib.request.Request(u,headers=UA)
    with urllib.request.urlopen(req,timeout=90) as r: return json.loads(r.read())
n=int(sys.argv[1]); mode=sys.argv[2]; scale=sys.argv[3]
lst=get("%s/challenges?type=1&mode=%s&scale=eq%s&status=eq1&limit=%d"%(API,mode,scale,n))
time.sleep(3)
print(len(lst),"raids listed",flush=True)
for r in lst:
    p=os.path.join(OUT,"%s.%s.m%s.s%s.json"%(r["uuid"],"nylo",mode,scale))
    if os.path.exists(p): continue
    ev=get("%s/raids/tob/%s/events?stage=12"%(API,r["uuid"]))
    json.dump(ev,open(p,"w"))
    print(r["uuid"],len(ev) if ev else 0,flush=True)
    time.sleep(3)
