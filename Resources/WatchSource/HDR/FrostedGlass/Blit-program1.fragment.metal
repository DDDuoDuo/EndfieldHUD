#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _8
{
    float4 _m0;
    float4 _m1;
    float _m2;
    packed_float2 _m3;
    uint _m4;
    int _m5;
};

struct main0_out
{
    float4 m_3 [[color(0)]];
};

struct main0_in
{
    float2 m_2 [[user(locn0)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant _8& _9 [[buffer(0)]], texture2d<float> _6 [[texture(0)]], sampler _7 [[sampler(0)]])
{
    main0_out out = {};
    out.m_3 = _6.sample(_7, in.m_2, level(_9._m2));
    return out;
}

