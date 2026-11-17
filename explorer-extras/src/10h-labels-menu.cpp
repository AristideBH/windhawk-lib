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
    if (!g_labels || g_unloading) {
        return;
    }
    auto bar = sender.try_as<muxc::CommandBarFlyout>();
    if (!bar) {
        return;
    }
    auto secondary = bar.SecondaryCommands();
    for (uint32_t i = secondary.Size(); i-- > 0;) {
        // The flyout is reused: an entry of the last opening is taken out first.
        auto old = secondary.GetAt(i).try_as<muxc::AppBarButton>();
        if (old && LabelsIsOurs(old)) {
            secondary.RemoveAt(i);
        }
    }
    std::vector<std::wstring> paths = LabelsSelectedPaths();
    if (paths.empty() || !LabelsVolumeSupportsStreams(paths[0])) {
        LogVerbose(L"LABELS MENU no entry, selection=%d", (int)paths.size());
        return;
    }
    muxc::AppBarButton button;
    button.Label(Str(L"labels.menu"));
    button.Tag(winrt::box_value(winrt::hstring{L"ee-labels"}));
    muxc::FontIcon icon;
    icon.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
    icon.Glyph(L"");
    button.Icon(icon);
    auto weakBar = winrt::make_weak(bar);
    button.Flyout(LabelsTagsSubmenu(paths, [weakBar]() -> mux::FrameworkElement {
        auto shown = weakBar.get();
        return shown ? shown.Target() : mux::FrameworkElement{nullptr};
    }));
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
