// ---- The details pane ----
//
// One pane in place of Explorer's Details and Preview panes. Explorer's own Details pane
// (a window of class DetailsPaneHwndHostClass, found with the probes of 0.36.8 and 0.36.9)
// stays on: it keeps the space, the splitter and the toggles (the toolbar's Details button,
// Alt+Shift+P and the View menu). A XAML island of ours is placed over its content and follows
// its rectangle, in every view. What is selected in Explorer's own view is read about 15 times
// a second; when it has been the same for a moment, the details are read on a thread of their
// own and shown with a short slide and fade. See SPEC-details-pane.md.

namespace mc = winrt::Microsoft::UI::Composition;
using PaneFloat3 = winrt::Windows::Foundation::Numerics::float3;

struct PaneRow {
    std::wstring label;
    std::wstring value;
};

struct PaneData {
    std::wstring previewClsid;  // The preview handler of the file, when it has one.
    std::wstring previewPath;
    int previewKind = 0;  // 0 none, 1 a shell preview handler, 2 a PDF, 3 text, 4 image, 5 video, 6 audio (2 to 6 are drawn by the pane).
    std::vector<std::wstring> sizeRoots;  // Folders whose size is worked out in the background.
    ULONGLONG sizeBase = 0;               // What the files among them already weigh.
    bool largeImage = false;              // A picture over the size that opens by itself.
    std::wstring mediaTitle;   // Of a sound file.
    std::wstring mediaArtist;
    int kind = 0;  // 0 the folder shown, 1 a file, 2 a folder, 3 several items.
    std::wstring name;
    std::wstring type;
    std::vector<PaneRow> rows;
    bool hasImage = false;
    IconBits image;
    std::vector<uint64_t> imageKeys;  // One per thumbnail, to tell the new ones from those that stay.
    std::vector<IconBits> images;  // The thumbnails of the first items of a selection.
    std::vector<PaneRow> more;     // The rest of the properties, behind the toggle.
    int canProperties = 0;
};

static std::atomic<double> g_panePreviewHeight{250};  // Dragged by the handle under the preview.

struct PaneCacheEntry {
    std::wstring key;
    std::shared_ptr<PaneData> data;
    ULONGLONG at = 0;
};

struct PaneShot {
    mud::DispatcherQueueTimer timer{nullptr};
    winrt::event_token token{};
    std::shared_ptr<bool> done;
};

struct DetailsPane {
    mud::DispatcherQueueTimer timer{nullptr};
    winrt::event_token token{};
    std::vector<PaneShot> shots;
    mux::Hosting::DesktopWindowXamlSource source{nullptr};
    winrt::Microsoft::UI::Content::DesktopChildSiteBridge bridge{nullptr};
    muxc::Grid root{nullptr};
    muxc::Grid body{nullptr};   // The opaque layer: slides and fades when the pane comes and goes.
    mux::UIElement preview{nullptr};  // The big preview: the only part that slides with the direction.
    mux::UIElement spinner{nullptr};  // Turns in the corner of the card until the real preview is loaded.
    mux::UIElement toggle{nullptr};   // The corner button: thumbnail or preview.
    muxc::Grid layered{nullptr};      // The card: thumbnail, then the pane's own preview, then the corner.
    mux::UIElement previewLayer{nullptr};  // The preview drawn by the pane (a PDF or text).
    mux::UIElement thumb{nullptr};         // The thumbnail in the card.
    bool loading = false;             // A preview is being read.
    bool previewShown = false;        // The preview window is the one on show.
    bool previewReady = false;        // The handler has loaded.
    RECT appliedRect = {};
    muxc::Grid inner{nullptr};  // The content: fades a little at each change of selection.
    muxc::ScrollViewer scroller{nullptr};
    RECT rect = {};
    bool visible = false;
    int failures = 0;
    int ticks = 0;
    // What is selected, what has been asked for, what is shown.
    std::wstring wantedKey;
    std::wstring requestedKey;
    std::wstring shownKey;
    ULONGLONG wantedSince = 0;
    std::vector<Pidl> wantedItems;
    std::vector<uint64_t> wantedHashes;  // One per wanted item, to see which ones were just added.
    std::vector<uint64_t> shownHashes;
    Pidl wantedFolder;
    std::vector<Pidl> shownItems;  // Absolute ids, for the actions of the pane.
    unsigned generation = 0;
    std::deque<PaneCacheEntry> cache;
    ULONGLONG redirectAt = 0;
    int redirectTries = 0;
    // Where the selection is in the list, for the direction of the slide.
    int wantedIndex = -1;
    int shownIndex = -1;
    std::wstring wantedFolderPath;
    std::wstring shownFolderPath;
    bool enter = true;     // The pane has just appeared: its content comes in from the right.
    bool expanded = false;  // The "more details" part is open.
    std::vector<muxc::Border> cards;  // The new thumbnails of a stack, to bring in one by one.
    std::vector<uint64_t> stackKeys;  // The thumbnails the stack shows now.
    int previewSeen = 0;
    std::shared_ptr<PaneData> deferred;                       // A big picture that waits for the corner button.
    std::shared_ptr<PaneData> shownData;                       // The data of the page on show (the size button needs it).
    std::shared_ptr<std::atomic<bool>> sizeCancel;            // Stops the size of folders being worked out.
    std::vector<muxc::TextBlock> sizeBlocks;                  // Where the size, the contents and the types go.
    winrt::Windows::Media::Playback::MediaPlayer mediaPlayer{nullptr};  // The player of the video or sound on show.
    winrt::Windows::Foundation::IInspectable pendingImage{nullptr};      // An image that is still opening.
    muxc::MediaPlayerElement mediaElement{nullptr};                      // The element that shows the player.
    muxc::Slider mediaSlider{nullptr};    // The pane's own small controls: position,
    muxc::TextBlock mediaTime{nullptr};   // time,
    muxc::FontIcon mediaPlay{nullptr};    // play or pause,
    muxc::FontIcon mediaVolume{nullptr};  // and sound on or off.
    bool mediaIgnore = false;
    bool mediaDragging = false;
    ItemsVector menuItems{nullptr};     // The View menu's items, when our entry is in them.
    muxc::MenuFlyoutItemBase menuToggle{nullptr};  // Our entry in the View menu: the one that shows the pane.
    std::vector<muxc::RadioMenuFlyoutItem> hiddenRadios;  // Explorer's own Details and Preview entries.
    muxc::ToggleMenuFlyoutItem navEntry{nullptr};  // Our "Navigation pane" entry, next to the pane's own.
    muxc::ToggleMenuFlyoutItem navOriginal{nullptr};  // Explorer's, in the Show submenu (hidden).
    muxc::IconElement navIcon{nullptr};   // The duotone icon of Explorer's navigation entry, moved to ours.
    ULONGLONG navInvokeAt = 0;
    winrt::event_token toolClickToken{}, menuClickToken{}, navClickToken{};  // Revoked at the end: Explorer keeps these elements.
    muxc::AppBarToggleButton toolToggle{nullptr};  // Our toggle, in the place of Explorer's at the end of the toolbar.
    mux::FrameworkElement toolNative{nullptr};     // Explorer's own Details toggle (hidden).
    muxc::CommandBar toolBar{nullptr};
    muxc::IconElement toolIcon{nullptr};           // Its icon, moved to ours.
    ULONGLONG toggleHoldUntil = 0;
    muxc::MenuFlyoutItemBase navSeparator{nullptr};  // The line that followed it in the Show submenu (hidden).
    muxc::IconElement movedIcon{nullptr};  // The icon Explorer gave its Details entry, now on ours.
    int tickMs = 60;
    ULONGLONG ownToggleUntil = 0;       // The mod is toggling Explorer's own control: it is not a command of the user.
    ULONGLONG shortcutAt = 0;            // Alt+Shift+P is to be sent at this time (the entry was chosen).
    bool leaving = false;  // The native pane went away: ours slides out before it is hidden.
    bool dragging = false;       // The height handle is being dragged: the preview waits.
    bool previewFocused = false;  // The mouse is over the preview, which has the keyboard focus.
    int overTicks = 0;
    ULONGLONG settleAt = 0;  // The pane was resized: the preview comes back once it has stopped moving.
    ULONGLONG leaveAt = 0;
    IPreviewHandler* handler = nullptr;  // The shell preview handler that is showing, in `host`.
    HWND host = nullptr;                 // A child window of the top-level window, above the island.
};

// ---- Reading the details, on a thread of its own ----

std::wstring PaneProp(IPropertyStore* store, const wchar_t* name, std::wstring* label) {
    PROPERTYKEY key;
    if (FAILED(PSGetPropertyKeyFromName(name, &key))) {
        return L"";
    }
    PROPVARIANT value;
    PropVariantInit(&value);
    std::wstring text;
    if (SUCCEEDED(store->GetValue(key, &value)) && value.vt != VT_EMPTY) {
        PWSTR formatted = nullptr;
        if (SUCCEEDED(PSFormatForDisplayAlloc(key, value, PDFF_DEFAULT, &formatted)) && formatted) {
            text = formatted;
            CoTaskMemFree(formatted);
        }
    }
    PropVariantClear(&value);
    if (!text.empty() && label) {
        label->clear();
        IPropertyDescription* description = nullptr;
        if (SUCCEEDED(PSGetPropertyDescription(key, IID_PPV_ARGS(&description))) && description) {
            PWSTR display = nullptr;
            if (SUCCEEDED(description->GetDisplayName(&display)) && display) {
                *label = display;
                CoTaskMemFree(display);
            }
            description->Release();
        }
        if (label->empty()) {
            *label = name;
        }
    }
    return text;
}

// Which properties say something about this kind of file, besides size and dates.
std::vector<const wchar_t*> PaneKeysFor(PERCEIVED type) {
    switch (type) {
        case PERCEIVED_TYPE_IMAGE:
            return {L"System.Image.Dimensions", L"System.Photo.DateTaken", L"System.Photo.CameraModel"};
        case PERCEIVED_TYPE_VIDEO:
            return {L"System.Media.Duration", L"System.Video.FrameWidth", L"System.Video.FrameHeight", L"System.Video.FrameRate",
                    L"System.Video.Compression"};
        case PERCEIVED_TYPE_AUDIO:
            return {L"System.Music.Artist", L"System.Music.AlbumTitle", L"System.Media.Duration", L"System.Audio.EncodingBitrate"};
        case PERCEIVED_TYPE_DOCUMENT:
            return {L"System.Title", L"System.Author", L"System.Document.PageCount", L"System.Document.WordCount"};
        default:
            return {};
    }
}

void PaneReadImage(IShellItem* item, PaneData* data) {
    IShellItemImageFactory* factory = nullptr;
    if (FAILED(item->QueryInterface(IID_PPV_ARGS(&factory))) || !factory) {
        return;
    }
    for (int size : {320, 128}) {
        HBITMAP bitmap = nullptr;
        if (SUCCEEDED(factory->GetImage(SIZE{size, size}, SIIGBF_BIGGERSIZEOK, &bitmap)) && bitmap) {
            bool ok = ReadBitmapBits(bitmap, &data->image);
            DeleteObject(bitmap);
            if (ok) {
                data->hasImage = true;
                break;
            }
        }
    }
    factory->Release();
}

std::wstring PaneLabelFor(const wchar_t* name) {
    PROPERTYKEY key;
    std::wstring label;
    if (SUCCEEDED(PSGetPropertyKeyFromName(name, &key))) {
        IPropertyDescription* description = nullptr;
        if (SUCCEEDED(PSGetPropertyDescription(key, IID_PPV_ARGS(&description))) && description) {
            PWSTR display = nullptr;
            if (SUCCEEDED(description->GetDisplayName(&display)) && display) {
                label = display;
                CoTaskMemFree(display);
            }
            description->Release();
        }
    }
    return label.empty() ? std::wstring(name) : label;
}

std::wstring PaneDate(FILETIME const& time) {
    wchar_t text[96] = L"";
    DWORD flags = FDTF_DEFAULT;
    SHFormatDateTimeW(&time, &flags, text, ARRAYSIZE(text));
    return text;
}

// ".ts" is video (MPEG transport stream) or TypeScript: a transport stream has a sync byte 0x47 at
// the start of every 188-byte packet.
bool LooksLikeTransportStream(const wchar_t* path) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return true;
    }
    uint8_t bytes[189] = {};
    DWORD got = 0;
    ReadFile(file, bytes, sizeof(bytes), &got, nullptr);
    CloseHandle(file);
    return got >= 189 && bytes[0] == 0x47 && bytes[188] == 0x47;
}

// The preview handler of a file, found from its extension (the shell's way), for a file of a size
// that can be previewed.
void PaneFindHandler(PaneData* data, const wchar_t* path) {
    const wchar_t* dot = wcsrchr(path, L'.');
    if (!dot) {
        return;
    }
    // Images, video and sound are played or shown by the pane, whatever their size.
    static const wchar_t* const imageTypes[] = {L".jpg", L".jpeg", L".jpe", L".jfif", L".png", L".gif", L".bmp", L".webp", L".tif", L".tiff",
                                                L".ico", L".avif", L".heic", L".heif", L".dib"};
    static const wchar_t* const videoTypes[] = {L".mp4", L".m4v", L".mov", L".mkv", L".avi", L".wmv", L".webm", L".mpg", L".mpeg", L".3gp", L".ts", L".m2ts"};
    static const wchar_t* const audioTypes[] = {L".mp3", L".wav", L".flac", L".m4a", L".aac", L".ogg", L".opus", L".wma", L".aiff", L".aif"};
    for (const wchar_t* extension : imageTypes) {
        if (g_previewImages && _wcsicmp(dot, extension) == 0) {
            data->previewKind = 4;
            data->previewPath = path;
            return;
        }
    }
    for (const wchar_t* extension : videoTypes) {
        if (g_previewMedia && _wcsicmp(dot, extension) == 0 &&
            !(_wcsicmp(extension, L".ts") == 0 && !LooksLikeTransportStream(path))) {
            data->previewKind = 5;
            data->previewPath = path;
            return;
        }
    }
    for (const wchar_t* extension : audioTypes) {
        if (g_previewMedia && _wcsicmp(dot, extension) == 0) {
            data->previewKind = 6;
            data->previewPath = path;
            return;
        }
    }
    if (!g_panePreview) {
        return;  // The documents are off.
    }
    WIN32_FILE_ATTRIBUTE_DATA info = {};
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        info.nFileSizeHigh != 0 || info.nFileSizeLow > (DWORD)g_previewMaxMb * 1024u * 1024u) {
        return;
    }
    // A PDF and plain text are drawn by the pane itself (they scroll, they take no native window).
    static const wchar_t* const textTypes[] = {L".txt", L".md", L".markdown", L".log", L".json", L".xml", L".csv", L".ini", L".cfg", L".conf",
                                               L".yml", L".yaml", L".toml", L".js", L".ts", L".tsx", L".jsx", L".css", L".scss", L".html",
                                               L".htm", L".c", L".h", L".cpp", L".hpp", L".cc", L".cs", L".py", L".rs", L".go", L".java",
                                               L".kt", L".swift", L".php", L".rb", L".sh", L".bat", L".cmd", L".ps1", L".psm1", L".sql",
                                               L".sln", L".csproj", L".vcxproj", L".cmake", L".gitignore", L".env", L".rc", L".lua", L".vue"};
    if (_wcsicmp(dot, L".pdf") == 0) {
        data->previewKind = 2;
        data->previewPath = path;
        return;
    }
    for (const wchar_t* extension : textTypes) {
        if (_wcsicmp(dot, extension) == 0 && info.nFileSizeLow <= 2u * 1024 * 1024) {
            data->previewKind = 3;
            data->previewPath = path;
            return;
        }
    }
    wchar_t clsid[96] = L"";
    DWORD length = ARRAYSIZE(clsid);
    if (SUCCEEDED(AssocQueryStringW(ASSOCF_INIT_DEFAULTTOSTAR, ASSOCSTR_SHELLEXTENSION, dot, L"{8895b1c6-b41f-4c1c-a562-0d564250836f}",
                                    clsid, &length)) &&
        clsid[0]) {
        data->previewClsid = clsid;
        data->previewPath = path;
        data->previewKind = 1;
    }
}

