
// ---- The settings window ----
//
// A top level window on the UI thread of the Explorer window that opened it, with
// a XAML island over it: a sidebar of pages on the left, a scrolling page on the
// right with a header, a description and cards of options. Mica behind it, as in
// Windows Settings. One window only. Spec: SPEC-settings-shell.md.
//
// In this version the Columns page is read only: it shows the settings as they are.

extern bool g_hooked;  // Defined with the hooks, later.
void LoadSettings();
void CloseEverything();
void ReloadAll(WindowState* state);

constexpr wchar_t kSettingsClass[] = L"ExplorerExtrasSettingsWindow";

struct SettingsUi {
    mux::Hosting::DesktopWindowXamlSource source{nullptr};
    winrt::Microsoft::UI::Content::DesktopChildSiteBridge bridge{nullptr};
    muxc::Grid root{nullptr};
    muxc::ListView nav{nullptr};
    muxc::ScrollViewer content{nullptr};
    int page = 0;
    bool abandon = false;  // Let the XAML objects go without releasing them.
};

HWND g_settingsWindow = nullptr;
DWORD g_settingsThread = 0;
SettingsUi* g_settingsUi = nullptr;
bool g_settingsClassMade = false;
std::atomic<bool> g_settingsAbandon{false};  // The mod is unloading.
bool g_settingsBuilding = false;  // A page is being built: its controls change nothing.

constexpr wchar_t kXamlNs[] = L"xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation'";

winrt::Windows::Foundation::IInspectable LoadXaml(std::wstring const& markup) {
    return winrt::Microsoft::UI::Xaml::Markup::XamlReader::Load(markup);
}

muxc::TextBlock MakeText(std::wstring const& text, double size, bool semibold, bool secondary) {
    std::wstring markup = L"<TextBlock " + std::wstring(kXamlNs) + L" TextWrapping='Wrap'";
    if (secondary) {
        markup += L" Foreground='{ThemeResource TextFillColorSecondaryBrush}'";
    }
    markup += L"/>";
    auto block = LoadXaml(markup).as<muxc::TextBlock>();
    block.Text(text);
    block.FontSize(size);
    if (semibold) {
        block.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
    }
    return block;
}

muxc::Border MakeCard() {
    return LoadXaml(std::wstring(L"<Border ") + kXamlNs +
                    L" Background='{ThemeResource CardBackgroundFillColorDefaultBrush}'"
                    L" BorderBrush='{ThemeResource CardStrokeColorDefaultBrush}'"
                    L" BorderThickness='1' CornerRadius='8' Padding='16,12'/>")
        .as<muxc::Border>();
}

// A line of a card: a name with its description on the left, a control on the right.
muxc::Grid MakeSettingRow(std::wstring const& name, std::wstring const& description,
                          mux::UIElement const& control) {
    muxc::Grid row;
    muxc::ColumnDefinition textColumn, controlColumn;
    textColumn.Width(mux::GridLength{1, mux::GridUnitType::Star});
    controlColumn.Width(mux::GridLength{0, mux::GridUnitType::Auto});
    row.ColumnDefinitions().Append(textColumn);
    row.ColumnDefinitions().Append(controlColumn);
    row.ColumnSpacing(24);
    row.Padding(mux::Thickness{0, 8, 0, 8});
    muxc::StackPanel texts;
    texts.VerticalAlignment(mux::VerticalAlignment::Center);
    texts.Children().Append(MakeText(name, 14, false, false));
    if (!description.empty()) {
        auto detail = MakeText(description, 12, false, true);
        detail.Margin(mux::Thickness{0, 2, 0, 0});
        texts.Children().Append(detail);
    }
    muxc::Grid::SetColumn(texts, 0);
    row.Children().Append(texts);
    if (control) {
        auto frame = control.as<mux::FrameworkElement>();
        frame.VerticalAlignment(mux::VerticalAlignment::Center);
        muxc::Grid::SetColumn(frame, 1);
        row.Children().Append(control);
    }
    return row;
}

