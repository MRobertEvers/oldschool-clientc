# Scene interface rewrite plan

## Goals

- **Scene owns all `SceneElement` and their sub-objects.** Entities (NPCEntity, PlayerEntity) hold only a reference (id or non-owning pointer) to a scene-owned element.
- **Static vs dynamic:** Static elements are built once and never freed until scene teardown. Dynamic elements (NPCs, players) are created/removed at runtime; when an NPC is removed, the scene frees that element and its animation.

## Current state (brief)

- `SceneScenery` has two arrays: `elements` (static) and `dynamic_elements`, both storing `SceneElement` by value. Pushing does `memcpy`, so internal pointers (animation, dash_model, dash_position) are shared/copied inconsistently.
- NPC/player init in `gameproto_exec.c` **malloc**s a `SceneElement` and assigns to `entity->scene_element` but **never adds it to the scene**. Clearing an entity with `memset` never frees that element → leak.
- Element “id” is currently the index in the combined (static then dynamic) array. Dynamic array is “reset” by setting length to 0 without freeing any sub-objects.

---

## 1. Get element by Id

**Requirement:** Stable lookup by a unique id so entities can hold an id and resolve to the element.

**Design:**

- Introduce a **stable element id** that is unique for the lifetime of the scene.
  - **Static elements:** Id can remain index-based (0 .. static_count-1) if static elements are never removed.
  - **Dynamic elements:** Use a monotonic id (e.g. `next_dynamic_id++`) so that after a dynamic element is removed, other elements’ ids stay valid.
- **Storage:** Keep `elements[]` for static (by value). For dynamic, store **pointers** (`SceneElement** dynamic_elements`) so addresses don’t change when we remove one; each element is heap-allocated and owned by the scene.
- **Mapping id → element:**
  - Option A: Id = index for static; for dynamic, store `id` in each `SceneElement` and maintain a small map (e.g. id → pointer) or linear scan. If dynamic count is small, linear scan by id is fine.
  - Option B: Single id space: static ids [0, N), dynamic ids [N, N+M). Dynamic element struct holds `id`; scene keeps a list of dynamic element pointers and scans by id (or a hash map if needed).
- **API:**
  - `SceneElement* scene_element_by_id(struct Scene* scene, int id);`  
    Returns `NULL` if id invalid or element removed.

**Concrete:** Use `int id` in `SceneElement`. Static: id = index. Dynamic: assign from `scene->scenery->next_dynamic_id++`. `scene_element_by_id(scene, id)` checks static range first, then scans dynamic list by `element->id`.

---

## 2. Get tile by coordinate

**Requirement:** Get terrain tile by (sx, sz, slevel).

**Design:**

- Already have `scene_terrain_tile_at(terrain, sx, sz, slevel)`.
- Add a **scene-level** API so callers don’t need to reach into `scene->terrain`:
  - `struct SceneTerrainTile* scene_tile_at(struct Scene* scene, int sx, int sz, int slevel);`  
    Implement as wrapper: `return scene_terrain_tile_at(scene->terrain, sx, sz, slevel);`
- Optional: “elements at tile” — if needed, either:
  - Add `scene_elements_at_tile(scene, sx, sz, slevel, out_array, out_count)` that scans all elements (static + dynamic) where `element->tile_sx == sx && element->tile_sz == sz && element->tile_slevel == slevel`, or
  - Maintain a tile→element index (more complex; only add if required for perf).

Recommendation: implement `scene_tile_at`; add “elements at tile” only if you have a concrete use case.

---

## 3. Add an animation to an element (animation owned by element, element by scene)

**Requirement:** Animation memory is owned by the scene element; the scene element is owned by the scene.

**Design:**

- **Ownership:** `SceneElement` has `struct SceneAnimation* animation`. When the scene creates/owns the element, it also creates and owns the animation. When the element is freed (e.g. dynamic element removed), the scene frees the element’s animation then the element.
- **API:** Prefer scene-centric APIs so the scene is the single place that allocates:
  - `struct SceneAnimation* scene_element_add_animation(struct Scene* scene, int element_id, int frame_count_hint);`  
    Returns the new animation (owned by the element). If element already has an animation, either replace (and free old) or return existing — define policy (e.g. “one animation per element” and return existing or replace).
  - Alternatively: `scene_element_add_animation(scene, element_ptr, ...)` if you pass pointers from `scene_element_by_id` or `scene_element_at`.
- **Implementation:** Allocate `SceneAnimation` (as today in `scene_animation_new`), assign to `element->animation`. Do **not** expose a way for external code to assign `element->animation` to a non–scene-created animation; all animations for elements are created via this API (or equivalent during static build).

**Concrete:** Add `scene_element_add_animation(Scene* scene, int element_id, int frame_count_hint)`. It calls `scene_element_by_id`; if null return null; if element->animation already set, either free and replace or return existing (e.g. return existing). Else allocate animation, set `element->animation = anim`, return anim. When freeing a dynamic element, call `scene_animation_free(element->animation)` before freeing the element.

---

## 4. Add a frame to a scene animation

**Requirement:** Add a frame to an animation that belongs to a scene element.

**Design:**

- Keep existing `scene_animation_push_frame(animation, dash_frame, dash_framemap)`.
- Animation is always reached via the element (which is owned by the scene). So call flow is: get element by id → get element->animation (or create via add_animation) → call `scene_animation_push_frame(element->animation, dash_frame, dash_framemap)`.
- Optional convenience: `scene_element_animation_push_frame(scene, element_id, dash_frame, dash_framemap)` that does “get element → get animation → push frame” and returns success/fail if element or animation missing.

