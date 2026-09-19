# Project conventions

## Never return early because an input parameter is wrong — assert

A function that is handed something it cannot accept must **abort loudly**, not
return a neutral value and let the caller carry on. Use `assert()` from
`<assert.h>`.

```c
/* NO — the caller's bug becomes a silent no-op, surfacing later as a
 *      blank frame, a missing sound, or a corrupt buffer. */
bool
ToriDraw_ComputeTextureUvBases(
    const struct ToriDraw_TextureUvSource* model,
    struct ToriDraw_TextureUvBasis* out_basis)
{
    if( !model || !out_basis || model->textured_face_count <= 0 )
        return false;

/* YES — the contract violation stops here, at the frame that caused it. */
bool
ToriDraw_ComputeTextureUvBases(
    const struct ToriDraw_TextureUvSource* model,
    struct ToriDraw_TextureUvBasis* out_basis)
{
    assert(model);
    assert(out_basis);
    if( model->textured_face_count <= 0 )
        return false;
```

One `assert()` per condition, never `assert(a && b)` — the failure message must
name the parameter that was wrong.

### Where the assert goes relative to a residual guard

Split a compound guard: the contract violations become asserts, and any
condition that is a *legitimate* runtime state stays behind as a guard. Placement
depends on whether that guard reads the pointer:

```c
/* Guard reads the pointer -> assert FIRST, or the guard dereferences NULL. */
assert(str);
if( str[0] == '\0' )
    return 0;

/* Guard is independent -> assert AFTER, so a documented empty-input
 * no-op still returns instead of aborting. */
if( count == 0 )
    return NULL;
assert(items);
```

### "The data is optional" is the CALLER's condition, not the callee's

This is the trap. When a field is optional — an unrigged model's
`vertex_bones`, an untextured model's `face_textures`, a widget with no label,
a painter with no occluders — it is tempting to write the callee as
"absent in, absent out" and call it legitimate. It is not: it makes every
genuine caller bug silent for the sake of saving the caller an `if`.

Put the existence test where the knowledge is, and assert in the callee:

```c
/* NO — now nobody can pass this a bad pointer by mistake and find out. */
struct ToriDraw_Bones*
ToriDraw_BonesCopy(const struct ToriDraw_Bones* src)
{
    if( !src || src->bones_count <= 0 || !src->bones )
        return NULL;

/* YES — "does this model have bones?" is answered once, by the caller. */
ToriDraw_BonesCopy(const struct ToriDraw_Bones* src)
{
    assert(src);
    assert(src->bones_count > 0);
    assert(src->bones);
    ...
}
/* call site */
if( src->vertex_bones )
    dst->vertex_bones = ToriDraw_BonesCopy(src->vertex_bones);
```

Where the callee is reached through a macro or a dozen call sites, put the test
in the macro — one place, still not the callee (`TORIDRAW_MODEL_COPY`).

### What genuinely is not a contract violation

Only two things:

- **Deallocators.** `free(NULL)` is an idiom; `*Free`, `*Cleanup`, `*Destroy`,
  `*Release` must accept NULL.
- **A sentinel that carries its own meaning**, distinct from "absent":
  `dat2_id_wanted(NULL)` means *want every id*, not *no ids*.

Non-pointer parameters are not covered by any of this. `if( !wearable )` on an
`int` flag is a plain boolean test — asserting it aborts on the common case.

## An allocation failure is an assertion, not an `if`

`malloc`/`calloc`/`realloc`/`strdup` returning NULL is not a case to handle —
handling it is what turns an out-of-memory into a blank model, an empty npc
table, or a truncated packet that reads as valid data. Assert the result.

```c
/* NO — every one of these silently produces a wrong-but-plausible program. */
struct ToriDraw_Sprite* sprite = malloc(sizeof(*sprite));
if( !sprite )
    return NULL;

g_npcs = calloc(count, sizeof(*g_npcs));
if( !g_npcs ) { g_npc_count = 0; return 0; }   /* now "there are no npcs" */

if( pixels )                                    /* silently skips the work */
{
    ...fill pixels...
}

/* YES */
struct ToriDraw_Sprite* sprite = malloc(sizeof(*sprite));
assert(sprite);
```

