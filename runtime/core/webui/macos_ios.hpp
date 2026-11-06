#pragma once

#include "bridge.hpp"

#if defined(__APPLE__) && defined(__OBJC__)

@interface CfireWebUISchemeHandler : NSObject <WKURLSchemeHandler> {
@public
    std::shared_ptr<cfire::webui::detail::AppState> state;
    std::mutex active_tasks_mtx;
    NSMutableSet<id<WKURLSchemeTask>>* active_tasks;
}
@end

@implementation CfireWebUISchemeHandler
- (instancetype)init {
    self = [super init];
    if (self) {
        active_tasks = [[NSMutableSet alloc] init];
    }
    return self;
}

- (void)webView:(WKWebView*)webView startURLSchemeTask:(id<WKURLSchemeTask>)urlSchemeTask {
    (void)webView;
    {
        std::lock_guard<std::mutex> lock(active_tasks_mtx);
        [active_tasks addObject:urlSchemeTask];
    }
    NSURL* url = urlSchemeTask.request.URL;
    std::string path_str = url.path ? [url.path UTF8String] : "/index.html";
    if (path_str.empty() || path_str == "/") path_str = "/index.html";

    auto asset = state ? state->resolve_asset(path_str) : std::nullopt;
    NSInteger status_code = asset.has_value() ? 200 : 404;
    std::string body_str = asset.has_value() ? asset->content : "404 Not Found";
    std::string mime_str = asset.has_value() ? asset->mime : "text/plain; charset=utf-8";

    NSString* ns_mime = [NSString stringWithUTF8String:mime_str.c_str()];
    NSData* data = [NSData dataWithBytes:body_str.data() length:body_str.size()];
    NSDictionary* headers = @{
        @"Content-Type": ns_mime ? ns_mime : @"application/octet-stream",
        @"Content-Length": [NSString stringWithFormat:@"%lu", static_cast<unsigned long>(body_str.size())],
        @"Access-Control-Allow-Origin": @"*",
        @"Cache-Control": @"no-store"
    };

    NSHTTPURLResponse* response = [[NSHTTPURLResponse alloc] initWithURL:url
                                                              statusCode:status_code
                                                             HTTPVersion:@"HTTP/1.1"
                                                            headerFields:headers];
    {
        std::lock_guard<std::mutex> lock(active_tasks_mtx);
        if (![active_tasks containsObject:urlSchemeTask]) return;
        [urlSchemeTask didReceiveResponse:response];
        [urlSchemeTask didReceiveData:data];
        [urlSchemeTask didFinish];
        [active_tasks removeObject:urlSchemeTask];
    }
}

- (void)webView:(WKWebView*)webView stopURLSchemeTask:(id<WKURLSchemeTask>)urlSchemeTask {
    (void)webView;
    std::lock_guard<std::mutex> lock(active_tasks_mtx);
    [active_tasks removeObject:urlSchemeTask];
}
@end

@interface CfireWebUIMessageHandler : NSObject <WKScriptMessageHandler> {
@public
    std::shared_ptr<cfire::webui::detail::AppState> state;
}
@end

@implementation CfireWebUIMessageHandler
- (void)userContentController:(WKUserContentController*)userContentController
      didReceiveScriptMessage:(WKScriptMessage*)message {
    (void)userContentController;
    if (!state) return;
    if ([message.body isKindOfClass:[NSString class]]) {
        NSString* body = (NSString*)message.body;
        const char* utf8 = [body UTF8String];
        if (utf8) {
            state->handle_ipc_message(utf8);
        }
    }
}
@end

#if CFIRE_WEBUI_IOS

namespace cfire::webui::detail {
inline std::shared_ptr<AppState>& active_ios_state() {
    static std::shared_ptr<AppState> s;
    return s;
}
} // namespace cfire::webui::detail

@interface CfireWebUIAppDelegate : UIResponder <UIApplicationDelegate>
@property (strong, nonatomic) UIWindow* window;
@property (strong, nonatomic) WKWebView* webView;
@end

