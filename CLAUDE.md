# Project conventions

## Never return early because an input parameter is wrong — assert

A function that is handed something it cannot accept must **abort loudly**, not
return a neutral value and let the caller carry on. Use `assert()` from
`<assert.h>`. `NDEBUG` is never defined in this tree, so asserts are live in
every configuration, `OPT=1` included.

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

### What is *not* a contract violation

`NULL` is a legitimate value — keep the guard, and say why in a comment — when:

- **Deallocators.** `free(NULL)` is an idiom; `*Free`, `*Cleanup`, `*Destroy`,
  `*Release` must accept NULL.
- **Absent optional data.** An unrigged model has no `vertex_bones`; an
  untextured model has no `face_textures`; a checkbox may carry no label.
  Absent in, absent out.
- **A documented sentinel.** `dat2_id_wanted(NULL)` means "want every id";
  `db_column_of()` returns NULL for "no such column", answered with `-1`.
- **An optional subsystem.** `painter->occluders` is NULL-checked throughout;
  no occluders means nothing is occluded.
- **Total queries** whose every missing link already answers 0 — e.g. a
  height lookup on a headless `World` with no scene attached.

The test is whether a real caller can reach it with NULL as a *meaningful*
value. "A caller might be buggy" is not such a reason — that is the case the
assert exists to catch.

### Do not write tests that pin silent-failure behaviour

`TEST_ASSERT(f(NULL) == 0, "f tolerates NULL")` freezes the exact habit above.
If NULL is a contract violation, delete the line rather than keeping the guard
alive to satisfy it.
