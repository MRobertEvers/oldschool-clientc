import pygame
import sys

# --- Configuration ---
GRID_SIZE = 11
NUM_LEVELS = 3
CELL_SIZE = 30
MARGIN = 1
GRID_WIDTH = GRID_SIZE * (CELL_SIZE + MARGIN) + MARGIN
WINDOW_WIDTH = (GRID_WIDTH + 40) * NUM_LEVELS + 40
WINDOW_HEIGHT = GRID_WIDTH + 180

# Colors
COLOR_UNVISITED = (80, 80, 80)
COLOR_MASKED = (30, 30, 30)
COLOR_PASS1 = (255, 165, 0)      # Orange
COLOR_PASS2 = (0, 200, 255)      # Cyan
COLOR_STALLED = (150, 50, 150)   # Purple (Waiting for tile below)
COLOR_LT = (0, 255, 0)
COLOR_LT_ACTIVE = (255, 0, 0)
COLOR_BG = (10, 10, 15)
COLOR_TEXT = (220, 220, 220)

class LargeTile:
    def __init__(self, rect, level):
        self.level = level
        self.coords = set()
        cx, cy = GRID_SIZE // 2, GRID_SIZE // 2
        for x in range(rect[0], min(rect[0] + rect[2], GRID_SIZE)):
            for y in range(rect[1], min(rect[1] + rect[3], GRID_SIZE)):
                self.coords.add((x, y))
        self.activation_dist = min(abs(x - cx) + abs(y - cy) for x, y in self.coords)

