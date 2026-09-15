/* Modern Windows browser backend: one WebView2 controller in the existing
 * plugin-chrome child HWND. The loader is resolved beside the executable (or
 * TORIRS_WEBVIEW2_LOADER); no browser/navigation UI is exposed. */

#define COBJMACROS
#include "platform/platform_win32_browser_backend.h"

#if defined(__has_include)
#  if __has_include(<WebView2.h>)
#    define TORIRS_HAVE_WEBVIEW2_SDK 1
#  endif
#endif

#if defined(TORIRS_HAVE_WEBVIEW2_SDK)

#include <WebView2.h>
#include <objidl.h>
#include <ole2.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WV2_INBOUND_MAX 128
#define WV2_OUTBOUND_MAX 64
#define WV2_JSON_MAX (8 * 1024 * 1024)

static WCHAR const WV2_BRIDGE_SCRIPT[] =
    L"window.torirsPluginChromePostMessage=function(s){"
    L"window.chrome.webview.postMessage(String(s));return true;};";

typedef HRESULT(STDAPICALLTYPE* CreateEnvironmentFn)(
    PCWSTR,
    PCWSTR,
    ICoreWebView2EnvironmentOptions*,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);

struct PlatformWin32Browser
{
    HWND parent;
    HMODULE loader;
    int com_initialized;
    int ready;
    int failed;
    int send_failed;
    int closing;
    int loading_page;
    int width;
    int height;
    LONG refs;
    WCHAR page_url[2048];
    WCHAR allowed_root[2048];
    WCHAR allowed_asset_root[2048];

    ICoreWebView2Environment* environment;
    ICoreWebView2Controller* controller;
    ICoreWebView2* core;
    ICoreWebView2_4* core4;
    EventRegistrationToken nav_start_token;
    EventRegistrationToken nav_done_token;
    EventRegistrationToken new_window_token;
    EventRegistrationToken permission_token;
    EventRegistrationToken web_message_token;
    EventRegistrationToken resource_token;
    EventRegistrationToken download_token;

    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler environment_handler;
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler controller_handler;
    ICoreWebView2NavigationStartingEventHandler nav_start_handler;
    ICoreWebView2NavigationCompletedEventHandler nav_done_handler;
    ICoreWebView2NewWindowRequestedEventHandler new_window_handler;
    ICoreWebView2PermissionRequestedEventHandler permission_handler;
    ICoreWebView2WebMessageReceivedEventHandler web_message_handler;
    ICoreWebView2WebResourceRequestedEventHandler resource_handler;
    ICoreWebView2DownloadStartingEventHandler download_handler;
    ICoreWebView2ExecuteScriptCompletedHandler execute_handler;
#if defined(TORIRS_WIN32_BROWSER_CAPTURE_API)
    ICoreWebView2CapturePreviewCompletedHandler capture_handler;
    IStream* capture_stream;
    char capture_path[MAX_PATH];
    int capture_pending;
    int capture_result;
#endif

    char* inbound[WV2_INBOUND_MAX];
    int inbound_count;
    char* outbound[WV2_OUTBOUND_MAX];
    int outbound_count;
};

static void wv2_fail(
    struct PlatformWin32Browser* s, char const* stage, HRESULT error)
{
    if( s ) s->failed = 1;
    fprintf(
        stderr, "plugin chrome WebView2: %s failed (HRESULT 0x%08lx)\n",
        stage ? stage : "initialization", (unsigned long)error);
}

#define WV2_FROM(field, pointer) \
    CONTAINING_RECORD((pointer), struct PlatformWin32Browser, field)

static ULONG wv2_addref(struct PlatformWin32Browser* s)
{ return (ULONG)InterlockedIncrement(&s->refs); }

static void wv2_destroy_state(struct PlatformWin32Browser* s)
{
    /* Loader and COM apartment must outlive every asynchronous handler ref.
     * If shutdown wins the race with environment/controller creation, the
     * handler observes `closing`; the SDK's final Release then retires this
     * allocation without dereferencing freed callback storage. */
    if( s->loader ) FreeLibrary(s->loader);
    if( s->com_initialized ) CoUninitialize();
    free(s);
}

static ULONG wv2_release(struct PlatformWin32Browser* s)
{
    LONG value = InterlockedDecrement(&s->refs);
    if( value == 0 )
        wv2_destroy_state(s);
    return (ULONG)value;
}

