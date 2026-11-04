#include "Check.h"
#include "post/TimestampMath.h"
#include <cstdint>
int main() {
    SK_TEST("timestamp_math_test");
    CHECK_NEAR(sk::timestampMilliseconds(100, 1100, 64, 2.0), .002, 1e-10);
    CHECK_NEAR(sk::timestampMilliseconds(250, 5, 8, 1000.0), .011, 1e-10);
    CHECK_NEAR(sk::timestampMilliseconds(UINT64_MAX - 4, 5, 64, 1000.0), .010, 1e-10);
    CHECK(sk::timestampMilliseconds(0, 1, 0, 1.0) < 0);
    CHECK(sk::timestampMilliseconds(0, 1, 65, 1.0) < 0);
    CHECK(sk::timestampMilliseconds(0, 1, 64, 0.0) < 0);
    return t_.finish();
}
