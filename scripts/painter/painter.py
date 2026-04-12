import pygame
import sys
import heapq
import math

# --- Configuration ---
GRID_SIZE = 11  # Must be odd for a clear center
CELL_SIZE = 40
LEVELS = 2
MARGIN = 50
FPS = 60
CENTER = GRID_SIZE // 2

FOV_DEG     = 90.0   # full cone angle
ROTATE_STEP = 5      # degrees per E/R step (fine 0-360 rotation)

# Colors
COLOR_BG         = (30, 30, 30)
COLOR_GRID       = (60, 60, 60)
COLOR_MASKED     = (10, 10, 10)
COLOR_NOT_VISITED = (100, 100, 100)
COLOR_BASE       = (200, 200, 0)    # Yellow
COLOR_DONE       = (0, 200, 0)     # Green
COLOR_SCENERY = (0, 150, 255)   # Blue
COLOR_EYE        = (255, 80, 80)
COLOR_FOV        = (200, 100, 50)


# ---------------------------------------------------------------------------
# Frustum helpers — precomputed bitmask (bit index = y * GRID_SIZE + x)
# ---------------------------------------------------------------------------

def compute_frustum_mask(eye_x: float, eye_y: float, yaw_deg: float) -> int:
    """Build an int bitmask: one bit per tile visible in the view cone."""
    mask = 0
    half_fov = FOV_DEG / 2.0
    face_x = math.sin(math.radians(yaw_deg))
    face_z = -math.cos(math.radians(yaw_deg))
    for y in range(GRID_SIZE):
        for x in range(GRID_SIZE):
            dx, dz = x - eye_x, y - eye_y
            bit = 1 << (y * GRID_SIZE + x)
            if dx == 0 and dz == 0:
                mask |= bit
                continue
            dist = math.sqrt(dx * dx + dz * dz)
            dot = (dx / dist) * face_x + (dz / dist) * face_z
            angle = math.degrees(math.acos(max(-1.0, min(1.0, dot))))
            if angle <= half_fov:
                mask |= bit
    return mask


def tile_visible_in_frustum_mask(frustum_mask: int, x: int, y: int) -> bool:
    return bool((frustum_mask >> (y * GRID_SIZE + x)) & 1)


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

    # Facing arrow (1.5 tile lengths)
    arrow_len = CELL_SIZE * 1.5
    fx =  math.sin(math.radians(yaw_deg))
    fz = -math.cos(math.radians(yaw_deg))
    tip = (int(cx + fx * arrow_len), int(cz + fz * arrow_len))
    pygame.draw.line(screen, COLOR_EYE, (cx, cz), tip, 2)

    # FOV boundary lines (RADIUS tile lengths)
    fov_len = CENTER * CELL_SIZE
    for sign in (-1, 1):
        angle = yaw_deg + sign * FOV_DEG / 2.0
        lx =  math.sin(math.radians(angle))
        lz = -math.cos(math.radians(angle))
        end = (int(cx + lx * fov_len), int(cz + lz * fov_len))
        pygame.draw.line(screen, COLOR_FOV, (cx, cz), end, 1)


# ---------------------------------------------------------------------------
# Data structures
# ---------------------------------------------------------------------------

class TileStep:
    NOT_VISITED = 0
    BASE = 1
    DONE = 2

class Scenery:
    def __init__(self, x, y, w, h, level):
        self.rect = pygame.Rect(x, y, w, h)
        self.level = level
        self.step = TileStep.NOT_VISITED
        self.tiles_under = []


class Tile:
    def __init__(self, x, y, level, mask=True, idx=0):
        self.x = x
        self.y = y
        self.level = level
        self.mask = mask
        self.step = TileStep.DONE if not mask else TileStep.NOT_VISITED
        self.dist = 0   # set by GridManager.reset() from current eye position
        self.idx = idx
        self.scenery_list = []
        self.combined_extend_dirs: int = 0
        self.in_heap = False

    def __lt__(self, other):
        return self.idx < other.idx


