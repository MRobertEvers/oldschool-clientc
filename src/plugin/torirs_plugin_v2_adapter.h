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
};

struct ToriRS_PluginV2Adapter
{
    struct ToriRS_ApiV2 api;
    struct ToriRS_PluginApi const* legacy;
    struct ToriRS_PluginCtx* context;
    struct ToriRS_PluginV2AdapterHooks hooks;
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

struct ToriRS_ApiV2*
ToriRS_PluginV2Adapter_Api(struct ToriRS_PluginV2Adapter* adapter);

/* Internal bridge helpers. Public v2 refs are zero-invalid/1-based; legacy
 * host resource tables use zero-valid/-1-invalid indices. */
int
ToriRS_PluginV2Adapter_ImageUnbox(struct ToriRS_ImageRef image);

int
ToriRS_PluginV2Adapter_ModelUnbox(struct ToriRS_ModelRef model);

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
