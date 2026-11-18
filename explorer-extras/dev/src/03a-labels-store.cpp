// Labels: the tags of one item (a file or a folder), kept in one named NTFS stream on the item itself, so they
// travel with it on NTFS. This part knows nothing about windows or painting.
//
// The text of the stream is UTF-8: a first line "v1", then one line per tag, a colour digit (0 is the neutral
// gray, 1 to 7 the palette), a tab, and the name. A backslash, a tab, a line feed and a carriage return in a
// name are written as \\, \t, \n and \r.

struct LabelTag {
    uint8_t colour = 0;
    std::wstring name;
};

constexpr size_t kLabelsMaxTags = 16;
constexpr size_t kLabelsMaxName = 40;
constexpr wchar_t kLabelsStream[] = L":ExplorerExtras.Labels";

enum class LabelsWrite { Ok, Unsupported, ReadOnly, Locked, Failed };

// Why a read gave nothing: a stream that is corrupt or from a newer version, a file in use, no access to it, or
// anything else (the file is gone, a network error).
enum class LabelsReadResult { Ok, Corrupt, Locked, NoAccess, Failed };

std::atomic<bool> g_labels{true};     // Labels on or off.

namespace {

std::mutex g_labelNamesMutex;
std::wstring g_labelNames[8];  // What each colour means to the user, from the settings.

// The name cut to its limit, without splitting a surrogate pair.
std::wstring LabelsClip(std::wstring const& name) {
    if (name.size() <= kLabelsMaxName) {
        return name;
    }
    size_t length = kLabelsMaxName;
    if (name[length - 1] >= 0xD800 && name[length - 1] <= 0xDBFF) {
        length--;
    }
    return name.substr(0, length);
}

std::wstring LabelsEscape(std::wstring const& name) {
    std::wstring result;
    for (wchar_t c : name) {
        switch (c) {
            case L'\\': result += L"\\\\"; break;
            case L'\t': result += L"\\t"; break;
            case L'\n': result += L"\\n"; break;
            case L'\r': result += L"\\r"; break;
            default: result += c;
        }
    }
    return result;
}

bool LabelsUnescape(std::wstring_view text, std::wstring* out) {
    out->clear();
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] != L'\\') {
            *out += text[i];
            continue;
        }
        if (++i >= text.size()) {
            return false;
        }
        switch (text[i]) {
            case L'\\': *out += L'\\'; break;
            case L't': *out += L'\t'; break;
            case L'n': *out += L'\n'; break;
            case L'r': *out += L'\r'; break;
            default: return false;
        }
    }
    return true;
}

LabelsWrite LabelsMapError(DWORD error) {
    switch (error) {
        case ERROR_ACCESS_DENIED:
        case ERROR_WRITE_PROTECT:
            return LabelsWrite::ReadOnly;
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
            return LabelsWrite::Locked;
        case ERROR_INVALID_NAME:
        case ERROR_NOT_SUPPORTED:
        case ERROR_INVALID_FUNCTION:
            return LabelsWrite::Unsupported;
        default:
            return LabelsWrite::Failed;
    }
}

// Tells a handle not to change the file's times when it writes or deletes (the stream's own write would
// otherwise reset the last-write time of the file or folder).
void LabelsKeepTimes(HANDLE file) {
    FILETIME keep = {0xFFFFFFFF, 0xFFFFFFFF};
    SetFileTime(file, nullptr, nullptr, &keep);
}

}  // namespace

std::wstring LabelsSerialize(std::vector<LabelTag> const& tags) {
    std::wstring text = L"v1\n";
    size_t count = 0;
    for (auto const& tag : tags) {
        if (count++ >= kLabelsMaxTags) {
            break;
        }
        text += static_cast<wchar_t>(L'0' + std::min<int>(tag.colour, 7));
        text += L'\t';
        text += LabelsEscape(LabelsClip(tag.name));
        text += L'\n';
    }
    return text;
}

bool LabelsParse(std::wstring_view text, std::vector<LabelTag>* out) {
    out->clear();
    size_t lineEnd = text.find(L'\n');
    if (lineEnd == std::wstring_view::npos || text.substr(0, lineEnd) != L"v1") {
        return false;
    }
    size_t at = lineEnd + 1;
    while (at < text.size()) {
        lineEnd = text.find(L'\n', at);
        std::wstring_view line = text.substr(at, lineEnd == std::wstring_view::npos ? text.size() - at : lineEnd - at);
        at = lineEnd == std::wstring_view::npos ? text.size() : lineEnd + 1;
        if (line.size() < 2 || line[0] < L'0' || line[0] > L'7' || line[1] != L'\t') {
            out->clear();
            return false;
        }
        LabelTag tag;
        tag.colour = static_cast<uint8_t>(line[0] - L'0');
        if (!LabelsUnescape(line.substr(2), &tag.name)) {
            out->clear();
            return false;
        }
        tag.name = LabelsClip(tag.name);
        if (out->size() < kLabelsMaxTags) {
            out->push_back(std::move(tag));
        }
    }
    return true;
}

