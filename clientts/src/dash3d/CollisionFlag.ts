export const enum CollisionFlag {
    OPEN = 0x0,
    WALL_NORTH_WEST = 0x1,
    WALL_NORTH = 0x2,
    WALL_NORTH_EAST = 0x4,
    WALL_EAST = 0x8,
    WALL_SOUTH_EAST = 0x10,
    WALL_SOUTH = 0x20,
    WALL_SOUTH_WEST = 0x40,
    WALL_WEST = 0x80,

    LOC = 0x100,
    WALL_NORTH_WEST_PROJ_BLOCKER = 0x200,
    WALL_NORTH_PROJ_BLOCKER = 0x400,
    WALL_NORTH_EAST_PROJ_BLOCKER = 0x800,
    WALL_EAST_PROJ_BLOCKER = 0x1000,
    WALL_SOUTH_EAST_PROJ_BLOCKER = 0x2000,
    WALL_SOUTH_PROJ_BLOCKER = 0x4000,
    WALL_SOUTH_WEST_PROJ_BLOCKER = 0x8000,
    WALL_WEST_PROJ_BLOCKER = 0x10000,
    LOC_PROJ_BLOCKER = 0x20000,

    ANTIMACRO = 0x80000,
    FLOOR = 0x200000,

    FLOOR_BLOCKED = 0x280000, // CollisionFlag.FLOOR | CollisionFlag.ANTIMACRO
    WALK_BLOCKED = 0x280100, // CollisionFlag.LOC | CollisionFlag.FLOOR_BLOCKED

    BLOCK_SOUTH = 0x280102, // CollisionFlag.WALL_NORTH | CollisionFlag.WALK_BLOCKED
    BLOCK_WEST = 0x280108, // CollisionFlag.WALL_EAST | CollisionFlag.WALK_BLOCKED
    BLOCK_SOUTH_WEST = 0x28010E, // CollisionFlag.WALL_NORTH | CollisionFlag.WALL_NORTH_EAST | CollisionFlag.BLOCK_WEST
    BLOCK_NORTH = 0x280120, // CollisionFlag.WALL_SOUTH | CollisionFlag.WALK_BLOCKED
    BLOCK_NORTH_WEST = 0x280138, // CollisionFlag.WALL_EAST | CollisionFlag.WALL_SOUTH_EAST | CollisionFlag.BLOCK_NORTH
    BLOCK_EAST = 0x280180, // CollisionFlag.WALL_WEST | CollisionFlag.WALK_BLOCKED
    BLOCK_SOUTH_EAST = 0x280183, // CollisionFlag.WALL_NORTH_WEST | CollisionFlag.WALL_NORTH | CollisionFlag.BLOCK_EAST
    BLOCK_NORTH_EAST = 0x2801E0, // CollisionFlag.WALL_SOUTH | CollisionFlag.WALL_SOUTH_WEST | CollisionFlag.BLOCK_EAST

    BOUNDS = 0xffffff
}
