// ==WindhawkMod==
// @id              probe-explorer-viewmenu
// @name            Probe: File Explorer View menu
// @description     Diagnostic only. Traces how File Explorer builds its View menu, and can add one test item to it.
// @version         0.7
// @author          AristideBH
// @github          https://github.com/AristideBH
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Probe: File Explorer View menu

Tests the first thing a native "Columns" view needs: can a mod add its own
clickable entry to File Explorer's View menu?

It hooks five functions in `FileExplorerExtensions.dll`, the module that builds
the command bar. Enabling it takes 10 to 20 seconds the first time, while
Windhawk reads that module's symbols.

**Trace (always).** Logs each hooked call and lists the menu items Explorer
builds: class, text and name.

**Add a test item (off by default).** While the View menu is being built,
appends one item named "Colonnes (test Windhawk)". Clicking it only writes a
line to the log.

## How to run it

1. Enable the mod and wait for `HOOKS READY` in the log.
2. In any File Explorer window, click once on an empty part of the command
   bar, or on the View button. The log should show `TREE WALK`.
3. Send the log.
4. Then turn on the test item, click the command bar again, open View, say
   whether "Colonnes (test Windhawk)" shows and where, click it, and send
   that log too.

Close any open Explorer menu before disabling the mod.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- addProbeItem: false
  $name: Add a test item to the View menu
  $description: Inserts "Colonnes (test Windhawk)" into the View menu, after the last layout. Off means trace only.
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>

#include <windows.h>

// winbase.h defines GetCurrentTime as a macro, which breaks a method of the
// same name in the WinUI animation headers.
#undef GetCurrentTime

#include <atomic>
#include <cwctype>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>

namespace mux = winrt::Microsoft::UI::Xaml;
namespace muxc = winrt::Microsoft::UI::Xaml::Controls;

using ItemsVector =
    winrt::Windows::Foundation::Collections::IVector<muxc::MenuFlyoutItemBase>;

