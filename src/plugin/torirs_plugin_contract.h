#ifndef TORIRS_PLUGIN_CONTRACT_H
#define TORIRS_PLUGIN_CONTRACT_H

/* Major 3: live widgets and native events. The host checks identities and
 * callback lifetimes; plugins never receive UITree pointers or retained indices.
 * This unshipped API replaces the earlier declaration/claim proposal. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TORIRS_PLUGIN_CONTRACT_MAJOR 3u
#define TORIRS_PLUGIN_CONTRACT_MINOR 0u

struct ToriRS_WidgetRef { uint64_t opaque[3]; };
struct ToriRS_WidgetActionRef
{
    struct ToriRS_WidgetRef widget;
    uint64_t operation;
    uint64_t revision;
};

enum ToriRS_ContractResult
{
    TORIRS_CONTRACT_OK,
    TORIRS_CONTRACT_UNAVAILABLE,
    TORIRS_CONTRACT_STALE_REFERENCE,
    TORIRS_CONTRACT_WRONG_CONTEXT,
    TORIRS_CONTRACT_INVALID_ARGUMENT,
    TORIRS_CONTRACT_UNSUPPORTED_LAYOUT,
    TORIRS_CONTRACT_NATIVE_BLOCKED,
    TORIRS_CONTRACT_PENDING,
    TORIRS_CONTRACT_BUDGET_EXCEEDED,
    TORIRS_CONTRACT_FAILED
};

/* Execution contexts are checked by host entry points. Native callbacks may
 * mutate widgets, but cannot recursively enter an already-running script VM.
 * Later/tick-end scheduling is the way out of such a callback. */
enum ToriRS_PluginExecutionContext
{
    TORIRS_PLUGIN_STARTUP,
    TORIRS_PLUGIN_EVENT,
    TORIRS_PLUGIN_SCRIPT_CALLBACK,
    TORIRS_PLUGIN_PAINT,
    TORIRS_PLUGIN_SHUTDOWN
};

enum ToriRS_PluginOperation
{
    TORIRS_PLUGIN_READ_WIDGET,
    TORIRS_PLUGIN_WRITE_WIDGET,
    TORIRS_PLUGIN_RUN_SCRIPT,
    TORIRS_PLUGIN_SCHEDULE,
    TORIRS_PLUGIN_RELEASE_OWNED
};

bool ToriRS_WidgetRefValid(struct ToriRS_WidgetRef ref);
bool ToriRS_WidgetRefEqual(struct ToriRS_WidgetRef a, struct ToriRS_WidgetRef b);
bool ToriRS_PluginContextAllows(enum ToriRS_PluginExecutionContext context,
                               enum ToriRS_PluginOperation operation);

struct ToriRS_PluginRequirement
{
    char const* capability;
    uint32_t version;
    bool optional;
};

struct ToriRS_WidgetBounds { int32_t x, y, width, height; };

/* Event payload strings and argument views are borrowed for the callback.
 * Widget references can be retained, but must still be live when next used. */
enum ToriRS_WidgetEventType
{
    TORIRS_WIDGET_LOADED,
    TORIRS_WIDGET_CLOSED,
    TORIRS_WIDGET_STATE_CHANGED,
    TORIRS_WIDGET_BEFORE_LAYOUT,
    TORIRS_WIDGET_AFTER_LAYOUT,
    TORIRS_WIDGET_OPERATION,
    TORIRS_SCRIPT_PRE_FIRED,
    TORIRS_SCRIPT_POST_FIRED,
    TORIRS_SCRIPT_CALLBACK,
    TORIRS_WIDGET_BOUND,
    TORIRS_WIDGET_UNBOUND
};

struct ToriRS_WidgetEvent
{
    enum ToriRS_WidgetEventType type;
    struct ToriRS_WidgetRef widget;
    uint64_t native_revision;
    /* Script fields are available only with the CS2 capability. */
    int32_t script_id;
    char const* callback_name;
    int32_t operation;
    /* Present for a semantic binding subscription. Borrowed for this call. */
    char const* role;
};

struct ToriRS_Api;
typedef void (*ToriRS_WidgetListener)(struct ToriRS_Api*, void* user,
                                    struct ToriRS_WidgetEvent const* event);

/* Initial production slice: shared widget lookup plus explicitly revision-
 * scoped native lookup, live setters, native layout, owned children/listeners.
 * Further product APIs are added alongside their ports, not via an alternate
 * declarative execution path. Setters are client-thread operations. */
struct ToriRS_WidgetApi
{
    void* context;
    enum ToriRS_ContractResult (*find)(void*, char const* role, struct ToriRS_WidgetRef*);
    enum ToriRS_ContractResult (*get_widget)(void*, int32_t component_id, struct ToriRS_WidgetRef*);
    enum ToriRS_ContractResult (*children)(void*, struct ToriRS_WidgetRef,
                                         struct ToriRS_WidgetRef*, size_t capacity, size_t* count);
    enum ToriRS_ContractResult (*bounds)(void*, struct ToriRS_WidgetRef, struct ToriRS_WidgetBounds*);
    /* Native-parent-local, unscrolled geometry; bounds is drawn canvas space. */
    enum ToriRS_ContractResult (*position)(void*, struct ToriRS_WidgetRef, struct ToriRS_WidgetBounds*);
    enum ToriRS_ContractResult (*get_text)(void*, struct ToriRS_WidgetRef, char*, size_t capacity, size_t* required);
    enum ToriRS_ContractResult (*set_position)(void*, struct ToriRS_WidgetRef, int32_t x, int32_t y);
    enum ToriRS_ContractResult (*set_size)(void*, struct ToriRS_WidgetRef, int32_t width, int32_t height);
    enum ToriRS_ContractResult (*revalidate)(void*, struct ToriRS_WidgetRef);
    enum ToriRS_ContractResult (*reset)(void*, struct ToriRS_WidgetRef);
    /* Follow a semantic binding at native publication boundaries. A new
     * subscription receives BOUND when available; replacement sends UNBOUND
     * for the old incarnation then BOUND for the new one. Hidden is still
     * bound. NULL listener unregisters this owner's subscription for the role. */
    enum ToriRS_ContractResult (*watch)(void*, char const* role, ToriRS_WidgetListener, void* user);

};

#endif
