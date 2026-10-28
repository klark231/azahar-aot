// Tests the Azahar-side driver (frame_interp_driver.cpp) against stand-ins for the PICA types,
// to check its own logic: output-map decoding, draw keys, and the whole path from registers to
// the log line. What it cannot check is that the real Azahar headers have these names; that is
// for the build.

#include <cstdio>
#include <string>

#include "common/logging/log.h"
#include "video_core/frame_interp/frame_interp_driver.h"
#include "video_core/pica/pica_core.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::printf("  FAILED %s:%d  %s\n", __FILE__, __LINE__, #cond);     \
        }                                                                        \
    } while (0)

using namespace FrameInterp;

static void SetOutputMap(Pica::RegsInternal& regs, u32 total, u32 a0, u32 a1, u32 a2, u32 mask) {
    regs.rasterizer.vs_output_total = total;
    regs.rasterizer.vs_output_attributes[0].raw = a0;
    regs.rasterizer.vs_output_attributes[1].raw = a1;
    regs.rasterizer.vs_output_attributes[2].raw = a2;
    regs.vs.output_mask = mask;
}

static void OutputMapDecoding() {
    std::puts("output_map_decoding");
    Pica::RegsInternal regs;
    u16 placing = 0, coords = 0;
    // Zakuro's layout: attribute 0 position, attribute 2 texcoord0, all registers enabled
    SetOutputMap(regs, 3, 0x03020100, 0x0B0A0908, 0x1F1F0D0C, 0);
    PlacingOutputs(regs, placing, coords);
    CHECK(placing == (1 << 0));
    CHECK(coords == (1 << 2));
    // the output mask enables o0, o1, o4, o5: attribute 2 is the third enabled one, o4
    SetOutputMap(regs, 3, 0x03020100, 0x0B0A0908, 0x1F1F0D0C, 0b110011);
    PlacingOutputs(regs, placing, coords);
    CHECK(placing == (1 << 0));
    CHECK(coords == (1 << 4));
    // quaternion and view place the vertices too
    SetOutputMap(regs, 3, 0x03020100, 0x07060504, 0x1F141312, 0);
    PlacingOutputs(regs, placing, coords);
    CHECK(placing == ((1 << 0) | (1 << 1) | (1 << 2)));
    CHECK(coords == 0);
    // a map that says nothing: position in o0, texcoord0 in the fourth attribute
    SetOutputMap(regs, 0, 0, 0, 0, 0);
    PlacingOutputs(regs, placing, coords);
    CHECK(placing == (1 << 0));
    CHECK(coords == (1 << 3));
    // texcoords 1 and 2
    SetOutputMap(regs, 3, 0x03020100, 0x1F1F0F0E, 0x17161F1F, 0);
    PlacingOutputs(regs, placing, coords);
    CHECK(coords == ((1 << 1) | (1 << 2)));
}

/// o0 = c0 . v0 ; o2 = c1 ; end
static void FillProgram(Pica::ShaderSetup& s) {
    s.code[0] = (0x02u << 26) | (0x20u << 12);
    s.code[1] = (0x13u << 26) | (0x02u << 21) | (0x21u << 12);
    s.code[2] = 0x22u << 26;
    s.biggest = 3;
}

static void FollowsAMovingMatrix() {
    std::puts("follows_a_moving_matrix");
    MockLog().clear();
    Pica::PicaCore pica;
    FillProgram(pica.vs_setup);
    SetOutputMap(pica.regs.internal, 3, 0x03020100, 0x0B0A0908, 0x1F1F0D0C, 0);
    pica.regs.internal.reg_array[0x228] = 36; // vertex count
    Driver driver{std::chrono::milliseconds(0)}; // report at every frame

    for (int frame = 0; frame < 4; ++frame) {
        pica.vs_setup.uniforms.f[0] = {{1.0f}, {0.0f}, {0.0f}, {0.1f * frame}};
        pica.vs_setup.uniforms.f[1] = {{0.5f}, {0.0f}, {0.0f}, {0.0f}}; // a texture offset
        driver.OnDraw(pica, true);
        driver.OnDraw(pica, false); // shaded on the CPU, only counted
        driver.OnFrameBoundary();
        driver.OnFrameBoundary(); // a refresh the game drew nothing in: not a frame
    }
    CHECK(MockLog().size() == 4); // one report per game frame, none for the idle refreshes
    if (MockLog().size() == 4) {
        const std::string& first = MockLog()[0];
        const std::string& last = MockLog()[3];
        CHECK(first.find("0% matched") != std::string::npos);
        CHECK(last.find("50% matched") != std::string::npos);  // one of two draws is on the CPU
        CHECK(last.find("50% moved smoothly") != std::string::npos);
        CHECK(last.find("(0.5 on the CPU") == std::string::npos); // counts shown per frame
        CHECK(last.find("1.0 on the CPU") != std::string::npos);
        std::printf("  e.g. %s\n", last.c_str());
    }
}

static void ADifferentModelDoesNotMatch() {
    std::puts("a_different_model_does_not_match");
    MockLog().clear();
    Pica::PicaCore pica;
    FillProgram(pica.vs_setup);
    SetOutputMap(pica.regs.internal, 3, 0x03020100, 0x0B0A0908, 0x1F1F0D0C, 0);
    Driver driver{std::chrono::milliseconds(0)};
    for (int frame = 0; frame < 3; ++frame) {
        // the vertex array moves in memory every frame, as a streamed buffer would
        pica.regs.internal.reg_array[0x200] = 0x1000 + frame * 0x100;
        pica.vs_setup.uniforms.f[0] = {{1.0f}, {0.0f}, {0.0f}, {0.1f * frame}};
        driver.OnDraw(pica, true);
        driver.OnFrameBoundary();
    }
    CHECK(MockLog().size() == 3);
    if (MockLog().size() == 3) {
        CHECK(MockLog()[2].find("0% matched") != std::string::npos);
    }
}

static void ACameraCutIsNotSmooth() {
    std::puts("a_camera_cut_is_not_smooth");
    MockLog().clear();
    Pica::PicaCore pica;
    FillProgram(pica.vs_setup);
    SetOutputMap(pica.regs.internal, 3, 0x03020100, 0x0B0A0908, 0x1F1F0D0C, 0);
    Driver driver{std::chrono::milliseconds(0)};
    const float xs[] = {0.0f, 0.1f, 0.2f, 50.0f};
    for (float x : xs) {
        pica.vs_setup.uniforms.f[0] = {{1.0f}, {0.0f}, {0.0f}, {x}};
        driver.OnDraw(pica, true);
        driver.OnFrameBoundary();
    }
    CHECK(MockLog().size() == 4);
    if (MockLog().size() == 4) {
        CHECK(MockLog()[3].find("100% jumped") == std::string::npos);
        CHECK(MockLog()[3].find("0% of frames smooth") != std::string::npos);
        std::printf("  e.g. %s\n", MockLog()[3].c_str());
    }
}

int main() {
    OutputMapDecoding();
    FollowsAMovingMatrix();
    ADifferentModelDoesNotMatch();
    ACameraCutIsNotSmooth();
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
