#pragma once
#include <string>
struct ANativeWindow;
namespace sk {
class AndroidRenderSession;
AndroidRenderSession* createAndroidRenderSession(ANativeWindow* window, const std::string& shaderDir, std::string& error);
int renderAndroidFrame(AndroidRenderSession*, const float* joints, int count, std::string& error);
void destroyAndroidRenderSession(AndroidRenderSession*);
}
