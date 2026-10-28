/*
 * One application-owned WKWebView embedded in the SDL/Cocoa client window.
 *
 * Plugins never supply markup, script, URLs, or navigation.  The view loads a
 * copied canonical bundle, receives bounded semantic JSON, and returns copied
 * intent JSON through one WKScriptMessageHandler.  The object survives page
 * collapse and plugin selection; only PlatformWindow_Free destroys it.
 */

#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>

#include "platform_macos_webview.h"
#include "platform_window.h"

/* Macros and comments only -- it includes nothing, so a platform file can read
 * the chrome's authored geometry without linking ui/ behind it. */
#include "../ui/torirs_chrome_metrics.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The rail's allocation, in window POINTS: the page lays the rail out in CSS
 * pixels at the same number, and a host that reserves more leaves a band of
 * child-window background down the seam. */
#define MAC_BROWSER_RAIL_POINTS TORIRS_CHROME_M_RAIL_W
#define MAC_BROWSER_QUEUE_MAX 64
#define MAC_BROWSER_JSON_MAX (8u * 1024u * 1024u)
_Static_assert(MAC_BROWSER_QUEUE_MAX == 64,
    "mac plugin bridge literal must match its native inbound queue");
#define MAC_BROWSER_PIXELS_MAX (4u * 1024u * 1024u)

@interface ToriRSPluginWebView : WKWebView
@end

@implementation ToriRSPluginWebView
- (BOOL)acceptsFirstMouse:(NSEvent*)event
{
    /*
     * This view lives in a borderless child window so WebKit can composite at
     * its own cadence. Clicking the game makes the SDL parent key again; by
     * default AppKit then spends the next click only activating this child and
     * never delivers it to the DOM. Plugin chrome is an application control,
     * so its activation click must also be the control click.
     */
    (void)event;
    return YES;
}

- (NSMenu*)menuForEvent:(NSEvent*)event
{
    (void)event;
    return nil;
}
@end

/*
 * The browser lives in a borderless CHILD WINDOW attached to SDL's NSWindow,
 * not in a subview of its content view.
 *
 * A WKWebView added as a subview makes SDL's whole window layer-backed: the
 * GL surface and the web view then share one layer tree and one CATransaction
 * commit, and SDL's swap is coupled to WebKit's commits -- the game's redraw
 * rate follows the browser's. A child window moves with its parent and reads
 * as attached, but the WindowServer composites it as a separate surface, so
 * the game's swap cadence is its own again.
 */
@interface ToriRSPluginChromeWindow : NSWindow
@end

@implementation ToriRSPluginChromeWindow
/* Borderless windows refuse key status by default; the page's text fields
 * need it to receive typing. */
- (BOOL)canBecomeKeyWindow
{
    return YES;
}
- (BOOL)canBecomeMainWindow
{
    return NO;
}

- (void)sendEvent:(NSEvent*)event
{
    /*
     * AppKit asks the farthest hit-tested view whether an activation click
     * should pass through. For WKWebView that receiver is WebKit's private
     * content view, not our WKWebView subclass, so acceptsFirstMouse: on the
     * public wrapper alone cannot guarantee delivery. This is especially
     * visible with the browser in a child NSWindow: every click on the SDL
     * parent makes that parent key and the next rail click would only make
     * this child key.
     *
     * Make the child key before NSWindow dispatches a mouse-down. Super then
     * sees an ordinary event in the key window and delivers the same event to
     * WebKit's real hit view; there is no synthetic second click and no DOM
     * coordinate reconstruction. acceptsFirstMouse: above remains useful for
     * the simpler case where AppKit does consult the public wrapper.
     */
    NSEventType const type = event.type;
    if( !self.isKeyWindow &&
        (type == NSEventTypeLeftMouseDown ||
         type == NSEventTypeRightMouseDown ||
         type == NSEventTypeOtherMouseDown) )
        [self makeKeyWindow];
    [super sendEvent:event];
}
@end

@interface ToriRSMacPluginBrowser : NSObject
    <WKScriptMessageHandler, WKNavigationDelegate, WKUIDelegate, NSWindowDelegate>
@property(nonatomic, assign) struct PlatformWindow* platform;
@property(nonatomic, strong) ToriRSPluginWebView* view;
@property(nonatomic, strong) ToriRSPluginChromeWindow* hostWindow;
@property(nonatomic, assign) NSRect lastScreenFrame;
/* Taken off screen because nothing is allocated to it: a hidden rail and no
 * page. @see PlatformWindow_ChromeSetRailHidden. */
