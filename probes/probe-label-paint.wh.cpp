// ==WindhawkMod==
// @id              probe-label-paint
// @name            Probe: draw a dot on File Explorer rows
// @description     Diagnostic only. Draws a coloured dot on file list rows whose accessible name contains a text, to test per-item drawing for labels.
// @version         0.11
// @author          AristideBH
// @github          https://github.com/AristideBH
// @include         explorer.exe
// @compilerOptions -lshlwapi -lgdi32 -lmsimg32 -ladvapi32
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Probe: label dot

Hooks `Element::PaintContent` (exported by `dui70.dll`). After the original has
painted a `UIItem` (a row of the file list), the probe asks the row for its
accessible name with `Element::GetAccName`, and when the name contains the text
in the control file (`mark=text`, default `a`) it draws a dot at the right end
of the row.

Questions it answers:

1. Does a dot drawn from the hook show, and does it survive hover, selection
   and scrolling?
2. Does `GetAccName` give the file's name, so a row can be mapped to a file?
3. What does it cost (the summary shows the time per row)?

Files are in `%LOCALAPPDATA%\ProbeLabel`: `probe-label-paint.log` and
`control.txt` (lines `mark=text`, `draw=0`, `draw=1`, `dump=1`).
*/
// ==/WindhawkModReadme==

#include <windows.h>
#include <shlwapi.h>

#include <atomic>
#include <cstdarg>
#include <cwchar>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace {

std::mutex g_outMutex;
wchar_t g_dir[MAX_PATH] = L"";
wchar_t g_outPath[MAX_PATH] = L"";
std::atomic<int> g_logged{0};
std::atomic<long> g_rows{0};
std::atomic<int> g_propLogged{0};
std::atomic<DWORD> g_dimColour{0x00191919};  // The page colour of the current theme, as 0x00RRGGBB.
std::atomic<long> g_dimmed{0};
std::atomic<long> g_dots{0};
std::atomic<long long> g_ticks{0};
wchar_t g_mark[64] = L"a";
std::atomic<bool> g_unloading{false};
std::atomic<bool> g_draw{true};
std::mutex g_seenMutex;
std::set<std::wstring> g_seen;
std::set<std::wstring> g_treeDone;

void Out(const wchar_t* format, ...) {
    wchar_t text[800];
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(text, ARRAYSIZE(text), _TRUNCATE, format, args);
    va_end(args);
    Wh_Log(L"%s", text);
    if (!g_outPath[0]) {
        return;
    }
    wchar_t line[900];
    SYSTEMTIME now;
    GetLocalTime(&now);
    int chars = _snwprintf_s(line, ARRAYSIZE(line), _TRUNCATE, L"%02d:%02d:%02d.%03d %s\r\n",
                             now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, text);
    char utf8[2000];
    int bytes = WideCharToMultiByte(CP_UTF8, 0, line, chars > 0 ? chars : (int)wcslen(line), utf8,
                                    sizeof(utf8), nullptr, nullptr);
    if (bytes <= 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_outMutex);
    HANDLE file = CreateFileW(g_outPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, utf8, (DWORD)bytes, &written, nullptr);
        CloseHandle(file);
    }
}

void* FindExport(HMODULE module, const char* prefix) {
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
            Out(L"export %S", name);
            return (void*)GetProcAddress(module, name);
        }
    }
    return nullptr;
}

int FindSlot(void* vtable, void* function) {
    if (!vtable || !function) {
        return -1;
    }
    void** slots = (void**)vtable;
    for (int i = 0; i < 120; i++) {
        if (IsBadReadPtr(&slots[i], sizeof(void*))) {
            break;
        }
        if (slots[i] == function) {
            return i;
        }
    }
    return -1;
}

int g_classInfoSlot = -1;
int g_nameSlot = -1;

using GetAccName_t = const wchar_t* (*)(void* self, void** out);
using ValueGetString_t = const wchar_t* (*)(void* value);
using ValueRelease_t = void (*)(void* value);
GetAccName_t GetAccName_fn = nullptr;
ValueGetString_t ValueGetString_fn = nullptr;
ValueRelease_t ValueRelease_fn = nullptr;

