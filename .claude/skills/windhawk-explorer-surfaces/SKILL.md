---
name: windhawk-explorer-surfaces
description: Map of what File Explorer and the desktop are made of, and which Windhawk technique reaches each part. Use for any mod idea that changes Explorer windows or the desktop, such as a new view mode, Miller columns, file labels or tags, desktop icon grouping or stacks, command bar or View menu entries, navigation pane changes or file list styling. Read it before estimating whether an Explorer idea is feasible and before choosing where to hook. This skill is mostly a survey of four published mods and is thin on first-hand experience, so it says what is confirmed and what is not.
---

# Windhawk Explorer surfaces

Load `windhawk-mod-development` and `windhawk-recon` first.

`explorer-extras` is the first mod in this repo that targets File Explorer, a Miller columns panel, tested through version 0.54. The section "What explorer-extras confirmed" records what it settled. This skill otherwise combines a reading of four published mods with a day of probe mods run on Ari's ARM64 laptop on 2026-11-12, Windows build 26100, French. Each statement says which of the two it rests on. Replace each "unconfirmed" with a dated fact as later work settles it.

The probe mods are worth keeping as starting points: `probe-explorer-recon`, `probe-explorer-viewmenu`, `probe-desktop-listview`, `probe-desktop-groupby` and `probe-symbols`, in the repo's `probes` folder.

## One window, four technologies

A File Explorer window is not one UI. A mod must pick the surface first, because nothing transfers between them.

| Surface | Technology | Reference mod | Technique |
| --- | --- | --- | --- |
| Tabs, command bar, address bar, their menus | WinUI 3, `Microsoft.UI.Xaml` | Windows 11 File Explorer Styler by m417z | XAML diagnostics API |
| File list and its column header | DirectUI, `dui70.dll` and `duser.dll` | File Explorer WinUI-Like Content Animation by crazyboyybs | Symbol hooks, DirectComposition overlay |
| Legacy dialogs, menus and classic controls, in any process | Win32 and uxtheme | Win32 UI Modernizer by crazyboyybs | API hooks, repainting |
| The desktop | Win32 list view | none read | unconfirmed, see below |

Sources, all under `https://raw.githubusercontent.com/ramensoftware/windhawk-mods/main/mods/`: `windows-11-file-explorer-styler.wh.cpp`, `file-explorer-content-animation.wh.cpp`, `win32-ui-modernizer.wh.cpp`, `explorer-column-view.wh.cpp`. Read the relevant one before writing code. The comments in the animation mod are in Portuguese.

Two facts apply to every Explorer mod:

- **There can be several explorer.exe processes.** The option to launch folder windows in a separate process creates more, and the mod loads in each. Don't assume the process that owns the taskbar also owns the folder window. Keep state per process and per window.
- **Each tab is its own view.** A window with three tabs has three file lists. The tab's host window class is `ShellTabWindowClass`. Track state per tab.

## The XAML chrome

The command bar and tabs use WinUI 3 from the Windows App SDK. The namespace is `Microsoft.UI.Xaml`, not the `Windows.UI.Xaml` used by the taskbar, and the two don't mix. Windhawk's compiler ships the WinUI 3 C++/WinRT headers, so a mod can use `winrt::Microsoft::UI::Xaml` types directly. Put `#undef GetCurrentTime` before including them.

### Confirmed window layout

A `CabinetWClass` window contains, as siblings:

- `ShellTabWindowClass`, one per tab, holding `DUIViewWndClassName`, then `DirectUIHWND`. Inside that are the navigation pane, a classic `SysTreeView32`, and `SHELLDLL_DefView` with its own `DirectUIHWND` for the file list.
- `Microsoft.UI.Content.DesktopChildSiteBridge`, the WinUI 3 island that draws the chrome over the whole window.

Each window has its own UI thread.

### The command bar is in FileExplorerExtensions.dll

Confirmed. The module ships in the `MicrosoftWindows.Client.FileExp` app package, and symbol lookup works on it. Its classes, all under `winrt::FileExplorerExtensions::implementation`:

- `CommandBarControl_Wave1` is the live command bar control. `CommandBarControl` also exists and was not seen in use.
- `CommandBarFlyout` is the menu behind each dropdown button. It behaves as a `MenuFlyout`.
- `CommandBarItemVM` is one command. Its `Id()` returns names such as `windows.iconsize.details`, `windows.sortbycolumn:0` and `windows.newitem`.
- `CommandBarManager::InvokeMenuItem` runs when the user picks an item.

Eight flyouts are constructed with each new window. Menus are built once per window, so a hook loaded later sees nothing until a new window opens.

### How each menu is built

| Menu | Built by | When |
| --- | --- | --- |
| New, kind 1 | `CreateNewItemMenu`, then `CreateMenuFlyoutItem` per entry | Window creation |
| Sort and group, kind 2 | `CreateSortAndGroupMenu`, then `CreateRadioMenuFlyoutItem` per entry | Window creation |
| Back and Forward, kinds 9 and 10 | `CreateTravelLogMenu` | Window creation |
| Filters, Gallery filter, Search options, kinds 5, 6 and 4 | their own `Create...Menu` | Window creation |
| View | No builder function. Declared in XAML, each entry bound to its command by id. | Loaded with the bar |

`CreateViewsMenu` exists in the module and is never called. Don't hook it. Three test rounds were spent finding that out.

### Reaching the View menu

Confirmed working. Hook `CommandBarControl_Wave1::PointerPressedHandler` or `CommandBarControlGotFocusHandler`. Both are `void (this, IInspectable const& sender, args const&)` and the sender is the control itself. Walk down from it with `VisualTreeHelper`. The bar has about 250 elements and six `AppBarButton`s with a flyout.

