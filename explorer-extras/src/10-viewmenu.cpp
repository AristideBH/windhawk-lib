// ---- View menu entry ----

// The icon of Columns, drawn from assets/columns.svg (16 x 12, one filled
// shape), as a path icon that takes the colour of the toolbar's text.
muxc::IconElement MakeColumnsIcon() {
    return winrt::Microsoft::UI::Xaml::Markup::XamlReader::Load(
               L"<PathIcon xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' "
               L"HorizontalAlignment='Center' VerticalAlignment='Center' Foreground='{ThemeResource TextFillColorSecondaryBrush}' Data='F1 M13.0625 0C13.4531 0 13.8255 0.0807292 14.1797 0.242188C14.5391 0.398438 14.8516 0.611979 15.1172 0.882812C15.388 1.14844 15.6016 1.46094 15.7578 1.82031C15.9193 2.17448 16 2.54688 16 2.9375V9.0625C16 9.45312 15.9193 9.82812 15.7578 10.1875C15.6016 10.5417 15.388 10.8542 15.1172 11.125C14.8516 11.3906 14.5391 11.6042 14.1797 11.7656C13.8255 11.9219 13.4531 12 13.0625 12H2.9375C2.54688 12 2.17188 11.9219 1.8125 11.7656C1.45833 11.6042 1.14583 11.3906 0.875 11.125C0.609375 10.8542 0.395833 10.5417 0.234375 10.1875C0.078125 9.82812 0 9.45312 0 9.0625V2.9375C0 2.54688 0.078125 2.17448 0.234375 1.82031C0.395833 1.46094 0.609375 1.14844 0.875 0.882812C1.14583 0.611979 1.45833 0.398438 1.8125 0.242188C2.17188 0.0807292 2.54688 0 2.9375 0H13.0625ZM11 11H13.0391C13.2995 11 13.5469 10.9479 13.7812 10.8438C14.0208 10.7344 14.2292 10.5911 14.4062 10.4141C14.5885 10.2318 14.7318 10.0234 14.8359 9.78906C14.9453 9.54948 15 9.29948 15 9.03906V2.96094C15 2.70052 14.9453 2.45312 14.8359 2.21875C14.7318 1.97917 14.5885 1.77083 14.4062 1.59375C14.2292 1.41146 14.0208 1.26823 13.7812 1.16406C13.5469 1.05469 13.2995 1 13.0391 1H11V11ZM6 11H10V1H6V11ZM2.96094 1C2.70052 1 2.45052 1.05469 2.21094 1.16406C1.97656 1.26823 1.76823 1.41146 1.58594 1.59375C1.40885 1.77083 1.26562 1.97917 1.15625 2.21875C1.05208 2.45312 1 2.70052 1 2.96094V9.03906C1 9.29948 1.05208 9.54948 1.15625 9.78906C1.26562 10.0234 1.40885 10.2318 1.58594 10.4141C1.76823 10.5911 1.97656 10.7344 2.21094 10.8438C2.45052 10.9479 2.70052 11 2.96094 11H5V1H2.96094Z' />")
        .as<muxc::IconElement>();
}

void OpenSettingsWindow(WindowState* state);

// The last layout the user chose, kept by Windhawk across restarts.
bool LastLayoutWasColumns() {
    return Wh_GetIntValue(L"lastLayoutColumns", 0) != 0;
}

void RememberColumns(bool on) {
    Wh_SetIntValue(L"lastLayoutColumns", on ? 1 : 0);
}

void RemoveMenuEntry(WindowState* state) {
    try {
        for (auto& sibling : state->siblings) {
            if (sibling.item) {
                sibling.item.Click(sibling.token);
            }
        }
        state->siblings.clear();
        if (state->settingsItem) {
            state->settingsItem.Click(state->settingsClickToken);
            uint32_t at = 0;
            if (state->menuItems && state->menuItems.IndexOf(state->settingsItem, at)) {
                state->menuItems.RemoveAt(at);
            }
            state->settingsItem = nullptr;
        }
        if (state->menuItem) {
            state->menuItem.Click(state->menuClickToken);
            uint32_t index = 0;
            if (state->menuItems &&
                state->menuItems.IndexOf(state->menuItem, index)) {
                state->menuItems.RemoveAt(index);
            }
        }
    } catch (...) {
        LogHresult(L"RemoveMenuEntry");
    }
    state->menuItem = nullptr;
    state->menuItems = nullptr;
}

