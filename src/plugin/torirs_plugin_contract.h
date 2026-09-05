#ifndef TORIRS_PLUGIN_CONTRACT_H
#define TORIRS_PLUGIN_CONTRACT_H

/* Public, language-neutral contract for API major 3. No UITree, App, numeric
 * cache lookup or previous plugin ABI is part of this surface. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TORIRS_PLUGIN_CONTRACT_MAJOR 3u
#define TORIRS_PLUGIN_CONTRACT_MINOR 0u
#define TORIRS_CONTRACT_CLAIMS_MAX 512u

struct ToriRS_ElementRef { uint64_t opaque[3]; };
struct ToriRS_ActionRef { struct ToriRS_ElementRef element; uint64_t operation; uint64_t revision; };
struct ToriRS_ClaimBundleRef { uint64_t opaque[2]; };

enum ToriRS_ContractResult
{
    TORIRS_CONTRACT_OK,
    TORIRS_CONTRACT_UNAVAILABLE,
    TORIRS_CONTRACT_STALE_REFERENCE,
    TORIRS_CONTRACT_WRONG_PHASE,
    TORIRS_CONTRACT_CONFLICT,
    TORIRS_CONTRACT_INVALID_DECLARATION,
    TORIRS_CONTRACT_UNSUPPORTED_LAYOUT,
    TORIRS_CONTRACT_NATIVE_BLOCKED,
    TORIRS_CONTRACT_PENDING,
    TORIRS_CONTRACT_BUDGET_EXCEEDED,
    TORIRS_CONTRACT_FAILED
};

enum ToriRS_ContractPhase
{
    TORIRS_PHASE_DESCRIBE,
    TORIRS_PHASE_ACTIVATE,
    TORIRS_PHASE_NATIVE_EVENT,
    TORIRS_PHASE_LAYOUT,
    TORIRS_PHASE_ACTION,
    TORIRS_PHASE_PAINT,
    TORIRS_PHASE_STOP
};

enum ToriRS_ContractOperation
{
    TORIRS_OPERATION_READ,
    TORIRS_OPERATION_DECLARE,
    TORIRS_OPERATION_INVOKE_NATIVE,
    TORIRS_OPERATION_RELEASE
};

struct ToriRS_CapabilityRequirement
{
    char const* name;
    uint32_t version;
    bool optional;
};

/* Coordinate labels are mandatory. Script computed geometry is native-parent
 * local and unscrolled; effective canvas bounds include scroll and placement. */
enum ToriRS_CoordinateSpace
{
    TORIRS_COORD_NATIVE_PARENT,
    TORIRS_COORD_CANVAS,
    TORIRS_COORD_WINDOW,
    TORIRS_COORD_DRAWABLE
};

struct ToriRS_ContractRect { int32_t x, y, width, height; };

enum ToriRS_PresentationProperty
{
    TORIRS_PROPERTY_X = UINT64_C(1) << 0,
    TORIRS_PROPERTY_Y = UINT64_C(1) << 1,
    TORIRS_PROPERTY_WIDTH = UINT64_C(1) << 2,
    TORIRS_PROPERTY_HEIGHT = UINT64_C(1) << 3,
    TORIRS_PROPERTY_IMAGE_MAPPING = UINT64_C(1) << 4,
    TORIRS_PROPERTY_FILL_MAPPING = UINT64_C(1) << 5,
    TORIRS_PROPERTY_TEXT_PALETTE = UINT64_C(1) << 6,
    TORIRS_PROPERTY_TEXT_FONT = UINT64_C(1) << 7,
    TORIRS_PROPERTY_OPACITY_MULTIPLIER = UINT64_C(1) << 8,
    TORIRS_PROPERTY_MODEL_POSE_OFFSET = UINT64_C(1) << 9,
    TORIRS_PROPERTY_CLIP_POLICY = UINT64_C(1) << 10,
    TORIRS_PROPERTY_SELF_SUPPRESSION = UINT64_C(1) << 11
};
#define TORIRS_PROPERTY_ALL ((UINT64_C(1) << 12) - 1)

