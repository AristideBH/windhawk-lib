// Labels on Explorer's own file list. Explorer draws the list with DirectUI. A hook on the exported
// Element::PaintContent of dui70.dll sees every element as it is painted: after a row (a "UIItem") has been
// painted, the dot of its tags is drawn next to its name. The tags come from the folder cache (04b), looked up by
// the row's accessible name, so painting never touches the disk. Where a dot goes depends on the view mode, see the
// surfaces skill and docs/superpowers/specs/2026-11-17-labels-design.md.

enum class LabelsView { None, Details, List, Tiles, Content, Icons };

struct LabelsPlaceInput {
    LabelsView view = LabelsView::None;
    RECT row = {};          // The row (UIItem).
    RECT cell = {};         // The cell the dot is placed against, in the same coordinates.
    int textLeft = 0;       // The text in that cell. textRight < textLeft when the cell has no text.
    int textRight = -1;
    int lineHeight = 16;
    int iconLeft = 0;       // The icon, for the corner of it.
    int iconBottom = 0;
    int blockWidth = 8;     // All the dots and the plus, side by side.
    int side = 8;
    int columnPad = 18;     // The padding after a Details cell, up to the next column.
};

struct LabelsPlace {
    int x = 0;  // Left edge of the block of dots.
    int centreY = 0;
    bool corner = false;  // There is no room by the name: the block goes on the icon's bottom-left corner.
};

// The gap between two dots.
static int LabelsDotGap(int side) {
    return std::max(2, side / 4);
}

// How far each dot is after the one before: the dots are stacked, each one partly under the one on its left.
int LabelsDotStep(int side) {
    return side * 3 / 4;
}

// The width of `count` stacked dots, plus a gap and a "plus" when there are more.
int LabelsBlockWidth(int count, bool more, int side) {
    if (count <= 0) {
        return 0;
    }
    return side + (count - 1) * LabelsDotStep(side) + (more ? std::max(4, side / 2) + LabelsMoreWidth(side) : 0);
}

// The colours to draw: each colour once, in the order of the tags, at most four, and whether more were left out.
std::vector<int> LabelsDotColours(std::vector<LabelTag> const& tags, bool* more) {
    std::vector<int> colours;
    *more = false;
    for (auto const& tag : tags) {
        if (std::find(colours.begin(), colours.end(), (int)tag.colour) != colours.end()) {
            continue;
        }
        if (colours.size() >= 4) {
            *more = true;
            break;
        }
        colours.push_back(tag.colour);
    }
    return colours;
}

// How many lines of text a block holds, and whether a property that ends at `bottom` is on the last of them.
int LabelsLines(int blockHeight, int lineHeight) {
    return lineHeight > 0 ? std::max(1, (blockHeight + lineHeight / 2) / lineHeight) : 1;
}

bool LabelsIsLastLine(int bottom, int blockBottom, int lineHeight) {
    return bottom >= blockBottom - lineHeight / 2;
}

// Where the dots go, from the geometry of the row. Pure, so it is tested without Explorer.
LabelsPlace LabelsPlaceFor(LabelsPlaceInput const& in) {
    LabelsPlace place;
    const int gap = 6;
    const int margin = std::max(4, in.side * 3 / 4);
    switch (in.view) {
        case LabelsView::List: {
            int height = in.row.bottom - in.row.top;
            place.x = in.row.left + (height - in.side) / 2;
            place.centreY = in.row.top + height / 2;
            return place;
        }
        case LabelsView::Details:
            // Right-aligned in the name cell, the same place on every row.
            place.x = in.cell.right - in.blockWidth - margin;
            place.centreY = (in.cell.top + in.cell.bottom) / 2;
            break;
        case LabelsView::Tiles:
            place.x = in.textRight >= in.textLeft ? in.textRight + gap : in.cell.left;
            place.centreY = (in.cell.top + in.cell.bottom) / 2 - in.lineHeight / 8 + 2 * std::max(1, in.side / 8);  // On the text line, a pixel lower than Content.
            place.corner = place.x + in.blockWidth > in.cell.right;
            break;
        case LabelsView::Content:
            if (in.textRight >= in.textLeft) {
                // The cell is the type line: after its text, a little further than elsewhere (the width of the type text is
                // measured, and ends close to the dot), on the line of the text.
                place.x = in.textRight + gap + in.side / 2;
                place.centreY = (in.cell.top + in.cell.bottom) / 2 - in.lineHeight / 8 + std::max(1, in.side / 8);
            } else {
                // The cell is the name line: at the start of the line below it.
                place.x = in.cell.left;
                place.centreY = in.cell.top + in.lineHeight * 3 / 2;
            }
            break;
        case LabelsView::Icons: {
            // The cell is the last line of the name: after its text, or on a new line below when it has no room.
            int width = in.cell.right - in.cell.left;
            place.x = in.textRight + gap;
            place.centreY = (in.cell.top + in.cell.bottom) / 2;
            if (place.x + in.blockWidth > in.cell.right) {
                place.x = in.cell.left + (width - in.blockWidth) / 2;
                place.centreY = in.cell.bottom + in.lineHeight / 2;
            }
            break;
        }
        default:
            break;
    }
    if (place.corner) {
        place.x = in.iconLeft + 2;
        place.centreY = in.iconBottom - in.side / 2 - 2;
    }
    return place;
}