@property(nonatomic, assign) BOOL hostOffScreen;
@property(nonatomic, strong) NSURL* rootURL;
@property(nonatomic, strong) NSURL* documentURL;
@property(nonatomic, strong) NSMutableArray<NSString*>* inbound;
@property(nonatomic, strong) NSMutableArray<NSString*>* outbound;
@property(nonatomic, strong) NSMutableDictionary<NSString*, NSString*>* bitmapFiles;
@property(nonatomic, assign) BOOL ready;
@property(nonatomic, assign) BOOL failed;
@property(nonatomic, assign) BOOL sendFailed;
/*
 * The page's pop-out window: a titled, movable NSWindow of its own (not a
 * child of SDL's) holding a second view of the same staged bundle on the page
 * view, modern.html#page. nil while the page is docked; the rail never leaves
 * the child window beside the game.
 */
@property(nonatomic, strong) ToriRSPluginWebView* pageView;
@property(nonatomic, strong) NSWindow* pageWindow;
@property(nonatomic, assign) BOOL pageReady;
@property(nonatomic, strong) NSMutableArray<NSString*>* pageOutbound;
/* Parallel to `inbound`: YES where that message came from the page view, so
 * its JS-side slot is released in the document that spent it. */
@property(nonatomic, strong) NSMutableArray<NSNumber*>* inboundFromPage;
/* Its close box was used (or its content died); reported once on Poll. */
@property(nonatomic, assign) BOOL pageClosed;
@end

static ToriRSMacPluginBrowser* g_mac_browser;

static NSString*
mac_existing_bundle_root(void)
{
    NSFileManager* files = [NSFileManager defaultManager];
    NSMutableArray<NSString*>* candidates = [NSMutableArray array];
    char const* configured = getenv("TORIRS_PLUGIN_CHROME_DIR");
    NSString* cwd = [[files currentDirectoryPath] stringByStandardizingPath];
    NSString* resources = [[NSBundle mainBundle] resourcePath];

    if( configured && configured[0] )
        [candidates addObject:[NSString stringWithUTF8String:configured]];
    if( resources )
        [candidates addObject:[resources stringByAppendingPathComponent:@"plugin_chrome"]];
    [candidates addObject:[cwd stringByAppendingPathComponent:@"src/plugin_chrome"]];
    [candidates addObject:[cwd stringByAppendingPathComponent:@"plugin_chrome"]];
    [candidates addObject:[cwd stringByAppendingPathComponent:@"../src/plugin_chrome"]];

    for( NSString* candidate in candidates )
    {
        NSString* root = [candidate stringByStandardizingPath];
        if( [files fileExistsAtPath:[root stringByAppendingPathComponent:@"modern.html"]] &&
            [files fileExistsAtPath:[root stringByAppendingPathComponent:@"runtime.js"]] &&
            [files fileExistsAtPath:[root stringByAppendingPathComponent:@"codec-es3.js"]] )
            return root;
    }
    return nil;
}

static NSString*
mac_existing_skin_root(NSString* bundleRoot)
{
    NSFileManager* files = [NSFileManager defaultManager];
    NSString* cwd = [[files currentDirectoryPath] stringByStandardizingPath];
    NSArray<NSString*>* candidates = @[
        [bundleRoot stringByAppendingPathComponent:@"skin"],
        [cwd stringByAppendingPathComponent:@"res/plugin_chrome/skin"],
        [cwd stringByAppendingPathComponent:@"../res/plugin_chrome/skin"]
    ];
    for( NSString* candidate in candidates )
        if( [files fileExistsAtPath:
                [candidate stringByAppendingPathComponent:@"PanelBody.png"]] )
            return [candidate stringByStandardizingPath];
    return nil;
}

static NSString*
mac_existing_font_root(NSString* bundleRoot)
{
    NSFileManager* files = [NSFileManager defaultManager];
    NSString* cwd = [[files currentDirectoryPath] stringByStandardizingPath];
    NSArray<NSString*>* candidates = @[
        [bundleRoot stringByAppendingPathComponent:@"font"],
        [cwd stringByAppendingPathComponent:@"res/plugin_chrome/font"],
        [cwd stringByAppendingPathComponent:@"../res/plugin_chrome/font"]
    ];
    for( NSString* candidate in candidates )
        if( [files fileExistsAtPath:
                [candidate stringByAppendingPathComponent:@"ToriRSBody.ttf"]] )
            return [candidate stringByStandardizingPath];
    return nil;
}

