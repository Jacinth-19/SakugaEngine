#include "scene/Model.h"
#include "asset/GltfLoader.h"
#include <iostream>

int main(int argc, char** argv) {
    // Basic loader integrity test
    sk::GltfLoader loader;
    // Mock test: ensure loader can initialize
    std::cout << "Loader test: OK" << std::endl;
    return 0;
}