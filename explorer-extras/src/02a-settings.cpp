
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
    bool rebuild;                 // A change needs the columns read or fitted again.
};

struct SettingCard {
    const wchar_t* id;
    const wchar_t* name;
};

// A text of the mod's own windows and menus, by id. English now; the table is where
// translations will come in.
struct StringInfo {
    const wchar_t* id;
    const wchar_t* text;
};

// The texts of a setting in French (the other languages are not done): its title and its description.
struct SettingText {
    const wchar_t* id;
    const wchar_t* name;
    const wchar_t* description;
};

@@SETTINGS_TABLE@@
constexpr size_t kSettingCount = sizeof(kSettings) / sizeof(kSettings[0]);
constexpr size_t kCardCount = sizeof(kCards) / sizeof(kCards[0]);
constexpr size_t kWhatsNewCount = sizeof(kWhatsNew) / sizeof(kWhatsNew[0]);
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

// The title and the description of a setting, in the language of the display.
std::wstring SettingName(SettingInfo const& info) {
    if (FrenchUi()) {
        for (auto const& text : kSettingsFr) {
            if (wcscmp(text.id, info.id) == 0) {
                return text.name;
            }
        }
    }
    return info.name;
}

std::wstring SettingDescription(SettingInfo const& info) {
    if (FrenchUi()) {
        for (auto const& text : kSettingsFr) {
            if (wcscmp(text.id, info.id) == 0) {
                return text.description;
            }
        }
    }
    return info.description;
}

std::wstring CardName(size_t index) {
    if (FrenchUi()) {
        for (auto const& card : kCardsFr) {
            if (wcscmp(card.id, kCards[index].id) == 0) {
                return card.name;
            }
        }
    }
    return kCards[index].name;
}

const SettingInfo* FindSetting(const wchar_t* id) {
    for (size_t i = 0; i < kSettingCount; i++) {
        if (wcscmp(kSettings[i].id, id) == 0) {
            return &kSettings[i];
        }
    }
    return nullptr;
}

// ---- Where a value comes from ----
//
// Windhawk's page holds the default. The settings window writes an override, kept
// in the mod's own storage as text under "ui.<id>", together with the value
// Windhawk's page had at that moment under "ui.seen.<id>". The value in use is the
// override, unless Windhawk's page has been edited since: that difference is a new
// edit on the page, and it wins and clears the override.

std::wstring WindhawkValueText(const SettingInfo* info) {
    if (info->kind == SettingKind::Bool || info->kind == SettingKind::Int) {
        return std::to_wstring(Wh_GetIntSetting(info->key));
    }
    std::wstring result = info->defaultText;
    if (const wchar_t* value = Wh_GetStringSetting(info->key)) {
        result = value;
        Wh_FreeStringSetting(value);
    }
    return result;
}

bool ReadStored(const std::wstring& name, std::wstring* out) {
    wchar_t buffer[256];
    size_t length = Wh_GetStringValue(name.c_str(), buffer, ARRAYSIZE(buffer));
    if (!length) {
        return false;
    }
    *out = buffer;
    return true;
}

std::wstring EffectiveText(const SettingInfo* info) {
    std::wstring windhawk = WindhawkValueText(info);
    std::wstring override;
    if (ReadStored(std::wstring(L"ui.") + info->id, &override)) {
        std::wstring seen;
        if (ReadStored(std::wstring(L"ui.seen.") + info->id, &seen) && seen != windhawk) {
            Wh_DeleteValue((std::wstring(L"ui.") + info->id).c_str());
            Wh_DeleteValue((std::wstring(L"ui.seen.") + info->id).c_str());
            return windhawk;  // Edited on Windhawk's page since: the page wins.
        }
        return override;
    }
    return windhawk;
}

bool SettingIsOverridden(const SettingInfo* info) {
    std::wstring stored;
    return ReadStored(std::wstring(L"ui.") + info->id, &stored);
}

// The window's choice. The same as Windhawk's value needs no override, and the
// override that was there goes.
void WriteSettingOverride(const SettingInfo* info, const std::wstring& text) {
    std::wstring name = std::wstring(L"ui.") + info->id;
    std::wstring seenName = std::wstring(L"ui.seen.") + info->id;
    std::wstring windhawk = WindhawkValueText(info);
    if (text == windhawk) {
        Wh_DeleteValue(name.c_str());
        Wh_DeleteValue(seenName.c_str());
        return;
    }
    Wh_SetStringValue(name.c_str(), text.c_str());
    Wh_SetStringValue(seenName.c_str(), windhawk.c_str());
}

void ClearAllOverrides() {
    for (size_t i = 0; i < kSettingCount; i++) {
        Wh_DeleteValue((std::wstring(L"ui.") + kSettings[i].id).c_str());
        Wh_DeleteValue((std::wstring(L"ui.seen.") + kSettings[i].id).c_str());
    }
}

int SettingInt(const wchar_t* id) {
    const SettingInfo* info = FindSetting(id);
    if (!info) {
        return 0;
    }
    int value = _wtoi(EffectiveText(info).c_str());
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
    return info ? EffectiveText(info) : std::wstring();
}
