#include "display_win32.h"

namespace dokodemo::gameplay {
bool PresentDisplay(HDC dc, const Display& display) {
    std::vector<std::uint32_t> pixels;
    if (!dc || !display.CopyPixels(pixels)) return false;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = Display::width;
    info.bmiHeader.biHeight = -static_cast<LONG>(Display::height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    return SetDIBitsToDevice(dc, 0, 0, Display::width, Display::height, 0, 0,
        0, Display::height, pixels.data(), &info, DIB_RGB_COLORS) == Display::height;
}
}
