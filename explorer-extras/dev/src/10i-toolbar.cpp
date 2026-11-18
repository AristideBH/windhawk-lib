// The toolbar of File Explorer: the row with Back, Forward, Up and Refresh before the address bar. Explorer puts the four
// AppBarButtons (x:Name backButton, forwardButton, upButton, refreshButton) in the "NavigationCommands" CommandBar, each
// with the margin 8,8,8,0, so two of them are 16 apart. The gap is set through the margin of each button: between two
// buttons it is the gap, and at the two ends it is the gap too (never more than Explorer's own 8), so the address bar,
// which takes what is left, grows. The margins Explorer gave are
// kept and put back when the setting goes off and when the mod unloads.

std::atomic<bool> g_toolbar{true};  // Change the navigation buttons.
std::atomic<int> g_navGap{0};       // The gap between them, in pixels (DIP).
std::atomic<int> g_addressGap{4};  // The gap between the last navigation button and the address bar.
std::atomic<bool> g_searchResize{true};  // The edge of the search box can be dragged.
std::atomic<int> g_searchWidth{0};       // The search box's width in pixels, 0 for Explorer's own proportion.
std::atomic<int> g_addressRadius{4};  // The corner radius of the address bar (Explorer's own is 4).
std::atomic<int> g_searchRadius{4};   // The same for the search box.
std::atomic<int> g_cmdGap{8};       // The gap between the command bar's buttons (Explorer's own is 8 for the icons).

// The margin of button `index` of `count`, for a gap, from the margin Explorer gave it. Pure.
mux::Thickness ToolbarMarginFor(mux::Thickness original, size_t index, size_t count, double gap, double addressGap) {
    mux::Thickness margin = original;
    margin.Left = index == 0 ? std::min(original.Left, gap) : 0;
    margin.Right = index + 1 == count ? addressGap : gap;
    return margin;
}

// The margin of a command bar button for a gap between two neighbours: half on each side, never more than Explorer's own.
mux::Thickness ToolbarCommandMargin(mux::Thickness original, double gap) {
    mux::Thickness margin = original;
    margin.Left = std::min(original.Left, gap / 2);
    margin.Right = std::min(original.Right, gap / 2);
    return margin;
}

// The width the search box gets for a wanted one: at least 160, at most what leaves 240 for the address bar. Pure.
double ToolbarSearchWidth(double wanted, double gridWidth, double leftWidth) {
    double most = std::max(160.0, gridWidth - leftWidth - 240);
    return std::min(std::max(wanted, 160.0), most);
}

