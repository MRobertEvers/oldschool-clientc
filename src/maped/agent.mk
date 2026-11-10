# The agent control server (agent/agent_server.h, docs/AGENT_API.md): a
# loopback JSON-lines port an AI agent drives the client and the map editor
# through, off unless --agent-port / TORIRS_AGENT_PORT asks for it.
#
# Its sockets are winsock on Windows; the client's Windows lanes already link
# -lws2_32 (platform/platform.mk PLATFORM_LDFLAGS), so no link flag is added.
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR, CFLAGS and the content paths are defined: rule lines here name no
# such variable (it would expand empty), recipes use them freely.
MAPED_CLIENT_SRCS += agent/agent_json.c \
                     agent/agent_server.c \
                     agent/agent_verbs_core.c \
                     agent/agent_verbs_editor.c \
                     agent/agent_verbs_game.c

# The protocol's JSON on its own (agent/agent_json.h): the writer's commas,
# escapes and UTF-8 repair, the reader's flat object and every refusal a
# request can earn. Needs no client: agent_json.c and the allocator.
.PHONY: test-agent-json
test-agent-json:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/agent_json_test $(TORIMEM_SRCS) agent/agent_json.c \
		agent/test/agent_json_test.c $(MAPED_TEST_LDFLAGS)
	./$(OBJ_DIR)/agent_json_test