std::wstring ElementClassName(void* element) {
    if (g_classInfoSlot < 0 || g_nameSlot < 0) {
        return L"";
    }
    void** vtable = *(void***)element;
    using GetClassInfo_t = void* (*)(void*);
    void* classInfo = ((GetClassInfo_t)vtable[g_classInfoSlot])(element);
    if (!classInfo || IsBadReadPtr(classInfo, sizeof(void*))) {
        return L"";
    }
    void** infoVtable = *(void***)classInfo;
    using GetName_t = const wchar_t* (*)(void*);
    const wchar_t* name = ((GetName_t)infoVtable[g_nameSlot])(classInfo);
    if (!name || IsBadStringPtrW(name, 200)) {
        return L"";
    }
    return name;
}

// The accessible name of the element, or empty.
std::wstring AccName(void* element) {
    if (!GetAccName_fn || !ValueGetString_fn) {
        return L"";
    }
    // Returns the string itself; the Value that owns it comes back in `value`.
    void* value = nullptr;
    const wchar_t* text = GetAccName_fn(element, &value);
    std::wstring result;
    if (text && !IsBadStringPtrW(text, 600)) {
        result = text;
    }
    if (value && ValueRelease_fn) {
        ValueRelease_fn(value);
    }
    return result;
}


using GetChildren_t = void* (*)(void* self, void** out);
using GetLocation_t = const POINT* (*)(void* self, void** out);
using GetExtent_t = const SIZE* (*)(void* self, void** out);
using GetParent_t = void* (*)(void* self);
GetParent_t GetParent_fn = nullptr;
GetChildren_t GetChildren_fn = nullptr;
GetLocation_t GetLocation_fn = nullptr;
GetExtent_t GetExtent_fn = nullptr;

bool Readable(const void* p, size_t n) { return p && !IsBadReadPtr(p, n); }

