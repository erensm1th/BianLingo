// Members of App. API drafts use Save; local engine selection and explicit model
// Apply are immediate. Merely choosing a model never starts a download.
Json provider_draft;
size_t provider_index = 0;
bool provider_sync = false, provider_testing = false;
HWND provider_select{}, provider_name{}, provider_name_label{}, provider_key_label{}, provider_setup{},
    provider_url_label{}, provider_model_label{}, provider_local_note{}, provider_advanced{};
HANDLE ollama_setup_process{};
HANDLE ollama_setup_job{};
std::filesystem::path ollama_setup_result;
std::string ollama_setup_model;
HWND ollama_model_label{}, ollama_model_choice{}, ollama_custom_label{}, ollama_custom_model{}, ollama_apply{};
std::vector<HWND> provider_advanced_labels;
HWND provider_format{}, provider_auth{}, provider_full_url{}, provider_parameters{}, provider_headers{},
    provider_balance_config{}, provider_advanced_status{};
const std::vector<std::pair<std::string, std::string>> provider_formats = {
    {"openai_chat", "OpenAI Chat Completions (default)"},
    {"openai_responses", "OpenAI Responses"},
    {"anthropic", "Claude Messages"},
    {"gemini", "Gemini Native"}};
const std::vector<std::pair<std::string, std::string>> provider_auths = {
    {"auto", "Auto-select by protocol (default)"},
    {"bearer", "Authorization: Bearer"},
    {"x-api-key", "x-api-key"},
    {"x-goog-api-key", "x-goog-api-key"},
    {"none", "No auth / auth via custom headers"}};