#define WV2_HANDLER_IUNKNOWN(prefix, type, field, iid_value)                         \
static HRESULT STDMETHODCALLTYPE prefix##_qi(type* self, REFIID iid, void** out)     \
{                                                                                     \
    struct PlatformWin32Browser* s = WV2_FROM(field, self);                            \
    if( !out ) return E_POINTER;                                                       \
    *out = NULL;                                                                       \
    if( IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &(iid_value)) )              \
        *out = self;                                                                   \
    if( !*out ) return E_NOINTERFACE;                                                  \
    wv2_addref(s);                                                                     \
    return S_OK;                                                                       \
}                                                                                     \
static ULONG STDMETHODCALLTYPE prefix##_addref(type* self)                            \
{ return wv2_addref(WV2_FROM(field, self)); }                                          \
static ULONG STDMETHODCALLTYPE prefix##_release(type* self)                           \
{ return wv2_release(WV2_FROM(field, self)); }

WV2_HANDLER_IUNKNOWN(env, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
    environment_handler, IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler)
WV2_HANDLER_IUNKNOWN(ctl, ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
    controller_handler, IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler)
WV2_HANDLER_IUNKNOWN(nav_start, ICoreWebView2NavigationStartingEventHandler,
    nav_start_handler, IID_ICoreWebView2NavigationStartingEventHandler)
WV2_HANDLER_IUNKNOWN(nav_done, ICoreWebView2NavigationCompletedEventHandler,
    nav_done_handler, IID_ICoreWebView2NavigationCompletedEventHandler)
WV2_HANDLER_IUNKNOWN(new_window, ICoreWebView2NewWindowRequestedEventHandler,
    new_window_handler, IID_ICoreWebView2NewWindowRequestedEventHandler)
WV2_HANDLER_IUNKNOWN(permission, ICoreWebView2PermissionRequestedEventHandler,
    permission_handler, IID_ICoreWebView2PermissionRequestedEventHandler)
WV2_HANDLER_IUNKNOWN(web_message, ICoreWebView2WebMessageReceivedEventHandler,
    web_message_handler, IID_ICoreWebView2WebMessageReceivedEventHandler)
WV2_HANDLER_IUNKNOWN(resource, ICoreWebView2WebResourceRequestedEventHandler,
    resource_handler, IID_ICoreWebView2WebResourceRequestedEventHandler)
WV2_HANDLER_IUNKNOWN(download, ICoreWebView2DownloadStartingEventHandler,
    download_handler, IID_ICoreWebView2DownloadStartingEventHandler)
WV2_HANDLER_IUNKNOWN(execute, ICoreWebView2ExecuteScriptCompletedHandler,
    execute_handler, IID_ICoreWebView2ExecuteScriptCompletedHandler)
#if defined(TORIRS_WIN32_BROWSER_CAPTURE_API)
WV2_HANDLER_IUNKNOWN(capture, ICoreWebView2CapturePreviewCompletedHandler,
    capture_handler, IID_ICoreWebView2CapturePreviewCompletedHandler)
#endif

static WCHAR* wide_from_utf8(char const* input)
{
    int chars;
    WCHAR* out;
    if( !input ) return NULL;
    chars = MultiByteToWideChar(CP_UTF8, 0, input, -1, NULL, 0);
    if( chars <= 0 || chars > WV2_JSON_MAX ) return NULL;
    out = (WCHAR*)malloc((size_t)chars * sizeof(*out));
    if( !out ) return NULL;
    if( !MultiByteToWideChar(CP_UTF8, 0, input, -1, out, chars) )
    { free(out); return NULL; }
    return out;
}

static char* utf8_from_wide(WCHAR const* input)
{
    int bytes;
    char* out;
    if( !input ) return NULL;
    bytes = WideCharToMultiByte(CP_UTF8, 0, input, -1, NULL, 0, NULL, NULL);
    if( bytes <= 0 || bytes > WV2_JSON_MAX ) return NULL;
    out = (char*)malloc((size_t)bytes);
    if( !out ) return NULL;
    if( !WideCharToMultiByte(CP_UTF8, 0, input, -1, out, bytes, NULL, NULL) )
    { free(out); return NULL; }
    return out;
}

static int wide_starts_ci(WCHAR const* value, WCHAR const* prefix)
{
    size_t n = prefix ? wcslen(prefix) : 0;
    return value && prefix && n > 0 && _wcsnicmp(value, prefix, n) == 0;
}

static int allowed_resource_uri(
    struct PlatformWin32Browser const* s, WCHAR const* uri)
{
    return uri && (wide_starts_ci(uri, s->allowed_root) ||
                   wide_starts_ci(uri, s->allowed_asset_root) ||
                   wide_starts_ci(uri, L"data:") ||
                   wide_starts_ci(uri, L"blob:") ||
                   _wcsicmp(uri, L"about:blank") == 0);
}