namespace {

std::atomic<bool> g_addProbeItem{false};
std::atomic<bool> g_unloading{false};

// Set while Explorer runs CreateViewsMenu on this thread, so the generic
// ReconcileMenuItems hook knows the vector it sees belongs to the View menu.
thread_local int g_inViewsMenu = 0;
thread_local int g_inSortMenu = 0;
// Stops the probe's own Append from being treated as Explorer's work if it
// re-enters a hooked function.
thread_local bool g_appending = false;

// Everything the probe added, so unload can take it out again on the UI
// thread that owns it.
struct Added {
    ItemsVector items{nullptr};
    muxc::MenuFlyoutItem item{nullptr};
    winrt::event_token clickToken{};
};
std::mutex g_addedMutex;
[[clang::no_destroy]] std::vector<Added> g_added;

void LogHresult(const wchar_t* where) {
    HRESULT hr = winrt::to_hresult();
    Wh_Log(L"  %s threw hr=0x%08lX", where, hr);
}

void LogItems(ItemsVector const& items, const wchar_t* label) {
    try {
        if (!items) {
            Wh_Log(L"  %s: items vector is null", label);
            return;
        }
        uint32_t count = items.Size();
        Wh_Log(L"  %s: %u items", label, count);
        for (uint32_t i = 0; i < count && i < 40; i++) {
            auto base = items.GetAt(i);
            winrt::hstring cls = winrt::get_class_name(base);
            winrt::hstring text;
            if (auto item = base.try_as<muxc::MenuFlyoutItem>()) {
                text = item.Text();
            } else if (auto sub = base.try_as<muxc::MenuFlyoutSubItem>()) {
                text = sub.Text();
            }
            Wh_Log(L"    [%u] %s text=\"%s\" name=\"%s\"", i, cls.c_str(),
                   text.c_str(), base.Name().c_str());
        }
    } catch (...) {
        LogHresult(label);
    }
}

bool AlreadyAdded(ItemsVector const& items) {
    std::lock_guard<std::mutex> lock(g_addedMutex);
    for (auto const& added : g_added) {
        if (added.items == items) {
            uint32_t index = 0;
            if (added.item && items.IndexOf(added.item, index)) {
                return true;
            }
        }
    }
    return false;
}

// Inserts the test item at `index`, or at the end when the index is past it.
void AppendProbeItem(ItemsVector const& items, uint32_t index = UINT32_MAX) {
    try {
        if (!items || AlreadyAdded(items)) {
            return;
        }

        // 0.7: a radio item like its neighbours, in their group, with an icon.
        // What the first neighbour looks like is logged so the final mod can
        // copy it exactly.
        winrt::hstring groupName;
        try {
            if (items.Size() > 0) {
                if (auto first =
                        items.GetAt(0).try_as<muxc::RadioMenuFlyoutItem>()) {
                    groupName = first.GroupName();
                    auto firstIcon = first.Icon();
                    std::wstring iconClass =
                        firstIcon ? std::wstring{winrt::get_class_name(firstIcon)
                                                     .c_str()}
                                  : L"(none)";
                    std::wstring sourceClass;
                    if (firstIcon) {
                        if (auto image = firstIcon.try_as<muxc::ImageIcon>()) {
                            auto source = image.Source();
                            sourceClass =
                                source ? winrt::get_class_name(source).c_str()
                                       : L"(none)";
                        }
                    }
                    Wh_Log(L"  first neighbour: group=\"%s\" icon=%s source=%s checked=%d",
                           groupName.c_str(), iconClass.c_str(),
                           sourceClass.c_str(), first.IsChecked() ? 1 : 0);
                }
            }
        } catch (...) {
            LogHresult(L"reading the first neighbour");
        }

        muxc::RadioMenuFlyoutItem item;
        item.Text(L"Colonnes (test Windhawk)");
        if (!groupName.empty()) {
            item.GroupName(groupName);
        }
        try {
            // A stand-in glyph, the bulleted list, until a real icon is drawn.
            muxc::FontIcon icon;
            icon.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
            icon.Glyph(L"\uE8FD");
            item.Icon(icon);
        } catch (...) {
            LogHresult(L"setting the icon");
        }
        item.Name(L"WindhawkProbeItem");
        auto token = item.Click(
            [](winrt::Windows::Foundation::IInspectable const&,
               mux::RoutedEventArgs const&) {
                Wh_Log(L"PROBE ITEM CLICKED on thread %lu",
                       GetCurrentThreadId());
            });

        g_appending = true;
        if (index < items.Size()) {
            items.InsertAt(index, item);
        } else {
            items.Append(item);
        }
        g_appending = false;

        {
            std::lock_guard<std::mutex> lock(g_addedMutex);
            g_added.push_back({items, item, token});
        }
        Wh_Log(L"  PROBE ITEM APPENDED, vector now has %u items", items.Size());
    } catch (...) {
        g_appending = false;
        LogHresult(L"AppendProbeItem");
    }
}

// The hooked functions are C++ member functions, so the first argument is the
// object. Reference parameters arrive as pointers, and a C++/WinRT projected
// type is one pointer wide, so the references below match what Explorer
// passes. Parameters whose types this mod doesn't know stay as void*.

// Defined further down. Returns the flyout's CommandBarFlyoutKind, or -1.
int KindOf(void* flyout);

using CreateMenu_t = void(WINAPI*)(void* pThis);
CreateMenu_t CreateViewsMenu_Original;
void WINAPI CreateViewsMenu_Hook(void* pThis) {
    Wh_Log(L"> CreateViewsMenu this=%p kind=%d thread=%lu", pThis,
           KindOf(pThis), GetCurrentThreadId());
    g_inViewsMenu++;
    CreateViewsMenu_Original(pThis);
    g_inViewsMenu--;
    Wh_Log(L"< CreateViewsMenu");
}

CreateMenu_t CreateSortAndGroupMenu_Original;
void WINAPI CreateSortAndGroupMenu_Hook(void* pThis) {
    Wh_Log(L"> CreateSortAndGroupMenu this=%p kind=%d thread=%lu", pThis,
           KindOf(pThis), GetCurrentThreadId());
    g_inSortMenu++;
    CreateSortAndGroupMenu_Original(pThis);
    g_inSortMenu--;
    Wh_Log(L"< CreateSortAndGroupMenu");
}

using ReconcileMenuItems_t = void(WINAPI*)(void* pThis,
                                           ItemsVector const& items,
                                           void* itemViewModels,
                                           unsigned int index,
                                           int const& states,
                                           winrt::hstring const& text);
ReconcileMenuItems_t ReconcileMenuItems_Original;
void WINAPI ReconcileMenuItems_Hook(void* pThis,
                                    ItemsVector const& items,
                                    void* itemViewModels,
                                    unsigned int index,
                                    int const& states,
                                    winrt::hstring const& text) {
    if (g_appending) {
        ReconcileMenuItems_Original(pThis, items, itemViewModels, index, states,
                                    text);
        return;
    }

    Wh_Log(L"> ReconcileMenuItems this=%p index=%u states=%d text=\"%s\" inViews=%d inSort=%d",
           pThis, index, states, text.c_str(), g_inViewsMenu, g_inSortMenu);
    ReconcileMenuItems_Original(pThis, items, itemViewModels, index, states,
                                text);
    LogItems(items, L"after reconcile");

    if (g_inViewsMenu > 0 && g_addProbeItem && !g_unloading) {
        AppendProbeItem(items);
    }
    Wh_Log(L"< ReconcileMenuItems");
}

using InitializeIconSizeSubItem_t = void(WINAPI*)(void* pThis,
                                                  void* itemViewModel,
                                                  void* icon);
InitializeIconSizeSubItem_t InitializeIconSizeSubItem_Original;
void WINAPI InitializeIconSizeSubItem_Hook(void* pThis,
                                           void* itemViewModel,
                                           void* icon) {
    Wh_Log(L"> InitializeIconSizeSubItem this=%p inViews=%d", pThis,
           g_inViewsMenu);
    InitializeIconSizeSubItem_Original(pThis, itemViewModel, icon);
}

using InvokeMenuItem_t = void(WINAPI*)(void* pThis, void* menuItem);
InvokeMenuItem_t InvokeMenuItem_Original;
void WINAPI InvokeMenuItem_Hook(void* pThis, void* menuItem) {
    Wh_Log(L"> CommandBarManager::InvokeMenuItem this=%p menuItem=%p thread=%lu",
           pThis, menuItem, GetCurrentThreadId());
    InvokeMenuItem_Original(pThis, menuItem);
}

// ---- Wider trace, added in 0.3 ----
// Run 0.2 showed Sort calls CreateSortAndGroupMenu and View calls none of the
// five hooked functions. These log-only hooks cover the other ways this module
// can build a menu. Every hook declares all of the real function's arguments,
// so nothing is lost on the way to the original.

// Resolved by address only, never hooked.
using FlyoutKind_t = int(WINAPI*)(void* pThis);
FlyoutKind_t FlyoutKind_Fn;
// A member function that returns a class by value takes the result's address
// as a hidden second argument and returns it.
using VmId_t = winrt::hstring*(WINAPI*)(void* pThis, winrt::hstring* result);
VmId_t VmId_Fn;

int KindOf(void* flyout) {
    return (FlyoutKind_Fn && flyout) ? FlyoutKind_Fn(flyout) : -1;
}

std::wstring VmIdOf(void* vm) {
    if (!VmId_Fn || !vm) {
        return L"?";
    }
    alignas(winrt::hstring) unsigned char storage[sizeof(winrt::hstring)] = {};
    auto* id = VmId_Fn(vm, reinterpret_cast<winrt::hstring*>(storage));
    std::wstring text{id->c_str()};
    id->~hstring();
    return text;
}

#define TRACE_FLYOUT_VOID(name)                                         \
    CreateMenu_t name##_Original;                                       \
    void WINAPI name##_Hook(void* pThis) {                              \
        Wh_Log(L"> " #name L" this=%p kind=%d thread=%lu", pThis,       \
               KindOf(pThis), GetCurrentThreadId());                    \
        name##_Original(pThis);                                         \
    }

