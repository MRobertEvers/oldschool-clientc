import pygame
import time

# --- Configuration ---
GRID_SIZE = 19  # Best with odd numbers for a perfect center
CELL_SIZE = 35
MARGIN = 1
WIDTH = GRID_SIZE * (CELL_SIZE + MARGIN) + MARGIN
HEIGHT = GRID_SIZE * (CELL_SIZE + MARGIN) + MARGIN + 80

# Colors
COLOR_UNVISITED = (180, 180, 180)
COLOR_MASKED = (40, 40, 40)
COLOR_VISITED_PASS1 = (255, 200, 50)  # Orange-Yellow
COLOR_VISITED_FINAL = (50, 150, 255)  # Sky Blue
COLOR_LT_OUTLINE = (0, 200, 0)        # Green
COLOR_LT_ACTIVE = (255, 50, 50)       # Red (When activating)
COLOR_TEXT = (255, 255, 255)
COLOR_BG = (20, 20, 20)

class LargeTile:
    def __init__(self, rect, grid_size):
        # rect: (x, y, w, h)
        self.coords = []
        cx, cy = grid_size // 2, grid_size // 2
        
        for gx in range(rect[0], min(rect[0] + rect[2], grid_size)):
            for gy in range(rect[1], min(rect[1] + rect[3], grid_size)):
                self.coords.append((gx, gy))
        
        # Activation Distance = Manhattan distance of the point closest to center
        self.activation_dist = min(abs(x - cx) + abs(y - cy) for x, y in self.coords)
        self.is_active = False

def get_traversal(grid_size, mask, large_tiles):
    cx, cy = grid_size // 2, grid_size // 2
    
    # 1. Group 1x1 tiles by Manhattan Distance
    ring_map = {}
    max_d = 0
    for x in range(grid_size):
        for y in range(grid_size):
            d = abs(x - cx) + abs(y - cy)
            max_d = max(max_d, d)
            ring_map.setdefault(d, []).append((x, y))
            
    # 2. Group Large Tiles by Activation Distance
    lt_map = {}
    for lt in large_tiles:
        lt_map.setdefault(lt.activation_dist, []).append(lt)

    # 3. Traverse Inward (Diamond Rings)
    for d in range(max_d, -1, -1):
        yield ('status', f"Processing Diamond Ring Distance: {d}")
        
        # Step A: Visit all 1x1 tiles in the ring
        for x, y in ring_map.get(d, []):
            if mask[x][y]:
                yield ('visit_1', (x, y))
        
        # Step B: Check if any Large Tiles "complete" their distance requirements at this ring
        if d in lt_map:
            for lt in lt_map[d]:
                yield ('status', f"Activating Large Tile at distance {d}")
                lt.is_active = True
                yield ('pause', 0.4)
                
                # Re-visit tiles under LT
                for sx, sy in lt.coords:
                    if mask[sx][sy]:
                        yield ('visit_2', (sx, sy))
                
                lt.is_active = False
                yield ('pause', 0.2)

class Visualizer:
    def __init__(self, grid_size, mask, large_tiles):
        pygame.init()
        self.screen = pygame.display.set_mode((WIDTH, HEIGHT))
        pygame.display.set_caption("Manhattan Diamond Traversal")
        self.clock = pygame.time.Clock()
        self.font = pygame.font.SysFont("Arial", 18)
        
        self.grid_size = grid_size
        self.mask = mask
        self.large_tiles = large_tiles
        self.states = [[0 for _ in range(grid_size)] for _ in range(grid_size)]
        
        self.gen = get_traversal(grid_size, mask, large_tiles)
        self.status = "Press SPACE to start/step, R to reset"
        self.running = True

    def run(self):
        while self.running:
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    self.running = False
                if event.type == pygame.KEYDOWN:
                    if event.key == pygame.K_SPACE:
                        self.step()
                    if event.key == pygame.K_r:
                        self.__init__(self.grid_size, self.mask, self.large_tiles)

            self.draw()
            self.clock.tick(60)
        pygame.quit()

    def step(self):
        try:
            action, data = next(self.gen)
            if action == 'status': self.status = data
            elif action == 'visit_1': self.states[data[0]][data[1]] = 1
            elif action == 'visit_2': self.states[data[0]][data[1]] = 2
            elif action == 'pause': time.sleep(data)
        except StopIteration:
            self.status = "Traversal Finished."

    def draw(self):
        self.screen.fill(COLOR_BG)
        
        # Draw 1x1 Grid
        for x in range(self.grid_size):
            for y in range(self.grid_size):
                rect = (x*(CELL_SIZE+MARGIN)+MARGIN, y*(CELL_SIZE+MARGIN)+MARGIN, CELL_SIZE, CELL_SIZE)
                
                if not self.mask[x][y]:
                    color = COLOR_MASKED
                else:
                    state = self.states[x][y]
                    color = COLOR_UNVISITED if state == 0 else (COLOR_VISITED_PASS1 if state == 1 else COLOR_VISITED_FINAL)
                
                pygame.draw.rect(self.screen, color, rect)

        # Draw Large Tiles
        for lt in self.large_tiles:
            # Calculate bounding box for the LT
            xs = [c[0] for c in lt.coords]
            ys = [c[1] for c in lt.coords]
            x_min, x_max = min(xs), max(xs)
            y_min, y_max = min(ys), max(ys)
            
            px = x_min * (CELL_SIZE + MARGIN)
            py = y_min * (CELL_SIZE + MARGIN)
            pw = (x_max - x_min + 1) * (CELL_SIZE + MARGIN)
            ph = (y_max - y_min + 1) * (CELL_SIZE + MARGIN)
            
            color = COLOR_LT_ACTIVE if lt.is_active else COLOR_LT_OUTLINE
            width = 4 if lt.is_active else 2
            pygame.draw.rect(self.screen, color, (px, py, pw, ph), width)

        # Status Text
        txt = self.font.render(self.status, True, COLOR_TEXT)
        self.screen.blit(txt, (20, HEIGHT - 50))
        pygame.display.flip()

# --- Setup Specific Scenario ---
mask = [[1 for _ in range(GRID_SIZE)] for _ in range(GRID_SIZE)]

# 1. Create some "holes" in the mask
for i in range(GRID_SIZE):
    mask[i][GRID_SIZE//2] = 0 # Vertical line hole
    mask[GRID_SIZE//2][i] = 0 # Horizontal line hole

for i in range(5):
    mask[i][i] = 0
    mask[GRID_SIZE - 1 - i][GRID_SIZE - 3] = 0

# 2. Define Large Tiles (some overlapping the holes)
lts = [
    LargeTile((1, 1, 1, 4), GRID_SIZE),   # Top Left
    LargeTile((2, 2, 4, 4), GRID_SIZE),   # Top Left
    LargeTile((2, 2, 3, 3), GRID_SIZE),   # Top Left
    LargeTile((12, 12, 4, 4), GRID_SIZE), # Bottom Right
    LargeTile((7, 7, 5, 5), GRID_SIZE),   # Center (heavily masked by the cross-hairs)
    LargeTile((2, 13, 5, 2), GRID_SIZE)   # Bottom Left
]

viz = Visualizer(GRID_SIZE, mask, lts)
viz.run()