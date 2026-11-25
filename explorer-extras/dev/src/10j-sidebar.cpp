// Sidebar: the mod's own navigation pane. A XAML island placed over Explorer's navigation pane (the details pane does the same
// on its side): Explorer keeps the space, the splitter and the View > Show > Navigation pane toggle, and the native tree stays
// alive under the island. Three sections, Favorites, Locations and Tags, each with a header that collapses it.
//
// The island follows the VISIBLE `NamespaceTreeControl` of the window (every tab has its own, and Explorer hides the pane by
// making that control invisible without moving it): shown on its rectangle while one is visible, hidden when none is.
//
// This part is the shell: the island, the headers, the rows and their icons. What the sections hold comes from 10j1 to 10j3
// (Favorites, Locations, Tags); until those exist the sections carry a few rows of known folders and the recent tags.

// A place of the Locations section (10j2). `path` is empty for a place that is not a folder on disk.
struct LocationPath {
    std::wstring key;
    std::wstring path;
};
struct LocationEntry {
    std::wstring key;
    std::wstring name;
    std::wstring path;
    std::shared_ptr<ITEMIDLIST> pidl;
    bool visible = true;
    bool drive = false;
};

// A folder pinned to Quick access, as the Favorites section lists it (10j1).
struct FavoriteEntry {
    std::wstring path;
    std::wstring name;
    std::shared_ptr<ITEMIDLIST> pidl;
    bool exists = true;
};

// A change to a tag on every file that carries it (rename, recolour, delete), running on a mod thread (10j3). The window reads
// the counters at its tick to show the progress.
struct TagsJob {
    std::atomic<bool> cancel{false};
    std::atomic<size_t> done{0};
    std::atomic<size_t> total{0};
    std::atomic<bool> finished{false};
};

struct SidebarPanel {
    mux::Hosting::DesktopWindowXamlSource source{nullptr};
    winrt::Microsoft::UI::Content::DesktopChildSiteBridge bridge{nullptr};
    mud::DispatcherQueueTimer timer{nullptr};
    winrt::event_token token{};
    muxc::StackPanel host{nullptr};
    HWND pane = nullptr;      // The visible native pane the island sits on.
    RECT rect{};              // Where the island is, in the window's client coordinates.
    bool shown = false;
    bool built = false;
    int tickMs = 60;
    unsigned ticks = 0;
    ULONGLONG iconSide = 0;   // The icon size (pixels) the rows asked for.
    bool dark = false;        // The theme the tag icons were last made for.
    struct Section {
        std::wstring key;
        muxc::StackPanel body{nullptr};
        mux::UIElement chevronUp{nullptr};
        mux::UIElement chevronDown{nullptr};
        mux::UIElement chevron{nullptr};
        mux::UIElement more{nullptr};  // The "…" button of the header, when it has one.
        muxc::Button headerButton{nullptr};  // The header's own button: where the pill goes when the section is collapsed.
        bool collapsed = false;
        bool hovered = false;
        mud::DispatcherQueueTimer collapseTimer{nullptr};  // Held here: a timer nobody holds is dropped and never ticks.
    };
    std::vector<Section> sections;
    // A row that shows a tag: its icon is remade when the theme changes.
    struct TagRow {
        winrt::weak_ref<muxc::Grid> icon;
        int colour = 0;
        std::wstring name;
        winrt::weak_ref<muxc::TextBlock> count;  // The file count at the right of the row, when that option is on.
    };
    std::vector<TagRow> tagRows;
    std::wstring viewKey;  // The row of the tag whose list is open ("tags" for the list of tags): it keeps the pill.
    bool tagEditing = false;  // A tag's name is being typed in its row: the rows are not made again meanwhile.
    std::wstring tagSig;  // The tags the rows were made from (names and colours, in order): the rows are made again when it changes.
    muxc::Grid tagProgress{nullptr};  // The line under the Tags header that shows a running change (a bar and a cancel button).
    muxc::ProgressBar tagBar{nullptr};
    std::shared_ptr<TagsJob> tagJob;
    // The Locations section: the entries shown, the rows that mark the selected one, the menu of the header.
    std::vector<LocationEntry> locations;
    struct Mark {
        std::wstring key;
        muxc::Border selected{nullptr};
        muxc::Border pill{nullptr};
        muxc::Button button{nullptr};
    };
    std::vector<Mark> locationMarks;
    // One pill for the selected row, which slides to the row that gets selected (like the active column's).
    muxc::Grid pillHost{nullptr};
    muxc::Border pill{nullptr};
    bool pillPlaced = false;
    std::wstring shownKey;  // The row the pill is on.
    muxc::MenuFlyout locationsMenu{nullptr};
    muxc::Grid driveChildren{nullptr};
    std::wstring selectedKey;
    std::shared_ptr<ITEMIDLIST> lastFolder;
    DWORD driveMask = 0;
    unsigned remoteVersion = 0;  // The network drives the list holds (they are named on a thread).
    // The Favorites section: the pinned folders shown, the block that takes a dropped folder, the drop target of the island.
    std::vector<FavoriteEntry> favorites;
    unsigned favoritesVersion = 0;
    mux::UIElement favBlock{nullptr};
    muxc::Border favHighlight{nullptr};
    mux::UIElement tagsBlock{nullptr};  // The whole Tags section: it is out of sight when labels are switched off.
    muxc::Border tagLine{nullptr};      // The accent line that shows where a dragged tag row will go (manual order).
    muxc::Canvas overlay{nullptr};  // Where the label of a drag from the columns is made.
    muxc::Primitives::Popup ghostPopup{nullptr};  // Where it shows: a popup that is not cut at the sidebar's edge.
    muxc::Border favLine{nullptr};  // The accent line that shows where a dropped folder goes in the list.
    IDropTarget* drop = nullptr;
    HWND dropWindow = nullptr;
    std::wstring dropZone;  // The row or section a drag is over, while one is.
    struct DriveBar {
        std::wstring path;
        winrt::weak_ref<muxc::Grid> bar;       // The free-space bar of a drive's row (its two columns are the used and the free share).
    };
    std::vector<DriveBar> driveBars;
    unsigned settingsVersion = 0;  // g_sidebarSettingsVersion when this sidebar was made.
    bool suppressClick = false;  // A row was dragged to a new place: the release is not a click.
};