void LabelsRefreshColumns();  // 10g2

namespace {

bool g_labelsPaintOk = false;
int g_labelsClassSlot = -1;
int g_labelsNameSlot = -1;

using LabelsGetAccName_t = const wchar_t* (*)(void* self, void** out);
using LabelsValueRelease_t = void (*)(void* value);
using LabelsGetChildren_t = void* (*)(void* self, void** out);
using LabelsGetLocation_t = const POINT* (*)(void* self, void** out);
using LabelsGetExtent_t = const SIZE* (*)(void* self, void** out);
using LabelsGetParent_t = void* (*)(void* self);
LabelsGetAccName_t LabelsGetAccName = nullptr;
LabelsValueRelease_t LabelsValueRelease = nullptr;
LabelsGetChildren_t LabelsGetChildren = nullptr;
LabelsGetLocation_t LabelsGetLocation = nullptr;
LabelsGetExtent_t LabelsGetExtent = nullptr;
LabelsGetParent_t LabelsGetParent = nullptr;

void* LabelsExport(HMODULE module, const char* prefix) {
    auto base = (BYTE*)module;
    auto dos = (IMAGE_DOS_HEADER*)base;
    auto nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress) {
        return nullptr;
    }
    auto exports = (IMAGE_EXPORT_DIRECTORY*)(base + dir.VirtualAddress);
    auto names = (DWORD*)(base + exports->AddressOfNames);
    size_t length = strlen(prefix);
    for (DWORD i = 0; i < exports->NumberOfNames; i++) {
        const char* name = (const char*)(base + names[i]);
        if (strncmp(name, prefix, length) == 0) {
            return (void*)GetProcAddress(module, name);
        }
    }
    return nullptr;
}

// Is [p, p + size) committed memory that can be read? VirtualQuery, which neither raises nor swallows a guard page.
bool LabelsReadable(const void* p, size_t size) {
    MEMORY_BASIC_INFORMATION info;
    if (!p || !VirtualQuery(p, &info, sizeof(info))) {
        return false;
    }
    if (info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) || info.Protect == 0) {
        return false;
    }
    return (const char*)p + size <= (const char*)info.BaseAddress + info.RegionSize;
}

int LabelsFindSlot(void* vtable, void* function) {
    if (!vtable || !function) {
        return -1;
    }
    void** slots = (void**)vtable;
    for (int i = 0; i < 120; i++) {
        if (!LabelsReadable(&slots[i], sizeof(void*))) {
            break;
        }
        if (slots[i] == function) {
            return i;
        }
    }
    return -1;
}

enum LabelsKind { kOther, kItem, kProperty, kTilesCollection, kSmartCollection, kElement, kImage, kCheckbox,
                  kDetailsCollection, kBase, kSmartProperty };

LabelsKind LabelsKindOf(void* element) {
    [[clang::no_destroy]] thread_local std::unordered_map<void*, LabelsKind> kinds;
    void* vtable = *(void**)element;
    auto found = kinds.find(vtable);
    if (found != kinds.end()) {
        return found->second;
    }
    LabelsKind kind = kOther;
    void** table = (void**)vtable;
    using GetClassInfo_t = void* (*)(void*);
    void* classInfo = ((GetClassInfo_t)table[g_labelsClassSlot])(element);
    if (classInfo && LabelsReadable(classInfo, sizeof(void*))) {
        void** infoTable = *(void***)classInfo;
        using GetName_t = const wchar_t* (*)(void*);
        const wchar_t* name = ((GetName_t)infoTable[g_labelsNameSlot])(classInfo);
        if (name) {  // DirectUI's own class name
            struct Row { const wchar_t* name; LabelsKind kind; };
            static const Row rows[] = {{L"UIItem", kItem}, {L"UIProperty", kProperty},
                                       {L"UITilesPropertyCollection", kTilesCollection},
                                       {L"UISmartPropertyCollection", kSmartCollection}, {L"Element", kElement},
                                       {L"UIImage", kImage}, {L"UICheckbox", kCheckbox},
                                       {L"UIDetailsPropertyCollection", kDetailsCollection}, {L"UIBase", kBase},
                                       {L"UISmartProperty", kSmartProperty}};
            for (auto const& row : rows) {
                if (wcscmp(name, row.name) == 0) {
                    kind = row.kind;
                }
            }
        }
    }
    if (kinds.size() < 400) {
        kinds[vtable] = kind;
    }
    return kind;
}

std::wstring LabelsAccName(void* element) {
    void* value = nullptr;
    const wchar_t* text = LabelsGetAccName(element, &value);
    std::wstring result;
    if (text) {  // DirectUI's own accessible name
        result = text;
    }
    if (value) {
        LabelsValueRelease(value);
    }
    return result;
}

