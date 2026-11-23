// Sidebar: the user's list of tags. A tag here is a name and a palette colour, with whether the sidebar shows it
// and when it was last added to a file. The list is kept as one Windhawk value; the labels themselves stay in the
// NTFS streams of the files (03a-labels-store.cpp). A tag is told apart by its name, in any case.
//
// The operations that change a list take the list, so the self-test can run them on a list of its own. The
// functions without "In", "To" or "From" in their name act on the saved list and write it.
//
// The text of the value: a first line "v1", then one line per tag: a colour digit, a tab, 0 or 1 for hidden, a
// tab, the last-use time (a decimal number), a tab, and the name written with the escapes of the label streams.

struct TagDef {
    std::wstring name;
    uint8_t colour = 0;
    bool hidden = false;
    ULONGLONG lastUsed = 0;  // 0: never added through the mod. Otherwise a FILETIME as a number.
};

constexpr size_t kTagListMax = 500;

namespace {

std::mutex g_tagListMutex;
std::vector<TagDef> g_tagList;
bool g_tagListLoaded = false;

std::wstring TagTrim(std::wstring const& name) {
    size_t first = name.find_first_not_of(L' ');
    if (first == std::wstring::npos) {
        return L"";
    }
    return name.substr(first, name.find_last_not_of(L' ') - first + 1);
}

bool TagNamesEqual(std::wstring const& a, std::wstring const& b) {
    return CompareStringOrdinal(a.c_str(), (int)a.size(), b.c_str(), (int)b.size(), TRUE) == CSTR_EQUAL;
}

size_t TagFindIn(std::vector<TagDef> const& list, std::wstring const& name) {
    for (size_t i = 0; i < list.size(); i++) {
        if (TagNamesEqual(list[i].name, name)) {
            return i;
        }
    }
    return std::wstring::npos;
}

uint8_t TagColour(int colour) {
    return (uint8_t)std::clamp(colour, 0, 7);
}

}  // namespace

// A name the user can give: not blank, at most kLabelsMaxName characters, no control character (a tab or a line
// break would not survive the list).
bool TagNameValid(std::wstring const& name) {
    std::wstring trimmed = TagTrim(name);
    if (trimmed.empty() || trimmed.size() > kLabelsMaxName) {
        return false;
    }
    for (wchar_t c : trimmed) {
        if (c < 0x20) {
            return false;
        }
    }
    return true;
}

bool TagListAddTo(std::vector<TagDef>& list, std::wstring const& name, int colour) {
    if (!TagNameValid(name) || list.size() >= kTagListMax) {
        return false;
    }
    std::wstring trimmed = TagTrim(name);
    if (TagFindIn(list, trimmed) != std::wstring::npos) {
        return false;
    }
    TagDef tag;
    tag.name = trimmed;
    tag.colour = TagColour(colour);
    list.push_back(tag);
    return true;
}

// A name found on a file. It is added as it is (it may hold what the user could not type), cut to the limit.
bool TagListEnsureIn(std::vector<TagDef>& list, std::wstring const& name, int colour) {
    if (name.empty() || list.size() >= kTagListMax || TagFindIn(list, name) != std::wstring::npos) {
        return false;
    }
    TagDef tag;
    tag.name = LabelsClip(name);
    tag.colour = TagColour(colour);
    list.push_back(tag);
    return true;
}

// A rename keeps the place, the colour and the rest. A name another tag has is refused; the same tag in another
// case is a rename.
bool TagListRenameIn(std::vector<TagDef>& list, std::wstring const& oldName, std::wstring const& newName) {
    size_t index = TagFindIn(list, oldName);
    if (index == std::wstring::npos || !TagNameValid(newName)) {
        return false;
    }
    std::wstring trimmed = TagTrim(newName);
    size_t other = TagFindIn(list, trimmed);
    if (other != std::wstring::npos && other != index) {
        return false;
    }
    list[index].name = trimmed;
    return true;
}

void TagListRecolorIn(std::vector<TagDef>& list, std::wstring const& name, int colour) {
    size_t index = TagFindIn(list, name);
    if (index != std::wstring::npos) {
        list[index].colour = TagColour(colour);
    }
}

void TagListSetHiddenIn(std::vector<TagDef>& list, std::wstring const& name, bool hidden) {
    size_t index = TagFindIn(list, name);
    if (index != std::wstring::npos) {
        list[index].hidden = hidden;
    }
}

