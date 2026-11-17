// ==WindhawkMod==
// @id              probe-ctx-hook
// @name            Probe: add an entry to Explorer's right-click menu
// @description     Diagnostic only. Hooks the two functions that build File Explorer's right-click menu and appends one test entry.
// @version         0.1
// @author          AristideBH
// @github          https://github.com/AristideBH
// @include         explorer.exe
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -lshlwapi
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Probe: right-click menu entry

Hooks `MenuFlyoutControl::CreateMenuHelper` (a `MenuFlyout`) and
`MenuFlyoutControl::AppendMenuItem` (a `CommandBarFlyout`) in
`FileExplorerExtensions.dll`. After the original ran, it appends a test entry
labelled "Tags (probe)" and logs what the flyout holds. Right-click a file, a
folder and the empty background of a folder, and read
`%LOCALAPPDATA%\ProbeLabel\ctx-hook.log`.
*/
// ==/WindhawkModReadme==

#include <windhawk_utils.h>
#include <windows.h>

#undef GetCurrentTime

#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>

#include <cstdarg>
#include <map>
#include <mutex>
#include <string>

namespace muxc = winrt::Microsoft::UI::Xaml::Controls;

namespace {

wchar_t g_path[MAX_PATH] = L"";
std::mutex g_mutex;
std::map<void*, int> g_appends;  // Flyout object (the ABI pointer) to the number of appends seen.

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
    int chars = _snwprintf_s(line, ARRAYSIZE(line), _TRUNCATE, L"%02d:%02d:%02d.%03d %s\r\n",
                             now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, text);
    char utf8[1800];
    int bytes = WideCharToMultiByte(CP_UTF8, 0, line, chars > 0 ? chars : (int)wcslen(line), utf8,
                                    sizeof(utf8), nullptr, nullptr);
    if (bytes <= 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    HANDLE file = CreateFileW(g_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, utf8, (DWORD)bytes, &written, nullptr);
        CloseHandle(file);
    }
}

using CreateMenuHelper_t = void(WINAPI*)(void* self, void* flyout, void* items);
CreateMenuHelper_t CreateMenuHelper_Original;
void WINAPI CreateMenuHelper_Hook(void* self, void* flyout, void* items) {
    CreateMenuHelper_Original(self, flyout, items);
    try {
        auto& menu = *(muxc::MenuFlyout*)flyout;
        uint32_t before = menu.Items().Size();
        muxc::MenuFlyoutItem item;
        item.Text(L"Tags (probe, MenuFlyout)");
        menu.Items().Append(item);
        Out(L"CreateMenuHelper thread=%lu flyout=%p items before=%u after=%u", GetCurrentThreadId(),
            flyout, before, menu.Items().Size());
    } catch (winrt::hresult_error const& e) {
        Out(L"CreateMenuHelper failed: %08X %s", (unsigned)e.code().value, e.message().c_str());
    } catch (...) {
        Out(L"CreateMenuHelper failed");
    }
}

using AppendMenuItem_t = void(WINAPI*)(void* self, void* flyout, void* menuItem);
AppendMenuItem_t AppendMenuItem_Original;
void WINAPI AppendMenuItem_Hook(void* self, void* flyout, void* menuItem) {
    AppendMenuItem_Original(self, flyout, menuItem);
    try {
        auto& bar = *(muxc::CommandBarFlyout*)flyout;
        void* key = *(void**)flyout;
        int count;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            count = ++g_appends[key];
            if (g_appends.size() > 200) {
                g_appends.clear();
            }
        }
        Out(L"AppendMenuItem #%d thread=%lu flyout=%p primary=%u secondary=%u", count,
            GetCurrentThreadId(), flyout, bar.PrimaryCommands().Size(),
            bar.SecondaryCommands().Size());
        if (count == 3) {
            // Proof that an entry can be added: it lands after the third command.
            muxc::AppBarButton button;
            button.Label(L"Tags (probe, CommandBarFlyout)");
            bar.SecondaryCommands().Append(button);
            Out(L"  appended a test entry, secondary=%u", bar.SecondaryCommands().Size());
        }
    } catch (winrt::hresult_error const& e) {
        Out(L"AppendMenuItem failed: %08X %s", (unsigned)e.code().value, e.message().c_str());
    } catch (...) {
        Out(L"AppendMenuItem failed");
    }
}

}  // namespace

BOOL Wh_ModInit() {
    wchar_t local[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (len && len < MAX_PATH - 40) {
        wchar_t dir[MAX_PATH];
        swprintf_s(dir, L"%s\\ProbeLabel", local);
        CreateDirectoryW(dir, nullptr);
        swprintf_s(g_path, L"%s\\ctx-hook.log", dir);
        DeleteFileW(g_path);
    }
    HMODULE module = GetModuleHandleW(L"FileExplorerExtensions.dll");
    if (!module) {
        Out(L"FileExplorerExtensions.dll is not loaded; open an Explorer window first and reload the mod");
        return FALSE;
    }
    WindhawkUtils::SYMBOL_HOOK hooks[] = {
        {{LR"(private: void __cdecl winrt::FileExplorerExtensions::implementation::MenuFlyoutControl::CreateMenuHelper(struct winrt::Microsoft::UI::Xaml::Controls::MenuFlyout &,struct winrt::Windows::Foundation::Collections::IVectorView<struct winrt::WindowsUdk::UI::MenuItem>))"},
         &CreateMenuHelper_Original,
         CreateMenuHelper_Hook,
         true},
        {{LR"(private: void __cdecl winrt::FileExplorerExtensions::implementation::MenuFlyoutControl::AppendMenuItem(struct winrt::Microsoft::UI::Xaml::Controls::CommandBarFlyout &,struct winrt::WindowsUdk::UI::MenuItem const &))"},
         &AppendMenuItem_Original,
         AppendMenuItem_Hook,
         true},
    };
    bool ok = WindhawkUtils::HookSymbols(module, hooks, ARRAYSIZE(hooks));
    Out(L"HookSymbols=%d CreateMenuHelper=%p AppendMenuItem=%p", ok ? 1 : 0,
        (void*)CreateMenuHelper_Original, (void*)AppendMenuItem_Original);
    return TRUE;
}

void Wh_ModUninit() {}