enum ToriRS_AttachmentRelation
{
    TORIRS_ATTACH_BEFORE,
    TORIRS_ATTACH_AFTER,
    TORIRS_ATTACH_REPLACE_SELF
};

enum ToriRS_ClipPolicy
{
    TORIRS_CLIP_NATIVE,
    /* The adapter must grant this for an identified frame wrapper. Native
     * scroll/mask restrictions and native availability still apply. */
    TORIRS_CLIP_FRAME_WRAPPER,
    /* Only a declared popup capability may escape its anchor's viewport. */
    TORIRS_CLIP_POPUP
};

struct ToriRS_ElementState
{
    struct ToriRS_ElementRef ref;
    uint64_t native_revision;
    uint64_t presentation_revision;
    struct ToriRS_ContractRect requested;
    struct ToriRS_ContractRect computed;
    struct ToriRS_ContractRect canvas;
    struct ToriRS_ContractRect clip;
    int32_t scroll_x, scroll_y, content_width, content_height;
    uint8_t native_opacity;
    bool native_paint_available;
    bool native_input_available;
    bool presented;
};

/* One property claim is scoped to one incarnation. A bundle is published or
 * released atomically. Two bundles claiming the same exclusive property both
 * suspend, including their dependent attachments; native state is exposed. */
struct ToriRS_PropertyClaim
{
    struct ToriRS_ElementRef element;
    uint64_t properties;
    uint64_t bundle;
};

bool ToriRS_ElementRefValid(struct ToriRS_ElementRef ref);
bool ToriRS_ElementRefEqual(struct ToriRS_ElementRef a, struct ToriRS_ElementRef b);
bool ToriRS_ContractPhaseAllows(enum ToriRS_ContractPhase phase, enum ToriRS_ContractOperation operation);

/* Engine policy shared by C and Lua publication. Output is per input claim,
 * and every claim in a conflicting bundle is suspended, independent of input
 * order. Intrinsically invalid declarations reject the whole candidate. */
enum ToriRS_ContractResult ToriRS_ContractResolveClaims(
    struct ToriRS_PropertyClaim const* claims, size_t count, bool* suspended);

struct ToriRS_PresentationTransaction;
struct ToriRS_UiPartDescription;
struct ToriRS_StyleMapping;

struct ToriRS_UiContractApi
{
    void* context;
    enum ToriRS_ContractResult (*resolve)(void*, char const* semantic_selector, struct ToriRS_ElementRef*);
    enum ToriRS_ContractResult (*collection)(void*, char const* semantic_selector, struct ToriRS_ElementRef*, size_t, size_t*);
    enum ToriRS_ContractResult (*state)(void*, struct ToriRS_ElementRef, struct ToriRS_ElementState*);
    enum ToriRS_ContractResult (*capability)(void*, char const* name, uint32_t version);
    /* Zero replace means a new bundle; otherwise replace that owner's bundle
     * atomically. Invalid candidates leave the previous bundle intact. */
    enum ToriRS_ContractResult (*begin)(void*, struct ToriRS_ClaimBundleRef replace, struct ToriRS_PresentationTransaction**);
    enum ToriRS_ContractResult (*allocate)(struct ToriRS_PresentationTransaction*, struct ToriRS_ElementRef, uint64_t axes, enum ToriRS_CoordinateSpace, struct ToriRS_ContractRect);
    enum ToriRS_ContractResult (*style)(struct ToriRS_PresentationTransaction*, struct ToriRS_ElementRef, struct ToriRS_StyleMapping const*);
    enum ToriRS_ContractResult (*attach)(struct ToriRS_PresentationTransaction*, struct ToriRS_ElementRef, enum ToriRS_AttachmentRelation, struct ToriRS_UiPartDescription const*, struct ToriRS_ElementRef*);
    enum ToriRS_ContractResult (*commit)(struct ToriRS_PresentationTransaction*, struct ToriRS_ClaimBundleRef*);
    void (*abort)(struct ToriRS_PresentationTransaction*);
    enum ToriRS_ContractResult (*release)(void*, struct ToriRS_ClaimBundleRef);
    enum ToriRS_ContractResult (*invoke)(void*, struct ToriRS_ActionRef);
};

#endif
