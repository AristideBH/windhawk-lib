
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
void ColumnsOffEverywhere();
void ReloadAll(WindowState* state);

constexpr wchar_t kSettingsClass[] = L"ExplorerExtrasSettingsWindow";

struct SettingsUi {
    mux::Hosting::DesktopWindowXamlSource source{nullptr};
    winrt::Microsoft::UI::Content::DesktopChildSiteBridge bridge{nullptr};
    muxc::Grid root{nullptr};
    muxc::ListView nav{nullptr};
    muxc::ScrollViewer content{nullptr};
    int page = 0;
    int navCount = 0;  // The pages in the sidebar.
    // Narrow window: the sidebar is a rail of icons, opened over the page by its button.
    muxc::ColumnDefinition sidebarColumn{nullptr};
    muxc::Grid navHost{nullptr};
    muxc::Button navToggle{nullptr};
    mux::Media::Brush overlayBrush{nullptr};
    std::vector<muxc::TextBlock> navLabels;
    std::vector<std::wstring> navNames;
    bool compact = false;
    bool expanded = false;
    bool abandon = false;  // Let the XAML objects go without releasing them.
};

HWND g_settingsWindow = nullptr;
DWORD g_settingsThread = 0;
SettingsUi* g_settingsUi = nullptr;
bool g_settingsClassMade = false;
std::atomic<bool> g_settingsAbandon{false};  // The window or the mod is going.
void DrainUi(mud::DispatcherQueue const& queue);  // 11-window
std::atomic<bool> g_settingsUnload{false};   // The mod is unloading: the XAML objects are released, not left.
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
            // The shading of every other row, on the rows that are already there.
            if (column->list) {
                auto items = column->list.Items();
                for (uint32_t i = 0; i < items.Size() && i < column->rowToEntry.size(); i++) {
                    int entry = column->rowToEntry[i];
                    if (entry < 0) {
                        continue;
                    }
                    if (auto item = items.GetAt(i).try_as<muxc::ListViewItem>()) {
                        if (g_oddRows && entry % 2 == 1) {
                            item.Background(Brush(13, 128, 128, 128));
                        } else {
                            item.Background(mux::Media::Brush{nullptr});
                        }
                        if (!item.IsSelected()) {
                            // The row repaints its own background only when its visual state is
                            // entered again (it showed only once the mouse went over it).
                            mux::VisualStateManager::GoToState(item, L"PointerOver", false);
                            mux::VisualStateManager::GoToState(item, L"Normal", false);
                        }
                    }
                }
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
    // The details pane stays when only Columns goes.
    StartModThread([]() {
        if (g_detailsPane) {
            ColumnsOffEverywhere();
        } else {
            CloseEverything();
        }
    });
}