// The elements of a DynamicArray<Element*>, whatever its layout: look for a data pointer
// and a count among the first three words.
bool ReadArray(void* array, std::vector<void*>* out) {
    if (!Readable(array, 24)) {
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
                if (count == 0 || count > 60 || !Readable(data, count * sizeof(void*))) {
                    continue;
                }
                bool ok = true;
                for (unsigned long long i = 0; i < count && ok; i++) {
                    ok = Readable(data[i], 8) && Readable(*(void**)data[i], 8);
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

void DumpTree(void* element, int depth) {
    if (depth > 3 || !GetChildren_fn) {
        return;
    }
    void* value = nullptr;
    void* array = GetChildren_fn(element, &value);
    std::vector<void*> children;
    bool ok = ReadArray(array, &children);
    if (value && ValueRelease_fn) {
        ValueRelease_fn(value);
    }
    if (!ok) {
        return;
    }
    for (void* child : children) {
        void* v1 = nullptr;
        void* v2 = nullptr;
        const POINT* location = GetLocation_fn ? GetLocation_fn(child, &v1) : nullptr;
        const SIZE* extent = GetExtent_fn ? GetExtent_fn(child, &v2) : nullptr;
        POINT at = location && Readable(location, sizeof(POINT)) ? *location : POINT{-1, -1};
        SIZE size = extent && Readable(extent, sizeof(SIZE)) ? *extent : SIZE{-1, -1};
        if (v1 && ValueRelease_fn) ValueRelease_fn(v1);
        if (v2 && ValueRelease_fn) ValueRelease_fn(v2);
        std::wstring text = AccName(child);
        Out(L"TREE %*s%s at (%ld,%ld) size %ldx%ld acc='%.40s'", depth * 2, L"",
            ElementClassName(child).c_str(), at.x, at.y, size.cx, size.cy, text.c_str());
        DumpTree(child, depth + 1);
    }
}

bool Contains(std::wstring const& name, const wchar_t* mark) {
    if (!mark[0]) {
        return false;
    }
    return StrStrIW(name.c_str(), mark) != nullptr;
}


void DimRect(HDC dc, const RECT* r, COLORREF colour) {
    int w = r->right - r->left, h = r->bottom - r->top;
    if (w <= 0 || h <= 0 || w > 2000 || h > 2000) {
        return;
    }
    HDC mem = CreateCompatibleDC(dc);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 1;
    bi.bmiHeader.biHeight = -1;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp && bits) {
        // COLORREF is 0x00BBGGRR, a DIB pixel is 0x00RRGGBB.
        *(DWORD*)bits = ((colour & 0xFF) << 16) | (colour & 0xFF00) | ((colour >> 16) & 0xFF);
        HGDIOBJ old = SelectObject(mem, bmp);
        BLENDFUNCTION blend = {AC_SRC_OVER, 0, 150, 0};
        AlphaBlend(dc, r->left, r->top, w, h, mem, 0, 0, 1, 1, blend);
        SelectObject(mem, old);
        g_dimmed++;
    }
    if (bmp) DeleteObject(bmp);
    DeleteDC(mem);
}

void ReadTheme() {
    DWORD light = 1, size = sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
    g_dimColour = light ? 0x00FFFFFF : 0x00191919;
}

void DotAt(HDC dc, int x, int centreY, int side, COLORREF colour) {
    int saved = SaveDC(dc);
    HBRUSH brush = CreateSolidBrush(colour);
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
    SelectObject(dc, brush);
    SelectObject(dc, pen);
    Ellipse(dc, x, centreY - side / 2, x + side, centreY - side / 2 + side);
    RestoreDC(dc, saved);
    DeleteObject(brush);
    DeleteObject(pen);
}

void Dot(HDC dc, const RECT* rect) {
    int side = (rect->bottom - rect->top) / 2;
    if (side < 8) {
        side = 8;
    }
    int right = rect->right - 12;
    int top = rect->top + ((rect->bottom - rect->top) - side) / 2;
    int saved = SaveDC(dc);
    HBRUSH brush = CreateSolidBrush(RGB(230, 40, 40));
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
    SelectObject(dc, brush);
    SelectObject(dc, pen);
    Ellipse(dc, right - side, top, right, top + side);
    RestoreDC(dc, saved);
    DeleteObject(brush);
    DeleteObject(pen);
}

using PaintContent_t = void (WINAPI*)(void* self, HDC dc, const RECT* rect);
PaintContent_t PaintContent_Original;
void WINAPI PaintContent_Hook(void* self, HDC dc, const RECT* rect) {
    // Before the original paints: decide whether this child belongs to a row to dim, and read
    // the colour behind it (the page, a hover or a selection colour), so the fade goes toward
    // the real background and leaves no blocks.
    bool dim = false;
    COLORREF background = g_dimColour;
    if (!g_unloading && g_draw && self && rect && !IsBadReadPtr(rect, sizeof(RECT)) && GetParent_fn) {
        std::wstring own = ElementClassName(self);
        if (own == L"UIImage" || own == L"UIProperty" || own == L"Element" ||
            own == L"UIDetailsPropertyCollection" || own == L"UITilesPropertyCollection" ||
            own == L"UISmartPropertyCollection" || own == L"UIBase") {
            void* item = GetParent_fn(self);
            for (int up = 0; item && up < 4 && ElementClassName(item) != L"UIItem"; up++) {
                item = GetParent_fn(item);
            }
            if (item && ElementClassName(item) == L"UIItem" && !Contains(AccName(item), g_mark)) {
                dim = true;
                int x = rect->right - 3 > rect->left ? rect->right - 3 : rect->left;
                COLORREF sample = GetPixel(dc, x, rect->top + 2);
                if (sample != CLR_INVALID) {
                    background = sample;
                }
            }
        }
    }
    PaintContent_Original(self, dc, rect);
    if (g_unloading || !g_draw || !self || !rect || IsBadReadPtr(rect, sizeof(RECT))) {
        return;
    }
    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);
    std::wstring className = ElementClassName(self);
    {
        // Each distinct class and size is logged once, to see what a view mode paints.
        wchar_t key[160];
        swprintf_s(key, L"%s %ldx%ld", className.c_str(), rect->right - rect->left,
                   rect->bottom - rect->top);
        std::lock_guard<std::mutex> lock(g_seenMutex);
        static const wchar_t* noise[] = {L"Element", L"HWNDView", L"Sizer", L"Button", L"LineScroller",
                                         L"LineViewer", L"Resizer", L"LinkedViewer",
                                         L"DetailsContainer", L"ProperTreeModule",
                                         L"ProperTreeModuleInner", L"UICollection", L"UIItemsView",
                                         L"UIRootItem", L"UIViewHeader", L"UIColumnHeader"};
        bool skip = rect->right - rect->left > 600 || rect->bottom - rect->top > 400;
        for (auto word : noise) {
            skip = skip || className == word;
        }
        if (!skip && g_seen.size() < 300 && g_seen.insert(key).second) {
            Out(L"CLASS %s", key);
        }
    }
    if (dim) {
        DimRect(dc, rect, background);
    }
    if (className == L"UIProperty" && GetParent_fn) {
        // The name cell: find the item it belongs to, measure the name's text, draw a blue dot after it.
        void* item = GetParent_fn(self);
        for (int up = 0; item && up < 4 && ElementClassName(item) != L"UIItem"; up++) {
            item = GetParent_fn(item);
        }
        std::wstring own = AccName(self);
        if (item && (own == L"Nom" || own == L"Name")) {
            std::wstring itemName = AccName(item);
            HFONT font = (HFONT)GetCurrentObject(dc, OBJ_FONT);
            LOGFONTW lf = {};
            GetObjectW(font, sizeof(lf), &lf);
            int saved = SaveDC(dc);
            SIZE text = {};
            GetTextExtentPoint32W(dc, itemName.c_str(), (int)itemName.size(), &text);
            RestoreDC(dc, saved);
            int width = rect->right - rect->left;
            int height = rect->bottom - rect->top;
            bool centred = height > 24 && width < 140;
            int x = centred ? rect->left + (width - text.cx) / 2 - 12 : rect->left + text.cx + 4;
            if (false && x >= rect->left - 20 && x + 8 <= rect->right + 20) {
                DotAt(dc, x, rect->top + (centred ? 8 : height / 2), 8, RGB(40, 90, 230));
            }
            if (g_propLogged < 60) {
                g_propLogged++;
                Out(L"NAMECELL rect (%ld,%ld) %ldx%ld text=%ldx%ld fontH=%ld centred=%d name='%s'",
                    rect->left, rect->top, width, height, text.cx, text.cy, (long)lf.lfHeight,
                    centred ? 1 : 0, itemName.c_str());
            }
        }
        return;
    }
    if (className != L"UIItem") {
        return;
    }
    g_rows++;
    if (g_draw && rect->bottom - rect->top == 22 && rect->right - rect->left < 400) {
        // List view: the gap in front of the icon (28 px wide, x 4 to 32).
        // Centred on the row, with the same margin to the row's left edge as above and below.
        int rowHeight = rect->bottom - rect->top;
        int side = 8;
        DotAt(dc, rect->left + (rowHeight - side) / 2, rect->top + rowHeight / 2, side, RGB(30, 160, 70));
    }
    std::wstring name = AccName(self);
    {
        wchar_t key[60];
        swprintf_s(key, L"%ldx%ld", rect->right - rect->left, rect->bottom - rect->top);
        bool first;
        {
            std::lock_guard<std::mutex> lock(g_seenMutex);
            first = g_treeDone.size() < 14 && g_treeDone.insert(key).second;
        }
        if (first) {
            Out(L"TREE item %s name='%s'", key, name.c_str());
            DumpTree(self, 1);
        }
    }
    bool hit = Contains(name, g_mark);
    if (hit) {
        g_dots++;
    }
    QueryPerformanceCounter(&end);
    g_ticks += end.QuadPart - start.QuadPart;
    if (g_logged < 40) {
        g_logged++;
        Out(L"row %ldx%ld at (%ld,%ld) name='%s' hit=%d", rect->right - rect->left,
            rect->bottom - rect->top, rect->left, rect->top, name.c_str(), hit ? 1 : 0);
    }
}

HANDLE g_worker = nullptr;
HANDLE g_stop = nullptr;

DWORD WINAPI Worker(LPVOID) {
    std::string last;
    long lastRows = 0;
    Out(L"worker started, control file in %s", g_dir);
    while (WaitForSingleObject(g_stop, 1000) == WAIT_TIMEOUT) {
        ReadTheme();
        // A summary every few seconds while rows are being painted.
        static int tick = 0;
        if (++tick % 4 == 0 && g_rows != lastRows) {
            lastRows = g_rows;
            LARGE_INTEGER frequency;
            QueryPerformanceFrequency(&frequency);
            long rows = g_rows;
            Out(L"SUMMARY rows=%ld hits=%ld dimmed=%ld avg=%.1f us per row", rows, g_dots.load(), g_dimmed.load(),
                (double)g_ticks * 1e6 / (double)frequency.QuadPart / rows);
        }
        std::wstring path = std::wstring(g_dir) + L"\\control.txt";
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            static bool told = false;
            if (!told) {
                told = true;
                Out(L"control file not readable, error %lu", GetLastError());
            }
            continue;
        }
        char buffer[300] = "";
        DWORD read = 0;
        ReadFile(file, buffer, sizeof(buffer) - 1, &read, nullptr);
        CloseHandle(file);
        buffer[read] = 0;
        if (last == buffer) {
            continue;
        }
        last = buffer;
        if (const char* at = strstr(buffer, "mark=")) {
            char text[64] = "";
            sscanf_s(at + 5, "%63[^\r\n]", text, (unsigned)sizeof(text));
            MultiByteToWideChar(CP_UTF8, 0, text, -1, g_mark, ARRAYSIZE(g_mark));
            Out(L"CONTROL: mark='%s'", g_mark);
            g_logged = 0;
        }
        if (strstr(buffer, "draw=0")) {
            g_draw = false;
        }
        if (strstr(buffer, "draw=1")) {
            g_draw = true;
        }
        if (strstr(buffer, "dump=1")) {
            LARGE_INTEGER frequency;
            QueryPerformanceFrequency(&frequency);
            long rows = g_rows;
            double microseconds =
                rows ? (double)g_ticks * 1e6 / (double)frequency.QuadPart / rows : 0.0;
            Out(L"SUMMARY rows=%ld dots=%ld avg=%.1f us per row", rows, g_dots.load(),
                microseconds);
        }
    }
    return 0;
}

}  // namespace

