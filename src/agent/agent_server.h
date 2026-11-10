#ifndef SRC_AGENT_AGENT_SERVER_H
#define SRC_AGENT_AGENT_SERVER_H

/**
 * The agent control server (docs/AGENT_API.md): a way for a program -- an AI
 * agent, a test, a shell loop -- to USE the client and the map editor through
 * an API instead of through the OS.
 *
 * Until this existed a program that wanted to drive the editor posted Win32
 * window messages at it and read screenshots back
 * (tools/editor_drive/drive.py), or set one of the TORIRS_EDITOR_* variables
 * that pose a single photograph. It could not ask what is on screen, press a
 * button by its label, or learn that an action was refused except by reading
 * pixels.
 *
 * TRANSPORT. A loopback TCP listener inside the client process, off unless
 * asked for: `--agent-port <N>` or TORIRS_AGENT_PORT=<N> (0 picks a free
 * port). Bound to 127.0.0.1 and nothing else. Non-blocking sockets, polled
 * once per main-loop iteration by AgentServer_Frame, as the embedded editor
 * server's door is (torirsmaped/torirs_maped_net.c). Several clients may
 * connect; each request is answered on the connection it came from.
 *
 * PROTOCOL. One JSON object per line each way, UTF-8:
 *   -> {"id": 7, "verb": "ui", "panel": "Catalog"}
 *   <- {"id": 7, "ok": true, "panels": [...]}
 *   <- {"id": 7, "ok": false, "error": "<one sentence>"}
 * A verb that must wait (frames, a condition, a click's release) holds its
 * reply until the frame it is satisfied on; other connections are served
 * meanwhile.
 *
 * INPUT GOES THROUGH THE COMMAND BUS, not the OS. The window procedure turns
 * OS events into CmdBus_Push* calls; this pushes the same commands on the same
 * bus, so injected input is what a person's is on every platform, needs no
 * window focus, and works under a headless video driver.
 *
 * Not built on the web: a page cannot listen on a socket, and the flag is
 * refused with a sentence.
 */

struct App;
struct ToriRS_CmdBus;
struct PlatformWindow;
struct AgentServer;

/**
 * Whether `text` is a port `--agent-port` can take: a whole number 0..65535.
 * Says why on stderr when it is not, so the argument parser can refuse the
 * flag the way it refuses a bad `--window`.
 */
int
AgentServer_PortTextOk(const char* text);

/**
 * Start listening when asked to.
 *
 * `port_text` is `--agent-port`'s value or NULL, when TORIRS_AGENT_PORT is
 * read instead; `port_file` is `--agent-port-file`'s or NULL, when
 * TORIRS_AGENT_PORT_FILE is. With neither port stated this returns NULL and
 * prints nothing: the server is off.
 *
 * On success it prints `agent: listening on 127.0.0.1:<port>` to stderr
 * (flushed: stderr is fully buffered here) and writes the port, as decimal
 * text and a newline, to the port file when one was named. Returns NULL with
 * the reason on stderr when the port cannot be bound, the port file cannot be
 * written, or the build has no sockets (the web).
 */
struct AgentServer*
AgentServer_Start(
    const char* port_text,
    const char* port_file);

/**
 * One main-loop iteration: accept, read, run what arrived, step what is
 * waiting, write. Call once per iteration BEFORE the frame's command drain,
 * so input a verb pushes on `bus` lands in this frame's edges.
 */
void
AgentServer_Frame(
    struct AgentServer* server,
    struct App* app,
    struct ToriRS_CmdBus* bus,
    struct PlatformWindow* platform);

/**
 * Close every connection (releasing on `bus` any key or button an agent left
 * held), the listener, and the port file. NULL is accepted (a deallocator);
 * `bus` may be NULL when the bus is already gone.
 */
void
AgentServer_Stop(
    struct AgentServer* server,
    struct ToriRS_CmdBus* bus);

#endif
