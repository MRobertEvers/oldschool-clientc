#!/usr/bin/env python3
"""Stage a fingerprint-checked native CS2 hook for the existing cachepack pipeline.

The authored spec pins the complete input bytes and local/argument contract.
No patch is guessed onto another cache. Branches and switch offsets retain
their original targets; native layout continues after the synchronous hook.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import bz2
import gzip
import subprocess
import tempfile
import sys

from js5_cache_verify import read_index, read_group

BRANCHES = {6, 7, 8, 9, 10, 31, 32, 68, 69, 70, 71, 72, 73, 86}


def cache_script(cache, script_id):
    entry=next((e for e in read_index(cache/'main_file_cache.idx12') if e[0]==script_id),None)
    if not entry: raise ValueError('native script is absent from cache')
    with (cache/'main_file_cache.dat2').open('rb') as stream:
        blob=read_group(stream,12,*entry)
    if not blob: raise ValueError('native script sector chain is corrupt')
    kind=blob[0];size=struct.unpack_from('>I',blob,1)[0]
    if kind==0: return blob[5:5+size]
    expected=struct.unpack_from('>I',blob,5)[0]
    compressed=blob[9:9+size]
    if kind==1: data=bz2.decompress(b'BZh1'+compressed)
    elif kind==2: data=gzip.decompress(compressed)
    else: raise ValueError('unsupported cache compression')
    if len(data)!=expected: raise ValueError('native script decompression length mismatch')
    return data


def decode(data):
    switch_size = struct.unpack_from('>H', data, len(data)-2)[0]
    for footer, field_count in ((18, 6), (14, 4)):
        start = len(data)-footer-switch_size
        if start < 1: continue
        count = struct.unpack_from('>I', data, start)[0]
        if not 0 < count <= 65536: continue
        fields = list(struct.unpack_from('>'+'H'*field_count, data, start+4))
        if field_count == 4: fields = fields[:2]+[0]+fields[2:]+[0]
        pos = data.index(0)+1
        signature = data[:pos]
        ops = []
        try:
            while pos < start:
                opcode = struct.unpack_from('>H', data, pos)[0]; pos += 2
                if opcode == 3:
                    end = data.index(0, pos); operand = data[pos:end]; pos = end+1
                elif opcode == 61:
                    raise ValueError('long constants are outside this hook format')
                elif opcode >= 100 or opcode in (21,38,39,62,63):
                    operand = struct.unpack_from('>b', data, pos)[0]; pos += 1
                else:
                    operand = struct.unpack_from('>i', data, pos)[0]; pos += 4
                ops.append((opcode, operand))
        except (ValueError, struct.error):
            continue
        if pos != start or len(ops) != count: continue
        pos = start+4+field_count*2
        n = data[pos]; pos += 1
        switches = []
        for _ in range(n):
            cases = struct.unpack_from('>H', data, pos)[0]; pos += 2
            table = []
            for _ in range(cases):
                table.append(struct.unpack_from('>ii', data, pos)); pos += 8
            switches.append(table)
        if pos != len(data)-2: raise ValueError('invalid switch trailer')
        return dict(signature=signature, fields=fields, modern=field_count==6, ops=ops, switches=switches)
    raise ValueError('invalid or unsupported script format')


def encode(script):
    out = bytearray(script['signature'])
    for opcode, operand in script['ops']:
        out += struct.pack('>H', opcode)
        if opcode == 3: out += operand+b'\0'
        elif opcode >= 100 or opcode in (21,38,39,62,63): out += struct.pack('>b',operand)
        else: out += struct.pack('>i',operand)
    fields = script['fields']
    if not script['modern']: fields = fields[:2]+fields[3:5]
    out += struct.pack('>I'+'H'*len(fields),len(script['ops']),*fields)
    switch_start = len(out)
    out += bytes([len(script['switches'])])
    for table in script['switches']:
        out += struct.pack('>H',len(table))
        for key, target in table: out += struct.pack('>ii',key,target)
    out += struct.pack('>H',len(out)-switch_start)
    return bytes(out)


def fingerprint(script, script_id):
    value = 14695981039346656037
    def feed(data):
        nonlocal value
        for byte in data: value=((value^byte)*1099511628211)&((1<<64)-1)
    def integer(n): feed(struct.pack('<I',n&0xffffffff))
    for n in [script_id,len(script['ops']),*script['fields']]: integer(n)
    for op, operand in script['ops']:
        integer(op); integer(0 if op==3 else operand)
        feed((operand if op==3 else b'')+b'\0')
    integer(len(script['switches']))
    for table in script['switches']:
        integer(len(table))
        for key,target in table: integer(key);integer(target)
    return value


def normalize_overlay_find(data, spec):
    """Undo only a pinned legacy repack; pristine bytes pass unchanged."""
    digest=hashlib.sha256(data).hexdigest()
    if digest==spec['canonical_sha256']: return data
    if digest!=spec['legacy_sha256']:
        raise ValueError('overlay-find script fingerprint mismatch')
    script=decode(data);old=script['ops'];new=[];origins=[];mapping={};pc=0
    changes={change['pc']:change for change in spec['changes']}
    while pc<len(old):
        mapping[pc]=len(new)
        change=changes.get(pc)
        if change:
            before=[tuple(x) for x in change['before']]
            after=[tuple(x) for x in change['after']]
            if old[pc:pc+len(before)]!=before:
                raise ValueError('overlay-find instruction contract changed')
            # No branch may enter the middle of a repaired expression.
            for skip in range(1,len(before)): mapping[pc+skip]=None
            new.extend(after);origins.extend([None]*len(after));pc+=len(before)
        else:
            new.append(old[pc]);origins.append(pc);pc+=1
    mapping[len(old)]=len(new)
    def target(at):
        if at not in mapping or mapping[at] is None:
            raise ValueError('overlay-find branch targets a changed expression')
        return mapping[at]
    used_switches=set()
    for at,source in enumerate(origins):
        opcode,value=new[at]
        if source is not None and opcode in BRANCHES:
            new[at]=(opcode,target(source+1+value)-at-1)
        if source is not None and opcode==60:
            if value in used_switches: raise ValueError('shared compatibility switch table')
            used_switches.add(value)
            script['switches'][value]=[(key,target(source+1+offset)-at-1)
                for key,offset in script['switches'][value]]
    script['ops']=new;output=encode(script)
    if hashlib.sha256(output).hexdigest()!=spec['canonical_sha256']:
        raise ValueError('overlay-find normalization did not reproduce pristine bytes')
    return output


def patch(data, spec):
    source_hash = hashlib.sha256(data).hexdigest()
    if source_hash == spec.get('legacy_source_sha256'):
        # The old decompiler metadata discarded103's secondary-widget bit.
        # Repair only the exact shipped bytes, and prove the result is the
        # canonical native script before applying the callback transform.
        legacy = decode(data)
        for pc in spec['legacy_overlay_dot_pcs']:
            if legacy['ops'][pc] != (103, 0):
                raise ValueError('legacy overlay target contract changed')
            legacy['ops'][pc] = (103, 1)
        data = encode(legacy)
        source_hash = hashlib.sha256(data).hexdigest()
    if source_hash != spec['source_sha256']:
        raise ValueError('native script fingerprint mismatch; refusing an incompatible hook')
    script = decode(data)
    if encode(script) != data: raise ValueError('input codec roundtrip changed bytes')
    if script['fields'] != spec['trailer_fields']: raise ValueError('native argument/local contract changed')
    at = spec['before_pc']; ops = script['ops']
    if ops[at] != (spec['before_opcode'],0) or ops[at-1] != (35,spec['string_local']):
        raise ValueError('native caption hook site changed')
    # Existing caption is on the string stack. Native input locals are copies;
    # only color and caption outputs are written back to native locals.
    added = [(33,i) for i in spec['int_locals']] + [
        (0,4),(33,5),(0,1),(40,spec['highlight_helper']['script_id']),
        (0,3),(33,5),(0,1),(40,spec['highlight_helper']['script_id']),
        (3,spec['name'].encode()),(6599,0),(38,0),(38,0)]
    outputs=set(spec['output_locals'])
    for local in reversed(spec['int_locals']):
        added.append((34,local) if local in outputs else (38,0))
    # Empty means the row is filtered out. The caption and its native timer /
    # edit controls form one row, although the cache stores them as siblings.
    # Return the next child index before creating those auxiliary siblings;
    # hiding only the caption would leave them beside a zero-width label.
    added += [(36,spec['string_local']),
        (35,spec['string_local']),(4117,0),(0,0),(7,2),(33,3),(21,0),
        (33,spec['color_local']),(1101,0),
        (0,0),(33,13),(0,1),(0,2),(1000,0),(35,spec['string_local'])]
    def target(pc): return pc+len(added) if pc>at else pc
    new_ops=[]
    switch_uses={}
    for pc,(op,value) in enumerate(ops):
        if pc==at: new_ops.extend(added)
        new_pc=pc+len(added) if pc>=at else pc
        if op in BRANCHES:
            dest=pc+1+value
            if not 0<=dest<=len(ops): raise ValueError('invalid original branch target')
            value=target(dest)-new_pc-1
        if op==60:
            if value in switch_uses: raise ValueError('shared switch table is not supported')
            switch_uses[value]=(pc,new_pc)
        new_ops.append((op,value))
    for index,table in enumerate(script['switches']):
        if index not in switch_uses: raise ValueError('unreferenced switch table')
        pc,new_pc=switch_uses[index]
        script['switches'][index]=[
            (key,target(pc+1+offset)-new_pc-1) for key,offset in table]
    script['ops']=new_ops
    output=encode(script)
    if encode(decode(output))!=output: raise ValueError('patched codec roundtrip changed bytes')
    return output, fingerprint(script,spec['script_id'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--spec',type=Path,default=Path(__file__).parent/'testdata/plugin-engine/script-hooks.json')
    parser.add_argument('--input',type=Path)
    parser.add_argument('--base',type=Path)
    parser.add_argument('--cache',type=Path)
    parser.add_argument('--check',action='store_true')
    parser.add_argument('--build',action='store_true')
    parser.add_argument('--cachepack',type=Path,default=Path(__file__).resolve().parents[1]/'3rd/rscache/tools/cachepack/cachepack')
    parser.add_argument('--stage',type=Path)
    parser.add_argument('--header',type=Path)
    args=parser.parse_args();spec=json.loads(args.spec.read_text())
    compatibility=json.loads((args.spec.parent/spec['overlay_find_compatibility']).read_text())['scripts'] if spec.get('overlay_find_compatibility') else []
    if args.build:
        if not args.base or not args.cache: parser.error('--build requires --base and --cache')
        if args.cache.exists(): raise ValueError('output cache already exists; use --check or a new destination')
        args.cache.parent.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='plugin-hooks-',dir=args.cache.parent) as temporary:
            stage=Path(temporary)/'stage'
            subprocess.run([sys.executable,__file__,'--spec',str(args.spec),'--base',str(args.base),'--stage',str(stage)],check=True)
            subprocess.run([str(args.cachepack),'pack','--src',str(stage),'--out',str(args.cache),
                '--base',str(args.base),'--rev','osrs239','--asset-only','--assets=scripts',
                '--archive-list',str(stage/'archives.txt')],check=True)
        args.check=True
    data=cache_script(args.base,spec['script_id']) if args.base else args.input.read_bytes()
    output,signature=patch(data,spec)
    normalized={entry['script_id']:normalize_overlay_find(cache_script(args.base,entry['script_id']),entry) for entry in compatibility} if args.base else {}
    if args.check:
        if not args.base or not args.cache: parser.error('--check requires --base and --cache')
        if cache_script(args.cache,spec['script_id'])!=output:
            raise ValueError('hook cache does not match its source/specification')
        for script_id,body in normalized.items():
            if cache_script(args.cache,script_id)!=body:
                raise ValueError(f'overlay-find compatibility script {script_id} is stale')
        helper=spec['highlight_helper'];body=cache_script(args.cache,helper['script_id'])
        if hashlib.sha256(body).hexdigest()!=helper['source_sha256'] or decode(body)['fields']!=helper['trailer_fields']:
            raise ValueError('native highlight helper contract changed')
        # cachepack appends changed containers. All original data and unrelated
        # index entries must remain byte-identical to the verified base.
        with (args.base/'main_file_cache.dat2').open('rb') as source, (args.cache/'main_file_cache.dat2').open('rb') as target:
            while chunk:=source.read(1<<20):
                if target.read(len(chunk))!=chunk: raise ValueError('hook cache changed base data')
        for path in args.base.glob('main_file_cache.idx*'):
            original=path.read_bytes();changed=(args.cache/path.name).read_bytes()
            permitted=({spec['script_id']} | set(normalized)) if path.name.endswith('.idx12') else {12,255} if path.name.endswith('.idx255') else set()
            for at in range(0,max(len(original),len(changed)),6):
                if at//6 not in permitted and original[at:at+6]!=changed[at:at+6]:
                    raise ValueError('hook cache changed an unrelated index entry')
        print('native script hook is fresh, compatible and isolated')
        # The repository's derived-artifact checker convention is 1=fresh,
        # 0=stale, 2=error. Builds themselves use ordinary 0=success.
        return 0 if args.build else 1
    if not args.stage: parser.error('--stage is required when not checking')
    args.stage.mkdir(parents=True,exist_ok=False)
    (args.stage/'scripts').mkdir();(args.stage/'pack').mkdir()
    name='script_'+str(spec['script_id'])
    (args.stage/'scripts'/(name+'.cs2b')).write_bytes(output)
    for script_id,body in normalized.items():
        (args.stage/'scripts'/f'script_{script_id}.cs2b').write_bytes(body)
    staged_ids=sorted({spec['script_id']} | set(normalized))
    (args.stage/'pack/12_clientscripts.pack').write_text(''.join(f'{script_id}=script_{script_id}\n' for script_id in staged_ids))
    (args.stage/'archives.txt').write_text(''.join(f'scripts={script_id}\n' for script_id in staged_ids))
    (args.stage/'hook-receipt.json').write_text(json.dumps(dict(spec=spec,output_sha256=hashlib.sha256(output).hexdigest(),
        runtime_fingerprint=f'{signature:016x}',normalized_scripts={str(k):hashlib.sha256(v).hexdigest() for k,v in normalized.items()},tool_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()),indent=2)+'\n')
    if args.header:
        args.header.write_text('/* Generated by tools/plugin_engine_script_hooks.py; do not edit. */\n'
            '#define TORIRS_GROUND_CAPTION_SCRIPT '+str(spec['script_id'])+'\n'
            '#define TORIRS_GROUND_CAPTION_INTS '+str(len(spec['int_locals'])+2)+'\n'
            '#define TORIRS_GROUND_CAPTION_WRITE_INTS UINT64_C('+str(spec['int_writable_mask'])+')\n'
            '#define TORIRS_GROUND_CAPTION_FINGERPRINT UINT64_C(0x'+f'{signature:016x}'+')\n')
    print(f"staged {spec['name']}: {len(output)} bytes, fingerprint {signature:016x}")


if __name__=='__main__':
    try:
        raise SystemExit(main())
    except (ValueError, OSError, struct.error, subprocess.CalledProcessError) as error:
        print(f'plugin_engine_script_hooks: {error}',file=sys.stderr)
        raise SystemExit(2)
