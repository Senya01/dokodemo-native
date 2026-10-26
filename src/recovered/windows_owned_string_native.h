#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace dokodemo::recovered {
struct WindowsStringState;
struct WindowsStringLease;

// Logical addresses map real native buffers; none is a recovered PSP address.
struct WindowsStringPolicy {
    uint32_t heap_base=0x01000000,capacity=0x00400000,common_base=0x01000000;
};
struct OwnedStringReceipt {
    bool live=false;
    uint64_t owner=0,epoch=0,serial=0;
    uint32_t address=0,common_base=0,encoded_word=0,bytes=0;
    std::array<uint8_t,32> sha256{};
};
struct OwnedStringArenaReceipt {
    bool initialized=false,failed=false;
    uint64_t owner=0,epoch=0,allocations=0,copies=0,resizes=0,releases=0;
    uint32_t heap_base=0,capacity=0,common_base=0,live_allocations=0;
};
class OwnedStringBinding {
public:
    bool bound()const noexcept{return bool(lease_);}
private:
    std::shared_ptr<WindowsStringLease> lease_;
    friend class WindowsOwnedStringArena;
};

// C++ transport copies share a lease. They are NOT source7581C copies.
// Source load/copy, assignment/move and release are explicit VM operations.
class WindowsOwnedStringArena {
public:
    explicit WindowsOwnedStringArena(WindowsStringPolicy policy={}) : policy_(policy) {}
    ~WindowsOwnedStringArena();
    WindowsOwnedStringArena(const WindowsOwnedStringArena&)=delete;
    WindowsOwnedStringArena& operator=(const WindowsOwnedStringArena&)=delete;
    void Restart()noexcept;
    OwnedStringBinding Create(const std::string& text);
    OwnedStringBinding Copy(const OwnedStringBinding&,const std::string& text)const;
    OwnedStringBinding Assign(const OwnedStringBinding& destination,const std::string& old_text,
                              const OwnedStringBinding& source,const std::string& text)const;
    OwnedStringBinding Resize(const OwnedStringBinding&,const std::string& old_text,const std::string& text)const;
    OwnedStringReceipt Observe(const OwnedStringBinding&,const std::string& text)const;
    OwnedStringArenaReceipt Snapshot()const;
    void Release(OwnedStringBinding&)const;
private:
    WindowsStringPolicy policy_;
    uint64_t epoch_=1;
    std::shared_ptr<WindowsStringState> state_;
    void Initialize();
};
} // namespace dokodemo::recovered
