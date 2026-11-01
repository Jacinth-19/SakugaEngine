#pragma once
// Minimal assertion + reporting helpers for the GPU-free test executables.
// Every test links only the parts of the engine that do not need a Vulkan device, so the whole suite runs in CI/containers.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace sktest {

struct Checker {
    const char* suite = "";
    int checks = 0;
    int failures = 0;

    explicit Checker(const char* name) : suite(name) {}

    void check(bool ok, const char* expr, const char* file, int line, const std::string& extra = {}) {
        ++checks;
        if (ok) return;
        ++failures;
        std::printf("  FAIL  %s:%d  %s%s%s\n", file, line, expr, extra.empty() ? "" : "  (", extra.c_str());
        if (!extra.empty()) std::printf("        )\n");
    }

    void near(double got, double want, double tol, const char* expr, const char* file, int line) {
        bool ok = std::isfinite(got) && std::fabs(got - want) <= tol;
        char extra[160];
        std::snprintf(extra, sizeof(extra), "got %.9g want %.9g +/- %g", got, want, tol);
        check(ok, expr, file, line, extra);
    }

    void finite(double v, const char* expr, const char* file, int line) {
        char extra[64];
        std::snprintf(extra, sizeof(extra), "got %.9g", v);
        check(std::isfinite(v), expr, file, line, extra);
    }

    int finish() const {
        if (failures == 0) {
            std::printf("%s: OK (%d checks)\n", suite, checks);
            return 0;
        }
        std::printf("%s: FAILED (%d/%d checks failed)\n", suite, failures, checks);
        return 1;
    }
};

}  // namespace sktest

#define SK_TEST(name) sktest::Checker t_(name)
#define CHECK(cond) t_.check((cond), #cond, __FILE__, __LINE__)
#define CHECK_MSG(cond, msg) t_.check((cond), #cond, __FILE__, __LINE__, (msg))
#define CHECK_NEAR(got, want, tol) t_.near((double)(got), (double)(want), (double)(tol), #got " ~= " #want, __FILE__, __LINE__)
#define CHECK_FINITE(v) t_.finite((double)(v), #v, __FILE__, __LINE__)
#define SK_TEST_MAIN_BEGIN(name) int main() { SK_TEST(name); {
#define SK_TEST_MAIN_END() } return t_.finish(); }