Only one button has a name, `SortAndGroupButton`. The others have an empty name and an empty automation id, so match on something else:

- The View flyout is the only one whose first eight items are `RadioMenuFlyoutItem`s. This holds in any language.
- The button's `Label()` is "Afficher" in French, useful as a second check and not as the only one.

The View flyout's 13 items, in order: eight layouts from extra large icons to content, a separator, the details pane and preview pane, a separator, and a "Show" submenu of toggles. The layouts are radio items in the group `windows.iconsize`, each with an `ImageIcon` whose source is an `SvgImageSource`.

### Adding an entry

Confirmed working: `Items().InsertAt(8, item)` puts an item after the last layout. A `RadioMenuFlyoutItem` with `GroupName` `windows.iconsize` takes the selection mark when clicked and its `Click` handler runs on the window's UI thread. A `FontIcon` displays, though the neighbours use SVG images, so the final icon should be an `ImageIcon` with an `SvgImageSource`.

What the final mod still has to handle:

- **Insertion at window creation.** The probe inserts on a click. Find a hook that runs when the bar loads, such as `OnApplyTemplate`.
- **State.** Explorer doesn't know the entry exists. The mark doesn't survive a new window or an Explorer restart, and Explorer won't clear it when another layout is chosen from elsewhere. The mod must store which folders use its view and set the mark itself.
- **Removal.** On unload, revoke the click handler and remove the item on the window's own thread through the item's `DispatcherQueue`, and drop the last references there. The probe does this and it worked.
- **Rebuilds.** Whether Explorer ever recreates the flyout, for example on a theme or language change, is unconfirmed.

Adding a ninth child under the `windows.iconsize` command would not show, because the XAML lists the entries one by one.

### The other route in

Windows 11 File Explorer Styler reaches the same tree with `InitializeXamlDiagnosticsEx` and `IVisualTreeService`, the mechanism a XAML debugger uses, and matches elements with selectors. Matching by tooltip text is language dependent. The hook-and-walk route above needs no diagnostics connection, so it doesn't compete with Styler for one. Inspect the tree by hand with UWPSpy, choosing the `explorer.exe` process and the WinUI 3 framework.

## The file list

The item view is DirectUI. Window classes seen in the animation mod: `SHELLDLL_DefView` as the view's host, `DirectUIHWND` inside it, and the element class names `UIItemsView` and `ItemsView`.

The animation mod hooks, with fallbacks marked optional:

- `CShellBrowser::BrowseObject` in `ExplorerFrame.dll`, to learn about navigation. It lists two signatures for different builds.
- `UIItem` and `UIItemsView` symbols in the same module, to enumerate visible items. The source comments say older builds and modes still use these classes, so they may be absent.
- Symbols in `dui70.dll` and `duser.dll`, to control painting.

It draws its transition on a DirectComposition surface placed over the list, using D3D11, and hands the real content back when done. So covering or replacing the content area smoothly is possible. It also falls back to plain behavior on builds where a symbol is missing, which is the right model.

Per-item drawing inside DirectUI works: Explorer Extras 0.70 to 0.75 draws label dots on every row from a hook on the exported `Element::PaintContent`, see "Drawing on file list rows" and "Labels, as built" below.

On Ari's laptop the symbol hooks above can't be resolved for now. Microsoft's server had no symbol files for the System32 modules shipped by the update of 2026-11-10, see the symbol section of the core skill. Recheck before assuming it still holds. The route still open is `dui70.dll`'s exported functions, which need no symbol file. Which exports paint a file list item is unconfirmed.

## What Column View teaches

Explorer Column View by ItsMikesch gives Finder-style columns. Ari uses the idea and dislikes the result, so its design is a record of what not to copy.

How it works: each column is a full Explorer folder view created through `IExplorerBrowser`, hosted in its own Win32 child window. It hooks `CreateWindowExW`, subclasses the tab's window, and hooks Explorer's internal get and set view-mode functions so the top View menu applies to the last focused column. It contains no XAML.

Why it doesn't feel native, in Ari's words and what the source explains:

- **No entry in the View dropdown.** The mod never touches the XAML chrome. Columns are a mod setting, not a view mode.
- **A column is not a simple list.** Each one is a whole folder view with its own view modes, headers and behaviors.
- **Dividers and styling are rough.** They are Win32 child windows, outside both the XAML and DirectUI look.

What it gets right and a replacement should keep: real shell behavior for free. Context menus, drag and drop, rename, thumbnails, archives and network folders all work because each column is a real view. A custom-drawn column has to rebuild every one of those.

That is the central trade-off for Miller columns:

| Approach | Looks native | Shell behaviors | Cost |
| --- | --- | --- | --- |
| Host real folder views, as Column View does | No | Free | Medium, already done by that mod |
| Draw light list columns over the content area, shell interfaces for data only | Yes, if drawn well | Each one rebuilt by hand | High |
| Real folder views forced into List mode with hidden headers, plus custom dividers | Partly | Free | Medium, unconfirmed |

Decide which one with Ari before any code. The third row is worth a feasibility test because it keeps the free behaviors.

## The desktop

Confirmed on 2026-11-12.

The desktop icons are a `SysListView32` titled "FolderView", inside `SHELLDLL_DefView`, inside `Progman`, in the explorer.exe process that owns the taskbar. The list view is in icon view with `LVS_OWNERDATA` set. It is a virtual list: Explorer supplies every item on demand.

Three routes to grouping were tested:

