#ifndef TORIRS_PLUGIN_V2_ADAPTER_H
#define TORIRS_PLUGIN_V2_ADAPTER_H

/*
 * Compatibility construction for one v2 plugin instance over the existing
 * language-neutral plugin API.
 *
 * The adapter contains no host or engine types.  Most operations are direct
 * translations to ToriRS_PluginApi.  Semantics that the old table cannot
 * represent (notably canonical named UI) are explicit hooks supplied by host
 * integration; they never fall back to component ids or legacy role names.
 */

#include "plugin/torirs_plugin_v2.h"

#include <stdbool.h>
#include <stdint.h>

struct ToriRS_PluginV2Adapter;

/*
 * A public resource ref is a positive, incarnation-fenced token, never the
 * legacy engine slot itself.  These are deliberately fixed-capacity: token
 * lookup is a decode plus one indexed comparison on every retained draw,
 * while allocation remains bounded by the host's existing per-plugin/global
 * resource ceilings.
 *
 * Eight low bits encode token-slot + 1, two bits encode the resource kind,
 * and five bits identify one of the host's 32 plugin instances.  The remaining
 * positive-int bits are the incarnation.  A slot is retired at the maximum
 * incarnation instead of wrapping and making its oldest token live again.
 */
#define TORIRS_PLUGIN_V2_RESOURCE_SLOT_BITS 8u
#define TORIRS_PLUGIN_V2_RESOURCE_KIND_BITS 2u
#define TORIRS_PLUGIN_V2_RESOURCE_NAMESPACE_BITS 5u
#define TORIRS_PLUGIN_V2_RESOURCE_NAMESPACE_SHIFT                                    \
    (TORIRS_PLUGIN_V2_RESOURCE_SLOT_BITS + TORIRS_PLUGIN_V2_RESOURCE_KIND_BITS)
#define TORIRS_PLUGIN_V2_RESOURCE_INCAR_SHIFT                                          \
    (TORIRS_PLUGIN_V2_RESOURCE_NAMESPACE_SHIFT +                                      \
     TORIRS_PLUGIN_V2_RESOURCE_NAMESPACE_BITS)
#define TORIRS_PLUGIN_V2_RESOURCE_INCAR_MAX                                            \
    ((uint32_t)INT32_MAX >> TORIRS_PLUGIN_V2_RESOURCE_INCAR_SHIFT)

#define TORIRS_PLUGIN_V2_IMAGE_TOKENS_MAX 192
#define TORIRS_PLUGIN_V2_MODEL_TOKENS_MAX 32
#define TORIRS_PLUGIN_V2_MESH_TOKENS_MAX 8
#define TORIRS_PLUGIN_V2_INSTANCE_TOKENS_MAX 64

/* A valid frame may retain one base plus every visual-state image for each
 * named node, two images for each of the two skinnable surfaces, one overlay
 * per surface, and all six scrollbar pieces. Keep the dependency ledger at
 * that real API/host maximum instead of imposing a smaller accidental cap. */
#define TORIRS_PLUGIN_V2_FRAME_NAMED_NODES_MAX 16
#define TORIRS_PLUGIN_V2_FRAME_SKIN_SURFACES_MAX 2
#define TORIRS_PLUGIN_V2_FRAME_SKIN_REFS_PER_SURFACE 2
#define TORIRS_PLUGIN_V2_FRAME_SCROLLBAR_REFS_MAX 6
#define TORIRS_PLUGIN_V2_FRAME_IMAGE_REFS_MAX                                      \
    (TORIRS_PLUGIN_V2_FRAME_NAMED_NODES_MAX * (1 + TORIRS_UI_VISUAL_STATE_COUNT) + \
     TORIRS_PLUGIN_V2_FRAME_SKIN_SURFACES_MAX *                                    \
         TORIRS_PLUGIN_V2_FRAME_SKIN_REFS_PER_SURFACE +                            \
     TORIRS_SURFACE_COUNT + TORIRS_PLUGIN_V2_FRAME_SCROLLBAR_REFS_MAX)

struct ToriRS_PluginV2ResourceToken
{
    int legacy;
    uint32_t incarnation;
    bool active;
    bool retired;
};

struct ToriRS_PluginV2AdapterHooks
{
    uint32_t struct_size;
    void* user;

    bool (*capability)(
        void* user,
        struct ToriRS_PluginCtx* context,
        char const* name);