// 10j2 (Locations), used by the shell below.
bool LocationsGrouped(size_t driveEntryCount);
bool LocationVisible(std::wstring const& key);
void LocationSetVisible(std::wstring const& key, bool visible);
std::wstring SidebarEntryIn(std::vector<LocationPath> const& entries, std::wstring const& folder);
std::vector<LocationEntry> LocationsList();
void SidebarFillLocations(WindowState* state, SidebarPanel* panel);
muxc::Button SidebarLocationsMore(WindowState* state, SidebarPanel* panel);
void SidebarFollowFolder(WindowState* state, SidebarPanel* panel);
namespace {
void SidebarMarkSelected(SidebarPanel* panel, std::wstring const& key);
}
// 10j4 (the list of a tag's files).
void TagResultsShow(WindowState* state, std::wstring const& tag);
void TagListShow(WindowState* state);
void TagViewLeave(WindowState* state);
void TagViewLeaveHere(WindowState* state);
void TagViewTick(WindowState* state);
void ReleaseTagView(WindowState* state);
void AbandonTagView(WindowState* state);
// 10j3 (Tags).
void SidebarKeyDown(WindowState* state, mux::Input::KeyRoutedEventArgs const& args);
std::vector<std::wstring> TagsItemsOf(IDataObject* data);
void TagsDropLater(std::wstring const& tag, std::vector<std::wstring> paths);
void SidebarFillTags(WindowState* state, SidebarPanel* panel);
void TagsTick(WindowState* state, SidebarPanel* panel);
muxc::Button SidebarTagsMore(WindowState* state, SidebarPanel* panel);
muxc::Grid SidebarTagsProgress(WindowState* state, SidebarPanel* panel);
void TagsReleaseThreadObjects();
// 10j1 (Favorites).
void SidebarFillFavorites(WindowState* state, SidebarPanel* panel);
void FavoritesTick(WindowState* state, SidebarPanel* panel);
void FavoritesWatchStart();
void SidebarRegisterDrop(WindowState* state, SidebarPanel* panel);
void SidebarRevokeDrop(SidebarPanel* panel);

namespace {

constexpr int kSidebarCollapseMs = 180;

std::wstring SidebarCollapsedValue(std::wstring const& key) {
    return L"sidebar.collapsed." + key;
}

}  // namespace

// The window of this thread's sidebar: the hook of Explorer's accelerators leaves Delete and F2 to it while it has the keyboard.
thread_local HWND g_sidebarWindowLocal = nullptr;
HWND SidebarWindow() {
    return g_sidebarWindowLocal;
}

bool SidebarSectionCollapsed(std::wstring const& key) {
    return Wh_GetIntValue(SidebarCollapsedValue(key).c_str(), 0) != 0;
}

void SidebarSetSectionCollapsed(std::wstring const& key, bool collapsed) {
    Wh_SetIntValue(SidebarCollapsedValue(key).c_str(), collapsed ? 1 : 0);
}

namespace {

const wchar_t kSidebarButtonTemplate[] =
    L"<ControlTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' "
    L"xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' TargetType='Button'>"
    L"<Grid Background='Transparent'>"
    L"<VisualStateManager.VisualStateGroups><VisualStateGroup x:Name='CommonStates'>"
    L"<VisualState x:Name='Normal'/>"
    L"<VisualState x:Name='PointerOver'><VisualState.Setters>"
    L"<Setter Target='Base.Background' Value='{ThemeResource SubtleFillColorSecondaryBrush}'/></VisualState.Setters></VisualState>"
    L"<VisualState x:Name='Pressed'><VisualState.Setters>"
    L"<Setter Target='Base.Background' Value='{ThemeResource SubtleFillColorTertiaryBrush}'/></VisualState.Setters></VisualState>"
    L"<VisualState x:Name='Disabled'/>"
    L"</VisualStateGroup></VisualStateManager.VisualStateGroups>"
    L"<Border x:Name='Base' Margin='{TemplateBinding Padding}' CornerRadius='4' Background='Transparent'/>"
    L"<ContentPresenter HorizontalContentAlignment='Stretch' VerticalContentAlignment='Center'/>"
    L"</Grid></ControlTemplate>";

// The header's button: no fill on hover (the "Highlight section headers" setting brings the row's one back).
const wchar_t kSidebarFlatTemplate[] =
    L"<ControlTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' TargetType='Button'>"
    L"<Grid Background='Transparent'><ContentPresenter HorizontalContentAlignment='Stretch' VerticalContentAlignment='Center'/></Grid>"
    L"</ControlTemplate>";

// The small buttons of a header (chevron, "…"): a button's look, a faint fill at rest, more on hover and press.
const wchar_t kSidebarSmallTemplate[] =
    L"<ControlTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' "
    L"xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' TargetType='Button'>"
    L"<Grid Background='Transparent'>"
    L"<VisualStateManager.VisualStateGroups><VisualStateGroup x:Name='CommonStates'>"
    L"<VisualState x:Name='Normal'/>"
    L"<VisualState x:Name='PointerOver'><VisualState.Setters>"
    L"<Setter Target='Base.Background' Value='{ThemeResource ControlFillColorSecondaryBrush}'/>"
    L"<Setter Target='Base.BorderBrush' Value='{ThemeResource ControlStrokeColorDefaultBrush}'/></VisualState.Setters></VisualState>"
    L"<VisualState x:Name='Pressed'><VisualState.Setters>"
    L"<Setter Target='Base.Background' Value='{ThemeResource ControlFillColorTertiaryBrush}'/>"
    L"<Setter Target='Base.BorderBrush' Value='{ThemeResource ControlStrokeColorDefaultBrush}'/></VisualState.Setters></VisualState>"
    L"<VisualState x:Name='Disabled'/>"
    L"</VisualStateGroup></VisualStateManager.VisualStateGroups>"
    L"<Border x:Name='Base' CornerRadius='4' Background='Transparent' BorderBrush='Transparent' BorderThickness='1'/>"
    L"<ContentPresenter HorizontalContentAlignment='Center' VerticalContentAlignment='Center'/>"
    L"</Grid></ControlTemplate>";

muxc::Button SidebarNewButtonWith(const wchar_t* automationId, double height, const wchar_t* padding, const wchar_t* templateMarkup);

muxc::Button SidebarNewButton(const wchar_t* automationId, double height, const wchar_t* padding) {
    return SidebarNewButtonWith(automationId, height, padding, kSidebarButtonTemplate);
}

muxc::Button SidebarNewButtonWith(const wchar_t* automationId, double height, const wchar_t* padding, const wchar_t* templateMarkup) {
    std::wstring markup = std::wstring(L"<Button ") + kXamlNs + L" Height='" + std::to_wstring(height) +
                          L"' Padding='" + padding + L"' BorderThickness='0' Background='Transparent' "
                          L"HorizontalAlignment='Stretch' HorizontalContentAlignment='Stretch' UseSystemFocusVisuals='True' "
                          L"AutomationProperties.AutomationId='" + automationId + L"'/>";
    auto button = LoadXaml(markup).as<muxc::Button>();
    button.Template(LoadXaml(templateMarkup).as<muxc::ControlTemplate>());
    return button;
}

// A Segoe Fluent Icons glyph in the secondary text colour (a theme resource, so it follows the theme).
muxc::FontIcon SidebarGlyph(const wchar_t* glyph, double size) {
    auto icon = LoadXaml(std::wstring(L"<FontIcon ") + kXamlNs +
                         L" FontFamily='Segoe Fluent Icons' Foreground='{ThemeResource TextFillColorSecondaryBrush}'/>")
                    .as<muxc::FontIcon>();
    icon.Glyph(glyph);
    icon.FontSize(size);
    return icon;
}

muxc::Border SidebarThemedBorder(const wchar_t* brush) {
    return LoadXaml(std::wstring(L"<Border ") + kXamlNs + L" Background='{ThemeResource " + brush + L"}'/>").as<muxc::Border>();
}

// The pixels of an icon as a bitmap (on the UI thread).
mux::Media::Imaging::WriteableBitmap SidebarBitmap(IconBits const& icon) {
    mux::Media::Imaging::WriteableBitmap bitmap(icon.width, icon.height);
    auto buffer = bitmap.PixelBuffer();
    winrt::com_ptr<IBufferByteAccessLocal> access;
    winrt::check_hresult(((::IUnknown*)winrt::get_abi(buffer))->QueryInterface(kIidBufferByteAccess, access.put_void()));
    uint8_t* pixels = nullptr;
    winrt::check_hresult(access->Buffer(&pixels));
    if (!pixels || buffer.Capacity() < icon.bgra.size()) {
        throw winrt::hresult_error(E_FAIL);
    }
    memcpy(pixels, icon.bgra.data(), icon.bgra.size());
    buffer.Length((uint32_t)icon.bgra.size());
    bitmap.Invalidate();
    return bitmap;
}

}  // namespace

