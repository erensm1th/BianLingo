// App members: data-driven language pairs, filtered selector and download queue.
struct LocalModelTask { std::mutex mutex; ModelProgress progress; std::string error; bool done = false; };
HWND model_window{}, model_hint{}, model_progress{}, model_folder{}, model_search{}, model_pair{}, model_reverse{},
    model_list{}, model_summary{}, model_search_label{};
std::shared_ptr<LocalModelTask> model_task;
std::jthread model_worker;
std::vector<LocalLanguagePack> model_inventory;
std::vector<std::string> model_choices, model_queue;
std::string model_progress_caption;
std::string model_target_snapshot;
bool model_ct2 = false;
#ifdef BL_UI_TESTS
std::filesystem::path model_fixture_root;
Json ct2_fixture_models;
#endif
std::filesystem::path model_root() const {
#ifdef BL_UI_TESTS
    if (!model_fixture_root.empty()) return model_fixture_root;
#endif
    return platform::package_root() / "data" / (model_ct2 ? "ctranslate2" : "bergamot");
}
std::filesystem::path model_cache() const { return settings_data_directory() / (model_ct2 ? "ctranslate2" : "bergamot"); }
LocalModelStore model_store() const {
#ifdef BL_UI_TESTS
    if (model_ct2 && !ct2_fixture_models.is_null())
        return CTranslate2Models(model_root(), model_cache(), ct2_fixture_models, {},
            [](const auto &, std::stop_token stop) {
                // UI fixtures contain placeholder executables. Native process
                // loading/cancellation is covered by runtime_probe instead.
                if (stop.stop_requested()) throw std::runtime_error("Model preparation cancelled");
            });
#endif
    if (model_ct2) return CTranslate2Models(model_root(), model_cache());
    return BergamotModels(model_root(), model_cache());
}
std::string draft_pack() const {
    auto index = SendMessageW(chat_target, CB_GETCURSEL, 0, 0);
    return local_pack_for_target(index >= 0 && size_t(index) < chat_targets.size() ? chat_targets[size_t(index)] :
        config.value("target_language", default_target_language()));
}
static std::string model_size(uint64_t bytes) { return std::to_string((bytes + 512 * 1024) / (1024 * 1024)) + " MiB"; }
std::string model_language(std::string_view language) const {
    wchar_t name[160]{};
    return GetLocaleInfoEx(wide(language).c_str(), LOCALE_SNATIVEDISPLAYNAME, name, 160) ? narrow(name) : std::string(language);
}
const LocalLanguagePack &model_pack(std::string_view id) const {
    auto p = std::find_if(model_inventory.begin(), model_inventory.end(), [&](const auto &p) { return p.id == id; });
    if (p == model_inventory.end()) throw std::runtime_error("This local engine has no pack for the selected language");
    return *p;
}
std::string model_pair_name(const LocalLanguagePack &p) const { return model_language(p.source) + " → " + model_language(p.target); }
std::string model_state(const LocalLanguagePack &p) const {
    return ui_text(p.installed ? "Installed (verified on use)" : p.installed_bytes ? "Partially installed" : p.bundled ? "Bundled, ready to unpack" : "Not downloaded");
}
void update_model_queue() {
    if (!model_list) return;
    SendMessageW(model_list, LB_RESETCONTENT, 0, 0); uint64_t bytes = 0;
    for (const auto &id : model_queue) {
        const auto &p = model_pack(id); bytes += p.bytes;
        auto line = wide(model_pair_name(p) + " · " + model_state(p) + " · " + model_size(p.bytes));
        SendMessageW(model_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
    }
    set(model_summary, std::to_string(model_queue.size()) + " " + ui_text("directions") + " · " + ui_text("Installed size") + ": " + model_size(bytes));
}
void fill_model_pairs() {
    if (!model_pair) return;
    auto filter = lower_ascii(trim(text(model_search)));
    auto current = SendMessageW(model_pair, CB_GETCURSEL, 0, 0);
    auto previous = current >= 0 && size_t(current) < model_choices.size() ? model_choices[size_t(current)] : std::string();
    model_choices.clear(); SendMessageW(model_pair, CB_RESETCONTENT, 0, 0);
    for (const auto &p : model_inventory) {
        auto caption = model_pair_name(p) + " [" + p.id + "]";
        if (!filter.empty() && lower_ascii(caption).find(filter) == caption.npos) continue;
        auto value = wide(caption); SendMessageW(model_pair, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value.c_str()));
        model_choices.push_back(p.id);
    }
    auto selected = std::find(model_choices.begin(), model_choices.end(), previous);
    SendMessageW(model_pair, CB_SETCURSEL, selected != model_choices.end() ? selected - model_choices.begin() : model_choices.empty() ? -1 : 0, 0);
}
void refresh_model_inventory() { model_inventory = model_store().inventory(); fill_model_pairs(); update_model_queue(); }
void queue_model_pair(std::string id) {
    model_pack(id);
    if (std::find(model_queue.begin(), model_queue.end(), id) == model_queue.end()) model_queue.push_back(std::move(id));
}
void choose_current_pack() {
    auto target = draft_pack(), source = target == "en" ? std::string("zh-Hans") : std::string("en");
    model_queue.clear(); model_target_snapshot = target; update_model_queue();
    auto store = model_store(); auto required = store.route(source, target);
    if (SendMessageW(model_reverse, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        try { auto reverse = store.route(target, source); required.insert(required.end(), reverse.begin(), reverse.end()); }
        catch (const std::exception &) { /* One-way catalogue entries remain usable. */ }
    }
    model_queue.clear(); for (const auto &id : required) queue_model_pair(id); update_model_queue();
    model_target_snapshot = target;
    set(model_progress, "Required models are listed. Click Prepare to download.");
}
void add_selected_model_pair() {
    auto selected = SendMessageW(model_pair, CB_GETCURSEL, 0, 0);
    if (selected < 0 || size_t(selected) >= model_choices.size()) return;
    const auto &p = model_pack(model_choices[size_t(selected)]); queue_model_pair(p.id);
    if (SendMessageW(model_reverse, BM_GETCHECK, 0, 0) == BST_CHECKED)
        try { for (const auto &id : model_store().route(p.target, p.source)) queue_model_pair(id); } catch (const std::exception &) {}
    update_model_queue();
}
void remove_selected_model_pairs() {
    auto count = SendMessageW(model_list, LB_GETSELCOUNT, 0, 0); if (count <= 0) return;
    std::vector<int> selected(size_t(count), 0); SendMessageW(model_list, LB_GETSELITEMS, selected.size(), reinterpret_cast<LPARAM>(selected.data()));
    std::sort(selected.rbegin(), selected.rend());
    for (auto index : selected) if (index >= 0 && size_t(index) < model_queue.size()) model_queue.erase(model_queue.begin() + index);
    update_model_queue();
}
void update_model_controls() {
    if (!model_window) return;
    const bool running = bool(model_task);
    for (auto h : {model_search, model_pair, model_reverse, model_list}) EnableWindow(h, !running);
    for (int id : {id_models_current, id_models_all, id_models_prepare, id_models_add, id_models_remove}) EnableWindow(GetDlgItem(model_window, id), !running);
    EnableWindow(GetDlgItem(model_window, id_models_cancel), running && !model_worker.get_stop_token().stop_requested());
}
void layout_model_window() {
    if (!model_window) return;
    RECT r{}; GetClientRect(model_window, &r); int width = MulDiv(r.right, 96, int(GetDpiForWindow(model_window))), half = (width - 52) / 2;
    auto move = [&](HWND h, int x, int y, int w, int height) { MoveWindow(h, scale(x, model_window), scale(y, model_window), scale(w, model_window), scale(height, model_window), TRUE); };
    move(model_hint, 20, 18, width - 40, 86); move(model_search_label, 20, 118, 160, 30); move(model_search, 192, 118, width - 212, 30);
    move(model_pair, 20, 160, width - 40, 30); move(model_reverse, 20, 202, width - 40, 30);
    move(GetDlgItem(model_window, id_models_add), 20, 244, half, 36); move(GetDlgItem(model_window, id_models_current), 32 + half, 244, half, 36);
    move(model_list, 20, 292, width - 40, 126); move(model_summary, 20, 432, half, 36);
    move(GetDlgItem(model_window, id_models_remove), 32 + half, 432, half, 36);
    move(GetDlgItem(model_window, id_models_prepare), 20, 480, half, 36); move(GetDlgItem(model_window, id_models_cancel), 32 + half, 480, half, 36);
    move(model_progress, 20, 528, width - 40, 48); move(model_folder, 20, 588, half, 34); move(GetDlgItem(model_window, id_models_all), 32 + half, 588, half, 34);
}
void open_model_window() {
    bool requested_ct2 = provider_draft.at(provider_index).at("format") == "ctranslate2_local";
    if (!model_task && model_ct2 != requested_ct2) {
        model_ct2 = requested_ct2; model_queue.clear(); model_target_snapshot.clear();
        if (model_search) set(model_search, "");
    }

    if (!model_window) {
        model_window = CreateWindowExW(WS_EX_APPWINDOW, L"BianLingo.Window", wide(ui_text("Bergamot language packs")).c_str(),
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, scale(800), scale(680), nullptr, nullptr, instance, nullptr);
        if (!model_window) throw std::runtime_error("Could not create the language pack window");
        BOOL dark = GetRValue(bg) + GetGValue(bg) + GetBValue(bg) < 450;
        DwmSetWindowAttribute(model_window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark)); DwmSetWindowAttribute(model_window, DWMWA_CAPTION_COLOR, &bg, sizeof(bg));
        model_hint = control(model_window, L"STATIC", "Plan models for the current target, or search other direct pairs. Other languages can pivot through English. Nothing downloads until you prepare.", 20, 18, 740, 86, SS_LEFT);
        model_search_label = control(model_window, L"STATIC", "Search pairs", 20, 118, 112, 30, SS_LEFT);
        model_search = control(model_window, L"EDIT", "", 144, 118, 616, 30, WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, id_models_search);
        model_pair = control(model_window, WC_COMBOBOXW, "", 20, 160, 740, 240, CBS_DROPDOWNLIST | WS_TABSTOP, id_models_pair);
        model_reverse = control(model_window, L"BUTTON", "Also add the reverse direction", 20, 202, 740, 30, BS_AUTOCHECKBOX | WS_TABSTOP); SendMessageW(model_reverse, BM_SETCHECK, BST_CHECKED, 0);
        control(model_window, L"BUTTON", "Add to download queue", 20, 244, 364, 36, BS_PUSHBUTTON | WS_TABSTOP, id_models_add);
        control(model_window, L"BUTTON", "Models for current target", 396, 244, 364, 36, BS_PUSHBUTTON | WS_TABSTOP, id_models_current);
        model_list = control(model_window, L"LISTBOX", "", 20, 292, 740, 126, WS_BORDER | LBS_EXTENDEDSEL | LBS_NOINTEGRALHEIGHT | WS_TABSTOP);
        SetWindowSubclass(model_list, [](HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) -> LRESULT {
            if (msg == WM_MOUSEWHEEL) { auto top = SendMessageW(h, LB_GETTOPINDEX, 0, 0), count = SendMessageW(h, LB_GETCOUNT, 0, 0);
                SendMessageW(h, LB_SETTOPINDEX, std::clamp<int>(int(top) - GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA * 3, 0, std::max(0, int(count) - 1)), 0); return 0; }
            return DefSubclassProc(h, msg, w, l);
        }, 33, 0);
        model_summary = control(model_window, L"STATIC", "", 20, 432, 364, 36, SS_LEFT);
        control(model_window, L"BUTTON", "Remove selected", 396, 432, 364, 36, BS_PUSHBUTTON | WS_TABSTOP, id_models_remove);
        control(model_window, L"BUTTON", "Prepare download queue", 20, 480, 364, 36, BS_PUSHBUTTON | WS_TABSTOP, id_models_prepare);
        control(model_window, L"BUTTON", "Cancel download", 396, 480, 364, 36, BS_PUSHBUTTON | WS_TABSTOP, id_models_cancel);
        model_progress = control(model_window, L"STATIC", "Not started. Installed files will be verified and skipped.", 20, 528, 740, 62, SS_LEFT);
        model_folder = control(model_window, L"BUTTON", "Open models folder", 20, 588, 364, 34, BS_PUSHBUTTON | WS_TABSTOP, id_models_folder);
        control(model_window, L"BUTTON", "Clear download queue", 396, 588, 364, 34, BS_PUSHBUTTON | WS_TABSTOP, id_models_all);
        muted_controls.insert(model_hint); refresh_interface_fonts();
    }
    SetWindowTextW(model_window, wide(model_ct2 ? ui_text("CTranslate2 language packs") : ui_text("Bergamot language packs")).c_str());
    set(model_hint, model_ct2 ? "CTranslate2: the native worker is included. Only selected models are downloaded; never during chat." :
        "Plan models for the current target, or search other direct pairs. Other languages can pivot through English. Nothing downloads until you prepare.");
    if (!model_task) {
        refresh_model_inventory();
        if (model_queue.empty() || model_target_snapshot != draft_pack()) { try { choose_current_pack(); } catch (const std::exception &e) { set(model_progress, e.what()); } }
    }
    layout_model_window(); update_model_controls(); ShowWindow(model_window, SW_SHOW); SetForegroundWindow(model_window);
}
void start_model_job() {
    if (model_task) return;
    if (model_queue.empty()) throw std::runtime_error("Select at least one language pack");
    if (smoke) return;
    if (!model_ct2 && !std::filesystem::is_regular_file(model_root() / "BianLingoBergamot.exe")) throw std::runtime_error("Bergamot components are missing. Install the full package with models.");
    launch_model_job(model_store(), model_queue);
}
