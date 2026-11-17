// The toolbar of File Explorer: the row with Back, Forward, Up and Refresh before the address bar. Explorer puts the four
// AppBarButtons (x:Name backButton, forwardButton, upButton, refreshButton) in the "NavigationCommands" CommandBar, each
// with the margin 8,8,8,0, so two of them are 16 apart. The gap is set through the margin of each button: between two
// buttons it is the gap, and at the two ends it is the gap too (never more than Explorer's own 8), so the address bar,
// which takes what is left, grows. The margins Explorer gave are
// kept and put back when the setting goes off and when the mod unloads.

std::atomic<bool> g_toolbar{true};  // Change the navigation buttons.
std::atomic<int> g_navGap{0};       // The gap between them, in pixels (DIP).

// The margin of button `index` of `count`, for a gap, from the margin Explorer gave it. Pure.
mux::Thickness ToolbarMarginFor(mux::Thickness original, size_t index, size_t count, double gap) {
    mux::Thickness margin = original;
    margin.Left = index == 0 ? std::min(original.Left, gap) : 0;
    margin.Right = index + 1 == count ? std::min(original.Right, gap) : gap;
    return margin;
}

namespace {

std::wstring ToolbarClassOf(Inspectable const& object) {
    try {
        return std::wstring(winrt::get_class_name(object));
    } catch (...) {
        return L"?";
    }
}

mux::FrameworkElement ToolbarFindById(mux::DependencyObject const& node, const wchar_t* id, int depth) {
    if (depth > 30) {
        return nullptr;
    }
    if (auto element = node.try_as<mux::FrameworkElement>()) {
        // The automation id of these buttons is their x:Name.
        if (element.Name() == id) {
            return element;
        }
    }
    int count = mux::Media::VisualTreeHelper::GetChildrenCount(node);
    for (int i = 0; i < count; i++) {
        if (auto found = ToolbarFindById(mux::Media::VisualTreeHelper::GetChild(node, i), id, depth + 1)) {
            return found;
        }
    }
    return nullptr;
}

#ifdef EXTRAS_VERBOSE
// What a button sits in, level by level, for the log: class, name, margin, size, spacing, grid columns.
void ToolbarDump(const wchar_t* id, mux::FrameworkElement const& button) {
    mux::DependencyObject at = button;
    for (int level = 0; level < 9 && at; level++) {
        auto element = at.try_as<mux::FrameworkElement>();
        if (element) {
            auto margin = element.Margin();
            std::wstring extra;
            if (auto stack = element.try_as<muxc::StackPanel>()) {
                wchar_t text[120];
                swprintf_s(text, L" spacing=%.1f orientation=%d", stack.Spacing(), (int)stack.Orientation());
                extra += text;
            }
            if (auto grid = element.try_as<muxc::Grid>()) {
                wchar_t text[120];
                swprintf_s(text, L" columns=%u rows=%u colSpacing=%.1f", grid.ColumnDefinitions().Size(), grid.RowDefinitions().Size(),
                           grid.ColumnSpacing());
                extra += text;
                for (auto const& column : grid.ColumnDefinitions()) {
                    swprintf_s(text, L" [col w=%.1f unit=%d actual=%.1f]", column.Width().Value, (int)column.Width().GridUnitType,
                               column.ActualWidth());
                    extra += text;
                }
            }
            if (auto control = element.try_as<muxc::Control>()) {
                auto padding = control.Padding();
                wchar_t text[120];
                swprintf_s(text, L" padding=%.1f,%.1f,%.1f,%.1f", padding.Left, padding.Top, padding.Right, padding.Bottom);
                extra += text;
            }
            LogVerbose(L"TOOLBAR %s L%d %s name='%s' margin=%.1f,%.1f,%.1f,%.1f w=%.1f actual=%.1fx%.1f col=%d minw=%.1f halign=%d%s", id,
                       level, ToolbarClassOf(element).c_str(), std::wstring(element.Name()).c_str(), margin.Left, margin.Top,
                       margin.Right, margin.Bottom, element.Width(), element.ActualWidth(), element.ActualHeight(),
                       (int)muxc::Grid::GetColumn(element), element.MinWidth(), (int)element.HorizontalAlignment(), extra.c_str());
        }
        at = mux::Media::VisualTreeHelper::GetParent(at);
    }
}
#endif

const wchar_t* const kToolbarIds[] = {L"backButton", L"forwardButton", L"upButton", L"refreshButton"};

struct ToolbarButtons {
    winrt::weak_ref<mux::FrameworkElement> button[4];
    mux::Thickness original[4] = {};
    winrt::weak_ref<muxc::Grid> contentRoot;  // The grid of the command bar: its second column is a spare of 6 DIP.
    mux::GridLength spare = {0, mux::GridUnitType::Auto};
    double spareMin = 0;  // The column also has a minimum width of its own.
    bool known = false;
};
[[clang::no_destroy]] thread_local ToolbarButtons t_toolbar;  // Weak: the buttons belong to the window.

// Puts the margins for the current setting (or Explorer's own when the area is off).
void ToolbarApplyNow() {
    if (t_toolbar.known) {
        if (auto grid = t_toolbar.contentRoot.get()) {
            try {
                if (grid.ColumnDefinitions().Size() >= 2) {
                    // The spare column after the buttons (6 DIP, for the overflow button) is taken away with the setting on:
                    // what is left between Refresh and the address bar is the end margin.
                    auto column = grid.ColumnDefinitions().GetAt(1);
                    column.Width(g_toolbar ? mux::GridLength{0, mux::GridUnitType::Pixel} : t_toolbar.spare);
                    column.MinWidth(g_toolbar ? 0 : t_toolbar.spareMin);
                }
            } catch (...) {
            }
        }
    }
    for (int i = 0; i < 4 && t_toolbar.known; i++) {
        if (auto element = t_toolbar.button[i].get()) {
            try {
                element.Margin(g_toolbar ? ToolbarMarginFor(t_toolbar.original[i], i, 4, (double)g_navGap) : t_toolbar.original[i]);
            } catch (...) {
            }
        }
    }
}

}  // namespace

