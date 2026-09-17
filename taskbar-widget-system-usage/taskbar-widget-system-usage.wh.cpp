// ==WindhawkMod==
// @id              taskbar-widget-system-usage
// @name            Taskbar System Usage
// @description     CPU/RAM/GPU usage bars injected into the Windows 11 taskbar
// @version         0.1.0
// @author          AristideBH
// @github          https://github.com/AristideBH
// @homepage        https://aristide-bh.com/
// @license         MIT
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -luser32 -lcomctl32 -lpdh
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Taskbar System Usage

> **Note:** This mod is vibe-coded - built largely with AI assistance and
> tested manually by the author, without a full independent code audit. Use
> at your own judgment, and please report anything odd via GitHub Issues.

> **Extracted prototype:** this mod started as one widget (`SystemUsageWidget`)
> inside [`taskbar-widget-stack`](../taskbar-widget-stack/README.md)'s own
> in-process widget SDK. It's been pulled out into its own standalone mod so
> it can be developed and installed independently - it is **not yet
> integrated** with `taskbar-widget-stack` (that would need a real cross-mod
> API between two separately-injected DLLs in the same `explorer.exe`
> process, which doesn't exist yet - see `PLAN.md`). Running both mods at
> once will very likely fight over the same taskbar position, since this one
> currently reuses the exact same anchor point (flush left of the taskbar's
> `RootGrid`).

Three small horizontal bars in the taskbar, one each for CPU, RAM, and GPU
usage - label on the left, live percentage on the right.

## Requirements

- Windows 11, 64-bit
- Windhawk v1.4 or later
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- showCpu: true
  $name: Show CPU
  $description: Show the CPU usage bar.
- showRam: true
  $name: Show RAM
  $description: Show the RAM usage bar.
- showGpu: true
  $name: Show GPU
  $description: >-
    Show the GPU usage bar. Requires the "GPU Engine" performance counters -
    shows "N/A" instead of a percentage if your system/driver doesn't expose
    them.
- refreshSeconds: 1
  $name: Refresh interval (seconds)
  $description: How often the bars update, from 1 to 5 seconds.
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>

#include <windows.h>
#include <unknwn.h>
#include <pdh.h>
#include <pdhmsg.h>

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Media.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Xaml::Media;

namespace {

// ---------------------------------------------------------------------
// Taskbar XAML Access
//
// Windows 11's taskbar is a XAML island, not a classic Win32 client area:
// a plain WS_CHILD/GDI overlay window can be created successfully next to
// it and simply never be visible, composited behind or unrelated to the
// actual taskbar surface. Getting real content to render *in* the taskbar
// means reaching into its live XAML visual tree and inserting real XAML
// elements as children of it.
//
// This whole section (RunFromWindowThread excepted) is ported near-
// verbatim from this repo's own `taskbar-widget-stack.wh.cpp`, which itself
// ported it (with attribution) from `taskbar-ai-quota.wh.cpp` (Cleroth,
// MIT-licensed, ramensoftware/windhawk-mods) - they walk internal,
// undocumented Taskbar.View.dll/taskbar.dll structures (symbol-hooked
// vtables and a machine-code pattern match to recover an internal field
// offset) to obtain the taskbar's XamlRoot. Not independently re-derived
// here; ported because getting any detail of this wrong (a wrong symbol
// string, a wrong offset-scan byte pattern) fails safely (returns null /
// doesn't hook) rather than silently, per the guards already present in
// the source it's ported from.
// ---------------------------------------------------------------------

using CTaskBand_GetTaskbarHost_t = void*(WINAPI*)(void*, void*);
using TaskbarHost_FrameHeight_t = int(WINAPI*)(void*);
using Std_Ref_Decref_t = void(WINAPI*)(void*);

CTaskBand_GetTaskbarHost_t CTaskBand_GetTaskbarHost_Original;
TaskbarHost_FrameHeight_t TaskbarHost_FrameHeight_Original;
Std_Ref_Decref_t Std_Ref_Decref_Original;
void* CTaskBand_ITaskListWndSite_vftable;

using TrayUI_StartTaskbar_t = void(WINAPI*)(void*);
TrayUI_StartTaskbar_t TrayUI_StartTaskbar_Original;

HRESULT TryGetTaskbarElementAbi(HWND hTaskbarWnd, void** result) {
    *result = nullptr;
    void* taskbarHostSharedPtr[2]{};

    auto cleanup = [&]() {
        if (taskbarHostSharedPtr[1] && Std_Ref_Decref_Original) {
            Std_Ref_Decref_Original(taskbarHostSharedPtr[1]);
        }
    };

    HWND hTaskSwWnd = (HWND)GetPropW(hTaskbarWnd, L"TaskbandHWND");
    if (!hTaskSwWnd) {
        return E_HANDLE;
    }

    void* taskBand = (void*)GetWindowLongPtrW(hTaskSwWnd, 0);
    if (!taskBand) {
        return E_POINTER;
    }

    if (!CTaskBand_ITaskListWndSite_vftable || !CTaskBand_GetTaskbarHost_Original) {
        return E_NOINTERFACE;
    }

    void* taskBandForTaskListWndSite = taskBand;
    for (int i = 0; *(void**)taskBandForTaskListWndSite !=
                    CTaskBand_ITaskListWndSite_vftable;
         i++) {
        if (i == 20) {
            return E_NOINTERFACE;
        }
        taskBandForTaskListWndSite = (void**)taskBandForTaskListWndSite + 1;
        if (!taskBandForTaskListWndSite) {
            return E_POINTER;
        }
    }

    CTaskBand_GetTaskbarHost_Original(taskBandForTaskListWndSite,
                                       taskbarHostSharedPtr);
    if (!taskbarHostSharedPtr[0]) {
        cleanup();
        return E_POINTER;
    }

    // TaskbarHost::FrameHeight's prologue moves `this + offset` into
    // rcx/x0 to reach the taskbar element pointer; the offset itself
    // isn't a stable, documented constant, so it's recovered by matching
    // the compiled function's own machine code. Checks both the x64 and
    // ARM64 prologue shapes at *runtime*, unconditionally - not gated by
    // `_M_X64`/`_M_ARM64` (this mod's own compile-time target), since
    // Explorer can run as an ARM64EC process where x64-compiled code
    // (this mod) and native ARM64 system DLL code (taskbar.dll) coexist,
    // so what architecture this mod was compiled as says nothing about
    // what architecture the target function's own code is.
    size_t taskbarElementIUnknownOffset;
    {
        // x64: 48:83EC 28 | sub rsp,28 / 48:83C1 48 | add rcx,48
        const BYTE* b = (const BYTE*)TaskbarHost_FrameHeight_Original;
        // ARM64: 7f2303d5 pacibsp / fd7bbfa9 stp fp,lr,[sp,#-0x10]! /
        // fd030091 mov fp,sp / 080c41f8 ldr x8,[x0,#0x10]!
        const DWORD* p = (const DWORD*)TaskbarHost_FrameHeight_Original;

        if (b[0] == 0x48 && b[1] == 0x83 && b[2] == 0xEC && b[4] == 0x48 &&
            b[5] == 0x83 && b[6] == 0xC1 && b[7] <= 0x7F) {
            taskbarElementIUnknownOffset = b[7];
        } else if (p[0] == 0xD503237F && (p[1] & 0xFFC07FFF) == 0xA9807BFD &&
                   p[2] == 0x910003FD &&
                   (p[3] & 0xFFF00FE0) == 0xF8400C00) {
            taskbarElementIUnknownOffset = (p[3] >> 12) & 0xFF;
        } else {
            wchar_t hex[64] = {};
            for (int i = 0; i < 16; i++) {
                wchar_t byteStr[4];
                wsprintfW(byteStr, L"%02X ", b[i]);
                wcscat_s(hex, byteStr);
            }
            Wh_Log(L"Unsupported TaskbarHost::FrameHeight, bytes: %s", hex);
            cleanup();
            return E_NOINTERFACE;
        }
    }

    auto* taskbarElementIUnknown = *(IUnknown**)(
        (BYTE*)taskbarHostSharedPtr[0] + taskbarElementIUnknownOffset);
    if (!taskbarElementIUnknown) {
        cleanup();
        return E_POINTER;
    }

    HRESULT hr = taskbarElementIUnknown->QueryInterface(
        winrt::guid_of<winrt::Windows::Foundation::IInspectable>(), result);
    cleanup();
    return hr;
}

XamlRoot GetTaskbarXamlRoot(HWND hTaskbarWnd) {
    if (!CTaskBand_ITaskListWndSite_vftable || !CTaskBand_GetTaskbarHost_Original ||
        !TaskbarHost_FrameHeight_Original) {
        return nullptr;
    }

    void* taskbarElementAbi = nullptr;
    if (FAILED(TryGetTaskbarElementAbi(hTaskbarWnd, &taskbarElementAbi)) ||
        !taskbarElementAbi) {
        return nullptr;
    }

    FrameworkElement taskbarElement{nullptr};
    winrt::attach_abi(taskbarElement, taskbarElementAbi);
    return taskbarElement ? taskbarElement.XamlRoot() : nullptr;
}

FrameworkElement FindChildByName(FrameworkElement const& root,
                                  std::wstring_view name, int depth = 32) {
    if (!root || depth == 0) {
        return nullptr;
    }
    int n = VisualTreeHelper::GetChildrenCount(root);
    for (int i = 0; i < n; ++i) {
        auto child = VisualTreeHelper::GetChild(root, i).try_as<FrameworkElement>();
        if (!child) {
            continue;
        }
        if (child.Name() == name) {
            return child;
        }
        if (auto found = FindChildByName(child, name, depth - 1)) {
            return found;
        }
    }
    return nullptr;
}

// The taskbar's root Grid (parent of both TaskbarFrameRepeater - pinned/
// running app icons and the Start button - and SystemTrayFrameGrid).
Grid FindTaskbarRootGrid(FrameworkElement const& root) {
    int count = VisualTreeHelper::GetChildrenCount(root);
    for (int i = 0; i < count; i++) {
        auto c = VisualTreeHelper::GetChild(root, i).try_as<FrameworkElement>();
        if (c && winrt::get_class_name(c) == L"Taskbar.TaskbarFrame") {
            auto rootGrid = FindChildByName(c, L"RootGrid");
            return rootGrid ? rootGrid.try_as<Grid>() : nullptr;
        }
    }
    return nullptr;
}

constexpr const wchar_t* kStartButtonNames[] = {
    L"StartButton",
    L"StartMenuButton",
    L"StartMenuLaunchButton",
    L"LaunchListButton",
};

// Used only as a readiness check (confirms the taskbar's own content has
// been realized in the visual tree) before injecting - this mod doesn't
// anchor its own position off the Start button.
FrameworkElement FindStartButton(FrameworkElement const& repeater) {
    if (!repeater) {
        return nullptr;
    }
    int childCount = VisualTreeHelper::GetChildrenCount(repeater);
    for (int i = 0; i < childCount; i++) {
        auto child = VisualTreeHelper::GetChild(repeater, i).try_as<FrameworkElement>();
        if (!child) {
            continue;
        }
        for (auto name : kStartButtonNames) {
            if (child.Name() == name) {
                return child;
            }
        }
    }
    for (int i = 0; i < childCount; i++) {
        auto child = VisualTreeHelper::GetChild(repeater, i).try_as<FrameworkElement>();
        if (!child) {
            continue;
        }
        int subCount = VisualTreeHelper::GetChildrenCount(child);
        for (int k = 0; k < subCount; k++) {
            auto subChild = VisualTreeHelper::GetChild(child, k).try_as<FrameworkElement>();
            if (!subChild) {
                continue;
            }
            for (auto name : kStartButtonNames) {
                if (subChild.Name() == name) {
                    return subChild;
                }
            }
        }
    }
    return nullptr;
}

void WINAPI TrayUI_StartTaskbar_Hook(void* pThis);

bool HookTaskbarDllSymbols() {
    HMODULE h = LoadLibraryExW(L"taskbar.dll", nullptr,
                                LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!h) {
        return false;
    }

    WindhawkUtils::SYMBOL_HOOK taskbarDllHooks[] = {
        {{LR"(const CTaskBand::`vftable'{for `ITaskListWndSite'})"},
         &CTaskBand_ITaskListWndSite_vftable},
        {{LR"(public: virtual class std::shared_ptr<class TaskbarHost> __cdecl CTaskBand::GetTaskbarHost(void)const )"},
         &CTaskBand_GetTaskbarHost_Original},
        {{LR"(public: int __cdecl TaskbarHost::FrameHeight(void)const )"},
         &TaskbarHost_FrameHeight_Original},
        {{LR"(public: void __cdecl std::_Ref_count_base::_Decref(void))"},
         &Std_Ref_Decref_Original},
        {{LR"(public: virtual void __cdecl TrayUI::StartTaskbar(void))"},
         &TrayUI_StartTaskbar_Original, TrayUI_StartTaskbar_Hook},
    };
    return WindhawkUtils::HookSymbols(h, taskbarDllHooks,
                                       ARRAYSIZE(taskbarDllHooks));
}

// ---------------------------------------------------------------------
// Marshaling (this mod's own code, ported from taskbar-widget-stack.wh.cpp)
// ---------------------------------------------------------------------

// Runs `task` synchronously on the thread that owns `hWnd` -
// Wh_ModInit/hook callbacks aren't guaranteed to run on Explorer's own UI
// thread, and touching a XAML tree or creating a window from the wrong
// thread can freeze/crash Explorer. Payloads are claimed by ID from a
// shared, mutex-guarded table (not read off the message directly) so two
// concurrent calls targeting the same thread can't double-claim each
// other's payload.
bool RunFromWindowThread(HWND hWnd, const std::function<void()>& task,
                          DWORD timeoutMs = 3000) {
    DWORD tid = GetWindowThreadProcessId(hWnd, nullptr);
    if (!tid) {
        return false;
    }
    if (tid == GetCurrentThreadId()) {
        task();
        return true;
    }

    struct Payload {
        const std::function<void()>* task;
        std::atomic<bool> ran{false};
    };
    static std::mutex pendingMutex;
    static std::vector<std::pair<UINT_PTR, Payload*>> pending;
    static std::atomic<UINT_PTR> nextId{1};

    Payload payload{&task};
    UINT_PTR id = nextId.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(pendingMutex);
        pending.push_back({id, &payload});
    }

    static const UINT kMsg = RegisterWindowMessageW(
        L"Windhawk_taskbar-widget-system-usage_RunFromWindowThread");
    HHOOK hook = SetWindowsHookExW(
        WH_CALLWNDPROC,
        [](int code, WPARAM w, LPARAM l) -> LRESULT {
            if (code == HC_ACTION) {
                auto* cwp = reinterpret_cast<const CWPSTRUCT*>(l);
                if (cwp->message == kMsg) {
                    Payload* claimed = nullptr;
                    {
                        std::lock_guard<std::mutex> lk(pendingMutex);
                        auto it = std::find_if(
                            pending.begin(), pending.end(),
                            [id = (UINT_PTR)cwp->wParam](const auto& e) {
                                return e.first == id;
                            });
                        if (it != pending.end()) {
                            claimed = it->second;
                            pending.erase(it);
                        }
                    }
                    if (claimed) {
                        (*claimed->task)();
                        claimed->ran.store(true, std::memory_order_release);
                    }
                }
            }
            return CallNextHookEx(nullptr, code, w, l);
        },
        nullptr, tid);
    if (!hook) {
        std::lock_guard<std::mutex> lk(pendingMutex);
        std::erase_if(pending, [id](const auto& e) { return e.first == id; });
        return false;
    }

    SendMessageTimeoutW(hWnd, kMsg, id, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK,
                         timeoutMs, nullptr);
    UnhookWindowsHookEx(hook);

    {
        std::lock_guard<std::mutex> lk(pendingMutex);
        std::erase_if(pending, [id](const auto& e) { return e.first == id; });
    }
    return payload.ran.load(std::memory_order_acquire);
}

// ---------------------------------------------------------------------
// System usage bars (this mod's own code)
//
// Extracted from taskbar-widget-stack.wh.cpp's SystemUsageWidget - see
// that file's PLAN.md (Incidents 30-32) for the history: bar sizing/
// spacing tuning, the CPU-row-clipping bug (VerticalAlignment set on the
// wrong element) and its fix. Simplified here since this mod injects the
// bars directly as its own top-level content - no multi-widget stack, no
// shared PaneHeight invariant to satisfy, no slider/dots/nav.
// ---------------------------------------------------------------------

struct {
    bool showCpu = true;
    bool showRam = true;
    bool showGpu = true;
    int refreshSeconds = 1;
} g_settings;

void LoadSettings() {
    g_settings.showCpu = Wh_GetIntSetting(L"showCpu");
    g_settings.showRam = Wh_GetIntSetting(L"showRam");
    g_settings.showGpu = Wh_GetIntSetting(L"showGpu");
    int seconds = Wh_GetIntSetting(L"refreshSeconds");
    g_settings.refreshSeconds = (seconds >= 1 && seconds <= 5) ? seconds : 1;
}

struct UiState {
    HWND hWnd{nullptr};
    bool windowSubclassed = false;
    Grid injectionParent{nullptr};  // The taskbar's RootGrid.
    StackPanel root{nullptr};

    ColumnDefinition cpuFillCol{nullptr};
    ColumnDefinition cpuEmptyCol{nullptr};
    TextBlock cpuPercentText{nullptr};

    ColumnDefinition ramFillCol{nullptr};
    ColumnDefinition ramEmptyCol{nullptr};
    TextBlock ramPercentText{nullptr};

    ColumnDefinition gpuFillCol{nullptr};
    ColumnDefinition gpuEmptyCol{nullptr};
    TextBlock gpuPercentText{nullptr};

    DispatcherTimer timer{nullptr};
    winrt::event_token timerToken;
};

UiState g_ui;
HWND g_taskbarWnd;
HANDLE g_retryThread;
HANDLE g_injectEvent;
std::mutex g_retryThreadMutex;
std::atomic<bool> g_stopRequested{false};

ULONGLONG g_lastIdle = 0;
ULONGLONG g_lastKernel = 0;
ULONGLONG g_lastUser = 0;
bool g_haveCpuSample = false;
double g_lastCpuPercent = 0.0;

PDH_HQUERY g_pdhQuery = nullptr;
PDH_HCOUNTER g_pdhCounter = nullptr;
bool g_pdhOk = false;

void BuildRow(StackPanel& content,
              const wchar_t* label,
              ColumnDefinition& fillCol,
              ColumnDefinition& emptyCol,
              TextBlock& percentText) {
    Grid row;
    row.Margin({0, 1, 0, 1});
    ColumnDefinition labelCol;
    labelCol.Width({22, GridUnitType::Pixel});
    ColumnDefinition trackCol;
    trackCol.Width({1.0, GridUnitType::Star});
    ColumnDefinition percentCol;
    percentCol.Width({26, GridUnitType::Pixel});
    row.ColumnDefinitions().Append(labelCol);
    row.ColumnDefinitions().Append(trackCol);
    row.ColumnDefinitions().Append(percentCol);

    TextBlock labelText;
    labelText.Text(winrt::hstring(label));
    labelText.FontSize(9);
    labelText.VerticalAlignment(VerticalAlignment::Center);
    SolidColorBrush textBrush{
        winrt::Windows::UI::ColorHelper::FromArgb(255, 220, 220, 220)};
    labelText.Foreground(textBrush);
    Grid::SetColumn(labelText, 0);
    row.Children().Append(labelText);

    Border track;
    track.Height(4);
    track.Margin({4, 0, 4, 0});
    track.CornerRadius({2, 2, 2, 2});
    SolidColorBrush trackBrush{
        winrt::Windows::UI::ColorHelper::FromArgb(255, 70, 70, 70)};
    track.Background(trackBrush);
    track.VerticalAlignment(VerticalAlignment::Center);
    Grid::SetColumn(track, 1);

    fillCol = ColumnDefinition();
    fillCol.Width({0.0, GridUnitType::Star});
    emptyCol = ColumnDefinition();
    emptyCol.Width({100.0, GridUnitType::Star});
    Grid fillGrid;
    fillGrid.ColumnDefinitions().Append(fillCol);
    fillGrid.ColumnDefinitions().Append(emptyCol);
    Border fill;
    SolidColorBrush fillBrush{
        winrt::Windows::UI::ColorHelper::FromArgb(255, 90, 170, 230)};
    fill.Background(fillBrush);
    fill.CornerRadius({2, 2, 2, 2});
    Grid::SetColumn(fill, 0);
    fillGrid.Children().Append(fill);
    track.Child(fillGrid);
    row.Children().Append(track);

    percentText = TextBlock();
    percentText.Text(L"0%");
    percentText.FontSize(9);
    percentText.HorizontalAlignment(HorizontalAlignment::Left);
    percentText.VerticalAlignment(VerticalAlignment::Center);
    percentText.Foreground(textBrush);
    Grid::SetColumn(percentText, 2);
    row.Children().Append(percentText);

    content.Children().Append(row);
}

void ApplyBar(ColumnDefinition& fillCol,
              ColumnDefinition& emptyCol,
              TextBlock& percentText,
              double percent) {
    percent = std::clamp(percent, 0.0, 100.0);
    try {
        fillCol.Width({percent, GridUnitType::Star});
        emptyCol.Width({100.0 - percent, GridUnitType::Star});
        wchar_t buf[8];
        wsprintfW(buf, L"%d%%", (int)std::lround(percent));
        percentText.Text(buf);
    } catch (...) {
    }
}

double SampleCpu() {
    FILETIME idleTime, kernelTime, userTime;
    if (!GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        return g_lastCpuPercent;
    }
    auto toULL = [](const FILETIME& ft) -> ULONGLONG {
        ULARGE_INTEGER u;
        u.LowPart = ft.dwLowDateTime;
        u.HighPart = ft.dwHighDateTime;
        return u.QuadPart;
    };
    ULONGLONG idle = toULL(idleTime);
    ULONGLONG kernel = toULL(kernelTime);
    ULONGLONG user = toULL(userTime);

    if (g_haveCpuSample) {
        ULONGLONG idleDelta = idle - g_lastIdle;
        // GetSystemTimes' kernelTime already includes idle time.
        ULONGLONG totalDelta = (kernel - g_lastKernel) + (user - g_lastUser);
        if (totalDelta > 0) {
            double busy =
                (double)(totalDelta - idleDelta) / (double)totalDelta;
            g_lastCpuPercent = std::clamp(busy * 100.0, 0.0, 100.0);
        }
    }
    g_lastIdle = idle;
    g_lastKernel = kernel;
    g_lastUser = user;
    g_haveCpuSample = true;
    return g_lastCpuPercent;
}

double SampleRam() {
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        return (double)status.dwMemoryLoad;
    }
    return 0.0;
}