class GridManager:
    def __init__(self):
        self.eye_x   = CENTER
        self.eye_y   = CENTER
        self.yaw_deg = 0.0

        self.grid        = {}
        self.all_scenery = []
        self.tile_heap   = []
        heapq.heapify(self.tile_heap)
        self.current_step = 0

        self.reset()

    def reset(self):
        self.grid        = {}
        self.all_scenery = []
        self.tile_heap   = []
        heapq.heapify(self.tile_heap)

        frustum_mask = compute_frustum_mask(
            self.eye_x, self.eye_y, self.yaw_deg)

        # Build tiles — mask = view frustum from current eye
        tile_idx = 0
        for l in range(LEVELS):
            for y in range(GRID_SIZE):
                for x in range(GRID_SIZE):
                    mask = tile_visible_in_frustum_mask(frustum_mask, x, y)
                    self.grid[(x, y, l)] = Tile(x, y, l, mask, tile_idx)
                    tile_idx += 1

        # Set Manhattan distance from the current eye (heap priority)
        for t in self.grid.values():
            t.dist = abs(t.x - self.eye_x) + abs(t.y - self.eye_y)

        # Scenery (x, y, w, h, level)
        self.all_scenery.append(Scenery(1, 1, 3, 2, 0))
        self.all_scenery.append(Scenery(2, 1, 2, 3, 0))
        self.all_scenery.append(Scenery(7, 3, 4, 4, 0))
        self.all_scenery.append(Scenery(7, 7, 2, 2, 1))

        for sc in self.all_scenery:
            for tx in range(sc.rect.x, sc.rect.x + sc.rect.width):
                for ty in range(sc.rect.y, sc.rect.y + sc.rect.height):
                    t = self.grid.get((tx, ty, sc.level))
                    if t:
                        sc.tiles_under.append(t)
                        t.scenery_list.append(sc)
                        spans = 0
                        if tx > sc.rect.x:
                            spans |= 0x1   # west
                        if tx < sc.rect.x + sc.rect.width - 1:
                            spans |= 0x4   # east
                        if ty > sc.rect.y:
                            spans |= 0x8   # north
                        if ty < sc.rect.y + sc.rect.height - 1:
                            spans |= 0x2   # south
                        t.combined_extend_dirs |= spans

        for t in self.grid.values():
            if t.mask:
                heapq.heappush(self.tile_heap, (-t.dist, t))
                t.in_heap = True

    # ------------------------------------------------------------------
    # Direction helpers — relative to the current eye position
    # ------------------------------------------------------------------

    def is_north(self, t):
        return t.y < self.eye_y

    def is_east(self, t):
        return t.x > self.eye_x

    def is_south(self, t):
        return t.y > self.eye_y

    def is_west(self, t):
        return t.x < self.eye_x

    def get_neighbors_out(self, t):
        """Returns the two neighbours farther from the eye based on quadrant."""
        x, y, l = t.x, t.y, t.level
        neighbors = []
        dx = -1 if x < self.eye_x else (1 if x > self.eye_x else 0)
        dy = -1 if y < self.eye_y else (1 if y > self.eye_y else 0)
        if dx != 0:
            neighbors.append(self.grid.get((x + dx, y, l)))
        if dy != 0:
            neighbors.append(self.grid.get((x, y + dy, l)))
        return [n for n in neighbors if n]

    def _push(self, tile):
        """Push tile onto the heap only if it is not already there."""
        if not tile.in_heap:
            heapq.heappush(self.tile_heap, (-tile.dist, tile))
            tile.in_heap = True

    def run(self):
        """Re-run from scratch up to current_step pops."""
        self.reset()

        runstep = 0

        while self.tile_heap and runstep < self.current_step:
            runstep += 1
            dist, tile = heapq.heappop(self.tile_heap)
            tile.in_heap = False

            if tile.step == TileStep.DONE:
                continue

            if tile.step == TileStep.NOT_VISITED:
                if tile.level > 0:
                    neighbor = self.grid.get((tile.x, tile.y, tile.level - 1))
                    if neighbor and neighbor.step != TileStep.DONE:
                        continue

                if self.is_north(tile):
                    neighbor = self.grid.get((tile.x, tile.y - 1, tile.level))
                    if neighbor and neighbor.step != TileStep.DONE:
                        if (tile.combined_extend_dirs & 0x8) == 0:
                            continue

                if self.is_east(tile):
                    neighbor = self.grid.get((tile.x + 1, tile.y, tile.level))
                    if neighbor and neighbor.step != TileStep.DONE:
                        if (tile.combined_extend_dirs & 0x4) == 0:
                            continue

                if self.is_south(tile):
                    neighbor = self.grid.get((tile.x, tile.y + 1, tile.level))
                    if neighbor and neighbor.step != TileStep.DONE:
                        if (tile.combined_extend_dirs & 0x2) == 0:
                            continue

                if self.is_west(tile):
                    neighbor = self.grid.get((tile.x - 1, tile.y, tile.level))
                    if neighbor and neighbor.step != TileStep.DONE:
                        if (tile.combined_extend_dirs & 0x1) == 0:
                            continue

            tile.step = TileStep.BASE
            visit_sc = []
            for sc in tile.scenery_list:
                if sc.step == TileStep.DONE:
                    continue
                for tile_under in sc.tiles_under:
                    if tile_under.step < TileStep.BASE:
                        break
                else:
                    visit_sc.append(sc)

            some_drawn = False
            for sc in visit_sc:
                sc.step = TileStep.DONE
                for tile_under in sc.tiles_under:
                    self._push(tile_under)
                    some_drawn = True

            if some_drawn:
                continue

            alldrawn = all(sc.step == TileStep.DONE for sc in tile.scenery_list)
            if not alldrawn:
                continue

            tile.step = TileStep.DONE

            if tile.level < LEVELS - 1:
                neighbor = self.grid.get((tile.x, tile.y, tile.level + 1))
                if neighbor and neighbor.step != TileStep.DONE:
                    self._push(neighbor)

            # Propagate towards eye
            if self.is_north(tile):
                neighbor = self.grid.get((tile.x, tile.y + 1, tile.level))
                if neighbor and neighbor.step != TileStep.DONE:
                    self._push(neighbor)
            if self.is_east(tile):
                neighbor = self.grid.get((tile.x - 1, tile.y, tile.level))
                if neighbor and neighbor.step != TileStep.DONE:
                    self._push(neighbor)
            if self.is_south(tile):
                neighbor = self.grid.get((tile.x, tile.y - 1, tile.level))
                if neighbor and neighbor.step != TileStep.DONE:
                    self._push(neighbor)
            if self.is_west(tile):
                neighbor = self.grid.get((tile.x + 1, tile.y, tile.level))
                if neighbor and neighbor.step != TileStep.DONE:
                    self._push(neighbor)


