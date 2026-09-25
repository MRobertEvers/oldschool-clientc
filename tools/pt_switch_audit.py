#!/usr/bin/env python3
"""List every `switch` between a PT_BEGIN and its PT_END (comments stripped).

A protothread is a switch on its resume point and every await is a case label;
a nested switch owns the label and the task resumes into a dead end. Run from
src/makefile as check-pt-switch; the last line must read `total 0`.
"""
import re,os,sys
root=sys.argv[1] if len(sys.argv)>1 else 'src'
hits=[]
for d,_,files in os.walk(root):
    for f in files:
        if not f.endswith(('.c','.h')): continue
        p=os.path.join(d,f)
        s=open(p,encoding='utf-8',errors='replace').read()
        for m in re.finditer(r'\bPT_BEGIN\s*\(',s):
            end=s.find('PT_END',m.end())
            if end<0: continue
            body=s[m.end():end]
            # strip comments and strings crudely
            body=re.sub(r'/\*.*?\*/','',body,flags=re.S)
            body=re.sub(r'//[^\n]*','',body)
            for sm in re.finditer(r'\bswitch\s*\(',body):
                line=s[:m.end()].count('\n')+1+body[:sm.start()].count('\n')
                hits.append((p,line))
for h in hits: print("%s:%d"%h)
print('total',len(hits))