void PaneReadOne(PaneData* data, PCIDLIST_ABSOLUTE pidl, bool isCurrentFolder) {
    IShellItem2* item = nullptr;
    if (FAILED(SHCreateItemFromIDList(pidl, IID_PPV_ARGS(&item))) || !item) {
        return;
    }
    SFGAOF attributes = 0;
    item->GetAttributes(SFGAO_FOLDER | SFGAO_STREAM, &attributes);
    bool folder = (attributes & SFGAO_FOLDER) && !(attributes & SFGAO_STREAM);
    data->kind = isCurrentFolder ? 0 : folder ? 2 : 1;
    PWSTR display = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &display)) && display) {
        data->name = display;
        CoTaskMemFree(display);
    }
    wchar_t path[MAX_PATH] = L"";
    data->canProperties = SHGetPathFromIDListW(pidl, path) && path[0] ? 1 : 0;
    if (folder && data->canProperties && !(isCurrentFolder && (PathIsRootW(path) || PathIsUNCServerShareW(path)))) {
        data->sizeRoots.push_back(path);
    }

    std::set<std::wstring> have;
    auto addTo = [&](std::vector<PaneRow>& rows, IPropertyStore* store, const wchar_t* name) {
        std::wstring label;
        std::wstring text = PaneProp(store, name, &label);
        if (!text.empty()) {
            rows.push_back({label, text});
            have.insert(name);
        }
    };

    // The store with every property; a file whose handler is slow or fails (an installer, for
    // one) is read again with the fast ones only.
    IPropertyStore* store = nullptr;
    if (FAILED(item->GetPropertyStore(GPS_DEFAULT, IID_PPV_ARGS(&store))) || !store) {
        store = nullptr;
        item->GetPropertyStore(GPS_FASTPROPERTIESONLY, IID_PPV_ARGS(&store));
    }
    if (store) {
        data->type = PaneProp(store, L"System.ItemTypeText", nullptr);
        have.insert(L"System.ItemTypeText");
        if (folder) {
            IEnumShellItems* items = nullptr;
            if (SUCCEEDED(item->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&items))) && items) {
                int count = 0;
                IShellItem* child = nullptr;
                ULONG fetched = 0;
                while (count < 5000 && SUCCEEDED(items->Next(1, &child, &fetched)) && fetched == 1) {
                    child->Release();
                    count++;
                }
                items->Release();
                wchar_t text[64];
                if (count >= 5000) {
                    swprintf(text, ARRAYSIZE(text), L"5000+");
                } else {
                    swprintf(text, ARRAYSIZE(text), count == 1 ? Str(L"pane.item") : Str(L"pane.items"), count);
                }
                data->rows.push_back({Str(L"pane.contains"), text});
            }
            addTo(data->rows, store, L"System.DateModified");
            addTo(data->rows, store, L"System.DateCreated");
        } else {
            addTo(data->rows, store, L"System.Size");
            addTo(data->rows, store, L"System.DateModified");
            addTo(data->rows, store, L"System.DateCreated");
            PERCEIVED perceived = PERCEIVED_TYPE_UNSPECIFIED;
            PERCEIVEDFLAG flags = 0;
            PWSTR name = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_PARENTRELATIVEPARSING, &name)) && name) {
                if (const wchar_t* dot = wcsrchr(name, L'.')) {
                    AssocGetPerceivedType(dot, &perceived, &flags, nullptr);
                }
                CoTaskMemFree(name);
            }
            for (const wchar_t* key : PaneKeysFor(perceived)) {
                addTo(data->rows, store, key);
            }
        }
        addTo(data->rows, store, L"System.ItemFolderPathDisplay");

        // Everything else the file has, for the "more details" part.
        static const wchar_t* const skip[] = {L"System.ItemNameDisplay", L"System.ItemType", L"System.FileName", L"System.ItemName",
                                              L"System.ItemPathDisplay", L"System.ItemFolderNameDisplay", L"System.ParsingName",
                                              L"System.ParsingPath", L"System.ItemUrl", L"System.NamespaceCLSID",
                                              L"System.ItemNameDisplayWithoutExtension", L"System.ItemFolderPathDisplayNarrow",
                                              L"System.ItemPathDisplayNarrow", L"System.ComputerName", L"System.FileExtension"};
        DWORD count = 0;
        store->GetCount(&count);
        for (DWORD i = 0; i < count && data->more.size() < 40; i++) {
            PROPERTYKEY key;
            PWSTR canonical = nullptr;
            if (FAILED(store->GetAt(i, &key)) || FAILED(PSGetNameFromPropertyKey(key, &canonical)) || !canonical) {
                continue;
            }
            std::wstring name = canonical;
            CoTaskMemFree(canonical);
            bool skipped = have.count(name) > 0;
            for (const wchar_t* s : skip) {
                skipped = skipped || name == s;
            }
            if (!skipped) {
                addTo(data->more, store, name.c_str());
            }
        }
        store->Release();
    }

    // The basics, when the store gave none (the shell item itself still has them).
    if (data->type.empty()) {
        PWSTR type = nullptr;
        if (SUCCEEDED(item->GetString(PKEY_ItemTypeText, &type)) && type) {
            data->type = type;
            CoTaskMemFree(type);
        }
    }
    if (!folder && !have.count(L"System.Size")) {
        ULONGLONG size = 0;
        if (SUCCEEDED(item->GetUInt64(PKEY_Size, &size))) {
            wchar_t text[64] = L"";
            StrFormatByteSizeW((LONGLONG)size, text, ARRAYSIZE(text));
            data->rows.insert(data->rows.begin(), {PaneLabelFor(L"System.Size"), text});
        }
    }
    FILETIME time = {};
    if (!have.count(L"System.DateModified") && SUCCEEDED(item->GetFileTime(PKEY_DateModified, &time))) {
        data->rows.push_back({PaneLabelFor(L"System.DateModified"), PaneDate(time)});
    }
    if (!have.count(L"System.DateCreated") && SUCCEEDED(item->GetFileTime(PKEY_DateCreated, &time))) {
        data->rows.push_back({PaneLabelFor(L"System.DateCreated"), PaneDate(time)});
    }
    if (!have.count(L"System.ItemFolderPathDisplay") && data->canProperties) {
        std::wstring whole = path;
        size_t slash = whole.find_last_of(L'\\');
        if (slash != std::wstring::npos && slash > 0) {
            data->rows.push_back({PaneLabelFor(L"System.ItemFolderPathDisplay"), whole.substr(0, slash)});
        }
    }
    if (!folder && data->canProperties) {
        PaneFindHandler(data, path);
        if (data->previewKind == 4) {
            // Over 40 megapixels the thumbnail stays (decoding it all in Explorer would be heavy).
            ULONG width = 0, height = 0;
            item->GetUInt32(PKEY_Image_HorizontalSize, &width);
            item->GetUInt32(PKEY_Image_VerticalSize, &height);
            if ((unsigned long long)width * height > (unsigned long long)g_previewMaxMp * 1000000ULL) {
                data->largeImage = true;  // The thumbnail stays until the corner button is used.
            }
        }
        if (data->previewKind == 6) {
            PWSTR value = nullptr;
            if (SUCCEEDED(item->GetString(PKEY_Title, &value)) && value) {
                data->mediaTitle = value;
                CoTaskMemFree(value);
            }
            value = nullptr;
            if (SUCCEEDED(item->GetString(PKEY_Music_Artist, &value)) && value) {
                data->mediaArtist = value;
                CoTaskMemFree(value);
            }
        }
    }
    PaneReadImage(item, data);
    item->Release();
}

void PaneReadSeveral(PaneData* data, std::vector<Pidl> const& items) {
    data->kind = 3;
    wchar_t text[96];
    swprintf(text, ARRAYSIZE(text), Str(L"pane.items"), (int)items.size());
    data->name = text;
    int files = 0, folders = 0;
    ULONGLONG total = 0;
    std::map<std::wstring, int> types;
    size_t looked = 0;
    for (auto const& pidl : items) {
        if (looked++ >= 400) {
            break;
        }
        IShellItem2* item = nullptr;
        if (FAILED(SHCreateItemFromIDList(pidl.get(), IID_PPV_ARGS(&item))) || !item) {
            continue;
        }
        SFGAOF attributes = 0;
        item->GetAttributes(SFGAO_FOLDER | SFGAO_STREAM, &attributes);
        bool folder = (attributes & SFGAO_FOLDER) && !(attributes & SFGAO_STREAM);
        if (folder) {
            folders++;
            wchar_t folderPath[MAX_PATH] = L"";
            if (data->sizeRoots.size() < 60 && SHGetPathFromIDListW(pidl.get(), folderPath) && folderPath[0]) {
                data->sizeRoots.push_back(folderPath);
            }
        } else {
            files++;
            ULONGLONG size = 0;
            if (SUCCEEDED(item->GetUInt64(PKEY_Size, &size))) {
                total += size;
            }
        }
        PWSTR type = nullptr;
        if (SUCCEEDED(item->GetString(PKEY_ItemTypeText, &type)) && type) {
            types[type]++;
            CoTaskMemFree(type);
        }
        // The first few thumbnails make the stack.
        if (data->images.size() < 4) {
            PaneData one;
            PaneReadImage(item, &one);
            if (one.hasImage) {
                uint64_t hash = 1469598103934665603ULL;
                for (size_t b = 0; b < one.image.bgra.size(); b += 61) {
                    hash = (hash ^ one.image.bgra[b]) * 1099511628211ULL;
                }
                data->imageKeys.push_back(hash);
                data->images.push_back(std::move(one.image));
                Wh_Log(L"details pane: thumbnail %d for the stack", (int)data->images.size());
            }
        }
        item->Release();
    }
    data->sizeBase = total;
    wchar_t number[32];
    swprintf(number, ARRAYSIZE(number), L"%d", files);
    data->rows.push_back({Str(L"pane.files"), number});
    swprintf(number, ARRAYSIZE(number), L"%d", folders);
    data->rows.push_back({Str(L"pane.folders"), number});
    if (files > 0) {
        wchar_t size[64] = L"";
        StrFormatByteSizeW((LONGLONG)total, size, ARRAYSIZE(size));
        std::wstring value = size;
        if (items.size() > 400) {
            value += L"+";
        }
        data->rows.push_back({Str(L"pane.totalsize"), value});
    }
    std::vector<std::pair<int, std::wstring>> ordered;
    for (auto const& entry : types) {
        ordered.push_back({entry.second, entry.first});
    }
    std::sort(ordered.begin(), ordered.end(), [](auto const& a, auto const& b) { return a.first > b.first; });
    std::wstring list;
    for (size_t i = 0; i < ordered.size() && i < 4; i++) {
        if (i) {
            list += L"\n";
        }
        list += ordered[i].second + L" (" + std::to_wstring(ordered[i].first) + L")";
    }
    if (!list.empty()) {
        data->rows.push_back({Str(L"pane.types"), list});
    }
}

std::shared_ptr<PaneData> PaneRead(std::vector<Pidl> const& items, Pidl const& folder) {
    auto data = std::make_shared<PaneData>();
    if (items.empty()) {
        if (folder) {
            PaneReadOne(data.get(), folder.get(), true);
        }
    } else if (items.size() == 1) {
        PaneReadOne(data.get(), items[0].get(), false);
    } else {
        PaneReadSeveral(data.get(), items);
    }
    return data;
}

// ---- One-shot timers of the pane ----

void PaneAfter(WindowState* state, DetailsPane* pane, int ms, std::function<void()> fn) {
    if (!state->queue) {
        return;
    }
    for (size_t i = pane->shots.size(); i-- > 0;) {
        if (*pane->shots[i].done) {
            try {
                pane->shots[i].timer.Tick(pane->shots[i].token);
            } catch (...) {
            }
            pane->shots.erase(pane->shots.begin() + i);
        }
    }
    try {
        PaneShot shot;
        shot.timer = state->queue.CreateTimer();
        shot.done = std::make_shared<bool>(false);
        auto done = shot.done;
        shot.timer.Interval(std::chrono::milliseconds(ms));
        shot.timer.IsRepeating(false);
        shot.token = shot.timer.Tick([done, fn, pane](mud::DispatcherQueueTimer const& sender, Inspectable const&) {
            sender.Stop();
            *done = true;
            if (g_unloading) {
                return;
            }
            WindowState* now = StateForThisThread(false);
            if (!now || now->details != pane) {
                return;
            }
            try {
                fn();
            } catch (...) {
                LogHresult(L"the pane's timer");
            }
        });
        shot.timer.Start();
        pane->shots.push_back(std::move(shot));
    } catch (...) {
        LogHresult(L"PaneAfter");
    }
}

// ---- What the pane shows ----

mux::Media::Imaging::WriteableBitmap PaneBitmap(IconBits const& bits) {
    mux::Media::Imaging::WriteableBitmap bitmap(bits.width, bits.height);
    auto buffer = bitmap.PixelBuffer();
    winrt::com_ptr<IBufferByteAccessLocal> access;
    winrt::check_hresult(((::IUnknown*)winrt::get_abi(buffer))->QueryInterface(kIidBufferByteAccess, access.put_void()));
    uint8_t* pixels = nullptr;
    winrt::check_hresult(access->Buffer(&pixels));
    if (!pixels || buffer.Capacity() < bits.bgra.size()) {
        throw winrt::hresult_error(E_FAIL);
    }
    memcpy(pixels, bits.bgra.data(), bits.bgra.size());
    buffer.Length((uint32_t)bits.bgra.size());
    bitmap.Invalidate();
    return bitmap;
}

// Back to Explorer's file list after a click in the pane, so the arrow keys keep working.
void PaneGiveFocusBack(WindowState* state) {
    HWND view = FindVisibleChild(state->topLevel, L"SHELLDLL_DefView");
    if (view) {
        HWND ui = FindWindowExW(view, nullptr, L"DirectUIHWND", nullptr);
        SetFocus(ui ? ui : view);
    }
}

// A button of the action row: an icon and a short name. The first one can be the accent one.
muxc::Button PaneActionButton(const wchar_t* glyph, const wchar_t* label, const wchar_t* tip, bool primary, bool enabled,
                              std::function<void(WindowState*, DetailsPane*)> action) {
    muxc::Button button;
    if (primary) {
        try {
            auto style = mux::Application::Current().Resources().Lookup(winrt::box_value(L"AccentButtonStyle")).as<mux::Style>();
            button.Style(style);
        } catch (...) {
        }
    }
    muxc::StackPanel line;
    line.Orientation(muxc::Orientation::Horizontal);
    line.Spacing(8);
    line.HorizontalAlignment(mux::HorizontalAlignment::Center);
    muxc::FontIcon icon;
    icon.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
    icon.Glyph(glyph);
    icon.FontSize(14);
    line.Children().Append(icon);
    auto text = MakeText(label, 13, false, false);
    text.TextWrapping(mux::TextWrapping::NoWrap);
    text.TextTrimming(mux::TextTrimming::CharacterEllipsis);
    line.Children().Append(text);
    button.Content(line);
    button.HorizontalAlignment(mux::HorizontalAlignment::Stretch);
    button.Padding(mux::Thickness{8, 7, 8, 7});
    button.IsEnabled(enabled);
    muxc::ToolTipService::SetToolTip(button, winrt::box_value(winrt::hstring{tip}));
    Keep(button, button.Click(winrt::auto_revoke, [action](Inspectable const&, mux::RoutedEventArgs const&) {
        WindowState* state = StateForThisThread(false);
        if (!state || !state->details || g_unloading) {
            return;
        }
        try {
            action(state, state->details);
            PaneGiveFocusBack(state);
        } catch (...) {
            LogHresult(L"a pane action");
        }
    }));
    return button;
}

std::wstring PanePaths(DetailsPane* pane) {
    std::wstring text;
    for (auto const& pidl : pane->shownItems) {
        if (!text.empty()) {
            text += L"\r\n";
        }
        wchar_t path[MAX_PATH] = L"";
        if (SHGetPathFromIDListW(pidl.get(), path) && path[0]) {
            text += path;
        } else {
            text += PathOf(pidl.get());
        }
    }
    return text;
}

muxc::StackPanel PaneRows(std::vector<PaneRow> const& rows) {
    muxc::StackPanel list;
    list.Spacing(9);
    for (auto const& row : rows) {
        // The name at the left, the value at the right.
        muxc::Grid entry;
        muxc::ColumnDefinition nameColumn, valueColumn;
        nameColumn.Width(mux::GridLength{1, mux::GridUnitType::Auto});
        nameColumn.MaxWidth(150);
        valueColumn.Width(mux::GridLength{1, mux::GridUnitType::Star});
        entry.ColumnDefinitions().Append(nameColumn);
        entry.ColumnDefinitions().Append(valueColumn);
        entry.ColumnSpacing(16);
        auto label = MakeText(row.label, 12, false, true);
        label.TextWrapping(mux::TextWrapping::Wrap);
        label.VerticalAlignment(mux::VerticalAlignment::Top);
        muxc::Grid::SetColumn(label, 0);
        entry.Children().Append(label);
        auto value = MakeText(row.value, 13, false, false);
        value.TextWrapping(mux::TextWrapping::Wrap);
        value.TextAlignment(mux::TextAlignment::Right);
        value.HorizontalAlignment(mux::HorizontalAlignment::Right);
        muxc::Grid::SetColumn(value, 1);
        std::wstring text = row.value;
        muxc::ToolTipService::SetToolTip(value, winrt::box_value(winrt::hstring{Str(L"pane.clicktocopy")}));
        Keep(value, value.Tapped(winrt::auto_revoke, [text](Inspectable const& sender, mux::Input::TappedRoutedEventArgs const&) {
            WindowState* state = StateForThisThread(false);
            if (!state || g_unloading) {
                return;
            }
            CopyTextToClipboard(state->topLevel, text);
            if (auto block = sender.try_as<muxc::TextBlock>()) {
                muxc::ToolTipService::SetToolTip(block, winrt::box_value(winrt::hstring{Str(L"pane.copied")}));
            }
        }));
        entry.Children().Append(value);
        list.Children().Append(entry);
    }
    return list;
}

void PaneLayoutPreview(WindowState* state, DetailsPane* pane);
void PaneSetMode(WindowState* state, DetailsPane* pane, bool preview);
void PaneStartMediaPreview(WindowState* state, DetailsPane* pane, std::wstring const& key, std::shared_ptr<PaneData> const& data, bool force = false);
void PaneCornerUpdate(DetailsPane* pane);

// ---- Animations ----
// Only the Translation and Opacity transitions of an element show in the island (storyboards
// and compositor animations did not). The element is put at its start without transitions;
// a moment later (after it has been drawn once) the transitions are attached and the end
// state is set. delay < 0: the element is already drawn at its start, go at once.

void PaneTween(WindowState* state, DetailsPane* pane, mux::UIElement const& element, float fx, float fy, float tx, float ty,
               double fo, double to, int ms, int delay) {
    if (delay >= 0) {
        element.TranslationTransition(nullptr);
        element.OpacityTransition(nullptr);
        element.Translation(PaneFloat3{fx, fy, 0});
        element.Opacity(fo);
    }
    auto go = [element, tx, ty, to, ms]() {
        mux::Vector3Transition slide;
        slide.Duration(std::chrono::milliseconds(ms));
        mux::ScalarTransition fade;
        fade.Duration(std::chrono::milliseconds(ms > 40 ? ms - 30 : ms));
        element.TranslationTransition(slide);
        element.OpacityTransition(fade);
        element.Translation(PaneFloat3{tx, ty, 0});
        element.Opacity(to);
    };
    if (delay < 0) {
        go();
    } else {
        PaneAfter(state, pane, delay, go);
    }
}

// Several items: their thumbnails, stacked and fanned a little. The cards are returned so
// that they can come in one after the other.
muxc::Grid PaneStack(PaneData const& data, std::vector<uint64_t> const& before, std::vector<muxc::Border>* cards) {
    Wh_Log(L"details pane: stack of %d", (int)data.images.size());
    muxc::Grid stack;
    stack.HorizontalAlignment(mux::HorizontalAlignment::Center);
    stack.VerticalAlignment(mux::VerticalAlignment::Center);
    double cardHeight = g_panePreviewHeight - 60 < 80 ? 80 : g_panePreviewHeight - 60;
    static const double angle[4] = {-2.0, 7.0, -8.0, 4.0};
    static const double shift[4] = {0.0, 26.0, -28.0, 12.0};
    size_t count = data.images.size();
    for (size_t k = count; k-- > 0;) {  // The first item on top.
        muxc::Border card;
        card.CornerRadius(mux::CornerRadius{6, 6, 6, 6});
        card.Padding(mux::Thickness{3, 3, 3, 3});
        card.Background(LoadXaml(std::wstring(L"<Border ") + kXamlNs + L" Background='{ThemeResource CardBackgroundFillColorSecondaryBrush}'/>")
                            .as<muxc::Border>()
                            .Background());
        card.BorderBrush(LoadXaml(std::wstring(L"<Border ") + kXamlNs + L" BorderBrush='{ThemeResource ControlStrokeColorDefaultBrush}'/>")
                             .as<muxc::Border>()
                             .BorderBrush());
        card.BorderThickness(mux::Thickness{1, 1, 1, 1});
        muxc::Image image;
        image.Source(PaneBitmap(data.images[k]));
        image.Stretch(mux::Media::Stretch::Uniform);
        image.MaxHeight(cardHeight);
        image.MaxWidth(cardHeight * 1.25);
        card.Child(image);
        card.HorizontalAlignment(mux::HorizontalAlignment::Center);
        card.VerticalAlignment(mux::VerticalAlignment::Center);
        card.RenderTransformOrigin(winrt::Windows::Foundation::Point{0.5, 0.5});
        mux::Media::CompositeTransform transform;
        transform.Rotation(angle[k]);
        transform.TranslateX(shift[k]);
                card.RenderTransform(transform);
        stack.Children().Append(card);
        bool stays = k < data.imageKeys.size() && std::find(before.begin(), before.end(), data.imageKeys[k]) != before.end();
        if (!stays) {
            cards->push_back(card);
        }
    }
    std::reverse(cards->begin(), cards->end());  // The top one first.
    return stack;
}

void PaneStartSizeJob(WindowState* state, DetailsPane* pane, std::wstring const& key, std::shared_ptr<PaneData> const& data);

