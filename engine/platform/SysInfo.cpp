#include "platform/SysInfo.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <cstdlib>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace sk {
SysInfo collectSysInfo(const std::string& path) {
    SysInfo result;
    result.storagePath = path;
    result.logicalCores = std::thread::hardware_concurrency();
#ifdef _WIN32
    HKEY key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        char value[512] = {}; DWORD size = sizeof(value), type = 0;
        if (RegQueryValueExA(key, "ProcessorNameString", nullptr, &type, reinterpret_cast<BYTE*>(value), &size) == ERROR_SUCCESS && type == REG_SZ)
            { value[sizeof(value) - 1] = '\0'; result.cpu = value; }
        RegCloseKey(key);
    }
    MEMORYSTATUSEX memory{}; memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) { result.totalMemory = memory.ullTotalPhys; result.availableMemory = memory.ullAvailPhys; }
#elif defined(__linux__)
    std::ifstream cpu("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpu, line)) {
        auto colon = line.find(':');
        if (colon != std::string::npos && (line.substr(0, colon).find("model name") != std::string::npos || line.substr(0, colon).find("Hardware") != std::string::npos)) {
            result.cpu = line.substr(colon + 1);
            const auto first = result.cpu.find_first_not_of(" \t");
            result.cpu = first == std::string::npos ? "" : result.cpu.substr(first);
            break;
        }
    }
    std::ifstream memory("/proc/meminfo");
    std::string key, unit; uint64_t value;
    while (memory >> key >> value >> unit) {
        if (key == "MemTotal:") result.totalMemory = value * 1024;
        else if (key == "MemAvailable:") result.availableMemory = value * 1024;
    }
#endif
    std::error_code ec;
    const auto space = std::filesystem::space(path, ec);
    if (!ec) { result.storageTotal = space.capacity; result.storageAvailable = space.available; }
    return result;
}
}
