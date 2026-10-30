/*
 * The Live panel over the window's editor host connection (live_panel.h).
 *
 * The host carries the live-editor frames without interpreting them:
 * Editor_HostMapEdLiveSend sends one request fire-and-forget (every answer is
 * a broadcast fact), and DrainFacts keeps the facts in 173..190 for
 * Editor_HostMapEdLiveTake instead of the document sink (editor_host.h).
 */

#include "live_panel.h"

#include "editor/editor.h"
#include "editor/editor_host.h"
#include "mem/torirs_mem.h"

#include <assert.h>

static int
live_host_send(
    void* user,
    uint32_t type,
    const uint8_t* payload,
    uint32_t length)
{
    struct Editor* editor = user;
    return Editor_HostMapEdLiveSend(&editor->host, type, payload, length);
}

static int
live_host_take(
    void* user,
    uint32_t* out_type,
    uint8_t** out_body,
    uint32_t* out_length)
{
    struct Editor* editor = user;
    return Editor_HostMapEdLiveTake(&editor->host, out_type, out_body, out_length);
}

static void
live_host_release(
    void* user,
    uint8_t* body)
{
    (void)user;
    ToriMem_Free(ToriMem_Editor(), body);
}

static int
live_host_publish(
    void* user,
    uint32_t key,
    const int32_t* values,
    int count)
{
    struct Editor* editor = user;
    return Editor_HostMapEdStateSet(&editor->host, key, values, count);
}

void
Editor_Live_Register(struct EditorUi* ui)
{
    struct Editor_LiveTransport transport;

    assert(ui);
    if( !ui->editor || !ui->editor->host_open )
        return;
    transport.user = ui->editor;
    transport.send = live_host_send;
    transport.take = live_host_take;
    transport.release = live_host_release;
    transport.publish = live_host_publish;
    Editor_Live_RegisterWith(ui, &transport);
}