static NSURL*
mac_stage_bundle(void)
{
    static NSString* const names[] = {
        @"modern.html", @"modern.css", @"runtime.js", @"codec-es3.js",
        @"legacy-ie8.html", @"legacy-ie8.css", @"runtime-ie8.js"
    };
    NSFileManager* files = [NSFileManager defaultManager];
    NSString* source = mac_existing_bundle_root();
    if( !source )
        return nil;
    NSString* stage = [NSTemporaryDirectory() stringByAppendingPathComponent:
        [NSString stringWithFormat:@"torirs-plugin-chrome-%d", getpid()]];
    NSError* error = nil;
    [files removeItemAtPath:stage error:nil];
    if( ![files createDirectoryAtPath:stage
          withIntermediateDirectories:YES attributes:nil error:&error] )
        return nil;
    for( unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++ )
    {
        NSString* from = [source stringByAppendingPathComponent:names[i]];
        NSString* to = [stage stringByAppendingPathComponent:names[i]];
        if( ![files copyItemAtPath:from toPath:to error:&error] )
        {
            [files removeItemAtPath:stage error:nil];
            return nil;
        }
    }
    NSString* bitmap = [stage stringByAppendingPathComponent:@"bitmap"];
    if( ![files createDirectoryAtPath:bitmap
          withIntermediateDirectories:NO attributes:nil error:&error] )
    {
        [files removeItemAtPath:stage error:nil];
        return nil;
    }
    NSString* skin = mac_existing_skin_root(source);
    if( skin )
        (void)[files copyItemAtPath:skin
            toPath:[stage stringByAppendingPathComponent:@"skin"] error:nil];
    NSString* font = mac_existing_font_root(source);
    if( font )
        (void)[files copyItemAtPath:font
            toPath:[stage stringByAppendingPathComponent:@"font"] error:nil];
    return [NSURL fileURLWithPath:stage isDirectory:YES];
}

static NSString*
mac_javascript_argument(NSString* value)
{
    if( !value )
        return nil;
    NSError* error = nil;
    NSData* encoded = [NSJSONSerialization dataWithJSONObject:@[ value ]
        options:0 error:&error];
    if( !encoded || error )
        return nil;
    return [[NSString alloc] initWithData:encoded encoding:NSUTF8StringEncoding];
}

static BOOL
mac_url_is_below(NSURL* url, NSURL* root)
{
    if( !url || !root || !url.isFileURL )
        return NO;
    NSString* path = [[url URLByStandardizingPath] path];
    NSString* rootPath = [[[root URLByStandardizingPath] path]
        stringByAppendingString:@"/"];
    return [path hasPrefix:rootPath];
}

@implementation ToriRSMacPluginBrowser

- (void)syncFrame
{
    if( !self.view || !self.platform )
        return;
    NSWindow* window = (__bridge NSWindow*)PlatformWindow_NativeWindowHandle(self.platform);
    NSView* content = window.contentView;
    if( !content )
        return;
    int density = PlatformWindow_PixelDensity(self.platform);
    int pixels = PlatformWindow_ChromeWidth(self.platform);
    CGFloat width = density > 0 ? (CGFloat)pixels / (CGFloat)density : (CGFloat)pixels;
    if( width < 1.0 && PlatformWindow_ChromeRailHidden(self.platform) )
    {
        /* Nothing is allocated: the rail is hidden and no page is up. A child window is ordered back in with
         * its parent, so it is detached rather than merely ordered out. */
        if( self.hostWindow && !self.hostOffScreen )
        {
            [window removeChildWindow:self.hostWindow];
            [self.hostWindow orderOut:nil];
            self.hostOffScreen = YES;
            self.lastScreenFrame = NSZeroRect;
        }
        return;
    }
    if( self.hostWindow && self.hostOffScreen )
    {
        [window addChildWindow:self.hostWindow ordered:NSWindowAbove];
        [self.hostWindow orderFront:nil];
        self.hostOffScreen = NO;
    }
    if( width < 1.0 )
        width = MAC_BROWSER_RAIL_POINTS;
    if( width > NSWidth(content.bounds) )
        width = NSWidth(content.bounds);
    NSRect local = NSMakeRect(
        NSMaxX(content.bounds) - width,
        NSMinY(content.bounds),
        width,
        NSHeight(content.bounds));
    if( !self.hostWindow )
    {
        self.view.frame = local;
        return;
    }
    /* The child window's frame is in screen space. Only move it when the
     * allocation actually changed: this runs every poll, and a setFrame: per
     * frame is a layout pass per frame. */
    NSRect screen = [window convertRectToScreen:[content convertRect:local toView:nil]];
    if( NSEqualRects(screen, self.lastScreenFrame) )
        return;
    self.lastScreenFrame = screen;
    [self.hostWindow setFrame:screen display:YES];
}

- (BOOL)evaluateEnvelope:(NSString*)json
{
    BOOL sent = [self evaluateEnvelope:json inView:self.view ready:self.ready
                                 queue:self.outbound];
    /* Every envelope reaches both documents while the page is out; each knows
     * from page.popout which of them holds the page. */
    if( self.pageView &&
        ![self evaluateEnvelope:json inView:self.pageView ready:self.pageReady
                          queue:self.pageOutbound] )
        sent = NO;
    return sent;
}

