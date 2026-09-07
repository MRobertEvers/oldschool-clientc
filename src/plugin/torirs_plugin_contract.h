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
/* A plugin image handle (see assets.image). Zero is "no image"; the host
 * validates every use against the owning plugin's live resources. */
struct ToriRS_ImageRef
{
    int value;
};
struct ToriRS_ScriptRef { uint64_t token; };
struct ToriRS_ScriptEvent
{
    char const* name; /* borrowed during this callback */
    int32_t script_id;
    struct ToriRS_ScriptRef ref;
    struct ToriRS_WidgetRef widget; /* Optional live widget at the approved hook site. */
};
struct ToriRS_WidgetActionRef
{
    struct ToriRS_WidgetRef widget;
    uint64_t operation;
    uint64_t revision;
};

struct ToriRS_WidgetAction
{
    struct ToriRS_WidgetActionRef ref;
    char label[256]; /* Current native menu label, copied into caller storage. */
};
/* Capacity of an owned control's operation label, including the terminator.
 * The native adapter checks this against its menu option storage. */
#define TORIRS_WIDGET_OP_LABEL_MAX 64

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

static inline bool ToriRS_WidgetRefValid(struct ToriRS_WidgetRef ref)
{
    return ref.opaque[0] && ref.opaque[1] && ref.opaque[2];
}
static inline bool ToriRS_WidgetRefEqual(struct ToriRS_WidgetRef a, struct ToriRS_WidgetRef b)
{
    return a.opaque[0] == b.opaque[0] && a.opaque[1] == b.opaque[1] && a.opaque[2] == b.opaque[2];
}
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
    TORIRS_WIDGET_UNBOUND,
    TORIRS_WIDGET_TREE_CHANGED
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
/* Access is valid only during this plugin's synchronous script callback.
 * Stack indices are zero-based from the top. Setters cannot resize stacks. */