// Settings changed: every window's buttons take the new gap (or go back to Explorer's), each on its own thread.
void ToolbarSettingsChanged() {
    std::lock_guard<std::mutex> lock(g_statesMutex);
    for (auto& state : g_states) {
        if (state->queue) {
            state->queue.TryEnqueue([]() {
                if (!g_unloading) {
                    ToolbarApplyNow();
                }
            });
        }
    }
}

// At unload, on each thread that holds buttons: Explorer's margins are put back.
void ToolbarRestoreThread() {
    if (auto grid = t_toolbar.contentRoot.get()) {
        try {
            if (t_toolbar.known && grid.ColumnDefinitions().Size() >= 2) {
                grid.ColumnDefinitions().GetAt(1).Width(t_toolbar.spare);
                grid.ColumnDefinitions().GetAt(1).MinWidth(t_toolbar.spareMin);
            }
        } catch (...) {
        }
    }
    for (int i = 0; i < 4 && t_toolbar.known; i++) {
        if (auto element = t_toolbar.button[i].get()) {
            try {
                element.Margin(t_toolbar.original[i]);
            } catch (...) {
            }
        }
    }
    t_toolbar = ToolbarButtons();
}

// Called on every tick of a new window until it returns true (the buttons were found and handled).
bool ToolbarTick(WindowState* state) {
    auto content = state->xamlRoot ? state->xamlRoot.Content() : nullptr;
    auto root = content ? content.try_as<mux::DependencyObject>() : nullptr;
    if (!root) {
        return false;
    }
    std::vector<mux::FrameworkElement> buttons;
    for (auto id : kToolbarIds) {
        auto found = ToolbarFindById(root, id, 0);
        if (!found) {
            LogVerbose(L"TOOLBAR not found yet: %s", id);
            return false;
        }
        buttons.push_back(found);
    }
#ifdef EXTRAS_VERBOSE
    for (size_t i = 0; i < buttons.size(); i++) {
        ToolbarDump(kToolbarIds[i], buttons[i]);
    }
#endif
    RegisterKeepThread();  // This thread restores Explorer's margins at unload.
    ToolbarButtons found;
    for (int i = 0; i < 4; i++) {
        found.button[i] = winrt::make_weak(buttons[i]);
        found.original[i] = buttons[i].Margin();
    }
    // The grid named ContentRoot above the buttons.
    mux::DependencyObject up = buttons[3];
    for (int level = 0; level < 10 && up; level++) {
        if (auto grid = up.try_as<muxc::Grid>(); grid && grid.Name() == L"ContentRoot" && grid.ColumnDefinitions().Size() >= 2) {
            found.contentRoot = winrt::make_weak(grid);
            found.spare = grid.ColumnDefinitions().GetAt(1).Width();
            found.spareMin = grid.ColumnDefinitions().GetAt(1).MinWidth();
            break;
        }
        up = mux::Media::VisualTreeHelper::GetParent(up);
    }
    found.known = true;
    t_toolbar = found;
    ToolbarApplyNow();
    LogVerbose(L"TOOLBAR gap %d applied to the four buttons, window=%p, contentRoot found=%d", g_navGap.load(),
               (void*)state->topLevel, t_toolbar.contentRoot.get() ? 1 : 0);
#ifdef EXTRAS_VERBOSE
    // The sizes after the change, once the layout has run: the grids above the buttons.
    {
        mux::DependencyObject up = buttons[3];
        for (int level = 0; level < 10 && up; level++) {
            if (auto element = up.try_as<mux::FrameworkElement>()) {
                element.UpdateLayout();
                wchar_t extra[200] = L"";
                if (auto grid = up.try_as<muxc::Grid>()) {
                    int at = 0;
                    for (auto const& column : grid.ColumnDefinitions()) {
                        at += swprintf_s(extra + at, 200 - at, L" [col w=%.1f actual=%.1f]", column.Width().Value, column.ActualWidth());
                    }
                }
                LogVerbose(L"TOOLBAR after L%d %s name='%s' actual=%.1fx%.1f margin=%.1f,%.1f,%.1f,%.1f%s", level,
                           ToolbarClassOf(element).c_str(), std::wstring(element.Name()).c_str(), element.ActualWidth(),
                           element.ActualHeight(), element.Margin().Left, element.Margin().Top, element.Margin().Right,
                           element.Margin().Bottom, extra);
            }
            up = mux::Media::VisualTreeHelper::GetParent(up);
        }
    }
#endif
    return true;
}
