#include "plugin/torirs_plugin_contract.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(c) do { if(!(c)) { fprintf(stderr,"contract FAIL %s:%d: %s\n",__FILE__,__LINE__,#c); return 1; } } while(0)

int main(void)
{
    struct ToriRS_ElementRef a = {{1,2,3}}, reused = {{1,2,4}}, other_tree = {{2,2,3}};
    CHECK(TORIRS_PLUGIN_CONTRACT_MAJOR == 3);
    CHECK(ToriRS_ElementRefValid(a));
    CHECK(!ToriRS_ElementRefValid((struct ToriRS_ElementRef){{1,0,3}}));
    CHECK(!ToriRS_ElementRefEqual(a,reused));
    CHECK(!ToriRS_ElementRefEqual(a,other_tree));
    CHECK(!ToriRS_ContractPhaseAllows(TORIRS_PHASE_LAYOUT,TORIRS_OPERATION_INVOKE_NATIVE));
    CHECK(!ToriRS_ContractPhaseAllows(TORIRS_PHASE_PAINT,TORIRS_OPERATION_DECLARE));
    CHECK(!ToriRS_ContractPhaseAllows(TORIRS_PHASE_STOP,TORIRS_OPERATION_INVOKE_NATIVE));
    CHECK(ToriRS_ContractPhaseAllows(TORIRS_PHASE_ACTION,TORIRS_OPERATION_INVOKE_NATIVE));
    CHECK(ToriRS_ContractPhaseAllows(TORIRS_PHASE_STOP,TORIRS_OPERATION_RELEASE));
    struct ToriRS_PropertyClaim claims[] = {
        {a,TORIRS_PROPERTY_WIDTH,1}, {a,TORIRS_PROPERTY_WIDTH,2},
        {reused,TORIRS_PROPERTY_IMAGE_MAPPING,1}, {a,TORIRS_PROPERTY_TEXT_PALETTE,3}
    };
    bool suspended[4];
    CHECK(ToriRS_ContractResolveClaims(claims,4,suspended) == TORIRS_CONTRACT_CONFLICT);
    CHECK(suspended[0] && suspended[1] && suspended[2] && !suspended[3]);
    struct ToriRS_PropertyClaim reversed[4];
    for(int i=0;i<4;i++) reversed[i]=claims[3-i];
    CHECK(ToriRS_ContractResolveClaims(reversed,4,suspended) == TORIRS_CONTRACT_CONFLICT);
    CHECK(!suspended[0] && suspended[1] && suspended[2] && suspended[3]);
    claims[1].properties=TORIRS_PROPERTY_HEIGHT;
    CHECK(ToriRS_ContractResolveClaims(claims,4,suspended) == TORIRS_CONTRACT_OK);
    for(int i=0;i<4;i++) CHECK(!suspended[i]);
    claims[1].properties=UINT64_C(1)<<63;
    suspended[0]=true;
    CHECK(ToriRS_ContractResolveClaims(claims,4,suspended) == TORIRS_CONTRACT_INVALID_DECLARATION);
    CHECK(suspended[0]); /* invalid candidate does not publish a partial result */
    claims[1]=claims[0];
    CHECK(ToriRS_ContractResolveClaims(claims,4,suspended) == TORIRS_CONTRACT_INVALID_DECLARATION);
    CHECK(ToriRS_ContractResolveClaims(NULL,TORIRS_CONTRACT_CLAIMS_MAX+1,NULL) == TORIRS_CONTRACT_BUDGET_EXCEEDED);
    puts("plugin contract major 3 policy: passed");
    return 0;
}