// ---- Applying a setting chosen in the window ----

// What a window does when a setting changed that affects columns already built:
// the widths the user has not set are fitted again, and the folders are read again.
void RebuildForSettings() {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || g_unloading) {
        return;
    }
    try {
        for (auto& column : state->columns) {
            if (column->list && column->filled && !column->userSized) {
                FitWidth(column.get());
                RefreshNames(column.get());
            }
        }
        ReloadAll(state);
        UpdatePill(state, false);
    } catch (...) {
        LogHresult(L"rebuilding after a setting");
    }
}

void RebuildEverywhere() {
    std::vector<mud::DispatcherQueue> queues;
    {
        std::lock_guard<std::mutex> lock(g_statesMutex);
        for (auto& state : g_states) {
            if (state->queue) {
                queues.push_back(state->queue);
            }
        }
    }
    for (auto& queue : queues) {
        try {
            queue.TryEnqueue([]() { RebuildForSettings(); });
        } catch (...) {
        }
    }
}

// Switched off from the window: every window is cleaned up from another thread, since
// the cleanup waits for this one to run its part.
void TurnColumnsOffEverywhere() {
    std::thread([]() { CloseEverything(); }).detach();
}

void ApplySettingChanged(const SettingInfo& info) {
    LoadSettings();
    Out(L"setting \"%s\" changed in the settings window, now \"%s\"", info.id, EffectiveText(&info).c_str());
    if (!g_enabled) {
        TurnColumnsOffEverywhere();
    } else if (info.rebuild) {
        RebuildEverywhere();
    }
}

void CommitSetting(std::wstring const& id, std::wstring const& text) {
    if (g_settingsBuilding || g_unloading) {
        return;
    }
    const SettingInfo* info = FindSetting(id.c_str());
    if (!info || EffectiveText(info) == text) {
        return;
    }
    WriteSettingOverride(info, text);
    ApplySettingChanged(*info);
}

void ShowSettingsPage(SettingsUi* ui, int page);

void ResetSettingsToWindhawk() {
    ClearAllOverrides();
    LoadSettings();
    Out(L"settings reset to Windhawk's page");
    if (!g_enabled) {
        TurnColumnsOffEverywhere();
    } else {
        RebuildEverywhere();
    }
    if (g_settingsUi) {
        ShowSettingsPage(g_settingsUi, g_settingsUi->page);
    }
}