| Route | Result |
| --- | --- |
| Native list view groups | Closed. `LVM_INSERTGROUP` and `LVM_ENABLEGROUPVIEW` succeed, but assigning an item to a group fails for every item, because the owner supplies item data. Nothing shows. |
| Explorer's own "Group by", through `IFolderView2::SetGroupBy` | Closed. Returns `E_FAIL` on the desktop. The internal reason is unknown. |
| Positioning icons through the shell | Open. `IFolderView::SelectAndPositionItems` placed 8 of 8 icons exactly. |

So desktop grouping has to be built as layout. A mod arranges icons into clusters and keeps them there. A stack would be several icons at one position, fanned out on demand. Whether Explorer accepts two icons at the same position with align-to-grid on is the next thing to test.

How to reach the desktop's folder view from inside explorer.exe, confirmed working from a worker thread: `CoCreateInstance(CLSID_ShellWindows)`, `FindWindowSW` with `CSIDL_DESKTOP` and `SWC_DESKTOP`, `IServiceProvider::QueryService(SID_STopLevelBrowser)`, `QueryActiveShellView`, then `QueryInterface` for `IFolderView2`.

Two warnings from the tests:

- **Turning group view on moves the icons and they don't come back.** The first probe left all eight in a single column, and `LVM_SETITEMPOSITION32` did not restore them. Only `SelectAndPositionItems` did. Read positions through `IFolderView::GetItemPosition`, save them before any test, and restore through the shell.
- **Never run a desktop test without a saved layout and a restore path.** Ari had to restore his by hand in a second round.

## File labels and tags

macOS-style colored labels need two parts that are independent: where a label is stored, and how it is shown.

Storage options, none tested here:

- An NTFS alternate data stream on the file. Travels with moves on the same volume, lost on FAT, on many cloud folders and in archives.
- The shell property system. Only some file types accept written properties.
- A sidecar database keyed by path or file id. Works for every file, breaks on renames done outside Explorer unless it tracks file ids.

Display options:

- Shell icon overlays. Windows supports a small fixed number of overlay handlers across all programs, and cloud sync clients usually fill them. An overlay handler is also a shell extension, which a Windhawk mod is not well placed to provide.
- Drawing a dot or tint in the file list. Done, from a hook on `dui70.dll`'s `PaintContent`, see "Labels, as built".
- A column or group in the existing views, if property storage works.

Ask Ari which storage trade-off he accepts before testing display. The display test depends on the file list result above.

## Order of work for an Explorer idea

1. Name the surface from the table at the top.
2. Read the reference mod for that surface.
3. Write the smallest feasibility test, a diagnostic mod that proves the one unconfirmed thing the idea depends on. Log results and nothing else.
4. Record the result in PLAN.md as a confirmed fact or a rejected approach.
5. Only then design settings, with `windhawk-ui-kit`.

Feasibility tests, with their state on 2026-11-12:

| Idea | The one thing to prove first | State |
| --- | --- | --- |
| Native "Columns" entry in the View menu | A mod can add a clickable item to the View flyout | Proved |
| Miller columns | A real folder view can be stripped down to a plain list, or a custom list can host shell drag and drop and context menus | Not started |
| File labels | A mark can be drawn on one item in the DirectUI file list | Built in explorer-extras 0.75 (dots in every view and in Columns, Tags in the pane and in the right-click menu) |
| Desktop grouping | Icons can be clustered and stacked by position | Positioning proved, stacking untested |

## Reaching a window as it opens, confirmed

Confirmed on 2026-11-13 with `probes/probe-window-created.wh.cpp` 0.6, ARM64, build 26100.9974, French. Findings for `FileExplorerExtensions.dll`:

- `CommandBarControl_Wave1::OnApplyTemplate` runs when a window is built, on the window's UI thread. `CommandBarControl_Wave1::OnDpiChanged` runs right after it, in the same call. At that moment the `CabinetWClass` window has its `ShellTabWindowClass` child and its `Microsoft.UI.Content.DesktopChildSiteBridge`. The window can still be hidden. `OnDpiChanged` runs again at window close, with no tab left, and on later resizes.
- `OnDpiChanged` returns `winrt::fire_and_forget` and takes `XamlRoot` and `XamlRootChangedEventArgs` by value. The hook is `void* Hook(void* this, void* ret, void* root, void* args)`, and `root` points at the caller's temporary holding the `XamlRoot`. The method is a coroutine and moves the `XamlRoot` out of that slot, so read it before calling the original. After the call the slot holds 0.
- Take a reference to the `XamlRoot` before the original and keep using it afterwards. `XamlRoot.Content()` is the island's root grid. Walking down from it finds the View flyout, 395 elements and 30 buttons, by the first `DispatcherQueueTimer` tick after the hook, about 60 ms. Inside the hook itself the tree was not checked.
- Never treat `this` of a command bar method as a XAML object. Calling `copy_from_abi` and `try_as` on it crashed Explorer twice, once with `0xC0000409` in ucrtbase and once with `0xC0000005` in `Microsoft.UI.Xaml.dll`, and left Explorer on the legacy command bar until it restarted. Take objects only from arguments that are XAML references.
- `OnCommandBarItemVMsChanged` is no use for this. It takes an observable map and the change arguments, and `this` is the control.
- A generic hook that forwards four integer registers is safe for methods without floats and without by-value structs. A by-value C++/WinRT object arrives as a pointer to a temporary.

## What explorer-extras confirmed

Confirmed 2026-11-13, ARM64, Windows 11 build 26100.9974, French, with `explorer-extras` up to 0.13.6. The mod's `HANDOFF.md` and `PLAN.md` hold the detail.