- (BOOL)evaluateEnvelope:(NSString*)json
                  inView:(WKWebView*)target
                   ready:(BOOL)targetReady
                   queue:(NSMutableArray<NSString*>*)queue
{
    if( !json || json.length == 0 || json.length > MAC_BROWSER_JSON_MAX )
        return NO;
    if( !targetReady )
    {
        if( queue.count >= MAC_BROWSER_QUEUE_MAX )
            return NO;
        NSString* copy = [json copy];
        if( !copy )
            return NO;
        [queue addObject:copy];
        return YES;
    }
    NSString* argument = mac_javascript_argument(json);
    if( !argument )
        return NO;
    NSString* script = [NSString stringWithFormat:
        @"(function(){try{return !!(window.ToriRSPluginChrome&&"
         "window.ToriRSPluginChrome.receive((%@)[0])!==false);}"
         "catch(e){return false;}})();",
        argument];
    if( !script || !target )
        return NO;
    [target evaluateJavaScript:script completionHandler:^(id result, NSError* error) {
        if( error || ![result respondsToSelector:@selector(boolValue)] || ![result boolValue] )
            self.sendFailed = YES;
    }];
    return YES;
}

/** Release one JS-side outbound slot only after C has consumed its native
 * copy, then move exactly one runtime fallback message into that slot. The
 * counters therefore bound the native queue rather than merely the number of
 * WebKit callbacks waiting to run. */
- (void)acknowledgeInboundSlot
{
    [self acknowledgeInboundSlotIn:self.view ready:self.ready];
}

- (void)acknowledgeInboundSlotIn:(WKWebView*)target ready:(BOOL)targetReady
{
    static NSString* const script =
        @"(function(){var n=window.__torirsChromePending|0;"
         "if(n>0)window.__torirsChromePending=n-1;"
         "var r=window.ToriRSPluginChrome;"
         "if(r&&typeof r.takeMessage==='function'){var m=r.takeMessage();"
         "if(m)return window.torirsPluginChromePostMessage(m);}return true;})();";
    if( !target || !targetReady )
        return;
    [target evaluateJavaScript:script completionHandler:^(id result, NSError* error) {
        if( error || ![result respondsToSelector:@selector(boolValue)] || ![result boolValue] )
            self.sendFailed = YES;
    }];
}

- (void)acknowledgeSlotFor:(WKScriptMessage*)message
{
    if( self.pageView && message.webView == self.pageView )
        [self acknowledgeInboundSlotIn:self.pageView ready:self.pageReady];
    else
        [self acknowledgeInboundSlot];
}

- (void)userContentController:(WKUserContentController*)controller
      didReceiveScriptMessage:(WKScriptMessage*)message
{
    (void)controller;
    if( ![message.name isEqualToString:@"torirsPluginChrome"] ||
        ![message.body isKindOfClass:[NSString class]] )
    {
        self.sendFailed = YES;
        [self acknowledgeSlotFor:message];
        return;
    }
    NSString* json = (NSString*)message.body;
    if( json.length == 0 || json.length > 8192 )
    {
        self.sendFailed = YES;
        [self acknowledgeSlotFor:message];
        return;
    }
    if( self.inbound.count >= MAC_BROWSER_QUEUE_MAX )
    {
        /* Reject the newest message. The accepted prefix stays ordered and
         * sendFailed tells the retained executor exactly what was lost. */
        self.sendFailed = YES;
        [self acknowledgeSlotFor:message];
        return;
    }
    NSString* copy = [json copy];
    if( !copy )
    {
        self.sendFailed = YES;
        [self acknowledgeSlotFor:message];
        return;
    }
    [self.inbound addObject:copy];
    [self.inboundFromPage addObject:@(self.pageView && message.webView == self.pageView)];
}

- (void)webView:(WKWebView*)webView didFinishNavigation:(WKNavigation*)navigation
{
    (void)navigation;
    if( self.pageView && webView == self.pageView )
    {
        /* The page view failing is the pop-out window going away, never the
         * shell's failure: the page docks back into the browser that works. */
        if( !mac_url_is_below(self.pageView.URL, self.rootURL) )
        {
            self.pageClosed = YES;
            return;
        }
        self.pageReady = YES;
        NSArray<NSString*>* pending = [self.pageOutbound copy];
        [self.pageOutbound removeAllObjects];
        for( NSString* json in pending )
            if( ![self evaluateEnvelope:json inView:self.pageView ready:YES
                                  queue:self.pageOutbound] )
                self.sendFailed = YES;
        return;
    }
    if( !mac_url_is_below(self.view.URL, self.rootURL) )
    {
        self.failed = YES;
        return;
    }
    self.ready = YES;
    NSArray<NSString*>* queued = [self.outbound copy];
    [self.outbound removeAllObjects];
    for( NSString* json in queued )
        if( ![self evaluateEnvelope:json] )
            self.sendFailed = YES;
}

- (void)webView:(WKWebView*)webView
      didFailNavigation:(WKNavigation*)navigation
              withError:(NSError*)error
{
    (void)navigation;
    (void)error;
    if( self.pageView && webView == self.pageView )
        self.pageClosed = YES;
    else
        self.failed = YES;
}

- (void)webView:(WKWebView*)webView
      didFailProvisionalNavigation:(WKNavigation*)navigation
                         withError:(NSError*)error
{
    (void)navigation;
    (void)error;
    if( self.pageView && webView == self.pageView )
        self.pageClosed = YES;
    else
        self.failed = YES;
}

