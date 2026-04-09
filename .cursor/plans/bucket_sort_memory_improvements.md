This is a classic optimization problem for software rasterizers. The current implementation uses a **Fixed-Grid Bucket Sort**, which is simple but suffers from extreme memory waste and risk of overflow.

To fix this, we will move to a **Linked-List Bucket Sort** (also known as a Chain Sort). This is the exact method used by the original RuneScape engine and is highly efficient for sorting small integers (depths) in linear time.

---

## 1. Context
The current `tmp_depth_faces` is a $1500 \times 512$ matrix. It assumes that no more than 512 faces will ever exist at a single depth unit. If you have a flat plane facing the camera, you might exceed 512 faces and cause a buffer overflow. Simultaneously, if a model only has 100 faces, you are still carrying the memory footprint of 768,000 potential faces.

The **Linked-List model** replaces the fixed "width" of the buckets with a dynamic chain. Each depth bucket only stores the index of the *first* face, and each face knows who the *next* face in the bucket is.

---

## 2. Scope of Changes

### `struct DashGraphics`
* **Remove:** `tmp_depth_faces[1500 * 512]` (~1.5MB - 3MB).
* **Add:** `int bucket_heads[1500]` — Stores the "entry point" for each depth.
* **Add:** `int face_links[4096]` — A parallel array to your faces that stores the "pointer" to the next face in the same depth bucket.

### `bucket_sort_by_average_depth`
* Instead of calculating a `bucket_index` using `face_depth_bucket_counts`, you will perform a **Head Insertion**.
* The new face becomes the new "Head," and its `face_links` value points to the previous "Head."

### `dash3d_sort_face_draw_order`
* The iteration logic changes from a nested loop to a `while` loop that traverses the chain for each depth.

---

## 3. Diagram of Changes



### Memory Layout Comparison
| Strategy | Memory Formula | 4096 Faces / 1500 Depth |
| :--- | :--- | :--- |
| **Current (Fixed)** | $Depth \times MaxWidth$ | **768,000 slots** |
| **Proposed (Linked)** | $Depth + MaxFaces$ | **5,596 slots** |

---

## 4. TODO List

### Phase 1: Header and Struct Updates
* [ ] Update `struct DashGraphics` in `dash.h` (or the local struct definition). 
    * Replace `tmp_depth_faces` with `int bucket_heads[1500]`.
    * Replace `tmp_depth_face_count` with `int face_links[4096]` (or whatever your max face constant is).
* [ ] Ensure `faceint_t` is consistent with these new index arrays.

### Phase 2: Logic Refactor
* [ ] **Update `bucket_sort`**: 
    * Change the signature to remove the pointer to the old 2D array.
    * Implementation:
      ```c
      // Inside the loop after calculating depth_average:
      dash->face_links[f] = dash->bucket_heads[depth_average];
      dash->bucket_heads[depth_average] = f;
      ```
* [ ] **Update `dash3d_sort_face_draw_order`**:
    * Replace the inner loop:
      ```c
      int face_idx = dash->bucket_heads[depth];
      while (face_idx != -1) {
          // Add face_idx to draw order or priority partition
          // ...
          face_idx = dash->face_links[face_idx];
      }
      ```

### Phase 3: Cleanup and Validation
* [ ] **Initialize**: Ensure `bucket_heads` is `memset` to `-1` (not `0`) at the start of every frame, as `0` is a valid face index.
* [ ] **Remove Overflow Guards**: You no longer need to worry about the `512` faces-per-depth limit.
* [ ] **Verify Rendering Order**: Note that linked-list insertion is LIFO (Last-In, First-Out). If the order of faces *within the same depth* matters to you, you may need to adjust insertion, though for OSRS depth sorting, LIFO is usually fine.