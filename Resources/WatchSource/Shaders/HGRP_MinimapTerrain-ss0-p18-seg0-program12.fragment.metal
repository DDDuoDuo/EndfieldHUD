#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _6
{
    float4 _m0;
    float4 _m1;
    float4 _m2;
    float4 _m3;
    float4 _m4;
    float4 _m5;
    float _m6;
    float _m7;
    float _m8;
    float4x4 _m9;
};

struct _8
{
    float _m0;
    float _m1;
    float _m2;
    float _m3;
    float _m4;
    float _m5;
    float _m6;
    float _m7;
    float4 _m8;
};

struct main0_out
{
    float4 m_5 [[color(0)]];
};

struct main0_in
{
    float4 m_3 [[user(locn0)]];
    float4 m_4 [[user(locn5)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant _6& _7 [[buffer(0)]], constant _8& _9 [[buffer(1)]])
{
    main0_out out = {};
    float2 _40 = in.m_4.xy / float2(in.m_4.w);
    if (isnan(_40.x) || isnan(_40.y))
    {
        discard_fragment();
    }
    float _65 = (_9._m5 != 0.0) ? 1.0 : smoothstep(_9._m6, _9._m7, distance((_7._m9 * float4(in.m_3.xyz, 1.0)).xz, float2(0.0)));
    if (!(_65 != 0.0))
    {
        discard_fragment();
    }
    float _72 = _65 * _9._m2;
    float4 _73 = float4(1.0);
    _73.w = _72;
    float3 _75 = _73.xyz * _72;
    out.m_5 = float4(_75.x, _75.y, _75.z, _73.w);
    return out;
}

