#include "asset_store.h"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace native_port {
std::wstring Utf8(const std::string& text) {
    if (text.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), nullptr, 0);
    if (!size) return L"[invalid UTF-8]";
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), result.data(), size);
    return result;
}
std::string Utf8(const std::wstring& text) {
    if (text.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    if (!size) return "[invalid UTF-16]";
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), (int)text.size(), result.data(), size, nullptr, nullptr);
    return result;
}
std::string JsonString(const std::string& text) {
    std::string out = "\"";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 32) { const char* hex = "0123456789abcdef"; out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += c;
    }
    return out + '"';
}
std::filesystem::path ResolveAssets(const std::filesystem::path& supplied) {
    if (!supplied.empty()) return std::filesystem::absolute(supplied);
    wchar_t exe[32768] = {};
    auto size = GetModuleFileNameW(nullptr, exe, 32768);
    if (!size || size == 32768) throw std::runtime_error("Cannot resolve executable directory");
    auto dir = std::filesystem::path(exe).parent_path();
    for (auto p : {dir / L"assets/native", dir.parent_path() / L"assets/native", std::filesystem::current_path() / L"assets/native"})
        if (std::filesystem::is_directory(p)) return std::filesystem::absolute(p);
    return dir / L"assets/native";
}
std::vector<std::filesystem::path> FindImages(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) return files;
    for (auto it = std::filesystem::recursive_directory_iterator(root, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        auto extension = it->path().extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
        if (extension == L".bmp") files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    return files;
}
bool LoadBmp(const std::filesystem::path& path, Image& image, std::wstring& error) {
    std::ifstream in(path, std::ios::binary);
    BITMAPFILEHEADER file{}; BITMAPINFOHEADER info{};
    if (!in.read((char*)&file, sizeof(file)) || !in.read((char*)&info, sizeof(info))) { error = L"Truncated BMP header"; return false; }
    if (file.bfType != 0x4d42 || (info.biSize != 40 && info.biSize != 108 && info.biSize != 124) || info.biPlanes != 1
        || (info.biCompression != BI_RGB && info.biCompression != BI_BITFIELDS) || (info.biBitCount != 24 && info.biBitCount != 32)
        || info.biWidth <= 0 || info.biWidth > 8192 || info.biHeight == 0 || info.biHeight == std::numeric_limits<LONG>::min() || abs(info.biHeight) > 8192) {
        error = L"Unsupported or invalid BMP (requires 24/32-bit BGR/BGRA)"; return false;
    }
    size_t headerEnd = sizeof(file) + info.biSize;
    if (info.biCompression == BI_BITFIELDS) {
        if (info.biBitCount != 32) { error = L"Only BGRA32 BMP bitfields are supported"; return false; }
        uint32_t masks[4] = {};
        size_t count = info.biSize >= 108 ? 4 : 3;
        if (!in.read(reinterpret_cast<char*>(masks), count * sizeof(uint32_t))) { error = L"Truncated BMP bit masks"; return false; }
        if (info.biSize == 40) headerEnd += count * sizeof(uint32_t);
        if (masks[0] != 0x00ff0000 || masks[1] != 0x0000ff00 || masks[2] != 0x000000ff || (masks[3] != 0 && masks[3] != 0xff000000)) {
            error = L"Unsupported BMP channel masks"; return false;
        }
    }
    int width = info.biWidth, height = abs(info.biHeight);
    size_t stride = ((size_t(width) * info.biBitCount + 31) / 32) * 4;
    in.seekg(0, std::ios::end); auto bytes = in.tellg();
    if (file.bfOffBits < headerEnd || bytes < 0 || uint64_t(file.bfOffBits) + stride * height > uint64_t(bytes)) { error = L"BMP pixels outside file"; return false; }
    in.seekg(file.bfOffBits);
    Image result; result.path = path; result.width = width; result.height = height;
    result.bgra.resize(size_t(width) * height * 4);
    std::vector<unsigned char> row(stride);
    for (int y = 0; y < height; ++y) {
        if (!in.read((char*)row.data(), stride)) { error = L"Truncated BMP pixels"; return false; }
        int dstY = info.biHeight > 0 ? height - 1 - y : y;
        for (int x = 0; x < width; ++x) {
            auto dst = result.bgra.data() + (size_t(dstY) * width + x) * 4;
            auto src = row.data() + size_t(x) * (info.biBitCount / 8);
            dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = info.biBitCount == 32 ? src[3] : 255;
        }
    }
    image = std::move(result); error.clear(); return true;
}
bool WriteBmp(const std::filesystem::path& path, int width, int height, const void* bgra, std::wstring& error) {
    if (!bgra || width <= 0 || height <= 0 || width > 8192 || height > 8192) { error = L"Invalid screenshot size"; return false; }
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    BITMAPFILEHEADER file{}; BITMAPINFOHEADER info{};
    file.bfType = 0x4d42; file.bfOffBits = sizeof(file) + sizeof(info); file.bfSize = file.bfOffBits + width * height * 4;
    info.biSize = sizeof(info); info.biWidth = width; info.biHeight = -height;
    info.biPlanes = 1; info.biBitCount = 32; info.biCompression = BI_RGB; info.biSizeImage = width * height * 4;
    out.write((char*)&file, sizeof(file)); out.write((char*)&info, sizeof(info)); out.write((const char*)bgra, info.biSizeImage);
    if (!out) { error = L"Cannot write screenshot"; return false; }
    error.clear(); return true;
}
}