This covers the `if( p ) { ...use p... }` shape too: the success-conditional is
the same silent skip written the other way round.

The one exception is a path that **already fails loudly** — `die()`,
`SSVM_Abort()`, `abort()`, `exit()`. Those carry a better message than an
assert would; leave them.

When a compound guard mixes an allocation with something else, split it, and
mind the order — an assert must precede a guard that reads the pointer:

```c
data = malloc(size);
assert(data);
if( fread(data, 1, size, f) != size )
    return -1;
```

### Do not write tests that pin silent-failure behaviour

`TEST_ASSERT(f(NULL) == 0, "f tolerates NULL")` freezes the exact habit above.
If NULL is a contract violation, delete the line rather than keeping the guard
alive to satisfy it.

## Never mutate source in this working tree to prove a test fails

Several sessions build from this checkout at once, into shared object
directories (`src/build_opt_es`, `src/build_opt`, ...). A mutation check that
edits a real file, runs a test, and restores it leaves a window in which any
other build compiles the mutant. The restore does not undo that: the object
can finish after the restore, and GNU Make 3.81 here compares whole-second
mtimes, so the object never looks stale again.

This happened on 2026-09-16. A check deleted the blank-line rule from
`3rd/toridraw/toridraw_font.c` for eight seconds; a concurrent build compiled
`src/build_opt_es/toridraw_unity.o` from it in the same second as the restore,
and every later `./launch` linked the mutant. The chat filter labels overlapped
"On" on every renderer while the source, the tests, and every clean private
build said the bug was fixed.

```sh
# NO — mutates the tree other builds are reading.
sed -i '' 's/rule/(void)0/' 3rd/toridraw/toridraw_font.c && make test-x; git checkout 3rd/toridraw/toridraw_font.c

# YES — mutate a throwaway worktree; the shared tree never sees the mutant.
git worktree add --detach "$SCRATCH/mut" HEAD
cp 3rd/toridraw/toridraw_font.c src/ui/test/font_markup_test.c ...   # uncommitted work the test needs
( cd "$SCRATCH/mut" && <apply mutation> && make -C src PLATFORM_OBJ_BASE=build_mut test-x )
git worktree remove --force "$SCRATCH/mut"
```

The same holds for any temporary edit to a source file that is not the change
itself (a probe, a forced branch, a commented-out call): do it in a worktree,
or make it the committed change.

If a build ever disagrees with its source, delete the object and rebuild; do
not trust `make` to notice.

## No `switch` inside a protothread

A protothread is any function whose body sits between `PT_BEGIN(...)` and
`PT_END(...)` — the `PT_*` macros are how you recognise one (`PT_BEGIN`,
`PT_END`, `PT_YIELD`, `PT_EXIT`, `PT_WAIT_*`, `PT_TASK_AWAITSELF*`,
`PT_TASK_JOIN`, `TASK_AWAIT_STATE`, `DAT2_GROUP_AWAIT`; anything that expands
to one of those is a suspension point). `PT_BEGIN` opens a `switch` on the
saved resume point and every suspension point expands to a `case __LINE__:`
label. A `switch` written inside that body captures any label inside it, so
on resume the outer switch finds no case, skips the body, and the task reaches
`PT_END` as if it had finished — without stepping its child, with a landed
answer still in its item. That is not a compile error; it is a silent
use-after-free (`Task_AppPlaceholder_Run`, 2026-09-18).

So: **never write a `switch` anywhere between `PT_BEGIN` and `PT_END`**, even
one with no suspension point inside it, even one that compiles. Dispatch with
an `if`/`else` chain, or call a plain (non-PT) helper that contains the
`switch` and returns a plan, then do the awaits linearly. `make -C src
check-pt-switch` (tools/pt_switch_audit.py) must print `total 0`.
