// Members of App. A menu keeps a copy of the message: new arrivals cannot retarget it.
struct MessageMenuItem { UINT command; std::wstring label; bool enabled; };
std::vector<MessageMenuItem> message_menu_items;
std::optional<Message> context_message;
HMENU message_menu{};
HWND block_window{}, block_list{}, block_name{}, block_status{}, block_hint{}, block_name_label{}, block_transfer_hint{};
std::vector<std::string> block_names;
std::string prepared_who;
std::string block_sync_note;
std::string prepared_block_code;

void close_message_menu() {
    if (message_menu) DestroyMenu(message_menu);
    message_menu = nullptr; context_message.reset();
}
void reply_to_message(const Message &m, bool whisper) {
    auto destination = whisper ? whisper_target(m) : reply_target(m);
    if (!destination) return;
    auto suggested = reply_language(m.text);
    retry_reply = false; open_output(); chosen_reply = std::move(destination);
    for (size_t i = 0; !suggested.empty() && i < output_languages.size(); ++i)
        if (output_languages[i].second == suggested) SendMessageW(target, CB_SETCURSEL, i, 0);
    set(output_heading, ui_text("Reply") + " " + localized_reply_label(chosen_reply->label));
    set(output_status, chosen_reply->prefix + ui_text("Translation · Confirm language, press Enter to translate and copy"));
    SetFocus(input);
}
void refresh_blocklist() {
    auto current = engine->blocklist();
    ui_blocks = PlayerBlocklist(current);
    if (!block_list) return;
    block_names = ui_blocks.names();
    SendMessageW(block_list, LB_RESETCONTENT, 0, 0);
    for (const auto &name : block_names) {
        auto value = wide(name);
        SendMessageW(block_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value.c_str()));
    }
}
void copy_block_code() {
    prepared_block_code = blocklist_import_code(PlayerBlocklist(engine->blocklist()));
    if (!smoke && !copy_clipboard(prepared_block_code)) throw std::runtime_error("Could not copy the sync code; retry");
    block_sync_note = status_note = ui_text("Code copied. Paste and import it in /bl blocks to apply in game.");
    if (block_status) { set(block_status, block_sync_note); layout_block_window(); }
}
void change_block(std::string name, bool blocked) {
    engine->set_player_blocked(std::move(name), blocked);
    refresh_blocklist();
    render(bookmark());
    block_sync_note = status_note = ui_text("Saved here. Copy the sync code and import it in the addon’s blocklist window.");
    if (block_status) { set(block_status, block_sync_note); layout_block_window(); }
}
void who_message(const Message &m) {
    auto name = block_player_name(m);
    if (name.empty()) return;
    if (reply.busy()) throw std::runtime_error("Wait for the outgoing translation before querying a level");
    name.resize(name.find('-') == name.npos ? name.size() : name.find('-'));
    prepared_who = "/who " + name;
    if (smoke) return;
    reply = QuickReply{}; update_bubble();
    if (!copy_clipboard(prepared_who)) throw std::runtime_error("Could not copy the level-query command; please retry");
    status_note = ui_text("Level query copied. Paste it in game and press Enter; view the result in game.");
    auto game = reinterpret_cast<HWND>(engine->game_window());
    if (game && IsWindow(game)) SetForegroundWindow(game);
    auto notification = tray;
    notification.uFlags = NIF_INFO;
    wcsncpy_s(notification.szInfoTitle, wide(ui_text("Level query")).c_str(), _TRUNCATE);
    wcsncpy_s(notification.szInfo, wide(status_note + "\n" + prepared_who).c_str(), _TRUNCATE);
    notification.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &notification);
}
void message_action(UINT command) {
    auto message = context_message;
    close_message_menu();
    if (!message) return;
    if (command == 1) reply_to_message(*message, true);
    else if (command == 2) who_message(*message);
    else if (command == 3) change_block(block_player_name(*message), true);
}
void show_message_menu(long character, POINT point) {
    if (message_preview) return;
    auto row = std::find_if(chat_rows.begin(), chat_rows.end(), [&](const auto &r) {
        return character >= r.start && character < r.end;
    });
    if (row == chat_rows.end() || row->index >= messages.size()) return;
    close_message_menu();
    context_message = messages.at(row->index).message;
    bool valid = !block_player_name(*context_message).empty();
    message_menu_items = {{1, wide(ui_text("Whisper")), bool(whisper_target(*context_message))},
                          {2, wide(ui_text("Check level (in-game /who)")), valid && !reply.busy()},
                          {3, wide(ui_text("Block this player")), valid}};
    message_menu = CreatePopupMenu();
    if (!message_menu) throw std::runtime_error("Could not create the message menu");
    for (auto &item : message_menu_items)
        AppendMenuW(message_menu, MF_OWNERDRAW | (item.enabled ? MF_ENABLED : MF_GRAYED), item.command,
                    reinterpret_cast<LPCWSTR>(&item));
    if (smoke) return; // UI tests inspect this same menu and choose an action explicitly.
    SetForegroundWindow(main);
    auto selected = TrackPopupMenu(message_menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                   point.x, point.y, 0, main, nullptr);
    message_action(selected);
}
bool measure_message_menu(MEASUREITEMSTRUCT *item) {
    if (item->CtlType != ODT_MENU || !item->itemData) return false;
    const auto &entry = *reinterpret_cast<MessageMenuItem *>(item->itemData);
    auto dc = GetDC(main);
    auto previous = SelectObject(dc, ui_font(13, false, main, has_cjk(narrow(entry.label))));
    SIZE size{}; GetTextExtentPoint32W(dc, entry.label.c_str(), int(entry.label.size()), &size);
    SelectObject(dc, previous); ReleaseDC(main, dc);
    item->itemWidth = size.cx + scale(36); item->itemHeight = std::max(scale(32), int(size.cy) + scale(14));
    return true;
}
bool draw_message_menu(DRAWITEMSTRUCT *item) {
    if (item->CtlType != ODT_MENU || !item->itemData) return false;
    const auto &entry = *reinterpret_cast<MessageMenuItem *>(item->itemData);
    auto dc = item->hDC;
    auto saved = SaveDC(dc);
    widgets::paint::solid(dc, item->rcItem,
        item->itemState & ODS_SELECTED ? rgb(active_theme.colors.selection) : panel);
    SelectObject(dc, ui_font(13, false, main, has_cjk(narrow(entry.label))));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, entry.enabled ? fg : rgb(active_theme.colors.disabled));
    RECT box = item->rcItem; box.left += scale(14); box.right -= scale(14);
    DrawTextW(dc, entry.label.c_str(), -1, &box, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    RestoreDC(dc, saved);
    return true;
}
void layout_block_window() {
    if (!block_window) return;
    RECT r{}; GetClientRect(block_window, &r);
    int width = MulDiv(r.right, 96, int(GetDpiForWindow(block_window))), height = MulDiv(r.bottom, 96, int(GetDpiForWindow(block_window)));
    auto move = [&](HWND h, int x, int y, int w, int hgt) { MoveWindow(h, scale(x, block_window), scale(y, block_window), scale(w, block_window), scale(hgt, block_window), TRUE); };
    auto add = GetDlgItem(block_window, id_blocks_add), remove = GetDlgItem(block_window, id_blocks_remove), sync = GetDlgItem(block_window, id_blocks_sync);
    auto dc = GetDC(block_window);
    auto measured = [&](HWND h, int w, int minimum) { return std::max(minimum, settings_caption_height(h, w, dc)); };
    const int hint_h = measured(block_hint, width - 40, 48), label_h = measured(block_name_label, width - 40, 30);
    const int input_h = std::max(measured(block_name, width - 210, 30), measured(add, 150, 30));
    const int remove_h = measured(remove, width - 40, 32), transfer_h = measured(block_transfer_hint, width - 40, 60);
    const int sync_h = measured(sync, width - 40, 32), status_h = measured(block_status, width - 40, 48);
    ReleaseDC(block_window, dc);
    const int list_y = 18 + hint_h + 12;
    const int footer_h = label_h + 6 + input_h + 12 + remove_h + 12 + transfer_h + 8 + sync_h + 12 + status_h;
    const int minimum_h = list_y + 100 + 12 + footer_h + 20;
    if (height < minimum_h) {
        RECT outer{}; GetWindowRect(block_window, &outer);
        SetWindowPos(block_window, nullptr, 0, 0, outer.right - outer.left,
                     outer.bottom - outer.top + scale(minimum_h - height, block_window), SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
        return;
    }
    const int list_h = height - list_y - 12 - footer_h - 20;
    move(block_hint, 20, 18, width - 40, hint_h);
    move(block_list, 20, list_y, width - 40, list_h);
    int y = list_y + list_h + 12;
    move(block_name_label, 20, y, width - 40, label_h); y += label_h + 6;
    move(block_name, 20, y, width - 210, input_h);
    move(add, width - 170, y, 150, input_h); y += input_h + 12;
    move(remove, 20, y, width - 40, remove_h); y += remove_h + 12;
    move(block_transfer_hint, 20, y, width - 40, transfer_h); y += transfer_h + 8;
    move(sync, 20, y, width - 40, sync_h); y += sync_h + 12;
    move(block_status, 20, y, width - 40, status_h);
}
void open_block_window() {
    if (!block_window) {
        block_window = CreateWindowExW(WS_EX_APPWINDOW, L"BianLingo.Window", wide(ui_text("Blocked players")).c_str(),
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
            scale(740), scale(660), nullptr, nullptr, instance, nullptr);
        if (!block_window) throw std::runtime_error("Could not create the blocklist window");
        BOOL dark = GetRValue(bg) + GetGValue(bg) + GetBValue(bg) < 450;
        DwmSetWindowAttribute(block_window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        DwmSetWindowAttribute(block_window, DWMWA_CAPTION_COLOR, &bg, sizeof(bg));
        block_hint = control(block_window, L"STATIC", "Hidden immediately here. Paste a sync code into the addon to apply there. A name without a realm matches all namesakes.", 20, 18, 680, 48, SS_LEFT);
        block_list = control(block_window, L"LISTBOX", "", 20, 78, 680, 240, WS_BORDER | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_TABSTOP);
        SetWindowSubclass(block_list, [](HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) -> LRESULT {
            if (msg == WM_MOUSEWHEEL) {
                auto top = SendMessageW(h, LB_GETTOPINDEX, 0, 0);
                auto count = SendMessageW(h, LB_GETCOUNT, 0, 0);
                SendMessageW(h, LB_SETTOPINDEX, std::clamp<int>(int(top) - GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA * 3, 0, std::max(0, int(count) - 1)), 0);
                return 0;
            }
            return DefSubclassProc(h, msg, w, l);
        }, 31, 0);
        block_name_label = control(block_window, L"STATIC", "Player name (optional -Realm)", 20, 320, 680, 30, SS_LEFT);
        block_name = control(block_window, L"EDIT", "", 20, 356, 510, 30, WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP);
        SendMessageW(block_name, EM_SETLIMITTEXT, 192, 0);
        control(block_window, L"BUTTON", "Add block", 550, 356, 150, 30, BS_PUSHBUTTON | WS_TABSTOP, id_blocks_add);
        control(block_window, L"BUTTON", "Unblock selected player", 20, 398, 680, 32, BS_PUSHBUTTON | WS_TABSTOP, id_blocks_remove);
        block_transfer_hint = control(block_window, L"STATIC", "In game: /bl blocks → Import from receiver. Paste the code and apply. WoW saves the list; no folder is needed.", 20, 442, 680, 60, SS_LEFT);
        control(block_window, L"BUTTON", "Copy sync code (paste in game)", 20, 514, 680, 32, BS_PUSHBUTTON | WS_TABSTOP, id_blocks_sync);
        block_status = control(block_window, L"STATIC", "", 20, 556, 680, 44, SS_LEFT);
        muted_controls.insert(block_hint); muted_controls.insert(block_status);
        refresh_interface_fonts();
    }
    set(block_status, block_sync_note.empty() ?
        ui_text("Saved here. Copy the sync code and import it in the addon’s blocklist window.") : block_sync_note);
    refresh_blocklist(); layout_block_window();
    ShowWindow(block_window, SW_SHOW); SetForegroundWindow(block_window);
}