**Concrete:** No change to `SceneAnimation` or `scene_animation_push_frame`. Optionally add `scene_element_animation_push_frame(scene, element_id, dash_frame, dash_framemap)` for convenience.

---

## 5. Update and read the dash fields of elements

**Requirement:** Read and update dash-related fields (e.g. `dash_model`, `dash_position`) on scene elements.

**Design:**

- **Read:** Already have `scene_element_model`, `scene_element_position`, `scene_element_name`. Keep these; they can take `(scene, element_id)` and resolve via `scene_element_by_id`, or keep index-based if you still support index.
- **Write:** Two options:
  - **Accessor:** Return non-owning pointer so caller can mutate: `struct DashPosition* scene_element_position(scene, element_id);` caller does `scene_element_position(scene, id)->x = ...`. Document that the pointer is valid only while the element exists and must not be freed by the caller.
  - **Setters:** `scene_element_set_position(scene, element_id, x, z, height)` etc. Use when you want to keep dash types opaque or enforce invariants.
- **Ownership of dash data:** Today `dash_model` and `dash_position` are often created outside the scene (e.g. in game code). For scene-owned elements, either:
  - Scene allocates and owns `DashPosition` (and optionally `DashModel`) when creating the element, or
  - Scene accepts external `DashModel*`/`DashPosition*` but does **not** free them (they’re owned by cache/game). For dynamic NPC elements, it’s reasonable for the scene to **own** the `DashPosition` (so when we free the element we free its `dash_position`) and to **reference** an external `DashModel` (cache-owned). Clarify in implementation.

**Concrete:** Keep read API; allow write by returning non-owning pointer (preferred for flexibility) or add setters. When freeing a dynamic element, free `element->dash_position` if the scene owns it; do not free `element->dash_model` if it’s cache-owned.

---

## 6. When an NPC is removed, free the element memory

**Requirement:** Removing an NPC must free the corresponding dynamic scene element (and its animation, and any scene-owned dash data).

**Design:**

- **Scene owns dynamic elements.** Creating an NPC should:
  - Ask the scene for a new dynamic element: e.g. `int element_id = scene_add_dynamic_element(scene, tile_sx, tile_sz, tile_slevel, ...);`  
    Scene allocates `SceneElement`, sets id, adds to dynamic list, returns id.
  - Entity stores `scene_element_id` (or `scene_element` pointer; if pointer, document that it’s invalid after removal).
- **Removing an NPC:** Before clearing the entity (e.g. `memset(&game->npcs[npc_id], 0, ...)`):
  - Call `scene_remove_dynamic_element(scene, element_id)` (or by pointer).
  - Scene: finds the dynamic element, frees `element->animation` (if any), frees `element->dash_position` if scene-owned, frees the `SceneElement`, removes it from the dynamic list (set slot to NULL or compact), invalidates any stored pointer for that id.
  - Then clear the entity so it no longer holds the id/pointer.

**Dynamic storage:** Use an array of **pointers** (`SceneElement** dynamic_elements`) so that:
- Adding: realloc array if needed, allocate one `SceneElement`, set id, store pointer, return id.
- Removing: find pointer by id (or index if you store id→index), free element and its animation/dash_position, set slot to NULL (and optionally compact to avoid many NULLs).

**Concrete:** Add `scene_add_dynamic_element(scene, ...)` returning `int element_id`. Add `scene_remove_dynamic_element(scene, element_id)`. In game code, on NPC add: get element_id from scene, store in entity (e.g. `npc_entity->scene_element_id` or keep `void* scene_element` and store the pointer returned by `scene_element_by_id` after add). On NPC remove: call `scene_remove_dynamic_element(scene, npc_entity->scene_element_id)`, then clear entity. Same pattern for players if they use dynamic elements.

---

## Summary of API changes (scene.h / scene.c)

| Requirement              | New or changed API |
|--------------------------|---------------------|
| 1. Get element by id     | `SceneElement* scene_element_by_id(Scene* scene, int id);` |
| 2. Get tile by coord     | `SceneTerrainTile* scene_tile_at(Scene* scene, int sx, int sz, int slevel);` |
| 3. Add animation         | `SceneAnimation* scene_element_add_animation(Scene* scene, int element_id, int frame_count_hint);` |
| 4. Add frame to animation| Keep `scene_animation_push_frame`; optional `scene_element_animation_push_frame(scene, element_id, ...)` |
| 5. Read/write dash       | Keep read accessors; allow write via returned pointer or add setters; document ownership |
| 6. NPC removed → free   | `void scene_remove_dynamic_element(Scene* scene, int element_id);` and `int scene_add_dynamic_element(Scene* scene, ...);` |

**Data structure changes:**

- `SceneElement` has stable `int id`.
- `SceneScenery`: static `elements[]` unchanged (by value). Dynamic: change to `SceneElement** dynamic_elements`, each allocated by scene. Add `int next_dynamic_id` (or similar).
- When freeing scene: free each dynamic element (animation, dash_position if owned, then element), then free the dynamic_elements array. Static elements: free any scene-owned sub-objects (e.g. animations) then free array.

**Entity side (game_entity.h / gameproto_exec.c):**

- Store either `int scene_element_id` or keep `void* scene_element` and treat as non-owning; on NPC/player clear, call `scene_remove_dynamic_element` before memset.
- Create NPC/player scene representation via `scene_add_dynamic_element` and optionally `scene_element_add_animation` / `scene_animation_push_frame`, then store returned id (or pointer from `scene_element_by_id(id)`).

This plan keeps the scene as the single owner of all `SceneElement` and their animations, supports stable get-by-id, tile lookup, and safe removal of dynamic elements when entities are removed.