// The tag glyph of the mockups, drawn as a vector in the colour of a label (softened in the light theme, as the dots are).
muxc::Grid SidebarTagIcon(int colour, bool dark) {
    COLORREF rgb = LabelsDotRgb(colour, dark);
    auto path = LoadXaml(std::wstring(L"<Path ") + kXamlNs +
                         L" Width='16' Height='16' Stretch='None' Data='F0 M 2.5,2.5 L 8.2,2.5 C 8.6,2.5 8.9,2.65 9.2,2.9 "
                         L"L 13.1,6.8 C 13.7,7.4 13.7,8.4 13.1,9 L 9,13.1 C 8.4,13.7 7.4,13.7 6.8,13.1 L 2.9,9.2 C 2.65,8.9 2.5,8.6 2.5,8.2 Z "
                         L"M 4.6,3.9 A 0.8,0.8 0 1 0 4.6,5.5 A 0.8,0.8 0 1 0 4.6,3.9 Z'/>")
                    .as<mux::Shapes::Path>();
    path.Fill(mux::Media::SolidColorBrush(winrt::Windows::UI::Color{255, GetRValue(rgb), GetGValue(rgb), GetBValue(rgb)}));
    muxc::Grid grid;
    grid.Width(16);
    grid.Height(16);
    grid.Children().Append(path);
    return grid;
}

// Reads an item's icon on a thread of the mod and hands it to the UI thread as a bitmap, which `done` puts where it goes.
// `done` runs on the window's thread, and not at all when the window or the mod is gone.
void SidebarLoadIcon(WindowState* state, PCIDLIST_ABSOLUTE pidl, std::function<void(mux::Media::ImageSource const&)> done) {
    if (!pidl || !state->queue || g_unloading) {
        return;
    }
    Pidl copy(ILCloneFull(pidl));
    LONG side = IconSideOf(state);
    mud::DispatcherQueue queue = state->queue;
    StartModThread([copy = std::move(copy), side, queue, done = std::move(done)]() mutable {
        HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        auto bits = std::make_shared<IconBits>();
        bool ok = false;
        IShellItem* item = nullptr;
        if (SUCCEEDED(SHCreateItemFromIDList(copy.get(), IID_PPV_ARGS(&item))) && item) {
            IShellItemImageFactory* factory = nullptr;
            if (SUCCEEDED(item->QueryInterface(IID_PPV_ARGS(&factory))) && factory) {
                ok = GetItemBits(factory, side ? side : 16, SIIGBF_ICONONLY, bits.get(), true);
                factory->Release();
            }
            item->Release();
        }
        if (SUCCEEDED(init)) {
            CoUninitialize();
        }
        if (!ok || g_unloading) {
            return;
        }
        auto shared = std::make_shared<std::function<void(mux::Media::ImageSource const&)>>(std::move(done));
        try {
            queue.TryEnqueue([bits, shared]() {
                if (g_unloading) {
                    return;
                }
                try {
                    (*shared)(SidebarBitmap(*bits));
                } catch (...) {
                }
            });
        } catch (...) {
        }
    });
}