// The control of a setting: it shows the value in use and writes the user's choice.
mux::UIElement MakeSettingControl(SettingInfo const& info) {
    switch (info.kind) {
        case SettingKind::Bool: {
            muxc::ToggleSwitch toggle;
            toggle.OnContent(winrt::box_value(L""));
            toggle.OffContent(winrt::box_value(L""));
            toggle.MinWidth(0);
            toggle.IsOn(SettingBool(info.id));
            std::wstring id = info.id;
            toggle.Toggled([id](Inspectable const& sender, mux::RoutedEventArgs const&) {
                if (auto changed = sender.try_as<muxc::ToggleSwitch>()) {
                    CommitSetting(id, changed.IsOn() ? L"1" : L"0");
                }
            });
            return toggle;
        }
        case SettingKind::Int: {
            muxc::NumberBox box;
            box.Minimum(info.minValue);
            box.Maximum(info.maxValue ? info.maxValue : 1000000);
            box.Value(SettingInt(info.id));
            box.Width(140);
            box.SpinButtonPlacementMode(muxc::NumberBoxSpinButtonPlacementMode::Compact);
            box.ValidationMode(muxc::NumberBoxValidationMode::InvalidInputOverwritten);
            std::wstring id = info.id;
            box.ValueChanged([id](muxc::NumberBox const&, muxc::NumberBoxValueChangedEventArgs const& args) {
                double value = args.NewValue();
                if (value == value) {  // Not a number while the box is cleared.
                    CommitSetting(id, std::to_wstring((int)value));
                }
            });
            return box;
        }
        case SettingKind::Enum: {
            muxc::ComboBox combo;
            std::wstring options = info.options;
            std::wstring current = SettingText(info.id);
            int selected = 0, index = 0;
            size_t at = 0;
            while (at <= options.size() && !options.empty()) {
                size_t end = options.find(L'|', at);
                std::wstring pair = options.substr(at, end == std::wstring::npos ? std::wstring::npos : end - at);
                size_t equals = pair.find(L'=');
                if (equals != std::wstring::npos) {
                    combo.Items().Append(winrt::box_value(winrt::hstring{pair.substr(equals + 1)}));
                    if (pair.substr(0, equals) == current) {
                        selected = index;
                    }
                    index++;
                }
                if (end == std::wstring::npos) {
                    break;
                }
                at = end + 1;
            }
            combo.SelectedIndex(selected);
            combo.MinWidth(220);
            std::wstring id = info.id;
            std::wstring choices = info.options;
            combo.SelectionChanged([id, choices](Inspectable const& sender, muxc::SelectionChangedEventArgs const&) {
                auto box = sender.try_as<muxc::ComboBox>();
                int index = box ? box.SelectedIndex() : -1;
                if (index < 0) {
                    return;
                }
                size_t at = 0;
                for (int i = 0;; i++) {
                    size_t end = choices.find(L'|', at);
                    std::wstring pair = choices.substr(at, end == std::wstring::npos ? std::wstring::npos : end - at);
                    if (i == index) {
                        size_t equals = pair.find(L'=');
                        if (equals != std::wstring::npos) {
                            CommitSetting(id, pair.substr(0, equals));
                        }
                        return;
                    }
                    if (end == std::wstring::npos) {
                        return;
                    }
                    at = end + 1;
                }
            });
            return combo;
        }
        default:
            break;
    }
    muxc::TextBlock text;
    text.Text(SettingText(info.id));
    return text;
}

muxc::StackPanel MakePageHeader(std::wstring const& title, std::wstring const& description) {
    muxc::StackPanel header;
    header.Spacing(4);
    header.Margin(mux::Thickness{0, 0, 0, 12});
    header.Children().Append(MakeText(title, 28, true, false));
    if (!description.empty()) {
        header.Children().Append(MakeText(description, 14, false, true));
    }
    return header;
}

std::wstring WindowsBuildText() {
    typedef LONG(WINAPI * RtlGetVersion_t)(OSVERSIONINFOW*);
    OSVERSIONINFOW info = {};
    info.dwOSVersionInfoSize = sizeof(info);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        if (auto get = (RtlGetVersion_t)GetProcAddress(ntdll, "RtlGetVersion")) {
            get(&info);
        }
    }
    return L"Windows build " + std::to_wstring(info.dwBuildNumber);
}

