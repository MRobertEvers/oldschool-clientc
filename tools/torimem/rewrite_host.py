#!/usr/bin/env python3
"""Rewrite bare allocation calls in host files into one lifetime group.

  python3 tools/torimem/rewrite_host.py 'ToriMem_Assets()' src/engine/foo.c ...

  malloc(n)       -> ToriMem_Alloc(G, n)
  calloc(c, n)    -> ToriMem_Calloc(G, c, n)
  realloc(p, n)   -> ToriMem_Realloc(G, p, n)
  strdup(s)       -> ToriMem_Strdup(G, s)
  strndup(s, n)   -> ToriMem_Strndup(G, s, n)
  free(p)         -> ToriMem_Free(G, p)

Comments, strings and #define lines are left alone (fix macro bodies by hand).
A cast in front of the call is kept. Adds #include "mem/torirs_mem.h". This is
the mechanical half of a conversion: frees of memory another group owns, and
pointers that change owner, are fixed by hand afterwards -- the reset walk and
the wrong-group check say where.
"""
import re, sys

group = sys.argv[1]
NAMES = {'malloc': 'ToriMem_Alloc', 'calloc': 'ToriMem_Calloc', 'realloc': 'ToriMem_Realloc',
         'strdup': 'ToriMem_Strdup', 'strndup': 'ToriMem_Strndup', 'free': 'ToriMem_Free'}
ident = re.compile(r'[A-Za-z_][A-Za-z0-9_]*')

def rewrite(src):
    out, i, n, count = [], 0, len(src), 0
    while i < n:
        c = src[i]
        if src.startswith('//', i):
            j = src.find('\n', i); j = n if j < 0 else j; out.append(src[i:j]); i = j; continue
        if src.startswith('/*', i):
            j = src.find('*/', i + 2); j = n if j < 0 else j + 2; out.append(src[i:j]); i = j; continue
        if c in '"\'':
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == '\\' else 1
            out.append(src[i:j + 1]); i = j + 1; continue
        if c == '#':
            k = i
            while True:
                e = src.find('\n', k); e = n if e < 0 else e
                if e > 0 and src[e - 1] == '\\' and e < n: k = e + 1; continue
                break
            line = src[i:e]
            if re.match(r'#\s*(include|define|undef|pragma|error)', line):
                out.append(line); i = e; continue
            out.append(c); i += 1; continue
        m = ident.match(src, i)
        if m:
            word = m.group(0); j = m.end(); k = j
            while k < n and src[k] in ' \t': k += 1
            b = i - 1
            while b >= 0 and src[b] in ' \t\n': b -= 1
            prev = src[max(0, b - 1):b + 1]
            member = prev.endswith('.') or prev.endswith('->')
            if word in NAMES and k < n and src[k] == '(' and not member:
                before = src[max(0, i - 40):i]
                if re.search(r'\b(void|char|extern)\s*\*?\s*$', before):
                    out.append(word)
                else:
                    out.append(NAMES[word] + '(' + group + ', '); count += 1
                    i = k + 1; continue
            else:
                out.append(word)
            i = j; continue
        out.append(c); i += 1
    return ''.join(out), count

total = 0
for path in sys.argv[2:]:
    src = open(path, encoding='utf-8', errors='surrogateescape').read()
    new, count = rewrite(src)
    if count:
        if 'mem/torirs_mem.h' not in new:
            m = re.search(r'^#\s*include[^\n]*\n', new, re.M)
            at = m.start() if m else 0
            new = new[:at] + '#include "mem/torirs_mem.h"\n' + new[at:]
        open(path, 'w', encoding='utf-8', errors='surrogateescape').write(new)
    total += count
    print('%5d %s' % (count, path))
print('%5d TOTAL' % total)
