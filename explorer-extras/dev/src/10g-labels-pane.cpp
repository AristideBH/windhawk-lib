// Labels in the details pane: a "Tags" section with one row per tag of the selection, a [+] button that opens a
// small flyout (the colours, and a name that is optional), and an inline line when a tag cannot be written.
// What it does with the tags is plain code at the top, tested by the self-test; the XAML is below.

struct LabelsMerged {
    std::vector<std::pair<LabelTag, bool>> tags;  // Each tag once; true when every selected item has it.
};

static bool LabelsSameTag(LabelTag const& a, LabelTag const& b) {
    return a.colour == b.colour && _wcsicmp(a.name.c_str(), b.name.c_str()) == 0;
}

// The tags of several items as one list.
LabelsMerged LabelsMerge(std::vector<std::vector<LabelTag>> const& perItem) {
    LabelsMerged merged;
    for (auto const& item : perItem) {
        for (auto const& tag : item) {
            bool known = false;
            for (auto const& entry : merged.tags) {
                known = known || LabelsSameTag(entry.first, tag);
            }
            if (!known) {
                merged.tags.push_back({tag, true});
            }
        }
    }
    for (auto& entry : merged.tags) {
        for (auto const& item : perItem) {
            bool has = false;
            for (auto const& tag : item) {
                has = has || LabelsSameTag(entry.first, tag);
            }
            if (!has) {
                entry.second = false;
                break;
            }
        }
    }
    return merged;
}

// False when the list is full (16) and the tag is not in it yet.
bool LabelsAddTag(std::vector<LabelTag>* tags, LabelTag const& tag) {
    for (auto const& existing : *tags) {
        if (LabelsSameTag(existing, tag)) {
            return true;
        }
    }
    if (tags->size() >= kLabelsMaxTags) {
        return false;
    }
    tags->push_back(tag);
    return true;
}

void LabelsRemoveTag(std::vector<LabelTag>* tags, LabelTag const& tag) {
    tags->erase(std::remove_if(tags->begin(), tags->end(), [&](LabelTag const& t) { return LabelsSameTag(t, tag); }), tags->end());
}

struct LabelsApplyResult {
    int done = 0;                          // Items that were written.
    LabelsWrite worst = LabelsWrite::Ok;   // The last write that failed.
    std::wstring failedName;               // The item that failed, a full one or an unreadable one.
    bool full = false;                     // An item already has 16 tags.
    bool unreadable = false;               // An item's stream is corrupt or from a newer version: left alone.
};

// What a failed read means for the report: a stream that cannot be understood, a file in use, no access to it.
void LabelsNoteReadFailure(LabelsApplyResult* result, LabelsReadResult read, std::wstring const& fileName) {
    result->failedName = fileName;
    if (read == LabelsReadResult::Locked) {
        result->worst = LabelsWrite::Locked;
    } else if (read == LabelsReadResult::NoAccess) {
        result->worst = LabelsWrite::ReadOnly;
    } else if (read == LabelsReadResult::Failed) {
        result->worst = LabelsWrite::Failed;
    } else {
        result->unreadable = true;  // Corrupt, or from a newer version: left alone.
    }
}

// Adds or removes one tag on every item. Items whose tags would not change are not written. The cache and the
// list are told.
LabelsApplyResult LabelsApplyToSelection(std::vector<std::wstring> const& paths, LabelTag const& tag, bool add) {
    LabelsApplyResult result;
    for (auto const& path : paths) {
        std::vector<LabelTag> tags;
        std::wstring fileName = PathFindFileNameW(path.c_str());
        LabelsReadResult read = LabelsReadFileEx(path, &tags);
        if (read != LabelsReadResult::Ok) {
            LabelsNoteReadFailure(&result, read, fileName);
            continue;
        }
        std::wstring before = LabelsSerialize(tags);
        if (add) {
            if (!LabelsAddTag(&tags, tag)) {
                result.full = true;
                result.failedName = fileName;
                continue;
            }
        } else {
            LabelsRemoveTag(&tags, tag);
        }
        if (LabelsSerialize(tags) == before) {
            continue;
        }
        LabelsWrite written = LabelsWriteFile(path, tags);
        if (written == LabelsWrite::Ok) {
            result.done++;
            LabelsSetTags(path, tags);
        } else {
            result.worst = written;
            result.failedName = fileName;
        }
    }
    if (result.done > 0) {
        LabelsRepaintWindows(std::wstring());
    }
    return result;
}