namespace {

// A row of a section: an icon (16 px), a text, a selected backing and the pill that marks the selected row.
// `iconHost` is where the icon goes; it is filled later (a shell icon arrives from a thread) or at once (a tag).
struct SidebarRow {
    muxc::Button button{nullptr};
    muxc::Grid iconHost{nullptr};
    muxc::Border selected{nullptr};
    muxc::Border pill{nullptr};
    muxc::Grid content{nullptr};  // The row's face: overlays (a count, a free-space bar, an eject button) go in it.
};

SidebarRow SidebarMakeRow(std::wstring const& key, std::wstring const& text, bool selected, double indent = 0) {
    SidebarRow row;
    std::wstring id = L"SidebarRow." + key;
    row.button = SidebarNewButton(id.c_str(), 32, L"0");
    muxc::Grid content;
    row.selected = SidebarThemedBorder(L"SubtleFillColorSecondaryBrush");
    row.selected.Margin(mux::Thickness{0, 0, 0, 0});
    row.selected.CornerRadius(mux::CornerRadius{3, 3, 3, 3});
    row.selected.Visibility(selected ? mux::Visibility::Visible : mux::Visibility::Collapsed);
    content.Height(32);  // The presenter centres its content: without a height the backing is as thin as its margins.
    content.Children().Append(row.selected);
    row.pill = SidebarThemedBorder(L"AccentFillColorDefaultBrush");
    row.pill.Width(3);
    row.pill.Height(16);
    row.pill.CornerRadius(mux::CornerRadius{1.5, 1.5, 1.5, 1.5});
    row.pill.HorizontalAlignment(mux::HorizontalAlignment::Left);
    row.pill.VerticalAlignment(mux::VerticalAlignment::Center);
    row.pill.Margin(mux::Thickness{4, 0, 0, 0});
    row.pill.Visibility(selected ? mux::Visibility::Visible : mux::Visibility::Collapsed);
    content.Children().Append(row.pill);
    muxc::StackPanel line;
    line.Orientation(muxc::Orientation::Horizontal);
    line.Margin(mux::Thickness{15 + indent, 0, 11, 0});
    line.VerticalAlignment(mux::VerticalAlignment::Center);
    row.iconHost = muxc::Grid();
    row.iconHost.Width(16);
    row.iconHost.Height(16);
    row.iconHost.OpacityTransition(mux::ScalarTransition());
    line.Children().Append(row.iconHost);
    muxc::TextBlock label;
    label.Text(text);  // Set in code: a name is data, not markup.
    label.FontSize(14);
    label.Margin(mux::Thickness{10, 0, 0, 0});
    label.VerticalAlignment(mux::VerticalAlignment::Center);
    label.TextTrimming(mux::TextTrimming::CharacterEllipsis);
    line.Children().Append(label);
    content.Children().Append(line);
    row.button.Content(content);
    row.content = content;
    return row;
}

namespace {

// Makes a row of the sidebar draggable to another place in its list (Favorites, the tags in manual order). `where` tells, for a point
// of the island, the place it means (an index) and where the line that shows it goes; `showLine` draws it (below zero hides it);
// `drop` moves the row there, after the release (the rows are made again, the row itself included). The row follows nothing: it
// dims while it is dragged and a line shows where it will go.
void SidebarAttachReorder(WindowState* state, muxc::Button const& button,
                          std::function<void(winrt::Windows::Foundation::Point, size_t*, float*)> where,
                          std::function<void(float)> showLine, std::function<void(size_t)> drop) {
    struct Drag {
        bool down = false;
        bool active = false;
        winrt::Windows::Foundation::Point start{};
        double opacity = 1;
    };
    auto drag = std::make_shared<Drag>();
    winrt::weak_ref<muxc::Button> weak = button;
    auto finish = [state, drag, weak, showLine](bool dropped, winrt::Windows::Foundation::Point at, std::function<void(winrt::Windows::Foundation::Point)> then) {
        bool was = drag->active;
        drag->down = false;
        drag->active = false;
        if (!was) {
            return;
        }
        showLine(-1);
        if (auto b = weak.get()) {
            b.Opacity(drag->opacity);
        }
        if (dropped) {
            then(at);
        }
    };
    mux::Input::PointerEventHandler pressed([state, drag, weak](Inspectable const&, mux::Input::PointerRoutedEventArgs const& args) {
        auto point = args.GetCurrentPoint(nullptr);
        if (!point.Properties().IsLeftButtonPressed()) {
            return;
        }
        if (state->sidebar) {
            state->sidebar->suppressClick = false;
        }
        drag->down = true;
        drag->active = false;
        drag->start = point.Position();
        if (auto b = weak.get()) {
            drag->opacity = b.Opacity();
        }
    });
    mux::Input::PointerEventHandler moved([state, drag, weak, where, showLine](Inspectable const&, mux::Input::PointerRoutedEventArgs const& args) {
        if (!drag->down) {
            return;
        }
        auto position = args.GetCurrentPoint(nullptr).Position();
        if (!drag->active) {
            double dx = position.X - drag->start.X, dy = position.Y - drag->start.Y;
            if (dx * dx + dy * dy < 36) {
                return;
            }
            drag->active = true;
            if (state->sidebar) {
                state->sidebar->suppressClick = true;
            }
            if (auto b = weak.get()) {
                b.CapturePointer(args.Pointer());
                b.Opacity(0.5);
            }
        }
        size_t index = 0;
        float line = -1;
        where(position, &index, &line);
        showLine(line);
    });
    mux::Input::PointerEventHandler released([state, drag, weak, where, drop, finish](Inspectable const&, mux::Input::PointerRoutedEventArgs const& args) {
        auto position = args.GetCurrentPoint(nullptr).Position();
        if (auto b = weak.get()) {
            b.ReleasePointerCapture(args.Pointer());
        }
        finish(true, position, [state, where, drop](winrt::Windows::Foundation::Point at) {
            size_t index = 0;
            float line = -1;
            where(at, &index, &line);
            if (state->queue) {
                state->queue.TryEnqueue([drop, index]() {
                    if (!g_unloading) {
                        drop(index);
                    }
                });
            }
        });
    });
    // The button's own handling of the release lets go of the capture BEFORE this row's PointerReleased handler runs, so a lost
    // capture is judged after it: if the drag is still going then, it was cancelled, else the release dropped it.
    mux::Input::PointerEventHandler lost([state, drag, finish](Inspectable const&, mux::Input::PointerRoutedEventArgs const&) {
        if (state->queue) {
            state->queue.TryEnqueue([drag, finish]() {
                if (drag->active) {
                    finish(false, winrt::Windows::Foundation::Point{}, nullptr);
                }
            });
        }
    });
    AddRouted(button, mux::UIElement::PointerPressedEvent(), winrt::box_value(pressed));
    AddRouted(button, mux::UIElement::PointerMovedEvent(), winrt::box_value(moved));
    AddRouted(button, mux::UIElement::PointerReleasedEvent(), winrt::box_value(released));
    AddRouted(button, mux::UIElement::PointerCaptureLostEvent(), winrt::box_value(lost));
}

// The shell's menu of a row of the sidebar that is a folder (Favorites, Locations), with the rows of the sidebar's own: `extras`.
void SidebarShowItemMenu(WindowState* state, SidebarPanel* panel, muxc::Button const& button, PCIDLIST_ABSOLUTE pidl,
                         std::wstring const& path, std::vector<MenuItemSet::Extra> extras, std::vector<std::wstring> hideVerbs,
                         std::vector<std::wstring> promoteVerbs, mux::Input::RightTappedRoutedEventArgs const& args) {
    if (!pidl || !panel->bridge) {
        return;
    }
    MenuItemSet set;
    if (!MenuItemSetAdd(set, pidl, path, true)) {
        return;
    }
    set.extras = std::move(extras);
    set.hideVerbs = std::move(hideVerbs);
    set.promoteVerbs = std::move(promoteVerbs);
    set.anchor = button;
    set.position = args.GetPosition(button);
    set.rowBox = winrt::Windows::Foundation::Rect{0, 0, (float)button.ActualWidth(), (float)button.ActualHeight()};
    set.hostWindow = (HWND)panel->bridge.WindowId().Value;
    set.hostScale = button.XamlRoot() ? button.XamlRoot().RasterizationScale() : 1.0;
    args.Handled(true);
    ShowItemFlyoutFor(state, std::move(set));
}

}  // namespace

// A shell item's icon in a row, when it arrives.
void SidebarRowIconFromShell(WindowState* state, SidebarRow const& row, PCIDLIST_ABSOLUTE pidl) {
    winrt::weak_ref<muxc::Grid> weak = row.iconHost;
    row.iconHost.Opacity(0);
    SidebarLoadIcon(state, pidl, [weak](mux::Media::ImageSource const& source) {
        if (auto host = weak.get()) {
            muxc::Image image;
            image.Source(source);
            image.Width(16);
            image.Height(16);
            image.Stretch(mux::Media::Stretch::Uniform);
            host.Children().Clear();
            host.Children().Append(image);
            host.Opacity(1);
        }
    });
}

void SidebarSetChevron(SidebarPanel::Section& section) {
    if (!section.chevron) {
        return;
    }
    section.chevron.Opacity(section.hovered ? 1.0 : 0.0);
    if (section.more) {
        section.more.Opacity(section.hovered ? 1.0 : 0.0);
    }
    section.chevronUp.Opacity(section.collapsed ? 0.0 : 1.0);
    section.chevronDown.Opacity(section.collapsed ? 1.0 : 0.0);
}

// A section's header: the title as a button (the whole row toggles the section), the chevron and, for Locations and Tags, a "…" menu
// button, both of them real buttons of their own. They show while the pointer is anywhere over the row (or the keyboard is on
// one of them), so the chevron stays while the pointer is on the "…".
muxc::Grid SidebarMakeHeaderRow(WindowState* state, SidebarPanel::Section& section, std::wstring const& title,
                                std::function<void()> onToggle, muxc::Button const& more) {
    std::wstring id = L"SidebarHeader." + section.key;
    auto button = SidebarNewButtonWith(id.c_str(), 32, L"0", g_sidebarHeaderHover ? kSidebarButtonTemplate : kSidebarFlatTemplate);
    auto text = MakeText(title, 12, false, true);
    text.TextWrapping(mux::TextWrapping::NoWrap);
    text.TextTrimming(mux::TextTrimming::CharacterEllipsis);
    text.Margin(mux::Thickness{16, 0, 74, 0});
    text.VerticalAlignment(mux::VerticalAlignment::Center);
    muxc::Grid line;
    line.Children().Append(text);
    button.Content(line);

    muxc::Grid row;
    row.Background(mux::Media::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
    row.Children().Append(button);

    auto chevron = SidebarNewButtonWith((id + L".chevron").c_str(), 26, L"0", kSidebarSmallTemplate);
    chevron.Width(26);
    chevron.HorizontalAlignment(mux::HorizontalAlignment::Right);
    chevron.VerticalAlignment(mux::VerticalAlignment::Center);
    chevron.Margin(mux::Thickness{0, 0, 8, 0});
    chevron.OpacityTransition(mux::ScalarTransition());
    muxc::Grid glyphs;
    auto up = SidebarGlyph(L"", 12);
    auto down = SidebarGlyph(L"", 12);
    up.OpacityTransition(mux::ScalarTransition());
    down.OpacityTransition(mux::ScalarTransition());
    glyphs.Children().Append(up);
    glyphs.Children().Append(down);
    chevron.Content(glyphs);
    row.Children().Append(chevron);
    if (more) {
        more.HorizontalAlignment(mux::HorizontalAlignment::Right);
        more.VerticalAlignment(mux::VerticalAlignment::Center);
        more.Margin(mux::Thickness{0, 0, 40, 0});
        more.OpacityTransition(mux::ScalarTransition());
        row.Children().Append(more);
    }

    section.headerButton = button;
    section.chevron = chevron;
    section.chevronUp = up;
    section.chevronDown = down;
    section.more = more ? more.as<mux::UIElement>() : mux::UIElement{nullptr};
    SidebarSetChevron(section);

    std::wstring key = section.key;
    auto setHovered = [state, key](bool hovered) {
        if (state->sidebar) {
            for (auto& s : state->sidebar->sections) {
                if (s.key == key) {
                    s.hovered = hovered;
                    SidebarSetChevron(s);
                }
            }
        }
    };
    Keep(row, row.PointerEntered(winrt::auto_revoke, [setHovered](Inspectable const&, mux::Input::PointerRoutedEventArgs const&) { setHovered(true); }));
    Keep(row, row.PointerExited(winrt::auto_revoke, [setHovered](Inspectable const&, mux::Input::PointerRoutedEventArgs const&) { setHovered(false); }));
    for (muxc::Button focusable : {button, chevron, more}) {
        if (!focusable) {
            continue;
        }
        Keep(focusable, focusable.GotFocus(winrt::auto_revoke, [setHovered](Inspectable const&, mux::RoutedEventArgs const&) { setHovered(true); }));
        Keep(focusable, focusable.LostFocus(winrt::auto_revoke, [setHovered](Inspectable const&, mux::RoutedEventArgs const&) { setHovered(false); }));
    }
    Keep(button, button.Click(winrt::auto_revoke, [onToggle](Inspectable const&, mux::RoutedEventArgs const&) { onToggle(); }));
    Keep(chevron, chevron.Click(winrt::auto_revoke, [onToggle](Inspectable const&, mux::RoutedEventArgs const&) { onToggle(); }));
    return row;
}

// Collapses or expands a section: the rows fade (and slide a little), then the layout follows; expanding shows the layout first.
void SidebarToggleSection(WindowState* state, std::wstring const& key) {
    SidebarPanel* panel = state->sidebar;
    if (!panel) {
        return;
    }
    for (auto& section : panel->sections) {
        if (section.key != key) {
            continue;
        }
        section.collapsed = !section.collapsed;
        SidebarSetSectionCollapsed(key, section.collapsed);
        SidebarSetChevron(section);
        auto body = section.body;
        if (section.collapsed) {
            body.Opacity(0);
            body.Translation(winrt::Windows::Foundation::Numerics::float3{0, -8, 0});
            if (section.collapseTimer) {
                section.collapseTimer.Stop();
            }
            auto timer = state->queue.CreateTimer();
            timer.Interval(std::chrono::milliseconds(kSidebarCollapseMs));
            timer.IsRepeating(false);
            Keep(timer, timer.Tick(winrt::auto_revoke, [state, key, body](mud::DispatcherQueueTimer const& sender, Inspectable const&) {
                sender.Stop();
                if (state->sidebar) {
                    for (auto& s : state->sidebar->sections) {
                        if (s.key == key && s.collapsed) {
                            body.Visibility(mux::Visibility::Collapsed);
                        }
                    }
                }
            }));
            section.collapseTimer = timer;
            timer.Start();
        } else {
            if (section.collapseTimer) {
                section.collapseTimer.Stop();
            }
            body.Visibility(mux::Visibility::Visible);
            body.Opacity(1);
            body.Translation(winrt::Windows::Foundation::Numerics::float3{0, 0, 0});
        }
        return;
    }
}

muxc::StackPanel SidebarMakeBody(std::wstring const& key, bool collapsed) {
    muxc::StackPanel body;
    body.Orientation(muxc::Orientation::Vertical);
    body.Spacing(2);  // A gap between the rows, so that a selected row and a hovered one are not one block.
    body.OpacityTransition(mux::ScalarTransition());
    body.TranslationTransition(mux::Vector3Transition());
    body.Opacity(collapsed ? 0 : 1);
    body.Visibility(collapsed ? mux::Visibility::Collapsed : mux::Visibility::Visible);
    mux::Automation::AutomationProperties::SetAutomationId(body, L"SidebarBody." + key);
    return body;
}

void SidebarBuild(WindowState* state, SidebarPanel* panel) {
    muxc::StackPanel host;
    host.Orientation(muxc::Orientation::Vertical);
    host.Padding(mux::Thickness{2, 8, 2, 8});
    host.Spacing(6);
    host.XYFocusKeyboardNavigation(mux::Input::XYFocusKeyboardNavigationMode::Enabled);  // The arrows move from row to row.
    {
        // Keys the rows do not take themselves (Delete, F2, Ctrl+arrows): seen also when a row has handled the key.
        mux::Input::KeyEventHandler keys([state](Inspectable const&, mux::Input::KeyRoutedEventArgs const& args) {
            try {
                SidebarKeyDown(state, args);
            } catch (...) {
                LogHresult(L"a key in the sidebar");
            }
        });
        AddRouted(host, mux::UIElement::KeyDownEvent(), winrt::box_value(keys));
    }
    panel->host = host;
    panel->dark = LabelsDarkTheme();

    struct Spec {
        const wchar_t* key;
        const wchar_t* title;
    };
    for (Spec spec : {Spec{L"favorites", L"sidebar.favorites"}, Spec{L"locations", L"sidebar.locations"}, Spec{L"tags", L"sidebar.tags"}}) {
        panel->sections.emplace_back();
        auto& section = panel->sections.back();
        section.key = spec.key;
        section.collapsed = SidebarSectionCollapsed(section.key);
    }
    for (size_t i = 0; i < panel->sections.size(); i++) {
        auto& section = panel->sections[i];
        const wchar_t* titleId = i == 0 ? L"sidebar.favorites" : i == 1 ? L"sidebar.locations" : L"sidebar.tags";
        muxc::StackPanel block;
        block.Orientation(muxc::Orientation::Vertical);
        std::wstring key = section.key;
        muxc::Button more{nullptr};
        if (i == 1) {
            more = SidebarLocationsMore(state, panel);
        } else if (i == 2) {
            more = SidebarTagsMore(state, panel);
        }
        if (i > 0) {
            // A thin line between the sections, at the top of the block that follows (it goes with the block when that is hidden).
            auto divider = SidebarThemedBorder(L"DividerStrokeColorDefaultBrush");
            divider.Height(1);
            divider.Margin(mux::Thickness{12, 8, 12, 8});
            divider.IsHitTestVisible(false);
            block.Children().Append(divider);
        }
        block.Children().Append(SidebarMakeHeaderRow(state, section, Str(titleId), [state, key]() { SidebarToggleSection(state, key); }, more));
        if (i == 2) {
            block.Children().Append(SidebarTagsProgress(state, panel));
        }
        section.body = SidebarMakeBody(section.key, section.collapsed);
        block.Children().Append(section.body);
        if (i == 0) {
            // The Favorites block sits in a grid with the outline that shows while a folder is dragged over it.
            muxc::Grid favGrid;
            auto outline = SidebarThemedBorder(L"SubtleFillColorSecondaryBrush");
            outline.CornerRadius(mux::CornerRadius{4, 4, 4, 4});
            outline.IsHitTestVisible(false);
            outline.Opacity(0);
            outline.OpacityTransition(mux::ScalarTransition());
            favGrid.Children().Append(outline);
            favGrid.Children().Append(block);
            auto insertLine = SidebarThemedBorder(L"AccentFillColorDefaultBrush");
            insertLine.Height(2);
            insertLine.CornerRadius(mux::CornerRadius{1, 1, 1, 1});
            insertLine.VerticalAlignment(mux::VerticalAlignment::Top);
            insertLine.IsHitTestVisible(false);
            insertLine.Visibility(mux::Visibility::Collapsed);
            favGrid.Children().Append(insertLine);
            host.Children().Append(favGrid);
            panel->favBlock = favGrid;
            panel->favHighlight = outline;
            panel->favLine = insertLine;
            SidebarFillFavorites(state, panel);
        } else if (i == 2) {
            // The Tags block in a grid with the line that shows where a dragged tag row goes (manual order).
            muxc::Grid tagGrid;
            tagGrid.Children().Append(block);
            auto insertLine = SidebarThemedBorder(L"AccentFillColorDefaultBrush");
            insertLine.Height(2);
            insertLine.CornerRadius(mux::CornerRadius{1, 1, 1, 1});
            insertLine.VerticalAlignment(mux::VerticalAlignment::Top);
            insertLine.IsHitTestVisible(false);
            insertLine.Visibility(mux::Visibility::Collapsed);
            tagGrid.Children().Append(insertLine);
            host.Children().Append(tagGrid);
            panel->tagsBlock = tagGrid;
            panel->tagLine = insertLine;
            tagGrid.Visibility(g_labels ? mux::Visibility::Visible : mux::Visibility::Collapsed);
        } else {
            host.Children().Append(block);
        }
        if (i == 1) {
            SidebarFillLocations(state, panel);
        } else if (i == 2) {
            SidebarFillTags(state, panel);
        }
    }

    muxc::ScrollViewer scroller;
    scroller.VerticalScrollBarVisibility(muxc::ScrollBarVisibility::Auto);
    scroller.HorizontalScrollBarVisibility(muxc::ScrollBarVisibility::Disabled);
    auto pill = SidebarThemedBorder(L"AccentFillColorDefaultBrush");
    pill.Width(3);
    pill.Height(16);
    pill.CornerRadius(mux::CornerRadius{1.5, 1.5, 1.5, 1.5});
    pill.HorizontalAlignment(mux::HorizontalAlignment::Left);
    pill.VerticalAlignment(mux::VerticalAlignment::Top);
    pill.Margin(mux::Thickness{4, 0, 0, 0});
    pill.Opacity(0);
    pill.OpacityTransition(mux::ScalarTransition());
    {
        mux::Vector3Transition slide;
        slide.Duration(std::chrono::milliseconds(140));
        pill.TranslationTransition(slide);
    }
    muxc::Grid pillHost;
    pillHost.Children().Append(host);
    pillHost.Children().Append(pill);
    panel->pillHost = pillHost;
    panel->pill = pill;
    scroller.Content(pillHost);
    muxc::Grid root;
    auto background = LoadXaml(std::wstring(L"<Grid ") + kXamlNs + L" Background='{ThemeResource SolidBackgroundFillColorBaseBrush}'/>").as<muxc::Grid>();
    background.Children().Append(scroller);
    root.Children().Append(background);
    muxc::Canvas overlay;
    overlay.IsHitTestVisible(false);
    root.Children().Append(overlay);
    panel->overlay = overlay;

    auto source = mux::Hosting::DesktopWindowXamlSource();
    source.Initialize(winrt::Microsoft::UI::WindowId{(uint64_t)state->topLevel});
    source.Content(root);
    panel->source = source;
    panel->bridge = source.SiteBridge();
    g_sidebarWindowLocal = (HWND)panel->bridge.WindowId().Value;
    SidebarRegisterDrop(state, panel);
    panel->built = true;
}

BOOL CALLBACK SidebarFindPaneProc(HWND hwnd, LPARAM lParam) {
    wchar_t name[64] = L"";
    GetClassNameW(hwnd, name, ARRAYSIZE(name));
    if (IsWindowVisible(hwnd) && wcscmp(name, L"NamespaceTreeControl") == 0) {
        *(HWND*)lParam = hwnd;
        return FALSE;
    }
    return TRUE;
}

// The rectangle of the visible navigation pane, in the window's client coordinates. False when none is visible.
bool SidebarPaneRect(WindowState* state, SidebarPanel* panel, RECT* rect) {
    HWND found = nullptr;
    EnumChildWindows(state->topLevel, SidebarFindPaneProc, (LPARAM)&found);
    if (!found || !GetWindowRect(found, rect)) {
        return false;
    }
    MapWindowPoints(nullptr, state->topLevel, (POINT*)rect, 2);
    panel->pane = found;
    return rect->right > rect->left && rect->bottom > rect->top;
}

void SidebarPlace(SidebarPanel* panel, RECT const& r) {
    panel->bridge.MoveAndResize(winrt::Windows::Graphics::RectInt32{r.left, r.top, r.right - r.left, r.bottom - r.top});
    panel->bridge.Show();
    SetWindowPos((HWND)panel->bridge.WindowId().Value, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    panel->rect = r;
    panel->shown = true;
}

void SidebarRefreshTheme(SidebarPanel* panel) {
    bool dark = LabelsDarkTheme();
    if (dark == panel->dark) {
        return;
    }
    panel->dark = dark;
    for (auto& row : panel->tagRows) {
        if (auto host = row.icon.get()) {
            host.Children().Clear();
            host.Children().Append(SidebarTagIcon(row.colour, dark));
        }
    }
}

// Closes the flyouts that are open in the sidebar. A click in another island (a column) never reaches this one, so XAML does
// not dismiss them by itself.
void SidebarDismissPopups(WindowState* state) {
    SidebarPanel* panel = state ? state->sidebar : nullptr;
    if (!panel || !panel->host) {
        return;
    }
    try {
        auto root = panel->host.XamlRoot();
        if (!root) {
            return;
        }
        for (auto const& popup : mux::Media::VisualTreeHelper::GetOpenPopupsForXamlRoot(root)) {
            popup.IsOpen(false);
        }
    } catch (...) {
        LogHresult(L"closing the sidebar's flyouts");
    }
}

void SidebarTick() {
    if (g_unloading) {
        return;
    }
    WindowState* state = StateForThisThread(false);
    if (!state || !state->sidebar) {
        return;
    }
    SidebarPanel* panel = state->sidebar;
    if (!state->topLevel || !IsWindow(state->topLevel)) {
        AbandonSidebar(state);
        return;
    }
    try {
        panel->ticks++;
        if (panel->settingsVersion != g_sidebarSettingsVersion) {
            // A setting of the sidebar changed: it is made again (after this tick, never from inside its own timer).
            panel->settingsVersion = g_sidebarSettingsVersion;
            if (state->queue) {
                state->queue.TryEnqueue([state]() {
                    if (!g_unloading && StateForThisThread(false) == state && state->sidebar) {
                        ReleaseSidebar(state);
                        StartSidebar(state);
                    }
                });
            }
            return;
        }
        if (!g_sidebar) {
            if (panel->shown && panel->bridge) {
                panel->bridge.Hide();
                panel->shown = false;
            }
            return;
        }
        if (!panel->built) {
            SidebarBuild(state, panel);
        }
        RECT r{};
        if (!SidebarPaneRect(state, panel, &r)) {
            if (panel->shown) {
                panel->bridge.Hide();  // The pane is hidden (View, Show, Navigation pane): so is the sidebar.
                panel->shown = false;
            }
            return;
        }
        if (!panel->shown || memcmp(&r, &panel->rect, sizeof(r)) != 0) {
            SidebarPlace(panel, r);
        }
        if (panel->ticks % 10 == 0) {
            SidebarRefreshTheme(panel);
        }
        if (panel->tagsBlock) {
            // Labels switched off: no Tags section, and no tag list left open.
            auto wanted = g_labels ? mux::Visibility::Visible : mux::Visibility::Collapsed;
            if (panel->tagsBlock.Visibility() != wanted) {
                panel->tagsBlock.Visibility(wanted);
            }
            if (!g_labels) {
                TagViewLeave(state);
            } else {
                TagIndexStart();  // Labels turned on while the sidebar is open (does nothing once started).
            }
        }
        TagViewTick(state);
        if (panel->ticks % 3 == 0) {
            FavoritesTick(state, panel);
            TagsTick(state, panel);
            SidebarFollowFolder(state, panel);
        }
    } catch (winrt::hresult_error const& error) {
        static int logged = 0;
        if (logged++ < 5) {
            Wh_Log(L"the sidebar's tick threw hr=0x%08X: %s", (unsigned)error.code().value, error.message().c_str());
        }
    } catch (...) {
        static int logged = 0;
        if (logged++ < 5) {
            LogHresult(L"the sidebar's tick");
        }
    }
}

}  // namespace

void StartSidebar(WindowState* state) {
    if (state->sidebar || !state->queue || !g_sidebar) {
        return;
    }
    try {
        auto* panel = new SidebarPanel();
        panel->settingsVersion = g_sidebarSettingsVersion;
        state->sidebar = panel;
        auto timer = state->queue.CreateTimer();
        timer.Interval(std::chrono::milliseconds(60));
        panel->token = timer.Tick([](mud::DispatcherQueueTimer const&, Inspectable const&) { SidebarTick(); });
        timer.Start();
        panel->timer = timer;
        if (g_labels) {
            TagIndexStart();  // Labels off: no tags to list, so no walk of the drives.
        }
        FavoritesWatchStart();
        Wh_Log(L"sidebar: watching window %p", state->topLevel);
    } catch (...) {
        LogHresult(L"StartSidebar");
    }
}

// The mod unloads or the window goes: everything is released, on the window's thread.
void ReleaseSidebar(WindowState* state) {
    SidebarPanel* panel = state->sidebar;
    if (!panel) {
        return;
    }
    state->sidebar = nullptr;
    g_sidebarWindowLocal = nullptr;
    ReleaseTagView(state);
    try {
        if (panel->timer) {
            panel->timer.Stop();
            panel->timer.Tick(panel->token);
        }
        panel->timer = nullptr;
        for (auto& section : panel->sections) {
            if (section.collapseTimer) {
                section.collapseTimer.Stop();
                section.collapseTimer = nullptr;
            }
        }
        panel->sections.clear();
        panel->tagRows.clear();
        SidebarRevokeDrop(panel);
        panel->locationMarks.clear();
        panel->favBlock = nullptr;
        panel->favHighlight = nullptr;
        panel->favLine = nullptr;
        panel->overlay = nullptr;
        panel->tagsBlock = nullptr;
        panel->tagLine = nullptr;
        panel->ghostPopup = nullptr;
        panel->tagProgress = nullptr;
        panel->tagBar = nullptr;
        if (panel->tagJob) {
            panel->tagJob->cancel = true;
            panel->tagJob.reset();
        }
        TagsReleaseThreadObjects();
        panel->pill = nullptr;
        panel->pillHost = nullptr;
        panel->locationsMenu = nullptr;
        panel->driveChildren = nullptr;
        panel->host = nullptr;
        if (panel->bridge) {
            panel->bridge.Hide();
        }
        if (panel->source) {
            panel->source.Content(nullptr);
            panel->source.Close();
        }
    } catch (...) {
        LogHresult(L"releasing the sidebar");
    }
    delete panel;
}

// The window closed: the objects are let go, never released (they belong to a window that is gone).
void AbandonSidebar(WindowState* state) {
    SidebarPanel* panel = state->sidebar;
    if (!panel) {
        return;
    }
    state->sidebar = nullptr;
    g_sidebarWindowLocal = nullptr;
    AbandonTagView(state);
    try {
        if (panel->timer) {
            panel->timer.Stop();
            panel->timer.Tick(panel->token);
        }
    } catch (...) {
    }
    (void)winrt::detach_abi(panel->timer);
    (void)winrt::detach_abi(panel->source);
    (void)winrt::detach_abi(panel->bridge);
    (void)winrt::detach_abi(panel->host);
    (void)winrt::detach_abi(panel->favBlock);
    (void)winrt::detach_abi(panel->favHighlight);
    (void)winrt::detach_abi(panel->favLine);
    (void)winrt::detach_abi(panel->overlay);
    (void)winrt::detach_abi(panel->tagsBlock);
    (void)winrt::detach_abi(panel->tagLine);
    (void)winrt::detach_abi(panel->ghostPopup);
    (void)winrt::detach_abi(panel->tagProgress);
    (void)winrt::detach_abi(panel->tagBar);
    if (panel->tagJob) {
        panel->tagJob->cancel = true;
    }
    (void)winrt::detach_abi(panel->pill);
    (void)winrt::detach_abi(panel->pillHost);
    (void)winrt::detach_abi(panel->locationsMenu);
    (void)winrt::detach_abi(panel->driveChildren);
    for (auto& mark : panel->locationMarks) {
        (void)winrt::detach_abi(mark.selected);
        (void)winrt::detach_abi(mark.pill);
        (void)winrt::detach_abi(mark.button);
    }
    for (auto& section : panel->sections) {
        (void)winrt::detach_abi(section.body);
        (void)winrt::detach_abi(section.collapseTimer);
        (void)winrt::detach_abi(section.chevron);
        (void)winrt::detach_abi(section.headerButton);
        (void)winrt::detach_abi(section.chevronUp);
        (void)winrt::detach_abi(section.chevronDown);
    }
    // The panel itself is leaked on purpose, like the details pane's.
}
