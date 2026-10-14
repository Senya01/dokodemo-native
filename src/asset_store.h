#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>

namespace native_port {
struct Image {
    std::filesystem::path path;
    std::wstring source;
    int width = 0, height = 0;
    std::vector<unsigned char> bgra;
};
bool LoadBmp(const std::filesystem::path& path, Image& image, std::wstring& error);
bool WriteBmp(const std::filesystem::path& path, int width, int height,
              const void* bgra, std::wstring& error);
std::vector<std::filesystem::path> FindImages(const std::filesystem::path& root);
std::filesystem::path ResolveAssets(const std::filesystem::path& supplied);
std::wstring Utf8(const std::string& text);
std::string Utf8(const std::wstring& text);
std::string JsonString(const std::string& text);
}