    struct ToriRS_UiNodeRef (*ui_ref)(
        void* user,
        struct ToriRS_PluginCtx* context,
        char const* name);
    bool (*ui_info)(
        void* user,
        struct ToriRS_PluginCtx* context,
        struct ToriRS_UiNodeRef node,
        struct ToriRS_UiNodeInfo* out);
    bool (*ui_invoke)(
        void* user,
        struct ToriRS_PluginCtx* context,
        struct ToriRS_UiNodeRef node,
        char const* action);
    bool (*ui_contribution_info)(
        void* user,
        struct ToriRS_PluginCtx* context,
        char const* node,
        uint32_t facets,
        struct ToriRS_UiContributionInfo* out);
    enum ToriRS_Result (*ui_update)(
        void* user,
        struct ToriRS_PluginCtx* context,
        struct ToriRS_UiNodeRef node,
        uint32_t facets,
        struct ToriRS_UiNode const* value);

    /* Frame declarations have no legacy equivalents for canonical UI nodes
     * or non-ready explanations. */
    void (*frame_ui_node)(
        void* user,
        struct ToriRS_PluginCtx* context,
        char const* name,
        struct ToriRS_UiNode const* node);
    void (*frame_reason)(
        void* user,
        struct ToriRS_PluginCtx* context,
        char const* reason);

    /* Host-aware release keeps retained frame/UI state from observing a
     * recycled legacy slot.  The token is still live for the duration of this
     * hook and is invalidated by the adapter immediately after it returns. */
    void (*image_release)(
        void* user,
        struct ToriRS_PluginCtx* context,
        struct ToriRS_ImageRef image);

    /* The v1 table tears shipped models down with the plugin but has no
     * individual release verb. */
    void (*model_release)(
        void* user,
        struct ToriRS_PluginCtx* context,
        struct ToriRS_ModelRef model);

    /* Structured options cannot always be represented by the legacy pipe
     * string (labels may contain '|', and disabled/detail are real fields). */
    void (*panel_select)(
        void* user,
        struct ToriRS_PluginCtx* context,
        char const* id,
        char const* label,
        char const* value,
        struct ToriRS_SelectOption const* options,
        int option_count);
    /* Append-only authoritative host requests. The legacy table cannot
     * distinguish a queued read from a cached miss or decode failure.
     * Image/model callbacks write a zero-based legacy handle only for
     * PENDING/READY. */
    enum ToriRS_AssetState (*asset_request)(
        void* user,
        struct ToriRS_PluginCtx* context,
        char const* name);
    enum ToriRS_AssetState (*image_request)(
        void* user,
        struct ToriRS_PluginCtx* context,
        char const* name,
        int* out_image);
    enum ToriRS_AssetState (*model_request)(
        void* user,
        struct ToriRS_PluginCtx* context,
        char const* name,
        int* out_model);

    size_t (*memory_bytes)(
        void* user,
        struct ToriRS_PluginCtx* context);
    enum ToriRS_Result (*panel_set_options)(
        void* user,
        struct ToriRS_PluginCtx* context,
        char const* id,
        char const* value,
        struct ToriRS_SelectOption const* options,
        int option_count);

    /* Stable for this adapter lifetime and unique among the host's live
     * plugin instances.  Five encoded bits cover TORIRS_PLUGIN_MAX == 32. */
    uint32_t resource_namespace;
};

struct ToriRS_PluginV2Adapter
{
    struct ToriRS_ApiV2 api;
    struct ToriRS_ClientApiV2 client_api;
    struct ToriRS_GameApiV2 game_api;
    struct ToriRS_PluginApi const* legacy;
    struct ToriRS_PluginCtx* context;
    struct ToriRS_PluginV2AdapterHooks hooks;
    struct ToriRS_PluginV2ResourceToken image_tokens[TORIRS_PLUGIN_V2_IMAGE_TOKENS_MAX];
    struct ToriRS_PluginV2ResourceToken model_tokens[TORIRS_PLUGIN_V2_MODEL_TOKENS_MAX];
    struct ToriRS_PluginV2ResourceToken mesh_tokens[TORIRS_PLUGIN_V2_MESH_TOKENS_MAX];
    struct ToriRS_PluginV2ResourceToken
        instance_tokens[TORIRS_PLUGIN_V2_INSTANCE_TOKENS_MAX];
};

struct ToriRS_PluginV2DrawScope
{
    struct ToriRS_PluginV2Adapter* adapter;
    void* legacy_surface;
    bool active;
    bool clip_active;
    struct ToriRS_Rect clip;
};