void AddMenuEntry(WindowState* state, ItemsVector const& items) {
    try {
        if (state->menuItem) {
            uint32_t index = 0;
            if (state->menuItems == items &&
                items.IndexOf(state->menuItem, index)) {
                return;
            }
            // The menu was rebuilt. Start again on the new one.
            RemoveMenuEntry(state);
        }

        winrt::hstring groupName;
        if (auto first = items.GetAt(0).try_as<muxc::RadioMenuFlyoutItem>()) {
            groupName = first.GroupName();
        }

        muxc::RadioMenuFlyoutItem item;
        item.Text(L"Columns");
        if (!groupName.empty()) {
            item.GroupName(groupName);
        }
        item.Icon(MakeColumnsIcon());
        item.Name(L"WindhawkColumnsItem");
        item.IsChecked(state->on);
        state->menuClickToken = item.Click(
            [](Inspectable const&, mux::RoutedEventArgs const&) {
                WindowState* state = StateForThisThread(false);
                if (state && !g_unloading) {
                    Out(L"Columns chosen in the View menu");
                    ChooseColumns(state);
                    if (state->on) {
                        RememberColumns(true);
                    }
                }
            });

        // Choosing any of Explorer's eight layouts turns columns off.
        for (uint32_t i = 0; i < 8 && i < items.Size(); i++) {
            auto sibling = items.GetAt(i).try_as<muxc::MenuFlyoutItem>();
            if (!sibling) {
                continue;
            }
            SiblingHook hook;
            hook.item = sibling;
            hook.token = sibling.Click(
                [](Inspectable const&, mux::RoutedEventArgs const&) {
                    WindowState* state = StateForThisThread(false);
                    if (state && state->on) {
                        LeaveColumns(state);
                    }
                    RememberColumns(false);
                });
            state->siblings.push_back(std::move(hook));
        }

        // Right after the eight layouts, before the separator.
        items.InsertAt(8, item);
        state->menuItem = item;
        state->menuItems = items;

        // The settings window, right under Columns.
        muxc::MenuFlyoutItem settings;
        settings.Text(L"Extras");
        muxc::FontIcon gear;
        gear.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
        gear.Glyph(L"\uE713");
        settings.Icon(gear);
        settings.Name(L"WindhawkSettingsItem");
        state->settingsClickToken = settings.Click([](Inspectable const&, mux::RoutedEventArgs const&) {
            WindowState* now = StateForThisThread(false);
            if (!now || g_unloading || !now->queue) {
                return;
            }
            // After the menu has closed and the click has been handled.
            try {
                now->queue.TryEnqueue([]() {
                    WindowState* later = StateForThisThread(false);
                    if (later && !g_unloading) {
                        OpenSettingsWindow(later);
                    }
                });
            } catch (...) {
            }
        });
        items.InsertAt(9, settings);
        state->settingsItem = settings;
        Out(L"Columns entry added to the View menu, window=%p",
            state->topLevel);
    } catch (...) {
        LogHresult(L"AddMenuEntry");
    }
}

std::wstring ClassOf(Inspectable const& obj) {
    try {
        return std::wstring{winrt::get_class_name(obj).c_str()};
    } catch (...) {
        return L"?";
    }
}