void InitPdh() {
    if (PdhOpenQueryW(nullptr, 0, &g_pdhQuery) != ERROR_SUCCESS) {
        g_pdhOk = false;
        return;
    }
    if (PdhAddEnglishCounterW(g_pdhQuery,
                               L"\\GPU Engine(*)\\Utilization Percentage", 0,
                               &g_pdhCounter) != ERROR_SUCCESS) {
        PdhCloseQuery(g_pdhQuery);
        g_pdhQuery = nullptr;
        g_pdhOk = false;
        return;
    }
    // Primes the query - PDH counters need at least one prior sample
    // before a formatted value means anything.
    PdhCollectQueryData(g_pdhQuery);
    g_pdhOk = true;
}

void ClosePdh() {
    if (g_pdhQuery) {
        PdhCloseQuery(g_pdhQuery);
        g_pdhQuery = nullptr;
    }
    g_pdhCounter = nullptr;
    g_pdhOk = false;
}

// Takes the MAX across all reported engine instances rather than Task
// Manager's more selective per-engine-type accounting - a known
// simplification, can read a little differently from Task Manager's own
// GPU% on some systems.
double SampleGpu() {
    if (PdhCollectQueryData(g_pdhQuery) != ERROR_SUCCESS) {
        return -1.0;
    }
    DWORD bufferSize = 0, itemCount = 0;
    PDH_STATUS status = PdhGetFormattedCounterArrayW(
        g_pdhCounter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr);
    if (status != PDH_MORE_DATA || bufferSize == 0) {
        return -1.0;
    }
    std::vector<BYTE> buffer(bufferSize);
    auto* items =
        reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
    if (PdhGetFormattedCounterArrayW(g_pdhCounter, PDH_FMT_DOUBLE,
                                      &bufferSize, &itemCount,
                                      items) != ERROR_SUCCESS) {
        return -1.0;
    }
    double maxUsage = 0.0;
    for (DWORD i = 0; i < itemCount; i++) {
        if (items[i].FmtValue.CStatus == ERROR_SUCCESS) {
            maxUsage = std::max(maxUsage, items[i].FmtValue.doubleValue);
        }
    }
    return std::clamp(maxUsage, 0.0, 100.0);
}

