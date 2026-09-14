#ifndef SRC_RENDER_TORIRS_WEDGE_CAMERA_PATH_H
#define SRC_RENDER_TORIRS_WEDGE_CAMERA_PATH_H

/**
 * Scripted camera routes for draw-order and frame-cost captures --
 * TORIRS_WEDGE_CAM_PATH.
 *
 *   TORIRS_WEDGE_CAM_PATH=<mode>,<frames>,<wrap>;x,y,z,pitch,yaw;x,y,z,...
 *
 * <mode> is `linear` (straight legs between waypoints) or `spline` (a
 * Catmull-Rom curve THROUGH them). <frames> is how many frames one full
 * traversal takes. <wrap> is `loop` (waypoint N joins back to waypoint 0),
 * `pingpong` (walk it back the way it came) or `hold` (stop at the end).
 * Two waypoints and up; one waypoint is TORIRS_WEDGE_CAM with extra syntax.
 *
 * The phase is a function of the FRAME ORDINAL, never of the clock. A camera
 * driven by elapsed time would make the geometry depend on how fast the
 * machine drew it, so a slower renderer would be measured over a different
 * scene than the faster one it is being compared against -- and the counter
 * columns would stop being the cross-check they exist to be. Frame-indexed,
 * two runs of a scene traverse it identically whatever they cost.
 *
 * Angles are the C client's 2048-per-turn units (the official client's
 * 16384-per-turn value divided by 8). Positions are world fine units.
 *
 * Nothing here reads the clock, the environment, or any client state: a path
 * is parsed from a string and evaluated at a frame ordinal, which is what
 * makes it testable without a client. Reading the env var and applying the
 * result to the camera is the caller's half.
 */

#define TORIRS_WEDGE_CAMERA_PATH_MAX 32

enum ToriRS_WedgeCameraInterpolation
{
    TORIRS_WEDGE_CAMERA_LINEAR = 0,
    TORIRS_WEDGE_CAMERA_SPLINE = 1
};

enum ToriRS_WedgeCameraWrap
{
    TORIRS_WEDGE_CAMERA_LOOP = 0,
    TORIRS_WEDGE_CAMERA_PINGPONG = 1,
    TORIRS_WEDGE_CAMERA_HOLD = 2
};

struct ToriRS_WedgeCameraKey
{
    int x;
    int y;
    int z;
    int pitch;
    int yaw;
};

struct ToriRS_WedgeCameraPath
{
    /** enum ToriRS_WedgeCameraInterpolation */
    int mode;
    /** enum ToriRS_WedgeCameraWrap */
    int wrap;
    int count;
    long frames;
    /** Net yaw carried by one full traversal, so a looping orbit keeps turning
     *  the same way across the seam instead of unwinding at it. Zero for a path
     *  that ends facing where it started. */
    long turn;
    struct ToriRS_WedgeCameraKey keys[TORIRS_WEDGE_CAMERA_PATH_MAX];
};

/**
 * Parse a whole route spec, or none of it.
 *
 * Returns 1 only if the whole spec parsed; `out_path` is then complete.
 * Never a partial path: half a route read as a whole one is a camera that
 * quietly measures somewhere else.
 */
int
ToriRS_WedgeCameraPathParse(
    char const* spec,
    struct ToriRS_WedgeCameraPath* out_path);

/**
 * The camera at frame ordinal `frame`, with both angles normalised into
 * [0, 2048).
 */
void
ToriRS_WedgeCameraPathEval(
    struct ToriRS_WedgeCameraPath const* path,
    long frame,
    struct ToriRS_WedgeCameraKey* out_key);

#endif /* SRC_RENDER_TORIRS_WEDGE_CAMERA_PATH_H */
