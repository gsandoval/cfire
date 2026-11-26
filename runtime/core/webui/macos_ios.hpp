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
    if (!state || !message) return;
    WKFrameInfo* frameInfo = message.frameInfo;
    if (!frameInfo || !frameInfo.isMainFrame) return;
    NSURL* reqUrl = frameInfo.request.URL;
    NSString* scheme = reqUrl.scheme ? [reqUrl.scheme lowercaseString] : nil;
    NSString* host = reqUrl.host ? [reqUrl.host lowercaseString] : nil;
    if (![scheme isEqualToString:@"app"] || ![host isEqualToString:@"localhost"]) {
        return;
    }
    NSString* urlStr = [reqUrl absoluteString];
    const char* urlUtf8 = urlStr ? [urlStr UTF8String] : nullptr;
    if (!urlUtf8) return;
    if ([message.body isKindOfClass:[NSString class]]) {
        NSString* body = (NSString*)message.body;
        const char* utf8 = [body UTF8String];
        if (utf8) {
            state->handle_ipc_message_verified(urlUtf8, utf8, false);
        }
    }
}
@end

@interface CfireWebUINavigationDelegate : NSObject <WKNavigationDelegate, WKUIDelegate> {
@public
    std::shared_ptr<cfire::webui::detail::AppState> state;
#if CFIRE_WEBUI_IOS
    __weak UIViewController* presentingVC;
#endif
}
- (void)openExternalURL:(NSURL*)url;
@end

@implementation CfireWebUINavigationDelegate
- (void)openExternalURL:(NSURL*)url {
    if (!url) return;
    NSString* urlStr = [url absoluteString];
    const char* urlUtf8 = urlStr ? [urlStr UTF8String] : nullptr;
    if (state && urlUtf8) {
        state->notify_external_navigation(urlUtf8);
    }
#if CFIRE_WEBUI_IOS
    NSString* scheme = url.scheme ? [url.scheme lowercaseString] : @"";
    if ([scheme isEqualToString:@"http"] || [scheme isEqualToString:@"https"]) {
        UIViewController* vc = presentingVC;
        if (vc && !vc.presentedViewController) {
            SFSafariViewController* safari = [[SFSafariViewController alloc] initWithURL:url];
            [vc presentViewController:safari animated:YES completion:nil];
            return;
        }
    }
    [[UIApplication sharedApplication] openURL:url options:@{} completionHandler:nil];
#else
    if (!cfire::webui::detail::is_headless_webui_env()) {
        [[NSWorkspace sharedWorkspace] openURL:url];
    }
#endif
}

- (void)webView:(WKWebView*)webView
decidePolicyForNavigationAction:(WKNavigationAction*)navigationAction
decisionHandler:(void (^)(WKNavigationActionPolicy))decisionHandler {
    NSURL* url = navigationAction.request.URL;
    NSString* urlStr = url ? [url absoluteString] : nil;
    if (!urlStr) {
        decisionHandler(WKNavigationActionPolicyAllow);
        return;
    }
    const char* urlUtf8 = [urlStr UTF8String];
    std::string_view sv = urlUtf8 ? std::string_view(urlUtf8) : std::string_view{};
    bool allowed = state ? state->should_allow_in_webview(sv) : cfire::webui::detail::is_app_origin(sv);
    if (navigationAction.targetFrame != nil && !navigationAction.targetFrame.isMainFrame) {
        decisionHandler(allowed ? WKNavigationActionPolicyAllow : WKNavigationActionPolicyCancel);
        return;
    }
    if (allowed) {
        if (navigationAction.targetFrame == nil) {
            [webView loadRequest:navigationAction.request];
            decisionHandler(WKNavigationActionPolicyCancel);
            return;
        }
        decisionHandler(WKNavigationActionPolicyAllow);
        return;
    }
    [self openExternalURL:url];
    decisionHandler(WKNavigationActionPolicyCancel);
}

- (WKWebView*)webView:(WKWebView*)webView
createWebViewWithConfiguration:(WKWebViewConfiguration*)configuration
  forNavigationAction:(WKNavigationAction*)navigationAction
       windowFeatures:(WKWindowFeatures*)windowFeatures {
    (void)configuration;
    (void)windowFeatures;
    NSURL* url = navigationAction.request.URL;
    NSString* urlStr = url ? [url absoluteString] : nil;
    if (url && urlStr) {
        const char* urlUtf8 = [urlStr UTF8String];
        std::string_view sv = urlUtf8 ? std::string_view(urlUtf8) : std::string_view{};
        bool allowed = state ? state->should_allow_in_webview(sv) : cfire::webui::detail::is_app_origin(sv);
        if (allowed) {
            [webView loadRequest:navigationAction.request];
        } else {
            [self openExternalURL:url];
        }
    }
    return nil;
}

- (void)webViewWebContentProcessDidTerminate:(WKWebView*)webView {
    NSURL* cur = webView.URL;
    NSString* curStr = cur ? [cur absoluteString] : nil;
    const char* curUtf8 = curStr ? [curStr UTF8String] : nullptr;
    if (curUtf8 && cfire::webui::detail::is_app_origin(curUtf8)) {
        [webView reload];
    } else {
        NSURL* home = [NSURL URLWithString:@"app://localhost/index.html"];
        [webView loadRequest:[NSURLRequest requestWithURL:home]];
    }
    if (state) {
        state->emit_system_event("app:webview_reloaded", "{}");
    }
}
@end

