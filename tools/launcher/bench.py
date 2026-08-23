"""Scene benchmarks: the `[bench*]` blocks of a world, and what they measure.

A world manifest already states the one thing a renderer benchmark cannot be
allowed to drift on — WHICH CACHE. `[bench:<scene>]` blocks state the rest of
it: which map squares to mesh, and where the eye stands while they are drawn.
One scene is one fixed camera over fixed geometry, so the only thing left that
can move between two runs is the renderer.

The runs are OFFLINE (`--offline`). No server, no login, no npc spawns, no
600 ms world tick — a scene benchmark that logs in measures the login as well,
and the login is not the same twice. What is left is cache load, scene build,
and the frame loop, which is exactly the surface `--soft3d` owns.

This module is the data half: parse the suite, turn one scene into the env a
client run needs, read the CSVs back, aggregate. cli.py owns the processes.
"""

import math
import os
import statistics

from .profiles import LaunchError

# One map square is 64x64 tiles (WORLD_MAP_TERRAIN_X in src/world/world.h), and
# World_ResetSceneChunkList bases the scene at the SW-most square it was given.
# Both numbers are needed to place an absolute world tile inside the scene the
# client will build, so they are stated once here.
TILES_PER_SQUARE = 64
# app.c app_world_map_squares_parse — the cap on one TORIRS_WORLD_MAP list.
MAX_SQUARES = 16
# Scene-local units per tile, and the eye's offset to a tile's centre.
UNITS_PER_TILE = 128

# The offline first-look camera in app.c App_WorldLoadFinish. A scene that
# states no height/look inherits it, so an under-specified scene is still a
# camera somebody has seen rather than one pointing at the sky.
DEFAULT_HEIGHT = -2000
DEFAULT_PITCH = 450
DEFAULT_YAW = 0

DEFAULT_CANVAS = "765x503"
DEFAULT_SAMPLE_FRAMES = 500
DEFAULT_SAMPLES = 4
DEFAULT_WARMUP = 1
DEFAULT_RENDERERS = ("soft3d",)

# Camera motion. app.c APP_WEDGE_CAM_PATH_MAX caps the waypoint list; the
# mode and wrap names are the ones app_wedge_cam_path_parse accepts, spelled
# once here so a typo in a manifest is caught before a client is launched
# rather than warned about on its stderr and then measured anyway.
MAX_WAYPOINTS = 32
MOTION_MODES = ("linear", "spline")
MOTION_WRAPS = ("loop", "pingpong", "hold")
DEFAULT_MOTION_MODE = "linear"
DEFAULT_MOTION_WRAP = "loop"

# app.c app_cinema_angles: yaw = atan2(dx, dz) * -325.949, masked to 11
# bits, where dx/dz point from the eye at the thing being looked at. Taken
# from the client rather than derived, because a benchmark whose camera
# faces the wrong way is a benchmark of the wrong geometry.
YAW_PER_RADIAN = -325.949
UNITS_PER_TURN = 2048

# A renderer is named, not spelled as a raw flag, so a suite reads as a list of
# renderers and the flag stays one thing this file knows. `soft3d` is the whole
# software rasterizer; the D3D9 entries are here because the only useful thing
# to put beside a soft3d number is the GPU number from the same scene.
RENDERER_FLAGS = {
    "soft3d": "--soft3d",
    "d3d9": "--d3d9",
    "d3d9-zbuffer": "--d3d9-zbuffer",
    "opengl3": "--opengl3",
    "opengl3-zbuffer": "--opengl3-zbuffer",
}

# The stage timers worth a column. `frame` is the whole loop iteration minus
# the pacing wait, `render` is the renderer, `build` is the frame the renderer
# was handed — a soft3d regression that is really a scene-build regression
# shows up as the wrong one of those two moving.
REPORT_STAGES = ("frame", "render", "build", "present", "paint")

# Per-frame counters that say what the renderer was asked to draw. A faster
# number with a smaller count is not a faster renderer, and these are what
# catch that.
REPORT_COUNTERS = ("painter_commands", "r_cmds_model", "scene_elements")


def _int_pair(value, what, scene):
    parts = [part.strip() for part in value.split(",")]
    if len(parts) != 2:
        raise LaunchError(
            "bench scene '%s': %s must be \"a,b\", got '%s'" % (scene, what, value))
    try:
        return int(parts[0]), int(parts[1])
    except ValueError:
        raise LaunchError(
            "bench scene '%s': %s must be two integers, got '%s'"
            % (scene, what, value))


def _int(value, what, scene):
    try:
        return int(str(value).strip())
    except (TypeError, ValueError):
        raise LaunchError(
            "bench scene '%s': %s must be an integer, got '%s'" % (scene, what, value))


