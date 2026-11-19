#include "retail_slot_lock.h"
#include <windows.h>
#include <stdexcept>
#include <string>

namespace dokodemo::gameplay {
RetailSlotLock::RetailSlotLock(const std::filesystem::path& directory) {
    // The slot name is fixed. The parent file identity also covers short names and case aliases.
    HANDLE parent=CreateFileW(directory.parent_path().c_str(),FILE_READ_ATTRIBUTES,
                              FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    if(parent==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open the savedata directory");
    BY_HANDLE_FILE_INFORMATION info{};
    const BOOL ok=GetFileInformationByHandle(parent,&info);
    CloseHandle(parent);
    if(!ok) throw std::runtime_error("Cannot identify the savedata directory");
    const auto name=L"Global\\DokodemoSlot-"+std::to_wstring(info.dwVolumeSerialNumber)+L"-"+
                    std::to_wstring(info.nFileIndexHigh)+L"-"+std::to_wstring(info.nFileIndexLow);
    // Object existence, not the event signal, owns the lock. Windows drops the last handle on process exit.
    HANDLE event=CreateEventW(nullptr,TRUE,FALSE,name.c_str());
    if(!event) throw std::runtime_error("Cannot acquire the savedata slot");
    if(GetLastError()==ERROR_ALREADY_EXISTS) {
        CloseHandle(event);
        throw std::runtime_error("The savedata slot is busy");
    }
    handle_=event;
}
RetailSlotLock::~RetailSlotLock() { CloseHandle(handle_); }
bool SlotReparsePoint(const std::filesystem::path& path) {
    const auto attr=GetFileAttributesW(path.c_str());
    return attr!=INVALID_FILE_ATTRIBUTES && (attr&FILE_ATTRIBUTE_REPARSE_POINT)!=0;
}
}
