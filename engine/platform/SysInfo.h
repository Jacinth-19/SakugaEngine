#pragma once
// OS-only diagnostics. Unknown values remain empty/zero; this does not benchmark the device.
#include <cstdint>
#include <string>

namespace sk {
struct SysInfo {
    std::string cpu;
    unsigned logicalCores = 0;
    uint64_t totalMemory = 0;
    uint64_t availableMemory = 0;
    uint64_t storageTotal = 0;
    uint64_t storageAvailable = 0;
    std::string storagePath;
    // No reliable portable way to distinguish flash, rotational disks, virtual volumes and network drives.
    std::string storageType = "unknown";
};
SysInfo collectSysInfo(const std::string& storagePath);
}