TRACE_FLYOUT_VOID(CreateFiltersMenu)
TRACE_FLYOUT_VOID(CreateGalleryFilterMenu)
TRACE_FLYOUT_VOID(CreateNewItemMenu)
TRACE_FLYOUT_VOID(CreateSearchOptionsMenu)

using CreateTravelLogMenu_t = void(WINAPI*)(void* pThis,
                                            winrt::hstring const& text);
CreateTravelLogMenu_t CreateTravelLogMenu_Original;
void WINAPI CreateTravelLogMenu_Hook(void* pThis, winrt::hstring const& text) {
    Wh_Log(L"> CreateTravelLogMenu this=%p kind=%d text=\"%s\"", pThis,
           KindOf(pThis), text.c_str());
    CreateTravelLogMenu_Original(pThis, text);
}

using RebindItem_t = void(WINAPI*)(void* pThis,
                                   void* item,
                                   void* itemViewModel,
                                   int const& states,
                                   winrt::hstring const& text);
RebindItem_t RebindItem_Original;
void WINAPI RebindItem_Hook(void* pThis,
                            void* item,
                            void* itemViewModel,
                            int const& states,
                            winrt::hstring const& text) {
    Wh_Log(L"> RebindItem this=%p kind=%d states=%d text=\"%s\"", pThis,
           KindOf(pThis), states, text.c_str());
    RebindItem_Original(pThis, item, itemViewModel, states, text);
}

using PopulateMoreItemsMenu_t = void(WINAPI*)(void* pThis,
                                              void* subItem,
                                              void* iterator,
                                              winrt::hstring const& text1,
                                              void* itemViewModel,
                                              winrt::hstring const& text2);
PopulateMoreItemsMenu_t PopulateMoreItemsMenu_Original;
void WINAPI PopulateMoreItemsMenu_Hook(void* pThis,
                                       void* subItem,
                                       void* iterator,
                                       winrt::hstring const& text1,
                                       void* itemViewModel,
                                       winrt::hstring const& text2) {
    Wh_Log(L"> PopulateMoreItemsMenu this=%p kind=%d text1=\"%s\" text2=\"%s\"",
           pThis, KindOf(pThis), text1.c_str(), text2.c_str());
    PopulateMoreItemsMenu_Original(pThis, subItem, iterator, text1,
                                   itemViewModel, text2);
}

// A constructor returns the object.
using FlyoutCtor_t = void*(WINAPI*)(void* pThis);
FlyoutCtor_t FlyoutCtor_Original;
void* WINAPI FlyoutCtor_Hook(void* pThis) {
    void* result = FlyoutCtor_Original(pThis);
    Wh_Log(L"> CommandBarFlyout constructed this=%p thread=%lu", result,
           GetCurrentThreadId());
    return result;
}

