#pragma once
#include <cstdio>
#include <cstdlib>
// Tiny logging helpers. SK_CHECK aborts with a message (used for unrecoverable setup errors).
#define SK_LOG(...)  do { std::fprintf(stdout, "[sakuga] " __VA_ARGS__); std::fprintf(stdout, "\n"); std::fflush(stdout);} while (0)
#define SK_WARN(...) do { std::fprintf(stderr, "[sakuga:warn] " __VA_ARGS__); std::fprintf(stderr, "\n"); } while (0)
#define SK_ERR(...)  do { std::fprintf(stderr, "[sakuga:error] " __VA_ARGS__); std::fprintf(stderr, "\n"); } while (0)
#define SK_FATAL(...) do { SK_ERR(__VA_ARGS__); std::abort(); } while (0)
#define SK_CHECK(cond, ...) do { if (!(cond)) { SK_ERR("check failed: " #cond " -- " __VA_ARGS__); std::abort(); } } while (0)
