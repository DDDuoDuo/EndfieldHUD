#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _7
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

struct _9
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
    float _m9;
    float _m10;
    float _m11;
    float4 _m12;
};

struct main0_out
{
    float4 m_6 [[color(0)]];
};

struct main0_in
{
    float4 m_3 [[user(locn0)]];
    float4 m_4 [[user(locn3)]];
    float4 m_5 [[user(locn5)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant _7& _8 [[buffer(0)]], constant _9& _10 [[buffer(1)]])
{
    main0_out out = {};
    float2 _47 = in.m_5.xy / float2(in.m_5.w);
    if (isnan(_47.x) || isnan(_47.y))
    {
        discard_fragment();
    }
    float3 _60 = _10._m8.xyz * _10._m1;
    float4 _61 = float4(_60.x, _60.y, _60.z, float4(1.0).w);
    float _85 = (_10._m5 != 0.0) ? 1.0 : smoothstep(_10._m6, _10._m7, distance((_8._m9 * float4(in.m_3.xyz, 1.0)).xz, float2(0.0)));
    if (!(_85 != 0.0))
    {
        discard_fragment();
    }
    float _93 = smoothstep(_10._m10, _10._m11, in.m_4.x) * (_85 * _10._m2);
    _61.w = _93;
    float3 _96 = _61.xyz * _93;
    out.m_6 = float4(_96.x, _96.y, _96.z, _61.w);
    return out;
}

