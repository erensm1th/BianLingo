// Members of App. Section ownership and rows are declared with the controls.
// Original coordinates specify column/row relationships, never card boundaries.
// Measure only when width, font, visibility or displayed captions have changed.
void draw_wrapped_caption(HDC dc, const std::wstring &caption, RECT box, UINT align, int padding = 0) {
    box.left += padding;
    box.right -= padding;
    RECT measured{0, 0, std::max(1L, box.right - box.left), 0};
    DrawTextW(dc, caption.c_str(), -1, &measured, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    box.top += std::max(0L, (box.bottom - box.top - measured.bottom) / 2);
    DrawTextW(dc, caption.c_str(), -1, &box, align | DT_WORDBREAK | DT_NOPREFIX);
}
int settings_caption_height(HWND h, int width, HDC dc) const {
    const auto kind = widgets::kind(h);
    const auto style = GetWindowLongPtrW(h, GWL_STYLE);
    const bool button = (style & BS_TYPEMASK) == BS_OWNERDRAW && !kind;
    if (kind != widgets::Kind::Label && kind != widgets::Kind::Toggle && !button) {
        wchar_t type[32]{};
        GetClassNameW(h, type, 32);
        if (kind != widgets::Kind::Choice && kind != widgets::Kind::Hotkey && _wcsicmp(type, L"EDIT"))
            return 0;
        auto previous = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(h, WM_GETFONT, 0, 0)));
        TEXTMETRICW metrics{};
        GetTextMetricsW(dc, &metrics);
        SelectObject(dc, previous);
        const auto dpi = std::max(1U, GetDpiForWindow(h));
        return int((int64_t(metrics.tmHeight) * 96 + dpi - 1) / dpi) + 8;
    }
    int padding = button ? 24 : 0;
    if (kind == widgets::Kind::Toggle)
        padding = beveled() ? active_theme.metrics.toggle_height + 20 : active_theme.metrics.toggle_width + 14;
    const auto id = GetDlgCtrlID(h);
    if (button && id >= id_skin_color0 && id < id_skin_color0 + 4) padding += 20;
    auto previous = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(h, WM_GETFONT, 0, 0)));
    RECT box{0, 0, std::max(1, scale(width - padding, h)), 0};
    auto caption = wide(text(h));
    DrawTextW(dc, caption.c_str(), -1, &box, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, previous);
    auto dpi = std::max(1U, GetDpiForWindow(h));
    return int((int64_t(box.bottom) * 96 + dpi - 1) / dpi) + (button ? 12 : 4);
}
int settings_min_width(HWND h, HDC dc) const {
    const auto kind = widgets::kind(h);
    const bool button = !kind && (GetWindowLongPtrW(h, GWL_STYLE) & BS_TYPEMASK) == BS_OWNERDRAW;
    if (kind != widgets::Kind::Label && kind != widgets::Kind::Toggle && !button)
        return kind == widgets::Kind::Slider ? 100 : kind == widgets::Kind::Choice ? 96 : 72;
    auto previous = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(h, WM_GETFONT, 0, 0)));
    auto caption = wide(text(h));
    int longest = 0;
    std::wstring token;
    auto flush = [&] {
        SIZE size{};
        GetTextExtentPoint32W(dc, token.c_str(), int(token.size()), &size);
        longest = std::max(longest, int(size.cx));
        token.clear();
    };
    for (auto c : caption) {
        if (std::iswspace(c)) flush();
        else if (c >= 0x3000 && c <= 0xd7af) { flush(); token = c; flush(); }
        else token += c;
    }
    flush();
    SelectObject(dc, previous);
    int padding = button ? 24 : 0;
    if (kind == widgets::Kind::Toggle)
        padding = beveled() ? active_theme.metrics.toggle_height + 20 : active_theme.metrics.toggle_width + 14;
    auto id = GetDlgCtrlID(h);
    if (button && id >= id_skin_color0 && id < id_skin_color0 + 4) padding += 20;
    const auto dpi = std::max(1U, GetDpiForWindow(h));
    return int((int64_t(longest) * 96 + dpi - 1) / dpi) + padding + 2;
}
void reflow_settings_page(int physical_width) {
    auto &sections = settings_sections[active_page];
    if (sections.empty()) return;
    const auto dpi = std::max(1U, GetDpiForWindow(body));
    const int width = std::max(180, MulDiv(physical_width, 96, int(dpi)));
    std::ostringstream signature;
    signature << width << ':' << dpi << ':' << beveled() << ':' << active_theme.metrics.toggle_width
              << ':' << active_theme.metrics.toggle_height << ':' << sections.size();
    for (auto h : pages[active_page]) {
        const auto kind = widgets::kind(h);
        signature << ':' << h << ':' << bool(GetWindowLongPtrW(h, GWL_STYLE) & WS_VISIBLE)
                  << ':' << SendMessageW(h, WM_GETFONT, 0, 0);
        if (kind == widgets::Kind::Label || kind == widgets::Kind::Toggle ||
            (!kind && (GetWindowLongPtrW(h, GWL_STYLE) & BS_TYPEMASK) == BS_OWNERDRAW))
        {
            auto caption = text(h);
            signature << ':' << caption.size() << ':' << caption;
        }
    }
    auto key = signature.str();
    if (key == settings_layout_keys[active_page]) return;
    settings_layout_keys[active_page] = std::move(key);
    auto &cards = settings_cards[active_page];
    cards.clear();
    HDC dc = GetDC(body);
    const int inset = 16, left = 8 + inset, available = width - 2 * left;
    int top = 10;
    for (const auto &section : sections) {
        std::vector<HWND> visible;
        for (auto h : section)
            if (GetWindowLongPtrW(h, GWL_STYLE) & WS_VISIBLE) visible.push_back(h);
        if (visible.empty()) continue;
        std::stable_sort(visible.begin(), visible.end(), [&](auto a, auto b) {
            const auto &x = body_seeds.at(a), &y = body_seeds.at(b);
            return x.y < y.y;
        });
        int row_top = top + inset;
        for (size_t first = 0; first < visible.size();) {
            size_t end = first + 1;
            const int seed_y = body_seeds.at(visible[first]).y;
            while (end < visible.size() && body_seeds.at(visible[end]).y <= seed_y + 8) ++end;
            std::stable_sort(visible.begin() + first, visible.begin() + end, [&](auto a, auto b) {
                return body_seeds.at(a).x < body_seeds.at(b).x;
            });
            struct Cell { HWND h; int w, minimum, gap; };
            std::vector<Cell> cells;
            int used = 0;
            for (size_t i = first; i < end; ++i) {
                auto h = visible[i];
                const auto &seed = body_seeds.at(h);
                int x = left + MulDiv(seed.x - 20, available, 606);
                int w = std::max(16, MulDiv(seed.w, available, 606));
                w = std::min(w, width - left - x);
                if (i + 1 < end) {
                    const auto &next = body_seeds.at(visible[i + 1]);
                    const int next_x = left + MulDiv(next.x - 20, available, 606);
                    w = std::min(w, std::max(16, next_x - x - 8));
                }
                int gap = cells.empty() ? 0 : 8;
                int minimum = std::min(available, settings_min_width(h, dc));
                cells.push_back({h, std::max(w, minimum), minimum, gap});
                used += cells.back().w + gap;
            }
            // Let editable fields/long columns lend unused width to translated
            // labels and buttons. Wrap a row only if all minimums cannot fit.
            while (used > available) {
                auto donor = std::max_element(cells.begin(), cells.end(), [](const auto &a, const auto &b) {
                    return a.w - a.minimum < b.w - b.minimum;
                });
                int spare = donor->w - donor->minimum;
                if (!spare) break;
                int take = std::min(spare, used - available);
                donor->w -= take;
                used -= take;
            }
            std::vector<HWND> line;
            int cursor = left, height = 0;
            auto finish_line = [&] {
                for (auto h : line) {
                    auto &p = body_positions[h];
                    p.y += (height - p.h) / 2;
                }
                row_top += height + 12;
                line.clear(); cursor = left; height = 0;
            };
            for (const auto &cell : cells) {
                int gap = line.empty() ? 0 : cell.gap;
                if (!line.empty() && cursor + gap + cell.w > width - left) { finish_line(); gap = 0; }
                auto h = cell.h;
                int minimum_height = body_seeds.at(h).h;
                if (widgets::kind(h) == widgets::Kind::Toggle)
                    minimum_height = std::max(minimum_height, active_theme.metrics.toggle_height + 4);
                int hgt = std::max(minimum_height, settings_caption_height(h, cell.w, dc));
                body_positions[h] = {cursor + gap, row_top, cell.w, hgt};
                height = std::max(height, hgt);
                cursor += gap + cell.w;
                line.push_back(h);
            }
            finish_line();
            first = end;
        }
        int height = row_top - 12 + inset - top;
        cards.push_back({8, top, width - 16, height});
        top += height + 12;
    }
    ReleaseDC(body, dc);
}
#ifdef BL_UI_TESTS
void verify_settings_page_layout() {
    auto require = [](bool ok, const std::string &why) {
        if (!ok) throw std::runtime_error(why);
    };
    HDC dc = GetDC(body);
    std::vector<HWND> visible;
    for (auto h : pages[active_page]) {
        if (!(GetWindowLongPtrW(h, GWL_STYLE) & WS_VISIBLE)) continue;
        visible.push_back(h);
        const auto &p = body_positions.at(h);
        int owners = 0;
        for (const auto &card : group_cards())
            if (p.x >= card.x + 12 && p.y >= card.y + 12 &&
                p.x + p.w <= card.x + card.w - 12 && p.y + p.h <= card.y + card.h - 12) ++owners;
        require(owners == 1, "Settings control crosses a section or lacks padding: " + text(h));
        require(settings_caption_height(h, p.w, dc) <= p.h,
                "Localized settings caption is clipped vertically: " + text(h));
        require(settings_min_width(h, dc) <= p.w,
                "Localized settings caption is clipped horizontally: " + text(h));
    }
    ReleaseDC(body, dc);
    for (size_t i = 0; i < visible.size(); ++i)
        for (size_t j = i + 1; j < visible.size(); ++j) {
            auto a = body_positions.at(visible[i]), b = body_positions.at(visible[j]);
            require(a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y,
                    "Settings controls overlap: " + text(visible[i]) + " / " + text(visible[j]));
        }
}
void verify_settings_layout_variants() {
    const auto original_preview = appearance_preview;
    const auto original_page = active_page;
    RECT original{};
    GetWindowRect(settings, &original);
    for (auto style : {"ios", "terminal", "classic"}) {
        auto candidate = config;
        theme_registry.change_style(candidate, style);
        appearance_transaction([&] { appearance_preview = candidate; });
        for (int content_width : {650, 520}) {
            SetWindowPos(settings, nullptr, 0, 0, scale(232 + content_width, settings),
                         original.bottom - original.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            for (int page = 0; page < int(pages.size()); ++page) {
                switch_page(page);
                if (page == 2 && text(incoming_prompt_help).empty())
                    throw std::runtime_error("Incoming engine prompt capability hint is empty");
                if (page == 3 && text(outbound_prompt_help).empty())
                    throw std::runtime_error("Outgoing engine prompt capability hint is empty");
                verify_settings_page_layout();
            }
            HDC dc = GetDC(settings);
            for (auto h : navigation) {
                RECT bounds{};
                GetClientRect(h, &bounds);
                const int width = MulDiv(bounds.right, 96, int(GetDpiForWindow(h))) - (beveled() ? 0 : 30);
                const int height = MulDiv(bounds.bottom, 96, int(GetDpiForWindow(h)));
                if (settings_caption_height(h, width, dc) > height) {
                    ReleaseDC(settings, dc);
                    throw std::runtime_error("Localized sidebar caption is clipped: " + text(h));
                }
            }
            ReleaseDC(settings, dc);
        }
    }
    SetWindowPos(settings, nullptr, 0, 0, original.right - original.left, original.bottom - original.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    appearance_transaction([&] { appearance_preview = original_preview; });
    switch_page(original_page);
}
#endif
