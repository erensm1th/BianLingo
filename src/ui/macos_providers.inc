- (void)captureProvider {
    if (providerSync || providerDraft.empty() || !providerName)
        return;
    auto &p = providerDraft.at(providerIndex);
    p["name"] = trim(str(providerName.stringValue));
    p["base_url"] = trim(str(fields[@"base_url"].stringValue));
    p["model"] = trim(str(fields[@"model"].stringValue));
    auto key = trim(str(apiKey.stringValue));
    if (key.empty())
        p.erase("key");
    else
        p["key"] = key;
}
- (void)fillProvider {
    providerSync = YES;
    [providerChoice removeAllItems];
    for (const auto &p : providerDraft)
        [providerChoice addItemWithTitle:ns(p.at("name").get<std::string>())];
    [providerChoice selectItemAtIndex:providerIndex];
    auto &p = providerDraft.at(providerIndex);
    providerName.stringValue = ns(p.at("name").get<std::string>());
    fields[@"base_url"].stringValue = ns(p.at("base_url").get<std::string>());
    fields[@"model"].stringValue = ns(p.at("model").get<std::string>());
    apiKey.stringValue = ns(p.value("key", std::string()));
    providerSync = NO;
}
- (void)loadProviders {
    providerDraft = engine->connections();
    providerIndex = 0;
    for (size_t i = 0; i < providerDraft.size(); ++i)
        if (providerDraft[i]["id"] == config["active_connection"])
            providerIndex = i;
    [self fillProvider];
}
- (void)selectProvider:(id)sender {
    (void)sender;
    if (providerSync || providerChoice.indexOfSelectedItem < 0)
        return;
    [self captureProvider];
    providerIndex = providerChoice.indexOfSelectedItem;
    [self fillProvider];
    status.stringValue = @"Connection selection changed; save settings to apply";
}
- (void)addProvider:(id)sender {
    (void)sender;
    [self captureProvider];
    if (providerDraft.size() >= 32) {
        status.stringValue = @"At most 32 connections can be saved";
        return;
    }
    providerDraft.push_back(public_connections(Json::array({new_connection()})).at(0));
    providerIndex = providerDraft.size() - 1;
    [self fillProvider];
    [settings makeFirstResponder:providerName];
}
- (void)deleteProvider:(id)sender {
    (void)sender;
    if (providerDraft.size() <= 1) {
        status.stringValue = @"Keep at least one connection";
        return;
    }
    providerDraft.erase(providerDraft.begin() + providerIndex);
    providerIndex = std::min(providerIndex, providerDraft.size() - 1);
    [self fillProvider];
    status.stringValue = @"Connection will be deleted after saving settings";
}
- (BOOL)providerDirty {
    [self captureProvider];
    auto current = engine->connections();
    auto &draft = providerDraft.at(providerIndex);
    if (draft.at("id") != config["active_connection"] || draft.contains("key"))
        return YES;
    for (auto &p : current)
        if (p.at("id") == draft.at("id"))
            return p != draft;
    return YES;
}
- (void)testProvider:(id)sender {
    (void)sender;
    try {
        if (providerTesting)
            return;
        if ([self providerDirty])
            throw std::runtime_error("Save the current connection before sending a test request");
        engine->test_connection();
        providerTesting = YES;
        providerTestButton.enabled = NO;
        status.stringValue = @"Sending a short connection test…";
    } catch (const std::exception &e) {
        status.stringValue = ns(notification_text(e.what()));
    }
}
- (void)advancedProvider:(id)sender {
    (void)sender;
    [self captureProvider];
    auto profile = providerDraft.at(providerIndex);
    Json options = Json::object();
    for (auto key : {"format", "auth", "full_url", "headers", "parameters", "balance"})
        options[key] = profile.at(key);
    NSAlert *alert = [NSAlert new];
    alert.messageText = @"Advanced connection options";
    alert.informativeText = @"format：openai_chat / openai_responses / anthropic / gemini\nauth：auto / "
                            @"bearer / x-api-key / x-goog-api-key / "
                            @"none\nfull_"
                            @"url: full request URL; headers and parameters: JSON objects. Balance is optional and defaults to type none"
                            @". All connection data is encrypted locally.";
    [alert addButtonWithTitle:@"Apply to draft"];
    [alert addButtonWithTitle:@"Cancel"];
    NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 550, 340)];
    NSTextView *editor = [[NSTextView alloc] initWithFrame:scroll.bounds];
    editor.richText = NO;
    editor.font = [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];
    editor.string = ns(options.dump(2));
    editor.verticallyResizable = YES;
    scroll.documentView = editor;
    scroll.hasVerticalScroller = YES;
    alert.accessoryView = scroll;
    if ([alert runModal] != NSAlertFirstButtonReturn)
        return;
    try {
        Json values;
        try {
            values = Json::parse(str(editor.string));
        } catch (const std::exception &) {
            throw std::runtime_error("Advanced options must contain a valid JSON object");
        }
        if (!values.is_object())
            throw std::runtime_error("Advanced options must be a JSON object");
        for (auto it = values.begin(); it != values.end(); ++it) {
            if (!options.contains(it.key()))
                throw std::runtime_error("Advanced options contain unknown fields");
            profile[it.key()] = it.value();
        }
        auto complete = merge_connections(Json::array({profile}), engine->connections());
        if (complete[0]["base_url"] == "")
            complete[0]["base_url"] = "https://example.invalid/v1";
        if (complete[0]["model"] == "")
            complete[0]["model"] = "unconfigured";
        validate_connections(complete, profile.at("id").get<std::string>());
        providerDraft[providerIndex] = std::move(profile);
        status.stringValue = @"Advanced options added to the draft; save settings to apply";
    } catch (const std::exception &e) {
        status.stringValue = ns(notification_text(e.what()));
    }
}