def _canvas(value, scene):
    text = str(value).strip().lower().replace("*", "x")
    parts = text.split("x")
    if len(parts) != 2:
        raise LaunchError(
            "bench scene '%s': canvas must be \"WxH\", got '%s'" % (scene, value))
    try:
        width, height = int(parts[0]), int(parts[1])
    except ValueError:
        raise LaunchError(
            "bench scene '%s': canvas must be \"WxH\" integers, got '%s'"
            % (scene, value))
    if width <= 0 or height <= 0:
        raise LaunchError(
            "bench scene '%s': canvas must be positive, got '%s'" % (scene, value))
    return width, height


class Motion:
    """A camera route: waypoints, how to get between them, and how long it takes.

    The phase is indexed by FRAME, not by elapsed time (app.c
    app_wedge_cam_path_eval). That is what keeps a moving camera usable as a
    benchmark at all: a route driven by the clock would put a slow renderer at
    a different point on it than the fast one it is being compared against, and
    the two would be measuring different geometry while appearing to measure
    the same scene.
    """

    def __init__(self, mode, wrap, frames, keys, source):
        self.mode = mode
        self.wrap = wrap
        self.frames = frames
        # Scene-local (x, y, z, pitch, yaw), the units TORIRS_WEDGE_CAM takes.
        self.keys = keys
        # "via" or "orbit r=..", for the report and for summary.json -- an
        # orbit is generated, so the run has to record what it generated.
        self.source = source

    @property
    def period(self):
        """Frames before the camera is back where it started.

        A pingpong walks the route and then walks it back, so its cycle is two
        traversals. `hold` never repeats: it has no period.
        """
        if self.wrap == "pingpong":
            return self.frames * 2
        if self.wrap == "loop":
            return self.frames
        return None

    def env(self):
        return "%s,%d,%s;%s" % (
            self.mode, self.frames, self.wrap,
            ";".join("%d,%d,%d,%d,%d" % key for key in self.keys))

    def describe(self):
        return "%s %s %d waypoints/%df (%s)" % (
            self.mode, self.wrap, len(self.keys), self.frames, self.source)


class Scene:
    """One fixed camera over one fixed set of map squares."""

    def __init__(self, name, description, squares, eye, pitch, yaw,
                 canvas, sample_frames, samples, warmup, at=None,
                 motion=None):
        self.name = name
        self.description = description
        self.squares = squares
        self.eye = eye
        self.pitch = pitch
        self.yaw = yaw
        self.canvas = canvas
        self.sample_frames = sample_frames
        self.samples = samples
        self.warmup = warmup
        self.at = at
        # None for a still camera. `eye`/`pitch`/`yaw` stay meaningful
        # either way: for a moving scene they are the still it was
        # authored from, and what the client falls back to if the route
        # does not parse.
        self.motion = motion

    @property
    def scene_size(self):
        """Tiles per side of the scene the client will allocate for `squares`.

        World_ResetSceneChunkList squares the bounding box off — a 1x2 list of
        squares still builds a 2x2 scene — so this is the span, not the count.
        """
        span_x = max(x for x, _ in self.squares) - min(x for x, _ in self.squares) + 1
        span_z = max(z for _, z in self.squares) - min(z for _, z in self.squares) + 1
        return max(span_x, span_z) * TILES_PER_SQUARE

    @property
    def base_tile(self):
        return (min(x for x, _ in self.squares) * TILES_PER_SQUARE,
                min(z for _, z in self.squares) * TILES_PER_SQUARE)

    @property
    def total_frames(self):
        return (self.warmup + self.samples) * self.sample_frames

    def world_map_env(self):
        return ";".join("%d,%d" % square for square in self.squares)

    def wedge_cam_path_env(self):
        return self.motion.env() if self.motion else None

    def wedge_cam_env(self):
        return "%d,%d,%d,%d,%d" % (
            self.eye[0], self.eye[1], self.eye[2], self.pitch, self.yaw)

    def summary_line(self):
        squares = " ".join("%d,%d" % square for square in self.squares)
        where = "tile %d,%d" % self.at if self.at else "eye %d,%d" % (
            self.eye[0], self.eye[2])
        camera = ("pitch %d yaw %d" % (self.pitch, self.yaw)
                  if not self.motion else self.motion.describe())
        return ("squares %-11s scene %dx%d  %s  h=%d  %s  %dx%d  "
                "%d+%dx%d frames"
                % (squares, self.scene_size, self.scene_size, where, self.eye[1],
                   camera, self.canvas[0], self.canvas[1],
                   self.warmup * self.sample_frames, self.samples,
                   self.sample_frames))


