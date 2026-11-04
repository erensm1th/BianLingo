// Member implementation included in App. Local drafts are committed only by
// the existing main settings Save button.
Json ocr_draft;
HWND ocr_window = nullptr, ocr_enable = nullptr, ocr_always = nullptr, ocr_info = nullptr, ocr_text = nullptr,
     ocr_selector = nullptr;
std::vector<std::pair<HWND, std::string>> ocr_language_checks;
POINT ocr_begin{}, ocr_end{};
bool ocr_drag = false;
Rect ocr_game_client{};
int ocr_game_dpi = 96;
static LRESULT CALLBACK ocr_select_proc(HWND h, UINT message, WPARAM w, LPARAM l) {
    auto self = reinterpret_cast<App *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<App *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self)
        return DefWindowProcW(h, message, w, l);
    switch (message) {
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) {
            DestroyWindow(h);
            return 0;
        }
        break;
    case WM_LBUTTONDOWN:
        self->ocr_begin = self->ocr_end = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        self->ocr_drag = true;
        SetCapture(h);
        return 0;
    case WM_MOUSEMOVE:
        if (self->ocr_drag) {
            self->ocr_end = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP: {
        self->ocr_drag = false;
        ReleaseCapture();
        int left = std::min(self->ocr_begin.x, LONG(GET_X_LPARAM(l))),
            top = std::min(self->ocr_begin.y, LONG(GET_Y_LPARAM(l)));
        int width = abs(self->ocr_begin.x - GET_X_LPARAM(l)),
            height = abs(self->ocr_begin.y - GET_Y_LPARAM(l));
        if (width >= 64 && height >= 32 && int64_t(width) * height <= 2'000'000) {
            self->ocr_draft["region"] = Json::array({left, top, width, height, self->ocr_game_client.w,
                                                     self->ocr_game_client.h, self->ocr_game_dpi});
            DestroyWindow(h);
        } else {
            set(self->ocr_info, "Selection must be at least 64×32 and no more than 2 million pixels. Please reselect.");
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint;
        auto dc = BeginPaint(h, &paint);
        RECT r{};
        GetClientRect(h, &r);
        FillRect(dc, &r, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        SetTextColor(dc, RGB(255, 255, 255));
        SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, wide(ui_text("Drag to select the native chat body area (avoid the input bar) · Esc to cancel")).c_str(), -1, &r, DT_TOP | DT_CENTER);
        if (self->ocr_drag) {
            RECT box{
                std::min(self->ocr_begin.x, self->ocr_end.x), std::min(self->ocr_begin.y, self->ocr_end.y),
                std::max(self->ocr_begin.x, self->ocr_end.x), std::max(self->ocr_begin.y, self->ocr_end.y)};
            FrameRect(dc, &box, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        }
        EndPaint(h, &paint);
        return 0;
    }
    case WM_DESTROY:
        self->ocr_selector = nullptr;
        EnableWindow(self->ocr_window, TRUE);
        ShowWindow(self->ocr_window, SW_SHOW);
        SetForegroundWindow(self->ocr_window);
        self->refresh_ocr_ui();
        return 0;
    }
    return DefWindowProcW(h, message, w, l);
}
void refresh_ocr_ui() {
    if (!ocr_window)
        return;
    auto saved = engine->settings().at("ocr");
    set(ocr_info, engine->ocr_status() + "\n" +
                      (ocr_draft.value("region", Json::array()).empty() ? "No area selected"
                                                                        : "Selection set; takes effect after saving in main settings"));
    set(ocr_text, engine->ocr_preview());
}
void capture_ocr_draft() {
    if (!ocr_window)
        return;
    bool always = SendMessageW(ocr_always, BM_GETCHECK, 0, 0) == BST_CHECKED;
    bool enabled = SendMessageW(ocr_enable, BM_GETCHECK, 0, 0) == BST_CHECKED || always;
    if (always)
        SendMessageW(ocr_enable, BM_SETCHECK, BST_CHECKED, 0);
    ocr_draft["enabled"] = enabled;
    ocr_draft["always"] = always;
    auto languages = Json::array();
    for (auto &[h, id] : ocr_language_checks)
        if (SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED)
            languages.push_back(id);
    ocr_draft["languages"] = languages;
}
void select_ocr_region() {
    try {
        capture_ocr_draft();
        auto screen = platform::make_capture();
        auto window = screen->game_window();
        if (!window)
            throw std::runtime_error("Open and show the World of Warcraft window first");
        ocr_game_client = window->client;
        ocr_game_dpi = int(GetDpiForWindow(reinterpret_cast<HWND>(window->id)));
        WNDCLASSW wc{};
        wc.lpfnWndProc = ocr_select_proc;
        wc.hInstance = instance;
        wc.lpszClassName = L"BianLingo.OcrSelect";
        wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
        RegisterClassW(&wc);
        ocr_selector =
            CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, wc.lpszClassName,
                            wide(ui_text("Select OCR region")).c_str(), WS_POPUP, ocr_game_client.x, ocr_game_client.y, ocr_game_client.w,
                            ocr_game_client.h, ocr_window, nullptr, instance, this);
        if (!ocr_selector)
            throw std::runtime_error("Cannot create OCR region selection window");
        SetLayeredWindowAttributes(ocr_selector, 0, 115, LWA_ALPHA);
        EnableWindow(ocr_window, FALSE);
        ShowWindow(ocr_selector, SW_SHOW);
        SetForegroundWindow(ocr_selector);
        SetFocus(ocr_selector);
    } catch (const std::exception &e) {
        set(ocr_info, e.what());
    }
}
void open_ocr_settings() {
    if (!ocr_window) {
        ocr_draft = config.at("ocr");
        ocr_window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"BianLingo.Window",
                                     wide(ui_text("Local OCR settings")).c_str(),
                                     WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, 100, 100,
                                     scale(640), scale(590), settings, nullptr, instance, nullptr);
        BOOL dark = TRUE;
        DwmSetWindowAttribute(ocr_window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        ocr_enable = control(ocr_window, L"BUTTON", "Automatically use OCR when chat text is restricted (off by default)", 20, 14, 590, 28,
                             BS_AUTOCHECKBOX);
        SendMessageW(ocr_enable, BM_SETCHECK, ocr_draft.value("enabled", false) ? BST_CHECKED : BST_UNCHECKED,
                     0);
        ocr_always = control(ocr_window, L"BUTTON", "OCR-only mode: continuously read chat, ignore addon chat data", 20, 48, 590,
                             28, BS_AUTOCHECKBOX);
        SendMessageW(ocr_always, BM_SETCHECK, ocr_draft.value("always", false) ? BST_CHECKED : BST_UNCHECKED,
                     0);
        control(ocr_window, L"STATIC", "Only recognizes selected chat body; new messages are translated per current API settings, read-only, no replies.", 20, 84, 590,
                25, SS_LEFT);
        control(ocr_window, L"BUTTON", "Select chat area", 20, 118, 170, 30, BS_PUSHBUTTON, id_ocr_region);
        int y = 159, x = 20;
        for (auto &model : ocr_models()) {
            auto h = control(ocr_window, L"BUTTON", model.name, x, y, 135, 28, BS_AUTOCHECKBOX);
            bool checked = std::find(ocr_draft["languages"].begin(), ocr_draft["languages"].end(),
                                     model.id) != ocr_draft["languages"].end();
            SendMessageW(h, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
            ocr_language_checks.emplace_back(h, model.id);
            x += 145;
        }
        control(ocr_window, L"STATIC", "Select up to three languages; click Save in main settings after changing toggles or selections.", 20, 196, 590,
                24, SS_LEFT);
        control(ocr_window, L"BUTTON", "Install saved language pack", 20, 231, 235, 30, BS_PUSHBUTTON, id_ocr_install);
        control(ocr_window, L"BUTTON", "Test recognition (no API request)", 280, 231, 300, 30, BS_PUSHBUTTON,
                id_ocr_test);
        ocr_info = control(ocr_window, L"STATIC", "", 20, 278, 590, 55, SS_LEFT);
        ocr_text = control(ocr_window, L"EDIT", "", 20, 343, 590, 130,
                           WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL);
        control(ocr_window, L"BUTTON", "Apply to draft and return", 350, 496, 260, 30, BS_PUSHBUTTON, id_ocr_close);
    }
    EnableWindow(settings, FALSE);
    ShowWindow(ocr_window, SW_SHOW);
    SetForegroundWindow(ocr_window);
    refresh_ocr_ui();
}
void close_ocr_settings() {
    capture_ocr_draft();
    ShowWindow(ocr_window, SW_HIDE);
    EnableWindow(settings, TRUE);
    SetForegroundWindow(settings);
}

#ifdef BL_UI_TESTS
void verify_ocr_ui() {
    auto before = engine->settings();
    auto draft = ocr_draft;
    open_ocr_settings();
    auto require = [](bool ok, const char *message) {
        if (!ok)
            throw std::runtime_error(message);
    };
    require(IsWindowVisible(ocr_window) && !IsWindowEnabled(settings), "OCR settings are not modal");
    require(ocr_language_checks.size() == ocr_models().size(), "Missing OCR language switches");
    SendMessageW(ocr_enable, BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessageW(ocr_always, BM_SETCHECK, BST_CHECKED, 0);
    capture_ocr_draft();
    require(ocr_draft["enabled"] == true && ocr_draft["always"] == true &&
                SendMessageW(ocr_enable, BM_GETCHECK, 0, 0) == BST_CHECKED && engine->settings() == before,
            "OCR draft editing changed live settings");
    SendMessageW(ocr_window, WM_COMMAND, id_ocr_test, 0);
    require(text(ocr_info) == ui_text("Return to main settings and save before installing language packs or testing"), "Unsaved OCR test did not require saving");
    SendMessageW(ocr_window, WM_CLOSE, 0, 0);
    require(!IsWindowVisible(ocr_window) && IsWindowEnabled(settings),
            "OCR close did not return to settings");
    ocr_draft = draft.is_null() ? before.at("ocr") : draft;
    SendMessageW(ocr_enable, BM_SETCHECK, ocr_draft.value("enabled", false) ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(ocr_always, BM_SETCHECK, ocr_draft.value("always", false) ? BST_CHECKED : BST_UNCHECKED, 0);
}
#endif