- (void)webView:(WKWebView*)webView
    decidePolicyForNavigationAction:(WKNavigationAction*)action
                    decisionHandler:(void (^)(WKNavigationActionPolicy))decisionHandler
{
    (void)webView;
    BOOL local = mac_url_is_below(action.request.URL, self.rootURL);
    BOOL sameFrame = action.targetFrame != nil;
    /*
     * A RELOAD is refused, and it is refused here rather than only by hiding
     * whatever offers it.
     *
     * The page is not a document that can be fetched again: it is the far end
     * of a retained conversation. A reloaded page comes up with no rail, no
     * theme and no widgets, while this side still holds every generation and
     * handle it has handed out and goes on addressing them. The bundle now
     * answers the right click itself (src/plugin_chrome), which takes the
     * view's own menu out of the picture; the key equivalent is still there,
     * and one keystroke must not be able to strand the host.
     */
    BOOL reload = action.navigationType == WKNavigationTypeReload;
    decisionHandler(local && sameFrame && !reload ?
        WKNavigationActionPolicyAllow : WKNavigationActionPolicyCancel);
}

- (WKWebView*)webView:(WKWebView*)webView
    createWebViewWithConfiguration:(WKWebViewConfiguration*)configuration
               forNavigationAction:(WKNavigationAction*)navigationAction
                    windowFeatures:(WKWindowFeatures*)windowFeatures
{
    (void)webView;
    (void)configuration;
    (void)navigationAction;
    (void)windowFeatures;
    return nil;
}

- (void)webViewWebContentProcessDidTerminate:(WKWebView*)webView
{
    if( self.pageView && webView == self.pageView )
    {
        self.pageClosed = YES;
        self.pageReady = NO;
        return;
    }
    self.failed = YES;
    self.ready = NO;
}

/* The pop-out window's close box is REPORTED, not obeyed: the executor's
 * PopOut(false) takes the window down, through the one path that also gives
 * the page its width back beside the game. */
- (BOOL)windowShouldClose:(NSWindow*)sender
{
    if( sender == self.pageWindow )
    {
        self.pageClosed = YES;
        return NO;
    }
    return YES;
}

- (void)closePageWindow
{
    if( self.pageView )
    {
        self.pageView.navigationDelegate = nil;
        self.pageView.UIDelegate = nil;
        [self.pageView stopLoading];
    }
    if( self.pageWindow )
    {
        self.pageWindow.delegate = nil;
        [self.pageWindow orderOut:nil];
        self.pageWindow.contentView = nil;
    }
    /* A message the page view posted and C has not read yet addresses a page
     * that is about to be restated in the other document; drop it with the
     * slot it held. */
    for( NSUInteger i = self.inboundFromPage.count; i > 0; i-- )
        if( [self.inboundFromPage[i - 1] boolValue] )
        {
            [self.inbound removeObjectAtIndex:i - 1];
            [self.inboundFromPage removeObjectAtIndex:i - 1];
        }
    [self.pageOutbound removeAllObjects];
    self.pageView = nil;
    self.pageWindow = nil;
    self.pageReady = NO;
    self.pageClosed = NO;
}

@end

