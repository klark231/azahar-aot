#!/bin/sh
# Builds and runs the frame interpolation tests; needs only a C++20 compiler.
#   test.cpp          the portable core (matching + shader analysis), Zakuro's tests ported
#   driver_test.cpp   the Azahar-side driver, against stand-ins for the PICA types
#
# The sources under test come with patches/0007, so apply it to azahar/ first
# (scripts/apply-patches.sh), or point SRC at an azahar/src that has it.
set -e
cd "$(dirname "$0")"
SRC=${SRC:-../../azahar/src}
CORE=$SRC/video_core/frame_interp/frame_interp.cpp
DRIVER=$SRC/video_core/frame_interp/frame_interp_driver.cpp
if [ ! -f "$CORE" ]; then
    echo "$CORE not found: apply patches/0007 first (scripts/apply-patches.sh)" >&2
    exit 2
fi
${CXX:-g++} -std=c++20 -O1 -Wall -Wextra -I"$SRC" test.cpp "$CORE" -o /tmp/frame-interp-test
/tmp/frame-interp-test
${CXX:-g++} -std=c++20 -O1 -Wall -Wextra -Imock -I"$SRC" driver_test.cpp "$DRIVER" "$CORE" \
    -o /tmp/frame-interp-driver-test
/tmp/frame-interp-driver-test