#if CFIRE_WEBUI_IOS

namespace cfire::webui::detail {
inline std::shared_ptr<AppState>& active_ios_state() {
    static std::shared_ptr<AppState> s;
    return s;
}

inline void dispatch_ios_open_url(const std::shared_ptr<AppState>& st, NSURL* url) {
    if (!st || !url) return;
    NSString* urlStr = [url absoluteString];
    const char* utf8 = urlStr ? [urlStr UTF8String] : nullptr;
    if (utf8 && utf8[0] != '\0') {
        st->notify_open_url(utf8);
    }
}

inline void dispatch_ios_user_activity(const std::shared_ptr<AppState>& st, NSUserActivity* activity) {
    if (!st || !activity) return;
    if ([activity.activityType isEqualToString:NSUserActivityTypeBrowsingWeb] && activity.webpageURL) {
        dispatch_ios_open_url(st, activity.webpageURL);
    }
}
} // namespace cfire::webui::detail

@interface CfireWebUIViewController : UIViewController {
@public
    std::shared_ptr<cfire::webui::detail::AppState> state;
}
@end

@implementation CfireWebUIViewController
- (UIStatusBarStyle)preferredStatusBarStyle {
    if (!state) return UIStatusBarStyleDefault;
    cfire::webui::StatusBarStyle s;
    {
        std::lock_guard<std::recursive_mutex> lock(state->mtx);
        s = state->status_bar_style;
    }
    if (s == cfire::webui::StatusBarStyle::Light) return UIStatusBarStyleLightContent;
    if (s == cfire::webui::StatusBarStyle::Dark) {
        if (@available(iOS 13.0, *)) return UIStatusBarStyleDarkContent;
    }
    return UIStatusBarStyleDefault;
}

- (BOOL)prefersStatusBarHidden {
    if (!state) return NO;
    std::lock_guard<std::recursive_mutex> lock(state->mtx);
    return state->status_bar_style == cfire::webui::StatusBarStyle::Hidden ? YES : NO;
}
@end

@interface CfireWebUINotificationDelegate : NSObject <UNUserNotificationCenterDelegate> {
@public
    std::shared_ptr<cfire::webui::detail::AppState> state;
}
@end

@implementation CfireWebUINotificationDelegate
- (void)userNotificationCenter:(UNUserNotificationCenter*)center
       willPresentNotification:(UNNotification*)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions options))completionHandler {
    (void)center;
    (void)notification;
#if defined(__IPHONE_OS_VERSION_MIN_REQUIRED) && __IPHONE_OS_VERSION_MIN_REQUIRED >= 140000
    completionHandler(UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionList |
                      UNNotificationPresentationOptionSound | UNNotificationPresentationOptionBadge);
#else
    completionHandler(UNNotificationPresentationOptionSound | UNNotificationPresentationOptionBadge);
#endif
}

- (void)userNotificationCenter:(UNUserNotificationCenter*)center
didReceiveNotificationResponse:(UNNotificationResponse*)response
         withCompletionHandler:(void (^)(void))completionHandler {
    (void)center;
    if (state && response && response.notification && response.notification.request) {
        NSString* reqId = response.notification.request.identifier;
        NSDictionary* userInfo = response.notification.request.content.userInfo;
        std::string idStr = (reqId && [reqId UTF8String]) ? [reqId UTF8String] : "";
        std::string payloadStr{};
        if (userInfo && [NSJSONSerialization isValidJSONObject:userInfo]) {
            NSData* jsonData = [NSJSONSerialization dataWithJSONObject:userInfo options:0 error:nil];
            if (jsonData) {
                NSString* jsonStr = [[NSString alloc] initWithData:jsonData encoding:NSUTF8StringEncoding];
                if (jsonStr && [jsonStr UTF8String]) {
                    payloadStr = [jsonStr UTF8String];
                }
            }
        }
        state->notify_notification_opened(idStr, payloadStr);
    }
    if (completionHandler) completionHandler();
}
@end