bool
PlatformWindow_PluginBrowserEnsure(struct PlatformWindow* platform)
{
    if( !platform || ![NSThread isMainThread] )
        return false;
    if( g_mac_browser )
        return g_mac_browser.platform == platform && !g_mac_browser.failed;
    if( !PlatformWindow_ChromeRailOpen(
            platform, MAC_BROWSER_RAIL_POINTS, "Plugins") )
        return false;

    /* SDL's dummy driver has no native window. Check before staging files:
     * a headless acceptance run polls this every frame. */
    NSWindow* window = (__bridge NSWindow*)PlatformWindow_NativeWindowHandle(platform);
    if( !window.contentView )
        return false;
    NSURL* root = mac_stage_bundle();
    if( !root )
        return false;

    ToriRSMacPluginBrowser* state = [[ToriRSMacPluginBrowser alloc] init];
    state.platform = platform;
    state.rootURL = root;
    state.documentURL = [root URLByAppendingPathComponent:@"modern.html"];
    state.inbound = [NSMutableArray arrayWithCapacity:MAC_BROWSER_QUEUE_MAX];
    state.outbound = [NSMutableArray arrayWithCapacity:MAC_BROWSER_QUEUE_MAX];
    state.bitmapFiles = [NSMutableDictionary dictionary];
    state.inboundFromPage = [NSMutableArray arrayWithCapacity:MAC_BROWSER_QUEUE_MAX];
    state.pageOutbound = [NSMutableArray arrayWithCapacity:MAC_BROWSER_QUEUE_MAX];

    WKUserContentController* controller = [[WKUserContentController alloc] init];
    NSString* bridge =
        @"window.__torirsChromePending=0;"
         "window.torirsPluginChromePostMessage=function(value){"
         "if((window.__torirsChromePending|0)>=64)return false;"
         "try{window.__torirsChromePending=(window.__torirsChromePending|0)+1;"
         "window.webkit.messageHandlers.torirsPluginChrome.postMessage(String(value));"
         "return true;}catch(e){window.__torirsChromePending="
         "Math.max(0,(window.__torirsChromePending|0)-1);return false;}};";
    WKUserScript* bridgeScript = [[WKUserScript alloc] initWithSource:bridge
        injectionTime:WKUserScriptInjectionTimeAtDocumentStart
        forMainFrameOnly:YES];
    [controller addUserScript:bridgeScript];
    [controller addScriptMessageHandler:state name:@"torirsPluginChrome"];

    WKWebViewConfiguration* configuration = [[WKWebViewConfiguration alloc] init];
    configuration.userContentController = controller;
    configuration.websiteDataStore = [WKWebsiteDataStore nonPersistentDataStore];
    configuration.preferences.javaScriptCanOpenWindowsAutomatically = NO;

    ToriRSPluginWebView* view = [[ToriRSPluginWebView alloc]
        initWithFrame:NSZeroRect configuration:configuration];
    view.navigationDelegate = state;
    view.UIDelegate = state;
    view.allowsBackForwardNavigationGestures = NO;
    view.allowsMagnification = NO;
    view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    state.view = view;

    ToriRSPluginChromeWindow* host = [[ToriRSPluginChromeWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, MAC_BROWSER_RAIL_POINTS, 100)
                  styleMask:NSWindowStyleMaskBorderless
                    backing:NSBackingStoreBuffered
                      defer:NO];
    host.opaque = YES;
    host.hasShadow = NO;
    host.releasedWhenClosed = NO;
    host.backgroundColor = [NSColor colorWithCalibratedRed:14.0 / 255.0
                                                     green:14.0 / 255.0
                                                      blue:12.0 / 255.0
                                                     alpha:1.0];
    view.frame = host.contentView.bounds;
    host.contentView = view;
    [window addChildWindow:host ordered:NSWindowAbove];
    state.hostWindow = host;
    state.lastScreenFrame = NSZeroRect;
    g_mac_browser = state;
    [host orderFront:nil];
    /* After ordering it in, so a browser created with nothing allocated to it
     * (a hidden rail and no page) is taken straight back off screen. */
    [state syncFrame];
    [view loadFileURL:state.documentURL allowingReadAccessToURL:state.rootURL];
    return true;
}

bool
PlatformWindow_PluginBrowserReady(struct PlatformWindow const* platform)
{
    return platform && g_mac_browser && g_mac_browser.platform == platform &&
           g_mac_browser.ready && !g_mac_browser.failed;
}

bool
PlatformWindow_PluginBrowserFailed(struct PlatformWindow const* platform)
{
    return !platform || (g_mac_browser && g_mac_browser.platform == platform &&
                         g_mac_browser.failed);
}

bool
PlatformWindow_PluginBrowserSend(
    struct PlatformWindow* platform, char const* json)
{
    NSString* value;
    if( !json || strlen(json) > MAC_BROWSER_JSON_MAX ||
        !PlatformWindow_PluginBrowserEnsure(platform) )
        return false;
    value = [NSString stringWithUTF8String:json];
    if( !value )
        return false;
    [g_mac_browser syncFrame];
    return [g_mac_browser evaluateEnvelope:value] ? true : false;
}

bool
PlatformWindow_PluginBrowserTakeSendFailure(struct PlatformWindow* platform)
{
    BOOL failed;
    if( !g_mac_browser || g_mac_browser.platform != platform )
        return false;
    failed = g_mac_browser.sendFailed;
    g_mac_browser.sendFailed = NO;
    return failed ? true : false;
}

static void
mac_acknowledge_first(ToriRSMacPluginBrowser* browser)
{
    BOOL const fromPage = browser.inboundFromPage.count > 0 &&
                          [browser.inboundFromPage[0] boolValue];
    if( browser.inboundFromPage.count > 0 )
        [browser.inboundFromPage removeObjectAtIndex:0];
    if( fromPage && browser.pageView )
        [browser acknowledgeInboundSlotIn:browser.pageView ready:browser.pageReady];
    else if( !fromPage )
        [browser acknowledgeInboundSlot];
}

