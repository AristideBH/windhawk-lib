// The Sort and Group by menus of the command bar list the Tags property under Windows' own name for it ("Mots clés" in French,
// "Tags" in English), where Explorer's other menus and this mod say "Étiquettes" / "Tags". Its entries are renamed to the mod's
// name each time the menu opens. The menu is found from the button named SortAndGroupButton on the ticks of a new window.

namespace {

// The display name Windows gives System.Keywords, or empty.
std::wstring LabelsKeywordsName() {
    static std::wstring name;
    if (name.empty()) {
        IPropertyDescription* description = nullptr;
        if (SUCCEEDED(PSGetPropertyDescription(PKEY_Keywords, IID_PPV_ARGS(&description))) && description) {
            PWSTR text = nullptr;
            if (SUCCEEDED(description->GetDisplayName(&text)) && text) {
                name = text;
                CoTaskMemFree(text);
            }
            description->Release();
        }
    }
    return name;
}

void LabelsRenameKeywords(winrt::Windows::Foundation::Collections::IVector<muxc::MenuFlyoutItemBase> const& items) {
    std::wstring original = LabelsKeywordsName();
    std::wstring ours = Str(L"labels.menu");
    if (original.empty()) {
        return;
    }
    for (auto const& base : items) {
        if (auto radio = base.try_as<muxc::RadioMenuFlyoutItem>()) {
            std::wstring text = std::wstring(radio.Text());
            bool on = g_labels && g_labelsGroup && !g_unloading;  // At unload the entry gets Windows' own name back.
            if (on && text == original) {
                LogVerbose(L"SORTMENU renamed '%s' to '%s'", text.c_str(), ours.c_str());
                radio.Text(ours);
            } else if (!on && text == ours) {
                radio.Text(original);
            }
        } else if (auto sub = base.try_as<muxc::MenuFlyoutSubItem>()) {
            LabelsRenameKeywords(sub.Items());
        }
    }
}

[[clang::no_destroy]] thread_local winrt::weak_ref<muxc::MenuFlyout> t_labelsSortMenu;  // This window's Sort and group menu.

void LabelsOnSortMenuOpening(Inspectable const& sender) {
    if (g_unloading) {
        return;
    }
    try {
        if (auto menu = sender.try_as<muxc::MenuFlyout>()) {
            LabelsRenameKeywords(menu.Items());
        }
    } catch (...) {
    }
}

}  // namespace

// Called on the ticks of a new window until it returns true: the Sort and group menu gets its handler.
bool LabelsSortMenuTick(WindowState* state) {
    auto content = state->xamlRoot ? state->xamlRoot.Content() : nullptr;
    auto root = content ? content.try_as<mux::DependencyObject>() : nullptr;
    if (!root) {
        return false;
    }
    auto button = ToolbarFindById(root, L"SortAndGroupButton", 0).try_as<muxc::AppBarButton>();
    if (!button) {
        return false;
    }
    auto menu = button.Flyout().try_as<muxc::MenuFlyout>();
    if (!menu) {
        return false;
    }
    t_labelsSortMenu = winrt::make_weak(menu);
    Keep(menu, menu.Opening(winrt::auto_revoke, [](Inspectable const& sender, Inspectable const&) { LabelsOnSortMenuOpening(sender); }));
    LabelsRenameKeywords(menu.Items());
    return true;
}

// At unload, on each thread that renamed the entry: Windows' own name is put back (the handler that would do it is revoked).
void LabelsRestoreSortMenu() {
    try {
        if (auto menu = t_labelsSortMenu.get()) {
            LabelsRenameKeywords(menu.Items());
        }
    } catch (...) {
    }
    t_labelsSortMenu = nullptr;
}