HWND provider_edit(const char *key) const {
    auto found = std::find_if(edits.begin(), edits.end(), [&](auto &c) { return c.key == key; });
    return found == edits.end() ? nullptr : found->h;
}
std::string selected_ollama_model() const {
    auto index = SendMessageW(ollama_model_choice, CB_GETCURSEL, 0, 0);
    return index >= 0 && size_t(index) < ollama_model_options.size()
               ? std::string(ollama_model_options[size_t(index)].name)
               : trim(text(ollama_custom_model));
}
void show_ollama_model_controls() {
    bool show = active_page == 2 && !provider_draft.empty() &&
                provider_draft.at(provider_index).at("format") == "ollama_local";
    bool custom = SendMessageW(ollama_model_choice, CB_GETCURSEL, 0, 0) == LRESULT(ollama_model_options.size());
    for (auto h : {ollama_model_label, ollama_model_choice, ollama_apply})
        ShowWindow(h, show ? SW_SHOW : SW_HIDE);
    for (auto h : {ollama_custom_label, ollama_custom_model})
        ShowWindow(h, show && custom ? SW_SHOW : SW_HIDE);
    for (auto h : {ollama_model_choice, ollama_custom_model, ollama_apply, provider_setup})
        EnableWindow(h, !ollama_setup_process);
}
void select_ollama_model() {
    if (provider_sync || provider_draft.empty() ||
        provider_draft.at(provider_index).at("format") != "ollama_local")
        return;
    provider_draft.at(provider_index)["model"] = selected_ollama_model();
    show_ollama_model_controls();
    layout_body();
}
void capture_provider() {
    if (provider_sync || provider_draft.empty() || !provider_name)
        return;
    auto &p = provider_draft.at(provider_index);
    if (p.at("format") == "ollama_local") {
        p["model"] = selected_ollama_model();
        return;
    }
    if (p.at("format") == "bergamot_local" || p.at("format") == "ctranslate2_local")
        return;
    p["name"] = trim(text(provider_name));
    p["base_url"] = trim(text(provider_edit("base_url")));
    p["model"] = trim(text(provider_edit("model")));
    auto key = trim(text(api_key));
    if (!key.empty())
        p["key"] = key;
    else
        p.erase("key");
}
void fill_provider() {
    if (!provider_select || provider_draft.empty())
        return;
    provider_sync = true;
    SendMessageW(provider_select, CB_RESETCONTENT, 0, 0);
    for (const auto &p : provider_draft) {
        const auto local = p.at("format") == "ollama_local" || p.at("format") == "bergamot_local" ||
                           p.at("format") == "ctranslate2_local";
        auto raw = p.at("name").get<std::string>();
        auto name = wide(local ? ui_text(raw) : raw);
        SendMessageW(provider_select, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    }
    SendMessageW(provider_select, CB_SETCURSEL, provider_index, 0);
    auto &p = provider_draft.at(provider_index);
    set(provider_name, (p.at("format") == "ollama_local" || p.at("format") == "bergamot_local" ||
                        p.at("format") == "ctranslate2_local")
                           ? ui_text(p.at("name").get<std::string>())
                           : p.at("name").get<std::string>());
    set(provider_edit("base_url"), p.at("base_url").get<std::string>());
    set(provider_edit("model"), p.at("model").get<std::string>());
    set(api_key, p.value("key", std::string()));
    bool bergamot = p.at("format") == "bergamot_local";
    bool ct2 = p.at("format") == "ctranslate2_local";
    bool local = p.at("format") == "ollama_local" || bergamot || ct2;
    if (p.at("format") == "ollama_local") {
        auto model = p.at("model").get<std::string>();
        size_t index = 0;
        while (index < ollama_model_options.size() && ollama_model_options[index].name != model)
            ++index;
        SendMessageW(ollama_model_choice, CB_SETCURSEL, index, 0);
        set(ollama_custom_model, model);
    }
    set(provider_key_label, local ? (bergamot ? "Local Bergamot needs no API key" :
                                     ct2 ? "Local CTranslate2 needs no API key" : "Local Ollama needs no API key") :
        p.value("has_key", false) ? "API Key (saved; leave blank to keep current)" : "API Key (encrypted locally only)");
    for (auto h : {provider_name_label, provider_name, provider_key_label, api_key,
                   provider_url_label, provider_edit("base_url"), provider_model_label,
                   provider_edit("model")})
        ShowWindow(h, !local && active_page == 2 ? SW_SHOW : SW_HIDE);
    ShowWindow(provider_local_note, local && active_page == 2 ? SW_SHOW : SW_HIDE);
    ShowWindow(provider_setup, local && !bergamot && !ct2 && active_page == 2 ? SW_SHOW : SW_HIDE);
    for (int id : {id_models_now, id_models_open}) ShowWindow(GetDlgItem(body, id), (bergamot || ct2) && active_page == 2 ? SW_SHOW : SW_HIDE);
    show_ollama_model_controls();
    set(provider_local_note, bergamot ?
        "Bergamot uses English pairs for the chat target language. English–Chinese is bundled; prepare other packs before playing. No automatic downloads." :
        ct2 ? "CTranslate2: prepare for your chat target or choose language pairs. Portable runtime included on first preparation. Downloads are manual." :
        "Ollama models support multiple languages; no separate language packs. Apply uses an installed model; only Download fetches files. Runtime size is additional.");
    EnableWindow(GetDlgItem(body, id_provider_advanced), !local);
    EnableWindow(GetDlgItem(body, id_balance), !local);
    EnableWindow(GetDlgItem(body, id_provider_delete), !local);
    provider_sync = false;
    layout_body();
}
void load_providers() {
    provider_draft = engine->connections();
    provider_index = 0;
    for (size_t i = 0; i < provider_draft.size(); ++i)
        if (provider_draft[i]["id"] == config["active_connection"])
            provider_index = i;
    fill_provider();
}
void select_provider() {
    if (provider_sync)
        return;
    auto index = SendMessageW(provider_select, CB_GETCURSEL, 0, 0);
    if (index < 0 || size_t(index) >= provider_draft.size())
        return;
    capture_provider();
    provider_index = size_t(index);
    fill_provider();
    const auto &p = provider_draft.at(provider_index);
    bool local = p.at("format") == "ollama_local" || p.at("format") == "bergamot_local" ||
                 p.at("format") == "ctranslate2_local";
    if (local) {
        auto next = engine->settings();
        next["active_connection"] = p.at("id");
        engine->configure(next, std::nullopt, std::nullopt);
        config = engine->settings();
        update_account_view();
        set(settings_status, "Switched to" + p.at("name").get<std::string>());
    } else {
        set(settings_status, "API connection selected; takes effect after saving settings");
    }
}
void add_provider() {
    capture_provider();
    if (provider_draft.size() >= 35)
        throw std::runtime_error("Save up to 32 API connections");
    provider_draft.push_back(public_connections(Json::array({new_connection()})).at(0));
    provider_index = provider_draft.size() - 1;
    fill_provider();
    SetFocus(provider_name);
    set(settings_status, "Enter address, key and model, then save; any vendor or model name allowed");
}
void add_local_provider() {
    SendMessageW(provider_select, CB_SETCURSEL, 0, 0);
    select_provider();
}
void add_bergamot_provider() {
    SendMessageW(provider_select, CB_SETCURSEL, 1, 0);
    select_provider();
}
void add_ctranslate2_provider() {
    SendMessageW(provider_select, CB_SETCURSEL, 2, 0);
    select_provider();
}
void persist_ollama_model(const std::string &model) {
    if (!valid_ollama_download_model(model))
        throw std::runtime_error("Ollama model name contains characters unsupported by the installer");
    // A background download may complete while the user is editing an API form.
    // Capture that form before fill_provider repaints it, without submitting it.
    capture_provider();
    auto profiles = engine->connections();
    for (auto &p : profiles)
        if (p.at("format") == "ollama_local")
            p["model"] = model;
    // Save only this local model; do not commit unsaved API drafts or change the active provider.
    auto previous_settings = engine->settings();
    engine->configure(previous_settings, std::nullopt, profiles);
    config = engine->settings();
    if (config.at("active_connection") == "local-ollama" && config.at("model") != previous_settings.at("model")) {
        provider_testing = false;
        EnableWindow(GetDlgItem(body, id_provider_test), TRUE);
    }
    for (auto &p : provider_draft)
        if (p.at("format") == "ollama_local")
            p["model"] = model;
    fill_provider();
    update_account_view();
}
void apply_ollama_model() {
    if (ollama_setup_process || provider_draft.at(provider_index).at("format") != "ollama_local")
        return;
    persist_ollama_model(selected_ollama_model());
    set(settings_status, "Model applied. If not installed yet, click Download.");
}
void setup_ollama() {
    if (ollama_setup_process || provider_draft.at(provider_index).at("format") != "ollama_local")
        return;
    const auto model = selected_ollama_model();
    if (!valid_ollama_download_model(model))
        throw std::runtime_error("Ollama model name contains characters unsupported by the installer");
    auto directory = platform::app_data();
    std::filesystem::create_directories(directory);
    auto script_path = directory / "ollama-setup.ps1";
    ollama_setup_result = directory / "ollama-setup-result.txt";
    std::error_code ignored;
    std::filesystem::remove(ollama_setup_result, ignored);
    auto script = std::string("\xef\xbb\xbf") + std::string(bundled_text("ollama-setup.ps1"));
    for (const auto &[name,resource] : {std::pair{"ollama-runtime-license.txt","licenses/Ollama.txt"},
                                     std::pair{"ollama-runtime-notices.txt","licenses/OllamaRuntime.txt"}}) {
        auto contents=bundled_text(resource);
        atomic_write(directory/name,std::span(reinterpret_cast<const uint8_t*>(contents.data()),contents.size()));
    }
    atomic_write(script_path, std::span<const uint8_t>(
        reinterpret_cast<const uint8_t *>(script.data()), script.size()));
    auto parameters = std::wstring(L"-NoProfile -ExecutionPolicy Bypass -File \"") +
                      script_path.wstring() + L"\" -Model \"" + wide(model) + L"\"" +
                      L" -Root \"" + platform::executable_dir().wstring() + L"\"" +
                      L" -Result \"" + ollama_setup_result.wstring() + L"\"";
    pause_portable_ollama(true);
    ollama_setup_job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!ollama_setup_job || !SetInformationJobObject(ollama_setup_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        if (ollama_setup_job) CloseHandle(ollama_setup_job); ollama_setup_job = nullptr;
        pause_portable_ollama(false); throw std::runtime_error("Could not create the Ollama process container");
    }
    SHELLEXECUTEINFOW launch{sizeof(launch)};
    launch.fMask = SEE_MASK_NOCLOSEPROCESS;
    launch.hwnd = settings;
    launch.lpVerb = L"open";
    launch.lpFile = L"powershell.exe";
    launch.lpParameters = parameters.c_str();
    auto working_directory = platform::executable_dir().wstring(); launch.lpDirectory = working_directory.c_str();
    launch.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&launch) || !launch.hProcess) {
        CloseHandle(ollama_setup_job); ollama_setup_job = nullptr; pause_portable_ollama(false);
        throw std::runtime_error("Failed to launch Ollama installer window");
    }
    if (!AssignProcessToJobObject(ollama_setup_job, launch.hProcess)) {
        TerminateProcess(launch.hProcess, 1); CloseHandle(launch.hProcess); CloseHandle(ollama_setup_job);
        ollama_setup_job = nullptr; pause_portable_ollama(false); throw std::runtime_error("Could not manage portable Ollama");
    }
    ollama_setup_process = launch.hProcess;
    ollama_setup_model = model;
    show_ollama_model_controls();
    status_note = "Installing or checking Ollama and models; see the progress window.";
    set(settings_status, status_note);
}
void poll_ollama_setup() {
    if (!ollama_setup_process || WaitForSingleObject(ollama_setup_process, 0) != WAIT_OBJECT_0)
        return;
    DWORD code = 1;
    GetExitCodeProcess(ollama_setup_process, &code);
    CloseHandle(ollama_setup_process);
    ollama_setup_process = nullptr;
    if (ollama_setup_job) CloseHandle(ollama_setup_job); ollama_setup_job = nullptr;
    pause_portable_ollama(false);
    show_ollama_model_controls();
    std::string result;
    try {
        auto bytes = read_bytes(ollama_setup_result);
        result.assign(bytes.begin(), bytes.end());
    } catch (...) {}
    finish_ollama_setup(code == 0 && result.find("OK") != std::string::npos, result);
}
void finish_ollama_setup(bool success, const std::string &result) {
    if (success) {
        try {
            persist_ollama_model(ollama_setup_model);
        } catch (const std::exception &e) {
            status_note = std::string("The model was downloaded, but settings could not be saved: ") + e.what();
            set(settings_status, status_note);
            return;
        }
        status_note = "Ollama and model are ready.";
        if (engine->settings().at("active_connection") == "local-ollama" &&
            provider_draft.at(provider_index).at("format") == "ollama_local") {
            status_note = "Ollama and model ready; sending test request…";
            test_provider();
        }
    } else {
        auto error = result.find("ERROR: ");
        status_note = error == std::string::npos ? "Ollama installation incomplete. Retry or check the install window." :
                      "Ollama installation failed:" + result.substr(error + 7, 240);
    }
    set(settings_status, status_note);
}
void delete_provider() {
    if (provider_draft.at(provider_index).at("format") == "ollama_local" ||
        provider_draft.at(provider_index).at("format") == "bergamot_local" ||
        provider_draft.at(provider_index).at("format") == "ctranslate2_local")
        return;
    provider_draft.erase(provider_draft.begin() + provider_index);
    provider_index = std::min(provider_index, provider_draft.size() - 1);
    fill_provider();
    set(settings_status, "Connection will be deleted after saving settings");
}
bool provider_dirty() {
    capture_provider();
    auto current = engine->connections();
    auto &draft = provider_draft.at(provider_index);
    if (draft.at("format") == "bergamot_local" || draft.at("format") == "ctranslate2_local")
        return draft.at("id") != config["active_connection"];
    if (draft.at("id") != config["active_connection"] || draft.contains("key"))
        return true;
    for (auto &p : current)
        if (p.at("id") == draft.at("id"))
            return p != draft;
    return true;
}
void test_provider() {
    try {
        if (provider_testing)
            return;
        if (provider_dirty())
            throw std::runtime_error("Save the current connection before sending a test request");
        engine->test_connection();
        provider_testing = true;
        EnableWindow(GetDlgItem(body, id_provider_test), FALSE);
        set(settings_status, "Testing current translation connection…");
    } catch (const std::exception &e) {
        set(settings_status, e.what());
    }
}
static Json provider_json(HWND h, const char *name) {
    auto value = trim(text(h));
    if (value.empty())
        return Json::object();
    try {
        auto j = Json::parse(value);
        if (j.is_object())
            return j;
    } catch (const std::exception &) {
    }
    throw std::runtime_error(std::string(name) + "Must be a valid JSON object");
}
void close_provider_advanced() {
    if (provider_advanced)
        ShowWindow(provider_advanced, SW_HIDE);
    if (settings) {
        EnableWindow(settings, TRUE);
        SetForegroundWindow(settings);
    }
}
void apply_provider_advanced() {
    try {
        auto p = provider_draft.at(provider_index);
        auto format = SendMessageW(provider_format, CB_GETCURSEL, 0, 0);
        auto auth = SendMessageW(provider_auth, CB_GETCURSEL, 0, 0);
        p["format"] = provider_formats.at(size_t(format)).first;
        p["auth"] = provider_auths.at(size_t(auth)).first;
        p["full_url"] = SendMessageW(provider_full_url, BM_GETCHECK, 0, 0) == BST_CHECKED;
        p["parameters"] = provider_json(provider_parameters, "Extra parameters");
        p["headers"] = provider_json(provider_headers, "Custom headers");
        p["balance"] = provider_json(provider_balance_config, "Balance Config");
        if (p["balance"].empty())
            p["balance"] = {{"type", "none"}};
        auto complete = merge_connections(Json::array({p}), engine->connections());
        if (complete[0]["base_url"] == "")
            complete[0]["base_url"] = "https://example.invalid/v1";
        if (complete[0]["model"] == "")
            complete[0]["model"] = "unconfigured";
        validate_connections(complete, p.at("id").get<std::string>());
        provider_draft[provider_index] = std::move(p);
        close_provider_advanced();
        set(settings_status, "Advanced options applied to draft; click Save settings to apply");
    } catch (const std::exception &e) {
        set(provider_advanced_status, e.what());
    }
}
void layout_provider_advanced() {
    if (!provider_advanced || provider_advanced_labels.size() != 5)
        return;
    RECT area{};
    GetClientRect(provider_advanced, &area);
    const int dpi = GetDpiForWindow(provider_advanced);
    int width = MulDiv(area.right, 96, dpi), height = MulDiv(area.bottom, 96, dpi);
    auto place = [&](HWND h, int x, int y, int w, int z) {
        MoveWindow(h, scale(x, provider_advanced), scale(y, provider_advanced),
                   scale(std::max(1, w), provider_advanced), scale(std::max(1, z), provider_advanced), TRUE);
    };
    bool wide = width >= 620;
    place(provider_advanced_labels[0], 20, 12, width - 40, 23);
    place(provider_format, 20, 40, wide ? 350 : width - 40, 28);
    place(provider_full_url, wide ? width - 250 : 20, wide ? 40 : 74, std::min(235, width - 40), 28);
    int shift = wide ? 0 : 34;
    place(provider_advanced_labels[1], 20, 80 + shift, width - 40, 23);
    place(provider_auth, 20, 108 + shift, width - 40, 28);
    int y = 148 + shift;
    int editor_space = std::max(30, height - y - 177);
    int params = editor_space * 45 / 100, heads = editor_space * 30 / 100,
        balance = editor_space - params - heads;
    place(provider_advanced_labels[2], 20, y, width - 40, 23);
    y += 27;
    place(provider_parameters, 20, y, width - 40, params);
    y += params + 8;
    place(provider_advanced_labels[3], 20, y, width - 40, 23);
    y += 27;
    place(provider_headers, 20, y, width - 40, heads);
    y += heads + 8;
    place(provider_advanced_labels[4], 20, y, width - 40, 23);
    y += 27;
    place(provider_balance_config, 20, y, width - 40, balance);
    place(provider_advanced_status, 20, height - 69, width - 40, 24);
    int button_width = std::min(140, (width - 55) / 2);
    place(GetDlgItem(provider_advanced, id_provider_apply), width - 35 - 2 * button_width, height - 37,
          button_width, 28);
    place(GetDlgItem(provider_advanced, id_provider_cancel), width - 20 - button_width, height - 37,
          button_width, 28);
}
void open_provider_advanced() {
    capture_provider();
    if (!provider_advanced) {
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(settings, MONITOR_DEFAULTTONEAREST), &monitor);
        int width = scale(660, settings), height = scale(650, settings);
        width = std::min(width, int(monitor.rcWork.right - monitor.rcWork.left) - 20);
        height = std::min(height, int(monitor.rcWork.bottom - monitor.rcWork.top) - 20);
        provider_advanced =
            CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"BianLingo.Window", wide(ui_text("Connection · Advanced settings")).c_str(),
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                            monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - width) / 2,
                            monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - height) / 2,
                            width, height, settings, nullptr, instance, nullptr);
        BOOL dark = TRUE;
        DwmSetWindowAttribute(provider_advanced, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        auto label = [&](const char *s, int y) {
            provider_advanced_labels.push_back(
                control(provider_advanced, L"STATIC", s, 20, y, 610, 23, SS_LEFT));
        };
        label("API format (choose protocol, any vendor)", 14);
        provider_format =
            control(provider_advanced, WC_COMBOBOXW, "", 20, 42, 350, 180, CBS_DROPDOWNLIST | WS_TABSTOP);
        for (const auto &[id, name] : provider_formats) {
            auto value = wide(ui_text(name));
            SendMessageW(provider_format, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value.c_str()));
        }
        provider_full_url =
            control(provider_advanced, L"BUTTON", "Address is the full request URL", 390, 42, 235, 28, BS_AUTOCHECKBOX);
        label("Auth method; base URL must include version path, e.g. /v1, /v1beta", 83);
        provider_auth =
            control(provider_advanced, WC_COMBOBOXW, "", 20, 111, 605, 180, CBS_DROPDOWNLIST | WS_TABSTOP);
        for (const auto &[id, name] : provider_auths) {
            auto value = wide(ui_text(name));
            SendMessageW(provider_auth, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value.c_str()));
        }
        label("Extra request parameters JSON (empty object = common default; null removes the parameter)", 151);
        provider_parameters = control(provider_advanced, L"EDIT", "", 20, 179, 605, 108,
                                      WS_BORDER | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL);
        label("Custom headers JSON (stored encrypted locally; ${API_KEY} allowed)", 300);
        provider_headers = control(provider_advanced, L"EDIT", "", 20, 328, 605, 82,
                                   WS_BORDER | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL);
        label("Balance API JSON (optional; default {\"type\":\"none\"} disables lookup)", 423);
        provider_balance_config =
            control(provider_advanced, L"EDIT", "", 20, 451, 605, 75,
                    WS_BORDER | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL);
        for (auto edit : {provider_parameters, provider_headers, provider_balance_config})
            SendMessageW(edit, EM_SETLIMITTEXT, 65536, 0);
        provider_advanced_status = control(provider_advanced, L"STATIC", "", 20, 536, 600, 24, SS_LEFT);
        control(provider_advanced, L"BUTTON", "Apply to draft", 340, 569, 140, 30, BS_PUSHBUTTON | WS_TABSTOP,
                id_provider_apply);
        control(provider_advanced, L"BUTTON", "Cancel", 495, 569, 130, 30, BS_PUSHBUTTON | WS_TABSTOP,
                id_provider_cancel);
    }
    const auto &p = provider_draft.at(provider_index);
    auto select = [](HWND h, const auto &values, const Json &key) {
        for (size_t i = 0; i < values.size(); ++i)
            if (values[i].first == key.get<std::string>())
                SendMessageW(h, CB_SETCURSEL, i, 0);
    };
    select(provider_format, provider_formats, p.at("format"));
    select(provider_auth, provider_auths, p.at("auth"));
    SendMessageW(provider_full_url, BM_SETCHECK, p.at("full_url").get<bool>() ? BST_CHECKED : BST_UNCHECKED,
                 0);
    set(provider_parameters, p.at("parameters").dump(2));
    set(provider_headers, p.at("headers").dump(2));
    set(provider_balance_config, p.at("balance").dump(2));
    set(provider_advanced_status, "This page sends no requests; enabled after saving main settings.");
    layout_provider_advanced();
    EnableWindow(settings, FALSE);
    ShowWindow(provider_advanced, SW_SHOW);
    SetForegroundWindow(provider_advanced);
}