- **Panel.** A `DesktopWindowXamlSource` on the `CabinetWClass` window, over the `SHELLDLL_DefView`, with Explorer's own view left alive underneath. Explorer's view follows the active column through `IShellBrowser::BrowseObject` with `SBSP_ACTIVATE_NOFOCUS`, so the toolbar, panes and status bar act on the selection. Selection is mirrored with `IShellView::SelectItem`.
- **Window creation.** `OnDpiChanged` in `FileExplorerExtensions.dll` runs when a window is built, see "Reaching a window as it opens". A new tab is seen by hooking `CreateWindowExW` for class `ShellTabWindowClass`.
- **Navigation pane clicks.** `IShellBrowser::BrowseObject` is vtable slot 11. A call with flags `0x80001` from `SHELL32` and `DUI70` is a sidebar click. The mod flags its own calls with a thread-local so it does not mistake them for one.
- **Toolbar commands.** Hooking `CommandBarManager::InvokeMenuItem` lets the mod run Cut, Copy, Paste, Delete and Rename on the selected column item.
- **Keys.** Explorer's accelerator table takes Backspace, Delete, Ctrl+C, Ctrl+V and Ctrl+Shift+N before the panel does. A `TranslateAcceleratorW` hook passes them through while the panel or its rename box has the keyboard.
- **Folder contents.** Read with `IShellFolder::EnumObjects` on a worker thread, sorted with `CompareIDs` on Explorer's own sort columns and grouped from the group property. Icons come in a first batch of 120, then per row as it is built.
- **Shortcuts to folders.** A `.lnk` has no `SFGAO_FOLDER`. Resolve it with `IShellItem::BindToHandler(BHID_LinkTargetItem)` and open the target. Treated as a file, it made Explorer open a new window.
- **Archives.** A zip is a shell folder (`SFGAO_FOLDER` plus `SFGAO_STREAM`) and browses as a column. Extraction through `IFileOperation::CopyItem` works. Properties on an item inside it crashed Explorer with `0xC0000409`.
- **Virtual items.** The Desktop folder mixes real files with virtual items (This PC, Recycle Bin, Control Panel). `SFGAO_FILESYSTEM` tells them apart. Control Panel is listed twice, as a folder and as an applet launcher.
- **Cloud and remote items.** A OneDrive placeholder has `FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS`. A synced file has a reparse tag that equals `0x9000001A` under the mask `0xF000FFFF`. `FILE_ATTRIBUTE_PINNED` (`0x80000`) means always keep on this device. UNC paths and `DRIVE_REMOTE` identify network items. A phone connected through Phone Link shows up under `C:\Users\<name>\CrossDevice`. Detecting MTP devices by their `\\?\usb#` parsing names is unconfirmed.
- **Tabs.** The visible `ShellTabWindowClass` identifies a tab; a switch is seen from a tick. Keep a snapshot per tab (folders, selection, widths, scroll). One panel per window, hidden while a tab in another layout shows. A window made after the panel sits above it and covers it, so raise the panel while `GetWindow(panel, GW_HWNDPREV)` is not null.
- **Menus.** A `CommandBarFlyout` cannot draw two-tone icons, so the item and folder menus are a custom `Flyout`: a row of icon buttons with labels, a list of rows, and "Show more options" holding the rest of the shell's entries. Windows' own two-tone icons are SVG files in `C:\Windows\SystemApps\MicrosoftWindows.Client.FileExp_cw5n1h2txyewy\FileExplorerExtensions\Assets\images\contrast-standard\theme-dark` (or `theme-light`), named like `windows.cut.svg`, and load from Explorer with `SvgImageSource(Uri("file:///C:/..."))`.
- **Per-folder memory.** One Wh string value, lines of path, sort, width and Columns flag, 150 folders. Explorer's own per-folder sort is the authority for a column once it is active.
- **Drag and drop between columns.** Follow the pointer in XAML and hand the items (`GetUIObjectOf(IID_IDataObject)`) to the target folder's `IDropTarget`, so the shell does the move, copy or link and shows its dialogs. A move done by the target returns effect 0. A row marks its press handled: start the drag from a handler registered with `handledEventsToo`.
- **Drag out.** When the pointer leaves the panel, end the XAML drag and start `SHDoDragDrop` from the dispatcher queue with an own `IDropSource`. A XAML island hands the mouse over as pointer messages, so the shell's drag loop sees no `WM_MOUSEMOVE` and tells a target only about the release point: the desktop copes, the navigation pane tree (`SysTreeView32`, which needs a hovered item) did not. Post the moves yourself from a helper thread (`PostThreadMessageW(threadId, WM_MOUSEMOVE, keys, MAKELPARAM(x, y))` every 15 ms while the cursor moves) and every target answers: the navigation pane, any view of another Explorer window (`DirectUIHWND`), the desktop, other applications. A key let go just before the button must still count: keep when each was last seen and set `Preferred DropEffect` in the data at the release. Never call a target's `DragEnter` or `DragLeave` by hand during a drag: it crashed Explorer in `explorerframe.dll` twice.
- **Drops coming in.** `RegisterDragDrop` an own `IDropTarget` on the island's bridge window (revoke the island's first), and give the data object to the folder under the pointer through `IShellFolder::CreateViewObject(IID_IDropTarget)`: the folder decides the effect and does the work. For a drag from elsewhere call `IDropTargetHelper` (`CLSID_DragDropHelper`) so its image shows. Revoke in the teardown.
- **Touch.** In a XAML island a finger is lost by the panel at its first real move after a hold (capture lost, the pointer id changes), and Windows gives the mod no other source: `GetPointerInfo` fails with XAML's id, `WM_POINTER` reaches no window of the frame, the cursor stays put. Nothing that follows the finger by events works. `UIElement.StartDragAsync` called in the event of the first move does: the platform follows the finger from there. Give it `SetStorageItems` (files fetched during the hold) and a `Shell IDList Array` provider (a `Buffer` into an `InMemoryRandomAccessStream`, the CIDA layout: count, offsets, folder pidl, item pidls); the sidebar and the desktop take it. Drops inside the panel are XAML `Drop` events on an element with `AllowDrop`, not the panel's `IDropTarget`. A hold and lift gives `Holding` then `RightTapped`: keep the drag start for the move, not the hold. A finger on empty space can draw a rectangle if the scrollers above are set to `ScrollMode::Disabled` at the press and restored at the end.
- **Custom context menus.** A `Flyout` whose content is built from buttons with a `ControlTemplate` loaded from XAML (a `Border` named `Base` inset in the row, `VisualState` setters for `PointerOver`, `Pressed`, `Disabled` and `Focused` using `SubtleFillColorSecondaryBrush` and `SubtleFillColorTertiaryBrush`) matches a Figma menu to the pixel; a `FlyoutPresenterStyle` with `Padding` 0 and `CornerRadius` 7 removes the presenter's own padding. The shell's entries come from `IContextMenu::QueryContextMenu`; a submenu such as Open with is empty until `IContextMenu2::HandleMenuMsg(WM_INITMENUPOPUP, submenu, MAKELPARAM(position, 0))` is called on it. Verbs seen: `open`, `openas`, `copyaspath`, `setdesktopwallpaper`, `print`, `Edit`, `Windows.PowerShell.Run`, `pintohomefile`, `Windows.ModernShare`, `PreviousVersions`. Compress to, the app extensions and the newer entries are not in that data. Hover submenus: a `MenuFlyout` shown with `FlyoutShowMode::Transient` and `RightEdgeAlignedTop` has no light-dismiss layer, so the rows under it keep seeing the pointer; a normal flyout blocks them. `FlyoutShowOptions.ExclusionRect` with `BottomEdgeAlignedLeft` keeps the menu off the clicked row, and the quick row goes under the list when the menu opens upwards (test the root's top against the pointer in `Opened`). The icon of the app that opens a file: `AssocQueryString(ASSOCSTR_EXECUTABLE, ext, "open")`, then `IShellItemImageFactory::GetImage` at 32 px. Windows' own `tar.exe` (bsdtar) writes zip (`--format zip`), 7z (`--format 7zip`) and tar.
- **Surgical column updates.** Rows of a `ListView` can be patched with `RemoveAt` and `InsertAt` (descending for removals, ascending for insertions), keeping the rest with its selection and scroll: match the new listing to the old by the item id bytes, keep the longest run in the same order, and remake only the rest. Keep the per-row arrays in step.
- **The View button on a narrow window.** The button goes into the command bar's "..." overflow, a popup outside the tree a walk can reach. It is still in `CommandBar.PrimaryCommands()`: check each `Button` there for the eight radio items.
- **The View button.** It is an `AppBarButton` whose `Icon` is an `ImageIcon` in a `Viewbox`; swap the `Icon` property. A replacement needs the same square box, because the Viewbox scales to fill. A `PathIcon` from `XamlReader::Load` with `Foreground='{ThemeResource TextFillColorSecondaryBrush}'` follows the theme.
- **Rows.** A selected `ListViewItem` draws its background in the first `Border` inside its `ListViewItemPresenter` (inset 4 and 2.3 px): change that border's `Margin` and `CornerRadius` to join neighbours. A resting pointer keeps a row's hover while the keyboard moves: make rows non-hit-testable until the cursor moves.
- **Previewers.** QuickLook asks Explorer's own view for the selection, so a mirrored selection must be immediate when Explorer already shows the folder.
- **Undo.** The mod keeps its own stack: Recycle Bin restore through the "undelete" verb, and renames and new folders by path.
- **Not solved.** A transparent or Mica background behind the panel. Several approaches failed. Parked.

## Compatibility to check

Ari runs these Explorer mods, so test with all of them enabled: Windows 11 File Explorer Styler, File Explorer WinUI-Like Content Animation and Win32 UI Modernizer. The animation mod already carries a compatibility option for Styler, which shows these mods do collide. Also test with several tabs, with a second Explorer window, with the separate-process option on, and in dark and light theme.

## What `dui70.dll` exports

Listed on 2026-11-12 with `probes/probe-dui70-exports.wh.cpp`, ARM64, build 26300.9974. Exports need no symbol file, so these hooks survive the symbol outages that follow Windows updates.

`dui70.dll` exports 4321 named functions. The ones that matter for drawing on file list items:

- `DirectUI::Element::Paint(HDC, RECT const*, RECT const*, RECT*, RECT*)`, virtual. Subclasses override it, and an override's own code is not exported.
- `DirectUI::Element::PaintBackground(HDC, Value*, RECT const&, RECT const&, RECT const&, RECT const&)`, `PaintBorder` and `PaintContent(HDC, RECT const*)`. These are not virtual, so every caller reaches the exported function.
- Tree access: `Element::GetParent()`, `GetChildren(Value**)`, `FindDescendent(unsigned short)`, `GetID()`, `GetAccName(Value**)`, `GetRootRelativeBounds`, `GetLocation`, `GetExtent`, `GetVisible`.
- Building: `Element::Create(unsigned int, Element*, unsigned long*, Element**)`, `Element::Add(Element*)`, and the plain C export `StrToID`.
- 78 classes export `GetClassInfoW`.

`ExplorerFrame.dll` exports 3 named functions and none helps. The file list's item classes live there, so their own methods stay out of reach without symbols. `duser.dll` exports 150 functions, 68 of them gadget functions.

Untested: whether a file list item's paint goes through the exported `PaintBackground` or `PaintContent`. A probe that hooks both and logs the class of each element painted would answer it.

## Column view groundwork, confirmed

Confirmed on 2026-11-12 with `probes/probe-bench.wh.cpp` v0.1, ARM64, build 26300.9974. None of it needs a System32 symbol file.

### A WinUI 3 panel over the file list

- Run on the window's UI thread. `CommandBarControl_Wave1::PointerPressedHandler` in `FileExplorerExtensions.dll` gets you there. It fires on a click on a command bar button. A click on an empty spot of the bar did not fire it.
- Create `Microsoft::UI::Xaml::Hosting::DesktopWindowXamlSource`, call `Initialize(WindowId{(uint64_t)topLevelHwnd})` with the `CabinetWClass` window, set `Content`, then use `SiteBridge()` with `MoveAndResize` and `Show`. The bridge's window is `(HWND)bridge.WindowId().Value`, class `Microsoft.UI.Content.DesktopChildSiteBridge`, a child of the top-level window. Raise it with `SetWindowPos(HWND_TOP)`.
- Position it on the visible `SHELLDLL_DefView`'s rectangle, mapped to the top-level window's client coordinates. Values are physical pixels.
- WinUI controls created this way get Explorer's own styles. A `ListView` shows the native selection look. Clicks and selection work.
- Explorer's own flyouts, such as the View menu, draw above the panel.
- A 250 ms `DispatcherQueueTimer` that re-reads the rectangle keeps the panel in step with maximise and restore.
- Navigation replaces the `SHELLDLL_DefView` window. The panel survives because its parent is the top-level window. Look the file list up again on each tick.
- The original file list stays alive under the panel and keeps painting. A real mod should hide it or accept the cost.
- Close in this order on the UI thread: stop the timer and revoke its handler, revoke control handlers, `Content(nullptr)`, `Close()`. From another thread, queue that through the thread's `DispatcherQueue` and wait.
- Headers needed: `winrt/Microsoft.UI.Content.h`, `Microsoft.UI.Xaml.Hosting.h`, `Microsoft.UI.Xaml.Controls.Primitives.h` for `Click` and `SelectionChanged`, `Microsoft.UI.h`, `Windows.Graphics.h`, `Windows.UI.h`.

### Folder data from the shell

- The top-level window, the visible `ShellTabWindowClass` and the file list were all on the same thread as the command bar click.
- `SendMessageTimeoutW(tabHwnd, WM_USER + 7, ...)` on the visible `ShellTabWindowClass` returns the tab's `IShellBrowser`, not AddRef'd.
- `QueryActiveShellView`, then `IFolderView`. `GetFolder(IPersistFolder2)` with `GetCurFolder` gives the current folder. `GetFolder(IShellFolder)` with `ItemCount`, `Item` and `GetDisplayNameOf` gives the items.

### DirectUI painting without symbols

- Hooks on the exported `Element::PaintBackground` and `Element::PaintContent` both fire for file list items. `UIItem` rows came through with a 954x34 rectangle in details view, and `UIProperty`, `UIImage` and `UIDetailsPropertyCollection` came through too.
- Class names are available without symbols. Find the vtable slot of `Element::GetClassInfoW` by searching the exported vtable `??_7Element@DirectUI@@6B@` for the exported function's address, and do the same for `ClassInfoBase::GetName` in `??_7ClassInfoBase@DirectUI@@6B@`. On this build the slots were 35 and 8. Never hard-code them.
- Mapping a `UIItem` to its file: confirmed, `Element::GetAccName(Value** out)` returns the displayed name as a `const wchar_t*` (not an HRESULT; release the `Value` it hands back). Two files that show the same name with "hide extensions" on are ambiguous, so match by row position for those.

### Drawing on file list rows (probe-label-paint 0.1 to 0.6, 2026-11-17, ARM64 build 26300, French)

Confirmed first-hand. A dot drawn on the DC after the original `PaintContent` returns shows on the real Explorer list, survives hover, click and scroll, and costs about 10 to 30 microseconds per row. Every view mode paints rows as `UIItem`, so one hook covers them all. Row sizes at this scale: Details 594x19, List 105 to 169x22, Tiles 242x48, Content 911x49, Icons 66x64 to 97x127. Grouping adds `UIGroupHeader` and `UIGroupItem`.

Children of a `UIItem` (position relative to the item):
- Details: `UIImage` at x 4, then a `UIDetailsPropertyCollection` at x 22 whose first cell is the Name column (230 wide, not the text width).
- List: a 28 px `UIBase` gap at x 4 (the item check box column), `UIImage` at x 32, `UIProperty` "Nom" at x 48. The cell is exactly as wide as the name text, so a dot after the text lies outside the item.
- Tiles: an icon `Element`, then an `Element` holding a `UIProperty` "Nom" (first line, text width) and a `UITilesPropertyCollection` (second line, 16 high).
- Content: `UIImage`, then a `UISmartPropertyCollection` with several lines.
- Icons: a thumbnail `Element` (4,2) and a label `Element` below it, 16 high per line, centred text.

Dimming a row (probe 0.7 to 0.11): children paint after the `UIItem`, so dim from the children's own `PaintContent` hooks (`UIImage`, `UIProperty`, `Element`, the three property collections, `UIBase`), finding the row by climbing `Element::GetParent` up to 4 levels. Fading toward a fixed page colour leaves dark blocks on hovered and selected rows. Instead, call `GetPixel` on the DC just inside the child's rect before the original paints, and alpha-blend (`AlphaBlend`, a 1x1 DIB, about 60 percent) that colour over the rect afterwards. This is clean on hover and selection. A List-view dot centred on `rect.top + height / 2`, with left margin `(height - side) / 2`, lines up with the icon.

Pitfalls: the DC's font after the original paint is not the item's font, so `GetTextExtentPoint32W` there overstates widths by about 1.4x. Child lists are `DynamicArray<Element*>`, read by searching the first three words for a data pointer and a count. A file written from the Bash tool to `%LOCALAPPDATA%` was not seen by Explorer, so do not use a control file written that way; log automatically instead.

### Probe practice

- Put related tests in one mod so one compile covers them.
- Log to a file in the repo as well as through `Wh_Log`.
- Read a control file once a second for switches, so a test can be turned off without Windhawk's settings.

## What the details pane and the status bar probe confirmed (explorer-extras 0.37 to 0.54, 2026-11-15 and 16)

Windows 11 24H2 build 26100, ARM64, French. First-hand facts, from probes and from running the mod.

**The window tree.** `TITLE_BAR_SCAFFOLDING_WINDOW_CLASS` and a toolbar `DesktopChildSiteBridge` (about 234 px tall) sit at the top. `ShellTabWindowClass` holds `DUIViewWndClassName`, which holds one `DirectUIHWND`. Inside it: a `CtrlNotifySink` with `NamespaceTreeControl` and `SysTreeView32` (the navigation pane), a `CtrlNotifySink` with `SHELLDLL_DefView` and `FileExplorer_FolderViewIsland` (a WinUI island), and `DetailsPaneHwndHostClass` (a window that hosts its own island) in another `CtrlNotifySink`. `Shell Preview Extension Host` is 0x0 and hidden while the Preview pane is off. A window list needs a minimum size of zero, or small windows (a 25 px bar) are missed.

**Details and Preview panes.** They are exclusive and share one slot. Switching the Details pane off hides its `CtrlNotifySink` and the file view grows by the pane's width; nothing is destroyed. Alt+Shift+P toggles Details, Alt+P toggles Preview, and both work as `SendInput`. The Preview pane has no window of its own when the selected file has nothing to preview, so it cannot be detected by a window: the gap between the file view and the right edge of `ShellTabWindowClass` shows it. Catching Alt+P with a low-level keyboard hook (installed per window thread, checked against the foreground window) and sending Alt+Shift+P is the reliable way to make a mod's own pane answer to it.

**Taking over the Details pane.** Leave Explorer's pane on and place a second `DesktopWindowXamlSource` over its rectangle (`HWND_TOP`, followed by a timer). Explorer keeps the space, the splitter and every toggle. The Columns panel and this island both ask for the top of the z-order: if each re-raises itself on a timer they flicker; raise once.

**The View menu.** Eight `RadioMenuFlyoutItem` layouts, a separator, a Details radio, a Preview radio, a separator, and a `MenuFlyoutSubItem` (the Show submenu) whose first child is the navigation pane `ToggleMenuFlyoutItem`, then a separator, compact view, a separator, item check boxes, file name extensions, hidden items. Items can be hidden with `Visibility`, and their `Icon` objects (an `ImageIcon` of an SVG file) can be moved to an item of ours and handed back. Invoking another item through its automation peer from inside the menu's own click crashed Explorer: defer it to a later tick.

**The toolbar.** The Details button is the `AppBarButton` named `PaneToggle` among the primary commands of `FileExplorerSecondaryCommandBar`. It can be hidden and replaced by an `AppBarToggleButton` inserted at its index. Its icon is an `ImageIcon` with an `SvgImageSource` pointing at a file under `C:\Windows\SystemApps\MicrosoftWindows.Client.FileExp_...\FileExplorerExtensions\Assets\images\...`; the file can be read, recoloured and loaded from the temp folder (this SDK's `ImageIcon` has no `ShowAsMonochrome`).

**The status bar is not a window.** UI Automation finds one element, "Barre d'état", class `StatusBarModuleInner`, framework DirectUI, no window handle, at the bottom of the `DirectUIHWND`. The `msctls_statusbar32` window that exists is a hidden leftover. Windows has its own option (Folder Options, View, Show status bar; the value `ShowStatusBar`), so no mod feature is needed.

**XAML in an island.** Only `UIElement.TranslationTransition` and `OpacityTransition` animate visibly (set the start state with the transitions off, attach them after the first layout, set the end state). Storyboards on transforms and compositor animations changed values but showed nothing. A `BitmapImage` that is not on screen never opens: put it in the tree, invisible, first. A native child window (a shell preview handler) draws over the island and receives no wheel from another process's renderer; drawing PDF, text, Markdown, images and media in XAML avoids it. `MediaPlayerElement` must let go of its `MediaPlayer` (`SetMediaPlayer(nullptr)`) before the player is released, or Explorer crashes.

**Touch on the columns.** A finger on a column's empty space locks every ancestor scroller to draw a rectangle. A second finger counts as a press again: lock each scroller once, restore in reverse, and give the rectangle up when a second finger arrives, or scrolling stays off.

## Confirmed in Explorer Extras 0.55 to 0.63 (Windows 11 24H2, ARM64, Windhawk 2)

- **A command bar announces itself.** `FileExplorerExtensions.dll`: `CommandBarManager::CommandBar(CommandBar const&)` is called when Explorer builds a command bar (the same hook Explorer Command Bar by DanRotaru uses). Hook it with `HookSymbols`, call the original, then queue the work: the bar is in the tree one turn later. The View entry then needs no 50 ms polling and no click. A mod reload does not rebuild bars: test with a **new** window.
- **Alt+P and other accelerators** reach `TranslateAcceleratorW` on the window's own thread, with the message in `msg`. Returning non-zero swallows the key. No low-level hook is needed.
- **Explorer's own Details toggle** can be pressed without a key: its toolbar button (`PaneToggle`) or the View menu's Details radio, through `FrameworkElementAutomationPeer::CreatePeerForElement` and `IToggleProvider` or `IInvokeProvider`. The element has to be visible for the call: set opacity 0, no hit testing, zero width, margin and minimum width, then restore it, or the toolbar jumps. The call goes through `CommandBarManager::InvokeMenuItem`, so a mod that reacts to toolbar commands must know its own call.
- **Preview handlers** are created with `CLSCTX_LOCAL_SERVER` only (they run in prevhost.exe). `IPreviewHandler::SetFocus` takes the focus. Do not attach input to another process.
- **A new tab:** press the tab strip's add button, found in the window's XAML tree as the element named `AddButton` (UIA: "Ajouter un nouvel onglet", AutomationId `AddButton`), through its automation peer (`IInvokeProvider`). No key is sent. The folder's `opennewtab` verb does NOT work: through `IContextMenu::InvokeCommand` it opens a new window, even when the menu is given the window's browser as its site (tried in 0.64.2). To look for such a control, list the buttons of a live window with .NET UI Automation in PowerShell (`System.Windows.Automation`), then find the same name in the XAML tree.
- **OneDrive states** come from the reparse tag: `(tag & 0xFFFF0FFF) == IO_REPARSE_TAG_CLOUD` covers the sixteen sub-states.
- **Hidden extensions.** With Windows' default "hide extensions for known file types", the display name of `backup.zip` is `backup`. Take an extension from the parsing name (`SHGDN_FORPARSING`, `PathFindExtensionW`), never from the display name. A rename keeps the extension that is shown, and sets the name without it when it is hidden (the shell puts it back); a name is never cut at its last dot (`v1.2 notes`).
- **A selectable group header.** In a grouped ListView a group header is a selectable row that is no file. Comparing "which entries are selected" skips it, so Invert selection and Select none left a header selected; compare the selected row count too.
- **A preview scrolls without a hook.** With `IPreviewHandler::SetFocus` on hover, the wheel and the keys reach a handler's window by themselves: a low-level mouse hook to forward the wheel was never needed.
- **The `.ts` extension** is a video or TypeScript: an MPEG transport stream has byte 0x47 at offsets 0 and 188.

## Labels, as built (explorer-extras 0.70 to 0.75, 2026-11-17)

Design and plan: `docs/superpowers/specs/2026-11-17-labels-design.md` and `docs/superpowers/plans/2026-11-17-labels.md`. Ari's labels are a name (optional) plus a palette colour, many per item, kept in a named NTFS stream `ExplorerExtras.Labels` on the item.

- **Storage.** `03a-labels-store.cpp`: a text of `v1`, then a colour digit, a tab and the name per line. Writing the stream resets the file's and folder's last-write time unless the writing handle is told not to: `SetFileTime(handle, nullptr, nullptr, &{0xFFFFFFFF, 0xFFFFFFFF})` before the write. Copies and moves keep the stream. `FILE_NAMED_STREAMS` from `GetVolumeInformationW` says whether a drive can hold it.
- **Cache.** `04b-labels-cache.cpp`: a mod thread reads a folder's streams, keyed by the displayed name (the text `Element::GetAccName` gives for a row). Two items that show one name (hidden extensions) are "ambiguous" and get no dot. The paint hook only reads the cache.
- **Drawing.** One hook, `Element::PaintContent` (exported by `dui70.dll`, no symbol file). Children paint after their parent, and a text is drawn by the element that owns it: the dot is placed from the geometry of the row and drawn from the hook of the element that paints the name. The kinds seen: `UIItem` (a row), `UIProperty`, `UISmartProperty`, `UIImage`, `UIBase`, `UICheckbox`, `Element`, `UIDetailsPropertyCollection`, `UITilesPropertyCollection`, `UISmartPropertyCollection`. The view is told from the row's children: a details collection is Details, a smart collection is Content, a property with an image is List, two elements with a check box are Tiles, two elements without are Icons. In Tiles and Content, a folder has an empty second line (the cache keeps which items are folders), and a Type line paints its text in children, so its width is measured from the shell's type text.
- **Dimming** a row: fade each child toward the colour sampled with `GetPixel` before it paints, then `AlphaBlend` about 60 percent. Clean on hover and selection. The filter that would switch it on has no UI (Ari did not see the point), the code is dormant.
- **Right-click menu.** `MenuFlyoutControl::AppendMenuItem(CommandBarFlyout&, MenuItem const&)` in `FileExplorerExtensions.dll`, hooked with the other hooks of that module. The first call for a flyout puts a handler on its `Opening` event (the flyout is reused, and its commands are appended again each time). At `Opening` an `AppBarButton` with a `Flyout` (a `MenuFlyout`) is inserted in `SecondaryCommands()`, before the command with the Shift+F10 accelerator ("Show more options"). The hook does not receive the selected items: they are read from the window's `IShellBrowser`.
- **A thread can have no `WindowState`.** A window that was open before the mod loaded has none, and `StateForThisThread(false)` is null: find the `CabinetWClass` window of the thread with `EnumThreadWindows` instead.
- **Theme.** The apps' theme is `AppsUseLightTheme` in `HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize`. A dot in the light theme is the palette colour softened 45 percent toward white (about a badge's colour); the rim is darker in the dark theme and lighter in the light one.
- **Not done:** drag onto a sidebar tag, an index and a "Tagged" view across folders, a list of tag presets (a Windhawk settings repeater), the exact placement in Content view.
