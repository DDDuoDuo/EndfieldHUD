#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _11
{
    float4 _m0;
    float4 _m1;
    float4 _m2;
    float4 _m3;
    float4 _m4;
    float4 _m5;
    float4 _m6;
    float4 _m7;
    float4 _m8;
    float4 _m9;
    float4 _m10;
    float4 _m11;
    float4 _m12;
    float4 _m13;
    float4 _m14;
    float4 _m15;
    float4 _m16;
    float4 _m17;
    float4 _m18;
    float4 _m19;
    float4 _m20;
    float4 _m21;
    float4 _m22;
    float4 _m23;
    float4 _m24;
    float4 _m25;
};

struct main0_out
{
    float4 m_4 [[color(0)]];
};

struct main0_in
{
    float2 m_3 [[user(locn0)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant _11& _12 [[buffer(0)]], texture2d<float> _7 [[texture(0)]], texture2d<float> _8 [[texture(1)]], sampler _9 [[sampler(0)]], sampler _10 [[sampler(1)]])
{
    main0_out out = {};
    float4 _41 = _7.sample(_10, in.m_3, level(0.0));
    float4 _47 = float4(_41.xyz, fast::clamp(_41.w, 0.0, 1.0));
    float4 _51 = _8.sample(_9, in.m_3, level(0.0));
    float _52 = _51.w;
    float3 _58 = _47.xyz;
    float3 _68 = mix(_58, _58 * mix(_12._m4.xyz, float3(1.0), float3(_52 * ((_52 * ((_52 * 0.305306017398834228515625) + 0.6821711063385009765625)) + 0.01252287812530994415283203125))), float3(_12._m4.w));
    out.m_4 = float4(_68.x, _68.y, _68.z, _47.w);
    return out;
}

