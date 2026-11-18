// Labels in File Explorer's own right-click menu. The menu is a CommandBarFlyout that FileExplorerExtensions.dll fills
// one command at a time through MenuFlyoutControl::AppendMenuItem. The first time a flyout is seen there, a handler
// is put on its Opening event; when the menu opens, a "Tags" entry with a submenu is added to its secondary commands
// (before "Show more options" when that is there), for the items selected in the window. The menu of an empty space
// has no selection, so it gets no entry.

// The paths of the items selected in this thread's Explorer window.
std::vector<std::wstring> LabelsSelectedPaths() {
    std::vector<std::wstring> paths;
    HWND topLevel = LabelsThreadTopLevel();
    if (!topLevel) {
        LogVerbose(L"LABELS MENU selection: no Explorer window on this thread");
        return paths;
    }
    IShellBrowser* browser = BrowserOf(topLevel);
    if (!browser) {
        LogVerbose(L"LABELS MENU selection: no shell browser for window %p", (void*)topLevel);
        return paths;
    }
    Pidl folder = CurrentFolder(browser);
    if (!folder) {
        LogVerbose(L"LABELS MENU selection: no current folder");
        return paths;
    }
    std::vector<Pidl> selected = ExplorerSelection(browser);
    LogVerbose(L"LABELS MENU selection: folder \"%s\", %d selected", PathOf(folder.get()).c_str(), (int)selected.size());
    for (auto const& child : selected) {
        std::wstring path = FilePathOf(folder.get(), child.get());
        if (path.empty()) {
            return std::vector<std::wstring>();  // One item without a path (a library, This PC): no entry.
        }
        paths.push_back(std::move(path));
    }
    return paths;
}

// True when the window is, or is inside, the navigation pane's tree.
static bool LabelsInNavTree(HWND window) {
    for (HWND at = window; at; at = GetParent(at)) {
        wchar_t name[64];
        if (GetClassNameW(at, name, 64) && (wcscmp(name, L"NamespaceTreeControl") == 0 || wcscmp(name, L"SysTreeView32") == 0)) {
            return true;
        }
    }
    return false;
}

// The menu of a folder in the navigation pane comes through the same hook, but the selection read here is the file
// list's, not the folder that was clicked: such a menu gets no entry. The focus and the pointer tell where it was asked.
bool LabelsMenuIsForNavPane() {
    if (LabelsInNavTree(GetFocus())) {
        return true;
    }
    POINT where = {};
    return GetCursorPos(&where) && LabelsInNavTree(WindowFromPoint(where));
}

namespace {

struct LabelsHookedFlyout {
    void* key = nullptr;
    winrt::weak_ref<muxc::CommandBarFlyout> flyout;
};
[[clang::no_destroy]] thread_local std::vector<LabelsHookedFlyout> t_labelsFlyouts;

bool LabelsIsOurs(muxc::AppBarButton const& button) {
    return winrt::unbox_value_or<winrt::hstring>(button.Tag(), L"") == L"ee-labels";
}

// The menu is about to show: its "Tags" entry is put in, for the items that are selected now.
void LabelsOnMenuOpening(Inspectable const& sender) {
    if (g_unloading) {
        return;
    }
    auto bar = sender.try_as<muxc::CommandBarFlyout>();
    if (!bar) {
        return;
    }
    auto secondary = bar.SecondaryCommands();
    for (uint32_t i = secondary.Size(); i-- > 0;) {
        // The flyout is reused: an entry of the last opening is taken out first, also when labels were switched off.
        auto old = secondary.GetAt(i).try_as<muxc::AppBarButton>();
        if (old && LabelsIsOurs(old)) {
            secondary.RemoveAt(i);
        }
    }
    if (!g_labels) {
        return;
    }
    if (LabelsMenuIsForNavPane()) {
        LogVerbose(L"LABELS MENU no entry, the menu is the navigation pane's");
        return;
    }
#ifdef EXTRAS_VERBOSE
    try {
        auto target = bar.Target();
        LogVerbose(L"LABELS MENU target=%s", target ? std::wstring(winrt::get_class_name(target)).c_str() : L"(none)");
    } catch (...) {
    }
#endif
    std::vector<std::wstring> paths = LabelsSelectedPaths();
    if (paths.empty()) {
        LogVerbose(L"LABELS MENU no entry, selection=%d", (int)paths.size());
        return;
    }
    // The entry is there at once, disabled; whether the drive keeps the stream is asked on a mod thread (a share may be slow).
    muxc::AppBarButton button;
    button.Label(Str(L"labels.menu"));
    button.IsEnabled(false);
    button.Tag(winrt::box_value(winrt::hstring{L"ee-labels"}));
    muxc::FontIcon icon;
    icon.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
    icon.Glyph(L"");
    button.Icon(icon);
    auto weakBar = winrt::make_weak(bar);
    auto weakButton = winrt::make_weak(button);
    auto shared = std::make_shared<std::vector<std::wstring>>(paths);
    LabelsReadAsync<bool>(
        [shared]() { return LabelsVolumeSupportsStreams((*shared)[0]); },
        [weakButton, weakBar, shared](bool const& keeps) {
            auto shown = weakButton.get();
            if (!shown) {
                return;
            }
            if (!keeps) {
                shown.Label(Str(L"labels.menu.ntfs"));  // A drive that cannot keep the stream: greyed.
                return;
            }
            shown.Flyout(LabelsTagsSubmenu(*shared, [weakBar]() -> mux::FrameworkElement {
                auto bar = weakBar.get();
                return bar ? bar.Target() : mux::FrameworkElement{nullptr};
            }));
            shown.IsEnabled(true);
        });
    // Before "Show more options" (the command with Shift+F10) when it is there, else at the end.
    uint32_t at = secondary.Size();
    for (uint32_t i = 0; i < secondary.Size(); i++) {
        auto other = secondary.GetAt(i).try_as<muxc::AppBarButton>();
        if (!other) {
            continue;
        }
        for (auto accelerator : other.KeyboardAccelerators()) {
            if (accelerator.Key() == winrt::Windows::System::VirtualKey::F10) {
                at = i;
            }
        }
    }
    secondary.InsertAt(at, button);
    LogVerbose(L"LABELS MENU appended entries=%u selection=%d at=%u", secondary.Size(), (int)paths.size(), at);
}

}  // namespace