using OneArg_t = void(WINAPI*)(void* pThis, void* arg);

OneArg_t MfcCreateMenu_Original;
void WINAPI MfcCreateMenu_Hook(void* pThis, void* menuItems) {
    Wh_Log(L"> MenuFlyoutControl::CreateMenu this=%p thread=%lu", pThis,
           GetCurrentThreadId());
    MfcCreateMenu_Original(pThis, menuItems);
    Wh_Log(L"< MenuFlyoutControl::CreateMenu");
}

OneArg_t MfcUpdateMenuItem_Original;
void WINAPI MfcUpdateMenuItem_Hook(void* pThis, void* menuItem) {
    Wh_Log(L"> MenuFlyoutControl::UpdateMenuItem this=%p menuItem=%p", pThis,
           menuItem);
    MfcUpdateMenuItem_Original(pThis, menuItem);
}

using MfcAppendToMenuFlyout_t = void(WINAPI*)(void* pThis,
                                              muxc::MenuFlyout& flyout,
                                              void* menuItem);
MfcAppendToMenuFlyout_t MfcAppendToMenuFlyout_Original;
void WINAPI MfcAppendToMenuFlyout_Hook(void* pThis,
                                       muxc::MenuFlyout& flyout,
                                       void* menuItem) {
    MfcAppendToMenuFlyout_Original(pThis, flyout, menuItem);
    try {
        auto items = flyout.Items();
        uint32_t count = items.Size();
        winrt::hstring text;
        if (count > 0) {
            if (auto item = items.GetAt(count - 1).try_as<muxc::MenuFlyoutItem>()) {
                text = item.Text();
            } else if (auto sub = items.GetAt(count - 1)
                                      .try_as<muxc::MenuFlyoutSubItem>()) {
                text = sub.Text();
            }
        }
        Wh_Log(L"> MenuFlyoutControl::AppendMenuItem(MenuFlyout) this=%p now %u items, last=\"%s\"",
               pThis, count, text.c_str());
    } catch (...) {
        LogHresult(L"AppendMenuItem(MenuFlyout)");
    }
}

using MfcAppendOther_t = void(WINAPI*)(void* pThis,
                                       void* container,
                                       void* menuItem);
MfcAppendOther_t MfcAppendToSubItem_Original;
void WINAPI MfcAppendToSubItem_Hook(void* pThis,
                                    void* container,
                                    void* menuItem) {
    Wh_Log(L"> MenuFlyoutControl::AppendMenuItem(MenuFlyoutSubItem) this=%p",
           pThis);
    MfcAppendToSubItem_Original(pThis, container, menuItem);
}

MfcAppendOther_t MfcAppendToCommandBarFlyout_Original;
void WINAPI MfcAppendToCommandBarFlyout_Hook(void* pThis,
                                             void* container,
                                             void* menuItem) {
    Wh_Log(L"> MenuFlyoutControl::AppendMenuItem(CommandBarFlyout) this=%p",
           pThis);
    MfcAppendToCommandBarFlyout_Original(pThis, container, menuItem);
}

CreateMenu_t VmUpdateSubItems_Original;
void WINAPI VmUpdateSubItems_Hook(void* pThis) {
    Wh_Log(L"> CommandBarItemVM::UpdateSubItems id=\"%s\" this=%p",
           VmIdOf(pThis).c_str(), pThis);
    VmUpdateSubItems_Original(pThis);
}

OneArg_t VmChangeMenuItem_Original;
void WINAPI VmChangeMenuItem_Hook(void* pThis, void* menuItem) {
    Wh_Log(L"> CommandBarItemVM::ChangeMenuItem id=\"%s\" this=%p",
           VmIdOf(pThis).c_str(), pThis);
    VmChangeMenuItem_Original(pThis, menuItem);
}

using VmSetChecked_t = void(WINAPI*)(void* pThis, bool checked);
VmSetChecked_t VmSetChecked_Original;
void WINAPI VmSetChecked_Hook(void* pThis, bool checked) {
    Wh_Log(L"> CommandBarItemVM::IsChecked(%d) id=\"%s\"", checked ? 1 : 0,
           VmIdOf(pThis).c_str());
    VmSetChecked_Original(pThis, checked);
}

// ---- Item builders, added in 0.4 ----
// Run 0.3: eight flyouts are constructed with each new window and seven get a
// Create...Menu call. The eighth, with no call, is the candidate for View.
// These hooks cover the functions that build single items.
//
// A member function that returns a class by value takes the result's address
// as a hidden second argument and returns that address. That holds for every
// class type, even an empty one such as fire_and_forget. The id helper above
// used the same rule in run 0.3 and returned correct strings on ARM64.

