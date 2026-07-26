# Container Lookup Benchmark

Measures ID-keyed lookup cost across flat arrays, an STB-style vector, `3rd/hmap`, a red-black tree, and linked lists.

```bash
make -C benchmarks/containers run
./benchmarks/containers/bench_containers --csv
```

## Results (dense / hit, min ns/lookup)

| Container | N=64 | N=256 | N=2048 |
|-----------|------|-------|--------|
| **flat-direct** | ~1.2 | ~1.0 | **~1.0** |
| **hmap-fnv** | ~4.2 | ~4.2 | ~10.7 |
| **hmap-intmix** | ~7.1 | ~6.8 | **~7.1** |
| **rbtree** | ~4.2 | ~5.4 | ~10.5 |
| **flat-binary** | ~4.2 | ~7.1 | ~16.9 |
| **vec/flat-linear** | ~12 | ~37 | ~240 |
| **list-*** | ~24 | ~95 | ~910 |

Numbers from a local `-O3` run; re-run the binary for current hardware.

### Takeaways

1. **Dense IDs → flat-direct** is ~7–10× faster than hmap at N=2048 (~1 ns vs ~7–11 ns).
2. **Integer hash beats FNV at scale** — at N=2048, `hmap-intmix` (~7 ns) beats default `hmap_hash_bytes` (~11 ns).
3. **Linear/list die past ~64–128** — by N=256 they’re already 5–20× slower than hmap/rbtree.
4. **Binary search / rbtree** stay competitive through N=2048 when IDs aren’t dense enough for direct indexing.
5. **Pool vs malloc lists** — similar on hits; pool wins hard on misses at N=2048 (pointer-chasing tax).