// At unload, on each thread that hooked a flyout: the "Tags" entries are taken out of Explorer's reused menus, since the handler
// that would remove them at the next opening is revoked.
void LabelsRemoveMenuEntries() {
    for (auto& hooked : t_labelsFlyouts) {
        auto bar = hooked.flyout.get();
        if (!bar) {
            continue;
        }
        try {
            auto secondary = bar.SecondaryCommands();
            for (uint32_t i = secondary.Size(); i-- > 0;) {
                auto old = secondary.GetAt(i).try_as<muxc::AppBarButton>();
                if (old && LabelsIsOurs(old)) {
                    secondary.RemoveAt(i);
                }
            }
        } catch (...) {
        }
    }
    t_labelsFlyouts.clear();
}

using LabelsAppendMenuItem_t = void(WINAPI*)(void* self, void* flyout, void* menuItem);
LabelsAppendMenuItem_t LabelsAppendMenuItem_Original;

// Called for every command that Explorer puts in a menu: the first one of a flyout puts the handler on it.
void WINAPI LabelsAppendMenuItem_Hook(void* self, void* flyout, void* menuItem) {
    LabelsAppendMenuItem_Original(self, flyout, menuItem);
#ifdef EXTRAS_VERBOSE
    {
        static std::atomic<int> seen{0};
        if (seen++ < 40) {
            Wh_Log(L"LABELS MENU append seen, flyout=%p thread=%lu", flyout, GetCurrentThreadId());
        }
    }
#endif
    if (!g_labels || g_unloading || !flyout) {
        return;
    }
    try {
        auto& bar = *(muxc::CommandBarFlyout*)flyout;
        void* key = *(void**)flyout;
        auto& all = t_labelsFlyouts;
        all.erase(std::remove_if(all.begin(), all.end(), [](LabelsHookedFlyout const& hooked) { return !hooked.flyout.get(); }),
                  all.end());
        for (auto const& hooked : all) {
            if (hooked.key == key) {
                return;
            }
        }
        Keep(bar, bar.Opening(winrt::auto_revoke, [](Inspectable const& sender, Inspectable const&) {
            try {
                LabelsOnMenuOpening(sender);
            } catch (...) {
            }
        }));
        LabelsHookedFlyout hooked;
        hooked.key = key;
        hooked.flyout = winrt::make_weak(bar);
        all.push_back(std::move(hooked));
        LogVerbose(L"LABELS MENU flyout seen, handler attached");
    } catch (...) {
    }
}

// Show labels, or a colour name, changed: the lists draw again, and a pane that is on show builds its page again
// (its Tags section appears or goes).
void LabelsSettingsChanged() {
    LabelsRepaintWindows(std::wstring());
    std::lock_guard<std::mutex> lock(g_statesMutex);
    for (auto& state : g_states) {
        if (state->queue) {
            state->queue.TryEnqueue([]() {
                if (g_unloading) {
                    return;
                }
                WindowState* now = StateForThisThread(false);
                if (now && now->details && now->details->visible) {
                    now->details->shownKey.clear();
                }
            });
        }
    }
}