// The elements of a DynamicArray<Element*>: a data pointer and a count are searched for among its first words.
bool LabelsReadArray(void* array, std::vector<void*>* out) {
    if (!LabelsReadable(array, 24)) {
        return false;
    }
    unsigned long long q[3];
    memcpy(q, array, 24);
    for (int d = 0; d < 3; d++) {
        for (int s = 0; s < 3; s++) {
            for (int half = 0; half < 2; half++) {
                if (s == d) {
                    continue;
                }
                unsigned long long count = half ? (q[s] >> 32) : (q[s] & 0xffffffffull);
                void** data = (void**)q[d];
                if (count == 0 || count > 60 || !LabelsReadable(data, count * sizeof(void*))) {
                    continue;
                }
                bool ok = true;
                for (unsigned long long i = 0; i < count && ok; i++) {
                    ok = LabelsReadable(data[i], 8) && LabelsReadable(*(void**)data[i], 8);
                }
                if (ok) {
                    out->assign(data, data + count);
                    return true;
                }
            }
        }
    }
    return false;
}

struct LabelsChild {
    void* element = nullptr;
    LabelsKind kind = kOther;
    POINT at = {};
    SIZE size = {};
};

std::vector<LabelsChild> LabelsChildrenOf(void* element) {
    std::vector<LabelsChild> result;
    void* value = nullptr;
    void* array = LabelsGetChildren(element, &value);
    std::vector<void*> children;
    bool ok = LabelsReadArray(array, &children);
    if (value) {
        LabelsValueRelease(value);
    }
    if (!ok) {
        return result;
    }
    for (void* child : children) {
        LabelsChild entry;
        entry.element = child;
        entry.kind = LabelsKindOf(child);
        void* v1 = nullptr;
        void* v2 = nullptr;
        const POINT* at = LabelsGetLocation(child, &v1);
        const SIZE* size = LabelsGetExtent(child, &v2);
        if (at && LabelsReadable(at, sizeof(POINT))) {
            entry.at = *at;
        }
        if (size && LabelsReadable(size, sizeof(SIZE))) {
            entry.size = *size;
        }
        if (v1) LabelsValueRelease(v1);
        if (v2) LabelsValueRelease(v2);
        result.push_back(entry);
    }
    return result;
}

// The DPI of the Explorer window of this thread, looked up at most twice a second.
UINT LabelsThreadDpi() {
    thread_local UINT dpi = 0;
    thread_local ULONGLONG at = 0;
    ULONGLONG now = GetTickCount64();
    if (dpi && now - at < 500) {
        return dpi;
    }
    struct Find {
        UINT dpi = 0;
    } find;
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM param) -> BOOL {
        wchar_t name[40];
        if (GetClassNameW(window, name, 40) && wcscmp(name, L"CabinetWClass") == 0) {
            ((Find*)param)->dpi = GetDpiForWindow(window);
            return FALSE;
        }
        return TRUE;
    }, (LPARAM)&find);
    dpi = find.dpi ? find.dpi : GetDpiForSystem();
    at = now;
    return dpi;
}

// The Explorer window of this thread. A window the mod has not seen being created (one that was open before the
// mod loaded) has no state of its own, so its top-level window is looked up by class.
HWND LabelsThreadTopLevel() {
    WindowState* state = StateForThisThread(false);
    if (state && state->topLevel) {
        return state->topLevel;
    }
    struct Find {
        HWND window = nullptr;
    } find;
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM param) -> BOOL {
        wchar_t name[40];
        if (GetClassNameW(window, name, 40) && wcscmp(name, L"CabinetWClass") == 0) {
            ((Find*)param)->window = window;
            return FALSE;
        }
        return TRUE;
    }, (LPARAM)&find);
    return find.window;
}

// The folder this thread's Explorer window shows, as a path, looked up at most four times a second.
std::wstring LabelsThreadFolder() {
    [[clang::no_destroy]] thread_local std::wstring path;
    thread_local ULONGLONG at = 0;
    ULONGLONG now = GetTickCount64();
    if (at && now - at < 250) {
        return path;
    }
    at = now;
    std::wstring before = path;
    try {
        HWND topLevel = LabelsThreadTopLevel();
        if (topLevel) {
            IShellBrowser* browser = BrowserOf(topLevel);
            if (browser) {
                Pidl folder = CurrentFolder(browser);
                path = folder ? PathOf(folder.get()) : std::wstring();
            }
        }
    } catch (...) {
    }
    if (path != before) {
        // Another folder is shown: the one that was shown stops being read, and the new one is read again when it was
        // cached, so what changed since (a rename, a tag from another program) shows. The old data is used meanwhile.
        if (!before.empty()) {
            LabelsCancelFolder(before);
        }
        if (!path.empty()) {
            LabelsInvalidate(path);
        }
    }
    return path;
}

// The width of a text in the list's font (Segoe UI, about 13 px at 96 DPI; the Content view's secondary lines are
// about 11), measured on `dc`.
[[clang::no_destroy]] thread_local std::map<std::pair<UINT, int>, HFONT> t_labelsFonts;