std::wstring TextOf(muxc::MenuFlyoutItemBase const& base) {
    try {
        if (!base) {
            return L"(null)";
        }
        std::wstring cls{winrt::get_class_name(base).c_str()};
        size_t dot = cls.rfind(L'.');
        if (dot != std::wstring::npos) {
            cls = cls.substr(dot + 1);
        }
        winrt::hstring text;
        if (auto item = base.try_as<muxc::MenuFlyoutItem>()) {
            text = item.Text();
        } else if (auto sub = base.try_as<muxc::MenuFlyoutSubItem>()) {
            text = sub.Text();
        }
        return cls + L" \"" + text.c_str() + L"\"";
    } catch (...) {
        return L"(error)";
    }
}

using CreateMenuAsync_t = void*(WINAPI*)(void* pThis, void* result);
CreateMenuAsync_t CreateMenuAsync_Original;
void* WINAPI CreateMenuAsync_Hook(void* pThis, void* result) {
    Wh_Log(L"> CreateMenuAsync this=%p kind=%d thread=%lu", pThis,
           KindOf(pThis), GetCurrentThreadId());
    return CreateMenuAsync_Original(pThis, result);
}

using HandleMenuItemsChanged_t = void*(WINAPI*)(void* pThis,
                                                void* result,
                                                ItemsVector* items,
                                                void* itemViewModels,
                                                void* changeArgs,
                                                int states,
                                                winrt::hstring* text);
HandleMenuItemsChanged_t HandleMenuItemsChanged_Original;
void* WINAPI HandleMenuItemsChanged_Hook(void* pThis,
                                         void* result,
                                         ItemsVector* items,
                                         void* itemViewModels,
                                         void* changeArgs,
                                         int states,
                                         winrt::hstring* text) {
    Wh_Log(L"> HandleMenuItemsChanged this=%p kind=%d states=%d text=\"%s\"",
           pThis, KindOf(pThis), states, text ? text->c_str() : L"");
    return HandleMenuItemsChanged_Original(pThis, result, items, itemViewModels,
                                           changeArgs, states, text);
}

using CreateMenuItem_t = muxc::MenuFlyoutItemBase*(WINAPI*)(
    void* pThis,
    muxc::MenuFlyoutItemBase* result,
    void* itemViewModel,
    int const& states,
    winrt::hstring const& text);
CreateMenuItem_t CreateMenuItem_Original;
muxc::MenuFlyoutItemBase* WINAPI CreateMenuItem_Hook(
    void* pThis,
    muxc::MenuFlyoutItemBase* result,
    void* itemViewModel,
    int const& states,
    winrt::hstring const& text) {
    auto* ret =
        CreateMenuItem_Original(pThis, result, itemViewModel, states, text);
    Wh_Log(L"> CreateMenuItem kind=%d states=%d text=\"%s\" -> %s",
           KindOf(pThis), states, text.c_str(),
           ret ? TextOf(*ret).c_str() : L"(null)");
    return ret;
}

using CreateSubItem_t = muxc::MenuFlyoutSubItem*(WINAPI*)(
    void* pThis,
    muxc::MenuFlyoutSubItem* result,
    void* itemViewModel,
    int const& states,
    winrt::hstring const& text);
CreateSubItem_t CreateMenuFlyoutSubItem_Original;
muxc::MenuFlyoutSubItem* WINAPI CreateMenuFlyoutSubItem_Hook(
    void* pThis,
    muxc::MenuFlyoutSubItem* result,
    void* itemViewModel,
    int const& states,
    winrt::hstring const& text) {
    auto* ret = CreateMenuFlyoutSubItem_Original(pThis, result, itemViewModel,
                                                 states, text);
    Wh_Log(L"> CreateMenuFlyoutSubItem kind=%d states=%d text=\"%s\" -> %s",
           KindOf(pThis), states, text.c_str(),
           ret ? TextOf(*ret).c_str() : L"(null)");
    return ret;
}

// One view-model argument, one item returned.
#define TRACE_ITEM_FROM_VM(name, type)                                        \
    using name##_t = type*(WINAPI*)(void* pThis, type* result,                \
                                    void* itemViewModel);                     \
    name##_t name##_Original;                                                 \
    type* WINAPI name##_Hook(void* pThis, type* result,                       \
                             void* itemViewModel) {                           \
        auto* ret = name##_Original(pThis, result, itemViewModel);            \
        Wh_Log(L"> " #name L" kind=%d -> %s", KindOf(pThis),                  \
               ret ? TextOf(*ret).c_str() : L"(null)");                       \
        return ret;                                                           \
    }

TRACE_ITEM_FROM_VM(CreateMenuFlyoutItem, muxc::MenuFlyoutItem)
TRACE_ITEM_FROM_VM(CreateToggleMenuFlyoutItem, muxc::ToggleMenuFlyoutItem)