BOOL Wh_ModInit() {
    wchar_t local[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (len && len < MAX_PATH - 40) {
        swprintf_s(g_dir, L"%s\\ProbeLabel", local);
        CreateDirectoryW(g_dir, nullptr);
        swprintf_s(g_outPath, L"%s\\probe-label-paint.log", g_dir);
        DeleteFileW(g_outPath);
    }
    ReadTheme();
    HMODULE dui = GetModuleHandleW(L"dui70.dll");
    if (!dui) {
        Out(L"dui70.dll isn't loaded in this process");
        return FALSE;
    }
    void* elementVtable = (void*)GetProcAddress(dui, "??_7Element@DirectUI@@6B@");
    void* getClassInfo = FindExport(dui, "?GetClassInfoW@Element@DirectUI@@");
    g_classInfoSlot = FindSlot(elementVtable, getClassInfo);
    void* infoVtable = (void*)GetProcAddress(dui, "??_7ClassInfoBase@DirectUI@@6B@");
    void* getName = FindExport(dui, "?GetName@ClassInfoBase@DirectUI@@");
    g_nameSlot = FindSlot(infoVtable, getName);
    Out(L"slots: classInfo=%d name=%d", g_classInfoSlot, g_nameSlot);

    GetAccName_fn = (GetAccName_t)FindExport(dui, "?GetAccName@Element@DirectUI@@");
    ValueGetString_fn = (ValueGetString_t)FindExport(dui, "?GetString@Value@DirectUI@@");
    ValueRelease_fn = (ValueRelease_t)FindExport(dui, "?Release@Value@DirectUI@@");
    GetChildren_fn = (GetChildren_t)FindExport(dui, "?GetChildren@Element@DirectUI@@");
    GetParent_fn = (GetParent_t)FindExport(dui, "?GetParent@Element@DirectUI@@");
    GetLocation_fn = (GetLocation_t)FindExport(dui, "?GetLocation@Element@DirectUI@@");
    GetExtent_fn = (GetExtent_t)FindExport(dui, "?GetExtent@Element@DirectUI@@");
    Out(L"GetAccName=%p GetString=%p Release=%p", GetAccName_fn, ValueGetString_fn,
        ValueRelease_fn);

    void* paintContent = FindExport(dui, "?PaintContent@Element@DirectUI@@");
    if (!paintContent || !Wh_SetFunctionHook(paintContent, (void*)PaintContent_Hook,
                                             (void**)&PaintContent_Original)) {
        Out(L"PaintContent hook failed");
        return FALSE;
    }
    g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_worker = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    Out(L"ready, mark='%s'", g_mark);
    return TRUE;
}

void Wh_ModBeforeUninit() {
    g_unloading = true;
    if (g_stop) {
        SetEvent(g_stop);
    }
    if (g_worker) {
        WaitForSingleObject(g_worker, 3000);
        CloseHandle(g_worker);
        g_worker = nullptr;
    }
    if (g_stop) {
        CloseHandle(g_stop);
        g_stop = nullptr;
    }
}

void Wh_ModUninit() {}