#ifdef BL_UI_TESTS
void verify_provider_ui() {
    auto require = [](bool ok, const char *why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    auto original = provider_draft;
    switch_page(3);
    auto saved_hotkey = SendMessageW(hotkey, HKM_GETHOTKEY, 0, 0);
    SendMessageW(hotkey, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(8, 8));
    require(GetFocus() == hotkey, "Clicking the output shortcut did not focus its editor");
    SendMessageW(hotkey, WM_KEYDOWN, VK_F9, 0);
    require(LOBYTE(SendMessageW(hotkey, HKM_GETHOTKEY, 0, 0)) == VK_F9,
            "Focused shortcut editor did not capture a replacement key");
    SendMessageW(hotkey, HKM_SETHOTKEY, saved_hotkey, 0);
    SetFocus(settings);
    show_notices();
    CHARFORMAT2W notice_format{};
    notice_format.cbSize = sizeof(notice_format);
    SendMessageW(notices_text, EM_GETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&notice_format));
    require(notice_format.crTextColor == fg && fg != bg && GetWindowTextLengthW(notices_text) < 2500,
            "Brief notices have unreadable colors or excessive text");
    ShowWindow(notices, SW_HIDE);
    auto original_index = provider_index;
    auto original_usage = engine->usage();
    auto original_settings = engine->settings();
    auto original_connections = engine->connections();
    auto a = new_connection("fixture-a"), b = new_connection("fixture-b");
    a["name"] = "Custom A";
    a["base_url"] = "https://alpha.invalid/v1";
    a["model"] = "unlisted-a";
    b["name"] = "Custom B";
    b["base_url"] = "https://beta.invalid/v1";
    b["model"] = "unlisted-b";
    auto fixture_active = std::string("fixture-a");
    provider_draft = public_connections(fixed_local_connections(Json::array({a, b}), fixture_active));
    provider_index = 3;
    switch_page(2);
    fill_provider();
    set(api_key, "synthetic-key-a");
    SendMessageW(provider_select, CB_SETCURSEL, 4, 0);
    select_provider();
    require(text(api_key).empty() && text(provider_edit("model")) == "unlisted-b",
            "Connection switch leaks another draft key or model");
    SendMessageW(provider_select, CB_SETCURSEL, 3, 0);
    select_provider();
    require(text(api_key) == "synthetic-key-a", "Switching profiles loses an unsaved key draft");
    test_provider();
    require(!provider_testing && engine->usage() == original_usage,
            "Unsaved connection test sends a real billable request");
    open_provider_advanced();
    require(IsWindowVisible(provider_advanced) && !IsWindowEnabled(settings),
            "Advanced connection editor is not isolated from the main draft");
    for (auto edit : {provider_parameters, provider_headers, provider_balance_config})
        require(!(GetWindowLongPtrW(edit, GWL_STYLE) & WS_VSCROLL),
                "Advanced connection editor exposes an unthemed native scrollbar");
    SendMessageW(provider_format, CB_SETCURSEL, 3, 0);
    SendMessageW(provider_auth, CB_SETCURSEL, 4, 0);
    SendMessageW(provider_full_url, BM_SETCHECK, BST_CHECKED, 0);
    set(provider_parameters, "{\"generationConfig\":{\"temperature\":0.3}}");
    set(provider_headers, "{\"X-Custom\":\"synthetic\"}");
    apply_provider_advanced();
    require(!IsWindowVisible(provider_advanced) && IsWindowEnabled(settings) &&
                provider_draft[3]["format"] == "gemini" && provider_draft[3]["full_url"] == true &&
                provider_draft[3]["parameters"]["generationConfig"]["temperature"] == 0.3,
            "Advanced protocol/header/parameter changes were not applied to the draft");
    require(engine->settings() == original_settings && engine->usage() == original_usage,
            "Connection draft editing changes live settings or calls the API");
    auto before_invalid = provider_draft;
    open_provider_advanced();
    set(provider_headers, "{invalid-json");
    apply_provider_advanced();
    require(IsWindowVisible(provider_advanced) && provider_draft == before_invalid,
            "Invalid advanced JSON changes the draft or closes the editor");
    close_provider_advanced();
    delete_provider();
    require(provider_draft.size() == 4, "Connection deletion did not update draft list");
    add_provider();
    require(provider_draft.size() == 5 && provider_draft.back()["base_url"] == "" &&
                provider_draft.back()["parameters"].empty(),
            "New custom connection is restricted to a vendor preset");
    auto count = provider_draft.size();
    add_local_provider();
    require(provider_draft.size() == count && provider_draft[provider_index]["format"] == "ollama_local" &&
                engine->settings()["active_connection"] == "local-ollama" &&
                (GetWindowLongPtrW(provider_setup, GWL_STYLE) & WS_VISIBLE) && IsWindowEnabled(provider_setup),
            "Local Ollama switch created a duplicate or did not activate");
    auto saved_model = engine->settings()["model"];
    require(SendMessageW(ollama_model_choice, CB_GETCOUNT, 0, 0) == 4 &&
                (GetWindowLongPtrW(ollama_model_choice, GWL_STYLE) & WS_VISIBLE),
            "Ollama model selector should offer three optional models and custom input");
    SendMessageW(ollama_model_choice, CB_SETCURSEL, 2, 0);
    select_ollama_model();
    require(engine->settings()["model"] == saved_model && !ollama_setup_process &&
                provider_dirty() && !(GetWindowLongPtrW(ollama_custom_model, GWL_STYLE) & WS_VISIBLE),
            "Selecting a model should be a draft and must not download or apply it");
    switch_page(0);
    switch_page(2);
    require(selected_ollama_model() == "qwen3:4b", "Changing pages lost the selected Ollama model");
    auto before_model_apply = engine->connections();
    apply_ollama_model();
    require(engine->settings()["model"] == "qwen3:4b" && !provider_dirty() && !ollama_setup_process &&
                provider_draft.back()["base_url"] == "" && engine->connections().size() == before_model_apply.size(),
            "Applying a model should save only this model, preserve API drafts and avoid downloading");
    SendMessageW(ollama_model_choice, CB_SETCURSEL, 3, 0);
    select_ollama_model();
    set(ollama_custom_model, "library/custom-small:Q4_K_M");
    require((GetWindowLongPtrW(ollama_custom_model, GWL_STYLE) & WS_VISIBLE),
            "Custom Ollama model name input is hidden");
    apply_ollama_model();
    require(engine->settings()["model"] == "library/custom-small:Q4_K_M" &&
                SendMessageW(ollama_model_choice, CB_GETCURSEL, 0, 0) == 3,
            "Applying a custom Ollama model did not persist or restore the custom selector");
    auto before_invalid_model = engine->settings();
    set(ollama_custom_model, "qwen3;exit");
    bool rejected_model = false;
    try { apply_ollama_model(); } catch (const std::exception &) { rejected_model = true; }
    require(rejected_model && engine->settings() == before_invalid_model && !ollama_setup_process,
            "Invalid model input changed the live configuration or started an installer");
    set(ollama_custom_model, "library/custom-small:Q4_K_M");
    ollama_setup_model = "qwen3:1.7b";
    finish_ollama_setup(false, "ERROR: synthetic interrupted download");
    require(engine->settings() == before_invalid_model,
            "An interrupted or failed download replaced the working model");
    add_bergamot_provider();
    require(provider_draft.size() == count && provider_draft[provider_index]["format"] == "bergamot_local" &&
                engine->settings()["active_connection"] == "local-bergamot" &&
                !(GetWindowLongPtrW(provider_setup, GWL_STYLE) & WS_VISIBLE),
            "Local Bergamot switch created a duplicate or did not activate");
    auto bergamot_id = engine->settings()["active_connection"];
    auto bergamot_usage = engine->usage();
    SendMessageW(provider_select, CB_SETCURSEL, provider_draft.size() - 1, 0);
    select_provider();
    set(provider_name, "Unsubmitted API draft");
    set(provider_edit("model"), "draft-model");
    set(api_key, "synthetic-unsaved-key");
    finish_ollama_setup(true, "OK");
    require(engine->settings()["active_connection"] == bergamot_id &&
                !provider_testing && engine->usage() == bergamot_usage &&
                active_connection(engine->connections(), "local-ollama")["model"] == "qwen3:1.7b" &&
                provider_draft.back()["name"] == "Unsubmitted API draft" &&
                text(provider_edit("model")) == "draft-model" && text(api_key) == "synthetic-unsaved-key" &&
                engine->connections().size() == before_model_apply.size() &&
                !(GetWindowLongPtrW(ollama_model_choice, GWL_STYLE) & WS_VISIBLE),
            "Download completion should save the captured model without switching away from another engine");
    add_ctranslate2_provider();
    require(provider_draft.size() == count && provider_draft[provider_index]["format"] == "ctranslate2_local" &&
                engine->settings()["active_connection"] == "local-ctranslate2" &&
                !(GetWindowLongPtrW(provider_setup, GWL_STYLE) & WS_VISIBLE),
            "Local CTranslate2 switch created a duplicate or did not activate");
    provider_draft.push_back(public_connections(Json::array({new_gemini_connection("gemini-ui")})).at(0));
    provider_index = provider_draft.size() - 1;
    fill_provider();
    require(text(provider_name) == "Gemini" && text(api_key).empty() &&
                text(provider_edit("base_url")) == "https://generativelanguage.googleapis.com/v1beta/openai" &&
                text(provider_edit("model")) == "gemini-3.5-flash-lite" &&
                IsWindowEnabled(api_key) && IsWindowEnabled(provider_edit("model")),
            "Gemini starter did not populate the editable connection form with an empty key");
    set(api_key, "synthetic-gemini-ui-key");
    capture_provider();
    require(provider_draft.back()["key"] == "synthetic-gemini-ui-key" &&
                provider_draft.back()["parameters"].empty() && provider_draft.back()["balance"]["type"] == "none",
            "Gemini form changed protocol defaults or lost the entered key");
    engine->configure(original_settings, std::nullopt, original_connections);
    config = engine->settings();
    provider_draft = std::move(original);
    provider_index = original_index;
    fill_provider();
    switch_page(0);
}
#endif