// The rows where the size, the contents and the types of folders go, filled in when the count has run.
void PaneShowSizeRows(DetailsPane* pane, muxc::StackPanel const& host) {
    host.Children().Clear();
    pane->sizeBlocks.clear();
    auto sizeList = PaneRows({{Str(L"pane.totalsize"), L"\u2026"}, {Str(L"pane.insides"), L"\u2026"}, {Str(L"pane.types"), L"\u2026"}});
    for (auto child : sizeList.Children()) {
        if (auto grid = child.try_as<muxc::Grid>()) {
            if (auto block = grid.Children().GetAt(1).try_as<muxc::TextBlock>()) {
                pane->sizeBlocks.push_back(block);
            }
        }
    }
    host.Children().Append(sizeList);
}

muxc::StackPanel LabelsPaneSection(DetailsPane* pane);  // 10g

mux::UIElement PaneBuild(PaneData const& data, DetailsPane* pane) {
    muxc::StackPanel page;  // The part that stays on top: preview, name, actions.
    page.Spacing(12);
    page.Margin(mux::Thickness{16, 16, 16, 8});
    muxc::StackPanel details;  // The part that scrolls.
    details.Spacing(12);
    details.Margin(mux::Thickness{16, 4, 16, 20});
    pane->cards.clear();
    pane->sizeBlocks.clear();
    auto keysBefore = pane->stackKeys;
    pane->preview = nullptr;
    pane->spinner = nullptr;
    pane->toggle = nullptr;
    pane->layered = nullptr;
    pane->previewLayer = nullptr;
    pane->thumb = nullptr;

    bool bigImage = data.hasImage && (data.image.width > 96 || data.image.height > 96);
    bool framed = false;
    bool stacked = data.kind == 3 && !data.images.empty();
    pane->stackKeys = stacked ? data.imageKeys : std::vector<uint64_t>();
    framed = stacked || bigImage || data.previewKind != 0;
    if (framed) {
        muxc::Border frame;
        frame.Height(g_panePreviewHeight);
        frame.CornerRadius(mux::CornerRadius{8, 8, 8, 8});
        frame.Background(LoadXaml(std::wstring(L"<Border ") + kXamlNs + L" Background='{ThemeResource CardBackgroundFillColorDefaultBrush}'/>")
                             .as<muxc::Border>()
                             .Background());
        if (stacked) {
            frame.Child(PaneStack(data, keysBefore, &pane->cards));
        } else {
            if (data.hasImage) {
                muxc::Image image;
                image.Source(PaneBitmap(data.image));
                image.Stretch(mux::Media::Stretch::Uniform);
                image.Margin(mux::Thickness{8, 8, 8, 8});
                if (!bigImage) {
                    image.MaxWidth(72);
                    image.MaxHeight(72);
                }
                frame.Child(image);
            }
        }
        if (!stacked && g_panePreview && data.previewKind != 0) {
            // A plain spinner while the real preview loads (it is shown when it is ready).
            muxc::Grid layered;
            if (auto child = frame.Child()) {
                frame.Child(nullptr);
                layered.Children().Append(child);
                pane->thumb = child;
            }
            muxc::ProgressRing ring;
            ring.Width(16);
            ring.Height(16);
            ring.IsActive(true);
            ring.Opacity(0.6);
            ring.HorizontalAlignment(mux::HorizontalAlignment::Right);
            ring.VerticalAlignment(mux::VerticalAlignment::Top);
            ring.Margin(mux::Thickness{0, 10, 14, 0});
            layered.Children().Append(ring);
            // The button that takes its place once the preview has loaded.
            // A small chip rather than a Button: its background is tinted by the pane, so that
            // it shows on a white page too.
            muxc::Border button;
            button.Width(30);
            button.Height(26);
            button.CornerRadius(mux::CornerRadius{7, 7, 7, 7});
            button.Background(Brush(40, 128, 128, 128));
            button.HorizontalAlignment(mux::HorizontalAlignment::Right);
            button.VerticalAlignment(mux::VerticalAlignment::Top);
            button.Margin(mux::Thickness{0, 5, 6, 0});
            button.Visibility(mux::Visibility::Collapsed);
            muxc::FontIcon icon;
            icon.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
            icon.Glyph(L"\xE890");
            icon.FontSize(14);
            icon.HorizontalAlignment(mux::HorizontalAlignment::Center);
            icon.VerticalAlignment(mux::VerticalAlignment::Center);
            button.Child(icon);
            Keep(button, button.Tapped(winrt::auto_revoke, [](Inspectable const&, mux::Input::TappedRoutedEventArgs const&) {
                WindowState* now = StateForThisThread(false);
                if (now && now->details && !g_unloading) {
                    if (now->details->deferred && !now->details->previewReady) {
                        auto big = now->details->deferred;
                        now->details->deferred = nullptr;
                        PaneStartMediaPreview(now, now->details, now->details->shownKey, big, true);
                        PaneCornerUpdate(now->details);
                    } else {
                        PaneSetMode(now, now->details, !now->details->previewShown);
                    }
                    PaneGiveFocusBack(now);
                }
            }));
            layered.Children().Append(button);
            frame.Child(layered);
            pane->spinner = ring;
            pane->toggle = button;
            pane->layered = layered;
        }
        page.Children().Append(frame);
        pane->preview = frame;
        // A handle right under the preview: drag it to change the height.
        muxc::Border handle;
        handle.Height(14);
        handle.Margin(mux::Thickness{0, -8, 0, -6});
        handle.Background(LoadXaml(std::wstring(L"<Border ") + kXamlNs + L" Background='Transparent'/>").as<muxc::Border>().Background());
        muxc::Border pill;
        pill.Width(40);
        pill.Height(4);
        pill.CornerRadius(mux::CornerRadius{2, 2, 2, 2});
        pill.HorizontalAlignment(mux::HorizontalAlignment::Center);
        pill.VerticalAlignment(mux::VerticalAlignment::Center);
        pill.Opacity(0.45);
        pill.Background(LoadXaml(std::wstring(L"<Border ") + kXamlNs + L" Background='{ThemeResource TextFillColorSecondaryBrush}'/>")
                            .as<muxc::Border>()
                            .Background());
        handle.Child(pill);
        auto drag = std::make_shared<std::pair<bool, double>>(false, 0.0);
        // While the handle is dragged the preview is hidden (it flickered over the thumbnail)
        // and is put back, at the new size, when the handle is let go. The height is kept.
        auto finish = [drag, pill]() {
            drag->first = false;
            pill.Opacity(0.45);
            Wh_SetIntValue(L"ui.paneHeight", (int)g_panePreviewHeight);
            WindowState* now = StateForThisThread(false);
            if (now && now->details && !g_unloading) {
                DetailsPane* pane = now->details;
                pane->dragging = false;
                PaneAfter(now, pane, 60, [now, pane]() { PaneLayoutPreview(now, pane); });
            }
        };
        Keep(handle, handle.PointerPressed(winrt::auto_revoke, [drag, pill](Inspectable const& sender, mux::Input::PointerRoutedEventArgs const& args) {
            if (WindowState* now = StateForThisThread(false)) {
                if (now->details && !g_unloading) {
                    now->details->dragging = true;
                    if (now->details->host && now->details->handler) {
                        ShowWindow(now->details->host, SW_HIDE);
                    }
                }
            }
            auto element = sender.as<mux::UIElement>();
            drag->first = true;
            drag->second = args.GetCurrentPoint(nullptr).Position().Y - g_panePreviewHeight;
            element.CapturePointer(args.Pointer());
            pill.Opacity(0.9);
            args.Handled(true);
        }));
        Keep(handle, handle.PointerMoved(winrt::auto_revoke, [drag, frame](Inspectable const&, mux::Input::PointerRoutedEventArgs const& args) {
            if (drag->first) {
                double height = args.GetCurrentPoint(nullptr).Position().Y - drag->second;
                g_panePreviewHeight = height < 90 ? 90 : height > 520 ? 520 : height;
                frame.Height(g_panePreviewHeight);
            }
        }));
        Keep(handle, handle.PointerReleased(winrt::auto_revoke, [drag, finish](Inspectable const& sender, mux::Input::PointerRoutedEventArgs const& args) {
            if (drag->first) {
                sender.as<mux::UIElement>().ReleasePointerCapture(args.Pointer());
                finish();
            }
        }));
        Keep(handle, handle.PointerCanceled(winrt::auto_revoke, [drag, finish](Inspectable const&, mux::Input::PointerRoutedEventArgs const&) {
            if (drag->first) {
                finish();
            }
        }));
        page.Children().Append(handle);
    }

    // The name and the type, with a small icon when there is no thumbnail.
    {
        muxc::Grid head;
        muxc::ColumnDefinition iconColumn, textColumn;
        iconColumn.Width(mux::GridLength{1, mux::GridUnitType::Auto});
        textColumn.Width(mux::GridLength{1, mux::GridUnitType::Star});
        head.ColumnDefinitions().Append(iconColumn);
        head.ColumnDefinitions().Append(textColumn);
        head.ColumnSpacing(12);
        if (!framed && data.kind != 3) {
            mux::UIElement icon{nullptr};
            if (data.hasImage) {
                muxc::Image image;
                image.Source(PaneBitmap(data.image));
                image.Width(44);
                image.Height(44);
                image.Stretch(mux::Media::Stretch::Uniform);
                icon = image;
            } else {
                muxc::FontIcon glyph;
                glyph.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
                glyph.Glyph(data.kind == 1 ? L"" : L"");
                glyph.FontSize(34);
                glyph.Opacity(0.7);
                icon = glyph;
            }
            icon.as<mux::FrameworkElement>().VerticalAlignment(mux::VerticalAlignment::Center);
            muxc::Grid::SetColumn(icon.as<mux::FrameworkElement>(), 0);
            head.Children().Append(icon);
        }
        muxc::StackPanel texts;
        auto name = MakeText(data.name.empty() ? Str(L"pane.nothing") : data.name, 17, true, false);
        texts.Children().Append(name);
        if (!data.type.empty()) {
            texts.Children().Append(MakeText(data.type, 12, false, true));
        }
        muxc::Grid::SetColumn(texts, framed || data.kind == 3 ? 0 : 1);
        if (framed || data.kind == 3) {
            muxc::Grid::SetColumnSpan(texts, 2);
        }
        head.Children().Append(texts);
        page.Children().Append(head);
    }

    // The action row.
    {
        muxc::Grid actions;
        actions.ColumnSpacing(8);
        bool several = data.kind == 3;
        struct Spec {
            const wchar_t* glyph;
            const wchar_t* label;
            bool primary;
            bool enabled;
            std::function<void(WindowState*, DetailsPane*)> action;
        };
        std::vector<Spec> specs;
        if (!several) {
            specs.push_back({L"", Str(L"pane.open"), true, data.kind != 0, [](WindowState* state, DetailsPane* pane) {
                                 if (!pane->shownItems.empty()) {
                                     OpenItem(state->topLevel, pane->shownItems[0].get());
                                 }
                             }});
        }
        specs.push_back({L"", several ? Str(L"pane.copypaths") : Str(L"pane.copypath"), several, true,
                         [](WindowState* state, DetailsPane* pane) { CopyTextToClipboard(state->topLevel, PanePaths(pane)); }});
        if (!several) {
            specs.push_back({L"", Str(L"pane.properties"), false, data.canProperties != 0,
                             [](WindowState* state, DetailsPane* pane) {
                                 if (pane->shownItems.empty()) {
                                     return;
                                 }
                                 SHELLEXECUTEINFOW info = {};
                                 info.cbSize = sizeof(info);
                                 info.fMask = SEE_MASK_INVOKEIDLIST;
                                 info.hwnd = state->topLevel;
                                 info.lpVerb = L"properties";
                                 info.lpIDList = (void*)pane->shownItems[0].get();
                                 info.nShow = SW_SHOW;
                                 ModalScope modal;
                                 ShellExecuteExW(&info);
                             }});
        }
        for (size_t i = 0; i < specs.size(); i++) {
            muxc::ColumnDefinition column;
            column.Width(mux::GridLength{1, mux::GridUnitType::Star});
            actions.ColumnDefinitions().Append(column);
            auto button = PaneActionButton(specs[i].glyph, specs[i].label, specs[i].label, specs[i].primary, specs[i].enabled, specs[i].action);
            muxc::Grid::SetColumn(button, (int32_t)i);
            actions.Children().Append(button);
        }
        // Narrow pane: only the icons stay (the names are in the tooltips).
        Keep(actions, actions.SizeChanged(winrt::auto_revoke, [](Inspectable const& sender, mux::SizeChangedEventArgs const& args) {
            auto grid = sender.try_as<muxc::Grid>();
            if (!grid || grid.Children().Size() == 0) {
                return;
            }
            bool tight = args.NewSize().Width / grid.Children().Size() < 118;
            for (auto child : grid.Children()) {
                if (auto button = child.try_as<muxc::Button>()) {
                    if (auto line = button.Content().try_as<muxc::StackPanel>()) {
                        if (line.Children().Size() > 1) {
                            line.Children().GetAt(1).Visibility(tight ? mux::Visibility::Collapsed : mux::Visibility::Visible);
                            line.Spacing(tight ? 0 : 8);
                        }
                    }
                }
            }
        }));
        page.Children().Append(actions);
    }

    // The details, then the rest of them behind a toggle.
    details.Children().Append(LabelsPaneSection(pane));
    details.Children().Append(PaneRows(data.rows));
    if (!data.sizeRoots.empty()) {
        muxc::StackPanel sizeHost;
        if (g_autoFolderSize) {
            PaneShowSizeRows(pane, sizeHost);
        } else {
            // A row like the others, with a compact button where the value goes: the count is made when it is
            // asked for (it can be heavy on a slow computer).
            muxc::Grid entry;
            muxc::ColumnDefinition nameColumn, valueColumn;
            nameColumn.Width(mux::GridLength{1, mux::GridUnitType::Auto});
            nameColumn.MaxWidth(150);
            valueColumn.Width(mux::GridLength{1, mux::GridUnitType::Star});
            entry.ColumnDefinitions().Append(nameColumn);
            entry.ColumnDefinitions().Append(valueColumn);
            entry.ColumnSpacing(16);
            auto label = MakeText(Str(L"pane.sizecontent"), 12, false, true);
            label.VerticalAlignment(mux::VerticalAlignment::Center);
            muxc::Grid::SetColumn(label, 0);
            entry.Children().Append(label);
            auto button = LoadXaml(std::wstring(L"<Button ") + kXamlNs +
                                   L" Padding='12,4' MinHeight='0' FontSize='13' HorizontalAlignment='Right'/>").as<muxc::Button>();
            button.Content(winrt::box_value(winrt::hstring{Str(L"pane.getsize")}));
            muxc::Grid::SetColumn(button, 1);
            entry.Children().Append(button);
            auto weakHost = winrt::make_weak(sizeHost);
            const PaneData* built = &data;
            Keep(button, button.Click(winrt::auto_revoke, [weakHost, built](Inspectable const&, mux::RoutedEventArgs const&) {
                WindowState* now = StateForThisThread(false);
                auto host = weakHost.get();
                if (g_unloading || !now || !now->details || !host || now->details->shownData.get() != built) {
                    return;
                }
                PaneShowSizeRows(now->details, host);
                PaneStartSizeJob(now, now->details, now->details->shownKey, now->details->shownData);
            }));
            sizeHost.Children().Append(entry);
        }
        details.Children().Append(sizeHost);
    }
    if (!data.more.empty()) {
        auto more = PaneRows(data.more);
        more.Visibility(pane->expanded ? mux::Visibility::Visible : mux::Visibility::Collapsed);
        muxc::HyperlinkButton toggle;
        muxc::StackPanel line;
        line.Orientation(muxc::Orientation::Horizontal);
        line.Spacing(6);
        auto label = MakeText(pane->expanded ? Str(L"pane.less") : Str(L"pane.more"), 13, false, false);
        muxc::FontIcon chevron;
        chevron.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
        chevron.Glyph(pane->expanded ? L"" : L"");
        chevron.FontSize(11);
        line.Children().Append(label);
        line.Children().Append(chevron);
        toggle.Content(line);
        toggle.Padding(mux::Thickness{0, 4, 0, 4});
        Keep(toggle, toggle.Click(winrt::auto_revoke, [more, label, chevron](Inspectable const&, mux::RoutedEventArgs const&) {
            WindowState* state = StateForThisThread(false);
            if (!state || !state->details || g_unloading) {
                return;
            }
            DetailsPane* now = state->details;
            now->expanded = !now->expanded;
            if (now->expanded) {
                more.Visibility(mux::Visibility::Visible);
                if (g_paneAnim) {
                    PaneTween(state, now, more, 0, -10, 0, 0, 0.0, 1.0, 220, 40);
                }
            } else if (g_paneAnim) {
                PaneTween(state, now, more, 0, 0, 0, -6, 1.0, 0.0, 150, -1);
                PaneAfter(state, now, 150, [more]() {
                    more.Visibility(mux::Visibility::Collapsed);
                    more.TranslationTransition(nullptr);
                    more.OpacityTransition(nullptr);
                    more.Translation(PaneFloat3{0, 0, 0});
                    more.Opacity(1.0);
                });
            } else {
                more.Visibility(mux::Visibility::Collapsed);
            }
            label.Text(now->expanded ? Str(L"pane.less") : Str(L"pane.more"));
            chevron.Glyph(now->expanded ? L"" : L"");
            PaneGiveFocusBack(state);
        }));
        details.Children().Append(toggle);
        details.Children().Append(more);
    }
    muxc::ScrollViewer list;
    list.VerticalScrollBarVisibility(muxc::ScrollBarVisibility::Auto);
    list.HorizontalScrollBarVisibility(muxc::ScrollBarVisibility::Disabled);
    list.Content(details);
    muxc::Grid whole;
    muxc::RowDefinition top, rest;
    top.Height(mux::GridLength{1, mux::GridUnitType::Auto});
    rest.Height(mux::GridLength{1, mux::GridUnitType::Star});
    whole.RowDefinitions().Append(top);
    whole.RowDefinitions().Append(rest);
    muxc::Grid::SetRow(page, 0);
    muxc::Grid::SetRow(list, 1);
    whole.Children().Append(page);
    whole.Children().Append(list);
    return whole;
}

// The content comes in: from the right when the pane itself has just appeared, a few pixels
// from below or above when the selection moved down or up the list, in place when it did not
// move along the list. The element is put at its start without a transition, then given the
// transitions and sent to its place a moment later (a change of the property after the first
// layout is what a transition animates).
// ---- The shell preview handler, in a native window above the island ----

// The video or sound on show stops and is let go (with its volume, which is kept for the next).
void PaneStopMedia(DetailsPane* pane) {
    pane->pendingImage = nullptr;
    if (pane->mediaPlayer) {
        auto player = pane->mediaPlayer;
        auto element = pane->mediaElement;
        pane->mediaPlayer = nullptr;
        pane->mediaElement = nullptr;
        pane->mediaSlider = nullptr;
        pane->mediaTime = nullptr;
        pane->mediaPlay = nullptr;
        pane->mediaVolume = nullptr;
        try {
            g_paneVolume = (int)(player.Volume() * 100.0 + 0.5);
            g_paneMuted = player.IsMuted();
            Wh_SetIntValue(L"ui.paneVolume", g_paneVolume);
            Wh_SetIntValue(L"ui.paneMuted", g_paneMuted ? 1 : 0);
            player.Pause();
            if (element) {
                element.SetMediaPlayer(nullptr);  // The element lets go first; its controls are still on screen.
            }
        } catch (...) {
        }
    }
}

