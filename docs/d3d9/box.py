import json, os, sys, urllib.request, urllib.parse, urllib.error
B = os.environ.get('BQ_BOX', 'http://10.10.10.2:8088')
def _q(s): return urllib.parse.quote(s, safe='')
def get(p, timeout=300):
    try: return urllib.request.urlopen(B+'/fs/get?path='+_q(p), timeout=timeout).read()
    except urllib.error.HTTPError as e:
        if e.code == 404: return None
        raise
def ls(p, timeout=120):
    try: raw = urllib.request.urlopen(B+'/fs/list?path='+_q(p), timeout=timeout).read()
    except urllib.error.HTTPError as e:
        if e.code == 404: return None
        raise
    return json.loads(raw.decode('utf-8','replace'))
def put(local, remote, timeout=3600):
    blob = open(local,'rb').read()
    req = urllib.request.Request(B+'/fs/put?path='+_q(remote), data=blob,
        headers={'Content-Type':'application/octet-stream','Expect':''})
    urllib.request.urlopen(req, timeout=timeout).read()
    return len(blob)
def putbytes(blob, remote, timeout=3600):
    req = urllib.request.Request(B+'/fs/put?path='+_q(remote), data=blob,
        headers={'Content-Type':'application/octet-stream','Expect':''})
    urllib.request.urlopen(req, timeout=timeout).read()
    return len(blob)
def putscript(text, name, timeout=300):
    req = urllib.request.Request(B+'/scripts/put?name='+_q(name), data=text.encode('utf-8'),
        headers={'Content-Type':'text/plain','Expect':''})
    return urllib.request.urlopen(req, timeout=timeout).read()
def run(name, timeout=180):
    req = urllib.request.Request(B+'/scripts/run?name='+_q(name), data=b'', headers={'Expect':''})
    try: return ('ok', urllib.request.urlopen(req, timeout=timeout).read().decode('utf-8','replace'))
    except Exception as e: return ('detached', repr(e)[:300])
