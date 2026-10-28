// Test stand-in for the parts of Azahar's PicaCore the frame interpolation driver reads, with
// the same names and shapes (regs.internal.*, vs_setup, gs_setup).
#pragma once
#include <array>
#include "common/common_types.h"
namespace Pica {
constexpr u32 MAX_PROGRAM_CODE_LENGTH = 4096;
struct F24 {
    float v = 0;
    float ToFloat32() const { return v; }
};
struct Vec4F24 {
    F24 x, y, z, w;
};
struct Uniforms {
    std::array<Vec4F24, 96> f;
};
struct ShaderSetup {
    Uniforms uniforms;
    std::array<u32, MAX_PROGRAM_CODE_LENGTH> code{};
    u32 biggest = 0;
    u32 entry_point = 0;
    u64 GetProgramCodeHash() {
        u64 h = 1469598103934665603ull;
        for (u32 w : code) h = (h ^ w) * 1099511628211ull;
        return h;
    }
    u64 GetSwizzleDataHash() { return 0; }
    u32 GetBiggestProgramSize() const { return biggest; }
    const std::array<u32, MAX_PROGRAM_CODE_LENGTH>& GetProgramCode() const { return code; }
};
struct RasterizerRegs {
    u32 vs_output_total = 0;
    struct { u32 raw = 0; } vs_output_attributes[7];
};
struct ShaderRegs {
    u32 output_mask = 0;
};
struct PipelineRegs {
    enum class UseGS : u32 { No = 0, Yes = 2 };
    enum class GSMode : u32 { Point = 0, VariablePrim = 1, FixedPrim = 2 };
    enum class TriangleTopology : u32 { List = 0, Strip = 1, Fan = 2, Shader = 3 };
    struct GSConfig {
        GSMode mode = GSMode::Point;
    };
    UseGS use_gs = UseGS::No;
    GSConfig gs_config;
    TriangleTopology triangle_topology = TriangleTopology::List;
};
struct RegsInternal {
    static constexpr std::size_t NUM_REGS = 0x300;
    std::array<u32, NUM_REGS> reg_array{};
    RasterizerRegs rasterizer;
    ShaderRegs vs;
    PipelineRegs pipeline;
};
struct Regs {
    RegsInternal internal;
};
class PicaCore {
public:
    Regs regs;
    ShaderSetup vs_setup;
    ShaderSetup gs_setup;
};
} // namespace Pica