int
PlatformWindow_PluginBrowserPoll(
    struct PlatformWindow* platform, char* out_json, int capacity)
{
    static char const closed[] =
        "{\"protocol\":1,\"type\":\"page.popout\",\"popout\":false}";
    if( !out_json || capacity <= 0 || !g_mac_browser ||
        g_mac_browser.platform != platform )
        return 0;
    [g_mac_browser syncFrame];
    /* The pop-out window's close box, said in the protocol's own words. */
    if( g_mac_browser.pageClosed && g_mac_browser.pageWindow &&
        capacity > (int)sizeof(closed) )
    {
        g_mac_browser.pageClosed = NO;
        memcpy(out_json, closed, sizeof(closed));
        return (int)sizeof(closed) - 1;
    }
    if( g_mac_browser.inbound.count == 0 )
        return 0;
    NSString* json = g_mac_browser.inbound[0];
    NSData* bytes = [json dataUsingEncoding:NSUTF8StringEncoding];
    if( !bytes )
    {
        [g_mac_browser.inbound removeObjectAtIndex:0];
        g_mac_browser.sendFailed = YES;
        mac_acknowledge_first(g_mac_browser);
        return 0;
    }
    if( bytes.length >= (NSUInteger)capacity )
        return 0;
    [g_mac_browser.inbound removeObjectAtIndex:0];
    memcpy(out_json, bytes.bytes, bytes.length);
    out_json[bytes.length] = '\0';
    mac_acknowledge_first(g_mac_browser);
    return (int)bytes.length;
}

bool
PlatformWindow_PluginBrowserCanPopOut(struct PlatformWindow const* platform)
{
    (void)platform;
    return true;
}

/*
 * Detach the page into a titled, movable window of its own.
 *
 * Not the child window itself: that one holds the shell document, whose rail
 * stays beside the game. The pop-out window holds a second view of the same
 * staged bundle on the page view, sharing the one script-message handler, so
 * its messages land on the same inbound queue and every envelope sent while it
 * is up reaches both documents (evaluateEnvelope:).
 */
bool
PlatformWindow_PluginBrowserPopOut(
    struct PlatformWindow* platform, bool out, char const* title)
{
    if( !g_mac_browser || g_mac_browser.platform != platform || ![NSThread isMainThread] )
        return false;
    NSString* name = title && title[0] ? [NSString stringWithUTF8String:title] : nil;
    if( !name )
        name = @"Plugins";
    if( !out )
    {
        [g_mac_browser closePageWindow];
        return true;
    }
    if( g_mac_browser.pageWindow )
    {
        g_mac_browser.pageWindow.title = name;
        return true;
    }
    if( g_mac_browser.failed || !g_mac_browser.view )
        return false;

    NSWindow* parent = (__bridge NSWindow*)PlatformWindow_NativeWindowHandle(platform);
    NSRect const anchor = parent ? parent.frame : NSMakeRect(0, 0, 765, 503);
    CGFloat const height = parent && parent.contentView
                               ? NSHeight(parent.contentView.bounds) : 480.0;
    NSRect const content = NSMakeRect(
        NSMaxX(anchor), NSMaxY(anchor) - height, 320.0, height);