struct ToriRS_PluginV2FrameScope
{
    struct ToriRS_PluginV2Adapter* adapter;
    bool active;
    bool invalid;
    uint32_t surface_mask;
    struct
    {
        int surface;
        int member;
    } members[64];
    int member_count;
    struct ToriRS_ImageRef image_refs[TORIRS_PLUGIN_V2_FRAME_IMAGE_REFS_MAX];
    int image_ref_count;
    char reason[TORIRS_FRAME_REASON_MAX];
};

struct ToriRS_PluginV2PanelScope
{
    struct ToriRS_PluginV2Adapter* adapter;
    bool active;
    int generated_id;
};

/** False only for an incompatible v1 ABI or malformed hook header. */
bool
ToriRS_PluginV2Adapter_Init(
    struct ToriRS_PluginV2Adapter* adapter,
    struct ToriRS_PluginApi const* legacy,
    struct ToriRS_PluginCtx* context,
    struct ToriRS_PluginV2AdapterHooks const* hooks);

/** Reconstruct the API/modules while preserving the retired/incarnation
 * resource ledger left by Reset. Used for the same plugin instance after a
 * disable/reload; never for uninitialized storage. */
bool
ToriRS_PluginV2Adapter_Reinit(
    struct ToriRS_PluginV2Adapter* adapter,
    struct ToriRS_PluginApi const* legacy,
    struct ToriRS_PluginCtx* context,
    struct ToriRS_PluginV2AdapterHooks const* hooks);

/** Invalidate every live resource token, advancing or retiring each token
 * slot, then clear callable adapter state without clearing that ledger. */
void
ToriRS_PluginV2Adapter_Reset(struct ToriRS_PluginV2Adapter* adapter);

struct ToriRS_ApiV2*
ToriRS_PluginV2Adapter_Api(struct ToriRS_PluginV2Adapter* adapter);

/* Internal bridge helpers.  Resolution validates type, token slot, liveness,
 * and incarnation before returning the zero-based legacy handle. */
int
ToriRS_PluginV2Adapter_ImageUnbox(
    struct ToriRS_PluginV2Adapter const* adapter,
    struct ToriRS_ImageRef image);

int
ToriRS_PluginV2Adapter_ModelUnbox(
    struct ToriRS_PluginV2Adapter const* adapter,
    struct ToriRS_ModelRef model);

void
ToriRS_PluginV2Adapter_DrawBegin(
    struct ToriRS_PluginV2Adapter* adapter,
    void* legacy_surface,
    struct ToriRS_PluginV2DrawScope* scope,
    struct ToriRS_DrawBuilder* out);

void
ToriRS_PluginV2Adapter_DrawEnd(
    struct ToriRS_PluginV2DrawScope* scope,
    struct ToriRS_DrawBuilder* builder);

/** Restrict this callback-scoped builder to an already resolved semantic
 * tree clip. The setting dies with the scope. */
void
ToriRS_PluginV2Adapter_DrawClip(
    struct ToriRS_PluginV2DrawScope* scope,
    struct ToriRS_Rect clip);

void
ToriRS_PluginV2Adapter_FrameBegin(
    struct ToriRS_PluginV2Adapter* adapter,
    struct ToriRS_PluginV2FrameScope* scope,
    struct ToriRS_FrameBuilder* out);

void
ToriRS_PluginV2Adapter_FrameEnd(
    struct ToriRS_PluginV2FrameScope* scope,
    struct ToriRS_FrameBuilder* builder);

char const*
ToriRS_PluginV2Adapter_FrameReason(struct ToriRS_PluginV2FrameScope const* scope);

/** True when the transaction used valid, non-duplicate declarations and
 * declared the required viewport surface. */
bool
ToriRS_PluginV2Adapter_FrameValid(struct ToriRS_PluginV2FrameScope const* scope);

void
ToriRS_PluginV2Adapter_PanelBegin(
    struct ToriRS_PluginV2Adapter* adapter,
    struct ToriRS_PluginV2PanelScope* scope,
    struct ToriRS_PanelBuilder* out);

void
ToriRS_PluginV2Adapter_PanelEnd(
    struct ToriRS_PluginV2PanelScope* scope,
    struct ToriRS_PanelBuilder* builder);

#endif /* TORIRS_PLUGIN_V2_ADAPTER_H */
