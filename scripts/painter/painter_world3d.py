import pygame
import sys
import math

# --- Configuration ---
GRID_SIZE = 11   # Must be odd; center = GRID_SIZE // 2
CELL_SIZE = 40
LEVELS = 2
MARGIN = 50
FPS = 60
CENTER = GRID_SIZE // 2
RADIUS = 10  # equivalent to the hardcoded 25 in World3D

FOV_DEG     = 90.0   # full cone angle
ROTATE_STEP = 5      # degrees per E/R step (fine 0-360 rotation)

# Colors
COLOR_BG          = (30, 30, 30)
COLOR_MASKED      = (10, 10, 10)
COLOR_NOT_VISITED = (100, 100, 100)
COLOR_BASE        = (200, 200, 0)   # drawFront=False, drawBack=True
COLOR_DONE        = (0, 200, 0)     # drawFront=False, drawBack=False
COLOR_LOC_BORDER  = (0, 150, 255)
COLOR_EYE         = (255, 80, 80)
COLOR_FOV         = (200, 100, 50)


# ---------------------------------------------------------------------------
# Frustum helpers
# ---------------------------------------------------------------------------

def tile_in_frustum(tx, tz, eye_x, eye_z, yaw_deg):
    """Return True if tile (tx, tz) falls inside the view cone."""
    dx, dz = tx - eye_x, tz - eye_z
    if dx == 0 and dz == 0:
        return True
    dist = math.sqrt(dx * dx + dz * dz)
    face_x =  math.sin(math.radians(yaw_deg))
    face_z = -math.cos(math.radians(yaw_deg))
    dot = (dx / dist) * face_x + (dz / dist) * face_z
    angle = math.degrees(math.acos(max(-1.0, min(1.0, dot))))
    return angle <= FOV_DEG / 2.0


def move_eye(eye_x, eye_z, key):
    """Move the eye one tile: W=up, S=down, A=left, D=right (grid-aligned)."""
    deltas = {'W': (0, -1), 'S': (0, 1), 'A': (-1, 0), 'D': (1, 0)}
    ddx, ddz = deltas[key]
    new_x = max(0, min(GRID_SIZE - 1, eye_x + ddx))
    new_z = max(0, min(GRID_SIZE - 1, eye_z + ddz))
    return new_x, new_z


def draw_frustum(screen, off_x, off_y, eye_x, eye_z, yaw_deg):
    """Draw facing arrow + FOV boundary lines for one level panel."""
    cx = off_x + eye_x * CELL_SIZE + CELL_SIZE // 2
    cz = off_y + eye_z * CELL_SIZE + CELL_SIZE // 2

    arrow_len = CELL_SIZE * 1.5
    fx =  math.sin(math.radians(yaw_deg))
    fz = -math.cos(math.radians(yaw_deg))
    tip = (int(cx + fx * arrow_len), int(cz + fz * arrow_len))
    pygame.draw.line(screen, COLOR_EYE, (cx, cz), tip, 2)

    fov_len = RADIUS * CELL_SIZE
    for sign in (-1, 1):
        angle = yaw_deg + sign * FOV_DEG / 2.0
        lx =  math.sin(math.radians(angle))
        lz = -math.cos(math.radians(angle))
        end = (int(cx + lx * fov_len), int(cz + lz * fov_len))
        pygame.draw.line(screen, COLOR_FOV, (cx, cz), end, 1)


# ---------------------------------------------------------------------------
# LinkList / Linkable
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
        if node.prev is not None:
            node.unlink()
        node.prev = self._sentinel.prev
        node.next = self._sentinel
        node.prev.next = node
        node.next.prev = node

    def pop(self) -> Linkable | None:
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
    MAX_PRIMARY = 5

    def __init__(self, level: int, x: int, z: int, mask: bool = True):
        super().__init__()
        self.level = level
        self.x = x
        self.z = z
        self.mask = mask

        self.primary_count: int = 0
        self.locs: list = [None] * self.MAX_PRIMARY
        self.primary_extend_dirs: list = [0] * self.MAX_PRIMARY
        self.combined_extend_dirs: int = 0

        self.draw_front: bool = False
        self.draw_back: bool = False
        self.draw_primaries: bool = False

    def reset_state(self) -> None:
        self.draw_front = False
        self.draw_back = False
        self.draw_primaries = False


# ---------------------------------------------------------------------------
# GridManager
# ---------------------------------------------------------------------------

