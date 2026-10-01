#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _8
{
    float4x4 _m0;
    float4x4 _m1;
    float4x4 _m2;
    float4x4 _m3;
    float4x4 _m4;
    float4x4 _m5;
    float4x4 _m6;
    float4x4 _m7;
    float4x4 _m8;
    float4x4 _m9;
    float4x4 _m10;
    float4 _m11;
    float4 _m12;
    float4 _m13;
    float4 _m14;
    float4 _m15;
};

struct _10
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
    float4 _m11;
};

struct main0_out
{
    float4 m_5 [[color(0)]];
};

struct main0_in
{
    float2 m_3 [[user(locn0)]];
    float4 m_4 [[user(locn1)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant _8& _9 [[buffer(0)]], constant _10& _11 [[buffer(1)]], texture2d<float> _13 [[texture(0)]], sampler _14 [[sampler(0)]])
{
    main0_out out = {};
    float4 _67 = _11._m11 * select(in.m_4, float4(1.0), bool4(_11._m0 != 0.0));
    float3 _71 = _67.xyz * _11._m1;
    float4 _72 = float4(_71.x, _71.y, _71.z, _67.w);
    _72.w = _67.w * _11._m2;
    float4 _113 = _13.sample(_14, ((((float2x2(float2(_11._m10.xy), float2(_11._m10.zw)) * ((mix(in.m_3, in.m_3, float2(_11._m5)) + (_11._m9.xy * fmod(_9._m12.y, 1024.0))) - float2(0.5))) + float2(0.5)) * _11._m8.xy) + _11._m8.zw));
    float4 _118 = _72 * mix(_113, float4(1.0, 1.0, 1.0, _113.x), float4(_11._m4));
    float3 _119 = _118.xyz;
    float3 _130 = fast::clamp((_119 + (precise::max(_119 - float3(_11._m6), float3(0.0)) * _11._m7)).xyz, float3(0.0), float3(500.0));
    float4 _131 = float4(_130.x, _130.y, _130.z, _118.w);
    _131.w = fast::clamp(_118.w, 0.0, 1.0);
    out.m_5 = _131;
    return out;
}