void PaneClosePreview(DetailsPane* pane) {
    pane->deferred = nullptr;
    if (pane->sizeCancel) {
        *pane->sizeCancel = true;
        pane->sizeCancel = nullptr;
    }
    PaneStopMedia(pane);
    if (pane->handler) {
        IPreviewHandler* handler = pane->handler;
        pane->handler = nullptr;
        try {
            handler->Unload();
        } catch (...) {
        }
        handler->Release();
    }
    pane->previewFocused = false;
    pane->overTicks = 0;
    pane->previewShown = false;
    pane->previewReady = false;
    pane->loading = false;
    pane->appliedRect = RECT{};
    if (pane->host && IsWindow(pane->host)) {
        ShowWindow(pane->host, SW_HIDE);
    }
}

// Where the preview card is, in the top-level window's client pixels.
bool PanePreviewRect(DetailsPane* pane, RECT* out) {
    if (!pane->preview || !pane->root) {
        return false;
    }
    auto frame = pane->preview.try_as<mux::FrameworkElement>();
    double width = pane->root.ActualWidth();
    if (!frame || width <= 0 || frame.ActualWidth() < 20 || frame.ActualHeight() < 20) {
        return false;
    }
    double scale = (pane->rect.right - pane->rect.left) / width;
    auto point = frame.TransformToVisual(mux::UIElement{nullptr}).TransformPoint(winrt::Windows::Foundation::Point{0, 0});
    LONG inset = (LONG)(6 * scale);
    out->left = pane->rect.left + (LONG)(point.X * scale) + inset;
    out->top = pane->rect.top + (LONG)(point.Y * scale) + inset;
    out->top += (LONG)(26 * scale);  // The strip of the card that keeps the corner button.
    out->right = pane->rect.left + (LONG)((point.X + frame.ActualWidth()) * scale) - inset;
    out->bottom = pane->rect.top + (LONG)((point.Y + frame.ActualHeight()) * scale) - inset;
    return out->right - out->left > 20 && out->bottom - out->top > 20;
}

void PaneLayoutPreview(WindowState* state, DetailsPane* pane) {
    RECT r;
    if (!pane->handler || !pane->host || pane->dragging || !PanePreviewRect(pane, &r)) {
        return;
    }
    // Called at every tick: the window follows the card (its place is not final while the page
    // settles), and does nothing when it is where it should be.
    if (EqualRect(&r, &pane->appliedRect) && (IsWindowVisible(pane->host) != 0) == pane->previewShown) {
        return;
    }
    pane->appliedRect = r;
    SetWindowPos(pane->host, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_NOACTIVATE | (pane->previewShown ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    RECT inside = {0, 0, r.right - r.left, r.bottom - r.top};
    pane->handler->SetRect(&inside);
}

// The corner of the card: a spinner while the preview loads, then a button that switches between
// the thumbnail and the preview.
void PaneCornerUpdate(DetailsPane* pane) {
    if (pane->spinner) {
        pane->spinner.Visibility((pane->loading || pane->handler) && !pane->previewReady ? mux::Visibility::Visible : mux::Visibility::Collapsed);
    }
    if (auto button = pane->toggle.try_as<muxc::Border>()) {
        bool waiting = pane->deferred && !pane->previewReady;  // A big picture: the button opens it.
        button.Visibility(pane->previewReady || waiting ? mux::Visibility::Visible : mux::Visibility::Collapsed);
        // On the preview (often a white page) the chip is dark with a white icon.
        button.Background(pane->previewShown ? Brush(220, 32, 32, 32) : Brush(40, 128, 128, 128));
        if (auto icon = button.Child().try_as<muxc::FontIcon>()) {
            icon.Glyph(waiting ? L"\xE8A3" : pane->previewShown ? L"\xED1A" : L"\xE890");
            if (pane->previewShown) {
                icon.Foreground(Brush(255, 255, 255, 255));
            } else {
                icon.ClearValue(muxc::IconElement::ForegroundProperty());
            }
        }
        muxc::ToolTipService::SetToolTip(
            button, winrt::box_value(winrt::hstring{waiting ? Str(L"pane.fullsize") : pane->previewShown ? Str(L"pane.showthumb") : Str(L"pane.showpreview")}));
    }
}

void PaneSetMode(WindowState* state, DetailsPane* pane, bool preview) {
    if (!pane->previewReady) {
        return;
    }
    pane->previewShown = preview;
    if (pane->handler && pane->host) {
        pane->appliedRect = RECT{};
        PaneLayoutPreview(state, pane);
    } else if (pane->previewLayer) {
        auto layer = pane->previewLayer;
        auto thumb = pane->thumb;
        if (preview) {
            // A real crossfade: the preview comes up as the thumbnail goes down.
            layer.Visibility(mux::Visibility::Visible);
            PaneTween(state, pane, layer, 0, 0, 0, 0, 0.0, 1.0, 300, 30);
            if (thumb) {
                PaneTween(state, pane, thumb, 0, 0, 0, 0, 1.0, 0.0, 300, -1);
            }
            PaneAfter(state, pane, 360, [pane, thumb]() {
                if (pane->previewShown && thumb) {
                    thumb.Visibility(mux::Visibility::Collapsed);
                }
            });
        } else {
            if (thumb) {
                thumb.Visibility(mux::Visibility::Visible);
                PaneTween(state, pane, thumb, 0, 0, 0, 0, 0.0, 1.0, 300, 30);
            }
            PaneTween(state, pane, layer, 0, 0, 0, 0, 1.0, 0.0, 300, -1);
            PaneAfter(state, pane, 360, [pane, layer]() {
                if (!pane->previewShown) {
                    layer.Visibility(mux::Visibility::Collapsed);
                }
            });
        }
    }
    PaneCornerUpdate(pane);
}

// ---- A PDF or text, drawn by the pane in XAML: it scrolls, and no native window covers anything ----

// ---- Markdown, drawn in XAML (headings, lists, quotes, code, bold, italic, links) ----

std::wstring PaneStripTags(std::wstring const& text) {
    std::wstring out;
    bool inTag = false;
    for (size_t i = 0; i < text.size(); i++) {
        wchar_t c = text[i];
        if (c == L'<' && i + 1 < text.size() && (iswalpha(text[i + 1]) || text[i + 1] == L'/' || text[i + 1] == L'!')) {
            inTag = true;
        } else if (c == L'>' && inTag) {
            inTag = false;
        } else if (!inTag) {
            out += c;
        }
    }
    return out;
}

void PaneInline(muxc::TextBlock const& block, std::wstring const& text) {
    namespace docs = mux::Documents;
    auto inlines = block.Inlines();
    std::wstring plain;
    bool bold = false, italic = false;
    auto emit = [&](std::wstring const& t, bool code, bool link) {
        if (t.empty()) {
            return;
        }
        docs::Run run;
        run.Text(t);
        if (bold) {
            run.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
        }
        if (italic) {
            run.FontStyle(winrt::Windows::UI::Text::FontStyle::Italic);
        }
        if (code) {
            run.FontFamily(mux::Media::FontFamily(L"Cascadia Mono, Consolas"));
        }
        if (link) {
            run.TextDecorations(winrt::Windows::UI::Text::TextDecorations::Underline);
        }
        inlines.Append(run);
    };
    auto flush = [&]() {
        emit(plain, false, false);
        plain.clear();
    };
    size_t n = text.size();
    for (size_t i = 0; i < n;) {
        wchar_t c = text[i];
        if (c == L'`') {
            size_t end = text.find(L'`', i + 1);
            if (end != std::wstring::npos) {
                flush();
                emit(text.substr(i + 1, end - i - 1), true, false);
                i = end + 1;
                continue;
            }
        }
        if (c == L'*' || c == L'_') {
            if (i + 1 < n && text[i + 1] == c) {
                flush();
                bold = !bold;
                i += 2;
                continue;
            }
            if (c == L'*' && ((i + 1 < n && text[i + 1] != L' ') || italic)) {
                flush();
                italic = !italic;
                i++;
                continue;
            }
        }
        if (c == L'!' && i + 1 < n && text[i + 1] == L'[') {
            size_t close = text.find(L']', i + 2);
            if (close != std::wstring::npos && close + 1 < n && text[close + 1] == L'(') {
                size_t end = text.find(L')', close + 2);
                if (end != std::wstring::npos) {
                    i = end + 1;  // An image: left out.
                    continue;
                }
            }
        }
        if (c == L'[') {
            size_t close = text.find(L']', i + 1);
            if (close != std::wstring::npos && close + 1 < n && text[close + 1] == L'(') {
                size_t end = text.find(L')', close + 2);
                if (end != std::wstring::npos) {
                    flush();
                    emit(text.substr(i + 1, close - i - 1), false, true);
                    i = end + 1;
                    continue;
                }
            }
        }
        plain += c;
        i++;
    }
    flush();
}

muxc::TextBlock PaneMdText(std::wstring const& text, double size, bool semibold) {
    muxc::TextBlock block;
    block.TextWrapping(mux::TextWrapping::Wrap);
    block.FontSize(size);
    if (semibold) {
        block.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
    }
    block.IsTextSelectionEnabled(true);
    PaneInline(block, text);
    return block;
}

muxc::StackPanel PaneMarkdown(std::wstring const& markdown) {
    muxc::StackPanel page;
    page.Spacing(9);
    page.Margin(mux::Thickness{14, 12, 14, 16});
    std::wstring paragraph;
    auto endParagraph = [&]() {
        if (!paragraph.empty()) {
            page.Children().Append(PaneMdText(paragraph, 13, false));
            paragraph.clear();
        }
    };
    auto codeBlock = [&](std::wstring const& code) {
        muxc::Border box;
        box.CornerRadius(mux::CornerRadius{6, 6, 6, 6});
        box.Padding(mux::Thickness{10, 8, 10, 8});
        box.Background(Brush(28, 128, 128, 128));
        muxc::TextBlock block;
        block.FontFamily(mux::Media::FontFamily(L"Cascadia Mono, Consolas"));
        block.FontSize(12);
        block.TextWrapping(mux::TextWrapping::Wrap);
        block.IsTextSelectionEnabled(true);
        block.Text(code);
        box.Child(block);
        page.Children().Append(box);
    };
    bool inCode = false;
    std::wstring code;
    size_t pos = 0;
    bool shortened = false;
    while (pos <= markdown.size()) {
        if (page.Children().Size() >= 600) {
            shortened = true;  // A very long document: the page is not built whole.
            break;
        }
        size_t end = markdown.find(L'\n', pos);
        std::wstring line = markdown.substr(pos, end == std::wstring::npos ? std::wstring::npos : end - pos);
        pos = end == std::wstring::npos ? markdown.size() + 1 : end + 1;
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ' || line.back() == L'\t')) {
            line.pop_back();
        }
        size_t lead = 0;
        while (lead < line.size() && (line[lead] == L' ' || line[lead] == L'\t')) {
            lead++;
        }
        std::wstring trimmed = line.substr(lead);
        if (trimmed.rfind(L"```", 0) == 0 || trimmed.rfind(L"~~~", 0) == 0) {
            if (!inCode) {
                endParagraph();
                inCode = true;
                code.clear();
            } else {
                if (!code.empty() && code.back() == L'\n') {
                    code.pop_back();
                }
                codeBlock(code);
                inCode = false;
            }
            continue;
        }
        if (inCode) {
            code += line + L"\n";
            continue;
        }
        trimmed = PaneStripTags(trimmed);
        while (!trimmed.empty() && trimmed.back() == L' ') {
            trimmed.pop_back();
        }
        if (trimmed.empty()) {
            endParagraph();
            continue;
        }
        if (trimmed[0] == L'#') {
            size_t level = 0;
            while (level < trimmed.size() && trimmed[level] == L'#') {
                level++;
            }
            endParagraph();
            static const double sizes[] = {0, 26, 21, 17, 15, 14, 13};
            std::wstring title = trimmed.substr(level);
            while (!title.empty() && title.front() == L' ') {
                title.erase(title.begin());
            }
            auto heading = PaneMdText(title, sizes[level > 6 ? 6 : level], true);
            if (level <= 2 && page.Children().Size() > 0) {
                heading.Margin(mux::Thickness{0, 6, 0, 0});
            }
            page.Children().Append(heading);
            continue;
        }
        bool rule = trimmed.size() >= 3 && (trimmed.find_first_not_of(L'-') == std::wstring::npos || trimmed.find_first_not_of(L'*') == std::wstring::npos ||
                                            trimmed.find_first_not_of(L'_') == std::wstring::npos);
        if (rule) {
            endParagraph();
            muxc::Border line2;
            line2.Height(1);
            line2.Background(Brush(60, 128, 128, 128));
            page.Children().Append(line2);
            continue;
        }
        if (trimmed[0] == L'>') {
            endParagraph();
            std::wstring quote = trimmed.substr(1);
            while (!quote.empty() && quote.front() == L' ') {
                quote.erase(quote.begin());
            }
            muxc::Border bar;
            bar.BorderThickness(mux::Thickness{3, 0, 0, 0});
            bar.BorderBrush(Brush(110, 128, 128, 128));
            bar.Padding(mux::Thickness{10, 0, 0, 0});
            auto text = PaneMdText(quote, 13, false);
            text.Opacity(0.85);
            bar.Child(text);
            page.Children().Append(bar);
            continue;
        }
        if (trimmed[0] == L'|') {
            endParagraph();
            if (trimmed.find_first_not_of(L"|-: ") == std::wstring::npos) {
                continue;  // The line under the header of a table.
            }
            muxc::TextBlock row;
            row.FontFamily(mux::Media::FontFamily(L"Cascadia Mono, Consolas"));
            row.FontSize(12);
            row.TextWrapping(mux::TextWrapping::Wrap);
            row.Text(trimmed);
            page.Children().Append(row);
            continue;
        }
        size_t marker = 0;
        std::wstring bullet;
        if ((trimmed[0] == L'-' || trimmed[0] == L'*' || trimmed[0] == L'+') && trimmed.size() > 1 && trimmed[1] == L' ') {
            marker = 2;
            bullet = L"\x2022";
        } else {
            size_t digits = 0;
            while (digits < trimmed.size() && iswdigit(trimmed[digits])) {
                digits++;
            }
            if (digits > 0 && digits + 1 < trimmed.size() && trimmed[digits] == L'.' && trimmed[digits + 1] == L' ') {
                marker = digits + 2;
                bullet = trimmed.substr(0, digits + 1);
            }
        }
        if (marker > 0) {
            endParagraph();
            muxc::Grid item;
            muxc::ColumnDefinition first, second;
            first.Width(mux::GridLength{1, mux::GridUnitType::Auto});
            second.Width(mux::GridLength{1, mux::GridUnitType::Star});
            item.ColumnDefinitions().Append(first);
            item.ColumnDefinitions().Append(second);
            item.ColumnSpacing(8);
            item.Margin(mux::Thickness{(double)(lead / 2) * 14 + 6, 0, 0, 0});
            auto mark = PaneMdText(bullet, 13, false);
            mark.Opacity(0.75);
            muxc::Grid::SetColumn(mark, 0);
            auto body = PaneMdText(trimmed.substr(marker), 13, false);
            muxc::Grid::SetColumn(body, 1);
            item.Children().Append(mark);
            item.Children().Append(body);
            page.Children().Append(item);
            continue;
        }
        paragraph += (paragraph.empty() ? L"" : L" ") + trimmed;
    }
    if (inCode && !shortened) {
        codeBlock(code);
    }
    endParagraph();
    if (shortened) {
        page.Children().Append(PaneMdText(L"…", 13, false));
    }
    return page;
}

struct PaneXamlResult {
    bool markdown = false;
    std::vector<winrt::Windows::Storage::Streams::InMemoryRandomAccessStream> pages;
    std::wstring text;
    bool ok = false;
};

std::wstring PaneReadText(std::wstring const& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return L"";
    }
    std::string bytes(240 * 1024, '\0');
    DWORD got = 0;
    ReadFile(file, bytes.data(), (DWORD)bytes.size(), &got, nullptr);
    CloseHandle(file);
    bytes.resize(got);
    std::wstring text;
    if (bytes.size() >= 2 && (uint8_t)bytes[0] == 0xFF && (uint8_t)bytes[1] == 0xFE) {
        text.assign((const wchar_t*)(bytes.data() + 2), (bytes.size() - 2) / 2);
    } else {
        size_t skip = bytes.size() >= 3 && (uint8_t)bytes[0] == 0xEF && (uint8_t)bytes[1] == 0xBB && (uint8_t)bytes[2] == 0xBF ? 3 : 0;
        int length = MultiByteToWideChar(CP_UTF8, 0, bytes.data() + skip, (int)(bytes.size() - skip), nullptr, 0);
        if (length > 0) {
            text.resize(length);
            MultiByteToWideChar(CP_UTF8, 0, bytes.data() + skip, (int)(bytes.size() - skip), text.data(), length);
        }
    }
    if (text.size() > 60000) {
        text.resize(60000);
        text += L"\n...";
    }
    return text;
}

// The preview that the pane draws takes its place over the thumbnail in the card (a crossfade),
// or waits behind the corner button when the previews do not open by themselves.
void PaneRevealLayer(WindowState* state, DetailsPane* pane, mux::UIElement const& view) {
    view.Visibility(g_autoPreview ? mux::Visibility::Visible : mux::Visibility::Collapsed);
    pane->previewLayer = view;
    pane->previewReady = true;
    pane->previewShown = g_autoPreview;
    if (g_autoPreview) {
        PaneTween(state, pane, view, 0, 0, 0, 0, 0.0, 1.0, 320, 30);
        auto thumb = pane->thumb;
        if (thumb) {
            PaneTween(state, pane, thumb, 0, 0, 0, 0, 1.0, 0.0, 320, -1);
        }
        PaneAfter(state, pane, 380, [pane, thumb]() {
            if (pane->previewShown && thumb) {
                thumb.Visibility(mux::Visibility::Collapsed);
            }
        });
    }
    PaneCornerUpdate(pane);
}

void PaneInstallLayer(WindowState* state, DetailsPane* pane, mux::UIElement const& view) {
    view.Opacity(0.0);
    uint32_t at = pane->layered.Children().Size() > 0 ? 1 : 0;
    pane->layered.Children().InsertAt(at, view);
    PaneRevealLayer(state, pane, view);
}

// ---- Images (full size, zoom, pan), video and sound, drawn by the pane ----

std::wstring PaneFileUri(std::wstring const& path) {
    std::wstring out = L"file:///";
    for (wchar_t c : path) {
        if (c == L'\\') {
            out += L'/';
        } else if (c == L' ' || c == L'#' || c == L'%' || c == L'?') {
            wchar_t code[8];
            swprintf(code, ARRAYSIZE(code), L"%%%02X", (unsigned)c);
            out += code;
        } else {
            out += c;
        }
    }
    return out;
}

mux::Media::Brush PaneCardBrush() {
    return LoadXaml(std::wstring(L"<Border ") + kXamlNs + L" Background='{ThemeResource CardBackgroundFillColorDefaultBrush}'/>")
        .as<muxc::Border>()
        .Background();
}