void UpdateValues() {
    if (g_settings.showCpu && g_ui.cpuPercentText) {
        ApplyBar(g_ui.cpuFillCol, g_ui.cpuEmptyCol, g_ui.cpuPercentText,
                  SampleCpu());
    }
    if (g_settings.showRam && g_ui.ramPercentText) {
        ApplyBar(g_ui.ramFillCol, g_ui.ramEmptyCol, g_ui.ramPercentText,
                  SampleRam());
    }
    if (g_settings.showGpu && g_ui.gpuPercentText) {
        double gpu = g_pdhOk ? SampleGpu() : -1.0;
        if (gpu >= 0.0) {
            ApplyBar(g_ui.gpuFillCol, g_ui.gpuEmptyCol, g_ui.gpuPercentText,
                      gpu);
        } else {
            ApplyBar(g_ui.gpuFillCol, g_ui.gpuEmptyCol, g_ui.gpuPercentText,
                      0.0);
            try {
                g_ui.gpuPercentText.Text(L"N/A");
            } catch (...) {
            }
        }
    }
}

void StartTimer() {
    g_ui.timer = DispatcherTimer();
    g_ui.timer.Interval(
        std::chrono::seconds(std::max(1, g_settings.refreshSeconds)));
    g_ui.timerToken = g_ui.timer.Tick(
        [](winrt::Windows::Foundation::IInspectable const&,
           winrt::Windows::Foundation::IInspectable const&) {
            UpdateValues();
        });
    g_ui.timer.Start();
}