# ---------------------------------------------------------------------------
# Pygame visualisation
# ---------------------------------------------------------------------------

def main():
    pygame.init()
    width  = (GRID_SIZE * CELL_SIZE + MARGIN * 2) * LEVELS
    height = GRID_SIZE * CELL_SIZE + MARGIN * 2
    screen = pygame.display.set_mode((width, height))
    pygame.display.set_caption("Manhattan Outside-In Traversal")
    clock = pygame.time.Clock()
    font  = pygame.font.SysFont(None, 24)

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

        # Apply movement / rotation at most once per MOVE_COOLDOWN ms
        if now - last_move_ms >= MOVE_COOLDOWN:
            if w_down:
                manager.eye_x, manager.eye_y = move_eye(
                    manager.eye_x, manager.eye_y, 'W')
                eye_changed = True
            elif s_down:
                manager.eye_x, manager.eye_y = move_eye(
                    manager.eye_x, manager.eye_y, 'S')
                eye_changed = True
            elif a_down:
                manager.eye_x, manager.eye_y = move_eye(
                    manager.eye_x, manager.eye_y, 'A')
                eye_changed = True
            elif d_down:
                manager.eye_x, manager.eye_y = move_eye(
                    manager.eye_x, manager.eye_y, 'D')
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

        for l in range(LEVELS):
            offset_x = MARGIN + l * (GRID_SIZE * CELL_SIZE + MARGIN)
            offset_y = MARGIN

            # 1x1 tiles
            for y in range(GRID_SIZE):
                for x in range(GRID_SIZE):
                    t = manager.grid[(x, y, l)]
                    rect = (offset_x + x * CELL_SIZE,
                            offset_y + y * CELL_SIZE,
                            CELL_SIZE - 2, CELL_SIZE - 2)
                    if not t.mask:
                        color = COLOR_MASKED
                    elif t.step == TileStep.BASE:
                        color = COLOR_BASE
                    elif t.step == TileStep.DONE:
                        color = COLOR_DONE
                    else:
                        color = COLOR_NOT_VISITED
                    pygame.draw.rect(screen, color, rect)

            # Scenery
            for sc in manager.all_scenery:
                if sc.level == l:
                    lx = offset_x + sc.rect.x * CELL_SIZE
                    ly = offset_y + sc.rect.y * CELL_SIZE
                    lw = sc.rect.width * CELL_SIZE - 2
                    lh = sc.rect.height * CELL_SIZE - 2
                    if lw <= 0 or lh <= 0:
                        continue
                    if sc.step == TileStep.DONE:
                        s = pygame.Surface((lw, lh), pygame.SRCALPHA)
                        s.fill((0, 150, 255, 120))
                        screen.blit(s, (lx, ly))
                    pygame.draw.rect(screen, COLOR_SCENERY, (lx, ly, lw, lh), 3)

            # Frustum (same for both levels — eye is the same)
            draw_frustum(screen, offset_x, offset_y,
                         manager.eye_x, manager.eye_y, manager.yaw_deg)

            # Level label
            lbl = font.render(f"Level {l}", True, (180, 180, 180))
            screen.blit(lbl, (offset_x, offset_y - 20))

        hud = font.render(
            f"Step: {manager.current_step}  |  "
            f"←/→ step  WASD move  E/R rotate  "
            f"yaw={manager.yaw_deg:.0f}°  eye=({manager.eye_x},{manager.eye_y})",
            True, (200, 200, 200))
        screen.blit(hud, (10, 10))

        pygame.display.flip()
        clock.tick(FPS)


if __name__ == "__main__":
    main()