// The corner radius for a box: the setting, between 0 and 16, or Explorer's own when the area is off. Pure.
double ToolbarRadius(double original, double wanted, bool on) {
    return on ? std::min(16.0, std::max(0.0, wanted)) : original;
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

#ifdef EXTRAS_VERBOSE
// The whole top of the island, for the log: every element down to a depth, with class, name, margin, size and padding.
void ToolbarTreeDump(mux::DependencyObject const& node, int depth, int& budget) {
    if (depth > 16 || budget <= 0) {
        return;
    }
    if (auto element = node.try_as<mux::FrameworkElement>()) {
        budget--;
        auto margin = element.Margin();
        wchar_t extra[200] = L"";
        if (auto control = element.try_as<muxc::Control>()) {
            auto padding = control.Padding();
            swprintf_s(extra, L" padding=%.1f,%.1f,%.1f,%.1f", padding.Left, padding.Top, padding.Right, padding.Bottom);
        } else if (auto stack = element.try_as<muxc::StackPanel>()) {
            swprintf_s(extra, L" spacing=%.1f", stack.Spacing());
        }
        wchar_t look[120] = L"";
        if (auto border = element.try_as<muxc::Border>()) {
            swprintf_s(look, L" radius=%.1f bg=%d borderBrush=%d", border.CornerRadius().TopLeft, border.Background() ? 1 : 0,
                       border.BorderBrush() ? 1 : 0);
        } else if (auto grid = element.try_as<muxc::Grid>()) {
            swprintf_s(look, L" radius=%.1f bg=%d", grid.CornerRadius().TopLeft, grid.Background() ? 1 : 0);
        } else if (auto control = element.try_as<muxc::Control>()) {
            swprintf_s(look, L" radius=%.1f bg=%d borderBrush=%d", control.CornerRadius().TopLeft, control.Background() ? 1 : 0,
                       control.BorderBrush() ? 1 : 0);
        }
        LogVerbose(L"TREE %*s%s '%s' m=%.1f,%.1f,%.1f,%.1f size=%.1fx%.1f h=%.1f minh=%.1f%s%s", depth * 2, L"",
                   ToolbarClassOf(element).c_str(), std::wstring(element.Name()).c_str(), margin.Left, margin.Top, margin.Right,
                   margin.Bottom, element.ActualWidth(), element.ActualHeight(), element.Height(), element.MinHeight(), extra, look);
    }
    int count = mux::Media::VisualTreeHelper::GetChildrenCount(node);
    for (int i = 0; i < count; i++) {
        ToolbarTreeDump(mux::Media::VisualTreeHelper::GetChild(node, i), depth + 1, budget);
    }
}
#endif

const wchar_t* const kToolbarIds[] = {L"backButton", L"forwardButton", L"upButton", L"refreshButton"};

struct ToolbarCommandButton {
    winrt::weak_ref<mux::FrameworkElement> button;
    void* key = nullptr;
    mux::Thickness original = {};  // The margin Explorer gave.
    mux::Thickness applied = {};   // The one this part set last.
};

struct ToolbarButtons {
    winrt::weak_ref<muxc::Panel> commandStack;  // The StackPanel of the command bar's buttons.
    std::vector<ToolbarCommandButton> command;
    winrt::weak_ref<mux::FrameworkElement> button[4];
    mux::Thickness original[4] = {};
    winrt::weak_ref<muxc::Grid> contentRoot;  // The grid of the command bar: its second column is a spare of 6 DIP.
    mux::GridLength spare = {0, mux::GridUnitType::Auto};
    double spareMin = 0;  // The column also has a minimum width of its own.
    winrt::weak_ref<muxc::Grid> navGrid;  // NavigationBarControlGrid: [buttons][address bar][search box].
    winrt::weak_ref<muxc::Border> searchHandle;
    mux::GridLength searchOriginal = {3, mux::GridUnitType::Star};
    winrt::weak_ref<muxc::Control> address, search;  // The two boxes (AutoSuggestBox) and the corner radius Explorer gave.
    mux::CornerRadius addressOriginal = {}, searchOriginal2 = {};
    winrt::weak_ref<muxc::Control> addressText;      // The address bar's own TextBox and its border draw the outline.
    winrt::weak_ref<muxc::Border> addressBorder;
    mux::CornerRadius addressTextOriginal = {}, addressBorderOriginal = {};
    // The grids around the box: PART_LayoutRoot paints the background that shows the outline (a radius of 4 of its own).
    winrt::weak_ref<muxc::Grid> addressGrid[2];
    mux::CornerRadius addressGridOriginal[2] = {};
    bool known = false;
};
[[clang::no_destroy]] thread_local ToolbarButtons t_toolbar;  // Weak: the buttons belong to the window.

// The command bar's buttons: margins from the gap, each one's own original kept. Runs again on every layout of the
// stack, because Explorer builds and shows buttons as the selection changes.
void ToolbarApplyCommandBar() {
    auto stack = t_toolbar.commandStack.get();
    if (!stack || !t_toolbar.known) {
        LogVerbose(L"TOOLBAR command bar not applied: stack=%d known=%d", stack ? 1 : 0, t_toolbar.known ? 1 : 0);
        return;
    }
    int changed = 0, seen = 0;
    try {
        for (auto const& child : stack.Children()) {
            auto element = child.try_as<muxc::AppBarButton>();
            if (!element) {
                continue;
            }
            mux::FrameworkElement frame = element;
            void* key = winrt::get_abi(frame);
            ToolbarCommandButton* entry = nullptr;
            for (auto& known : t_toolbar.command) {
                if (known.key == key && winrt::get_abi(known.button.get()) == key) {
                    entry = &known;
                    break;
                }
            }
            auto now = element.Margin();
            if (!entry) {
                ToolbarCommandButton added;
                added.button = winrt::make_weak(frame);
                added.key = key;
                added.original = now;
                added.applied = now;
                t_toolbar.command.push_back(added);
                entry = &t_toolbar.command.back();
            } else if (now.Left != entry->applied.Left || now.Right != entry->applied.Right) {
                entry->original = now;  // Explorer set it again: this is the new margin to work from.
            }
            auto wanted = g_toolbar ? ToolbarCommandMargin(entry->original, (double)g_cmdGap) : entry->original;
            seen++;
            if (wanted.Left != now.Left || wanted.Right != now.Right) {
                element.Margin(wanted);
                changed++;
            }
            entry->applied = wanted;
        }
    } catch (...) {
        LogVerbose(L"TOOLBAR command bar apply threw");
    }
    LogVerbose(L"TOOLBAR command bar: gap %d, %d of %d buttons changed", g_cmdGap.load(), changed, seen);
}

// The wanted width of the search box: the dragged one (kept while the setting it was made against is unchanged), else the setting.
double ToolbarWantedSearch() {
    int stored = Wh_GetIntValue(L"ui.searchWidth", 0);
    if (stored > 0 && Wh_GetIntValue(L"ui.searchBase", -1) == g_searchWidth) {
        return stored;
    }
    return g_searchWidth;
}

// The search box's column and its handle, for the current settings.
void ToolbarApplySearch(double forced = 0) {
    auto grid = t_toolbar.navGrid.get();
    if (!grid || !t_toolbar.known) {
        return;
    }
    try {
        auto columns = grid.ColumnDefinitions();
        if (columns.Size() < 3) {
            return;
        }
        auto column = columns.GetAt(2);
        double want = forced > 0 ? forced : (g_searchResize ? ToolbarWantedSearch() : (double)g_searchWidth);
        if (!g_toolbar || want <= 0 || grid.ActualWidth() <= 0) {
            column.Width(t_toolbar.searchOriginal);
        } else {
            column.Width(mux::GridLength{ToolbarSearchWidth(want, grid.ActualWidth(), columns.GetAt(0).ActualWidth()),
                                         mux::GridUnitType::Pixel});
        }
        if (auto handle = t_toolbar.searchHandle.get()) {
            handle.Visibility(g_toolbar && g_searchResize ? mux::Visibility::Visible : mux::Visibility::Collapsed);
        }
    } catch (...) {
    }
}

// A handle on the edge between the address bar and the search box: drag it (mouse, touch or pen) to resize, double click or
// double tap to go back to the setting.
void ToolbarMakeHandle(muxc::Grid const& grid) {
    muxc::Border handle;
    handle.Width(16);
    handle.Height(36);
    handle.HorizontalAlignment(mux::HorizontalAlignment::Left);
    handle.VerticalAlignment(mux::VerticalAlignment::Center);
    handle.Margin(mux::Thickness{-8, 0, 0, 0});
    handle.Background(Brush(0, 0, 0, 0));
    muxc::Grid::SetColumn(handle, 2);
    try {
        handle.as<mux::IUIElementProtected>().ProtectedCursor(winrt::Microsoft::UI::Input::InputSystemCursor::Create(
            winrt::Microsoft::UI::Input::InputSystemCursorShape::SizeWestEast));
    } catch (...) {
        LogHresult(L"setting the resize cursor of the search box");
    }
    auto drag = std::make_shared<std::pair<bool, double>>(false, 0.0);  // dragging, offset of the pointer from the edge
    auto weakGrid = winrt::make_weak(grid);
    auto edgeOf = [weakGrid](double pointerX) {
        auto g = weakGrid.get();
        return g ? g.ActualWidth() - g.ColumnDefinitions().GetAt(2).ActualWidth() : 0.0;
    };
    Keep(handle, handle.PointerPressed(winrt::auto_revoke, [drag, weakGrid, edgeOf](Inspectable const& sender, mux::Input::PointerRoutedEventArgs const& args) {
        if (auto g = weakGrid.get()) {
            drag->first = true;
            drag->second = args.GetCurrentPoint(g).Position().X - edgeOf(0);
            sender.as<mux::UIElement>().CapturePointer(args.Pointer());
            args.Handled(true);
        }
    }));
    Keep(handle, handle.PointerMoved(winrt::auto_revoke, [drag, weakGrid](Inspectable const&, mux::Input::PointerRoutedEventArgs const& args) {
        if (drag->first) {
            if (auto g = weakGrid.get()) {
                double edge = args.GetCurrentPoint(g).Position().X - drag->second;
                ToolbarApplySearch(g.ActualWidth() - edge);
            }
        }
    }));
    auto finish = [drag, weakGrid](bool keep) {
        if (!drag->first) {
            return;
        }
        drag->first = false;
        if (auto g = weakGrid.get(); g && keep) {
            double width = g.ColumnDefinitions().GetAt(2).Width().Value;
            if (g.ColumnDefinitions().GetAt(2).Width().GridUnitType == mux::GridUnitType::Pixel && width > 0) {
                Wh_SetIntValue(L"ui.searchWidth", (int)std::lround(width));
                Wh_SetIntValue(L"ui.searchBase", g_searchWidth);
            }
        }
    };
    Keep(handle, handle.PointerReleased(winrt::auto_revoke, [finish](Inspectable const& sender, mux::Input::PointerRoutedEventArgs const& args) {
        sender.as<mux::UIElement>().ReleasePointerCapture(args.Pointer());
        finish(true);
    }));
    Keep(handle, handle.PointerCanceled(winrt::auto_revoke, [finish](Inspectable const&, mux::Input::PointerRoutedEventArgs const&) {
        finish(true);
    }));
    Keep(handle, handle.DoubleTapped(winrt::auto_revoke, [](Inspectable const&, mux::Input::DoubleTappedRoutedEventArgs const& args) {
        Wh_SetIntValue(L"ui.searchWidth", 0);
        ToolbarApplySearch();
        args.Handled(true);
    }));
    // A narrower window keeps the address bar's room.
    Keep(grid, grid.SizeChanged(winrt::auto_revoke, [](Inspectable const&, mux::SizeChangedEventArgs const&) {
        if (!g_unloading) {
            ToolbarApplySearch();
        }
    }));
    grid.Children().Append(handle);
    t_toolbar.searchHandle = winrt::make_weak(handle);
}

void ToolbarApplyRadius() {
    if (!t_toolbar.known) {
        return;
    }
    try {
        auto radiusFor = [](mux::CornerRadius original, int wanted) {
            // The setting when the area is on; Explorer's own corners, each as they were, when it is off.
            double r = ToolbarRadius(original.TopLeft, wanted, g_toolbar);
            return g_toolbar ? mux::CornerRadius{r, r, r, r} : original;
        };
        if (auto box = t_toolbar.address.get()) {
            box.CornerRadius(radiusFor(t_toolbar.addressOriginal, g_addressRadius));
        }
        {
            if (auto text = t_toolbar.addressText.get()) {
                text.CornerRadius(radiusFor(t_toolbar.addressTextOriginal, g_addressRadius));
            }
            if (auto border = t_toolbar.addressBorder.get()) {
                border.CornerRadius(radiusFor(t_toolbar.addressBorderOriginal, g_addressRadius));
            }
            for (int i = 0; i < 2; i++) {
                if (auto grid = t_toolbar.addressGrid[i].get()) {
                    grid.CornerRadius(radiusFor(t_toolbar.addressGridOriginal[i], g_addressRadius));
                }
            }
        }
        if (auto box = t_toolbar.search.get()) {
            box.CornerRadius(radiusFor(t_toolbar.searchOriginal2, g_searchRadius));
        }
    } catch (...) {
    }
}

// Puts the margins for the current setting (or Explorer's own when the area is off).
void ToolbarApplyNow() {
    ToolbarApplyRadius();
    ToolbarApplySearch();
    if (t_toolbar.known) {
        ToolbarApplyCommandBar();
    }
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
                element.Margin(g_toolbar ? ToolbarMarginFor(t_toolbar.original[i], i, 4, (double)g_navGap, (double)g_addressGap) : t_toolbar.original[i]);
            } catch (...) {
            }
        }
    }
}

}  // namespace