class Suite:
    """Every scene a world declares, plus the settings they share."""

    def __init__(self, manifest, scenes, renderers, budget_ms, repeat):
        self.manifest = manifest
        self.scenes = scenes
        self.renderers = renderers
        self.budget_ms = budget_ms
        self.repeat = repeat

    @property
    def cache_dir(self):
        """The one cache every scene is measured against.

        Not a `[bench]` key: a world already names its cache, and a benchmark
        that could name a second one is a benchmark that can compare two caches
        while reporting a renderer change.
        """
        return self.manifest.cache_dir

    def select(self, names):
        """The named scenes, in the order the manifest declares them."""
        if not names:
            return list(self.scenes)
        known = {scene.name: scene for scene in self.scenes}
        missing = [name for name in names if name not in known]
        if missing:
            raise LaunchError(
                "no bench scene %s in %s\ndeclared: %s"
                % (", ".join("'%s'" % name for name in missing),
                   os.path.basename(self.manifest.path),
                   ", ".join(scene.name for scene in self.scenes) or "(none)"))
        wanted = set(names)
        return [scene for scene in self.scenes if scene.name in wanted]


def load_suite(manifest):
    """Read `[bench]` and every `[bench:<name>]` block out of a world."""
    ini = manifest.ini
    defaults = dict(ini.items("bench"))

    renderers = [part.strip()
                 for part in (defaults.get("renderers") or "").split(",")
                 if part.strip()] or list(DEFAULT_RENDERERS)
    unknown = [name for name in renderers if name not in RENDERER_FLAGS]
    if unknown:
        raise LaunchError(
            "[bench] renderers names %s, which is not a renderer this client has "
            "(want one of %s)"
            % (", ".join("'%s'" % name for name in unknown),
               ", ".join(sorted(RENDERER_FLAGS))))

    budget_ms = defaults.get("budget_ms")
    budget_ms = float(budget_ms) if budget_ms else None
    repeat = int(defaults.get("repeat") or 1)

    scenes = []
    for section in ini.sections_with_prefix("bench:"):
        name = section.split(":", 1)[1].strip()
        scenes.append(_scene(name, ini.items(section), defaults))
    return Suite(manifest, scenes, renderers, budget_ms, repeat)


def _via_keys(name, vias, local_eye, height, pitch, yaw):
    """Waypoints written out one `via=` at a time.

    Absolute OSRS tiles, like `at=`, so a route is written the way a location
    is written. Pitch/yaw and height are optional per waypoint and fall back to
    the scene's own -- a route that only moves the eye needs two numbers a line.
    """
    keys = []
    for raw in vias:
        parts = [part.strip() for part in raw.split(",")]
        if len(parts) not in (2, 4, 5):
            raise LaunchError(
                "bench scene '%s': via must be \"worldx,worldz\", "
                "\"worldx,worldz,pitch,yaw\" or "
                "\"worldx,worldz,pitch,yaw,height\", got '%s'" % (name, raw))
        numbers = [_int(part, "via", name) for part in parts]
        way_pitch = numbers[2] if len(numbers) >= 4 else pitch
        way_yaw = numbers[3] if len(numbers) >= 4 else yaw
        way_height = numbers[4] if len(numbers) == 5 else height
        x, y, z = local_eye((numbers[0], numbers[1]), way_height, "via")
        keys.append((x, y, z, way_pitch, way_yaw))
    return keys, "via"


