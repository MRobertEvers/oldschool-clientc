import pygame
import sys

# --- Configuration ---
GRID_SIZE = 11   # Must be odd; center = GRID_SIZE // 2
CELL_SIZE = 40
LEVELS = 2
MARGIN = 50
FPS = 60
CENTER = GRID_SIZE // 2
RADIUS = CENTER  # equivalent to the hardcoded 25 in World3D

# Colors
COLOR_BG          = (30, 30, 30)
COLOR_MASKED      = (10, 10, 10)
COLOR_NOT_VISITED = (100, 100, 100)
COLOR_BASE        = (200, 200, 0)   # drawFront=False, drawBack=True
COLOR_DONE        = (0, 200, 0)     # drawFront=False, drawBack=False
COLOR_LOC_BORDER  = (0, 150, 255)
COLOR_EYE         = (255, 80, 80)


# ---------------------------------------------------------------------------
# LinkList / Linkable
# Faithful port of Client-TS/src/datastruct/LinkList.ts + Linkable.ts.
# Key behaviour: push() unlinks before inserting at tail, so re-pushing a node
# that is already in the list silently moves it to the tail (no duplicates).
# pop() removes from the head.  Net effect: FIFO queue with de-duplication.
# ---------------------------------------------------------------------------

class Linkable:
    def __init__(self):
        self.prev = None
        self.next = None

    def unlink(self):
        if self.prev is not None:
            self.prev.next = self.next
            if self.next is not None:
                self.next.prev = self.prev
            self.prev = None
            self.next = None


class LinkList:
    def __init__(self):
        self._sentinel = Linkable()
        self._sentinel.next = self._sentinel
        self._sentinel.prev = self._sentinel

    def push(self, node: Linkable) -> None:
        """Insert at tail.  If node is already linked, unlink first (moves to tail)."""
        if node.prev is not None:
            node.unlink()
        node.prev = self._sentinel.prev
        node.next = self._sentinel
        node.prev.next = node
        node.next.prev = node

    def pop(self) -> Linkable | None:
        """Remove and return the head node; None if empty."""
        node = self._sentinel.next
        if node is self._sentinel:
            return None
        node.unlink()
        return node

    def is_empty(self) -> bool:
        return self._sentinel.next is self._sentinel


# ---------------------------------------------------------------------------
# Data classes
# ---------------------------------------------------------------------------

class Loc:
    """
    Represents a multi-tile object (Sprite in TS).  Holds the rectangular tile
    footprint; `cycle` is used as a "already drawn this frame" marker.
    """
    def __init__(self, min_tile_x: int, max_tile_x: int,
                 min_tile_z: int, max_tile_z: int, level: int):
        self.min_tile_x = min_tile_x
        self.max_tile_x = max_tile_x
        self.min_tile_z = min_tile_z
        self.max_tile_z = max_tile_z
        self.level = level
        self.cycle: int = 0
        self.distance: int = 0

    def reset(self) -> None:
        self.cycle = 0


class Square(Linkable):
    """
    One tile cell on one level.  Extends Linkable so it can live directly
    inside the draw queue (intrusive list — no wrapper object needed).

    Structural fields (set once during grid construction, never reset):
      primary_count, locs[], primary_extend_dirs[], combined_extend_dirs

    State flags (reset at the start of every run() call):
      draw_front, draw_back, draw_primaries
    """
    MAX_PRIMARY = 5

    def __init__(self, level: int, x: int, z: int, mask: bool = True):
        super().__init__()
        self.level = level
        self.x = x
        self.z = z
        self.mask = mask               # False → tile is invisible / not processed

        # Structural
        self.primary_count: int = 0
        self.locs: list = [None] * self.MAX_PRIMARY
        self.primary_extend_dirs: list = [0] * self.MAX_PRIMARY
        self.combined_extend_dirs: int = 0

        # State
        self.draw_front: bool = False
        self.draw_back: bool = False
        self.draw_primaries: bool = False

    def reset_state(self) -> None:
        self.draw_front = False
        self.draw_back = False
        self.draw_primaries = False


# ---------------------------------------------------------------------------
# GridManager — owns the grid, locs, seed list, and the algorithm
# ---------------------------------------------------------------------------

