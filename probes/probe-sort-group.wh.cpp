// ==WindhawkMod==
// @id              probe-sort-group
// @name            Probe: sort and group by our own tag values
// @description     Diagnostic only. Hooks the file system folder's column functions and the item property store, logs which ones File Explorer calls for Sort by and Group by, and serves test values for the Tags property.
// @version         0.3
// @author          AristideBH
// @github          https://github.com/AristideBH
// @include         explorer.exe
// @compilerOptions -lole32 -loleaut32 -lshlwapi -lshell32 -luuid -lpropsys
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Probe: sort and group by our own tag values

Question: can File Explorer's own Sort by and Group by work on values the mod serves?

It finds the code behind `IShellFolder2` of a file system folder (Documents) and behind `IPropertyStore` of one item,
hooks `GetDetailsEx`, `GetDetailsOf`, `CompareIDs`, `MapColumnToSCID` and `GetValue`, and logs each distinct property
asked for. For `System.Keywords` (Tags) it serves a test value by the item: ProbeA, ProbeB, or both.

Open a folder, then in the Sort menu choose Tags (or More...), then Group by Tags. Read
`%LOCALAPPDATA%\ProbeLabel\sort-group.log`. Set the mod's "serve" to 0 by disabling the mod to compare.
*/
// ==/WindhawkModReadme==

#include <windhawk_utils.h>
#include <windows.h>
#include <oleauto.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <cstdarg>
#include <map>
#include <mutex>
#include <string>

namespace {

wchar_t g_path[MAX_PATH] = L"";
std::mutex g_mutex;
std::map<std::wstring, int> g_seen;  // "which call, which property" to the number of calls.
int g_overrides = 0;

void Out(const wchar_t* format, ...) {
    wchar_t text[700];
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(text, ARRAYSIZE(text), _TRUNCATE, format, args);
    va_end(args);
    Wh_Log(L"%s", text);
    if (!g_path[0]) {
        return;
    }
    wchar_t line[800];
    SYSTEMTIME now;
    GetLocalTime(&now);
    int chars = _snwprintf_s(line, ARRAYSIZE(line), _TRUNCATE, L"%02d:%02d:%02d.%03d [%lu] %s\r\n", now.wHour,
                             now.wMinute, now.wSecond, now.wMilliseconds, GetCurrentThreadId(), text);
    char utf8[1800];
    int bytes = WideCharToMultiByte(CP_UTF8, 0, line, chars > 0 ? chars : (int)wcslen(line), utf8, sizeof(utf8), nullptr, nullptr);
    if (bytes <= 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    HANDLE file = CreateFileW(g_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, utf8, (DWORD)bytes, &written, nullptr);
        CloseHandle(file);
    }
}

std::wstring KeyText(PROPERTYKEY const& key) {
    wchar_t name[MAX_PATH] = L"";
    PWSTR canonical = nullptr;
    if (SUCCEEDED(PSGetNameFromPropertyKey(key, &canonical)) && canonical) {
        wcsncpy_s(name, canonical, _TRUNCATE);
        CoTaskMemFree(canonical);
    } else {
        wchar_t guid[64];
        StringFromGUID2(key.fmtid, guid, 64);
        swprintf_s(name, L"%s,%lu", guid, key.pid);
    }
    return name;
}

// Logs the first time a call and a property meet, then every 500th time.
void Note(const wchar_t* call, PROPERTYKEY const& key) {
    std::wstring label = std::wstring(call) + L" " + KeyText(key);
    int count;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        count = ++g_seen[label];
    }
    if (count == 1 || count % 500 == 0) {
        Out(L"%s (call number %d)", label.c_str(), count);
    }
}

bool IsKeywords(PROPERTYKEY const& key) {
    return IsEqualPropertyKey(key, PKEY_Keywords);
}

// A test value from the item's bytes: 0 ProbeA, 1 ProbeB, 2 both.
int Bucket(PCUITEMID_CHILD pidl) {
    if (!pidl) {
        return 0;
    }
    UINT size = ILGetSize(pidl);
    unsigned hash = 17;
    for (UINT i = 0; i < size; i++) {
        hash = hash * 31 + ((const unsigned char*)pidl)[i];
    }
    return (int)(hash % 3);
}

SAFEARRAY* KeywordArray(int bucket) {
    const wchar_t* values[2] = {bucket == 1 ? L"ProbeB" : L"ProbeA", L"ProbeB"};
    LONG count = bucket == 2 ? 2 : 1;
    SAFEARRAY* array = SafeArrayCreateVector(VT_BSTR, 0, count);
    for (LONG i = 0; i < count; i++) {
        SafeArrayPutElement(array, &i, SysAllocString(values[i]));
    }
    return array;
}