// Removes every tag from every item. Items without tags are not written.
LabelsApplyResult LabelsClearSelection(std::vector<std::wstring> const& paths) {
    LabelsApplyResult result;
    for (auto const& path : paths) {
        std::vector<LabelTag> tags;
        std::wstring fileName = PathFindFileNameW(path.c_str());
        LabelsReadResult read = LabelsReadFileEx(path, &tags);
        if (read != LabelsReadResult::Ok) {
            LabelsNoteReadFailure(&result, read, fileName);
            continue;
        }
        if (tags.empty()) {
            continue;
        }
        LabelsWrite written = LabelsWriteFile(path, {});
        if (written == LabelsWrite::Ok) {
            result.done++;
            LabelsSetTags(path, {});
        } else {
            result.worst = written;
            result.failedName = fileName;
        }
    }
    if (result.done > 0) {
        LabelsRepaintWindows(std::wstring());
    }
    return result;
}

// Reads from the disk on a mod thread and hands the result to `apply` on this UI thread, so a slow or offline share never
// freezes the window. `apply` must hold weak references only: it is copied and freed on the mod thread. False when the mod is
// unloading, and then `apply` never runs.
template <typename T>
bool LabelsReadAsync(std::function<T()> read, std::function<void(T const&)> apply) {
    auto queue = mud::DispatcherQueue::GetForCurrentThread();
    return StartModThread([queue, read, apply]() {
        HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        auto result = std::make_shared<T>(read());
        if (SUCCEEDED(init)) {
            CoUninitialize();
        }
        if (g_unloading) {
            return;
        }
        try {
            queue.TryEnqueue([result, apply]() {
                if (!g_unloading) {
                    apply(*result);
                }
            });
        } catch (...) {
        }
    });
}

// Runs `work` on a mod thread and hands its result to `done`, on that thread. False when the mod is unloading.
bool LabelsRunAsync(std::function<LabelsApplyResult()> work, std::function<void(LabelsApplyResult const&)> done) {
    return StartModThread([work, done]() {
        HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        LabelsApplyResult result = work();
        if (SUCCEEDED(init)) {
            CoUninitialize();
        }
        if (!g_unloading) {
            done(result);
        }
    });
}