void StopTimer() {
    if (g_ui.timer) {
        try {
            if (g_ui.timerToken) {
                g_ui.timer.Tick(g_ui.timerToken);
            }
            g_ui.timer.Stop();
        } catch (...) {
        }
        g_ui.timer = nullptr;
    }
}

// ---------------------------------------------------------------------
// Injection lifecycle
// ---------------------------------------------------------------------

LRESULT CALLBACK TaskbarWindowSubclassProc(HWND hWnd, UINT msg, WPARAM wParam,
                                            LPARAM lParam, UINT_PTR);
void RemoveSystemUsageGrid();

constexpr double kLeftEdgeGap = 6.0;

bool InjectSystemUsageGrid(HWND hWnd) {
    if (g_ui.root) {
        return true;  // Already injected for this taskbar instance.
    }

    auto xamlRoot = GetTaskbarXamlRoot(hWnd);
    if (!xamlRoot) {
        return false;
    }
    auto rootElement = xamlRoot.Content().try_as<FrameworkElement>();
    if (!rootElement) {
        return false;
    }
    auto taskbarRootGrid = FindTaskbarRootGrid(rootElement);
    if (!taskbarRootGrid) {
        return false;
    }
    auto repeater = FindChildByName(taskbarRootGrid, L"TaskbarFrameRepeater");
    auto startButton = FindStartButton(repeater);
    if (!repeater || !startButton) {
        return false;
    }

    try {
        LoadSettings();

        StackPanel root;
        root.Orientation(Orientation::Vertical);
        root.VerticalAlignment(VerticalAlignment::Center);
        root.HorizontalAlignment(HorizontalAlignment::Left);
        root.Margin({kLeftEdgeGap, 0, 0, 0});
        root.Padding({4, 0, 4, 0});

        if (g_settings.showCpu) {
            BuildRow(root, L"CPU", g_ui.cpuFillCol, g_ui.cpuEmptyCol,
                     g_ui.cpuPercentText);
        }
        if (g_settings.showRam) {
            BuildRow(root, L"RAM", g_ui.ramFillCol, g_ui.ramEmptyCol,
                     g_ui.ramPercentText);
        }
        if (g_settings.showGpu) {
            BuildRow(root, L"GPU", g_ui.gpuFillCol, g_ui.gpuEmptyCol,
                     g_ui.gpuPercentText);
        }

        Canvas::SetZIndex(root, 1000);
        taskbarRootGrid.Children().Append(root);

        g_ui.hWnd = hWnd;
        g_ui.injectionParent = taskbarRootGrid;
        g_ui.root = root;

        if (!g_ui.windowSubclassed) {
            g_ui.windowSubclassed = WindhawkUtils::SetWindowSubclassFromAnyThread(
                hWnd, TaskbarWindowSubclassProc, 0);
        }

        InitPdh();
        SampleCpu();  // primes the delta baseline, first return unused
        UpdateValues();
        StartTimer();

        Wh_Log(L"Injected system usage bars");
        return true;
    } catch (...) {
        Wh_Log(L"InjectSystemUsageGrid: exception");
        g_ui = {};
        return false;
    }
}

