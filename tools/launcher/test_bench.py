"""Tests for the scene benchmark suite.

The two things worth pinning here are the ones a wrong answer makes INVISIBLE:
the tile-to-scene arithmetic (a camera off by a square still renders something,
just not the scene the name promises) and the repeated-key parse (a four-square
scene silently collapsing to one square looks exactly like a renderer that got
faster).
"""

import os
import tempfile
import unittest

from .bench import (MAX_SQUARES, MAX_WAYPOINTS, Run, Scene, load_suite,
                    plan_runs, read_windows, summarise)
from .iniparse import Ini
from .profiles import LaunchError, Manifest


def manifest_from(text):
    return Manifest("manifests/test.ini", Ini.loads(text, "manifests/test.ini"))


SUITE = """
[cache:boot]
dir=../cache.osrs239
revision=239

[bench]
renderers=soft3d
sample_frames=100
samples=3
warmup=1
canvas=640x480

[bench:one-square]
description=a single square
at=3220,3170

[bench:block]
description=four squares
description=and a second description line
map=50,50
map=51,50
map=50,51
map=51,51
at=3222,3218
look=280,64
canvas=800x600
samples=5
"""


class SuiteParsing(unittest.TestCase):
    def setUp(self):
        self.suite = load_suite(manifest_from(SUITE))
        self.scenes = {scene.name: scene for scene in self.suite.scenes}

    def test_every_scene_is_read(self):
        self.assertEqual([scene.name for scene in self.suite.scenes],
                         ["one-square", "block"])

    def test_repeated_map_keys_all_survive(self):
        # dict(items) would keep only the last, turning a 128x128 scene into a
        # 64x64 one with no visible sign that it happened.
        self.assertEqual(self.scenes["block"].squares,
                         [(50, 50), (51, 50), (50, 51), (51, 51)])
        self.assertEqual(self.scenes["block"].scene_size, 128)

    def test_repeated_description_keys_join(self):
        self.assertEqual(self.scenes["block"].description,
                         "four squares and a second description line")

    def test_at_alone_derives_its_square(self):
        self.assertEqual(self.scenes["one-square"].squares, [(50, 49)])
        self.assertEqual(self.scenes["one-square"].scene_size, 64)

    def test_bench_defaults_apply_and_scenes_override_them(self):
        self.assertEqual(self.scenes["one-square"].canvas, (640, 480))
        self.assertEqual(self.scenes["one-square"].samples, 3)
        self.assertEqual(self.scenes["block"].canvas, (800, 600))
        self.assertEqual(self.scenes["block"].samples, 5)
        # Not overridden, so still the [bench] value.
        self.assertEqual(self.scenes["block"].sample_frames, 100)

    def test_look_defaults_to_the_offline_first_look(self):
        self.assertEqual(
            (self.scenes["one-square"].pitch, self.scenes["one-square"].yaw),
            (450, 0))
        self.assertEqual((self.scenes["block"].pitch, self.scenes["block"].yaw),
                         (280, 64))

    def test_the_cache_comes_from_the_world(self):
        self.assertTrue(self.suite.cache_dir.endswith("cache.osrs239"))

    def test_select_names_the_scenes_a_world_has(self):
        self.assertEqual([scene.name for scene in self.suite.select(["block"])],
                         ["block"])
        with self.assertRaises(LaunchError) as caught:
            self.suite.select(["blcok"])
        self.assertIn("blcok", str(caught.exception))
        # The message has to list what IS there, or the typo costs a second
        # command to find out.
        self.assertIn("one-square", str(caught.exception))