namespace {

std::wstring LabelsFailText(LabelsApplyResult const& result) {
    const wchar_t* key = L"labels.fail.failed";
    if (result.worst == LabelsWrite::ReadOnly) {
        key = L"labels.fail.readonly";
    } else if (result.worst == LabelsWrite::Locked) {
        key = L"labels.fail.locked";
    } else if (result.worst == LabelsWrite::Unsupported) {
        key = L"labels.fail.ntfs";
    } else if (result.worst == LabelsWrite::Ok) {
        key = result.full ? L"labels.fail.full" : L"labels.fail.corrupt";
    }
    wchar_t text[400];
    swprintf_s(text, Str(key), result.failedName.c_str());
    return text;
}

std::wstring LabelsTrim(std::wstring text) {
    while (!text.empty() && iswspace(text.back())) {
        text.pop_back();
    }
    size_t start = 0;
    while (start < text.size() && iswspace(text[start])) {
        start++;
    }
    return text.substr(start);
}

using LabelsPaths = std::shared_ptr<std::vector<std::wstring>>;

// The fill of a tag's badge: the colour of the theme, at about a third of full strength, so the text keeps its
// own colour and reads in both themes.
std::wstring LabelsPillFill(int colour, bool dark) {
    COLORREF fill = LabelsColourRgb(colour, dark);
    wchar_t text[16];
    swprintf_s(text, L"#59%02X%02X%02X", GetRValue(fill), GetGValue(fill), GetBValue(fill));
    return text;
}

std::wstring LabelsXmlEscape(std::wstring const& text) {
    std::wstring result;
    for (wchar_t c : text) {
        switch (c) {
            case L'&': result += L"&amp;"; break;
            case L'<': result += L"&lt;"; break;
            case L'>': result += L"&gt;"; break;
            case L'"': result += L"&quot;"; break;
            case L'\'': result += L"&apos;"; break;
            default: result += c;
        }
    }
    return result;
}

// A coloured dot as XAML: the fill, and a rim a little darker.
mux::FrameworkElement LabelsDotElement(int colour, int size) {
    bool dark = LabelsDarkTheme();
    COLORREF fill = LabelsDotRgb(colour, dark);
    auto toward = [dark](int channel) { return dark ? channel * 82 / 100 : channel + (255 - channel) * 35 / 100; };
    COLORREF rim = RGB(toward(GetRValue(fill)), toward(GetGValue(fill)), toward(GetBValue(fill)));
    wchar_t markup[400];
    swprintf_s(markup,
               L"<Ellipse xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' Width='%d' Height='%d' "
               L"Fill='#FF%02X%02X%02X' Stroke='#FF%02X%02X%02X' StrokeThickness='1' VerticalAlignment='Center'/>",
               size, size, GetRValue(fill), GetGValue(fill), GetBValue(fill), GetRValue(rim), GetGValue(rim), GetBValue(rim));
    return LoadXaml(markup).as<mux::FrameworkElement>();
}

void LabelsFillSection(muxc::StackPanel const& host, LabelsPaths const& paths, std::wstring const& message);

struct LabelsSectionRef {
    winrt::weak_ref<muxc::StackPanel> host;
    LabelsPaths paths;
};
[[clang::no_destroy]] thread_local LabelsSectionRef t_labelsSection;  // The Tags section of this window's pane.
[[clang::no_destroy]] thread_local muxc::Flyout t_labelsNotice{nullptr};  // A notice when there is no pane to show it in.

// After a change: what went wrong, if anything, is shown in the pane's Tags section, which is filled again a moment
// later (not from inside the click that asked). Without a pane the notice is a small flyout at `anchor`.
void LabelsAfterChange(winrt::weak_ref<muxc::StackPanel> const& weakHost, LabelsPaths const& paths,
                       LabelsApplyResult const& result, std::function<mux::FrameworkElement()> const& anchor) {
    if (g_unloading) {
        return;
    }
    std::wstring message;
    if (result.worst != LabelsWrite::Ok || result.full || result.unreadable) {
        message = LabelsFailText(result);
    }
    WindowState* state = StateForThisThread(false);
    auto host = weakHost.get();
    if (host && state && state->details) {
        PaneAfter(state, state->details, 30, [weakHost, paths, message]() {
            if (auto shown = weakHost.get()) {
                LabelsFillSection(shown, paths, message);
            }
        });
        if (!message.empty()) {
            PaneAfter(state, state->details, 6000, [weakHost, paths]() {
                if (auto shown = weakHost.get()) {
                    LabelsFillSection(shown, paths, std::wstring());  // The line fades.
                }
            });
        }
        return;
    }
    if (!message.empty() && anchor) {
        mux::FrameworkElement target = anchor();
        if (target) {
            t_labelsNotice = muxc::Flyout();
            t_labelsNotice.Content(MakeText(message, 13, false, false));
            Keep(t_labelsNotice, t_labelsNotice.Closed(winrt::auto_revoke, [](Inspectable const&, Inspectable const&) {
                t_labelsNotice = nullptr;
            }));
            t_labelsNotice.ShowAt(target);
        }
    }
}

// Applies one change. It is written on a mod thread, so the window does not wait for the disk.
void LabelsDo(winrt::weak_ref<muxc::StackPanel> const& weakHost, LabelsPaths const& paths, LabelTag const& tag, bool add) {
    if (g_unloading) {
        return;
    }
    if (add) {
        LabelsRecentRemember(tag);  // A named tag is offered again in the Add a tag flyout.
    }
    {
        auto queue = mud::DispatcherQueue::GetForCurrentThread();
        bool started = LabelsRunAsync([paths, tag, add]() { return LabelsApplyToSelection(*paths, tag, add); },
                                      [queue, weakHost, paths](LabelsApplyResult const& result) {
                                          queue.TryEnqueue([weakHost, paths, result]() {
                                              LabelsAfterChange(weakHost, paths, result, nullptr);
                                          });
                                      });
        if (started) {
            return;
        }
    }
    LabelsAfterChange(weakHost, paths, LabelsApplyToSelection(*paths, tag, add), nullptr);
}

// The flyout of the [+] button: the colours, and a name that is optional. A colour makes the tag at once, with the
// name typed so far; Enter in the box makes a named tag in the neutral gray.
muxc::Flyout LabelsMakeFlyout(winrt::weak_ref<muxc::StackPanel> const& weakHost, LabelsPaths const& paths) {
    muxc::Flyout flyout;
    muxc::StackPanel content;
    content.Spacing(10);
    muxc::StackPanel colours;
    colours.Orientation(muxc::Orientation::Horizontal);
    colours.Spacing(4);
    auto box = LoadXaml(std::wstring(L"<TextBox ") + kXamlNs + L" MaxLength='40' MinWidth='200'/>").as<muxc::TextBox>();
    box.PlaceholderText(Str(L"labels.name"));
    auto weakFlyout = winrt::make_weak(flyout);
    for (int colour = 0; colour < 8; colour++) {
        auto button = LoadXaml(std::wstring(L"<Button ") + kXamlNs +
                               L" Padding='5' MinHeight='0' MinWidth='0' Background='Transparent' BorderThickness='0'/>").as<muxc::Button>();
        button.Content(LabelsDotElement(colour, 18));
        std::wstring colourName = LabelsColourName(colour);
        if (!colourName.empty()) {
            muxc::ToolTipService::SetToolTip(button, winrt::box_value(winrt::hstring{colourName}));
        }
        Keep(button, button.Click(winrt::auto_revoke, [weakHost, paths, weakFlyout, box, colour](Inspectable const&, mux::RoutedEventArgs const&) {
            std::wstring name = LabelsTrim(std::wstring(box.Text()));
            if (auto shown = weakFlyout.get()) {
                shown.Hide();
            }
            LabelsDo(weakHost, paths, LabelTag{(uint8_t)colour, name}, true);
        }));
        colours.Children().Append(button);
    }
    Keep(box, box.KeyDown(winrt::auto_revoke, [weakHost, paths, weakFlyout](Inspectable const& sender, mux::Input::KeyRoutedEventArgs const& args) {
        using winrt::Windows::System::VirtualKey;
        if (args.Key() != VirtualKey::Enter) {
            return;
        }
        auto text = sender.try_as<muxc::TextBox>();
        std::wstring name = text ? LabelsTrim(std::wstring(text.Text())) : std::wstring();
        if (name.empty()) {
            return;
        }
        args.Handled(true);
        if (auto shown = weakFlyout.get()) {
            shown.Hide();
        }
        LabelsDo(weakHost, paths, LabelTag{0, name}, true);
    }));
    content.Children().Append(colours);
    content.Children().Append(box);
    // The tags named lately: one click puts the colour and the name.
    auto recent = LabelsRecentGet();
    if (!recent.empty()) {
        content.Children().Append(MakeText(Str(L"labels.recent"), 12, false, true));
        muxc::StackPanel list;
        list.Spacing(2);
        for (auto const& tag : recent) {
            auto button = LoadXaml(std::wstring(L"<Button ") + kXamlNs +
                                   L" HorizontalAlignment='Stretch' HorizontalContentAlignment='Left' Padding='6,3' MinHeight='0' "
                                   L"MinWidth='0' Background='Transparent' BorderThickness='0'/>").as<muxc::Button>();
            muxc::StackPanel row;
            row.Orientation(muxc::Orientation::Horizontal);
            row.Spacing(8);
            row.Children().Append(LabelsDotElement(tag.colour, 12));
            row.Children().Append(MakeText(tag.name, 13, false, false));
            button.Content(row);
            Keep(button, button.Click(winrt::auto_revoke, [weakHost, paths, weakFlyout, tag](Inspectable const&, mux::RoutedEventArgs const&) {
                if (auto shown = weakFlyout.get()) {
                    shown.Hide();
                }
                LabelsDo(weakHost, paths, tag, true);
            }));
            list.Children().Append(button);
        }
        content.Children().Append(list);
    }
    flyout.Content(content);
    return flyout;
}

// What the Tags section shows, read from the disk (any thread).
struct LabelsSectionRead {
    bool supported = false;
    std::vector<std::vector<LabelTag>> perItem;
    bool unreadable = false;
    LabelsApplyResult unreadableWhy;  // The first item that could not be read, and why.
    bool ambiguous = false;
};

LabelsSectionRead LabelsReadSection(LabelsPaths const& paths) {
    LabelsSectionRead out;
    out.supported = !paths->empty() && LabelsVolumeSupportsStreams((*paths)[0]);
    if (!out.supported) {
        return out;
    }
    // The first 100 items give the badges (a bigger selection is not read item by item on every change).
    std::shared_ptr<const LabelsFolder> data;
    for (size_t i = 0; i < paths->size() && i < 100; i++) {
        std::vector<LabelTag> tags;
        LabelsReadResult read = LabelsReadFileEx((*paths)[i], &tags);
        if (read != LabelsReadResult::Ok) {
            if (!out.unreadable) {
                LabelsNoteReadFailure(&out.unreadableWhy, read, PathFindFileNameW((*paths)[i].c_str()));
            }
            out.unreadable = true;
            tags.clear();
        }
        out.perItem.push_back(std::move(tags));
        if (i < 20 && !out.ambiguous) {
            // An item that shares its displayed name with another one has no dot: the pane says so.
            if (!data) {
                std::wstring folder = (*paths)[i];
                PathRemoveFileSpecW(folder.data());
                folder.resize(wcslen(folder.c_str()));
                data = LabelsFolderFor(folder);
            }
            out.ambiguous = data->complete && data->ambiguous.count(LabelsDisplayNameOf((*paths)[i])) != 0;
        }
    }
    return out;
}

// Builds the section: the title, the tags as badges that wrap like text, with the "Add a tag" link after the last
// one, and the line of a failed write. A click on a badge opens a small menu (remove, or add to all).
void LabelsBuildSection(muxc::StackPanel const& host, LabelsPaths const& paths, std::wstring const& message,
                        LabelsSectionRead const& read) {
    host.Children().Clear();
    auto weakHost = winrt::make_weak(host);
    bool supported = read.supported;
    std::vector<std::vector<LabelTag>> const& perItem = read.perItem;
    bool unreadable = read.unreadable;
    LabelsApplyResult unreadableWhy = read.unreadableWhy;
    bool ambiguous = read.ambiguous;
    LabelsMerged merged = LabelsMerge(perItem);
    host.Children().Append(MakeText(Str(L"labels.tags"), 13, true, false));
    if (!supported) {
        host.Children().Append(MakeText(Str(L"labels.ntfs"), 12, false, true));
        return;
    }

    bool dark = LabelsDarkTheme();
    std::wstring markup = std::wstring(L"<RichTextBlock ") + kXamlNs +
                          L" xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' TextWrapping='Wrap' "
                          L"IsTextSelectionEnabled='False'><Paragraph>";
    for (size_t i = 0; i < merged.tags.size(); i++) {
        LabelTag const& tag = merged.tags[i].first;
        std::wstring shown = tag.name.empty() ? LabelsColourName(tag.colour) : tag.name;
        markup += L"<InlineUIContainer><Border x:Name='pill" + std::to_wstring(i) + L"' Background='" +
                  LabelsPillFill(tag.colour, dark) + L"' CornerRadius='4' Padding='8,2,8,3' Margin='0,0,6,4'><TextBlock x:Name='text" +
                  std::to_wstring(i) + L"' FontSize='13'/></Border></InlineUIContainer>";
    }
    markup += L"<InlineUIContainer><HyperlinkButton x:Name='addLink' Padding='4,2' MinHeight='0' MinWidth='0' FontSize='13' "
              L"Margin='0,0,0,2'>" + LabelsXmlEscape(Str(L"labels.add")) + L"</HyperlinkButton></InlineUIContainer></Paragraph></RichTextBlock>";
    auto text = LoadXaml(markup).as<muxc::RichTextBlock>();
    for (size_t i = 0; i < merged.tags.size(); i++) {
        auto pill = text.FindName(L"pill" + winrt::hstring{std::to_wstring(i)}).try_as<muxc::Border>();
        if (!pill) {
            continue;
        }
        LabelTag tag = merged.tags[i].first;
        bool everywhere = merged.tags[i].second;
        if (auto label = text.FindName(L"text" + winrt::hstring{std::to_wstring(i)}).try_as<muxc::TextBlock>()) {
            label.Text(tag.name.empty() ? LabelsColourName(tag.colour) : tag.name);
        }
        if (!everywhere) {
            pill.Opacity(0.55);  // On some of the selected items only.
            muxc::ToolTipService::SetToolTip(pill, winrt::box_value(winrt::hstring{Str(L"labels.some")}));
        }
        Keep(pill, pill.Tapped(winrt::auto_revoke, [weakHost, paths, tag, everywhere](Inspectable const& sender, mux::Input::TappedRoutedEventArgs const&) {
            auto element = sender.try_as<mux::FrameworkElement>();
            if (!element) {
                return;
            }
            muxc::MenuFlyout menu;
            if (!everywhere) {
                muxc::MenuFlyoutItem all;
                all.Text(Str(L"labels.apply"));
                Keep(all, all.Click(winrt::auto_revoke, [weakHost, paths, tag](Inspectable const&, mux::RoutedEventArgs const&) {
                    LabelsDo(weakHost, paths, tag, true);
                }));
                menu.Items().Append(all);
            }
            muxc::MenuFlyoutItem remove;
            remove.Text(Str(L"labels.remove"));
            Keep(remove, remove.Click(winrt::auto_revoke, [weakHost, paths, tag](Inspectable const&, mux::RoutedEventArgs const&) {
                LabelsDo(weakHost, paths, tag, false);
            }));
            menu.Items().Append(remove);
            menu.ShowAt(element);
        }));
    }
    auto link = text.FindName(L"addLink").try_as<muxc::HyperlinkButton>();
    if (link) {
        link.IsEnabled(!unreadable);
        auto flyout = LabelsMakeFlyout(weakHost, paths);
        Keep(link, link.Click(winrt::auto_revoke, [flyout](Inspectable const& sender, mux::RoutedEventArgs const&) {
            if (auto element = sender.try_as<mux::FrameworkElement>()) {
                flyout.ShowAt(element);
            }
        }));
    }
    host.Children().Append(text);

    std::wstring line = message;
    if (line.empty() && unreadable) {
        line = LabelsFailText(unreadableWhy);
    }
    if (!line.empty()) {
        host.Children().Append(MakeText(line, 12, false, true));
    }
    if (ambiguous) {
        host.Children().Append(MakeText(Str(L"labels.ambiguous"), 12, false, true));
    }
}

[[clang::no_destroy]] thread_local uint64_t t_labelsFillSeq = 0;  // The newest fill of the section; an older read is dropped.

// Fills the section: the tags are read on a mod thread and the badges built here when they come. What was on show stays
// until then. A newer fill makes an older one obsolete.
void LabelsFillSection(muxc::StackPanel const& host, LabelsPaths const& paths, std::wstring const& message) {
    uint64_t seq = ++t_labelsFillSeq;
    auto weakHost = winrt::make_weak(host);
    LabelsReadAsync<LabelsSectionRead>(
        [paths]() { return LabelsReadSection(paths); },
        [weakHost, paths, message, seq](LabelsSectionRead const& read) {
            if (seq != t_labelsFillSeq) {
                return;
            }
            if (auto shown = weakHost.get()) {
                LabelsBuildSection(shown, paths, message, read);
            }
        });
}

}  // namespace

