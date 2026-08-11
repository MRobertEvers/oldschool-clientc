# CS2 execution and frame settlement

This note records the execution contract used by the C client. It was made
explicit while fixing the Equipment ↔ Summoning transition, where a partially
executed clientscript was being published as a frame.

## The invariant

**CS2 always drains to a fixed point before the client interacts with or renders
the UI tree.** A cooperative yield is a scheduler detail, not a frame boundary.

```text
enqueue CS2
    ↓
run every ready task → resolve layout → enqueue CS2 follow-ups ─┐
    ↑                                                          │
    └───────────── repeat until no work remains ─────────────┘
    ↓
interact → emit → render/present one committed frame
```

If a task is genuinely waiting for platform I/O, the loop stops without
publishing anything new. The client keeps the last committed framebuffer and
resumes settlement when the I/O completes.

## Why queue-idle is not enough

CS2 hooks and `RUNCLIENTSCRIPT` packets enqueue `Task_CS2Run` work; dispatch is
not an inline VM call. While executing, a host opcode may yield to load a script,
sprite, font, model, or config. The VM retries the yielding opcode from its
checkpoint, but successful earlier opcodes remain committed. The UI tree can
therefore be validly *intermediate* while the script is suspended.

Finishing that task may also create more work: `onResize`, trigger-op, inventory,
var, stat, and widget transmit hooks. The app's settlement loop consequently
does this until stable:

1. [`TaskRunner_SettleFrame`](../src/task_runner.h) runs through every ready
   yield, with no arbitrary step limit.
2. Layout is resolved for the mutations just made.
3. [`app_settle_cs2_frame`](../src/app.c) enqueues resize, trigger, and transmit
   follow-ups.
4. The loop repeats until the runner is idle and no follow-up is dirty.

`TaskRunner_Drain` is not suitable for the interactive frame path: it can spin
forever on real asynchronous I/O.

## Server-tick ordering

Server `RUNCLIENTSCRIPT` packets are held until `SERVER_TICK_END`. This lets a
script observe every interface, inventory, var, and stat update from the same
server transaction. It also makes packet order irrelevant: an `IF_SETHIDE` may
arrive either before or after the clientscript which completes the transition.

Only packets that can affect UI/CS2-visible state open this atomic transaction.
World feedback such as `SET_MAP_FLAG` can arrive between scheduled ticks; treating
every packet as a tick opener made ordinary world clicks retain the old frame for
as long as the next 600 ms server cycle. Immediate mock-server response bursts
therefore receive their own `SERVER_TICK_END` boundary as well.

## Publication rule

[`App_FrameSettled`](../src/app.c) is the publication gate. While it is false:

- do not run UI interaction against the transient tree;
- do not rebuild or render the emit list;
- do not swap a discarded GPU backbuffer; retain the previous frontbuffer; and
- preserve unconsumed input so it is handled after settlement.

Every path that enqueues CS2 must set `runner.frame_settle_pending`. The permanent
regressions are `make -C src test-cs2-frame-settle` and
`make -C src test-cs2-world-click-responsiveness`.