def _orbit_keys(name, spec, at, local_eye, height, pitch,
                base_x, base_z, scene_size):
    """`orbit=<radius_tiles>,<steps>`: a ring of waypoints facing `at`.

    The one route worth generating rather than writing out. A still camera
    measures whatever happens to lie along one bearing, and a renderer change
    that only helps that bearing reads as a win; a full turn makes every wall,
    roof and skyline take its turn at being the near thing. Pair it with
    `motion=spline` -- linear legs cut the corners off the ring.
    """
    radius, steps = _int_pair(spec, "orbit", name)
    if not at:
        raise LaunchError(
            "bench scene '%s': orbit= needs at=<worldx>,<worldz>, the tile it "
            "turns around" % name)
    if radius <= 0:
        raise LaunchError(
            "bench scene '%s': orbit radius must be positive, got %d"
            % (name, radius))
    if steps < 3:
        raise LaunchError(
            "bench scene '%s': an orbit needs at least 3 steps, got %d -- two "
            "waypoints are a line through the centre, not a ring"
            % (name, steps))

    centre = local_eye(at, height, "at")
    extent = scene_size * UNITS_PER_TILE
    radius_units = radius * UNITS_PER_TILE
    keys = []
    for step in range(steps):
        angle = 2.0 * math.pi * step / steps
        # In scene UNITS, not tiles. Snapping a ring to whole tiles is what
        # turns a circle into a lumpy polygon: at radius 12 the diagonal
        # waypoints land 11.3 tiles out, a 6% pulse in the orbit radius --
        # and a pulsing radius is a pulsing draw distance, which is a
        # per-frame swing in the very number being measured. A camera has no
        # reason to stand on a tile centre anyway.
        x = int(round(centre[0] + radius_units * math.sin(angle)))
        z = int(round(centre[2] + radius_units * math.cos(angle)))
        if not (0 <= x < extent and 0 <= z < extent):
            raise LaunchError(
                "bench scene '%s': orbit=%d,%d reaches tile %d,%d, outside "
                "the %dx%d scene its map squares build (tiles %d..%d by "
                "%d..%d)"
                % (name, radius, steps,
                   base_x + x // UNITS_PER_TILE, base_z + z // UNITS_PER_TILE,
                   scene_size, scene_size,
                   base_x, base_x + scene_size - 1,
                   base_z, base_z + scene_size - 1))
        # app.c app_cinema_angles, in its units: the yaw that points the eye
        # at the centre it is circling.
        way_yaw = int(math.atan2(centre[0] - x, centre[2] - z)
                      * YAW_PER_RADIAN) % UNITS_PER_TURN
        keys.append((x, height, z, pitch, way_yaw))
    return keys, "orbit r=%d n=%d" % (radius, steps)