// One view-model argument and one string, one radio item returned.
#define TRACE_RADIO(name)                                                     \
    using name##_t = muxc::RadioMenuFlyoutItem*(WINAPI*)(                     \
        void* pThis, muxc::RadioMenuFlyoutItem* result, void* itemViewModel,  \
        winrt::hstring const& text);                                          \
    name##_t name##_Original;                                                 \
    muxc::RadioMenuFlyoutItem* WINAPI name##_Hook(                            \
        void* pThis, muxc::RadioMenuFlyoutItem* result, void* itemViewModel,  \
        winrt::hstring const& text) {                                         \
        auto* ret = name##_Original(pThis, result, itemViewModel, text);      \
        Wh_Log(L"> " #name L" kind=%d text=\"%s\" -> %s", KindOf(pThis),      \
               text.c_str(), ret ? TextOf(*ret).c_str() : L"(null)");         \
        return ret;                                                           \
    }

TRACE_RADIO(CreateRadioMenuFlyoutItem)
TRACE_RADIO(CreateRadioForPane)

using AddBiLevelRadioSubMenu_t = bool(WINAPI*)(void* pThis,
                                               void* itemViewModel,
                                               unsigned int index,
                                               winrt::hstring const& text1,
                                               winrt::hstring const& text2);
AddBiLevelRadioSubMenu_t AddBiLevelRadioSubMenu_Original;
bool WINAPI AddBiLevelRadioSubMenu_Hook(void* pThis,
                                        void* itemViewModel,
                                        unsigned int index,
                                        winrt::hstring const& text1,
                                        winrt::hstring const& text2) {
    bool ret = AddBiLevelRadioSubMenu_Original(pThis, itemViewModel, index,
                                               text1, text2);
    Wh_Log(L"> AddBiLevelRadioSubMenu kind=%d index=%u text1=\"%s\" text2=\"%s\" -> %d",
           KindOf(pThis), index, text1.c_str(), text2.c_str(), ret ? 1 : 0);
    return ret;
}

using RegisterForSubItemChanges_t = void(WINAPI*)(void* pThis,
                                                  void* itemViewModels,
                                                  muxc::MenuFlyoutSubItem const& subItem,
                                                  winrt::hstring const& text,
                                                  unsigned int index);
RegisterForSubItemChanges_t RegisterForSubItemChanges_Original;
void WINAPI RegisterForSubItemChanges_Hook(void* pThis,
                                           void* itemViewModels,
                                           muxc::MenuFlyoutSubItem const& subItem,
                                           winrt::hstring const& text,
                                           unsigned int index) {
    Wh_Log(L"> RegisterForSubItemChanges kind=%d text=\"%s\" index=%u sub=%s",
           KindOf(pThis), text.c_str(), index, TextOf(subItem).c_str());
    RegisterForSubItemChanges_Original(pThis, itemViewModels, subItem, text,
                                       index);
}

// ---- Command bar tree walk, added in 0.5 ----
// Run 0.4: no builder function runs for View, on creation or on open. The
// module's symbols include compiled XAML bindings keyed by item id, so the
// View menu is most likely declared in XAML and attached to its button. These
// hooks sit on the command bar's own pointer and focus handlers, whose first
// argument is a live XAML object. From it the probe walks the tree, lists
// every button that has a flyout, and can append the test item to one.

thread_local ULONGLONG g_lastTreeWalk = 0;

std::wstring ClassOf(winrt::Windows::Foundation::IInspectable const& obj) {
    try {
        return std::wstring{winrt::get_class_name(obj).c_str()};
    } catch (...) {
        return L"?";
    }
}

bool ContainsNoCase(std::wstring const& text, std::wstring const& needle) {
    if (needle.empty()) {
        return false;
    }
    std::wstring a = text, b = needle;
    for (auto& ch : a) ch = (wchar_t)towlower(ch);
    for (auto& ch : b) ch = (wchar_t)towlower(ch);
    return a.find(b) != std::wstring::npos;
}

void LogFlyoutItems(ItemsVector const& items, int depth) {
    uint32_t count = items.Size();
    for (uint32_t i = 0; i < count && i < 40; i++) {
        auto base = items.GetAt(i);
        Wh_Log(L"      %*s[%u] %s name=\"%s\"", depth * 2, L"", i,
               TextOf(base).c_str(), base.Name().c_str());
        if (depth < 2) {
            if (auto sub = base.try_as<muxc::MenuFlyoutSubItem>()) {
                LogFlyoutItems(sub.Items(), depth + 1);
            }
        }
    }
}