bool TagListRemoveFrom(std::vector<TagDef>& list, std::wstring const& name) {
    size_t index = TagFindIn(list, name);
    if (index == std::wstring::npos) {
        return false;
    }
    list.erase(list.begin() + (ptrdiff_t)index);
    return true;
}

void TagListTouchIn(std::vector<TagDef>& list, std::wstring const& name, ULONGLONG now) {
    size_t index = TagFindIn(list, name);
    if (index != std::wstring::npos) {
        list[index].lastUsed = now;
    }
}

// The tags the sidebar shows: not hidden, the most recently used first; those never used come after, by name.
std::vector<TagDef> TagListRecentOf(std::vector<TagDef> const& list, int count) {
    std::vector<TagDef> shown;
    for (auto const& tag : list) {
        if (!tag.hidden) {
            shown.push_back(tag);
        }
    }
    std::stable_sort(shown.begin(), shown.end(), [](TagDef const& a, TagDef const& b) {
        if (a.lastUsed != b.lastUsed) {
            return a.lastUsed > b.lastUsed;
        }
        return CompareStringOrdinal(a.name.c_str(), (int)a.name.size(), b.name.c_str(), (int)b.name.size(), TRUE) ==
               CSTR_LESS_THAN;
    });
    if (count < 0) {
        count = 0;
    }
    if (shown.size() > (size_t)count) {
        shown.resize((size_t)count);
    }
    return shown;
}

// Takes in the tags of the old recent-tags store, given newest first: those the list lacks are added, oldest first, with
// last-use times just under `base` so their order stays; a tag the list has keeps its colour and a later last-use time.
void TagListImportInto(std::vector<TagDef>& list, std::vector<LabelTag> const& newestFirst, ULONGLONG base) {
    for (size_t i = newestFirst.size(); i-- > 0;) {
        LabelTag const& tag = newestFirst[i];
        TagListEnsureIn(list, tag.name, tag.colour);
        size_t index = TagFindIn(list, tag.name);
        ULONGLONG stamp = base > i ? base - i : 1;
        if (index != std::wstring::npos && list[index].lastUsed < stamp) {
            list[index].lastUsed = stamp;
        }
    }
}

// What the "Add a tag" flyout and the label menus take: name and colour.
std::vector<LabelTag> TagListAsLabels(std::vector<TagDef> const& list) {
    std::vector<LabelTag> tags;
    for (auto const& tag : list) {
        tags.push_back(LabelTag{tag.colour, tag.name});
    }
    return tags;
}

std::wstring TagListSerialize(std::vector<TagDef> const& list) {
    std::wstring text = L"v1\n";
    for (auto const& tag : list) {
        text += static_cast<wchar_t>(L'0' + std::min<int>(tag.colour, 7));
        text += L'\t';
        text += tag.hidden ? L'1' : L'0';
        text += L'\t';
        text += std::to_wstring(tag.lastUsed);
        text += L'\t';
        text += LabelsEscape(tag.name);
        text += L'\n';
    }
    return text;
}

// False for text that is not ours (no version line, a newer version, a field that is not what it should be).
bool TagListParse(std::wstring const& text, std::vector<TagDef>* out) {
    out->clear();
    size_t lineEnd = text.find(L'\n');
    if (text.substr(0, lineEnd) != L"v1") {
        return false;
    }
    size_t start = lineEnd == std::wstring::npos ? text.size() : lineEnd + 1;
    while (start < text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) {
            end = text.size();
        }
        std::wstring line = text.substr(start, end - start);
        start = end + 1;
        if (line.empty()) {
            continue;
        }
        size_t a = line.find(L'\t');
        size_t b = a == std::wstring::npos ? a : line.find(L'\t', a + 1);
        size_t c = b == std::wstring::npos ? b : line.find(L'\t', b + 1);
        if (a != 1 || c == std::wstring::npos || b != 3 || line[0] < L'0' || line[0] > L'7' ||
            (line[2] != L'0' && line[2] != L'1') || c <= b + 1) {
            return false;
        }
        TagDef tag;
        tag.colour = (uint8_t)(line[0] - L'0');
        tag.hidden = line[2] == L'1';
        for (size_t i = b + 1; i < c; i++) {
            if (line[i] < L'0' || line[i] > L'9') {
                return false;
            }
        }
        tag.lastUsed = wcstoull(line.substr(b + 1, c - b - 1).c_str(), nullptr, 10);
        if (!LabelsUnescape(std::wstring_view(line).substr(c + 1), &tag.name) || tag.name.empty()) {
            return false;
        }
        out->push_back(tag);
    }
    return true;
}