struct ToriRS_ScriptApi
{
    void* context;
    bool (*available)(void*);
    /* Coalesced native rebuild at its normal safe point, also valid during
     * shutdown. This schedules no plugin callback or arbitrary script. */
    enum ToriRS_ContractResult (*invalidate)(void*,char const* callback_name);
    enum ToriRS_ContractResult (*counts)(void*,struct ToriRS_ScriptRef,size_t* ints,size_t* strings);
    enum ToriRS_ContractResult (*get_int)(void*,struct ToriRS_ScriptRef,size_t index,int32_t*);
    enum ToriRS_ContractResult (*set_int)(void*,struct ToriRS_ScriptRef,size_t index,int32_t);
    enum ToriRS_ContractResult (*get_string)(void*,struct ToriRS_ScriptRef,size_t index,char*,size_t capacity,size_t* required);
    enum ToriRS_ContractResult (*set_string)(void*,struct ToriRS_ScriptRef,size_t index,char const*);
};
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
    enum ToriRS_ContractResult (*find_all)(void*, char const* role, struct ToriRS_WidgetRef*, size_t capacity, size_t* count);
    enum ToriRS_ContractResult (*get_widget)(void*, int32_t component_id, struct ToriRS_WidgetRef*);
    enum ToriRS_ContractResult (*children)(void*, struct ToriRS_WidgetRef,
                                         struct ToriRS_WidgetRef*, size_t capacity, size_t* count);
    /* Retained actions expire when widget identity, operation state or the
     * native menu row changes. Invoke always checks current native authority. */
    enum ToriRS_ContractResult (*visible)(void*,struct ToriRS_WidgetRef,bool*);
    enum ToriRS_ContractResult (*actions)(void*,struct ToriRS_WidgetRef,struct ToriRS_WidgetAction*,size_t capacity,size_t* count);
    enum ToriRS_ContractResult (*invoke)(void*,struct ToriRS_WidgetActionRef);
    enum ToriRS_ContractResult (*parent)(void*,struct ToriRS_WidgetRef,struct ToriRS_WidgetRef*);
    enum ToriRS_ContractResult (*bounds)(void*, struct ToriRS_WidgetRef, struct ToriRS_WidgetBounds*);
    /* Native-parent-local, unscrolled geometry; bounds is drawn canvas space. */
    enum ToriRS_ContractResult (*position)(void*, struct ToriRS_WidgetRef, struct ToriRS_WidgetBounds*);
    enum ToriRS_ContractResult (*get_text)(void*, struct ToriRS_WidgetRef, char*, size_t capacity, size_t* required);
    enum ToriRS_ContractResult (*set_position)(void*, struct ToriRS_WidgetRef, int32_t x, int32_t y);
    enum ToriRS_ContractResult (*set_size)(void*, struct ToriRS_WidgetRef, int32_t width, int32_t height);
    enum ToriRS_ContractResult (*set_hidden)(void*, struct ToriRS_WidgetRef, bool hidden);
    enum ToriRS_ContractResult (*set_projection_height)(void*,struct ToriRS_WidgetRef,int32_t height);
    enum ToriRS_ContractResult (*set_text_outline)(void*,struct ToriRS_WidgetRef,bool outline);
    enum ToriRS_ContractResult (*revalidate)(void*, struct ToriRS_WidgetRef);
    enum ToriRS_ContractResult (*reset)(void*, struct ToriRS_WidgetRef);
    /* Keys are scoped to owner and parent; repeated create_text returns the
     * same live child. These content setters/remove apply only to owned text. */
    enum ToriRS_ContractResult (*create_text)(void*, struct ToriRS_WidgetRef parent, char const* key, struct ToriRS_WidgetRef*);
    enum ToriRS_ContractResult (*set_text)(void*, struct ToriRS_WidgetRef, char const*);
    enum ToriRS_ContractResult (*set_text_color)(void*, struct ToriRS_WidgetRef, uint32_t rgb);
    enum ToriRS_ContractResult (*set_text_align)(void*, struct ToriRS_WidgetRef, int horizontal, int vertical);
    enum ToriRS_ContractResult (*remove)(void*, struct ToriRS_WidgetRef);
    /* Owned controls only: arm one left-click/menu operation with this label
     * (shorter than TORIRS_WIDGET_OP_LABEL_MAX). The listener receives
     * TORIRS_WIDGET_OPERATION through the normal native hit test and retained
     * menu checks. Replacing the listener retires earlier retained rows. A NULL
     * listener removes the operation. */
    enum ToriRS_ContractResult (*set_on_op)(void*,struct ToriRS_WidgetRef,char const* label,ToriRS_WidgetListener,void* user);
    /* Owned image controls. create_image returns this owner's keyed child
     * (idempotent like create_text). set_image installs one of this plugin's
     * live images and the control's size in canvas pixels; releasing the image
     * later blanks the control. set_opacity: 255 opaque .. 0 invisible, owned
     * widgets only. */
    enum ToriRS_ContractResult (*create_image)(void*,struct ToriRS_WidgetRef parent,char const* key,struct ToriRS_WidgetRef* out);
    enum ToriRS_ContractResult (*set_image)(void*,struct ToriRS_WidgetRef,struct ToriRS_ImageRef image,int width,int height);
    enum ToriRS_ContractResult (*set_opacity)(void*,struct ToriRS_WidgetRef,int opacity);

    /* Follow a semantic binding at native publication boundaries. A new
     * subscription receives BOUND when available; replacement sends UNBOUND
     * for the old incarnation then BOUND for the new one. Hidden is still
     * bound. NULL listener unregisters this owner's subscription for the role. */
    enum ToriRS_ContractResult (*watch)(void*, char const* role, ToriRS_WidgetListener, void* user);
    /* Initial notification and subsequent topology publications. Geometry or
     * hiding alone do not trigger this. Event widget is empty; query live refs.
     * A replacement subscription starts at the next publication fence. */
    enum ToriRS_ContractResult (*watch_tree)(void*, ToriRS_WidgetListener, void* user);

};

#endif
