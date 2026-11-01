#pragma once
#include <string>
#include <vector>
#include <cstdint>
namespace sk {
bool readFile(const std::string& path, std::vector<uint8_t>& out);
bool readTextFile(const std::string& path, std::string& out);
// Locates the project root (a directory containing "assets/") by walking up from the executable / cwd.
std::string findRoot(const char* argv0);
std::string exeDir(const char* argv0);
}