namespace {

void TagListSaveLocked() {
    Wh_SetStringValue(L"tagList", TagListSerialize(g_tagList).c_str());
}

ULONGLONG TagListNow() {
    FILETIME time;
    GetSystemTimeAsFileTime(&time);
    ULARGE_INTEGER value;
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return value.QuadPart;
}

void TagListLoadLocked() {
    if (g_tagListLoaded) {
        return;
    }
    g_tagListLoaded = true;
    std::vector<wchar_t> buffer(60000);
    size_t length = Wh_GetStringValue(L"tagList", buffer.data(), buffer.size());
    std::vector<TagDef> loaded;
    if (length && length < buffer.size() && TagListParse(std::wstring(buffer.data(), length), &loaded)) {
        g_tagList = std::move(loaded);
    }
    // Once: the recent tags the first builds stored on their own become part of the list.
    if (Wh_GetIntValue(L"tagList.legacyImported", 0) == 0) {
        TagListImportInto(g_tagList, LabelsRecentLegacyTake(), TagListNow());
        TagListSaveLocked();
        Wh_SetIntValue(L"tagList.legacyImported", 1);
    }
}

}  // namespace

std::vector<TagDef> TagListSnapshot() {
    std::lock_guard<std::mutex> lock(g_tagListMutex);
    TagListLoadLocked();
    return g_tagList;
}

bool TagListAdd(std::wstring const& name, int colour) {
    std::lock_guard<std::mutex> lock(g_tagListMutex);
    TagListLoadLocked();
    bool changed = TagListAddTo(g_tagList, name, colour);
    if (changed) {
        TagListSaveLocked();
    }
    return changed;
}

bool TagListRename(std::wstring const& oldName, std::wstring const& newName) {
    std::lock_guard<std::mutex> lock(g_tagListMutex);
    TagListLoadLocked();
    bool changed = TagListRenameIn(g_tagList, oldName, newName);
    if (changed) {
        TagListSaveLocked();
    }
    return changed;
}

void TagListRecolor(std::wstring const& name, int colour) {
    std::lock_guard<std::mutex> lock(g_tagListMutex);
    TagListLoadLocked();
    TagListRecolorIn(g_tagList, name, colour);
    TagListSaveLocked();
}

void TagListSetHidden(std::wstring const& name, bool hidden) {
    std::lock_guard<std::mutex> lock(g_tagListMutex);
    TagListLoadLocked();
    TagListSetHiddenIn(g_tagList, name, hidden);
    TagListSaveLocked();
}

void TagListRemove(std::wstring const& name) {
    std::lock_guard<std::mutex> lock(g_tagListMutex);
    TagListLoadLocked();
    if (TagListRemoveFrom(g_tagList, name)) {
        TagListSaveLocked();
    }
}

void TagListTouch(std::wstring const& name) {
    std::lock_guard<std::mutex> lock(g_tagListMutex);
    TagListLoadLocked();
    TagListTouchIn(g_tagList, name, TagListNow());
    TagListSaveLocked();
}

// A name the index found on a file: added when the list does not have it.
bool TagListEnsure(std::wstring const& name, int colour) {
    std::lock_guard<std::mutex> lock(g_tagListMutex);
    TagListLoadLocked();
    bool changed = TagListEnsureIn(g_tagList, name, colour);
    if (changed) {
        TagListSaveLocked();
    }
    return changed;
}

std::vector<TagDef> TagListRecent(int count) {
    std::lock_guard<std::mutex> lock(g_tagListMutex);
    TagListLoadLocked();
    return TagListRecentOf(g_tagList, count);
}

// The labels' own entry points (they used to keep a store of their own, 03c-labels-recent.cpp): the tags offered again in the
// "Add a tag" flyout, and a named tag that was just used, added to the list when it is new and marked as the latest.
std::vector<LabelTag> LabelsRecentGet() {
    return TagListAsLabels(TagListRecent((int)kLabelsRecentMax));
}

void LabelsRecentRemember(LabelTag const& tag) {
    if (tag.name.empty()) {
        return;
    }
    TagListEnsure(tag.name, tag.colour);
    TagListTouch(tag.name);
}
