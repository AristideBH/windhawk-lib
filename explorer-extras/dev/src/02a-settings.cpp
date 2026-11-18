
// ---- Settings ----
//
// Every setting is declared once in settings.schema.json. build.py writes the block Windhawk reads
// (with the groups and sub-groups of its page) and the table below from it. The rest of the mod reads
// a setting by its id, never by Windhawk's key. (The mod had a settings window of its own until 0.65;
// see docs/settings-window-notes.md.)

enum class SettingKind { Bool, Int, Enum, String };

struct SettingInfo {
    const wchar_t* id;            // "restoreOnOpen".
    const wchar_t* key;           // Windhawk's name for it: "columns.behaviour.restoreOnOpen".
    SettingKind kind;
    int defaultInt;               // Bool and Int.
    const wchar_t* defaultText;   // Enum and String.
    int minValue;                 // Int: the limits, both 0 for none.
    int maxValue;
    bool rebuild;                 // A change needs the columns read or fitted again.
};

// A text of the mod's own menus, by id, in English and in French.
struct StringInfo {
    const wchar_t* id;
    const wchar_t* text;
};

@@SETTINGS_TABLE@@
constexpr size_t kSettingCount = sizeof(kSettings) / sizeof(kSettings[0]);
constexpr size_t kStringCount = sizeof(kStrings) / sizeof(kStrings[0]);

// The language of Windows' display: French has its own table, every other language is English.
bool FrenchUi() {
    static int cached = -1;
    if (cached < 0) {
        cached = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_FRENCH ? 1 : 0;
    }
    return cached == 1;
}

// The text for an id. A missing id shows as itself, so a gap is seen at once.
const wchar_t* Str(const wchar_t* id) {
    if (FrenchUi()) {
        for (size_t i = 0; i < sizeof(kStringsFr) / sizeof(kStringsFr[0]); i++) {
            if (wcscmp(kStringsFr[i].id, id) == 0) {
                return kStringsFr[i].text;
            }
        }
    }
    for (size_t i = 0; i < kStringCount; i++) {
        if (wcscmp(kStrings[i].id, id) == 0) {
            return kStrings[i].text;
        }
    }
    return id;
}

const SettingInfo* FindSetting(const wchar_t* id) {
    for (size_t i = 0; i < kSettingCount; i++) {
        if (wcscmp(kSettings[i].id, id) == 0) {
            return &kSettings[i];
        }
    }
    return nullptr;
}

int SettingInt(const wchar_t* id) {
    const SettingInfo* info = FindSetting(id);
    if (!info) {
        return 0;
    }
    int value = Wh_GetIntSetting(info->key);
    if (info->minValue || info->maxValue) {
        value = std::clamp(value, info->minValue, info->maxValue);
    }
    return value;
}

bool SettingBool(const wchar_t* id) {
    return SettingInt(id) != 0;
}

std::wstring SettingText(const wchar_t* id) {
    const SettingInfo* info = FindSetting(id);
    if (!info) {
        return std::wstring();
    }
    std::wstring result = info->defaultText;
    auto value = WindhawkUtils::StringSetting::make(info->key);
    if (value.get()) {
        result = value.get();
    }
    return result;
}

// A number that changes when a setting changes that needs the columns read or fitted again.
unsigned long long RebuildSignature() {
    unsigned long long signature = 1469598103934665603ULL;
    for (size_t i = 0; i < kSettingCount; i++) {
        if (!kSettings[i].rebuild) {
            continue;
        }
        unsigned long long value = kSettings[i].kind == SettingKind::Int || kSettings[i].kind == SettingKind::Bool
                                       ? (unsigned long long)SettingInt(kSettings[i].id)
                                       : (unsigned long long)std::hash<std::wstring>()(SettingText(kSettings[i].id));
        signature = (signature ^ value) * 1099511628211ULL;
    }
    return signature;
}
