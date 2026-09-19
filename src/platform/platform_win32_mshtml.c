/*
 * XP browser backend: the system IWebBrowser2/MSHTML control, hosted in the
 * application's existing plugin-chrome child HWND. No ATL, no comctl32, no
 * extra top-level window, and no browser files shipped with the executable.
 */

#define COBJMACROS
#include "platform/platform_win32_browser_backend.h"

#include <exdisp.h>
#include <mshtml.h>
#include <mshtmhst.h>
#include <ocidl.h>
#include <ole2.h>
#include <urlmon.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef DISPID_AMBIENT_DLCONTROL
#  define DISPID_AMBIENT_DLCONTROL (-5512)
#endif
#ifndef DLCTL_NO_JAVA
#  define DLCTL_NO_JAVA 0x00000100
#endif
#ifndef DLCTL_NO_DLACTIVEXCTLS
#  define DLCTL_NO_DLACTIVEXCTLS 0x00000400
#endif
#ifndef DLCTL_NO_RUNACTIVEXCTLS
#  define DLCTL_NO_RUNACTIVEXCTLS 0x00000200
#endif
#ifndef DLCTL_FORCEOFFLINE
#  define DLCTL_FORCEOFFLINE 0x10000000
#endif
#ifndef DLCTL_SILENT
#  define DLCTL_SILENT 0x40000000
#endif
#ifndef DISPID_BEFORENAVIGATE2
#  define DISPID_BEFORENAVIGATE2 250
#  define DISPID_NEWWINDOW2 251
#  define DISPID_DOCUMENTCOMPLETE 259
#  define DISPID_FILEDOWNLOAD 270
#  define DISPID_NEWWINDOW3 273
#endif

#define MSHTML_INBOUND_MAX 128
#define MSHTML_OUTBOUND_MAX 64
#define MSHTML_JSON_MAX (8 * 1024 * 1024)

struct PlatformWin32Browser
{
    HWND parent;
    IOleClientSite client_site;
    IOleInPlaceSite inplace_site;
    IOleInPlaceFrame inplace_frame;
    IDocHostUIHandler ui_handler;
    IDispatch ambient_dispatch;
    IDispatch external_dispatch;
    IDispatch event_dispatch;
    LONG refs;

    IOleObject* ole;
    IOleInPlaceObject* inplace_object;
    IOleInPlaceActiveObject* active_object;
    IWebBrowser2* browser;
    IConnectionPoint* events;
    DWORD event_cookie;
    int com_initialized;
    int ready;
    int failed;
    int send_failed;
    int ready_probe_count;
    int width;
    int height;

    WCHAR page_url[2048];
    WCHAR page_path[2048];
    WCHAR allowed_root[2048];
    WCHAR allowed_asset_root[2048];
    char* inbound[MSHTML_INBOUND_MAX];
    int inbound_count;
    char* outbound[MSHTML_OUTBOUND_MAX];
    int outbound_count;
};

static struct PlatformWin32Browser* from_client(IOleClientSite* p)
{ return CONTAINING_RECORD(p, struct PlatformWin32Browser, client_site); }
static struct PlatformWin32Browser* from_site(IOleInPlaceSite* p)
{ return CONTAINING_RECORD(p, struct PlatformWin32Browser, inplace_site); }
static struct PlatformWin32Browser* from_frame(IOleInPlaceFrame* p)
{ return CONTAINING_RECORD(p, struct PlatformWin32Browser, inplace_frame); }
static struct PlatformWin32Browser* from_ui(IDocHostUIHandler* p)
{ return CONTAINING_RECORD(p, struct PlatformWin32Browser, ui_handler); }
static struct PlatformWin32Browser* from_ambient(IDispatch* p)
{ return CONTAINING_RECORD(p, struct PlatformWin32Browser, ambient_dispatch); }
static struct PlatformWin32Browser* from_external(IDispatch* p)
{ return CONTAINING_RECORD(p, struct PlatformWin32Browser, external_dispatch); }
static struct PlatformWin32Browser* from_events(IDispatch* p)
{ return CONTAINING_RECORD(p, struct PlatformWin32Browser, event_dispatch); }

static ULONG browser_addref(struct PlatformWin32Browser* s)
{
    return (ULONG)InterlockedIncrement(&s->refs);
}

static ULONG browser_release(struct PlatformWin32Browser* s)
{
    LONG value = InterlockedDecrement(&s->refs);
    /* The PlatformWindow owns the allocation. COM references may reach zero
     * between callbacks; freeing here would race that explicit lifetime. */
    if( value < 1 )
    {
        InterlockedIncrement(&s->refs);
        value = 1;
    }
    return (ULONG)value;
}