    WKWebViewConfiguration* configuration = [[WKWebViewConfiguration alloc] init];
    configuration.userContentController =
        g_mac_browser.view.configuration.userContentController;
    configuration.websiteDataStore = [WKWebsiteDataStore nonPersistentDataStore];
    configuration.preferences.javaScriptCanOpenWindowsAutomatically = NO;
    ToriRSPluginWebView* view = [[ToriRSPluginWebView alloc]
        initWithFrame:NSMakeRect(0, 0, NSWidth(content), NSHeight(content))
        configuration:configuration];
    view.navigationDelegate = g_mac_browser;
    view.UIDelegate = g_mac_browser;
    view.allowsBackForwardNavigationGestures = NO;
    view.allowsMagnification = NO;
    view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    NSWindow* window = [[NSWindow alloc]
        initWithContentRect:content
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    window.releasedWhenClosed = NO;
    window.title = name;
    window.contentMinSize = NSMakeSize(TORIRS_CHROME_M_RAIL_W * 4, 120);
    window.backgroundColor = [NSColor colorWithCalibratedRed:14.0 / 255.0
                                                       green:14.0 / 255.0
                                                        blue:12.0 / 255.0
                                                       alpha:1.0];
    window.contentView = view;
    window.delegate = g_mac_browser;
    g_mac_browser.pageView = view;
    g_mac_browser.pageWindow = window;
    g_mac_browser.pageReady = NO;
    g_mac_browser.pageClosed = NO;
    [window makeKeyAndOrderFront:nil];

    NSURL* page = [NSURL URLWithString:@"#page" relativeToURL:g_mac_browser.documentURL];
    [view loadFileURL:page.absoluteURL allowingReadAccessToURL:g_mac_browser.rootURL];
    return true;
}

static NSString*
mac_safe_bitmap_key(char const* key)
{
    if( !key || !key[0] )
        return nil;
    char clean[97];
    unsigned used = 0;
    for( ; key[used] && used < sizeof(clean) - 1; used++ )
    {
        unsigned char c = (unsigned char)key[used];
        if( !(isalnum(c) || c == '-' || c == '_') )
            return nil;
        clean[used] = (char)c;
    }
    if( key[used] )
        return nil;
    clean[used] = '\0';
    return [NSString stringWithUTF8String:clean];
}

bool
PlatformWindow_PluginBrowserBitmapUrl(
    struct PlatformWindow* platform,
    char const* cache_key,
    uint32_t revision,
    uint32_t const* argb,
    int width,
    int height,
    char* out_url,
    int capacity)
{
    if( out_url && capacity > 0 )
        out_url[0] = '\0';
    if( !out_url || capacity <= 0 || !argb || !revision || width <= 0 || height <= 0 ||
        (size_t)width > SIZE_MAX / (size_t)height ||
        (size_t)width * (size_t)height > MAC_BROWSER_PIXELS_MAX ||
        !PlatformWindow_PluginBrowserEnsure(platform) )
        return false;
    NSString* key = mac_safe_bitmap_key(cache_key);
    if( !key )
        return false;

    NSBitmapImageRep* bitmap = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:NULL pixelsWide:width pixelsHigh:height
        bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
        colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:width * 4 bitsPerPixel:32];
    unsigned char* rgba = bitmap.bitmapData;
    if( !bitmap || !rgba )
        return false;
    size_t count = (size_t)width * (size_t)height;
    for( size_t i = 0; i < count; i++ )
    {
        uint32_t p = argb[i];
        rgba[i * 4 + 0] = (unsigned char)(p >> 16);
        rgba[i * 4 + 1] = (unsigned char)(p >> 8);
        rgba[i * 4 + 2] = (unsigned char)p;
        rgba[i * 4 + 3] = (unsigned char)(p >> 24);
    }
    NSData* png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    if( !png )
        return false;
    NSString* filename = [NSString stringWithFormat:@"%@-%u.png", key, revision];
    NSURL* target = [[g_mac_browser.rootURL URLByAppendingPathComponent:@"bitmap"]
        URLByAppendingPathComponent:filename];
    if( ![png writeToURL:target options:NSDataWritingAtomic error:nil] )
        return false;
    NSString* previous = g_mac_browser.bitmapFiles[key];
    if( previous && ![previous isEqualToString:filename] )
    {
        NSURL* old = [[g_mac_browser.rootURL URLByAppendingPathComponent:@"bitmap"]
            URLByAppendingPathComponent:previous];
        [[NSFileManager defaultManager] removeItemAtURL:old error:nil];
    }
    g_mac_browser.bitmapFiles[key] = filename;
    NSString* relative = [@"bitmap/" stringByAppendingString:filename];
    NSData* utf8 = [relative dataUsingEncoding:NSUTF8StringEncoding];
    if( !utf8 || utf8.length >= (NSUInteger)capacity )
        return false;
    memcpy(out_url, utf8.bytes, utf8.length);
    out_url[utf8.length] = '\0';
    return true;
}

void
PlatformMacPluginBrowser_SyncFrame(struct PlatformWindow* platform)
{
    if( g_mac_browser && g_mac_browser.platform == platform )
        [g_mac_browser syncFrame];
}

void
PlatformMacPluginBrowser_Reattach(struct PlatformWindow* platform)
{
    if( !g_mac_browser || g_mac_browser.platform != platform || !g_mac_browser.hostWindow )
        return;
    NSWindow* window = (__bridge NSWindow*)PlatformWindow_NativeWindowHandle(platform);
    NSWindow* parent = g_mac_browser.hostWindow.parentWindow;
    if( !window || parent == window || g_mac_browser.hostOffScreen )
        return;
    if( parent )
        [parent removeChildWindow:g_mac_browser.hostWindow];
    [window addChildWindow:g_mac_browser.hostWindow ordered:NSWindowAbove];
    [g_mac_browser.hostWindow orderFront:nil];
    g_mac_browser.lastScreenFrame = NSZeroRect;
    [g_mac_browser syncFrame];
}

void
PlatformMacPluginBrowser_Destroy(struct PlatformWindow* platform)
{
    if( !g_mac_browser || g_mac_browser.platform != platform )
        return;
    [g_mac_browser closePageWindow];
    WKUserContentController* controller =
        g_mac_browser.view.configuration.userContentController;
    [controller removeScriptMessageHandlerForName:@"torirsPluginChrome"];
    g_mac_browser.view.navigationDelegate = nil;
    g_mac_browser.view.UIDelegate = nil;
    [g_mac_browser.view stopLoading];
    if( g_mac_browser.hostWindow )
    {
        NSWindow* parent = g_mac_browser.hostWindow.parentWindow;
        if( parent )
            [parent removeChildWindow:g_mac_browser.hostWindow];
        [g_mac_browser.hostWindow orderOut:nil];
        g_mac_browser.hostWindow.contentView = nil;
        g_mac_browser.hostWindow = nil;
    }
    [g_mac_browser.view removeFromSuperview];
    [[NSFileManager defaultManager] removeItemAtURL:g_mac_browser.rootURL error:nil];
    g_mac_browser = nil;
}
