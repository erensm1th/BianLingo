#include "bianlingo/bubble.hpp"
#include "bianlingo/blocklist.hpp"
#include "bianlingo/localization.hpp"
#include "bianlingo/bundle.hpp"
#include "bianlingo/chat_preview.hpp"
#include "bianlingo/chat_style.hpp"
#include "bianlingo/chat_tabs.hpp"
#include "bianlingo/core.hpp"
#include "bianlingo/provider.hpp"
#include "bianlingo/quick_reply.hpp"
#include "bianlingo/reply_target.hpp"
#include "bianlingo/theme.hpp"
#include "version.hpp"
#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>
#import <QuartzCore/QuartzCore.h>
#include <algorithm>
#include <cmath>
#include <ctime>

using namespace bl;
static NSString *ns(std::string_view s) {
    return [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding];
}
static std::string str(NSString *s) {
    return s ? std::string(s.UTF8String) : std::string();
}
struct MacChatRow {
    NSRange range;
    uint32_t session, seq;
    size_t index;
};

struct MacChatBookmark {
    NSPoint origin{};
    bool bottom = true;
    std::optional<std::pair<uint32_t, uint32_t>> anchor;
    CGFloat offset = 0;
};
@interface BLChatView : NSTextView
@property(copy) void (^context)(NSUInteger, NSEvent *);
@end
@implementation BLChatView
- (void)rightMouseDown:(NSEvent *)event {
    NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    NSUInteger index = [self characterIndexForInsertionAtPoint:point];
    if (index < self.string.length && self.context)
        self.context(index, event);
}
@end

@interface BLGearButton : NSButton
@end
@implementation BLGearButton
- (void)mouseDown:(NSEvent *)event {
    NSPoint start = event.locationInWindow;
    while (true) {
        NSEvent *next = [NSApp nextEventMatchingMask:NSEventMaskLeftMouseDragged | NSEventMaskLeftMouseUp
                                           untilDate:NSDate.distantFuture
                                              inMode:NSEventTrackingRunLoopMode
                                             dequeue:YES];
        if (!next)
            return;
        if (next.type == NSEventTypeLeftMouseDragged && (std::abs(next.locationInWindow.x - start.x) > 4 ||
                                                         std::abs(next.locationInWindow.y - start.y) > 4)) {
            [self.window performWindowDragWithEvent:event];
            return;
        }
        if (next.type == NSEventTypeLeftMouseUp) {
            if (NSPointInRect([self convertPoint:next.locationInWindow fromView:nil], self.bounds))
                [self performClick:self];
            return;
        }
    }
}
@end