mux::UIElement BuildHomePage() {
    muxc::StackPanel page;
    page.Spacing(8);
    page.Children().Append(MakePageHeader(L"Explorer Extras", L"Optional additions to File Explorer, each one with its own page and switch."));

    // The features.
    {
        auto title = MakeText(L"Features", 14, true, false);
        title.Margin(mux::Thickness{0, 8, 0, 0});
        page.Children().Append(title);
        auto card = MakeCard();
        const SettingInfo* enabled = FindSetting(L"enabled");
        muxc::ToggleSwitch toggle;
        toggle.OnContent(winrt::box_value(L""));
        toggle.OffContent(winrt::box_value(L""));
        toggle.MinWidth(0);
        toggle.IsOn(SettingBool(L"enabled"));
        toggle.Toggled([](Inspectable const& sender, mux::RoutedEventArgs const&) {
            if (auto changed = sender.try_as<muxc::ToggleSwitch>()) {
                CommitSetting(L"enabled", changed.IsOn() ? L"1" : L"0");
            }
        });
        card.Child(MakeSettingRow(L"Columns", enabled ? enabled->description : L"", toggle));
        page.Children().Append(card);
    }
    // Status and version.
    {
        auto title = MakeText(L"Status", 14, true, false);
        title.Margin(mux::Thickness{0, 12, 0, 0});
        page.Children().Append(title);
        auto card = MakeCard();
        muxc::StackPanel rows;
        rows.Spacing(2);
        rows.Children().Append(MakeSettingRow(L"Version", L"", MakeText(WH_MOD_VERSION, 14, false, true)));
        rows.Children().Append(MakeSettingRow(L"Windows", L"", MakeText(WindowsBuildText(), 14, false, true)));
        rows.Children().Append(MakeSettingRow(L"Hooks", L"The Explorer extension the mod hooks into.",
                                              MakeText(g_hooked ? L"Found" : L"Not found", 14, false, true)));
        card.Child(rows);
        page.Children().Append(card);
    }
    // Quick actions, to come.
    {
        auto title = MakeText(L"Quick actions", 14, true, false);
        title.Margin(mux::Thickness{0, 12, 0, 0});
        page.Children().Append(title);
        auto card = MakeCard();
        muxc::StackPanel rows;
        for (auto const& label : {L"Open the log", L"Copy diagnostics", L"Reset to Windhawk's settings"}) {
            muxc::Button button;
            button.Content(winrt::box_value(label));
            button.MinWidth(180);
            bool isReset = wcsncmp(label, L"Reset", 5) == 0;
            button.IsEnabled(isReset);
            if (isReset) {
                button.Click([](Inspectable const&, mux::RoutedEventArgs const&) { ResetSettingsToWindhawk(); });
            }
            rows.Children().Append(MakeSettingRow(label, isReset ? L"Every setting goes back to what Windhawk's page for the mod says." : L"", button));
        }
        card.Child(rows);
        page.Children().Append(card);
    }
    return page;
}

mux::UIElement BuildColumnsPage() {
    muxc::StackPanel page;
    page.Spacing(8);
    page.Children().Append(MakePageHeader(
        L"Columns", L"A Columns layout in the View menu: Miller columns drawn with Explorer's own controls."));
    {
        auto note = MakeCard();
        note.Child(MakeText(
            L"Changes apply at once. The mod keeps them in front of Windhawk's page, and a later edit on that page for the same setting wins. Reset on Home goes back to Windhawk's values.",
            12, false, true));
        page.Children().Append(note);
    }
    for (size_t c = 0; c < kCardCount; c++) {
        bool any = false;
        for (size_t i = 0; i < kSettingCount; i++) {
            any = any || (!kSettings[i].windhawkOnly && wcscmp(kSettings[i].page, L"columns") == 0 &&
                          wcscmp(kSettings[i].card, kCards[c].id) == 0);
        }
        if (!any) {
            continue;
        }
        auto title = MakeText(kCards[c].name, 14, true, false);
        title.Margin(mux::Thickness{0, 12, 0, 0});
        page.Children().Append(title);
        auto card = MakeCard();
        muxc::StackPanel rows;
        rows.Spacing(2);
        for (size_t i = 0; i < kSettingCount; i++) {
            auto const& info = kSettings[i];
            if (info.windhawkOnly || wcscmp(info.page, L"columns") != 0 || wcscmp(info.card, kCards[c].id) != 0) {
                continue;
            }
            rows.Children().Append(MakeSettingRow(info.name, info.description, MakeSettingControl(info)));
        }
        card.Child(rows);
        page.Children().Append(card);
    }
    return page;
}

void ShowSettingsPage(SettingsUi* ui, int page) {
    ui->page = page;
    g_settingsBuilding = true;
    mux::UIElement content{nullptr};
    try {
        content = page == 0 ? BuildHomePage() : BuildColumnsPage();
    } catch (...) {
        g_settingsBuilding = false;
        throw;
    }
    ui->content.Content(content);
    g_settingsBuilding = false;
    ui->content.ChangeView(nullptr, 0.0, nullptr, true);
}

