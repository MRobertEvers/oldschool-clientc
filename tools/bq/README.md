# The XP benchmark queue

One Windows XP box, many agents, one number that has to be trustworthy.

The box renders the osrs239 bench wedge at ~35 ms/frame and is **bimodal by
~7.5%** -- the same binary measures 35.2 or 37.8 depending on which mode the run
lands in, and the mode persists for a whole run. Two consequences drive this
whole design:

1. **Two benchmarks must never overlap.** A run that shares the box with
   anything else is not slow, it is meaningless. Serialization is enforced by
   the queue, not by agent discipline -- an agent has no way to run a timed
   benchmark itself.
2. **A difference under ~8% is not a result.** The client prints that warning
   on every delta it renders, so nobody has to remember it.

## Using it (this is all an agent needs)

    python tools/bq/bq.py submitwait --label gouraud-v2 --frames 900 --reps 2 \
        --arm base=/abs/torirs-base.exe \
        --arm v2=/abs/torirs-v2.exe

Blocks until the result is in, prints best/median/worst per arm and the deltas.
Arms may carry per-arm env: `--arm name=/abs/x.exe:TORIDRAW_EIP_SAMPLE=1`.

**Batch.** A 4-arm palindrome at 900 frames is ~4.5 min of exclusive box time,
so twelve 2-arm jobs cost ~54 min while one 12-arm job costs ~13. Building
variants is cheap and parallel and local; box time is the scarce thing. Build
everything, then benchmark once.

Other verbs: `submit` (returns a job id now), `wait`, `show`, `ledger` (every
arm ever measured), `logs <job> --outdir DIR` (pulls run logs and any EIP dumps
off the box), `fetch <remote> <local>`. None of those touch the box's CPU, so
none of them queue -- you can pull logs while someone else's benchmark runs.

## The pieces

| file | where it runs | what it is |
|---|---|---|
| `bq.py` | host | the agent-facing client |
| `bqd.py` | host | the drainer: `run` / `status`. Exactly one may exist |
| `bqlib.py` | host | box HTTP API, lease, runlog, hashing |
| `xp_runner.py.tmpl` | **box, Python 3.2** | runs the arms, times them |
| `bqaccept.py` | host | the acceptance tests below |
| `eipresolve.py` | host | names the offsets in an EIP dump |

## Why it is shaped like this

**The drainer is the lock.** One process holds a lease and is the only thing
that launches a benchmark. A second drainer refuses to start. Serialization is
therefore a property of the system rather than something every agent has to get
right, which matters because agents will get it wrong.

**The lease expires, and a dead owner is detected immediately.** A crashed
drainer leaves a lease behind with a fresh heartbeat. Waiting `LEASE_SECONDS`
to find out it is dead parks the queue for 90 s, so `lease_is_live` also asks
the OS whether the owning pid still exists (via `OpenProcess`, never
`os.kill(pid, 0)` -- on Windows that *kills* the process it is asked about).

**An interrupted job is re-run or explicitly reported, never silently dropped.**
A silent requeue that never runs reads as "that arm was slow", which is worse
than no measurement at all. On restart the drainer looks at whether the box
actually started the job -- the box writes a `.started` marker as its first act
-- and either re-runs it or *adopts the live run in place*, so a kill 3 minutes
into a 4-minute job does not make the box repeat the work.

**Provenance on every result.** The binary is hashed **on the box**, not by the
uploader: a sha256 that was only ever claimed by the sender proves nothing about
what executed. Results carry the sha256, build command, source rev, frame
count, arm order, every per-run wall time, and box-side start/end timestamps.
Arms upload once as `bq_<sha12>.exe`, so the filename on the box *is* the
provenance. Every event appends to a runlog that is never rewritten.

**Non-overlap is proved from the box's clock.** The box's wall clock reads 2005,
which is fine -- it is monotonic, and monotonic within the box is exactly what
the proof needs.

**The measurement contract is baked into the runner**, not into a checklist:
900 frames, `SDL_VIDEODRIVER=dummy`, palindrome arm order, `TORIRS_PERF*`
deleted from the environment (unset, not `=0` -- the profiler is ~69% of the XP
frame and a stray CSV var re-arms it), and stray `torirs*.exe` processes killed
before timing, since a leftover contaminates every measurement after it.

**Asm kernels are verified present.** `TORIDRAW_ABLATE`, `TORIDRAW_SPAN_CENSUS`
and `TORIDRAW_SPAN_TRACE` in `TORIDRAW_PROBE_CFLAGS` trip a makefile gate that
silently withdraws all four handrolled asm kernels. Every arm is checked with
`nm` at submit time and the result records `asm=all 4 present`.

Two box-side landmines worth keeping written down: it is **Python 3.2**, so no
f-strings, no `subprocess.run`, no `subprocess.DEVNULL`, no `os.replace`. And a
detached child **must** be handed an explicit `env` carrying `PYTHONIOENCODING`
-- without it, Python 3.2's console-encoding probe kills the child before it
executes a line, silently, because stdout is devnull.

## Acceptance

Output in `docs/xpbench/`.

1. **Concurrency** (`acceptance-1-concurrency.txt`) -- three agents enqueued
   within 7 s of each other; the box ran them at offsets 392-426, 428-460,
   462-496 with ~2 s gaps. 21 runs across 11 jobs, none overlapping.
2. **Restart** (`acceptance-2-restart.txt`) -- drainer killed with
   `taskkill /F /T` so no cleanup runs. Killed before the box started -> logged
   `rerun`, job completed on attempt 2. Killed mid-run -> logged `adopt-live`,
   4 timed runs, box never asked to repeat work. Queue kept moving afterwards.
3. **Baseline** (`acceptance-3-baseline.txt`) -- `torirs-dense.exe` (sha256
   `64166bdf...`) re-measured at **35.226 ms** against its known 35.1 (+0.36%).