// The View button has no name. Its flyout is the only one that opens with
// eight radio items, the eight layouts. That holds in any language.
void WalkForViewFlyout(WindowState* state, mux::DependencyObject const& node,
                       int depth, int& visited) {
    if (!node || depth > 40 || visited > 3000) {
        return;
    }
    visited++;
    if (auto button = node.try_as<muxc::Button>()) {
        if (auto flyout = button.Flyout()) {
            if (auto menu = flyout.try_as<muxc::MenuFlyout>()) {
                auto items = menu.Items();
                uint32_t radios = 0;
                bool hasOurs = false;
                for (uint32_t i = 0; i < items.Size() && i < 9; i++) {
                    auto radio =
                        items.GetAt(i).try_as<muxc::RadioMenuFlyoutItem>();
                    if (!radio) {
                        break;
                    }
                    radios++;
                    if (radio.Name() == L"WindhawkColumnsItem") {
                        hasOurs = true;
                    }
                }
                if (items.Size() > 8 && (radios == 8 || (radios == 9 && hasOurs))) {
                    if (!hasOurs) {
                        AddMenuEntry(state, items);
                    }
                    if (state->viewButton != button) {
                        RestoreViewButtonIcon(state);
                        state->viewButton = button;
                    }
                    visited = 100000;  // Found. Stop the walk.
                    return;
                }
            }
        }
    }
    int children = mux::Media::VisualTreeHelper::GetChildrenCount(node);
    for (int i = 0; i < children; i++) {
        WalkForViewFlyout(state, mux::Media::VisualTreeHelper::GetChild(node, i),
                          depth + 1, visited);
    }
}

void OnCommandBarPointer(Inspectable const& sender) {
    if (g_unloading || !g_enabled) {
        return;
    }
    ULONGLONG now = GetTickCount64();
    if (now - g_lastWalk < 1000) {
        return;
    }
    g_lastWalk = now;
    try {
        WindowState* state = StateForThisThread(true);
        if (!state->topLevel || !IsWindow(state->topLevel)) {
            state->topLevel = FindTopLevel();
        }
        if (!state->queue) {
            state->queue = mud::DispatcherQueue::GetForCurrentThread();
        }
        if (!state->topLevel || !state->queue) {
            return;
        }
        auto node = sender.try_as<mux::DependencyObject>();
        if (!node) {
            return;
        }
        // Up to the command bar itself, so the walk stays small.
        mux::DependencyObject root = node;
        for (int i = 0; i < 12; i++) {
            if (ClassOf(root).find(L"CommandBarControl") != std::wstring::npos) {
                break;
            }
            auto parent = mux::Media::VisualTreeHelper::GetParent(root);
            if (!parent) {
                break;
            }
            root = parent;
        }
        int visited = 0;
        WalkForViewFlyout(state, root, 0, visited);
    } catch (...) {
        LogHresult(L"OnCommandBarPointer");
    }
}


// ---- The icon of the View button ----

// The icon the button shows now: the first icon element in its content.
mux::UIElement FindIconElement(mux::DependencyObject const& node, int depth, int& visited) {
    if (!node || depth > 12 || visited > 400) {
        return nullptr;
    }
    visited++;
    std::wstring name = ClassOf(node);
    if (name.find(L"FontIcon") != std::wstring::npos || name.find(L"PathIcon") != std::wstring::npos ||
        name.find(L"ImageIcon") != std::wstring::npos || name.find(L"BitmapIcon") != std::wstring::npos) {
        return node.try_as<mux::UIElement>();
    }
    int children = mux::Media::VisualTreeHelper::GetChildrenCount(node);
    for (int i = 0; i < children; i++) {
        if (auto found = FindIconElement(mux::Media::VisualTreeHelper::GetChild(node, i), depth + 1, visited)) {
            return found;
        }
    }
    return nullptr;
}

void LogTree(mux::DependencyObject const& node, int depth) {
    if (!node || depth > 8) {
        return;
    }
    Out(L"view button tree: %*s%s", depth * 2, L"", ClassOf(node).c_str());
    int children = mux::Media::VisualTreeHelper::GetChildrenCount(node);
    for (int i = 0; i < children; i++) {
        LogTree(mux::Media::VisualTreeHelper::GetChild(node, i), depth + 1);
    }
}