// A picture at its full size, fitted to the card. The wheel, a pinch and a double tap or click zoom
// it; a drag with the mouse moves it (a finger moves it by itself).
muxc::ScrollViewer PaneImageView(mux::Media::Imaging::BitmapImage const& bitmap) {
    muxc::ScrollViewer view;
    view.ZoomMode(muxc::ZoomMode::Enabled);
    view.MinZoomFactor(1.0f);
    view.MaxZoomFactor(12.0f);
    view.HorizontalScrollMode(muxc::ScrollMode::Enabled);
    view.VerticalScrollMode(muxc::ScrollMode::Enabled);
    view.HorizontalScrollBarVisibility(muxc::ScrollBarVisibility::Hidden);
    view.VerticalScrollBarVisibility(muxc::ScrollBarVisibility::Hidden);
    view.Background(PaneCardBrush());
    view.CornerRadius(mux::CornerRadius{8, 8, 8, 8});
    muxc::Image image;
    image.Source(bitmap);
    image.Stretch(mux::Media::Stretch::Uniform);
    muxc::Grid holder;
    holder.Children().Append(image);
    view.Content(holder);
    Keep(view, view.SizeChanged(winrt::auto_revoke, [holder](Inspectable const&, mux::SizeChangedEventArgs const& args) {
        holder.Width(args.NewSize().Width);
        holder.Height(args.NewSize().Height);
    }));
    // The zoom keeps the point under the pointer where it is.
    auto zoomAt = [](muxc::ScrollViewer const& v, winrt::Windows::Foundation::Point point, float next) {
        float old = v.ZoomFactor();
        next = next < 1.0f ? 1.0f : next > 12.0f ? 12.0f : next;
        if (next == old) {
            return;
        }
        double h = (v.HorizontalOffset() + point.X) * next / old - point.X;
        double w = (v.VerticalOffset() + point.Y) * next / old - point.Y;
        v.ChangeView(h, w, next, true);
    };
    Keep(view, view.PointerWheelChanged(winrt::auto_revoke, [zoomAt](Inspectable const& sender, mux::Input::PointerRoutedEventArgs const& args) {
        auto v = sender.as<muxc::ScrollViewer>();
        auto properties = args.GetCurrentPoint(v).Properties();
        if (properties.IsHorizontalMouseWheel()) {
            return;
        }
        zoomAt(v, args.GetCurrentPoint(v).Position(), v.ZoomFactor() * (properties.MouseWheelDelta() > 0 ? 1.25f : 0.8f));
        args.Handled(true);
    }));
    Keep(view, view.DoubleTapped(winrt::auto_revoke, [zoomAt](Inspectable const& sender, mux::Input::DoubleTappedRoutedEventArgs const& args) {
        auto v = sender.as<muxc::ScrollViewer>();
        if (v.ZoomFactor() > 1.05f) {
            v.ChangeView(0.0, 0.0, 1.0f);
        } else {
            zoomAt(v, args.GetPosition(v), 3.0f);
        }
        args.Handled(true);
    }));
    struct Drag {
        bool on = false;
        winrt::Windows::Foundation::Point start{};
        double h = 0, v = 0;
    };
    auto drag = std::make_shared<Drag>();
    Keep(view, view.PointerPressed(winrt::auto_revoke, [drag](Inspectable const& sender, mux::Input::PointerRoutedEventArgs const& args) {
        auto v = sender.as<muxc::ScrollViewer>();
        auto point = args.GetCurrentPoint(v);
        if (args.Pointer().PointerDeviceType() == winrt::Microsoft::UI::Input::PointerDeviceType::Mouse && point.Properties().IsLeftButtonPressed() &&
            v.ZoomFactor() > 1.01f) {
            drag->on = true;
            drag->start = point.Position();
            drag->h = v.HorizontalOffset();
            drag->v = v.VerticalOffset();
            v.CapturePointer(args.Pointer());
        }
    }));
    Keep(view, view.PointerMoved(winrt::auto_revoke, [drag](Inspectable const& sender, mux::Input::PointerRoutedEventArgs const& args) {
        if (drag->on) {
            auto v = sender.as<muxc::ScrollViewer>();
            auto point = args.GetCurrentPoint(v).Position();
            v.ChangeView(drag->h - (point.X - drag->start.X), drag->v - (point.Y - drag->start.Y), nullptr, true);
        }
    }));
    auto release = [drag](Inspectable const& sender, mux::Input::PointerRoutedEventArgs const& args) {
        if (drag->on) {
            drag->on = false;
            sender.as<muxc::ScrollViewer>().ReleasePointerCapture(args.Pointer());
        }
    };
    Keep(view, view.PointerReleased(winrt::auto_revoke, release));
    Keep(view, view.PointerCanceled(winrt::auto_revoke, [drag](Inspectable const&, mux::Input::PointerRoutedEventArgs const&) { drag->on = false; }));
    return view;
}

std::wstring PaneClock(double seconds) {
    int total = seconds < 0 ? 0 : (int)(seconds + 0.5);
    wchar_t text[32];
    if (total >= 3600) {
        swprintf(text, ARRAYSIZE(text), L"%d:%02d:%02d", total / 3600, total / 60 % 60, total % 60);
    } else {
        swprintf(text, ARRAYSIZE(text), L"%d:%02d", total / 60, total % 60);
    }
    return text;
}

// The pane's own controls follow the player (called at each tick of the pane).
void PaneMediaTick(DetailsPane* pane) {
    if (!pane->mediaPlayer || !pane->mediaSlider) {
        return;
    }
    try {
        auto session = pane->mediaPlayer.PlaybackSession();
        double total = std::chrono::duration<double>(session.NaturalDuration()).count();
        double now = std::chrono::duration<double>(session.Position()).count();
        pane->mediaIgnore = true;
        if (total > 0.1) {
            pane->mediaSlider.IsEnabled(true);
            if (pane->mediaSlider.Maximum() != total) {
                pane->mediaSlider.Maximum(total);
            }
            if (!pane->mediaDragging) {
                pane->mediaSlider.Value(now);
            }
        }
        pane->mediaIgnore = false;
        if (pane->mediaTime) {
            pane->mediaTime.Text(PaneClock(now) + L" / " + PaneClock(total));
        }
        if (pane->mediaPlay) {
            bool playing = session.PlaybackState() == winrt::Windows::Media::Playback::MediaPlaybackState::Playing;
            pane->mediaPlay.Glyph(playing ? L"\uE769" : L"\uE768");
        }
        if (pane->mediaVolume) {
            pane->mediaVolume.Glyph(pane->mediaPlayer.IsMuted() ? L"\uE74F" : L"\uE767");
        }
    } catch (...) {
        pane->mediaIgnore = false;
    }
}

// A video, or a sound with its cover. It starts paused. The controls are the pane's own: a thin bar
// stuck to the bottom of the card, shown while the mouse is over a video.
mux::UIElement PaneMediaView(PaneData const& data, DetailsPane* pane, bool audio, DWORD thread, mud::DispatcherQueue const& queue) {
    namespace media = winrt::Windows::Media;
    media::Playback::MediaPlayer player;
    player.AutoPlay(false);
    player.Volume(g_paneVolume / 100.0);
    player.IsMuted(g_paneMuted);
    player.Source(media::Core::MediaSource::CreateFromUri(winrt::Windows::Foundation::Uri(PaneFileUri(data.previewPath))));
    pane->mediaPlayer = player;

    muxc::Grid root;
    root.Background(PaneCardBrush());
    root.CornerRadius(mux::CornerRadius{8, 8, 8, 8});
    bool realThumb = data.hasImage && (data.image.width > 96 || data.image.height > 96);  // Not the file type's icon.
    if (!audio) {
        muxc::MediaPlayerElement element;
        element.AreTransportControlsEnabled(false);
        element.Stretch(mux::Media::Stretch::Uniform);
        element.SetMediaPlayer(player);
        if (realThumb) {
            element.PosterSource(PaneBitmap(data.image));
        }
        pane->mediaElement = element;
        root.Children().Append(element);
    } else {
        muxc::StackPanel column;
        column.VerticalAlignment(mux::VerticalAlignment::Center);
        column.Spacing(4);
        column.Margin(mux::Thickness{12, 14, 12, 50});
        if (realThumb) {
            muxc::Image cover;
            cover.Source(PaneBitmap(data.image));
            cover.Stretch(mux::Media::Stretch::Uniform);
            cover.MaxHeight(120);
            cover.Margin(mux::Thickness{0, 0, 0, 8});
            column.Children().Append(cover);
        } else {
            muxc::FontIcon note;
            note.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
            note.Glyph(L"\uE8D6");
            note.FontSize(44);
            note.Opacity(0.7);
            note.Margin(mux::Thickness{0, 0, 0, 8});
            column.Children().Append(note);
        }
        if (!data.mediaTitle.empty()) {
            auto title = MakeText(data.mediaTitle, 15, true, false);
            title.TextAlignment(mux::TextAlignment::Center);
            title.TextTrimming(mux::TextTrimming::CharacterEllipsis);
            column.Children().Append(title);
        }
        if (!data.mediaArtist.empty()) {
            auto artist = MakeText(data.mediaArtist, 13, false, true);
            artist.TextAlignment(mux::TextAlignment::Center);
            artist.TextTrimming(mux::TextTrimming::CharacterEllipsis);
            column.Children().Append(artist);
        }
        root.Children().Append(column);
    }

    // The bar: play, position, time, sound.
    muxc::Grid bar;
    bar.Height(38);
    bar.VerticalAlignment(mux::VerticalAlignment::Bottom);
    bar.Padding(mux::Thickness{6, 0, 6, 0});
    bar.CornerRadius(mux::CornerRadius{0, 0, 8, 8});
    bar.Background(audio ? Brush(34, 128, 128, 128) : Brush(205, 18, 18, 18));
    for (auto width : {mux::GridLength{1, mux::GridUnitType::Auto}, mux::GridLength{1, mux::GridUnitType::Star},
                       mux::GridLength{1, mux::GridUnitType::Auto}, mux::GridLength{1, mux::GridUnitType::Auto}}) {
        muxc::ColumnDefinition column;
        column.Width(width);
        bar.ColumnDefinitions().Append(column);
    }
    auto white = Brush(255, 255, 255, 255);
    auto makeIcon = [&](const wchar_t* glyph) {
        muxc::FontIcon icon;
        icon.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
        icon.Glyph(glyph);
        icon.FontSize(15);
        if (!audio) {
            icon.Foreground(white);
        }
        return icon;
    };
    auto makeButton = [&](muxc::FontIcon const& icon) {
        muxc::Button button;
        button.Width(30);
        button.Height(30);
        button.Padding(mux::Thickness{0, 0, 0, 0});
        button.Background(Brush(0, 0, 0, 0));
        button.BorderThickness(mux::Thickness{0, 0, 0, 0});
        button.VerticalAlignment(mux::VerticalAlignment::Center);
        button.Content(icon);
        return button;
    };
    auto playIcon = makeIcon(L"\uE768");
    auto playButton = makeButton(playIcon);
    Keep(playButton, playButton.Click(winrt::auto_revoke, [](Inspectable const&, mux::RoutedEventArgs const&) {
        WindowState* now = StateForThisThread(false);
        if (!now || !now->details || !now->details->mediaPlayer || g_unloading) {
            return;
        }
        try {
            auto player = now->details->mediaPlayer;
            if (player.PlaybackSession().PlaybackState() == winrt::Windows::Media::Playback::MediaPlaybackState::Playing) {
                player.Pause();
            } else {
                player.Play();
            }
        } catch (...) {
        }
    }));
    muxc::Grid::SetColumn(playButton, 0);
    bar.Children().Append(playButton);

    muxc::Slider slider;
    slider.Minimum(0);
    slider.Maximum(1);
    slider.IsEnabled(false);
    slider.IsThumbToolTipEnabled(false);
    slider.VerticalAlignment(mux::VerticalAlignment::Center);
    slider.Margin(mux::Thickness{6, 0, 8, 0});
    Keep(slider, slider.ValueChanged(winrt::auto_revoke, [](Inspectable const&, muxc::Primitives::RangeBaseValueChangedEventArgs const& args) {
        WindowState* now = StateForThisThread(false);
        if (!now || !now->details || !now->details->mediaPlayer || now->details->mediaIgnore || g_unloading) {
            return;
        }
        try {
            now->details->mediaPlayer.PlaybackSession().Position(
                winrt::Windows::Foundation::TimeSpan(static_cast<int64_t>(args.NewValue() * 10000000.0)));
        } catch (...) {
        }
    }));
    auto pressed = [](Inspectable const&, mux::Input::PointerRoutedEventArgs const&) {
        WindowState* now = StateForThisThread(false);
        if (now && now->details) {
            now->details->mediaDragging = true;
        }
    };
    auto released = [](Inspectable const&, mux::Input::PointerRoutedEventArgs const&) {
        WindowState* now = StateForThisThread(false);
        if (now && now->details) {
            now->details->mediaDragging = false;
        }
    };
    AddRouted(slider, mux::UIElement::PointerPressedEvent(), winrt::box_value(mux::Input::PointerEventHandler(pressed)));
    AddRouted(slider, mux::UIElement::PointerReleasedEvent(), winrt::box_value(mux::Input::PointerEventHandler(released)));
    AddRouted(slider, mux::UIElement::PointerCaptureLostEvent(), winrt::box_value(mux::Input::PointerEventHandler(released)));
    muxc::Grid::SetColumn(slider, 1);
    bar.Children().Append(slider);

    auto time = MakeText(L"0:00 / 0:00", 11, false, audio);
    time.VerticalAlignment(mux::VerticalAlignment::Center);
    time.Margin(mux::Thickness{0, 0, 6, 0});
    if (!audio) {
        time.Foreground(white);
    }
    muxc::Grid::SetColumn(time, 2);
    bar.Children().Append(time);

    auto volumeIcon = makeIcon(g_paneMuted ? L"\uE74F" : L"\uE767");
    auto volumeButton = makeButton(volumeIcon);
    Keep(volumeButton, volumeButton.Click(winrt::auto_revoke, [](Inspectable const&, mux::RoutedEventArgs const&) {
        WindowState* now = StateForThisThread(false);
        if (now && now->details && now->details->mediaPlayer && !g_unloading) {
            try {
                now->details->mediaPlayer.IsMuted(!now->details->mediaPlayer.IsMuted());
            } catch (...) {
            }
        }
    }));
    muxc::Grid::SetColumn(volumeButton, 3);
    bar.Children().Append(volumeButton);

    pane->mediaSlider = slider;
    pane->mediaTime = time;
    pane->mediaPlay = playIcon;
    pane->mediaVolume = volumeIcon;
    root.Children().Append(bar);

    if (!audio) {
        // A video's bar is there while the mouse is over the card (a tap shows it on a touch screen).
        mux::ScalarTransition fade;
        fade.Duration(std::chrono::milliseconds(150));
        bar.OpacityTransition(fade);
        bar.Opacity(0.0);
        Keep(root, root.PointerEntered(winrt::auto_revoke, [bar](Inspectable const&, mux::Input::PointerRoutedEventArgs const&) { bar.Opacity(1.0); }));
        Keep(root, root.PointerExited(winrt::auto_revoke, [bar](Inspectable const&, mux::Input::PointerRoutedEventArgs const&) {
            WindowState* now = StateForThisThread(false);
            if (!(now && now->details && now->details->mediaDragging)) {
                bar.Opacity(0.0);
            }
        }));
        Keep(root, root.Tapped(winrt::auto_revoke, [bar](Inspectable const&, mux::Input::TappedRoutedEventArgs const&) { bar.Opacity(bar.Opacity() > 0.5 ? 0.0 : 1.0); }));
    }

    // A file that cannot be played says so (the event comes from another thread).
    Keep(player, player.MediaFailed(winrt::auto_revoke, [root, queue, thread](media::Playback::MediaPlayer const&, media::Playback::MediaPlayerFailedEventArgs const&) {
        try {
            queue.TryEnqueue([root, thread]() {
                if (g_unloading) {
                    return;
                }
                WindowState* state = StateForThisThread(false);
                if (!state || state->thread != thread || !state->details) {
                    return;
                }
                auto message = MakeText(Str(L"pane.cantplay"), 13, false, true);
                message.HorizontalAlignment(mux::HorizontalAlignment::Center);
                message.VerticalAlignment(mux::VerticalAlignment::Center);
                root.Children().Append(message);
            });
        } catch (...) {
        }
    }));
    return root;
}

// ---- What the folders hold (size, count, types), worked out on a thread and cancelled with the selection ----

struct PaneSizeState {
    ULONGLONG bytes = 0;
    ULONGLONG files = 0;
    ULONGLONG folders = 0;
    std::map<std::wstring, ULONGLONG> types;
    bool done = false;
    bool partial = false;
};

void PaneSizeArrived(DWORD thread, std::wstring const& key, std::shared_ptr<PaneSizeState> const& result) {
    if (g_unloading || !result) {
        return;
    }
    WindowState* state = StateForThisThread(false);
    if (!state || state->thread != thread || !state->details) {
        return;
    }
    DetailsPane* pane = state->details;
    if (pane->shownKey != key || pane->sizeBlocks.size() != 3) {
        return;
    }
    try {
        wchar_t size[64] = L"";
        StrFormatByteSizeW((LONGLONG)result->bytes, size, ARRAYSIZE(size));
        std::wstring text = size;
        text += result->partial ? L"+" : result->done ? L"" : L"\u2026";
        pane->sizeBlocks[0].Text(text);
        wchar_t counts[96];
        swprintf(counts, ARRAYSIZE(counts), Str(L"pane.filesfolders"), result->files, result->folders);
        pane->sizeBlocks[1].Text(counts);
        std::vector<std::pair<ULONGLONG, std::wstring>> ordered;
        for (auto const& entry : result->types) {
            ordered.push_back({entry.second, entry.first});
        }
        std::sort(ordered.begin(), ordered.end(), [](auto const& x, auto const& y) { return x.first > y.first; });
        std::wstring list;
        for (size_t i = 0; i < ordered.size() && i < 4; i++) {
            if (i) {
                list += L"\n";
            }
            list += ordered[i].second + L" (" + std::to_wstring(ordered[i].first) + L")";
        }
        pane->sizeBlocks[2].Text(list.empty() ? std::wstring(L"-") : list);
    } catch (...) {
        LogHresult(L"showing the folder size");
    }
}

void PaneStartSizeJob(WindowState* state, DetailsPane* pane, std::wstring const& key, std::shared_ptr<PaneData> const& data) {
    if (pane->sizeCancel) {
        *pane->sizeCancel = true;
    }
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    pane->sizeCancel = cancel;
    DWORD thread = GetCurrentThreadId();
    auto queue = state->queue;
    auto roots = data->sizeRoots;
    ULONGLONG base = data->sizeBase;
    try {
        StartModThread([queue, thread, key, roots, base, cancel]() mutable {
            auto result = std::make_shared<PaneSizeState>();
            result->bytes = base;
            ULONGLONG began = GetTickCount64(), lastPost = began, entries = 0;
            std::vector<std::wstring> stack(roots.begin(), roots.end());
            auto post = [&](bool done) {
                auto copy = std::make_shared<PaneSizeState>(*result);
                copy->done = done;
                try {
                    queue.TryEnqueue([thread, key, copy]() { PaneSizeArrived(thread, key, copy); });
                } catch (...) {
                }
            };
            bool stopped = false;
            while (!stack.empty() && !stopped) {
                std::wstring dir = stack.back();
                stack.pop_back();
                WIN32_FIND_DATAW found;
                HANDLE handle = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &found, FindExSearchNameMatch, nullptr,
                                                 FIND_FIRST_EX_LARGE_FETCH);
                if (handle == INVALID_HANDLE_VALUE) {
                    continue;
                }
                do {
                    if (found.cFileName[0] == L'.' && (found.cFileName[1] == 0 || (found.cFileName[1] == L'.' && found.cFileName[2] == 0))) {
                        continue;
                    }
                    if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                        result->folders++;
                        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                            stack.push_back(dir + L"\\" + found.cFileName);  // Links are not followed.
                        }
                    } else {
                        result->files++;
                        result->bytes += ((ULONGLONG)found.nFileSizeHigh << 32) | found.nFileSizeLow;
                        const wchar_t* dot = wcsrchr(found.cFileName, L'.');
                        std::wstring type = dot ? std::wstring(dot + 1) : std::wstring();
                        for (auto& c : type) {
                            c = (wchar_t)towupper(c);
                        }
                        result->types[type.empty() ? std::wstring(L"-") : type]++;
                    }
                    if ((++entries & 255) == 0) {
                        ULONGLONG now = GetTickCount64();
                        if (*cancel || g_unloading) {
                            stopped = true;
                            break;
                        }
                        if (now - began > 40000 || entries > 3000000) {
                            result->partial = true;  // A very big folder: the count so far, marked with a +.
                            stopped = true;
                            break;
                        }
                        if (now - lastPost > 400) {
                            lastPost = now;
                            post(false);
                        }
                    }
                } while (FindNextFileW(handle, &found));
                FindClose(handle);
                if (*cancel || g_unloading) {
                    stopped = true;
                }
            }
            if (!*cancel && !g_unloading) {
                post(true);
            }
            queue = nullptr;
        });
    } catch (...) {
        LogHresult(L"starting the folder size");
    }
}

