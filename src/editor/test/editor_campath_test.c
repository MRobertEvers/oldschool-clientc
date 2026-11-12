/*
 * The Camera path panel's pure half (editor/editor_campath.h), content-free:
 *
 *   codec    parse -> emit byte-identical (comments, blank lines, CRLF, a
 *            missing final newline); the grammar's refusals with their line
 *            (a key before its path, a path opened twice, a segment twice, a
 *            glide that is a cut, a hold of 0); format -> parse
 *   verbs    the document's new text: a first path bringing the header, a
 *            keyframe added in place with every other line kept byte for
 *            byte, a rename, a taken name refused, a delete, the last delete
 *            taking the header back
 *   maths    eye -> keyframe against hand-computed cases on flat ground (the
 *            four compass yaws, a 45 degree pitch), the height measured above
 *            the ground under the EYE tile on a slope, the fitted camera
 *            still looking at the eye's subject from 500 poses over uneven
 *            ground, straight down, looking up refused, off the terrain
 *   easing   the client's ease and yaw ease digit for digit, a snap at
 *            rate2 100, a glide's cycle count, a rate that never arrives
 *   script   the proc in world tiles, instance-relative, unlocked and framed,
 *            byte for byte; the ::cam cheat; the list row
 */

#include "editor/editor_campath.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks;
static int g_failed;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if( !(cond) )                                                                              \
        {                                                                                          \
            g_failed++;                                                                            \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while( 0 )

/* ---- helpers -------------------------------------------------------------------- */

static char*
paths_emit(const struct Editor_CamPathsFile* file)
{
    size_t const n = Editor_CamPathsEmit(file, NULL, 0);
    char* out = malloc(n + 1);
    assert(out);
    Editor_CamPathsEmit(file, out, n + 1);
    return out;
}

static int
round_trip(const char* text)
{
    struct Editor_CamPathsFile file;
    char* out;
    int same;

    Editor_CamPathsInit(&file);
    (void)Editor_CamPathsParse(&file, text, strlen(text));
    out = paths_emit(&file);
    same = strcmp(out, text) == 0;
    free(out);
    Editor_CamPathsFree(&file);
    return same;
}

/** The line a text fails on, 0 when it parses. */
static int
fails_at(const char* text)
{
    struct Editor_CamPathsFile file;
    struct Editor_CamPathsParseResult r;

    Editor_CamPathsInit(&file);
    r = Editor_CamPathsParse(&file, text, strlen(text));
    Editor_CamPathsFree(&file);
    return r.ok ? 0 : r.line;
}

/** `file`'s text with `path` written over `old_name`; NULL when refused. */
static char*
text_with(
    const struct Editor_CamPathsFile* file,
    const char* old_name,
    const struct Editor_CamPath* path)
{
    size_t length = 0;
    char* out;

    if( !Editor_CamPathsTextWith(file, old_name, path, NULL, 0, &length) )
        return NULL;
    out = malloc(length + 1);
    assert(out);
    Editor_CamPathsTextWith(file, old_name, path, out, length + 1, &length);
    return out;
}

static struct Editor_CamKey
key_of(
    int eye_x,
    int eye_z,
    int eye_height,
    int look_x,
    int look_z,
    int look_height,
    int glide,
    int hold)
{
    struct Editor_CamKey key;

    memset(&key, 0, sizeof(key));
    Editor_CamSpeedApply(EDITOR_CAM_SPEED_MEDIUM, &key);
    key.eye_x = eye_x;
    key.eye_z = eye_z;
    key.eye_height = eye_height;
    key.look_x = look_x;
    key.look_z = look_z;
    key.look_height = look_height;
    key.glide = glide;
    key.hold = hold;
    return key;
}

/* ---- grounds ---------------------------------------------------------------------- */

/** Flat at y 0, a hundred squares each way. */
static int
ground_flat(
    void* user,
    int x,
    int z,
    int level,
    int* out_y)
{
    (void)user;
    (void)level;
    if( x < 0 || z < 0 || x >= 6400 * 128 || z >= 6400 * 128 )
        return 0;
    *out_y = 0;
    return 1;
}

/** Rising to the east, one unit up (y down) every four east. */
static int
ground_slope(
    void* user,
    int x,
    int z,
    int level,
    int* out_y)
{
    (void)user;
    (void)level;
    if( x < 0 || z < 0 || x >= 6400 * 128 || z >= 6400 * 128 )
        return 0;
    *out_y = -(x - 3200 * 128) / 4;
    return 1;
}

/** Rolling ground, up to 120 units either way. */
static int
ground_hills(
    void* user,
    int x,
    int z,
    int level,
    int* out_y)
{
    (void)user;
    (void)level;
    if( x < 3200 * 128 || z < 3200 * 128 || x >= 3264 * 128 || z >= 3264 * 128 )
        return 0;
    *out_y = (int)(-120.0 * sin(x / 700.0) * cos(z / 900.0));
    return 1;
}

/** The orbit camera's eye for a pose (render/torirs_world_projection.h
 *  ToriRS_OrbitCameraEye, in doubles): `zoom` back from a ground point. */
static struct Editor_CamEye
orbit_eye(
    int target_x,
    int target_y,
    int target_z,
    int yaw,
    int pitch,
    int zoom)
{
    double const a = yaw * 2.0 * 3.14159265358979323846 / 2048.0;
    double const p = pitch * 2.0 * 3.14159265358979323846 / 2048.0;
    struct Editor_CamEye eye;

    eye.x = target_x + (int)lround(zoom * cos(p) * sin(a));
    eye.y = target_y - (int)lround(zoom * sin(p));
    eye.z = target_z - (int)lround(zoom * cos(p) * cos(a));
    eye.yaw = yaw;
    eye.pitch = pitch;
    return eye;
}

static int
angle_off(
    int a,
    int b)
{
    int d = (a - b) & 0x7ff;
    if( d >= 1024 )
        d = 2048 - d;
    return d;
}

/* ---- the codec -------------------------------------------------------------------- */

static const char k_sample[] =
    "# camera paths\r\n"
    "\r\n"
    "path lumbridge_intro | frame\r\n"
    "key lumbridge_intro | eye 3215,3211,0,1100 | look 3222,3218,180 | cut | hold 4\r\n"
    "  // a hand-written note\n"
    "key lumbridge_intro | hold 8 | glide 5,5,5,5 | look 3222,3219,150 | eye 3230,3214,0,700\n"
    "path arena | coords instance m16_101\n"
    "key arena | eye 1030,6470,1,600 | look 1040,6480,0 | glide 2,3,4,6 | hold 500";

static void
test_codec(void)
{
    struct Editor_CamPathsFile file;
    struct Editor_CamPathsParseResult r;
    struct Editor_CamPath path;
    char line[256];

    CHECK(round_trip(""), "an empty file round-trips");
    CHECK(round_trip("\n"), "a lone newline round-trips");
    CHECK(round_trip(k_sample), "comments, blanks, CRLF and no final newline round-trip");
    CHECK(round_trip("path a\nkey a | bad\n"), "a file that does not parse still emits byte for byte");

    Editor_CamPathsInit(&file);
    r = Editor_CamPathsParse(&file, k_sample, sizeof(k_sample) - 1);
    CHECK(r.ok && r.line == 0, "the sample parses (line %d)", r.line);
    CHECK(Editor_CamPathsCount(&file) == 2, "two paths (%d)", Editor_CamPathsCount(&file));
    CHECK(strcmp(Editor_CamPathsName(&file, 0), "lumbridge_intro") == 0 && strcmp(Editor_CamPathsName(&file, 1), "arena") == 0
              && Editor_CamPathsName(&file, 2) == NULL,
        "in file order");
    CHECK(Editor_CamPathsGet(&file, "lumbridge_intro", &path), "the first is found");
    CHECK(path.frame == 1 && path.instance == 0 && path.key_count == 2, "framed, world tiles, two keyframes (%d)",
        path.key_count);
    CHECK(path.keys[0].eye_x == 3215 && path.keys[0].eye_z == 3211 && path.keys[0].level == 0
              && path.keys[0].eye_height == 1100 && path.keys[0].look_x == 3222 && path.keys[0].look_z == 3218
              && path.keys[0].look_height == 180 && path.keys[0].glide == 0 && path.keys[0].hold == 4,
        "keyframe 1's fields");
    CHECK(path.keys[1].glide == 1 && path.keys[1].move_rate == 5 && path.keys[1].look_rate2 == 5
              && path.keys[1].hold == 8 && path.keys[1].eye_x == 3230,
        "keyframe 2's segments in any order");
    CHECK(Editor_CamPathsGet(&file, "arena", &path) && path.instance == 1 && path.origin_mx == 16
              && path.origin_mz == 101 && path.frame == 0 && path.key_count == 1,
        "the second is instance-relative to m16_101");
    CHECK(path.keys[0].level == 1 && path.keys[0].move_rate == 2 && path.keys[0].move_rate2 == 3
              && path.keys[0].look_rate == 4 && path.keys[0].look_rate2 == 6 && path.keys[0].hold == 500,
        "a custom glide and the longest hold");
    CHECK(!Editor_CamPathsGet(&file, "nowhere", &path), "an unknown path is not found");
    Editor_CamPathsFree(&file);

    CHECK(fails_at("key a | eye 1,1,0,1 | look 1,2,0 | cut | hold 1\npath a\n") == 1, "a key before its path: line 1");
    CHECK(fails_at("path a\npath b\npath a\n") == 3, "a path opened twice: line 3");
    CHECK(fails_at("path a | frame | frame\n") == 1, "frame twice");
    CHECK(fails_at("path a | coords instance 16_101\n") == 1, "a square without its m");
    CHECK(fails_at("path A\n") == 1 && fails_at("path 1a\n") == 1 && fails_at("path a b\n") == 1, "names a proc cannot have");
    CHECK(fails_at("path a\nkey a | eye 1,1,0,1 | look 1,2,0 | cut\n") == 2, "a key without its hold");
    CHECK(fails_at("path a\nkey a | eye 1,1,0,1 | look 1,2,0 | cut | hold 0\n") == 2, "a hold of 0");
    CHECK(fails_at("path a\nkey a | eye 1,1,0,1 | look 1,2,0 | cut | glide 1,1,1,1 | hold 1\n") == 2, "cut and glide");
    CHECK(fails_at("path a\nkey a | eye 1,1,0,1 | look 1,2,0 | glide 5,100,5,5 | hold 1\n") == 2,
        "a glide whose rate2 is the snap");
    CHECK(fails_at("path a\nkey a | eye 1,1,4,1 | look 1,2,0 | cut | hold 1\n") == 2, "level 4");
    CHECK(fails_at("path a\nkey a | eye 1,1,0,65536 | look 1,2,0 | cut | hold 1\n") == 2, "a height past u16");
    CHECK(fails_at("path a\nkey a | eye 1,1,0 | look 1,2,0 | cut | hold 1\n") == 2, "an eye without its height");
    CHECK(fails_at("path a\nkey a | eye 1,1,0,1 | look 1,2,0 | cut | hold 1 | shake 3\n") == 2, "an unknown segment");
    CHECK(fails_at("camera a\n") == 1, "an unknown record");
    {
        /* The 49th keyframe. */
        char* text = malloc(64 * 100);
        size_t at;
        assert(text);
        at = (size_t)sprintf(text, "path a\n");
        for( int i = 0; i < EDITOR_CAMPATH_KEYS_MAX + 1; i++ )
            at += (size_t)sprintf(text + at, "key a | eye 1,1,0,1 | look 1,2,0 | cut | hold 1\n");
        CHECK(fails_at(text) == EDITOR_CAMPATH_KEYS_MAX + 2, "a path's 49th keyframe: line %d", fails_at(text));
        free(text);
    }

    /* format -> parse */
    {
        struct Editor_CamKey key = key_of(3215, 3211, 1100, 3222, 3218, 180, 1, 7);
        struct Editor_CamPathsFile one;
        char text[512];

        key.level = 2;
        key.move_rate = 3;
        key.move_rate2 = 10;
        key.look_rate = 20;
        key.look_rate2 = 50;
        Editor_CamKeyFormat("a", &key, line, sizeof(line));
        CHECK(strcmp(line, "key a | eye 3215,3211,2,1100 | look 3222,3218,180 | glide 3,10,20,50 | hold 7") == 0,
            "a glide's line: %s", line);
        snprintf(text, sizeof(text), "path a\n%s\n", line);
        Editor_CamPathsInit(&one);
        CHECK(Editor_CamPathsParse(&one, text, strlen(text)).ok && Editor_CamPathsGet(&one, "a", &path)
                  && path.key_count == 1 && memcmp(&path.keys[0], &key, sizeof(key)) == 0,
            "and it parses back to the same keyframe");
        Editor_CamPathsFree(&one);
        key.glide = 0;
        Editor_CamKeyFormat("a", &key, line, sizeof(line));
        CHECK(strcmp(line, "key a | eye 3215,3211,2,1100 | look 3222,3218,180 | cut | hold 7") == 0, "a cut's line: %s",
            line);
    }
    {
        struct Editor_CamPath header;
        Editor_CamPathInit(&header, "boss_intro");
        Editor_CamPathFormatHeader(&header, line, sizeof(line));
        CHECK(strcmp(line, "path boss_intro") == 0, "a plain header: %s", line);
        header.instance = 1;
        header.origin_mx = 16;
        header.origin_mz = 101;
        header.frame = 1;
        Editor_CamPathFormatHeader(&header, line, sizeof(line));
        CHECK(strcmp(line, "path boss_intro | coords instance m16_101 | frame") == 0, "a full header: %s", line);
    }
    CHECK(Editor_CamPathNameValid("a") && Editor_CamPathNameValid("boss_2") && !Editor_CamPathNameValid("")
              && !Editor_CamPathNameValid("_a") && !Editor_CamPathNameValid("Boss") && !Editor_CamPathNameValid("a-b")
              && !Editor_CamPathNameValid("a123456789012345678901234567890123456789"),
        "the name rule");
}

/* ---- the verbs --------------------------------------------------------------------- */

static void
test_verbs(void)
{
    struct Editor_CamPathsFile file;
    struct Editor_CamPath path;
    struct Editor_CamPath other;
    struct Editor_CamPath read;
    char* text;
    char* next;
    size_t length = 0;

    Editor_CamPathsInit(&file);
    Editor_CamPathInit(&path, "intro");
    text = text_with(&file, NULL, &path);
    CHECK(text && strncmp(text, "# ToriRSMapEd camera paths", 26) == 0 && strstr(text, "\npath intro\n")
              && text[strlen(text) - 1] == '\n',
        "the first path brings the header:\n%s", text ? text : "(refused)");
    CHECK(Editor_CamPathsParse(&file, text, strlen(text)).ok && Editor_CamPathsCount(&file) == 1,
        "which parses to one path");
    free(text);

    /* A keyframe added, another path beside it. */
    path.keys[path.key_count++] = key_of(3215, 3211, 1100, 3222, 3218, 180, 0, 4);
    text = text_with(&file, "intro", &path);
    CHECK(text && strstr(text, "path intro\nkey intro | eye 3215,3211,0,1100 | look 3222,3218,180 | cut | hold 4\n"),
        "a keyframe follows its path line:\n%s", text ? text : "(refused)");
    (void)Editor_CamPathsParse(&file, text, strlen(text));
    free(text);
    Editor_CamPathInit(&other, "outro");
    other.frame = 1;
    other.keys[other.key_count++] = key_of(3200, 3200, 500, 3205, 3205, 0, 1, 3);
    text = text_with(&file, NULL, &other);
    CHECK(text && strstr(text, "hold 4\npath outro | frame\nkey outro | "), "a second path goes to the end");
    (void)Editor_CamPathsParse(&file, text, strlen(text));

    /* Editing the first leaves every other line as it was. */
    path.keys[path.key_count++] = key_of(3230, 3214, 700, 3222, 3219, 150, 1, 8);
    next = text_with(&file, "intro", &path);
    CHECK(next && strstr(next, "hold 4\nkey intro | eye 3230,3214,0,700 | look 3222,3219,150 | glide 5,5,5,5 | hold 8\n"
                               "path outro | frame\n"),
        "the new keyframe lands inside its path:\n%s", next ? next : "(refused)");
    {
        const char* old_tail = strstr(text, "path outro");
        const char* new_tail = next ? strstr(next, "path outro") : NULL;
        CHECK(old_tail && new_tail && strcmp(old_tail, new_tail) == 0, "the other path's lines are untouched");
        CHECK(next && strncmp(text, next, (size_t)(strstr(text, "path intro") - text)) == 0, "and so is the header");
    }
    free(text);
    (void)Editor_CamPathsParse(&file, next, strlen(next));
    free(next);
    CHECK(Editor_CamPathsGet(&file, "intro", &read) && read.key_count == 2
              && memcmp(&read.keys[1], &path.keys[1], sizeof(read.keys[1])) == 0,
        "read back with both keyframes");

    /* Rename, and the refusals. */
    snprintf(path.name, sizeof(path.name), "outro");
    CHECK(text_with(&file, "intro", &path) == NULL, "a rename onto a taken name is refused");
    CHECK(text_with(&file, "nowhere", &path) == NULL, "so is a replace of a path that is not there");
    CHECK(text_with(&file, NULL, &other) == NULL, "and a second path of a name");
    snprintf(path.name, sizeof(path.name), "opening");
    text = text_with(&file, "intro", &path);
    CHECK(text && strstr(text, "path opening\nkey opening | ") && !strstr(text, "intro"), "a rename renames every line");
    CHECK(text && strstr(text, "path opening") < strstr(text, "path outro"), "in its old place");
    (void)Editor_CamPathsParse(&file, text, strlen(text));
    free(text);
    path.keys[0].hold = 0;
    CHECK(text_with(&file, "opening", &path) == NULL, "an invalid keyframe is refused");

    /* Delete, down to nothing. */
    CHECK(!Editor_CamPathsTextWithout(&file, "intro", NULL, 0, &length), "no such path to delete");
    CHECK(Editor_CamPathsTextWithout(&file, "opening", NULL, 0, &length), "a delete");
    text = malloc(length + 1);
    assert(text);
    Editor_CamPathsTextWithout(&file, "opening", text, length + 1, &length);
    CHECK(!strstr(text, "opening") && strstr(text, "path outro | frame\nkey outro"), "takes every line of it");
    (void)Editor_CamPathsParse(&file, text, strlen(text));
    free(text);
    CHECK(Editor_CamPathsTextWithout(&file, "outro", NULL, 0, &length) && length == 0,
        "the last delete takes the header back (%d bytes left)", (int)length);
    Editor_CamPathsFree(&file);

    /* A hand-written file with no final newline gains one line cleanly. */
    {
        static const char k_hand[] = "// mine\npath a";
        Editor_CamPathsInit(&file);
        (void)Editor_CamPathsParse(&file, k_hand, sizeof(k_hand) - 1);
        Editor_CamPathInit(&other, "b");
        text = text_with(&file, NULL, &other);
        CHECK(text && strcmp(text, "// mine\npath a\npath b\n") == 0, "appended after a file with no final newline: %s",
            text ? text : "(refused)");
        free(text);
        Editor_CamPathsFree(&file);
    }
}

/* ---- the maths --------------------------------------------------------------------- */

static void
test_maths(void)
{
    struct Editor_CamKey key;
    struct Editor_CamEye eye;
    struct Editor_CamEye fitted;
    struct Editor_CamFitNote note;
    /* The centre of tile 3222,3218. */
    int const tx = 3222 * 128 + 64;
    int const tz = 3218 * 128 + 64;

    memset(&key, 0, sizeof(key));

    /* Ten tiles back and ten tiles up, looking north at 45 degrees: the eye
     * is over 3222,3208, 1280 above the ground, looking at 3222,3218. */
    eye = orbit_eye(tx, 0, tz, 0, 256, 1810);
    CHECK(eye.x == tx && eye.y == -1280 && eye.z == tz - 1280, "the orbit eye is where the hand says (%d,%d,%d)", eye.x,
        eye.y, eye.z);
    CHECK(Editor_CamKeyFromEye(&eye, 0, ground_flat, NULL, &key, &note) == EDITOR_CAM_FIT_OK, "it fits");
    CHECK(key.eye_x == 3222 && key.eye_z == 3208 && key.eye_height == 1280 && key.level == 0,
        "north: the eye tile and height (%d,%d +%d)", key.eye_x, key.eye_z, key.eye_height);
    CHECK(key.look_x == 3222 && key.look_z == 3218 && key.look_height == 0, "north: the look tile (%d,%d +%d)",
        key.look_x, key.look_z, key.look_height);
    CHECK(Editor_CamKeyEye(&key, ground_flat, NULL, &fitted) && fitted.x == eye.x && fitted.y == eye.y
              && fitted.z == eye.z,
        "and back: the same eye");
    /* atan2(1280, 1280) * 325.949 = 255.9997, which the client truncates. */
    CHECK(fitted.yaw == 0 && fitted.pitch == 255, "the client's angles for it: yaw %d pitch %d", fitted.yaw,
        fitted.pitch);
    CHECK(note.yaw_error == 0 && note.pitch_error == 1 && !note.eye_raised && !note.steep, "the note says one unit");

    /* West (yaw 512 looks towards -x), south (1024), east (1536). */
    eye = orbit_eye(tx, 0, tz, 512, 256, 1810);
    CHECK(Editor_CamKeyFromEye(&eye, 0, ground_flat, NULL, &key, NULL) == EDITOR_CAM_FIT_OK && key.eye_x == 3232
              && key.eye_z == 3218 && key.look_x == 3222 && key.look_z == 3218,
        "yaw 512 stands east of the subject and looks west (eye %d,%d look %d,%d)", key.eye_x, key.eye_z, key.look_x,
        key.look_z);
    CHECK(Editor_CamKeyEye(&key, ground_flat, NULL, &fitted) && angle_off(fitted.yaw, 512) <= 1, "its yaw comes back (%d)",
        fitted.yaw);
    eye = orbit_eye(tx, 0, tz, 1024, 256, 1810);
    CHECK(Editor_CamKeyFromEye(&eye, 0, ground_flat, NULL, &key, NULL) == EDITOR_CAM_FIT_OK && key.eye_x == 3222
              && key.eye_z == 3228 && key.look_z == 3218,
        "yaw 1024 stands north and looks south (eye %d,%d)", key.eye_x, key.eye_z);
    eye = orbit_eye(tx, 0, tz, 1536, 256, 1810);
    CHECK(Editor_CamKeyFromEye(&eye, 0, ground_flat, NULL, &key, NULL) == EDITOR_CAM_FIT_OK && key.eye_x == 3212
              && key.eye_z == 3218 && key.look_x == 3222,
        "yaw 1536 stands west and looks east (eye %d,%d)", key.eye_x, key.eye_z);
    CHECK(Editor_CamKeyEye(&key, ground_flat, NULL, &fitted) && angle_off(fitted.yaw, 1536) <= 1, "its yaw comes back (%d)",
        fitted.yaw);

    /* A slope rising east: the ground under the eye tile 3230,3210 is 976
     * units up (y -976), under the subject's 3222,3218 it is 720. An eye 900
     * above ITS ground reads 900, not the 1156 the subject's ground would
     * give. */
    {
        int ground_y = 0;
        int const ex = 3230 * 128 + 64;
        int const ez = 3210 * 128 + 64;
        int look_x;
        int look_y;
        int look_z;

        (void)ground_slope(NULL, ex, ez, 0, &ground_y);
        CHECK(ground_y == -976, "the slope under the eye (%d)", ground_y);
        eye.x = ex;
        eye.z = ez;
        eye.y = ground_y - 900;
        /* Towards 3222,3218 on its ground, by the client's own formula. */
        (void)ground_slope(NULL, tx, tz, 0, &look_y);
        Editor_CamAnglesTo(eye.x, eye.y, eye.z, tx, look_y, tz, &eye.pitch, &eye.yaw);
        CHECK(Editor_CamKeyFromEye(&eye, 0, ground_slope, NULL, &key, &note) == EDITOR_CAM_FIT_OK
                  && key.eye_x == 3230 && key.eye_z == 3210 && key.eye_height == 900,
            "on a slope the height is above the ground under the eye (+%d)", key.eye_height);
        CHECK(Editor_CamKeyEye(&key, ground_slope, NULL, &fitted) && fitted.x == eye.x && fitted.y == eye.y
                  && fitted.z == eye.z,
            "and the eye comes back exactly");
        CHECK(angle_off(fitted.yaw, eye.yaw) <= 4 && angle_off(fitted.pitch, eye.pitch) <= 3,
            "looking the way it looked (yaw %d for %d, pitch %d for %d)", fitted.yaw, eye.yaw, fitted.pitch, eye.pitch);
        CHECK(Editor_CamKeyLookPoint(&key, ground_slope, NULL, &look_x, &look_y, &look_z)
                  && look_x == key.look_x * 128 + 64 && look_z == key.look_z * 128 + 64,
            "the look point is over its tile's centre");
        {
            int under = 0;
            (void)ground_slope(NULL, look_x, look_z, 0, &under);
            CHECK(look_y == under - key.look_height && key.look_height >= 0, "its height above ITS ground (+%d)",
                key.look_height);
        }
    }

    /* 500 poses over rolling ground: the fitted camera stands within its
     * tile of the eye, at the eye's own height, and still looks at the
     * eye's subject -- the point its ray meets the ground at. */
    {
        unsigned seed = 12345u;
        int worst_yaw = 0;
        int worst_pitch = 0;
        int worst_far = 0;
        int fits = 0;
        int placed = 1;

        for( int i = 0; i < 500; i++ )
        {
            int sx;
            int sz;
            int sy = 0;
            int yaw;
            int pitch;
            int zoom;
            int want_pitch;
            int want_yaw;

            seed = seed * 1103515245u + 12345u;
            sx = 3216 * 128 + (int)((seed >> 8) % (32 * 128));
            seed = seed * 1103515245u + 12345u;
            sz = 3216 * 128 + (int)((seed >> 8) % (32 * 128));
            seed = seed * 1103515245u + 12345u;
            yaw = (int)((seed >> 8) % 2048);
            seed = seed * 1103515245u + 12345u;
            pitch = 128 + (int)((seed >> 8) % 256);
            seed = seed * 1103515245u + 12345u;
            zoom = 900 + (int)((seed >> 8) % 2200);
            (void)ground_hills(NULL, sx, sz, 0, &sy);
            eye = orbit_eye(sx, sy, sz, yaw, pitch, zoom);
            if( Editor_CamKeyFromEye(&eye, 0, ground_hills, NULL, &key, &note) != EDITOR_CAM_FIT_OK )
                continue;
            if( !Editor_CamKeyEye(&key, ground_hills, NULL, &fitted) )
                continue;
            fits++;
            placed = placed && abs(fitted.x - eye.x) <= 64 && abs(fitted.z - eye.z) <= 64
                     && (note.eye_raised || fitted.y == eye.y);
            /* The subject is the FIRST ground the eye's ray meets, which a
             * hill may put short of the point the orbit turned about:
             * walked here a unit at a time. */
            {
                double const a = yaw * 2.0 * 3.14159265358979323846 / 2048.0;
                double const p = pitch * 2.0 * 3.14159265358979323846 / 2048.0;
                for( int t = 1; t <= zoom; t++ )
                {
                    int const px = eye.x + (int)lround(-cos(p) * sin(a) * t);
                    int const py = eye.y + (int)lround(sin(p) * t);
                    int const pz = eye.z + (int)lround(cos(p) * cos(a) * t);
                    int g = 0;
                    if( !ground_hills(NULL, px, pz, 0, &g) )
                        break;
                    if( py >= g )
                    {
                        sx = px;
                        sy = py;
                        sz = pz;
                        break;
                    }
                }
            }
            /* Where the fitted eye must look to hold the subject. */
            Editor_CamAnglesTo(fitted.x, fitted.y, fitted.z, sx, sy, sz, &want_pitch, &want_yaw);
            if( angle_off(fitted.pitch, want_pitch) > worst_pitch )
                worst_pitch = angle_off(fitted.pitch, want_pitch);
            /* The game aims from a tile centre at a tile centre, so the yaw
             * it can hold is one of those directions: the fit must be the
             * best of them, found here by trying every tile round the eye
             * at the height that keeps the pitch. */
            {
                double const run = sqrt((double)(sx - fitted.x) * (sx - fitted.x) + (double)(sz - fitted.z) * (sz - fitted.z));
                double const slope = run > 0.0 ? (sy - fitted.y) / run : 0.0;
                int least = 2048;
                int const mine = angle_off(fitted.yaw, want_yaw);

                for( int tz = key.eye_z - 20; tz <= key.eye_z + 20; tz++ )
                    for( int tx = key.eye_x - 20; tx <= key.eye_x + 20; tx++ )
                    {
                        int const cx = tx * 128 + 64;
                        int const cz = tz * 128 + 64;
                        double const ox = cx - fitted.x;
                        double const oz = cz - fitted.z;
                        int g = 0;
                        int tile_pitch;
                        int tile_yaw;

                        if( (tx == key.eye_x && tz == key.eye_z) || run <= 0.0 || !ground_hills(NULL, cx, cz, 0, &g) )
                            continue;
                        /* In front of the eye, and the line not under its ground. */
                        if( (ox * (sx - fitted.x) + oz * (sz - fitted.z)) / run < 64.0 )
                            continue;
                        if( (double)g - (fitted.y + slope * sqrt(ox * ox + oz * oz)) < -0.5 )
                            continue;
                        Editor_CamAnglesTo(fitted.x, fitted.y, fitted.z, cx, fitted.y, cz, &tile_pitch, &tile_yaw);
                        if( angle_off(tile_yaw, want_yaw) < least )
                            least = angle_off(tile_yaw, want_yaw);
                    }
                if( mine - least > worst_yaw )
                    worst_yaw = mine - least;
                if( run >= 16 * 128 && mine > worst_far )
                    worst_far = mine;
            }
        }
        CHECK(fits >= 300, "most poses over the hills fit (%d of 500)", fits);
        CHECK(placed, "every fitted eye is over the eye's tile at the eye's height");
        CHECK(worst_yaw <= 4, "the fitted yaw is the nearest a tile centre gives, or within half a degree of it (%d units off)",
            worst_yaw);
        CHECK(worst_far <= 12, "and within two degrees of the subject from sixteen tiles out (worst %d units)", worst_far);
        CHECK(worst_pitch <= 2, "the fitted pitch holds the subject (worst %d units)", worst_pitch);
    }

    /* Straight down: the game cannot hold a yaw over its own tile, so the
     * look tile is the neighbour the yaw points at. */
    eye.x = tx;
    eye.z = tz;
    eye.y = -1000;
    eye.yaw = 0;
    eye.pitch = 512;
    CHECK(Editor_CamKeyFromEye(&eye, 0, ground_flat, NULL, &key, &note) == EDITOR_CAM_FIT_OK && note.steep
              && key.look_x == 3222 && key.look_z == 3219 && key.look_height == 0 && key.eye_height == 1000,
        "straight down looks at the tile north (%d,%d), flagged steep", key.look_x, key.look_z);

    /* Refusals. */
    eye.pitch = 2000;
    CHECK(Editor_CamKeyFromEye(&eye, 0, ground_flat, NULL, &key, NULL) == EDITOR_CAM_FIT_LOOKS_UP, "looking up is refused");
    eye.pitch = 256;
    eye.x = 100 * 128;
    eye.z = 100 * 128;
    CHECK(Editor_CamKeyFromEye(&eye, 0, ground_hills, NULL, &key, NULL) == EDITOR_CAM_FIT_EYE_OFF_TERRAIN,
        "an eye over no terrain is refused");
    eye.x = -5;
    CHECK(Editor_CamKeyFromEye(&eye, 0, ground_flat, NULL, &key, NULL) == EDITOR_CAM_FIT_EYE_OFF_WORLD,
        "an eye west of the world is refused");
    key = key_of(100, 100, 500, 3222, 3218, 0, 0, 1);
    CHECK(!Editor_CamKeyEye(&key, ground_hills, NULL, &fitted), "a keyframe off the terrain has no eye to give");

    /* An eye under the ground stands on it. */
    eye.x = tx;
    eye.z = tz;
    eye.y = 40;
    eye.yaw = 0;
    eye.pitch = 200;
    CHECK(Editor_CamKeyFromEye(&eye, 0, ground_flat, NULL, &key, &note) == EDITOR_CAM_FIT_OK && note.eye_raised
              && key.eye_height == 0,
        "an eye under the ground is raised to it");

    CHECK(Editor_CamPitchClamp(100, 0) == 128 && Editor_CamPitchClamp(400, 0) == 383 && Editor_CamPitchClamp(100, 1) == 100
              && Editor_CamPitchClamp(600, 1) == 520 && Editor_CamPitchClamp(300, 0) == 300,
        "the locked and unlocked pitch ranges");
    CHECK(Editor_CamPitchNeedsUnlock(127) && Editor_CamPitchNeedsUnlock(384) && !Editor_CamPitchNeedsUnlock(128)
              && !Editor_CamPitchNeedsUnlock(383),
        "which pitches need cam_unlock(true)");
}

/* ---- the easing -------------------------------------------------------------------- */

static void
test_easing(void)
{
    struct Editor_CamSim sim;
    struct Editor_CamEye eye;
    struct Editor_CamKey key;
    int cycles;

    /* 5 flat plus 5/1000 of the 1000 left. */
    CHECK(Editor_CamEase(0, 1000, 5, 5) == 10, "one step of 5,5 over 1000 is 10");
    CHECK(Editor_CamEase(1000, 0, 5, 5) == 990, "and the same downwards");
    CHECK(Editor_CamEase(998, 1000, 5, 5) == 1000 && Editor_CamEase(2, 0, 5, 5) == 0, "never past the target");
    CHECK(Editor_CamEase(7, 7, 5, 5) == 7, "at rest on the target");
    CHECK(Editor_CamEase(0, 150, 0, 5) == 0, "a rate of 0 does not close the last 200 units");

    /* Across the seam the short way: 2040 -> 10 is 18 units, 5 a step. */
    CHECK(Editor_CamEaseYaw(2040, 10, 5, 0) == 2045, "a yaw turns the short way");
    CHECK(Editor_CamEaseYaw(2045, 10, 5, 0) == 2, "over the seam");
    CHECK(Editor_CamEaseYaw(7, 10, 5, 0) == 10, "and stops on its target rather than past it");
    CHECK(Editor_CamEaseYaw(10, 2040, 5, 0) == 5, "the other way round too");

    /* 100 units at a flat 10: ten cycles, not nine. */
    memset(&eye, 0, sizeof(eye));
    Editor_CamSimInit(&sim, &eye, 0);
    CHECK(Editor_CamSimArrived(&sim), "a camera with no target is at rest");
    Editor_CamSimMoveTo(&sim, 100, 0, 0, 10, 0);
    for( cycles = 0; !Editor_CamSimArrived(&sim) && cycles < 100; cycles++ )
        Editor_CamSimStep(&sim);
    CHECK(cycles == 10 && sim.eye.x == 100, "100 units at 10 a cycle take 10 cycles (%d)", cycles);

    /* A rate2 of 100 is there at once, and the look snaps from where the eye
     * now is. */
    Editor_CamSimInit(&sim, &eye, 0);
    Editor_CamSimMoveTo(&sim, 5000, -1280, 5000, 232, 232);
    Editor_CamSimLookAt(&sim, 5000, 0, 6280, 232, 232);
    CHECK(sim.eye.x == 5000 && sim.eye.y == -1280 && sim.eye.z == 5000 && sim.eye.yaw == 0 && sim.eye.pitch == 255
              && Editor_CamSimArrived(&sim),
        "a cut is there in no cycles (pitch %d yaw %d)", sim.eye.pitch, sim.eye.yaw);
    Editor_CamSimStep(&sim);
    CHECK(sim.eye.x == 5000 && sim.eye.pitch == 255, "and stays");

    /* The clamp: a look point far below reads 383 locked, its own unlocked. */
    Editor_CamSimInit(&sim, &eye, 0);
    Editor_CamSimMoveTo(&sim, 5000, -3000, 5000, 232, 232);
    Editor_CamSimLookAt(&sim, 5000, 0, 5400, 232, 232);
    CHECK(sim.eye.pitch == 383, "a steep look is clamped to 383 while locked (%d)", sim.eye.pitch);
    Editor_CamSimInit(&sim, &eye, 1);
    Editor_CamSimMoveTo(&sim, 5000, -3000, 5000, 232, 232);
    Editor_CamSimLookAt(&sim, 5000, 0, 5400, 232, 232);
    CHECK(sim.eye.pitch > 383 && sim.eye.pitch <= 520, "and free once unlocked (%d)", sim.eye.pitch);

    /* A glide between two keyframes on flat ground arrives, eye and angles,
     * in the cycles the count says. */
    key = key_of(3222, 3208, 1280, 3222, 3218, 0, 0, 4);
    CHECK(Editor_CamKeyEye(&key, ground_flat, NULL, &eye), "a start");
    eye.pitch = Editor_CamPitchClamp(eye.pitch, 0);
    CHECK(Editor_CamGlideCycles(&eye, &key, ground_flat, NULL, 0, 3000) == 0, "a cut takes no cycles");
    key = key_of(3232, 3218, 900, 3222, 3218, 0, 1, 4);
    cycles = Editor_CamGlideCycles(&eye, &key, ground_flat, NULL, 0, 3000);
    CHECK(cycles > 60 && cycles < 400, "a Medium glide of ten tiles takes a few seconds (%d cycles)", cycles);
    {
        struct Editor_CamEye target;
        int look_x;
        int look_y;
        int look_z;
        int before = 0;

        (void)Editor_CamKeyEye(&key, ground_flat, NULL, &target);
        (void)Editor_CamKeyLookPoint(&key, ground_flat, NULL, &look_x, &look_y, &look_z);
        Editor_CamSimInit(&sim, &eye, 0);
        Editor_CamSimMoveTo(&sim, target.x, target.y, target.z, 5, 5);
        Editor_CamSimLookAt(&sim, look_x, look_y, look_z, 5, 5);
        for( int i = 0; i < cycles; i++ )
        {
            before = Editor_CamSimArrived(&sim);
            Editor_CamSimStep(&sim);
        }
        CHECK(!before && Editor_CamSimArrived(&sim) && sim.eye.x == target.x && sim.eye.y == target.y
                  && sim.eye.z == target.z && sim.eye.yaw == target.yaw
                  && sim.eye.pitch == Editor_CamPitchClamp(target.pitch, 0),
            "stepped that many cycles it has just arrived");
    }
    Editor_CamSpeedApply(EDITOR_CAM_SPEED_SLOW, &key);
    {
        int const slow = Editor_CamGlideCycles(&eye, &key, ground_flat, NULL, 0, 3000);
        Editor_CamSpeedApply(EDITOR_CAM_SPEED_FAST, &key);
        CHECK(slow > cycles && Editor_CamGlideCycles(&eye, &key, ground_flat, NULL, 0, 3000) < cycles,
            "Slow is slower than Medium and Fast faster (%d, %d, %d)", slow, cycles,
            Editor_CamGlideCycles(&eye, &key, ground_flat, NULL, 0, 3000));
    }
    key.move_rate = 0;
    key.move_rate2 = 1;
    CHECK(Editor_CamGlideCycles(&eye, &key, ground_flat, NULL, 0, 3000) == -1, "a rate of 0 never arrives: -1");
    CHECK(Editor_CamCyclesToTicks(0) == 0 && Editor_CamCyclesToTicks(1) == 1 && Editor_CamCyclesToTicks(30) == 1
              && Editor_CamCyclesToTicks(31) == 2,
        "cycles to whole ticks");

    Editor_CamSpeedApply(EDITOR_CAM_SPEED_SLOW, &key);
    CHECK(Editor_CamSpeedOf(&key) == EDITOR_CAM_SPEED_SLOW && key.move_rate == 2 && key.look_rate2 == 2, "the Slow preset");
    key.look_rate = 9;
    CHECK(Editor_CamSpeedOf(&key) == EDITOR_CAM_SPEED_CUSTOM && strcmp(Editor_CamSpeedName(EDITOR_CAM_SPEED_CUSTOM), "Custom") == 0,
        "rates no preset holds are Custom");
}

/* ---- the script -------------------------------------------------------------------- */

static void
test_script(void)
{
    struct Editor_CamPath path;
    char text[EDITOR_CAMPATH_SCRIPT_MAX];
    char line[192];
    size_t length;

    Editor_CamPathInit(&path, "lumbridge_intro");
    path.keys[path.key_count++] = key_of(3215, 3211, 1100, 3222, 3218, 180, 0, 4);
    path.keys[path.key_count++] = key_of(3230, 3214, 700, 3222, 3219, 150, 1, 8);
    length = Editor_CamPathScript(&path, 0, text, sizeof(text));
    CHECK(length == strlen(text)
              && strcmp(text,
                     "[proc,lumbridge_intro_camera]\n"
                     "// generated by the map editor's Camera path panel from editor/campaths.txt: lumbridge_intro\n"
                     "// ~lumbridge_intro_camera; plays it for a player standing in the scene these tiles are in\n"
                     "cam_moveto(0_50_50_15_11, 1100, 232, 232);\n"
                     "cam_lookat(0_50_50_22_18, 180, 232, 232);\n"
                     "p_delay(3);\n"
                     "cam_moveto(0_50_50_30_14, 700, 5, 5);\n"
                     "cam_lookat(0_50_50_22_19, 150, 5, 5);\n"
                     "p_delay(7);\n"
                     "cam_reset;\n")
                     == 0,
        "the proc in world tiles:\n%s", text);
    CHECK(Editor_CamPathScript(&path, 0, NULL, 0) == length, "sized without a buffer");
    CHECK(Editor_CamPathTicks(&path) == 12, "it runs its holds: 12 ticks (%d)", Editor_CamPathTicks(&path));

    /* A framing the locked camera cannot hold: unlocked, and locked again. */
    Editor_CamPathScript(&path, 1, text, sizeof(text));
    CHECK(strstr(text, "plays it for a player standing in the scene these tiles are in\ncam_unlock(true);\ncam_moveto(")
              && strstr(text, "p_delay(7);\ncam_unlock(false);\ncam_reset;\n"),
        "unlocked before the first keyframe and locked before the reset:\n%s", text);

    /* Offsets from m50_50's south-west tile, on level 1. */
    path.instance = 1;
    path.origin_mx = 50;
    path.origin_mz = 50;
    path.keys[0].level = 1;
    Editor_CamPathScript(&path, 0, text, sizeof(text));
    CHECK(strncmp(text, "[proc,lumbridge_intro_camera](int $handle)\n", 43) == 0, "an instance path takes the handle");
    CHECK(strstr(text, "cam_moveto(map_instance_coord($handle, 15, 11, 1), 1100, 232, 232);\n"
                       "cam_lookat(map_instance_coord($handle, 22, 18, 1), 180, 232, 232);\n"),
        "and states offsets from the template's south-west tile:\n%s", text);
    CHECK(strstr(text, "// ~lumbridge_intro_camera($handle); plays it in the map instance $handle"), "and says how to call it");
    path.instance = 0;
    path.keys[0].level = 0;

    /* The cutscene frame. */
    path.frame = 1;
    Editor_CamPathScript(&path, 1, text, sizeof(text));
    CHECK(strstr(text, "these tiles are in\n"
                       "~fade_screen(0, 255, 0, 0);\n"
                       "p_delay(1);\n"
                       "~cutscene_lock(3, 0);\n"
                       "~cutscene_hud_lock;\n"
                       "p_delay(0);\n"
                       "cam_unlock(true);\n"
                       "cam_moveto(0_50_50_15_11, 1100, 232, 232);\n"
                       "cam_lookat(0_50_50_22_18, 180, 232, 232);\n"
                       "p_delay(0);\n"
                       "~fade_screen(0, 0, 0, 255);\n"
                       "p_delay(2);\n"
                       "cam_moveto(0_50_50_30_14, 700, 5, 5);\n"),
        "framed: the black, the lock, the first framing, the black lifted, the rest of its hold:\n%s", text);
    CHECK(strstr(text, "p_delay(7);\n"
                       "~fade_screen(0, 255, 0, 0);\n"
                       "p_delay(1);\n"
                       "~cutscene_unlock;\n"
                       "~cutscene_hud_unlock;\n"
                       "p_delay(1);\n"
                       "~fade_screen(0, 0, 0, 255);\n"
                       "p_delay(1);\n"
                       "if_closesub(toplevel_osrs_stretch:overlay_atmosphere);\n")
              && !strstr(text, "cam_reset") && !strstr(text, "cam_unlock(false)"),
        "and the way out, whose HUD unlock resets the camera itself:\n%s", text);
    CHECK(Editor_CamPathTicks(&path) == 21, "framed it runs 9 ticks longer (%d)", Editor_CamPathTicks(&path));
    path.keys[0].hold = 1;
    Editor_CamPathScript(&path, 0, text, sizeof(text));
    CHECK(strstr(text, "~fade_screen(0, 0, 0, 255);\ncam_moveto(0_50_50_30_14"), "a first hold of one tick is the fade's own");

    Editor_CamKeyCheat(&path.keys[0], line, sizeof(line));
    CHECK(strcmp(line, "cam 0_50_50_15_11 1100 0_50_50_22_18 180 232 232") == 0, "a cut's cheat: %s", line);
    Editor_CamKeyCheat(&path.keys[1], line, sizeof(line));
    CHECK(strcmp(line, "cam 0_50_50_30_14 700 0_50_50_22_19 150 5 5") == 0, "a glide's cheat: %s", line);
    path.keys[0].hold = 4;
    Editor_CamKeyRowText(&path.keys[0], 0, line, sizeof(line));
    CHECK(strcmp(line, "1  cut, hold 4  eye 3215,3211 +1100  look 3222,3218 +180") == 0, "a cut's row: %s", line);
    Editor_CamKeyRowText(&path.keys[1], 1, line, sizeof(line));
    CHECK(strcmp(line, "2  Medium, hold 8  eye 3230,3214 +700  look 3222,3219 +150") == 0, "a glide's row: %s",
        line);
    path.keys[1].level = 2;
    path.keys[1].look_rate = 9;
    Editor_CamKeyRowText(&path.keys[1], 1, line, sizeof(line));
    CHECK(strcmp(line, "2  glide 5,5/9,5, hold 8  eye 3230,3214 L2 +700  look 3222,3219 +150") == 0,
        "a custom glide upstairs: %s", line);
    Editor_CamCoordText(3222, 3218, 2, line, sizeof(line));
    CHECK(strcmp(line, "2_50_50_22_18") == 0, "a coord literal: %s", line);
}

int
main(void)
{
    ToriMem_Init();
    test_codec();
    test_verbs();
    test_maths();
    test_easing();
    test_script();
    printf("editor_campath_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
