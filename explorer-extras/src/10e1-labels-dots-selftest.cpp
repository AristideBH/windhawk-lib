// Labels: the self-test of the palette and the dot images (debug build only). One log line:
// "LABELS DOTS SELFTEST pass" or "LABELS DOTS SELFTEST FAIL <what>".
#ifdef EXTRAS_VERBOSE

namespace {

std::wstring g_dotsFailure;

void DotsExpect(bool ok, const wchar_t* what) {
    if (!ok && g_dotsFailure.empty()) {
        g_dotsFailure = what;
    }
}

uint32_t DotsPixelOf(COLORREF rgb) {
    return (255u << 24) | (GetRValue(rgb) << 16) | (GetGValue(rgb) << 8) | GetBValue(rgb);
}

}  // namespace

void LabelsDotsSelfTest() {
    DotsExpect(LabelsDotSide(22, 96) == 8, L"8 px at 96 DPI");
    DotsExpect(LabelsDotSide(44, 192) == 16, L"16 px at 192 DPI");
    DotsExpect(LabelsDotSide(10, 96) == 6, L"never above 60 percent of a short row");
    DotsExpect(LabelsDotSide(100, 48) >= 6, L"never below 6 px");

    for (int dark = 0; dark < 2; dark++) {
        for (int colour = 0; colour < 8; colour++) {
            for (int side : {8, 12, 24}) {
                std::vector<uint32_t> pixels;
                bool rendered = LabelsRenderDot(colour, dark != 0, side, &pixels);
                DotsExpect(rendered && (int)pixels.size() == side * side, L"a dot image has side x side pixels");
                if (!rendered || (int)pixels.size() != side * side) {
                    continue;
                }
                uint32_t centre = pixels[(side / 2) * side + side / 2];
                DotsExpect(centre == DotsPixelOf(LabelsDotRgb(colour, dark != 0)), L"the centre is the opaque fill colour");
                DotsExpect((pixels[0] >> 24) == 0, L"a corner is transparent");
                bool partial = false;
                for (uint32_t pixel : pixels) {
                    uint32_t alpha = pixel >> 24;
                    partial = partial || (alpha > 0 && alpha < 255);
                }
                DotsExpect(partial, L"the edge is anti-aliased");
            }
        }
    }
    for (int dark = 0; dark < 2; dark++) {
        for (int colour = 1; colour < 8; colour++) {
            std::vector<uint32_t> big;
            if (LabelsRenderDot(colour, dark != 0, 24, &big) && big.size() == 24 * 24) {
                uint32_t rim = big[12 * 24 + 1];
                COLORREF fill = LabelsDotRgb(colour, dark != 0);
                int rimSum = (int)((rim >> 16) & 255) + (int)((rim >> 8) & 255) + (int)(rim & 255);
                int fillSum = GetRValue(fill) + GetGValue(fill) + GetBValue(fill);
                DotsExpect((rim >> 24) == 255 && (dark ? rimSum < fillSum : rimSum > fillSum),
                           L"the rim is darker than the fill in the dark theme and lighter in the light theme");
            }
        }
    }
    std::vector<uint32_t> disc;
    DotsExpect(LabelsRenderDisc(RGB(10, 20, 30), 12, &disc) && disc.size() == 144 && disc[6 * 12 + 6] == DotsPixelOf(RGB(10, 20, 30)) &&
                   (disc[0] >> 24) == 0,
               L"a disc is opaque in the middle and transparent in the corner");
    DotsExpect(LabelsColourRgb(1, false) != LabelsColourRgb(1, true), L"light and dark variants differ");
    DotsExpect(LabelsDotRgb(1, true) == LabelsColourRgb(1, true), L"in the dark theme a dot is the palette colour");
    COLORREF lightDot = LabelsDotRgb(1, false), lightBase = LabelsColourRgb(1, false);
    DotsExpect(GetRValue(lightDot) > GetRValue(lightBase) && GetGValue(lightDot) > GetGValue(lightBase) &&
                   GetBValue(lightDot) > GetBValue(lightBase) && GetGValue(lightDot) < 255,
               L"in the light theme a dot is the palette colour softened toward white, like a badge");
    DotsExpect(LabelsColourRgb(0, false) != LabelsColourRgb(1, false), L"gray differs from red");

    // Drawing onto a DC with a white background.
    const int size = 40;
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    uint32_t* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, (void**)&bits, nullptr, 0);
    if (!bitmap || !bits) {
        DotsExpect(false, L"could not make a test bitmap");
    } else {
        HGDIOBJ old = SelectObject(dc, bitmap);
        for (int i = 0; i < size * size; i++) {
            bits[i] = 0x00FFFFFF;
        }
        LabelsDrawDot(dc, 14, 20, 1, 12);
        COLORREF fill = LabelsDotRgb(1, LabelsDarkTheme());
        DotsExpect((bits[20 * size + 20] & 0xFFFFFF) == (DotsPixelOf(fill) & 0xFFFFFF), L"a drawn dot has its colour at the centre");
        DotsExpect((bits[0] & 0xFFFFFF) == 0xFFFFFF, L"a drawn dot leaves the corner untouched");
        bool blended = false;
        for (int y = 12; y < 28; y++) {
            for (int x = 12; x < 28; x++) {
                uint32_t pixel = bits[y * size + x] & 0xFFFFFF;
                blended = blended || (pixel != 0xFFFFFF && pixel != (DotsPixelOf(fill) & 0xFFFFFF));
            }
        }
        DotsExpect(blended, L"a drawn dot has soft edge pixels");
        SelectObject(dc, old);
        DeleteObject(bitmap);
    }
    DeleteDC(dc);
    LabelsReleaseDots();

    if (g_dotsFailure.empty()) {
        Wh_Log(L"LABELS DOTS SELFTEST pass");
    } else {
        Wh_Log(L"LABELS DOTS SELFTEST FAIL %s", g_dotsFailure.c_str());
    }
}

#endif  // EXTRAS_VERBOSE
