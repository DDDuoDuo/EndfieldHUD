#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _9
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

struct _11
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
    float4 _m9;
    float4 _m10;
};

struct main0_out
{
    float4 m_6 [[color(0)]];
};

struct main0_in
{
    float4 m_3 [[user(locn0)]];
    float2 m_4 [[user(locn1)]];
    float4 m_5 [[user(locn5)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant _9& _10 [[buffer(0)]], constant _11& _12 [[buffer(1)]], texture2d<float> _14 [[texture(0)]], sampler _15 [[sampler(0)]])
{
    main0_out out = {};
    float2 _57 = in.m_5.xy / float2(in.m_5.w);
    if (isnan(_57.x) || isnan(_57.y))
    {
        discard_fragment();
    }
    float4 _68 = _14.sample(_15, in.m_4);
    float _70 = _68.w;
    float3 _87 = (((_68.xyz * _70) * _12._m8.xyz) + (_12._m9.xyz * fast::clamp(_70 - _68.x, 0.0, 1.0))).xyz * _12._m1;
    float4 _88 = float4(_87.x, _87.y, _87.z, float4(1.0).w);
    float _106 = (_12._m5 != 0.0) ? 1.0 : smoothstep(_12._m6, _12._m7, distance((_10._m9 * float4(in.m_3.xyz, 1.0)).xz, float2(0.0)));
    if (!(_106 != 0.0))
    {
        discard_fragment();
    }
    float _114 = _70 * (_106 * _12._m2);
    _88.w = _114;
    float3 _117 = _88.xyz * _114;
    out.m_6 = float4(_117.x, _117.y, _117.z, _88.w);
    return out;
}

