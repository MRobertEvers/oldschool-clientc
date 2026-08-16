# Analyzing memtrace with Cursor

This guide explains how to export a trace from the viewer and use **Cursor Agent** to find memory inefficiencies, leaks, and churn in this codebase.

**Prerequisites:** a `MEMTRACE=1` build and a representative capture (reproduce the scenario you care about before opening the viewer).

---

## 1. Capture a trace

### Browser (WASM)

```bash
make -C src2/programs/browser MEMTRACE=1 clean all
# serve dist/ and run the app — see memtrace/README.md
```

1. Play through the scenario (load assets, enter world, perform the action you suspect is wasteful).
2. Click **Memtrace** in the app header (allow popups).
3. In the viewer tab, confirm the timeline and **Still live at end of trace** look reasonable.

### Native

```bash
make -C src2/programs/sdl2 MEMTRACE=1
./sdl2   # run your scenario; memtrace.bin written on exit
```

Open `memtrace.bin` in [viewer.html](viewer.html) (drag/drop or **Load memtrace.bin**), or decode with [decode_memtrace.py](decode_memtrace.py) first.

---

## 2. Export SQLite

In the viewer:

1. Click **Export SQLite** (requires network the first time — loads [sql.js](https://sql.js.org/) from CDN).
2. Save `memtrace.db`.

Put the file somewhere Cursor can see it, for example:

```bash
mkdir -p tools/memtrace/bins
mv ~/Downloads/memtrace.db tools/memtrace/bins/my_scenario.db
```

Generated traces and exports live under `tools/memtrace/bins/` (gitignored).

---

## 3. Start a Cursor analysis

### Option A — Use the prompt template (recommended)

1. Open [ai_analysis_prompt.md](ai_analysis_prompt.md).
2. Copy the **Prompt** section (everything under `## Prompt`).
3. Start a **new Agent** chat in Cursor.
4. Attach the database: `@tools/memtrace/bins/my_scenario.db` or drag `memtrace.db` into the chat.
5. Paste the prompt. Fill in the **Scenario** line (what you did during capture).
6. Send.

The template tells the agent which SQL to run, how to filter instrumentation frames, and what report format to return.

### Option B — Short prompt

If you already know the problem area:

```text
Analyze @tools/memtrace/bins/my_scenario.db (memtrace SQLite export).
Find the top memory inefficiencies and likely leaks. Run sqlite3 queries,
map stacks to files in this repo, and give a ranked report with evidence.
Use tools/memtrace/ai_analysis_prompt.md for schema and report format.
Scenario: <what you did during capture>
```

### Option C — Plan mode first

For a large trace or unclear scope:

1. Ask in **Plan** mode: “Read @tools/memtrace/bins/my_scenario.db meta and top 10 live sites; propose an analysis plan.”
2. Switch to **Agent** with [ai_analysis_prompt.md](ai_analysis_prompt.md) to execute.

---

## 4. What the agent can query

| Table / view | Use for |
|--------------|---------|
| `meta` | Trace size, peak live, still-live totals, duration |
| `events` | Per-event timeline, kind mix, churn |
| `sites` | Per-stack alloc/free/live aggregates |
| `stacks` / `frames` | Full call stacks, hot functions |
| `live_pointers` | Individual allocations still live at end |
| `v_sites_by_live` | Sites ranked by leaked/live bytes + stack text |
| `v_leaks` | Live pointers with stacks |

Example local check before involving the agent:

```bash
sqlite3 tools/memtrace/bins/my_scenario.db "SELECT key, value FROM meta ORDER BY key;"
sqlite3 tools/memtrace/bins/my_scenario.db "SELECT live_bytes, alloc_count, substr(frames,1,120) FROM v_sites_by_live LIMIT 10;"
```

---

## 5. Turning the report into fixes

The agent report should list **files to inspect** and **suggested fixes**. Typical next steps:

1. Open the cited source files and verify the allocation path.
2. Use the viewer **Diff two points in time** panel to see if bytes grow without bound during the same scenario.
3. Re-capture after a fix and export a new `.db` for before/after comparison (`meta.still_live_bytes`, `peak_live_bytes`).

Ask Cursor to implement only after you agree on a finding, e.g.:

```text
Implement fix #1 from the memtrace analysis: reduce allocations in <function>.
Re-run is manual; I will capture a new trace to verify.
```

---

## 6. Tips for useful traces

| Tip | Why |
|-----|-----|
| Capture **after** warmup | Avoid one-time init dominating still-live |
| Keep scenario **focused** | Shorter traces are faster to export and analyze |
| Note **wall-clock actions** | Helps interpret timeline growth |
| Compare **before/after** | Export two `.db` files when testing a fix |
| Filter instrumentation in SQL | See `instrumentation_prefixes` in `meta` |

---

## Files

| File | Purpose |
|------|---------|
| [ai_analysis_prompt.md](ai_analysis_prompt.md) | Copy-paste Cursor prompt with schema, SQL, and report format |
| [AI_ANALYSIS.md](AI_ANALYSIS.md) | This workflow guide |
| [README.md](README.md) | Memtrace build, viewer, and SQLite export details |
| [viewer.html](viewer.html) | Viewer with **Export SQLite** |
