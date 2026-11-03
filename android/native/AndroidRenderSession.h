#pragma once
#include <string>
#include <vector>
struct ANativeWindow;
namespace sk {
class AndroidRenderSession;
AndroidRenderSession* createAndroidRenderSession(ANativeWindow* window, const std::string& shaderDir, const std::string& modelPath, const std::vector<std::string>& motionPaths, float density, std::string& error);
int renderAndroidFrame(AndroidRenderSession*, float moveX, float moveY, int clip, std::string& error);
void pauseAndroidRenderSession(AndroidRenderSession*);
void resumeAndroidRenderSession(AndroidRenderSession*);
void touchAndroidRenderSession(AndroidRenderSession*, bool imgui, int action, int id, float x, float y);
void destroyAndroidRenderSession(AndroidRenderSession*);
}