// Puts the original icon back.
void RestoreViewButtonIcon(WindowState* state) {
    if (!state->viewIconShown) {
        return;
    }
    state->viewIconShown = false;
    try {
        auto original = state->viewIconOriginal;
        auto ours = state->viewIconOurs;
        if (original && ours) {
            if (state->viewIconWhere == 4) {
                if (auto bar = state->viewButton.try_as<muxc::AppBarButton>()) {
                    bar.Icon(original.as<muxc::IconElement>());
                }
            } else if (state->viewIconWhere == 1 && state->viewButton) {
                state->viewButton.Content(original);
            } else if (auto parent = mux::Media::VisualTreeHelper::GetParent(ours)) {
                if (auto panel = parent.try_as<muxc::Panel>()) {
                    uint32_t index = 0;
                    if (panel.Children().IndexOf(ours, index)) {
                        panel.Children().SetAt(index, original);
                    }
                } else if (auto border = parent.try_as<muxc::Border>()) {
                    border.Child(original);
                }
            }
        }
    } catch (...) {
        LogHresult(L"restoring the View button icon");
    }
    state->viewIconOriginal = nullptr;
    state->viewIconOurs = nullptr;
}

// While Columns shows, the View button wears the Columns icon: Explorer does not
// know the layout and keeps the icon of the last one it set.
void SyncViewButtonIcon(WindowState* state) {
    bool want = state->on && !state->softHidden && state->viewButton;
    if (state->viewIconShown) {
        // The button may have been rebuilt, taking our icon with it.
        bool gone = true;
        try {
            gone = !state->viewIconOurs ||
                   (state->viewIconWhere != 1 && state->viewIconWhere != 4 &&
                    !mux::Media::VisualTreeHelper::GetParent(state->viewIconOurs));
            if (state->viewIconWhere == 1 && state->viewButton) {
                gone = state->viewButton.Content() != state->viewIconOurs;
            } else if (state->viewIconWhere == 4) {
                auto bar = state->viewButton.try_as<muxc::AppBarButton>();
                gone = !bar || !(bar.Icon() == state->viewIconOurs.try_as<muxc::IconElement>());
            }
        } catch (...) {
        }
        if (gone) {
            state->viewIconShown = false;
            state->viewIconOriginal = nullptr;
            state->viewIconOurs = nullptr;
        }
    }
    if (!want) {
        RestoreViewButtonIcon(state);
        return;
    }
    if (state->viewIconShown) {
        return;
    }
    try {
        auto button = state->viewButton;
        mux::UIElement original{nullptr};
        int where = 0;
        auto content = button.Content();
        if (auto bar = button.try_as<muxc::AppBarButton>(); bar && bar.Icon()) {
            // The toolbar's button: its Icon property is what shows.
            original = bar.Icon();
            where = 4;
        } else if (content && ClassOf(content).find(L"Icon") != std::wstring::npos) {
            original = content.try_as<mux::UIElement>();
            where = 1;
        } else {
            int visited = 0;
            original = FindIconElement(button, 0, visited);
            if (original) {
                auto parent = mux::Media::VisualTreeHelper::GetParent(original);
                where = parent.try_as<muxc::Panel>() ? 2 : parent.try_as<muxc::Border>() ? 3 : 0;
            }
        }
        if (!original || !where) {
            if (!state->viewIconLogged) {
                state->viewIconLogged = true;
                Out(L"the View button has no icon this mod can replace, its tree:");
                LogTree(button, 0);
            }
            return;
        }
        auto ours = MakeColumnsIcon();
        auto frame = ours.as<mux::FrameworkElement>();
        if (auto source = original.try_as<mux::FrameworkElement>()) {
            frame.Margin(source.Margin());
            frame.HorizontalAlignment(source.HorizontalAlignment());
            frame.VerticalAlignment(source.VerticalAlignment());
        }
        if (where == 4) {
            // As wide as the icon it replaces, and as high as its shape asks.
            double width = 16;
            if (auto source = original.try_as<mux::FrameworkElement>()) {
                if (source.ActualWidth() > 1) {
                    width = source.ActualWidth();
                }
                Out(L"the View button's icon is %.1f x %.1f", source.ActualWidth(), source.ActualHeight());
            }
            // The toolbar scales the icon to fill its box, so the box must be the
            // one the original has (square); the drawing is then made smaller inside
            // it, to the width of the glyphs around it.
            frame.Width(width);
            frame.Height(width);
            // The drawing is 16 x 12 in a square box and sits at the top of it:
            // moved down by half the difference, so it is centred like the others.
            mux::Media::CompositeTransform inside;
            inside.ScaleX(0.86);
            inside.ScaleY(0.86);
            inside.TranslateY(2.5);
            frame.RenderTransformOrigin(winrt::Windows::Foundation::Point{0.5f, 0.5f});
            frame.RenderTransform(inside);
            button.as<muxc::AppBarButton>().Icon(ours);
        } else if (where == 1) {
            button.Content(ours);
        } else {
            auto parent = mux::Media::VisualTreeHelper::GetParent(original);
            if (where == 2) {
                auto panel = parent.as<muxc::Panel>();
                uint32_t index = 0;
                if (!panel.Children().IndexOf(original, index)) {
                    return;
                }
                panel.Children().SetAt(index, ours);
            } else {
                parent.as<muxc::Border>().Child(ours);
            }
        }
        state->viewIconOriginal = original;
        state->viewIconOurs = ours;
        state->viewIconWhere = where;
        state->viewIconShown = true;
        Out(L"the View button shows the Columns icon (replaced a %s)", ClassOf(original).c_str());
    } catch (...) {
        LogHresult(L"showing the Columns icon on the View button");
    }
}

