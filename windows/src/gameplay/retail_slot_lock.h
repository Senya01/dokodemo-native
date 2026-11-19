#pragma once
#include <filesystem>

namespace dokodemo::gameplay {
class RetailSlotLock {
public:
    explicit RetailSlotLock(const std::filesystem::path& directory);
    ~RetailSlotLock();
    RetailSlotLock(const RetailSlotLock&)=delete;
    RetailSlotLock& operator=(const RetailSlotLock&)=delete;
private:
    void* handle_;
};
bool SlotReparsePoint(const std::filesystem::path& path);
}
