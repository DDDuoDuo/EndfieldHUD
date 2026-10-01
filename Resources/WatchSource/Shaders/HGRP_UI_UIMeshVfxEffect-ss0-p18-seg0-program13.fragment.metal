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
    float _m12;
    float _m13;
    float _m14;
    float4 _m15;
    float4 _m16;
    float4 _m17;
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

fragment main0_out main0(main0_in in [[stage_in]], constant _8& _9 [[buffer(0)]], constant _10& _11 [[buffer(1)]], texture2d<float> _13 [[texture(0)]], texture2d<float> _15 [[texture(1)]], sampler _14 [[sampler(0)]], sampler _16 [[sampler(1)]])
{
    main0_out out = {};
    float4 _74 = _11._m11 * select(in.m_4, float4(1.0), bool4(_11._m0 != 0.0));
    float3 _78 = _74.xyz * _11._m1;
    float4 _79 = float4(_78.x, _78.y, _78.z, _74.w);
    _79.w = _74.w * _11._m2;
    float _96 = fmod(_9._m12.y, 1024.0);
    float4 _120 = _13.sample(_14, ((((float2x2(float2(_11._m10.xy), float2(_11._m10.zw)) * ((mix(in.m_3, in.m_3, float2(_11._m5)) + (_11._m9.xy * _96)) - float2(0.5))) + float2(0.5)) * _11._m8.xy) + _11._m8.zw));
    float4 _158 = _15.sample(_16, ((((float2x2(float2(_11._m17.xy), float2(_11._m17.zw)) * ((mix(in.m_3, in.m_3, float2(_11._m14)) + (_11._m16.xy * _96)) - float2(0.5))) + float2(0.5)) * _11._m15.xy) + _11._m15.zw));
    float4 _163 = (_79 * mix(_120, float4(1.0, 1.0, 1.0, _120.x), float4(_11._m4))) * mix(_158, float4(1.0, 1.0, 1.0, _158.x), float4(_11._m13));
    float3 _175 = fast::clamp((_163.xyz + (precise::max(_163.xyz - float3(_11._m6), float3(0.0)) * _11._m7)).xyz, float3(0.0), float3(500.0));
    float4 _176 = float4(_175.x, _175.y, _175.z, _163.w);
    _176.w = fast::clamp(_163.w, 0.0, 1.0);
    out.m_5 = _176;
    return out;
}