@interface BLApp : NSObject <NSApplicationDelegate, NSWindowDelegate, NSTabViewDelegate, NSTextViewDelegate,
                             NSTextFieldDelegate> {
  @public
    std::unique_ptr<Engine> engine;
    Json config;
    ThemeRegistry themeRegistry;
    ResolvedTheme activeTheme;
    std::deque<DisplayMessage> messages, previewMessages;
    bool messagePreview;
    std::vector<ChatTab> displayTabs;
    size_t activeChatTab, editingChatTab; // Objective-C allocation zeroes scalar ivars.
    Json chatTabDraft, gameLayout;
    std::map<std::string, MacChatBookmark> chatTabViews, previewTabViews;
    std::map<std::string, unsigned> chatTabUnread;
    NSScrollView *chatTabScroll;
    NSSegmentedControl *chatTabBar;
    NSPopUpButton *chatTabEditor;
    NSTextField *chatTabName, *chatTabChannels, *chatTabExcluded;
    NSMutableArray<NSButton *> *chatTabChecks;
    std::optional<MacChatBookmark> previewLiveView, nextRenderView;
    NSButton *previewButton;
    std::vector<MacChatRow> chatRows;
    std::optional<ReplyTarget> chosenReply;
    std::optional<Message> contextMessage;
    NSWindow *blockWindow;
    NSTextView *blockNames;
    NSTextField *blockNameInput, *blockStatus;
    std::optional<ReplyTarget> submittedReply;
    bool retryReply;
    NSWindow *main;
    NSWindow *settings;
    NSWindow *outbound;
    NSWindow *noticesWindow;
    NSTextView *chat;
    NSTextField *input;
    NSTextField *status;
    NSTextField *outputStatus;
    NSTextField *outputHeading;
    NSPanel *bubble;
    NSTextField *bubbleLabel;
    NSScreen *bubbleScreen;
    bool bubblePreview;
    NSButton *bubblePreviewButton;
    NSRunningApplication *previousApp;
    NSWindow *previousWindow;
    QuickReply reply;
    int clipboardAttempts;
    std::string pendingInput;
    NSPopUpButton *language;
    NSPopUpButton *outputStyle;
    NSSecureTextField *apiKey;
    NSTextView *statusView;
    NSTabView *settingsTabs;
    NSTextField *balanceAmount;
    NSTextField *balanceDetail;
    NSTextField *balanceState;
    NSTextField *balanceUpdated;
    NSTextField *usageTotal;
    NSTextField *usageDetail;
    NSButton *balanceRefresh;
    BalanceView account;
    std::string balanceError;
    bool balanceBusy;
    bool balanceRequested;
    Json providerDraft;
    size_t providerIndex;
    bool providerSync, providerTesting;
    NSPopUpButton *providerChoice;
    NSTextField *providerName;
    NSButton *providerTestButton;
    NSMutableDictionary<NSString *, NSControl *> *fields;
    NSMutableDictionary<NSString *, NSSlider *> *appearanceSliders;
    NSMutableDictionary<NSString *, NSColorWell *> *skinWells;
    NSPopUpButton *skinChoice;
    NSPopUpButton *designChoice;
    Json skinDraft;
    std::vector<std::pair<std::string, std::string>> skinChoices, designChoices;
    NSMutableDictionary<NSString *, NSTextView *> *prompts;
    NSStatusItem *tray;
    NSTimer *timer;
    EventHotKeyRef hotkey;
    EventHotKeyRef emergency;
    EventHandlerRef keyHandler;
    bool smoke;
}
- (void)showSettings:(id)sender;
- (void)appearanceSlider:(NSSlider *)sender;
- (void)selectSkin:(id)sender;
- (void)selectDesign:(id)sender;
- (void)populateSkinChoices;
- (void)populateDesignChoices;
- (void)importTheme:(id)sender;
- (void)exportTheme:(id)sender;
- (void)reloadThemes:(id)sender;
- (void)openThemes:(id)sender;
- (void)pickSkinColor:(NSColorWell *)sender;
- (void)applySkin;
- (void)toggleMessagePreview:(id)sender;
- (void)syncChatTabs;
- (void)refreshChatTabBar;
- (void)selectChatTab:(id)sender;
- (void)captureChatTabEditor;
- (void)populateChatTabEditor;
- (void)selectChatTabEditor:(id)sender;
- (void)addChatTab:(id)sender;
- (void)deleteChatTab:(id)sender;
- (void)importChatTabs:(id)sender;
- (BOOL)applyGameLayout:(const Json &)layout;
- (void)showOutput:(id)sender;
- (void)pickReply:(NSUInteger)index;
- (void)pickReply:(NSUInteger)index whisper:(BOOL)privateMessage;
- (void)showMessageMenu:(NSUInteger)character event:(NSEvent *)event;
- (void)messageWhisper:(id)sender;
- (void)messageWho:(id)sender;
- (void)messageBlock:(id)sender;
- (void)replyMessage:(const Message &)message whisper:(BOOL)privateMessage;
- (void)openBlockManager:(id)sender;
- (void)refreshBlockManager;
- (void)addBlock:(id)sender;
- (void)removeBlock:(id)sender;
- (void)copyBlockCode:(id)sender;
- (void)unlock:(id)sender;
- (void)save:(id)sender;
- (void)balance:(id)sender;
- (void)updateAccountView;
- (void)send:(id)sender;
- (void)resetOutputPrompt:(id)sender;
- (void)selectOutputStyle:(id)sender;
- (void)showNotices:(id)sender;
- (void)openLogs:(id)sender;
- (void)clearChatLogs:(id)sender;
- (void)hideOutput:(id)sender;
- (void)captureProvider;
- (void)fillProvider;
- (void)loadProviders;
- (BOOL)providerDirty;
- (void)selectProvider:(id)sender;
- (void)addProvider:(id)sender;
- (void)deleteProvider:(id)sender;
- (void)advancedProvider:(id)sender;
- (void)testProvider:(id)sender;
- (void)updateBubble;
- (void)toggleBubblePreview:(id)sender;
- (void)stopBubblePreview;
- (void)outputError:(NSString *)message;
- (void)poll:(id)sender;
- (void)render;
- (void)persistGeometry;
- (BOOL)registerHotkey:(Json)key;
@end
static BLApp *app;
@interface ATShortcut : NSTextField
@end
@implementation ATShortcut
- (void)keyDown:(NSEvent *)event {
    int flags = 0;
    if (event.modifierFlags & NSEventModifierFlagOption)
        flags |= 1;
    if (event.modifierFlags & NSEventModifierFlagControl)
        flags |= 2;
    if (event.modifierFlags & NSEventModifierFlagShift)
        flags |= 4;
    if (event.modifierFlags & NSEventModifierFlagCommand)
        flags |= 8;
    self.tag = event.keyCode;
    self.identifier = [NSString stringWithFormat:@"%d", flags];
    self.stringValue =
        [NSString stringWithFormat:@"%@%@%@%@%@", flags & 2 ? @"⌃" : @"", flags & 1 ? @"⌥" : @"",
                                   flags & 4 ? @"⇧" : @"", flags & 8 ? @"⌘" : @"",
                                   event.charactersIgnoringModifiers.uppercaseString ?: @""];
}
@end
@interface ATInput : NSTextField
@end
@implementation ATInput
- (BOOL)performKeyEquivalent:(NSEvent *)event {
    if (event.type == NSEventTypeKeyDown && event.keyCode == 53 &&
        ![(NSTextView *)self.currentEditor hasMarkedText]) {
        [app hideOutput:nil];
        return YES;
    }
    return [super performKeyEquivalent:event];
}
@end
@interface ATOutputWindow : NSWindow
@end
@implementation ATOutputWindow
- (BOOL)canBecomeKeyWindow {
    return YES;
}
- (BOOL)canBecomeMainWindow {
    return NO;
}
@end
@interface ATBubble : NSPanel
@end
@implementation ATBubble
- (BOOL)canBecomeKeyWindow {
    return NO;
}
- (BOOL)canBecomeMainWindow {
    return NO;
}
@end
namespace {
MacChatBookmark captureChatBookmark(BLApp *owner) {
    MacChatBookmark view;
    NSScrollView *scroll = owner->chat.enclosingScrollView;
    view.bottom = NSMaxY(scroll.documentVisibleRect) >= owner->chat.bounds.size.height - 5;
    if (!view.bottom) {
        auto glyphs = [owner->chat.layoutManager glyphRangeForBoundingRect:scroll.documentVisibleRect
                                                           inTextContainer:owner->chat.textContainer];
        auto visible = [owner->chat.layoutManager characterRangeForGlyphRange:glyphs
                                                             actualGlyphRange:nullptr];
        for (const auto &row : owner->chatRows)
            if (NSMaxRange(row.range) > visible.location) {
                view.anchor = {row.session, row.seq};
                auto g = [owner->chat.layoutManager glyphRangeForCharacterRange:row.range
                                                           actualCharacterRange:nullptr];
                auto bounds = [owner->chat.layoutManager boundingRectForGlyphRange:g
                                                                   inTextContainer:owner->chat.textContainer];
                view.offset = scroll.contentView.bounds.origin.y - bounds.origin.y;
                break;
            }
    }
    view.origin = scroll.contentView.bounds.origin;
    return view;
}
NSColor *skinColor(uint32_t c) {
    return [NSColor colorWithSRGBRed:((c >> 16) & 255) / 255.0
                               green:((c >> 8) & 255) / 255.0
                                blue:(c & 255) / 255.0
                               alpha:1];
}
NSColor *background() {
    return skinColor(app->activeTheme.palette.background);
}
NSAppearance *skinAppearance() {
    auto c = app->activeTheme.palette.background;
    bool light = (((c >> 16) & 255) * 299 + ((c >> 8) & 255) * 587 + (c & 255) * 114) > 160000;
    return [NSAppearance appearanceNamed:light ? NSAppearanceNameAqua : NSAppearanceNameDarkAqua];
}
void themeView(NSView *view) {
    auto p = app->activeTheme.palette;
    if (view == app->chat)
        return;
    if ([view isKindOfClass:NSTextField.class]) {
        NSTextField *f = (NSTextField *)view;
        f.textColor = skinColor(p.text);
        if (f.drawsBackground)
            f.backgroundColor = skinColor(p.surface);
    } else if ([view isKindOfClass:NSTextView.class]) {
        NSTextView *f = (NSTextView *)view;
        f.textColor = skinColor(p.text);
        if (f.drawsBackground)
            f.backgroundColor = skinColor(p.surface);
    } else if ([view isKindOfClass:NSButton.class]) {
        NSButton *b = (NSButton *)view;
        b.contentTintColor = skinColor(p.text);
        b.bezelColor = skinColor(p.surface);
    } else if ([view isKindOfClass:NSBox.class]) {
        NSBox *box = (NSBox *)view;
        box.fillColor = skinColor(p.surface);
        box.borderColor = skinColor(p.accent);
    }
    bool terminal = app->activeTheme.fonts.monospace;
    if ([view isKindOfClass:NSControl.class]) {
        NSControl *control = (NSControl *)view;
        CGFloat size = control.font ? control.font.pointSize : 13;
        control.font = terminal ? [NSFont monospacedSystemFontOfSize:size weight:NSFontWeightRegular]
                       : app->activeTheme.fonts.serif
                           ? ([NSFont fontWithName:@"Songti SC" size:size] ?: [NSFont systemFontOfSize:size])
                           : [NSFont systemFontOfSize:size];
        if ([view isKindOfClass:NSButton.class]) {
            NSButton *button = (NSButton *)view;
            if (![view isKindOfClass:NSPopUpButton.class] &&
                (button.bezelStyle == NSBezelStyleRounded || button.bezelStyle == NSBezelStyleSmallSquare)) {
                button.bezelStyle = terminal ? NSBezelStyleSmallSquare : NSBezelStyleRounded;
                button.wantsLayer = YES;
                button.layer.cornerRadius = app->activeTheme.metrics.button_radius;
                button.layer.borderWidth = app->activeTheme.metrics.border_width;
                button.layer.borderColor = skinColor(app->activeTheme.colors.border).CGColor;
            }
        }
    }
    for (NSView *child in view.subviews)
        themeView(child);
}
NSTextField *label(std::string_view value) {
    NSTextField *l = [NSTextField labelWithString:ns(value)];
    l.textColor = skinColor(app->activeTheme.palette.text);
    return l;
}
NSButton *button(std::string_view title, SEL action) {
    return [NSButton buttonWithTitle:ns(title) target:app action:action];
}
NSStackView *page(NSTabView *tabs, std::string_view name) {
    NSTabViewItem *tab = [[NSTabViewItem alloc] initWithIdentifier:ns(name)];
    tab.label = ns(name);
    NSScrollView *scroll = [NSScrollView new];
    scroll.hasVerticalScroller = YES;
    scroll.drawsBackground = NO;
    NSStackView *stack = [NSStackView stackViewWithViews:@[]];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = NSLayoutAttributeLeading;
    stack.spacing = 12;
    stack.edgeInsets = NSEdgeInsetsMake(18, 18, 18, 18);
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.documentView = stack;
    [stack.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor].active = YES;
    [stack.heightAnchor constraintGreaterThanOrEqualToConstant:400].active = YES;
    tab.view = scroll;
    [tabs addTabViewItem:tab];
    return stack;
}
void addCheck(NSStackView *stack, const char *key, const char *title) {
    NSButton *c = [NSButton checkboxWithTitle:ns(title) target:nil action:nil];
    c.state = app->config.value(key, false) ? NSControlStateValueOn : NSControlStateValueOff;
    app->fields[ns(key)] = c;
    [stack addArrangedSubview:c];
}
NSTextField *addText(NSStackView *stack, const char *key, const char *title, std::string value = {}) {
    [stack addArrangedSubview:label(title)];
    NSTextField *field =
        [NSTextField textFieldWithString:ns(value.empty() ? app->config.value(key, std::string()) : value)];
    [field.widthAnchor constraintGreaterThanOrEqualToConstant:530].active = YES;
    app->fields[ns(key)] = field;
    [stack addArrangedSubview:field];
    return field;
}
void addAppearanceNumber(NSStackView *stack, const char *key, const char *title, std::string value) {
    NSStackView *row = [NSStackView new];
    row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    row.alignment = NSLayoutAttributeCenterY;
    row.spacing = 12;
    auto caption = label(title);
    [caption.widthAnchor constraintEqualToConstant:200].active = YES;
    int minimum = std::string_view(key) == "__font"           ? 9
                  : std::string_view(key) == "__limit"        ? 20
                  : std::string_view(key) == "__bubble_scale" ? 50
                                                              : 0;
    int maximum = std::string_view(key) == "__font"           ? 36
                  : std::string_view(key) == "__limit"        ? 1000
                  : std::string_view(key) == "__bubble_scale" ? 200
                                                              : 100;
    NSSlider *slider = [NSSlider sliderWithValue:std::stoi(value)
                                        minValue:minimum
                                        maxValue:maximum
                                          target:app
                                          action:@selector(appearanceSlider:)];
    slider.identifier = ns(key);
    slider.continuous = YES;
    [slider.widthAnchor constraintEqualToConstant:210].active = YES;
    NSTextField *field = [NSTextField textFieldWithString:ns(value)];
    field.identifier = ns(key);
    field.delegate = app;
    [field.widthAnchor constraintEqualToConstant:80].active = YES;
    [row addArrangedSubview:caption];
    [row addArrangedSubview:slider];
    [row addArrangedSubview:field];
    [stack addArrangedSubview:row];
    app->fields[ns(key)] = field;
    app->appearanceSliders[ns(key)] = slider;
}
void addSkinColor(NSStackView *stack, const char *key, const char *title, const std::string &value) {
    NSStackView *row = [NSStackView new];
    row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    row.alignment = NSLayoutAttributeCenterY;
    row.spacing = 12;
    auto caption = label(title);
    [caption.widthAnchor constraintEqualToConstant:200].active = YES;
    NSColorWell *well = [NSColorWell new];
    well.identifier = ns(key);
    well.color = skinColor(parse_skin_color(value));
    well.target = app;
    well.action = @selector(pickSkinColor:);
    [well.widthAnchor constraintEqualToConstant:65].active = YES;
    NSTextField *field = [NSTextField textFieldWithString:ns(value)];
    field.identifier = ns(std::string("__skin_") + key);
    field.delegate = app;
    [field.widthAnchor constraintEqualToConstant:150].active = YES;
    [row addArrangedSubview:caption];
    [row addArrangedSubview:well];
    [row addArrangedSubview:field];
    [stack addArrangedSubview:row];
    app->fields[field.identifier] = field;
    app->skinWells[ns(key)] = well;
}
void addPrompt(NSStackView *stack, const char *key, const char *title) {
    [stack addArrangedSubview:label(title)];
    NSScrollView *scroll = [NSScrollView new];
    scroll.hasVerticalScroller = YES;
    NSTextView *view = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 530, 120)];
    view.richText = NO;
    view.font = [NSFont systemFontOfSize:13];
    view.string = ns(app->config.at(key).get<std::string>());
    scroll.documentView = view;
    [scroll.widthAnchor constraintEqualToConstant:540].active = YES;
    [scroll.heightAnchor constraintEqualToConstant:120].active = YES;
    [stack addArrangedSubview:scroll];
    app->prompts[ns(key)] = view;
}
NSColor *classColor(const std::string &name) {
    static const std::map<std::string, uint32_t> colors = {
        {"WARRIOR", 0xc79c6e}, {"PALADIN", 0xf58cba},     {"HUNTER", 0xabd473}, {"ROGUE", 0xfff569},
        {"PRIEST", 0xffffff},  {"DEATHKNIGHT", 0xc41f3b}, {"SHAMAN", 0x0070de}, {"MAGE", 0x69ccf0},
        {"WARLOCK", 0x9482c9}, {"MONK", 0x00ff98},        {"DRUID", 0xff7d0a},  {"DEMONHUNTER", 0xa330c9},
        {"EVOKER", 0x33937f}};
    auto it = colors.find(name);
    if (it == colors.end())
        return NSColor.labelColor;
    auto c = it->second;
    return [NSColor colorWithSRGBRed:((c >> 16) & 255) / 255.0
                               green:((c >> 8) & 255) / 255.0
                                blue:(c & 255) / 255.0
                               alpha:1];
}
OSStatus keyEvent(EventHandlerCallRef, EventRef event, void *context) {
    EventHotKeyID identifier{};
    GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof(identifier), nullptr,
                      &identifier);
    BLApp *application = (__bridge BLApp *)context;
    if (identifier.id == 1) {
        if (application->outbound.visible)
            [application hideOutput:nil];
        else
            [application showOutput:nil];
    } else if (identifier.id == 2)
        [application unlock:nil];
    return noErr;
}
} // namespace
@implementation BLApp
#include "macos_providers.inc"
#include "macos_blocklist.inc"
- (void)applySkin {
    [self syncChatTabs];
    activeTheme = themeRegistry.fallback(config);
    for (NSWindow *w : std::array<NSWindow *, 5>{main, settings, outbound, noticesWindow, bubble}) {
        if (!w)
            continue;
        w.appearance = skinAppearance();
        if (w != main)
            w.backgroundColor = background();
        themeView(w.contentView);
    }
}
- (void)syncChatTabs {
    displayTabs = parse_chat_tabs(config.at("chat_tabs"));
    activeChatTab = 0;
    for (size_t i = 0; i < displayTabs.size(); ++i)
        if (displayTabs[i].id == config.value("active_chat_tab", std::string("main")))
            activeChatTab = i;
    auto exists = [&](const auto &entry) {
        return std::any_of(displayTabs.begin(), displayTabs.end(),
                           [&](auto &t) { return t.id == entry.first; });
    };
    std::erase_if(chatTabViews, [&](auto &entry) { return !exists(entry); });
    std::erase_if(previewTabViews, [&](auto &entry) { return !exists(entry); });
    std::erase_if(chatTabUnread, [&](auto &entry) { return !exists(entry); });
    [self refreshChatTabBar];
}
- (void)refreshChatTabBar {
    if (!chatTabBar)
        return;
    chatTabScroll.hidden =
        displayTabs.size() == 1 && displayTabs.front().id == "main" && displayTabs.front().name == "General";
    chatTabBar.segmentCount = NSInteger(displayTabs.size());
    CGFloat total = 0;
    for (size_t i = 0; i < displayTabs.size(); ++i) {
        auto title = displayTabs[i].name;
        auto count = chatTabUnread.find(displayTabs[i].id);
        if (count != chatTabUnread.end() && count->second)
            title += " · " + std::to_string(count->second);
        NSString *caption = ns(title);
        [chatTabBar setLabel:caption forSegment:i];
        CGFloat width = std::clamp(
            [caption sizeWithAttributes:@{NSFontAttributeName : [NSFont systemFontOfSize:12]}].width + 20,
            CGFloat(50), CGFloat(146));
        [chatTabBar setWidth:width forSegment:i];
        total += width;
    }
    chatTabBar.frame = NSMakeRect(0, 0, total + 8, 24);
    chatTabBar.selectedSegment = activeChatTab;
}
- (void)selectChatTab:(id)sender {
    (void)sender;
    NSInteger chosen = chatTabBar.selectedSegment;
    if (chosen < 0 || size_t(chosen) >= displayTabs.size() || size_t(chosen) == activeChatTab)
        return;
    try {
        auto next = config;
        next["active_chat_tab"] = displayTabs[size_t(chosen)].id;
        if (!smoke)
            engine->configure(next);
        auto &views = messagePreview ? previewTabViews : chatTabViews;
        views[displayTabs[activeChatTab].id] = captureChatBookmark(self);
        config = std::move(next);
        activeChatTab = size_t(chosen);
        auto found = views.find(displayTabs[activeChatTab].id);
        nextRenderView = found == views.end() ? MacChatBookmark{} : found->second;
        if (!messagePreview)
            chatTabUnread[displayTabs[activeChatTab].id] = 0;
        [self render];
        [self refreshChatTabBar];
    } catch (const std::exception &e) {
        chatTabBar.selectedSegment = activeChatTab;
        status.stringValue = ns(notification_text(e.what()));
    }
}
- (void)captureChatTabEditor {
    if (!chatTabEditor || editingChatTab >= chatTabDraft.size())
        return;
    auto candidate = chatTabDraft;
    auto &tab = candidate[editingChatTab];
    tab["name"] = trim(str(chatTabName.stringValue));
    tab["events"] = Json::array();
    for (size_t i = 0; i < chat_event_options().size(); ++i)
        if (chatTabChecks[i].state == NSControlStateValueOn)
            tab["events"].push_back(chat_event_options()[i].first);
    tab["channels"] = parse_channel_filter(str(chatTabChannels.stringValue));
    tab["exclude_channels"] = parse_channel_filter(str(chatTabExcluded.stringValue));
    parse_chat_tabs(candidate);
    chatTabDraft = std::move(candidate);
}
- (void)populateChatTabEditor {
    if (editingChatTab >= chatTabDraft.size())
        editingChatTab = 0;
    [chatTabEditor removeAllItems];
    for (auto &tab : chatTabDraft)
        [chatTabEditor addItemWithTitle:ns(tab.at("name").get<std::string>())];
    [chatTabEditor selectItemAtIndex:editingChatTab];
    auto &tab = chatTabDraft[editingChatTab];
    chatTabName.stringValue = ns(tab.at("name").get<std::string>());
    for (size_t i = 0; i < chat_event_options().size(); ++i)
        chatTabChecks[i].state = std::find(tab["events"].begin(), tab["events"].end(),
                                           chat_event_options()[i].first) != tab["events"].end()
                                     ? NSControlStateValueOn
                                     : NSControlStateValueOff;
    chatTabChannels.stringValue = ns(format_channel_filter(tab.value("channels", Json::array())));
    chatTabExcluded.stringValue = ns(format_channel_filter(tab.value("exclude_channels", Json::array())));
}
- (void)selectChatTabEditor:(id)sender {
    (void)sender;
    auto selected = chatTabEditor.indexOfSelectedItem;
    try {
        [self captureChatTabEditor];
        editingChatTab = size_t(std::max(NSInteger(0), selected));
        [self populateChatTabEditor];
    } catch (const std::exception &e) {
        [chatTabEditor selectItemAtIndex:editingChatTab];
        status.stringValue = ns(notification_text(e.what()));
    }
}
- (void)addChatTab:(id)sender {
    (void)sender;
    try {
        [self captureChatTabEditor];
        if (chatTabDraft.size() >= max_chat_tabs)
            throw std::runtime_error("Up to 16 chat tabs");
        chatTabDraft.push_back(new_chat_tab("tab-" + sha256(random_bytes(16)).substr(0, 16),
                                            "New tab" + std::to_string(chatTabDraft.size() + 1)));
        editingChatTab = chatTabDraft.size() - 1;
        [self populateChatTabEditor];
    } catch (const std::exception &e) {
        status.stringValue = ns(notification_text(e.what()));
    }
}
- (void)deleteChatTab:(id)sender {
    (void)sender;
    try {
        if (chatTabDraft.size() <= 1)
            throw std::runtime_error("Keep at least one chat tab");
        chatTabDraft.erase(chatTabDraft.begin() + editingChatTab);
        editingChatTab = std::min(editingChatTab, chatTabDraft.size() - 1);
        [self populateChatTabEditor];
    } catch (const std::exception &e) {
        status.stringValue = ns(notification_text(e.what()));
    }
}
- (BOOL)applyGameLayout:(const Json &)layout {
    auto next = synchronize_chat_layout(config, layout, [self](const Json &candidate) {
        if (!self->smoke)
            self->engine->configure(candidate);
    });
    bool changed = next != config;
    auto oldId = displayTabs.at(activeChatTab).id;
    auto view = captureChatBookmark(self);
    bool cacheChanged = gameLayout != layout;
    config = std::move(next);
    gameLayout = layout;
    std::string note = changed ? "Game chat windows synced and saved:" : "Game chat windows already match:";
    note += std::to_string(config["chat_tabs"].size()) + "tabs";
    try {
        [self syncChatTabs];
        chatTabDraft = config["chat_tabs"];
        editingChatTab = activeChatTab;
        if (chatTabEditor)
            [self populateChatTabEditor];
        if (changed) {
            nextRenderView = displayTabs.at(activeChatTab).id == oldId ? view : MacChatBookmark{};
            [self render];
        }
    } catch (const std::exception &e) {
        note += std::string("; settings saved, UI refresh failed:") + e.what();
    }
    if (cacheChanged && !smoke) {
        try {
            write_json(platform::app_data() / "game-chat-layout.json", gameLayout);
        } catch (const std::exception &e) {
            note += std::string("; recent export cache failed:") + e.what();
        }
    }
    status.stringValue = ns(note);
    return changed ? YES : NO;
}
- (void)importChatTabs:(id)sender {
    (void)sender;
    try {
        if (gameLayout.is_null())
            throw std::runtime_error("Run /bl windows in game; the complete layout will be synchronized and saved automatically");
        [self applyGameLayout:gameLayout];
    } catch (const std::exception &e) {
        status.stringValue = ns(notification_text(e.what()));
    }
}
- (void)toggleMessagePreview:(id)sender {
    (void)sender;
    if (!messagePreview) {
        auto samples = chat_preview_messages(uint32_t(std::time(nullptr)));
        previewLiveView = captureChatBookmark(self);
        chatTabViews[displayTabs.at(activeChatTab).id] = *previewLiveView;
        previewTabViews.clear();
        previewMessages = std::move(samples);
        messagePreview = true;
        nextRenderView = MacChatBookmark{};
        [main orderFrontRegardless];
    } else {
        messagePreview = false;
        previewMessages.clear();
        auto found = chatTabViews.find(displayTabs.at(activeChatTab).id);
        nextRenderView = found == chatTabViews.end() ? MacChatBookmark{} : found->second;
        previewTabViews.clear();
        chatTabUnread[displayTabs[activeChatTab].id] = 0;
        [self refreshChatTabBar];
        previewLiveView.reset();
    }
    [self render];
    previewButton.title = messagePreview ? @"Exit test preview" : @"Show test message";
    statusView.string = messagePreview ? @"Test preview: style only, scrollable, not used for replies; no API requests, no logs"
                                       : @"Returned to real chat";
}
- (void)windowWillClose:(NSNotification *)notification {
    if (notification.object == settings)
        [self stopBubblePreview];
    if (notification.object == settings && messagePreview)
        [self toggleMessagePreview:nil];
}
- (void)appearanceSlider:(NSSlider *)sender {
    sender.integerValue = std::lround(sender.doubleValue);
    fields[sender.identifier].stringValue = [NSString stringWithFormat:@"%ld", (long)sender.integerValue];
    if (bubblePreview && [sender.identifier hasPrefix:@"__bubble_"])
        [self updateBubble];
}
- (void)controlTextDidChange:(NSNotification *)notification {
    NSTextField *field = notification.object;
    NSSlider *slider = appearanceSliders[field.identifier];
    auto value = str(field.stringValue);
    if (slider && !value.empty() && value.size() <= 4 &&
        std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        int v = std::stoi(value);
        if (v >= slider.minValue && v <= slider.maxValue) {
            slider.integerValue = v;
            if (bubblePreview && [field.identifier hasPrefix:@"__bubble_"])
                [self updateBubble];
        }
    }
    if ([field.identifier hasPrefix:@"__skin_"]) {
        [skinChoice selectItemAtIndex:skinChoices.size() - 1];
        try {
            skinWells[[field.identifier substringFromIndex:7]].color = skinColor(parse_skin_color(value));
        } catch (const std::exception &) {
        }
    }
}
- (void)populateDesignChoices {
    designChoices = themeRegistry.styles();
    [designChoice removeAllItems];
    for (size_t i = 0; i < designChoices.size(); ++i) {
        [designChoice addItemWithTitle:ns(designChoices[i].second)];
        if (designChoices[i].first == skinDraft.value("ui_style", std::string("ios")))
            [designChoice selectItemAtIndex:i];
    }
}
- (void)importTheme:(id)sender {
    (void)sender;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.allowsMultipleSelection = NO;
    panel.canChooseDirectories = NO;
    if ([panel runModal] != NSModalResponseOK)
        return;
    try {
        auto id = themeRegistry.import_file(std::filesystem::path(str(panel.URL.path)));
        themeRegistry.change_style(skinDraft, id);
        [self populateDesignChoices];
        [self populateSkinChoices];
        [self selectSkin:nil];
        statusView.string = @"Skin imported; save settings to keep it";
    } catch (const std::exception &e) {
        statusView.string = ns(notification_text(e.what()));
    }
}
- (void)exportTheme:(id)sender {
    (void)sender;
    try {
        auto draft = skinDraft;
        draft["skin"] = skinChoices.at(size_t(skinChoice.indexOfSelectedItem)).first;
        for (auto key : {"background", "surface", "text", "accent"})
            draft["skin_colors"][key] =
                skin_color_hex(parse_skin_color(str(fields[ns(std::string("__skin_") + key)].stringValue)));
        auto definition =
            themeRegistry.export_theme(draft, "community.export", themeRegistry.resolve(draft).name);
        definition["id"] = "community.export-" + sha256(definition.dump()).substr(0, 12);
        NSSavePanel *panel = [NSSavePanel savePanel];
        panel.nameFieldStringValue = @"theme.json";
        if ([panel runModal] == NSModalResponseOK) {
            write_json(std::filesystem::path(str(panel.URL.path)), definition);
            statusView.string = @"Skin exported";
        }
    } catch (const std::exception &e) {
        statusView.string = ns(notification_text(e.what()));
    }
}
- (void)reloadThemes:(id)sender {
    (void)sender;
    try {
        themeRegistry.reload();
        std::string warning;
        auto candidate = themeRegistry.fallback(skinDraft, &warning);
        if (!warning.empty()) {
            skinDraft["ui_style"] = candidate.id;
            skinDraft["skin"] = "graphite";
            skinDraft["skin_colors"] = skin_colors(candidate.palette);
        }
        [self populateDesignChoices];
        [self populateSkinChoices];
        [self selectSkin:nil];
        statusView.string = warning.empty() ? @"Skins reloaded; save settings to keep the selection" : ns(warning);
    } catch (const std::exception &e) {
        statusView.string = ns(notification_text(e.what()));
    }
}
- (void)openThemes:(id)sender {
    (void)sender;
    try {
        std::filesystem::create_directories(themeRegistry.directory());
        [[NSWorkspace sharedWorkspace]
            openURL:[NSURL fileURLWithPath:ns(themeRegistry.directory().string())]];
    } catch (const std::exception &e) {
        statusView.string = ns(notification_text(e.what()));
    }
}
- (void)populateSkinChoices {
    skinChoices = themeRegistry.palettes(skinDraft.value("ui_style", std::string("ios")));
    [skinChoice removeAllItems];
    NSInteger index = 0;
    for (size_t i = 0; i < skinChoices.size(); ++i) {
        [skinChoice addItemWithTitle:ns(skinChoices[i].second)];
        if (skinChoices[i].first == skinDraft.value("skin", std::string("graphite")))
            index = i;
    }
    [skinChoice selectItemAtIndex:index];
    auto opacityField = fields[@"__background_transparency"];
    if (opacityField) {
        auto value = int(std::lround((1 - background_opacity(skinDraft)) * 100));
        opacityField.stringValue = ns(std::to_string(value));
        appearanceSliders[@"__background_transparency"].integerValue = value;
    }
}
- (void)selectDesign:(id)sender {
    (void)sender;
    try {
        skinDraft["skin"] = skinChoices.at(size_t(skinChoice.indexOfSelectedItem)).first;
        for (auto key : {"background", "surface", "text", "accent"})
            skinDraft["skin_colors"][key] =
                skin_color_hex(parse_skin_color(str(fields[ns(std::string("__skin_") + key)].stringValue)));
        skinDraft["background_opacity"] = 1 - fields[@"__background_transparency"].integerValue / 100.0;
        themeRegistry.change_style(skinDraft,
                                   designChoices.at(size_t(designChoice.indexOfSelectedItem)).first);
        [self populateSkinChoices];
        auto colors = skin_colors(themeRegistry.resolve(skinDraft).palette);
        for (auto key : {"background", "surface", "text", "accent"}) {
            auto value = colors[key].get<std::string>();
            fields[ns(std::string("__skin_") + key)].stringValue = ns(value);
            skinWells[ns(key)].color = skinColor(parse_skin_color(value));
        }
    } catch (const std::exception &e) {
        statusView.string = ns(notification_text(e.what()));
    }
}
- (void)selectSkin:(id)sender {
    (void)sender;
    NSInteger index = skinChoice.indexOfSelectedItem;
    if (index < 0 || index >= NSInteger(skinChoices.size()) || skinChoices[index].first == "custom")
        return;
    skinDraft["skin"] = skinChoices[index].first;
    auto colors = skin_colors(themeRegistry.resolve(skinDraft).palette);
    for (auto key : {"background", "surface", "text", "accent"}) {
        auto value = colors[key].get<std::string>();
        fields[ns(std::string("__skin_") + key)].stringValue = ns(value);
        skinWells[ns(key)].color = skinColor(parse_skin_color(value));
    }
}
- (void)pickSkinColor:(NSColorWell *)sender {
    NSColor *c = [sender.color colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    if (!c)
        return;
    uint32_t rgb = uint32_t(std::lround(c.redComponent * 255)) << 16 |
                   uint32_t(std::lround(c.greenComponent * 255)) << 8 |
                   uint32_t(std::lround(c.blueComponent * 255));
    fields[ns(std::string("__skin_") + str(sender.identifier))].stringValue = ns(skin_color_hex(rgb));
    [skinChoice selectItemAtIndex:skinChoices.size() - 1];
}
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification;
    try {
        app = self;
        engine = std::make_unique<Engine>();
        config = engine->settings();
        [self syncChatTabs];
        try {
            gameLayout = parse_chat_layout(read_json(platform::app_data() / "game-chat-layout.json").dump());
        } catch (...) {
        }
        themeRegistry = ThemeRegistry(platform::app_data() / "Skins");
        std::string themeWarning;
        activeTheme = themeRegistry.fallback(config, &themeWarning);
        if (!themeWarning.empty()) {
            config["ui_style"] = "ios";
            config["skin"] = "graphite";
            config["skin_colors"] = skin_colors(activeTheme.palette);
        }
        main = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(config.value("x", 60), config.value("y", 100),
                                           config.value("width", 460), config.value("height", 240))
                      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskResizable |
                                NSWindowStyleMaskFullSizeContentView
                        backing:NSBackingStoreBuffered
                          defer:NO];
        main.title = @"BianLingo";
        main.titleVisibility = NSWindowTitleHidden;
        main.titlebarAppearsTransparent = YES;
        main.opaque = NO;
        main.level = NSFloatingWindowLevel;
        main.collectionBehavior =
            NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary;
        main.movableByWindowBackground = YES;
        main.minSize = NSMakeSize(220, 90);
        main.delegate = self;
        main.appearance = skinAppearance();
        [self applySkin];
        main.backgroundColor = [background() colorWithAlphaComponent:background_opacity(config)];
        main.hasShadow = background_opacity(config) > 0;
        main.ignoresMouseEvents = config.value("click_through", false);
        NSScrollView *scroll =
            [[NSScrollView alloc] initWithFrame:NSMakeRect(5, 4, main.contentView.bounds.size.width - 10,
                                                           main.contentView.bounds.size.height - 30)];
        scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        scroll.hasVerticalScroller = YES;
        scroll.scrollerStyle = NSScrollerStyleOverlay;
        scroll.autohidesScrollers = YES;
        scroll.drawsBackground = NO;
        BLChatView *chatView = [[BLChatView alloc] initWithFrame:scroll.contentView.bounds];
        __weak BLApp *owner = self;
        chatView.context = ^(NSUInteger index, NSEvent *event) {
          [owner showMessageMenu:index event:event];
        };
        chat = chatView;
        chat.editable = NO;
        chat.selectable = YES;
        chat.delegate = self;
        chat.linkTextAttributes = @{NSUnderlineStyleAttributeName : @0};
        chat.drawsBackground = NO;
        chat.font = [NSFont systemFontOfSize:config.value("font_size", 14)];
        chat.textContainerInset = NSMakeSize(2, 2);
        chat.autoresizingMask = NSViewWidthSizable;
        chat.textContainer.widthTracksTextView = YES;
        chat.verticallyResizable = YES;
        scroll.documentView = chat;
        [main.contentView addSubview:scroll];
        BLGearButton *gear = [BLGearButton new];
        gear.title = @"⚙";
        gear.target = self;
        gear.action = @selector(showSettings:);
        gear.frame = NSMakeRect(main.contentView.bounds.size.width - 32,
                                main.contentView.bounds.size.height - 27, 28, 24);
        gear.autoresizingMask = NSViewMinXMargin | NSViewMinYMargin;
        [main.contentView addSubview:gear];
        chatTabScroll =
            [[NSScrollView alloc] initWithFrame:NSMakeRect(5, main.contentView.bounds.size.height - 27,
                                                           main.contentView.bounds.size.width - 42, 24)];
        chatTabScroll.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
        chatTabScroll.drawsBackground = NO;
        chatTabScroll.hasHorizontalScroller = YES;
        chatTabScroll.autohidesScrollers = YES;
        chatTabScroll.scrollerStyle = NSScrollerStyleOverlay;
        chatTabBar = [[NSSegmentedControl alloc] initWithFrame:NSMakeRect(0, 0, 1, 24)];
        chatTabBar.segmentStyle = NSSegmentStyleSeparated;
        chatTabBar.trackingMode = NSSegmentSwitchTrackingSelectOne;
        chatTabBar.target = self;
        chatTabBar.action = @selector(selectChatTab:);
        chatTabScroll.documentView = chatTabBar;
        [main.contentView addSubview:chatTabScroll];
        [self refreshChatTabBar];
        [main orderFront:nil];
        tray = [NSStatusBar.systemStatusBar statusItemWithLength:NSVariableStatusItemLength];
        tray.button.title = @"BianLingo";
        NSMenu *menu = [NSMenu new];
        [menu addItemWithTitle:@"Settings" action:@selector(showSettings:) keyEquivalent:@""];
        [menu addItemWithTitle:@"Send translation" action:@selector(showOutput:) keyEquivalent:@""];
        [menu addItemWithTitle:@"Unlock overlay" action:@selector(unlock:) keyEquivalent:@""];
        [menu addItemWithTitle:@"Exit" action:@selector(terminate:) keyEquivalent:@"q"];
        for (NSMenuItem *item in menu.itemArray)
            if (item.action != @selector(terminate:))
                item.target = self;
        tray.menu = menu;
        EventTypeSpec spec{kEventClassKeyboard, kEventHotKeyPressed};
        InstallApplicationEventHandler(keyEvent, 1, &spec, (__bridge void *)self, &keyHandler);
        RegisterEventHotKey(109, controlKey | optionKey, {'ATNC', 2}, GetApplicationEventTarget(), 0,
                            &emergency);
        [self registerHotkey:config["hotkey"]];
        timer = [NSTimer scheduledTimerWithTimeInterval:0.1
                                                 target:self
                                               selector:@selector(poll:)
                                               userInfo:nil
                                                repeats:YES];
        if (smoke) {
            [self showSettings:nil];
            [self showOutput:nil];
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{
              [NSApp terminate:nil];
            });
        } else {
            engine->start();
        }
    } catch (const std::exception &error) {
        NSAlert *alert = [NSAlert new];
        alert.messageText = @"BianLingo failed to start";
        alert.informativeText = ns(error.what());
        [alert runModal];
        [NSApp terminate:nil];
    }
}
- (BOOL)registerHotkey:(Json)key {
    if (hotkey) {
        UnregisterEventHotKey(hotkey);
        hotkey = nullptr;
    }
    int flags = key.value("modifiers", 0);
    UInt32 modifiers = (flags & 1 ? optionKey : 0) | (flags & 2 ? controlKey : 0) |
                       (flags & 4 ? shiftKey : 0) | (flags & 8 ? cmdKey : 0);
    return RegisterEventHotKey(config.value("mac_keycode", 50), modifiers, {'ATNC', 1},
                               GetApplicationEventTarget(), 0, &hotkey) == noErr;
}
- (void)showSettings:(id)sender {
    (void)sender;
    if (settings) {
        [self applySkin];
        [settings makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        return;
    }
    fields = [NSMutableDictionary new];
    appearanceSliders = [NSMutableDictionary new];
    skinWells = [NSMutableDictionary new];
    prompts = [NSMutableDictionary new];
    settings = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(100, 150, 680, 680)
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    settings.title = @"BianLingo · Settings";
    settings.releasedWhenClosed = NO;
    settings.delegate = self;
    settings.appearance = skinAppearance();
    settings.minSize = NSMakeSize(650, 550);
    NSTabView *tabs = [[NSTabView alloc] initWithFrame:NSMakeRect(10, 70, 660, 600)];
    settingsTabs = tabs;
    tabs.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [settings.contentView addSubview:tabs];
    auto appearance = page(tabs, "Chat appearance");
    for (auto [key, title] :
         std::vector<std::pair<const char *, const char *>>{{"auto", "Auto translate"},
                                                            {"terms_enabled", "Use WoW Glossary"},
                                                            {"show_original", "Show original text too"},
                                                            {"show_time", "Show time"},
                                                            {"class_colors", "Color names by class"},
                                                            {"show_class", "Show class"},
                                                            {"show_race", "Show race"},
                                                            {"show_sex", "Show gender"},
                                                            {"click_through", "Click through (unlock from the tray menu)"}})
        addCheck(appearance, key, title);
    addAppearanceNumber(appearance, "__background_transparency", "Background transparency 0–100% (background only)",
                        std::to_string(int(std::lround((1 - background_opacity(config)) * 100))));
    addAppearanceNumber(appearance, "__font", "Font size 9–36", std::to_string(config.value("font_size", 14)));
    addAppearanceNumber(appearance, "__limit", "History limit 10–200",
                        std::to_string(config.value("max_messages", 20)));
    [appearance addArrangedSubview:label("Drag or enter a number, then save. Click the top-right settings button to open settings, or drag it to move the overlay.")];
    skinDraft = config;
    designChoice = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [self populateDesignChoices];
    designChoice.target = self;
    designChoice.action = @selector(selectDesign:);
    [appearance addArrangedSubview:label("Design style")];
    [appearance addArrangedSubview:designChoice];
    skinChoice = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [self populateSkinChoices];
    skinChoice.target = self;
    skinChoice.action = @selector(selectSkin:);
    [appearance addArrangedSubview:label("Color scheme")];
    [appearance addArrangedSubview:skinChoice];
    auto colors = skin_colors(themeRegistry.resolve(config).palette);
    for (auto [key, title] : std::vector<std::pair<const char *, const char *>>{
             {"background", "Background"}, {"surface", "Controls"}, {"text", "Interface text"}, {"accent", "Accent color"}})
        addSkinColor(appearance, key, title, colors[key].get<std::string>());
    [appearance addArrangedSubview:label("Choose a color or enter #RRGGBB; game channel and item colors stay unchanged.")];
    previewButton = button("Show test message", @selector(toggleMessagePreview:));
    [appearance addArrangedSubview:previewButton];
    NSStackView *themeActions = [NSStackView stackViewWithViews:@[
        button("Import", @selector(importTheme:)), button("Export", @selector(exportTheme:)),
        button("Reload", @selector(reloadThemes:)), button("Theme folder", @selector(openThemes:))
    ]];
    themeActions.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    [appearance addArrangedSubview:themeActions];
    auto scope = page(tabs, "Translation scope");
    for (auto [key, title] :
         std::vector<std::pair<const char *, const char *>>{{"party", "Party"},
                                                            {"raid", "Raid"},
                                                            {"instance", "Instance"},
                                                            {"whisper", "Received whispers"},
                                                            {"bn", "Received Battle.net whispers"},
                                                            {"outgoing", "Whispers you send"},
                                                            {"guild", "Guild / Officer"},
                                                            {"say", "Nearby / Yell"},
                                                            {"community", "Community"},
                                                            {"public", "Public Channels"}}) {
        NSButton *c = [NSButton checkboxWithTitle:ns(title) target:nil action:nil];
        c.state = std::find(config["groups"].begin(), config["groups"].end(), key) != config["groups"].end()
                      ? NSControlStateValueOn
                      : NSControlStateValueOff;
        fields[ns(std::string("group:") + key)] = c;
        [scope addArrangedSubview:c];
    }
    std::string numbers;
    for (auto &n : config["channels"]) {
        if (!numbers.empty())
            numbers += ",";
        numbers += std::to_string(n.get<int>());
    }
    addText(scope, "__channels", "Public channel numbers, separated by commas; leave empty for all", numbers);
    addCheck(scope, "skip_target_language_nicknames", "Skip names using the target language's script");
    [scope addArrangedSubview:label("Only the character name is checked, excluding the realm; matching messages stay original without a translation request.")];
    [scope addArrangedSubview:button("Blocked players & addon sync…", @selector(openBlockManager:))];
    auto api = page(tabs, "API");
    [api addArrangedSubview:label("Active connection (custom name; any compatible provider)")];
    providerChoice = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    providerChoice.target = self;
    providerChoice.action = @selector(selectProvider:);
    [providerChoice.widthAnchor constraintEqualToConstant:320].active = YES;
    auto providerRow = [NSStackView stackViewWithViews:@[
        providerChoice, button("Add", @selector(addProvider:)), button("Delete", @selector(deleteProvider:))
    ]];
    providerRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    [api addArrangedSubview:providerRow];
    [api addArrangedSubview:label("Connection name")];
    providerName = [NSTextField new];
    [providerName.widthAnchor constraintEqualToConstant:530].active = YES;
    [api addArrangedSubview:providerName];
    [api addArrangedSubview:label(engine->has_key() ? "API key saved; leave empty to keep it"
                                                    : "API keys are encrypted locally")];
    apiKey = [NSSecureTextField new];
    [apiKey.widthAnchor constraintEqualToConstant:530].active = YES;
    [api addArrangedSubview:apiKey];
    addText(api, "base_url", "API URL");
    addText(api, "model", "Model");
    providerTestButton = button("Send test request", @selector(testProvider:));
    auto providerActions = [NSStackView
        stackViewWithViews:@[ button("Advanced", @selector(advancedProvider:)), providerTestButton ]];
    providerActions.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    [api addArrangedSubview:providerActions];
    [api addArrangedSubview:label("OpenAI-compatible by default. Include the version path in the base URL, then save.")];
    [self loadProviders];
    NSBox *card = [[NSBox alloc] initWithFrame:NSMakeRect(0, 0, 540, 155)];
    card.boxType = NSBoxCustom;
    card.borderType = NSLineBorder;
    card.borderColor = [NSColor colorWithSRGBRed:0.23 green:0.30 blue:0.37 alpha:1];
    card.fillColor = [NSColor colorWithSRGBRed:0.08 green:0.12 blue:0.17 alpha:1];
    card.cornerRadius = 7;
    card.contentViewMargins = NSMakeSize(0, 0);
    [card.widthAnchor constraintEqualToConstant:540].active = YES;
    [card.heightAnchor constraintEqualToConstant:155].active = YES;
    auto caption = label("Account balance");
    caption.frame = NSMakeRect(12, 121, 310, 20);
    [card.contentView addSubview:caption];
    balanceAmount = label("—");
    balanceAmount.frame = NSMakeRect(12, 80, 370, 36);
    balanceAmount.font = [NSFont systemFontOfSize:24 weight:NSFontWeightSemibold];
    balanceAmount.textColor = [NSColor colorWithSRGBRed:0.39 green:0.84 blue:0.76 alpha:1];
    [card.contentView addSubview:balanceAmount];
    balanceState = label("Not queried yet");
    balanceState.frame = NSMakeRect(378, 87, 150, 22);
    balanceState.alignment = NSTextAlignmentRight;
    [card.contentView addSubview:balanceState];
    balanceDetail = label("");
    balanceDetail.frame = NSMakeRect(12, 52, 516, 22);
    balanceDetail.textColor = NSColor.secondaryLabelColor;
    [card.contentView addSubview:balanceDetail];
    balanceUpdated = label("");
    balanceUpdated.frame = NSMakeRect(12, 26, 516, 20);
    balanceUpdated.textColor = NSColor.secondaryLabelColor;
    [card.contentView addSubview:balanceUpdated];
    balanceRefresh = button("Refresh Balance", @selector(balance:));
    balanceRefresh.frame = NSMakeRect(402, 116, 126, 28);
    [card.contentView addSubview:balanceRefresh];
    [api addArrangedSubview:card];
    usageTotal = label("");
    usageDetail = label("");
    usageDetail.textColor = NSColor.secondaryLabelColor;
    [api addArrangedSubview:usageTotal];
    [api addArrangedSubview:usageDetail];
    [self updateAccountView];
    addPrompt(api, "incoming_prompt", "Auto chat translation prompt (separate from manual output)");
    addPrompt(api, "glossary_prefix", "Term-matching prompt prefix");
    auto outputPage = page(tabs, "Send translation");
    [outputPage addArrangedSubview:label("Click the field, press the shortcut you want, then save settings")];
    ATShortcut *shortcut = [[ATShortcut alloc] initWithFrame:NSZeroRect];
    shortcut.stringValue = @"Click, then press a shortcut (default ` / ·)";
    shortcut.tag = config.value("mac_keycode", 50);
    shortcut.identifier = [NSString stringWithFormat:@"%d", config["hotkey"].value("modifiers", 0)];
    [shortcut.widthAnchor constraintEqualToConstant:530].active = YES;
    fields[@"__hotkey"] = shortcut;
    [outputPage addArrangedSubview:shortcut];
    [outputPage addArrangedSubview:label("Applies only to manual output; save after editing or resetting")];
    addPrompt(outputPage, "outbound_template", "Short output prompt (must include {language})");
    addPrompt(outputPage, "outbound_default_template", "Standard output prompt (must include {language})");
    addPrompt(outputPage, "outbound_terms_prefix", "Output terminology prompt prefix");
    [outputPage addArrangedSubview:button("Open output translation", @selector(showOutput:))];
    [outputPage addArrangedSubview:button("Reset both output prompts", @selector(resetOutputPrompt:))];
    [outputPage addArrangedSubview:label("Output bubble: above screen center by default; size includes text and padding")];
    addAppearanceNumber(outputPage, "__bubble_transparency", "Bubble transparency 0–100%",
                        std::to_string(config.value("bubble_transparency", 50)));
    addAppearanceNumber(outputPage, "__bubble_scale", "Bubble size 50–200%",
                        std::to_string(config.value("bubble_scale", 100)));
    bubblePreviewButton = button("Preview tooltip", @selector(toggleBubblePreview:));
    [outputPage addArrangedSubview:bubblePreviewButton];

    auto runtime = page(tabs, "Status");
    NSScrollView *statusScroll = [NSScrollView new];
    statusScroll.hasVerticalScroller = YES;
    statusView = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 540, 400)];
    statusView.editable = NO;
    statusView.font = [NSFont systemFontOfSize:13];
    statusScroll.documentView = statusView;
    [statusScroll.widthAnchor constraintEqualToConstant:550].active = YES;
    [statusScroll.heightAnchor constraintEqualToConstant:310].active = YES;
    [runtime addArrangedSubview:statusScroll];
    [runtime addArrangedSubview:button("Notes and component notices", @selector(showNotices:))];
    [runtime addArrangedSubview:button("Open log directory", @selector(openLogs:))];
    addCheck(runtime, "chat_logging", "Save chat and output logs (includes whispers and names)");
    addText(runtime, "__log_retention_days", "Retention days (0: keep forever, 1–3650)",
            std::to_string(config.value("log_retention_days", 0)));
    [runtime addArrangedSubview:button("Delete saved chat logs", @selector(clearChatLogs:))];
    [runtime addArrangedSubview:label("Cloud connections send text to the selected service; local engines process it on this computer.")];
    auto tabPage = page(tabs, "Chat tabs");
    [tabPage addArrangedSubview:label("Each tab filters shared history independently; global translation filters and the history limit still apply.")];
    chatTabDraft = config.at("chat_tabs");
    editingChatTab = activeChatTab;
    chatTabEditor = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    chatTabEditor.target = self;
    chatTabEditor.action = @selector(selectChatTabEditor:);
    [tabPage addArrangedSubview:chatTabEditor];
    NSStackView *actions = [NSStackView stackViewWithViews:@[
        button("Add", @selector(addChatTab:)), button("Delete", @selector(deleteChatTab:))
    ]];
    actions.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    [tabPage addArrangedSubview:actions];
    auto tabField = [&](const char *title) {
        [tabPage addArrangedSubview:label(title)];
        NSTextField *field = [NSTextField new];
        [field.widthAnchor constraintEqualToConstant:530].active = YES;
        [tabPage addArrangedSubview:field];
        return field;
    };
    chatTabName = tabField("Tab name (1–24 characters)");
    chatTabChecks = [NSMutableArray new];
    [tabPage addArrangedSubview:label("Message types shown (uncheck to hide)")];
    for (auto &[event, title] : chat_event_options()) {
        (void)event;
        auto box = [NSButton checkboxWithTitle:ns(title) target:nil action:nil];
        [chatTabChecks addObject:box];
        [tabPage addArrangedSubview:box];
    }
    chatTabChannels = tabField("Include public channels (numbers/names; empty includes all)");
    chatTabExcluded = tabField("Exclude public channels (takes priority)");
    [tabPage addArrangedSubview:button("Reapply last export", @selector(importChatTabs:))];
    [tabPage addArrangedSubview:label("Run /bl windows in-game to auto-replace and save; no need to apply again.")];
    [self populateChatTabEditor];
    NSButton *save = button("Save", @selector(save:));
    save.frame = NSMakeRect(535, 16, 125, 32);
    save.autoresizingMask = NSViewMinXMargin;
    [settings.contentView addSubview:save];
    status = label("");
    status.frame = NSMakeRect(18, 16, 500, 32);
    status.autoresizingMask = NSViewWidthSizable;
    [settings.contentView addSubview:status];
    tabs.delegate = self;
    [self applySkin];
    [settings makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}