void RemoveSystemUsageGrid() {
    StopTimer();
    ClosePdh();

    if (!g_ui.root || !g_ui.injectionParent) {
        g_ui = {};
        return;
    }
    try {
        auto rootGrid = g_ui.injectionParent;
        uint32_t rootIndex;
        if (rootGrid.Children().IndexOf(g_ui.root, rootIndex)) {
            rootGrid.Children().RemoveAt(rootIndex);
        }
    } catch (...) {
        Wh_Log(L"RemoveSystemUsageGrid: exception");
    }
    g_ui = {};
}

LRESULT CALLBACK TaskbarWindowSubclassProc(HWND hWnd, UINT msg, WPARAM wParam,
                                            LPARAM lParam, UINT_PTR) {
    if (msg == WM_NCDESTROY) {
        StopTimer();
        ClosePdh();
        g_ui = {};
    } else if (msg == WM_DISPLAYCHANGE && !g_stopRequested) {
        StopTimer();
        ClosePdh();
        g_ui = {};
        if (g_injectEvent) {
            SetEvent(g_injectEvent);
        }
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

DWORD WINAPI RetryInjectThreadProc(LPVOID) {
    for (int attempt = 0; attempt < 600; attempt++) {
        if (g_stopRequested.load(std::memory_order_acquire)) {
            return 0;
        }
        HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
        if (tray) {
            g_taskbarWnd = tray;
            bool ok = false;
            RunFromWindowThread(tray, [&] { ok = InjectSystemUsageGrid(tray); });
            if (ok) {
                return 0;
            }
        }
        WaitForSingleObject(g_injectEvent, 500);
        ResetEvent(g_injectEvent);
    }
    Wh_Log(L"Giving up on system usage bars injection");
    return 0;
}

void StartRetryInject() {
    std::lock_guard<std::mutex> lk(g_retryThreadMutex);
    if (g_stopRequested.load(std::memory_order_acquire)) {
        return;
    }
    if (g_retryThread &&
        WaitForSingleObject(g_retryThread, 0) == WAIT_OBJECT_0) {
        CloseHandle(g_retryThread);
        g_retryThread = nullptr;
    }
    if (!g_retryThread) {
        g_retryThread =
            CreateThread(nullptr, 0, RetryInjectThreadProc, nullptr, 0, nullptr);
    } else if (g_injectEvent) {
        SetEvent(g_injectEvent);
    }
}

void WINAPI TrayUI_StartTaskbar_Hook(void* pThis) {
    TrayUI_StartTaskbar_Original(pThis);
    if (g_stopRequested.load(std::memory_order_acquire)) {
        return;
    }
    RunFromWindowThread(g_taskbarWnd ? g_taskbarWnd
                                      : FindWindowW(L"Shell_TrayWnd", nullptr),
                         RemoveSystemUsageGrid);
    StartRetryInject();
}

}  // namespace

BOOL Wh_ModInit() {
    LoadSettings();

    g_stopRequested = false;
    g_injectEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_injectEvent) {
        return FALSE;
    }

    if (!HookTaskbarDllSymbols()) {
        Wh_Log(L"HookTaskbarDllSymbols failed");
        CloseHandle(g_injectEvent);
        g_injectEvent = nullptr;
        return FALSE;
    }

    return TRUE;
}

