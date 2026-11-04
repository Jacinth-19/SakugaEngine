#include "Check.h"
#include "asset/FbxInfo.h"
int main() {
    SK_TEST("fbx_info_test"); sk::FbxInfo info; std::string error;
    CHECK_MSG(sk::inspectFbx(std::string(SAKUGA_ROOT) + "/tests/data/triangle.fbx", info, error), error);
    CHECK(info.meshes == 1); CHECK(info.triangles == 1); CHECK(info.skins == 0);
    CHECK(!sk::inspectFbx(std::string(SAKUGA_ROOT) + "/tests/data/does-not-exist.fbx", info, error));
    CHECK(!error.empty()); CHECK(info.meshes == 0);
    return t_.finish();
}