// The window's backdrop and caption follow the theme of its content.
void ApplySettingsWindowTheme(HWND window, SettingsUi* ui) {
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) {
        return;
    }
    typedef HRESULT(WINAPI * Set_t)(HWND, DWORD, const void*, DWORD);
    struct FrameMargins {
        int left, right, top, bottom;
    };
    typedef HRESULT(WINAPI * Extend_t)(HWND, const FrameMargins*);
    auto set = (Set_t)GetProcAddress(dwm, "DwmSetWindowAttribute");
    auto extend = (Extend_t)GetProcAddress(dwm, "DwmExtendFrameIntoClientArea");
    if (set) {
        BOOL dark = ui && ui->root && ui->root.ActualTheme() == mux::ElementTheme::Dark;
        set(window, 20, &dark, sizeof(dark));          // DWMWA_USE_IMMERSIVE_DARK_MODE
        int backdrop = 2;                              // DWMSBT_MAINWINDOW: Mica
        set(window, 38, &backdrop, sizeof(backdrop));  // DWMWA_SYSTEMBACKDROP_TYPE
    }
    if (extend) {
        FrameMargins margins = {-1, -1, -1, -1};
        extend(window, &margins);
    }
    FreeLibrary(dwm);
}

void RememberSettingsWindow(HWND window) {
    if (!g_settingsUi || IsIconic(window) || IsZoomed(window)) {
        return;
    }
    RECT box;
    if (GetWindowRect(window, &box)) {
        wchar_t text[96];
        swprintf(text, 96, L"%ld,%ld,%ld,%ld,%d", box.left, box.top, box.right - box.left, box.bottom - box.top,
                 g_settingsUi->page);
        Wh_SetStringValue(L"ui.window", text);
    }
}

// Ends the window. A window closed by the user is released properly; one whose
// owner is going (the Explorer window, or the mod) is let go without releasing,
// as the panel is, since releasing XAML objects there crashes Explorer.
void DestroySettingsUi(bool abandon) {
    SettingsUi* ui = g_settingsUi;
    g_settingsUi = nullptr;
    if (!ui) {
        return;
    }
    if (abandon) {
        (void)winrt::detach_abi(ui->source);
        (void)winrt::detach_abi(ui->bridge);
        (void)winrt::detach_abi(ui->root);
        (void)winrt::detach_abi(ui->nav);
        (void)winrt::detach_abi(ui->content);
        return;  // The struct itself is let go with them.
    }
    try {
        ui->nav = nullptr;
        ui->content = nullptr;
        if (ui->source) {
            ui->source.Content(nullptr);
            ui->source.Close();
        }
    } catch (...) {
        LogHresult(L"closing the settings window");
    }
    ui->source = nullptr;
    ui->bridge = nullptr;
    ui->root = nullptr;
    delete ui;
}