@implementation CfireWebUIAppDelegate
- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {
    (void)application;
    (void)launchOptions;
    auto st = cfire::webui::detail::active_ios_state();

    CGRect bounds = [[UIScreen mainScreen] bounds];
    self.window = [[UIWindow alloc] initWithFrame:bounds];
    UIViewController* rootVC = [[UIViewController alloc] init];
    rootVC.view.backgroundColor = [UIColor colorWithRed:0.05 green:0.07 blue:0.09 alpha:1.0];

    WKWebViewConfiguration* config = [[WKWebViewConfiguration alloc] init];
    WKUserContentController* ucm = [[WKUserContentController alloc] init];

    NSString* preloadSource = [NSString stringWithUTF8String:std::string(cfire::webui::detail::kPreloadBridgeJs).c_str()];
    WKUserScript* preloadScript = [[WKUserScript alloc] initWithSource:preloadSource
                                                         injectionTime:WKUserScriptInjectionTimeAtDocumentStart
                                                      forMainFrameOnly:YES];
    [ucm addUserScript:preloadScript];

    CfireWebUIMessageHandler* msgHandler = [[CfireWebUIMessageHandler alloc] init];
    msgHandler->state = st;
    [ucm addScriptMessageHandler:msgHandler name:@"external"];
    config.userContentController = ucm;

    CfireWebUISchemeHandler* schemeHandler = [[CfireWebUISchemeHandler alloc] init];
    schemeHandler->state = st;
    [config setURLSchemeHandler:schemeHandler forURLScheme:@"app"];

    self.webView = [[WKWebView alloc] initWithFrame:bounds configuration:config];
    self.webView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    self.webView.scrollView.contentInsetAdjustmentBehavior = UIScrollViewContentInsetAdjustmentNever;
    self.webView.opaque = NO;
    self.webView.backgroundColor = [UIColor clearColor];

    [rootVC.view addSubview:self.webView];
    self.window.rootViewController = rootVC;
    [self.window makeKeyAndVisible];

    if (st) {
        WKWebView* wv = self.webView;
        std::lock_guard<std::recursive_mutex> lock(st->mtx);
        st->platform_eval = [wv](const std::string& js) {
            NSString* ns_js = [NSString stringWithUTF8String:js.c_str()];
            if (!ns_js) return;
            dispatch_async(dispatch_get_main_queue(), ^{
                [wv evaluateJavaScript:ns_js completionHandler:nil];
            });
        };
    }

    NSURL* startURL = [NSURL URLWithString:@"app://localhost/index.html"];
    [self.webView loadRequest:[NSURLRequest requestWithURL:startURL]];
    return YES;
}
@end

#else // macOS Cocoa + WKWebView

@interface CfireWebUIWindowDelegate : NSObject <NSWindowDelegate> {
@public
    std::shared_ptr<cfire::webui::detail::AppState> state;
    bool stop_nsapp_on_close;
}
@end

@implementation CfireWebUIWindowDelegate
- (void)windowWillClose:(NSNotification*)notification {
    (void)notification;
    if (state) {
        state->quit_requested.store(true, std::memory_order_relaxed);
    }
    if (stop_nsapp_on_close) {
        [NSApp stop:nil];
        NSEvent* ev = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                         location:NSMakePoint(0, 0)
                                    modifierFlags:0
                                        timestamp:0
                                     windowNumber:0
                                          context:nil
                                          subtype:0
                                            data1:0
                                            data2:0];
        [NSApp postEvent:ev atStart:YES];
    }
}
@end

@interface CfireWebUIMenuTarget : NSObject {
@public
    std::shared_ptr<cfire::webui::detail::AppState> state;
}
- (void)handleCustomMenuItem:(NSMenuItem*)sender;
- (void)handleQuitMenuItem:(NSMenuItem*)sender;
@end

@implementation CfireWebUIMenuTarget
- (void)handleCustomMenuItem:(NSMenuItem*)sender {
    if (!state || !sender) return;
    NSString* rep = [sender representedObject];
    if (rep && [rep isKindOfClass:[NSString class]]) {
        std::string ev([rep UTF8String]);
        state->record_emitted(ev, "{}");
        state->dispatch_event_internal(ev, "{}");
        std::string ev_escaped = cfire::json::escape_string(ev);
        state->eval_js(std::format("window.__cfire_event_dispatch(\"{}\", {{}});", ev_escaped));
    }
}

- (void)handleQuitMenuItem:(NSMenuItem*)sender {
    (void)sender;
    NSWindow* keyWin = [NSApp keyWindow];
    if (keyWin) {
        [keyWin performClose:nil];
    } else {
        if (state) state->quit_requested.store(true, std::memory_order_relaxed);
        [NSApp stop:nil];
    }
}
@end