void WalkForFlyouts(mux::DependencyObject const& node, int depth, int& visited,
                    int& found) {
    if (!node || depth > 40 || visited > 3000) {
        return;
    }
    visited++;

    if (auto button = node.try_as<muxc::Button>()) {
        auto flyout = button.Flyout();
        if (flyout) {
            found++;
            std::wstring name{button.Name().c_str()};
            std::wstring label, automationId;
            try {
                if (auto appBarButton = button.try_as<muxc::AppBarButton>()) {
                    label = appBarButton.Label().c_str();
                }
                automationId = mux::Automation::AutomationProperties::
                                   GetAutomationId(button)
                                       .c_str();
            } catch (...) {
            }
            Wh_Log(L"  BUTTON name=\"%s\" label=\"%s\" automationId=\"%s\" flyout=%s",
                   name.c_str(), label.c_str(), automationId.c_str(),
                   ClassOf(flyout).c_str());
            if (auto menu = flyout.try_as<muxc::MenuFlyout>()) {
                auto items = menu.Items();
                Wh_Log(L"    MenuFlyout with %u items", items.Size());
                LogFlyoutItems(items, 0);

                // Run 0.5: the View button has no name. Its flyout is the only
                // one that opens with eight radio items, the eight layouts,
                // followed by a separator. That holds in any language.
                bool isViewFlyout = items.Size() > 8;
                for (uint32_t i = 0; isViewFlyout && i < 8; i++) {
                    isViewFlyout =
                        items.GetAt(i).try_as<muxc::RadioMenuFlyoutItem>() !=
                        nullptr;
                }
                if (isViewFlyout) {
                    Wh_Log(L"    THIS IS THE VIEW FLYOUT");
                    if (g_addProbeItem && !g_unloading) {
                        // Right after the last layout, before the separator.
                        AppendProbeItem(items, 8);
                    }
                }
            }
        }
    }

    int children = mux::Media::VisualTreeHelper::GetChildrenCount(node);
    for (int i = 0; i < children; i++) {
        WalkForFlyouts(mux::Media::VisualTreeHelper::GetChild(node, i),
                       depth + 1, visited, found);
    }
}

void WalkFromSender(winrt::Windows::Foundation::IInspectable const& sender,
                    const wchar_t* source) {
    ULONGLONG now = GetTickCount64();
    if (now - g_lastTreeWalk < 3000) {
        return;
    }
    g_lastTreeWalk = now;

    try {
        auto node = sender.try_as<mux::DependencyObject>();
        if (!node) {
            Wh_Log(L"TREE WALK from %s: sender isn't a DependencyObject, class=%s",
                   source, ClassOf(sender).c_str());
            return;
        }
        Wh_Log(L"TREE WALK from %s: sender class=%s thread=%lu", source,
               ClassOf(sender).c_str(), GetCurrentThreadId());

        // Up to the command bar itself, not further, so the walk stays small.
        mux::DependencyObject root = node;
        for (int i = 0; i < 12; i++) {
            if (ContainsNoCase(ClassOf(root), L"CommandBarControl")) {
                break;
            }
            auto parent = mux::Media::VisualTreeHelper::GetParent(root);
            if (!parent) {
                break;
            }
            root = parent;
        }
        Wh_Log(L"  walking down from class=%s", ClassOf(root).c_str());

        int visited = 0, found = 0;
        WalkForFlyouts(root, 0, visited, found);
        Wh_Log(L"TREE WALK END: %d elements visited, %d buttons with a flyout",
               visited, found);
    } catch (...) {
        LogHresult(L"WalkFromSender");
    }
}

using BarHandler_t = void(WINAPI*)(
    void* pThis,
    winrt::Windows::Foundation::IInspectable const& sender,
    void* args);