// Settings changed: every window's buttons take the new gap (or go back to Explorer's), each on its own thread.
void ToolbarSettingsChanged() {
    // Every thread that holds toolbar state (registered when its buttons were found), also when no window has a WindowState
    // any more: Columns and the details pane off drops the states.
    std::vector<mud::DispatcherQueue> queues;
    {
        std::lock_guard<std::mutex> lock(g_keepThreadsMutex);
        for (auto& thread : g_keepThreads) {
            queues.push_back(thread.queue);
        }
    }
    for (auto& queue : queues) {
        try {
            queue.TryEnqueue([]() {
                if (!g_unloading) {
                    ToolbarApplyNow();
                }
            });
        } catch (...) {
        }
    }
    LogVerbose(L"TOOLBAR settings changed: on=%d navGap=%d commandGap=%d, %d threads", g_toolbar.load() ? 1 : 0, g_navGap.load(),
               g_cmdGap.load(), (int)queues.size());
}

// At unload, on each thread that holds buttons: Explorer's margins are put back.
void ToolbarRestoreThread() {
    try {
        if (t_toolbar.known) {
            if (auto box = t_toolbar.address.get()) {
                box.CornerRadius(t_toolbar.addressOriginal);
            }
            if (auto text = t_toolbar.addressText.get()) {
                text.CornerRadius(t_toolbar.addressTextOriginal);
            }
            if (auto border = t_toolbar.addressBorder.get()) {
                border.CornerRadius(t_toolbar.addressBorderOriginal);
            }
            for (int i = 0; i < 2; i++) {
                if (auto grid = t_toolbar.addressGrid[i].get()) {
                    grid.CornerRadius(t_toolbar.addressGridOriginal[i]);
                }
            }
            if (auto box = t_toolbar.search.get()) {
                box.CornerRadius(t_toolbar.searchOriginal2);
            }
        }
    } catch (...) {
    }
    try {
        if (auto grid = t_toolbar.navGrid.get(); grid && t_toolbar.known) {
            if (grid.ColumnDefinitions().Size() >= 3) {
                grid.ColumnDefinitions().GetAt(2).Width(t_toolbar.searchOriginal);
            }
            if (auto handle = t_toolbar.searchHandle.get()) {
                uint32_t at = 0;
                if (grid.Children().IndexOf(handle, at)) {
                    grid.Children().RemoveAt(at);
                }
            }
        }
    } catch (...) {
    }
    try {
        for (auto& entry : t_toolbar.command) {
            if (auto element = entry.button.get()) {
                element.Margin(entry.original);
            }
        }
    } catch (...) {
    }
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
    // The command bar's buttons.
    auto commandBar = ToolbarFindById(root, L"FileExplorerCommandBar", 0);
    if (!commandBar) {
        LogVerbose(L"TOOLBAR the command bar is not there yet");
        return false;
    }
    auto items = ToolbarFindById(commandBar, L"PrimaryItemsControl", 0);
    muxc::Panel stack{nullptr};
    if (items) {
        std::function<muxc::Panel(mux::DependencyObject const&, int)> firstPanel = [&](mux::DependencyObject const& node, int depth) -> muxc::Panel {
            if (depth > 6) {
                return nullptr;
            }
            if (auto panel = node.try_as<muxc::StackPanel>()) {
                return panel;
            }
            int count = mux::Media::VisualTreeHelper::GetChildrenCount(node);
            for (int i = 0; i < count; i++) {
                if (auto found = firstPanel(mux::Media::VisualTreeHelper::GetChild(node, i), depth + 1)) {
                    return found;
                }
            }
            return nullptr;
        };
        stack = firstPanel(items, 0);
    }
    if (!stack) {
        LogVerbose(L"TOOLBAR the command bar's stack is not there yet");
        return false;
    }
    found.commandStack = winrt::make_weak(stack);
    Keep(stack, stack.LayoutUpdated(winrt::auto_revoke, [](Inspectable const&, Inspectable const&) {
             if (!g_unloading) {
                 ToolbarApplyCommandBar();
             }
         }));
    for (mux::DependencyObject at = buttons[0]; at; at = mux::Media::VisualTreeHelper::GetParent(at)) {
        if (auto grid = at.try_as<muxc::Grid>(); grid && grid.Name() == L"NavigationBarControlGrid" && grid.ColumnDefinitions().Size() >= 3) {
            found.navGrid = winrt::make_weak(grid);
            found.searchOriginal = grid.ColumnDefinitions().GetAt(2).Width();
            break;
        }
    }
    if (auto box = ToolbarFindById(root, L"PART_AutoSuggestBox", 0).try_as<muxc::Control>()) {
        found.address = winrt::make_weak(box);
        found.addressOriginal = box.CornerRadius();
        if (auto text = ToolbarFindById(box, L"TextBox", 0).try_as<muxc::Control>()) {
            found.addressText = winrt::make_weak(text);
            found.addressTextOriginal = text.CornerRadius();
        }
        if (auto border = ToolbarFindById(box, L"BorderElement", 0).try_as<muxc::Border>()) {
            found.addressBorder = winrt::make_weak(border);
            found.addressBorderOriginal = border.CornerRadius();
        }
        // PART_LayoutRoot (the parent of the box) and NormalModeGrid (the breadcrumbs, its sibling).
        mux::DependencyObject parent = mux::Media::VisualTreeHelper::GetParent(box);
        if (auto layoutRoot = parent.try_as<muxc::Grid>()) {
            found.addressGrid[0] = winrt::make_weak(layoutRoot);
            found.addressGridOriginal[0] = layoutRoot.CornerRadius();
        }
        if (auto normal = ToolbarFindById(parent, L"NormalModeGrid", 0).try_as<muxc::Grid>()) {
            found.addressGrid[1] = winrt::make_weak(normal);
            found.addressGridOriginal[1] = normal.CornerRadius();
        }
        LogVerbose(L"TOOLBAR address radius: box %.1f text %.1f border %.1f", found.addressOriginal.TopLeft,
                   found.addressTextOriginal.TopLeft, found.addressBorderOriginal.TopLeft);
    }
    if (auto box = ToolbarFindById(root, L"FileExplorerSearchBox", 0).try_as<muxc::Control>()) {
        found.search = winrt::make_weak(box);
        found.searchOriginal2 = box.CornerRadius();
    }
    found.known = true;
    t_toolbar = found;
    if (auto grid = t_toolbar.navGrid.get()) {
        ToolbarMakeHandle(grid);
    }
    ToolbarApplyNow();
    LogVerbose(L"TOOLBAR gap %d applied to the four buttons, window=%p, contentRoot found=%d", g_navGap.load(),
               (void*)state->topLevel, t_toolbar.contentRoot.get() ? 1 : 0);
#ifdef EXTRAS_VERBOSE
    {
        static bool dumped = false;
        if (!dumped) {
            dumped = true;
            int budget = 700;
            ToolbarTreeDump(root, 0, budget);
        }
    }
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