LRESULT CALLBACK SettingsWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            if (g_settingsUi && g_settingsUi->bridge && wparam != SIZE_MINIMIZED) {
                try {
                    RECT client;
                    GetClientRect(window, &client);
                    g_settingsUi->bridge.MoveAndResize(winrt::Windows::Graphics::RectInt32{
                        0, 0, client.right - client.left, client.bottom - client.top});
                } catch (...) {
                }
            }
            return 0;
        case WM_GETMINMAXINFO: {
            auto* limits = (MINMAXINFO*)lparam;
            UINT dpi = GetDpiForWindow(window);
            limits->ptMinTrackSize.x = MulDiv(760, dpi, 96);
            limits->ptMinTrackSize.y = MulDiv(520, dpi, 96);
            return 0;
        }
        case WM_DPICHANGED: {
            auto* suggested = (RECT*)lparam;
            SetWindowPos(window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_EXITSIZEMOVE:
            RememberSettingsWindow(window);
            return 0;
        case WM_CLOSE:
            RememberSettingsWindow(window);
            if (!g_settingsAbandon) {
                // Closing only hides it: the XAML island lives on, so opening again is
                // instant and nothing is created or released while Explorer runs.
                ShowWindow(window, SW_HIDE);
                return 0;
            }
            DestroySettingsUi(true);
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            if (g_settingsWindow == window) {
                g_settingsWindow = nullptr;
            }
            DestroySettingsUi(true);  // A window destroyed another way: nothing is released.
            return 0;
        default:
            break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

// Opens the window, or brings it forward. Called on the UI thread of an Explorer window.
void OpenSettingsWindow(WindowState* state) {
    if (g_unloading) {
        return;
    }
    if (g_settingsWindow && IsWindow(g_settingsWindow)) {
        // The XAML objects belong to the thread that made them: only that thread
        // reads the settings into the page again.
        if (g_settingsThread == GetCurrentThreadId() && g_settingsUi) {
            try {
                ShowSettingsPage(g_settingsUi, g_settingsUi->page);
            } catch (...) {
                LogHresult(L"refreshing the settings window");
            }
        }
        ShowWindow(g_settingsWindow, IsIconic(g_settingsWindow) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(g_settingsWindow);
        RedrawWindow(g_settingsWindow, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        return;
    }
    try {
        if (!g_settingsClassMade) {
            WNDCLASSEXW cls = {};
            cls.cbSize = sizeof(cls);
            cls.lpfnWndProc = SettingsWindowProc;
            cls.hInstance = GetModuleHandleW(nullptr);
            cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            cls.lpszClassName = kSettingsClass;
            if (!RegisterClassExW(&cls)) {
                Out(L"settings window: the class could not be registered, gle=%lu", GetLastError());
                return;
            }
            g_settingsClassMade = true;
        }
        // The last place and page, if they are still on a screen.
        int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
        UINT dpi = GetDpiForSystem();
        int width = MulDiv(980, dpi, 96), height = MulDiv(700, dpi, 96);
        int page = 0;
        {
            wchar_t saved[96] = L"";
            Wh_GetStringValue(L"ui.window", saved, ARRAYSIZE(saved));
            long sx, sy, sw, sh;
            int sp;
            if (swscanf(saved, L"%ld,%ld,%ld,%ld,%d", &sx, &sy, &sw, &sh, &sp) == 5 && sw > 300 && sh > 300) {
                RECT box = {sx, sy, sx + sw, sy + sh};
                if (MonitorFromRect(&box, MONITOR_DEFAULTTONULL)) {
                    x = sx;
                    y = sy;
                    width = sw;
                    height = sh;
                }
                page = sp == 1 ? 1 : 0;
            }
        }
        HWND window = CreateWindowExW(0, kSettingsClass, L"Explorer Extras", WS_OVERLAPPEDWINDOW, x, y, width, height,
                                      nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!window) {
            Out(L"settings window: could not be created, gle=%lu", GetLastError());
            return;
        }
        g_settingsWindow = window;
        g_settingsThread = GetCurrentThreadId();
        auto* ui = new SettingsUi();
        g_settingsUi = ui;

        ui->source = mux::Hosting::DesktopWindowXamlSource();
        ui->source.Initialize(winrt::Microsoft::UI::WindowId{(uint64_t)window});
        ui->bridge = ui->source.SiteBridge();

        // The layout: a sidebar of pages, a page that scrolls.
        // The island paints its own opaque background: with a transparent root, a white
        // rectangle of the island's first size showed through over the Mica backdrop.
        auto root = LoadXaml(std::wstring(L"<Grid ") + kXamlNs +
                             L" Background='{ThemeResource SolidBackgroundFillColorBaseBrush}'/>")
                        .as<muxc::Grid>();
        muxc::ColumnDefinition sidebarColumn, pageColumn;
        sidebarColumn.Width(mux::GridLength{248, mux::GridUnitType::Pixel});
        pageColumn.Width(mux::GridLength{1, mux::GridUnitType::Star});
        root.ColumnDefinitions().Append(sidebarColumn);
        root.ColumnDefinitions().Append(pageColumn);
        ui->root = root;

        muxc::ListView nav;
        nav.Margin(mux::Thickness{8, 16, 8, 8});
        nav.SelectionMode(muxc::ListViewSelectionMode::Single);
        for (int i = 0; i < 2; i++) {
            muxc::ListViewItem item;
            muxc::StackPanel line;
            line.Orientation(muxc::Orientation::Horizontal);
            line.Spacing(12);
            if (i == 0) {
                muxc::FontIcon icon;
                icon.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
                icon.Glyph(L"");
                icon.FontSize(16);
                line.Children().Append(icon);
            } else {
                auto icon = MakeColumnsIcon();
                icon.as<mux::FrameworkElement>().VerticalAlignment(mux::VerticalAlignment::Center);
                line.Children().Append(icon);
            }
            auto label = MakeText(i == 0 ? L"Home" : L"Columns", 14, false, false);
            label.VerticalAlignment(mux::VerticalAlignment::Center);
            line.Children().Append(label);
            item.Content(line);
            item.Height(40);
            nav.Items().Append(item);
        }
        muxc::Grid::SetColumn(nav, 0);
        root.Children().Append(nav);
        ui->nav = nav;

        muxc::ScrollViewer content;
        content.VerticalScrollBarVisibility(muxc::ScrollBarVisibility::Auto);
        content.Padding(mux::Thickness{32, 24, 32, 32});
        muxc::Grid::SetColumn(content, 1);
        root.Children().Append(content);
        ui->content = content;

        nav.SelectionChanged([](Inspectable const&, muxc::SelectionChangedEventArgs const&) {
            SettingsUi* now = g_settingsUi;
            if (now && now->nav) {
                int selected = now->nav.SelectedIndex();
                if (selected >= 0 && selected != now->page) {
                    ShowSettingsPage(now, selected);
                }
            }
        });
        root.ActualThemeChanged([window](mux::FrameworkElement const&, Inspectable const&) {
            ApplySettingsWindowTheme(window, g_settingsUi);
            RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        });
        ui->source.Content(root);

        nav.SelectedIndex(page);
        ShowSettingsPage(ui, page);
        ApplySettingsWindowTheme(window, ui);
        RECT client;
        GetClientRect(window, &client);
        ui->bridge.MoveAndResize(winrt::Windows::Graphics::RectInt32{0, 0, client.right, client.bottom});
        ui->bridge.Show();
        ShowWindow(window, SW_SHOW);
        SetForegroundWindow(window);
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        Out(L"settings window opened, window=%p, dpi=%u, client %ldx%ld", window, GetDpiForWindow(window),
            client.right, client.bottom);
    } catch (...) {
        LogHresult(L"OpenSettingsWindow");
        if (g_settingsWindow) {
            DestroyWindow(g_settingsWindow);
        }
    }
}

// The Explorer window that owns the settings window is going: the window goes with it,
// its XAML objects let go.
void CloseSettingsWindowOfThread() {
    if (g_settingsWindow && g_settingsThread == GetCurrentThreadId() && IsWindow(g_settingsWindow)) {
        Out(L"the settings window goes with its Explorer window");
        g_settingsAbandon = true;
        DestroyWindow(g_settingsWindow);
        g_settingsAbandon = false;
    }
}

// The mod is unloading: ask the window to close, on its own thread, and let the class go.
void CloseSettingsWindowAtUnload() {
    g_settingsAbandon = true;
    if (g_settingsWindow && IsWindow(g_settingsWindow)) {
        DWORD_PTR result = 0;
        SendMessageTimeoutW(g_settingsWindow, WM_CLOSE, 0, 0, SMTO_ABORTIFHUNG, 3000, &result);
    }
    if (g_settingsClassMade) {
        UnregisterClassW(kSettingsClass, GetModuleHandleW(nullptr));
        g_settingsClassMade = false;
    }
}