#define TRACE_BAR_HANDLER(name)                                               \
    BarHandler_t name##_Original;                                             \
    void WINAPI name##_Hook(                                                  \
        void* pThis, winrt::Windows::Foundation::IInspectable const& sender,  \
        void* args) {                                                         \
        name##_Original(pThis, sender, args);                                 \
        WalkFromSender(sender, L"" #name);                                    \
    }

TRACE_BAR_HANDLER(BarPointerPressed)
TRACE_BAR_HANDLER(BarW1PointerPressed)
TRACE_BAR_HANDLER(BarGotFocus)
TRACE_BAR_HANDLER(BarW1GotFocus)

bool HookCommandBarSymbols() {
    HMODULE module = GetModuleHandleW(L"FileExplorerExtensions.dll");
    if (!module) {
        Wh_Log(L"FileExplorerExtensions.dll isn't loaded in this process. Open a File Explorer window, then re-enable the mod.");
        return false;
    }

    // Symbol names copied from the recon probe's log of 2026-11-12, Windows
    // build 26100, ARM64. All are optional: a missing one is logged and
    // the rest still work.
    WindhawkUtils::SYMBOL_HOOK hooks[] = {
        {{LR"(public: void __cdecl winrt::FileExplorerExtensions::implementation::CommandBarManager::InvokeMenuItem(struct winrt::WindowsUdk::UI::MenuItem const &))"},
         &InvokeMenuItem_Original,
         InvokeMenuItem_Hook,
         true},
        {{LR"(public: void __cdecl winrt::FileExplorerExtensions::implementation::CommandBarControl::PointerPressedHandler(struct winrt::Windows::Foundation::IInspectable const &,struct winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const &))"},
         &BarPointerPressed_Original,
         BarPointerPressed_Hook,
         true},
        {{LR"(public: void __cdecl winrt::FileExplorerExtensions::implementation::CommandBarControl_Wave1::PointerPressedHandler(struct winrt::Windows::Foundation::IInspectable const &,struct winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const &))"},
         &BarW1PointerPressed_Original,
         BarW1PointerPressed_Hook,
         true},
        {{LR"(public: void __cdecl winrt::FileExplorerExtensions::implementation::CommandBarControl::CommandBarControlGotFocusHandler(struct winrt::Windows::Foundation::IInspectable const &,struct winrt::Microsoft::UI::Xaml::RoutedEventArgs const &))"},
         &BarGotFocus_Original,
         BarGotFocus_Hook,
         true},
        {{LR"(public: void __cdecl winrt::FileExplorerExtensions::implementation::CommandBarControl_Wave1::CommandBarControlGotFocusHandler(struct winrt::Windows::Foundation::IInspectable const &,struct winrt::Microsoft::UI::Xaml::RoutedEventArgs const &))"},
         &BarW1GotFocus_Original,
         BarW1GotFocus_Hook,
         true},
    };

    bool ok = WindhawkUtils::HookSymbols(module, hooks, ARRAYSIZE(hooks));
    Wh_Log(L"HookSymbols returned %d", ok ? 1 : 0);
    Wh_Log(L"  InvokeMenuItem=%p", (void*)InvokeMenuItem_Original);
    Wh_Log(L"  BarPointerPressed=%p", (void*)BarPointerPressed_Original);
    Wh_Log(L"  BarW1PointerPressed=%p", (void*)BarW1PointerPressed_Original);
    Wh_Log(L"  BarGotFocus=%p", (void*)BarGotFocus_Original);
    Wh_Log(L"  BarW1GotFocus=%p", (void*)BarW1GotFocus_Original);
    return ok;
}

// Takes every added item out again. Each one is revoked and removed on its own
// UI thread, and the last references are dropped there too.
void RemoveAddedItems() {
    std::vector<Added> added;
    {
        std::lock_guard<std::mutex> lock(g_addedMutex);
        added.swap(g_added);
    }
    if (added.empty()) {
        return;
    }
    Wh_Log(L"Removing %d probe item(s)", (int)added.size());

    for (auto& entry : added) {
        HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        bool queued = false;
        try {
            auto queue = entry.item.DispatcherQueue();
            auto holder = std::make_shared<Added>(std::move(entry));
            entry = {};
            queued = queue.TryEnqueue([holder, done]() {
                try {
                    holder->item.Click(holder->clickToken);
                    uint32_t index = 0;
                    if (holder->items.IndexOf(holder->item, index)) {
                        holder->items.RemoveAt(index);
                    }
                } catch (...) {
                }
                holder->item = nullptr;
                holder->items = nullptr;
                if (done) {
                    SetEvent(done);
                }
            });
        } catch (...) {
            LogHresult(L"RemoveAddedItems");
        }
        if (queued && done) {
            DWORD wait = WaitForSingleObject(done, 3000);
            Wh_Log(L"  probe item removal %s",
                   wait == WAIT_OBJECT_0 ? L"done" : L"TIMED OUT");
        } else {
            Wh_Log(L"  probe item removal could not be queued");
        }
        // Left open after a timeout: the queued work may still signal it.
        if (done && (!queued || WaitForSingleObject(done, 0) == WAIT_OBJECT_0)) {
            CloseHandle(done);
        }
    }
}

}  // namespace

BOOL Wh_ModInit() {
#if defined(_M_ARM64) || defined(__aarch64__)
    const wchar_t* arch = L"arm64";
#elif defined(_M_X64) || defined(__x86_64__)
    const wchar_t* arch = L"x64";
#else
    const wchar_t* arch = L"x86";
#endif
    Wh_Log(L"PROBE START probe-explorer-viewmenu v0.7 compiled as %s, pid=%lu",
           arch, GetCurrentProcessId());

    g_addProbeItem = Wh_GetIntSetting(L"addProbeItem") != 0;
    Wh_Log(L"addProbeItem=%d", g_addProbeItem ? 1 : 0);

    // A failed lookup leaves the mod loaded and idle, so the log stays
    // readable in Windhawk.
    HookCommandBarSymbols();
    return TRUE;
}

void Wh_ModAfterInit() {
    Wh_Log(L"HOOKS READY. Open the View menu now.");
}

void Wh_ModSettingsChanged() {
    g_addProbeItem = Wh_GetIntSetting(L"addProbeItem") != 0;
    Wh_Log(L"addProbeItem=%d", g_addProbeItem ? 1 : 0);
    if (!g_addProbeItem) {
        RemoveAddedItems();
    }
}

void Wh_ModBeforeUninit() {
    g_unloading = true;
    RemoveAddedItems();
}

void Wh_ModUninit() {
    Wh_Log(L"PROBE END");
}
