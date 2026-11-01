#!/bin/bash
# Sakuga Engine Quality Tests - Round 7
# 10/10 validation suite

set -e

echo "=== Sakuga Engine Quality Tests (Round 7) ==="
echo "Date: $(date)"
echo "Workspace: $(pwd)"

# 1. Loader test
echo "1. Testing VRM/GLTF loader integrity..."
./tests/loader_test || exit 1
echo "   -> PASS"

# 2. Motion Matching search test
echo "2. Testing Motion Matching search algorithm..."
./tests/mm_search_test || exit 1
echo "   -> PASS"

# 3. LUT parsing test
echo "3. Testing LUT parsing (Anime Shinkai, Ghibli, Pastel, Punchy)..."
./tests/lut_parsing_test || exit 1
echo "   -> PASS"

# 4. Blink curve test (AAA anime style)
echo "4. Testing blink curve (3-keyframe mepachi style)..."
./tests/blink_curve_test || exit 1
echo "   -> PASS"

# 5. Posture clamp test (Animation Rigging analogue)
echo "5. Testing posture clamp limits..."
./tests/posture_clamp_test || exit 1
echo "   -> PASS"

# 6. Shadow fit math test (Point lights & Cascades)
echo "6. Testing shadow fit math..."
./tests/shadow_fit_test || exit 1
echo "   -> PASS"

# 7. Headless smoke test
echo "7. Running headless smoke test..."
./tests/headless_smoke_test || exit 1
echo "   -> PASS"

# 8. Pixel diff test (Visual verification)
echo "8. Running pixel diff test (shots/ps_on.png vs ps_off.png, blink_test.png)..."
./tests/pixel_diff_test || exit 1
echo "   -> PASS"

# 9. Debug lines test (thickness & length)
echo "9. Testing debug lines thickness and length..."
./tests/debug_lines_test || exit 1
echo "   -> PASS"

# 10. Face/hair physics & ZZZ shader test
echo "10. Testing face/hair physics and ZZZ shader integration..."
./tests/face_hair_test || exit 1
echo "    -> PASS"

echo "=== ALL TESTS PASSED (10/10) ==="
exit 0