class GridManager:
    def __init__(self):
        self.eye_x   = CENTER
        self.eye_z   = CENTER
        self.yaw_deg = 0.0

        self.min_draw_x = 0
        self.max_draw_x = GRID_SIZE
        self.min_draw_z = 0
        self.max_draw_z = GRID_SIZE
        self.current_step: int = 0

        self.grid: dict = {}
        self.all_locs: list = []

        self._build_grid()

    # ------------------------------------------------------------------
    # Grid construction (called once — masks are updated dynamically)
    # ------------------------------------------------------------------

    def _build_grid(self) -> None:
        for level in range(LEVELS):
            for x in range(GRID_SIZE):
                for z in range(GRID_SIZE):
                    self.grid[(level, x, z)] = Square(level, x, z, mask=True)

        self._add_loc(1, 3, 1, 2, 0)
        self._add_loc(2, 3, 1, 3, 0)
        self._add_loc(6, 9, 4, 7, 0)
        self._add_loc(7, 8, 7, 8, 1)

    def _add_loc(self, min_x: int, max_x: int,
                 min_z: int, max_z: int, level: int) -> None:
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
    # Dynamic mask update — recomputed each run() from current frustum
    # ------------------------------------------------------------------

    def _update_masks(self) -> None:
        for sq in self.grid.values():
            sq.mask = tile_in_frustum(sq.x, sq.z, self.eye_x, self.eye_z, self.yaw_deg)

    # ------------------------------------------------------------------
    # Seed sequence — rebuilt each run() because eye_x/eye_z can change
    # ------------------------------------------------------------------

    def _build_seeds(self) -> list:
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
    # Algorithm
    # ------------------------------------------------------------------

    def run(self) -> None:
        # Recompute draw range and masks for current eye position
        self.min_draw_x = max(0, self.eye_x - RADIUS)
        self.max_draw_x = min(GRID_SIZE, self.eye_x + RADIUS + 1)
        self.min_draw_z = max(0, self.eye_z - RADIUS)
        self.max_draw_z = min(GRID_SIZE, self.eye_z + RADIUS + 1)

        self._update_masks()

        for sq in self.grid.values():
            sq.reset_state()
        for loc in self.all_locs:
            loc.reset()

        seeds = self._build_seeds()

        cycle = 1
        tiles_remaining = 0
        queue = LinkList()

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
        check_adjacent = True
        steps = 0

        while steps < self.current_step:

            if queue.is_empty():
                if tiles_remaining == 0:
                    break
                seeded = False
                while seed_idx < len(seeds):
                    phase, lv, sx, sz = seeds[seed_idx]
                    seed_idx += 1
                    sq = self.grid.get((lv, sx, sz))
                    if sq and sq.draw_front:
                        queue.push(sq)
                        check_adjacent = (phase == 1)
                        seeded = True
                        break
                if not seeded:
                    break

            tile = queue.pop()
            steps += 1

            if tile is None or not tile.draw_back:
                continue

            tx = tile.x
            tz = tile.z
            lv = tile.level

            if tile.draw_front:
                if check_adjacent:
                    if lv > 0:
                        below = self.grid.get((lv - 1, tx, tz))
                        if below and below.draw_back:
                            continue

                    if tx <= self.eye_x and tx > self.min_draw_x:
                        adj = self.grid.get((lv, tx - 1, tz))
                        if adj and adj.draw_back and (
                                adj.draw_front or (tile.combined_extend_dirs & 0x1) == 0):
                            continue

                    if tx >= self.eye_x and tx < self.max_draw_x - 1:
                        adj = self.grid.get((lv, tx + 1, tz))
                        if adj and adj.draw_back and (
                                adj.draw_front or (tile.combined_extend_dirs & 0x4) == 0):
                            continue

                    if tz <= self.eye_z and tz > self.min_draw_z:
                        adj = self.grid.get((lv, tx, tz - 1))
                        if adj and adj.draw_back and (
                                adj.draw_front or (tile.combined_extend_dirs & 0x8) == 0):
                            continue

                    if tz >= self.eye_z and tz < self.max_draw_z - 1:
                        adj = self.grid.get((lv, tx, tz + 1))
                        if adj and adj.draw_back and (
                                adj.draw_front or (tile.combined_extend_dirs & 0x2) == 0):
                            continue
                else:
                    check_adjacent = True

                tile.draw_front = False

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

            if tile.draw_primaries:
                tile.draw_primaries = False
                loc_buffer = []

                for i in range(tile.primary_count):
                    loc = tile.locs[i]
                    if loc is None or loc.cycle == cycle:
                        continue

                    blocked = False
                    for lx in range(loc.min_tile_x, loc.max_tile_x + 1):
                        if blocked:
                            break
                        for lz in range(loc.min_tile_z, loc.max_tile_z + 1):
                            other = self.grid.get((lv, lx, lz))
                            if other and other.draw_front:
                                tile.draw_primaries = True
                                blocked = True
                                break

                    if not blocked:
                        dist_x = max(self.eye_x - loc.min_tile_x,
                                     loc.max_tile_x - self.eye_x)
                        dz_a = self.eye_z - loc.min_tile_z
                        dz_b = loc.max_tile_z - self.eye_z
                        loc.distance = dist_x + max(dz_a, dz_b)
                        loc_buffer.append(loc)

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

                    for lx in range(farthest.min_tile_x, farthest.max_tile_x + 1):
                        for lz in range(farthest.min_tile_z, farthest.max_tile_z + 1):
                            occ = self.grid.get((lv, lx, lz))
                            if occ and (lx != tx or lz != tz) and occ.draw_back:
                                queue.push(occ)

                if tile.draw_primaries:
                    continue

            if not tile.draw_back:
                continue

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

            tile.draw_back = False
            tiles_remaining -= 1

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
    w_down = s_down = a_down = d_down = e_down = r_down = False
    MOVE_COOLDOWN = 100  # ms between eye/rotation steps when held
    last_move_ms  = 0

    while True:
        eye_changed = False
        now = pygame.time.get_ticks()

        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                pygame.quit()
                sys.exit()

            if event.type == pygame.KEYDOWN:
                if event.key == pygame.K_RIGHT:
                    right_down = True
                elif event.key == pygame.K_LEFT:
                    left_down = True
                elif event.key == pygame.K_w: w_down = True
                elif event.key == pygame.K_s: s_down = True
                elif event.key == pygame.K_a: a_down = True
                elif event.key == pygame.K_d: d_down = True
                elif event.key == pygame.K_e: e_down = True
                elif event.key == pygame.K_r: r_down = True

            if event.type == pygame.KEYUP:
                if event.key == pygame.K_RIGHT:
                    right_down = False
                elif event.key == pygame.K_LEFT:
                    left_down = False
                elif event.key == pygame.K_w: w_down = False
                elif event.key == pygame.K_s: s_down = False
                elif event.key == pygame.K_a: a_down = False
                elif event.key == pygame.K_d: d_down = False
                elif event.key == pygame.K_e: e_down = False
                elif event.key == pygame.K_r: r_down = False

        if now - last_move_ms >= MOVE_COOLDOWN:
            if w_down:
                manager.eye_x, manager.eye_z = move_eye(
                    manager.eye_x, manager.eye_z, 'W')
                eye_changed = True
            elif s_down:
                manager.eye_x, manager.eye_z = move_eye(
                    manager.eye_x, manager.eye_z, 'S')
                eye_changed = True
            elif a_down:
                manager.eye_x, manager.eye_z = move_eye(
                    manager.eye_x, manager.eye_z, 'A')
                eye_changed = True
            elif d_down:
                manager.eye_x, manager.eye_z = move_eye(
                    manager.eye_x, manager.eye_z, 'D')
                eye_changed = True
            if e_down:
                manager.yaw_deg = (manager.yaw_deg + ROTATE_STEP) % 360
                eye_changed = True
            elif r_down:
                manager.yaw_deg = (manager.yaw_deg - ROTATE_STEP) % 360
                eye_changed = True
            if eye_changed:
                last_move_ms = now

        if eye_changed:
            manager.current_step = 0
            manager.run()

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

            for loc in manager.all_locs:
                if loc.level != level:
                    continue
                lx = off_x + loc.min_tile_x * CELL_SIZE
                lz = off_y + loc.min_tile_z * CELL_SIZE
                lw = (loc.max_tile_x - loc.min_tile_x + 1) * CELL_SIZE - 2
                lh = (loc.max_tile_z - loc.min_tile_z + 1) * CELL_SIZE - 2
                if lw <= 0 or lh <= 0:
                    continue

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

            draw_frustum(screen, off_x, off_y,
                         manager.eye_x, manager.eye_z, manager.yaw_deg)

            lbl = font.render(f"Level {level}", True, (180, 180, 180))
            screen.blit(lbl, (off_x, off_y - 20))

        hud = font.render(
            f"Step: {manager.current_step}  |  "
            f"←/→ step  WASD move  E/R rotate  "
            f"yaw={manager.yaw_deg:.0f}°  eye=({manager.eye_x},{manager.eye_z})",
            True, (200, 200, 200))
        screen.blit(hud, (10, 10))

        pygame.display.flip()
        clock.tick(FPS)


if __name__ == "__main__":
    main()