void ApplySettingChanged(const SettingInfo& info) {
    LoadSettings();
    Wh_Log(L"setting \"%s\" changed in the settings window, now \"%s\"", info.id, EffectiveText(&info).c_str());
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
    Wh_Log(L"settings reset to Windhawk's page");
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
            Keep(toggle, toggle.Toggled(winrt::auto_revoke, [id](Inspectable const& sender, mux::RoutedEventArgs const&) {
                if (auto changed = sender.try_as<muxc::ToggleSwitch>()) {
                    CommitSetting(id, changed.IsOn() ? L"1" : L"0");
                }
            }));
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
            Keep(box, box.ValueChanged(winrt::auto_revoke, [id](muxc::NumberBox const&, muxc::NumberBoxValueChangedEventArgs const& args) {
                double value = args.NewValue();
                if (value == value) {  // Not a number while the box is cleared.
                    CommitSetting(id, std::to_wstring((int)value));
                }
            }));
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
            Keep(combo, combo.SelectionChanged(winrt::auto_revoke, [id, choices](Inspectable const& sender, muxc::SelectionChangedEventArgs const&) {
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
            }));
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

// ---- Quick actions ----

std::wstring DiagnosticsText() {
    std::wstring text = L"Explorer Extras ";
    text += WH_MOD_VERSION;
    text += L"\r\n" + WindowsBuildText() + L"\r\n";
    text += g_hooked ? L"Hooks: found\r\n" : L"Hooks: not found\r\n";
    {
        std::lock_guard<std::mutex> lock(g_statesMutex);
        text += L"Explorer windows known: " + std::to_wstring(g_states.size()) + L"\r\n";
    }
    text += L"Settings (an asterisk marks one set in the settings window):\r\n";
    for (size_t i = 0; i < kSettingCount; i++) {
        auto const& info = kSettings[i];
        text += L"  ";
        text += SettingIsOverridden(&info) ? L"* " : L"  ";
        text += info.id;
        text += L" = " + EffectiveText(&info) + L"\r\n";
    }
    return text;
}

bool CopyTextToClipboard(HWND owner, std::wstring const& text) {
    if (!OpenClipboard(owner)) {
        return false;
    }
    bool done = false;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* target = GlobalLock(memory)) {
            memcpy(target, text.c_str(), bytes);
            GlobalUnlock(memory);
            done = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
        }
        if (!done) {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
    return done;
}

// What's new and the tip.
bool TipDismissed() {
    return Wh_GetIntValue(L"ui.tipDismissed", 0) != 0;
}

enum class QuickAction { Copy, Reset };

mux::UIElement BuildHomePage() {
    muxc::StackPanel page;
    page.Spacing(8);
    page.Children().Append(MakePageHeader(Str(L"home.title"), Str(L"home.subtitle")));

    // A tip, until it is dismissed.
    if (!TipDismissed()) {
        auto card = MakeCard();
        muxc::Grid line;
        muxc::ColumnDefinition textColumn, buttonColumn;
        textColumn.Width(mux::GridLength{1, mux::GridUnitType::Star});
        buttonColumn.Width(mux::GridLength{0, mux::GridUnitType::Auto});
        line.ColumnDefinitions().Append(textColumn);
        line.ColumnDefinitions().Append(buttonColumn);
        line.ColumnSpacing(16);
        muxc::StackPanel texts;
        texts.Children().Append(MakeText(Str(L"tip.title"), 14, true, false));
        auto body = MakeText(Str(L"tip.body"), 13, false, true);
        body.Margin(mux::Thickness{0, 2, 0, 0});
        texts.Children().Append(body);
        muxc::Grid::SetColumn(texts, 0);
        line.Children().Append(texts);
        muxc::Button gotIt;
        gotIt.Content(winrt::box_value(winrt::hstring{Str(L"tip.gotit")}));
        gotIt.VerticalAlignment(mux::VerticalAlignment::Center);
        Keep(gotIt, gotIt.Click(winrt::auto_revoke, [card](Inspectable const&, mux::RoutedEventArgs const&) {
            Wh_SetIntValue(L"ui.tipDismissed", 1);
            card.Visibility(mux::Visibility::Collapsed);
        }));
        muxc::Grid::SetColumn(gotIt, 1);
        line.Children().Append(gotIt);
        card.Child(line);
        page.Children().Append(card);
    }

    // The features.
    {
        auto title = MakeText(Str(L"card.features"), 14, true, false);
        title.Margin(mux::Thickness{0, 8, 0, 0});
        page.Children().Append(title);
        auto card = MakeCard();
        const SettingInfo* enabled = FindSetting(L"enabled");
        muxc::ToggleSwitch toggle;
        toggle.OnContent(winrt::box_value(L""));
        toggle.OffContent(winrt::box_value(L""));
        toggle.MinWidth(0);
        toggle.IsOn(SettingBool(L"enabled"));
        Keep(toggle, toggle.Toggled(winrt::auto_revoke, [](Inspectable const& sender, mux::RoutedEventArgs const&) {
            if (auto changed = sender.try_as<muxc::ToggleSwitch>()) {
                CommitSetting(L"enabled", changed.IsOn() ? L"1" : L"0");
            }
        }));
        muxc::StackPanel featureRows;
        featureRows.Spacing(2);
        featureRows.Children().Append(MakeSettingRow(Str(L"columns.title"), enabled ? SettingDescription(*enabled) : std::wstring(), toggle));
        const SettingInfo* paneInfo = FindSetting(L"detailsPane");
        if (paneInfo) {
            featureRows.Children().Append(MakeSettingRow(Str(L"details.title"), SettingDescription(*paneInfo), MakeSettingControl(*paneInfo)));
        }
        card.Child(featureRows);
        page.Children().Append(card);
    }
    // Status and version.
    {
        auto title = MakeText(Str(L"card.status"), 14, true, false);
        title.Margin(mux::Thickness{0, 12, 0, 0});
        page.Children().Append(title);
        auto card = MakeCard();
        muxc::StackPanel rows;
        rows.Spacing(2);
        rows.Children().Append(MakeSettingRow(Str(L"status.version"), L"", MakeText(WH_MOD_VERSION, 14, false, true)));
        rows.Children().Append(MakeSettingRow(Str(L"status.windows"), L"", MakeText(WindowsBuildText(), 14, false, true)));
        rows.Children().Append(MakeSettingRow(Str(L"status.hooks"), Str(L"status.hooks.detail"),
                                              MakeText(g_hooked ? Str(L"status.found") : Str(L"status.notfound"), 14, false, true)));
        card.Child(rows);
        page.Children().Append(card);
    }
    // Quick actions.
    {
        auto title = MakeText(Str(L"card.actions"), 14, true, false);
        title.Margin(mux::Thickness{0, 12, 0, 0});
        page.Children().Append(title);
        auto card = MakeCard();
        muxc::StackPanel rows;
        for (QuickAction action : {QuickAction::Copy, QuickAction::Reset}) {
            muxc::Button button;
            const wchar_t* label = action == QuickAction::Copy ? Str(L"action.copy") : Str(L"action.reset");
            const wchar_t* detail = action == QuickAction::Copy ? Str(L"action.copy.detail") : Str(L"action.reset.detail");
            button.Content(winrt::box_value(winrt::hstring{label}));
            button.MinWidth(180);
            if (action == QuickAction::Reset) {
                Keep(button, button.Click(winrt::auto_revoke, [](Inspectable const&, mux::RoutedEventArgs const&) { ResetSettingsToWindhawk(); }));
            } else {
                Keep(button, button.Click(winrt::auto_revoke, [](Inspectable const& sender, mux::RoutedEventArgs const&) {
                    bool copied = CopyTextToClipboard(g_settingsWindow, DiagnosticsText());
                    if (auto clicked = sender.try_as<muxc::Button>()) {
                        clicked.Content(winrt::box_value(winrt::hstring{copied ? Str(L"action.copy.done") : Str(L"action.copy.fail")}));
                    }
                }));
            }
            rows.Children().Append(MakeSettingRow(label, detail, button));
        }
        card.Child(rows);
        page.Children().Append(card);
    }
    // About: who made it, and where the code is.
    {
        auto title = MakeText(Str(L"card.about"), 14, true, false);
        title.Margin(mux::Thickness{0, 12, 0, 0});
        page.Children().Append(title);
        auto card = MakeCard();
        muxc::HyperlinkButton link;
        link.Content(winrt::box_value(winrt::hstring{Str(L"about.link")}));
        link.Padding(mux::Thickness{0, 0, 0, 0});
        Keep(link, link.Click(winrt::auto_revoke, [](Inspectable const&, mux::RoutedEventArgs const&) {
            ShellExecuteW(nullptr, L"open", L"https://github.com/AristideBH/windhawk-lib", nullptr, nullptr, SW_SHOWNORMAL);
        }));
        card.Child(MakeSettingRow(Str(L"about.creator"), Str(L"about.detail"), link));
        page.Children().Append(card);
    }
    return page;
}

mux::UIElement BuildSettingsPage(const wchar_t* pageId, const wchar_t* title, const wchar_t* subtitle, bool withNote) {
    muxc::StackPanel page;
    page.Spacing(8);
    page.Children().Append(MakePageHeader(title, subtitle));
    if (withNote) {
        auto note = MakeCard();
        note.Child(MakeText(Str(L"columns.note"), 12, false, true));
        page.Children().Append(note);
    }
    for (size_t c = 0; c < kCardCount; c++) {
        bool any = false;
        for (size_t i = 0; i < kSettingCount; i++) {
            any = any || (!kSettings[i].windhawkOnly && wcscmp(kSettings[i].page, pageId) == 0 &&
                          wcscmp(kSettings[i].card, kCards[c].id) == 0);
        }
        if (!any) {
            continue;
        }
        auto title = MakeText(CardName(c), 14, true, false);
        title.Margin(mux::Thickness{0, 12, 0, 0});
        page.Children().Append(title);
        auto card = MakeCard();
        muxc::StackPanel rows;
        rows.Spacing(2);
        for (size_t i = 0; i < kSettingCount; i++) {
            auto const& info = kSettings[i];
            if (info.windhawkOnly || wcscmp(info.page, pageId) != 0 || wcscmp(info.card, kCards[c].id) != 0) {
                continue;
            }
            rows.Children().Append(MakeSettingRow(SettingName(info), SettingDescription(info), MakeSettingControl(info)));
        }
        card.Child(rows);
        page.Children().Append(card);
    }
    return page;
}

// Explorer Extras (the overview), Columns and the details pane.
int SettingsPageCount() {
    return 3;
}

void ShowSettingsPage(SettingsUi* ui, int page) {
    ui->page = page;
    g_settingsBuilding = true;
    mux::UIElement content{nullptr};
    try {
        content = page == 1   ? BuildSettingsPage(L"columns", Str(L"columns.title"), Str(L"columns.subtitle"), true)
                  : page == 2 ? BuildSettingsPage(L"details", Str(L"details.title"), Str(L"details.subtitle"), false)
                              : BuildHomePage();
    } catch (...) {
        g_settingsBuilding = false;
        throw;
    }
    ui->content.Content(content);
    g_settingsBuilding = false;
    ui->content.ChangeView(nullptr, 0.0, nullptr, true);
}

// The sidebar: its pages, as the flag has them.
void ApplyNavMode(SettingsUi* ui);

void FillNav(SettingsUi* ui) {
    auto items = ui->nav.Items();
    items.Clear();
    ui->navLabels.clear();
    ui->navNames.clear();
    int count = SettingsPageCount();
    for (int i = 0; i < count; i++) {
        muxc::ListViewItem item;
        muxc::StackPanel line;
        line.Orientation(muxc::Orientation::Horizontal);
        line.Spacing(12);
        const wchar_t* name = nullptr;
        if (i == 1) {
            auto icon = MakeColumnsIcon();
            icon.as<mux::FrameworkElement>().VerticalAlignment(mux::VerticalAlignment::Center);
            line.Children().Append(icon);
            name = Str(L"columns.title");
        } else if (i == 2) {
            muxc::FontIcon icon;
            icon.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
            icon.Glyph(L"");  // Info.
            icon.FontSize(16);
            line.Children().Append(icon);
            name = Str(L"details.title");
        } else {
            muxc::FontIcon icon;
            icon.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
            icon.Glyph(L"");
            icon.FontSize(16);
            line.Children().Append(icon);
            name = Str(L"page.home");
        }
        auto label = MakeText(name, 14, false, false);
        label.VerticalAlignment(mux::VerticalAlignment::Center);
        line.Children().Append(label);
        ui->navLabels.push_back(label);
        ui->navNames.push_back(name);
        item.Content(line);
        item.Height(40);
        items.Append(item);
    }
    ui->navCount = count;
    if (ui->page >= count) {
        ui->page = 0;
    }
    ui->nav.SelectedIndex(ui->page);
    ApplyNavMode(ui);
}

// The sidebar's look for the window's width: full names, or a rail of icons (with the names
// as tooltips) that a button opens over the page.
void ApplyNavMode(SettingsUi* ui) {
    if (!ui->navHost || !ui->sidebarColumn) {
        return;
    }
    bool rail = ui->compact && !ui->expanded;
    ui->sidebarColumn.Width(mux::GridLength{ui->compact ? 56.0 : 248.0, mux::GridUnitType::Pixel});
    ui->navHost.Width(rail ? 56.0 : 248.0);
    // Opened over the page, the sidebar spans both columns: a child wider than its cell is clipped to it.
    muxc::Grid::SetColumnSpan(ui->navHost, ui->compact && ui->expanded ? 2 : 1);
    ui->navHost.Background(ui->compact && ui->expanded ? ui->overlayBrush : mux::Media::Brush{nullptr});
    ui->navToggle.Visibility(ui->compact ? mux::Visibility::Visible : mux::Visibility::Collapsed);
    for (auto const& label : ui->navLabels) {
        label.Visibility(rail ? mux::Visibility::Collapsed : mux::Visibility::Visible);
    }
    // A rail item holds only its icon, centred in the item with no side padding, so nothing is cut.
    ui->nav.Margin(rail ? mux::Thickness{4, 8, 4, 8} : mux::Thickness{8, 8, 8, 8});
    muxc::ScrollViewer::SetHorizontalScrollBarVisibility(ui->nav, muxc::ScrollBarVisibility::Disabled);
    auto items = ui->nav.Items();
    for (uint32_t i = 0; i < items.Size() && i < ui->navNames.size(); i++) {
        if (auto item = items.GetAt(i).try_as<muxc::ListViewItem>()) {
            item.MinWidth(0);  // A list item is 88 wide at least, which is wider than the rail.
            item.Padding(rail ? mux::Thickness{0, 0, 0, 0} : mux::Thickness{12, 0, 12, 0});
            item.HorizontalContentAlignment(rail ? mux::HorizontalAlignment::Center : mux::HorizontalAlignment::Left);
            muxc::ToolTipService::SetToolTip(item, rail ? winrt::box_value(winrt::hstring{ui->navNames[i]})
                                                        : Inspectable{nullptr});
        }
    }
    ui->content.Padding(ui->compact ? mux::Thickness{16, 16, 16, 24} : mux::Thickness{32, 24, 32, 32});
}


// The icon of the window and the taskbar: the ExploitProtection glyph of Segoe Fluent
// Icons, drawn into a bitmap in the colour the caption text has, with its shape as
// the alpha. Made again when the theme changes.
HICON g_settingsIconSmall = nullptr;
HICON g_settingsIconBig = nullptr;

HICON IconFromGlyph(wchar_t glyph, int size, COLORREF colour) {
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;  // Top-down.
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    void* pixels = nullptr;
    HBITMAP colourBitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    ReleaseDC(nullptr, screen);
    HICON icon = nullptr;
    if (dc && colourBitmap && pixels) {
        HGDIOBJ oldBitmap = SelectObject(dc, colourBitmap);
        memset(pixels, 0, (size_t)size * size * 4);
        HFONT font = CreateFontW(-(size * 9 / 10), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH,
                                 L"Segoe Fluent Icons");
        HGDIOBJ oldFont = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        RECT box = {0, 0, size, size};
        wchar_t text[2] = {glyph, 0};
        DrawTextW(dc, text, 1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
        SelectObject(dc, oldFont);
        DeleteObject(font);
        // White on black: the brightness is the shape.
        uint8_t* data = (uint8_t*)pixels;
        for (int i = 0; i < size * size; i++) {
            uint8_t* px = data + i * 4;
            unsigned alpha = std::max(px[0], std::max(px[1], px[2]));
            px[0] = (uint8_t)(GetBValue(colour) * alpha / 255);
            px[1] = (uint8_t)(GetGValue(colour) * alpha / 255);
            px[2] = (uint8_t)(GetRValue(colour) * alpha / 255);
            px[3] = (uint8_t)alpha;
        }
        SelectObject(dc, oldBitmap);
        HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
        ICONINFO made = {TRUE, 0, 0, mask, colourBitmap};
        icon = CreateIconIndirect(&made);
        DeleteObject(mask);
    }
    if (colourBitmap) {
        DeleteObject(colourBitmap);
    }
    if (dc) {
        DeleteDC(dc);
    }
    return icon;
}

void UpdateSettingsWindowIcon(HWND window, bool dark) {
    UINT dpi = GetDpiForWindow(window);
    int small = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
    int big = GetSystemMetricsForDpi(SM_CXICON, dpi);
    COLORREF colour = dark ? RGB(235, 235, 235) : RGB(32, 32, 32);
    HICON newSmall = IconFromGlyph(L'', small, colour);
    HICON newBig = IconFromGlyph(L'', big, colour);
    if (newSmall) {
        SendMessageW(window, WM_SETICON, ICON_SMALL, (LPARAM)newSmall);
    }
    if (newBig) {
        SendMessageW(window, WM_SETICON, ICON_BIG, (LPARAM)newBig);
    }
    if (g_settingsIconSmall) {
        DestroyIcon(g_settingsIconSmall);
    }
    if (g_settingsIconBig) {
        DestroyIcon(g_settingsIconBig);
    }
    g_settingsIconSmall = newSmall;
    g_settingsIconBig = newBig;
}

// The window's backdrop and caption follow the theme of its content.
void ApplySettingsWindowTheme(HWND window, SettingsUi* ui) {
    HMODULE dwm = LoadLibraryExW(L"dwmapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
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
        UpdateSettingsWindowIcon(window, dark != FALSE);
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
        ui->navHost = nullptr;
        ui->navToggle = nullptr;
        ui->sidebarColumn = nullptr;
        ui->overlayBrush = nullptr;
        ui->navLabels.clear();
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
            limits->ptMinTrackSize.x = MulDiv(440, dpi, 96);
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
            // At unload the objects are released while the window still exists, as the pane's are.
            if (g_settingsUnload) {
                RevokeAllHandlers();
            }
            DestroySettingsUi(!g_settingsUnload);
            DestroyWindow(window);
            if (g_settingsUnload) {
                // XAML frees the window's objects on later turns, and their delegates are the mod's code.
                try {
                    DrainUi(winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread());
                } catch (...) {
                }
            }
            return 0;
        case WM_DESTROY:
            if (g_settingsWindow == window) {
                g_settingsWindow = nullptr;
            }
            if (g_settingsIconSmall) {
                DestroyIcon(g_settingsIconSmall);
                g_settingsIconSmall = nullptr;
            }
            if (g_settingsIconBig) {
                DestroyIcon(g_settingsIconBig);
                g_settingsIconBig = nullptr;
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
                if (g_settingsUi->navCount != SettingsPageCount()) {
                    FillNav(g_settingsUi);
                }
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
            cls.hInstance = ModHandle();
            cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            cls.lpszClassName = kSettingsClass;
            if (!RegisterClassExW(&cls)) {
                Wh_Log(L"settings window: the class could not be registered, gle=%lu", GetLastError());
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
                page = 0;  // It always opens on the first section.
                (void)sp;
            }
        }
        HWND window = CreateWindowExW(0, kSettingsClass, Str(L"window.title"), WS_OVERLAPPEDWINDOW, x, y, width, height,
                                      nullptr, nullptr, ModHandle(), nullptr);
        if (!window) {
            Wh_Log(L"settings window: could not be created, gle=%lu", GetLastError());
            return;
        }
        g_settingsWindow = window;
        g_settingsThread = GetCurrentThreadId();
        {
            // No icon and no title drawn in the title bar (the window keeps its name for the taskbar).
            if (HMODULE theme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
                struct Options {
                    DWORD flags;
                    DWORD mask;
                } options = {3, 3};  // WTNCA_NODRAWCAPTION | WTNCA_NODRAWICON
                auto setAttribute = (HRESULT(WINAPI*)(HWND, int, void*, DWORD))GetProcAddress(theme, "SetWindowThemeAttribute");
                if (setAttribute) {
                    setAttribute(window, 1 /* WTA_NONCLIENT */, &options, sizeof(options));
                }
                FreeLibrary(theme);
            }
        }
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
        ui->sidebarColumn = sidebarColumn;

        muxc::ListView nav;
        nav.Margin(mux::Thickness{8, 8, 8, 8});
        nav.SelectionMode(muxc::ListViewSelectionMode::Single);
        ui->nav = nav;

        // The sidebar sits in a host that can be wider than its column: on a narrow window it
        // is a rail of icons, and the button at its top opens it over the page.
        auto host = LoadXaml(std::wstring(L"<Grid ") + kXamlNs +
                             L" HorizontalAlignment='Left' Background='{ThemeResource SolidBackgroundFillColorSecondaryBrush}'>"
                             L"<Grid.RowDefinitions><RowDefinition Height='Auto'/><RowDefinition Height='*'/></Grid.RowDefinitions>"
                             L"</Grid>")
                        .as<muxc::Grid>();
        ui->overlayBrush = host.Background();
        host.Background(nullptr);
        muxc::Button toggle;
        toggle.Margin(mux::Thickness{8, 12, 8, 0});
        toggle.Width(40);
        toggle.Height(36);
        toggle.Padding(mux::Thickness{0, 0, 0, 0});
        toggle.Background(mux::Media::SolidColorBrush(winrt::Windows::UI::Color{0, 0, 0, 0}));
        toggle.BorderThickness(mux::Thickness{0, 0, 0, 0});
        muxc::FontIcon bars;
        bars.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
        bars.Glyph(L"\uE700");
        bars.FontSize(16);
        toggle.Content(bars);
        toggle.Visibility(mux::Visibility::Collapsed);
        muxc::ToolTipService::SetToolTip(toggle, winrt::box_value(winrt::hstring{L"Menu"}));
        muxc::Grid::SetRow(toggle, 0);
        muxc::Grid::SetRow(nav, 1);
        host.Children().Append(toggle);
        host.Children().Append(nav);
        muxc::Grid::SetColumn(host, 0);
        muxc::Canvas::SetZIndex(host, 10);
        root.Children().Append(host);
        ui->navHost = host;
        ui->navToggle = toggle;
        Keep(toggle, toggle.Click(winrt::auto_revoke, [](Inspectable const&, mux::RoutedEventArgs const&) {
            SettingsUi* now = g_settingsUi;
            if (now) {
                now->expanded = !now->expanded;
                ApplyNavMode(now);
            }
        }));

        muxc::ScrollViewer content;
        content.VerticalScrollBarVisibility(muxc::ScrollBarVisibility::Auto);
        content.Padding(mux::Thickness{32, 24, 32, 32});
        muxc::Grid::SetColumn(content, 1);
        root.Children().Append(content);
        ui->content = content;
        // A press on the page closes the sidebar that was opened over it.
        AddRouted(content, mux::UIElement::PointerPressedEvent(),
                           winrt::box_value(mux::Input::PointerEventHandler(
                               [](Inspectable const&, mux::Input::PointerRoutedEventArgs const&) {
                                   SettingsUi* now = g_settingsUi;
                                   if (now && now->compact && now->expanded) {
                                       now->expanded = false;
                                       ApplyNavMode(now);
                                   }
                               })));
        // The width decides: below 760 px the sidebar is a rail.
        Keep(root, root.SizeChanged(winrt::auto_revoke, [](Inspectable const&, mux::SizeChangedEventArgs const& sizeArgs) {
            SettingsUi* now = g_settingsUi;
            if (!now) {
                return;
            }
            bool narrow = sizeArgs.NewSize().Width < 760;
            if (narrow != now->compact) {
                now->compact = narrow;
                now->expanded = false;
                ApplyNavMode(now);
            }
        }));

        Keep(nav, nav.SelectionChanged(winrt::auto_revoke, [](Inspectable const&, muxc::SelectionChangedEventArgs const&) {
            SettingsUi* now = g_settingsUi;
            if (now && now->nav) {
                int selected = now->nav.SelectedIndex();
                if (selected >= 0 && selected != now->page) {
                    ShowSettingsPage(now, selected);
                }
                if (now->compact && now->expanded) {
                    now->expanded = false;
                    ApplyNavMode(now);
                }
            }
        }));
        Keep(root, root.ActualThemeChanged(winrt::auto_revoke, [window](mux::FrameworkElement const&, Inspectable const&) {
            ApplySettingsWindowTheme(window, g_settingsUi);
            RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        }));
        ui->source.Content(root);

        ui->page = page;
        FillNav(ui);
        ShowSettingsPage(ui, ui->page);
        ApplySettingsWindowTheme(window, ui);
        RECT client;
        GetClientRect(window, &client);
        ui->bridge.MoveAndResize(winrt::Windows::Graphics::RectInt32{0, 0, client.right, client.bottom});
        ui->bridge.Show();
        ShowWindow(window, SW_SHOW);
        SetForegroundWindow(window);
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        Wh_Log(L"settings window opened, window=%p, dpi=%u, client %ldx%ld", window, GetDpiForWindow(window),
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
        Wh_Log(L"the settings window goes with its Explorer window");
        g_settingsAbandon = true;
        DestroyWindow(g_settingsWindow);
        g_settingsAbandon = false;
    }
}

// The mod is unloading: ask the window to close, on its own thread, and let the class go.
void CloseSettingsWindowAtUnload() {
    g_settingsAbandon = true;
    g_settingsUnload = true;
    if (g_settingsWindow && IsWindow(g_settingsWindow)) {
        // Sent, not posted: the window is gone and its objects released when this returns.
        SendMessageW(g_settingsWindow, WM_CLOSE, 0, 0);
    }
    if (g_settingsClassMade) {
        if (UnregisterClassW(kSettingsClass, ModHandle())) {
            g_settingsClassMade = false;
        } else {
            Wh_Log(L"settings window: the class is still in use, gle=%lu", GetLastError());
        }
    }
}
