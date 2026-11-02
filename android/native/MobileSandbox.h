#pragma once
#include <string>
struct ANativeWindow;
namespace sk {
class MobileSandbox;
MobileSandbox* createMobileSandbox(ANativeWindow*, const std::string& root, const std::string& shaders, float density, std::string& error);
int mobileSandboxFrame(MobileSandbox*, float x, float y, int clip, std::string& error);
void mobileSandboxTouch(MobileSandbox*, int action, float x, float y);
void destroyMobileSandbox(MobileSandbox*);
}
