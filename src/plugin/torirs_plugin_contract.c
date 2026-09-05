#include "plugin/torirs_plugin_contract.h"

bool ToriRS_ElementRefValid(struct ToriRS_ElementRef ref)
{
    return ref.opaque[0] && ref.opaque[1] && ref.opaque[2];
}

bool ToriRS_ElementRefEqual(struct ToriRS_ElementRef a, struct ToriRS_ElementRef b)
{
    return a.opaque[0] == b.opaque[0] && a.opaque[1] == b.opaque[1] && a.opaque[2] == b.opaque[2];
}

bool ToriRS_ContractPhaseAllows(enum ToriRS_ContractPhase phase, enum ToriRS_ContractOperation operation)
{
    if( phase < TORIRS_PHASE_DESCRIBE || phase > TORIRS_PHASE_STOP ) return false;
    switch( operation )
    {
    case TORIRS_OPERATION_READ: return true;
    case TORIRS_OPERATION_DECLARE:
        return phase == TORIRS_PHASE_ACTIVATE || phase == TORIRS_PHASE_NATIVE_EVENT ||
               phase == TORIRS_PHASE_LAYOUT || phase == TORIRS_PHASE_ACTION;
    case TORIRS_OPERATION_INVOKE_NATIVE: return phase == TORIRS_PHASE_ACTION;
    case TORIRS_OPERATION_RELEASE: return phase != TORIRS_PHASE_DESCRIBE && phase != TORIRS_PHASE_PAINT;
    default: return false;
    }
}

enum ToriRS_ContractResult ToriRS_ContractResolveClaims(
    struct ToriRS_PropertyClaim const* claims, size_t count, bool* suspended)
{
    bool conflict = false;
    if( count > TORIRS_CONTRACT_CLAIMS_MAX ) return TORIRS_CONTRACT_BUDGET_EXCEEDED;
    if( count && (!claims || !suspended) ) return TORIRS_CONTRACT_INVALID_DECLARATION;
    for( size_t i = 0; i < count; i++ )
        if( !ToriRS_ElementRefValid(claims[i].element) || !claims[i].bundle ||
            !claims[i].properties || (claims[i].properties & ~TORIRS_PROPERTY_ALL) )
            return TORIRS_CONTRACT_INVALID_DECLARATION;
    for( size_t i = 0; i < count; i++ )
        for( size_t j = i + 1; j < count; j++ )
            if( claims[i].bundle == claims[j].bundle &&
                ToriRS_ElementRefEqual(claims[i].element, claims[j].element) &&
                (claims[i].properties & claims[j].properties) )
                return TORIRS_CONTRACT_INVALID_DECLARATION;
    for( size_t i = 0; i < count; i++ ) suspended[i] = false;
    for( size_t i = 0; i < count; i++ )
        for( size_t j = i + 1; j < count; j++ )
            if( claims[i].bundle != claims[j].bundle &&
                ToriRS_ElementRefEqual(claims[i].element, claims[j].element) &&
                (claims[i].properties & claims[j].properties) )
            {
                conflict = true;
                suspended[i] = suspended[j] = true;
            }
    for( size_t i = 0; i < count; i++ )
        if( suspended[i] )
            for( size_t j = 0; j < count; j++ )
                if( claims[i].bundle == claims[j].bundle ) suspended[j] = true;
    return conflict ? TORIRS_CONTRACT_CONFLICT : TORIRS_CONTRACT_OK;
}
