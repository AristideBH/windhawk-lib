// Labels: the palette, the theme and the dot images. A dot is rendered once per colour, theme and size into a
// premultiplied 32-bit image (4x4 supersampling, so its edge is soft), kept in a cache, and blended onto a
// device context with AlphaBlend. There is no outline: the rim of a dot is its fill, a little lighter in the
// dark theme and a little darker in the light one. GDI+ is not used on purpose (its start-up and shutdown are
// the kind of global teardown that hangs Explorer at sign-out).

namespace {

struct LabelsPaletteEntry {
    COLORREF light;
    COLORREF dark;
};

// 0 is the neutral gray, then red, orange, yellow, green, teal, blue, purple. From the Fluent colours; the dark
// variants are the lighter tints that keep their contrast on a dark list.
constexpr LabelsPaletteEntry kLabelsPalette[8] = {
    {RGB(138, 136, 134), RGB(161, 159, 157)},  // gray
    {RGB(209, 52, 56), RGB(241, 112, 123)},    // red
    {RGB(247, 99, 12), RGB(255, 140, 58)},     // orange
    {RGB(255, 185, 0), RGB(252, 225, 0)},      // yellow
    {RGB(16, 124, 16), RGB(108, 203, 95)},     // green
    {RGB(0, 153, 188), RGB(76, 194, 207)},     // teal
    {RGB(0, 120, 212), RGB(96, 205, 255)},     // blue
    {RGB(135, 100, 184), RGB(180, 160, 255)},  // purple
};

std::atomic<ULONGLONG> g_labelsThemeChecked{0};
std::atomic<bool> g_labelsDark{false};
std::mutex g_labelDotMutex;
[[clang::no_destroy]] std::map<unsigned, HBITMAP> g_labelDots;  // colour, theme and side to the image.

BITMAPINFO LabelsDotInfo(int side) {
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = side;
    info.bmiHeader.biHeight = -side;  // Top down.
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    return info;
}

}  // namespace

COLORREF LabelsColourRgb(int colour, bool dark) {
    const LabelsPaletteEntry& entry = kLabelsPalette[std::clamp(colour, 0, 7)];
    return dark ? entry.dark : entry.light;
}

// The apps' theme, read again at most once a second. Safe from any thread.
bool LabelsDarkTheme() {
    ULONGLONG now = GetTickCount64();
    ULONGLONG checked = g_labelsThemeChecked;
    if (checked == 0 || now - checked >= 1000) {
        DWORD light = 1, size = sizeof(light);
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
        g_labelsDark = light == 0;
        g_labelsThemeChecked = now ? now : 1;
    }
    return g_labelsDark;
}

// 8 px at 96 DPI, scaled with the DPI, at most 60 percent of the row and at least 6 px.
int LabelsDotSide(int rowHeightPx, UINT dpi) {
    int side = MulDiv(8, (int)dpi, 96);
    side = std::min(side, rowHeightPx * 6 / 10);
    return std::max(side, 6);
}

// The image of a dot: side x side premultiplied BGRA pixels, top down.
bool LabelsRenderDot(int colour, bool dark, int side, std::vector<uint32_t>* out) {
    if (side < 2 || side > 256) {
        return false;
    }
    COLORREF fill = LabelsColourRgb(colour, dark);
    int target = dark ? 255 : 0;
    auto toward = [target](int channel) { return channel + (target - channel) * 28 / 100; };
    COLORREF rim = RGB(toward(GetRValue(fill)), toward(GetGValue(fill)), toward(GetBValue(fill)));
    const double radius = side / 2.0;
    const double rimWidth = std::max(1.0, side / 10.0);
    const int n = 4;
    out->assign((size_t)side * side, 0);
    for (int y = 0; y < side; y++) {
        for (int x = 0; x < side; x++) {
            int covered = 0, red = 0, green = 0, blue = 0;
            for (int sy = 0; sy < n; sy++) {
                for (int sx = 0; sx < n; sx++) {
                    double dx = x + (sx + 0.5) / n - radius;
                    double dy = y + (sy + 0.5) / n - radius;
                    double distance = std::sqrt(dx * dx + dy * dy);
                    if (distance > radius) {
                        continue;
                    }
                    COLORREF colourHere = distance >= radius - rimWidth ? rim : fill;
                    covered++;
                    red += GetRValue(colourHere);
                    green += GetGValue(colourHere);
                    blue += GetBValue(colourHere);
                }
            }
            if (!covered) {
                continue;
            }
            uint32_t alpha = (uint32_t)(covered * 255 / (n * n));
            (*out)[(size_t)y * side + x] = (alpha << 24) | ((uint32_t)(red / (n * n)) << 16) |
                                           ((uint32_t)(green / (n * n)) << 8) | (uint32_t)(blue / (n * n));
        }
    }
    return true;
}

// Blends a dot onto the device context: x is its left edge, centreY the middle of the row.
void LabelsDrawDot(HDC dc, int x, int centreY, int colour, int sidePx) {
    if (sidePx < 2 || sidePx > 256) {
        return;
    }
    bool dark = LabelsDarkTheme();
    unsigned key = (unsigned)(std::clamp(colour, 0, 7) | (dark ? 8 : 0) | (sidePx << 4));
    std::lock_guard<std::mutex> lock(g_labelDotMutex);
    HBITMAP bitmap = nullptr;
    auto found = g_labelDots.find(key);
    if (found != g_labelDots.end()) {
        bitmap = found->second;
    } else {
        std::vector<uint32_t> pixels;
        if (!LabelsRenderDot(colour, dark, sidePx, &pixels)) {
            return;
        }
        BITMAPINFO info = LabelsDotInfo(sidePx);
        void* bits = nullptr;
        bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bitmap || !bits) {
            if (bitmap) {
                DeleteObject(bitmap);
            }
            return;
        }
        memcpy(bits, pixels.data(), pixels.size() * sizeof(uint32_t));
        g_labelDots[key] = bitmap;
    }
    // The image is selected into a device context only while the lock is held: one bitmap cannot be in two.
    HDC memory = CreateCompatibleDC(dc);
    if (!memory) {
        return;
    }
    HGDIOBJ old = SelectObject(memory, bitmap);
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    AlphaBlend(dc, x, centreY - sidePx / 2, sidePx, sidePx, memory, 0, 0, sidePx, sidePx, blend);
    SelectObject(memory, old);
    DeleteDC(memory);
}

// The small "plus" that says there are more tags than dots.
void LabelsDrawMore(HDC dc, int x, int centreY, int sidePx) {
    int arm = std::max(sidePx / 2, 3);
    int thickness = sidePx >= 16 ? 2 : 1;
    HPEN pen = CreatePen(PS_SOLID, thickness, LabelsDarkTheme() ? RGB(200, 200, 200) : RGB(96, 96, 96));
    if (!pen) {
        return;
    }
    HGDIOBJ old = SelectObject(dc, pen);
    int centreX = x + arm;
    MoveToEx(dc, x, centreY, nullptr);
    LineTo(dc, x + 2 * arm + 1, centreY);
    MoveToEx(dc, centreX, centreY - arm, nullptr);
    LineTo(dc, centreX, centreY + arm + 1);
    SelectObject(dc, old);
    DeleteObject(pen);
}

// Frees every cached image. Called when the mod unloads.
void LabelsReleaseDots() {
    std::lock_guard<std::mutex> lock(g_labelDotMutex);
    for (auto& entry : g_labelDots) {
        DeleteObject(entry.second);
    }
    g_labelDots.clear();
}
