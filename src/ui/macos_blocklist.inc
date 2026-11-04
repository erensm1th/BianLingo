// Native AppKit message menu; shared data/merge rules live in PlayerBlocklist.
- (void)showMessageMenu:(NSUInteger)character event:(NSEvent *)event {
    if (messagePreview) return;
    for (const auto &row : chatRows) {
        if (!NSLocationInRange(character, row.range) || row.index >= messages.size()) continue;
        contextMessage = messages[row.index].message;
        NSMenu *menu = [NSMenu new]; menu.autoenablesItems = NO;
        for (auto [caption, action] : {std::pair{"Whisper", @selector(messageWhisper:)},
                                      {"Check level (in-game /who)", @selector(messageWho:)},
                                      {"Block this player", @selector(messageBlock:)}}) {
            auto item = [[NSMenuItem alloc] initWithTitle:ns(ui_text(caption)) action:action keyEquivalent:@""];
            item.target = self;
            item.enabled = action == @selector(messageWhisper:) ? bool(whisper_target(*contextMessage)) :
                           !block_player_name(*contextMessage).empty() && (action != @selector(messageWho:) || !reply.busy());
            [menu addItem:item];
        }
        [NSMenu popUpContextMenu:menu withEvent:event forView:chat];
        return;
    }
}
- (void)messageWhisper:(id)sender {
    (void)sender;
    if (contextMessage) [self replyMessage:*contextMessage whisper:YES];
}
- (void)messageWho:(id)sender {
    (void)sender;
    if (!contextMessage || reply.busy()) return;
    auto name = block_player_name(*contextMessage);
    if (name.empty()) return;
    name = name.substr(0, name.find('-'));
    auto command = "/who " + name;
    [NSPasteboard.generalPasteboard clearContents];
    [NSPasteboard.generalPasteboard setString:ns(command) forType:NSPasteboardTypeString];
    status.stringValue = ns(ui_text("Level query copied. Paste it in game and press Enter; view the result in game."));
    for (NSRunningApplication *app in NSWorkspace.sharedWorkspace.runningApplications)
        if ([app.bundleIdentifier.lowercaseString containsString:@"worldofwarcraft"])
            [app activateWithOptions:NSApplicationActivateIgnoringOtherApps];
}
- (void)messageBlock:(id)sender {
    (void)sender;
    if (!contextMessage) return;
    try {
        engine->set_player_blocked(block_player_name(*contextMessage), true);
        [self render]; [self refreshBlockManager];
    } catch (const std::exception &e) { status.stringValue = ns(ui_text(e.what())); }
}
- (void)refreshBlockManager {
    if (!blockNames) return;
    PlayerBlocklist list(engine->blocklist());
    std::string text;
    for (const auto &name : list.names()) text += name + "\n";
    blockNames.string = ns(text);
    if (blockStatus) blockStatus.stringValue = ns(ui_text("Saved here. Copy the sync code and import it in the addon’s blocklist window."));
}
- (void)openBlockManager:(id)sender {
    (void)sender;
    if (!blockWindow) {
        blockWindow = [[NSWindow alloc] initWithContentRect:NSMakeRect(160, 100, 660, 660)
            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:NO];
        blockWindow.title = ns(ui_text("Blocked players")); blockWindow.releasedWhenClosed = NO;
        NSStackView *stack = [NSStackView new];
        stack.orientation = NSUserInterfaceLayoutOrientationVertical; stack.alignment = NSLayoutAttributeLeading; stack.spacing = 12;
        stack.translatesAutoresizingMaskIntoConstraints = NO;
        [blockWindow.contentView addSubview:stack];
        [NSLayoutConstraint activateConstraints:@[
            [stack.leadingAnchor constraintEqualToAnchor:blockWindow.contentView.leadingAnchor constant:20],
            [stack.trailingAnchor constraintEqualToAnchor:blockWindow.contentView.trailingAnchor constant:-20],
            [stack.topAnchor constraintEqualToAnchor:blockWindow.contentView.topAnchor constant:20]
        ]];
        auto label = [&](const char *value) {
            auto field = [NSTextField wrappingLabelWithString:ns(ui_text(value))];
            [field.widthAnchor constraintEqualToConstant:620].active = YES;
            [stack addArrangedSubview:field]; return field;
        };
        auto button = [&](const char *value, SEL selector) {
            auto control = [NSButton buttonWithTitle:ns(ui_text(value)) target:self action:selector];
            [stack addArrangedSubview:control]; return control;
        };
        label("Hidden immediately here. Paste a sync code into the addon to apply there. A name without a realm matches all namesakes.");
        NSScrollView *scroll = [NSScrollView new]; scroll.hasVerticalScroller = YES; scroll.autohidesScrollers = YES;
        blockNames = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 620, 160)];
        blockNames.editable = NO; scroll.documentView = blockNames;
        [scroll.widthAnchor constraintEqualToConstant:620].active = YES;
        [scroll.heightAnchor constraintEqualToConstant:160].active = YES;
        [stack addArrangedSubview:scroll];
        label("Player name (optional -Realm)");
        blockNameInput = [NSTextField new]; [blockNameInput.widthAnchor constraintEqualToConstant:620].active = YES;
        [stack addArrangedSubview:blockNameInput];
        button("Add block", @selector(addBlock:)); button("Unblock entered name", @selector(removeBlock:));
        label("In game: /bl blocks → Import from receiver. Paste the code and apply. WoW saves the list; no folder is needed.");
        button("Copy sync code (paste in game)", @selector(copyBlockCode:));
        blockStatus = label("");
    }
    [self refreshBlockManager]; [blockWindow makeKeyAndOrderFront:nil];
}
- (void)addBlock:(id)sender {
    (void)sender;
    try { engine->set_player_blocked(str(blockNameInput.stringValue), true); [self render]; [self refreshBlockManager]; }
    catch (const std::exception &e) { blockStatus.stringValue = ns(ui_text(e.what())); }
}
- (void)removeBlock:(id)sender {
    (void)sender;
    try { engine->set_player_blocked(str(blockNameInput.stringValue), false); [self render]; [self refreshBlockManager]; }
    catch (const std::exception &e) { blockStatus.stringValue = ns(ui_text(e.what())); }
}
- (void)copyBlockCode:(id)sender {
    (void)sender;
    try {
        auto code = blocklist_import_code(PlayerBlocklist(engine->blocklist()));
        [NSPasteboard.generalPasteboard clearContents];
        if (![NSPasteboard.generalPasteboard setString:ns(code) forType:NSPasteboardTypeString])
            throw std::runtime_error("Could not copy the sync code; retry");
        auto note = ns(ui_text("Code copied. Paste and import it in /bl blocks to apply in game."));
        status.stringValue = note; if (blockStatus) blockStatus.stringValue = note;
    } catch (const std::exception &e) { if (blockStatus) blockStatus.stringValue = ns(ui_text(e.what())); }
}
