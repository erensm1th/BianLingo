#ifdef BL_UI_TESTS
void verify_models_ui() {
    auto require = [](bool ok, const char *why) { if (!ok) throw std::runtime_error(why); };
    model_fixture_root = settings_data_directory() / "model-fixture";
    auto manifest = read_json(TEST_MODEL_MANIFEST); write_json(model_fixture_root / "models.json", manifest);
    auto saved = engine->settings(), connections = engine->connections(), usage = engine->usage();
    auto original = SendMessageW(chat_target, CB_GETCURSEL, 0, 0);
    auto french = std::find(chat_targets.begin(), chat_targets.end(), "fr") - chat_targets.begin();
    SendMessageW(chat_target, CB_SETCURSEL, french, 0); open_model_window();
    require(model_inventory.size() > 8 && model_queue == std::vector<std::string>{"en-fr", "fr-en"}, "Current target did not plan its directions from the catalogue");
    set(model_search, "en-ja");
    require(model_choices == std::vector<std::string>{"en-ja"}, "Pair search did not filter actual catalogue entries");
    SendMessageW(model_window, WM_COMMAND, id_models_add, 0);
    require(model_queue.size() == 4 && model_queue.back() == "ja-en", "Pair selector did not add both requested directions");
    SendMessageW(model_window, WM_COMMAND, id_models_add, 0);
    require(model_queue.size() == 4, "Adding a pair again duplicated downloads");
    SendMessageW(model_list, LB_SETSEL, TRUE, 2); SendMessageW(model_list, LB_SETSEL, TRUE, 3);
    SendMessageW(model_window, WM_COMMAND, id_models_remove, 0);
    require(model_queue.size() == 2, "Removing selected download dependencies failed");
    SendMessageW(model_window, WM_COMMAND, id_models_all, 0); require(model_queue.empty(), "Clear queue failed");
    choose_current_pack(); start_model_job();
    require(!model_task && engine->settings() == saved && engine->connections() == connections && engine->usage() == usage,
            "Selecting language pairs downloaded or changed settings");
    auto tiny = manifest;
    for (auto &r : tiny["models"]) { r["size"] = uint64_t(1); r["sha256"] = sha256("x"); r.erase("compressed_sha256"); }
    write_json(model_fixture_root / "models.json", tiny);
    auto wait = [&] { auto deadline = Clock::now() + std::chrono::seconds(5); while (model_task && Clock::now() < deadline) { poll_model_job(); Sleep(1); }
        require(!model_task, "Model worker did not complete or cancel"); };
    launch_model_job(BergamotModels(model_root(), model_cache()), {"en-fr", "fr-en"},
        [](auto, const auto &out, auto, const auto &progress) { Bytes payload{'x'}; atomic_write(out, payload); progress(1); });
    require(!IsWindowEnabled(model_pair), "Download allows selection changes to its active job");
    wait(); require(model_pack("en-fr").installed && model_pack("fr-en").installed && engine->settings() == saved,
        "Worker did not prepare both directions without changing settings");
    launch_model_job(BergamotModels(model_root(), model_cache()), {"en-de"},
        [](auto, const auto &, auto, const auto &progress) { for (;;) { progress(0); Sleep(1); } });
    cancel_model_job(); wait(); require(!model_pack("en-de").installed, "Cancelled model reported installed");
    write_json(model_fixture_root / "models.json", manifest); refresh_model_inventory(); set(model_search, "");
    auto previous_format = provider_draft[provider_index]["format"]; auto previous_page = active_page; switch_page(2);
    ct2_fixture_models = bundled_json("ctranslate2-models.json");
    for (auto &m : ct2_fixture_models["models"]) for (auto &f : m["files"]) f = Json::array({uint64_t(1), sha256("x")});
    provider_draft[provider_index]["format"] = "ctranslate2_local"; fill_provider();
    SendMessageW(chat_target, CB_SETCURSEL, french, 0); open_model_window();
    require(model_ct2 && model_queue == std::vector<std::string>{"en-fr", "fr-en"} &&
        (GetWindowLongPtrW(GetDlgItem(body, id_models_open), GWL_STYLE) & WS_VISIBLE), "CT2 manager buttons or target planning missing");
    atomic_write(model_root()/"BianLingoCT2.exe", Bytes{'x'});
    auto hash=sha256("x");atomic_write(model_root()/"engine.sha256",Bytes(hash.begin(),hash.end()));
    write_json(model_root()/"native-build.json",{{"binary_sha256",hash},{"protocol","BLCT3"},
        {"source_lock_sha256",bundled_json("ctranslate2-runtime.json")["source_lock_sha256"]}});
    atomic_write(model_root()/"engine-LICENSES.txt",Bytes{'x'});
    set(model_search, "ja-en");
    SendMessageW(model_window, WM_COMMAND, id_models_add, 0);
    require(model_queue.back() == "ja-en" && model_queue.size() == 3, "One-way CT2 pair cannot be selected");
    choose_current_pack();
    launch_model_job(model_store(), model_queue,
        [](auto, const auto &out, auto, const auto &progress) { atomic_write(out, Bytes{'x'}); progress(1); });
    // Selecting another backend cannot relabel or retarget an active task.
    provider_draft[provider_index]["format"] = "bergamot_local"; fill_provider(); open_model_window();
    require(model_ct2, "Backend changed during an active CT2 preparation snapshot");
    wait(); require(model_pack("en-fr").installed && engine->settings() == saved && engine->connections() == connections,
        "CT2 worker changed configuration or failed to install selected models");
    set(model_search, "");
    // Dependency failures must fit the same status area in every UI language.
    set(model_progress, ui_text("Language pack preparation failed: ") + ui_text("CTranslate2 native component could not start; reinstall the complete package"));
    RECT client{}; GetClientRect(model_window, &client); auto dc = GetDC(model_window);
    for (HWND child = GetWindow(model_window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        RECT area{}; GetWindowRect(child, &area); MapWindowPoints(nullptr, model_window, reinterpret_cast<POINT *>(&area), 2);
        require(area.left >= scale(16, model_window) && area.right <= client.right - scale(16, model_window) && area.bottom <= client.bottom - scale(16, model_window),
            "Language manager control extends beyond padding");
        int height = settings_caption_height(child, MulDiv(area.right - area.left, 96, int(GetDpiForWindow(model_window))), dc);
        if (scale(height, model_window) > area.bottom - area.top)
            throw std::runtime_error("Localized model manager text is clipped: " + text(child));
    }
    ReleaseDC(model_window, dc);
    ct2_fixture_models = Json(); provider_draft[provider_index]["format"] = previous_format;
    fill_provider(); switch_page(previous_page); SendMessageW(chat_target, CB_SETCURSEL, original, 0);
    set(model_progress, "Not started. Installed files will be verified and skipped."); ShowWindow(model_window, SW_HIDE);
}
#endif