- (void)save:(id)sender {
    (void)sender;
    try {
        auto next = config;
        Json groups = Json::array();
        for (NSString *name in fields) {
            std::string key = str(name);
            NSControl *c = fields[name];
            if (key.rfind("group:", 0) == 0) {
                if (((NSButton *)c).state == NSControlStateValueOn)
                    groups.push_back(key.substr(6));
            } else if ([c isKindOfClass:NSButton.class])
                next[key] = ((NSButton *)c).state == NSControlStateValueOn;
            else if (appearanceSliders[name]) {
                auto text = str(c.stringValue);
                auto slider = appearanceSliders[name];
                if (text.empty() || text.size() > 4 ||
                    !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }))
                    throw std::runtime_error("Enter an integer within the range");
                int v = std::stoi(text);
                if (v < slider.minValue || v > slider.maxValue)
                    throw std::runtime_error("Value out of allowed range");
                if (key == "__background_transparency")
                    next["background_opacity"] = 1 - v / 100.0;
                else if (key.rfind("__bubble_", 0) == 0)
                    next[key.substr(2)] = v;
                else
                    next[key == "__font" ? "font_size" : "max_messages"] = v;
            } else if (key.rfind("__skin_", 0) == 0)
                next["skin_colors"][key.substr(7)] = skin_color_hex(parse_skin_color(str(c.stringValue)));
            else if (key == "__log_retention_days") {
                auto value = str(c.stringValue); size_t used = 0;
                int days = std::stoi(value, &used);
                if (used != value.size() || days < 0 || days > 3650) throw std::runtime_error("Log retention must be 0–3650 days");
                next["log_retention_days"] = days;
            }
            else if (key == "__channels") {
                auto value = str(c.stringValue);
                Json channels = Json::array();
                size_t start = 0;
                while (start < value.size()) {
                    auto end = value.find(',', start);
                    auto piece = trim(value.substr(start, end == std::string::npos ? end : end - start));
                    if (!piece.empty()) {
                        if (piece.size() > 3 || !std::all_of(piece.begin(), piece.end(),
                                                             [](char x) { return x >= '0' && x <= '9'; }))
                            throw std::runtime_error("Invalid channel number format");
                        channels.push_back(std::stoi(piece));
                    }
                    if (end == std::string::npos)
                        break;
                    start = end + 1;
                }
                next["channels"] = channels;
            } else if (key == "__hotkey") {
                next["mac_keycode"] = int(c.tag);
                next["hotkey"]["modifiers"] = std::stoi(str(c.identifier));
            } else
                next[key] = str(c.stringValue);
        }
        next["groups"] = groups;
        [self captureChatTabEditor];
        next["chat_tabs"] = chatTabDraft;
        normalize_chat_tabs(next);
        next["ui_style"] = skinDraft.value("ui_style", std::string("ios"));
        next["style_memory"] = skinDraft.value("style_memory", Json::object());
        next["skin"] = skinChoices.at(size_t(skinChoice.indexOfSelectedItem)).first;
        themeRegistry.change_style(next, next.at("ui_style").get<std::string>());
        for (NSString *key in prompts)
            next[str(key)] = str(prompts[key].string);
        [self captureProvider];
        next["active_connection"] = providerDraft.at(providerIndex).at("id");
        bool providerChanged = [self providerDirty];
        auto old = config;
        config = next;
        if (![self registerHotkey:next["hotkey"]]) {
            config = old;
            [self registerHotkey:old["hotkey"]];
            throw std::runtime_error("The shortcut is in use; choose another");
        }
        try {
            engine->configure(next, std::nullopt, providerDraft);
            config = engine->settings();
            [self loadProviders];
            providerTesting = NO;
            providerTestButton.enabled = YES;
        } catch (...) {
            config = old;
            [self registerHotkey:old["hotkey"]];
            throw;
        }
        apiKey.stringValue = @"";
        balanceBusy = false;
        if (providerChanged) {
            account = BalanceView{};
            balanceError.clear();
            balanceRequested = false;
        }
        [self updateAccountView];
        if ([settingsTabs.selectedTabViewItem.identifier isEqual:@"API"] && !balanceRequested &&
            engine->has_key() && engine->has_balance())
            [self balance:nil];
        [self applySkin];
        main.backgroundColor = [background() colorWithAlphaComponent:background_opacity(config)];
        main.hasShadow = background_opacity(config) > 0;
        main.ignoresMouseEvents = config.value("click_through", false);
        [self render];
        [self stopBubblePreview];
        [self updateBubble];
        status.stringValue = @"Saved";
    } catch (const std::exception &error) {
        status.stringValue = ns(error.what());
    }
}
- (void)unlock:(id)sender {
    (void)sender;
    config["click_through"] = false;
    main.ignoresMouseEvents = NO;
    [main orderFront:nil];
    try {
        engine->configure(config);
    } catch (const std::exception &e) {
        status.stringValue = ns(notification_text(e.what()));
    }
}
- (void)balance:(id)sender {
    (void)sender;
    if (balanceBusy)
        return;
    try {
        if ([self providerDirty])
            throw std::runtime_error("Save the current connection before refreshing the balance");
        engine->query_balance();
        balanceBusy = true;
        balanceRequested = true;
        balanceError.clear();
    } catch (const std::exception &e) {
        balanceError = e.what();
    }
    [self updateAccountView];
}
- (void)tabView:(NSTabView *)tabView didSelectTabViewItem:(NSTabViewItem *)item {
    (void)tabView;
    if (![item.identifier isEqual:@"Send translation"])
        [self stopBubblePreview];
    if ([item.identifier isEqual:@"API"]) {
        [self updateAccountView];
        if (!balanceRequested && engine->has_key() && engine->has_balance() && !smoke)
            [self balance:nil];
    }
}
- (void)updateAccountView {
    if (!balanceAmount)
        return;
    auto assign = [](NSTextField *field, const std::string &value) {
        if (str(field.stringValue) != value)
            field.stringValue = ns(value);
    };
    assign(balanceAmount, account.amount);
    assign(balanceDetail, balanceError.empty() ? account.detail : "Refresh failed:" + balanceError);
    assign(balanceUpdated, account.updated);
    assign(balanceState, balanceBusy             ? "Querying…"
                         : !balanceError.empty() ? "Refresh Failed"
                         : !engine->has_key()    ? "API Key not saved"
                                                 : account.state);
    auto usage = format_usage(engine->usage());
    assign(usageTotal, usage.total);
    assign(usageDetail, usage.detail);
    if (!engine->has_balance()) {
        assign(balanceState, "Balance API not configured");
        assign(balanceDetail, "Configure a balance API in advanced settings; usage is counted per connection.");
    }
    balanceRefresh.enabled = !balanceBusy && engine->has_key() && engine->has_balance();
    balanceRefresh.title = balanceBusy ? @"Querying…" : @"Refresh Balance";
    balanceState.textColor = balanceError.empty() ? NSColor.secondaryLabelColor : NSColor.systemOrangeColor;
}
- (void)showOutput:(id)sender {
    (void)sender;
    [self stopBubblePreview];
    if (!retryReply)
        chosenReply.reset();
    if (!outbound.keyWindow) {
        auto foreground = NSWorkspace.sharedWorkspace.frontmostApplication;
        previousApp =
            foreground.processIdentifier == NSProcessInfo.processInfo.processIdentifier ? nil : foreground;
        previousWindow = previousApp ? nil : NSApp.keyWindow;
        bubbleScreen = NSScreen.mainScreen;
    }
    if (!outbound) {
        auto area = (bubbleScreen ?: NSScreen.mainScreen).visibleFrame;
        outbound = [[ATOutputWindow alloc]
            initWithContentRect:NSMakeRect(NSMidX(area) - 230, NSMinY(area) + 90, 460, 104)
                      styleMask:NSWindowStyleMaskBorderless
                        backing:NSBackingStoreBuffered
                          defer:NO];
        outbound.title = @"BianLingo · Output translation";
        outbound.releasedWhenClosed = NO;
        outbound.level = NSFloatingWindowLevel;
        outbound.backgroundColor = background();
        outbound.movableByWindowBackground = YES;
        outbound.collectionBehavior =
            NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary;
        outbound.appearance = skinAppearance();
        outputHeading = label("Send translation");
        outputHeading.frame = NSMakeRect(10, 76, 190, 22);
        [outbound.contentView addSubview:outputHeading];
        input = [[ATInput alloc] initWithFrame:NSMakeRect(10, 36, 440, 29)];
        input.target = self;
        input.action = @selector(send:);
        [(NSTextFieldCell *)input.cell setSendsActionOnEndEditing:NO];
        [outbound.contentView addSubview:input];
        outputStyle = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(202, 72, 94, 28)];
        [outputStyle addItemsWithTitles:@[ @"Short", @"Default" ]];
        [outputStyle selectItemAtIndex:config.value("outbound_style", "short") == "default" ? 1 : 0];
        outputStyle.target = self;
        outputStyle.action = @selector(selectOutputStyle:);
        [outbound.contentView addSubview:outputStyle];
        language = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(306, 72, 104, 28)];
        for (const auto &[name, value] : output_languages) {
            [language addItemWithTitle:ns(name)];
            if (value == config.value("target", "English"))
                [language selectItemWithTitle:ns(name)];
        }
        [outbound.contentView addSubview:language];
        auto close = button("×", @selector(hideOutput:));
        close.frame = NSMakeRect(420, 73, 28, 26);
        [outbound.contentView addSubview:close];
        outputStatus = label("");
        outputStatus.font = [NSFont systemFontOfSize:12];
        outputStatus.textColor = NSColor.secondaryLabelColor;
        outputStatus.frame = NSMakeRect(10, 8, 440, 21);
        [outbound.contentView addSubview:outputStatus];
    }
    if (!retryReply)
        for (size_t i = 0; i < output_languages.size(); ++i)
            if (output_languages[i].second == config.value("target", "English"))
                [language selectItemAtIndex:NSInteger(i)];
    outputHeading.stringValue = chosenReply ? ns("Reply " + chosenReply->label) : @"Send translation";
    outputStatus.stringValue = chosenReply ? ns(chosenReply->prefix + "Translation · Confirm language, press Enter to translate and copy")
                                           : @"Enter to translate and copy · Esc to return to game";
    [self applySkin];
    [outbound makeKeyAndOrderFront:nil];
    [outbound makeFirstResponder:input];
    [NSApp activateIgnoringOtherApps:YES];
    [input selectText:nil];
}
- (void)pickReply:(NSUInteger)index {
    [self pickReply:index whisper:NO];
}
- (void)pickReply:(NSUInteger)index whisper:(BOOL)privateMessage {
    if (messagePreview || index >= messages.size())
        return;
    [self replyMessage:messages[index].message whisper:privateMessage];
}
- (void)replyMessage:(const Message &)message whisper:(BOOL)privateMessage {
    auto selected = privateMessage ? whisper_target(message) : reply_target(message);
    if (!selected)
        return;
    auto suggested = reply_language(message.text);
    retryReply = false;
    [self showOutput:nil];
    chosenReply = std::move(selected);
    for (size_t i = 0; i < output_languages.size(); ++i)
        if (output_languages[i].second == suggested)
            [language selectItemAtIndex:NSInteger(i)];
    outputHeading.stringValue = ns("Reply " + chosenReply->label);
    outputStatus.stringValue = ns(chosenReply->prefix + "Translation · Confirm language, press Enter to translate and copy");
    [outbound makeFirstResponder:input];
}
- (BOOL)textView:(NSTextView *)textView clickedOnLink:(id)link atIndex:(NSUInteger)charIndex {
    (void)charIndex;
    NSString *prefix = @"bianlingo-reply:";
    NSString *value = [link isKindOfClass:[NSURL class]]      ? [(NSURL *)link absoluteString]
                      : [link isKindOfClass:[NSString class]] ? (NSString *)link
                                                              : nil;
    if (textView != chat || ![value hasPrefix:prefix])
        return NO;
    [self pickReply:NSUInteger([[value substringFromIndex:prefix.length] integerValue])];
    return YES;
}
- (void)showNotices:(id)sender {
    (void)sender;
    if (!noticesWindow) {
        noticesWindow =
            [[NSWindow alloc] initWithContentRect:NSMakeRect(180, 160, 700, 500)
                                        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                  NSWindowStyleMaskResizable
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
        noticesWindow.title = @"BianLingo · About & component notices";
        noticesWindow.releasedWhenClosed = NO;
        noticesWindow.appearance = skinAppearance();
        NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:noticesWindow.contentView.bounds];
        scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        scroll.hasVerticalScroller = YES;
        NSTextView *view = [[NSTextView alloc] initWithFrame:scroll.contentView.bounds];
        view.editable = NO;
        view.richText = NO;
        view.font = [NSFont systemFontOfSize:13];
        view.string = ns(bundled_notices());
        view.autoresizingMask = NSViewWidthSizable;
        scroll.documentView = view;
        noticesWindow.contentView = scroll;
    }
    [noticesWindow makeKeyAndOrderFront:nil];
}

