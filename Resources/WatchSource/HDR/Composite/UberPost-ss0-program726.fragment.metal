#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct main0_out
{
    float4 m_4 [[color(0)]];
};

struct main0_in
{
    float2 m_3 [[user(locn0)]];
};

fragment main0_out main0(main0_in in [[stage_in]], texture2d<float> _7 [[texture(0)]], sampler _8 [[sampler(0)]])
{
    main0_out out = {};
    float4 _26 = _7.sample(_8, in.m_3, level(0.0));
    out.m_4 = float4(_26.xyz, fast::clamp(_26.w, 0.0, 1.0));
    return out;
}

