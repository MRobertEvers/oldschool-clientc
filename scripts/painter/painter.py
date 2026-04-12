import pygame
import sys
import heapq

# --- Configuration ---
GRID_SIZE = 11  # Must be odd for a clear center
CELL_SIZE = 40
LEVELS = 2
MARGIN = 50
FPS = 60

# Colors
COLOR_BG = (30, 30, 30)
COLOR_GRID = (60, 60, 60)
COLOR_MASKED = (10, 10, 10)
COLOR_NOT_VISITED = (100, 100, 100)
COLOR_BASE = (200, 200, 0)  # Yellow
COLOR_DONE = (0, 200, 0)    # Green
COLOR_LARGE_TILE = (0, 150, 255) # Blue

# States
class TileStep:
    NOT_VISITED = 0
    BASE = 1
    DONE = 2

class LargeTile:
    def __init__(self, x, y, w, h, level):
        self.rect = pygame.Rect(x, y, w, h)
        self.level = level
        self.step = TileStep.NOT_VISITED
        self.tiles_under = [] # Will be populated by grid

class Tile:
    def __init__(self, x, y, level, mask=True, idx=0):
        self.x = x
        self.y = y
        self.level = level
        self.mask = mask
        self.step = TileStep.DONE if not mask else TileStep.NOT_VISITED
        self.dist = abs(x - (GRID_SIZE // 2)) + abs(y - (GRID_SIZE // 2))
        self.idx = idx
        self.large_tiles = []

    def __lt__(self, other):
        return self.idx < other.idx

class GridManager:
    def __init__(self):
        self.grid = {} # (x, y, level) -> Tile
        self.large_tiles = []
        self.tile_heap = [] # (-dist, Tile)
        heapq.heapify(self.tile_heap)
        self.center = GRID_SIZE // 2
       
        self.max_dist = self.center * 2
        self.current_step = 0

        self.reset()

    def reset(self):
        self.grid = {} # (x, y, level) -> Tile
        self.large_tiles = []
        self.tile_heap = [] # (-dist, Tile)
        heapq.heapify(self.tile_heap)
        self.center = GRID_SIZE // 2
        
        # Initialize Tiles
        tile_idx = 0
        for l in range(LEVELS):
            for y in range(GRID_SIZE):
                for x in range(GRID_SIZE):
                    # Random mask: center always valid, edges 20% masked
                    mask = True if (x == self.center and y == self.center) else (x % 4 != 0 or y % 3 != 0)
                    self.grid[(x, y, l)] = Tile(x, y, l, mask, tile_idx)
                    tile_idx += 1

        # Add a few overlapping Large Tiles for testing
        self.large_tiles.append(LargeTile(1, 1, 3, 2, 0)) # Level 0
        self.large_tiles.append(LargeTile(2, 1, 2, 3, 0)) # Overlapping
        self.large_tiles.append(LargeTile(7, 7, 2, 2, 1)) # Level 1
        
        # Map Large Tiles to 1x1 Tiles
        for lt in self.large_tiles:
            for tx in range(lt.rect.x, lt.rect.x + lt.rect.width):
                for ty in range(lt.rect.y, lt.rect.y + lt.rect.height):
                    t = self.grid.get((tx, ty, lt.level))
                    if t:
                        lt.tiles_under.append(t)
                        t.large_tiles.append(lt)

        for t in self.grid.values():
            heapq.heappush(self.tile_heap, (-t.dist, t))


    def is_north(self, t):
        return t.y < self.center
    
    def is_east(self, t):
        return t.x > self.center
    
    def is_south(self, t):
        return t.y > self.center
    
    def is_west(self, t):
        return t.x < self.center

    def get_neighbors_out(self, t):
        """Returns the two neighbors farther from center based on quadrant."""
        x, y, l = t.x, t.y, t.level
        neighbors = []
        if x < self.center: dx = -1
        elif x > self.center: dx = 1
        else: dx = 0

        if y < self.center: dy = -1
        elif y > self.center: dy = 1
        else: dy = 0

        if dx != 0: neighbors.append(self.grid.get((x + dx, y, l)))
        if dy != 0: neighbors.append(self.grid.get((x, y + dy, l)))
        return [n for n in neighbors if n]

    def check_spanning(self, current, neighbor):
        """Check if a large tile covers both current and neighbor."""
        for lt in current.large_tiles:
            if lt in neighbor.large_tiles:
                return True
        return False

    def run(self):
        """Executes one tick of the state machine logic."""

        self.reset()
        
        runstep = 0



        while self.tile_heap and runstep < self.current_step:
            runstep += 1
            if runstep == self.current_step:
                print(f"Running step {runstep}")
            dist, tile = heapq.heappop(self.tile_heap)

            if tile.step == TileStep.DONE:
                continue

            if tile.step == TileStep.NOT_VISITED:
                if tile.level > 0:
                    neighbor = self.grid.get((tile.x, tile.y, tile.level - 1))
                    if neighbor and neighbor.step != TileStep.DONE:
                        if not self.check_spanning(tile, neighbor):
                            continue


                if self.is_north(tile):
                    neighbor = self.grid.get((tile.x, tile.y - 1, tile.level))
                    if neighbor and neighbor.step != TileStep.DONE:
                        if not self.check_spanning(tile, neighbor):
                            continue
                
                if self.is_east(tile):
                    neighbor = self.grid.get((tile.x + 1, tile.y, tile.level))
                    if neighbor and neighbor.step != TileStep.DONE:
                        if not self.check_spanning(tile, neighbor):
                            continue
                
                if self.is_south(tile):
                    neighbor = self.grid.get((tile.x, tile.y + 1, tile.level))
                    if neighbor and neighbor.step != TileStep.DONE:
                        if not self.check_spanning(tile, neighbor):
                            continue
                
                if self.is_west(tile):
                    neighbor = self.grid.get((tile.x - 1, tile.y, tile.level))
                    if neighbor and neighbor.step != TileStep.DONE:
                        if not self.check_spanning(tile, neighbor):
                            continue
            
            tile.step = TileStep.BASE
            visit_lt = []
            for lt in tile.large_tiles:
                if lt.step == TileStep.DONE:
                    continue
                
                for tile_under in lt.tiles_under:
                    if tile_under.step < TileStep.BASE:
                        break
                else: 
                    visit_lt.append(lt)
                    
            
            for lt in visit_lt:
                lt.step = TileStep.DONE
                for tile_under in lt.tiles_under:
                    heapq.heappush(self.tile_heap, (-tile_under.dist, tile_under))

            alldrawn = True
            for t in tile.large_tiles:
                if t.step != TileStep.DONE:
                    alldrawn = False
                    break
            
            if not alldrawn:
                continue

            tile.step = TileStep.DONE

            if tile.level < LEVELS - 1:
                neighbor = self.grid.get((tile.x, tile.y, tile.level + 1))
                if neighbor and neighbor.step != TileStep.DONE:
                    heapq.heappush(self.tile_heap, (-neighbor.dist, neighbor))
            
            # Pass towards center
            if self.is_north(tile):
                neighbor = self.grid.get((tile.x, tile.y + 1, tile.level))
                if neighbor and neighbor.step != TileStep.DONE:
                    heapq.heappush(self.tile_heap, (-neighbor.dist, neighbor))
            if self.is_east(tile):
                neighbor = self.grid.get((tile.x - 1 , tile.y, tile.level))
                if neighbor and neighbor.step != TileStep.DONE:
                    heapq.heappush(self.tile_heap, (-neighbor.dist, neighbor))
            if self.is_south(tile):
                neighbor = self.grid.get((tile.x, tile.y - 1, tile.level))
                if neighbor and neighbor.step != TileStep.DONE:
                    heapq.heappush(self.tile_heap, (-neighbor.dist, neighbor))
            if self.is_west(tile):
                neighbor = self.grid.get((tile.x + 1, tile.y, tile.level))
                if neighbor and neighbor.step != TileStep.DONE:
                    heapq.heappush(self.tile_heap, (-neighbor.dist, neighbor))

def main():
    pygame.init()
    width = (GRID_SIZE * CELL_SIZE + MARGIN * 2) * LEVELS
    screen = pygame.display.set_mode((width, GRID_SIZE * CELL_SIZE + MARGIN * 2))
    pygame.display.set_caption("Manhattan Outside-In Traversal")
    clock = pygame.time.Clock()
    
    manager = GridManager()
    manager.current_step = 0

    right_down = False
    left_down = False

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
        if left_down:
            manager.current_step -= 1
            manager.run()

        screen.fill(COLOR_BG)

        # Draw Grids
        for l in range(LEVELS):
            offset_x = MARGIN + l * (GRID_SIZE * CELL_SIZE + MARGIN)
            offset_y = MARGIN
            
            # Draw 1x1 Tiles
            for y in range(GRID_SIZE):
                for x in range(GRID_SIZE):
                    t = manager.grid[(x, y, l)]
                    rect = (offset_x + x*CELL_SIZE, offset_y + y*CELL_SIZE, CELL_SIZE-2, CELL_SIZE-2)
                    
                    color = COLOR_NOT_VISITED
                    if not t.mask: color = COLOR_MASKED
                    elif t.step == TileStep.BASE: color = COLOR_BASE
                    elif t.step == TileStep.DONE: color = COLOR_DONE
                    
                    # Highlight current distance being processed
                    pygame.draw.rect(screen, color, rect)

            # Draw Large Tiles
            for lt in manager.large_tiles:
                if lt.level == l:
                    lx = offset_x + lt.rect.x * CELL_SIZE
                    ly = offset_y + lt.rect.y * CELL_SIZE
                    lw = lt.rect.width * CELL_SIZE - 2
                    lh = lt.rect.height * CELL_SIZE - 2
                    
                    l_rect = (lx, ly, lw, lh)
                    if lt.step == TileStep.DONE:
                        s = pygame.Surface((lw, lh), pygame.SRCALPHA)
                        s.fill((0, 150, 255, 120))
                        screen.blit(s, (lx, ly))
                    pygame.draw.rect(screen, COLOR_LARGE_TILE, l_rect, 3)

        # Instructions
        font = pygame.font.SysFont(None, 24)
        img = font.render(f"Current Step: {manager.current_step} | Press RIGHT ARROW to step", True, (200, 200, 200))
        screen.blit(img, (20, 10))

        pygame.display.flip()
        clock.tick(FPS)

if __name__ == "__main__":
    main()