void Wh_ModAfterInit() {
    StartRetryInject();
}

void Wh_ModSettingsChanged() {
    LoadSettings();
    if (g_ui.hWnd) {
        HWND hWnd = g_ui.hWnd;
        RunFromWindowThread(hWnd, [] {
            RemoveSystemUsageGrid();
            InjectSystemUsageGrid(g_taskbarWnd);
        });
    }
}

void Wh_ModBeforeUninit() {
    g_stopRequested = true;
    if (g_injectEvent) {
        SetEvent(g_injectEvent);
    }
    {
        std::lock_guard<std::mutex> lk(g_retryThreadMutex);
        if (g_retryThread) {
            WaitForSingleObject(g_retryThread, 3000);
            CloseHandle(g_retryThread);
            g_retryThread = nullptr;
        }
    }

    if (g_ui.root && g_ui.hWnd) {
        HWND hWnd = g_ui.hWnd;
        if (g_ui.windowSubclassed) {
            WindhawkUtils::RemoveWindowSubclassFromAnyThread(
                hWnd, TaskbarWindowSubclassProc);
        }
        RunFromWindowThread(hWnd, RemoveSystemUsageGrid);
    }
    g_ui = {};

    if (g_injectEvent) {
        CloseHandle(g_injectEvent);
        g_injectEvent = nullptr;
    }
}