int LabelsFontCount() {
    return (int)t_labelsFonts.size();
}

// Frees the fonts of this thread. At unload each thread does it for itself (RevokeKeptThreads).
void LabelsReleaseFonts() {
    for (auto& entry : t_labelsFonts) {
        if (entry.second) {
            DeleteObject(entry.second);
        }
    }
    t_labelsFonts.clear();
}

int LabelsTextWidth(HDC dc, std::wstring const& text, UINT dpi, int fontPx = 13) {
    HFONT& font = t_labelsFonts[{dpi, fontPx}];
    if (!font) {
        RegisterKeepThread();  // This thread will be asked to free its fonts at unload.
        font = CreateFontW(-MulDiv(fontPx, (int)dpi, 96), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                           CLEARTYPE_QUALITY, 0, L"Segoe UI");
    }
    if (!font) {
        return 0;
    }
    HGDIOBJ old = SelectObject(dc, font);
    SIZE size = {};
    GetTextExtentPoint32W(dc, text.c_str(), (int)text.size(), &size);
    SelectObject(dc, old);
    return size.cx;
}

// The right edge of the text drawn inside `band`: the last column with a pixel that differs from the corner's.
bool LabelsScanText(HDC dc, RECT const& band, int* left, int* right) {
    int width = band.right - band.left;
    int height = band.bottom - band.top;
    if (width <= 0 || height <= 0 || width > 1200 || height > 120) {
        return false;
    }
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    uint32_t* bits = nullptr;
    HDC memory = CreateCompatibleDC(dc);
    HBITMAP bitmap = memory ? CreateDIBSection(memory, &info, DIB_RGB_COLORS, (void**)&bits, nullptr, 0) : nullptr;
    bool found = false;
    if (bitmap && bits) {
        HGDIOBJ old = SelectObject(memory, bitmap);
        BitBlt(memory, 0, 0, width, height, dc, band.left, band.top, SRCCOPY);
        uint32_t background = bits[width - 1] & 0xFFFFFF;
        *left = width;
        *right = -1;
        for (int x = 0; x < width; x++) {
            for (int y = 0; y < height; y++) {
                if ((bits[y * width + x] & 0xFFFFFF) != background) {
                    *left = std::min(*left, x);
                    *right = std::max(*right, x);
                    break;
                }
            }
        }
        found = *right >= 0;
        *left += band.left;
        *right += band.left;
        SelectObject(memory, old);
    }
    if (bitmap) DeleteObject(bitmap);
    if (memory) DeleteDC(memory);
    return found;
}

// What the row that was just painted needs from the elements that paint after it.
struct LabelsRowContext {
    void* item = nullptr;
    LabelsView view = LabelsView::None;
    RECT row = {};
    RECT block = {};  // The text block of Tiles and Icons rows, in the device context's coordinates.
    RECT name = {};   // The name line of a Content row, once it has been painted.
    int nameTextLeft = 0;  // Where its text starts.
    bool haveName = false;
    bool folder = false;
    std::wstring typeName;  // The text of the type line of a Content row, from the cache.
    UINT dpi = 96;
    int iconLeft = 0;
    int iconBottom = 0;
    std::vector<int> colours;
    bool more = false;
    int side = 8;
    int lineHeight = 16;
    int columnPad = 18;
    bool drawn = false;
};
thread_local LabelsRowContext t_labelsRow;
thread_local int t_contentTextOffset = -1;  // Where a Content row's text starts, learned from a row that has a Type line.

void LabelsDrawBlock(HDC dc, LabelsRowContext const& row, LabelsPlace const& place) {
    int step = LabelsDotStep(row.side);
    int count = (int)row.colours.size();
    // What is behind the dots, so that each dot can be cut free of the one it overlaps in the colour of the row.
    COLORREF background = GetPixel(dc, place.x - 2, place.centreY - row.side / 2 - 1);
    if (background == CLR_INVALID) {
        background = LabelsDarkTheme() ? RGB(25, 25, 25) : RGB(255, 255, 255);
    }
    // The last dot first: each one is drawn over the one on its right, the first on top.
    for (int i = count - 1; i >= 0; i--) {
        int x = place.x + i * step;
        if (i < count - 1) {
            LabelsDrawDisc(dc, x - 1, place.centreY, background, row.side + 2);
        }
        LabelsDrawDot(dc, x, place.centreY, row.colours[i], row.side);
    }
    if (row.more && count > 0) {
        LabelsDrawMore(dc, place.x + (count - 1) * step + row.side + std::max(4, row.side / 2), place.centreY, row.side);
    }
}

// Fewer dots, and a plus, when the room is narrow: the colours that do not fit are left out.
void LabelsFitDots(LabelsRowContext* row, int available) {
    while (row->colours.size() > 1 && LabelsBlockWidth((int)row->colours.size(), row->more, row->side) > available) {
        row->colours.pop_back();
        row->more = true;
    }
}

