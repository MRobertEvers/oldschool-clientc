#ifndef TORIRS_PLUGIN_RUNTIME_H
#define TORIRS_PLUGIN_RUNTIME_H

/*
 * Host-private storage for one V2 plugin instance: its module table,
 * callback-scoped draw and panel builders, and incarnation-fenced resource
 * tokens. The
 * implementation is included by torirs_plugin_host.c so module calls reach
 * the host's checked primitives directly, without a compatibility ABI.
 */

#include "plugin/torirs_plugin_api.h"

#include <stdbool.h>
#include <stdint.h>

struct PluginV2Runtime;

/*
 * A public resource ref is a positive, incarnation-fenced token, never the
 * engine slot itself.  These are deliberately fixed-capacity: token
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

struct PluginV2ResourceToken
{
    int slot;
    uint32_t incarnation;
    uint64_t source_revision;
    bool active;
    bool retired;
};


struct PluginV2Runtime
{
    struct ToriRS_Api api;
    struct ToriRS_ClientApi client_api;
    struct ToriRS_GameApi game_api;
    struct PluginContext* context;
    struct PluginV2ResourceToken image_tokens[TORIRS_PLUGIN_V2_IMAGE_TOKENS_MAX];
    struct PluginV2ResourceToken model_tokens[TORIRS_PLUGIN_V2_MODEL_TOKENS_MAX];
    struct PluginV2ResourceToken mesh_tokens[TORIRS_PLUGIN_V2_MESH_TOKENS_MAX];
    struct PluginV2ResourceToken
        instance_tokens[TORIRS_PLUGIN_V2_INSTANCE_TOKENS_MAX];
};

struct PluginV2DrawScope
{
    struct PluginV2Runtime* runtime;
    void* surface_token;
    bool active;
    bool clip_active;
    struct ToriRS_Rect clip;
    bool context_valid;
    int origin_x;
    int origin_y;
    struct ToriRS_Rect local_bounds;
    struct ToriRS_Rect local_clip;
};

struct PluginV2PanelScope
{
    struct PluginV2Runtime* runtime;
    bool active;
    int generated_id;
};

/** False only for a malformed host-hook header. */
bool
plugin_v2_runtime_init(
    struct PluginV2Runtime* runtime,
    struct PluginContext* context);

/** Reconstruct the API/modules while preserving the retired/incarnation
 * resource ledger left by Reset. Used for the same plugin instance after a
 * disable/reload; never for uninitialized storage. */
bool
plugin_v2_runtime_reinit(
    struct PluginV2Runtime* runtime,
    struct PluginContext* context);

/** Invalidate every live resource token, advancing or retiring each token
 * slot, then clear callable runtime state without clearing that ledger. */
void
plugin_v2_runtime_reset(struct PluginV2Runtime* runtime);

/* Internal bridge helpers.  Resolution validates type, token slot, liveness,
 * and incarnation before returning the zero-based engine slot. */
int
plugin_v2_runtime_image_slot(
    struct PluginV2Runtime const* runtime,
    struct ToriRS_ImageRef image);

int
plugin_v2_runtime_model_slot(
    struct PluginV2Runtime const* runtime,
    struct ToriRS_ModelRef model);

void
plugin_v2_runtime_draw_begin(
    struct PluginV2Runtime* runtime,
    void* surface_token,
    struct PluginV2DrawScope* scope,
    struct ToriRS_Graphics* out);

void
plugin_v2_runtime_draw_end(
    struct PluginV2DrawScope* scope,
    struct ToriRS_Graphics* builder);

/** Set a callback-local drawing region; subsequent builder coordinates are
 * translated by its origin and clipped to it. */
void
plugin_v2_runtime_draw_region(
    struct PluginV2DrawScope* scope,
    struct ToriRS_Rect region);

void
plugin_v2_runtime_panel_begin(
    struct PluginV2Runtime* runtime,
    struct PluginV2PanelScope* scope,
    struct ToriRS_PanelBuilder* out);

void
plugin_v2_runtime_panel_end(
    struct PluginV2PanelScope* scope,
    struct ToriRS_PanelBuilder* builder);

#endif /* TORIRS_PLUGIN_RUNTIME_H */