class GridManager:
    def __init__(self):
        self.eye_x = CENTER
        self.eye_z = CENTER
        self.min_draw_x = 0
        self.max_draw_x = GRID_SIZE
        self.min_draw_z = 0
        self.max_draw_z = GRID_SIZE
        self.current_step: int = 0

        self.grid: dict = {}        # (level, x, z) -> Square
        self.all_locs: list = []    # all Loc objects

        # Seed sequence built once; each entry is (phase, level, x, z).
        # phase=1 → checkAdjacent starts True; phase=2 → starts False (one-shot bypass).
        self._seeds: list = []

        self._build_grid()
        self._seeds = self._build_seeds()

    # ------------------------------------------------------------------
    # Grid / loc construction (called once)
    # ------------------------------------------------------------------

    def _build_grid(self) -> None:
        for level in range(LEVELS):
            for x in range(GRID_SIZE):
                for z in range(GRID_SIZE):
                    # Same mask pattern as painter.py: center always visible,
                    # tiles where (x%4==0 and z%3==0) are masked out.
                    mask = (x == CENTER and z == CENTER) or not (x % 4 == 0 and z % 3 == 0)
                    self.grid[(level, x, z)] = Square(level, x, z, mask)

        # A few multi-tile locs to exercise the spanning logic.
        # Arguments: min_x, max_x, min_z, max_z, level
        self._add_loc(1, 3, 1, 2, 0)   # 3-wide × 2-tall, level 0
        self._add_loc(2, 3, 1, 3, 0)   # overlapping 2-wide × 3-tall, level 0
        self._add_loc(7, 8, 7, 8, 1)   # 2×2, level 1

    def _add_loc(self, min_x: int, max_x: int,
                 min_z: int, max_z: int, level: int) -> None:
        """
        Create one Loc and register it on every tile it covers.
        The spanning bitmask mirrors addModel() in World3D.ts:
          bit 0x1 → tile has a western neighbour in the same loc  (tx > min_x)
          bit 0x4 → tile has an eastern neighbour in the same loc  (tx < max_x)
          bit 0x8 → tile has a northern neighbour in the same loc  (tz > min_z)
          bit 0x2 → tile has a southern neighbour in the same loc  (tz < max_z)
        """
        loc = Loc(min_x, max_x, min_z, max_z, level)
        self.all_locs.append(loc)
        for tx in range(min_x, max_x + 1):
            for tz in range(min_z, max_z + 1):
                sq = self.grid.get((level, tx, tz))
                if sq is None or sq.primary_count >= Square.MAX_PRIMARY:
                    continue
                spans = 0
                if tx > min_x: spans |= 0x1
                if tx < max_x: spans |= 0x4
                if tz > min_z: spans |= 0x8
                if tz < max_z: spans |= 0x2
                sq.locs[sq.primary_count] = loc
                sq.primary_extend_dirs[sq.primary_count] = spans
                sq.combined_extend_dirs |= spans
                sq.primary_count += 1

    # ------------------------------------------------------------------
    # Seed sequence (mirrors the two spiral loops in World3D.draw)
    # ------------------------------------------------------------------

    def _build_seeds(self) -> list:
        """
        Emit (phase, level, x, z) tuples in the same order as World3D.draw.
        Phase 1 corresponds to checkAdjacent=True seeds; phase 2 to False.
        Duplicate (x,z) positions that arise when dx=0 or dz=0 are collapsed
        so that each unique tile appears at most once per (phase, level, dx, dz)
        iteration — equivalent to the TS behaviour where the second drawTile call
        for an already-processed tile is a harmless no-op.
        """
        seeds = []
        for phase in (1, 2):
            for level in range(LEVELS):
                for dx in range(-RADIUS, 1):
                    right_x = self.eye_x + dx
                    left_x  = self.eye_x - dx
                    if right_x < self.min_draw_x and left_x >= self.max_draw_x:
                        continue
                    for dz in range(-RADIUS, 1):
                        fwd_z = self.eye_z + dz
                        bwd_z = self.eye_z - dz

                        seen = set()

                        def _emit(cx, cz):
                            if (cx, cz) in seen:
                                return
                            seen.add((cx, cz))
                            seeds.append((phase, level, cx, cz))

                        if right_x >= self.min_draw_x:
                            if fwd_z >= self.min_draw_z:
                                _emit(right_x, fwd_z)
                            if bwd_z < self.max_draw_z:
                                _emit(right_x, bwd_z)
                        if left_x < self.max_draw_x:
                            if fwd_z >= self.min_draw_z:
                                _emit(left_x, fwd_z)
                            if bwd_z < self.max_draw_z:
                                _emit(left_x, bwd_z)
        return seeds

    # ------------------------------------------------------------------
    # Algorithm — flattened so each queue pop is one step
    # ------------------------------------------------------------------

    def run(self) -> None:
        """
        Re-execute the World3D.draw algorithm from scratch, stopping after
        exactly `current_step` queue pops so the visualisation can show any
        intermediate state.
        """
        # --- reset tile state and loc cycles ---
        for sq in self.grid.values():
            sq.reset_state()
        for loc in self.all_locs:
            loc.reset()

        cycle = 1           # "drawn this frame" marker
        tiles_remaining = 0
        queue = LinkList()

        # Phase 0: mark every visible tile active (mirrors the first loop in draw())
        for level in range(LEVELS):
            for x in range(self.min_draw_x, self.max_draw_x):
                for z in range(self.min_draw_z, self.max_draw_z):
                    sq = self.grid.get((level, x, z))
                    if sq and sq.mask:
                        sq.draw_front = True
                        sq.draw_back  = True
                        sq.draw_primaries = sq.primary_count > 0
                        tiles_remaining += 1

        seed_idx = 0
        check_adjacent = True   # persists across pops within one BFS run
        steps = 0

        while steps < self.current_step:

            # ----- seed the queue when it is empty -----
            if queue.is_empty():
                if tiles_remaining == 0:
                    break
                seeded = False
                while seed_idx < len(self._seeds):
                    phase, lv, sx, sz = self._seeds[seed_idx]
                    seed_idx += 1
                    sq = self.grid.get((lv, sx, sz))
                    if sq and sq.draw_front:
                        queue.push(sq)
                        check_adjacent = (phase == 1)
                        seeded = True
                        break
                if not seeded:
                    break       # all seeds exhausted

            # ----- pop one tile — this is one step -----
            tile = queue.pop()
            steps += 1

            if tile is None or not tile.draw_back:
                continue

            tx = tile.x
            tz = tile.z
            lv = tile.level

            # ================================================================
            # drawFront block (mirrors TS lines 1463-1689)
            # ================================================================
            if tile.draw_front:
                if check_adjacent:
                    # Level below must already be done.
                    # (TS: "level" here is the tile's level; level-1 is below it)
                    if lv > 0:
                        below = self.grid.get((lv - 1, tx, tz))
                        if below and below.draw_back:
                            continue

                    # West neighbour blocks if tile is on or east of the eye
                    # and that neighbour is still pending.
                    if tx <= self.eye_x and tx > self.min_draw_x:
                        adj = self.grid.get((lv, tx - 1, tz))
                        if adj and adj.draw_back and (
                                adj.draw_front or (tile.combined_extend_dirs & 0x1) == 0):
                            continue

                    # East neighbour blocks if tile is on or west of the eye
                    if tx >= self.eye_x and tx < self.max_draw_x - 1:
                        adj = self.grid.get((lv, tx + 1, tz))
                        if adj and adj.draw_back and (
                                adj.draw_front or (tile.combined_extend_dirs & 0x4) == 0):
                            continue

                    # North neighbour (lower z) blocks if tile is on or south of eye
                    if tz <= self.eye_z and tz > self.min_draw_z:
                        adj = self.grid.get((lv, tx, tz - 1))
                        if adj and adj.draw_back and (
                                adj.draw_front or (tile.combined_extend_dirs & 0x8) == 0):
                            continue

                    # South neighbour (higher z) blocks if tile is on or north of eye
                    if tz >= self.eye_z and tz < self.max_draw_z - 1:
                        adj = self.grid.get((lv, tx, tz + 1))
                        if adj and adj.draw_back and (
                                adj.draw_front or (tile.combined_extend_dirs & 0x2) == 0):
                            continue
                else:
                    # Phase-2 first pop: skip adjacency check once, then re-enable.
                    check_adjacent = True

                tile.draw_front = False

                # Push spanned neighbours towards the eye so multi-tile locs can
                # be unlocked as soon as all their tiles have draw_front cleared.
                # (TS lines 1660-1688)
                spans = tile.combined_extend_dirs
                if spans:
                    if tx < self.eye_x and (spans & 0x4):
                        adj = self.grid.get((lv, tx + 1, tz))
                        if adj and adj.draw_back:
                            queue.push(adj)
                    if tz < self.eye_z and (spans & 0x2):
                        adj = self.grid.get((lv, tx, tz + 1))
                        if adj and adj.draw_back:
                            queue.push(adj)
                    if tx > self.eye_x and (spans & 0x1):
                        adj = self.grid.get((lv, tx - 1, tz))
                        if adj and adj.draw_back:
                            queue.push(adj)
                    if tz > self.eye_z and (spans & 0x8):
                        adj = self.grid.get((lv, tx, tz - 1))
                        if adj and adj.draw_back:
                            queue.push(adj)

            # ================================================================
            # cornerSides block omitted (always 0 — no walls).
            #
            # drawPrimaries block (TS lines 1716-1837).
            # NOTE: this is OUTSIDE the drawFront block so it also runs on
            # tiles that were re-queued after their drawFront was already cleared
            # (e.g. pushed back by a loc draw).
            # ================================================================
            if tile.draw_primaries:
                tile.draw_primaries = False
                loc_buffer = []

                for i in range(tile.primary_count):
                    loc = tile.locs[i]
                    if loc is None or loc.cycle == cycle:
                        continue

                    # A loc is ready only when every tile it spans has cleared draw_front.
                    blocked = False
                    for lx in range(loc.min_tile_x, loc.max_tile_x + 1):
                        if blocked:
                            break
                        for lz in range(loc.min_tile_z, loc.max_tile_z + 1):
                            other = self.grid.get((lv, lx, lz))
                            if other and other.draw_front:
                                tile.draw_primaries = True   # re-arm; will retry
                                blocked = True
                                break

                    if not blocked:
                        # Farthest-first distance from eye (TS lines 1771-1785)
                        dist_x = max(self.eye_x - loc.min_tile_x,
                                     loc.max_tile_x - self.eye_x)
                        dz_a = self.eye_z - loc.min_tile_z
                        dz_b = loc.max_tile_z - self.eye_z
                        loc.distance = dist_x + max(dz_a, dz_b)
                        loc_buffer.append(loc)

                # Draw locs farthest-first (model.draw() omitted; state tracking kept).
                # (TS lines 1789-1831)
                remaining = list(loc_buffer)
                while remaining:
                    farthest = None
                    best = -50
                    for l in remaining:
                        if l.cycle != cycle and l.distance > best:
                            best = l.distance
                            farthest = l
                    if farthest is None:
                        break

                    farthest.cycle = cycle
                    remaining.remove(farthest)

                    # Push every tile in the loc's footprint back into the queue
                    # (except the current tile — it is handled by the final gate).
                    for lx in range(farthest.min_tile_x, farthest.max_tile_x + 1):
                        for lz in range(farthest.min_tile_z, farthest.max_tile_z + 1):
                            occ = self.grid.get((lv, lx, lz))
                            if occ and (lx != tx or lz != tz) and occ.draw_back:
                                queue.push(occ)

                if tile.draw_primaries:
                    continue    # some loc still blocked — revisit later

            # ================================================================
            # Final adjacency gate + DONE transition (TS lines 1839-1975)
            # ================================================================
            if not tile.draw_back:
                continue

            # All four cardinal neighbours towards the eye must be done.
            if tx <= self.eye_x and tx > self.min_draw_x:
                adj = self.grid.get((lv, tx - 1, tz))
                if adj and adj.draw_back:
                    continue

            if tx >= self.eye_x and tx < self.max_draw_x - 1:
                adj = self.grid.get((lv, tx + 1, tz))
                if adj and adj.draw_back:
                    continue

            if tz <= self.eye_z and tz > self.min_draw_z:
                adj = self.grid.get((lv, tx, tz - 1))
                if adj and adj.draw_back:
                    continue

            if tz >= self.eye_z and tz < self.max_draw_z - 1:
                adj = self.grid.get((lv, tx, tz + 1))
                if adj and adj.draw_back:
                    continue

            # Mark DONE
            tile.draw_back = False
            tiles_remaining -= 1

            # Propagate: level above + 4 cardinal neighbours towards the eye.
            if lv < LEVELS - 1:
                above = self.grid.get((lv + 1, tx, tz))
                if above and above.draw_back:
                    queue.push(above)

            if tx < self.eye_x:
                adj = self.grid.get((lv, tx + 1, tz))
                if adj and adj.draw_back:
                    queue.push(adj)

            if tz < self.eye_z:
                adj = self.grid.get((lv, tx, tz + 1))
                if adj and adj.draw_back:
                    queue.push(adj)

            if tx > self.eye_x:
                adj = self.grid.get((lv, tx - 1, tz))
                if adj and adj.draw_back:
                    queue.push(adj)

            if tz > self.eye_z:
                adj = self.grid.get((lv, tx, tz - 1))
                if adj and adj.draw_back:
                    queue.push(adj)