void PaneStartMediaPreview(WindowState* state, DetailsPane* pane, std::wstring const& key, std::shared_ptr<PaneData> const& data, bool force) {
    if (data->largeImage && !force) {
        return;
    }
    DWORD thread = GetCurrentThreadId();
    try {
        if (data->previewKind == 4) {
            pane->loading = true;
            mux::Media::Imaging::BitmapImage bitmap;
            auto view = PaneImageView(bitmap);
            view.Opacity(0.0);  // It is in the card, unseen, while the image opens (it does not open off screen).
            uint32_t at = pane->layered.Children().Size() > 0 ? 1 : 0;
            pane->layered.Children().InsertAt(at, view);
            pane->previewLayer = view;
            pane->pendingImage = bitmap;
            Keep(bitmap, bitmap.ImageOpened(winrt::auto_revoke, [thread, key](Inspectable const&, mux::RoutedEventArgs const&) {
                WindowState* now = StateForThisThread(false);
                if (g_unloading || !now || now->thread != thread || !now->details) {
                    return;
                }
                DetailsPane* current = now->details;
                current->loading = false;
                if (current->shownKey != key || !current->pendingImage || !current->visible || !current->previewLayer) {
                    PaneCornerUpdate(current);
                    return;
                }
                auto opened = current->pendingImage.as<mux::Media::Imaging::BitmapImage>();
                current->pendingImage = nullptr;
                try {
                    PaneRevealLayer(now, current, current->previewLayer);
                    Wh_Log(L"preview: an image, %dx%d", (int)opened.PixelWidth(), (int)opened.PixelHeight());
                } catch (...) {
                    LogHresult(L"showing the image");
                    PaneCornerUpdate(current);
                }
            }));
            Keep(bitmap, bitmap.ImageFailed(winrt::auto_revoke, [thread, key](Inspectable const&, mux::ExceptionRoutedEventArgs const&) {
                WindowState* now = StateForThisThread(false);
                if (g_unloading || !now || now->thread != thread || !now->details) {
                    return;
                }
                DetailsPane* current = now->details;
                current->loading = false;
                current->pendingImage = nullptr;
                if (current->previewLayer && current->layered && current->shownKey == key) {
                    uint32_t index = 0;
                    if (current->layered.Children().IndexOf(current->previewLayer, index)) {
                        current->layered.Children().RemoveAt(index);
                    }
                    current->previewLayer = nullptr;
                }
                PaneCornerUpdate(current);
                Wh_Log(L"preview: the image could not be opened, the thumbnail stays");
            }));
            bitmap.UriSource(winrt::Windows::Foundation::Uri(PaneFileUri(data->previewPath)));
        } else {
            PaneInstallLayer(state, pane, PaneMediaView(*data, pane, data->previewKind == 6, thread, state->queue));
            Wh_Log(L"preview: a %s", data->previewKind == 6 ? L"sound" : L"video");
        }
    } catch (...) {
        LogHresult(L"starting the media preview");
        pane->loading = false;
        PaneCornerUpdate(pane);
    }
}

void PaneXamlArrived(DWORD thread, std::wstring const& key, std::shared_ptr<PaneXamlResult> const& result) {
    if (g_unloading) {
        return;
    }
    WindowState* state = StateForThisThread(false);
    if (!state || state->thread != thread || !state->details) {
        return;
    }
    DetailsPane* pane = state->details;
    pane->loading = false;
    if (pane->shownKey != key || !pane->visible || !pane->layered || !result || !result->ok) {
        PaneCornerUpdate(pane);
        Wh_Log(L"preview: nothing to show for this page");
        return;
    }
    try {
        muxc::ScrollViewer view;
        view.VerticalScrollBarVisibility(muxc::ScrollBarVisibility::Auto);
        view.Background(LoadXaml(std::wstring(L"<Border ") + kXamlNs + L" Background='{ThemeResource CardBackgroundFillColorDefaultBrush}'/>")
                            .as<muxc::Border>()
                            .Background());
        view.CornerRadius(mux::CornerRadius{8, 8, 8, 8});
        if (!result->pages.empty()) {
            view.HorizontalScrollBarVisibility(muxc::ScrollBarVisibility::Disabled);
            muxc::StackPanel list;
            list.Spacing(8);
            list.Margin(mux::Thickness{8, 8, 8, 8});
            for (auto const& stream : result->pages) {
                mux::Media::Imaging::BitmapImage bitmap;
                bitmap.SetSource(stream);
                muxc::Image image;
                image.Source(bitmap);
                image.Stretch(mux::Media::Stretch::Uniform);
                list.Children().Append(image);
            }
            view.Content(list);
        } else if (result->markdown) {
            view.HorizontalScrollBarVisibility(muxc::ScrollBarVisibility::Disabled);
            view.Content(PaneMarkdown(result->text));
        } else {
            view.HorizontalScrollBarVisibility(muxc::ScrollBarVisibility::Auto);
            muxc::TextBlock block;
            block.FontFamily(mux::Media::FontFamily(L"Cascadia Mono, Consolas"));
            block.FontSize(12);
            block.Text(result->text);
            block.IsTextSelectionEnabled(true);
            block.Margin(mux::Thickness{12, 10, 12, 10});
            view.Content(block);
        }
        PaneInstallLayer(state, pane, view);
        Wh_Log(L"preview: drawn by the pane, %d pages", (int)result->pages.size());
    } catch (...) {
        LogHresult(L"drawing the preview");
        PaneCornerUpdate(pane);
    }
}

void PaneStartXamlPreview(WindowState* state, DetailsPane* pane, std::wstring const& key, std::shared_ptr<PaneData> const& data) {
    pane->loading = true;
    int kind = data->previewKind;
    std::wstring path = data->previewPath;
    DWORD thread = GetCurrentThreadId();
    auto queue = state->queue;
    try {
        StartModThread([queue, thread, key, kind, path]() mutable {
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            auto result = std::make_shared<PaneXamlResult>();
            try {
                if (kind == 2) {
                    namespace pdf = winrt::Windows::Data::Pdf;
                    auto file = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(path).get();
                    auto document = pdf::PdfDocument::LoadFromFileAsync(file).get();
                    uint32_t count = document.PageCount() < 10 ? document.PageCount() : 10;
                    for (uint32_t i = 0; i < count; i++) {
                        auto page = document.GetPage(i);
                        pdf::PdfPageRenderOptions options;
                        options.DestinationWidth(900);
                        winrt::Windows::Storage::Streams::InMemoryRandomAccessStream stream;
                        page.RenderToStreamAsync(stream, options).get();
                        stream.Seek(0);
                        result->pages.push_back(stream);
                    }
                    result->ok = !result->pages.empty();
                } else {
                    result->text = PaneReadText(path);
                    result->ok = !result->text.empty();
                    const wchar_t* dot = wcsrchr(path.c_str(), L'.');
                    result->markdown = dot && (_wcsicmp(dot, L".md") == 0 || _wcsicmp(dot, L".markdown") == 0);
                }
            } catch (...) {
                result->ok = false;
            }
            try {
                queue.TryEnqueue([thread, key, result]() { PaneXamlArrived(thread, key, result); });
            } catch (...) {
            }
            queue = nullptr;
            result.reset();
            CoUninitialize();
        });
    } catch (...) {
        pane->loading = false;
        LogHresult(L"starting the preview reader");
    }
}

void PaneOpenPreview(WindowState* state, DetailsPane* pane, PaneData const& data) {
    PaneClosePreview(pane);
    RECT r;
    if (!PanePreviewRect(pane, &r)) {
        return;
    }
    ULONGLONG started = GetTickCount64();
    CLSID clsid;
    if (FAILED(CLSIDFromString(data.previewClsid.c_str(), &clsid))) {
        return;
    }
    IPreviewHandler* handler = nullptr;
    HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&handler));
    if (FAILED(hr) || !handler) {
        Wh_Log(L"preview: no handler could be made for %s (0x%08lx)", data.previewPath.c_str(), (unsigned long)hr);
        return;
    }
    // The handler is given the file, the way it asks for it.
    bool ready = false;
    if (IInitializeWithItem* withItem = nullptr; SUCCEEDED(handler->QueryInterface(IID_PPV_ARGS(&withItem)))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(data.previewPath.c_str(), nullptr, IID_PPV_ARGS(&item))) && item) {
            ready = SUCCEEDED(withItem->Initialize(item, STGM_READ));
            item->Release();
        }
        withItem->Release();
    }
    if (!ready) {
        if (IInitializeWithStream* withStream = nullptr; SUCCEEDED(handler->QueryInterface(IID_PPV_ARGS(&withStream)))) {
            IStream* stream = nullptr;
            if (SUCCEEDED(SHCreateStreamOnFileEx(data.previewPath.c_str(), STGM_READ | STGM_SHARE_DENY_NONE, 0, FALSE, nullptr, &stream)) &&
                stream) {
                ready = SUCCEEDED(withStream->Initialize(stream, STGM_READ));
                stream->Release();
            }
            withStream->Release();
        }
    }
    if (!ready) {
        if (IInitializeWithFile* withFile = nullptr; SUCCEEDED(handler->QueryInterface(IID_PPV_ARGS(&withFile)))) {
            ready = SUCCEEDED(withFile->Initialize(data.previewPath.c_str(), STGM_READ));
            withFile->Release();
        }
    }
    if (!ready) {
        Wh_Log(L"preview: the handler of %s did not take the file", data.previewPath.c_str());
        handler->Release();
        return;
    }
    if (!pane->host || !IsWindow(pane->host)) {
        pane->host = CreateWindowExW(0, L"Static", L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 10, 10, state->topLevel, nullptr,
                                     ModHandle(), nullptr);
    }
    if (!pane->host) {
        handler->Release();
        return;
    }
    // The window stays hidden while the handler loads (its own loading bar is not shown);
    // it is shown after a wait that suits the kind of file.
    SetWindowPos(pane->host, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOACTIVATE);
    RECT inside = {0, 0, r.right - r.left, r.bottom - r.top};
    hr = handler->SetWindow(pane->host, &inside);
    if (SUCCEEDED(hr)) {
        hr = handler->DoPreview();
    }
    if (FAILED(hr)) {
        Wh_Log(L"preview: %s failed (0x%08lx)", data.previewPath.c_str(), (unsigned long)hr);
        handler->Unload();
        handler->Release();
        ShowWindow(pane->host, SW_HIDE);
        return;
    }
    pane->handler = handler;
    Wh_Log(L"preview: %s loading, started in %llu ms", data.previewPath.c_str(), GetTickCount64() - started);
    static const wchar_t* const heavy[] = {L".pdf", L".doc", L".docx", L".xls", L".xlsx", L".ppt", L".pptx", L".odt", L".htm", L".html"};
    const wchar_t* dot = wcsrchr(data.previewPath.c_str(), L'.');
    int wait = 250;
    for (const wchar_t* extension : heavy) {
        if (dot && _wcsicmp(dot, extension) == 0) {
            wait = 1100;
        }
    }
    std::wstring key = pane->shownKey;
    PaneAfter(state, pane, wait, [pane, handler, key]() {
        if (pane->handler == handler && pane->shownKey == key && pane->host) {
            // Loaded: with automatic previews it takes the card, otherwise the corner button
            // offers it.
            pane->previewReady = true;
            pane->previewShown = g_autoPreview;
            pane->appliedRect = RECT{};
            if (WindowState* now = StateForThisThread(false)) {
                PaneLayoutPreview(now, pane);
            }
            PaneCornerUpdate(pane);
            Wh_Log(L"preview: ready, %s", g_autoPreview ? L"shown" : L"waiting for the button");
        }
    });
}

void PaneAnimateIn(WindowState* state, DetailsPane* pane, float dx, float dy, muxc::Grid const& layer) {
    auto body = pane->body;
    auto inner = pane->inner;
    if (!body || !inner) {
        return;
    }
    LogVerbose(L"details pane: content in, dx=%.0f dy=%.0f", dx, dy);
    if (!g_paneAnim) {
        for (mux::UIElement e : {mux::UIElement(body), mux::UIElement(inner)}) {
            e.TranslationTransition(nullptr);
            e.OpacityTransition(nullptr);
            e.Translation(PaneFloat3{0, 0, 0});
            e.Opacity(1.0);
        }
        return;
    }
    if (dx != 0) {
        // The pane came in (its layer may have been left faded and shifted by the exit).
        inner.TranslationTransition(nullptr);
        inner.OpacityTransition(nullptr);
        inner.Translation(PaneFloat3{0, 0, 0});
        inner.Opacity(1.0);
        PaneTween(state, pane, body, dx, 0, 0, 0, 0.0, 1.0, 240, 70);
    } else {
        // The new page is an opaque layer over the old one and fades in: a crossfade with no
        // dip. Only the preview slides, up or down with the selection.
        PaneTween(state, pane, layer, 0, 0, 0, 0, 0.0, 1.0, 300, 40);
        if (dy != 0 && pane->preview) {
            PaneTween(state, pane, pane->preview, 0, dy * 1.8f, 0, 0, 1.0, 1.0, 340, 40);
        }
        // The thumbnails that are new in the stack fly in one after the other, from below
        // and the side; those that stay do not move.
        for (size_t i = 0; i < pane->cards.size(); i++) {
            float side = (i % 2 == 0) ? 26.0f : -26.0f;
            PaneTween(state, pane, pane->cards[i], side, 44, 0, 0, 1.0, 1.0, 380, 50 + (int)i * 55);
        }
    }
}

// The Columns panel follows the file list. Explorer's file list changes size when the pane comes
// or goes and when the window is resized; the panel is brought to it at every tick of the pane
// too, so that the default view does not show behind it for a moment.
void PaneSyncPanel(WindowState* state) {
    if (!state->on || !state->bridge) {
        return;
    }
    HWND fileList = FindVisibleChildCached(&state->paneFileList, state->topLevel, L"SHELLDLL_DefView");
    RECT rect;
    if (fileList && FileListRect(state->topLevel, fileList, &rect) && !EqualRect(&rect, &state->lastRect)) {
        state->lastRect = rect;
        state->bridge.MoveAndResize(winrt::Windows::Graphics::RectInt32{rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top});
    }
}

// ---- The View menu: one entry for the pane, in place of Explorer's two ----

// Explorer's own Details control is used, as a person would: its toggle on the toolbar, or its
// entry of the View menu. Our pane follows it. No keys are sent.
bool PaneToggleNative(WindowState* state, bool useMenuEntry = false) {
    DetailsPane* pane = state->details;
    if (!pane) {
        return true;
    }
    mux::FrameworkElement native{nullptr};
    if (pane->toolNative && !useMenuEntry) {
        native = pane->toolNative;
    } else if (!pane->hiddenRadios.empty()) {
        native = pane->hiddenRadios[0];
    }
    if (!native) {
        Wh_Log(L"details pane: there is no Explorer control to toggle");
        return true;  // Nothing to wait for.
    }
    try {
        auto before = native.Visibility();
        double opacity = native.Opacity();
        bool hit = native.IsHitTestVisible();
        double width = native.Width(), minWidth = native.MinWidth();
        auto margin = native.Margin();
        native.Opacity(0.0);
        native.IsHitTestVisible(false);
        // No width, so it takes no room in the toolbar while it is made visible: our toggle, in its
        // place, must not move.
        native.MinWidth(0.0);
        native.Width(0.0);
        native.Margin(mux::Thickness{0, 0, 0, 0});
        native.Visibility(mux::Visibility::Visible);
        pane->ownToggleUntil = GetTickCount64() + 1200;
        auto peer = mux::Automation::Peers::FrameworkElementAutomationPeer::CreatePeerForElement(native);
        bool done = false;
        if (auto toggle = peer.GetPattern(mux::Automation::Peers::PatternInterface::Toggle)
                              .try_as<mux::Automation::Provider::IToggleProvider>()) {
            toggle.Toggle();
            done = true;
        } else if (auto invoke = peer.GetPattern(mux::Automation::Peers::PatternInterface::Invoke)
                                     .try_as<mux::Automation::Provider::IInvokeProvider>()) {
            invoke.Invoke();
            done = true;
        }
        Wh_Log(L"details pane: Explorer's own control was %s", done ? L"toggled" : L"not usable");
        PaneAfter(state, pane, 200, [native, before, opacity, hit, width, minWidth, margin]() {
            native.Visibility(before);
            native.Width(width);
            native.MinWidth(minWidth);
            native.Margin(margin);
            native.Opacity(opacity);
            native.IsHitTestVisible(hit);
        });
    } catch (...) {
        LogHresult(L"toggling Explorer's details control");
    }
    return true;
}

// Alt+P is Explorer's Preview pane shortcut. It is taken before Explorer sees it, in the accelerator
// hook of the window's thread, and shows or hides this pane instead, so that Explorer's Preview
// pane never opens. Alt+P is dealt with here only while the window is one of ours.
bool PaneHandleAltP(HWND hwnd, LPMSG msg) {
    (void)hwnd;
    if (!msg || (msg->message != WM_SYSKEYDOWN && msg->message != WM_SYSKEYUP) || msg->wParam != 'P' ||
        !(msg->lParam & (1 << 29)) || !g_detailsPane || !g_previewRedirect || g_unloading) {
        return false;
    }
    if ((GetKeyState(VK_SHIFT) | GetKeyState(VK_CONTROL)) & 0x8000) {
        return false;
    }
    WindowState* state = StateForThisThread(false);
    if (!state || !state->details || !state->topLevel || GetAncestor(msg->hwnd, GA_ROOT) != state->topLevel) {
        return false;
    }
    if (msg->message == WM_SYSKEYDOWN && !(msg->lParam & (1 << 30))) {  // Not an auto-repeat.
        // No hold on the buttons: nothing flips them here (a click does), so they follow the pane at once.
        state->details->shortcutAt = GetTickCount64();
        LogVerbose(L"details pane: Alt+P");
    }
    return true;
}

// Explorer's "Details pane" and "Preview pane" entries are hidden and one entry, ours, takes
// their place. They are the two radio items after the separator that follows the layouts, so
// that it holds in every language.
muxc::ToggleMenuFlyoutItem PaneNavItem(DetailsPane* pane);