static int allowed_navigation_uri(
    struct PlatformWin32Browser const* s, WCHAR const* uri)
{
    /* Subresources may be local bundle/data/blob objects, but the top-level
     * control is permanently pinned to the one staged application page. */
    return uri && _wcsicmp(uri, s->page_url) == 0;
}

static void outbound_push(struct PlatformWin32Browser* s, WCHAR const* message)
{
    char* copy = utf8_from_wide(message);
    if( !copy ) { s->send_failed = 1; return; }
    if( s->outbound_count >= WV2_OUTBOUND_MAX )
    { free(copy); s->send_failed = 1; return; }
    s->outbound[s->outbound_count++] = copy;
}

static int execute_json(struct PlatformWin32Browser* s, char const* json)
{
    size_t size;
    char* script;
    WCHAR* wide;
    HRESULT hr = E_FAIL;
    if( !s->core || !json ) return 0;
    size = strlen(json) + 192;
    script = (char*)malloc(size);
    if( !script ) return 0;
    snprintf(script, size,
        "(function(){try{return !!(window.ToriRSPluginChrome&&"
        "window.ToriRSPluginChrome.receive(%s)!==false);}" 
        "catch(e){return false;}})();", json);
    wide = wide_from_utf8(script);
    if( wide )
        hr = ICoreWebView2_ExecuteScript(s->core, wide, &s->execute_handler);
    free(wide);
    free(script);
    return SUCCEEDED(hr);
}

static void flush_inbound(struct PlatformWin32Browser* s)
{
    for( int i = 0; i < s->inbound_count; i++ )
    {
        if( !execute_json(s, s->inbound[i]) )
            s->send_failed = 1;
        free(s->inbound[i]);
        s->inbound[i] = NULL;
    }
    s->inbound_count = 0;
}

