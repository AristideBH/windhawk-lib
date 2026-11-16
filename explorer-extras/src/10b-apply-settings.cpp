// ---- Helpers that stayed when the settings window went (see docs/settings-window-notes.md) ----

void ReloadAll(WindowState* state);

// XAML written as text, for the controls of the pane.
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