namespace cfire::webui::detail {

struct IosMountedWindow {
    UIWindow* window{nil};
    WKWebView* webView{nil};
    CfireWebUIViewController* rootVC{nil};
    CfireWebUINavigationDelegate* navDelegate{nil};
    id keyboardShowObserver{nil};
    id keyboardHideObserver{nil};
};

[[nodiscard]] inline auto ios_mount_webview_in_window(
    const std::shared_ptr<AppState>& st,
    UIWindow* window
) -> IosMountedWindow {
    IosMountedWindow mounted;
    mounted.window = window;

    CfireWebUIViewController* rootVC = [[CfireWebUIViewController alloc] init];
    rootVC->state = st;
    rootVC.view.backgroundColor = [UIColor colorWithRed:0.05 green:0.07 blue:0.09 alpha:1.0];
    mounted.rootVC = rootVC;

    WKWebViewConfiguration* config = [[WKWebViewConfiguration alloc] init];
    config.allowsInlineMediaPlayback = YES;
    config.mediaTypesRequiringUserActionForPlayback = WKAudiovisualMediaTypeNone;
    WKUserContentController* ucm = [[WKUserContentController alloc] init];

    std::string preload_cpp = st
        ? cfire::webui::detail::build_preload_script(*st)
        : std::string(cfire::webui::detail::kPreloadBridgeJs);
    NSString* preloadSource = [NSString stringWithUTF8String:preload_cpp.c_str()];
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

    bool dev_enabled = cfire::webui::detail::kDefaultWebUIDevExtras;
    bool ctx_enabled = cfire::webui::detail::kDefaultWebUIDevExtras;
    bool bounce_enabled = true;
    if (st) {
        std::lock_guard<std::recursive_mutex> lock(st->mtx);
        dev_enabled = st->devtools_enabled;
        ctx_enabled = st->context_menu_enabled;
        bounce_enabled = st->scroll_bounce_enabled;
    }
    [config.preferences setValue:(dev_enabled ? @YES : @NO) forKey:@"developerExtrasEnabled"];

    WKWebView* webView = [[WKWebView alloc] initWithFrame:rootVC.view.bounds configuration:config];
    webView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    webView.scrollView.contentInsetAdjustmentBehavior = UIScrollViewContentInsetAdjustmentNever;
    webView.scrollView.bounces = bounce_enabled ? YES : NO;
    webView.opaque = NO;
    webView.backgroundColor = [UIColor clearColor];
    if (@available(iOS 16.4, *)) {
        if ([webView respondsToSelector:@selector(setInspectable:)]) {
            webView.inspectable = dev_enabled ? YES : NO;
        }
    }
    if ([webView respondsToSelector:@selector(setAllowsLinkPreview:)]) {
        webView.allowsLinkPreview = ctx_enabled ? YES : NO;
    }

    CfireWebUINavigationDelegate* navDelegate = [[CfireWebUINavigationDelegate alloc] init];
    navDelegate->state = st;
    navDelegate->presentingVC = rootVC;
    webView.navigationDelegate = navDelegate;
    webView.UIDelegate = navDelegate;
    mounted.navDelegate = navDelegate;
    mounted.webView = webView;

    [rootVC.view addSubview:webView];
    window.rootViewController = rootVC;
    [window makeKeyAndVisible];

    if (st) {
        {
            std::lock_guard<std::recursive_mutex> lock(st->mtx);
            st->native_window_handle = (__bridge void*)window;
            st->native_webview_handle = (__bridge void*)webView;
            st->native_view_controller_handle = (__bridge void*)rootVC;
            st->platform_eval = [webView](const std::string& js) {
                NSString* ns_js = [NSString stringWithUTF8String:js.c_str()];
                if (!ns_js) return;
                dispatch_async(dispatch_get_main_queue(), ^{
                    [webView evaluateJavaScript:ns_js completionHandler:nil];
                });
            };
            st->platform_set_devtools = [webView, config](bool enabled) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    [config.preferences setValue:(enabled ? @YES : @NO) forKey:@"developerExtrasEnabled"];
                    if (@available(iOS 16.4, *)) {
                        if ([webView respondsToSelector:@selector(setInspectable:)]) {
                            webView.inspectable = enabled ? YES : NO;
                        }
                    }
                });
            };
            st->platform_set_context_menu = [webView](bool enabled) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    if ([webView respondsToSelector:@selector(setAllowsLinkPreview:)]) {
                        webView.allowsLinkPreview = enabled ? YES : NO;
                    }
                });
            };
            st->platform_set_autofill = [webView](bool enabled) {
                NSString* ns_js = enabled
                    ? @"if(window.__cfire_set_autofill)window.__cfire_set_autofill(true);"
                    : @"if(window.__cfire_set_autofill)window.__cfire_set_autofill(false);";
                dispatch_async(dispatch_get_main_queue(), ^{
                    [webView evaluateJavaScript:ns_js completionHandler:nil];
                });
            };
            st->platform_set_scroll_bounce = [webView](bool enabled) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    webView.scrollView.bounces = enabled ? YES : NO;
                });
            };
            st->platform_set_status_bar_style = [rootVC](StatusBarStyle) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    [rootVC setNeedsStatusBarAppearanceUpdate];
                });
            };
        }

        std::weak_ptr<AppState> weakSt = st;
        NSNotificationCenter* nc = [NSNotificationCenter defaultCenter];
        mounted.keyboardShowObserver = [nc addObserverForName:UIKeyboardWillShowNotification
                                                       object:nil
                                                        queue:[NSOperationQueue mainQueue]
                                                   usingBlock:^(NSNotification* note) {
            auto s = weakSt.lock();
            if (!s) return;
            NSValue* val = note.userInfo[UIKeyboardFrameEndUserInfoKey];
            long height_px = 0;
            if (val) {
                CGRect r = [val CGRectValue];
                if (r.size.height > 0) height_px = static_cast<long>(r.size.height + 0.5);
            }
            s->emit_system_event("keyboard:change",
                std::format("{{\"visible\":true,\"height\":{}}}", height_px));
        }];
        mounted.keyboardHideObserver = [nc addObserverForName:UIKeyboardWillHideNotification
                                                       object:nil
                                                        queue:[NSOperationQueue mainQueue]
                                                   usingBlock:^(NSNotification* note) {
            (void)note;
            auto s = weakSt.lock();
            if (!s) return;
            s->emit_system_event("keyboard:change", "{\"visible\":false,\"height\":0}");
        }];
    }

    NSURL* startURL = [NSURL URLWithString:@"app://localhost/index.html"];
    [webView loadRequest:[NSURLRequest requestWithURL:startURL]];
    return mounted;
}

