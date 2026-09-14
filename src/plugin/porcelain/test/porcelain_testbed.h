#ifndef TORIRS_PORCELAIN_TESTBED_H
#define TORIRS_PORCELAIN_TESTBED_H

/*
 * One shared fake engine for every Porcelain test.
 *
 * The record's seventh bug class is that each unit test shipped its own fake
 * engine, 1,256 to 3,382 lines of it, and every new engine callback had to be
 * added to all of them. There is one here, and it does two things no per-test
 * fake did:
 *
 *   - It RECORDS every call in order. "An unchanged description makes zero
 *     engine calls" is then a readable assertion and not a belief, and the
 *     recorded log IS the hand-expansion into raw api-> calls that the plan
 *     says any Porcelain plugin must have.
 *   - It lets a test move one input at a time -- an element binds, an asset
 *     lands, the config changes -- so the readiness matrix can replay the six
 *     inputs in every order against the same description.
 */

#include "plugin/porcelain/torirs_porcelain.h"

#define TESTBED_ELEMENTS_MAX 32
#define TESTBED_WATCHES_MAX 64
#define TESTBED_CONTROLS_MAX 64
#define TESTBED_ASSETS_MAX 32
#define TESTBED_CONFIG_MAX 16
#define TESTBED_LOG_MAX 512
#define TESTBED_LOG_LINE 120

struct TestbedElement
{
    bool used;
    char role[64];
    bool bound;
    struct ToriRS_WidgetBounds local;
    struct ToriRS_WidgetBounds bounds;
    bool presented;
    bool own_hidden;
    bool native_hidden;
    bool input_present;
    uint32_t graphic_token;
    uint32_t facets;
    uint64_t incarnation;
    struct ToriRS_WidgetRef ref;
    struct ToriRS_WidgetRef parent;
};

struct TestbedControl
{
    bool live;
    char key[64];
    struct ToriRS_WidgetRef ref;
    struct ToriRS_WidgetRef parent;
    struct ToriRS_WidgetRef anchor;
    enum ToriRS_WidgetRelation relation;
    int32_t x, y, width, height;
    int opacity;
    bool hidden;
    bool armed;
    char label[64];
    ToriRS_WidgetListener op;
    void* op_user;
    char text[128];
};

struct TestbedAsset
{
    bool used;
    char name[64];
    enum ToriRS_AssetState state;
    int value;
};

struct TestbedConfigRow
{
    bool used;
    char key[64];
    char value[PORCELAIN_CONFIG_VALUE_MAX];
    int number;
    bool has_number;
};

struct Testbed
{
    struct ToriRS_Api api;
    struct ToriRS_GameApi game;

    struct TestbedElement elements[TESTBED_ELEMENTS_MAX];
    struct
    {
        bool used;
        char role[64];
        ToriRS_WidgetListener fn;
        void* user;
    } watches[TESTBED_WATCHES_MAX];
    struct TestbedControl controls[TESTBED_CONTROLS_MAX];
    struct TestbedAsset assets[TESTBED_ASSETS_MAX];
    struct TestbedConfigRow config[TESTBED_CONFIG_MAX];

    char log[TESTBED_LOG_MAX][TESTBED_LOG_LINE];
    int log_count;
    /* Counted separately so a test can say "no engine call at all" without
     * reading the log. */
    int calls;

    int screen;
    uint64_t frame_ms;
    bool touch;
    bool skill_stated;
    bool player_present;
    /* Capabilities this lane answers, comma separated. */
    char capabilities[256];
    /* Named ids the profile declares, as "kind:name=value" rows. */
    char named_ids[256];
    int key_held;
    int next_ref;
};

/** The single instance. Reset it between scenarios. */
extern struct Testbed g_testbed;

void Testbed_Reset(void);
struct ToriRS_Api* Testbed_Api(void);

/* Elements ---------------------------------------------------------------- */

/** Declare an element this lane HAS but has not bound yet. */
struct TestbedElement* Testbed_DeclareElement(char const* role, int x, int y, int width,
                                              int height);
/** Bind it, raising BOUND on every watch that named it. */
void Testbed_BindElement(char const* role);
/** Unbind it, raising UNBOUND. */
void Testbed_UnbindElement(char const* role);
/** Move it, raising STATE_CHANGED. */
void Testbed_MoveElement(char const* role, int x, int y);
/** Hide or show it, raising STATE_CHANGED. */
void Testbed_PresentElement(char const* role, bool presented);
struct TestbedElement* Testbed_Element(char const* role);

/* Assets ------------------------------------------------------------------ */

void Testbed_DeclareAsset(char const* name, enum ToriRS_AssetState state);
/** Land a pending asset, as the loader would. */
void Testbed_LandAsset(char const* name);

/* Config ------------------------------------------------------------------ */

void Testbed_SetConfigString(char const* key, char const* value);
void Testbed_SetConfigInt(char const* key, int value);
char const* Testbed_ConfigString(char const* key);

/* The log ----------------------------------------------------------------- */

void Testbed_ClearLog(void);
int Testbed_LogCount(void);
char const* Testbed_LogLine(int index);
/** How many recorded lines begin with `prefix`. */
int Testbed_LogCountWith(char const* prefix);
/** The index of the first line beginning with `prefix`, or -1. */
int Testbed_LogFind(char const* prefix);
void Testbed_PrintLog(void);

struct TestbedControl* Testbed_Control(char const* key);
int Testbed_LiveControls(void);

#endif /* TORIRS_PORCELAIN_TESTBED_H */