static HRESULT STDMETHODCALLTYPE nav_start_invoke(
    ICoreWebView2NavigationStartingEventHandler* self,
    ICoreWebView2* sender,
    ICoreWebView2NavigationStartingEventArgs* args)
{
    struct PlatformWin32Browser* s = WV2_FROM(nav_start_handler, self);
    LPWSTR uri = NULL;
    (void)sender;
    if( args && SUCCEEDED(ICoreWebView2NavigationStartingEventArgs_get_Uri(args, &uri)) )
    {
        if( !allowed_navigation_uri(s, uri) )
        {
            s->loading_page = 0;
            ICoreWebView2NavigationStartingEventArgs_put_Cancel(args, TRUE);
        }
        else if( _wcsicmp(uri, s->page_url) == 0 )
        {
            s->loading_page = 1;
            s->ready = 0;
        }
        CoTaskMemFree(uri);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE nav_done_invoke(
    ICoreWebView2NavigationCompletedEventHandler* self,
    ICoreWebView2* sender,
    ICoreWebView2NavigationCompletedEventArgs* args)
{
    struct PlatformWin32Browser* s = WV2_FROM(nav_done_handler, self);
    BOOL success = FALSE;
    (void)sender;
    if( !s->loading_page )
        return S_OK;
    s->loading_page = 0;
    if( !args || FAILED(ICoreWebView2NavigationCompletedEventArgs_get_IsSuccess(args, &success)) ||
        !success )
    { wv2_fail(s, "navigation", E_FAIL); return S_OK; }
    s->ready = 1;
    flush_inbound(s);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE new_window_invoke(
    ICoreWebView2NewWindowRequestedEventHandler* self,
    ICoreWebView2* sender,
    ICoreWebView2NewWindowRequestedEventArgs* args)
{
    (void)self; (void)sender;
    if( args ) ICoreWebView2NewWindowRequestedEventArgs_put_Handled(args, TRUE);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE permission_invoke(
    ICoreWebView2PermissionRequestedEventHandler* self,
    ICoreWebView2* sender,
    ICoreWebView2PermissionRequestedEventArgs* args)
{
    (void)self; (void)sender;
    if( args ) ICoreWebView2PermissionRequestedEventArgs_put_State(
        args, COREWEBVIEW2_PERMISSION_STATE_DENY);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE web_message_invoke(
    ICoreWebView2WebMessageReceivedEventHandler* self,
    ICoreWebView2* sender,
    ICoreWebView2WebMessageReceivedEventArgs* args)
{
    struct PlatformWin32Browser* s = WV2_FROM(web_message_handler, self);
    LPWSTR message = NULL;
    LPWSTR source = NULL;
    (void)sender;
    if( args && SUCCEEDED(
            ICoreWebView2WebMessageReceivedEventArgs_get_Source(args, &source)) &&
        source && wide_starts_ci(source, s->allowed_root) &&
        SUCCEEDED(ICoreWebView2WebMessageReceivedEventArgs_TryGetWebMessageAsString(
            args, &message)) )
    {
        outbound_push(s, message);
        CoTaskMemFree(message);
    }
    CoTaskMemFree(source);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE resource_invoke(
    ICoreWebView2WebResourceRequestedEventHandler* self,
    ICoreWebView2* sender,
    ICoreWebView2WebResourceRequestedEventArgs* args)
{
    struct PlatformWin32Browser* s = WV2_FROM(resource_handler, self);
    ICoreWebView2WebResourceRequest* request = NULL;
    ICoreWebView2WebResourceResponse* response = NULL;
    IStream* empty = NULL;
    LPWSTR uri = NULL;
    (void)sender;
    if( !args || FAILED(ICoreWebView2WebResourceRequestedEventArgs_get_Request(args, &request)) ||
        !request ) return S_OK;
    if( SUCCEEDED(ICoreWebView2WebResourceRequest_get_Uri(request, &uri)) &&
        !allowed_resource_uri(s, uri) &&
        SUCCEEDED(CreateStreamOnHGlobal(NULL, TRUE, &empty)) && empty &&
        SUCCEEDED(ICoreWebView2Environment_CreateWebResourceResponse(
            s->environment, empty, 403, L"Blocked", L"Content-Type: text/plain", &response)) &&
        response )
        ICoreWebView2WebResourceRequestedEventArgs_put_Response(args, response);
    CoTaskMemFree(uri);
    if( response ) ICoreWebView2WebResourceResponse_Release(response);
    if( empty ) IStream_Release(empty);
    ICoreWebView2WebResourceRequest_Release(request);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE download_invoke(
    ICoreWebView2DownloadStartingEventHandler* self,
    ICoreWebView2* sender,
    ICoreWebView2DownloadStartingEventArgs* args)
{
    (void)self; (void)sender;
    if( args ) ICoreWebView2DownloadStartingEventArgs_put_Cancel(args, TRUE);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE execute_invoke(
    ICoreWebView2ExecuteScriptCompletedHandler* self,
    HRESULT error,
    LPCWSTR result_json)
{
    struct PlatformWin32Browser* s = WV2_FROM(execute_handler, self);
    /* ExecuteScript's immediate S_OK means only that work was queued. The
     * boolean result proves the runtime existed and accepted the envelope. */
    if( !s->closing &&
        (FAILED(error) || !result_json || wcscmp(result_json, L"true") != 0) )
        s->send_failed = 1;
    return S_OK;
}

static ICoreWebView2NavigationStartingEventHandlerVtbl NAV_START_VTBL = {
    nav_start_qi, nav_start_addref, nav_start_release, nav_start_invoke
};
static ICoreWebView2NavigationCompletedEventHandlerVtbl NAV_DONE_VTBL = {
    nav_done_qi, nav_done_addref, nav_done_release, nav_done_invoke
};
static ICoreWebView2NewWindowRequestedEventHandlerVtbl NEW_WINDOW_VTBL = {
    new_window_qi, new_window_addref, new_window_release, new_window_invoke
};
static ICoreWebView2PermissionRequestedEventHandlerVtbl PERMISSION_VTBL = {
    permission_qi, permission_addref, permission_release, permission_invoke
};
static ICoreWebView2WebMessageReceivedEventHandlerVtbl WEB_MESSAGE_VTBL = {
    web_message_qi, web_message_addref, web_message_release, web_message_invoke
};
static ICoreWebView2WebResourceRequestedEventHandlerVtbl RESOURCE_VTBL = {
    resource_qi, resource_addref, resource_release, resource_invoke
};
static ICoreWebView2DownloadStartingEventHandlerVtbl DOWNLOAD_VTBL = {
    download_qi, download_addref, download_release, download_invoke
};
static ICoreWebView2ExecuteScriptCompletedHandlerVtbl EXECUTE_VTBL = {
    execute_qi, execute_addref, execute_release, execute_invoke
};

#if defined(TORIRS_WIN32_BROWSER_CAPTURE_API)
static int capture_write_png(struct PlatformWin32Browser* s)
{
    STATSTG stat;
    HGLOBAL global = NULL;
    void* bytes = NULL;
    HANDLE file = INVALID_HANDLE_VALUE;
    char temporary[MAX_PATH];
    DWORD wrote = 0;
    DWORD length;
    int ok = 0;

    if( !s || !s->capture_stream || !s->capture_path[0] ||
        strlen(s->capture_path) + 5 >= sizeof(temporary) ||
        FAILED(IStream_Stat(s->capture_stream, &stat, STATFLAG_NONAME)) ||
        stat.cbSize.QuadPart <= 0 || stat.cbSize.QuadPart > UINT32_MAX ||
        FAILED(GetHGlobalFromStream(s->capture_stream, &global)) || !global )
        return 0;
    length = (DWORD)stat.cbSize.QuadPart;
    bytes = GlobalLock(global);
    if( !bytes ) return 0;
    snprintf(temporary, sizeof(temporary), "%s.tmp", s->capture_path);
    file = CreateFileA(
        temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY, NULL);
    if( file != INVALID_HANDLE_VALUE &&
        WriteFile(file, bytes, length, &wrote, NULL) && wrote == length &&
        FlushFileBuffers(file) )
    {
        CloseHandle(file);
        file = INVALID_HANDLE_VALUE;
        ok = MoveFileExA(
            temporary, s->capture_path,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    }
    if( file != INVALID_HANDLE_VALUE ) CloseHandle(file);
    if( !ok ) DeleteFileA(temporary);
    GlobalUnlock(global);
    return ok;
}

static HRESULT STDMETHODCALLTYPE capture_invoke(
    ICoreWebView2CapturePreviewCompletedHandler* self, HRESULT error)
{
    struct PlatformWin32Browser* s = WV2_FROM(capture_handler, self);
    int const ok = !s->closing && SUCCEEDED(error) && capture_write_png(s);
    if( s->capture_stream ) IStream_Release(s->capture_stream);
    s->capture_stream = NULL;
    s->capture_pending = 0;
    s->capture_result = ok ? 1 : -1;
    if( !ok && !s->closing )
        wv2_fail(s, "preview capture", error);
    return S_OK;
}

static ICoreWebView2CapturePreviewCompletedHandlerVtbl CAPTURE_VTBL = {
    capture_qi, capture_addref, capture_release, capture_invoke
};
#endif

static void set_bounds(struct PlatformWin32Browser* s)
{
    RECT bounds = { 0, 0, s->width, s->height };
    if( s->controller )
        ICoreWebView2Controller_put_Bounds(s->controller, bounds);
}

static HRESULT STDMETHODCALLTYPE controller_invoke(
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* self,
    HRESULT error,
    ICoreWebView2Controller* controller)
{
    struct PlatformWin32Browser* s = WV2_FROM(controller_handler, self);
    ICoreWebView2Settings* settings = NULL;
    if( FAILED(error) || !controller || s->closing )
    {
        if( !s->closing ) wv2_fail(s, "controller creation", error);
        return S_OK;
    }
    s->controller = controller;
    ICoreWebView2Controller_AddRef(controller);
    if( FAILED(ICoreWebView2Controller_get_CoreWebView2(controller, &s->core)) || !s->core )
    { wv2_fail(s, "controller core", E_FAIL); return S_OK; }
    set_bounds(s);
    ICoreWebView2Controller_put_IsVisible(controller, TRUE);

    if( SUCCEEDED(ICoreWebView2_get_Settings(s->core, &settings)) && settings )
    {
        ICoreWebView2Settings_put_IsScriptEnabled(settings, TRUE);
        ICoreWebView2Settings_put_IsWebMessageEnabled(settings, TRUE);
        ICoreWebView2Settings_put_AreDefaultScriptDialogsEnabled(settings, FALSE);
        ICoreWebView2Settings_put_IsStatusBarEnabled(settings, FALSE);
        ICoreWebView2Settings_put_AreDevToolsEnabled(settings, FALSE);
        ICoreWebView2Settings_put_AreDefaultContextMenusEnabled(settings, FALSE);
        ICoreWebView2Settings_put_AreHostObjectsAllowed(settings, FALSE);
        ICoreWebView2Settings_put_IsZoomControlEnabled(settings, FALSE);
        ICoreWebView2Settings_put_IsBuiltInErrorPageEnabled(settings, FALSE);
        ICoreWebView2Settings_Release(settings);
    }
    if( FAILED(ICoreWebView2_add_NavigationStarting(
            s->core, &s->nav_start_handler, &s->nav_start_token)) ||
        FAILED(ICoreWebView2_add_NavigationCompleted(
            s->core, &s->nav_done_handler, &s->nav_done_token)) ||
        FAILED(ICoreWebView2_add_NewWindowRequested(
            s->core, &s->new_window_handler, &s->new_window_token)) ||
        FAILED(ICoreWebView2_add_PermissionRequested(
            s->core, &s->permission_handler, &s->permission_token)) ||
        FAILED(ICoreWebView2_add_WebMessageReceived(
            s->core, &s->web_message_handler, &s->web_message_token)) ||
        FAILED(ICoreWebView2_AddWebResourceRequestedFilter(
            s->core, L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL)) ||
        FAILED(ICoreWebView2_add_WebResourceRequested(
            s->core, &s->resource_handler, &s->resource_token)) ||
        FAILED(ICoreWebView2_QueryInterface(
            s->core, &IID_ICoreWebView2_4, (void**)&s->core4)) || !s->core4 ||
        FAILED(ICoreWebView2_4_add_DownloadStarting(
            s->core4, &s->download_handler, &s->download_token)) )
    {
        wv2_fail(s, "security event registration", E_FAIL);
        return S_OK;
    }
    if( FAILED(ICoreWebView2_AddScriptToExecuteOnDocumentCreated(
            s->core, WV2_BRIDGE_SCRIPT, NULL)) )
    {
        wv2_fail(s, "document bridge registration", E_FAIL);
        return S_OK;
    }
    ICoreWebView2_Navigate(s->core, s->page_url);
    return S_OK;
}

static ICoreWebView2CreateCoreWebView2ControllerCompletedHandlerVtbl CONTROLLER_VTBL = {
    ctl_qi, ctl_addref, ctl_release, controller_invoke
};

static HRESULT STDMETHODCALLTYPE environment_invoke(
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* self,
    HRESULT error,
    ICoreWebView2Environment* environment)
{
    struct PlatformWin32Browser* s = WV2_FROM(environment_handler, self);
    if( FAILED(error) || !environment || s->closing )
    {
        if( !s->closing ) wv2_fail(s, "environment creation", error);
        return S_OK;
    }
    s->environment = environment;
    ICoreWebView2Environment_AddRef(environment);
    if( FAILED(ICoreWebView2Environment_CreateCoreWebView2Controller(
            environment, s->parent, &s->controller_handler)) )
        wv2_fail(s, "controller request", E_FAIL);
    return S_OK;
}

static ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandlerVtbl ENVIRONMENT_VTBL = {
    env_qi, env_addref, env_release, environment_invoke
};

static int path_to_file_url(WCHAR const* path, WCHAR* out, int capacity)
{
    int at = 0;
    static WCHAR const prefix[] = L"file:///";
    if( !path || !path[0] || capacity < 16 ) return 0;
    for( int i = 0; prefix[i] && at < capacity - 1; i++ ) out[at++] = prefix[i];
    for( int i = 0; path[i] && at < capacity - 4; i++ )
    {
        WCHAR c = path[i] == L'\\' ? L'/' : path[i];
        if( c == L' ' ) { out[at++] = L'%'; out[at++] = L'2'; out[at++] = L'0'; }
        else out[at++] = c;
    }
    out[at] = 0;
    return 1;
}

static HMODULE load_loader(void)
{
    WCHAR path[2048];
    WCHAR* slash;
    DWORD n = GetEnvironmentVariableW(L"TORIRS_WEBVIEW2_LOADER", path, 2048);
    if( n > 0 && n < 2048 )
        return LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    n = GetModuleFileNameW(NULL, path, 2048);
    if( n == 0 || n >= 2048 ) return NULL;
    slash = wcsrchr(path, L'\\');
    if( !slash ) return NULL;
    lstrcpyW(slash + 1, L"WebView2Loader.dll");
    return LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
}

struct PlatformWin32Browser*
PlatformWin32Browser_New(HWND parent, WCHAR const* bundle_root)
{
    struct PlatformWin32Browser* s;
    WCHAR root[2048];
    WCHAR page[2048];
    WCHAR user_data[2048];
    CreateEnvironmentFn create_environment;
    HRESULT hr;
    if( !parent || !bundle_root || !bundle_root[0] ) return NULL;
    lstrcpynW(root, bundle_root, 2048);
    if( wcslen(root) + 20 >= 2048 ) return NULL;
    lstrcpyW(page, root);
    if( page[wcslen(page) - 1] != L'\\' ) lstrcatW(page, L"\\");
    lstrcatW(page, L"modern.html");
    if( GetFileAttributesW(page) == INVALID_FILE_ATTRIBUTES ) return NULL;

    s = (struct PlatformWin32Browser*)calloc(1, sizeof(*s));
    if( !s ) return NULL;
    s->parent = parent;
    s->refs = 1;
    s->environment_handler.lpVtbl = &ENVIRONMENT_VTBL;
    s->controller_handler.lpVtbl = &CONTROLLER_VTBL;
    s->nav_start_handler.lpVtbl = &NAV_START_VTBL;
    s->nav_done_handler.lpVtbl = &NAV_DONE_VTBL;
    s->new_window_handler.lpVtbl = &NEW_WINDOW_VTBL;
    s->permission_handler.lpVtbl = &PERMISSION_VTBL;
    s->web_message_handler.lpVtbl = &WEB_MESSAGE_VTBL;
    s->resource_handler.lpVtbl = &RESOURCE_VTBL;
    s->download_handler.lpVtbl = &DOWNLOAD_VTBL;
    s->execute_handler.lpVtbl = &EXECUTE_VTBL;
#if defined(TORIRS_WIN32_BROWSER_CAPTURE_API)
    s->capture_handler.lpVtbl = &CAPTURE_VTBL;
#endif
    path_to_file_url(root, s->allowed_root, 2048);
    if( s->allowed_root[wcslen(s->allowed_root) - 1] != L'/' )
        lstrcatW(s->allowed_root, L"/");
    path_to_file_url(page, s->page_url, 2048);

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if( SUCCEEDED(hr) ) s->com_initialized = 1;
    else if( hr != RPC_E_CHANGED_MODE ) goto fail;
    s->loader = load_loader();
    if( !s->loader ) goto fail;
    create_environment = (CreateEnvironmentFn)(void*)GetProcAddress(
        s->loader, "CreateCoreWebView2EnvironmentWithOptions");
    if( !create_environment ) goto fail;
    if( !GetTempPathW(1900, user_data) ) goto fail;
    lstrcatW(user_data, L"ToriRS-WebView2");
    CreateDirectoryW(user_data, NULL);
    hr = create_environment(NULL, user_data, NULL, &s->environment_handler);
    if( FAILED(hr) ) goto fail;
    return s;

fail:
    if( s ) wv2_fail(s, "bootstrap", hr);
    PlatformWin32Browser_Free(s);
    return NULL;
}

void
PlatformWin32Browser_Free(struct PlatformWin32Browser* s)
{
    if( !s ) return;
    s->closing = 1;
    for( int i = 0; i < s->inbound_count; i++ ) free(s->inbound[i]);
    for( int i = 0; i < s->outbound_count; i++ ) free(s->outbound[i]);
    if( s->core4 && s->download_token.value )
        ICoreWebView2_4_remove_DownloadStarting(s->core4, s->download_token);
    if( s->core )
    {
        if( s->nav_start_token.value ) ICoreWebView2_remove_NavigationStarting(s->core, s->nav_start_token);
        if( s->nav_done_token.value ) ICoreWebView2_remove_NavigationCompleted(s->core, s->nav_done_token);
        if( s->new_window_token.value ) ICoreWebView2_remove_NewWindowRequested(s->core, s->new_window_token);
        if( s->permission_token.value ) ICoreWebView2_remove_PermissionRequested(s->core, s->permission_token);
        if( s->web_message_token.value ) ICoreWebView2_remove_WebMessageReceived(s->core, s->web_message_token);
        if( s->resource_token.value ) ICoreWebView2_remove_WebResourceRequested(s->core, s->resource_token);
    }
    if( s->controller ) ICoreWebView2Controller_Close(s->controller);
    if( s->core4 ) ICoreWebView2_4_Release(s->core4);
    if( s->core ) ICoreWebView2_Release(s->core);
    if( s->controller ) ICoreWebView2Controller_Release(s->controller);
    if( s->environment ) ICoreWebView2Environment_Release(s->environment);
    s->core4 = NULL;
    s->core = NULL;
    s->controller = NULL;
    s->environment = NULL;

    /* Drop only PlatformWindow's owner reference. Environment/controller
     * creation may still own a callback reference; that is the final arbiter
     * of the allocation's lifetime. */
    wv2_release(s);
}

void
PlatformWin32Browser_Resize(struct PlatformWin32Browser* s, int width, int height)
{
    if( !s || width < 0 || height < 0 ) return;
    s->width = width;
    s->height = height;
    set_bounds(s);
}

int
PlatformWin32Browser_Send(struct PlatformWin32Browser* s, char const* json)
{
    char* copy;
    if( !s || !json || !json[0] || strlen(json) > WV2_JSON_MAX ) return 0;
    if( s->ready ) return execute_json(s, json);
    if( s->inbound_count >= WV2_INBOUND_MAX ) return 0;
    copy = (char*)malloc(strlen(json) + 1);
    if( !copy ) return 0;
    strcpy(copy, json);
    s->inbound[s->inbound_count++] = copy;
    return 1;
}

int
PlatformWin32Browser_TakeSendFailure(struct PlatformWin32Browser* s)
{
    int failed = s ? s->send_failed : 0;
    if( s ) s->send_failed = 0;
    return failed;
}

int
PlatformWin32Browser_Poll(
    struct PlatformWin32Browser* s, char* out_json, int capacity)
{
    char* message;
    int length;
    if( !s || !out_json || capacity <= 0 || s->outbound_count <= 0 ) return 0;
    message = s->outbound[0];
    for( int i = 1; i < s->outbound_count; i++ )
        s->outbound[i - 1] = s->outbound[i];
    s->outbound_count--;
    length = (int)strlen(message);
    if( length >= capacity ) length = capacity - 1;
    memcpy(out_json, message, (size_t)length);
    out_json[length] = 0;
    free(message);
    return length;
}

int PlatformWin32Browser_Ready(struct PlatformWin32Browser const* s)
{ return s && s->ready; }
int PlatformWin32Browser_Failed(struct PlatformWin32Browser const* s)
{ return !s || s->failed; }
int PlatformWin32Browser_PreTranslateMessage(
    struct PlatformWin32Browser* s, MSG* message)
{ (void)s; (void)message; return 0; }
#if defined(TORIRS_WIN32_BROWSER_CAPTURE_API)
int PlatformWin32Browser_CapturePng(
    struct PlatformWin32Browser* s, char const* path)
{
    HRESULT hr;
    if( !s || !s->ready || !s->core || !path || !path[0] ||
        strlen(path) >= sizeof(s->capture_path) || s->capture_pending )
        return 0;
    if( FAILED(CreateStreamOnHGlobal(NULL, TRUE, &s->capture_stream)) ||
        !s->capture_stream )
        return 0;
    strcpy(s->capture_path, path);
    s->capture_pending = 1;
    s->capture_result = 0;
    hr = ICoreWebView2_CapturePreview(
        s->core, COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG,
        s->capture_stream, &s->capture_handler);
    if( FAILED(hr) )
    {
        IStream_Release(s->capture_stream);
        s->capture_stream = NULL;
        s->capture_pending = 0;
        s->capture_result = -1;
        wv2_fail(s, "preview request", hr);
        return 0;
    }
    return 1;
}

int PlatformWin32Browser_CaptureStatus(
    struct PlatformWin32Browser const* s)
{
    return s ? s->capture_result : -1;
}
#endif
void PlatformWin32Browser_AllowLocalRoot(
    struct PlatformWin32Browser* s, WCHAR const* root)
{ if( s ) lstrcpynW(s->allowed_asset_root, root ? root : L"", 2048); }

#else /* no WebView2 SDK header: keep ordinary builds linkable, fail explicitly */

#include <stdlib.h>

struct PlatformWin32Browser { int failed; };
struct PlatformWin32Browser* PlatformWin32Browser_New(
    HWND parent, WCHAR const* bundle_root)
{ (void)parent; (void)bundle_root; return NULL; }
void PlatformWin32Browser_Free(struct PlatformWin32Browser* browser)
{ free(browser); }
void PlatformWin32Browser_Resize(struct PlatformWin32Browser* b, int w, int h)
{ (void)b; (void)w; (void)h; }
int PlatformWin32Browser_Send(struct PlatformWin32Browser* b, char const* json)
{ (void)b; (void)json; return 0; }
int PlatformWin32Browser_TakeSendFailure(struct PlatformWin32Browser* b)
{ (void)b; return 0; }
int PlatformWin32Browser_Poll(struct PlatformWin32Browser* b, char* out, int cap)
{ (void)b; (void)out; (void)cap; return 0; }
int PlatformWin32Browser_Ready(struct PlatformWin32Browser const* b)
{ (void)b; return 0; }
int PlatformWin32Browser_Failed(struct PlatformWin32Browser const* b)
{ (void)b; return 1; }
int PlatformWin32Browser_PreTranslateMessage(
    struct PlatformWin32Browser* b, MSG* message)
{ (void)b; (void)message; return 0; }
#if defined(TORIRS_WIN32_BROWSER_CAPTURE_API)
int PlatformWin32Browser_CapturePng(
    struct PlatformWin32Browser* b, char const* path)
{ (void)b; (void)path; return 0; }
int PlatformWin32Browser_CaptureStatus(struct PlatformWin32Browser const* b)
{ (void)b; return -1; }
#endif
void PlatformWin32Browser_AllowLocalRoot(
    struct PlatformWin32Browser* b, WCHAR const* root)
{ (void)b; (void)root; }

#endif