// IShellFolder2 slots: 0-2 IUnknown, 3-12 IShellFolder (CompareIDs is 7), 13 GetDefaultSearchGUID, 14 EnumSearches,
// 15 GetDefaultColumn, 16 GetDefaultColumnState, 17 GetDetailsEx, 18 GetDetailsOf, 19 MapColumnToSCID.
using GetDetailsEx_t = HRESULT(STDMETHODCALLTYPE*)(void*, PCUITEMID_CHILD, const SHCOLUMNID*, VARIANT*);
GetDetailsEx_t GetDetailsEx_Original;
HRESULT STDMETHODCALLTYPE GetDetailsEx_Hook(void* self, PCUITEMID_CHILD pidl, const SHCOLUMNID* key, VARIANT* out) {
    HRESULT hr = GetDetailsEx_Original(self, pidl, key, out);
    if (key) {
        Note(SUCCEEDED(hr) ? L"GetDetailsEx ok" : L"GetDetailsEx failed", *key);
        if (out && IsKeywords(*key)) {
            if (SUCCEEDED(hr)) {
                VariantClear(out);
            }
            VariantInit(out);
            out->vt = VT_ARRAY | VT_BSTR;
            out->parray = KeywordArray(Bucket(pidl));
            g_overrides++;
            hr = S_OK;
        }
    }
    return hr;
}

using GetDetailsOf_t = HRESULT(STDMETHODCALLTYPE*)(void*, PCUITEMID_CHILD, UINT, SHELLDETAILS*);
GetDetailsOf_t GetDetailsOf_Original;
HRESULT STDMETHODCALLTYPE GetDetailsOf_Hook(void* self, PCUITEMID_CHILD pidl, UINT column, SHELLDETAILS* details) {
    HRESULT hr = GetDetailsOf_Original(self, pidl, column, details);
    PROPERTYKEY fake = {{0, 0, 0, {0}}, column};
    Note(pidl ? L"GetDetailsOf item, column pid" : L"GetDetailsOf header, column pid", fake);
    return hr;
}

using CompareIDs_t = HRESULT(STDMETHODCALLTYPE*)(void*, LPARAM, PCIDLIST_RELATIVE, PCIDLIST_RELATIVE);
CompareIDs_t CompareIDs_Original;
HRESULT STDMETHODCALLTYPE CompareIDs_Hook(void* self, LPARAM lParam, PCIDLIST_RELATIVE a, PCIDLIST_RELATIVE b) {
    HRESULT hr = CompareIDs_Original(self, lParam, a, b);
    PROPERTYKEY fake = {{0, 0, 0, {0}}, (DWORD)(lParam & 0xFFFF)};
    Note(L"CompareIDs column", fake);
    return hr;
}

using MapColumnToSCID_t = HRESULT(STDMETHODCALLTYPE*)(void*, UINT, SHCOLUMNID*);
MapColumnToSCID_t MapColumnToSCID_Original;
HRESULT STDMETHODCALLTYPE MapColumnToSCID_Hook(void* self, UINT column, SHCOLUMNID* key) {
    HRESULT hr = MapColumnToSCID_Original(self, column, key);
    if (SUCCEEDED(hr) && key) {
        std::wstring label = L"MapColumnToSCID " + std::to_wstring(column) + L" = " + KeyText(*key);
        int count;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            count = ++g_seen[label];
        }
        if (count == 1) {
            Out(L"%s", label.c_str());
        }
    }
    return hr;
}