// The filter of this thread's window, or null when it has none. A debug build also reads
// %LOCALAPPDATA%\\ExplorerExtras\\labels-filter.txt ("colour=3" or "name=Urgent", empty or missing for none), about
// twice a second, so the dimming can be tried before the pane that sets the filter exists.
// Is any window filtered? The paint hook asks this first, so a paint does not take the lock on the window states for nothing.
// A source of a filter (the debug build's file, or a pane later) must set it.
#ifdef EXTRAS_VERBOSE
std::atomic<bool> g_labelsFilterAny{true};  // The debug build polls a file for a filter.
#else
std::atomic<bool> g_labelsFilterAny{false};
#endif

LabelsFilter* LabelsFilterOfThread() {
    WindowState* state = StateForThisThread(false);
    if (!state) {
        return nullptr;
    }
#ifdef EXTRAS_VERBOSE
    thread_local ULONGLONG polled = 0;
    ULONGLONG now = GetTickCount64();
    if (now - polled > 500) {
        polled = now;
        wchar_t local[MAX_PATH];
        std::wstring text;
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH)) {
            HANDLE file = CreateFileW((std::wstring(local) + L"\\ExplorerExtras\\labels-filter.txt").c_str(), GENERIC_READ,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                char buffer[200] = "";
                DWORD read = 0;
                ReadFile(file, buffer, sizeof(buffer) - 1, &read, nullptr);
                CloseHandle(file);
                buffer[read] = 0;
                wchar_t wide[200] = L"";
                MultiByteToWideChar(CP_UTF8, 0, buffer, -1, wide, 200);
                text = wide;
            }
        }
        LabelsFilter filter;
        if (text.rfind(L"colour=", 0) == 0) {
            filter.on = true;
            filter.colour = (uint8_t)std::clamp(_wtoi(text.c_str() + 7), 0, 7);
        } else if (text.rfind(L"name=", 0) == 0) {
            filter.on = true;
            filter.name = text.substr(5);
            while (!filter.name.empty() && (filter.name.back() == L'\r' || filter.name.back() == L'\n')) {
                filter.name.pop_back();
            }
        }
        filter.folder = LabelsThreadFolder();
        if (filter.on != state->labelsFilter.on || filter.colour != state->labelsFilter.colour ||
            filter.name != state->labelsFilter.name) {
            state->labelsFilter = filter;
        }
    }
#endif
    LabelsFilter& filter = state->labelsFilter;
    if (filter.on && !filter.folder.empty() && _wcsicmp(filter.folder.c_str(), LabelsThreadFolder().c_str()) != 0) {
        filter.on = false;  // Another folder is shown: the filter ends.
    }
    return filter.on ? &filter : nullptr;
}

// True when the row is to be dimmed: a filter is on, the row's name is not ambiguous, and its tags do not match.
bool LabelsRowDims(void* item, LabelsFilter const& filter) {
    std::wstring folder = LabelsThreadFolder();
    if (folder.empty()) {
        return false;
    }
    auto data = LabelsFolderFor(folder);
    if (!data->complete || !data->streamsSupported) {
        return false;
    }
    std::wstring name = LabelsAccName(item);
    if (data->ambiguous.count(name)) {
        return false;  // Cannot be told from the other item of that name.
    }
    auto found = data->byName.find(name);
    static const std::vector<LabelTag> none;
    return !LabelsMatches(filter, found == data->byName.end() ? none : found->second);
}

