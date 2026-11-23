// The tags the user gave a name lately, offered again in the "Add a tag" flyout so a name is typed once. Kept in the mod's storage.

constexpr size_t kLabelsRecentMax = 8;

// The list with `tag` first, once (same colour and name, any case), the others after it, at most `max`. A tag with no name is not
// remembered (the colours are always there). Pure.
std::vector<LabelTag> LabelsRecentPush(std::vector<LabelTag> recent, LabelTag const& tag, size_t max = kLabelsRecentMax) {
    if (tag.name.empty()) {
        return recent;
    }
    recent.erase(std::remove_if(recent.begin(), recent.end(),
                                [&tag](LabelTag const& other) {
                                    return other.colour == tag.colour && _wcsicmp(other.name.c_str(), tag.name.c_str()) == 0;
                                }),
                 recent.end());
    recent.insert(recent.begin(), tag);
    if (recent.size() > max) {
        recent.resize(max);
    }
    return recent;
}

// One stored value of a recent tag: the colour, a bar, the name with its line breaks escaped. Portable Windhawk keeps the storage in
// an INI file, where a value cannot hold a line break, so each tag is its own single-line value.
std::wstring LabelsRecentEntry(LabelTag const& tag) {
    return std::to_wstring(std::min<int>(tag.colour, 7)) + L"|" + LabelsEscape(tag.name);
}

bool LabelsRecentParse(std::wstring_view text, LabelTag* out) {
    if (text.size() < 2 || text[0] < L'0' || text[0] > L'7' || text[1] != L'|') {
        return false;
    }
    LabelTag tag;
    tag.colour = (uint8_t)(text[0] - L'0');
    if (!LabelsUnescape(text.substr(2), &tag.name) || tag.name.empty()) {
        return false;
    }
    *out = std::move(tag);
    return true;
}

// The recent tags now live in the tag list (03d-tag-list.cpp), which LabelsRecentGet and LabelsRecentRemember read and write
// (defined there). This reads what the first builds stored, newest first, and deletes it, so the list can take it in once.
std::vector<LabelTag> LabelsRecentLegacyTake() {
    std::vector<LabelTag> found;
    int count = Wh_GetIntValue(L"labels.recent.count", 0);
    for (int i = 0; i < count && i < (int)kLabelsRecentMax; i++) {
        wchar_t name[40];
        swprintf_s(name, L"labels.recent.%d", i);
        std::vector<wchar_t> buffer(200);
        size_t length = Wh_GetStringValue(name, buffer.data(), buffer.size());
        LabelTag tag;
        if (length > 0 && length < buffer.size() && LabelsRecentParse(std::wstring_view(buffer.data(), length), &tag)) {
            found.push_back(std::move(tag));
        }
        Wh_DeleteValue(name);
    }
    Wh_DeleteValue(L"labels.recent.count");
    return found;
}