// IPropertyStore slots: 3 GetCount, 4 GetAt, 5 GetValue.
using GetValue_t = HRESULT(STDMETHODCALLTYPE*)(void*, REFPROPERTYKEY, PROPVARIANT*);
GetValue_t GetValue_Original;
HRESULT STDMETHODCALLTYPE GetValue_Hook(void* self, REFPROPERTYKEY key, PROPVARIANT* out) {
    HRESULT hr = GetValue_Original(self, key, out);
    Note(SUCCEEDED(hr) ? L"GetValue ok" : L"GetValue failed", key);
    if (out && IsKeywords(key)) {
        // Which item is this? Ask the same store for its path, and serve a value by the name.
        int bucket = (int)(((UINT_PTR)self >> 4) % 3);
        PROPVARIANT path;
        PropVariantInit(&path);
        if (SUCCEEDED(GetValue_Original(self, PKEY_ParsingPath, &path)) && path.vt == VT_LPWSTR && path.pwszVal) {
            const wchar_t* name = PathFindFileNameW(path.pwszVal);
            unsigned hash = 5;
            for (const wchar_t* c = name; *c; c++) {
                hash = hash * 33 + *c;
            }
            bucket = (int)(hash % 3);
            Out(L"item %s", path.pwszVal);
        } else {
            Out(L"item path unknown, vt=%d", (int)path.vt);
        }
        PropVariantClear(&path);
        PropVariantClear(out);
        PCWSTR values[2] = {bucket == 1 ? L"ProbeB" : L"ProbeA", L"ProbeB"};
        InitPropVariantFromStringVector(values, bucket == 2 ? 2 : 1, out);
        g_overrides++;
        Out(L"served Keywords bucket %d for store %p", bucket, self);
        hr = S_OK;
    }
    return hr;
}

void* SlotOf(void* object, int slot) {
    return (*(void***)object)[slot];
}

template <typename T>
bool HookSlot(void* object, int slot, T hook, T* original, const wchar_t* name) {
    void* target = SlotOf(object, slot);
    bool ok = Wh_SetFunctionHook(target, (void*)hook, (void**)original);
    Out(L"hook %s at %p: %d", name, target, ok ? 1 : 0);
    return ok;
}

}  // namespace

BOOL Wh_ModInit() {
    wchar_t local[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (len && len < MAX_PATH - 40) {
        wchar_t dir[MAX_PATH];
        swprintf_s(dir, L"%s\\ProbeLabel", local);
        CreateDirectoryW(dir, nullptr);
        swprintf_s(g_path, L"%s\\sort-group.log", dir);
        DeleteFileW(g_path);
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    PIDLIST_ABSOLUTE pidl = nullptr;
    IShellFolder* folder = nullptr;
    if (FAILED(SHGetKnownFolderIDList(FOLDERID_Documents, 0, nullptr, &pidl)) || !pidl ||
        FAILED(SHBindToObject(nullptr, pidl, nullptr, IID_PPV_ARGS(&folder))) || !folder) {
        Out(L"could not bind the Documents folder");
        return FALSE;
    }
    IShellFolder2* folder2 = nullptr;
    if (SUCCEEDED(folder->QueryInterface(IID_PPV_ARGS(&folder2))) && folder2) {
        Out(L"IShellFolder2 of Documents is %p, vtable %p", folder2, *(void**)folder2);
        HookSlot(folder2, 17, GetDetailsEx_Hook, &GetDetailsEx_Original, L"GetDetailsEx");
        HookSlot(folder2, 18, GetDetailsOf_Hook, &GetDetailsOf_Original, L"GetDetailsOf");
        HookSlot(folder2, 7, CompareIDs_Hook, &CompareIDs_Original, L"CompareIDs");
        HookSlot(folder2, 19, MapColumnToSCID_Hook, &MapColumnToSCID_Original, L"MapColumnToSCID");
        folder2->Release();
    }
    // The property store of the first file in the folder.
    IEnumIDList* list = nullptr;
    if (SUCCEEDED(folder->EnumObjects(nullptr, SHCONTF_NONFOLDERS, &list)) && list) {
        PITEMID_CHILD child = nullptr;
        if (list->Next(1, &child, nullptr) == S_OK && child) {
            PIDLIST_ABSOLUTE full = ILCombine(pidl, child);
            IShellItem2* item = nullptr;
            if (full && SUCCEEDED(SHCreateItemFromIDList(full, IID_PPV_ARGS(&item))) && item) {
                IPropertyStore* store = nullptr;
                if (SUCCEEDED(item->GetPropertyStore(GPS_DEFAULT, IID_PPV_ARGS(&store))) && store) {
                    Out(L"IPropertyStore of the first file is %p, vtable %p", store, *(void**)store);
                    HookSlot(store, 5, GetValue_Hook, &GetValue_Original, L"IPropertyStore::GetValue");
                    store->Release();
                }
                item->Release();
            }
            if (full) {
                ILFree(full);
            }
            ILFree(child);
        }
        list->Release();
    }
    folder->Release();
    ILFree(pidl);
    Out(L"ready: open a folder, choose Sort by Tags, then Group by Tags");
    return TRUE;
}

void Wh_ModUninit() {
    Out(L"unloaded, %d Tags values were served", g_overrides);
}