bool LabelsVolumeSupportsStreams(std::wstring const& path) {
    wchar_t root[MAX_PATH];
    if (!GetVolumePathNameW(path.c_str(), root, MAX_PATH)) {
        return false;
    }
    DWORD flags = 0;
    if (!GetVolumeInformationW(root, nullptr, 0, nullptr, nullptr, &flags, nullptr, 0)) {
        return false;
    }
    return (flags & FILE_NAMED_STREAMS) != 0;
}

// Ok with an empty list when the item has no stream. Otherwise why the tags could not be read: the caller must then
// not overwrite them.
LabelsReadResult LabelsReadFileEx(std::wstring const& path, std::vector<LabelTag>* out) {
    out->clear();
    HANDLE file = CreateFileW((path + kLabelsStream).c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES ? LabelsReadResult::Ok : LabelsReadResult::Failed;
        }
        if (error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION) {
            return LabelsReadResult::Locked;
        }
        return error == ERROR_ACCESS_DENIED ? LabelsReadResult::NoAccess : LabelsReadResult::Failed;
    }
    char buffer[8192];
    DWORD read = 0;
    BOOL ok = ReadFile(file, buffer, sizeof(buffer), &read, nullptr);
    DWORD size = GetFileSize(file, nullptr);
    CloseHandle(file);
    if (!ok) {
        return LabelsReadResult::Failed;
    }
    if (size > sizeof(buffer)) {
        return LabelsReadResult::Corrupt;
    }
    int wide = read ? MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, buffer, (int)read, nullptr, 0) : 0;
    if (read && wide <= 0) {
        return LabelsReadResult::Corrupt;
    }
    std::wstring text(wide, L'\0');
    if (wide) {
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, buffer, (int)read, text.data(), wide);
    }
    return text.empty() || LabelsParse(text, out) ? LabelsReadResult::Ok : LabelsReadResult::Corrupt;
}

// True with an empty list when the item has no stream. False when the item cannot be read, or its stream is
// corrupt or from a newer version (the caller must then not overwrite it).
bool LabelsReadFile(std::wstring const& path, std::vector<LabelTag>* out) {
    return LabelsReadFileEx(path, out) == LabelsReadResult::Ok;
}

// An empty list removes the stream. The item's times are never changed.
LabelsWrite LabelsWriteFile(std::wstring const& path, std::vector<LabelTag> const& tags) {
    if (!LabelsVolumeSupportsStreams(path)) {
        return LabelsWrite::Unsupported;
    }
    std::wstring stream = path + kLabelsStream;
    if (tags.empty()) {
        HANDLE file = CreateFileW(stream.c_str(), DELETE | FILE_WRITE_ATTRIBUTES,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? LabelsWrite::Ok
                                                                                  : LabelsMapError(error);
        }
        LabelsKeepTimes(file);
        FILE_DISPOSITION_INFO remove = {TRUE};
        BOOL removed = SetFileInformationByHandle(file, FileDispositionInfo, &remove, sizeof(remove));
        DWORD error = GetLastError();
        CloseHandle(file);
        return removed ? LabelsWrite::Ok : LabelsMapError(error);
    }
    std::wstring text = LabelsSerialize(tags);
    int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string utf8(bytes, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), utf8.data(), bytes, nullptr, nullptr);
    HANDLE file = CreateFileW(stream.c_str(), GENERIC_WRITE | FILE_WRITE_ATTRIBUTES,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                              FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return LabelsMapError(GetLastError());
    }
    LabelsKeepTimes(file);
    // Not CREATE_ALWAYS: that truncates before the write (a write that fails would leave an empty stream, and a hidden or
    // system item refuses it). The new text is written over the old one, and the end is cut after.
    DWORD written = 0;
    BOOL ok = WriteFile(file, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
    DWORD error = GetLastError();
    if (ok && written == utf8.size()) {
        ok = SetEndOfFile(file);
        error = GetLastError();
    }
    CloseHandle(file);
    return ok && written == utf8.size() ? LabelsWrite::Ok : LabelsMapError(error);
}

// The name the user gave a colour in the settings ("Red = Urgent"). Safe from any thread.
std::wstring LabelsColourName(int colour) {
    std::lock_guard<std::mutex> lock(g_labelNamesMutex);
    return g_labelNames[std::clamp(colour, 0, 7)];
}

void LabelsLoadSettings() {
    g_labels = SettingBool(L"labelsOn");
    std::lock_guard<std::mutex> lock(g_labelNamesMutex);
    for (int i = 0; i < 8; i++) {
        g_labelNames[i] = LabelsClip(SettingText((std::wstring(L"colourName") + std::to_wstring(i)).c_str()));
    }
}