// The Tags section for the page of the details pane. Empty (collapsed) when labels are off or the selection has
// no file system path.
muxc::StackPanel LabelsPaneSection(DetailsPane* pane) {
    muxc::StackPanel host;
    host.Spacing(6);
    auto paths = std::make_shared<std::vector<std::wstring>>();
    if (g_labels) {
        for (auto const& pidl : pane->shownItems) {
            std::wstring path = pidl ? PathOf(pidl.get()) : std::wstring();
            if (!path.empty()) {
                paths->push_back(path);
            }
        }
    }
    if (paths->empty()) {
        host.Visibility(mux::Visibility::Collapsed);
        return host;
    }
    t_labelsSection.host = winrt::make_weak(host);
    t_labelsSection.paths = paths;
    LabelsFillSection(host, paths, std::wstring());
    return host;
}

// A change made from a menu: the pane's Tags section is filled again (and shows what went wrong), or a notice
// opens at the anchor when there is no pane.
void LabelsAfterMenuChange(LabelsApplyResult const& result, std::function<mux::FrameworkElement()> const& anchor) {
    LabelsAfterChange(t_labelsSection.host, t_labelsSection.paths, result, anchor);
}

// A change from a menu, with the same rule as the pane's: written on a mod thread.
void LabelsFromMenu(LabelsPaths const& paths, LabelTag const& tag, bool add, bool clear,
                    std::function<mux::FrameworkElement()> const& anchor) {
    if (g_unloading) {
        return;
    }
    auto work = [paths, tag, add, clear]() { return clear ? LabelsClearSelection(*paths) : LabelsApplyToSelection(*paths, tag, add); };
    {
        auto queue = mud::DispatcherQueue::GetForCurrentThread();
        bool started = LabelsRunAsync(work, [queue, anchor](LabelsApplyResult const& result) {
            queue.TryEnqueue([result, anchor]() { LabelsAfterMenuChange(result, anchor); });
        });
        if (started) {
            return;
        }
    }
    LabelsAfterMenuChange(work(), anchor);
}