# ---------------------------------------------------------------------------
# Pygame visualisation
# ---------------------------------------------------------------------------

def main():
    pygame.init()
    width  = (GRID_SIZE * CELL_SIZE + MARGIN * 2) * LEVELS
    height = GRID_SIZE * CELL_SIZE + MARGIN * 2
    screen = pygame.display.set_mode((width, height))
    pygame.display.set_caption("World3D Draw Algorithm — step-through")
    clock  = pygame.time.Clock()
    font   = pygame.font.SysFont(None, 24)

    manager = GridManager()
    manager.current_step = 0
    manager.run()

    right_down = False
    left_down  = False

    while True:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                pygame.quit()
                sys.exit()
            if event.type == pygame.KEYDOWN:
                if event.key == pygame.K_RIGHT:
                    right_down = True
                elif event.key == pygame.K_LEFT:
                    left_down = True
            if event.type == pygame.KEYUP:
                if event.key == pygame.K_RIGHT:
                    right_down = False
                elif event.key == pygame.K_LEFT:
                    left_down = False

        if right_down:
            manager.current_step += 1
            manager.run()
        if left_down and manager.current_step > 0:
            manager.current_step -= 1
            manager.run()

        screen.fill(COLOR_BG)

        for level in range(LEVELS):
            off_x = MARGIN + level * (GRID_SIZE * CELL_SIZE + MARGIN)
            off_y = MARGIN

            # --- 1x1 tiles ---
            for x in range(GRID_SIZE):
                for z in range(GRID_SIZE):
                    sq = manager.grid.get((level, x, z))
                    if sq is None:
                        continue
                    rect = (off_x + x * CELL_SIZE,
                            off_y + z * CELL_SIZE,
                            CELL_SIZE - 2, CELL_SIZE - 2)
                    if not sq.mask:
                        color = COLOR_MASKED
                    elif not sq.draw_front and not sq.draw_back:
                        color = COLOR_DONE
                    elif not sq.draw_front and sq.draw_back:
                        color = COLOR_BASE
                    else:
                        color = COLOR_NOT_VISITED
                    pygame.draw.rect(screen, color, rect)

            # --- multi-tile locs ---
            for loc in manager.all_locs:
                if loc.level != level:
                    continue
                lx = off_x + loc.min_tile_x * CELL_SIZE
                lz = off_y + loc.min_tile_z * CELL_SIZE
                lw = (loc.max_tile_x - loc.min_tile_x + 1) * CELL_SIZE - 2
                lh = (loc.max_tile_z - loc.min_tile_z + 1) * CELL_SIZE - 2

                # Fill blue when every tile in the footprint has draw_front cleared
                all_front_cleared = all(
                    not manager.grid[(level, lx2, lz2)].draw_front
                    for lx2 in range(loc.min_tile_x, loc.max_tile_x + 1)
                    for lz2 in range(loc.min_tile_z, loc.max_tile_z + 1)
                    if (level, lx2, lz2) in manager.grid
                )
                if all_front_cleared:
                    surf = pygame.Surface((lw, lh), pygame.SRCALPHA)
                    surf.fill((0, 150, 255, 100))
                    screen.blit(surf, (lx, lz))
                pygame.draw.rect(screen, COLOR_LOC_BORDER, (lx, lz, lw, lh), 3)

            # --- eye marker ---
            ex = off_x + manager.eye_x * CELL_SIZE + CELL_SIZE // 2
            ez = off_y + manager.eye_z * CELL_SIZE + CELL_SIZE // 2
            pygame.draw.circle(screen, COLOR_EYE, (ex, ez), 5)

            # --- level label ---
            lbl = font.render(f"Level {level}", True, (180, 180, 180))
            screen.blit(lbl, (off_x, off_y - 20))

        # --- HUD ---
        hud = font.render(
            f"Step: {manager.current_step}  |  LEFT / RIGHT arrows to step  "
            f"|  gray=unvisited  yellow=base  green=done  blue=loc drawn",
            True, (200, 200, 200))
        screen.blit(hud, (10, 10))

        pygame.display.flip()
        clock.tick(FPS)


if __name__ == "__main__":
    main()