static HRESULT browser_site_qi(
    struct PlatformWin32Browser* s, REFIID iid, void** out)
{
    if( !out )
        return E_POINTER;
    *out = NULL;
    if( IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IOleClientSite) )
        *out = &s->client_site;
    else if( IsEqualIID(iid, &IID_IOleWindow) ||
             IsEqualIID(iid, &IID_IOleInPlaceSite) )
        *out = &s->inplace_site;
    else if( IsEqualIID(iid, &IID_IOleInPlaceUIWindow) ||
             IsEqualIID(iid, &IID_IOleInPlaceFrame) )
        *out = &s->inplace_frame;
    else if( IsEqualIID(iid, &IID_IDocHostUIHandler) )
        *out = &s->ui_handler;
    else if( IsEqualIID(iid, &IID_IDispatch) )
        *out = &s->ambient_dispatch;
    if( !*out )
        return E_NOINTERFACE;
    browser_addref(s);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE client_qi(IOleClientSite* p, REFIID iid, void** out)
{ return browser_site_qi(from_client(p), iid, out); }
static ULONG STDMETHODCALLTYPE client_addref(IOleClientSite* p)
{ return browser_addref(from_client(p)); }
static ULONG STDMETHODCALLTYPE client_release(IOleClientSite* p)
{ return browser_release(from_client(p)); }
static HRESULT STDMETHODCALLTYPE client_save(IOleClientSite* p)
{ (void)p; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE client_moniker(
    IOleClientSite* p, DWORD assign, DWORD which, IMoniker** out)
{ (void)p; (void)assign; (void)which; if( out ) *out = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE client_container(
    IOleClientSite* p, IOleContainer** out)
{ (void)p; if( !out ) return E_POINTER; *out = NULL; return E_NOINTERFACE; }
static HRESULT STDMETHODCALLTYPE client_show(IOleClientSite* p)
{ (void)p; return S_OK; }
static HRESULT STDMETHODCALLTYPE client_on_show(IOleClientSite* p, WINBOOL show)
{ (void)p; (void)show; return S_OK; }
static HRESULT STDMETHODCALLTYPE client_layout(IOleClientSite* p)
{ (void)p; return E_NOTIMPL; }

static IOleClientSiteVtbl CLIENT_VTBL = {
    client_qi, client_addref, client_release, client_save, client_moniker,
    client_container, client_show, client_on_show, client_layout
};

static HRESULT STDMETHODCALLTYPE site_qi(IOleInPlaceSite* p, REFIID iid, void** out)
{ return browser_site_qi(from_site(p), iid, out); }
static ULONG STDMETHODCALLTYPE site_addref(IOleInPlaceSite* p)
{ return browser_addref(from_site(p)); }
static ULONG STDMETHODCALLTYPE site_release(IOleInPlaceSite* p)
{ return browser_release(from_site(p)); }
static HRESULT STDMETHODCALLTYPE site_window(IOleInPlaceSite* p, HWND* out)
{ if( !out ) return E_POINTER; *out = from_site(p)->parent; return S_OK; }
static HRESULT STDMETHODCALLTYPE site_help(IOleInPlaceSite* p, WINBOOL enter)
{ (void)p; (void)enter; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE site_can_activate(IOleInPlaceSite* p)
{ (void)p; return S_OK; }
static HRESULT STDMETHODCALLTYPE site_activate(IOleInPlaceSite* p)
{ (void)p; return S_OK; }
static HRESULT STDMETHODCALLTYPE site_ui_activate(IOleInPlaceSite* p)
{ (void)p; return S_OK; }
static HRESULT STDMETHODCALLTYPE site_context(
    IOleInPlaceSite* p,
    IOleInPlaceFrame** frame,
    IOleInPlaceUIWindow** doc,
    LPRECT pos,
    LPRECT clip,
    LPOLEINPLACEFRAMEINFO info)
{
    struct PlatformWin32Browser* s = from_site(p);
    RECT rect;
    if( !frame || !doc || !pos || !clip || !info )
        return E_POINTER;
    GetClientRect(s->parent, &rect);
    *pos = rect;
    *clip = rect;
    *frame = &s->inplace_frame;
    IOleInPlaceFrame_AddRef(*frame);
    *doc = NULL;
    memset(info, 0, sizeof(*info));
    info->cb = sizeof(*info);
    info->fMDIApp = FALSE;
    info->hwndFrame = s->parent;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE site_scroll(IOleInPlaceSite* p, SIZE amount)
{ (void)p; (void)amount; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE site_ui_deactivate(IOleInPlaceSite* p, WINBOOL undo)
{ (void)p; (void)undo; return S_OK; }
static HRESULT STDMETHODCALLTYPE site_deactivate(IOleInPlaceSite* p)
{ (void)p; return S_OK; }
static HRESULT STDMETHODCALLTYPE site_discard(IOleInPlaceSite* p)
{ (void)p; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE site_undo(IOleInPlaceSite* p)
{ (void)p; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE site_pos(IOleInPlaceSite* p, LPCRECT rect)
{
    struct PlatformWin32Browser* s = from_site(p);
    if( s->inplace_object && rect )
        IOleInPlaceObject_SetObjectRects(s->inplace_object, rect, rect);
    return S_OK;
}

static IOleInPlaceSiteVtbl SITE_VTBL = {
    site_qi, site_addref, site_release, site_window, site_help,
    site_can_activate, site_activate, site_ui_activate, site_context,
    site_scroll, site_ui_deactivate, site_deactivate, site_discard,
    site_undo, site_pos
};

static HRESULT STDMETHODCALLTYPE frame_qi(IOleInPlaceFrame* p, REFIID iid, void** out)
{ return browser_site_qi(from_frame(p), iid, out); }
static ULONG STDMETHODCALLTYPE frame_addref(IOleInPlaceFrame* p)
{ return browser_addref(from_frame(p)); }
static ULONG STDMETHODCALLTYPE frame_release(IOleInPlaceFrame* p)
{ return browser_release(from_frame(p)); }
static HRESULT STDMETHODCALLTYPE frame_window(IOleInPlaceFrame* p, HWND* out)
{ if( !out ) return E_POINTER; *out = from_frame(p)->parent; return S_OK; }
static HRESULT STDMETHODCALLTYPE frame_help(IOleInPlaceFrame* p, WINBOOL enter)
{ (void)p; (void)enter; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE frame_border(IOleInPlaceFrame* p, LPRECT rect)
{ (void)p; (void)rect; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE frame_request(IOleInPlaceFrame* p, LPCBORDERWIDTHS widths)
{ (void)p; (void)widths; return INPLACE_E_NOTOOLSPACE; }
static HRESULT STDMETHODCALLTYPE frame_set_border(IOleInPlaceFrame* p, LPCBORDERWIDTHS widths)
{ (void)p; (void)widths; return S_OK; }
static HRESULT STDMETHODCALLTYPE frame_active(
    IOleInPlaceFrame* p, IOleInPlaceActiveObject* active, LPCOLESTR name)
{ (void)p; (void)active; (void)name; return S_OK; }
static HRESULT STDMETHODCALLTYPE frame_insert(
    IOleInPlaceFrame* p, HMENU menu, LPOLEMENUGROUPWIDTHS widths)
{ (void)p; (void)menu; (void)widths; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE frame_menu(
    IOleInPlaceFrame* p, HMENU menu, HOLEMENU hole, HWND active)
{ (void)p; (void)menu; (void)hole; (void)active; return S_OK; }
static HRESULT STDMETHODCALLTYPE frame_remove(IOleInPlaceFrame* p, HMENU menu)
{ (void)p; (void)menu; return S_OK; }
static HRESULT STDMETHODCALLTYPE frame_status(IOleInPlaceFrame* p, LPCOLESTR text)
{ (void)p; (void)text; return S_OK; }
static HRESULT STDMETHODCALLTYPE frame_modeless(IOleInPlaceFrame* p, WINBOOL enable)
{ (void)p; (void)enable; return S_OK; }
static HRESULT STDMETHODCALLTYPE frame_key(IOleInPlaceFrame* p, LPMSG msg, WORD id)
{ (void)p; (void)msg; (void)id; return S_FALSE; }

static IOleInPlaceFrameVtbl FRAME_VTBL = {
    frame_qi, frame_addref, frame_release, frame_window, frame_help,
    frame_border, frame_request, frame_set_border, frame_active, frame_insert,
    frame_menu, frame_remove, frame_status, frame_modeless, frame_key
};

static HRESULT STDMETHODCALLTYPE ui_qi(IDocHostUIHandler* p, REFIID iid, void** out)
{ return browser_site_qi(from_ui(p), iid, out); }
static ULONG STDMETHODCALLTYPE ui_addref(IDocHostUIHandler* p)
{ return browser_addref(from_ui(p)); }
static ULONG STDMETHODCALLTYPE ui_release(IDocHostUIHandler* p)
{ return browser_release(from_ui(p)); }
static HRESULT STDMETHODCALLTYPE ui_context(
    IDocHostUIHandler* p, DWORD id, POINT* point, IUnknown* command, IDispatch* dispatch)
{ (void)p; (void)id; (void)point; (void)command; (void)dispatch; return S_OK; }
static HRESULT STDMETHODCALLTYPE ui_info(IDocHostUIHandler* p, DOCHOSTUIINFO* info)
{
    (void)p;
    if( !info ) return E_POINTER;
    info->cbSize = sizeof(*info);
    info->dwFlags = DOCHOSTUIFLAG_NO3DBORDER | DOCHOSTUIFLAG_DISABLE_HELP_MENU |
                    DOCHOSTUIFLAG_DIALOG;
    info->dwDoubleClick = DOCHOSTUIDBLCLK_DEFAULT;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ui_show(
    IDocHostUIHandler* p, DWORD id, IOleInPlaceActiveObject* active,
    IOleCommandTarget* command, IOleInPlaceFrame* frame, IOleInPlaceUIWindow* doc)
{ (void)p; (void)id; (void)active; (void)command; (void)frame; (void)doc; return S_FALSE; }
static HRESULT STDMETHODCALLTYPE ui_hide(IDocHostUIHandler* p)
{ (void)p; return S_OK; }
static HRESULT STDMETHODCALLTYPE ui_update(IDocHostUIHandler* p)
{ (void)p; return S_OK; }
static HRESULT STDMETHODCALLTYPE ui_modeless(IDocHostUIHandler* p, WINBOOL enable)
{ (void)p; (void)enable; return S_OK; }
static HRESULT STDMETHODCALLTYPE ui_doc_activate(IDocHostUIHandler* p, WINBOOL active)
{ (void)p; (void)active; return S_OK; }
static HRESULT STDMETHODCALLTYPE ui_frame_activate(IDocHostUIHandler* p, WINBOOL active)
{ (void)p; (void)active; return S_OK; }
static HRESULT STDMETHODCALLTYPE ui_resize(
    IDocHostUIHandler* p, LPCRECT rect, IOleInPlaceUIWindow* window, WINBOOL frame)
{ (void)p; (void)rect; (void)window; (void)frame; return S_OK; }
static HRESULT STDMETHODCALLTYPE ui_key(
    IDocHostUIHandler* p, LPMSG msg, const GUID* group, DWORD id)
{ (void)p; (void)msg; (void)group; (void)id; return S_FALSE; }
static HRESULT STDMETHODCALLTYPE ui_option(IDocHostUIHandler* p, LPOLESTR* key, DWORD value)
{ (void)p; (void)value; if( key ) *key = NULL; return S_FALSE; }
static HRESULT STDMETHODCALLTYPE ui_drop(
    IDocHostUIHandler* p, IDropTarget* original, IDropTarget** out)
{ (void)p; (void)original; if( out ) *out = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE ui_external(IDocHostUIHandler* p, IDispatch** out)
{
    struct PlatformWin32Browser* s = from_ui(p);
    if( !out ) return E_POINTER;
    *out = &s->external_dispatch;
    IDispatch_AddRef(*out);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ui_url(
    IDocHostUIHandler* p, DWORD flags, LPWSTR in, LPWSTR* out)
{ (void)p; (void)flags; (void)in; if( out ) *out = NULL; return S_FALSE; }
static HRESULT STDMETHODCALLTYPE ui_filter(
    IDocHostUIHandler* p, IDataObject* in, IDataObject** out)
{ (void)p; (void)in; if( out ) *out = NULL; return S_FALSE; }

static IDocHostUIHandlerVtbl UI_VTBL = {
    ui_qi, ui_addref, ui_release, ui_context, ui_info, ui_show, ui_hide,
    ui_update, ui_modeless, ui_doc_activate, ui_frame_activate, ui_resize,
    ui_key, ui_option, ui_drop, ui_external, ui_url, ui_filter
};

static HRESULT dispatch_type_count(UINT* out)
{ if( !out ) return E_POINTER; *out = 0; return S_OK; }
static HRESULT dispatch_type(UINT index, LCID locale, ITypeInfo** out)
{ (void)index; (void)locale; if( out ) *out = NULL; return E_NOTIMPL; }
static HRESULT dispatch_names_unknown(
    REFIID iid, LPOLESTR* names, UINT count, LCID locale, DISPID* ids)
{ (void)iid; (void)names; (void)count; (void)locale; (void)ids; return DISP_E_UNKNOWNNAME; }

static HRESULT STDMETHODCALLTYPE ambient_qi(IDispatch* p, REFIID iid, void** out)
{ return browser_site_qi(from_ambient(p), iid, out); }
static ULONG STDMETHODCALLTYPE ambient_addref(IDispatch* p)
{ return browser_addref(from_ambient(p)); }
static ULONG STDMETHODCALLTYPE ambient_release(IDispatch* p)
{ return browser_release(from_ambient(p)); }
static HRESULT STDMETHODCALLTYPE ambient_count(IDispatch* p, UINT* out)
{ (void)p; return dispatch_type_count(out); }
static HRESULT STDMETHODCALLTYPE ambient_type(
    IDispatch* p, UINT index, LCID locale, ITypeInfo** out)
{ (void)p; return dispatch_type(index, locale, out); }
static HRESULT STDMETHODCALLTYPE ambient_names(
    IDispatch* p, REFIID iid, LPOLESTR* names, UINT count, LCID locale, DISPID* ids)
{ (void)p; return dispatch_names_unknown(iid, names, count, locale, ids); }
static HRESULT STDMETHODCALLTYPE ambient_invoke(
    IDispatch* p, DISPID id, REFIID iid, LCID locale, WORD flags,
    DISPPARAMS* args, VARIANT* result, EXCEPINFO* exception, UINT* arg_error)
{
    (void)p; (void)iid; (void)locale; (void)args; (void)exception; (void)arg_error;
    if( id != DISPID_AMBIENT_DLCONTROL || !(flags & DISPATCH_PROPERTYGET) || !result )
        return DISP_E_MEMBERNOTFOUND;
    VariantInit(result);
    result->vt = VT_I4;
    result->lVal = DLCTL_NO_JAVA | DLCTL_NO_DLACTIVEXCTLS |
                   DLCTL_NO_RUNACTIVEXCTLS | DLCTL_FORCEOFFLINE | DLCTL_SILENT;
    return S_OK;
}

static IDispatchVtbl AMBIENT_VTBL = {
    ambient_qi, ambient_addref, ambient_release, ambient_count, ambient_type,
    ambient_names, ambient_invoke
};

static char* utf8_from_wide(WCHAR const* input)
{
    int bytes;
    char* out;
    if( !input ) return NULL;
    bytes = WideCharToMultiByte(CP_UTF8, 0, input, -1, NULL, 0, NULL, NULL);
    if( bytes <= 0 || bytes > MSHTML_JSON_MAX ) return NULL;
    out = (char*)malloc((size_t)bytes);
    if( !out ) return NULL;
    if( !WideCharToMultiByte(CP_UTF8, 0, input, -1, out, bytes, NULL, NULL) )
    { free(out); return NULL; }
    return out;
}

static WCHAR* wide_from_utf8(char const* input)
{
    int chars;
    WCHAR* out;
    if( !input ) return NULL;
    chars = MultiByteToWideChar(CP_UTF8, 0, input, -1, NULL, 0);
    if( chars <= 0 || chars > MSHTML_JSON_MAX ) return NULL;
    out = (WCHAR*)malloc((size_t)chars * sizeof(*out));
    if( !out ) return NULL;
    if( !MultiByteToWideChar(CP_UTF8, 0, input, -1, out, chars) )
    { free(out); return NULL; }
    return out;
}

static void outbound_push(struct PlatformWin32Browser* s, WCHAR const* message)
{
    char* copy = utf8_from_wide(message);
    if( !copy ) { s->send_failed = 1; return; }
    if( s->outbound_count >= MSHTML_OUTBOUND_MAX )
    { free(copy); s->send_failed = 1; return; }
    s->outbound[s->outbound_count++] = copy;
}

static HRESULT STDMETHODCALLTYPE external_qi(IDispatch* p, REFIID iid, void** out)
{
    struct PlatformWin32Browser* s = from_external(p);
    if( !out ) return E_POINTER;
    *out = NULL;
    if( IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IDispatch) )
        *out = &s->external_dispatch;
    if( !*out ) return E_NOINTERFACE;
    browser_addref(s);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE external_addref(IDispatch* p)
{ return browser_addref(from_external(p)); }
static ULONG STDMETHODCALLTYPE external_release(IDispatch* p)
{ return browser_release(from_external(p)); }
static HRESULT STDMETHODCALLTYPE external_count(IDispatch* p, UINT* out)
{ (void)p; return dispatch_type_count(out); }
static HRESULT STDMETHODCALLTYPE external_type(
    IDispatch* p, UINT index, LCID locale, ITypeInfo** out)
{ (void)p; return dispatch_type(index, locale, out); }
static HRESULT STDMETHODCALLTYPE external_names(
    IDispatch* p, REFIID iid, LPOLESTR* names, UINT count, LCID locale, DISPID* ids)
{
    (void)p; (void)iid; (void)locale;
    if( !names || !ids ) return E_POINTER;
    if( count == 1 && names[0] && lstrcmpiW(names[0], L"postMessage") == 0 )
    { ids[0] = 1; return S_OK; }
    return DISP_E_UNKNOWNNAME;
}
static HRESULT STDMETHODCALLTYPE external_invoke(
    IDispatch* p, DISPID id, REFIID iid, LCID locale, WORD flags,
    DISPPARAMS* args, VARIANT* result, EXCEPINFO* exception, UINT* arg_error)
{
    struct PlatformWin32Browser* s = from_external(p);
    VARIANTARG* value;
    (void)iid; (void)locale; (void)exception; (void)arg_error;
    if( id != 1 || !(flags & DISPATCH_METHOD) || !args || args->cArgs != 1 )
        return DISP_E_MEMBERNOTFOUND;
    value = &args->rgvarg[0];
    if( value->vt != VT_BSTR || !value->bstrVal )
        return DISP_E_TYPEMISMATCH;
    if( wcscmp(
            value->bstrVal,
            L"{\"protocol\":1,\"type\":\"transport.loss\"}") == 0 )
        s->send_failed = 1;
    else
        outbound_push(s, value->bstrVal);
    if( result )
    { VariantInit(result); result->vt = VT_BOOL; result->boolVal = VARIANT_TRUE; }
    return S_OK;
}

static IDispatchVtbl EXTERNAL_VTBL = {
    external_qi, external_addref, external_release, external_count,
    external_type, external_names, external_invoke
};

static WCHAR const* variant_bstr(VARIANT const* value)
{
    VARIANT const* actual = value;
    if( !value ) return NULL;
    if( (value->vt & VT_BYREF) && (value->vt & VT_TYPEMASK) == VT_VARIANT )
        actual = value->pvarVal;
    if( !actual ) return NULL;
    if( actual->vt == VT_BSTR ) return actual->bstrVal;
    if( actual->vt == (VT_BYREF | VT_BSTR) && actual->pbstrVal )
        return *actual->pbstrVal;
    return NULL;
}

static int wide_starts_ci(WCHAR const* value, WCHAR const* prefix)
{
    size_t const n = prefix ? wcslen(prefix) : 0;
    return value && prefix && n > 0 && _wcsnicmp(value, prefix, n) == 0;
}

static int local_page_url(
    struct PlatformWin32Browser const* s, WCHAR const* uri)
{
    WCHAR const* tail;
    if( !s || !uri ) return 0;
    if( _wcsicmp(uri, s->page_url) == 0 ||
        _wcsicmp(uri, s->page_path) == 0 )
        return 1;
    if( !wide_starts_ci(uri, s->allowed_root) ) return 0;
    tail = wcsrchr(uri, L'/');
    return tail && _wcsicmp(tail + 1, L"legacy-ie8.html") == 0;
}

static int navigation_allowed(
    struct PlatformWin32Browser const* s, VARIANT const* value)
{
    WCHAR const* uri = variant_bstr(value);
    if( !uri ) return 0;
    return (!s->ready && _wcsicmp(uri, L"about:blank") == 0) ||
           local_page_url(s, uri);
}

static int browser_exec(struct PlatformWin32Browser* s, char const* json);

static void inject_bridge(struct PlatformWin32Browser* s)
{
    static char const script[] =
        "window.torirsPluginChromePostMessage=function(s){"
        "try{window.external.postMessage(String(s));return true;}"
        "catch(e){return false;}};";
    if( !browser_exec(s, script) )
    {
        s->failed = 1;
        return;
    }
    s->ready = 1;
    for( int i = 0; i < s->inbound_count; i++ )
    {
        char* message = s->inbound[i];
        size_t size = strlen(message) + 320;
        char* call = (char*)malloc(size);
        if( call )
        {
            snprintf(call, size,
                "(function(){var ok=false;try{ok=!!(window.ToriRSPluginChrome&&"
                "window.ToriRSPluginChrome.receive(%s)!==false);}catch(e){}"
                "if(!ok){try{window.external.postMessage(" 
                "'{\"protocol\":1,\"type\":\"transport.loss\"}');}catch(e){}}})();",
                message);
            if( !browser_exec(s, call) )
                s->send_failed = 1;
            free(call);
        }
        else
            s->send_failed = 1;
        free(message);
        s->inbound[i] = NULL;
    }
    s->inbound_count = 0;
}

static void browser_try_ready(struct PlatformWin32Browser* s)
{
    READYSTATE state = READYSTATE_UNINITIALIZED;
    BSTR uri = NULL;
    HRESULT state_hr;
    HRESULT uri_hr;

    if( !s || s->ready || s->failed || !s->browser )
        return;
    state_hr = IWebBrowser2_get_ReadyState(s->browser, &state);
    uri_hr = IWebBrowser2_get_LocationURL(s->browser, &uri);
    s->ready_probe_count++;
    if( SUCCEEDED(state_hr) && SUCCEEDED(uri_hr) && uri )
    {
        if( state == READYSTATE_COMPLETE && local_page_url(s, uri) )
            inject_bridge(s);
        else if( getenv("TORIRS_CHROME_DEBUG") &&
                 (s->ready_probe_count == 1 || s->ready_probe_count % 100 == 0) )
            fprintf(
                stderr,
                "plugin chrome MSHTML: waiting at state %d, URL %ls\n",
                (int)state, uri);
    }
    else if( getenv("TORIRS_CHROME_DEBUG") &&
             (s->ready_probe_count == 1 || s->ready_probe_count % 100 == 0) )
        fprintf(
            stderr,
            "plugin chrome MSHTML: document probe failed "
            "(state HRESULT=0x%08lx, URL HRESULT=0x%08lx)\n",
            (unsigned long)state_hr, (unsigned long)uri_hr);
    SysFreeString(uri);
}

static HRESULT STDMETHODCALLTYPE events_qi(IDispatch* p, REFIID iid, void** out)
{
    struct PlatformWin32Browser* s = from_events(p);
    if( !out ) return E_POINTER;
    *out = NULL;
    if( IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IDispatch) ||
        IsEqualIID(iid, &DIID_DWebBrowserEvents2) )
        *out = &s->event_dispatch;
    if( !*out ) return E_NOINTERFACE;
    browser_addref(s);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE events_addref(IDispatch* p)
{ return browser_addref(from_events(p)); }
static ULONG STDMETHODCALLTYPE events_release(IDispatch* p)
{ return browser_release(from_events(p)); }
static HRESULT STDMETHODCALLTYPE events_count(IDispatch* p, UINT* out)
{ (void)p; return dispatch_type_count(out); }
static HRESULT STDMETHODCALLTYPE events_type(
    IDispatch* p, UINT index, LCID locale, ITypeInfo** out)
{ (void)p; return dispatch_type(index, locale, out); }
static HRESULT STDMETHODCALLTYPE events_names(
    IDispatch* p, REFIID iid, LPOLESTR* names, UINT count, LCID locale, DISPID* ids)
{ (void)p; return dispatch_names_unknown(iid, names, count, locale, ids); }
static void event_cancel(VARIANTARG* arg)
{
    if( arg && arg->vt == (VT_BYREF | VT_BOOL) && arg->pboolVal )
        *arg->pboolVal = VARIANT_TRUE;
}
static HRESULT STDMETHODCALLTYPE events_invoke(
    IDispatch* p, DISPID id, REFIID iid, LCID locale, WORD flags,
    DISPPARAMS* args, VARIANT* result, EXCEPINFO* exception, UINT* arg_error)
{
    struct PlatformWin32Browser* s = from_events(p);
    (void)iid; (void)locale; (void)flags; (void)result; (void)exception; (void)arg_error;
    if( !args ) return S_OK;
    if( id == DISPID_BEFORENAVIGATE2 && args->cArgs >= 7 )
    {
        int const allowed = navigation_allowed(s, &args->rgvarg[5]);
        WCHAR const* uri = variant_bstr(&args->rgvarg[5]);
        if( getenv("TORIRS_CHROME_DEBUG") )
            fprintf(
                stderr, "plugin chrome MSHTML: navigation %s: %ls\n",
                allowed ? "allowed" : "blocked", uri ? uri : L"(no URL)");
        if( !allowed )
            event_cancel(&args->rgvarg[0]);
    }
    else if( id == DISPID_NEWWINDOW2 && args->cArgs >= 2 )
        event_cancel(&args->rgvarg[0]);
    else if( id == DISPID_NEWWINDOW3 && args->cArgs >= 5 )
        event_cancel(&args->rgvarg[3]);
    else if( id == DISPID_FILEDOWNLOAD && args->cArgs >= 2 )
        event_cancel(&args->rgvarg[0]);
    else if( id == DISPID_DOCUMENTCOMPLETE && args->cArgs >= 1 )
    {
        WCHAR const* uri = variant_bstr(&args->rgvarg[0]);
        /* OLE activation can complete an initial about:blank after event
         * hookup. Never flush queued application state into that transient
         * document: the real navigation would immediately discard it. */
        if( local_page_url(s, uri) )
            inject_bridge(s);
    }
    return S_OK;
}

static IDispatchVtbl EVENTS_VTBL = {
    events_qi, events_addref, events_release, events_count, events_type,
    events_names, events_invoke
};

static int path_to_file_url(WCHAR const* path, WCHAR* out, int capacity)
{
    int at = 0;
    static WCHAR const prefix[] = L"file:///";
    if( capacity < 16 ) return 0;
    for( int i = 0; prefix[i] && at < capacity - 1; i++ ) out[at++] = prefix[i];
    for( int i = 0; path[i] && at < capacity - 4; i++ )
    {
        WCHAR c = path[i] == L'\\' ? L'/' : path[i];
        if( c == L' ' )
        { out[at++] = L'%'; out[at++] = L'2'; out[at++] = L'0'; }
        else out[at++] = c;
    }
    out[at] = 0;
    return path[0] != 0;
}

static IHTMLWindow2* document_window(struct PlatformWin32Browser* s)
{
    IDispatch* dispatch = NULL;
    IHTMLDocument2* document = NULL;
    IHTMLWindow2* window = NULL;
    if( !s->browser || FAILED(IWebBrowser2_get_Document(s->browser, &dispatch)) || !dispatch )
        return NULL;
    if( SUCCEEDED(IDispatch_QueryInterface(
            dispatch, &IID_IHTMLDocument2, (void**)&document)) && document )
    {
        IHTMLDocument2_get_parentWindow(document, &window);
        IHTMLDocument2_Release(document);
    }
    IDispatch_Release(dispatch);
    return window;
}

static int browser_exec(struct PlatformWin32Browser* s, char const* script)
{
    WCHAR* wide = wide_from_utf8(script);
    IHTMLWindow2* window;
    BSTR code;
    BSTR language;
    VARIANT result;
    HRESULT hr = E_FAIL;
    if( !wide ) return 0;
    window = document_window(s);
    if( !window ) { free(wide); return 0; }
    code = SysAllocString(wide);
    language = SysAllocString(L"javascript");
    VariantInit(&result);
    if( code && language )
        hr = IHTMLWindow2_execScript(window, code, language, &result);
    VariantClear(&result);
    SysFreeString(language);
    SysFreeString(code);
    IHTMLWindow2_Release(window);
    free(wide);
    return SUCCEEDED(hr);
}

static HRESULT browser_navigate(struct PlatformWin32Browser* s)
{
    VARIANT empty;
    VariantInit(&empty);
    BSTR url = SysAllocString(s->page_url);
    HRESULT hr = E_OUTOFMEMORY;
    if( url )
        hr = IWebBrowser2_Navigate(
            s->browser, url, &empty, &empty, &empty, &empty);
    SysFreeString(url);
    return hr;
}

struct PlatformWin32Browser*
PlatformWin32Browser_New(HWND parent, WCHAR const* bundle_root)
{
    struct PlatformWin32Browser* s;
    WCHAR root[2048];
    WCHAR page[2048];
    RECT rect;
    HRESULT hr;
    VARIANT_BOOL yes = VARIANT_TRUE;
    IConnectionPointContainer* points = NULL;

    if( !parent || !bundle_root || !bundle_root[0] ) return NULL;
    lstrcpynW(root, bundle_root, 2048);
    if( wcslen(root) + 24 >= 2048 ) return NULL;
    lstrcpyW(page, root);
    if( page[wcslen(page) - 1] != L'\\' ) lstrcatW(page, L"\\");
    lstrcatW(page, L"legacy-ie8.html");
    if( GetFileAttributesW(page) == INVALID_FILE_ATTRIBUTES ) return NULL;

    s = (struct PlatformWin32Browser*)calloc(1, sizeof(*s));
    if( !s ) return NULL;
    s->parent = parent;
    s->refs = 1;
    s->client_site.lpVtbl = &CLIENT_VTBL;
    s->inplace_site.lpVtbl = &SITE_VTBL;
    s->inplace_frame.lpVtbl = &FRAME_VTBL;
    s->ui_handler.lpVtbl = &UI_VTBL;
    s->ambient_dispatch.lpVtbl = &AMBIENT_VTBL;
    s->external_dispatch.lpVtbl = &EXTERNAL_VTBL;
    s->event_dispatch.lpVtbl = &EVENTS_VTBL;

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if( SUCCEEDED(hr) ) s->com_initialized = 1;
    else if( hr != RPC_E_CHANGED_MODE ) goto fail;
    hr = CoCreateInstance(
        &CLSID_WebBrowser, NULL, CLSCTX_INPROC_SERVER,
        &IID_IOleObject, (void**)&s->ole);
    if( FAILED(hr) || !s->ole ) goto fail;
    IOleObject_SetClientSite(s->ole, &s->client_site);
    OleSetContainedObject((IUnknown*)s->ole, TRUE);
    GetClientRect(parent, &rect);
    hr = IOleObject_DoVerb(
        s->ole, OLEIVERB_INPLACEACTIVATE, NULL, &s->client_site,
        0, parent, &rect);
    if( FAILED(hr) ) goto fail;
    IOleObject_QueryInterface(s->ole, &IID_IOleInPlaceObject, (void**)&s->inplace_object);
    IOleObject_QueryInterface(
        s->ole, &IID_IOleInPlaceActiveObject, (void**)&s->active_object);
    hr = IOleObject_QueryInterface(s->ole, &IID_IWebBrowser2, (void**)&s->browser);
    if( FAILED(hr) || !s->browser ) goto fail;
    IWebBrowser2_put_Silent(s->browser, yes);
    IWebBrowser2_put_Offline(s->browser, VARIANT_TRUE);
    IWebBrowser2_put_RegisterAsDropTarget(s->browser, VARIANT_FALSE);
    IWebBrowser2_put_Visible(s->browser, VARIANT_TRUE);

    path_to_file_url(root, s->allowed_root, 2048);
    if( s->allowed_root[wcslen(s->allowed_root) - 1] != L'/' )
        lstrcatW(s->allowed_root, L"/");
    path_to_file_url(page, s->page_url, 2048);
    lstrcpynW(s->page_path, page, 2048);
    hr = IWebBrowser2_QueryInterface(
        s->browser, &IID_IConnectionPointContainer, (void**)&points);
    if( FAILED(hr) || !points ) goto fail;
    hr = IConnectionPointContainer_FindConnectionPoint(
        points, &DIID_DWebBrowserEvents2, &s->events);
    IConnectionPointContainer_Release(points);
    points = NULL;
    if( FAILED(hr) || !s->events ) goto fail;
    hr = IConnectionPoint_Advise(
        s->events, (IUnknown*)&s->event_dispatch, &s->event_cookie);
    if( FAILED(hr) || !s->event_cookie ) goto fail;
    hr = browser_navigate(s);
    if( FAILED(hr) ) goto fail;
    return s;

fail:
    if( s ) s->failed = 1;
    PlatformWin32Browser_Free(s);
    return NULL;
}

void
PlatformWin32Browser_Free(struct PlatformWin32Browser* s)
{
    if( !s ) return;
    for( int i = 0; i < s->inbound_count; i++ ) free(s->inbound[i]);
    for( int i = 0; i < s->outbound_count; i++ ) free(s->outbound[i]);
    if( s->events && s->event_cookie )
        IConnectionPoint_Unadvise(s->events, s->event_cookie);
    if( s->events ) IConnectionPoint_Release(s->events);
    if( s->browser ) IWebBrowser2_Release(s->browser);
    if( s->active_object ) IOleInPlaceActiveObject_Release(s->active_object);
    if( s->inplace_object ) IOleInPlaceObject_Release(s->inplace_object);
    if( s->ole )
    {
        IOleObject_Close(s->ole, OLECLOSE_NOSAVE);
        IOleObject_SetClientSite(s->ole, NULL);
        IOleObject_Release(s->ole);
    }
    if( s->com_initialized ) CoUninitialize();
    free(s);
}

void
PlatformWin32Browser_Resize(struct PlatformWin32Browser* s, int width, int height)
{
    RECT rect;
    if( !s || width < 0 || height < 0 ) return;
    s->width = width;
    s->height = height;
    rect.left = rect.top = 0;
    rect.right = width;
    rect.bottom = height;
    if( s->inplace_object )
        IOleInPlaceObject_SetObjectRects(s->inplace_object, &rect, &rect);
    if( s->browser )
    {
        IWebBrowser2_put_Left(s->browser, 0);
        IWebBrowser2_put_Top(s->browser, 0);
        IWebBrowser2_put_Width(s->browser, width);
        IWebBrowser2_put_Height(s->browser, height);
    }
}

int
PlatformWin32Browser_Send(struct PlatformWin32Browser* s, char const* json)
{
    size_t size;
    char* call;
    char* copy;
    if( !s || !json || !json[0] || strlen(json) > MSHTML_JSON_MAX ) return 0;
    browser_try_ready(s);
    if( !s->ready )
    {
        if( s->inbound_count >= MSHTML_INBOUND_MAX ) return 0;
        copy = (char*)malloc(strlen(json) + 1);
        if( !copy ) return 0;
        strcpy(copy, json);
        s->inbound[s->inbound_count++] = copy;
        return 1;
    }
    size = strlen(json) + 320;
    call = (char*)malloc(size);
    if( !call ) return 0;
    snprintf(call, size,
        "(function(){var ok=false;try{ok=!!(window.ToriRSPluginChrome&&"
        "window.ToriRSPluginChrome.receive(%s)!==false);}catch(e){}"
        "if(!ok){try{window.external.postMessage(" 
        "'{\"protocol\":1,\"type\":\"transport.loss\"}');}catch(e){}}})();", json);
    {
        int const sent = browser_exec(s, call);
        free(call);
        return sent && !s->send_failed;
    }
}

int
PlatformWin32Browser_TakeSendFailure(struct PlatformWin32Browser* s)
{
    int failed = s ? s->send_failed : 0;
    if( s ) s->send_failed = 0;
    return failed;
}

static char* browser_pull(struct PlatformWin32Browser* s)
{
    static char const call[] =
        "window.ToriRSPluginChrome?window.ToriRSPluginChrome.takeMessage():''";
    WCHAR* wide = wide_from_utf8(call);
    IHTMLWindow2* window;
    BSTR code;
    BSTR language;
    VARIANT result;
    char* out = NULL;
    if( !s->ready || !wide ) { free(wide); return NULL; }
    window = document_window(s);
    if( !window ) { free(wide); return NULL; }
    code = SysAllocString(wide);
    language = SysAllocString(L"javascript");
    VariantInit(&result);
    if( code && language &&
        SUCCEEDED(IHTMLWindow2_execScript(window, code, language, &result)) &&
        result.vt == VT_BSTR && result.bstrVal && result.bstrVal[0] )
        out = utf8_from_wide(result.bstrVal);
    VariantClear(&result);
    SysFreeString(language);
    SysFreeString(code);
    IHTMLWindow2_Release(window);
    free(wide);
    return out;
}

int
PlatformWin32Browser_Poll(
    struct PlatformWin32Browser* s, char* out_json, int capacity)
{
    char* message;
    int length;
    if( !s || !out_json || capacity <= 0 ) return 0;
    browser_try_ready(s);
    if( s->outbound_count > 0 )
    {
        message = s->outbound[0];
        for( int i = 1; i < s->outbound_count; i++ )
            s->outbound[i - 1] = s->outbound[i];
        s->outbound_count--;
    }
    else message = browser_pull(s);
    if( !message ) return 0;
    length = (int)strlen(message);
    if( length >= capacity ) length = capacity - 1;
    memcpy(out_json, message, (size_t)length);
    out_json[length] = 0;
    free(message);
    return length;
}

int PlatformWin32Browser_Ready(struct PlatformWin32Browser const* s)
{
    browser_try_ready((struct PlatformWin32Browser*)s);
    return s && s->ready;
}
int PlatformWin32Browser_Failed(struct PlatformWin32Browser const* s)
{ return !s || s->failed; }
int PlatformWin32Browser_PreTranslateMessage(
    struct PlatformWin32Browser* s, MSG* message)
{
    return s && s->active_object && message &&
           IOleInPlaceActiveObject_TranslateAccelerator(
               s->active_object, message) == S_OK;
}
#if defined(TORIRS_WIN32_BROWSER_CAPTURE_API)
int PlatformWin32Browser_CapturePng(
    struct PlatformWin32Browser* s, char const* path)
{ (void)s; (void)path; return 0; }
int PlatformWin32Browser_CaptureStatus(struct PlatformWin32Browser const* s)
{ (void)s; return -1; }
#endif
void PlatformWin32Browser_AllowLocalRoot(
    struct PlatformWin32Browser* s, WCHAR const* root)
{
    if( s ) lstrcpynW(s->allowed_asset_root, root ? root : L"", 2048);
}