// ---- Hover while the keyboard is in use ----

// The rows ignore the pointer, so none keeps a hover look because the pointer
// rests where it was. The tick gives them back when the pointer moves.
void SetRowsHitTest(WindowState* state, bool on) {
    for (auto& column : state->columns) {
        if (!column->list) {
            continue;
        }
        try {
            auto items = column->list.Items();
            for (uint32_t i = 0; i < items.Size(); i++) {
                if (auto row = items.GetAt(i).try_as<muxc::ListViewItem>()) {
                    row.IsHitTestVisible(on);
                }
            }
        } catch (...) {
        }
    }
}

void HoverOff(WindowState* state) {
    if (state->kbHoverOff) {
        return;
    }
    state->kbHoverOff = true;
    GetCursorPos(&state->kbCursor);
    SetRowsHitTest(state, false);
}

void HoverOn(WindowState* state) {
    if (!state->kbHoverOff) {
        return;
    }
    state->kbHoverOff = false;
    SetRowsHitTest(state, true);
}

void HoverTick(WindowState* state) {
    if (!state->kbHoverOff) {
        return;
    }
    POINT now;
    if (GetCursorPos(&now) && (std::abs(now.x - state->kbCursor.x) > 2 || std::abs(now.y - state->kbCursor.y) > 2)) {
        HoverOn(state);
    }
}

// The last column, when it is empty, has no right edge and no handle to drag:
// there is nothing beside it to resize against.
void UpdateLastEdge(WindowState* state) {
    for (size_t i = 0; i < state->columns.size(); i++) {
        auto& column = state->columns[i];
        if (!column->grip) {
            continue;
        }
        bool last = i + 1 == state->columns.size();
        bool hide = last && column->filled && column->entries.empty();
        auto wanted = hide ? mux::Visibility::Collapsed : mux::Visibility::Visible;
        try {
            if (column->grip.Visibility() != wanted) {
                column->grip.Visibility(wanted);
            }
            // The last column has no separator line to its right, whatever it holds;
            // the handle to resize it still shows when the pointer is on the edge.
            if (auto content = column->grip.Child().try_as<muxc::Panel>()) {
                if (content.Children().Size() > 0) {
                    content.Children().GetAt(0).Opacity(last ? 0.0 : 1.0);
                }
            }
        } catch (...) {
        }
    }
}

void LogRowTree(mux::DependencyObject const& node, int depth) {
    if (!node || depth > 6) {
        return;
    }
    std::wstring extra;
    if (auto element = node.try_as<mux::FrameworkElement>()) {
        wchar_t text[96];
        swprintf(text, 96, L" %.0fx%.0f margin %.1f,%.1f,%.1f,%.1f", element.ActualWidth(), element.ActualHeight(),
                 element.Margin().Left, element.Margin().Top, element.Margin().Right, element.Margin().Bottom);
        extra = text;
    }
    Out(L"row tree: %*s%s%s", depth * 2, L"", ClassOf(node).c_str(), extra.c_str());
    int children = mux::Media::VisualTreeHelper::GetChildrenCount(node);
    for (int i = 0; i < children; i++) {
        LogRowTree(mux::Media::VisualTreeHelper::GetChild(node, i), depth + 1);
    }
}