namespace cfire::webui::detail {

inline void apply_macos_unthrottled_prefs(WKWebViewConfiguration* config) {
    if (!config || !config.preferences) return;
    [config.preferences setValue:@YES forKey:@"developerExtrasEnabled"];
    // CF-050: Set WKPreferences.inactiveSchedulingPolicy = WKInactiveSchedulingPolicyNone (0) on macOS 14+
    // so timers (setTimeout/setInterval) and requestAnimationFrame run at full speed even when offscreen.
    @try {
        if ([config.preferences respondsToSelector:NSSelectorFromString(@"setInactiveSchedulingPolicy:")]) {
            [config.preferences setValue:@0 forKey:@"inactiveSchedulingPolicy"];
        }
    } @catch (NSException*) {
    }
}

[[nodiscard]] inline auto run_macos_webview_test(
    const std::shared_ptr<AppState>& state_,
    std::int32_t timeout_ms
) -> std::expected<bool, cfire::Error> {
    @autoreleasepool {
        [NSApplication sharedApplication];
        state_->quit_requested.store(false, std::memory_order_relaxed);

        // CF-050: Prevent App Nap & background timer throttling during headless WebView tests
        id activityToken = [[NSProcessInfo processInfo]
            beginActivityWithOptions:(NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical)
                              reason:@"cfire WebUI headless test runner"];

        NSRect frame = NSMakeRect(-2000, -2000, state_->width, state_->height);
        NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
                                                       styleMask:NSWindowStyleMaskBorderless
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        [window setReleasedWhenClosed:NO];

        WKWebViewConfiguration* config = [[WKWebViewConfiguration alloc] init];
        apply_macos_unthrottled_prefs(config);
        WKUserContentController* ucm = [[WKUserContentController alloc] init];

        NSString* preloadSource = [NSString stringWithUTF8String:std::string(detail::kPreloadBridgeJs).c_str()];
        WKUserScript* preloadScript = [[WKUserScript alloc] initWithSource:preloadSource
                                                             injectionTime:WKUserScriptInjectionTimeAtDocumentStart
                                                          forMainFrameOnly:YES];
        [ucm addUserScript:preloadScript];

        CfireWebUIMessageHandler* msgHandler = [[CfireWebUIMessageHandler alloc] init];
        msgHandler->state = state_;
        [ucm addScriptMessageHandler:msgHandler name:@"external"];
        config.userContentController = ucm;

        CfireWebUISchemeHandler* schemeHandler = [[CfireWebUISchemeHandler alloc] init];
        schemeHandler->state = state_;
        [config setURLSchemeHandler:schemeHandler forURLScheme:@"app"];

        WKWebView* webView = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, state_->width, state_->height)
                                                configuration:config];
        [window setContentView:webView];

        {
            std::lock_guard<std::recursive_mutex> lock(state_->mtx);
            state_->platform_eval = [webView](const std::string& js) {
                NSString* ns_js = [NSString stringWithUTF8String:js.c_str()];
                if (!ns_js) return;
                if ([NSThread isMainThread]) {
                    [webView evaluateJavaScript:ns_js completionHandler:nil];
                } else {
                    dispatch_async(dispatch_get_main_queue(), ^{
                        [webView evaluateJavaScript:ns_js completionHandler:nil];
                    });
                }
            };
            state_->platform_set_title = [window](const std::string& new_title) {
                NSString* ns_t = [NSString stringWithUTF8String:new_title.c_str()];
                if (ns_t) [window setTitle:ns_t];
            };
            state_->platform_quit = [st = state_]() {
                st->quit_requested.store(true, std::memory_order_relaxed);
            };
            state_->platform_screenshot = [webView](const std::string& out_path, std::function<void(bool)> done) {
                NSString* ns_path = [NSString stringWithUTF8String:out_path.c_str()];
                WKSnapshotConfiguration* snapCfg = [[WKSnapshotConfiguration alloc] init];
                [webView takeSnapshotWithConfiguration:snapCfg completionHandler:^(NSImage* image, NSError* error) {
                    bool ok = false;
                    if (image && !error && ns_path) {
                        CGImageRef cgRef = [image CGImageForProposedRect:NULL context:nil hints:nil];
                        if (cgRef) {
                            NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithCGImage:cgRef];
                            NSData* pngData = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
                            if (pngData) {
                                ok = [pngData writeToFile:ns_path atomically:YES];
                            }
                        }
                    }
                    if (done) done(ok);
                }];
            };
        }

        [window orderBack:nil];
        NSURL* startURL = [NSURL URLWithString:@"app://localhost/index.html"];
        [webView loadRequest:[NSURLRequest requestWithURL:startURL]];

        auto start = std::chrono::steady_clock::now();
        while (!state_->quit_requested.load(std::memory_order_relaxed)) {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true);
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= timeout_ms) {
                break;
            }
        }

        [ucm removeScriptMessageHandlerForName:@"external"];
        {
            std::lock_guard<std::recursive_mutex> lock(state_->mtx);
            state_->platform_eval = nullptr;
            state_->platform_set_title = nullptr;
            state_->platform_quit = nullptr;
            state_->platform_screenshot = nullptr;
        }
        [window close];
        if (activityToken) {
            [[NSProcessInfo processInfo] endActivity:activityToken];
        }
        return state_->quit_requested.load(std::memory_order_relaxed);
    }
}