- (void)resetOutputPrompt:(id)sender {
    (void)sender;
    prompts[@"outbound_template"].string = ns(default_settings().at("outbound_template").get<std::string>());
    prompts[@"outbound_default_template"].string =
        ns(default_settings().at("outbound_default_template").get<std::string>());
    status.stringValue = @"Default output prompts restored; save settings to apply";
}
- (void)selectOutputStyle:(id)sender {
    (void)sender;
    auto next = config;
    next["outbound_style"] = outputStyle.indexOfSelectedItem == 1 ? "default" : "short";
    try {
        engine->configure(next);
        config = std::move(next);
        [outbound makeFirstResponder:input];
    } catch (const std::exception &e) {
        [outputStyle selectItemAtIndex:config.value("outbound_style", "short") == "default" ? 1 : 0];
        outputStatus.stringValue = ns(notification_text(e.what()));
    }
}
- (void)hideOutput:(id)sender {
    (void)sender;
    BOOL focused = outbound.keyWindow;
    [outbound orderOut:nil];
    if (retryReply) {
        retryReply = false;
        chosenReply.reset();
    }
    if (focused) {
        if (previousApp && !previousApp.terminated)
            [previousApp activateWithOptions:NSApplicationActivateIgnoringOtherApps];
        else if (previousWindow && previousWindow != outbound)
            [previousWindow makeKeyAndOrderFront:nil];
    }
}
- (void)stopBubblePreview {
    if (!bubblePreview)
        return;
    bubblePreview = NO;
    bubblePreviewButton.title = @"Preview tooltip";
    [self updateBubble];
}
- (void)toggleBubblePreview:(id)sender {
    (void)sender;
    if (bubblePreview) {
        [self stopBubblePreview];
        return;
    }
    if (reply.phase != QuickReply::Hidden) {
        status.stringValue = @"Output tip is still showing. Please preview again later.";
        return;
    }
    bubbleScreen = settings.screen ?: NSScreen.mainScreen;
    bubblePreview = YES;
    bubblePreviewButton.title = @"Disable Bubble Preview";
    [self updateBubble];
}
- (void)updateBubble {
    if (!bubblePreview && reply.phase == QuickReply::Hidden) {
        [bubble orderOut:nil];
        return;
    }
    auto options = Json::object();
    if (bubblePreview)
        for (auto key : {"bubble_transparency", "bubble_scale"}) {
            auto field = fields[ns(std::string("__") + key)];
            auto value = str(field.stringValue);
            if (value.empty() || value.size() > 4 ||
                !std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; }))
                return;
            options[key] = std::stoi(value);
        }
    BubbleAppearance appearance;
    try {
        appearance = bubble_appearance(bubblePreview ? options : config);
    } catch (const std::exception &) {
        return;
    }
    double factor = appearance.scale / 100.0;
    if (!bubble) {
        bubble = [[ATBubble alloc]
            initWithContentRect:NSMakeRect(0, 0, 1, 1)
                      styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                        backing:NSBackingStoreBuffered
                          defer:NO];
        bubble.releasedWhenClosed = NO;
        bubble.level = NSFloatingWindowLevel;
        bubble.hidesOnDeactivate = NO;
        bubble.ignoresMouseEvents = YES;
        bubble.collectionBehavior =
            NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary;
        bubbleLabel = label("");
        bubbleLabel.alignment = NSTextAlignmentCenter;
        bubbleLabel.lineBreakMode = NSLineBreakByWordWrapping;
        bubbleLabel.maximumNumberOfLines = 0;
        [bubble.contentView addSubview:bubbleLabel];
    }
    bubble.appearance = skinAppearance();
    bubble.backgroundColor = background();
    bubble.alphaValue = appearance.opacity();
    bubbleLabel.font = [NSFont systemFontOfSize:14 * factor weight:NSFontWeightSemibold];
    bubbleLabel.stringValue = ns(bubblePreview ? "Translating… · Tooltip preview" : reply.caption());
    bubbleLabel.textColor =
        reply.phase == QuickReply::Error ? NSColor.systemOrangeColor : skinColor(activeTheme.palette.accent);
    auto area = (bubbleScreen ?: NSScreen.mainScreen).visibleFrame;
    CGFloat padX = 18 * factor, padY = 11 * factor;
    auto size =
        [bubbleLabel.attributedStringValue
            boundingRectWithSize:NSMakeSize(std::max(CGFloat(1), std::min(CGFloat(440 * factor),
                                                                          area.size.width - 16 - 2 * padX)),
                                            10000)
                         options:NSStringDrawingUsesLineFragmentOrigin]
            .size;
    auto bounds =
        bubble_bounds({0, 0, int(area.size.width), int(area.size.height)},
                      int(std::ceil(size.width) + 2 * padX), int(std::ceil(size.height) + 2 * padY), 8);
    [bubble
        setFrame:NSMakeRect(area.origin.x + bounds.x, NSMaxY(area) - bounds.y - bounds.h, bounds.w, bounds.h)
         display:YES];
    bubbleLabel.frame = NSMakeRect(padX, padY, std::max(CGFloat(1), bounds.w - 2 * padX),
                                   std::max(CGFloat(1), bounds.h - 2 * padY));
    [bubble orderFrontRegardless];
}
- (void)outputError:(NSString *)message {
    reply.fail(str(message), Clock::now());
    chosenReply = submittedReply;
    retryReply = chosenReply.has_value();
    if (input.stringValue.length == 0)
        input.stringValue = ns(pendingInput);
    outputStatus.stringValue = message;
    [self updateBubble];
}
- (void)send:(id)sender {
    (void)sender;
    [self stopBubblePreview];
    if (reply.busy()) {
        outputStatus.stringValue = @"Previous message still translating, please wait; current input will be kept.";
        return;
    }
    try {
        if (trim(str(input.stringValue)).empty())
            return;
        auto target = std::string(
            output_languages.at(size_t(std::max(NSInteger(0), language.indexOfSelectedItem))).second);
        if (!chosenReply && config.value("target", "English") != target) {
            auto next = config;
            next["target"] = target;
            engine->configure(next);
            config = std::move(next);
        }
        pendingInput = str(input.stringValue);
        engine->outbound(pendingInput, target);
        reply.begin();
        retryReply = false;
        submittedReply = chosenReply;
        input.stringValue = @"";
        [self hideOutput:nil];
        [self updateBubble];
    } catch (const std::exception &e) {
        outputStatus.stringValue = ns(notification_text(e.what()));
    }
}
- (void)openLogs:(id)sender {
    (void)sender;
    auto directory = platform::app_data() / "Logs";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    [NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:ns(directory.string())]];
}
- (void)clearChatLogs:(id)sender {
    (void)sender;
    NSAlert *alert = [NSAlert new];
    alert.messageText = ns(ui_text("Delete all chat logs? This cannot be undone."));
    [alert addButtonWithTitle:ns(ui_text("Delete saved chat logs"))];
    [alert addButtonWithTitle:ns(ui_text("Cancel"))];
    if ([alert runModal] == NSAlertFirstButtonReturn) engine->clear_chat_logs();
}
- (void)render {
    NSScrollView *scroll = chat.enclosingScrollView;
    auto view = nextRenderView ? *nextRenderView : captureChatBookmark(self);
    nextRenderView.reset();
    auto bottom = view.bottom;
    auto anchor = view.anchor;
    auto offset = view.offset;
    const auto &visibleMessages = messagePreview ? previewMessages : messages;
    chatRows.clear();
    NSMutableAttributedString *all = [[NSMutableAttributedString alloc] initWithString:@""];
    NSFont *font = [NSFont systemFontOfSize:config.value("font_size", 14)];
    auto append = [&](std::string_view s, NSColor *color) {
        [all appendAttributedString:[[NSAttributedString alloc] initWithString:ns(s)
                                                                    attributes:@{
                                                                        NSFontAttributeName : font,
                                                                        NSForegroundColorAttributeName : color
                                                                    }]];
    };
    for (size_t messageIndex = 0; messageIndex < visibleMessages.size(); ++messageIndex) {
        const auto &display = visibleMessages[messageIndex];
        auto &m = display.message;
        if (!displayTabs.at(activeChatTab).matches(m) || !display.error.empty() || engine->player_blocked(m))
            continue;
        NSUInteger rowStart = all.length;
        if (messagePreview)
            append("[Test]", skinColor(activeTheme.palette.accent));
        if (m.replay_batch)
            append("[Resend]", NSColor.systemMintColor);
        if (config.value("show_time", false) && m.sent) {
            std::time_t t = m.sent;
            std::tm tm{};
            localtime_r(&t, &tm);
            char stamp[16];
            std::strftime(stamp, sizeof(stamp), "[%H:%M] ", &tm);
            append(stamp, NSColor.secondaryLabelColor);
        }
        auto channel = chat_color(m);
        auto rgb = [](uint32_t c) {
            return [NSColor colorWithSRGBRed:((c >> 16) & 255) / 255.0
                                       green:((c >> 8) & 255) / 255.0
                                        blue:(c & 255) / 255.0
                                       alpha:1];
        };
        auto appendChat = [&](std::string_view text) {
            for (const auto &span : chat_spans(text, channel))
                append(span.text, rgb(span.color));
        };
        append(chat_channel_label(m), rgb(channel));
        auto name = player_display_name(m);
        auto metadata = player_badges(m, config);
        if (!name.empty())
            append(metadata + "[" + name + "]: ",
                   config.value("class_colors", true) && !m.player_class.empty() ? classColor(m.player_class)
                                                                                 : rgb(channel));
        appendChat(display.translation);
        if (config.value("show_original", false))
            appendChat("  " + m.text);
        append("\n", NSColor.labelColor);
        if (!messagePreview && reply_target(m))
            [all addAttribute:NSLinkAttributeName
                        value:[NSString stringWithFormat:@"bianlingo-reply:%zu", messageIndex]
                        range:NSMakeRange(rowStart, all.length - rowStart)];
        chatRows.push_back({NSMakeRange(rowStart, all.length - rowStart), m.session, m.seq, messageIndex});
    }
    [chat.textStorage setAttributedString:all];
    [chat.layoutManager ensureLayoutForTextContainer:chat.textContainer];
    if (bottom)
        [chat scrollRangeToVisible:NSMakeRange(chat.string.length, 0)];
    else {
        auto point = view.origin;
        if (anchor)
            for (const auto &row : chatRows)
                if (anchor == std::pair{row.session, row.seq}) {
                    auto glyphs = [chat.layoutManager glyphRangeForCharacterRange:row.range
                                                             actualCharacterRange:nullptr];
                    auto bounds = [chat.layoutManager boundingRectForGlyphRange:glyphs
                                                                inTextContainer:chat.textContainer];
                    point.y = bounds.origin.y + offset;
                    break;
                }
        [scroll.contentView scrollToPoint:point];
        [scroll reflectScrolledClipView:scroll.contentView];
    }
}
- (void)poll:(id)sender {
    (void)sender;
    bool dirty = false;
    for (auto &n : engine->drain()) {
        if (n.kind == Notice::ChatLayout && n.layout) {
            try {
                if ([self applyGameLayout:*n.layout])
                    dirty = false;
            } catch (const std::exception &e) {
                status.stringValue = ns(std::string("Game layout sync failed; original layout kept:") + e.what());
            }
        } else if (n.kind == Notice::Blocklist) {
            [self refreshBlockManager];
            dirty = true;
        } else if (n.kind == Notice::Chat) {
            if (!n.chat.error.empty()) {
                status.stringValue = ns(n.chat.error);
                continue;
            }
            std::array<bool, max_chat_tabs> matches{};
            bool any = false;
            for (size_t i = 0; i < displayTabs.size(); ++i) {
                matches[i] = displayTabs[i].matches(n.chat.message);
                any = any || matches[i];
            }
            if (!any)
                continue;
            size_t limit = size_t(config.value("max_messages", 20)),
                   removed = messages.size() + 1 > limit ? messages.size() + 1 - limit : 0;
            bool lost = false;
            for (size_t i = 0; i < std::min(removed, messages.size()); ++i)
                lost = lost || displayTabs[activeChatTab].matches(messages[i].message);
            auto change = update_history(messages, std::move(n.chat), limit);
            if (change.retained) {
                bool redraw = matches[activeChatTab] || lost;
                if (!messagePreview && !redraw)
                    for (auto &row : chatRows) {
                        size_t shifted = row.index + (row.index >= change.position ? 1 : 0);
                        if (shifted < change.removed) {
                            redraw = true;
                            break;
                        }
                        row.index = shifted - change.removed;
                    }
                for (size_t i = 0; i < displayTabs.size(); ++i)
                    if ((i != activeChatTab || messagePreview) && matches[i])
                        chatTabUnread[displayTabs[i].id] =
                            std::min(9999u, chatTabUnread[displayTabs[i].id] + 1);
                dirty = redraw || dirty;
                [self refreshChatTabBar];
            }
        } else if (n.kind == Notice::Outbound) {
            if (reply.receive(!submittedReply ? std::move(n.text) : compose_reply(*submittedReply, n.text)))
                clipboardAttempts = 0;
        } else if (n.kind == Notice::OutboundError) {
            if (reply.busy())
                [self outputError:ns(n.text)];
        } else if (n.kind == Notice::Balance) {
            if (n.balance)
                account = *n.balance;
            balanceBusy = false;
            balanceError.clear();
            [self updateAccountView];
        } else if (n.kind == Notice::BalanceError) {
            balanceBusy = false;
            balanceError = n.text;
            [self updateAccountView];
        } else if (n.kind == Notice::ProviderTest || n.kind == Notice::ProviderTestError) {
            providerTesting = NO;
            providerTestButton.enabled = YES;
            status.stringValue = ns(n.text);
        } else {
            status.stringValue = ns(n.text);
        }
    }
    if (reply.phase == QuickReply::Clipboard) {
        [NSPasteboard.generalPasteboard clearContents];
        if (!reply.result.empty() && [NSPasteboard.generalPasteboard setString:ns(reply.result)
                                                                       forType:NSPasteboardTypeString]) {
            reply.copied(Clock::now());
            pendingInput.clear();
            [self updateBubble];
        } else if (++clipboardAttempts >= 10)
            [self outputError:@"Copy failed: clipboard busy, please resubmit translation"];
    }
    if (reply.tick(Clock::now()))
        [self updateBubble];
    if (dirty && !messagePreview)
        [self render];
    if (settings.visible) {
        auto value = std::string("Native version ") + BL_VERSION + "\n" + engine->status() + "\n\n" +
                     str(status.stringValue);
        if (str(statusView.string) != value)
            statusView.string = ns(value);
        if ([settingsTabs.selectedTabViewItem.identifier isEqual:@"API"])
            [self updateAccountView];
    }
}
- (void)windowDidEndLiveResize:(NSNotification *)notification {
    (void)notification;
    [self persistGeometry];
}
- (void)windowDidMove:(NSNotification *)notification {
    if (notification.object == main) {
        [NSObject cancelPreviousPerformRequestsWithTarget:self
                                                 selector:@selector(persistGeometry)
                                                   object:nil];
        [self performSelector:@selector(persistGeometry) withObject:nil afterDelay:0.4];
    }
}
- (void)persistGeometry {
    if (!engine)
        return;
    auto frame = main.frame;
    config["x"] = int(frame.origin.x);
    config["y"] = int(frame.origin.y);
    config["width"] = int(frame.size.width);
    config["height"] = int(frame.size.height);
    try {
        engine->configure(config);
    } catch (const std::exception &e) {
        status.stringValue = ns(notification_text(e.what()));
    }
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    (void)sender;
    return NO;
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    (void)sender;
    [timer invalidate];
    if (hotkey)
        UnregisterEventHotKey(hotkey);
    if (emergency)
        UnregisterEventHotKey(emergency);
    if (keyHandler)
        RemoveEventHandler(keyHandler);
    if (engine)
        engine->stop();
    return NSTerminateNow;
}
@end
int main(int argc, char **argv) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        BLApp *application = [BLApp new];
        for (int i = 1; i < argc; i++)
            if (std::string_view(argv[i]) == "--smoke-test")
                application->smoke = true;
        NSApp.delegate = application;
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp run];
        return 0;
    }
}
