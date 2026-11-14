
// ---- Settings ----
//
// Every setting is declared once in settings.schema.json. build.py writes the
// block Windhawk reads and the table below from it. The rest of the mod reads a
// setting by its id, never by Windhawk's key, so the window that comes next can put
// its own value in front of Windhawk's in one place.

enum class SettingKind { Bool, Int, Enum, String };

struct SettingInfo {
    const wchar_t* id;            // "dragOut".
    const wchar_t* key;           // Windhawk's name for it: "columns.dragOut", "debugLog".
    SettingKind kind;
    int defaultInt;               // Bool and Int.
    const wchar_t* defaultText;   // Enum and String.
    int minValue;                 // Int: the limits, both 0 for none.
    int maxValue;
    const wchar_t* name;
    const wchar_t* description;
    const wchar_t* page;          // "home", "columns", or "" for none.
    const wchar_t* card;          // The card of the page it sits in.
    bool windhawkOnly;            // Not shown in the settings window.
    const wchar_t* options;       // Enum: "value=Label|value=Label".
};

struct SettingCard {
    const wchar_t* id;
    const wchar_t* name;
};

@@SETTINGS_TABLE@@
constexpr size_t kSettingCount = sizeof(kSettings) / sizeof(kSettings[0]);
constexpr size_t kCardCount = sizeof(kCards) / sizeof(kCards[0]);

const SettingInfo* FindSetting(const wchar_t* id) {
    for (size_t i = 0; i < kSettingCount; i++) {
        if (wcscmp(kSettings[i].id, id) == 0) {
            return &kSettings[i];
        }
    }
    return nullptr;
}

// The value of a setting. For now it is Windhawk's; the settings window's own
// overrides will be looked up here first.
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
        return L"";
    }
    std::wstring result = info->defaultText;
    if (const wchar_t* value = Wh_GetStringSetting(info->key)) {
        result = value;
        Wh_FreeStringSetting(value);
    }
    return result;
}
