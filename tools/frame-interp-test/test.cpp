// Tests for video_core/frame_interp (stage A of frame interpolation). The first nine are
// Zakuro's own tests (zakuro-gpu/src/interpolation.rs) ported over, with the same programs,
// numbers and expectations; the rest check the shader analysis on programs assembled by hand.
//
//   ./run.sh

#include <cmath>
#include <cstdio>
#include <optional>
#include <vector>

#include "video_core/frame_interp/frame_interp.h"

using namespace FrameInterp;

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond)                                                                         \
    do {                                                                                    \
        ++g_checks;                                                                         \
        if (!(cond)) {                                                                      \
            ++g_failures;                                                                   \
            std::printf("  FAILED %s:%d  %s\n", __FILE__, __LINE__, #cond);                \
        }                                                                                   \
    } while (0)
#define CHECK_NEAR(a, b) CHECK(std::fabs((a) - (b)) < 1e-6)

static Uniforms Unit(float x) {
    Uniforms u{};
    u[0] = {1.0f, 0.0f, 0.0f, x};
    u[1] = {0.0f, 1.0f, 0.0f, 2.0f};
    return u;
}

/// Runs one draw and copies what came back.
static std::optional<Uniforms> Run(FrameInterpolator& fi, uint32_t vertices, const Uniforms& u,
                                   bool geometry = false, const Program* program = nullptr,
                                   uint16_t outputs = 0, uint16_t coordinates = 0) {
    DrawDesc d;
    d.key = (uint64_t{vertices} << 1) | (geometry ? 1u : 0u);
    d.uniforms = &u;
    d.geometry = geometry;
    d.program = program;
    d.program_id = program ? 1 : 0;
    d.outputs = outputs;
    d.coordinates = coordinates;
    const Uniforms* r = fi.Draw(d);
    if (!r) {
        return std::nullopt;
    }
    return *r;
}

static bool Eq(const Vec4& a, const Vec4& b) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

// ---- Zakuro's tests --------------------------------------------------------------------------

static void DrawsMatchTheFrameBeforeInOrder() {
    std::puts("draws_match_the_frame_before_in_order");
    FrameInterpolator fi;
    CHECK(!Run(fi, 3, Unit(0.0f)));
    CHECK(!Run(fi, 3, Unit(4.0f)));
    CHECK(!Run(fi, 6, Unit(1.0f)));
    const Matches first = fi.FrameDone();
    CHECK(first == (Matches{3, 0, 0, 0, 0}));
    CHECK(!first.Smooth());

    auto between = Run(fi, 3, Unit(0.5f));
    CHECK(between.has_value());
    if (between) {
        CHECK(Eq((*between)[0], (Vec4{1.0f, 0.0f, 0.0f, 0.25f})));
        CHECK(Eq((*between)[1], (Vec4{0.0f, 1.0f, 0.0f, 2.0f})));
    }
    between = Run(fi, 3, Unit(4.2f));
    CHECK(between.has_value());
    if (between) {
        CHECK_NEAR((*between)[0][3], 4.1f);
    }
    CHECK(!Run(fi, 6, Unit(1.0f))); // stayed
    CHECK(!Run(fi, 9, Unit(1.0f))); // new
    const Matches second = fi.FrameDone();
    CHECK(second == (Matches{4, 3, 2, 0, 0}));
    CHECK(second.Smooth());
}

static void JumpsAreNotDrawnInBetween() {
    std::puts("jumps_are_not_drawn_in_between");
    FrameInterpolator fi;
    Run(fi, 3, Unit(100.0f));
    Run(fi, 4, Unit(0.0f));
    fi.FrameDone();
    CHECK(Run(fi, 3, Unit(120.0f)).has_value()); // a fifth of its distance
    CHECK(!Run(fi, 4, Unit(1.0f)));              // a flag flipped
    const Matches matches = fi.FrameDone();
    CHECK(matches == (Matches{2, 2, 1, 1, 0}));
    CHECK(!matches.Smooth());
    Run(fi, 3, Unit(200.0f));
    CHECK(fi.FrameDone().jumped == 1); // five sixths of its distance
}

static void GeometryShadersAreDrawnAsTheyAre() {
    std::puts("geometry_shaders_are_drawn_as_they_are");
    FrameInterpolator fi;
    Run(fi, 3, Unit(0.0f), true);
    fi.FrameDone();
    CHECK(!Run(fi, 3, Unit(0.2f), false)); // no geometry stage
    CHECK(!Run(fi, 3, Unit(0.2f), true));
    CHECK(fi.FrameDone() == (Matches{2, 1, 0, 0, 0}));
}

static void DrawsDrawnTheOtherWayRoundMatchTheNearest() {
    std::puts("draws_drawn_the_other_way_round_match_the_nearest");
    FrameInterpolator fi;
    Run(fi, 3, Unit(-3.7f));
    Run(fi, 3, Unit(3.7f));
    fi.FrameDone();
    auto right = Run(fi, 3, Unit(3.8f));
    CHECK(right.has_value());
    if (right) {
        CHECK_NEAR((*right)[0][3], 3.75f);
    }
    auto left = Run(fi, 3, Unit(-3.6f));
    CHECK(left.has_value());
    if (left) {
        CHECK_NEAR((*left)[0][3], -3.65f);
    }
    CHECK(fi.FrameDone() == (Matches{2, 2, 2, 0, 0}));
}

static void GoingOnAsBeforeMovesHoweverFast() {
    std::puts("going_on_as_before_moves_however_fast");
    FrameInterpolator fi;
    std::vector<bool> moved;
    for (float x : {-16.3f, -5.7f, 5.1f, 16.2f, 27.5f}) {
        moved.push_back(Run(fi, 3, Unit(x)).has_value());
        fi.FrameDone();
    }
    CHECK((moved == std::vector<bool>{false, false, true, true, true}));
}

static void DrawsThatKeepJumpingCountApart() {
    std::puts("draws_that_keep_jumping_count_apart");
    FrameInterpolator fi;
    const auto frame = [&](float batch, float model) {
        Run(fi, 3, Unit(batch));
        Run(fi, 4, Unit(model));
        return fi.FrameDone();
    };
    frame(0.0f, 0.0f);
    frame(5.0f, 0.1f);
    CHECK(frame(-5.0f, 0.2f) == (Matches{2, 2, 1, 0, 1}));
    const Matches m = frame(5.0f, 0.3f);
    CHECK(m.Smooth() && m.jumping == 1);
    CHECK(frame(-5.0f, 9.0f).jumped == 1); // the model jumped after moving

    // an animation stepping every other frame
    FrameInterpolator fi2;
    std::vector<std::pair<uint32_t, uint32_t>> jumps;
    for (float x : {0.0f, 0.0f, 5.0f, 5.0f, 10.0f, 10.0f, 15.0f}) {
        Run(fi2, 3, Unit(x));
        const Matches mm = fi2.FrameDone();
        jumps.emplace_back(mm.jumped, mm.jumping);
    }
    const std::vector<std::pair<uint32_t, uint32_t>> want{{0, 0}, {0, 0}, {1, 0}, {0, 0},
                                                          {0, 1}, {0, 0}, {0, 1}};
    CHECK(jumps == want);
}

// outputs o0 places the vertices, o2 carries texture coordinates
static constexpr uint16_t OUT_PLACING = 1 << 0;
static constexpr uint16_t OUT_COORDS = 1 << 2;

static void OnlyWhatPlacesTheVerticesMoves() {
    std::puts("only_what_places_the_vertices_moves");
    // o0 = c0 . v0, o2 = c1
    const uint32_t words[3] = {(0x02u << 26) | (0x20u << 12),
                               (0x13u << 26) | (0x02u << 21) | (0x21u << 12), 0x22u << 26};
    const auto program = Program::Decode(words, 3);
    const auto at = [](float x, float sprite) {
        Uniforms u{};
        u[0] = {1.0f, 0.0f, 0.0f, x};
        u[1] = {sprite, 0.0f, 0.0f, 0.0f};
        return u;
    };
    FrameInterpolator fi;
    const auto run = [&](const Uniforms& u) {
        return Run(fi, 3, u, false, program.get(), OUT_PLACING, OUT_COORDS);
    };
    run(at(0.0f, 0.25f));
    fi.FrameDone();
    auto between = run(at(0.4f, 0.5f));
    CHECK(between.has_value());
    if (between) {
        CHECK(Eq((*between)[0], (Vec4{1.0f, 0.0f, 0.0f, 0.2f})));
        CHECK(Eq((*between)[1], (Vec4{0.5f, 0.0f, 0.0f, 0.0f}))); // the next sprite, not half of each
    }
    fi.FrameDone();
    CHECK(!run(at(0.4f, 0.75f))); // only the sprite changed
    fi.FrameDone();
    CHECK(run(at(0.6f, 9.0f)).has_value()); // the sprite's jump does not count
}

static void SharedUniformsMoveButForRoundSteps() {
    std::puts("shared_uniforms_move_but_for_round_steps");
    // o0 = c[a0 + 20] . v0, o2 = c[a0 + 20]
    const uint32_t words[3] = {(0x02u << 26) | (1u << 19) | ((0x20u + 20) << 12),
                               (0x13u << 26) | (0x02u << 21) | (1u << 19) | ((0x20u + 20) << 12),
                               0x22u << 26};
    const auto program = Program::Decode(words, 3);
    const auto at = [](float x) {
        Uniforms u{};
        u[20] = {1.0f, 0.0f, 0.0f, x};
        return u;
    };
    FrameInterpolator fi;
    const auto run = [&](const Uniforms& u) {
        return Run(fi, 3, u, false, program.get(), OUT_PLACING, OUT_COORDS);
    };
    run(at(0.25f));
    fi.FrameDone();
    CHECK(!run(at(0.5f))); // a step between round values
    fi.FrameDone();
    run(at(0.3f));
    fi.FrameDone();
    auto between = run(at(0.4f));
    CHECK(between.has_value());
    if (between) {
        CHECK_NEAR((*between)[20][3], 0.35f);
    }
}

static void ASharedUniformThatJumpsStaysAsTheFrameHasIt() {
    std::puts("a_shared_uniform_that_jumps_stays_as_the_frame_has_it");
    // o0.x = c0 . v0, o0.y = c[a0 + 20] . v0, o2 = c[a0 + 20]
    const uint32_t words[4] = {(0x02u << 26) | (0x20u << 12),
                               (0x02u << 26) | (1u << 19) | ((0x20u + 20) << 12) | 1,
                               (0x13u << 26) | (0x02u << 21) | (1u << 19) | ((0x20u + 20) << 12),
                               0x22u << 26};
    const auto program = Program::Decode(words, 4);
    const auto at = [](float x, float shared) {
        Uniforms u{};
        u[0] = {1.0f, 0.0f, 0.0f, x};
        u[20] = {shared, 0.0f, 0.0f, 0.0f};
        return u;
    };
    FrameInterpolator fi;
    const auto run = [&](const Uniforms& u) {
        return Run(fi, 3, u, false, program.get(), OUT_PLACING, OUT_COORDS);
    };
    run(at(0.0f, 0.3f));
    fi.FrameDone();
    run(at(0.1f, 5.0f));
    fi.FrameDone();
    auto between = run(at(0.2f, -5.0f));
    CHECK(between.has_value());
    if (between) {
        CHECK_NEAR((*between)[0][3], 0.15f);
        CHECK((*between)[20][0] == -5.0f); // the shared one as the frame has it
    }
    CHECK(fi.FrameDone().jumped == 0);
    CHECK(!run(at(0.2f, 6.0f))); // only the shared one changed, and jumped
}

// ---- Shader analysis on hand-assembled programs ---------------------------------------------

static Mask96 Bits(std::initializer_list<int> list) {
    Mask96 m;
    for (int i : list) m |= Mask96::Bit(i);
    return m;
}

static void FeedingFollowsTemporaries() {
    std::puts("feeding_follows_temporaries");
    // r0 = c3 * v0 ; o0 = r0 ; o1 = c9 (not asked for)
    const uint32_t words[4] = {
        (0x08u << 26) | (0x10u << 21) | (0x23u << 12),             // MUL r0, c3, v0
        (0x13u << 26) | (0x00u << 21) | (0x10u << 12),             // MOV o0, r0
        (0x13u << 26) | (0x01u << 21) | (0x29u << 12),             // MOV o1, c9
        0x22u << 26,                                               // END
    };
    const auto p = Program::Decode(words, 4);
    const auto [direct, indexed] = p->UniformsFeeding(0, 1u << 0);
    CHECK(direct == Bits({3}));
    CHECK(!indexed.Any());
    const auto [d1, i1] = p->UniformsFeeding(0, 1u << 1);
    CHECK(d1 == Bits({9}));
}

static void FeedingIgnoresWhatIsNotReached() {
    std::puts("feeding_ignores_what_is_not_reached");
    // CALL 5 (1 instruction) ; END ; (zeros that would read c0) ; 5: MOV o0, c4
    uint32_t words[8] = {};
    words[0] = (0x24u << 26) | (5u << 10) | 1;
    words[1] = 0x22u << 26;
    words[5] = (0x13u << 26) | (0x00u << 21) | (0x24u << 12);
    const auto p = Program::Decode(words, 8);
    const auto [direct, indexed] = p->UniformsFeeding(0, 1u << 0);
    CHECK(direct == Bits({4})); // not c0, which the unreached zero words would read
    CHECK(!indexed.Any());
}

static void FeedingTakesBothSidesOfAnIf() {
    std::puts("feeding_takes_both_sides_of_an_if");
    // IFU b0: then (1 instruction at 1) / else at 2 (1 instruction) ; END
    //   0: IFU dest=2 count=1     1: MOV o0, c1     2: MOV o0, c2     3: END
    uint32_t words[4] = {};
    words[0] = (0x27u << 26) | (2u << 10) | 1;
    words[1] = (0x13u << 26) | (0x00u << 21) | (0x21u << 12);
    words[2] = (0x13u << 26) | (0x00u << 21) | (0x22u << 12);
    words[3] = 0x22u << 26;
    const auto p = Program::Decode(words, 4);
    const auto [direct, indexed] = p->UniformsFeeding(0, 1u << 0);
    CHECK(direct == Bits({1, 2}));
}

static void MultiplyAddUsesItsOwnEncoding() {
    std::puts("multiply_add_uses_its_own_encoding");
    // r0 = c7 ; o0 = v0 * c5 + r0   (MAD: src2 is the wide one)
    uint32_t words[3] = {};
    words[0] = (0x13u << 26) | (0x10u << 21) | (0x27u << 12);
    words[1] = (7u << 29) | (0u << 24) | (0u << 17) | (0x25u << 10) | (0x10u << 5);
    words[2] = 0x22u << 26;
    const auto p = Program::Decode(words, 3);
    const auto [direct, indexed] = p->UniformsFeeding(0, 1u << 0);
    CHECK(direct == Bits({5, 7}));
    CHECK(!indexed.Any());
}

static void InvertedFormsHaveTheWideSourceSecond() {
    std::puts("inverted_forms_have_the_wide_source_second");
    // DPHI o0, v0, c2     then with the address register: DPHI o0, v0, c[a0 + 2]
    uint32_t plain[2] = {(0x18u << 26) | (0u << 21) | (0u << 14) | (0x22u << 7), 0x22u << 26};
    const auto p = Program::Decode(plain, 2);
    const auto [direct, indexed] = p->UniformsFeeding(0, 1u << 0);
    CHECK(direct == Bits({2}));
    uint32_t idx[2] = {(0x18u << 26) | (0u << 21) | (1u << 19) | (0u << 14) | (0x22u << 7),
                       0x22u << 26};
    const auto q = Program::Decode(idx, 2);
    const auto [d2, i2] = q->UniformsFeeding(0, 1u << 0);
    CHECK(!d2.Any());
    CHECK(i2 == Mask96::From(2));
}

static void MaskBasics() {
    std::puts("mask_basics");
    CHECK(Mask96::From(90) == Bits({90, 91, 92, 93, 94, 95}));
    CHECK(Mask96::Bit(63).Test(63) && !Mask96::Bit(63).Test(64));
    CHECK(Mask96::Bit(64).Test(64) && Mask96::Bit(95).Test(95));
    CHECK((~Mask96{}) == Mask96::All());
    CHECK(!(Mask96::All() & ~Mask96::All()).Any());
}

int main() {
    DrawsMatchTheFrameBeforeInOrder();
    JumpsAreNotDrawnInBetween();
    GeometryShadersAreDrawnAsTheyAre();
    DrawsDrawnTheOtherWayRoundMatchTheNearest();
    GoingOnAsBeforeMovesHoweverFast();
    DrawsThatKeepJumpingCountApart();
    OnlyWhatPlacesTheVerticesMoves();
    SharedUniformsMoveButForRoundSteps();
    ASharedUniformThatJumpsStaysAsTheFrameHasIt();
    FeedingFollowsTemporaries();
    FeedingIgnoresWhatIsNotReached();
    FeedingTakesBothSidesOfAnIf();
    MultiplyAddUsesItsOwnEncoding();
    InvertedFormsHaveTheWideSourceSecond();
    MaskBasics();
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