def get_dist(x, y):
    return abs(x - (GRID_SIZE // 2)) + abs(y - (GRID_SIZE // 2))

class SimState:
    def __init__(self):
        # 0: Unvisited, 1: Pass1, 2: Pass2/Complete
        self.grid = [[[0 for _ in range(GRID_SIZE)] for _ in range(GRID_SIZE)] for _ in range(NUM_LEVELS)]
        self.stalled = [[[False for _ in range(GRID_SIZE)] for _ in range(GRID_SIZE)] for _ in range(NUM_LEVELS)]
        self.active_lts = [None for _ in range(NUM_LEVELS)]
        self.status = ""

def generate_history(masks, levels_lts):
    history = []
    state = SimState()
    max_d = (GRID_SIZE // 2) * 2
    
    # Track current ring distance and sub-stage per level
    # stages: 0=Visiting Ring Pass1, 1=Activating LTs at this distance
    level_ptr = [{'d': max_d, 'stage': 0, 'lt_idx': 0} for _ in range(NUM_LEVELS)]
    
    # Pre-map LTs
    lt_maps = []
    for i in range(NUM_LEVELS):
        m = {}
        for lt in levels_lts[i]:
            m.setdefault(lt.activation_dist, []).append(lt)
        lt_maps.append(m)

    # Completion helper
    def is_complete(lvl, x, y):
        if lvl < 0: return True
        return state.grid[lvl][x][y] == 2

    while True:
        changed = False
        state.stalled = [[[False for _ in range(GRID_SIZE)] for _ in range(GRID_SIZE)] for _ in range(NUM_LEVELS)]
        
        for i in range(NUM_LEVELS):
            p = level_ptr[i]
            if p['d'] < 0: continue
            
            # --- 1. Pipeline Lag Constraint ---
            # We can't be more than 1 distance behind the level below (where d_i <= d_{i-1} + 1)
            if i > 0:
                if level_ptr[i-1]['d'] > p['d'] - 1 and level_ptr[i-1]['d'] >= 0:
                    continue

            d = p['d']
            
            if p['stage'] == 0: # PASS 1
                ring_tiles = [(x,y) for x in range(GRID_SIZE) for y in range(GRID_SIZE) if get_dist(x,y) == d]
                
                # Check point-to-point dependencies for the whole ring
                all_ready = True
                for rx, ry in ring_tiles:
                    if masks[i][rx][ry] and not is_complete(i-1, rx, ry):
                        state.stalled[i][rx][ry] = True
                        all_ready = False
                
                if all_ready:
                    for rx, ry in ring_tiles:
                        if masks[i][rx][ry]: state.grid[i][rx][ry] = 1
                    p['stage'] = 1
                    state.status = f"L{i} finished Pass 1 for Ring {d}"
                    changed = True
            
            elif p['stage'] == 1: # LT ACTIVATION
                lts = lt_maps[i].get(d, [])
                if p['lt_idx'] < len(lts):
                    lt = lts[p['lt_idx']]
                    state.active_lts[i] = lt
                    for sx, sy in lt.coords:
                        if masks[i][sx][sy]: state.grid[i][sx][sy] = 2
                    p['lt_idx'] += 1
                    state.status = f"L{i} activated Large Tile at Ring {d}"
                    changed = True
                else:
                    # Clean up: Mark tiles in this ring that AREN'T part of an LT as complete (Pass 2)
                    state.active_lts[i] = None
                    ring_tiles = [(x,y) for x in range(GRID_SIZE) for y in range(GRID_SIZE) if get_dist(x,y) == d]
                    for rx, ry in ring_tiles:
                        # If tile isn't covered by an LT at this distance, it completes now
                        is_in_lt = any( (rx, ry) in lt.coords for lt in lts )
                        if not is_in_lt and state.grid[i][rx][ry] == 1:
                            state.grid[i][rx][ry] = 2
                    
                    p['d'] -= 1
                    p['stage'] = 0
                    p['lt_idx'] = 0
                    changed = True

        if not changed: break
        
        # Snapshot
        snap = SimState()
        snap.grid = [[row[:] for row in lvl] for lvl in state.grid]
        snap.stalled = [[row[:] for row in lvl] for lvl in state.stalled]
        snap.active_lts = list(state.active_lts)
        snap.status = state.status
        history.append(snap)
            
    return history

class Visualizer:
    def __init__(self):
        pygame.init()
        self.screen = pygame.display.set_mode((WINDOW_WIDTH, WINDOW_HEIGHT))
        self.font = pygame.font.SysFont("Courier", 14, bold=True)
        
        # Setup Data
        self.masks = [[[1 for _ in range(GRID_SIZE)] for _ in range(GRID_SIZE)] for _ in range(NUM_LEVELS)]
        # Mask out a vertical strip on Level 0 to see how it stalls Level 1
        for y in range(GRID_SIZE): self.masks[0][GRID_SIZE//2][y] = 0
        
        self.lts = [
            [LargeTile((1,1,2,2), 0), LargeTile((7,7,3,3), 0)],
            [LargeTile((4,4,3,3), 1)],
            [LargeTile((2,7,2,2), 2)]
        ]
        
        self.history = generate_history(self.masks, self.lts)
        self.idx = 0

    def run(self):
        while True:
            for e in pygame.event.get():
                if e.type == pygame.QUIT: return
                if e.type == pygame.KEYDOWN:
                    if e.key == pygame.K_RIGHT: self.idx = min(self.idx+1, len(self.history)-1)
                    if e.key == pygame.K_LEFT: self.idx = max(self.idx-1, 0)

            self.draw()
            pygame.time.Clock().tick(60)

    def draw(self):
        self.screen.fill(COLOR_BG)
        state = self.history[self.idx]
        
        for i in range(NUM_LEVELS):
            ox, oy = 40 + i*(GRID_WIDTH+40), 60
            pygame.draw.rect(self.screen, (40,40,50), (ox-5, oy-5, GRID_WIDTH+10, GRID_WIDTH+10), 1)
            self.screen.blit(self.font.render(f"LAYER {i}", True, COLOR_TEXT), (ox, 30))
            
            for x in range(GRID_SIZE):
                for y in range(GRID_SIZE):
                    r = (ox + x*(CELL_SIZE+MARGIN), oy + y*(CELL_SIZE+MARGIN), CELL_SIZE, CELL_SIZE)
                    if not self.masks[i][x][y]: col = COLOR_MASKED
                    elif state.stalled[i][x][y]: col = COLOR_STALLED
                    else:
                        val = state.grid[i][x][y]
                        col = COLOR_UNVISITED if val==0 else (COLOR_PASS1 if val==1 else COLOR_PASS2)
                    pygame.draw.rect(self.screen, col, r)

            for lt in self.lts[i]:
                xs, ys = [c[0] for c in lt.coords], [c[1] for c in lt.coords]
                lx, ly, lw, lh = min(xs), min(ys), max(xs)-min(xs)+1, max(ys)-min(ys)+1
                lr = (ox+lx*(CELL_SIZE+MARGIN), oy+ly*(CELL_SIZE+MARGIN), lw*(CELL_SIZE+MARGIN), lh*(CELL_SIZE+MARGIN))
                pygame.draw.rect(self.screen, COLOR_LT_ACTIVE if state.active_lts[i]==lt else COLOR_LT, lr, 2)

        self.screen.blit(self.font.render(f"STEP: {self.idx} | {state.status}", True, COLOR_PASS2), (40, WINDOW_HEIGHT-80))
        self.screen.blit(self.font.render("Arrows: Navigate | Purple: Stalled (Waiting for tile below to complete)", True, (150,150,150)), (40, WINDOW_HEIGHT-50))
        pygame.display.flip()

if __name__ == "__main__":
    Visualizer().run()