inline void ios_unmount_window(const std::shared_ptr<AppState>& st, IosMountedWindow& mounted) {
    NSNotificationCenter* nc = [NSNotificationCenter defaultCenter];
    if (mounted.keyboardShowObserver) {
        [nc removeObserver:mounted.keyboardShowObserver];
        mounted.keyboardShowObserver = nil;
    }
    if (mounted.keyboardHideObserver) {
        [nc removeObserver:mounted.keyboardHideObserver];
        mounted.keyboardHideObserver = nil;
    }
    if (mounted.webView) {
        mounted.webView.navigationDelegate = nil;
        mounted.webView.UIDelegate = nil;
        [mounted.webView.configuration.userContentController removeScriptMessageHandlerForName:@"external"];
        mounted.webView = nil;
    }
    mounted.navDelegate = nil;
    mounted.rootVC = nil;
    mounted.window = nil;
    if (st) {
        std::lock_guard<std::recursive_mutex> lock(st->mtx);
        st->native_window_handle = nullptr;
        st->native_webview_handle = nullptr;
        st->native_view_controller_handle = nullptr;
        st->platform_eval = nullptr;
        st->platform_set_devtools = nullptr;
        st->platform_set_context_menu = nullptr;
        st->platform_set_autofill = nullptr;
        st->platform_set_scroll_bounce = nullptr;
        st->platform_set_status_bar_style = nullptr;
    }
}

} // namespace cfire::webui::detail

@interface CfireWebUISceneDelegate : UIResponder <UIWindowSceneDelegate> {
@public
    cfire::webui::detail::IosMountedWindow mounted;
}
@property (strong, nonatomic) UIWindow* window;
@end

@implementation CfireWebUISceneDelegate
- (void)scene:(UIScene*)scene
willConnectToSession:(UISceneSession*)session
      options:(UISceneConnectionOptions*)connectionOptions {
    (void)session;
    if (![scene isKindOfClass:[UIWindowScene class]]) return;
    UIWindowScene* windowScene = (UIWindowScene*)scene;
    auto st = cfire::webui::detail::active_ios_state();

    for (UIOpenURLContext* ctx in connectionOptions.URLContexts) {
        cfire::webui::detail::dispatch_ios_open_url(st, ctx.URL);
    }
    for (NSUserActivity* activity in connectionOptions.userActivities) {
        cfire::webui::detail::dispatch_ios_user_activity(st, activity);
    }

    UIWindow* win = [[UIWindow alloc] initWithWindowScene:windowScene];
    self.window = win;
    mounted = cfire::webui::detail::ios_mount_webview_in_window(st, win);
}

- (void)scene:(UIScene*)scene openURLContexts:(NSSet<UIOpenURLContext*>*)URLContexts {
    (void)scene;
    auto st = cfire::webui::detail::active_ios_state();
    for (UIOpenURLContext* ctx in URLContexts) {
        cfire::webui::detail::dispatch_ios_open_url(st, ctx.URL);
    }
}

- (void)scene:(UIScene*)scene continueUserActivity:(NSUserActivity*)userActivity {
    (void)scene;
    auto st = cfire::webui::detail::active_ios_state();
    cfire::webui::detail::dispatch_ios_user_activity(st, userActivity);
}

- (void)sceneDidBecomeActive:(UIScene*)scene {
    (void)scene;
    auto st = cfire::webui::detail::active_ios_state();
    if (st) st->emit_system_event("app:active", "{}");
}

- (void)sceneWillResignActive:(UIScene*)scene {
    (void)scene;
    auto st = cfire::webui::detail::active_ios_state();
    if (st) st->emit_system_event("app:inactive", "{}");
}

- (void)sceneWillEnterForeground:(UIScene*)scene {
    (void)scene;
    auto st = cfire::webui::detail::active_ios_state();
    if (st) st->emit_system_event("app:foreground", "{}");
}

- (void)sceneDidEnterBackground:(UIScene*)scene {
    (void)scene;
    auto st = cfire::webui::detail::active_ios_state();
    if (st) st->emit_system_event("app:background", "{}");
}

- (void)sceneDidDisconnect:(UIScene*)scene {
    (void)scene;
    auto st = cfire::webui::detail::active_ios_state();
    cfire::webui::detail::ios_unmount_window(st, mounted);
}
@end

@interface CfireWebUIAppDelegate : UIResponder <UIApplicationDelegate> {
@public
    cfire::webui::detail::IosMountedWindow fallbackMounted;
    CfireWebUINotificationDelegate* notificationDelegate;
    bool usingScenes;
}
@property (strong, nonatomic) UIWindow* window;
@property (strong, nonatomic) WKWebView* webView;
@end

@implementation CfireWebUIAppDelegate
- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {
    (void)application;
    auto st = cfire::webui::detail::active_ios_state();

    notificationDelegate = [[CfireWebUINotificationDelegate alloc] init];
    notificationDelegate->state = st;
    [UNUserNotificationCenter currentNotificationCenter].delegate = notificationDelegate;

    if (launchOptions) {
        NSURL* launchUrl = launchOptions[UIApplicationLaunchOptionsURLKey];
        if ([launchUrl isKindOfClass:[NSURL class]]) {
            cfire::webui::detail::dispatch_ios_open_url(st, launchUrl);
        }
    }

    NSDictionary* sceneManifest = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"UIApplicationSceneManifest"];
    usingScenes = (sceneManifest != nil);
    if (!usingScenes) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        CGRect bounds = [[UIScreen mainScreen] bounds];
#pragma clang diagnostic pop
        UIWindow* win = [[UIWindow alloc] initWithFrame:bounds];
        self.window = win;
        fallbackMounted = cfire::webui::detail::ios_mount_webview_in_window(st, win);
        self.webView = fallbackMounted.webView;
    }
    return YES;
}

