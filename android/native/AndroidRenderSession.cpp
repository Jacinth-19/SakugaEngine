#include "AndroidRenderSession.h"
#include "MobileSandbox.h"
#include <filesystem>
#include <memory>

namespace sk {
class AndroidRenderSession {
public:
    MobileSandbox* sandbox = nullptr;
    ~AndroidRenderSession() { destroyMobileSandbox(sandbox); }
};
AndroidRenderSession* createAndroidRenderSession(ANativeWindow* window, const std::string& shaderDir, const std::string& modelPath,
                                                  const std::vector<std::string>&, float density, std::string& error) {
    auto s = std::make_unique<AndroidRenderSession>();
    // Model lives at <root>/assets/models/AvatarSample.vrm; all sandbox assets use the same root.
    const auto root = std::filesystem::path(modelPath).parent_path().parent_path().parent_path().string();
    s->sandbox = createMobileSandbox(window, root, shaderDir, density, error);
    return s->sandbox ? s.release() : nullptr;
}
int renderAndroidFrame(AndroidRenderSession* s, float x, float y, int clip, std::string& error) {
    return s && s->sandbox ? mobileSandboxFrame(s->sandbox, x, y, clip, error) : -1;
}
void touchAndroidRenderSession(AndroidRenderSession* s, bool imgui, int action, int id, float x, float y) {
    if (s && s->sandbox) mobileSandboxTouch(s->sandbox, imgui, action, id, x, y);
}
void destroyAndroidRenderSession(AndroidRenderSession* session) { delete session; }
}