// Fades what a child painted toward the colour behind it (a hover or a selection colour included), about 60 percent.
void LabelsDimRect(HDC dc, const RECT* r, COLORREF colour) {
    int w = r->right - r->left, h = r->bottom - r->top;
    if (w <= 0 || h <= 0 || w > 2000 || h > 2000) {
        return;
    }
    HDC memory = CreateCompatibleDC(dc);
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 1;
    info.bmiHeader.biHeight = -1;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = memory ? CreateDIBSection(memory, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    if (bitmap && bits) {
        *(DWORD*)bits = ((colour & 0xFF) << 16) | (colour & 0xFF00) | ((colour >> 16) & 0xFF);  // COLORREF to 0x00RRGGBB.
        HGDIOBJ old = SelectObject(memory, bitmap);
        BLENDFUNCTION blend = {AC_SRC_OVER, 0, 150, 0};
        AlphaBlend(dc, r->left, r->top, w, h, memory, 0, 0, 1, 1, blend);
        SelectObject(memory, old);
    }
    if (bitmap) DeleteObject(bitmap);
    if (memory) DeleteDC(memory);
}

using LabelsPaintContent_t = void (WINAPI*)(void* self, HDC dc, const RECT* rect);
LabelsPaintContent_t LabelsPaintContent_Original;

// A row was painted: classify its view, and draw the dot now where the view allows it (List). The other views
// draw from the element that paints the name, after it.
void LabelsOnItem(void* item, HDC dc, const RECT* rect) {
    t_labelsRow.item = nullptr;
    std::wstring folder = LabelsThreadFolder();
    if (folder.empty()) {
        return;
    }
    auto data = LabelsFolderFor(folder);
    if (!data->complete || !data->streamsSupported || data->byName.empty()) {
        return;
    }
    std::wstring name = LabelsAccName(item);
    auto found = data->byName.find(name);
    if (found == data->byName.end()) {
        return;
    }
    LabelsRowContext row;
    row.colours = LabelsDotColours(found->second, &row.more);
    if (row.colours.empty()) {
        return;
    }
    row.folder = data->folders.count(name) != 0;
    auto typed = data->typeNames.find(name);
    if (typed != data->typeNames.end()) {
        row.typeName = typed->second;
    }
    UINT dpi = LabelsThreadDpi();
    row.dpi = dpi;
    row.row = *rect;
    row.side = LabelsDotSide(rect->bottom - rect->top, dpi);
    row.lineHeight = MulDiv(16, (int)dpi, 96);
    row.columnPad = MulDiv(18, (int)dpi, 96);
    auto children = LabelsChildrenOf(item);
    int elements = 0;
    bool checkbox = false, image = false, property = false, details = false, smart = false;
    const LabelsChild* icon = nullptr;
    const LabelsChild* label = nullptr;
    for (auto const& child : children) {
        checkbox = checkbox || child.kind == kCheckbox;
        image = image || child.kind == kImage;
        property = property || child.kind == kProperty;
        details = details || child.kind == kDetailsCollection;
        smart = smart || child.kind == kSmartCollection;
        if (child.kind == kImage && !icon) {
            icon = &child;
        }
        if (child.kind == kElement) {
            if (++elements == 1 && !icon) {
                icon = &child;
            } else if (elements == 2) {
                label = &child;
            }
        }
    }
    if (details) {
        row.view = LabelsView::Details;
    } else if (smart) {
        row.view = LabelsView::Content;
    } else if (property && image) {
        row.view = LabelsView::List;
    } else if (elements >= 2 && checkbox) {
        row.view = LabelsView::Tiles;
    } else if (elements >= 2) {
        row.view = LabelsView::Icons;
    } else {
        return;
    }
    if (icon) {
        row.iconLeft = rect->left + icon->at.x;
        row.iconBottom = rect->top + icon->at.y + icon->size.cy;
    }
    if (label) {
        row.block = {rect->left + label->at.x, rect->top + label->at.y, rect->left + label->at.x + label->size.cx,
                     rect->top + label->at.y + label->size.cy};
    }
    if (row.view == LabelsView::List) {
        // The gap in front of the icon (where the check box shows) is the room: fewer dots when it is narrow.
        if (icon) {
            int start = (rect->bottom - rect->top - row.side) / 2;
            LabelsFitDots(&row, std::max<int>(row.side, (int)icon->at.x - start - 2));
        }
        LabelsPlaceInput in;
        in.view = row.view;
        in.row = *rect;
        in.side = row.side;
        in.blockWidth = LabelsBlockWidth((int)row.colours.size(), row.more, row.side);
        LabelsDrawBlock(dc, row, LabelsPlaceFor(in));
        return;
    }
    row.item = item;
    t_labelsRow = row;
}

// A name or detail text of the row was painted: Details, Tiles, Content and Icons draw from here, next to it.
void LabelsOnChild(void* self, LabelsKind kind, HDC dc, const RECT* rect) {
    LabelsRowContext& row = t_labelsRow;
    if (!row.item || row.drawn || (kind != kProperty && kind != kSmartProperty)) {
        return;
    }
    void* up = LabelsGetParent(self);
    for (int level = 0; up && level < 3 && up != row.item; level++) {
        up = LabelsGetParent(up);
    }
    if (up != row.item) {
        return;
    }
    int textLeft = rect->left, textRight = rect->right - 1;
    int scanLeft = 0, scanRight = -1;
    RECT band = *rect;
    if (row.view == LabelsView::Icons) {
        band.top = std::max(rect->top, rect->bottom - row.lineHeight);  // The last line of the name.
    }
    bool scanned = LabelsScanText(dc, band, &scanLeft, &scanRight);
    if (scanned) {
        textLeft = scanLeft;
        textRight = scanRight;
    }
    COLORREF detailsPad = CLR_INVALID;
    LabelsPlaceInput in;
    in.view = row.view;
    in.row = row.row;
    in.lineHeight = row.lineHeight;
    in.iconLeft = row.iconLeft;
    in.iconBottom = row.iconBottom;
    in.side = row.side;
    in.columnPad = row.columnPad;
#ifdef EXTRAS_VERBOSE
    {
        static std::atomic<int> logged{0};
        if (logged++ < 250) {
            Wh_Log(L"LABELS PROP view=%d kind=%d rect=(%ld,%ld,%ld,%ld) text=%d..%d scanned=%d acc='%.30s' row=(%ld,%ld,%ld,%ld) block=(%ld,%ld,%ld,%ld) folder=%d",
                   (int)row.view, (int)kind, rect->left, rect->top, rect->right, rect->bottom, textLeft, textRight,
                   scanned ? 1 : 0, LabelsAccName(self).c_str(), row.row.left, row.row.top, row.row.right,
                   row.row.bottom, row.block.left, row.block.top, row.block.right, row.block.bottom, row.folder ? 1 : 0);
        }
    }
#endif
    if (row.view == LabelsView::Details) {
        in.cell = *rect;
        in.textLeft = textLeft;
        in.textRight = textRight;
        detailsPad = GetPixel(dc, rect->right + 2, rect->top + 2);
    } else if (row.view == LabelsView::Icons) {
        // The name, centred under the icon: the dot goes after its last line.
        in.cell = {row.block.left, band.top, row.block.right, band.bottom};
        in.textLeft = textLeft;
        in.textRight = textRight;
        if (!scanned) {
            return;
        }
    } else if (row.view == LabelsView::Tiles) {
        int lines = LabelsLines(row.block.bottom - row.block.top, row.lineHeight);
        bool nameLine = rect->top < row.block.top + row.lineHeight / 2;
        if (row.folder) {
            // A folder has no detail lines: the dot goes at the start of the empty line below its name.
            if (!nameLine) {
                return;
            }
            int nameLeft = scanned ? textLeft : rect->left + 4;  // The name's text edge, not its element's.
            in.cell = {nameLeft, rect->top + row.lineHeight, row.block.right, rect->top + 2 * row.lineHeight};
            in.textLeft = nameLeft;
            in.textRight = nameLeft - 1;
        } else if (LabelsIsLastLine(rect->bottom, row.block.bottom, row.lineHeight) && (lines <= 1 || !nameLine)) {
            // The last detail line: after its text.
            in.cell = {rect->left, rect->top, row.block.right, rect->bottom};
            in.textLeft = textLeft;
            in.textRight = scanned ? textRight : rect->left - 1;
        } else {
            return;  // The name, or a detail line that is not the last one: the next ones are painted after it.
        }
    } else if (row.view == LabelsView::Content) {
        // The left block (name, then the type when there is one) is painted before the columns on the right.
        bool leftBlock = rect->left < row.row.left + (row.row.right - row.row.left) * 45 / 100;
        bool secondLine = rect->top - row.row.top > (row.row.bottom - row.row.top) * 35 / 100;
        if (leftBlock && secondLine) {
            t_contentTextOffset = rect->left + 4 - row.row.left;
            in.cell = *rect;
            in.textLeft = textLeft;
            in.textRight = scanned ? textRight : rect->left - 1;
            if (!scanned && !row.typeName.empty()) {
                // The type line paints its text in children, after this element: its width is measured instead.
                in.textLeft = rect->left + 4;
                in.textRight = in.textLeft + LabelsTextWidth(dc, LabelsAccName(self) + L" : " + row.typeName, row.dpi, 11);
            }
        } else if (leftBlock) {
            row.name = *rect;
            row.nameTextLeft = scanned ? textLeft : rect->left + 4;
            row.haveName = true;
            return;
        } else {
            // The first column on the right: the row has no type line, the dot goes at the start of the line below the name.
            int left = row.haveName ? row.nameTextLeft : (t_contentTextOffset >= 0 ? row.row.left + t_contentTextOffset : -1);
            if (left < 0) {
                return;
            }
            in.cell = {left, row.row.top, rect->left, row.row.bottom};
            in.cell.top = row.row.top + row.lineHeight * 35 / 16 - row.lineHeight * 3 / 2;
            in.textLeft = left;
            in.textRight = left - 1;
        }
    } else {
        return;
    }
    in.blockWidth = LabelsBlockWidth((int)row.colours.size(), row.more, row.side);
    row.drawn = true;
    LabelsPlace place = LabelsPlaceFor(in);
#ifdef EXTRAS_VERBOSE
    {
        static std::atomic<int> placed{0};
        if (placed++ < 150) {
            Wh_Log(L"LABELS DRAW view=%d x=%d centreY=%d corner=%d dots=%d", (int)row.view, place.x, place.centreY,
                   place.corner ? 1 : 0, (int)row.colours.size());
        }
    }
#endif
    if (row.view == LabelsView::Details && detailsPad != CLR_INVALID && textRight > place.x - 4) {
        // A long name runs under the dots: the end of it is cleared in the colour of the row, so the dots read.
        HBRUSH brush = CreateSolidBrush(detailsPad);
        if (brush) {
            RECT clear = {place.x - 3, rect->top, place.x + in.blockWidth + 3, rect->bottom};
            FillRect(dc, &clear, brush);
            DeleteObject(brush);
        }
    }
    LabelsDrawBlock(dc, row, place);
}

void WINAPI LabelsPaintContent_Hook(void* self, HDC dc, const RECT* rect) {
    bool usable = g_labels && !g_unloading && g_labelsPaintOk && self && rect;  // rect is PaintContent's own argument
    // Before the child paints: is it part of a row that the filter dims, and what colour is behind it?
    bool dim = false;
    COLORREF behind = 0;
    if (usable) {
        try {
            LabelsKind own = LabelsKindOf(self);
            if (own == kImage || own == kProperty || own == kSmartProperty || own == kElement || own == kTilesCollection ||
                own == kSmartCollection || own == kDetailsCollection || own == kBase) {
                LabelsFilter* filter = g_labelsFilterAny ? LabelsFilterOfThread() : nullptr;
                if (filter) {
                    void* item = LabelsGetParent(self);
                    for (int up = 0; item && up < 4 && LabelsKindOf(item) != kItem; up++) {
                        item = LabelsGetParent(item);
                    }
                    if (item && LabelsKindOf(item) == kItem && LabelsRowDims(item, *filter)) {
                        dim = true;
                        behind = GetPixel(dc, std::max<LONG>(rect->left, rect->right - 3), rect->top + 2);
                        if (behind == CLR_INVALID) {
                            behind = LabelsDarkTheme() ? RGB(25, 25, 25) : RGB(255, 255, 255);
                        }
                    }
                }
            }
        } catch (...) {
        }
    }
    LabelsPaintContent_Original(self, dc, rect);
    if (dim) {
        LabelsDimRect(dc, rect, behind);
    }
    if (!usable) {
        return;
    }
    try {
        LabelsKind kind = LabelsKindOf(self);
        if (kind == kItem) {
            LabelsOnItem(self, dc, rect);
        } else if (kind == kProperty || kind == kSmartProperty) {
            LabelsOnChild(self, kind, dc, rect);
        }
    } catch (...) {
    }
}

// A folder was read: the windows are painted again so the dots show.
void LabelsRepaintWindows(std::wstring const& folderPath) {
    if (!folderPath.empty()) {
        LabelsGroupFolderLoaded(folderPath);
    }
    EnumWindows([](HWND window, LPARAM) -> BOOL {
        DWORD pid = 0;
        wchar_t name[40];
        GetWindowThreadProcessId(window, &pid);
        if (pid == GetCurrentProcessId() && GetClassNameW(window, name, 40) && wcscmp(name, L"CabinetWClass") == 0 &&
            IsWindowVisible(window)) {
            RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
        }
        return TRUE;
    }, 0);
    // The Columns view of each window is not painted by the list, its rows are XAML: its thread fills the dots again.
    std::lock_guard<std::mutex> lock(g_statesMutex);
    for (auto& state : g_states) {
        if (state->queue) {
            state->queue.TryEnqueue([]() { LabelsRefreshColumns(); });
        }
    }
}

}  // namespace

