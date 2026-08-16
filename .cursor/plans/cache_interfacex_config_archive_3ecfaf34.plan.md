---
name: Cache interfacex config archive
overview: Eliminate the redundant bzip2 decompression that accounts for ~82% of interfacex's runtime by caching the decoded Object/Params config file lists instead of reloading and decompressing them from disk on every lookup.
todos: []
isProject: false
---

# Cache the object/params config archive in `interfacex`

## Root cause (recap)

Profiling showed 82% of runtime (951/1155 samples) goes into `bzip_decompress` under `InterfaceX_LoadObjConfig`. That function, plus two siblings, each independently call `RSCacheDat2Disk_ArchiveNewLoad` + `RSCacheShared_FileListNewFromCacheArchive` on **every invocation**, fully re-reading and re-decompressing the whole `Object`/`Params` config archive from disk just to pull out one item's bytes by id. There are three call sites in [tools/interfacex/main.c](tools/interfacex/main.c), all doing the identical load/free dance for the same archive over and over:

- `InterfaceX_LoadObjConfig` (~line 6697) - `RSCacheDat2A_ConfigKind_Object`
- `InterfaceX_LoadParamType` (~line 8843) - `RSCacheDat2A_ConfigKind_Params`
- `InterfaceX_LoadObjType` (~line 8886) - `RSCacheDat2A_ConfigKind_Object`

## Why caching in `main.c` (not the shared cache library)

`RSCacheShared_FileList` (in [src/osrs/rscache/shared/shared_file_list.c](src/osrs/rscache/shared/shared_file_list.c)) copies each file's bytes into its own independently-`malloc`'d buffer during decode - it holds no pointers back into the source `RSCacheDat2Disk_Archive`. That means once a `FileList` is built, the underlying `archive` can be freed immediately and the `FileList` stays valid on its own. This makes a tiny, self-contained cache safe to add locally in `tools/interfacex/main.c`, without touching the shared `dat2disk`/`shared_file_list` library used by many other tools (lower risk, smaller diff, and scoped exactly to the tool with the observed lag).

## Change

Add a small static cache of decoded `RSCacheShared_FileList*` keyed by `(disk, config_kind)`, and route all three loaders through it.

1. Forward-declare two new static helpers near the existing forward declarations (~line 5494 in `main.c`, next to `InterfaceX_ConfigArchiveReady`/`InterfaceX_ConfigArchiveFindFile`):
   - `InterfaceX_ConfigArchiveGetFileList(struct RSCacheDat2Disk* disk, int config_kind) -> struct RSCacheShared_FileList*`
   - `InterfaceX_ConfigArchiveCacheFreeAll(void)`

2. Implement them next to `InterfaceX_ConfigArchiveReady`/`InterfaceX_ConfigArchiveFindFile` (~line 8751):
   - A small fixed-size static array (e.g. 4 entries - only `Object` and `Params` kinds are ever used by this tool) of `{ struct RSCacheDat2Disk* disk; int config_kind; struct RSCacheShared_FileList* file_list; }`.
   - `InterfaceX_ConfigArchiveGetFileList`: linear-scan the cache for a matching `(disk, config_kind)`; if found return it. Otherwise call `InterfaceX_ConfigArchiveReady` + `RSCacheDat2Disk_ArchiveNewLoad` + `RSCacheDat2Disk_ArchiveInitMetadata` + `RSCacheShared_FileListNewFromCacheArchive` exactly as today, immediately free the now-unneeded `archive` (since `FileList` owns independent copies), store the `FileList` in a new cache slot, and return it.
   - `InterfaceX_ConfigArchiveCacheFreeAll`: iterate the cache and call `RSCacheShared_FileListFree` on each entry, reset count to 0. This keeps ownership clean and avoids leak-sanitizer complaints under the `ASAN=1` Makefile build.

3. Update the three loader functions to call `InterfaceX_ConfigArchiveGetFileList(disk, <kind>)` instead of doing the load/init/build-filelist sequence inline, and remove their now-incorrect `RSCacheShared_FileListFree(fl)` / `RSCacheDat2Disk_ArchiveFree(arch)` calls at the end (the cache now owns the `FileList`, and the `archive` is already freed inside the helper). `InterfaceX_ConfigArchiveFindFile` usage stays unchanged since it just reads from the returned `fl`.

4. In `main()` (~line 9528, right before `return 0;`), call `InterfaceX_ConfigArchiveCacheFreeAll()` for clean teardown.

## Verification

- Rebuild both `interfacex` and `interfacex_dbg` (`make -C tools/interfacex clean all`, and the ASAN variant if present) and confirm it still produces identical output (`interfacex_12-3.bmp` diff against the current output, plus the `uitreex: N nodes` console output/tree dump should be byte-identical).
- Re-time with `time ./interfacex` and compare against the current ~1.1s baseline.
- Re-run the same `xctrace record --template 'Time Profiler'` + folded-stack + `flamegraph.pl` profiling workflow used earlier and confirm `bzip_decompress`/`read_bunzip`/`get_next_block` drop from ~82% of samples to a small one-time cost (should now show up once, not dozens of times, in the flamegraph).
  </plan>
  <todos>
  <todo id="add-cache-helpers">Add InterfaceX_ConfigArchiveGetFileList and InterfaceX_ConfigArchiveCacheFreeAll (forward decls + implementation) in tools/interfacex/main.c</todo>
  <todo id="update-loaders">Route InterfaceX_LoadObjConfig, InterfaceX_LoadParamType, and InterfaceX_LoadObjType through the new cached helper and drop their now-redundant free calls</todo>
  <todo id="teardown">Call InterfaceX_ConfigArchiveCacheFreeAll() at the end of main() before return</todo>
  <todo id="verify">Rebuild, diff output bitmap/console output against baseline, re-time, and re-profile with xctrace + flamegraph to confirm the fix</todo>
  </todos>
