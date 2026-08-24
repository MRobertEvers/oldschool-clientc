/* Command-name contract shared by the CS2 compiler and decompiler. */

#include "cs2/cs2_command.h"
#include "rscache_test.h"

int
main(void)
{
    RSCACHE_TEST_GROUP("canonical names from current VM metadata");
    RSCACHE_CHECK_STR_EQ(RSCache_CS2_CommandName(103), "overlay_cc_create");
    RSCACHE_CHECK_STR_EQ(RSCache_CS2_CommandName(1703), "cc_getcomponentparam");
    RSCACHE_CHECK_STR_EQ(RSCache_CS2_CommandName(7000), "highlight_npc_setup");
    RSCACHE_CHECK_STR_EQ(RSCache_CS2_CommandName(6750), "npc_name");
    RSCACHE_CHECK_STR_EQ(RSCache_CS2_CommandName(6902), "p_routelength");
    RSCACHE_CHECK_STR_EQ(RSCache_CS2_CommandName(7040), "highlight_group_setup");
    RSCACHE_CHECK_STR_EQ(RSCache_CS2_CommandName(8021), "enum_getoutputs");
    RSCACHE_CHECK_STR_EQ(RSCache_CS2_CommandName(8022), "array_new");

    RSCACHE_TEST_GROUP("compiler command lookup");
    RSCACHE_CHECK_EQ(RSCache_CS2_CommandOfName("cc_getcomponentparam"), 1703);
    RSCACHE_CHECK_EQ(RSCache_CS2_CommandOfName("CC_GETCOMPONENTPARAM"), 1703);
    RSCACHE_CHECK_EQ(RSCache_CS2_CommandOfName("highlight_npc_setup"), 7000);
    RSCACHE_CHECK_EQ(RSCache_CS2_CommandOfName("npc_name"), 6750);
    RSCACHE_CHECK_EQ(RSCache_CS2_CommandOfName("P_ROUTELENGTH"), 6902);
    RSCACHE_CHECK_EQ(RSCache_CS2_CommandOfName("highlight_group_setup"), 7040);
    RSCACHE_CHECK_EQ(RSCache_CS2_CommandOfName("enum_getoutputs"), 8021);
    RSCACHE_CHECK_EQ(
        RSCache_CS2_CommandArg(RSCache_CS2_CommandGet(3113), 1),
        RSCACHE_CS2_PROTO_BOOLEAN);

    RSCACHE_TEST_GROUP("numeric compatibility aliases");
    RSCACHE_CHECK_EQ(RSCache_CS2_CommandOfName("_103"), 103);
    RSCACHE_CHECK_EQ(RSCache_CS2_CommandOfName("_1703"), 1703);
    RSCACHE_CHECK_EQ(RSCache_CS2_CommandOfName("_7000"), 7000);
    RSCACHE_CHECK_EQ(RSCache_CS2_CommandOfName("_9999"), -1);
    RSCACHE_CHECK_EQ(RSCache_CS2_CommandOfName("_1703x"), -1);

    RSCACHE_TEST_GROUP("previous semantic aliases");
    RSCACHE_CHECK_EQ(RSCache_CS2_CommandOfName("activeplayer_getroutelength"), 6902);
    RSCACHE_CHECK_EQ(RSCache_CS2_CommandOfName("highlight_opgroup_setup"), 7040);

    return rscache_test_report("test_cs2_command");
}