void PaneMenuSetup(WindowState* state, ItemsVector const& items) {
    DetailsPane* pane = state->details;
    if (!pane || !g_detailsPane || g_unloading) {
        return;
    }
    try {
        uint32_t at = 0;
        if (pane->menuToggle && pane->menuItems == items && items.IndexOf(pane->menuToggle, at)) {
            return;
        }
        int radios = 0;
        bool separated = false;
        std::vector<muxc::RadioMenuFlyoutItem> natives;
        uint32_t firstIndex = 0;
        for (uint32_t i = 0; i < items.Size(); i++) {
            auto base = items.GetAt(i);
            if (!separated) {
                if (base.try_as<muxc::MenuFlyoutSeparator>() && radios >= 8) {
                    separated = true;
                } else if (base.try_as<muxc::RadioMenuFlyoutItem>()) {
                    radios++;
                }
                continue;
            }
            if (auto radio = base.try_as<muxc::RadioMenuFlyoutItem>()) {
                if (natives.empty()) {
                    firstIndex = i;
                }
                natives.push_back(radio);
                if (natives.size() == 2) {
                    break;
                }
            }
        }
        if (natives.size() < 2) {
            Wh_Log(L"view menu: the Details and Preview entries were not found");
            return;
        }
        muxc::ToggleMenuFlyoutItem item;
        item.Text(natives[0].Text());
        item.Name(L"WindhawkPaneItem");
        // Explorer's own icon (its duotone art) is moved to our entry, and given back at the end.
        if (auto icon = natives[0].Icon()) {
            Wh_Log(L"view menu: the Details entry's icon is a %s", winrt::get_class_name(icon).c_str());
            natives[0].Icon(nullptr);
            item.Icon(icon);
            pane->movedIcon = icon;
        }
        item.IsChecked(pane->visible && !pane->leaving);
        pane->menuClickToken = item.Click([](Inspectable const&, mux::RoutedEventArgs const&) {
            WindowState* now = StateForThisThread(false);
            if (now && now->details && !g_unloading) {
                now->details->shortcutAt = GetTickCount64() + 150;  // After the menu has closed.
                now->details->toggleHoldUntil = GetTickCount64() + 1500;
            }
        });
        items.InsertAt(firstIndex, item);
        pane->menuItems = items;
        // The navigation pane's entry, moved up from the Show submenu to sit right after it.
        if (auto original = PaneNavItem(pane)) {
            muxc::ToggleMenuFlyoutItem nav;
            nav.Text(original.Text());
            nav.Name(L"WindhawkNavItem");
            if (auto icon = original.Icon()) {
                original.Icon(nullptr);  // Explorer's own icon moves to our entry.
                nav.Icon(icon);
                pane->navIcon = icon;
            } else {
                muxc::FontIcon glyph;
                glyph.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
                glyph.Glyph(L"\xE8A0");
                nav.Icon(glyph);
            }
            nav.IsChecked(original.IsChecked());
            pane->navClickToken = nav.Click([](Inspectable const&, mux::RoutedEventArgs const&) {
                // Not now: the menu is closing. The entry of the Show submenu is used a moment
                // later, from the pane's tick.
                WindowState* now = StateForThisThread(false);
                if (now && now->details && !g_unloading) {
                    now->details->navInvokeAt = GetTickCount64() + 160;
                }
            });
            items.InsertAt(firstIndex, nav);  // Before the Details pane entry.
            original.Visibility(mux::Visibility::Collapsed);
            // The separator that followed it in the Show submenu goes too.
            for (uint32_t i = 0; i < items.Size(); i++) {
                if (auto sub = items.GetAt(i).try_as<muxc::MenuFlyoutSubItem>()) {
                    if (sub.Items().Size() > 1 && sub.Items().GetAt(1).try_as<muxc::MenuFlyoutSeparator>()) {
                        pane->navSeparator = sub.Items().GetAt(1);
                        pane->navSeparator.Visibility(mux::Visibility::Collapsed);
                    }
                    break;
                }
            }
            pane->navEntry = nav;
            pane->navOriginal = original;
        }
        for (auto const& radio : natives) {
            radio.Visibility(mux::Visibility::Collapsed);
        }
        pane->menuToggle = item;
        pane->menuItems = items;
        pane->hiddenRadios = natives;
        Wh_Log(L"view menu: our pane entry replaces the Details and Preview entries");
    } catch (...) {
        LogHresult(L"PaneMenuSetup");
    }
}

// The navigation pane's entry in the View menu: the first item of the "Show" submenu.
muxc::ToggleMenuFlyoutItem PaneNavItem(DetailsPane* pane) {
    if (!pane->menuItems) {
        return nullptr;
    }
    for (uint32_t i = 0; i < pane->menuItems.Size(); i++) {
        if (auto sub = pane->menuItems.GetAt(i).try_as<muxc::MenuFlyoutSubItem>()) {
            if (sub.Items().Size() > 0) {
                return sub.Items().GetAt(0).try_as<muxc::ToggleMenuFlyoutItem>();
            }
            return nullptr;
        }
    }
    return nullptr;
}

mux::FrameworkElement PaneFindByName(mux::DependencyObject const& node, const wchar_t* name, int depth, int& visited) {
    if (!node || depth > 40 || visited > 4000) {
        return nullptr;
    }
    visited++;
    if (auto element = node.try_as<mux::FrameworkElement>()) {
        if (element.Name() == name) {
            return element;
        }
    }
    int children = mux::Media::VisualTreeHelper::GetChildrenCount(node);
    for (int i = 0; i < children; i++) {
        if (auto found = PaneFindByName(mux::Media::VisualTreeHelper::GetChild(node, i), name, depth + 1, visited)) {
            return found;
        }
    }
    return nullptr;
}

muxc::IconElement PaneButtonIcon(mux::FrameworkElement const& button) {
    if (auto plain = button.try_as<muxc::AppBarButton>()) {
        return plain.Icon();
    }
    if (auto toggle = button.try_as<muxc::AppBarToggleButton>()) {
        return toggle.Icon();
    }
    return nullptr;
}

void PaneSetButtonIcon(mux::FrameworkElement const& button, muxc::IconElement const& icon) {
    if (auto plain = button.try_as<muxc::AppBarButton>()) {
        plain.Icon(icon);
    } else if (auto toggle = button.try_as<muxc::AppBarToggleButton>()) {
        toggle.Icon(icon);
    }
}

// Explorer's Details toggle at the far right of the toolbar ("PaneToggle") is replaced by a toggle
// of ours: the same icon and label, in the same place, ticked while the pane shows. Choosing it
// sends the shortcut that shows or hides the pane.
void PaneToolbarSetup(WindowState* state, mux::DependencyObject const& node) {
    DetailsPane* pane = state->details;
    if (!pane || !g_detailsPane || g_unloading) {
        return;
    }
    try {
        if (pane->toolToggle && pane->toolBar) {
            uint32_t at = 0;
            if (pane->toolBar.PrimaryCommands().IndexOf(pane->toolToggle.as<muxc::ICommandBarElement>(), at)) {
                if (pane->toolNative && pane->toolNative.Visibility() != mux::Visibility::Collapsed) {
                    pane->toolNative.Visibility(mux::Visibility::Collapsed);  // Explorer showed it again.
                }
                return;
            }
            pane->toolToggle = nullptr;  // The toolbar was rebuilt: find the button again.
            pane->toolBar = nullptr;
            pane->toolNative = nullptr;
            pane->toolIcon = nullptr;
        }
        mux::DependencyObject top = node;
        for (int i = 0; i < 60; i++) {
            auto parent = mux::Media::VisualTreeHelper::GetParent(top);
            if (!parent) {
                break;
            }
            top = parent;
        }
        int visited = 0;
        auto native = PaneFindByName(top, L"PaneToggle", 0, visited);
        if (!native) {
            return;
        }
        muxc::CommandBar bar{nullptr};
        mux::DependencyObject up = native;
        for (int i = 0; i < 30 && up; i++) {
            if (auto found = up.try_as<muxc::CommandBar>()) {
                bar = found;
                break;
            }
            up = mux::Media::VisualTreeHelper::GetParent(up);
        }
        if (!bar) {
            return;
        }
        uint32_t index = 0;
        if (!bar.PrimaryCommands().IndexOf(native.as<muxc::ICommandBarElement>(), index)) {
            return;
        }
        muxc::AppBarToggleButton button;
        button.Name(L"WindhawkPaneToggle");
        if (auto plain = native.try_as<muxc::AppBarButton>()) {
            button.Label(plain.Label());
            button.LabelPosition(plain.LabelPosition());
        } else if (auto toggle = native.try_as<muxc::AppBarToggleButton>()) {
            button.Label(toggle.Label());
            button.LabelPosition(toggle.LabelPosition());
        }
        if (auto icon = PaneButtonIcon(native)) {
            PaneSetButtonIcon(native, nullptr);  // Explorer's icon moves to ours.
            button.Icon(icon);
            pane->toolIcon = icon;
        }
        button.IsChecked(false);  // Never shown as ticked: no accent colour, and the icon stays as Explorer draws it.
        pane->toolClickToken = button.Click([](Inspectable const& sender, mux::RoutedEventArgs const&) {
            if (auto toggle = sender.try_as<muxc::AppBarToggleButton>()) {
                toggle.IsChecked(false);
            }
            WindowState* now = StateForThisThread(false);
            if (now && now->details && !g_unloading) {
                now->details->shortcutAt = GetTickCount64() + 60;
                now->details->toggleHoldUntil = GetTickCount64() + 1500;  // The tick leaves the tick alone meanwhile.
                Wh_Log(L"details pane: the toolbar toggle was chosen");
            }
        });
        bar.PrimaryCommands().InsertAt(index, button);
        native.Visibility(mux::Visibility::Collapsed);
        pane->toolToggle = button;
        pane->toolBar = bar;
        pane->toolNative = native;
        Wh_Log(L"details pane: the toolbar toggle replaces Explorer's");
    } catch (...) {
        LogHresult(L"PaneToolbarSetup");
    }
}

void PaneMenuRestore(DetailsPane* pane) {
    // Explorer keeps these elements (its toolbar and menu) after the mod is gone: a click that
    // reaches a handler whose code is unmapped would crash it. They are revoked first, each alone.
    try {
        if (pane->toolToggle && pane->toolClickToken.value) {
            pane->toolToggle.Click(pane->toolClickToken);
        }
    } catch (...) {
    }
    try {
        if (pane->menuToggle && pane->menuClickToken.value) {
            if (auto ours = pane->menuToggle.try_as<muxc::MenuFlyoutItem>()) {
                ours.Click(pane->menuClickToken);
            }
        }
    } catch (...) {
    }
    try {
        if (pane->navEntry && pane->navClickToken.value) {
            pane->navEntry.Click(pane->navClickToken);
        }
    } catch (...) {
    }
    pane->toolClickToken = {};
    pane->menuClickToken = {};
    pane->navClickToken = {};
    try {
        if (pane->toolToggle && pane->toolBar) {
            uint32_t at = 0;
            if (pane->toolBar.PrimaryCommands().IndexOf(pane->toolToggle.as<muxc::ICommandBarElement>(), at)) {
                pane->toolBar.PrimaryCommands().RemoveAt(at);
            }
        }
        if (pane->toolNative) {
            if (pane->toolIcon) {
                pane->toolToggle.Icon(nullptr);
                PaneSetButtonIcon(pane->toolNative, pane->toolIcon);
            }
            pane->toolNative.Visibility(mux::Visibility::Visible);
        }
        pane->toolToggle = nullptr;
        pane->toolBar = nullptr;
        pane->toolNative = nullptr;
        pane->toolIcon = nullptr;

        if (pane->menuItems && pane->navEntry) {
            uint32_t at = 0;
            if (pane->menuItems.IndexOf(pane->navEntry, at)) {
                pane->menuItems.RemoveAt(at);
            }
        }
        if (pane->navOriginal) {
            if (pane->navIcon && pane->navEntry) {
                pane->navEntry.Icon(nullptr);
                pane->navOriginal.Icon(pane->navIcon);
            }
            pane->navOriginal.Visibility(mux::Visibility::Visible);
        }
        pane->navIcon = nullptr;
        if (pane->navSeparator) {
            pane->navSeparator.Visibility(mux::Visibility::Visible);
            pane->navSeparator = nullptr;
        }
        pane->navEntry = nullptr;
        pane->navOriginal = nullptr;
        if (pane->movedIcon && !pane->hiddenRadios.empty()) {
            if (auto ours = pane->menuToggle.try_as<muxc::MenuFlyoutItem>()) {
                ours.Icon(nullptr);
            }
            pane->hiddenRadios[0].Icon(pane->movedIcon);
        }
        pane->movedIcon = nullptr;

        if (pane->menuItems && pane->menuToggle) {
            uint32_t at = 0;
            if (pane->menuItems.IndexOf(pane->menuToggle, at)) {
                pane->menuItems.RemoveAt(at);
            }
        }
        for (auto const& radio : pane->hiddenRadios) {
            radio.Visibility(mux::Visibility::Visible);
        }
    } catch (...) {
        LogHresult(L"PaneMenuRestore");
    }
    pane->hiddenRadios.clear();
    pane->menuToggle = nullptr;
    pane->menuItems = nullptr;
}