// Installs the hook. False, with one log line, when a needed export or slot is missing: labels are then off and
// everything else works.
bool LabelsInstallPaintHooks() {
    HMODULE dui = GetModuleHandleW(L"dui70.dll");
    if (!dui) {
        Wh_Log(L"labels: dui70.dll is not loaded, labels are off");
        return false;
    }
    void* elementVtable = (void*)GetProcAddress(dui, "??_7Element@DirectUI@@6B@");
    void* infoVtable = (void*)GetProcAddress(dui, "??_7ClassInfoBase@DirectUI@@6B@");
    g_labelsClassSlot = LabelsFindSlot(elementVtable, LabelsExport(dui, "?GetClassInfoW@Element@DirectUI@@"));
    g_labelsNameSlot = LabelsFindSlot(infoVtable, LabelsExport(dui, "?GetName@ClassInfoBase@DirectUI@@"));
    LabelsGetAccName = (LabelsGetAccName_t)LabelsExport(dui, "?GetAccName@Element@DirectUI@@");
    LabelsValueRelease = (LabelsValueRelease_t)LabelsExport(dui, "?Release@Value@DirectUI@@");
    LabelsGetChildren = (LabelsGetChildren_t)LabelsExport(dui, "?GetChildren@Element@DirectUI@@");
    LabelsGetLocation = (LabelsGetLocation_t)LabelsExport(dui, "?GetLocation@Element@DirectUI@@");
    LabelsGetExtent = (LabelsGetExtent_t)LabelsExport(dui, "?GetExtent@Element@DirectUI@@");
    LabelsGetParent = (LabelsGetParent_t)LabelsExport(dui, "?GetParent@Element@DirectUI@@");
    void* paintContent = LabelsExport(dui, "?PaintContent@Element@DirectUI@@");
    if (g_labelsClassSlot < 0 || g_labelsNameSlot < 0 || !LabelsGetAccName || !LabelsValueRelease || !LabelsGetChildren ||
        !LabelsGetLocation || !LabelsGetExtent || !LabelsGetParent || !paintContent) {
        Wh_Log(L"labels: a dui70.dll export or vtable slot is missing (slots %d %d), labels are off",
               g_labelsClassSlot, g_labelsNameSlot);
        return false;
    }
    if (!WindhawkUtils::SetFunctionHook((LabelsPaintContent_t)paintContent, LabelsPaintContent_Hook, &LabelsPaintContent_Original)) {
        Wh_Log(L"labels: the PaintContent hook could not be set, labels are off");
        return false;
    }
    LabelsSetLoadedCallback(LabelsRepaintWindows);
    g_labelsPaintOk = true;
    Wh_Log(L"labels: paint hook installed (slots %d %d)", g_labelsClassSlot, g_labelsNameSlot);
    return true;
}

void LabelsRemovePaintHooks() {
    g_labelsPaintOk = false;
    LabelsSetLoadedCallback(nullptr);
}
