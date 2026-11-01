#include "platform/SysInfo.h"
#include "Check.h"
#include <filesystem>
#include <iostream>

int main() {
    SK_TEST("sys_info_test");
    const auto info = sk::collectSysInfo(std::filesystem::current_path().string());
    CHECK(info.storageTotal > 0);
    CHECK(info.storageAvailable <= info.storageTotal);
    CHECK(info.storageType == "unknown");
    CHECK(info.totalMemory == 0 || info.availableMemory <= info.totalMemory);
    const auto missing = sk::collectSysInfo((std::filesystem::temp_directory_path() / "sakuga-nonexistent-volume-test" / "missing").string());
    CHECK(missing.storageTotal == 0);
    CHECK(missing.storageAvailable == 0);
    return t_.finish();
}
