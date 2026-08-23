#!/usr/bin/env python3
"""Aggregate verysleepy_stacks.folded into the groupings the optimization
plan needs: per-subtree inclusive totals, immediate-child splits, OS-tail
classification (heap / file IO / syscall / GDI), and heap-caller attribution.

Weights in the folded file are microseconds of weighted sample time
(sum ~= 59.94s for the 60s capture).  Frames are root -> leaf.

msvcrt/ntdll frame names are nearest-export guesses from Very Sleepy and are
classified by *chain*, not trusted individually (e.g. fopen->fsopen->wscanf->
sopen->mktemp->CreateFileA is the CRT _sopen path).
"""

import collections
import sys

FOLDED = sys.argv[1] if len(sys.argv) > 1 else "verysleepy_stacks.folded"

HEAP_SYMS = {
    "msvcrt!malloc", "msvcrt!calloc", "msvcrt!free", "msvcrt!realloc",
    "ntdll!RtlAllocateHeap", "ntdll!RtlReAllocateHeap", "ntdll!RtlFreeHeap",
    "ntdll!RtlInitializeCriticalSection",  # nearest-export alias inside heap paths
    "ntdll!RtlGetNtGlobalFlags", "ntdll!RtlSizeHeap",
}
FILE_SYMS = {
    "msvcrt!fopen", "msvcrt!fsopen", "msvcrt!sopen", "msvcrt!mktemp",
    "msvcrt!wscanf", "kernel32!CreateFileA", "msvcrt!fread", "msvcrt!filbuf",
    "msvcrt!read", "msvcrt!putch", "msvcrt!fclose", "msvcrt!clearerr",
    "msvcrt!close", "msvcrt!chsize", "msvcrt!fwrite", "msvcrt!write",
    "msvcrt!fputws", "msvcrt!fseek", "msvcrt!lseek", "msvcrt!fflush",
    "msvcrt!commit", "kernel32!ReadFile", "kernel32!WriteFile",
    "kernel32!SetFilePointer", "kernel32!CloseHandle", "kernel32!FlushFileBuffers",
}
GDI_SYMS_PREFIX = ("gdi32!", "user32!")
WAIT_SYMS = {
    "kernel32!Sleep", "kernel32!SleepEx", "kernel32!WaitForSingleObject",
    "kernel32!WaitForMultipleObjects", "ntdll!NtDelayExecution",
    "ntdll!ZwWaitForSingleObject", "winmm!timeGetTime",
}
SYSCALL = "ntdll!KiFastSystemCallRet"


def tail_class(tail):
    """Classify the CRT/OS frames after the last torirs-profile frame."""
    if not tail:
        return "self"
    s = set(tail)
    if s & HEAP_SYMS:
        return "heap"
    if s & FILE_SYMS:
        return "file-io"
    if s & WAIT_SYMS:
        return "wait"
    if any(f.startswith(GDI_SYMS_PREFIX) for f in tail):
        return "gdi/user32"
    if tail == [SYSCALL] or s == {SYSCALL}:
        return "raw-syscall"
    return "os-other"


def short(frame):
    return frame.split("!", 1)[-1]


stacks = []
total = 0
with open(FOLDED, encoding="utf-8", errors="replace") as f:
    for line in f:
        line = line.strip()
        if not line:
            continue
        path, _, w = line.rpartition(" ")
        w = int(w)
        frames = path.split(";")
        stacks.append((frames, w))
        total += w


def last_torirs_index(frames):
    idx = -1
    for i, fr in enumerate(frames):
        if fr.startswith("torirs-profile!"):
            idx = i
    return idx


def subtree(anchor, child_depth=1, top=14):
    """Inclusive total for stacks containing anchor, split by the next
    distinct frame after the anchor's last consecutive occurrence."""
    tot = 0
    children = collections.Counter()
    tails = collections.Counter()
    for frames, w in stacks:
        names = [short(f) for f in frames]
        if anchor not in names:
            continue
        tot += w
        i = names.index(anchor)
        while i + 1 < len(names) and names[i + 1] == anchor:
            i += 1
        rest = [n for n in names[i + 1:] if n != anchor]
        if rest:
            children[" > ".join(rest[:child_depth])] += w
        else:
            children["(leaf)"] += w
        ti = last_torirs_index(frames)
        tails[tail_class(frames[ti + 1:])] += w
    if tot == 0:
        return
    print(f"\n== {anchor}: {tot/1e6:.3f}s ({100.0*tot/total:.2f}%) ==")
    for name, w in children.most_common(top):
        print(f"   {w/1e6:8.3f}s {100.0*w/tot:5.1f}%  {name}")
    print("   tail classes: " + ", ".join(
        f"{k}={w/1e6:.3f}s" for k, w in tails.most_common()))


def os_tail_attribution(cls_wanted, top=22):
    """For samples whose OS-tail class matches, attribute to the nearest
    torirs-profile frame (and its parent for context)."""
    agg = collections.Counter()
    tot = 0
    for frames, w in stacks:
        ti = last_torirs_index(frames)
        cls = tail_class(frames[ti + 1:])
        if cls != cls_wanted:
            continue
        tot += w
        owner = short(frames[ti]) if ti >= 0 else "(no torirs frame)"
        parent = short(frames[ti - 1]) if ti > 0 else ""
        agg[f"{owner}  (under {parent})"] += w
    print(f"\n== OS tail '{cls_wanted}': {tot/1e6:.3f}s ({100.0*tot/total:.2f}%) by nearest torirs frame ==")
    for name, w in agg.most_common(top):
        print(f"   {w/1e6:8.3f}s {100.0*w/tot:5.1f}%  {name}")


def exclusive_torirs(top=30):
    agg = collections.Counter()
    for frames, w in stacks:
        if frames[-1].startswith("torirs-profile!"):
            agg[short(frames[-1])] += w
    print(f"\n== top exclusive torirs-profile leaves ==")
    for name, w in agg.most_common(top):
        print(f"   {w/1e6:8.3f}s {100.0*w/total:5.2f}%  {name}")


print(f"total weighted: {total/1e6:.3f}s over {len(stacks)} folded stacks")

print("\n#### OS-tail overview ####")
overview = collections.Counter()
for frames, w in stacks:
    ti = last_torirs_index(frames)
    overview[tail_class(frames[ti + 1:])] += w
for k, w in overview.most_common():
    print(f"   {w/1e6:8.3f}s {100.0*w/total:5.2f}%  {k}")

os_tail_attribution("heap")
os_tail_attribution("file-io")
os_tail_attribution("gdi/user32", top=10)
os_tail_attribution("raw-syscall", top=14)
os_tail_attribution("wait", top=8)

exclusive_torirs()

print("\n#### task-queue subtrees ####")
for a in [
    "Task_CS2Run_Run", "cs2vm2_run_script_body", "CS2VM2_RunOp",
    "RS_CS2Host_Exec", "Task_OpenSubRefresh_Run", "Task_GameProtoExec_Run",
    "Task_InterfaceOpen_Run", "Task_WorldLoad_Run", "Task_Dat2MusicLoad_Run",
    "vorbis_decode_packet", "PlatformXIO_Js5Pump",
]:
    subtree(a)

print("\n#### render / present subtrees ####")
for a in [
    "App_Render", "ToriRS_Soft3D_RenderFrame", "ToriRS_Soft3D_Execute",
    "PlatformSDL2_SetWindowSize", "App_WorldDraw",
]:
    subtree(a)