- (BOOL)application:(UIApplication*)app
            openURL:(NSURL*)url
            options:(NSDictionary<UIApplicationOpenURLOptionsKey, id>*)options {
    (void)app;
    (void)options;
    auto st = cfire::webui::detail::active_ios_state();
    cfire::webui::detail::dispatch_ios_open_url(st, url);
    return YES;
}

- (BOOL)application:(UIApplication*)application
continueUserActivity:(NSUserActivity*)userActivity
 restorationHandler:(void (^)(NSArray<id<UIUserActivityRestoring>>*))restorationHandler {
    (void)application;
    (void)restorationHandler;
    auto st = cfire::webui::detail::active_ios_state();
    cfire::webui::detail::dispatch_ios_user_activity(st, userActivity);
    return YES;
}

- (void)applicationDidBecomeActive:(UIApplication*)application {
    (void)application;
    if (usingScenes) return;
    auto st = cfire::webui::detail::active_ios_state();
    if (st) st->emit_system_event("app:active", "{}");
}

- (void)applicationWillResignActive:(UIApplication*)application {
    (void)application;
    if (usingScenes) return;
    auto st = cfire::webui::detail::active_ios_state();
    if (st) st->emit_system_event("app:inactive", "{}");
}

- (void)applicationWillEnterForeground:(UIApplication*)application {
    (void)application;
    if (usingScenes) return;
    auto st = cfire::webui::detail::active_ios_state();
    if (st) st->emit_system_event("app:foreground", "{}");
}

- (void)applicationDidEnterBackground:(UIApplication*)application {
    (void)application;
    if (usingScenes) return;
    auto st = cfire::webui::detail::active_ios_state();
    if (st) st->emit_system_event("app:background", "{}");
}
@end

#else // macOS Cocoa + WKWebView

@interface CfireWKWebView : WKWebView {
@public
    std::shared_ptr<cfire::webui::detail::AppState> state;
}
@end