def _scene(name, items, defaults):
    # The raw item LIST, not a dict: `map=` repeats once per square, and a
    # `description=` too long for one line repeats too. dict() would keep the
    # last of each -- a four-square scene would quietly become a one-square
    # scene, which is the exact mistake this suite exists to make visible.
    def repeated(key):
        return [value for item_key, value in items if item_key == key]

    fields = dict(items)

    def setting(key, fallback):
        raw = fields.get(key, defaults.get(key))
        return fallback if raw is None or raw == "" else raw

    squares = [_int_pair(raw, "map", name) for raw in repeated("map")]

    at = None
    if fields.get("at"):
        at = _int_pair(fields["at"], "at", name)
        if not squares:
            squares = [(at[0] // TILES_PER_SQUARE, at[1] // TILES_PER_SQUARE)]

    if not squares:
        raise LaunchError(
            "bench scene '%s': needs at=<worldx>,<worldz> (the tile the eye "
            "stands on) or at least one map=<x>,<z>" % name)
    duplicates = sorted({square for square in squares
                         if squares.count(square) > 1})
    if duplicates:
        # Not deduplicated silently. A square named twice is meshed twice,
        # and the extra locs land in the counters as a scene that is
        # heavier than the map it claims to be.
        raise LaunchError(
            "bench scene '%s': map square%s %s named more than once"
            % (name, "" if len(duplicates) == 1 else "s",
               ", ".join("%d,%d" % square for square in duplicates)))
    if len(squares) > MAX_SQUARES:
        raise LaunchError(
            "bench scene '%s': %d map squares, but one TORIRS_WORLD_MAP load "
            "takes at most %d" % (name, len(squares), MAX_SQUARES))

    base_x = min(x for x, _ in squares) * TILES_PER_SQUARE
    base_z = min(z for _, z in squares) * TILES_PER_SQUARE
    span_x = max(x for x, _ in squares) - min(x for x, _ in squares) + 1
    span_z = max(z for _, z in squares) - min(z for _, z in squares) + 1
    scene_size = max(span_x, span_z) * TILES_PER_SQUARE

    height = _int(setting("height", DEFAULT_HEIGHT), "height", name)

    def local_eye(tile, eye_height, key):
        """An absolute OSRS tile as a scene-local eye, or an error saying why not.

        Shared by `at` and every waypoint, so a route that wanders off the
        meshed squares is caught by name at parse time rather than becoming a
        camera pointed into the void for part of every sample.
        """
        local_x = tile[0] - base_x
        local_z = tile[1] - base_z
        if not (0 <= local_x < scene_size and 0 <= local_z < scene_size):
            raise LaunchError(
                "bench scene '%s': %s=%d,%d is outside the %dx%d scene its "
                "map squares build (tiles %d..%d by %d..%d)"
                % (name, key, tile[0], tile[1], scene_size, scene_size,
                   base_x, base_x + scene_size - 1,
                   base_z, base_z + scene_size - 1))
        return (local_x * UNITS_PER_TILE + UNITS_PER_TILE // 2,
                eye_height,
                local_z * UNITS_PER_TILE + UNITS_PER_TILE // 2)

    if fields.get("eye"):
        # The raw form: paste a TORIRS_POS_DEBUG `cam=` readout back in. Scene
        # LOCAL units, because that is what the readout prints and what
        # TORIRS_WEDGE_CAM consumes.
        parts = [part.strip() for part in fields["eye"].split(",")]
        if len(parts) != 3:
            raise LaunchError(
                "bench scene '%s': eye must be \"x,y,z\" in scene-local units, "
                "got '%s'" % (name, fields["eye"]))
        eye = tuple(_int(part, "eye", name) for part in parts)
    elif at:
        eye = local_eye(at, height, "at")
    else:
        # Squares but no tile: the scene centre, which is where an offline load
        # puts the camera anyway.
        eye = (scene_size // 2 * UNITS_PER_TILE + UNITS_PER_TILE // 2,
               height,
               scene_size // 2 * UNITS_PER_TILE + UNITS_PER_TILE // 2)

    if fields.get("look"):
        pitch, yaw = _int_pair(fields["look"], "look", name)
    else:
        pitch, yaw = DEFAULT_PITCH, DEFAULT_YAW

    sample_frames = _int(setting("sample_frames", DEFAULT_SAMPLE_FRAMES),
                         "sample_frames", name)
    samples = _int(setting("samples", DEFAULT_SAMPLES), "samples", name)
    warmup = _int(setting("warmup", DEFAULT_WARMUP), "warmup", name)
    if sample_frames <= 0:
        raise LaunchError("bench scene '%s': sample_frames must be positive" % name)
    if samples <= 0:
        raise LaunchError("bench scene '%s': samples must be positive" % name)
    if warmup < 0:
        raise LaunchError("bench scene '%s': warmup cannot be negative" % name)

    # -- camera motion --------------------------------------------------------
    #
    # A still scene measures one view. A route measures what a player actually
    # does to a renderer: sweeping the yaw past every wall in turn, crossing a
    # skyline, walking into a dense block and out the far side. Both are worth
    # having, and a scene chooses by declaring waypoints or not.
    vias = repeated("via")
    orbit = fields.get("orbit")
    if vias and orbit:
        raise LaunchError(
            "bench scene '%s': via= and orbit= both name the camera's route; "
            "keep one" % name)

    mode = str(setting("motion", DEFAULT_MOTION_MODE)).strip().lower()
    wrap = str(setting("wrap", DEFAULT_MOTION_WRAP)).strip().lower()
    if mode not in MOTION_MODES:
        raise LaunchError(
            "bench scene '%s': motion must be one of %s, got '%s'"
            % (name, ", ".join(MOTION_MODES), mode))
    if wrap not in MOTION_WRAPS:
        raise LaunchError(
            "bench scene '%s': wrap must be one of %s, got '%s'"
            % (name, ", ".join(MOTION_WRAPS), wrap))

    motion = None
    if vias or orbit:
        if orbit:
            keys, source = _orbit_keys(
                name, orbit, at, local_eye, height, pitch,
                base_x, base_z, scene_size)
        else:
            keys, source = _via_keys(
                name, vias, local_eye, height, pitch, yaw)

        if len(keys) < 2:
            raise LaunchError(
                "bench scene '%s': a route needs at least 2 waypoints, got %d "
                "-- one waypoint is a still camera, which is what a scene "
                "without via=/orbit= already is" % (name, len(keys)))
        if len(keys) > MAX_WAYPOINTS:
            raise LaunchError(
                "bench scene '%s': %d waypoints, but one "
                "TORIRS_WEDGE_CAM_PATH takes at most %d"
                % (name, len(keys), MAX_WAYPOINTS))

        default_frames = {
            "loop": sample_frames,
            # A pingpong's cycle is there AND back, so half a window each way
            # puts exactly one cycle in a sample.
            "pingpong": sample_frames // 2,
            # `hold` runs once and stops, so its natural length is the warmup:
            # the camera flies in, and every measured sample is the settled
            # view it arrived at.
            "hold": max(warmup * sample_frames, 1),
        }[wrap]
        frames = _int(setting("motion_frames", default_frames),
                      "motion_frames", name)
        if frames <= 0:
            raise LaunchError(
                "bench scene '%s': motion_frames must be positive" % name)

        motion = Motion(mode, wrap, frames, keys, source)

        # The rule that makes a moving camera measurable at all: a sample
        # window has to hold a WHOLE number of camera cycles. Otherwise every
        # window covers a different arc -- window 1 the dense north side,
        # window 2 the empty south -- and the per-window percentiles stop
        # being repeats of one measurement. They become four measurements of
        # four different scenes, and both --repeat and --baseline turn into
        # noise that no amount of averaging removes.
        cycle = motion.period
        if cycle is not None and sample_frames % cycle:
            raise LaunchError(
                "bench scene '%s': one camera cycle is %d frames but a sample "
                "is %d, so each sample would cover a different part of the "
                "route and the samples would not be comparable.\n"
                "  set motion_frames=%d, or make sample_frames a multiple "
                "of %d"
                % (name, cycle, sample_frames,
                   sample_frames // (2 if wrap == "pingpong" else 1), cycle))
    elif fields.get("motion") or fields.get("wrap") or fields.get("motion_frames"):
        raise LaunchError(
            "bench scene '%s': motion=/wrap=/motion_frames= say how the camera "
            "moves, but nothing says where -- add via= or orbit=" % name)

    return Scene(
        name=name,
        description=" ".join(repeated("description")),
        squares=squares,
        eye=eye,
        pitch=pitch,
        yaw=yaw,
        canvas=_canvas(setting("canvas", DEFAULT_CANVAS), name),
        sample_frames=sample_frames,
        samples=samples,
        warmup=warmup,
        at=at,
        motion=motion,
    )


# ------------------------------------------------------------------- a run
class Run:
    """One client process: one scene, one renderer, one repetition."""

    def __init__(self, scene, renderer, repetition, out_dir):
        self.scene = scene
        self.renderer = renderer
        self.repetition = repetition
        self.stem = "%s.%s" % (scene.name, renderer)
        if repetition > 0:
            self.stem += ".r%d" % (repetition + 1)
        self.csv_path = os.path.join(out_dir, self.stem + ".csv")
        self.log_path = os.path.join(out_dir, self.stem + ".log")
        # Per RUN, not per suite. Every scene takes its shot at the same
        # frame number, and the client names the file after that number,
        # so one shared directory means six runs writing six times to
        # frame_00299.bmp and only the last one surviving.
        self.shot_dir = os.path.join(out_dir, "shots", self.stem)

    @property
    def windows_csv_path(self):
        # torirs_perf.c window_csv_open appends this to TORIRS_PERF_CSV.
        return self.csv_path + ".windows.csv"

    def env(self, shots=False):
        """The environment that pins this scene, on top of the profile's own."""
        env = {
            "TORIRS_WORLD_MAP": self.scene.world_map_env(),
            "TORIRS_WEDGE_CAM": self.scene.wedge_cam_env(),
            "TORIRS_MAX_FRAMES": str(self.scene.total_frames),
            "TORIRS_PERF": "1",
            "TORIRS_PERF_CSV": self.csv_path,
            "TORIRS_PERF_WINDOW": str(self.scene.sample_frames),
            # No window manager, no compositor, no vsync borrowed from a
            # desktop. Ignored by the Win32 GDI lane, which has no SDL video
            # driver to point anywhere.
            "SDL_VIDEODRIVER": "dummy",
        }
        path = self.scene.wedge_cam_path_env()
        if path:
            # Set alongside TORIRS_WEDGE_CAM, not instead of it: the path
            # wins in app.c, and the still stays as what the client falls
            # back to if it ever cannot read the route.
            env["TORIRS_WEDGE_CAM_PATH"] = path
        if shots:
            # One frame, taken at the end of the warmup — the same camera the
            # samples are measured through. This is how a scene author checks
            # that `at`/`look` frame what they meant rather than a hillside.
            env["TORIRS_BMP_SERIES"] = "%s,%d,1,1" % (
                self.shot_dir, max(self.scene.warmup * self.scene.sample_frames - 1, 0))
        return env

    def args(self):
        # --offline and --uncapped are also in the bench profile's [args],
        # so that `./launch run` on the same profile boots the client these
        # numbers came from. Repeated here because the RUNNER has to
        # guarantee them: a benchmark that measures a paced frame loop, or
        # one waiting on a login, is measuring the wrong thing, and that
        # cannot be left to a profile to remember. Both flags are
        # idempotent, so stating them twice costs nothing.
        width, height = self.scene.canvas
        return [
            RENDERER_FLAGS[self.renderer],
            "--offline",
            "--uncapped",
            # Resizable, not fixed: in fixed mode the tree is laid out at the
            # classic 765x503 and the finished frame is scaled to the window,
            # so a scene with canvas=1440x900 would measure a 765x503 raster
            # and a stretch. Here the canvas IS the window and `canvas=` is
            # the pixel count the rasteriser writes.
            "--windowmode", "resizable",
            "--window", "%dx%d" % (width, height),
        ]


def plan_runs(suite, scenes, renderers, repeat, out_dir):
    """Every client process this suite implies, in the order they will run.

    Scene-major: all renderers for one scene before moving on, so a partial run
    still holds a complete comparison for the scenes it reached.
    """
    runs = []
    for scene in scenes:
        for renderer in renderers:
            for repetition in range(repeat):
                runs.append(Run(scene, renderer, repetition, out_dir))
    return runs


# --------------------------------------------------------------- the CSVs
def read_windows(path):
    """Per-window samples out of a `<csv>.windows.csv`, keyed by window index.

    Each window is one SAMPLE: torirs_perf.c flushes stage percentiles and
    counter deltas every `TORIRS_PERF_WINDOW` frames, so the file already
    contains the repeated measurement this harness wants, taken inside one
    process at one camera.
    """
    if not os.path.isfile(path):
        return {}
    samples = {}
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        header = handle.readline().strip().split(",")
        try:
            index_of = {name: position for position, name in enumerate(header)}
            kind_at = index_of["kind"]
            name_at = index_of["name"]
            window_at = index_of["window"]
        except KeyError:
            return {}
        for line in handle:
            row = line.rstrip("\n").split(",")
            if len(row) <= window_at:
                continue
            try:
                window = int(row[window_at])
            except ValueError:
                continue
            entry = samples.setdefault(window, {})
            kind, name = row[kind_at], row[name_at]

            def number(column):
                raw = row[index_of[column]].strip() if index_of[column] < len(row) else ""
                return float(raw) if raw else None

            if kind == "window_stage":
                entry["stage:" + name] = {
                    "mean_ns": number("mean_ns"), "p50_ns": number("p50_ns"),
                    "p95_ns": number("p95_ns"), "max_ns": number("max_ns")}
            elif kind == "window_cpu":
                entry["cpu"] = {
                    "mean_ns": number("mean_ns"), "p50_ns": number("p50_ns"),
                    "p95_ns": number("p95_ns"), "max_ns": number("max_ns")}
            elif kind in ("window_counter", "window_gauge"):
                entry["counter:" + name] = number("per_frame")
    return samples


def _median(values):
    values = [value for value in values if value is not None]
    return statistics.median(values) if values else None


def _max(values):
    values = [value for value in values if value is not None]
    return max(values) if values else None


def summarise(run, samples):
    """Collapse a run's kept samples into the row the report prints.

    `p95` is the MEDIAN of the samples' p95s and `p95_worst` the largest of
    them. Reporting only the median hides a scene that is fine three windows
    out of four, and reporting only the worst makes every run look like its
    unluckiest window.
    """
    scene = run.scene
    kept = sorted(index for index in samples if index >= scene.warmup)
    row = {
        "scene": scene.name,
        "renderer": run.renderer,
        "repetition": run.repetition,
        "squares": ["%d,%d" % square for square in scene.squares],
        "scene_size": scene.scene_size,
        "canvas": "%dx%d" % scene.canvas,
        "eye": list(scene.eye),
        "pitch": scene.pitch,
        "yaw": scene.yaw,
        # The resolved waypoint list, not the manifest key that produced
        # it. An `orbit=` is generated from a sin/cos sweep, so recording
        # the request would leave the run reproducible only by whoever has
        # the same libm; recording the waypoints leaves it reproducible.
        "motion": ({
            "mode": scene.motion.mode,
            "wrap": scene.motion.wrap,
            "frames": scene.motion.frames,
            "source": scene.motion.source,
            "waypoints": [list(key) for key in scene.motion.keys],
        } if scene.motion else None),
        "sample_frames": scene.sample_frames,
        "samples_kept": len(kept),
        "samples_discarded": len(samples) - len(kept),
        "csv": os.path.basename(run.csv_path),
        "stages": {},
        "counters": {},
        "cpu": {},
    }
    if not kept:
        return row

    for stage in REPORT_STAGES:
        key = "stage:" + stage
        present = [samples[index][key] for index in kept if key in samples[index]]
        if not present:
            continue
        row["stages"][stage] = {
            "mean_ms": _ms(_median([entry["mean_ns"] for entry in present])),
            "p50_ms": _ms(_median([entry["p50_ns"] for entry in present])),
            "p95_ms": _ms(_median([entry["p95_ns"] for entry in present])),
            "p95_worst_ms": _ms(_max([entry["p95_ns"] for entry in present])),
            "max_ms": _ms(_max([entry["max_ns"] for entry in present])),
        }

    cpu = [samples[index]["cpu"] for index in kept if "cpu" in samples[index]]
    if cpu:
        row["cpu"] = {
            "mean_ms": _ms(_median([entry["mean_ns"] for entry in cpu])),
            "p95_ms": _ms(_median([entry["p95_ns"] for entry in cpu])),
            "p95_worst_ms": _ms(_max([entry["p95_ns"] for entry in cpu])),
        }

    for counter in REPORT_COUNTERS:
        key = "counter:" + counter
        present = [samples[index][key] for index in kept if key in samples[index]]
        if present:
            row["counters"][counter] = _median(present)
    return row


def _ms(nanoseconds):
    return None if nanoseconds is None else round(nanoseconds / 1e6, 3)


# ------------------------------------------------------------- the report
def _stage(row, stage, metric):
    return row.get("stages", {}).get(stage, {}).get(metric)


def _fmt(value, places=2):
    return "-" if value is None else ("%.*f" % (places, value))


# The report columns, as (heading, width, extractor). `frame` first because it
# is the only number that cannot be gamed by moving work between stages.
TABLE_COLUMNS = (
    ("scene", 24, lambda row: row["scene"]),
    ("renderer", 9, lambda row: row["renderer"]),
    ("frame p50", 10, lambda row: _fmt(_stage(row, "frame", "p50_ms"))),
    ("frame p95", 10, lambda row: _fmt(_stage(row, "frame", "p95_ms"))),
    ("worst p95", 10, lambda row: _fmt(_stage(row, "frame", "p95_worst_ms"))),
    ("fps", 7, lambda row: _fps(row)),
    ("render", 8, lambda row: _fmt(_stage(row, "render", "p50_ms"))),
    ("build", 8, lambda row: _fmt(_stage(row, "build", "p50_ms"))),
    ("paint", 8, lambda row: _fmt(_stage(row, "paint", "p50_ms"))),
    ("cmds", 9, lambda row: _fmt(row.get("counters", {}).get("painter_commands"), 0)),
    ("n", 3, lambda row: str(row.get("samples_kept", 0))),
)


def _fps(row):
    mean = _stage(row, "frame", "mean_ms")
    if not mean:
        return "-"
    return "%.1f" % (1000.0 / mean)


def format_table(rows):
    """The per-run table, milliseconds, one line per (scene, renderer)."""
    lines = []
    header = "  ".join(name.ljust(width) for name, width, _ in TABLE_COLUMNS)
    lines.append(header.rstrip())
    lines.append("-" * len(header.rstrip()))
    for row in rows:
        cells = []
        for _, width, extract in TABLE_COLUMNS:
            cells.append(str(extract(row)).ljust(width))
        lines.append("  ".join(cells).rstrip())
    return "\n".join(lines)


def format_deltas(rows, baseline_rows):
    """Per-run change against an earlier summary. Positive percent = slower.

    Matched on (scene, renderer, repetition). A row the baseline does not have
    is listed as new rather than dropped -- a comparison that quietly omits the
    scene somebody just added reads as "that scene is fine".
    """
    index = {}
    for row in baseline_rows:
        index[(row["scene"], row["renderer"], row.get("repetition", 0))] = row

    lines = ["%-24s  %-9s  %10s  %10s  %10s"
             % ("scene", "renderer", "frame p50", "render p50", "cmds")]
    lines.append("-" * len(lines[0]))
    for row in rows:
        was = index.get((row["scene"], row["renderer"], row.get("repetition", 0)))
        if was is None:
            lines.append("%-24s  %-9s  %s"
                         % (row["scene"], row["renderer"], "(new -- not in baseline)"))
            continue
        lines.append("%-24s  %-9s  %10s  %10s  %10s" % (
            row["scene"], row["renderer"],
            _delta(_stage(row, "frame", "p50_ms"), _stage(was, "frame", "p50_ms")),
            _delta(_stage(row, "render", "p50_ms"), _stage(was, "render", "p50_ms")),
            _delta(row.get("counters", {}).get("painter_commands"),
                   was.get("counters", {}).get("painter_commands"))))
    missing = [key for key in index
               if key not in {(row["scene"], row["renderer"],
                               row.get("repetition", 0)) for row in rows}]
    for scene, renderer, _ in sorted(missing):
        lines.append("%-24s  %-9s  (in baseline, not run now)" % (scene, renderer))
    return "\n".join(lines)


def _delta(now, before):
    if now is None or before is None:
        return "-"
    if not before:
        return "-"
    percent = (now - before) / before * 100.0
    return "%+.1f%%" % percent


def over_budget(rows, budget_ms):
    """Runs whose median frame p95 exceeds the suite's stated budget.

    A gate, not a report: a suite that states `budget_ms` wants a non-zero exit
    when a scene crosses it, so it can be run by something that is not a person.
    """
    assert budget_ms is not None
    breached = []
    for row in rows:
        value = _stage(row, "frame", "p95_ms")
        if value is not None and value > budget_ms:
            breached.append((row, value))
    return breached