// The native pane went away: ours slides out before it is hidden.
void PaneLeave(WindowState* state, DetailsPane* pane) {
    pane->leaving = true;
    pane->leaveAt = GetTickCount64();
    g_paneLeaveUntil = pane->leaveAt + 320;
    Wh_Log(L"details pane: leaving");
    PaneClosePreview(pane);
    PaneSyncPanel(state);
    HWND bridgeWindow = (HWND)pane->bridge.WindowId().Value;
    SetWindowPos(bridgeWindow, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (pane->body) {
        PaneTween(state, pane, pane->body, 0, 0, 30, 0, 1.0, 0.0, 220, -1);
    }
}

void PaneShow(WindowState* state, DetailsPane* pane, std::wstring const& key, std::shared_ptr<PaneData> const& data) {
    pane->shownKey = key;
    pane->shownItems.clear();
    float dx = 0, dy = 0;
    if (key == pane->wantedKey) {
        if (pane->wantedItems.empty()) {
            if (pane->wantedFolder) {
                pane->shownItems.emplace_back(ILClone(pane->wantedFolder.get()));
            }
        } else {
            for (auto const& pidl : pane->wantedItems) {
                pane->shownItems.emplace_back(ILClone(pidl.get()));
            }
        }
        if (pane->enter) {
            dx = 24;
        } else if (pane->shownFolderPath == pane->wantedFolderPath && pane->shownIndex >= 0 && pane->wantedIndex >= 0 &&
                   pane->wantedIndex != pane->shownIndex) {
            dy = pane->wantedIndex > pane->shownIndex ? 10.0f : -10.0f;
        }
        pane->shownIndex = pane->wantedIndex;
        pane->shownFolderPath = pane->wantedFolderPath;
        pane->shownHashes = pane->wantedHashes;
    }
    pane->enter = false;
    PaneClosePreview(pane);
    if (data->kind == 3) {
        dy = 0;  // A stack of several items: the card stays, only its thumbnails change.
    }
    try {
        auto inner = pane->inner;
        pane->shownData = data;
        auto page = PaneBuild(*data, pane);
        auto layer = LoadXaml(std::wstring(L"<Grid ") + kXamlNs + L" Background='{ThemeResource SolidBackgroundFillColorBaseBrush}'/>").as<muxc::Grid>();
        layer.Children().Append(page);
        bool crossfade = g_paneAnim && dx == 0 && inner.Children().Size() > 0;
        if (!crossfade) {
            inner.Children().Clear();
        }
        inner.Children().Append(layer);
        PaneAnimateIn(state, pane, dx, dy, layer);
        if (!data->sizeRoots.empty() && g_autoFolderSize) {
            PaneStartSizeJob(state, pane, key, data);
        }
        if (g_panePreview && (data->previewKind == 2 || data->previewKind == 3) && pane->preview) {
            PaneAfter(state, pane, dx != 0 ? 600 : 300, [pane, key, data]() {
                WindowState* now = StateForThisThread(false);
                if (now && pane->visible && !pane->leaving && pane->shownKey == key) {
                    PaneStartXamlPreview(now, pane, key, data);
                }
            });
        } else if (data->previewKind >= 4 && data->previewKind <= 6 && data->largeImage && pane->preview) {
            pane->deferred = data;  // Over the size that opens by itself: the corner button opens it.
            PaneCornerUpdate(pane);
        } else if (data->previewKind >= 4 && data->previewKind <= 6 && pane->preview) {
            PaneAfter(state, pane, dx != 0 ? 600 : 300, [pane, key, data]() {
                WindowState* now = StateForThisThread(false);
                if (now && pane->visible && !pane->leaving && pane->shownKey == key) {
                    PaneStartMediaPreview(now, pane, key, data);
                }
            });
        } else if (g_panePreview && data->previewKind == 1 && pane->preview) {
            // Once the page has settled, the real preview takes the place of the thumbnail.
            PaneAfter(state, pane, dx != 0 ? 650 : 480, [pane, key, data]() {
                WindowState* now = StateForThisThread(false);
                if (now && pane->visible && !pane->leaving && pane->shownKey == key) {
                    PaneOpenPreview(now, pane, *data);
                    if (!pane->handler) {
                        PaneCornerUpdate(pane);  // It did not open: no spinner, no button.
                    }
                }
            });
        }
        if (crossfade) {
            // The old pages go once the newest one has faded in.
            PaneAfter(state, pane, 400, [inner, layer]() {
                uint32_t size = inner.Children().Size();
                if (size > 1 && winrt::get_abi(inner.Children().GetAt(size - 1)) == winrt::get_abi(layer)) {
                    while (inner.Children().Size() > 1) {
                        inner.Children().RemoveAt(0);
                    }
                }
            });
        }
    } catch (...) {
        LogHresult(L"showing the pane");
    }
}

// The details came back from the thread, on the window's own thread.
void PaneArrived(DWORD thread, unsigned generation, std::wstring const& key, std::shared_ptr<PaneData> const& data) {
    if (g_unloading || !data) {
        return;
    }
    WindowState* state = StateForThisThread(false);
    if (!state || state->thread != thread || !state->details) {
        return;
    }
    DetailsPane* pane = state->details;
    pane->cache.push_back({key, data, GetTickCount64()});
    while (pane->cache.size() > 24) {
        pane->cache.pop_front();
    }
    if (key == pane->wantedKey && pane->visible && pane->generation == generation) {
        PaneShow(state, pane, key, data);
    }
}

struct PaneJob {
    std::vector<Pidl> items;
    Pidl folder;
};

void PaneStartJob(WindowState* state, DetailsPane* pane, std::wstring const& key) {
    auto job = std::make_shared<PaneJob>();
    // The items that were just added to the selection come first, so that the newest
    // thumbnail is the one on top of the stack.
    for (int pass = 0; pass < 2; pass++) {
        for (size_t i = 0; i < pane->wantedItems.size(); i++) {
            bool known = i < pane->wantedHashes.size() &&
                         std::find(pane->shownHashes.begin(), pane->shownHashes.end(), pane->wantedHashes[i]) != pane->shownHashes.end();
            if ((pass == 0) != known) {
                job->items.emplace_back(ILClone(pane->wantedItems[i].get()));
            }
        }
    }
    if (pane->wantedFolder) {
        job->folder.reset(ILClone(pane->wantedFolder.get()));
    }
    unsigned generation = ++pane->generation;
    DWORD thread = GetCurrentThreadId();
    auto queue = state->queue;
    try {
        StartModThread([queue, thread, generation, key, job]() mutable {
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            std::shared_ptr<PaneData> data;
            try {
                ULONGLONG began = GetTickCount64();
                data = PaneRead(job->items, job->folder);
                if (GetTickCount64() - began > 250) {
                    Wh_Log(L"details pane: reading the details took %llu ms", GetTickCount64() - began);
                }
            } catch (...) {
            }
            job.reset();
            CoUninitialize();
            try {
                queue.TryEnqueue([thread, generation, key, data]() { PaneArrived(thread, generation, key, data); });
            } catch (...) {
            }
            queue = nullptr;
            data.reset();
        });
    } catch (...) {
        LogHresult(L"starting the pane's reader");
    }
}

// ---- The island and the window ----

void PaneHide(DetailsPane* pane) {
    try {
        if (pane->bridge) {
            pane->bridge.Hide();
        }
    } catch (...) {
        LogHresult(L"hiding the pane");
    }
    PaneClosePreview(pane);
    pane->visible = false;
    pane->leaving = false;
}

void PaneCreateIsland(WindowState* state, DetailsPane* pane, RECT const& r) {
    muxc::Grid root;
    auto body = LoadXaml(std::wstring(L"<Grid ") + kXamlNs + L" Background='{ThemeResource SolidBackgroundFillColorBaseBrush}'/>").as<muxc::Grid>();
    muxc::Grid inner;
    muxc::ScrollViewer scroller;  // Holds the page, which scrolls its own details.
    scroller.VerticalScrollBarVisibility(muxc::ScrollBarVisibility::Disabled);
    scroller.HorizontalScrollBarVisibility(muxc::ScrollBarVisibility::Disabled);
    body.Children().Append(inner);
    root.Children().Append(body);
    pane->root = root;
    pane->body = body;
    pane->inner = inner;
    pane->scroller = scroller;
    pane->source = mux::Hosting::DesktopWindowXamlSource();
    pane->source.Initialize(winrt::Microsoft::UI::WindowId{(uint64_t)state->topLevel});
    pane->source.Content(root);
    pane->bridge = pane->source.SiteBridge();
    pane->bridge.MoveAndResize(winrt::Windows::Graphics::RectInt32{r.left, r.top, r.right - r.left, r.bottom - r.top});
    pane->rect = r;
    Wh_Log(L"details pane: island made over the native pane, %ldx%ld at %ld,%ld", r.right - r.left, r.bottom - r.top, r.left, r.top);
}

// Explorer's own Preview pane was switched on: the merged pane is asked for instead, with the
// shortcut of Explorer's Details pane (Alt+Shift+P). The View menu's entries are found by
// position, so that it holds in every language: the two radio items that follow the layouts.
void PaneRedirectPreview(WindowState* state, DetailsPane* pane) {
    if (!g_previewRedirect) {
        return;
    }
    // Explorer's Preview pane is on when the file view stops well short of the right edge of the
    // frame and the Details pane's window is not there to fill the gap. (The Preview pane has no
    // window of its own when the file has nothing to preview, so its window cannot be looked for.)
    HWND view = FindVisibleChildCached(&state->paneView, state->topLevel, L"SHELLDLL_DefView");
    HWND tab = FindVisibleChildCached(&state->paneTab, state->topLevel, L"ShellTabWindowClass");
    bool previewOn = false;
    if (view && tab) {
        RECT v, t;
        GetWindowRect(view, &v);
        GetWindowRect(tab, &t);
        previewOn = t.right - v.right > 80 && v.right - v.left > 100;
    }
    if (!previewOn) {
        pane->redirectTries = 0;
        pane->previewSeen = 0;
        return;
    }
    if (++pane->previewSeen < 2) {
        return;  // Seen twice in a row, not in the middle of a change.
    }
    ULONGLONG now = GetTickCount64();
    if (now < pane->redirectAt || pane->redirectTries >= 3) {
        return;
    }
    HWND front = GetForegroundWindow();
    if (!front || GetAncestor(front, GA_ROOT) != state->topLevel) {
        return;
    }
    if ((GetAsyncKeyState(VK_MENU) | GetAsyncKeyState(VK_SHIFT) | GetAsyncKeyState(VK_CONTROL) | GetAsyncKeyState('P')) & 0x8000) {
        return;  // A key of the shortcut is still down.
    }
    // Explorer's Preview pane came on: its own Details entry is chosen, which swaps it for ours.
    PaneToggleNative(state, true);
    pane->redirectTries++;
    pane->redirectAt = now + 1500;
    Wh_Log(L"details pane: the Preview pane was switched on, Explorer's Details entry chosen, try %d", pane->redirectTries);
}

// What is selected in Explorer's own view, as absolute ids and a key that changes with it.
bool PaneReadSelection(WindowState* state, DetailsPane* pane, std::wstring* key) {
    IShellBrowser* browser = BrowserOf(state->topLevel);
    if (!browser) {
        return false;
    }
    Pidl folder = CurrentFolder(browser);
    if (!folder) {
        return false;
    }
    auto children = ExplorerSelection(browser);
    unsigned long long hash = 1469598103934665603ULL;
    for (auto const& child : children) {
        UINT size = ILGetSize(child.get());
        const unsigned char* bytes = (const unsigned char*)child.get();
        for (UINT i = 0; i < size; i++) {
            hash = (hash ^ bytes[i]) * 1099511628211ULL;
        }
    }
    wchar_t tail[64];
    swprintf(tail, ARRAYSIZE(tail), L"|%d|%llx", (int)children.size(), hash);
    std::wstring next = PathOf(folder.get()) + tail;
    if (next != pane->wantedKey) {
        LogVerbose(L"details pane: Explorer's selection is now %d item(s) in \"%s\"", (int)children.size(), PathOf(folder.get()).c_str());
        pane->wantedKey = next;
        PaneClosePreview(pane);
        pane->wantedSince = GetTickCount64();
        pane->wantedFolderPath = PathOf(folder.get());
        pane->wantedIndex = -1;
        if (IFolderView* view = FolderViewOf(browser)) {
            int index = -1;
            if (SUCCEEDED(view->GetFocusedItem(&index))) {
                pane->wantedIndex = index;
            }
            // Columns: the focus of Explorer's own view may not follow, so find the first
            // selected item's place in the list.
            if (!children.empty()) {
                int count = 0;
                if (SUCCEEDED(view->ItemCount(SVGIO_ALLVIEW, &count)) && count > 0 && count <= 4000) {
                    for (int i = 0; i < count; i++) {
                        PITEMID_CHILD item = nullptr;
                        if (SUCCEEDED(view->Item(i, &item)) && item) {
                            bool same = ILIsEqual(item, children[0].get());
                            CoTaskMemFree(item);
                            if (same) {
                                pane->wantedIndex = i;
                                break;
                            }
                        }
                    }
                }
            }
            view->Release();
        }
        pane->wantedItems.clear();
        pane->wantedHashes.clear();
        for (size_t i = 0; i < children.size() && i < 2000; i++) {
            pane->wantedItems.emplace_back(ILCombine(folder.get(), children[i].get()));
            uint64_t each = 1469598103934665603ULL;
            UINT size = ILGetSize(children[i].get());
            const unsigned char* bytes = (const unsigned char*)children[i].get();
            for (UINT b = 0; b < size; b++) {
                each = (each ^ bytes[b]) * 1099511628211ULL;
            }
            pane->wantedHashes.push_back(each);
        }
        pane->wantedFolder.reset(ILClone(folder.get()));
    }
    *key = next;
    return true;
}

void PaneStep(WindowState* state, DetailsPane* pane) {
    pane->ticks++;
    PaneSyncPanel(state);
    PaneMediaTick(pane);
    if (pane->shortcutAt && GetTickCount64() >= pane->shortcutAt) {
        pane->shortcutAt = 0;
        PaneToggleNative(state);
    }
    if (pane->navInvokeAt && GetTickCount64() >= pane->navInvokeAt && pane->navOriginal) {
        pane->navInvokeAt = 0;
        auto original = pane->navOriginal;
        try {
            // Shown for the moment of the call, hidden again after it.
            original.Visibility(mux::Visibility::Visible);
            auto peer = mux::Automation::Peers::FrameworkElementAutomationPeer::CreatePeerForElement(original);
            bool done = false;
            if (auto invoke = peer.GetPattern(mux::Automation::Peers::PatternInterface::Invoke)
                                  .try_as<mux::Automation::Provider::IInvokeProvider>()) {
                invoke.Invoke();
                done = true;
            } else if (auto toggle = peer.GetPattern(mux::Automation::Peers::PatternInterface::Toggle)
                                         .try_as<mux::Automation::Provider::IToggleProvider>()) {
                toggle.Toggle();
                done = true;
            }
            Wh_Log(L"navigation pane: the entry was %s, Explorer's is now %s", done ? L"used" : L"not usable",
                original.IsChecked() ? L"checked" : L"unchecked");
            PaneAfter(state, pane, 200, [original]() { original.Visibility(mux::Visibility::Collapsed); });
        } catch (...) {
            LogHresult(L"the navigation pane entry");
        }
    }
    if (pane->toolToggle && pane->toolToggle.IsChecked().Value()) {
        pane->toolToggle.IsChecked(false);  // A click flips it; it is set back, so it never shows as ticked.
    }
    if (pane->navEntry && pane->navOriginal && pane->navEntry.IsChecked() != pane->navOriginal.IsChecked()) {
        pane->navEntry.IsChecked(pane->navOriginal.IsChecked());
    }
    if (pane->menuToggle) {
        if (auto toggle = pane->menuToggle.try_as<muxc::ToggleMenuFlyoutItem>()) {
            bool want = pane->visible && !pane->leaving;
            bool held = pane->toggleHoldUntil && GetTickCount64() < pane->toggleHoldUntil;
            if (toggle.IsChecked() != want && !held) {
                toggle.IsChecked(want);
            }
        }
    }
    HWND host = g_detailsPane ? FindVisibleChildCached(&state->paneHost, state->topLevel, L"DetailsPaneHwndHostClass") : nullptr;
    RECT r = {};
    bool gone = !host;
    if (host) {
        GetWindowRect(host, &r);
        MapWindowPoints(HWND_DESKTOP, state->topLevel, (POINT*)&r, 2);
        gone = r.right - r.left < 40 || r.bottom - r.top < 40;
    }
    if (gone) {
        if (pane->visible) {
            if (!g_paneAnim) {
                PaneHide(pane);
            } else if (!pane->leaving) {
                PaneLeave(state, pane);
            } else if (GetTickCount64() - pane->leaveAt >= 230) {
                PaneHide(pane);
            }
        }
        if (!host && g_detailsPane && pane->ticks % 5 == 0) {
            PaneRedirectPreview(state, pane);
        }
        return;
    }
    if (pane->leaving) {
        // The pane came back before it was hidden.
        pane->leaving = false;
        pane->shownKey.clear();
        pane->requestedKey.clear();
        pane->enter = true;
    }
    if (!pane->source) {
        if (pane->failures >= 3) {
            return;
        }
        try {
            PaneCreateIsland(state, pane, r);
        } catch (...) {
            pane->failures++;
            LogHresult(L"making the pane's island");
            return;
        }
    } else if (!EqualRect(&r, &pane->rect)) {
        pane->bridge.MoveAndResize(winrt::Windows::Graphics::RectInt32{r.left, r.top, r.right - r.left, r.bottom - r.top});
        pane->rect = r;
        // While the pane is being resized the preview is hidden (it jumped with each step) and
        // comes back when the size has not changed for a moment.
        if (pane->handler && pane->host) {
            ShowWindow(pane->host, SW_HIDE);
            pane->settleAt = GetTickCount64();
        }
    } else if (pane->settleAt) {
        if (GetTickCount64() - pane->settleAt >= 180) {
            pane->settleAt = 0;
            pane->appliedRect = RECT{};
            PaneLayoutPreview(state, pane);
        }
    } else if (pane->handler) {
        PaneLayoutPreview(state, pane);  // Follows the card while the page settles.
    }
    HWND bridgeWindow = (HWND)pane->bridge.WindowId().Value;
    if (!pane->visible) {
        pane->bridge.Show();
        SetWindowPos(bridgeWindow, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        pane->visible = true;
        pane->shownKey.clear();
        pane->requestedKey.clear();
        pane->enter = true;
        Wh_Log(L"details pane: shown");
    }
    // The mouse over the preview gives it the keyboard focus (so that the wheel and the keys
    // scroll it); leaving takes the focus back to the file list.
    if (pane->handler && pane->host && IsWindow(pane->host) && !pane->dragging) {
        POINT cursor;
        RECT hostRect;
        GetCursorPos(&cursor);
        GetWindowRect(pane->host, &hostRect);
        bool over = IsWindowVisible(pane->host) && PtInRect(&hostRect, cursor);
        if (over) {
            pane->overTicks++;
            if (!pane->previewFocused && pane->overTicks >= 3 && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                // The handler is asked to take the focus itself: its window may be in another process,
                // and Explorer's input must not be attached to that process's.
                HRESULT focus = pane->handler->SetFocus();
                Wh_Log(L"preview: focus asked, handler 0x%08lx", (unsigned long)focus);
                pane->previewFocused = true;
            }
        } else {
            pane->overTicks = 0;
            if (pane->previewFocused) {
                pane->previewFocused = false;
                PaneGiveFocusBack(state);
            }
        }
    }
    std::wstring key;
    if (!PaneReadSelection(state, pane, &key)) {
        return;
    }
    if (key != pane->shownKey && key != pane->requestedKey && GetTickCount64() - pane->wantedSince >= 90) {
        pane->requestedKey = key;
        for (auto const& entry : pane->cache) {
            if (entry.key == key && GetTickCount64() - entry.at < 15000) {
                PaneShow(state, pane, key, entry.data);
                return;
            }
        }
        PaneStartJob(state, pane, key);
    }
}

void DetailsTick() {
    if (g_unloading) {
        return;
    }
    WindowState* state = StateForThisThread(false);
    if (!state || !state->details) {
        return;
    }
    if (!state->topLevel || !IsWindow(state->topLevel)) {
        AbandonDetails(state);
        return;
    }
    try {
        DetailsPane* pane = state->details;
        bool busy = pane->leaving || pane->enter || pane->loading || pane->shortcutAt || pane->navInvokeAt ||
                    pane->ticks < 15 || pane->mediaPlayer || pane->dragging;
        RetimeTick(pane->timer, &pane->tickMs, TickIntervalMs(state->topLevel, busy));
        PaneStep(state, state->details);
    } catch (...) {
        static int logged = 0;
        if (logged++ < 5) {
            LogHresult(L"the details pane's tick");
        }
    }
}

void StartDetails(WindowState* state) {
    if (state->details || !state->queue) {
        return;
    }
    try {
        auto* pane = new DetailsPane();
        state->details = pane;
        auto timer = state->queue.CreateTimer();
        timer.Interval(std::chrono::milliseconds(60));
        pane->token = timer.Tick([](mud::DispatcherQueueTimer const&, Inspectable const&) { DetailsTick(); });
        timer.Start();
        pane->timer = timer;
        Wh_Log(L"details pane: watching window %p", state->topLevel);
    } catch (...) {
        LogHresult(L"StartDetails");
    }
}

void PaneStopTimers(DetailsPane* pane) {
    try {
        if (pane->timer) {
            pane->timer.Stop();
            pane->timer.Tick(pane->token);
        }
        for (auto& shot : pane->shots) {
            shot.timer.Stop();
            shot.timer.Tick(shot.token);
        }
    } catch (...) {
        LogHresult(L"stopping the pane's timers");
    }
}

// The mod unloads: everything is released, on the window's thread.
void ReleaseDetails(WindowState* state) {
    DetailsPane* pane = state->details;
    if (!pane) {
        return;
    }
    state->details = nullptr;
    PaneStopTimers(pane);
    PaneMenuRestore(pane);
    pane->shots.clear();
    pane->timer = nullptr;
    PaneClosePreview(pane);
    if (pane->host && IsWindow(pane->host)) {
        DestroyWindow(pane->host);
    }
    pane->host = nullptr;
    try {
        if (pane->bridge) {
            pane->bridge.Hide();
        }
        if (pane->source) {
            pane->source.Content(nullptr);
            pane->source.Close();
        }
    } catch (...) {
        LogHresult(L"releasing the details pane");
    }
    delete pane;
}

// The window closed: the objects are let go, never released (they belong to a window that is gone).
void AbandonDetails(WindowState* state) {
    DetailsPane* pane = state->details;
    if (!pane) {
        return;
    }
    state->details = nullptr;
    try {
        PaneStopMedia(pane);  // A song or video must not go on playing with no window to stop it.
    } catch (...) {
    }
    pane->handler = nullptr;  // Let go: the window it lived in is gone.
    pane->host = nullptr;
    PaneStopTimers(pane);
    for (auto& shot : pane->shots) {
        (void)winrt::detach_abi(shot.timer);
    }
    (void)winrt::detach_abi(pane->timer);
    (void)winrt::detach_abi(pane->source);
    (void)winrt::detach_abi(pane->bridge);
    (void)winrt::detach_abi(pane->root);
    (void)winrt::detach_abi(pane->body);
    (void)winrt::detach_abi(pane->inner);
    (void)winrt::detach_abi(pane->preview);
    (void)winrt::detach_abi(pane->spinner);
    (void)winrt::detach_abi(pane->toggle);
    (void)winrt::detach_abi(pane->menuToggle);
    (void)winrt::detach_abi(pane->navEntry);
    (void)winrt::detach_abi(pane->navOriginal);
    (void)winrt::detach_abi(pane->navIcon);
    (void)winrt::detach_abi(pane->toolToggle);
    (void)winrt::detach_abi(pane->toolNative);
    (void)winrt::detach_abi(pane->toolBar);
    (void)winrt::detach_abi(pane->toolIcon);
    (void)winrt::detach_abi(pane->mediaPlayer);
    (void)winrt::detach_abi(pane->pendingImage);
    (void)winrt::detach_abi(pane->mediaElement);
    (void)winrt::detach_abi(pane->mediaSlider);
    (void)winrt::detach_abi(pane->mediaTime);
    (void)winrt::detach_abi(pane->mediaPlay);
    (void)winrt::detach_abi(pane->mediaVolume);
    if (pane->sizeCancel) {
        *pane->sizeCancel = true;
    }
    for (auto& block : pane->sizeBlocks) {
        (void)winrt::detach_abi(block);
    }
    pane->sizeBlocks.clear();
    (void)winrt::detach_abi(pane->navSeparator);
    (void)winrt::detach_abi(pane->movedIcon);
    (void)winrt::detach_abi(pane->menuItems);
    for (auto& radio : pane->hiddenRadios) {
        (void)winrt::detach_abi(radio);
    }
    pane->hiddenRadios.clear();
    (void)winrt::detach_abi(pane->layered);
    (void)winrt::detach_abi(pane->previewLayer);
    (void)winrt::detach_abi(pane->thumb);
    (void)winrt::detach_abi(pane->scroller);
    Wh_Log(L"window closed, the details pane's objects are let go");
    // The pane itself is not deleted: it still holds what the XAML objects held.
}