inline void run_macos_app(const std::shared_ptr<AppState>& state_) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        // CF-050: Prevent App Nap / background timer throttling for active terminal/SSH/audio sessions
        id activityToken = [[NSProcessInfo processInfo]
            beginActivityWithOptions:(NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical)
                              reason:@"cfire WebUI active session"];

        // CF-052: Set application icon if configured via build.cf or app.set_icon()
        if (!state_->icon_path.empty()) {
            NSString* nsIconPath = [NSString stringWithUTF8String:state_->icon_path.c_str()];
            if (nsIconPath) {
                NSImage* iconImg = [[NSImage alloc] initWithContentsOfFile:nsIconPath];
                if (iconImg) {
                    [NSApp setApplicationIconImage:iconImg];
                }
            }
        }

        CfireWebUIMenuTarget* menuTarget = [[CfireWebUIMenuTarget alloc] init];
        menuTarget->state = state_;

        // CF-051: Install standard macOS Application, Edit, Window, and custom menus
        if ([NSApp mainMenu] == nil) {
            NSMenu* mainBar = [[NSMenu alloc] init];

            NSMenuItem* appMenuItem = [[NSMenuItem alloc] init];
            [mainBar addItem:appMenuItem];
            NSMenu* appMenu = [[NSMenu alloc] init];
            NSString* appName = [NSString stringWithUTF8String:state_->title.c_str()];
            [appMenu addItemWithTitle:[NSString stringWithFormat:@"Hide %@", appName]
                               action:@selector(hide:)
                        keyEquivalent:@"h"];
            [appMenu addItem:[NSMenuItem separatorItem]];
            NSMenuItem* quitItem = [[NSMenuItem alloc]
                initWithTitle:[NSString stringWithFormat:@"Quit %@", appName]
                       action:@selector(handleQuitMenuItem:)
                keyEquivalent:@"q"];
            [quitItem setTarget:menuTarget];
            [appMenu addItem:quitItem];
            [appMenuItem setSubmenu:appMenu];

            NSMenuItem* editMenuItem = [[NSMenuItem alloc] init];
            [mainBar addItem:editMenuItem];
            NSMenu* editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
            [editMenu addItemWithTitle:@"Undo" action:NSSelectorFromString(@"undo:") keyEquivalent:@"z"];
            [editMenu addItemWithTitle:@"Redo" action:NSSelectorFromString(@"redo:") keyEquivalent:@"Z"];
            [editMenu addItem:[NSMenuItem separatorItem]];
            [editMenu addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"];
            [editMenu addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
            [editMenu addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
            [editMenu addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
            [editMenuItem setSubmenu:editMenu];

            // Attach any custom menu items registered via app.add_menu_item(...)
            std::map<std::string, NSMenu*> customSubmenus;
            customSubmenus["App"] = appMenu;
            customSubmenus["Application"] = appMenu;
            customSubmenus["Edit"] = editMenu;

            NSMenuItem* winMenuItem = [[NSMenuItem alloc] init];
            NSMenu* winMenu = [[NSMenu alloc] initWithTitle:@"Window"];
            [winMenu addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
            [winMenu addItemWithTitle:@"Close Window" action:@selector(performClose:) keyEquivalent:@"w"];
            customSubmenus["Window"] = winMenu;

            {
                std::lock_guard<std::recursive_mutex> lock(state_->mtx);
                for (const auto& item : state_->custom_menus) {
                    std::string menuName = item.menu.empty() ? "App" : item.menu;
                    NSMenu* targetMenu = nullptr;
                    auto it = customSubmenus.find(menuName);
                    if (it != customSubmenus.end()) {
                        targetMenu = it->second;
                    } else {
                        NSMenuItem* topItem = [[NSMenuItem alloc] init];
                        [mainBar addItem:topItem];
                        targetMenu = [[NSMenu alloc] initWithTitle:[NSString stringWithUTF8String:menuName.c_str()]];
                        [topItem setSubmenu:targetMenu];
                        customSubmenus[menuName] = targetMenu;
                    }
                    NSString* nsTitle = [NSString stringWithUTF8String:item.title.c_str()];
                    NSString* nsKey = [NSString stringWithUTF8String:item.shortcut.c_str()];
                    NSMenuItem* mi = [[NSMenuItem alloc]
                        initWithTitle:(nsTitle ? nsTitle : @"Item")
                               action:@selector(handleCustomMenuItem:)
                        keyEquivalent:(nsKey ? nsKey : @"")];
                    [mi setTarget:menuTarget];
                    [mi setRepresentedObject:[NSString stringWithUTF8String:item.event.c_str()]];
                    [targetMenu addItem:mi];
                }
            }

            [mainBar addItem:winMenuItem];
            [winMenuItem setSubmenu:winMenu];

            [NSApp setMainMenu:mainBar];
        }

        NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                  NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
        if (state_->frameless) {
            style |= NSWindowStyleMaskFullSizeContentView;
        }

        NSRect frame = NSMakeRect(0, 0, state_->width, state_->height);
        NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
                                                       styleMask:style
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        [window setReleasedWhenClosed:NO];
        [window setTitle:[NSString stringWithUTF8String:state_->title.c_str()]];
        [window center];

        if (state_->min_width > 0 && state_->min_height > 0) {
            [window setContentMinSize:NSMakeSize(state_->min_width, state_->min_height)];
        }
        if (state_->max_width > 0 && state_->max_height > 0) {
            [window setContentMaxSize:NSMakeSize(state_->max_width, state_->max_height)];
        }
        if (state_->dark_mode) {
            [window setAppearance:[NSAppearance appearanceNamed:NSAppearanceNameDarkAqua]];
        } else {
            [window setAppearance:[NSAppearance appearanceNamed:NSAppearanceNameAqua]];
        }
        if (state_->frameless) {
            [window setTitlebarAppearsTransparent:YES];
            [window setTitleVisibility:NSWindowTitleHidden];
        }
        if (state_->transparent) {
            [window setOpaque:NO];
            [window setBackgroundColor:[NSColor clearColor]];
        } else {
            [window setBackgroundColor:[NSColor colorWithSRGBRed:0.05 green:0.07 blue:0.09 alpha:1.0]];
        }
        if (state_->always_on_top) {
            [window setLevel:NSFloatingWindowLevel];
        }

        CfireWebUIWindowDelegate* winDelegate = [[CfireWebUIWindowDelegate alloc] init];
        winDelegate->state = state_;
        winDelegate->stop_nsapp_on_close = true;
        [window setDelegate:winDelegate];

        WKWebViewConfiguration* config = [[WKWebViewConfiguration alloc] init];
        apply_macos_unthrottled_prefs(config);

        WKUserContentController* ucm = [[WKUserContentController alloc] init];
        NSString* preloadSource = [NSString stringWithUTF8String:std::string(detail::kPreloadBridgeJs).c_str()];
        WKUserScript* preloadScript = [[WKUserScript alloc] initWithSource:preloadSource
                                                             injectionTime:WKUserScriptInjectionTimeAtDocumentStart
                                                          forMainFrameOnly:YES];
        [ucm addUserScript:preloadScript];

        CfireWebUIMessageHandler* msgHandler = [[CfireWebUIMessageHandler alloc] init];
        msgHandler->state = state_;
        [ucm addScriptMessageHandler:msgHandler name:@"external"];
        config.userContentController = ucm;

        CfireWebUISchemeHandler* schemeHandler = [[CfireWebUISchemeHandler alloc] init];
        schemeHandler->state = state_;
        [config setURLSchemeHandler:schemeHandler forURLScheme:@"app"];

        WKWebView* webView = [[WKWebView alloc] initWithFrame:[[window contentView] bounds]
                                                configuration:config];
        [webView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
        if (state_->transparent) {
            [webView setValue:@NO forKey:@"drawsBackground"];
        }
        [window setContentView:webView];

        {
            std::lock_guard<std::recursive_mutex> lock(state_->mtx);
            state_->platform_eval = [webView](const std::string& js) {
                NSString* ns_js = [NSString stringWithUTF8String:js.c_str()];
                if (!ns_js) return;
                dispatch_async(dispatch_get_main_queue(), ^{
                    [webView evaluateJavaScript:ns_js completionHandler:nil];
                });
            };
            state_->platform_set_title = [window](const std::string& new_title) {
                NSString* ns_t = [NSString stringWithUTF8String:new_title.c_str()];
                if (!ns_t) return;
                dispatch_async(dispatch_get_main_queue(), ^{
                    [window setTitle:ns_t];
                });
            };
            state_->platform_set_size = [window](std::int32_t w, std::int32_t h) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    [window setContentSize:NSMakeSize(w, h)];
                });
            };
            state_->platform_fullscreen = [window]() {
                dispatch_async(dispatch_get_main_queue(), ^{
                    [window toggleFullScreen:nil];
                });
            };
            state_->platform_minimize = [window]() {
                dispatch_async(dispatch_get_main_queue(), ^{
                    [window miniaturize:nil];
                });
            };
            state_->platform_maximize = [window]() {
                dispatch_async(dispatch_get_main_queue(), ^{
                    [window zoom:nil];
                });
            };
            state_->platform_center = [window]() {
                dispatch_async(dispatch_get_main_queue(), ^{
                    [window center];
                });
            };
            state_->platform_drag = [window]() {
                dispatch_async(dispatch_get_main_queue(), ^{
                    NSEvent* ev = [NSApp currentEvent];
                    if (ev) [window performWindowDragWithEvent:ev];
                });
            };
            state_->platform_quit = [window]() {
                dispatch_async(dispatch_get_main_queue(), ^{
                    [window close];
                    [NSApp stop:nil];
                });
            };
            state_->platform_screenshot = [webView](const std::string& out_path, std::function<void(bool)> done) {
                NSString* ns_path = [NSString stringWithUTF8String:out_path.c_str()];
                dispatch_async(dispatch_get_main_queue(), ^{
                    WKSnapshotConfiguration* snapCfg = [[WKSnapshotConfiguration alloc] init];
                    [webView takeSnapshotWithConfiguration:snapCfg completionHandler:^(NSImage* image, NSError* error) {
                        bool ok = false;
                        if (image && !error && ns_path) {
                            CGImageRef cgRef = [image CGImageForProposedRect:NULL context:nil hints:nil];
                            if (cgRef) {
                                NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithCGImage:cgRef];
                                NSData* pngData = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
                                if (pngData) {
                                    ok = [pngData writeToFile:ns_path atomically:YES];
                                }
                            }
                        }
                        if (done) done(ok);
                    }];
                });
            };
        }

        NSURL* startURL = [NSURL URLWithString:@"app://localhost/index.html"];
        [webView loadRequest:[NSURLRequest requestWithURL:startURL]];

        [window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        [NSApp run];

        [ucm removeScriptMessageHandlerForName:@"external"];
        {
            std::lock_guard<std::recursive_mutex> lock(state_->mtx);
            state_->platform_eval = nullptr;
            state_->platform_set_title = nullptr;
            state_->platform_set_size = nullptr;
            state_->platform_fullscreen = nullptr;
            state_->platform_minimize = nullptr;
            state_->platform_maximize = nullptr;
            state_->platform_center = nullptr;
            state_->platform_drag = nullptr;
            state_->platform_quit = nullptr;
        }
        if (activityToken) {
            [[NSProcessInfo processInfo] endActivity:activityToken];
        }
    }
}

} // namespace cfire::webui::detail

#endif // PLATFORM_IOS vs macOS
#endif // defined(__APPLE__) && defined(__OBJC__)
