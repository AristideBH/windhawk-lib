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

namespace {

[[clang::no_destroy]] std::mutex g_labelsRecentMutex;
[[clang::no_destroy]] std::vector<LabelTag> g_labelsRecent;
bool g_labelsRecentLoaded = false;

void LabelsRecentLoad() {  // The caller holds the lock.
    if (g_labelsRecentLoaded) {
        return;
    }
    g_labelsRecentLoaded = true;
    std::vector<wchar_t> buffer(4000);
    size_t length = Wh_GetStringValue(L"labels.recent", buffer.data(), buffer.size());
    if (length > 0 && length < buffer.size()) {
        std::vector<LabelTag> parsed;
        if (LabelsParse(std::wstring_view(buffer.data(), length), &parsed)) {
            g_labelsRecent = std::move(parsed);
        }
    }
}

}  // namespace

std::vector<LabelTag> LabelsRecentGet() {
    std::lock_guard<std::mutex> lock(g_labelsRecentMutex);
    LabelsRecentLoad();
    return g_labelsRecent;
}

void LabelsRecentRemember(LabelTag const& tag) {
    if (tag.name.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_labelsRecentMutex);
    LabelsRecentLoad();
    g_labelsRecent = LabelsRecentPush(g_labelsRecent, tag);
    Wh_SetStringValue(L"labels.recent", LabelsSerialize(g_labelsRecent).c_str());
}
