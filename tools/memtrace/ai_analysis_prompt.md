# Memtrace analysis prompt (paste into Cursor)

Copy everything below the line into a **new Cursor Agent chat**. Attach or `@`-mention your exported `memtrace.db` and, if helpful, the scenario you captured (e.g. “loaded into game, walked around Lumbridge for 2 minutes”).

---

## Prompt

You are analyzing a TorIRS **memtrace** SQLite database (`memtrace.db`) to find **memory inefficiencies, leaks, and optimization opportunities** in this codebase.

### Context

- The trace records every `malloc` / `calloc` / `realloc` / `free` (and related) with call stacks and live-heap totals.
- Stacks are **leaf-first** (innermost allocator frame at index 0 in `frames`).
- Instrumentation noise is present in raw stacks. Filter it out when grouping by “logical” call site. Read `instrumentation_prefixes` from `meta` and exclude frames matching: `torirs_memtrace_`, `torirs_hook_`, `__wrap_`, `real_malloc`, `real_calloc`, `real_realloc`, `real_reallocf`, `real_free`, `real_posix_memalign`, `real_strdup`.
- `live_pointers` / `sites.live_bytes` = allocations **still live when the trace ended** (potential leaks or long-lived caches).
- High `alloc_count` with low `live_bytes` can indicate **churn** (allocate/free loops) rather than leaks.
- `realloc` events may not model `old_ptr` lifetime; treat realloc-heavy sites as suspicious but verify in source.

### Your task

1. **Inspect schema and meta**
   - Run `sqlite3 memtrace.db` (or equivalent) and read `meta`, table list, and row counts.
   - Summarize: platform, `event_count`, `peak_live_bytes`, `still_live_count`, `still_live_bytes`, trace duration.

2. **Find top problems** (run SQL, iterate as needed)
   - **Still-live weight:** largest `sites.live_bytes` with representative stacks (use `v_sites_by_live` or join `sites` + `stacks`).
   - **Allocation churn:** high `sites.alloc_count` and/or `alloc_bytes` with low `live_bytes`; top stacks by total bytes allocated.
   - **Hot functions:** `frames` grouped by frame string (after stripping instrumentation prefixes).
   - **Kind mix:** `events` grouped by `kind` — unexpected `strdup`/`realloc` volume, etc.
   - **Growth:** compare `live_bytes` over time via `events` (bucket by `t_ns` if useful).
   - **Large individual leaks:** `v_leaks` or `live_pointers` ordered by `size`.

3. **Map findings to this repo**
   - For each high-impact stack, identify the **first non-instrumentation frame** and search the codebase for that function/file.
   - Propose **concrete** improvements: pooling, caching policy, avoiding per-frame/temp allocs, sizing buffers, freeing on shutdown, `realloc` avoidance, etc.
   - Distinguish **bugs** (true leaks) from **intentional caches** (still live but bounded and useful).

4. **Deliver a report** with this structure:

```markdown
## Executive summary
(2–4 sentences: biggest issue, estimated impact, recommended first fix)

## Trace stats
(table from meta + key counts)

## Findings (ranked by impact)

### 1. [Short title]
- **Symptom:** …
- **Evidence:** (SQL used + top rows / stack snippet)
- **Likely cause:** …
- **Suggested fix:** …
- **Files to inspect:** `path/to/file.c` …

### 2. …

## Quick wins vs larger refactors
| Item | Effort | Impact |

## SQL appendix
(queries you ran that were most informative)
```

### Starter SQL

```sql
SELECT key, value FROM meta ORDER BY key;

SELECT st.live_bytes, st.alloc_bytes, st.alloc_count, st.free_count, s.frames
FROM sites st JOIN stacks s ON s.sid = st.sid
ORDER BY st.live_bytes DESC LIMIT 25;

SELECT st.alloc_bytes, st.alloc_count, st.live_bytes, s.frames
FROM sites st JOIN stacks s ON s.sid = st.sid
ORDER BY st.alloc_bytes DESC LIMIT 25;

SELECT kind, COUNT(*) AS n, SUM(size) AS bytes, AVG(size) AS avg_size
FROM events GROUP BY kind ORDER BY bytes DESC;

SELECT frame, COUNT(*) AS n
FROM frames
WHERE frame NOT LIKE '%torirs_memtrace_%'
  AND frame NOT LIKE '%torirs_hook_%'
  AND frame NOT LIKE '%__wrap_%'
  AND frame NOT LIKE '%real_malloc%'
  AND frame NOT LIKE '%real_calloc%'
  AND frame NOT LIKE '%real_realloc%'
  AND frame NOT LIKE '%real_free%'
  AND frame NOT LIKE '%real_strdup%'
GROUP BY frame ORDER BY n DESC LIMIT 40;

SELECT ptr, size, stack_id FROM v_leaks LIMIT 20;
```

### Constraints

- Prefer **evidence from the database** over guesses.
- If a finding is uncertain, say what extra trace or code reading would confirm it.
- Do not modify source files unless I explicitly ask you to implement fixes.
- Keep stack excerpts short (top 5–8 frames after filtering instrumentation).

**Attached trace:** `@memtrace.db`  
**Scenario (optional):** _describe what you were doing when the trace was captured_