class SceneGeometry(unittest.TestCase):
    def scene(self, text):
        suite = load_suite(manifest_from(
            "[cache:boot]\ndir=../c\n[bench:s]\n" + text))
        return suite.scenes[0]

    def test_eye_lands_on_the_tile_centre(self):
        # Square 50,49 bases the scene at tile 3200,3136. Tile 3220,3170 is
        # therefore local 20,34, and the eye sits at the centre of it.
        scene = self.scene("at=3220,3170\n")
        self.assertEqual(scene.eye, (20 * 128 + 64, -2000, 34 * 128 + 64))

    def test_the_scene_bases_at_the_sw_most_square(self):
        scene = self.scene("map=51,51\nmap=50,50\nmap=51,50\nmap=50,51\n"
                           "at=3222,3218\n")
        self.assertEqual(scene.base_tile, (3200, 3200))
        self.assertEqual(scene.eye, (22 * 128 + 64, -2000, 18 * 128 + 64))

    def test_height_moves_only_the_eye(self):
        scene = self.scene("at=3220,3170\nheight=-3500\n")
        self.assertEqual(scene.eye[1], -3500)

    def test_eye_overrides_what_at_would_derive(self):
        scene = self.scene("at=3220,3170\neye=100,-900,200\n")
        self.assertEqual(scene.eye, (100, -900, 200))

    def test_squares_without_at_look_at_the_scene_centre(self):
        scene = self.scene("map=50,50\nmap=51,50\nmap=50,51\nmap=51,51\n")
        self.assertEqual(scene.eye, (64 * 128 + 64, -2000, 64 * 128 + 64))

    def test_a_tile_outside_its_own_squares_is_an_error(self):
        # The client would build the scene and put the camera off the edge of
        # it, which renders a horizon rather than an error.
        with self.assertRaises(LaunchError) as caught:
            self.scene("map=50,50\nat=3164,3486\n")
        self.assertIn("outside", str(caught.exception))

    def test_a_square_named_twice_is_an_error(self):
        with self.assertRaises(LaunchError) as caught:
            self.scene("map=50,50\nmap=50,50\nat=3222,3218\n")
        self.assertIn("more than once", str(caught.exception))

    def test_a_scene_with_no_location_at_all_is_an_error(self):
        with self.assertRaises(LaunchError):
            self.scene("description=nowhere\n")

    def test_the_square_list_cap_matches_the_client(self):
        squares = "".join("map=%d,%d\n" % (50 + n % 5, 50 + n // 5)
                          for n in range(MAX_SQUARES + 1))
        with self.assertRaises(LaunchError) as caught:
            self.scene(squares)
        self.assertIn(str(MAX_SQUARES), str(caught.exception))

    def test_a_malformed_pair_names_the_scene_and_the_key(self):
        with self.assertRaises(LaunchError) as caught:
            self.scene("at=3220\n")
        self.assertIn("'s'", str(caught.exception))
        self.assertIn("at", str(caught.exception))


class RunShape(unittest.TestCase):
    def setUp(self):
        self.scene = Scene(
            name="here", description="", squares=[(50, 50), (51, 50)],
            eye=(2624, -2000, 2368), pitch=280, yaw=64, canvas=(765, 503),
            sample_frames=100, samples=3, warmup=1, at=(3220, 3218))
        self.run = Run(self.scene, "soft3d", 0, os.path.join("build", "bench"))

    def test_the_env_pins_the_map_and_the_camera(self):
        env = self.run.env()
        self.assertEqual(env["TORIRS_WORLD_MAP"], "50,50;51,50")
        self.assertEqual(env["TORIRS_WEDGE_CAM"], "2624,-2000,2368,280,64")

    def test_the_frame_cap_covers_warmup_plus_every_sample(self):
        self.assertEqual(self.scene.total_frames, 400)
        self.assertEqual(self.run.env()["TORIRS_MAX_FRAMES"], "400")
        self.assertEqual(self.run.env()["TORIRS_PERF_WINDOW"], "100")

    def test_the_shot_is_taken_at_the_end_of_warmup(self):
        # One frame, at the camera the samples are measured through — not at
        # frame 0, where the scene may not have finished loading.
        self.assertIn(",99,1,1", self.run.env(shots=True)["TORIRS_BMP_SERIES"])
        self.assertNotIn("TORIRS_BMP_SERIES", self.run.env())

    def test_the_args_state_the_renderer_and_the_canvas(self):
        args = self.run.args()
        self.assertIn("--soft3d", args)
        self.assertIn("--offline", args)
        self.assertIn("--uncapped", args)
        self.assertIn("765x503", args)

    def test_the_window_csv_is_where_the_client_writes_it(self):
        self.assertEqual(self.run.windows_csv_path, self.run.csv_path + ".windows.csv")

    def test_runs_do_not_share_a_shot_directory(self):
        # The client names a shot after its frame number, and every scene with
        # the same warmup takes its shot at the same one -- so a shared
        # directory leaves one BMP behind for the whole suite.
        other = Run(self.scene, "d3d9", 0, os.path.join("build", "bench"))
        self.assertNotEqual(other.shot_dir, self.run.shot_dir)

    def test_repetitions_do_not_share_a_csv(self):
        second = Run(self.scene, "soft3d", 1, "out")
        self.assertNotEqual(second.csv_path, self.run.csv_path)
        self.assertIn("r2", second.stem)


class RunPlanning(unittest.TestCase):
    def test_scene_major_order_so_a_partial_run_still_compares(self):
        suite = load_suite(manifest_from(SUITE))
        runs = plan_runs(suite, suite.scenes, ["soft3d", "d3d9"], 1, "out")
        self.assertEqual(
            [(run.scene.name, run.renderer) for run in runs],
            [("one-square", "soft3d"), ("one-square", "d3d9"),
             ("block", "soft3d"), ("block", "d3d9")])

    def test_repeat_multiplies_processes(self):
        suite = load_suite(manifest_from(SUITE))
        runs = plan_runs(suite, suite.scenes[:1], ["soft3d"], 3, "out")
        self.assertEqual(len(runs), 3)
        self.assertEqual(len({run.csv_path for run in runs}), 3)

    def test_an_unknown_renderer_is_refused_at_parse_time(self):
        with self.assertRaises(LaunchError) as caught:
            load_suite(manifest_from(
                "[cache:boot]\ndir=../c\n[bench]\nrenderers=vulkan\n"
                "[bench:s]\nat=3220,3170\n"))
        self.assertIn("vulkan", str(caught.exception))


WINDOWS_CSV = """kind,name,count,mean_ns,p50_ns,p95_ns,max_ns,per_frame,window
window_stage,frame,100,9000000,8500000,12000000,20000000,,0
window_stage,render,100,7000000,6800000,9000000,15000000,,0
window_counter,painter_commands,100,,,,,9999,0
window_stage,frame,100,5000000,4800000,6000000,9000000,,1
window_stage,render,100,4000000,3900000,5000000,7000000,,1
window_counter,painter_commands,100,,,,,4000,1
window_stage,frame,100,5200000,5000000,7000000,11000000,,2
window_stage,render,100,4200000,4100000,5400000,8000000,,2
window_counter,painter_commands,100,,,,,4020,2
"""


class Aggregation(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.scene = Scene(
            name="here", description="", squares=[(50, 50)], eye=(0, -2000, 0),
            pitch=450, yaw=0, canvas=(765, 503), sample_frames=100, samples=2,
            warmup=1)
        self.run = Run(self.scene, "soft3d", 0, self.dir)
        with open(self.run.windows_csv_path, "w", encoding="utf-8") as handle:
            handle.write(WINDOWS_CSV)

    def test_warmup_windows_are_discarded(self):
        row = summarise(self.run, read_windows(self.run.windows_csv_path))
        self.assertEqual(row["samples_kept"], 2)
        self.assertEqual(row["samples_discarded"], 1)
        # Window 0's 12 ms p95 is the boot window. If it leaked into the
        # report every scene would look like it stutters.
        self.assertEqual(row["stages"]["frame"]["p95_ms"], 6.5)
        self.assertEqual(row["stages"]["frame"]["p95_worst_ms"], 7.0)

    def test_percentiles_are_medians_across_the_kept_samples(self):
        row = summarise(self.run, read_windows(self.run.windows_csv_path))
        self.assertEqual(row["stages"]["frame"]["p50_ms"], 4.9)
        self.assertEqual(row["stages"]["render"]["p50_ms"], 4.0)

    def test_counters_come_through_so_a_lighter_scene_is_visible(self):
        row = summarise(self.run, read_windows(self.run.windows_csv_path))
        self.assertEqual(row["counters"]["painter_commands"], 4010)

    def test_a_run_that_wrote_no_csv_reports_no_samples(self):
        empty = Run(self.scene, "soft3d", 9, self.dir)
        row = summarise(empty, read_windows(empty.windows_csv_path))
        self.assertEqual(row["samples_kept"], 0)
        self.assertEqual(row["stages"], {})


if __name__ == "__main__":
    unittest.main()


class CameraMotion(unittest.TestCase):
    """Routes: waypoints in, scene-local camera keys out.

    The one that matters most here is the ring radius. A generated orbit is the
    only route nobody writes out by hand, so it is the only one whose waypoints
    nobody reads — and the first version of it snapped to whole tiles, which
    pulsed the radius by 6% and would have shown up as a scene that breathes
    rather than as a bug.
    """

    def scene(self, text, head=""):
        suite = load_suite(manifest_from(
            "[cache:boot]\ndir=../c\n" + head + "[bench:s]\n" + text))
        return suite.scenes[0]

    def test_a_scene_without_waypoints_does_not_move(self):
        self.assertIsNone(self.scene("at=3220,3170\n").motion)

    def test_via_waypoints_become_scene_local_eyes(self):
        # Square 50,49 bases the scene at tile 3200,3136, so tile 3220,3170 is
        # local 20,34 and lands on that tile's centre.
        motion = self.scene(
            "at=3220,3170\nvia=3220,3170\nvia=3230,3180\n").motion
        self.assertEqual(motion.keys[0][:3],
                         (20 * 128 + 64, -2000, 34 * 128 + 64))
        self.assertEqual(motion.keys[1][:3],
                         (30 * 128 + 64, -2000, 44 * 128 + 64))

    def test_a_two_number_waypoint_keeps_the_scene_look(self):
        motion = self.scene(
            "at=3220,3170\nlook=300,64\nvia=3220,3170\nvia=3230,3180\n").motion
        for key in motion.keys:
            self.assertEqual((key[3], key[4]), (300, 64))

    def test_a_four_number_waypoint_turns_the_camera(self):
        motion = self.scene(
            "at=3220,3170\nvia=3220,3170,280,512\nvia=3230,3180,300,1024\n").motion
        self.assertEqual(motion.keys[0][3:], (280, 512))
        self.assertEqual(motion.keys[1][3:], (300, 1024))

    def test_a_five_number_waypoint_moves_the_height_too(self):
        motion = self.scene(
            "at=3220,3170\nvia=3220,3170,280,0,-900\nvia=3230,3180\n").motion
        self.assertEqual(motion.keys[0][1], -900)
        self.assertEqual(motion.keys[1][1], -2000)

    def test_an_orbit_holds_its_radius(self):
        # The bug this pins: rounding waypoints to whole TILES puts the
        # diagonals of a radius-12 ring 11.3 tiles out.
        scene = self.scene("at=3220,3170\norbit=10,8\nmotion=spline\n")
        centre = (20 * 128 + 64, 34 * 128 + 64)
        for key in scene.motion.keys:
            radius = ((key[0] - centre[0]) ** 2 + (key[2] - centre[1]) ** 2) ** 0.5
            self.assertAlmostEqual(radius, 10 * 128, delta=1.0)

    def test_an_orbit_faces_what_it_circles(self):
        scene = self.scene("at=3220,3170\norbit=10,8\n")
        keys = scene.motion.keys
        self.assertEqual(len(keys), 8)
        # Waypoint 0 is due north of the centre (+z), so it looks back down -z,
        # which is half a turn from yaw 0.
        self.assertAlmostEqual(keys[0][4], 1024, delta=2)
        # And every step turns the same way by an eighth of a turn.
        for index in range(len(keys)):
            step = (keys[(index + 1) % len(keys)][4] - keys[index][4]) % 2048
            self.assertAlmostEqual(step, 2048 - 256, delta=2)

    def test_via_and_orbit_together_are_refused(self):
        with self.assertRaises(LaunchError) as caught:
            self.scene("at=3220,3170\norbit=10,8\nvia=3220,3170\n")
        self.assertIn("keep one", str(caught.exception))

    def test_an_orbit_needs_something_to_circle(self):
        with self.assertRaises(LaunchError):
            self.scene("map=50,49\norbit=10,8\n")

    def test_a_waypoint_off_the_meshed_squares_is_an_error(self):
        with self.assertRaises(LaunchError) as caught:
            self.scene("at=3220,3170\nvia=3220,3170\nvia=9999,9999\n")
        self.assertIn("outside", str(caught.exception))

    def test_an_orbit_that_leaves_the_scene_is_an_error(self):
        with self.assertRaises(LaunchError) as caught:
            self.scene("at=3220,3170\norbit=60,8\n")
        self.assertIn("outside", str(caught.exception))

    def test_one_waypoint_is_not_a_route(self):
        with self.assertRaises(LaunchError) as caught:
            self.scene("at=3220,3170\nvia=3220,3170\n")
        self.assertIn("at least 2 waypoints", str(caught.exception))

    def test_the_waypoint_cap_matches_the_client(self):
        vias = "".join("via=%d,3170\n" % (3201 + n)
                       for n in range(MAX_WAYPOINTS + 1))
        with self.assertRaises(LaunchError) as caught:
            self.scene("at=3220,3170\n" + vias)
        self.assertIn(str(MAX_WAYPOINTS), str(caught.exception))

    def test_motion_without_a_route_is_an_error(self):
        with self.assertRaises(LaunchError) as caught:
            self.scene("at=3220,3170\nmotion=spline\n")
        self.assertIn("nothing says where", str(caught.exception))

    def test_an_unknown_mode_or_wrap_is_named(self):
        for bad in ("motion=cubic\n", "wrap=bounce\n"):
            with self.assertRaises(LaunchError):
                self.scene("at=3220,3170\nvia=3220,3170\nvia=3230,3180\n" + bad)

    def test_a_loop_defaults_to_one_traversal_per_sample(self):
        scene = self.scene(
            "at=3220,3170\nsample_frames=300\nvia=3220,3170\nvia=3230,3180\n")
        self.assertEqual(scene.motion.frames, 300)
        self.assertEqual(scene.motion.period, 300)

    def test_a_pingpong_defaults_to_one_round_trip_per_sample(self):
        scene = self.scene(
            "at=3220,3170\nsample_frames=300\nwrap=pingpong\n"
            "via=3220,3170\nvia=3230,3180\n")
        # There and back is the cycle, so half a window each way.
        self.assertEqual(scene.motion.frames, 150)
        self.assertEqual(scene.motion.period, 300)

    def test_a_hold_flies_in_during_the_warmup(self):
        scene = self.scene(
            "at=3220,3170\nsample_frames=300\nwarmup=2\nwrap=hold\n"
            "via=3220,3170\nvia=3230,3180\n")
        self.assertEqual(scene.motion.frames, 600)
        # It never comes back round, so no sample can straddle a seam.
        self.assertIsNone(scene.motion.period)

    def test_a_cycle_that_straddles_a_sample_is_refused(self):
        # THE rule. 300 frames of sample holding 1.5 laps means sample 1 covers
        # the north half and sample 2 the south, and the two stop being repeats
        # of one measurement.
        with self.assertRaises(LaunchError) as caught:
            self.scene(
                "at=3220,3170\nsample_frames=300\nmotion_frames=200\n"
                "via=3220,3170\nvia=3230,3180\n")
        self.assertIn("not be comparable", str(caught.exception))

    def test_several_whole_cycles_in_a_sample_are_fine(self):
        scene = self.scene(
            "at=3220,3170\nsample_frames=300\nmotion_frames=100\n"
            "via=3220,3170\nvia=3230,3180\n")
        self.assertEqual(scene.motion.frames, 100)

    def test_a_hold_is_not_asked_to_divide_anything(self):
        scene = self.scene(
            "at=3220,3170\nsample_frames=300\nwrap=hold\nmotion_frames=137\n"
            "via=3220,3170\nvia=3230,3180\n")
        self.assertEqual(scene.motion.frames, 137)

    def test_the_env_is_what_the_client_parses(self):
        scene = self.scene(
            "at=3220,3170\nsample_frames=300\nmotion=spline\n"
            "via=3220,3170,280,0\nvia=3230,3180,300,512\n")
        self.assertEqual(
            scene.wedge_cam_path_env(),
            "spline,300,loop;2624,-2000,4416,280,0;3904,-2000,5696,300,512")

    def test_the_run_sets_the_route_and_keeps_the_still(self):
        scene = self.scene(
            "at=3220,3170\nvia=3220,3170\nvia=3230,3180\n")
        env = Run(scene, "soft3d", 0, "out").env()
        self.assertIn("TORIRS_WEDGE_CAM_PATH", env)
        # The still stays: it is what the client falls back to if it ever
        # cannot read the route.
        self.assertEqual(env["TORIRS_WEDGE_CAM"], scene.wedge_cam_env())

    def test_a_still_scene_sets_no_route(self):
        env = Run(self.scene("at=3220,3170\n"), "soft3d", 0, "out").env()
        self.assertNotIn("TORIRS_WEDGE_CAM_PATH", env)

    def test_a_suite_wide_wrap_does_not_move_a_still_scene(self):
        scene = self.scene("at=3220,3170\n",
                           head="[bench]\nmotion=spline\nwrap=pingpong\n")
        self.assertIsNone(scene.motion)

    def test_the_scanline_variant_is_the_same_flag_with_the_selector_pinned(self):
        scene = self.scene("at=3220,3170\n")
        default = Run(scene, "soft3d", 0, "out")
        scanline = Run(scene, "soft3d-scanline", 0, "out")
        self.assertEqual(default.args()[0], "--soft3d")
        self.assertEqual(scanline.args()[0], "--soft3d")
        # Both sides pin the selector: an inherited TORIDRAW_RASTER_SCANLINE=1
        # on the bench machine must not turn the A/B into a B/B.
        self.assertEqual(default.env()["TORIDRAW_RASTER_SCANLINE"], "0")
        self.assertEqual(scanline.env()["TORIDRAW_RASTER_SCANLINE"], "1")
