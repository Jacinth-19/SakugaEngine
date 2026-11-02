#include "Check.h"
#include "gfx/SurfaceChoices.h"
int main() {
    SK_TEST("surface_choices_test");
    CHECK(sk::preferredSampleCount(7, 7) == 4);
    CHECK(sk::preferredSampleCount(7, 3) == 2);
    CHECK(sk::preferredSampleCount(5, 3) == 1);
    CHECK(sk::preferredSampleCount(4, 2) == 0);
    CHECK(sk::preferredCompositeAlpha(15) == 1);
    CHECK(sk::preferredCompositeAlpha(14) == 2);
    CHECK(sk::preferredCompositeAlpha(12) == 4);
    CHECK(sk::preferredCompositeAlpha(8) == 8);
    CHECK(sk::preferredCompositeAlpha(0) == 0);
    return t_.finish();
}