@implementation CfireWKWebView
- (instancetype)initWithFrame:(NSRect)frameRect configuration:(WKWebViewConfiguration*)configuration {
    self = [super initWithFrame:frameRect configuration:configuration];
    if (self) {
        [self registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
    }
    return self;
}

- (void)willOpenMenu:(NSMenu*)menu withEvent:(NSEvent*)event {
    bool allow = cfire::webui::detail::kDefaultWebUIDevExtras;
    if (state) {
        std::lock_guard<std::recursive_mutex> lock(state->mtx);
        allow = state->context_menu_enabled;
    }
    if (!allow) {
        [menu removeAllItems];
        return;
    }
    if ([WKWebView instancesRespondToSelector:@selector(willOpenMenu:withEvent:)]) {
        [super willOpenMenu:menu withEvent:event];
    }
}

- (BOOL)hasDraggedFiles:(id<NSDraggingInfo>)sender {
    if (!sender) return NO;
    NSPasteboard* pb = [sender draggingPasteboard];
    if (!pb) return NO;
    return [pb canReadObjectForClasses:@[[NSURL class]]
                               options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    if ([self hasDraggedFiles:sender]) {
        return NSDragOperationCopy;
    }
    if ([WKWebView instancesRespondToSelector:@selector(draggingEntered:)]) {
        return [super draggingEntered:sender];
    }
    return NSDragOperationNone;
}

- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender {
    if ([self hasDraggedFiles:sender]) {
        return NSDragOperationCopy;
    }
    if ([WKWebView instancesRespondToSelector:@selector(draggingUpdated:)]) {
        return [super draggingUpdated:sender];
    }
    return NSDragOperationNone;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    if (sender) {
        NSPasteboard* pb = [sender draggingPasteboard];
        if (pb) {
            NSArray<NSURL*>* urls = [pb readObjectsForClasses:@[[NSURL class]]
                                                      options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
            if (urls && [urls count] > 0) {
                std::vector<std::string> paths;
                paths.reserve([urls count]);
                for (NSURL* url in urls) {
                    if (url && [url isFileURL]) {
                        NSURL* resolved = [url isFileReferenceURL] ? [url filePathURL] : url;
                        NSString* nsPath = resolved ? resolved.path : url.path;
                        if (nsPath) {
                            const char* utf8 = [nsPath UTF8String];
                            if (utf8 && utf8[0] != '\0') {
                                paths.emplace_back(utf8);
                            }
                        }
                    }
                }
                if (!paths.empty()) {
                    if (state) state->notify_file_drop(paths);
                    return YES;
                }
            }
        }
    }
    if ([WKWebView instancesRespondToSelector:@selector(performDragOperation:)]) {
        return [super performDragOperation:sender];
    }
    return NO;
}
@end

@interface CfireWebUIMacAppDelegate : NSObject <NSApplicationDelegate> {
@public
    std::shared_ptr<cfire::webui::detail::AppState> state;
    __weak NSWindow* window;
}
@end

@implementation CfireWebUIMacAppDelegate
- (BOOL)applicationShouldHandleReopen:(NSApplication*)sender hasVisibleWindows:(BOOL)hasVisibleWindows {
    (void)sender;
    bool hidden = false;
    if (state) {
        std::lock_guard<std::recursive_mutex> lock(state->mtx);
        hidden = state->window_hidden;
    }
    NSWindow* win = window;
    if (win && (!hasVisibleWindows || hidden || ![win isVisible] || [win isMiniaturized])) {
        if (state) {
            std::lock_guard<std::recursive_mutex> lock(state->mtx);
            state->window_hidden = false;
        }
        if ([win isMiniaturized]) {
            [win deminiaturize:nil];
        }
        [win makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        if (state) {
            state->emit_system_event("window:reopen", "{}");
        }
        return NO;
    }
    return YES;
}

- (void)application:(NSApplication*)application openURLs:(NSArray<NSURL*>*)urls {
    (void)application;
    if (!state || !urls) return;
    for (NSURL* url in urls) {
        NSString* urlStr = url ? [url absoluteString] : nil;
        const char* utf8 = urlStr ? [urlStr UTF8String] : nullptr;
        if (utf8 && utf8[0] != '\0') {
            state->notify_open_url(utf8);
        }
    }
}

- (void)applicationDidBecomeActive:(NSNotification*)notification {
    (void)notification;
    if (state) state->emit_system_event("app:active", "{}");
}

- (void)applicationDidResignActive:(NSNotification*)notification {
    (void)notification;
    if (state) state->emit_system_event("app:inactive", "{}");
}
@end

@interface CfireWebUIWindowDelegate : NSObject <NSWindowDelegate> {
@public
    std::shared_ptr<cfire::webui::detail::AppState> state;
    bool stop_nsapp_on_close;
}
@end

@implementation CfireWebUIWindowDelegate
- (BOOL)windowShouldClose:(NSWindow*)sender {
    if (state && state->hide_on_close && !state->quit_requested.load(std::memory_order_relaxed)) {
        {
            std::lock_guard<std::recursive_mutex> lock(state->mtx);
            state->window_hidden = true;
        }
        [sender orderOut:nil];
        state->emit_system_event("window:close_requested", "{}");
        return NO;
    }
    return YES;
}

- (void)windowDidEnterFullScreen:(NSNotification*)notification {
    (void)notification;
    if (state) {
        state->notify_fullscreen_changed(true);
    }
}

- (void)windowDidExitFullScreen:(NSNotification*)notification {
    (void)notification;
    if (state) {
        state->notify_fullscreen_changed(false);
    }
}

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

inline void apply_macos_webview_prefs(
    WKWebViewConfiguration* config,
    WKWebView* webView,
    bool devtools_enabled
) {
    if (config && config.preferences) {
        [config.preferences setValue:(devtools_enabled ? @YES : @NO) forKey:@"developerExtrasEnabled"];
        // Set WKPreferences.inactiveSchedulingPolicy = WKInactiveSchedulingPolicyNone (0) on macOS 14+
        // so timers (setTimeout/setInterval) and requestAnimationFrame run at full speed even when offscreen.
        @try {
            if ([config.preferences respondsToSelector:NSSelectorFromString(@"setInactiveSchedulingPolicy:")]) {
                [config.preferences setValue:@0 forKey:@"inactiveSchedulingPolicy"];
            }
        } @catch (NSException*) {
        }
    }
    if (webView) {
        if (@available(macOS 13.3, *)) {
            if ([webView respondsToSelector:@selector(setInspectable:)]) {
                webView.inspectable = devtools_enabled ? YES : NO;
            }
        }
    }
}

[[nodiscard]] inline auto run_macos_webview_test(
    const std::shared_ptr<AppState>& state_,
    std::int32_t timeout_ms
) -> std::expected<bool, cfire::Error> {
    @autoreleasepool {
        [NSApplication sharedApplication];
        state_->quit_requested.store(false, std::memory_order_relaxed);

        // Prevent App Nap & background timer throttling during headless WebView tests
        id activityToken = [[NSProcessInfo processInfo]
            beginActivityWithOptions:(NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical)
                              reason:@"cfire WebUI headless test runner"];

        NSRect frame = NSMakeRect(-2000, -2000, state_->width, state_->height);
        NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
                                                       styleMask:NSWindowStyleMaskBorderless
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        [window setReleasedWhenClosed:NO];

        bool init_devtools = kDefaultWebUIDevExtras;
        {
            std::lock_guard<std::recursive_mutex> lock(state_->mtx);
            init_devtools = state_->devtools_enabled;
        }

        WKWebViewConfiguration* config = [[WKWebViewConfiguration alloc] init];
        apply_macos_webview_prefs(config, nil, init_devtools);
        WKUserContentController* ucm = [[WKUserContentController alloc] init];

        std::string preload_cpp = detail::build_preload_script(*state_);
        NSString* preloadSource = [NSString stringWithUTF8String:preload_cpp.c_str()];
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

        CfireWKWebView* webView = [[CfireWKWebView alloc] initWithFrame:NSMakeRect(0, 0, state_->width, state_->height)
                                                          configuration:config];
        webView->state = state_;
        apply_macos_webview_prefs(config, webView, init_devtools);

        CfireWebUINavigationDelegate* navDelegate = [[CfireWebUINavigationDelegate alloc] init];
        navDelegate->state = state_;
        webView.navigationDelegate = navDelegate;
        webView.UIDelegate = navDelegate;

        CfireWebUIMacAppDelegate* testAppDelegate = [[CfireWebUIMacAppDelegate alloc] init];
        testAppDelegate->state = state_;
        testAppDelegate->window = window;
        [NSApp setDelegate:testAppDelegate];

        [window setContentView:webView];

        {
            std::lock_guard<std::recursive_mutex> lock(state_->mtx);
            state_->native_window_handle = (__bridge void*)window;
            state_->native_webview_handle = (__bridge void*)webView;
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
            state_->platform_set_devtools = [webView, config](bool enabled) {
                if ([NSThread isMainThread]) {
                    apply_macos_webview_prefs(config, webView, enabled);
                } else {
                    dispatch_async(dispatch_get_main_queue(), ^{
                        apply_macos_webview_prefs(config, webView, enabled);
                    });
                }
            };
            state_->platform_set_context_menu = [](bool) {};
            state_->platform_set_autofill = [webView](bool enabled) {
                NSString* ns_js = enabled
                    ? @"if(window.__cfire_set_autofill)window.__cfire_set_autofill(true);"
                    : @"if(window.__cfire_set_autofill)window.__cfire_set_autofill(false);";
                if ([NSThread isMainThread]) {
                    [webView evaluateJavaScript:ns_js completionHandler:nil];
                } else {
                    dispatch_async(dispatch_get_main_queue(), ^{
                        [webView evaluateJavaScript:ns_js completionHandler:nil];
                    });
                }
            };
            state_->platform_fullscreen = [st = state_]() {
                bool fs = false;
                {
                    std::lock_guard<std::recursive_mutex> lock(st->mtx);
                    fs = st->is_fullscreen;
                }
                st->notify_fullscreen_changed(fs);
            };
            state_->platform_set_fullscreen = [st = state_](bool on) {
                st->notify_fullscreen_changed(on);
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

        [NSApp setDelegate:nil];
        webView.navigationDelegate = nil;
        webView.UIDelegate = nil;
        [ucm removeScriptMessageHandlerForName:@"external"];
        {
            std::lock_guard<std::recursive_mutex> lock(state_->mtx);
            state_->native_window_handle = nullptr;
            state_->native_webview_handle = nullptr;
            state_->platform_eval = nullptr;
            state_->platform_set_title = nullptr;
            state_->platform_set_devtools = nullptr;
            state_->platform_set_context_menu = nullptr;
            state_->platform_set_autofill = nullptr;
            state_->platform_fullscreen = nullptr;
            state_->platform_set_fullscreen = nullptr;
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

        // Prevent App Nap / background timer throttling for active terminal/SSH/audio sessions
        id activityToken = [[NSProcessInfo processInfo]
            beginActivityWithOptions:(NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical)
                              reason:@"cfire WebUI active session"];

        // Set application icon if configured via build.cf or app.set_icon
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

        // Install standard macOS Application, Edit, Window, and custom menus
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

        // System Tray / Menu Bar Icon (`NSStatusBar` / `NSStatusItem`)
        NSStatusItem* statusItem = nil;
        NSMenu* trayMenu = nil;
        if (state_->has_tray) {
            statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
            {
                std::lock_guard<std::recursive_mutex> lock(state_->mtx);
                state_->tray_shown = (statusItem != nil);
            }
            if (statusItem && statusItem.button) {
                if (!state_->tray_tooltip.empty()) {
                    NSString* nsTip = [NSString stringWithUTF8String:state_->tray_tooltip.c_str()];
                    if (nsTip) [statusItem.button setToolTip:nsTip];
                }
                bool loaded_icon = false;
                if (!state_->tray_icon_path.empty()) {
                    NSString* nsTrayPath = [NSString stringWithUTF8String:state_->tray_icon_path.c_str()];
                    if (nsTrayPath) {
                        NSImage* trayImg = [[NSImage alloc] initWithContentsOfFile:nsTrayPath];
                        if (trayImg) {
                            [trayImg setSize:NSMakeSize(18, 18)];
                            [trayImg setTemplate:YES];
                            [statusItem.button setImage:trayImg];
                            loaded_icon = true;
                        }
                    }
                }
                if (!loaded_icon) {
                    NSString* nsTitle = [NSString stringWithUTF8String:state_->title.c_str()];
                    [statusItem.button setTitle:(nsTitle && [nsTitle length] > 0) ? [nsTitle substringToIndex:1] : @"●"];
                }
            }
            if (statusItem && !state_->tray_items.empty()) {
                trayMenu = [[NSMenu alloc] init];
                std::lock_guard<std::recursive_mutex> lock(state_->mtx);
                for (const auto& titem : state_->tray_items) {
                    if (titem.title == "-") {
                        [trayMenu addItem:[NSMenuItem separatorItem]];
                        continue;
                    }
                    NSString* nsTitle = [NSString stringWithUTF8String:titem.title.c_str()];
                    NSMenuItem* mi = [[NSMenuItem alloc]
                        initWithTitle:(nsTitle ? nsTitle : @"Item")
                               action:@selector(handleCustomMenuItem:)
                        keyEquivalent:@""];
                    [mi setTarget:menuTarget];
                    [mi setRepresentedObject:[NSString stringWithUTF8String:titem.event.c_str()]];
                    [trayMenu addItem:mi];
                }
                [statusItem setMenu:trayMenu];
            }
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
        [window setCollectionBehavior:NSWindowCollectionBehaviorFullScreenPrimary];
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

        CfireWebUIMacAppDelegate* appDelegate = [[CfireWebUIMacAppDelegate alloc] init];
        appDelegate->state = state_;
        appDelegate->window = window;
        [NSApp setDelegate:appDelegate];

        CfireWebUIWindowDelegate* winDelegate = [[CfireWebUIWindowDelegate alloc] init];
        winDelegate->state = state_;
        winDelegate->stop_nsapp_on_close = true;
        [window setDelegate:winDelegate];

        bool init_devtools = kDefaultWebUIDevExtras;
        {
            std::lock_guard<std::recursive_mutex> lock(state_->mtx);
            init_devtools = state_->devtools_enabled;
        }

        WKWebViewConfiguration* config = [[WKWebViewConfiguration alloc] init];
        apply_macos_webview_prefs(config, nil, init_devtools);

        WKUserContentController* ucm = [[WKUserContentController alloc] init];
        std::string preload_cpp = detail::build_preload_script(*state_);
        NSString* preloadSource = [NSString stringWithUTF8String:preload_cpp.c_str()];
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

        CfireWKWebView* webView = [[CfireWKWebView alloc] initWithFrame:[[window contentView] bounds]
                                                          configuration:config];
        webView->state = state_;
        apply_macos_webview_prefs(config, webView, init_devtools);

        CfireWebUINavigationDelegate* navDelegate = [[CfireWebUINavigationDelegate alloc] init];
        navDelegate->state = state_;
        webView.navigationDelegate = navDelegate;
        webView.UIDelegate = navDelegate;

        [webView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
        if (state_->transparent) {
            [webView setValue:@NO forKey:@"drawsBackground"];
        }
        [window setContentView:webView];

        {
            std::lock_guard<std::recursive_mutex> lock(state_->mtx);
            state_->native_window_handle = (__bridge void*)window;
            state_->native_webview_handle = (__bridge void*)webView;
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
            state_->platform_set_dark_mode = [window](bool dark) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    if (dark) {
                        [window setAppearance:[NSAppearance appearanceNamed:NSAppearanceNameDarkAqua]];
                    } else {
                        [window setAppearance:[NSAppearance appearanceNamed:NSAppearanceNameAqua]];
                    }
                });
            };
            state_->platform_set_devtools = [webView, config](bool enabled) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    apply_macos_webview_prefs(config, webView, enabled);
                });
            };
            state_->platform_set_context_menu = [](bool) {};
            state_->platform_set_autofill = [webView](bool enabled) {
                NSString* ns_js = enabled
                    ? @"if(window.__cfire_set_autofill)window.__cfire_set_autofill(true);"
                    : @"if(window.__cfire_set_autofill)window.__cfire_set_autofill(false);";
                dispatch_async(dispatch_get_main_queue(), ^{
                    [webView evaluateJavaScript:ns_js completionHandler:nil];
                });
            };
            state_->platform_fullscreen = [window]() {
                dispatch_async(dispatch_get_main_queue(), ^{
                    [window toggleFullScreen:nil];
                });
            };
            state_->platform_set_fullscreen = [window](bool on) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    bool cur = (([window styleMask] & NSWindowStyleMaskFullScreen) != 0);
                    if (cur != on) {
                        [window toggleFullScreen:nil];
                    }
                });
            };
            state_->platform_set_tray_item_title = [trayMenu](const std::string& ev, const std::string& title) {
                if (!trayMenu) return;
                NSString* nsEv = [NSString stringWithUTF8String:ev.c_str()];
                NSString* nsTitle = [NSString stringWithUTF8String:title.c_str()];
                if (!nsEv || !nsTitle) return;
                dispatch_async(dispatch_get_main_queue(), ^{
                    for (NSMenuItem* mi in [trayMenu itemArray]) {
                        id rep = [mi representedObject];
                        if ([rep isKindOfClass:[NSString class]] && [(NSString*)rep isEqualToString:nsEv]) {
                            [mi setTitle:nsTitle];
                        }
                    }
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
            state_->platform_hide_window = [window]() {
                dispatch_async(dispatch_get_main_queue(), ^{
                    [window orderOut:nil];
                });
            };
            state_->platform_show_window = [window]() {
                dispatch_async(dispatch_get_main_queue(), ^{
                    [window makeKeyAndOrderFront:nil];
                    [NSApp activateIgnoringOtherApps:YES];
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

        [NSApp setDelegate:nil];
        [window setDelegate:nil];
        webView.navigationDelegate = nil;
        webView.UIDelegate = nil;
        [ucm removeScriptMessageHandlerForName:@"external"];
        if (statusItem) {
            [[NSStatusBar systemStatusBar] removeStatusItem:statusItem];
        }
        {
            std::lock_guard<std::recursive_mutex> lock(state_->mtx);
            state_->tray_shown = false;
            state_->native_window_handle = nullptr;
            state_->native_webview_handle = nullptr;
            state_->platform_eval = nullptr;
            state_->platform_set_title = nullptr;
            state_->platform_set_size = nullptr;
            state_->platform_set_dark_mode = nullptr;
            state_->platform_set_devtools = nullptr;
            state_->platform_set_context_menu = nullptr;
            state_->platform_set_autofill = nullptr;
            state_->platform_fullscreen = nullptr;
            state_->platform_set_fullscreen = nullptr;
            state_->platform_set_tray_item_title = nullptr;
            state_->platform_minimize = nullptr;
            state_->platform_maximize = nullptr;
            state_->platform_center = nullptr;
            state_->platform_drag = nullptr;
            state_->platform_hide_window = nullptr;
            state_->platform_show_window = nullptr;
            state_->platform_quit = nullptr;
            state_->platform_screenshot = nullptr;
        }
        if (activityToken) {
            [[NSProcessInfo processInfo] endActivity:activityToken];
        }
    }
}

} // namespace cfire::webui::detail

#endif // PLATFORM_IOS vs macOS
#endif // defined(__APPLE__) && defined(__OBJC__)
